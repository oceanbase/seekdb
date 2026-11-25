// Copyright 2016 The Fancy Regex Authors.
//
// Permission is hereby granted, free of charge, to any person obtaining a copy
// of this software and associated documentation files (the "Software"), to deal
// in the Software without restriction, including without limitation the rights
// to use, copy, modify, merge, publish, distribute, sublicense, and/or sell
// copies of the Software, and to permit persons to whom the Software is
// furnished to do so, subject to the following conditions:
//
// The above copyright notice and this permission notice shall be included in
// all copies or substantial portions of the Software.
//
// THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
// IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
// FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE
// AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
// LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM,
// OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN
// THE SOFTWARE.

//! Backtracking VM for implementing fancy regexes.
//!
//! Read <https://swtch.com/~rsc/regexp/regexp2.html> for a good introduction for how this works.
//!
//! The VM executes a sequence of instructions (a program) against an input string. It keeps track
//! of a program counter (PC) and an index into the string (IX). Execution can have one or more
//! threads.
//!
//! One of the basic instructions is `Lit`, which matches a string against the input. If it matches,
//! the PC advances to the next instruction and the IX to the position after the matched string.
//! If not, the current thread is stopped because it failed.
//!
//! If execution reaches an `End` instruction, the program is successful because a match was found.
//! If there are no more threads to execute, the program has failed to match.
//!
//! A very simple program for the regex `a`:
//!
//! ```text
//! 0: Lit("a")
//! 1: End
//! ```
//!
//! The `Split` instruction causes execution to split into two threads. The first thread is executed
//! with the current string index. If it fails, we reset the string index and resume execution with
//! the second thread. That is what "backtracking" refers to. In order to do that, we keep a stack
//! of threads (PC and IX) to try.
//!
//! Example program for the regex `ab|ac`:
//!
//! ```text
//! 0: Split(1, 4)
//! 1: Lit("a")
//! 2: Lit("b")
//! 3: Jmp(6)
//! 4: Lit("a")
//! 5: Lit("c")
//! 6: End
//! ```
//!
//! The `Jmp` instruction causes execution to jump to the specified instruction. In the example it
//! is needed to separate the two threads.
//!
//! Let's step through execution with that program for the input `ac`:
//!
//! 1. We're at PC 0 and IX 0
//! 2. `Split(1, 4)` means we save a thread with PC 4 and IX 0 for trying later
//! 3. Continue at `Lit("a")` which matches, so we advance IX to 1
//! 4. `Lit("b")` doesn't match at IX 1 (`"b" != "c"`), so the thread fails
//! 5. We continue with the previously saved thread at PC 4 and IX 0 (backtracking)
//! 6. Both `Lit("a")` and `Lit("c")` match and we reach `End` -> successful match (index 0 to 2)

use alloc::boxed::Box;
use alloc::string::String;
#[cfg(feature = "variable-lookbehinds")]
use alloc::sync::Arc;
use alloc::vec;
use alloc::vec::Vec;
use core::fmt;
use regex_automata::meta::Regex;
use regex_automata::util::look::LookMatcher;
use regex_automata::util::pool::Pool;
use regex_automata::util::primitives::NonMaxUsize;
use regex_automata::Anchored;
use regex_automata::Input;

#[cfg(feature = "variable-lookbehinds")]
pub(crate) type CachePoolFn = alloc::boxed::Box<
    dyn Fn() -> regex_automata::hybrid::dfa::Cache
        + Send
        + Sync
        + core::panic::UnwindSafe
        + core::panic::RefUnwindSafe,
>;

use crate::error::RuntimeError;
use crate::input::{Input as HaystackInput, RegexInput};
use crate::Assertion;
use crate::BytesMode;
use crate::Error;
use crate::Formatter;
use crate::Result;
use crate::{codepoint_len, HardRegexRuntimeOptions};

/// Enable tracing of VM execution. Only for debugging/investigating.
const OPTION_TRACE: u32 = 1 << 0;
/// When iterating over all matches within a text (e.g. with `find_iter`), empty matches need to be
/// handled specially. If we kept matching at the same position, we'd never stop. So what we do
/// after we've had an empty match, is to advance the position where matching is attempted.
/// If `\G` is used in the pattern, that means it no longer matches. If we didn't tell the VM about
/// the fact that we skipped because of an empty match, it would still treat `\G` as matching. So
/// this option is for communicating that to the VM. Phew.
pub(crate) const OPTION_NOT_CONTINUED_FROM_PREVIOUS_MATCH: u32 = 1 << 1;
/// When this option is set, the VM will reject any match where the engine consumed no characters.
/// \K is ignored as part of this check - so empty matches can still be reported if the engine
/// consumed characters and then \K was used afterwards.
pub(crate) const OPTION_FIND_NOT_EMPTY: u32 = 1 << 2;
#[cfg(feature = "leftmost_longest")]
/// When set, the VM uses leftmost-longest match semantics instead of leftmost-first.
pub(crate) const OPTION_LEFTMOST_LONGEST: u32 = 1 << 3;

// TODO: make configurable
const MAX_STACK: usize = 1_000_000;

/// Represents a range of capture groups by storing the first and last group numbers.
#[derive(Clone, Copy, Debug, PartialEq, Eq)]
pub struct CaptureGroupRange(pub usize, pub usize);

impl CaptureGroupRange {
    /// Returns the start (first) group number.
    pub fn start(&self) -> usize {
        self.0
    }

    /// Returns the end (last) group number.
    pub fn end(&self) -> usize {
        self.1
    }

    /// Converts this range to an Option, returning None if start equals end (no capture groups).
    pub fn to_option_if_non_empty(self) -> Option<Self> {
        if self.start() == self.end() {
            None
        } else {
            Some(self)
        }
    }
}

/// Matches a single character class (e.g. `\d`, `[a-z]`, `\w`) without building
/// a regex-automata engine.
///
/// Most "fancy" patterns interleave plain character classes with the features
/// that force backtracking (look-around, back-references, ...). Each such class
/// would otherwise be compiled to a full delegated `meta::Regex` whose
/// construction (NFA + reverse NFA + lazy DFA) dominates both compile time and
/// memory — and which is then searched, anchored, once per character inside the
/// backtracking loop. Because a class matches exactly one codepoint/byte, we can
/// instead test membership directly here, which is cheaper to build *and* faster
/// to match than calling into the engine.
///
/// The ranges are taken verbatim from the `regex-syntax` `Hir` for the class
/// (sorted, non-overlapping, and already case-folded / Unicode-expanded), so the
/// matched set is identical to what the delegated engine would accept.
#[derive(Clone, PartialEq, Eq)]
pub enum CharClassMatcher {
    /// Match one Unicode scalar value against inclusive `char` ranges. Used in
    /// Unicode bytes mode, where the haystack is valid UTF-8.
    Codepoint {
        ranges: Box<[(char, char)]>,
        name: Option<String>,
    },
    /// Match one byte against inclusive byte ranges. Used in ASCII bytes mode
    /// (and for ASCII-only `(?-u:...)` classes).
    Byte {
        ranges: Box<[(u8, u8)]>,
        name: Option<String>,
    },
}

impl fmt::Debug for CharClassMatcher {
    fn fmt(&self, f: &mut fmt::Formatter<'_>) -> fmt::Result {
        if let Some(ref name) = match self {
            CharClassMatcher::Codepoint { name, .. } => name,
            CharClassMatcher::Byte { name, .. } => name,
        } {
            match self {
                CharClassMatcher::Codepoint { .. } => write!(f, "Codepoint {}", name),
                CharClassMatcher::Byte { .. } => write!(f, "Byte {}", name),
            }
        } else {
            match self {
                CharClassMatcher::Codepoint { ranges, .. } => {
                    f.debug_tuple("Codepoint").field(ranges).finish()
                }
                CharClassMatcher::Byte { ranges, .. } => {
                    f.debug_tuple("Byte").field(ranges).finish()
                }
            }
        }
    }
}

impl CharClassMatcher {
    /// If the character at `ix` is in the class, returns the number of bytes it
    /// occupies (so the caller can advance). Returns `None` on no match or at the
    /// end of input.
    #[inline]
    fn match_len<S: HaystackInput + ?Sized>(&self, s: &S, ix: usize) -> Option<usize> {
        let bytes = s.as_bytes();
        if ix >= bytes.len() {
            return None;
        }
        match self {
            CharClassMatcher::Byte { ranges, .. } => {
                if range_contains(ranges, bytes[ix]) {
                    Some(1)
                } else {
                    None
                }
            }
            CharClassMatcher::Codepoint { ranges, .. } => {
                let len = codepoint_len(bytes[ix]);
                let end = ix + len;
                if end > bytes.len() {
                    return None;
                }
                // The haystack is valid UTF-8 in Unicode mode, so this decodes the
                // codepoint; the `?` is a safety net for an unexpected boundary.
                let c = core::str::from_utf8(&bytes[ix..end]).ok()?.chars().next()?;
                if range_contains(ranges, c) {
                    Some(len)
                } else {
                    None
                }
            }
        }
    }
}

/// A case-insensitive literal, matched natively by the VM instead of being
/// delegated to a `meta::Regex` engine.
///
/// A case-insensitive alternation branch such as `(?i)abort` is a run of
/// literal characters; delegating each branch to its own engine makes build
/// cost scale with the number of branches. Like [`CharClassMatcher`],
/// membership can be tested directly: one sorted range set per original
/// character, holding its Unicode simple case-fold class (a singleton for
/// case-sensitive characters). The fold classes come from `regex-syntax`, so
/// the matched set is identical to what the delegated `(?i)` engine would
/// accept — including width-changing variants such as `k` ↔ U+212A KELVIN
/// SIGN, which is why matching advances codepoint by codepoint instead of
/// assuming the literal's own byte length.
///
/// Only built in Unicode bytes mode (the haystack is valid UTF-8).
#[derive(Clone, Debug, PartialEq, Eq)]
pub struct CaseiLiteral {
    chars: Box<[FoldRanges]>,
}

/// The characters matching one literal character, as sorted inclusive ranges
/// (its simple case-fold class, or a singleton when case-sensitive).
pub(crate) type FoldRanges = Box<[(char, char)]>;

impl CaseiLiteral {
    pub(crate) fn new(chars: Box<[FoldRanges]>) -> Self {
        CaseiLiteral { chars }
    }

    /// If the literal matches at `ix`, returns the matched byte length (which
    /// can differ from the literal's own length under folding). `None` on no
    /// match or end of input.
    fn match_len<S: HaystackInput + ?Sized>(&self, s: &S, ix: usize) -> Option<usize> {
        let bytes = s.as_bytes();
        let mut pos = ix;
        for ranges in self.chars.iter() {
            if pos >= bytes.len() {
                return None;
            }
            let len = codepoint_len(bytes[pos]);
            let end = pos + len;
            if end > bytes.len() {
                return None;
            }
            // The haystack is valid UTF-8 in Unicode mode, so this decodes the
            // codepoint; the `?` is a safety net for an unexpected boundary.
            let c = core::str::from_utf8(&bytes[pos..end])
                .ok()?
                .chars()
                .next()?;
            if !range_contains(ranges.as_ref(), c) {
                return None;
            }
            pos = end;
        }
        Some(pos - ix)
    }
}

/// Binary-searches `ranges` (sorted, non-overlapping, inclusive) for `needle`.
#[inline]
fn range_contains<T: Ord + Copy>(ranges: &[(T, T)], needle: T) -> bool {
    ranges
        .binary_search_by(|&(lo, hi)| {
            if needle < lo {
                core::cmp::Ordering::Greater
            } else if needle > hi {
                core::cmp::Ordering::Less
            } else {
                core::cmp::Ordering::Equal
            }
        })
        .is_ok()
}

#[derive(Clone)]
/// Delegate matching to the regex crate
pub struct Delegate {
    /// The regex
    pub inner: Regex,
    /// The regex pattern as a string
    pub pattern: String,
    /// The range of capture groups. None if there are no capture groups.
    pub capture_groups: Option<CaptureGroupRange>,
}

impl core::fmt::Debug for Delegate {
    fn fmt(&self, f: &mut Formatter<'_>) -> core::fmt::Result {
        // Ensures it fails to compile if the struct changes
        let Self {
            inner: _,
            pattern,
            capture_groups,
        } = self;

        f.debug_struct("Delegate")
            .field("pattern", pattern)
            .field("capture_groups", capture_groups)
            .finish()
    }
}

/// Seek pre-filter: find the next plausible match position using a simplified approximation of the
/// pattern, then hand off to the backtracking VM at that position.
pub struct Seek {
    /// The compiled seek pre-filter regex (un-anchored, finds leftmost match).
    pub inner: Regex,
    /// The seek-pattern string (for debug display).
    pub pattern: String,
}

impl core::fmt::Debug for Seek {
    fn fmt(&self, f: &mut Formatter<'_>) -> core::fmt::Result {
        // Ensures it fails to compile if the struct changes
        let Self { inner: _, pattern } = self;

        f.debug_struct("Seek").field("pattern", pattern).finish()
    }
}

#[cfg(feature = "variable-lookbehinds")]
/// Delegate matching in reverse to regex-automata
pub struct ReverseBackwardsDelegate {
    /// The regex pattern as a string which will be matched in reverse, in a backwards direction
    pub pattern: String,
    /// The delegate regex to match backwards (wrapped in Arc for sharing)
    pub(crate) dfa: Arc<regex_automata::hybrid::dfa::DFA>,
    /// Cache pool for DFA searches
    pub(crate) cache_pool: Pool<regex_automata::hybrid::dfa::Cache, CachePoolFn>,
    /// The forward regex for capture group extraction
    pub(crate) capture_group_extraction_inner: Option<Regex>,
    /// The range of capture groups. None if there are no capture groups.
    pub capture_groups: Option<CaptureGroupRange>,
}

#[cfg(feature = "variable-lookbehinds")]
impl core::fmt::Debug for ReverseBackwardsDelegate {
    fn fmt(&self, f: &mut Formatter<'_>) -> core::fmt::Result {
        // Ensures it fails to compile if the struct changes
        let Self {
            pattern,
            dfa: _,
            cache_pool: _,
            capture_group_extraction_inner: _,
            capture_groups,
        } = self;

        f.debug_struct("ReverseBackwardsDelegate")
            .field("pattern", pattern)
            .field("capture_groups", capture_groups)
            .finish()
    }
}

#[cfg(feature = "variable-lookbehinds")]
/// Hard variable-length lookbehind: use a reverse DFA to find candidate start
/// positions, then verify each with the backtracking VM.
pub struct HardVariableLookbehind {
    /// Seek pattern used to build the reverse DFA (for debug display).
    pub pattern: String,
    /// Reverse DFA for finding candidate start positions.
    pub(crate) dfa: Arc<regex_automata::hybrid::dfa::DFA>,
    /// Cache pool for DFA searches.
    pub(crate) cache_pool: Pool<regex_automata::hybrid::dfa::Cache, CachePoolFn>,
    /// Slot containing the lookbehind end position (saved by the outer Save).
    pub end_pos_slot: usize,
    /// Slot where the candidate start position is saved before running inner instructions.
    pub candidate_pos_slot: usize,
}

#[cfg(feature = "variable-lookbehinds")]
impl core::fmt::Debug for HardVariableLookbehind {
    fn fmt(&self, f: &mut Formatter<'_>) -> core::fmt::Result {
        let Self {
            pattern,
            dfa: _,
            cache_pool: _,
            end_pos_slot,
            candidate_pos_slot,
        } = self;

        f.debug_struct("HardVariableLookbehind")
            .field("pattern", pattern)
            .field("end_pos_slot", end_pos_slot)
            .field("candidate_pos_slot", candidate_pos_slot)
            .finish()
    }
}

/// Instruction of the VM.
#[derive(Debug)]
pub enum Insn {
    /// Successful end of program
    End,
    /// Match any character (including newline)
    Any,
    /// Match any character except for the line feed character (`\n`)
    AnyNoNL,
    /// Match any character except for a carriage return or line feed character (`\r` or `\n`)
    AnyNoCRLF,
    /// Assertions
    Assertion(Assertion),
    /// Match the literal string at the current index
    Lit(String),
    /// Match a literal byte sequence at the current index (used in bytes modes)
    LitBytes(Vec<u8>),
    /// Match a case-insensitive literal at the current index, without
    /// delegating to a regex-automata engine.
    LitCasei(CaseiLiteral),
    /// Match a single character class (e.g. `\d`, `[a-z]`) at the current index,
    /// without delegating to a regex-automata engine.
    CharClass(CharClassMatcher),
    /// Split execution into two threads. The two fields are positions of instructions. Execution
    /// first tries the first thread. If that fails, the second position is tried.
    Split(usize, usize),
    /// Like `Split`, but also updates `match_attempt_start` for `OPTION_FIND_NOT_EMPTY` tracking.
    /// Used exclusively for the unanchored search preamble.
    SplitUnanchored(usize, usize),
    /// Jump to instruction at position
    Jmp(usize),
    /// Save the current string index into the specified slot
    Save(usize),
    /// Save `0` into the specified slot
    Save0(usize),
    /// Save the current string index into the specified capture group start slot if the capture group is empty
    /// or has already completed.
    SaveCaptureGroupStart(usize),
    /// Set the string index to the value that was saved in the specified slot
    Restore(usize),
    /// Repeat greedily (match as much as possible)
    RepeatGr {
        /// Minimum number of matches
        lo: usize,
        /// Maximum number of matches
        hi: usize,
        /// The instruction after the repeat
        next: usize,
        /// The slot for keeping track of the number of repetitions
        repeat: usize,
    },
    /// Repeat non-greedily (prefer matching as little as possible)
    RepeatNg {
        /// Minimum number of matches
        lo: usize,
        /// Maximum number of matches
        hi: usize,
        /// The instruction after the repeat
        next: usize,
        /// The slot for keeping track of the number of repetitions
        repeat: usize,
    },
    /// Repeat greedily and prevent infinite loops from empty matches
    RepeatEpsilonGr {
        /// Minimum number of matches
        lo: usize,
        /// The instruction after the repeat
        next: usize,
        /// The slot for keeping track of the number of repetitions
        repeat: usize,
        /// The slot for saving the previous IX to check if we had an empty match
        check: usize,
    },
    /// Repeat non-greedily and prevent infinite loops from empty matches
    RepeatEpsilonNg {
        /// Minimum number of matches
        lo: usize,
        /// The instruction after the repeat
        next: usize,
        /// The slot for keeping track of the number of repetitions
        repeat: usize,
        /// The slot for saving the previous IX to check if we had an empty match
        check: usize,
    },
    /// Negative look-around failed
    FailNegativeLookAround,
    /// Set IX back by the specified number of characters
    GoBack(usize),
    /// Verify the inner body of a hard variable lookbehind matched the
    /// expected span: if IX equals the saved end position, continue to the
    /// next instruction. Otherwise truncate any inner backtrack states and
    /// jump back to the `HardVariableLookbehind` instruction to try the next
    /// candidate.
    ReverseLookbehindPosCheck {
        /// Slot containing the expected end position.
        slot: usize,
        /// Slot containing the candidate start position saved by `HardVariableLookbehind`.
        candidate_pos_slot: usize,
        /// PC of the enclosing `HardVariableLookbehind` instruction.
        hard_lb_pc: usize,
    },
    /// Back reference to a group number to check
    Backref {
        /// The save slot representing the start of the capture group
        slot: usize,
        /// Whether the backref should be matched case insensitively
        casei: bool,
        /// Whether Unicode mode is enabled (affects case folding behavior)
        unicode: bool,
    },
    /// Begin of atomic group
    BeginAtomic,
    /// End of atomic group
    EndAtomic,
    /// Delegate matching to the regex crate
    Delegate(Delegate),
    /// Anchor to match at the position where the previous match ended
    ContinueFromPreviousMatchEnd {
        /// Whether this is at the start of the pattern (allowing early exit on failure)
        at_start: bool,
    },
    /// Continue only if the specified capture group has already been populated as part of the match
    BackrefExistsCondition(usize),
    /// Immediately fail the current match attempt and trigger backtracking.
    /// This is used for backtracking control verbs like `(*FAIL)`.
    Fail,
    #[cfg(feature = "variable-lookbehinds")]
    /// Reverse lookbehind using regex-automata for variable-sized patterns
    BackwardsDelegate(ReverseBackwardsDelegate),
    #[cfg(feature = "variable-lookbehinds")]
    /// Hard variable-length lookbehind: reverse DFA finds candidate start positions,
    /// then the VM verifies each candidate.
    HardVariableLookbehind(HardVariableLookbehind),
    /// Absent repeater operator - matches if delegate does not match from current position
    AbsentRepeater(Delegate),
    /// Seek pre-filter: advance `ix` to the next position where the pattern could plausibly match,
    /// using an over-approximating regular expression.  Replaces the `SplitUnanchored` / `Any` /
    /// `Jmp` preamble for hard patterns when a useful seek approximation is available.
    ///
    /// Execution:
    /// 1. Search `s[ix..]` un-anchored for the seek regex.
    /// 2. If no match exists, return `None` immediately (the full pattern can never match).
    /// 3. Otherwise push a backtrack branch `(pc, next_seek_start)` where `next_seek_start` is
    ///    one codepoint past the match start (or end for zero-width matches), set
    ///    `match_attempt_start = m.start()`, and continue from `pc+1` with `ix = m.start()`.
    Seek(Seek),
    /// For compatibility with Oniguruma, which ignores empty matches at the end of the input
    /// if the previous character was a line break. So ^/$ and (?=) all fail to match, unless
    /// \z was used and matched at this position.
    RejectEmptyMatchAtEOFFollowingNewline,
}

/// Reusable per-run working memory: the backtracking [`State`] plus the slot
/// buffer used when calling delegated engines. Pooled on the [`Prog`] so that
/// repeated runs (every `find_iter` step, every `RegexSet` candidate
/// verification) don't re-allocate it; `run` resets it before use.
#[derive(Debug)]
struct Scratch {
    state: State,
    inner_slots: Vec<Option<NonMaxUsize>>,
}

fn new_scratch() -> Scratch {
    Scratch {
        state: State::new(0, MAX_STACK, 0),
        inner_slots: Vec::new(),
    }
}

/// Sequence of instructions for the VM to execute.
#[derive(Debug)]
pub struct Prog {
    /// Instructions of the program
    pub body: Vec<Insn>,
    n_saves: usize,
    /// How the VM advances positions: byte-level (Ascii) vs codepoint-level (Unicode/UnicodeBytes).
    bytes_mode: BytesMode,
    /// Whether Unicode word rules are in effect for word-boundary assertions.
    /// Must match the `unicode` flag used by the delegated sub-engines.
    unicode: bool,
    /// A pattern compatible with a DFA which can be used to seek to candidate positions where the real/full pattern might match
    pub(crate) seek_pattern: String,
    scratch_pool: Pool<Scratch, fn() -> Scratch>,
    /// Maximum number of bytes this pattern can match. When `leftmost_longest` is enabled and a match
    /// of this length is found, no longer match is possible, so backtracking can stop early.
    pub max_size: usize,
}

impl Prog {
    pub(crate) fn new(
        body: Vec<Insn>,
        n_saves: usize,
        bytes_mode: BytesMode,
        unicode: bool,
        seek_pattern: String,
        max_size: usize,
    ) -> Prog {
        Prog {
            body,
            n_saves,
            bytes_mode,
            unicode,
            seek_pattern,
            scratch_pool: Pool::new(new_scratch),
            max_size,
        }
    }

    #[doc(hidden)]
    pub(crate) fn debug_print(&self, writer: &mut Formatter<'_>) -> core::fmt::Result {
        for (i, insn) in self.body.iter().enumerate() {
            writeln!(writer, "{:3}: {:?}", i, insn)?;
        }
        Ok(())
    }
}

#[derive(Debug)]
struct Branch {
    pc: usize,
    ix: usize,
    nsave: usize,
}

#[derive(Debug)]
struct Save {
    slot: usize,
    value: usize,
}

#[derive(Debug)]
struct State {
    /// Saved values indexed by slot. Mostly indices to s, but can be repeat values etc.
    /// Always contains the saves of the current state.
    saves: Vec<usize>,
    /// Stack of backtrack branches.
    stack: Vec<Branch>,
    /// Old saves (slot, value)
    oldsave: Vec<Save>,
    /// Number of saves at the end of `oldsave` that need to be restored to `saves` on pop
    nsave: usize,
    explicit_sp: usize,
    /// Maximum size of the stack. If the size would be exceeded during execution, a `StackOverflow`
    /// error is raised.
    max_stack: usize,
    #[allow(dead_code)]
    options: u32,
    /// Reusable buffer for `backtrack_cut`'s slot dedup, kept to avoid
    /// allocating on every atomic-group exit.
    cut_scratch: Vec<usize>,
    #[cfg(feature = "leftmost_longest")]
    /// Reusable buffer for the leftmost-longest best-saves snapshot. Kept
    /// across runs so that the hot backtrack path in `Insn::End` doesn't
    /// re-allocate a `Vec` every time a longer match is found.
    best_saves: Vec<usize>,
    #[cfg(feature = "leftmost_longest")]
    /// Whether `best_saves` holds a recorded best match. `best_match_len`
    /// alone can't carry this: a legitimate empty match records length 0,
    /// which is indistinguishable from "no match yet".
    best_match_set: bool,
    #[cfg(feature = "variable-lookbehinds")]
    /// Candidate start positions collected by `HardVariableLookbehind` for the
    /// current lookbehind assertion. Empty when not inside a hard lookbehind.
    hard_lb_candidates: Vec<usize>,
    #[cfg(feature = "variable-lookbehinds")]
    /// Index of the next candidate to try from `hard_lb_candidates`.
    hard_lb_candidate_idx: usize,
}

// Each element in the stack conceptually represents the entire state
// of the machine: the pc (index into prog), the index into the
// string, and the entire vector of saves. However, copying the save
// vector on every push/pop would be inefficient, so instead we use a
// copy-on-write approach for each slot within the save vector. The
// top `nsave` elements in `oldsave` represent the delta from the
// current machine state to the top of stack.

impl State {
    fn new(n_saves: usize, max_stack: usize, options: u32) -> State {
        State {
            saves: vec![usize::MAX; n_saves],
            stack: Vec::new(),
            oldsave: Vec::new(),
            nsave: 0,
            explicit_sp: n_saves,
            max_stack,
            options,
            cut_scratch: Vec::new(),
            #[cfg(feature = "leftmost_longest")]
            best_saves: Vec::new(),
            #[cfg(feature = "leftmost_longest")]
            best_match_set: false,
            #[cfg(feature = "variable-lookbehinds")]
            hard_lb_candidates: Vec::new(),
            #[cfg(feature = "variable-lookbehinds")]
            hard_lb_candidate_idx: 0,
        }
    }

    /// Reset for a fresh run, keeping the allocated buffers.
    fn reset(&mut self, n_saves: usize, options: u32) {
        self.saves.clear();
        self.saves.resize(n_saves, usize::MAX);
        self.stack.clear();
        self.oldsave.clear();
        self.nsave = 0;
        self.explicit_sp = n_saves;
        self.options = options;
        #[cfg(feature = "leftmost_longest")]
        {
            // Mirror `saves` length so the reusable best-saves snapshot
            // buffer never needs to grow on the hot backtrack path.
            self.best_saves.clear();
            self.best_saves.resize(n_saves, usize::MAX);
            self.best_match_set = false;
        }
        #[cfg(feature = "variable-lookbehinds")]
        {
            self.hard_lb_candidates.clear();
            self.hard_lb_candidate_idx = 0;
        }
    }

    // push a backtrack branch
    fn push(&mut self, pc: usize, ix: usize) -> Result<()> {
        if self.stack.len() < self.max_stack {
            let nsave = self.nsave;
            self.stack.push(Branch { pc, ix, nsave });
            self.nsave = 0;
            self.trace_stack("push");
            Ok(())
        } else {
            Err(Error::RuntimeError(RuntimeError::StackOverflow))
        }
    }

    // pop a backtrack branch
    fn pop(&mut self) -> (usize, usize) {
        for _ in 0..self.nsave {
            let Save { slot, value } = self.oldsave.pop().unwrap();
            self.saves[slot] = value;
        }
        let Branch { pc, ix, nsave } = self.stack.pop().unwrap();
        self.nsave = nsave;
        self.trace_stack("pop");
        (pc, ix)
    }

    fn save(&mut self, slot: usize, val: usize) {
        for i in 0..self.nsave {
            // could avoid this iteration with some overhead; worth it?
            if self.oldsave[self.oldsave.len() - i - 1].slot == slot {
                // already saved, just update
                self.saves[slot] = val;
                return;
            }
        }
        self.oldsave.push(Save {
            slot,
            value: self.saves[slot],
        });
        self.nsave += 1;
        self.saves[slot] = val;

        #[cfg(feature = "std")]
        if self.options & OPTION_TRACE != 0 {
            println!("saves: {:?}", self.saves);
        }
    }

    fn get(&self, slot: usize) -> usize {
        self.saves[slot]
    }

    // push a value onto the explicit stack; note: the entire contents of
    // the explicit stack is saved and restored on backtrack.
    fn stack_push(&mut self, val: usize) {
        if self.saves.len() == self.explicit_sp {
            self.saves.push(self.explicit_sp + 1);
        }
        let explicit_sp = self.explicit_sp;
        let sp = self.get(explicit_sp);
        if self.saves.len() == sp {
            self.saves.push(val);
        } else {
            self.save(sp, val);
        }
        self.save(explicit_sp, sp + 1);
    }

    // pop a value from the explicit stack
    fn stack_pop(&mut self) -> usize {
        let explicit_sp = self.explicit_sp;
        let sp = self.get(explicit_sp) - 1;
        let result = self.get(sp);
        self.save(explicit_sp, sp);
        result
    }

    /// Get the current number of backtrack branches
    fn backtrack_count(&self) -> usize {
        self.stack.len()
    }

    /// Discard backtrack branches that were pushed since the call to `backtrack_count`.
    ///
    /// What we want:
    /// * Keep the current `saves` as they are
    /// * Only keep `count` backtrack branches on `stack`, discard the rest
    /// * Keep the first `oldsave` for each slot, discard the rest (multiple pushes might have
    ///   happened with saves to the same slot)
    fn backtrack_cut(&mut self, count: usize) {
        if self.stack.len() == count {
            // no backtrack branches to discard, all good
            return;
        }
        // start and end indexes of old saves for the branch we're cutting to
        let (oldsave_start, oldsave_end) = {
            let mut end = self.oldsave.len() - self.nsave;
            for &Branch { nsave, .. } in &self.stack[count + 1..] {
                end -= nsave;
            }
            let start = end - self.stack[count].nsave;
            (start, end)
        };
        // The seen-slot set is a plain Vec with linear lookup: per-branch save
        // counts are small, and this avoids allocating on every atomic-group
        // exit (the buffer is reused across calls).
        let mut saved = core::mem::take(&mut self.cut_scratch);
        saved.clear();
        // keep all the old saves of our branch (they're all for different slots)
        for &Save { slot, .. } in &self.oldsave[oldsave_start..oldsave_end] {
            saved.push(slot);
        }
        let mut oldsave_ix = oldsave_end;
        // for other old saves, keep them only if they're for a slot that we haven't saved yet
        for ix in oldsave_end..self.oldsave.len() {
            let Save { slot, .. } = self.oldsave[ix];
            let new_slot = !saved.contains(&slot);
            if new_slot {
                saved.push(slot);
                // put the save we want to keep (ix) after the ones we already have (oldsave_ix)
                // note that it's fine if the indexes are the same (then swapping is a no-op)
                self.oldsave.swap(oldsave_ix, ix);
                oldsave_ix += 1;
            }
        }
        self.stack.truncate(count);
        self.oldsave.truncate(oldsave_ix);
        self.nsave = oldsave_ix - oldsave_start;
        self.cut_scratch = saved;
    }

    #[inline]
    #[allow(unused_variables)]
    fn trace_stack(&self, operation: &str) {
        #[cfg(feature = "std")]
        if self.options & OPTION_TRACE != 0 {
            println!("stack after {}: {:?}", operation, self.stack);
        }
    }
}

fn codepoint_len_at<S: HaystackInput + ?Sized>(s: &S, ix: usize) -> usize {
    codepoint_len(s.as_bytes()[ix])
}

/// Returns the number of bytes to advance forward from `ix`, respecting the bytes mode.
/// In `Ascii` mode, always advances 1 byte. In `Unicode`/`UnicodeBytes` mode, advances
/// by the full codepoint length at `ix`.
#[inline]
fn advance_one<S: HaystackInput + ?Sized>(s: &S, ix: usize, bytes_mode: BytesMode) -> usize {
    match bytes_mode {
        BytesMode::Ascii => 1,
        _ => codepoint_len_at(s, ix),
    }
}

/// Returns the previous position before `ix`, respecting the bytes mode.
/// In `Ascii` mode, simply returns `ix - 1`. In `Unicode`/`UnicodeBytes` mode,
/// skips backward over UTF-8 continuation bytes.
#[inline]
fn prev_ix<S: HaystackInput + ?Sized>(s: &S, ix: usize, bytes_mode: BytesMode) -> usize {
    match bytes_mode {
        BytesMode::Ascii => ix - 1,
        _ => s.prev_codepoint_ix(ix),
    }
}

#[inline]
fn matches_literal<S: HaystackInput + ?Sized>(
    s: &S,
    ix: usize,
    end: usize,
    literal: &[u8],
) -> bool {
    // Compare as bytes because the literal might be a single byte char whereas ix
    // points to a multibyte char. Comparing with str would result in an error like
    // "byte index N is not a char boundary".
    end <= s.len() && &s.as_bytes()[ix..end] == literal
}

fn matches_literal_casei_unicode(text: &str, literal: &str) -> bool {
    let mut text_chars = text.chars();
    let mut literal_chars = literal.chars();
    loop {
        match (text_chars.next(), literal_chars.next()) {
            (None, None) => return true,
            (Some(t), Some(l)) => {
                if t == l {
                    continue;
                }
                if t.is_ascii() && l.is_ascii() {
                    if t.eq_ignore_ascii_case(&l) {
                        continue;
                    }
                    return false;
                }
                if !chars_fold_equal(t, l) {
                    return false;
                }
            }
            // One string ended before the other: not equal under folding.
            _ => return false,
        }
    }
}

/// Whether two codepoints are equal under Unicode simple case folding — the
/// same equivalence a case-insensitive engine literal uses.
fn chars_fold_equal(a: char, b: char) -> bool {
    use regex_syntax::hir::{ClassUnicode, ClassUnicodeRange};
    let mut class = ClassUnicode::new([ClassUnicodeRange::new(a, a)]);
    match class.try_case_fold_simple() {
        Ok(()) => class
            .ranges()
            .iter()
            .any(|r| r.start() <= b && b <= r.end()),
        // Case-folding tables unavailable (regex-syntax built without
        // unicode-case): exact comparison already failed, so no match.
        Err(_) => false,
    }
}

fn matches_literal_casei<S: HaystackInput + ?Sized>(
    s: &S,
    ix: usize,
    end: usize,
    literal: &[u8],
    unicode: bool,
) -> bool {
    if end > s.len() {
        return false;
    }
    if matches_literal(s, ix, end, literal) {
        return true;
    }
    if !s.is_char_boundary(ix) || !s.is_char_boundary(end) {
        return false;
    }
    let text_bytes = &s.as_bytes()[ix..end];
    if text_bytes.is_ascii() && literal.is_ascii() {
        return text_bytes.eq_ignore_ascii_case(literal);
    }
    if !unicode {
        // ASCII-only case folding: if content is not ASCII, no match
        return false;
    }
    // text captured and being backreferenced is not ascii, so we utilize regex-automata's case insensitive matching
    if let (Ok(text_str), Ok(lit_str)) = (
        core::str::from_utf8(text_bytes),
        core::str::from_utf8(literal),
    ) {
        return matches_literal_casei_unicode(text_str, lit_str);
    }
    false
}

/// Helper function to store capture group positions from inner_slots into state.
/// This is used by both Delegate and BackwardsDelegate instructions.
#[inline]
fn store_capture_groups(
    state: &mut State,
    inner_slots: &[Option<NonMaxUsize>],
    range: CaptureGroupRange,
    skip_earlier_captures: bool,
) {
    let start_group = range.start();
    let end_group = range.end();
    for i in 0..(end_group - start_group) {
        let slot = (start_group + i) * 2;
        if let Some(start) = inner_slots[(i + 1) * 2] {
            let end = inner_slots[(i + 1) * 2 + 1].unwrap();

            let mut save = !skip_earlier_captures;
            if skip_earlier_captures {
                let existing_start = state.get(slot);
                let existing_end = state.get(slot + 1);
                save = (start.get() >= existing_start || existing_start == usize::MAX)
                    && (end.get() >= existing_end || existing_end == usize::MAX);
            }
            if save {
                state.save(slot, start.get());
                state.save(slot + 1, end.get());
            }
        }
    }
}

#[cfg(feature = "leftmost_longest")]
#[inline]
fn apply_best_saves(
    saves: &mut Vec<usize>,
    best_saves: &mut Vec<usize>,
    best_match_set: bool,
) -> bool {
    if best_match_set && !best_saves.is_empty() {
        // Restore `saves` to the best snapshot. `saves` may have grown past
        // the snapshot length via atomic-group explicit-stack pushes during
        // subsequent match attempts (or shrunk back); `resize` handles both
        // directions without reallocating when capacity already suffices. This
        // is on the hot backtrack path, so the scalar resize avoids an
        // allocation.
        saves.resize(best_saves.len(), usize::MAX);
        saves.copy_from_slice(best_saves);
        true
    } else {
        false
    }
}

/// Run the program with trace printing for debugging.
pub fn run_trace(prog: &Prog, s: &str, pos: usize) -> Result<Option<Vec<usize>>> {
    run(
        prog,
        &RegexInput::new(s).from_pos(pos),
        OPTION_TRACE,
        &HardRegexRuntimeOptions::default(),
    )
}

/// Run the program with default options.
pub fn run_default(prog: &Prog, s: &str, pos: usize) -> Result<Option<Vec<usize>>> {
    run(
        prog,
        &RegexInput::new(s).from_pos(pos),
        0,
        &HardRegexRuntimeOptions::default(),
    )
}

/// Run the program with options, returning the full saves vector on a match.
pub(crate) fn run<S: HaystackInput + ?Sized>(
    prog: &Prog,
    input: &RegexInput<'_, S>,
    option_flags: u32,
    options: &HardRegexRuntimeOptions,
) -> Result<Option<Vec<usize>>> {
    // The clone is the one allocation the caller keeps (it owns the captures).
    run_with(prog, input, option_flags, options, |state| {
        state.saves.clone()
    })
}

/// Run the program with options, returning only the overall match span.
///
/// Unlike [`run`], nothing is moved out of the pooled scratch, so `is_match`,
/// `find`, `find_iter`, and RegexSet verification are allocation-free per call
/// (after buffers have grown to steady state).
pub(crate) fn run_spans<S: HaystackInput + ?Sized>(
    prog: &Prog,
    input: &RegexInput<'_, S>,
    option_flags: u32,
    options: &HardRegexRuntimeOptions,
) -> Result<Option<(usize, usize)>> {
    run_with(prog, input, option_flags, options, |state| {
        (state.get(0), state.get(1))
    })
}

/// Run the program with options; `extract` pulls the result out of the final
/// state before the scratch returns to the pool.
#[allow(clippy::cognitive_complexity)]
fn run_with<S: HaystackInput + ?Sized, T>(
    prog: &Prog,
    input: &RegexInput<'_, S>,
    option_flags: u32,
    options: &HardRegexRuntimeOptions,
    extract: impl FnOnce(&State) -> T,
) -> Result<Option<T>> {
    if input.is_done() {
        return Ok(None);
    }
    let haystack = input.haystack();
    let pos = input.effective_start();
    let match_range = input.get_range();
    let mut scratch_guard = prog.scratch_pool.get();
    let Scratch { state, inner_slots } = &mut *scratch_guard;
    state.reset(prog.n_saves, option_flags);
    inner_slots.clear();
    let look_matcher = LookMatcher::new();
    #[cfg(feature = "std")]
    if option_flags & OPTION_TRACE != 0 {
        println!("pos\tinstruction");
    }
    let mut backtrack_count = 0;
    let mut pc = 0;
    let mut ix = pos;
    let mut slash_z_matched = false;
    let mut match_attempt_start = pos;
    #[cfg(feature = "leftmost_longest")]
    let leftmost_longest = option_flags & OPTION_LEFTMOST_LONGEST != 0;
    #[cfg(feature = "leftmost_longest")]
    let mut best_match_len = 0;
    loop {
        // break from this loop to fail, causes stack to pop
        'fail: loop {
            #[cfg(feature = "std")]
            if option_flags & OPTION_TRACE != 0 {
                println!("{}\t{} {:?}", ix, pc, prog.body[pc]);
            }
            match prog.body[pc] {
                Insn::End => {
                    // save of end position into slot 1 is now done
                    // with an explicit group; we might want to
                    // optimize that.
                    //state.saves[1] = ix;
                    #[cfg(feature = "std")]
                    if option_flags & OPTION_TRACE != 0 {
                        println!("saves: {:?}", state.saves);
                    }
                    // Reject the match if it is empty and the flag to do so is enabled.
                    // `match_attempt_start` is set by `SplitUnanchored` each time the unanchored
                    // preamble begins a new match attempt at a fresh position, so this correctly
                    // rejects empty matches regardless of where in the haystack the attempt starts.
                    if option_flags & OPTION_FIND_NOT_EMPTY != 0 && ix == match_attempt_start {
                        break 'fail;
                    }
                    if let Some(&slot1) = state.saves.get(1) {
                        // With some features like keep out (\K), the match start can be after
                        // the match end. Cap the start to <= end.
                        if state.get(0) > slot1 {
                            state.save(0, slot1);
                        }
                    }
                    if state.get(0) < match_range.start || state.get(1) > match_range.end {
                        break 'fail;
                    }
                    #[cfg(feature = "leftmost_longest")]
                    if leftmost_longest {
                        let match_len = state.get(1) - state.get(0);
                        if !state.best_match_set || match_len > best_match_len {
                            // Reuse the pooled `best_saves` buffer instead of
                            // re-allocating on every longer match. This is on
                            // the hot backtrack path, so the scalar copy avoids
                            // both an allocation and a `Vec::clone`'s capacity
                            // bookkeeping. `saves` may have grown past the
                            // snapshot length via atomic-group explicit-stack
                            // pushes, so resize the buffer to match on demand
                            // (rare; only when an atomic group is in scope).
                            let best = &mut state.best_saves;
                            best.resize(state.saves.len(), usize::MAX);
                            for (d, s) in best.iter_mut().zip(state.saves.iter()) {
                                *d = *s;
                            }
                            best_match_len = match_len;
                            state.best_match_set = true;
                        }
                        if best_match_len == prog.max_size {
                            return Ok(Some(extract(state)));
                        }
                        break 'fail;
                    }
                    return Ok(Some(extract(state)));
                }
                Insn::Any => {
                    if ix < haystack.len() {
                        ix += advance_one(haystack, ix, prog.bytes_mode);
                    } else {
                        break 'fail;
                    }
                }
                Insn::AnyNoNL => {
                    if ix < haystack.len() && haystack.as_bytes()[ix] != b'\n' {
                        ix += advance_one(haystack, ix, prog.bytes_mode);
                    } else {
                        break 'fail;
                    }
                }
                Insn::AnyNoCRLF => {
                    if ix < haystack.len()
                        && haystack.as_bytes()[ix] != b'\r'
                        && haystack.as_bytes()[ix] != b'\n'
                    {
                        ix += advance_one(haystack, ix, prog.bytes_mode);
                    } else {
                        break 'fail;
                    }
                }
                Insn::Lit(ref val) => {
                    let ix_end = ix + val.len();
                    if !matches_literal(haystack, ix, ix_end, val.as_bytes()) {
                        break 'fail;
                    }
                    ix = ix_end
                }
                Insn::LitBytes(ref bytes) => {
                    let ix_end = ix + bytes.len();
                    if !matches_literal(haystack, ix, ix_end, bytes) {
                        break 'fail;
                    }
                    ix = ix_end
                }
                Insn::LitCasei(ref lit) => match lit.match_len(haystack, ix) {
                    Some(len) => ix += len,
                    None => break 'fail,
                },
                Insn::CharClass(ref matcher) => match matcher.match_len(haystack, ix) {
                    Some(len) => ix += len,
                    None => break 'fail,
                },
                Insn::Assertion(assertion) => {
                    if !match assertion {
                        Assertion::StartText => input
                            .start_text_override()
                            .and_then(|value| {
                                options.allow_input_assertion_overrides.then_some(value)
                            })
                            .unwrap_or_else(|| look_matcher.is_start(haystack.as_bytes(), ix)),
                        Assertion::EndText => {
                            let matched = input
                                .end_text_override()
                                .and_then(|value| {
                                    options.allow_input_assertion_overrides.then_some(value)
                                })
                                .unwrap_or_else(|| look_matcher.is_end(haystack.as_bytes(), ix));
                            slash_z_matched |= matched;
                            matched
                        }
                        Assertion::EndTextIgnoreTrailingNewlines { crlf } => {
                            let bytes = haystack.as_bytes();
                            let matched = if ix == bytes.len() {
                                // At the end of string
                                true
                            } else if crlf {
                                // In CRLF mode, trailing \r\n pairs and bare \n are ignored
                                bytes[ix..].iter().all(|&b| b == b'\n' || b == b'\r')
                            } else {
                                // Check if all remaining bytes are newlines
                                bytes[ix..].iter().all(|&b| b == b'\n')
                            };
                            slash_z_matched |= matched;
                            matched
                        }
                        Assertion::StartLine { crlf: false } => {
                            look_matcher.is_start_lf(haystack.as_bytes(), ix)
                        }
                        Assertion::StartLine { crlf: true } => {
                            look_matcher.is_start_crlf(haystack.as_bytes(), ix)
                        }
                        Assertion::StartLineOniguruma { crlf: false } => {
                            look_matcher.is_start_lf(haystack.as_bytes(), ix)
                                && !(ix > 0 && ix == haystack.len())
                        }
                        Assertion::StartLineOniguruma { crlf: true } => {
                            look_matcher.is_start_crlf(haystack.as_bytes(), ix)
                                && !(ix > 0 && ix == haystack.len())
                        }
                        Assertion::EndLine { crlf: false } => {
                            look_matcher.is_end_lf(haystack.as_bytes(), ix)
                        }
                        Assertion::EndLine { crlf: true } => {
                            look_matcher.is_end_crlf(haystack.as_bytes(), ix)
                        }
                        Assertion::LeftWordBoundary => {
                            if prog.unicode {
                                look_matcher
                                    .is_word_start_unicode(haystack.as_bytes(), ix)
                                    .unwrap()
                            } else {
                                look_matcher.is_word_start_ascii(haystack.as_bytes(), ix)
                            }
                        }
                        Assertion::RightWordBoundary => {
                            if prog.unicode {
                                look_matcher
                                    .is_word_end_unicode(haystack.as_bytes(), ix)
                                    .unwrap()
                            } else {
                                look_matcher.is_word_end_ascii(haystack.as_bytes(), ix)
                            }
                        }
                        Assertion::LeftWordHalfBoundary => {
                            if prog.unicode {
                                look_matcher
                                    .is_word_start_half_unicode(haystack.as_bytes(), ix)
                                    .unwrap()
                            } else {
                                look_matcher.is_word_start_half_ascii(haystack.as_bytes(), ix)
                            }
                        }
                        Assertion::RightWordHalfBoundary => {
                            if prog.unicode {
                                look_matcher
                                    .is_word_end_half_unicode(haystack.as_bytes(), ix)
                                    .unwrap()
                            } else {
                                look_matcher.is_word_end_half_ascii(haystack.as_bytes(), ix)
                            }
                        }
                        Assertion::WordBoundary => {
                            if prog.unicode {
                                look_matcher
                                    .is_word_unicode(haystack.as_bytes(), ix)
                                    .unwrap()
                            } else {
                                look_matcher.is_word_ascii(haystack.as_bytes(), ix)
                            }
                        }
                        Assertion::NotWordBoundary => {
                            if prog.unicode {
                                look_matcher
                                    .is_word_unicode_negate(haystack.as_bytes(), ix)
                                    .unwrap()
                            } else {
                                look_matcher.is_word_ascii_negate(haystack.as_bytes(), ix)
                            }
                        }
                    } {
                        break 'fail;
                    }
                }
                Insn::Split(x, y) => {
                    state.push(y, ix)?;
                    pc = x;
                    continue;
                }
                Insn::SplitUnanchored(x, y) => {
                    #[cfg(feature = "leftmost_longest")]
                    if leftmost_longest
                        && apply_best_saves(
                            &mut state.saves,
                            &mut state.best_saves,
                            state.best_match_set,
                        )
                    {
                        return Ok(Some(extract(state)));
                    }
                    if ix > match_range.end {
                        return Ok(None);
                    }
                    match_attempt_start = ix;
                    slash_z_matched = false;

                    if input.is_anchored() {
                        // Anchored mode: only try at the current position; do not push a
                        // backtrack branch for advancing to the next position.
                    } else {
                        state.push(y, ix)?;
                    }
                    pc = x;
                    continue;
                }
                Insn::Jmp(target) => {
                    pc = target;
                    continue;
                }
                Insn::Save(slot) => state.save(slot, ix),
                Insn::Save0(slot) => state.save(slot, 0),
                Insn::SaveCaptureGroupStart(group) => {
                    let start_slot = group * 2;
                    // if the capture group's start slot is empty
                    // i.e. execution is not currently inside this capture group
                    // or the end slot for that capture group is complete
                    // then we save the current position in the capture group start slot
                    if state.get(start_slot) == usize::MAX || state.get(start_slot + 1) <= ix {
                        state.save(start_slot, ix);
                    }
                }
                Insn::Restore(slot) => ix = state.get(slot),
                Insn::RepeatGr {
                    lo,
                    hi,
                    next,
                    repeat,
                } => {
                    let repcount = state.get(repeat);
                    if repcount == hi {
                        pc = next;
                        continue;
                    }
                    state.save(repeat, repcount + 1);
                    if repcount >= lo {
                        state.push(next, ix)?;
                    }
                }
                Insn::RepeatNg {
                    lo,
                    hi,
                    next,
                    repeat,
                } => {
                    let repcount = state.get(repeat);
                    if repcount == hi {
                        pc = next;
                        continue;
                    }
                    state.save(repeat, repcount + 1);
                    if repcount >= lo {
                        state.push(pc + 1, ix)?;
                        pc = next;
                        continue;
                    }
                }
                Insn::RepeatEpsilonGr {
                    lo,
                    next,
                    repeat,
                    check,
                } => {
                    let repcount = state.get(repeat);
                    if repcount > 0 && state.get(check) == ix {
                        // zero-length match on repeat, then move to next instruction
                        pc = next;
                        continue;
                    }
                    state.save(repeat, repcount + 1);
                    if repcount >= lo {
                        state.save(check, ix);
                        state.push(next, ix)?;
                    }
                }
                Insn::RepeatEpsilonNg {
                    lo,
                    next,
                    repeat,
                    check,
                } => {
                    let repcount = state.get(repeat);
                    if repcount > 0 && state.get(check) == ix {
                        // zero-length match on repeat, then move to next instruction
                        pc = next;
                        continue;
                    }
                    state.save(repeat, repcount + 1);
                    if repcount >= lo {
                        state.save(check, ix);
                        state.push(pc + 1, ix)?;
                        pc = next;
                        continue;
                    }
                }
                Insn::GoBack(count) => {
                    for _ in 0..count {
                        if ix == 0 {
                            break 'fail;
                        }
                        ix = prev_ix(haystack, ix, prog.bytes_mode);
                    }
                }
                Insn::ReverseLookbehindPosCheck {
                    slot,
                    candidate_pos_slot: _,
                    hard_lb_pc: _,
                } => {
                    if ix == state.get(slot) {
                        // Inner instructions matched exactly up to the lookbehind end —
                        // success.  We deliberately do *not* clear the candidate list
                        // here: the candidate index has already advanced past the current
                        // candidate, so if the surrounding pattern later fails and
                        // backtracking pops our checkpoint, the next candidate will be
                        // tried instead of re-collecting and re-trying the same one —
                        // which would loop forever.
                        pc += 1;
                        continue;
                    } else {
                        // Inner instructions consumed the wrong span (ix !=
                        // end_pos).  Let normal backtracking explore alternatives
                        // within the inner expression.  When all inner states are
                        // exhausted, the checkpoint branch pushed by
                        // HardVariableLookbehind will be popped, redirecting
                        // to the next candidate with nsave properly restored.
                        break 'fail;
                    }
                }
                Insn::FailNegativeLookAround => {
                    // Reaching this instruction means that the body of the
                    // look-around matched. Because it's a *negative* look-around,
                    // that means the look-around itself should fail (not match).
                    // But before, we need to discard all the states that have
                    // been pushed with the look-around, because we don't want to
                    // explore them.
                    loop {
                        let (popped_pc, _) = state.pop();
                        if popped_pc == pc + 1 {
                            // We've reached the state that would jump us to
                            // after the look-around (in case the look-around
                            // succeeded). That means we popped enough states.
                            break;
                        }
                    }
                    break 'fail;
                }
                Insn::Backref {
                    slot,
                    casei,
                    unicode,
                } => {
                    let lo = state.get(slot);
                    if lo == usize::MAX {
                        // Referenced group hasn't matched, so the backref doesn't match either
                        break 'fail;
                    }
                    let hi = state.get(slot + 1);
                    if hi == usize::MAX {
                        // Referenced group hasn't matched, so the backref doesn't match either
                        break 'fail;
                    }
                    let ref_text = &haystack.as_bytes()[lo..hi];
                    let ix_end = ix + ref_text.len();
                    if casei {
                        if !matches_literal_casei(haystack, ix, ix_end, ref_text, unicode) {
                            break 'fail;
                        }
                    } else if !matches_literal(haystack, ix, ix_end, ref_text) {
                        break 'fail;
                    }
                    ix = ix_end;
                }
                Insn::BackrefExistsCondition(group) => {
                    let lo = state.get(group * 2);
                    if lo == usize::MAX {
                        // Referenced group hasn't matched, so the backref doesn't match either
                        break 'fail;
                    }
                }
                Insn::Fail => {
                    // Immediately fail and trigger backtracking
                    break 'fail;
                }
                #[cfg(feature = "variable-lookbehinds")]
                Insn::BackwardsDelegate(ReverseBackwardsDelegate {
                    ref dfa,
                    ref cache_pool,
                    pattern: _,
                    ref capture_group_extraction_inner,
                    capture_groups,
                }) => {
                    // Use regex-automata to search backwards from current position
                    let mut cache_guard = cache_pool.get();
                    let input = Input::new(haystack.as_bytes())
                        .anchored(Anchored::Yes)
                        .range(0..ix);

                    match dfa.try_search_rev(&mut cache_guard, &input) {
                        Ok(Some(match_result)) => {
                            // Update ix to the start position of the match
                            let match_start = match_result.offset();

                            if let Some(inner) = capture_group_extraction_inner {
                                if let Some(range) = capture_groups {
                                    // There are capture groups, need to search forward to populate them
                                    let forward_input = Input::new(haystack.as_bytes())
                                        .span(match_start..ix)
                                        .anchored(Anchored::Yes);
                                    inner_slots.resize((range.end() - range.start() + 1) * 2, None);

                                    if inner.search_slots(&forward_input, inner_slots).is_some() {
                                        // Store capture group positions, ignoring any whose range is earlier than what has been stored already
                                        store_capture_groups(state, inner_slots, range, true);
                                    } else {
                                        break 'fail;
                                    }
                                } else {
                                    // No groups, just update ix to the match start
                                    ix = match_start;
                                }
                            } else {
                                // No groups, just update ix to the match start
                                ix = match_start;
                            }
                        }
                        _ => break 'fail,
                    }
                }
                #[cfg(feature = "variable-lookbehinds")]
                Insn::HardVariableLookbehind(HardVariableLookbehind {
                    ref dfa,
                    ref cache_pool,
                    ref end_pos_slot,
                    pattern: _,
                    ref candidate_pos_slot,
                }) => {
                    // First time entering this lookbehind: collect all candidate
                    // start positions where the seek pattern matches ending at
                    // the lookbehind end position.
                    if state.hard_lb_candidates.is_empty() {
                        let end_pos = state.get(*end_pos_slot);
                        let mut cache_guard = cache_pool.get();
                        let mut overlap_state =
                            regex_automata::hybrid::dfa::OverlappingState::start();
                        let input = Input::new(haystack.as_bytes())
                            .anchored(Anchored::Yes)
                            .range(0..end_pos);
                        loop {
                            if backtrack_count + state.hard_lb_candidates.len()
                                > options.backtrack_limit
                            {
                                break;
                            }
                            match dfa.try_search_overlapping_rev(
                                &mut cache_guard,
                                &input,
                                &mut overlap_state,
                            ) {
                                Ok(()) => match overlap_state.get_match() {
                                    Some(m) => state.hard_lb_candidates.push(m.offset()),
                                    None => break,
                                },
                                Err(_) => break,
                            }
                        }
                    }

                    if state.hard_lb_candidate_idx < state.hard_lb_candidates.len() {
                        let candidate = state.hard_lb_candidates[state.hard_lb_candidate_idx];
                        state.hard_lb_candidate_idx += 1;
                        state.save(*candidate_pos_slot, candidate);
                        ix = candidate;
                        // Push a checkpoint branch: when all inner backtrack
                        // states are exhausted (via normal `break 'fail`
                        // backtracking), this branch is popped and re-enters
                        // HardVariableLookbehind to try the next candidate.
                        // The stored `nsave` ensures save-delta from the inner
                        // expression is properly restored.  This handles nested
                        // lookbehinds naturally — each level has its own
                        // checkpoint on the stack.
                        state.push(pc, candidate)?;
                        pc += 1;
                        continue;
                    } else {
                        // Exhausted all candidates — clean up and fail.
                        state.hard_lb_candidates.clear();
                        state.hard_lb_candidate_idx = 0;
                        break 'fail;
                    }
                }
                Insn::BeginAtomic => {
                    let count = state.backtrack_count();
                    state.stack_push(count);
                }
                Insn::EndAtomic => {
                    let count = state.stack_pop();
                    state.backtrack_cut(count);
                }
                Insn::Delegate(Delegate {
                    ref inner,
                    pattern: _,
                    capture_groups,
                }) => {
                    let input = Input::new(haystack.as_bytes())
                        .span(ix..haystack.len())
                        .anchored(Anchored::Yes);
                    if let Some(range) = capture_groups {
                        // Has capture groups, need to extract them
                        inner_slots.resize((range.end() - range.start() + 1) * 2, None);
                        if inner.search_slots(&input, inner_slots).is_some() {
                            // store the capture groups, no need to check current state to see if new values are further to the right
                            store_capture_groups(state, inner_slots, range, false);
                            ix = inner_slots[1].unwrap().get();
                        } else {
                            break 'fail;
                        }
                    } else {
                        // No groups, so we can use faster methods
                        match inner.search_half(&input) {
                            Some(m) => ix = m.offset(),
                            _ => break 'fail,
                        }
                    }
                }
                Insn::AbsentRepeater(ref delegate) => {
                    // The absent operator matches the shortest string not containing the delegate pattern
                    // We advance one character at a time, checking if delegate matches at each position
                    // If delegate matches, we've found the boundary and continue to next instruction
                    // If we reach end of string without delegate matching, we also continue

                    // Check if delegate matches at current position
                    let input = Input::new(haystack.as_bytes())
                        .span(ix..haystack.len())
                        .anchored(Anchored::Yes);
                    // capture groups in the delegate are always ignored, so we can use the quicker search_half method
                    let delegate_matches_here = delegate.inner.search_half(&input).is_some();

                    if delegate_matches_here {
                        // Delegate matches at current position - we've reached the boundary
                        // Continue to next instruction without consuming any characters
                        // Fall through via pc += 1 below
                    } else if ix < haystack.len() {
                        // Try advancing one character and checking again
                        state.push(pc + 1, ix)?;
                        ix += advance_one(haystack, ix, prog.bytes_mode);
                        // Stay at same pc to check delegate match at new position
                        continue;
                    } else {
                        // Reached end of string - delegate never matched, so we succeed
                        // Fall through via pc += 1 below
                    }
                }
                Insn::ContinueFromPreviousMatchEnd { at_start } => {
                    let at_previous_match_end = input
                        .continue_from_previous_match_end_override()
                        .and_then(|value| options.allow_input_assertion_overrides.then_some(value))
                        .unwrap_or(
                            ix == pos
                                && option_flags & OPTION_NOT_CONTINUED_FROM_PREVIOUS_MATCH == 0,
                        );
                    if !at_previous_match_end {
                        // If \G is at the start of the pattern, and we are performing a non-anchored
                        // search, then we can fail early instead of checking at each position in the
                        // haystack because \G will never match at any other position
                        if at_start && state.stack.len() == 1 && !input.is_anchored() {
                            // The only item on the stack is from the SplitUnanchored (or Seek)
                            // instruction for non-anchored search.
                            // We can safely return None immediately.
                            return Ok(None);
                        }
                        break 'fail;
                    }
                }
                Insn::Seek(Seek { ref inner, .. }) => {
                    #[cfg(feature = "leftmost_longest")]
                    if leftmost_longest
                        && apply_best_saves(
                            &mut state.saves,
                            &mut state.best_saves,
                            state.best_match_set,
                        )
                    {
                        return Ok(Some(extract(state)));
                    }
                    // A sentinel value greater than haystack.len() is pushed onto the backtrack stack
                    // when the seek found a zero-width match at end-of-string.  On re-entry with
                    // that sentinel, there are no more positions to try.
                    if ix > match_range.end {
                        return Ok(None);
                    }

                    if input.is_anchored() {
                        // Anchored mode: the idea is that since the many-DFA from the regex-set has
                        // already confirmed a candidate match starting at this position, there is no
                        // need to verify the seek pattern matches here. We also don't push any backtracking
                        // states and just continue on like a no-op.
                    } else {
                        // TODO: ideally we would be able to use .earliest(true) as an extra optimization
                        //       as we only care about the start of the match, but unfortunately this doesn't
                        //       always return the correct start position, perhaps a bug in regex-automata
                        let seek_input = Input::new(haystack.as_bytes()).span(ix..match_range.end);
                        match inner.search(&seek_input) {
                            None => return Ok(None),
                            Some(m) => {
                                // Compute the next position to retry the seek from on backtrack:
                                // one codepoint past the start of this match (or past the end for
                                // zero-width matches) so we make progress.
                                let next_seek_start = if m.start() == m.end() {
                                    if m.end() < haystack.len() {
                                        m.end() + advance_one(haystack, m.end(), prog.bytes_mode)
                                    } else {
                                        // Zero-width match at end-of-string.  Push a sentinel value
                                        // (haystack.len() + 1) so that if the main pattern fails and we
                                        // backtrack here, the `ix > haystack.len()` guard above returns None
                                        // immediately instead of looping.
                                        haystack.len() + 1
                                    }
                                } else {
                                    m.start() + advance_one(haystack, m.start(), prog.bytes_mode)
                                };
                                state.push(pc, next_seek_start)?;
                                ix = m.start();
                            }
                        }
                    }
                    match_attempt_start = ix;
                    slash_z_matched = false;
                    pc += 1;
                    continue;
                }
                Insn::RejectEmptyMatchAtEOFFollowingNewline => {
                    if ix == haystack.len()
                        && ix > 0
                        && matches_literal(haystack, ix - 1, ix, b"\n")
                        && !slash_z_matched
                        && match_attempt_start == ix
                    {
                        break 'fail;
                    }
                }
            }
            pc += 1;
        }
        #[cfg(feature = "std")]
        if option_flags & OPTION_TRACE != 0 {
            println!("fail");
        }
        // "break 'fail" goes here
        if state.stack.is_empty() {
            #[cfg(feature = "leftmost_longest")]
            if leftmost_longest
                && apply_best_saves(
                    &mut state.saves,
                    &mut state.best_saves,
                    state.best_match_set,
                )
            {
                return Ok(Some(extract(state)));
            }
            return Ok(None);
        }

        #[cfg(feature = "leftmost_longest")]
        if leftmost_longest
            && best_match_len == prog.max_size
            && apply_best_saves(
                &mut state.saves,
                &mut state.best_saves,
                state.best_match_set,
            )
        {
            return Ok(Some(extract(state)));
        }

        backtrack_count += 1;
        if backtrack_count > options.backtrack_limit {
            return Err(Error::RuntimeError(RuntimeError::BacktrackLimitExceeded));
        }

        let (newpc, newix) = state.pop();
        pc = newpc;
        ix = newix;
    }
}

#[cfg(test)]
mod tests {
    use super::*;
    use quickcheck::{quickcheck, Arbitrary, Gen};

    #[test]
    fn casei_unicode_fold_equality() {
        // Same-case and simple-fold pairs match.
        assert!(matches_literal_casei_unicode("ΑΛΦΑ", "αλφα"));
        assert!(matches_literal_casei_unicode("σ", "ς"));
        // Fold orbits that cross scripts/blocks: ſ (U+017F) folds to s,
        // K (U+212A, Kelvin sign) folds to k, Å (U+212B, Angstrom sign)
        // folds to å.
        assert!(matches_literal_casei_unicode("ſ", "s"));
        assert!(matches_literal_casei_unicode("S", "ſ"));
        assert!(matches_literal_casei_unicode("\u{212A}", "k"));
        assert!(matches_literal_casei_unicode("\u{212B}", "å"));
        // Mixed ASCII/non-ASCII content.
        assert!(matches_literal_casei_unicode("aΛb", "AλB"));
        // Simple folding does not include full case folding (ß ≠ ss).
        assert!(!matches_literal_casei_unicode("straße", "STRASSE"));
        // Both strings must end together: a folded prefix is not a match.
        assert!(!matches_literal_casei_unicode("sab", "ſa"));
        assert!(!matches_literal_casei_unicode("ſa", "ſab"));
        assert!(!matches_literal_casei_unicode("αα", "α"));
    }

    #[test]
    fn state_push_pop() {
        let mut state = State::new(1, MAX_STACK, 0);

        state.push(0, 0).unwrap();
        state.push(1, 1).unwrap();
        assert_eq!(state.pop(), (1, 1));
        assert_eq!(state.pop(), (0, 0));
        assert!(state.stack.is_empty());

        state.push(2, 2).unwrap();
        assert_eq!(state.pop(), (2, 2));
        assert!(state.stack.is_empty());
    }

    #[test]
    #[cfg(feature = "variable-lookbehinds")]
    fn state_reset_clears_hard_lb_candidates() {
        // `reset` is called between matches on the shared scratch state, so
        // leftover candidate data from a previous hard lookbehind must not
        // leak into the next search.
        let mut state = State::new(2, MAX_STACK, 0);
        state.hard_lb_candidates.push(7);
        state.hard_lb_candidates.push(8);
        state.hard_lb_candidate_idx = 1;

        state.reset(2, 0);

        assert!(state.hard_lb_candidates.is_empty());
        assert_eq!(state.hard_lb_candidate_idx, 0);
        // saves are reset to the new length, as in the other reset paths.
        assert_eq!(state.get(0), usize::MAX);
        assert_eq!(state.get(1), usize::MAX);
    }

    #[test]
    fn state_save_override() {
        let mut state = State::new(1, MAX_STACK, 0);
        state.save(0, 10);
        state.push(0, 0).unwrap();
        state.save(0, 20);
        assert_eq!(state.pop(), (0, 0));
        assert_eq!(state.get(0), 10);
    }

    #[test]
    fn state_save_override_twice() {
        let mut state = State::new(1, MAX_STACK, 0);
        state.save(0, 10);
        state.push(0, 0).unwrap();
        state.save(0, 20);
        state.push(1, 1).unwrap();
        state.save(0, 30);

        assert_eq!(state.get(0), 30);
        assert_eq!(state.pop(), (1, 1));
        assert_eq!(state.get(0), 20);
        assert_eq!(state.pop(), (0, 0));
        assert_eq!(state.get(0), 10);
    }

    #[test]
    fn state_explicit_stack() {
        let mut state = State::new(1, MAX_STACK, 0);
        state.stack_push(11);
        state.stack_push(12);

        state.push(100, 101).unwrap();
        state.stack_push(13);
        assert_eq!(state.stack_pop(), 13);
        state.stack_push(14);
        assert_eq!(state.pop(), (100, 101));

        // Note: 14 is not there because it was pushed as part of the backtrack branch
        assert_eq!(state.stack_pop(), 12);
        assert_eq!(state.stack_pop(), 11);
    }

    #[test]
    fn state_backtrack_cut_simple() {
        let mut state = State::new(2, MAX_STACK, 0);
        state.save(0, 1);
        state.save(1, 2);

        let count = state.backtrack_count();

        state.push(0, 0).unwrap();
        state.save(0, 3);
        assert_eq!(state.backtrack_count(), 1);

        state.backtrack_cut(count);
        assert_eq!(state.backtrack_count(), 0);
        assert_eq!(state.get(0), 3);
        assert_eq!(state.get(1), 2);
    }

    #[test]
    fn state_backtrack_cut_complex() {
        let mut state = State::new(2, MAX_STACK, 0);
        state.save(0, 1);
        state.save(1, 2);

        state.push(0, 0).unwrap();
        state.save(0, 3);

        let count = state.backtrack_count();

        state.push(1, 1).unwrap();
        state.save(0, 4);
        state.push(2, 2).unwrap();
        state.save(1, 5);
        assert_eq!(state.backtrack_count(), 3);

        state.backtrack_cut(count);
        assert_eq!(state.backtrack_count(), 1);
        assert_eq!(state.get(0), 4);
        assert_eq!(state.get(1), 5);

        state.pop();
        assert_eq!(state.backtrack_count(), 0);
        // Check that oldsave were set correctly
        assert_eq!(state.get(0), 1);
        assert_eq!(state.get(1), 2);
    }

    #[derive(Clone, Debug)]
    enum Operation {
        Push,
        Pop,
        Save(usize, usize),
    }

    impl Arbitrary for Operation {
        fn arbitrary(g: &mut Gen) -> Self {
            match g.choose(&[0, 1, 2]) {
                Some(0) => Operation::Push,
                Some(1) => Operation::Pop,
                _ => Operation::Save(
                    *g.choose(&[0usize, 1, 2, 3, 4]).unwrap(),
                    usize::arbitrary(g),
                ),
            }
        }
    }

    fn check_saves_for_operations(operations: Vec<Operation>) -> bool {
        let slots = operations
            .iter()
            .map(|o| match o {
                &Operation::Save(slot, _) => slot + 1,
                _ => 0,
            })
            .max()
            .unwrap_or(0);
        if slots == 0 {
            // No point checking if there's no save instructions
            return true;
        }

        // Stack with the complete VM state (including saves)
        let mut stack = Vec::new();
        let mut saves = vec![usize::MAX; slots];

        let mut state = State::new(slots, MAX_STACK, 0);

        let mut expected = Vec::new();
        let mut actual = Vec::new();

        for operation in operations {
            match operation {
                Operation::Push => {
                    // We're not checking pc and ix later, so don't bother
                    // putting in random values.
                    stack.push((0, 0, saves.clone()));
                    state.push(0, 0).unwrap();
                }
                Operation::Pop => {
                    // Note that because we generate the operations randomly
                    // there might be more pops than pushes. So ignore a pop
                    // if the stack was empty.
                    if let Some((_, _, previous_saves)) = stack.pop() {
                        saves = previous_saves;
                        state.pop();
                    }
                }
                Operation::Save(slot, value) => {
                    saves[slot] = value;
                    state.save(slot, value);
                }
            }

            // Remember state of saves for checking later
            expected.push(saves.clone());
            let mut actual_saves = vec![usize::MAX; slots];
            for (i, item) in actual_saves.iter_mut().enumerate().take(slots) {
                *item = state.get(i);
            }
            actual.push(actual_saves);
        }

        expected == actual
    }

    quickcheck! {
        fn state_save_quickcheck(operations: Vec<Operation>) -> bool {
            check_saves_for_operations(operations)
        }
    }
}
