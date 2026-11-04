// Copyright 2026 The Fancy Regex Authors.
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

//! Fancy regular expressions with an opt-in strict numeric repeat parser.
#![deny(missing_docs)]
#![deny(missing_debug_implementations)]
#![cfg_attr(not(feature = "std"), no_std)]

extern crate alloc;

use alloc::borrow::Cow;
use alloc::boxed::Box;
use alloc::string::{String, ToString};
use alloc::sync::Arc;
use alloc::vec;
use alloc::vec::Vec;

use core::convert::TryFrom;
use core::fmt;
use core::fmt::{Debug, Formatter};
use core::ops::{Index, Range};
use core::str::FromStr;
use regex_automata::meta::Regex as RaRegex;
use regex_automata::util::captures::Captures as RaCaptures;
use regex_automata::util::syntax::Config as SyntaxConfig;
use regex_automata::Anchored as RaAnchored;
use regex_automata::Input as RaInput;

mod analyze;
mod byte_set;
mod bytes;
mod compile;
mod error;
mod expand;
mod input;
mod optimize;
mod parse;
mod parse_flags;
mod regexset;
mod replacer;
mod seek;
mod to_hir;
mod vm;

use crate::analyze::can_compile_as_anchored;
use crate::analyze::{analyze, AnalyzeContext};
use crate::compile::{compile, CompileOptions};
use crate::optimize::optimize;
use crate::parse::{ExprTree, NamedGroups, Parser};
use crate::parse_flags::*;
#[cfg(feature = "leftmost_longest")]
use crate::vm::OPTION_LEFTMOST_LONGEST;
use crate::vm::{Prog, OPTION_FIND_NOT_EMPTY, OPTION_NOT_CONTINUED_FROM_PREVIOUS_MATCH};

use bit_set::BitSet;

pub use crate::byte_set::ByteSet;
pub use crate::bytes::MatchBytes;
pub use crate::error::{CompileError, Error, ParseError, Result, RuntimeError};
pub use crate::expand::Expander;
pub use crate::input::{Input, RegexInput};
pub use crate::regexset::{RegexSet, RegexSetMatch, RegexSetOptions};
pub use crate::replacer::{NoExpand, Replacer, ReplacerRef};
pub use crate::seek::seek_pattern_is_useful;

/// Controls how the regex engine handles input encoding.
///
/// This enum represents the three valid combinations of the `utf8` and `unicode`
/// flags in the underlying regex engine. Each variant has different trade-offs
/// between input flexibility and character class semantics.
///
/// The default is [`BytesMode::Unicode`].
///
/// # Variants
///
/// ## `BytesMode::Unicode` (default)
///
/// - Input is expected to be valid UTF-8
/// - `.` matches any Unicode scalar value (except `\n` unless `dot_matches_new_line` is set)
/// - `\w`, `\d`, `\s` match Unicode characters
/// - Unicode properties like `\p{Letter}` are available
/// - Word boundaries (`\b`) are Unicode-aware
///
/// ## `BytesMode::Ascii`
///
/// - Input can be arbitrary bytes (no UTF-8 requirement)
/// - `.` matches any single **byte** (except `\n` unless `dot_matches_new_line` is set)
/// - `\w` matches `[a-zA-Z0-9_]` only (ASCII)
/// - `\d` matches `[0-9]` only (ASCII)
/// - `\s` matches ASCII whitespace only
/// - Unicode properties are **not available**
/// - Word boundaries (`\b`) use ASCII-only word characters
///
/// Use this mode when matching raw binary data or filenames that may contain
/// non-UTF-8 bytes and you don't need Unicode character classes.
///
/// ## `BytesMode::UnicodeBytes`
///
/// - Input can be arbitrary bytes (no UTF-8 requirement)
/// - `.` matches Unicode scalar values (sequences of valid UTF-8 bytes)
/// - `\w`, `\d`, `\s` match Unicode characters
/// - Unicode properties like `\p{Letter}` are available
///
/// Use this mode when the input may contain non-UTF-8 bytes but you still want
/// Unicode-aware character classes. Note that `.` will **not** match individual
/// non-UTF-8 bytes — it only matches valid UTF-8 codepoint sequences.
#[derive(Clone, Copy, Debug, Default, Eq, PartialEq)]
pub enum BytesMode {
    /// Unicode mode: input must be valid UTF-8, full Unicode support.
    /// (utf8=true, unicode=true)
    #[default]
    Unicode,
    /// ASCII bytes mode: `.` matches any byte, character classes are ASCII-only.
    /// (utf8=false, unicode=false)
    Ascii,
    /// Unicode-aware bytes mode: input can be non-UTF-8, character classes
    /// remain Unicode-aware. `.` matches Unicode scalar values only.
    /// (utf8=false, unicode=true)
    UnicodeBytes,
}

const MAX_RECURSION: usize = 64;

// the public API

/// A builder for a `Regex` to allow configuring options.
#[derive(Debug)]
pub struct RegexBuilder {
    pattern: String,
    options: RegexOptionsBuilder,
}

/// A builder for a `Regex` to allow configuring options.
#[derive(Debug)]
pub struct RegexOptionsBuilder {
    options: RegexOptions,
}

/// A compiled regular expression.
#[derive(Clone)]
pub struct Regex {
    inner: RegexImpl,
    named_groups: Arc<NamedGroups>,
}

// Separate enum because we don't want to expose any of this
#[derive(Clone)]
enum RegexImpl {
    // Do we want to box this? It's pretty big...
    Wrap {
        inner: RaRegex,
        /// The original pattern which the regex was constructed from
        pattern: String,
        /// Some optimizations avoid the VM, but need to use an extra capture group to represent the match boundaries
        explicit_capture_group_0: bool,
        /// The actual pattern passed to regex-automata for delegation
        delegated_pattern: String,
        /// Group indices that are inside {0} (zero-repetition) quantifiers.
        /// These groups never participate in matching, so their captures
        /// return None even though they exist in the regex-automata engine
        /// for correct group-counting.
        zero_rep_groups: BitSet,
    },
    Fancy {
        prog: Arc<Prog>,
        n_groups: usize,
        /// The original pattern which the regex was constructed from
        pattern: String,
        options: HardRegexRuntimeOptions,
    },
}

/// A single match of a regex or group in an input text
#[derive(Copy, Clone, Debug, Eq, PartialEq)]
pub struct Match<'t> {
    text: &'t str,
    start: usize,
    end: usize,
}

/// An iterator over all non-overlapping matches for a particular input.
///
/// The iterator yields a `Result<S::Match>`. The iterator stops when no more
/// matches can be found.
///
/// `'r` is the lifetime of the compiled regular expression and `'t` is the
/// lifetime of the matched input.
#[derive(Debug)]
pub struct Matches<'r, 't, S: input::Input + ?Sized> {
    re: &'r Regex,
    input: RegexInput<'t, S>,
    last_match: Option<usize>,
    last_skipped_empty: bool,
}

impl<'r, 't, S: input::Input + ?Sized> Matches<'r, 't, S> {
    /// Return the underlying regex.
    pub fn regex(&self) -> &'r Regex {
        self.re
    }

    /// Return the text being searched.
    pub fn text(&self) -> &'t S {
        self.input.haystack()
    }

    /// Return the current search input configuration.
    pub fn input(&self) -> &RegexInput<'t, S> {
        &self.input
    }

    /// Adapted from the `regex` crate. Calls `find_from_pos`/`captures_from_pos` repeatedly.
    /// Ignores empty matches immediately after a match.
    /// Also passes a flag when skipping an empty match, so that \G wouldn't match at the new start position.
    fn next_with<F, R>(&mut self, mut search: F) -> Option<Result<R>>
    where
        F: FnMut(&Regex, &RegexInput<'t, S>, u32) -> Result<Option<(R, (usize, usize))>>,
    {
        if self.input.is_done() {
            return None;
        }

        let option_flags = if self.last_skipped_empty {
            OPTION_NOT_CONTINUED_FROM_PREVIOUS_MATCH
        } else {
            0
        };

        let pos = self.input.effective_start();
        let (result, (match_start, match_end)) = match search(self.re, &self.input, option_flags) {
            Err(error) => {
                // Stop on first error: If an error is encountered, return it, and set the "last match position"
                // to the string length, so that the next next() call will return None, to prevent an infinite loop.
                self.input
                    .set_start(self.input.get_range().end.saturating_add(1));
                return Some(Err(error));
            }
            Ok(None) => return None,
            Ok(Some(pair)) => pair,
        };

        if match_start == match_end {
            // This is an empty match. To ensure we make progress, start
            // the next search at the smallest possible starting position
            // of the next match following this one.
            self.input
                .set_start(self.input.haystack().advance_position(match_end));
            // Only set OPTION_NOT_CONTINUED_FROM_PREVIOUS_MATCH on the next call if this was a
            // truly zero-length match (the VM consumed no bytes from `pos`).
            // This means that \K won't prevent \G from matching.
            self.last_skipped_empty = match_end == pos;
            // Don't accept empty matches immediately following a match.
            // Just move on to the next match.
            if Some(match_end) == self.last_match {
                return self.next_with(search);
            }
        } else {
            self.input.set_start(match_end);
            self.last_skipped_empty = false;
        }

        self.last_match = Some(match_end);

        Some(Ok(result))
    }
}

impl<'r, 't, S: input::Input + ?Sized> Iterator for Matches<'r, 't, S> {
    type Item = Result<S::Match<'t>>;

    fn next(&mut self) -> Option<Self::Item> {
        let text = self.input.haystack();
        self.next_with(move |re, input, flags| {
            re.find_input_raw(input, flags)
                .map(|opt| opt.map(|(s, e)| (text.make_match(s, e), (s, e))))
        })
    }
}

/// An iterator that yields all non-overlapping capture groups matching a
/// particular regular expression.
///
/// The iterator stops when no more matches can be found.
///
/// `'r` is the lifetime of the compiled regular expression and `'t` is the
/// lifetime of the matched string.
#[derive(Debug)]
pub struct CaptureMatches<'r, 't, S: input::Input + ?Sized>(Matches<'r, 't, S>);

impl<'r, 't, S: input::Input + ?Sized> CaptureMatches<'r, 't, S> {
    /// Return the text being searched.
    pub fn text(&self) -> &'t S {
        self.0.input.haystack()
    }

    /// Return the underlying regex.
    pub fn regex(&self) -> &'r Regex {
        self.0.re
    }
}

impl<'r, 't, S: input::Input + ?Sized> Iterator for CaptureMatches<'r, 't, S> {
    type Item = Result<Captures<'t, S>>;

    fn next(&mut self) -> Option<Self::Item> {
        self.0.next_with(move |re, input, flags| {
            let captures = re.captures_input_with_option_flags(input, flags)?;
            Ok(captures.map(|c| {
                let (start, end) = c
                    .inner
                    .get_span(0)
                    .expect("`Captures` is expected to have entire match at 0th position");
                (c, (start, end))
            }))
        })
    }
}

/// A set of capture groups found for a regex.
///
/// `S` is the input type (`str` or `[u8]`).
#[derive(Debug)]
pub struct Captures<'t, S: input::Input + ?Sized> {
    inner: CapturesImpl,
    named_groups: Arc<NamedGroups>,
    input: &'t S,
}

#[derive(Debug)]
enum CapturesImpl {
    Wrap {
        locations: RaCaptures,
        /// Some optimizations avoid the VM but need an extra capture group to represent the match boundaries.
        /// Therefore what is actually capture group 1 should be treated as capture group 0, and all other
        /// capture groups should have their index reduced by one as well to line up with what the pattern specifies.
        explicit_capture_group_0: bool,
        /// Group indices that are inside {0} (zero-repetition) quantifiers.
        /// These groups never participate in matching, so their captures return None.
        zero_rep_groups: BitSet,
    },
    Fancy {
        saves: Vec<usize>,
    },
}

impl CapturesImpl {
    fn get_span(&self, i: usize) -> Option<(usize, usize)> {
        match self {
            CapturesImpl::Wrap {
                locations,
                explicit_capture_group_0,
                zero_rep_groups,
            } => {
                if zero_rep_groups.contains(i) {
                    return None;
                }
                locations
                    .get_group(i + if *explicit_capture_group_0 { 1 } else { 0 })
                    .map(|span| (span.start, span.end))
            }
            CapturesImpl::Fancy { saves } => {
                let slot = i * 2;
                if slot >= saves.len() {
                    return None;
                }
                let lo = saves[slot];
                if lo == usize::MAX {
                    return None;
                }
                let hi = saves[slot + 1];
                Some((lo, hi))
            }
        }
    }

    fn len(&self) -> usize {
        match self {
            CapturesImpl::Wrap {
                locations,
                explicit_capture_group_0,
                ..
            } => locations.group_len() - if *explicit_capture_group_0 { 1 } else { 0 },
            CapturesImpl::Fancy { saves } => saves.len() / 2,
        }
    }
}

/// Iterator for captured groups in order in which they appear in the regex.
#[derive(Debug)]
pub struct SubCaptureMatches<'c, 't, S: input::Input + ?Sized> {
    data: &'c Captures<'t, S>,
    i: usize,
}

/// An iterator over all substrings delimited by a regex.
///
/// This iterator yields `Result<&'h str>`, where each item is a substring of the
/// target string that is delimited by matches of the regular expression. It stops when there
/// are no more substrings to yield.
///
/// `'r` is the lifetime of the compiled regular expression, and `'h` is the
/// lifetime of the target string being split.
///
/// This iterator can be created by the [`Regex::split`] method.
#[derive(Debug)]
pub struct Split<'r, 'h> {
    matches: Matches<'r, 'h, str>,
    next_start: usize,
    target: &'h str,
}

impl<'r, 'h> Iterator for Split<'r, 'h> {
    type Item = Result<&'h str>;

    /// Returns the next substring that results from splitting the target string by the regex.
    ///
    /// If no more matches are found, returns the remaining part of the string,
    /// or `None` if all substrings have been yielded.
    fn next(&mut self) -> Option<Result<&'h str>> {
        match self.matches.next() {
            None => {
                let len = self.target.len();
                if self.next_start > len {
                    // No more substrings to return
                    None
                } else {
                    // Return the last part of the target string
                    // Next call will return None
                    let part = &self.target[self.next_start..len];
                    self.next_start = len + 1;
                    Some(Ok(part))
                }
            }
            // Return the next substring
            Some(Ok(m)) => {
                let part = &self.target[self.next_start..m.start()];
                self.next_start = m.end();
                Some(Ok(part))
            }
            Some(Err(e)) => Some(Err(e)),
        }
    }
}

impl<'r, 'h> core::iter::FusedIterator for Split<'r, 'h> {}

/// An iterator over at most `N` substrings delimited by a regex.
///
/// This iterator yields `Result<&'h str>`, where each item is a substring of the
/// target that is delimited by matches of the regular expression. It stops either when
/// there are no more substrings to yield, or after `N` substrings have been yielded.
///
/// The `N`th substring is the remaining part of the target.
///
/// `'r` is the lifetime of the compiled regular expression, and `'h` is the
/// lifetime of the target string being split.
///
/// This iterator can be created by the [`Regex::splitn`] method.
#[derive(Debug)]
pub struct SplitN<'r, 'h> {
    splits: Split<'r, 'h>,
    limit: usize,
}

impl<'r, 'h> Iterator for SplitN<'r, 'h> {
    type Item = Result<&'h str>;

    /// Returns the next substring resulting from splitting the target by the regex,
    /// limited to `N` splits.
    ///
    /// Returns `None` if no more matches are found or if the limit is reached after yielding
    /// the remaining part of the target.
    fn next(&mut self) -> Option<Result<&'h str>> {
        if self.limit == 0 {
            // Limit reached. No more substrings available.
            return None;
        }

        // Decrement the limit for each split.
        self.limit -= 1;
        if self.limit > 0 {
            return self.splits.next();
        }

        // Nth split
        let len = self.splits.target.len();
        if self.splits.next_start > len {
            // No more substrings available.
            None
        } else {
            // Return the remaining part of the target
            let start = self.splits.next_start;
            self.splits.next_start = len + 1;
            Some(Ok(&self.splits.target[start..len]))
        }
    }

    fn size_hint(&self) -> (usize, Option<usize>) {
        (0, Some(self.limit))
    }
}

impl<'r, 'h> core::iter::FusedIterator for SplitN<'r, 'h> {}

#[derive(Clone)]
struct RegexOptions {
    syntaxc: SyntaxConfig,
    delegate_size_limit: Option<usize>,
    delegate_dfa_size_limit: Option<usize>,
    delegate_dfa_skip_cache_capacity_check: bool,
    /// Optional cap on the number of VM instructions emitted while compiling.
    /// Subroutine calls are inlined at compile time, so a self- or
    /// mutually-recursive pattern can expand without bound even while the
    /// recursion-depth cap is respected; this bounds the emitted instruction
    /// vector instead. `None` disables the check.
    max_prog_size: Option<usize>,
    oniguruma_mode: bool,
    strict_numeric_repeat: bool,
    reject_posix_word_boundary: bool,
    ignore_numbered_groups_when_named_groups_exist: bool,
    hard_regex_runtime_options: HardRegexRuntimeOptions,
    bytes_mode: BytesMode,
    seek_filter: Option<fn(&str) -> bool>,
    /// Whether the top-level engine of an easy (`Wrap`) pattern should build a
    /// prefilter. `RegexSet` turns this off for its internally-built members:
    /// the set only ever searches them anchored at candidate positions, where
    /// a prefilter is never consulted, so building one wastes time and memory.
    delegate_prefilter: bool,
    #[cfg(feature = "leftmost_longest")]
    leftmost_longest: bool,
}

impl fmt::Debug for RegexOptions {
    fn fmt(&self, f: &mut fmt::Formatter<'_>) -> fmt::Result {
        let seek_filter_desc = match self.seek_filter {
            None => "None",
            Some(f_ptr) if (f_ptr as *const ()) == (seek_pattern_is_useful as *const ()) => {
                "Some(seek_pattern_is_useful)"
            }
            Some(_) => "Some(<custom>)",
        };
        let mut debug = f.debug_struct("RegexOptions");
        debug
            .field("syntaxc", &self.syntaxc)
            .field("delegate_size_limit", &self.delegate_size_limit)
            .field("delegate_dfa_size_limit", &self.delegate_dfa_size_limit)
            .field(
                "delegate_dfa_skip_cache_capacity_check",
                &self.delegate_dfa_skip_cache_capacity_check,
            )
            .field("max_prog_size", &self.max_prog_size)
            .field("oniguruma_mode", &self.oniguruma_mode)
            .field("strict_numeric_repeat", &self.strict_numeric_repeat)
            .field(
                "reject_posix_word_boundary",
                &self.reject_posix_word_boundary,
            )
            .field(
                "ignore_numbered_groups_when_named_groups_exist",
                &self.ignore_numbered_groups_when_named_groups_exist,
            )
            .field(
                "hard_regex_runtime_options",
                &self.hard_regex_runtime_options,
            )
            .field("seek_filter", &seek_filter_desc)
            .field("delegate_prefilter", &self.delegate_prefilter);
        #[cfg(feature = "leftmost_longest")]
        debug.field("leftmost_longest", &self.leftmost_longest);
        debug.finish()
    }
}

impl Default for RegexOptions {
    fn default() -> Self {
        RegexOptions {
            syntaxc: SyntaxConfig::new().unicode(true),
            delegate_size_limit: None,
            delegate_dfa_size_limit: None,
            delegate_dfa_skip_cache_capacity_check: true,
            max_prog_size: None,
            oniguruma_mode: false,
            strict_numeric_repeat: false,
            reject_posix_word_boundary: false,
            ignore_numbered_groups_when_named_groups_exist: false,
            hard_regex_runtime_options: HardRegexRuntimeOptions::default(),
            bytes_mode: BytesMode::default(),
            seek_filter: None, // when we are ready to enable seek by default, use: `Some(seek_pattern_is_useful)`
            delegate_prefilter: true,
            #[cfg(feature = "leftmost_longest")]
            leftmost_longest: false,
        }
    }
}

#[derive(Copy, Clone, Debug)]
struct HardRegexRuntimeOptions {
    backtrack_limit: usize,
    find_not_empty: bool,
    disallow_empty_match_at_eof_after_newline: bool,
    allow_input_assertion_overrides: bool,
    #[cfg(feature = "leftmost_longest")]
    leftmost_longest: bool,
}

impl RegexOptions {
    fn get_flag_value(flag_value: bool, enum_value: u32) -> u32 {
        if flag_value {
            enum_value
        } else {
            0
        }
    }

    fn compute_flags(&self) -> u32 {
        let insensitive = Self::get_flag_value(self.syntaxc.get_case_insensitive(), FLAG_CASEI);
        let multiline = Self::get_flag_value(self.syntaxc.get_multi_line(), FLAG_MULTI);
        let whitespace =
            Self::get_flag_value(self.syntaxc.get_ignore_whitespace(), FLAG_IGNORE_SPACE);
        let dotnl = Self::get_flag_value(self.syntaxc.get_dot_matches_new_line(), FLAG_DOTNL);
        let unicode = Self::get_flag_value(
            self.syntaxc.get_unicode() && !matches!(self.bytes_mode, BytesMode::Ascii),
            FLAG_UNICODE,
        );
        let oniguruma_mode = Self::get_flag_value(self.oniguruma_mode, FLAG_ONIGURUMA_MODE);
        let strict_numeric_repeat =
            Self::get_flag_value(self.strict_numeric_repeat, FLAG_STRICT_NUMERIC_REPEAT);
        let reject_posix_word_boundary = Self::get_flag_value(
            self.reject_posix_word_boundary,
            FLAG_REJECT_POSIX_WORD_BOUNDARY,
        );
        let crlf = Self::get_flag_value(self.syntaxc.get_crlf(), FLAG_CRLF);
        let named_groups_only = Self::get_flag_value(
            self.ignore_numbered_groups_when_named_groups_exist,
            FLAG_IGNORE_NUMBERED_GROUPS_WHEN_NAMED_GROUPS_EXIST,
        );

        insensitive
            | multiline
            | whitespace
            | dotnl
            | unicode
            | oniguruma_mode
            | strict_numeric_repeat
            | reject_posix_word_boundary
            | crlf
            | named_groups_only
    }
}

impl Default for HardRegexRuntimeOptions {
    fn default() -> Self {
        HardRegexRuntimeOptions {
            backtrack_limit: 1_000_000,
            find_not_empty: false,
            disallow_empty_match_at_eof_after_newline: false,
            allow_input_assertion_overrides: false,
            #[cfg(feature = "leftmost_longest")]
            leftmost_longest: false,
        }
    }
}

impl Default for RegexOptionsBuilder {
    fn default() -> Self {
        Self::new()
    }
}

impl RegexOptionsBuilder {
    /// Create a new regex options builder.
    pub fn new() -> Self {
        RegexOptionsBuilder {
            options: RegexOptions::default(),
        }
    }

    /// Build a `Regex` from the given pattern.
    ///
    /// Returns an [`Error`](enum.Error.html) if the pattern could not be parsed.
    pub fn build(&self, pattern: String) -> Result<Regex> {
        Regex::new_options(pattern, &self.options)
    }

    fn set_config(&mut self, func: impl Fn(SyntaxConfig) -> SyntaxConfig) -> &mut Self {
        self.options.syntaxc = func(self.options.syntaxc);
        self
    }

    /// Override default case insensitive
    /// this is to enable/disable casing via builder instead of a flag within
    /// the raw string pattern which will be parsed
    ///
    /// Default is false
    pub fn case_insensitive(&mut self, yes: bool) -> &mut Self {
        self.set_config(|x| x.case_insensitive(yes))
    }

    /// Enable multi-line regex
    pub fn multi_line(&mut self, yes: bool) -> &mut Self {
        self.set_config(|x| x.multi_line(yes))
    }

    /// Allow ignore whitespace
    pub fn ignore_whitespace(&mut self, yes: bool) -> &mut Self {
        self.set_config(|x| x.ignore_whitespace(yes))
    }

    /// Enable or disable the "dot matches any character" flag.
    /// When this is enabled, `.` will match any character. When it's disabled, then `.` will match any character
    /// except for a new line character.
    pub fn dot_matches_new_line(&mut self, yes: bool) -> &mut Self {
        self.set_config(|x| x.dot_matches_new_line(yes))
    }

    /// Enable or disable the CRLF mode flag (`R`).
    ///
    /// When enabled, `\r\n` is treated as a single line ending for the purposes of
    /// `^` and `$` in multi-line mode, instead of treating `\r` and `\n` as separate
    /// line endings.
    ///
    /// By default, this is disabled. It may be selectively enabled in the regular
    /// expression by using the `R` flag, e.g. `(?mR)` or `(?Rm)`.
    pub fn crlf(&mut self, yes: bool) -> &mut Self {
        self.set_config(|x| x.crlf(yes))
    }

    /// Enable verbose mode in the regular expression.
    ///
    /// The same as ignore_whitespace
    ///
    /// When enabled, verbose mode permits insigificant whitespace in many
    /// places in the regular expression, as well as comments. Comments are
    /// started using `#` and continue until the end of the line.
    ///
    /// By default, this is disabled. It may be selectively enabled in the
    /// regular expression by using the `x` flag regardless of this setting.
    pub fn verbose_mode(&mut self, yes: bool) -> &mut Self {
        self.set_config(|x| x.ignore_whitespace(yes))
    }

    /// Enable or disable the Unicode flag (`u`) by default.
    ///
    /// By default this is **enabled**. The inline `u` flag inside a pattern
    /// is only accepted when it **matches** the current builder setting (e.g.
    /// `(?u)` when unicode is already enabled, or `(?-u)` when it is already
    /// disabled). Attempts to change the mode inline are rejected with a
    /// [`ParseError::ChangingUnicodeModeUnsupported`] error. Use this builder
    /// method to set the desired mode instead.
    ///
    /// ## Effect on `str` input (default)
    ///
    /// When matching against `&str` (the default), the underlying engine
    /// requires that all matches respect UTF-8 boundaries. Disabling Unicode
    /// therefore has the following effects:
    ///
    /// - **`\w`, `\d`, `\s`** become ASCII-only (`[a-zA-Z0-9_]`, `[0-9]`,
    ///   and ASCII whitespace respectively).
    /// - **`\W`, `\D`, `\S`**, bare **`.`**, and **`\p{...}`** Unicode
    ///   properties **fail to compile**, because they could match byte
    ///   sequences that violate UTF-8 boundaries.
    ///
    /// If you need those constructs with `unicode_mode(false)`, use the bytes
    /// API with [`BytesMode::Ascii`] instead.
    ///
    /// ## Effect on byte input
    ///
    /// When matching against `&[u8]` (via [`BytesMode::Ascii`]), all
    /// constructs work as expected in ASCII mode (`.` matches any byte,
    /// `\W`/`\D`/`\S` match non-ASCII byte values, etc.).
    ///
    /// **WARNING**: Unicode mode can greatly increase the size of the compiled
    /// DFA, which can noticeably impact both memory usage and compilation
    /// time. This is especially noticeable if your regex contains character
    /// classes like `\w` that are impacted by whether Unicode is enabled or
    /// not. If Unicode is not necessary, you are encouraged to disable it.
    pub fn unicode_mode(&mut self, yes: bool) -> &mut Self {
        self.set_config(|x| x.unicode(yes))
    }

    /// Limit for how many times backtracking should be attempted for fancy regexes (where
    /// backtracking is used). If this limit is exceeded, execution returns an error with
    /// [`Error::BacktrackLimitExceeded`](enum.Error.html#variant.BacktrackLimitExceeded).
    /// This is for preventing a regex with catastrophic backtracking to run for too long.
    ///
    /// Default is `1_000_000` (1 million).
    pub fn backtrack_limit(&mut self, limit: usize) -> &mut Self {
        self.options.hard_regex_runtime_options.backtrack_limit = limit;
        self
    }

    /// Set the approximate size limit of the compiled regular expression.
    ///
    /// This option is forwarded from the wrapped `regex` crate. Note that depending on the used
    /// regex features there may be multiple delegated sub-regexes fed to the `regex` crate. As
    /// such the actual limit is closer to `<number of delegated regexes> * delegate_size_limit`.
    pub fn delegate_size_limit(&mut self, limit: usize) -> &mut Self {
        self.options.delegate_size_limit = Some(limit);
        self
    }

    /// Set the approximate capacity, in bytes, of the cache used by the lazy DFA of delegated
    /// regexes.
    ///
    /// This corresponds to `hybrid_cache_capacity` in regex-automata, and has the same meaning as
    /// `RegexBuilder::dfa_size_limit` in the `regex` crate. Note that depending on the used
    /// regex features there may be multiple delegated sub-regexes. As such the actual limit is
    /// closer to `<number of delegated regexes> * delegate_dfa_size_limit`.
    ///
    /// Patterns with a variable-length lookbehind additionally build a dedicated reverse lazy
    /// DFA. If the limit is below the minimum cache capacity that DFA needs (which depends on the
    /// NFA size of the lookbehind body and can reach several MiB to tens of MiB for large
    /// repetitions of Unicode classes), it is rounded up to that minimum instead of causing a
    /// compile error.
    pub fn delegate_dfa_size_limit(&mut self, limit: usize) -> &mut Self {
        self.options.delegate_dfa_size_limit = Some(limit);
        self
    }

    /// Configure whether the reverse lazy DFA built for a variable-length lookbehind may exceed
    /// the limit set by [`delegate_dfa_size_limit`](Self::delegate_dfa_size_limit) (or the
    /// default) when it needs a larger minimum cache capacity.
    ///
    /// When `true` (the default), the cache capacity is rounded up to the minimum that the DFA
    /// requires. When `false`, the limit is strictly honoured and a pattern whose DFA needs more
    /// capacity fails to compile with [`CompileError::DfaBuildError`]. Like
    /// [`RegexSetOptions::overlapping_dfa_skip_cache_capacity_check`], this only affects DFAs
    /// that fancy-regex builds itself; ordinary delegated sub-regexes are unaffected.
    pub fn delegate_dfa_skip_cache_capacity_check(&mut self, yes: bool) -> &mut Self {
        self.options.delegate_dfa_skip_cache_capacity_check = yes;
        self
    }

    /// Set the maximum number of VM instructions the compiled program may
    /// contain. Subroutine calls are inlined at compile time, so a self- or
    /// mutually-recursive pattern can expand without bound even while the
    /// recursion-depth cap is respected; this bounds the emitted instruction
    /// vector instead. If exceeded, compilation returns
    /// `CompileError::PatternTooComplex`. `None` (the default) disables the
    /// check, preserving the previous behavior.
    pub fn max_prog_size(&mut self, limit: usize) -> &mut Self {
        self.options.max_prog_size = Some(limit);
        self
    }

    /// Require that matches are non-empty (i.e. match at least one character).
    ///
    /// When this is enabled, any match attempt that would result in a zero-length match is
    /// rejected.
    ///
    /// Default is `false`.
    ///
    /// N.B. When `find_not_empty` is set and analysis determines the pattern will only ever
    /// produce an empty match, compiling the regex will return
    /// `CompileError::PatternCanNeverMatch` instead of silently constructing a regex that can never
    /// return a result. This catches the user error at compile time rather than allowing the
    /// combination to execute pointlessly at runtime.
    pub fn find_not_empty(&mut self, yes: bool) -> &mut Self {
        self.options.hard_regex_runtime_options.find_not_empty = yes;
        self
    }

    /// Enable leftmost-longest match semantics.
    ///
    /// When enabled, among all matches starting at the leftmost possible position,
    /// the longest match is returned. This contrasts with the default leftmost-first
    /// (PCRE-style) semantics.
    ///
    /// Default is `false`.
    ///
    /// **Note:** This requires the `leftmost_longest` feature to be enabled.
    #[cfg(feature = "leftmost_longest")]
    pub fn leftmost_longest(&mut self, yes: bool) -> &mut Self {
        self.options.leftmost_longest = yes;
        self.options.hard_regex_runtime_options.leftmost_longest = yes;
        self
    }

    /// Treat unnamed capture groups as non-capturing when named groups exist.
    /// Prevents accessing capture groups by number from within the pattern
    /// (backrefs, subroutine calls) when named groups are present.
    pub fn ignore_numbered_groups_when_named_groups_exist(&mut self, yes: bool) -> &mut Self {
        self.options.ignore_numbered_groups_when_named_groups_exist = yes;
        self
    }

    /// ⚠️ Experimental: This API may change, be removed without notice, or cause matches to be
    /// skipped. This requires more real-world testing to prove correctness and observe in which
    /// circumstances it brings performance benefits and in which it has the opposite effect.
    /// Feedback (and benchmarks on real-world patterns/haystacks) would be very welcome!
    ///
    /// Enable the Seek pre-filter optimization for hard (backtracking) patterns.
    ///
    /// When enabled, the compiler attempts to derive a regular approximation of the pattern
    /// which is used to skip to the earliest plausible match position in the haystack before
    /// invoking the backtracking VM. This can dramatically speed up searches in long haystacks
    /// when the pattern can only match at infrequent positions.
    ///
    /// The seek pattern is always a conservative over-approximation — it may report false-positive
    /// positions but will never skip a true match.
    ///
    /// When `yes` is `true`, uses the default [`seek_pattern_is_useful`] filter to decide
    /// whether the derived pattern is worth using. When `false`, disables seek entirely.
    ///
    /// To supply a custom filter, use [`seek_filter`](Self::seek_filter) instead.
    pub fn seek(&mut self, yes: bool) -> &mut Self {
        self.options.seek_filter = if yes {
            Some(seek_pattern_is_useful)
        } else {
            None
        };
        self
    }

    /// Set a custom filter function that decides whether the derived seek pattern is useful.
    ///
    /// The function receives the seek pattern string and returns `true` if the pattern should
    /// be used as a pre-filter, or `false` to fall back to the standard unanchored search.
    ///
    /// Calling this method implicitly enables seek. Pass [`seek_pattern_is_useful`] to restore
    /// the default behavior.
    ///
    /// # Example
    ///
    /// ```rust
    /// use fancy_regex::{RegexOptionsBuilder, seek_pattern_is_useful};
    ///
    /// // Use the default filter (equivalent to seek(true))
    /// let mut builder = RegexOptionsBuilder::new();
    /// builder.seek_filter(seek_pattern_is_useful);
    ///
    /// // Use a custom filter that additionally requires the pattern to be longer than 3 bytes
    /// let mut builder = RegexOptionsBuilder::new();
    /// builder.seek_filter(|pat| pat.len() > 3 && seek_pattern_is_useful(pat));
    /// ```
    pub fn seek_filter(&mut self, filter: fn(&str) -> bool) -> &mut Self {
        self.options.seek_filter = Some(filter);
        self
    }

    /// Attempts to better match [Oniguruma](https://github.com/kkos/oniguruma)'s default parsing behavior
    ///
    /// Currently this amounts to changing behavior with:
    ///
    /// # Left and right word bounds
    ///
    /// `fancy-regex` follows the default of other regex engines such as the `regex` crate itself
    /// where `\<` and `\>` correspond to a _left_ and _right_ word-bound respectively. This
    /// differs from Oniguruma's defaults which treat them as matching the literals `<` and `>`.
    /// When this option is set using `\<` and `\>` in the pattern will match the literals
    /// `<` and `>` instead of word bounds.
    ///
    /// # Repetition/Quantifiers on empty groups
    ///
    /// `fancy-regex` would normally reject patterns like `(?:)+` because the `+` has nothing
    /// to target. In Oniguruma mode, the empty repeat is silently dropped at parse time.
    ///
    /// # Swapped order quantifiers
    ///
    /// `fancy-regex` would normally treat `x{0,3}` as a syntax error because the minimum is
    /// greater than the maximum. In Oniguruma mode, the limits are swapped (behaving as
    /// `x{3,0}`) and the resulting repeat is treated as atomic (possessive), matching
    /// Oniguruma's behavior.
    ///
    /// # Adjacent quantifiers
    ///
    /// `fancy-regex` would normally reject adjacent quantifiers like `a{3}{2}`, treating the
    /// second as having nothing to target. In Oniguruma mode, each quantifier wraps the
    /// previous result (e.g. `a{3}{2}` becomes `(?:a{3}){2}`).
    ///
    /// Outside Oniguruma mode, `+` after `{...}` is a possessive modifier (e.g. `x{2}+` is
    /// possessive). In Oniguruma mode, `+` after a user-specified `{...}` repeat is treated
    /// as another repeat modifier — `x{2}+` is equivalent to `(?:x{2})+`.
    ///
    /// # Start of line (`^`) in multiline mode
    ///
    /// In multiline mode (`(?m)`), `^` normally matches at the start of the input and after
    /// any newline. In Oniguruma mode, it additionally rejects a match at the absolute end of
    /// the input when it is preceded by a trailing newline.
    /// Inside lookarounds, `^` behaves as a plain line start without the end-of-input rejection.
    ///
    /// ## Example
    ///
    /// ```
    /// use fancy_regex::{Regex, RegexBuilder};
    ///
    /// let haystack = "turbo::<Fish>";
    /// let regex = r"\<\w*\>";
    ///
    /// // By default `\<` and `\>` will match the start and end of a word boundary
    /// let word_bounds_regex = Regex::new(regex).unwrap();
    /// let word_bounds = word_bounds_regex.find(haystack).unwrap().unwrap();
    /// assert_eq!(word_bounds.as_str(), "turbo");
    ///
    /// // With the option set they instead match the literal `<` and `>` characters
    /// let literals_regex = RegexBuilder::new(regex).oniguruma_mode(true).build().unwrap();
    /// let literals = literals_regex.find(haystack).unwrap().unwrap();
    /// assert_eq!(literals.as_str(), "<Fish>");
    /// ```
    pub fn oniguruma_mode(&mut self, yes: bool) -> &mut Self {
        self.options.oniguruma_mode = yes;
        self
    }

    /// Reject malformed repeats beginning with a decimal digit after `{`.
    /// The default keeps fancy-regex's literal-brace behavior.
    pub fn strict_numeric_repeat(&mut self, yes: bool) -> &mut Self {
        self.options.strict_numeric_repeat = yes;
        self
    }

    /// Reject `[[:<:]]` and `[[:>:]]` instead of treating them as classes.
    pub fn reject_posix_word_boundary(&mut self, yes: bool) -> &mut Self {
        self.options.reject_posix_word_boundary = yes;
        self
    }

    /// Set the input encoding mode for the regex.
    ///
    /// Controls how the regex engine handles input encoding. See [`BytesMode`]
    /// for details on each variant.
    ///
    /// Default is [`BytesMode::Unicode`].
    ///
    /// # Example
    ///
    /// ```rust
    /// use fancy_regex::{BytesMode, RegexBuilder};
    ///
    /// // ASCII bytes mode: . matches any byte including non-UTF-8
    /// let re = RegexBuilder::new(r".+")
    ///     .bytes_mode(BytesMode::Ascii)
    ///     .build()
    ///     .unwrap();
    /// assert!(re.is_match(b"\x80\x81\x82").unwrap());
    ///
    /// // Default Unicode mode: . only matches valid UTF-8 codepoints
    /// let re = RegexBuilder::new(r".+")
    ///     .build()
    ///     .unwrap();
    /// assert!(!re.is_match(b"\x80\x81\x82").unwrap());
    /// ```
    pub fn bytes_mode(&mut self, mode: BytesMode) -> &mut Self {
        self.options.bytes_mode = mode;
        self
    }

    /// Sometimes you want to pass in a haystack containing a trailing newline,
    /// and have that last position ignored for the purposes of anchors like ^ and $
    /// and even for lookarounds, unless \z is specifically used to anchor to the end of the string.
    /// This is how Oniguruma works for example.
    pub fn disallow_empty_match_at_eof_after_newline(&mut self, yes: bool) -> &mut Self {
        self.options
            .hard_regex_runtime_options
            .disallow_empty_match_at_eof_after_newline = yes;
        self
    }

    /// Allow [`RegexInput`] assertion suppression overrides at runtime.
    ///
    /// When enabled, patterns containing `\A` and `\z` are treated as hard and compiled to the VM
    /// so that [`RegexInput::start_text`] and [`RegexInput::end_text`] can suppress those
    /// assertions.
    ///
    /// When disabled (the default), those runtime overrides are ignored.
    pub fn allow_input_assertion_overrides(&mut self, yes: bool) -> &mut Self {
        self.options
            .hard_regex_runtime_options
            .allow_input_assertion_overrides = yes;
        self
    }

    /// Whether to build a prefilter for this Regex.
    /// If you only do anchored search, the prefilter would not be used and would just waste
    /// time and memory to build it.
    pub fn build_delegate_prefilter(&mut self, yes: bool) -> &mut Self {
        self.options.delegate_prefilter = yes;
        self
    }

    /// Computes the bytes a match of `pattern` can start with, without compiling it.
    /// The possible return values are:
    ///
    /// - `Ok(Some(set))`: a byte set could be built and you can quickly check whether a byte
    ///   could match the pattern without calling the regex
    /// - `Ok(None)`: we couldn't built a byte set from the pattern so any byte could match
    /// - `Err(..)`: the pattern is invalid
    ///
    /// This is useful if you are building your own prefilter.
    ///
    /// ```
    /// # use fancy_regex::RegexOptionsBuilder;
    /// let builder = RegexOptionsBuilder::new();
    /// let set = builder.start_bytes(r"fn|let|impl").unwrap().unwrap();
    /// assert_eq!(set.iter().collect::<Vec<_>>(), vec![b'f', b'i', b'l']);
    /// assert!(builder.start_bytes(r"a?").unwrap().is_none());
    /// ```
    pub fn start_bytes(&self, pattern: &str) -> Result<Option<ByteSet>> {
        crate::byte_set::start_bytes(pattern, &self.options)
    }

    /// Returns the [ByteSet] for the required bytes of the given pattern.
    /// For example, a pattern `a?b` will unconditionally require `b` to be found in the
    /// haystack. This means that for a haystack like `aaa` the pattern will _never_ match and
    /// we can know that without even compiling the regex.
    /// An empty set means there's nothing actionable.
    ///
    /// ```
    /// # use fancy_regex::RegexOptionsBuilder;
    /// let builder = RegexOptionsBuilder::new();
    /// let set = builder.required_bytes(r"a?b").unwrap();
    /// assert_eq!(set.iter().collect::<Vec<_>>(), vec![b'b']);
    /// ```
    pub fn required_bytes(&self, pattern: &str) -> Result<ByteSet> {
        crate::byte_set::required_bytes(pattern, &self.options)
    }
}

impl RegexBuilder {
    /// Create a new regex builder.
    pub fn new(pattern: &str) -> Self {
        RegexBuilder {
            pattern: pattern.to_string(),
            options: RegexOptionsBuilder::new(),
        }
    }

    /// Build a `Regex` from the given pattern.
    ///
    /// Returns an [`Error`](enum.Error.html) if the pattern could not be parsed.
    pub fn build(&self) -> Result<Regex> {
        self.options.build(self.pattern.clone())
    }

    /// Change the pattern to build. Useful when building multiple regexes from
    /// many patterns.
    pub fn pattern(&mut self, pattern: String) -> &mut Self {
        self.pattern = pattern;
        self
    }

    /// See [`RegexOptionsBuilder::case_insensitive`]
    pub fn case_insensitive(&mut self, yes: bool) -> &mut Self {
        self.options.case_insensitive(yes);
        self
    }

    /// See [`RegexOptionsBuilder::multi_line`]
    pub fn multi_line(&mut self, yes: bool) -> &mut Self {
        self.options.multi_line(yes);
        self
    }

    /// See [`RegexOptionsBuilder::ignore_whitespace`]
    pub fn ignore_whitespace(&mut self, yes: bool) -> &mut Self {
        self.options.ignore_whitespace(yes);
        self
    }

    /// See [`RegexOptionsBuilder::dot_matches_new_line`]
    pub fn dot_matches_new_line(&mut self, yes: bool) -> &mut Self {
        self.options.dot_matches_new_line(yes);
        self
    }

    /// See [`RegexOptionsBuilder::verbose_mode`]
    pub fn verbose_mode(&mut self, yes: bool) -> &mut Self {
        self.options.ignore_whitespace(yes);
        self
    }

    /// See [`RegexOptionsBuilder::unicode_mode`]
    pub fn unicode_mode(&mut self, yes: bool) -> &mut Self {
        self.options.unicode_mode(yes);
        self
    }

    /// See [`RegexOptionsBuilder::backtrack_limit`]
    pub fn backtrack_limit(&mut self, limit: usize) -> &mut Self {
        self.options.backtrack_limit(limit);
        self
    }

    /// See [`RegexOptionsBuilder::delegate_size_limit`]
    pub fn delegate_size_limit(&mut self, limit: usize) -> &mut Self {
        self.options.delegate_size_limit(limit);
        self
    }

    /// See [`RegexOptionsBuilder::delegate_dfa_size_limit`]
    pub fn delegate_dfa_size_limit(&mut self, limit: usize) -> &mut Self {
        self.options.delegate_dfa_size_limit(limit);
        self
    }

    /// See [`RegexOptionsBuilder::delegate_dfa_skip_cache_capacity_check`]
    pub fn delegate_dfa_skip_cache_capacity_check(&mut self, yes: bool) -> &mut Self {
        self.options.delegate_dfa_skip_cache_capacity_check(yes);
        self
    }

    /// See [`RegexOptionsBuilder::max_prog_size`]
    pub fn max_prog_size(&mut self, limit: usize) -> &mut Self {
        self.options.max_prog_size(limit);
        self
    }

    /// See [`RegexOptionsBuilder::oniguruma_mode`]
    pub fn oniguruma_mode(&mut self, yes: bool) -> &mut Self {
        self.options.oniguruma_mode(yes);
        self
    }

    /// See [`RegexOptionsBuilder::strict_numeric_repeat`]
    pub fn strict_numeric_repeat(&mut self, yes: bool) -> &mut Self {
        self.options.strict_numeric_repeat(yes);
        self
    }

    /// See [`RegexOptionsBuilder::reject_posix_word_boundary`]
    pub fn reject_posix_word_boundary(&mut self, yes: bool) -> &mut Self {
        self.options.reject_posix_word_boundary(yes);
        self
    }

    /// See [`RegexOptionsBuilder::bytes_mode`]
    pub fn bytes_mode(&mut self, mode: BytesMode) -> &mut Self {
        self.options.bytes_mode(mode);
        self
    }

    /// See [`RegexOptionsBuilder::crlf`]
    pub fn crlf(&mut self, yes: bool) -> &mut Self {
        self.options.crlf(yes);
        self
    }

    /// See [`RegexOptionsBuilder::find_not_empty`]
    pub fn find_not_empty(&mut self, yes: bool) -> &mut Self {
        self.options.find_not_empty(yes);
        self
    }

    /// See [`RegexOptionsBuilder::leftmost_longest`]
    #[cfg(feature = "leftmost_longest")]
    pub fn leftmost_longest(&mut self, yes: bool) -> &mut Self {
        self.options.leftmost_longest(yes);
        self
    }

    /// See [`RegexOptionsBuilder::ignore_numbered_groups_when_named_groups_exist`]
    pub fn ignore_numbered_groups_when_named_groups_exist(&mut self, yes: bool) -> &mut Self {
        self.options
            .ignore_numbered_groups_when_named_groups_exist(yes);
        self
    }

    /// See [`RegexOptionsBuilder::seek`]
    pub fn seek(&mut self, yes: bool) -> &mut Self {
        self.options.seek(yes);
        self
    }

    /// See [`RegexOptionsBuilder::seek_filter`]
    pub fn seek_filter(&mut self, filter: fn(&str) -> bool) -> &mut Self {
        self.options.seek_filter(filter);
        self
    }

    /// See [`RegexOptionsBuilder::disallow_empty_match_at_eof_after_newline`]
    pub fn disallow_empty_match_at_eof_after_newline(&mut self, yes: bool) -> &mut Self {
        self.options.disallow_empty_match_at_eof_after_newline(yes);
        self
    }

    /// See [`RegexOptionsBuilder::allow_input_assertion_overrides`]
    pub fn allow_input_assertion_overrides(&mut self, yes: bool) -> &mut Self {
        self.options.allow_input_assertion_overrides(yes);
        self
    }
}

impl fmt::Debug for Regex {
    /// Shows the original regular expression.
    fn fmt(&self, f: &mut fmt::Formatter<'_>) -> fmt::Result {
        write!(f, "{}", self.as_str())
    }
}

impl fmt::Display for Regex {
    /// Shows the original regular expression
    fn fmt(&self, f: &mut Formatter<'_>) -> fmt::Result {
        write!(f, "{}", self.as_str())
    }
}

impl FromStr for Regex {
    type Err = Error;

    /// Attempts to parse a string into a regular expression
    fn from_str(s: &str) -> Result<Regex> {
        Regex::new(s)
    }
}

impl Regex {
    /// Parse and compile a regex with default options, see `RegexBuilder`.
    ///
    /// Returns an [`Error`](enum.Error.html) if the pattern could not be parsed.
    pub fn new(re: &str) -> Result<Regex> {
        Self::new_options(re.to_string(), &RegexOptions::default())
    }

    pub(crate) fn new_options(pattern: String, options: &RegexOptions) -> Result<Regex> {
        let mut tree = Expr::parse_tree_with_flags(&pattern, options.compute_flags())?;

        let find_not_empty = options.hard_regex_runtime_options.find_not_empty;
        let disallow_empty_match_at_eof_after_newline = options
            .hard_regex_runtime_options
            .disallow_empty_match_at_eof_after_newline;
        let allow_input_assertion_overrides = options
            .hard_regex_runtime_options
            .allow_input_assertion_overrides;
        #[cfg(feature = "leftmost_longest")]
        let leftmost_longest = options.leftmost_longest;

        let requires_capture_group_fixup = if find_not_empty {
            // if the find_not_empty flag is set, we skip optimizations
            // partially because we have to go though the VM anyway
            // partially because having the last instruction of the expression not have
            // ix be at the end of capture group 0 ruins our empty match checking logic.
            false
        } else {
            // try to optimize the expression tree so that a hard pattern could become easy
            // with a fixup of the capture groups
            optimize(&mut tree)
        };
        let info = analyze(
            &tree,
            AnalyzeContext {
                explicit_capture_group_0: requires_capture_group_fixup,
                find_not_empty,
                disallow_empty_match_at_eof_after_newline,
                allow_input_assertion_overrides,
                #[cfg(feature = "leftmost_longest")]
                leftmost_longest,
            },
        )?;

        if find_not_empty && info.const_size && info.min_size == 0 {
            return Err(CompileError::PatternCanNeverMatch.into());
        }

        if !info.hard {
            // easy case, wrap regex

            // we do our own to_str because escapes are different
            // The cooked form is the pattern plus some flag-group decoration. It is
            // kept even though the engine is normally built from an Hir below,
            // because it doubles as the seek pattern for RegexSet membership and
            // as debug output.
            let mut re_cooked = String::with_capacity(pattern.len() + pattern.len() / 2);
            tree.expr.to_str(&mut re_cooked, 0);
            let compile_options = CompileOptions {
                bytes_mode: options.bytes_mode,
                unicode: options.syntaxc.get_unicode()
                    && !matches!(options.bytes_mode, BytesMode::Ascii),
                delegate_size_limit: options.delegate_size_limit,
                delegate_dfa_size_limit: options.delegate_dfa_size_limit,
                delegate_dfa_skip_cache_capacity_check: options
                    .delegate_dfa_skip_cache_capacity_check,
                // The remaining fields (anchored, contains_subroutines, seek_filter,
                // disallow_empty_match_at_eof_after_newline) are irrelevant for a plain
                // delegate compile in the easy path and their defaults are correct.
                ..CompileOptions::default()
            };
            // The whole pattern is delegated and searched unanchored, so it keeps
            // its prefilter and all capture groups (the user may request captures).
            // RegexSet members are the exception: they are only searched anchored,
            // so their prefilter build is skipped (see `delegate_prefilter`).
            let usage = if options.delegate_prefilter {
                compile::DelegateUsage::unanchored()
            } else {
                compile::DelegateUsage::unanchored_no_prefilter()
            };
            // Build the engine from an Hir translated directly from the tree, so
            // the engine doesn't re-parse the cooked pattern. Fall back to the
            // string path for anything the translator doesn't cover.
            let utf8 = matches!(compile_options.bytes_mode, BytesMode::Unicode);
            let mut hir_ctx = to_hir::HirCtx::new(compile_options.unicode, utf8);
            hir_ctx.enable_preserve_zero_rep_captures();
            let inner = match to_hir::expr_to_hir(&tree.expr, &mut hir_ctx) {
                Some(hir) => compile::compile_inner_from_hir(&hir, &compile_options, usage)?,
                None => compile::compile_inner(&re_cooked, &compile_options, usage)?,
            };
            return Ok(Regex {
                inner: RegexImpl::Wrap {
                    inner,
                    pattern,
                    explicit_capture_group_0: requires_capture_group_fixup,
                    delegated_pattern: re_cooked,
                    zero_rep_groups: hir_ctx.into_zero_rep_groups(),
                },
                named_groups: Arc::new(tree.named_groups),
            });
        }

        let prog = compile(
            &info,
            CompileOptions {
                anchored: can_compile_as_anchored(&tree.expr),
                contains_subroutines: tree.contains_subroutines,
                seek_filter: options.seek_filter,
                disallow_empty_match_at_eof_after_newline,
                bytes_mode: options.bytes_mode,
                unicode: options.syntaxc.get_unicode()
                    && !matches!(options.bytes_mode, BytesMode::Ascii),
                delegate_size_limit: options.delegate_size_limit,
                delegate_dfa_size_limit: options.delegate_dfa_size_limit,
                delegate_dfa_skip_cache_capacity_check: options
                    .delegate_dfa_skip_cache_capacity_check,
                max_prog_size: options.max_prog_size,
            },
        )?;
        Ok(Regex {
            inner: RegexImpl::Fancy {
                prog: Arc::new(prog),
                n_groups: info.end_group(),
                options: options.hard_regex_runtime_options,
                pattern,
            },
            named_groups: Arc::new(tree.named_groups),
        })
    }

    /// Returns the original string of this regex.
    pub fn as_str(&self) -> &str {
        match &self.inner {
            RegexImpl::Wrap { pattern, .. } => pattern,
            RegexImpl::Fancy { pattern, .. } => pattern,
        }
    }

    /// Check if the regex matches the input.
    ///
    /// Accepts any type implementing [`Input`]: `&str`, `&String`, or `&[u8]`.
    ///
    /// # Example
    ///
    /// Test if some text contains the same word twice:
    ///
    /// ```rust
    /// # use fancy_regex::Regex;
    ///
    /// let re = Regex::new(r"(\w+) \1").unwrap();
    /// assert!(re.is_match("mirror mirror on the wall").unwrap());
    /// ```
    ///
    /// Match against raw bytes:
    ///
    /// ```rust
    /// # use fancy_regex::{BytesMode, RegexBuilder};
    ///
    /// let re = RegexBuilder::new(r"\d+")
    ///     .bytes_mode(BytesMode::Ascii)
    ///     .build()
    ///     .unwrap();
    /// assert!(re.is_match(b"abc 123").unwrap());
    /// ```
    pub fn is_match<S: input::Input + ?Sized>(&self, input: &S) -> Result<bool> {
        self.is_match_input(RegexInput::new(input))
    }

    /// Returns true if and only if this regex matches anywhere in the given
    /// search input.
    pub fn is_match_input<S: input::Input + ?Sized>(
        &self,
        input: RegexInput<'_, S>,
    ) -> Result<bool> {
        match &self.inner {
            RegexImpl::Wrap { inner, .. } => {
                if input.is_done() {
                    Ok(false)
                } else {
                    Ok(inner.is_match(ra_input(&input)))
                }
            }
            RegexImpl::Fancy { .. } => self.find_input_raw(&input, 0).map(|m| m.is_some()),
        }
    }

    /// Returns an iterator for each successive non-overlapping match in `text`.
    ///
    /// If you have capturing groups in your regex that you want to extract, use the [Regex::captures_iter()]
    /// method.
    ///
    /// # Example
    ///
    /// Find all words followed by an exclamation point:
    ///
    /// ```rust
    /// # use fancy_regex::Regex;
    ///
    /// let re = Regex::new(r"\w+(?=!)").unwrap();
    /// let mut matches = re.find_iter("so fancy! even with! iterators!");
    /// assert_eq!(matches.next().unwrap().unwrap().as_str(), "fancy");
    /// assert_eq!(matches.next().unwrap().unwrap().as_str(), "with");
    /// assert_eq!(matches.next().unwrap().unwrap().as_str(), "iterators");
    /// assert!(matches.next().is_none());
    /// ```
    pub fn find_iter<'r, 't, S: input::Input + ?Sized>(
        &'r self,
        text: &'t S,
    ) -> Matches<'r, 't, S> {
        self.find_iter_input(RegexInput::new(text))
    }

    /// Returns an iterator for each successive non-overlapping match in the
    /// given search input.
    pub fn find_iter_input<'r, 't, S: input::Input + ?Sized>(
        &'r self,
        input: RegexInput<'t, S>,
    ) -> Matches<'r, 't, S> {
        Matches {
            re: self,
            input,
            last_match: None,
            last_skipped_empty: false,
        }
    }

    /// Find the first match in the input.
    ///
    /// Accepts any type implementing [`Input`]: `&str`, `&String`, or `&[u8]`.
    ///
    /// # Example
    ///
    /// Find a word that is followed by an exclamation point:
    ///
    /// ```rust
    /// # use fancy_regex::Regex;
    ///
    /// let re = Regex::new(r"\w+(?=!)").unwrap();
    /// assert_eq!(re.find("so fancy!").unwrap().unwrap().as_str(), "fancy");
    /// ```
    pub fn find<'t, S: input::Input + ?Sized>(&self, input: &'t S) -> Result<Option<S::Match<'t>>> {
        self.find_input(RegexInput::new(input))
    }

    /// Find the first match in the given search input.
    pub fn find_input<'t, S: input::Input + ?Sized>(
        &self,
        input: RegexInput<'t, S>,
    ) -> Result<Option<S::Match<'t>>> {
        Ok(self
            .find_input_raw(&input, 0)?
            .map(|(s, e)| input.haystack().make_match(s, e)))
    }

    /// Returns the first match in `input`, starting from the specified byte position `pos`.
    ///
    /// # Examples
    ///
    /// Finding match starting at a position:
    ///
    /// ```
    /// # use fancy_regex::Regex;
    /// let re = Regex::new(r"(?m:^)(\d+)").unwrap();
    /// let text = "1 test 123\n2 foo";
    /// let mat = re.find_from_pos(text, 7).unwrap().unwrap();
    ///
    /// assert_eq!(mat.start(), 11);
    /// assert_eq!(mat.end(), 12);
    /// ```
    ///
    /// Note that in some cases this is not the same as using the `find`
    /// method and passing a slice of the string, see [Regex::captures_from_pos()]
    /// for details. To constrain matching to a byte range without slicing, use
    /// [Regex::find_input()] with [`RegexInput`].
    pub fn find_from_pos<'t, S: input::Input + ?Sized>(
        &self,
        input: &'t S,
        pos: usize,
    ) -> Result<Option<S::Match<'t>>> {
        self.find_input(RegexInput::new(input).from_pos(pos))
    }

    pub(crate) fn find_input_raw<S: input::Input + ?Sized>(
        &self,
        input: &RegexInput<'_, S>,
        option_flags: u32,
    ) -> Result<Option<(usize, usize)>> {
        if input.is_done() {
            return Ok(None);
        }
        match &self.inner {
            RegexImpl::Wrap {
                inner,
                explicit_capture_group_0,
                ..
            } => {
                let mut delegated_input = ra_input(input);
                if input.is_anchored() {
                    delegated_input = delegated_input.anchored(RaAnchored::Yes);
                }
                let result = if !*explicit_capture_group_0 {
                    inner.search(&delegated_input).map(|m| (m.start(), m.end()))
                } else {
                    // Only group 1's span is needed (the real match bounds of
                    // the rewritten pattern); a fixed 4-slot search avoids
                    // allocating full captures on every find.
                    let mut slots = [None; 4];
                    if inner.search_slots(&delegated_input, &mut slots).is_some() {
                        slots[2]
                            .zip(slots[3])
                            .map(|(start, end)| (start.get(), end.get()))
                    } else {
                        None
                    }
                };
                Ok(result)
            }
            RegexImpl::Fancy { prog, options, .. } => {
                #[allow(unused_mut)]
                let mut option_flags = option_flags
                    | if options.find_not_empty {
                        OPTION_FIND_NOT_EMPTY
                    } else {
                        0
                    };
                #[cfg(feature = "leftmost_longest")]
                {
                    option_flags |= if options.leftmost_longest {
                        OPTION_LEFTMOST_LONGEST
                    } else {
                        0
                    };
                }
                // Span-only VM entry: nothing is moved out of the pooled
                // scratch, so this path is allocation-free per call.
                vm::run_spans(prog, input, option_flags, options)
            }
        }
    }

    /// Build a `Captures` value containing only group 0 for the given span.
    ///
    /// This is used by `RegexSet` as a fast path for patterns without capture
    /// groups, where we only need to preserve the overall match range.
    ///
    /// The caller must pass a valid `start..end` match span for `input`.
    /// Behavior is otherwise undefined for APIs reading the resulting captures.
    pub(crate) fn captures_for_span<'t, S: input::Input + ?Sized>(
        &self,
        input: &'t S,
        start: usize,
        end: usize,
    ) -> Captures<'t, S> {
        Captures {
            inner: CapturesImpl::Fancy {
                saves: vec![start, end],
            },
            named_groups: self.named_groups.clone(),
            input,
        }
    }

    /// Returns an iterator over all the non-overlapping capture groups matched in `text`.
    ///
    /// # Examples
    ///
    /// Finding all matches and capturing parts of each:
    ///
    /// ```rust
    /// # use fancy_regex::Regex;
    ///
    /// let re = Regex::new(r"(\d{4})-(\d{2})").unwrap();
    /// let text = "It was between 2018-04 and 2020-01";
    /// let mut all_captures = re.captures_iter(text);
    ///
    /// let first = all_captures.next().unwrap().unwrap();
    /// assert_eq!(first.get(1).unwrap().as_str(), "2018");
    /// assert_eq!(first.get(2).unwrap().as_str(), "04");
    /// assert_eq!(first.get(0).unwrap().as_str(), "2018-04");
    ///
    /// let second = all_captures.next().unwrap().unwrap();
    /// assert_eq!(second.get(1).unwrap().as_str(), "2020");
    /// assert_eq!(second.get(2).unwrap().as_str(), "01");
    /// assert_eq!(second.get(0).unwrap().as_str(), "2020-01");
    ///
    /// assert!(all_captures.next().is_none());
    /// ```
    pub fn captures_iter<'r, 't, S: input::Input + ?Sized>(
        &'r self,
        text: &'t S,
    ) -> CaptureMatches<'r, 't, S> {
        self.captures_iter_input(RegexInput::new(text))
    }

    /// Returns an iterator over all the non-overlapping capture groups matched
    /// in the given search input.
    pub fn captures_iter_input<'r, 't, S: input::Input + ?Sized>(
        &'r self,
        input: RegexInput<'t, S>,
    ) -> CaptureMatches<'r, 't, S> {
        CaptureMatches(self.find_iter_input(input))
    }

    /// Returns the capture groups for the first match in `text`.
    ///
    /// If no match is found, then `Ok(None)` is returned.
    ///
    /// # Examples
    ///
    /// Finding matches and capturing parts of the match:
    ///
    /// ```rust
    /// # use fancy_regex::Regex;
    ///
    /// let re = Regex::new(r"(\d{4})-(\d{2})-(\d{2})").unwrap();
    /// let text = "The date was 2018-04-07";
    /// let captures = re.captures(text).unwrap().unwrap();
    ///
    /// assert_eq!(captures.get(1).unwrap().as_str(), "2018");
    /// assert_eq!(captures.get(2).unwrap().as_str(), "04");
    /// assert_eq!(captures.get(3).unwrap().as_str(), "07");
    /// assert_eq!(captures.get(0).unwrap().as_str(), "2018-04-07");
    /// ```
    pub fn captures<'t, S: input::Input + ?Sized>(
        &self,
        text: &'t S,
    ) -> Result<Option<Captures<'t, S>>> {
        self.captures_input(RegexInput::new(text))
    }

    /// Returns the capture groups for the first match in the given search
    /// input.
    pub fn captures_input<'t, S: input::Input + ?Sized>(
        &self,
        input: RegexInput<'t, S>,
    ) -> Result<Option<Captures<'t, S>>> {
        self.captures_input_with_option_flags(&input, 0)
    }

    /// Returns the capture groups for the first match in `text`, starting from
    /// the specified byte position `pos`.
    ///
    /// # Examples
    ///
    /// Finding captures starting at a position:
    ///
    /// ```
    /// # use fancy_regex::Regex;
    /// let re = Regex::new(r"(?m:^)(\d+)").unwrap();
    /// let text = "1 test 123\n2 foo";
    /// let captures = re.captures_from_pos(text, 7).unwrap().unwrap();
    ///
    /// let group = captures.get(1).unwrap();
    /// assert_eq!(group.as_str(), "2");
    /// assert_eq!(group.start(), 11);
    /// assert_eq!(group.end(), 12);
    /// ```
    ///
    /// Note that in some cases this is not the same as using the `captures`
    /// method and passing a slice of the string, see the capture that we get
    /// when we do this:
    ///
    /// ```
    /// # use fancy_regex::Regex;
    /// let re = Regex::new(r"(?m:^)(\d+)").unwrap();
    /// let text = "1 test 123\n2 foo";
    /// let captures = re.captures(&text[7..]).unwrap().unwrap();
    /// assert_eq!(captures.get(1).unwrap().as_str(), "123");
    /// ```
    ///
    /// This matched the number "123" because it's at the beginning of the text
    /// of the string slice.
    ///
    /// To constrain matching to a byte range without slicing, use
    /// [Regex::captures_input()] with [`RegexInput`].
    ///
    pub fn captures_from_pos<'t, S: input::Input + ?Sized>(
        &self,
        text: &'t S,
        pos: usize,
    ) -> Result<Option<Captures<'t, S>>> {
        self.captures_input(RegexInput::new(text).from_pos(pos))
    }

    pub(crate) fn captures_input_with_option_flags<'t, S: input::Input + ?Sized>(
        &self,
        input: &RegexInput<'t, S>,
        option_flags: u32,
    ) -> Result<Option<Captures<'t, S>>> {
        if input.is_done() {
            return Ok(None);
        }
        let named_groups = self.named_groups.clone();
        let haystack = input.haystack();
        match &self.inner {
            RegexImpl::Wrap {
                inner,
                explicit_capture_group_0,
                zero_rep_groups,
                ..
            } => {
                // find_not_empty patterns are always compiled as Fancy, so find_not_empty is
                // always false here.
                let explicit = *explicit_capture_group_0;
                let zero_rep_groups = zero_rep_groups.clone();
                let mut locations = inner.create_captures();
                let mut delegated_input = ra_input(input);
                if input.is_anchored() {
                    delegated_input = delegated_input.anchored(RaAnchored::Yes);
                }
                inner.captures(delegated_input, &mut locations);
                Ok(locations.is_match().then_some(Captures {
                    inner: CapturesImpl::Wrap {
                        locations,
                        explicit_capture_group_0: explicit,
                        zero_rep_groups,
                    },
                    named_groups,
                    input: haystack,
                }))
            }
            RegexImpl::Fancy {
                prog,
                n_groups,
                options,
                ..
            } => {
                #[allow(unused_mut)]
                let mut option_flags = option_flags
                    | if options.find_not_empty {
                        OPTION_FIND_NOT_EMPTY
                    } else {
                        0
                    };
                #[cfg(feature = "leftmost_longest")]
                {
                    option_flags |= if options.leftmost_longest {
                        OPTION_LEFTMOST_LONGEST
                    } else {
                        0
                    };
                }
                let result = vm::run(prog, input, option_flags, options)?;
                Ok(result.map(|mut saves| {
                    saves.truncate(n_groups * 2);
                    Captures {
                        inner: CapturesImpl::Fancy { saves },
                        named_groups,
                        input: haystack,
                    }
                }))
            }
        }
    }

    pub(crate) fn seek_pattern(&self) -> &str {
        match &self.inner {
            RegexImpl::Wrap {
                delegated_pattern, ..
            } => delegated_pattern,
            RegexImpl::Fancy { prog, .. } => &prog.seek_pattern,
        }
    }

    /// Returns the number of captures, including the implicit capture of the entire expression.
    pub fn captures_len(&self) -> usize {
        match &self.inner {
            RegexImpl::Wrap {
                inner,
                explicit_capture_group_0,
                ..
            } => inner.captures_len() - if *explicit_capture_group_0 { 1 } else { 0 },
            RegexImpl::Fancy { n_groups, .. } => *n_groups,
        }
    }

    /// Returns an iterator over the capture names.
    pub fn capture_names(&self) -> CaptureNames<'_> {
        let mut names = Vec::new();
        names.resize(self.captures_len(), None);
        for (name, groups) in self.named_groups.iter() {
            for &i in groups {
                if let Some(slot) = names.get_mut(i) {
                    *slot = Some(name.as_str());
                }
            }
        }
        CaptureNames(names.into_iter())
    }

    // for debugging only
    #[doc(hidden)]
    pub fn debug_print(&self, writer: &mut Formatter<'_>) -> fmt::Result {
        match &self.inner {
            RegexImpl::Wrap {
                delegated_pattern,
                explicit_capture_group_0,
                ..
            } => {
                write!(
                    writer,
                    "wrapped Regex {:?}, explicit_capture_group_0: {:}",
                    delegated_pattern, *explicit_capture_group_0
                )
            }
            RegexImpl::Fancy { prog, .. } => prog.debug_print(writer),
        }
    }

    /// Replaces the leftmost-first match with the replacement provided.
    /// The replacement can be a regular string (where `$N` and `$name` are
    /// expanded to match capture groups) or a function that takes the matches'
    /// `Captures` and returns the replaced string.
    ///
    /// If no match is found, then a copy of the string is returned unchanged.
    ///
    /// # Replacement string syntax
    ///
    /// All instances of `$name` in the replacement text is replaced with the
    /// corresponding capture group `name`.
    ///
    /// `name` may be an integer corresponding to the index of the
    /// capture group (counted by order of opening parenthesis where `0` is the
    /// entire match) or it can be a name (consisting of letters, digits or
    /// underscores) corresponding to a named capture group.
    ///
    /// If `name` isn't a valid capture group (whether the name doesn't exist
    /// or isn't a valid index), then it is replaced with the empty string.
    ///
    /// The longest possible name is used. e.g., `$1a` looks up the capture
    /// group named `1a` and not the capture group at index `1`. To exert more
    /// precise control over the name, use braces, e.g., `${1}a`.
    ///
    /// To write a literal `$` use `$$`.
    ///
    /// # Examples
    ///
    /// Note that this function is polymorphic with respect to the replacement.
    /// In typical usage, this can just be a normal string:
    ///
    /// ```rust
    /// # use fancy_regex::Regex;
    /// let re = Regex::new("[^01]+").unwrap();
    /// assert_eq!(re.replace("1078910", ""), "1010");
    /// ```
    ///
    /// But anything satisfying the `Replacer` trait will work. For example,
    /// a closure of type `|&Captures| -> String` provides direct access to the
    /// captures corresponding to a match. This allows one to access
    /// capturing group matches easily:
    ///
    /// ```rust
    /// # use fancy_regex::{Regex, Captures};
    /// let re = Regex::new(r"([^,\s]+),\s+(\S+)").unwrap();
    /// let result = re.replace("Springsteen, Bruce", |caps: &Captures<'_, str>| {
    ///     format!("{} {}", &caps[2], &caps[1])
    /// });
    /// assert_eq!(result, "Bruce Springsteen");
    /// ```
    ///
    /// But this is a bit cumbersome to use all the time. Instead, a simple
    /// syntax is supported that expands `$name` into the corresponding capture
    /// group. Here's the last example, but using this expansion technique
    /// with named capture groups:
    ///
    /// ```rust
    /// # use fancy_regex::Regex;
    /// let re = Regex::new(r"(?P<last>[^,\s]+),\s+(?P<first>\S+)").unwrap();
    /// let result = re.replace("Springsteen, Bruce", "$first $last");
    /// assert_eq!(result, "Bruce Springsteen");
    /// ```
    ///
    /// Note that using `$2` instead of `$first` or `$1` instead of `$last`
    /// would produce the same result. To write a literal `$` use `$$`.
    ///
    /// Sometimes the replacement string requires use of curly braces to
    /// delineate a capture group replacement and surrounding literal text.
    /// For example, if we wanted to join two words together with an
    /// underscore:
    ///
    /// ```rust
    /// # use fancy_regex::Regex;
    /// let re = Regex::new(r"(?P<first>\w+)\s+(?P<second>\w+)").unwrap();
    /// let result = re.replace("deep fried", "${first}_$second");
    /// assert_eq!(result, "deep_fried");
    /// ```
    ///
    /// Without the curly braces, the capture group name `first_` would be
    /// used, and since it doesn't exist, it would be replaced with the empty
    /// string.
    ///
    /// Finally, sometimes you just want to replace a literal string with no
    /// regard for capturing group expansion. This can be done by wrapping a
    /// byte string with `NoExpand`:
    ///
    /// ```rust
    /// # use fancy_regex::Regex;
    /// use fancy_regex::NoExpand;
    ///
    /// let re = Regex::new(r"(?P<last>[^,\s]+),\s+(\S+)").unwrap();
    /// let result = re.replace("Springsteen, Bruce", NoExpand("$2 $last"));
    /// assert_eq!(result, "$2 $last");
    /// ```
    pub fn replace<'t, R: Replacer>(&self, text: &'t str, rep: R) -> Cow<'t, str> {
        self.replacen(text, 1, rep)
    }

    /// Replaces all non-overlapping matches in `text` with the replacement
    /// provided. This is the same as calling `replacen` with `limit` set to
    /// `0`.
    ///
    /// See the documentation for `replace` for details on how to access
    /// capturing group matches in the replacement string.
    pub fn replace_all<'t, R: Replacer>(&self, text: &'t str, rep: R) -> Cow<'t, str> {
        self.replacen(text, 0, rep)
    }

    /// Replaces at most `limit` non-overlapping matches in `text` with the
    /// replacement provided. If `limit` is 0, then all non-overlapping matches
    /// are replaced.
    ///
    /// Will panic if any errors are encountered. Use `try_replacen`, which this
    /// function unwraps, if you want to handle errors.
    ///
    /// See the documentation for `replace` for details on how to access
    /// capturing group matches in the replacement string.
    ///
    pub fn replacen<'t, R: Replacer>(&self, text: &'t str, limit: usize, rep: R) -> Cow<'t, str> {
        self.try_replacen(text, limit, rep).unwrap()
    }

    /// Replaces at most `limit` non-overlapping matches in `text` with the
    /// replacement provided. If `limit` is 0, then all non-overlapping matches
    /// are replaced.
    ///
    /// Propagates any errors encountered, such as `RuntimeError::BacktrackLimitExceeded`.
    ///
    /// See the documentation for `replace` for details on how to access
    /// capturing group matches in the replacement string.
    pub fn try_replacen<'t, R: Replacer>(
        &self,
        text: &'t str,
        limit: usize,
        mut rep: R,
    ) -> Result<Cow<'t, str>> {
        // If we know that the replacement doesn't have any capture expansions,
        // then we can fast path. The fast path can make a tremendous
        // difference:
        //
        //   1) We use `find_iter` instead of `captures_iter`. Not asking for
        //      captures generally makes the regex engines faster.
        //   2) We don't need to look up all of the capture groups and do
        //      replacements inside the replacement string. We just push it
        //      at each match and be done with it.
        if let Some(rep) = rep.no_expansion() {
            let mut it = self.find_iter(text).enumerate().peekable();
            if it.peek().is_none() {
                return Ok(Cow::Borrowed(text));
            }
            let mut new = String::with_capacity(text.len());
            let mut last_match = 0;
            for (i, m) in it {
                let m = m?;

                if limit > 0 && i >= limit {
                    break;
                }
                new.push_str(&text[last_match..m.start()]);
                new.push_str(&rep);
                last_match = m.end();
            }
            new.push_str(&text[last_match..]);
            return Ok(Cow::Owned(new));
        }

        // The slower path, which we use if the replacement needs access to
        // capture groups.
        let mut it = self.captures_iter(text).enumerate().peekable();
        if it.peek().is_none() {
            return Ok(Cow::Borrowed(text));
        }
        let mut new = String::with_capacity(text.len());
        let mut last_match = 0;
        for (i, cap) in it {
            let cap = cap?;

            if limit > 0 && i >= limit {
                break;
            }
            // unwrap on 0 is OK because captures only reports matches
            let m = cap.get(0).unwrap();
            new.push_str(&text[last_match..m.start()]);
            rep.replace_append(&cap, &mut new);
            last_match = m.end();
        }
        new.push_str(&text[last_match..]);
        Ok(Cow::Owned(new))
    }

    /// Splits the string by matches of the regex.
    ///
    /// Returns an iterator over the substrings of the target string
    ///  that *aren't* matched by the regex.
    ///
    /// # Example
    ///
    /// To split a string delimited by arbitrary amounts of spaces or tabs:
    ///
    /// ```rust
    /// # use fancy_regex::Regex;
    /// let re = Regex::new(r"[ \t]+").unwrap();
    /// let target = "a b \t  c\td    e";
    /// let fields: Vec<&str> = re.split(target).map(|x| x.unwrap()).collect();
    /// assert_eq!(fields, vec!["a", "b", "c", "d", "e"]);
    /// ```
    pub fn split<'r, 'h>(&'r self, target: &'h str) -> Split<'r, 'h> {
        Split {
            matches: self.find_iter(target),
            next_start: 0,
            target,
        }
    }

    /// Splits the string by matches of the regex at most `limit` times.
    ///
    /// Returns an iterator over the substrings of the target string
    /// that *aren't* matched by the regex.
    ///
    /// The `N`th substring is the remaining part of the target.
    ///
    /// # Example
    ///
    /// To split a string delimited by arbitrary amounts of spaces or tabs
    /// 3 times:
    ///
    /// ```rust
    /// # use fancy_regex::Regex;
    /// let re = Regex::new(r"[ \t]+").unwrap();
    /// let target = "a b \t  c\td    e";
    /// let fields: Vec<&str> = re.splitn(target, 3).map(|x| x.unwrap()).collect();
    /// assert_eq!(fields, vec!["a", "b", "c\td    e"]);
    /// ```
    pub fn splitn<'r, 'h>(&'r self, target: &'h str, limit: usize) -> SplitN<'r, 'h> {
        SplitN {
            splits: self.split(target),
            limit,
        }
    }
}

/// `Display` adapter that prints a [`Regex`]'s internal representation via
/// [`Regex::debug_print`]. Intended for debugging and test output only.
#[doc(hidden)]
#[derive(Debug)]
#[allow(dead_code)]
pub struct DebugRegex<'a>(pub &'a Regex);

impl fmt::Display for DebugRegex<'_> {
    fn fmt(&self, f: &mut Formatter<'_>) -> fmt::Result {
        self.0.debug_print(f)
    }
}

fn ra_input<'a, S: input::Input + ?Sized>(input: &'a RegexInput<'a, S>) -> RaInput<'a> {
    let mut ra_input = RaInput::new(input.haystack().as_bytes()).range(input.get_range());
    ra_input.set_start(input.effective_start());
    ra_input
}

impl TryFrom<&str> for Regex {
    type Error = Error;

    /// Attempts to parse a string into a regular expression
    fn try_from(s: &str) -> Result<Self> {
        Self::new(s)
    }
}

impl TryFrom<String> for Regex {
    type Error = Error;

    /// Attempts to parse a string into a regular expression
    fn try_from(s: String) -> Result<Self> {
        Self::new(&s)
    }
}

impl<'t> Match<'t> {
    /// Returns the starting byte offset of the match in the text.
    #[inline]
    pub fn start(&self) -> usize {
        self.start
    }

    /// Returns the ending byte offset of the match in the text.
    #[inline]
    pub fn end(&self) -> usize {
        self.end
    }

    /// Returns the range over the starting and ending byte offsets of the match in text.
    #[inline]
    pub fn range(&self) -> Range<usize> {
        self.start..self.end
    }

    /// Returns the matched text.
    #[inline]
    pub fn as_str(&self) -> &'t str {
        &self.text[self.start..self.end]
    }

    /// Creates a new match from the given text and byte offsets.
    pub(crate) fn new(text: &'t str, start: usize, end: usize) -> Match<'t> {
        Match { text, start, end }
    }
}

impl<'t> From<Match<'t>> for &'t str {
    fn from(m: Match<'t>) -> &'t str {
        m.as_str()
    }
}

impl<'t> From<Match<'t>> for Range<usize> {
    fn from(m: Match<'t>) -> Range<usize> {
        m.range()
    }
}

#[allow(clippy::len_without_is_empty)] // follow regex's API
impl<'t, S: input::Input + ?Sized> Captures<'t, S> {
    pub(crate) fn get_span(&self, i: usize) -> Option<(usize, usize)> {
        self.inner.get_span(i)
    }

    /// Get the capture group by its index in the regex.
    ///
    /// If there is no match for that group or the index does not correspond to a group, `None` is
    /// returned. The index 0 returns the whole match.
    pub fn get(&self, i: usize) -> Option<S::Match<'t>> {
        self.inner
            .get_span(i)
            .map(|(start, end)| self.input.make_match(start, end))
    }

    /// Returns the match for a named capture group.  Returns `None` the capture
    /// group did not match or if there is no group with the given name.
    pub fn name(&self, name: &str) -> Option<S::Match<'t>> {
        self.named_groups
            .get(name)
            .and_then(|groups| groups.last())
            .and_then(|i| self.get(*i))
    }

    /// Iterate over the captured groups in order in which they appeared in the regex. The first
    /// capture corresponds to the whole match.
    pub fn iter<'c>(&'c self) -> SubCaptureMatches<'c, 't, S> {
        SubCaptureMatches { data: self, i: 0 }
    }

    /// How many groups were captured. This is always at least 1 because group 0 returns the whole
    /// match.
    pub fn len(&self) -> usize {
        self.inner.len()
    }

    /// Returns the byte slice of the entire input that was searched.
    pub fn input_as_bytes(&self) -> &'t [u8] {
        self.input.as_bytes()
    }
}

// str-only methods
impl<'t> Captures<'t, str> {
    /// Expands all instances of `$group` in `replacement` to the corresponding
    /// capture group `name`, and writes them to the `dst` buffer given.
    ///
    /// `group` may be an integer corresponding to the index of the
    /// capture group (counted by order of opening parenthesis where `\0` is the
    /// entire match) or it can be a name (consisting of letters, digits or
    /// underscores) corresponding to a named capture group.
    ///
    /// If `group` isn't a valid capture group (whether the name doesn't exist
    /// or isn't a valid index), then it is replaced with the empty string.
    ///
    /// The longest possible name is used. e.g., `$1a` looks up the capture
    /// group named `1a` and not the capture group at index `1`. To exert more
    /// precise control over the name, use braces, e.g., `${1}a`.
    ///
    /// To write a literal `$`, use `$$`.
    ///
    /// For more control over expansion, see [`Expander`].
    ///
    /// [`Expander`]: expand/struct.Expander.html
    pub fn expand(&self, replacement: &str, dst: &mut String) {
        Expander::default().append_expansion(dst, replacement, self);
    }
}

/// Get a group by index.
///
/// `'t` is the lifetime of the matched text.
///
/// The text can't outlive the `Captures` object if this method is
/// used, because of how `Index` is defined (normally `a[i]` is part
/// of `a` and can't outlive it); to do that, use `get()` instead.
///
/// # Panics
///
/// If there is no group at the given index.
impl<'t> Index<usize> for Captures<'t, str> {
    type Output = str;

    fn index(&self, i: usize) -> &str {
        self.get(i)
            .map(|m| m.as_str())
            .unwrap_or_else(|| panic!("no group at index '{}'", i))
    }
}

/// Get a group by name.
///
/// `'t` is the lifetime of the matched text and `'i` is the lifetime
/// of the group name (the index).
///
/// The text can't outlive the `Captures` object if this method is
/// used, because of how `Index` is defined (normally `a[i]` is part
/// of `a` and can't outlive it); to do that, use `name` instead.
///
/// # Panics
///
/// If there is no group named by the given value.
impl<'t, 'i> Index<&'i str> for Captures<'t, str> {
    type Output = str;

    fn index<'a>(&'a self, name: &'i str) -> &'a str {
        self.name(name)
            .map(|m| m.as_str())
            .unwrap_or_else(|| panic!("no group named '{}'", name))
    }
}

impl<'c, 't, S: input::Input + ?Sized> Iterator for SubCaptureMatches<'c, 't, S> {
    type Item = Option<S::Match<'t>>;

    fn next(&mut self) -> Option<Option<S::Match<'t>>> {
        if self.i < self.data.len() {
            let result = self.data.get(self.i);
            self.i += 1;
            Some(result)
        } else {
            None
        }
    }
}

// TODO: might be nice to implement ExactSizeIterator etc for SubCaptures

/// Regular expression AST. This is public for now but may change.
#[derive(Debug, PartialEq, Eq, Clone)]
pub enum Expr {
    /// An empty expression, e.g. the last branch in `(a|b|)`
    Empty,
    /// Any character, regex `.`
    Any {
        /// Whether it also matches newlines or not
        newline: bool,
        /// Whether CRLF mode is enabled (`\r` also counts as a newline, so dot
        /// excludes both `\r` and `\n`)
        crlf: bool,
    },
    /// An assertion
    Assertion(Assertion),
    /// General newline sequence, `\R`
    /// Matches `\r\n` or any single newline character (\n, \v, \f, \r)
    /// In Unicode mode, also matches U+0085, U+2028, U+2029
    GeneralNewline {
        /// Whether Unicode mode is enabled
        unicode: bool,
    },
    /// The string as a literal, e.g. `a`
    Literal {
        /// The string to match
        val: String,
        /// Whether match is case-insensitive or not
        casei: bool,
    },
    /// A literal consisting of raw bytes, produced by `\xHH` escapes where
    /// the byte value is greater than 0x7F. In Unicode mode these are
    /// re-encoded as UTF-8; in bytes modes they match the exact byte sequence.
    LiteralBytes {
        /// The raw bytes to match
        bytes: Vec<u8>,
    },
    /// Concatenation of multiple expressions, must match in order, e.g. `a.` is a concatenation of
    /// the literal `a` and `.` for any character
    Concat(Vec<Expr>),
    /// Alternative of multiple expressions, one of them must match, e.g. `a|b` is an alternative
    /// where either the literal `a` or `b` must match
    Alt(Vec<Expr>),
    /// Capturing group of expression, e.g. `(a.)` matches `a` and any character and "captures"
    /// (remembers) the match
    Group(Arc<Expr>),
    /// Look-around (e.g. positive/negative look-ahead or look-behind) with an expression, e.g.
    /// `(?=a)` means the next character must be `a` (but the match is not consumed)
    LookAround(Box<Expr>, LookAround),
    /// Repeat of an expression, e.g. `a*` or `a+` or `a{1,3}`
    Repeat {
        /// The expression that is being repeated
        child: Box<Expr>,
        /// The minimum number of repetitions
        lo: usize,
        /// The maximum number of repetitions (or `usize::MAX`)
        hi: usize,
        /// Greedy means as much as possible is matched, e.g. `.*b` would match all of `abab`.
        /// Non-greedy means as little as possible, e.g. `.*?b` would match only `ab` in `abab`.
        greedy: bool,
    },
    /// Delegate a regex to the regex crate. This is used as a simplification so that we don't have
    /// to represent all the expressions in the AST, e.g. character classes.
    ///
    /// **Constraint**: All Delegate expressions must match exactly 1 character. This ensures
    /// consistent analysis and compilation behavior. For zero-width or multi-character patterns,
    /// use the appropriate Expr variants instead (e.g., Assertion, Repeat, Concat).
    Delegate {
        /// The regex
        inner: String,
        /// Whether the matching is case-insensitive or not
        casei: bool,
    },
    /// Back reference to a capture group, e.g. `\1` in `(abc|def)\1` references the captured group
    /// and the whole regex matches either `abcabc` or `defdef`.
    Backref {
        /// The capture group number being referenced
        group: usize,
        /// Whether the matching is case-insensitive or not
        casei: bool,
    },
    /// Back reference to a capture group at the given specified relative recursion level.
    BackrefWithRelativeRecursionLevel {
        /// The capture group number being referenced
        group: usize,
        /// Relative recursion level
        relative_level: isize,
        /// Whether the matching is case-insensitive or not
        casei: bool,
    },
    /// Atomic non-capturing group, e.g. `(?>ab|a)` in text that contains `ab` will match `ab` and
    /// never backtrack and try `a`, even if matching fails after the atomic group.
    AtomicGroup(Box<Expr>),
    /// Keep matched text so far out of overall match
    KeepOut,
    /// Anchor to match at the position where the previous match ended
    ContinueFromPreviousMatchEnd,
    /// Conditional expression based on whether the numbered capture group matched or not.
    /// The optional `relative_recursion_level` qualifies which recursion level's capture is
    /// tested (Oniguruma `(?(name+N)...)` syntax).
    BackrefExistsCondition {
        /// The resolved capture group number
        group: usize,
        /// Optional relative recursion level (e.g. `+0`, `-1`)
        relative_recursion_level: Option<isize>,
    },
    /// If/Then/Else Condition. If there is no Then/Else, these will just be empty expressions.
    Conditional {
        /// The conditional expression to evaluate
        condition: Box<Expr>,
        /// What to execute if the condition is true
        true_branch: Box<Expr>,
        /// What to execute if the condition is false
        false_branch: Box<Expr>,
    },
    /// Subroutine call to the specified group number
    SubroutineCall(usize),
    /// Backtracking control verb
    BacktrackingControlVerb(BacktrackingControlVerb),
    /// Match while the given expression is absent from the haystack
    Absent(Absent),
    /// DEFINE group - defines capture groups for subroutines without matching anything
    /// The expressions inside are parsed and assigned group numbers, but no VM instructions
    /// are generated for the DEFINE block itself.
    DefineGroup {
        /// The expressions/groups being defined
        definitions: Box<Expr>,
    },
    /// Abstract Syntax Tree node - will be resolved into an Expr before analysis.
    /// Contains the position in the pattern where the node was parsed from
    AstNode(AstNode, usize),
}

/// Target of a backreference or subroutine call
#[derive(Debug, PartialEq, Eq, Clone)]
pub enum CaptureGroupTarget {
    /// Direct numbered reference
    ByNumber(usize),

    /// Named reference
    ByName(String),

    /// Relative reference (e.g., -1, -2, etc.)
    Relative(isize),
}

/// Abstract Syntax Tree node - will be resolved into an Expr before analysis
#[derive(Debug, PartialEq, Eq, Clone)]
pub enum AstNode {
    /// Group with optional name - name is only present if explicitly specified in pattern
    AstGroup {
        /// Optional name of the capture group, present only when explicitly named in the pattern
        name: Option<String>,
        /// The inner expression of the group
        inner: Box<Expr>,
    },
    /// Backreference
    Backref {
        /// The target capture group being referenced
        target: CaptureGroupTarget,
        /// Whether the matching is case-insensitive or not
        // TODO: move out of Backref and prefer a Flags AstNode. The resolver can then track the flags and set casei on the resolved Expr accordingly
        casei: bool,
        /// Optional relative recursion level for the backreference
        relative_recursion_level: Option<isize>,
    },
    /// Subroutine Call
    SubroutineCall(CaptureGroupTarget),
    /// Backreference exists condition `(?(name)...)` or `(?(1)...)` - unresolved target.
    /// The optional `relative_recursion_level` corresponds to the Oniguruma `+N`/`-N` suffix
    /// (e.g. `(?(name+0)...)`) which qualifies which recursion level's capture is tested.
    BackrefExistsCondition {
        /// The target capture group being tested for existence
        target: CaptureGroupTarget,
        /// Optional relative recursion level qualifier (e.g. `+0`, `-1`)
        relative_recursion_level: Option<isize>,
    },
}

/// Type of look-around assertion as used for a look-around expression.
#[derive(Debug, PartialEq, Eq, Clone, Copy)]
pub enum LookAround {
    /// Look-ahead assertion, e.g. `(?=a)`
    LookAhead,
    /// Negative look-ahead assertion, e.g. `(?!a)`
    LookAheadNeg,
    /// Look-behind assertion, e.g. `(?<=a)`
    LookBehind,
    /// Negative look-behind assertion, e.g. `(?<!a)`
    LookBehindNeg,
}

/// Type of absent operator as used for Oniguruma's absent functionality.
#[derive(Debug, PartialEq, Eq, Clone)]
pub enum Absent {
    /// Absent repeater `(?~absent)` - works like `\O*` (match any character including newline, repeated)
    /// but is limited by the range that does not include the string match with `absent`.
    /// This is a written abbreviation of `(?~|absent|\O*)`.
    Repeater(Box<Expr>),
    /// Absent expression `(?~|absent|exp)` - works like `exp`, but is limited by the range
    /// that does not include the string match with `absent`.
    Expression {
        /// The expression to avoid matching
        absent: Box<Expr>,
        /// The expression to match
        exp: Box<Expr>,
    },
    /// Absent stopper `(?~|absent)` - after this operator, haystack range is limited
    /// up to the point where `absent` matches.
    Stopper(Box<Expr>),
    /// Range clear `(?~|)` - clears the effects caused by absent stoppers.
    Clear,
}

/// Type of backtracking control verb which affects how backtracking will behave.
/// See <https://www.regular-expressions.info/verb.html>
#[derive(Debug, PartialEq, Eq, Clone, Copy)]
pub enum BacktrackingControlVerb {
    /// Fail this branch immediately
    Fail,
    /// Treat match so far as successful overall match
    Accept,
    /// Abort the entire match on failure
    Commit,
    /// Restart the entire match attempt at the current position
    Skip,
    /// Prune all backtracking states and restart the entire match attempt at the next position
    Prune,
}

/// An iterator over capture names in a [Regex].  The iterator
/// returns the name of each group, or [None] if the group has
/// no name.  Because capture group 0 cannot have a name, the
/// first item returned is always [None].
pub struct CaptureNames<'r>(vec::IntoIter<Option<&'r str>>);

impl Debug for CaptureNames<'_> {
    fn fmt(&self, f: &mut Formatter<'_>) -> fmt::Result {
        f.write_str("<CaptureNames>")
    }
}

impl<'r> Iterator for CaptureNames<'r> {
    type Item = Option<&'r str>;

    fn next(&mut self) -> Option<Self::Item> {
        self.0.next()
    }
}

// silly to write my own, but this is super-fast for the common 1-digit
// case.
fn push_usize(s: &mut String, x: usize) {
    if x >= 10 {
        push_usize(s, x / 10);
        s.push((b'0' + (x % 10) as u8) as char);
    } else {
        s.push((b'0' + (x as u8)) as char);
    }
}

/// Emit a repeat quantifier `?`, `*`, `+`, or `{lo,hi}` (optionally non-greedy) into `buf`.
///
/// This is shared between [`Expr::to_str`] and `build_seek_pattern` in the compiler.
pub(crate) fn write_quantifier(buf: &mut String, lo: usize, hi: usize, greedy: bool) {
    match (lo, hi) {
        (0, 1) => buf.push('?'),
        (0, usize::MAX) => buf.push('*'),
        (1, usize::MAX) => buf.push('+'),
        (lo, hi) => {
            buf.push('{');
            push_usize(buf, lo);
            if lo != hi {
                buf.push(',');
                if hi != usize::MAX {
                    push_usize(buf, hi);
                }
            }
            buf.push('}');
        }
    }
    if !greedy {
        buf.push('?');
    }
}

fn is_special(c: char) -> bool {
    matches!(
        c,
        '\\' | '.' | '+' | '*' | '?' | '(' | ')' | '|' | '[' | ']' | '{' | '}' | '^' | '$' | '#'
    )
}

pub(crate) fn push_quoted(buf: &mut String, s: &str) {
    if !s.bytes().any(|b| {
        b == b'\\'
            || b == b'\n'
            || b == b'\t'
            || b == b'\r'
            || matches!(
                b,
                b'.' | b'+'
                    | b'*'
                    | b'?'
                    | b'('
                    | b')'
                    | b'|'
                    | b'['
                    | b']'
                    | b'{'
                    | b'}'
                    | b'^'
                    | b'$'
                    | b'#'
            )
    }) {
        buf.push_str(s);
        return;
    }

    for c in s.chars() {
        match c {
            '\\' => buf.push_str("\\\\"),
            '\n' => buf.push_str("\\n"),
            '\t' => buf.push_str("\\t"),
            '\r' => buf.push_str("\\r"),
            _ => {
                if is_special(c) {
                    buf.push('\\');
                }
                buf.push(c);
            }
        }
    }
}

/// Escapes special characters in `text` with '\\'.  Returns a string which, when interpreted
/// as a regex, matches exactly `text`.
pub fn escape(text: &str) -> Cow<'_, str> {
    // Using bytes() is OK because all special characters are single bytes.
    match text.bytes().filter(|&b| is_special(b as char)).count() {
        0 => Cow::Borrowed(text),
        n => {
            // The capacity calculation is exact because '\\' is a single byte.
            let mut buf = String::with_capacity(text.len() + n);
            push_quoted(&mut buf, text);
            Cow::Owned(buf)
        }
    }
}

/// Type of assertions
#[derive(Debug, PartialEq, Eq, Clone, Copy)]
pub enum Assertion {
    /// Start of input text
    StartText,
    /// End of input text
    EndText,
    /// End of input text, or before any trailing newlines at the end (Oniguruma's `\Z`)
    EndTextIgnoreTrailingNewlines {
        /// Whether CRLF mode is enabled.
        /// If `true`, trailing `\r\n` pairs (in addition to bare `\n`) are also ignored.
        crlf: bool,
    },
    /// Start of a line
    StartLine {
        /// CRLF mode.
        /// If true, this assertion matches at the starting position of the input text, or at the position immediately
        /// following either a `\r` or `\n` character, but never after a `\r` when a `\n` follows.
        crlf: bool,
    },
    /// Start of a line in Oniguruma mode.
    /// Behaves like [`Assertion::StartLine`], but additionally rejects matches at the end of the input
    /// when it is preceded by a newline.
    StartLineOniguruma {
        /// CRLF mode.
        /// If true, this assertion matches at the starting position of the input text, or at the position immediately
        /// following either a `\r` or `\n` character, but never after a `\r` when a `\n` follows.
        crlf: bool,
    },
    /// End of a line
    EndLine {
        /// CRLF mode
        /// If true, this assertion matches at the ending position of the input text, or at the position immediately
        /// preceding either a `\r` or `\n` character, but never after a `\r` when a `\n` follows.
        crlf: bool,
    },
    /// Left word boundary
    LeftWordBoundary,
    /// Left word half boundary
    LeftWordHalfBoundary,
    /// Right word boundary
    RightWordBoundary,
    /// Right word half boundary
    RightWordHalfBoundary,
    /// Both word boundaries
    WordBoundary,
    /// Not word boundary
    NotWordBoundary,
}

impl Assertion {
    /// Whether this assertion is const-size and must be considered for
    /// delegation. Word boundaries and `\Z` are const-size; everything else
    /// (start/end of line/text) is handled by other analysis branches.
    pub(crate) fn is_always_hard(&self) -> bool {
        use Assertion::*;
        matches!(
            self,
            // these will make regex-automata use PikeVM and are not compabible with certain regex-automata features we use
            LeftWordBoundary
                | LeftWordHalfBoundary
                | RightWordBoundary
                | RightWordHalfBoundary
                | WordBoundary
                | NotWordBoundary
                // `\Z` needs custom trailing-newline handling.
                | EndTextIgnoreTrailingNewlines { .. }
        )
    }

    /// Whether this assertion forces the containing expression onto the VM.
    ///
    /// Word boundaries (`\b`, `\B`, `\<`, `\>`) are only hard inside a
    /// lookbehind: regex-automata's reverse DFA can't run a variable-length
    /// lookbehind, so the whole lookbehind is compiled on the VM. Outside a
    /// lookbehind the engine supports word boundaries, so they can be
    /// delegated — which is what keeps patterns like `\b(?:foo|bar|baz)\b`
    /// a single delegate instead of one branch per alternative. The
    /// half-boundary variants (`\<half`, `\>half`) and `\Z` are always hard:
    /// regex-automata has no half-boundary look, and `\Z` needs custom
    /// trailing-newline handling.
    pub(crate) fn is_hard_in_lookbehind(&self, in_lookbehind: bool) -> bool {
        use Assertion::*;
        match self {
            LeftWordHalfBoundary | RightWordHalfBoundary => true,
            EndTextIgnoreTrailingNewlines { .. } => true,
            LeftWordBoundary | RightWordBoundary | WordBoundary | NotWordBoundary => in_lookbehind,
            _ => true,
        }
    }
}

/// An iterator over the immediate children of an [`Expr`].
///
/// This iterator yields references to child expressions but does not recurse into them.
#[derive(Debug)]
pub enum ExprChildrenIter<'a> {
    /// No children (leaf node)
    Empty,
    /// A single child (Group, LookAround, AtomicGroup, Repeat)
    Single(Option<&'a Expr>),
    /// Multiple children in a Vec (Concat, Alt)
    Vec(alloc::slice::Iter<'a, Expr>),
    /// Three children (Conditional)
    Triple {
        /// First child
        first: Option<&'a Expr>,
        /// Second child
        second: Option<&'a Expr>,
        /// Third child
        third: Option<&'a Expr>,
    },
}

/// An iterator over the immediate children of an [`Expr`] for mutable access.
///
/// This iterator yields mutable references to child expressions but does not recurse into them.
#[derive(Debug)]
pub enum ExprChildrenIterMut<'a> {
    /// No children (leaf node)
    Empty,
    /// A single child (Group, LookAround, AtomicGroup, Repeat)
    Single(Option<&'a mut Expr>),
    /// Multiple children in a Vec (Concat, Alt)
    Vec(alloc::slice::IterMut<'a, Expr>),
    /// Three children (Conditional)
    Triple {
        /// First child
        first: Option<&'a mut Expr>,
        /// Second child
        second: Option<&'a mut Expr>,
        /// Third child
        third: Option<&'a mut Expr>,
    },
}

impl<'a> Iterator for ExprChildrenIter<'a> {
    type Item = &'a Expr;

    fn next(&mut self) -> Option<Self::Item> {
        match self {
            ExprChildrenIter::Empty => None,
            ExprChildrenIter::Single(ref mut child) => child.take(),
            ExprChildrenIter::Vec(ref mut iter) => iter.next(),
            ExprChildrenIter::Triple {
                ref mut first,
                ref mut second,
                ref mut third,
            } => first
                .take()
                .or_else(|| second.take())
                .or_else(|| third.take()),
        }
    }
}

impl<'a> Iterator for ExprChildrenIterMut<'a> {
    type Item = &'a mut Expr;

    fn next(&mut self) -> Option<Self::Item> {
        match self {
            ExprChildrenIterMut::Empty => None,
            ExprChildrenIterMut::Single(ref mut child) => child.take(),
            ExprChildrenIterMut::Vec(ref mut iter) => iter.next(),
            ExprChildrenIterMut::Triple {
                ref mut first,
                ref mut second,
                ref mut third,
            } => first
                .take()
                .or_else(|| second.take())
                .or_else(|| third.take()),
        }
    }
}

macro_rules! children_iter_match {
    ($self:expr, $iter:ident, $vec_method:ident, $single_method:ident, $group_method:ident) => {
        match $self {
            Expr::Concat(children) | Expr::Alt(children) => $iter::Vec(children.$vec_method()),
            Expr::Group(child) => $iter::Single(Some(Arc::$group_method(child))),
            Expr::Absent(Absent::Repeater(child))
            | Expr::Absent(Absent::Stopper(child))
            | Expr::LookAround(child, _)
            | Expr::AtomicGroup(child)
            | Expr::Repeat { child, .. } => $iter::Single(Some(child.$single_method())),
            Expr::Conditional {
                condition,
                true_branch,
                false_branch,
            } => $iter::Triple {
                first: Some(condition.$single_method()),
                second: Some(true_branch.$single_method()),
                third: Some(false_branch.$single_method()),
            },
            Expr::Absent(Absent::Expression { absent, exp }) => $iter::Triple {
                first: Some(absent.$single_method()),
                second: Some(exp.$single_method()),
                third: None,
            },
            Expr::DefineGroup { definitions } => $iter::Single(Some(definitions.$single_method())),
            _ if $self.is_leaf_node() => $iter::Empty,
            _ => unimplemented!(),
        }
    };
}
impl Expr {
    /// Parse the regex and return an expression (AST) and a bit set with the indexes of groups
    /// that are referenced by backrefs.
    pub fn parse_tree(re: &str) -> Result<ExprTree> {
        Expr::parse_tree_with_flags(re, RegexOptions::default().compute_flags())
    }

    /// Parse the regex and return an expression (AST)
    /// Flags should be bit based based on flags
    pub fn parse_tree_with_flags(re: &str, flags: u32) -> Result<ExprTree> {
        Parser::parse_with_flags(re, flags)
    }

    /// Returns `true` if this expression is a leaf node (has no children).
    ///
    /// Leaf nodes include literals, assertions, backreferences, and other atomic expressions.
    /// Non-leaf nodes include groups, concatenations, alternations, and repetitions.
    pub fn is_leaf_node(&self) -> bool {
        matches!(
            self,
            Expr::Empty
                | Expr::Any { .. }
                | Expr::Assertion(_)
                | Expr::GeneralNewline { .. }
                | Expr::Literal { .. }
                | Expr::LiteralBytes { .. }
                | Expr::Delegate { .. }
                | Expr::Backref { .. }
                | Expr::BackrefWithRelativeRecursionLevel { .. }
                | Expr::KeepOut
                | Expr::ContinueFromPreviousMatchEnd
                | Expr::BackrefExistsCondition { .. }
                | Expr::BacktrackingControlVerb(_)
                |             Expr::SubroutineCall(_)
                | Expr::Absent(Absent::Clear)
                // An unresolved AstNode has no separate child Expr to iterate; the resolver
                // should have replaced it before analysis, so treat it as a leaf so that
                // collection/iteration doesn't panic, and let the analyzer emit the error.
                | Expr::AstNode(..),
        )
    }

    /// Returns `true` if any descendant of this expression (not including itself)
    /// satisfies the given predicate.
    ///
    /// This performs an iterative depth-first search using [`children_iter`](Self::children_iter).
    pub fn has_descendant(&self, predicate: impl Fn(&Expr) -> bool) -> bool {
        let mut stack: Vec<&Expr> = self.children_iter().collect();
        while let Some(expr) = stack.pop() {
            if predicate(expr) {
                return true;
            }
            stack.extend(expr.children_iter());
        }
        false
    }

    /// Returns an iterator over the immediate children of this expression.
    ///
    /// For leaf nodes, this returns an empty iterator. For non-leaf nodes, it returns
    /// references to their immediate children (non-recursive).
    pub fn children_iter(&self) -> ExprChildrenIter<'_> {
        children_iter_match!(self, ExprChildrenIter, iter, as_ref, as_ref)
    }

    /// Returns an iterator over the immediate children of this expression for mutable access.
    ///
    /// For leaf nodes, this returns an empty iterator. For non-leaf nodes, it returns
    /// mutable references to their immediate children (non-recursive).
    pub fn children_iter_mut(&mut self) -> ExprChildrenIterMut<'_> {
        children_iter_match!(self, ExprChildrenIterMut, iter_mut, as_mut, make_mut)
    }

    /// Convert expression to a regex string in the regex crate's syntax.
    ///
    /// # Panics
    ///
    /// Panics for expressions that are hard, i.e. can not be handled by the regex crate.
    pub fn to_str(&self, buf: &mut String, precedence: u8) {
        match *self {
            Expr::Empty => (),
            Expr::Any { newline, crlf } => buf.push_str(match (newline, crlf) {
                (true, _) => "(?s:.)",
                (false, true) => "(?R-s:.)",
                (false, false) => ".",
            }),
            Expr::Literal { ref val, casei } => {
                if casei {
                    buf.push_str("(?i:");
                }
                push_quoted(buf, val);
                if casei {
                    buf.push(')');
                }
            }
            Expr::Assertion(Assertion::StartText) => buf.push('^'),
            Expr::Assertion(Assertion::EndText) => buf.push('$'),
            Expr::Assertion(
                Assertion::StartLine { crlf: false }
                | Assertion::StartLineOniguruma { crlf: false },
            ) => buf.push_str("(?m:^)"),
            Expr::Assertion(Assertion::EndLine { crlf: false }) => buf.push_str("(?m:$)"),
            Expr::Assertion(
                Assertion::StartLine { crlf: true } | Assertion::StartLineOniguruma { crlf: true },
            ) => buf.push_str("(?Rm:^)"),
            Expr::Assertion(Assertion::EndLine { crlf: true }) => buf.push_str("(?Rm:$)"),
            Expr::Assertion(Assertion::WordBoundary) => buf.push_str("\\b"),
            Expr::Assertion(Assertion::NotWordBoundary) => buf.push_str("\\B"),
            Expr::Assertion(Assertion::LeftWordBoundary) => buf.push_str("\\<"),
            Expr::Assertion(Assertion::RightWordBoundary) => buf.push_str("\\>"),
            Expr::Assertion(Assertion::LeftWordHalfBoundary) => buf.push_str("\\b{start-half}"),
            Expr::Assertion(Assertion::RightWordHalfBoundary) => buf.push_str("\\b{end-half}"),
            Expr::Concat(ref children) => {
                if precedence > 1 {
                    buf.push_str("(?:");
                }
                for child in children {
                    child.to_str(buf, 2);
                }
                if precedence > 1 {
                    buf.push(')')
                }
            }
            Expr::Alt(_) => {
                if precedence > 0 {
                    buf.push_str("(?:");
                }
                let mut children = self.children_iter();
                if let Some(first) = children.next() {
                    first.to_str(buf, 1);
                    for child in children {
                        buf.push('|');
                        child.to_str(buf, 1);
                    }
                }
                if precedence > 0 {
                    buf.push(')');
                }
            }
            Expr::Group(ref child) => {
                buf.push('(');
                child.to_str(buf, 0);
                buf.push(')');
            }
            Expr::Repeat {
                ref child,
                lo,
                hi,
                greedy,
            } => {
                if precedence > 2 {
                    buf.push_str("(?:");
                }
                child.to_str(buf, 3);
                write_quantifier(buf, lo, hi, greedy);
                if precedence > 2 {
                    buf.push(')');
                }
            }
            Expr::Delegate {
                ref inner, casei, ..
            } => {
                // at the moment, delegate nodes are just atoms
                if casei {
                    buf.push_str("(?i:");
                }
                buf.push_str(inner);
                if casei {
                    buf.push(')');
                }
            }
            Expr::DefineGroup { .. } => {
                // DEFINE groups match nothing - output empty string for delegation
            }
            _ => panic!("attempting to format hard expr {:?}", self),
        }
    }
}

// precondition: ix > 0
fn prev_codepoint_ix(s: &str, mut ix: usize) -> usize {
    let bytes = s.as_bytes();
    loop {
        ix -= 1;
        // fancy bit magic for ranges 0..0x80 + 0xc0..
        if (bytes[ix] as i8) >= -0x40 {
            break;
        }
    }
    ix
}

fn codepoint_len(b: u8) -> usize {
    match b {
        b if b < 0x80 => 1,
        b if b < 0xe0 => 2,
        b if b < 0xf0 => 3,
        _ => 4,
    }
}

// If this returns false, then there is no possible backref in the re

// Both potential implementations are turned off, because we currently
// always need to do a deeper analysis because of 1-character
// look-behind. If we could call a find_from_pos method of regex::Regex,
// it would make sense to bring this back.
/*
pub fn detect_possible_backref(re: &str) -> bool {
    let mut last = b'\x00';
    for b in re.as_bytes() {
        if b'0' <= *b && *b <= b'9' && last == b'\\' { return true; }
        last = *b;
    }
    false
}

pub fn detect_possible_backref(re: &str) -> bool {
    let mut bytes = re.as_bytes();
    loop {
        match memchr::memchr(b'\\', &bytes[..bytes.len() - 1]) {
            Some(i) => {
                bytes = &bytes[i + 1..];
                let c = bytes[0];
                if b'0' <= c && c <= b'9' { return true; }
            }
            None => return false
        }
    }
}
*/

/// The internal module only exists so that the toy example can access internals for debugging and
/// experimenting.
#[doc(hidden)]
pub mod internal {
    pub use crate::analyze::{analyze, can_compile_as_anchored, AnalyzeContext, Info};
    pub use crate::compile::{compile, CompileOptions};
    pub use crate::optimize::optimize;
    pub use crate::parse_flags::{
        FLAG_CASEI, FLAG_CRLF, FLAG_DOTNL, FLAG_IGNORE_NUMBERED_GROUPS_WHEN_NAMED_GROUPS_EXIST,
        FLAG_IGNORE_SPACE, FLAG_MULTI, FLAG_ONIGURUMA_MODE, FLAG_UNICODE,
    };
    pub use crate::vm::{run_default, run_trace, Insn, Prog, Seek};
}

#[cfg(test)]
mod tests {
    use alloc::borrow::Cow;
    use alloc::boxed::Box;
    use alloc::string::{String, ToString};
    use alloc::sync::Arc;
    use alloc::vec::Vec;
    use alloc::{format, vec};

    use crate::parse::{make_group, make_literal};
    use crate::{Absent, Expr, Regex, RegexBuilder, RegexImpl, RegexInput};

    //use detect_possible_backref;

    // tests for to_str

    fn to_str(e: Expr) -> String {
        let mut s = String::new();
        e.to_str(&mut s, 0);
        s
    }

    #[test]
    fn to_str_concat_alt() {
        let e = Expr::Concat(vec![
            Expr::Alt(vec![make_literal("a"), make_literal("b")]),
            make_literal("c"),
        ]);
        assert_eq!(to_str(e), "(?:a|b)c");
    }

    #[test]
    fn to_str_rep_concat() {
        let e = Expr::Repeat {
            child: Box::new(Expr::Concat(vec![make_literal("a"), make_literal("b")])),
            lo: 2,
            hi: 3,
            greedy: true,
        };
        assert_eq!(to_str(e), "(?:ab){2,3}");
    }

    #[test]
    fn to_str_group_alt() {
        let e = Expr::Group(Arc::new(Expr::Alt(vec![
            make_literal("a"),
            make_literal("b"),
        ])));
        assert_eq!(to_str(e), "(a|b)");
    }

    #[test]
    fn to_str_literal_special_chars() {
        assert_eq!(to_str(make_literal("\n")), "\\n");
        assert_eq!(to_str(make_literal("\t")), "\\t");
        assert_eq!(to_str(make_literal("\r")), "\\r");
        assert_eq!(to_str(make_literal("\\")), "\\\\");
        assert_eq!(to_str(make_literal(".")), "\\.");
    }

    #[test]
    fn as_str_debug() {
        let s = r"(a+)b\1";
        let regex = Regex::new(s).unwrap();
        assert_eq!(s, regex.as_str());
        assert_eq!(s, format!("{:?}", regex));
    }

    #[test]
    fn display() {
        let s = r"(a+)b\1";
        let regex = Regex::new(s).unwrap();
        assert_eq!(s, format!("{}", regex));
    }

    #[test]
    fn from_str() {
        let s = r"(a+)b\1";
        let regex = s.parse::<Regex>().unwrap();
        assert_eq!(regex.as_str(), s);
    }

    #[test]
    fn to_str_repeat() {
        fn repeat(lo: usize, hi: usize, greedy: bool) -> Expr {
            Expr::Repeat {
                child: Box::new(make_literal("a")),
                lo,
                hi,
                greedy,
            }
        }

        assert_eq!(to_str(repeat(2, 2, true)), "a{2}");
        assert_eq!(to_str(repeat(2, 2, false)), "a{2}?");
        assert_eq!(to_str(repeat(2, 3, true)), "a{2,3}");
        assert_eq!(to_str(repeat(2, 3, false)), "a{2,3}?");
        assert_eq!(to_str(repeat(2, usize::MAX, true)), "a{2,}");
        assert_eq!(to_str(repeat(2, usize::MAX, false)), "a{2,}?");
        assert_eq!(to_str(repeat(0, 1, true)), "a?");
        assert_eq!(to_str(repeat(0, 1, false)), "a??");
        assert_eq!(to_str(repeat(0, usize::MAX, true)), "a*");
        assert_eq!(to_str(repeat(0, usize::MAX, false)), "a*?");
        assert_eq!(to_str(repeat(1, usize::MAX, true)), "a+");
        assert_eq!(to_str(repeat(1, usize::MAX, false)), "a+?");
    }

    #[test]
    fn escape() {
        // Check that strings that need no quoting are borrowed, and that non-special punctuation
        // is not quoted.
        match crate::escape("@foo") {
            Cow::Borrowed(s) => assert_eq!(s, "@foo"),
            _ => panic!("Value should be borrowed."),
        }

        // Check typical usage.
        assert_eq!(crate::escape("fo*o").into_owned(), "fo\\*o");

        // Check that multibyte characters are handled correctly.
        assert_eq!(crate::escape("fø*ø").into_owned(), "fø\\*ø");
    }

    #[test]
    fn push_quoted_special_chars() {
        fn pq(s: &str) -> String {
            let mut buf = String::new();
            crate::push_quoted(&mut buf, s);
            buf
        }
        assert_eq!(pq("\n"), "\\n");
        assert_eq!(pq("\t"), "\\t");
        assert_eq!(pq("\r"), "\\r");
        assert_eq!(pq("\\"), "\\\\");
        assert_eq!(pq("."), "\\.");
        assert_eq!(pq("hello"), "hello");
        assert_eq!(pq("a.b"), "a\\.b");
        assert_eq!(pq("fø*ø"), "fø\\*ø");
    }

    #[test]
    fn trailing_positive_lookahead_wrap_capture_group_fixup() {
        let s = r"a+(?=c)";
        let regex = s.parse::<Regex>().unwrap();
        assert!(matches!(regex.inner,
            RegexImpl::Wrap { explicit_capture_group_0: true, .. }),
            "trailing positive lookahead for an otherwise easy pattern should avoid going through the VM");
        assert_eq!(s, regex.as_str());
        assert_eq!(s, format!("{:?}", regex));
    }

    #[test]
    fn easy_regex() {
        let s = r"(a+)b";
        let regex = s.parse::<Regex>().unwrap();
        assert!(
            matches!(regex.inner, RegexImpl::Wrap { explicit_capture_group_0: false, .. }),
            "easy pattern should avoid going through the VM, and capture group 0 should be implicit"
        );

        assert_eq!(s, regex.as_str());
        assert_eq!(s, format!("{:?}", regex));
    }

    #[test]
    fn hard_regex() {
        let s = r"(a+)(?>c)";
        let regex = s.parse::<Regex>().unwrap();
        assert!(
            matches!(regex.inner, RegexImpl::Fancy { .. }),
            "hard regex should be compiled into a VM"
        );
        assert_eq!(s, regex.as_str());
        assert_eq!(s, format!("{:?}", regex));
    }

    #[cfg(feature = "leftmost_longest")]
    #[test]
    fn leftmost_longest_alt_compiles_to_fancy() {
        let regex = RegexBuilder::new(r"a|ab")
            .leftmost_longest(true)
            .build()
            .unwrap();
        assert!(
            matches!(regex.inner, RegexImpl::Fancy { .. }),
            "alternation with non-const size should compile to VM when leftmost_longest is enabled"
        );
    }

    #[test]
    fn start_end_text_assertions_can_stay_wrap_without_override_opt_in() {
        let regex = Regex::new(r"\Afoo\z").unwrap();
        assert!(
            matches!(regex.inner, RegexImpl::Wrap { .. }),
            r"\A...\z should stay on the wrap path unless input assertion overrides are enabled"
        );
    }

    #[test]
    fn start_end_text_assertions_become_fancy_with_override_opt_in() {
        let regex = RegexBuilder::new(r"\Afoo\z")
            .allow_input_assertion_overrides(true)
            .build()
            .unwrap();
        assert!(
            matches!(regex.inner, RegexImpl::Fancy { .. }),
            r"\A...\z should use the VM when input assertion overrides are enabled"
        );
    }

    /*
    #[test]
    fn detect_backref() {
        assert_eq!(detect_possible_backref("a0a1a2"), false);
        assert_eq!(detect_possible_backref("a0a1\\a2"), false);
        assert_eq!(detect_possible_backref("a0a\\1a2"), true);
        assert_eq!(detect_possible_backref("a0a1a2\\"), false);
    }
    */

    #[test]
    fn test_is_leaf_node_leaf_nodes() {
        // Test all leaf node variants
        assert!(Expr::Empty.is_leaf_node());
        assert!(Expr::Any {
            newline: false,
            crlf: false
        }
        .is_leaf_node());
        assert!(Expr::Any {
            newline: true,
            crlf: false
        }
        .is_leaf_node());
        assert!(Expr::Assertion(crate::Assertion::StartText).is_leaf_node());
        assert!(Expr::Literal {
            val: "test".to_string(),
            casei: false
        }
        .is_leaf_node());
        assert!(Expr::LiteralBytes { bytes: vec![0x80] }.is_leaf_node());
        assert!(Expr::Delegate {
            inner: "[0-9]".to_string(),
            casei: false,
        }
        .is_leaf_node());
        assert!(Expr::Backref {
            group: 1,
            casei: false
        }
        .is_leaf_node());
        assert!(Expr::BackrefWithRelativeRecursionLevel {
            group: 1,
            relative_level: -1,
            casei: false
        }
        .is_leaf_node());
        assert!(Expr::KeepOut.is_leaf_node());
        assert!(Expr::ContinueFromPreviousMatchEnd.is_leaf_node());
        assert!(Expr::BackrefExistsCondition {
            group: 1,
            relative_recursion_level: None
        }
        .is_leaf_node());
        assert!(Expr::BacktrackingControlVerb(crate::BacktrackingControlVerb::Fail).is_leaf_node());
        assert!(Expr::SubroutineCall(1).is_leaf_node());

        assert!(Expr::Absent(Absent::Clear).is_leaf_node());
    }

    #[test]
    fn test_is_leaf_node_non_leaf_nodes() {
        // Test all non-leaf node variants
        assert!(!Expr::Concat(vec![make_literal("a")]).is_leaf_node());
        assert!(!Expr::Alt(vec![make_literal("a"), make_literal("b")]).is_leaf_node());
        assert!(!make_group(make_literal("a")).is_leaf_node());
        assert!(
            !Expr::LookAround(Box::new(make_literal("a")), crate::LookAround::LookAhead)
                .is_leaf_node()
        );
        assert!(!Expr::Repeat {
            child: Box::new(make_literal("a")),
            lo: 0,
            hi: 1,
            greedy: true
        }
        .is_leaf_node());
        assert!(!Expr::AtomicGroup(Box::new(make_literal("a"))).is_leaf_node());
        assert!(!Expr::Conditional {
            condition: Box::new(Expr::BackrefExistsCondition {
                group: 1,
                relative_recursion_level: None
            }),
            true_branch: Box::new(make_literal("a")),
            false_branch: Box::new(Expr::Empty)
        }
        .is_leaf_node());

        assert!(!Expr::Absent(Absent::Repeater(Box::new(make_literal("a")))).is_leaf_node());
        assert!(!Expr::Absent(Absent::Expression {
            absent: Box::new(make_literal("/*")),
            exp: Box::new(Expr::Repeat {
                child: Box::new(Expr::Any {
                    newline: true,
                    crlf: false
                }),
                lo: 0,
                hi: usize::MAX,
                greedy: true
            })
        })
        .is_leaf_node());
        assert!(!Expr::Absent(Absent::Stopper(Box::new(make_literal("/*")))).is_leaf_node());
    }

    #[test]
    fn test_children_iter_empty() {
        // Leaf nodes should return empty iterator
        let expr = Expr::Empty;
        let mut iter = expr.children_iter();
        assert!(iter.next().is_none());

        let expr = make_literal("test");
        let mut iter = expr.children_iter();
        assert!(iter.next().is_none());
    }

    #[test]
    fn test_children_iter_single() {
        // Group, LookAround, AtomicGroup, Repeat should return single child
        let child = make_literal("a");
        let expr = make_group(child.clone());
        let children: Vec<_> = expr.children_iter().collect();
        assert_eq!(children.len(), 1);

        let expr = Expr::Repeat {
            child: Box::new(child.clone()),
            lo: 0,
            hi: 1,
            greedy: true,
        };
        let children: Vec<_> = expr.children_iter().collect();
        assert_eq!(children.len(), 1);
    }

    #[test]
    fn test_children_iter_vec() {
        // Concat and Alt should return all children
        let children_vec = vec![make_literal("a"), make_literal("b"), make_literal("c")];
        let expr = Expr::Concat(children_vec.clone());
        let children: Vec<_> = expr.children_iter().collect();
        assert_eq!(children.len(), 3);

        let expr = Expr::Alt(children_vec);
        let children: Vec<_> = expr.children_iter().collect();
        assert_eq!(children.len(), 3);
    }

    #[test]
    fn test_children_iter_triple() {
        // Conditional should return three children
        let expr = Expr::Conditional {
            condition: Box::new(Expr::BackrefExistsCondition {
                group: 1,
                relative_recursion_level: None,
            }),
            true_branch: Box::new(make_literal("a")),
            false_branch: Box::new(make_literal("b")),
        };
        let children: Vec<_> = expr.children_iter().collect();
        assert_eq!(children.len(), 3);

        // Absent expression should return two children
        let expr = Expr::Absent(Absent::Expression {
            absent: Box::new(make_literal("/*")),
            exp: Box::new(Expr::Repeat {
                child: Box::new(Expr::Any {
                    newline: true,
                    crlf: false,
                }),
                lo: 0,
                hi: usize::MAX,
                greedy: true,
            }),
        });
        let children: Vec<_> = expr.children_iter().collect();
        assert_eq!(children.len(), 2);
    }

    #[test]
    fn find_input_raw_honors_anchored_flag_for_wrapped_regex() {
        let regex = Regex::new("abc").unwrap();

        // Unanchored search finds match at position 1
        let input = RegexInput::new("zabc");
        assert_eq!(
            Some((1, 4)),
            regex
                .find_input(input)
                .unwrap()
                .map(|m| (m.start(), m.end()))
        );

        // Anchored at position 0 returns None (abc doesn't match at pos 0)
        let anchored_at_start = RegexInput::new("zabc").anchored(true);
        assert!(regex.find_input(anchored_at_start).unwrap().is_none());

        // Anchored at position 1 finds match
        let anchored_at_match = RegexInput::new("zabc").from_pos(1).anchored(true);
        assert_eq!(
            regex
                .find_input(anchored_at_match)
                .unwrap()
                .map(|m| (m.start(), m.end())),
            Some((1, 4))
        );
    }
}
