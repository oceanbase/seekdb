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

//! RegexSet API for matching multiple patterns against the same input.
//!
//! This module provides [`RegexSet`], which allows efficient matching of multiple
//! regular expression patterns against the same input text. This is particularly
//! useful for applications like syntax highlighting, where many patterns need to
//! be matched against each line of text.
//!
//! # Examples
//!
//! Basic usage:
//!
//! ```rust
//! use fancy_regex::{RegexInput, RegexSet};
//!
//! # fn main() -> Result<(), fancy_regex::Error> {
//! let set = RegexSet::new(&[
//!     r"\d+",              // Pattern 0: numbers
//!     r"\w+",              // Pattern 1: words
//!     r"\.\d+",            // Pattern 2: decimal fractions - only matches later in "$29.99"
//!     r"(?<=\$)\d+\.\d+",  // Pattern 3: prices (with lookbehind)
//! ])?;
//!
//! let text = "$29.99";
//!
//! let mut matches = set
//!     .find_input(RegexInput::new(text))?
//!     .expect("expected at least one match");
//!
//! let first = matches.next().expect("expected first match")?;
//! assert_eq!(first.pattern(), 0);
//! assert_eq!(first.as_str(), "29");
//! assert_eq!(first.start(), 1);
//!
//! let second = matches.next().expect("expected second match")?;
//! assert_eq!(second.pattern(), 1);
//! assert_eq!(second.as_str(), "29");
//!
//! // Pattern 2 (\.\d+) matches ".99" but only at position 3, not at the
//! // earliest match position (1), so it is not returned here.
//!
//! let third = matches.next().expect("expected third match")?;
//! assert_eq!(third.pattern(), 3);
//! assert_eq!(third.as_str(), "29.99");
//! assert!(matches.next().is_none());
//! # Ok(())
//! # }
//! ```
//!
//! # Differences from `regex::RegexSet`
//!
//! [`regex::RegexSet`](https://docs.rs/regex/latest/regex/struct.RegexSet.html) and
//! `fancy_regex::RegexSet` solve similar "multiple patterns" problems, but their APIs
//! are intentionally different:
//!
//! - `regex::RegexSet` reports which patterns match somewhere in the haystack.
//! - `fancy_regex::RegexSet::find_input` finds the **earliest match position** and then
//!   yields concrete matches at that position in pattern index order.
//! - `regex::RegexSet` does not produce captures or match spans. `fancy_regex::RegexSet`
//!   yields [`RegexSetMatch`] values with captures and offsets.
//! - Each yielded item is `Result<RegexSetMatch, Error>` because fancy features
//!   (look-around, backreferences, etc.) can fail at runtime, for example due to a
//!   backtrack limit.
//!
//! # Performance
//!
//! The `RegexSet` uses a hybrid approach to achieve good performance:
//!
//! A multi-pattern DFA is built for parallel evaluation, to provide very fast
//! candidate position matching with linear time complexity.
//!
//! At the earliest candidate position:
//! - any **hard patterns** (those with backreferences, lookaround, etc.) which could
//!   match there are evaluated individually using a backtracking VM in anchored mode.
//!   These may have exponential time complexity in pathological cases.
//! - any **easy patterns** (those without backreferences, lookaround, etc.) which do
//!   match are also individually run through the underlying regex crate, to resolve
//!   capture groups etc. which may have been skipped in multi-DFA mode.
//!
//! For best performance, try to design patterns that can be fully delegated to the
//! DFA when possible.
//!
//! # Priority and Non-Overlapping Matches
//!
//! `find_input` returns only matches at the earliest start offset. This is deliberate:
//! the caller can inspect all matches at that position and decide which one has priority.
//! This allows for most flexibility, without having to cater for various scenarios in
//! the RegexSet itself. The `Input` struct makes it easy for the caller to advance
//! position in case of an empty match winning, and the `RegexInput` struct sits on top
//! of that to make it possible to specify rules like whether `\G` should match or not.

use alloc::boxed::Box;
use alloc::string::ToString;
use alloc::sync::Arc;
use alloc::vec::Vec;

use crate::CompileOptions;
use crate::Input;
use crate::RegexInput;
use crate::RegexOptionsBuilder;
use regex_automata::hybrid::dfa;
use regex_automata::meta::Builder as RaBuilder;
use regex_automata::meta::Config as RaConfig;
use regex_automata::meta::Regex as RaRegex;
use regex_automata::nfa::thompson;
use regex_automata::nfa::thompson::WhichCaptures;
use regex_automata::util::pool::Pool;
use regex_automata::util::syntax::Config as SyntaxConfig;
use regex_automata::Anchored;
use regex_automata::Input as RaInput;
use regex_automata::MatchErrorKind;
use regex_automata::MatchKind;
use regex_automata::PatternID;
use regex_automata::PatternSet;

use crate::vm::OPTION_NOT_CONTINUED_FROM_PREVIOUS_MATCH;
use crate::CompileError;
use crate::Error;
use crate::RegexOptions;
use crate::{BytesMode, Captures, Regex, Result};

type DfaCachePoolFactory = alloc::boxed::Box<
    dyn Fn() -> dfa::Cache + Send + Sync + core::panic::UnwindSafe + core::panic::RefUnwindSafe,
>;

const BYTES_PER_MIB: usize = 1 << 20;
const DEFAULT_META_NFA_SIZE_LIMIT: usize = 64 * BYTES_PER_MIB;
const DEFAULT_META_HYBRID_CACHE_CAPACITY: usize = 64 * BYTES_PER_MIB;
const DEFAULT_OVERLAPPING_DFA_CACHE_CAPACITY: usize = 64 * BYTES_PER_MIB;

#[derive(Clone, Debug)]
/// RegexSet API for matching multiple patterns against the same input.
pub struct RegexSet {
    regexes: Vec<Arc<Regex>>,
    earliest_match_finder: RaRegex,
    overlapping_dfa: Arc<dfa::DFA>,
    overlapping_cache_pool: Arc<Pool<dfa::Cache, DfaCachePoolFactory>>,
}

#[derive(Clone, Debug)]
/// Configuration for a RegexSet
pub struct RegexSetOptions {
    syntaxc: SyntaxConfig,
    delegate_size_limit: Option<usize>,
    delegate_dfa_size_limit: Option<usize>,
    meta_nfa_size_limit: Option<usize>,
    meta_hybrid_cache_capacity: usize,
    overlapping_dfa_cache_capacity: usize,
    overlapping_dfa_skip_cache_capacity_check: bool,
    bytes_mode: BytesMode,
}

impl Default for RegexSetOptions {
    fn default() -> Self {
        let default_options = RegexOptions::default();
        RegexSetOptions {
            syntaxc: default_options.syntaxc,
            delegate_size_limit: default_options.delegate_size_limit,
            delegate_dfa_size_limit: default_options.delegate_dfa_size_limit,
            meta_nfa_size_limit: Some(DEFAULT_META_NFA_SIZE_LIMIT),
            meta_hybrid_cache_capacity: DEFAULT_META_HYBRID_CACHE_CAPACITY,
            overlapping_dfa_cache_capacity: DEFAULT_OVERLAPPING_DFA_CACHE_CAPACITY,
            overlapping_dfa_skip_cache_capacity_check: true,
            bytes_mode: default_options.bytes_mode,
        }
    }
}

impl RegexSetOptions {
    /// Create a new RegexSet options value with default settings.
    pub fn new() -> Self {
        Self::default()
    }

    /// Set the approximate size limit of each delegated sub-regex.
    ///
    /// This option is forwarded from the wrapped `regex` crate. Note that depending on the used
    /// regex features there may be multiple delegated sub-regexes fed to the `regex` crate. As
    /// such the actual limit is closer to `<number of delegated regexes> * delegate_size_limit`.
    pub fn delegate_size_limit(mut self, limit: usize) -> Self {
        self.delegate_size_limit = Some(limit);
        self
    }

    /// Set the approximate capacity, in bytes, of the cache used by the lazy DFA of delegated
    /// regexes.
    ///
    /// This corresponds to `hybrid_cache_capacity` in regex-automata, and has the same meaning as
    /// `RegexBuilder::dfa_size_limit` in the `regex` crate. Note that depending on the used
    /// regex features there may be multiple delegated sub-regexes. As such the actual limit is
    /// closer to `<number of delegated regexes> * delegate_dfa_size_limit`.
    pub fn delegate_dfa_size_limit(mut self, limit: usize) -> Self {
        self.delegate_dfa_size_limit = Some(limit);
        self
    }

    /// Set the approximate NFA size limit for the regex-automata meta regex used to find the
    /// earliest match position.
    ///
    /// Passing `None` disables the limit. The default is 64 MiB.
    pub fn meta_nfa_size_limit(mut self, limit: Option<usize>) -> Self {
        self.meta_nfa_size_limit = limit;
        self
    }

    /// Set the cache capacity, in bytes, for the lazy DFA used by the regex-automata meta regex
    /// that finds the earliest match position.
    ///
    /// The default is 64 MiB.
    pub fn meta_hybrid_cache_capacity(mut self, limit: usize) -> Self {
        self.meta_hybrid_cache_capacity = limit;
        self
    }

    /// Set the cache capacity, in bytes, for the lazy DFA used to find overlapping matches at a
    /// candidate match position.
    ///
    /// The default is 64 MiB.
    pub fn overlapping_dfa_cache_capacity(mut self, limit: usize) -> Self {
        self.overlapping_dfa_cache_capacity = limit;
        self
    }

    /// Configure whether the overlapping DFA builder should skip its minimum cache capacity check.
    ///
    /// Enabling this can allow large DFAs to build when the default cache capacity check would
    /// reject them, but it may allocate more memory at build time. The default is `true`.
    pub fn overlapping_dfa_skip_cache_capacity_check(mut self, yes: bool) -> Self {
        self.overlapping_dfa_skip_cache_capacity_check = yes;
        self
    }
}

impl RegexSet {
    /// Create a new RegexSet from an iterator of patterns using default options.
    ///
    /// All patterns will use the same default configuration:
    /// - Case sensitive
    /// - Multi-line mode disabled
    /// - Dot does not match newline
    /// - Unicode mode enabled
    ///
    /// # Errors
    ///
    /// Returns an error if any pattern fails to compile.
    pub fn new<I, S>(patterns: I) -> Result<Self>
    where
        I: IntoIterator<Item = S>,
        S: AsRef<str>,
    {
        let builder = RegexOptionsBuilder::new();
        Self::new_with_options(patterns, &builder)
    }

    /// Create a new RegexSet from an iterator of patterns using specified options.
    ///
    /// # Errors
    ///
    /// Returns an error if any pattern fails to compile.
    pub fn new_with_options<I, S>(
        patterns: I,
        options_builder: &RegexOptionsBuilder,
    ) -> Result<Self>
    where
        I: IntoIterator<Item = S>,
        S: AsRef<str>,
    {
        // Set members are only ever searched anchored at candidate positions
        // (see `match_pattern_at_input_position`), where the engine never
        // consults a prefilter — skip spending build time and memory on one.
        let mut member_options = options_builder.options.clone();
        member_options.delegate_prefilter = false;
        let regexes = patterns
            .into_iter()
            .map(|pattern| {
                Regex::new_options(pattern.as_ref().to_string(), &member_options).map(Arc::new)
            })
            .collect::<Result<Vec<_>>>()?;

        let config = RegexSetOptions {
            syntaxc: options_builder.options.syntaxc,
            delegate_size_limit: options_builder.options.delegate_size_limit,
            delegate_dfa_size_limit: options_builder.options.delegate_dfa_size_limit,
            meta_nfa_size_limit: Some(DEFAULT_META_NFA_SIZE_LIMIT),
            meta_hybrid_cache_capacity: DEFAULT_META_HYBRID_CACHE_CAPACITY,
            overlapping_dfa_cache_capacity: DEFAULT_OVERLAPPING_DFA_CACHE_CAPACITY,
            overlapping_dfa_skip_cache_capacity_check: true,
            bytes_mode: options_builder.options.bytes_mode,
        };
        Self::from_regexes(regexes, config)
    }

    /// Create a new RegexSet from pre-built `Arc<Regex>` instances.
    ///
    /// # Examples
    ///
    /// ```rust
    /// use fancy_regex::{Regex, RegexBuilder, RegexInput, RegexSet, RegexSetOptions};
    /// use std::sync::Arc;
    ///
    /// # fn main() -> Result<(), fancy_regex::Error> {
    /// // Create regexes with different options
    /// let re1 = Arc::new(RegexBuilder::new(r"hello")
    ///     .case_insensitive(true)
    ///     .build()?);
    /// let re2 = Arc::new(Regex::new(r"\d+")?);
    /// let re3 = Arc::new(Regex::new(r"(?<=\w)end")?); // lookbehind - fancy pattern
    ///
    /// // Combine them into a RegexSet
    /// let set = RegexSet::from_regexes([re1, re2, re3], Default::default())?;
    ///
    /// let text = "HELLO";
    /// let mut matches = set
    ///     .find_input(RegexInput::new(text))?
    ///     .expect("expected at least one match");
    ///
    /// let m = matches.next().expect("expected first match")?;
    /// assert_eq!(m.pattern(), 0);
    /// assert_eq!(m.as_str(), "HELLO");
    /// assert!(matches.next().is_none());
    /// # Ok(())
    /// # }
    /// ```
    ///
    /// # Errors
    ///
    /// Returns an error if the multi-pattern DFA construction fails.
    pub fn from_regexes<I>(regexes: I, config: RegexSetOptions) -> Result<Self>
    where
        I: IntoIterator<Item = Arc<Regex>>,
    {
        let regexes_vec: Vec<Arc<Regex>> = regexes.into_iter().collect();

        let mut patterns = Vec::with_capacity(regexes_vec.len());

        for regex in &regexes_vec {
            patterns.push(regex.seek_pattern());
        }

        let compile_options = CompileOptions {
            bytes_mode: config.bytes_mode,
            unicode: config.syntaxc.get_unicode() && !matches!(config.bytes_mode, BytesMode::Ascii),
            delegate_size_limit: config.delegate_size_limit,
            delegate_dfa_size_limit: config.delegate_dfa_size_limit,
            ..CompileOptions::default()
        };

        let utf8 = matches!(compile_options.bytes_mode, BytesMode::Unicode);

        // Parse each pattern once and share the resulting Hirs between the two
        // set-wide engines (the "earliest match" meta regex and the overlapping
        // DFA), instead of letting each engine re-parse every pattern string.
        let syntax_config = SyntaxConfig::new()
            .utf8(utf8)
            .unicode(compile_options.unicode);
        let hirs = regex_automata::util::syntax::parse_many_with(&patterns, &syntax_config)
            .map_err(|e| {
                Error::CompileError(Box::new(CompileError::UnexpectedGeneralError(
                    alloc::format!("failed to parse regex set pattern: {}", e),
                )))
            })?;

        // The multi-pattern "earliest match" engine is searched unanchored. The
        // syntax options are already encoded in the Hirs.
        let mut earliest_builder = RaBuilder::new();
        earliest_builder.configure(
            RaConfig::new()
                .match_kind(MatchKind::LeftmostFirst)
                .nfa_size_limit(config.meta_nfa_size_limit)
                .hybrid_cache_capacity(config.meta_hybrid_cache_capacity),
        );
        let earliest_match_finder = earliest_builder
            .build_many_from_hir(&hirs)
            .map_err(CompileError::InnerError)
            .map_err(|e| Error::CompileError(Box::new(e)))?;

        let format_patterns = || {
            patterns
                .iter()
                .enumerate()
                .map(|(i, p)| alloc::format!("[{}]: {}", i, p))
                .collect::<Vec<_>>()
                .join("\n---\n")
        };

        // Build the overlapping DFA from a Thompson NFA compiled from the same
        // Hirs. This replicates what `dfa::Builder::build_many` does internally
        // (it forces `WhichCaptures::None` and calls `build_from_nfa`), minus
        // the extra pattern parse.
        let mut thompson_config = thompson::Config::new().which_captures(WhichCaptures::None);
        if let Some(limit) = compile_options.delegate_size_limit {
            thompson_config = thompson_config.nfa_size_limit(Some(limit));
        }
        let nfa = thompson::Compiler::new()
            .configure(thompson_config)
            .build_many_from_hir(&hirs)
            .map_err(|e| {
                Error::CompileError(Box::new(CompileError::DfaBuildError(
                    format_patterns(),
                    e.to_string(),
                )))
            })?;

        let mut overlapping_dfa_builder = dfa::DFA::builder();
        let mut overlapping_config = dfa::Config::new()
            .match_kind(MatchKind::All)
            .unicode_word_boundary(compile_options.unicode)
            .cache_capacity(config.overlapping_dfa_cache_capacity);
        if config.overlapping_dfa_skip_cache_capacity_check {
            overlapping_config = overlapping_config.skip_cache_capacity_check(true);
        }
        overlapping_dfa_builder.configure(overlapping_config);
        let overlapping_dfa =
            Arc::new(overlapping_dfa_builder.build_from_nfa(nfa).map_err(|e| {
                Error::CompileError(Box::new(CompileError::DfaBuildError(
                    format_patterns(),
                    e.to_string(),
                )))
            })?);
        let create: DfaCachePoolFactory = alloc::boxed::Box::new({
            let dfa = Arc::clone(&overlapping_dfa);
            move || dfa.create_cache()
        });
        let overlapping_cache_pool = Arc::new(Pool::new(create));

        Ok(Self {
            regexes: regexes_vec,
            earliest_match_finder,
            overlapping_dfa,
            overlapping_cache_pool,
        })
    }

    /// Returns the number of patterns in the set.
    pub fn len(&self) -> usize {
        self.regexes.len()
    }

    /// Returns true if the set contains no patterns.
    pub fn is_empty(&self) -> bool {
        self.len() == 0
    }

    /// Returns an iterator over matches at the earliest match position - if any.
    /// Iterator yields matches in pattern index order
    pub fn find_input<'r, 't, S: Input + ?Sized>(
        &'r self,
        input: RegexInput<'t, S>,
    ) -> Result<Option<RegexSetMatchesAt<'r, 't, S>>> {
        if input.is_done() {
            return Ok(None);
        }

        let haystack = input.haystack();
        let match_range = input.get_range();
        let mut search_start = input.effective_start();
        let mut seen_pattern_indices = PatternSet::new(self.regexes.len());

        while search_start <= match_range.end {
            let ra_input = RaInput::new(haystack.as_bytes())
                .range(search_start..match_range.end)
                .anchored(if input.is_anchored() {
                    Anchored::Yes
                } else {
                    Anchored::No
                });
            let Some(candidate) = self.earliest_match_finder.search(&ra_input) else {
                return Ok(None);
            };
            let match_start = candidate.start();
            let overlapping_input = RaInput::new(haystack.as_bytes())
                .anchored(Anchored::Yes)
                .range(match_start..match_range.end);
            seen_pattern_indices.clear();
            {
                let mut cache_guard = self.overlapping_cache_pool.get();
                if let Err(e) = self.overlapping_dfa.try_which_overlapping_matches(
                    &mut cache_guard,
                    &overlapping_input,
                    &mut seen_pattern_indices,
                ) {
                    match e.kind() {
                        MatchErrorKind::Quit { .. } | MatchErrorKind::GaveUp { .. } => {
                            // The DFA gave up (e.g. it encountered non-ASCII bytes while
                            // unicode word boundaries are enabled, which adds quit bytes
                            // for multi-byte UTF-8 sequences). Fall back to trying every
                            // pattern at this position so correctness is preserved.
                            seen_pattern_indices.clear();
                            for i in 0..self.regexes.len() {
                                seen_pattern_indices.insert(PatternID::must(i));
                            }
                        }
                        _ => panic!("unexpected overlapping DFA error: {:?}", e),
                    }
                }
            } // release cache_guard back to pool before doing per-pattern matching
              // Verify candidates straight off the PatternSet (ascending pattern
              // order). Only when a match is found are the *remaining* indices
              // collected, for lazy verification while the caller iterates — so
              // a failed candidate position allocates nothing.
            let mut candidate_pattern_indices = seen_pattern_indices
                .iter()
                .map(|pattern| pattern.as_usize());
            let mut first_match = None;
            for pattern_index in &mut candidate_pattern_indices {
                if let Some(candidate_match) =
                    self.match_pattern_at_input_position(pattern_index, &input, match_start)?
                {
                    first_match = Some(candidate_match);
                    break;
                }
            }

            if let Some(first_match) = first_match {
                let pending_pattern_indices = candidate_pattern_indices.collect::<Vec<_>>();
                return Ok(Some(RegexSetMatchesAt {
                    regex_set: self,
                    input,
                    haystack,
                    match_start,
                    first_match: Some(first_match),
                    pending_pattern_indices: pending_pattern_indices.into_iter(),
                }));
            }

            search_start = haystack.advance_position(match_start);
        }

        Ok(None)
    }

    fn match_pattern_at_input_position<'t, S: Input + ?Sized>(
        &self,
        pattern_index: usize,
        input: &RegexInput<'t, S>,
        match_start: usize,
    ) -> Result<Option<RegexSetMatch<'t, S>>> {
        let candidate_input = input.clone().from_pos(match_start).anchored(true);
        let regex = &self.regexes[pattern_index];
        let mut option_flags = 0;
        if input.start() < match_start {
            option_flags |= OPTION_NOT_CONTINUED_FROM_PREVIOUS_MATCH;
        }
        if regex.captures_len() == 1 {
            return Ok(regex.find_input_raw(&candidate_input, option_flags)?.map(
                |(start, end)| RegexSetMatch {
                    pattern_index,
                    captures: regex.captures_for_span(input.haystack(), start, end),
                },
            ));
        }
        Ok(regex
            .captures_input_with_option_flags(&candidate_input, option_flags)?
            .map(|captures| RegexSetMatch {
                pattern_index,
                captures,
            }))
    }
}

/// A match from a RegexSet, including the pattern index and capture groups.
///
/// This type represents a single match found by a [`RegexSet`]. It provides
/// information about which pattern matched, the location of the match, and
/// access to any capture groups.
///
/// # Examples
///
/// ```rust
/// use fancy_regex::{RegexInput, RegexSet};
///
/// # fn main() -> Result<(), fancy_regex::Error> {
/// let set = RegexSet::new(&[r"\w+"])?;
/// let mut matches = set
///     .find_input(RegexInput::new("abc"))?
///     .expect("expected at least one match");
///
/// let m = matches.next().expect("expected first match")?;
/// assert_eq!(m.pattern(), 0);
/// assert_eq!(m.get().as_str(), "abc");
/// assert_eq!(m.start(), 0);
/// assert_eq!(m.end(), 3);
/// assert_eq!(m.captures().len(), 1);
/// # Ok(())
/// # }
/// ```
#[derive(Debug)]
pub struct RegexSetMatch<'t, S: Input + ?Sized> {
    pattern_index: usize,
    captures: Captures<'t, S>,
}

impl<'t, S: Input + ?Sized> RegexSetMatch<'t, S> {
    /// Returns the pattern index that matched.
    pub fn pattern(&self) -> usize {
        self.pattern_index
    }

    /// Returns the full set of capture groups for this match.
    pub fn captures(&self) -> &Captures<'t, S> {
        &self.captures
    }

    /// Returns the full match.
    pub fn get(&self) -> S::Match<'t> {
        self.captures
            .get(0)
            .expect("`RegexSetMatch` must always contain the overall match")
    }

    /// Returns the start offset of the full match.
    pub fn start(&self) -> usize {
        self.captures
            .get_span(0)
            .expect("`RegexSetMatch` must always contain the overall match")
            .0
    }

    /// Returns the end offset of the full match.
    pub fn end(&self) -> usize {
        self.captures
            .get_span(0)
            .expect("`RegexSetMatch` must always contain the overall match")
            .1
    }
}

impl<'t> RegexSetMatch<'t, str> {
    /// Returns the matched text.
    pub fn as_str(&self) -> &'t str {
        self.captures
            .get(0)
            .expect("`RegexSetMatch` must always contain the overall match")
            .as_str()
    }
}

#[derive(Debug)]
pub struct RegexSetMatchesAt<'r, 't, S: Input + ?Sized> {
    regex_set: &'r RegexSet,
    input: RegexInput<'t, S>,
    haystack: &'t S,
    match_start: usize,
    first_match: Option<RegexSetMatch<'t, S>>,
    pending_pattern_indices: alloc::vec::IntoIter<usize>,
}

impl<'r, 't, S: Input + ?Sized> RegexSetMatchesAt<'r, 't, S> {
    /// Returns the originating regex set.
    pub fn regex_set(&self) -> &'r RegexSet {
        self.regex_set
    }

    /// Returns the searched haystack.
    pub fn haystack(&self) -> &'t S {
        self.haystack
    }

    /// Returns the earliest start position shared by these matches.
    pub fn start(&self) -> usize {
        self.match_start
    }
}

impl<'r, 't, S: Input + ?Sized> Iterator for RegexSetMatchesAt<'r, 't, S> {
    type Item = Result<RegexSetMatch<'t, S>>;

    fn next(&mut self) -> Option<Self::Item> {
        if let Some(first_match) = self.first_match.take() {
            return Some(Ok(first_match));
        }

        for pattern_index in self.pending_pattern_indices.by_ref() {
            match self.regex_set.match_pattern_at_input_position(
                pattern_index,
                &self.input,
                self.match_start,
            ) {
                Ok(Some(regex_set_match)) => return Some(Ok(regex_set_match)),
                Ok(None) => continue,
                Err(err) => return Some(Err(err)),
            }
        }

        None
    }
}

#[cfg(test)]
mod tests {
    use super::{
        RegexSet, RegexSetOptions, DEFAULT_META_HYBRID_CACHE_CAPACITY, DEFAULT_META_NFA_SIZE_LIMIT,
        DEFAULT_OVERLAPPING_DFA_CACHE_CAPACITY,
    };
    use crate::{Error, RegexInput, RegexOptionsBuilder, RuntimeError};

    #[test]
    fn regex_set_options_defaults_to_larger_regex_automata_limits() {
        let options = RegexSetOptions::default();

        assert_eq!(
            options.meta_nfa_size_limit,
            Some(DEFAULT_META_NFA_SIZE_LIMIT)
        );
        assert_eq!(
            options.meta_hybrid_cache_capacity,
            DEFAULT_META_HYBRID_CACHE_CAPACITY
        );
        assert_eq!(
            options.overlapping_dfa_cache_capacity,
            DEFAULT_OVERLAPPING_DFA_CACHE_CAPACITY
        );
        assert!(options.overlapping_dfa_skip_cache_capacity_check);
    }

    #[test]
    fn regex_set_options_setters_update_regex_automata_limits() {
        let options = RegexSetOptions::new()
            .delegate_size_limit(11)
            .delegate_dfa_size_limit(22)
            .meta_nfa_size_limit(None)
            .meta_hybrid_cache_capacity(33)
            .overlapping_dfa_cache_capacity(44)
            .overlapping_dfa_skip_cache_capacity_check(false);

        assert_eq!(options.delegate_size_limit, Some(11));
        assert_eq!(options.delegate_dfa_size_limit, Some(22));
        assert_eq!(options.meta_nfa_size_limit, None);
        assert_eq!(options.meta_hybrid_cache_capacity, 33);
        assert_eq!(options.overlapping_dfa_cache_capacity, 44);
        assert!(!options.overlapping_dfa_skip_cache_capacity_check);
    }

    #[test]
    fn find_input_returns_all_matches_at_earliest_position_in_pattern_order() {
        let set = RegexSet::new(&[r"\d+", r"\w+", r"(?<=\$)\d+\.\d+"]).unwrap();
        let mut matches = set.find_input(RegexInput::new("$29.99")).unwrap().unwrap();

        let first = matches.next().unwrap().unwrap();
        assert_eq!(0, first.pattern());
        assert_eq!(1, first.start());
        assert_eq!(3, first.end());
        assert_eq!("29", first.as_str());

        let second = matches.next().unwrap().unwrap();
        assert_eq!(1, second.pattern());
        assert_eq!(1, second.start());
        assert_eq!(3, second.end());
        assert_eq!("29", second.as_str());

        let third = matches.next().unwrap().unwrap();
        assert_eq!(2, third.pattern());
        assert_eq!(1, third.start());
        assert_eq!(6, third.end());
        assert_eq!("29.99", third.as_str());

        assert!(matches.next().is_none());
    }

    #[test]
    fn find_input_skips_false_positive_candidate_positions() {
        let set = RegexSet::new(&[r"(?<=foo)bar"]).unwrap();
        let mut matches = set
            .find_input(RegexInput::new("barfoobar"))
            .unwrap()
            .unwrap();

        let only = matches.next().unwrap().unwrap();
        assert_eq!(0, only.pattern());
        assert_eq!(6, only.start());
        assert_eq!(9, only.end());
        assert_eq!("bar", only.as_str());
        assert!(matches.next().is_none());
    }

    #[test]
    fn find_input_returns_none_when_input_is_done() {
        let set = RegexSet::new(&[r"."]).unwrap();

        assert!(set
            .find_input(RegexInput::new("a").from_pos(2))
            .unwrap()
            .is_none());
    }

    #[test]
    fn find_input_returns_none_when_input_is_anchored_and_match_not_at_start_position() {
        let set = RegexSet::new(&[r"b"]).unwrap();

        assert!(set
            .find_input(RegexInput::new("ab").from_pos(0).anchored(true))
            .unwrap()
            .is_none());
    }

    #[test]
    fn find_input_returns_match_when_input_is_anchored_and_match_at_start_position() {
        let set = RegexSet::new(&[r"b"]).unwrap();

        let mut matches = set
            .find_input(RegexInput::new("ab").from_pos(1).anchored(true))
            .unwrap()
            .unwrap();

        let only = matches.next().unwrap().unwrap();
        assert_eq!(0, only.pattern());
        assert_eq!(1, only.start());
        assert_eq!(2, only.end());
        assert_eq!("b", only.as_str());
        assert!(matches.next().is_none());
    }

    #[test]
    fn find_input_defers_later_pattern_evaluation_until_iteration() {
        let mut options_builder = RegexOptionsBuilder::new();
        options_builder.backtrack_limit(0);
        let set = RegexSet::new_with_options(&[r"a", r"(?:(a|aa)+)\1"], &options_builder).unwrap();

        let mut matches = set.find_input(RegexInput::new("aa")).unwrap().unwrap();

        let first = matches.next().unwrap().unwrap();
        assert_eq!(0, first.pattern());
        assert_eq!(0, first.start());
        assert_eq!(1, first.end());

        let second = matches.next().unwrap();
        assert!(matches!(
            second,
            Err(Error::RuntimeError(RuntimeError::BacktrackLimitExceeded))
        ));
    }

    #[test]
    fn find_input_picks_earliest_start_position_before_iterating_pattern_order() {
        let mut options_builder = RegexOptionsBuilder::new();
        options_builder.multi_line(true);
        let set = RegexSet::new_with_options(
            &[
                r"//.*$",
                r#""(?:[^"\\]|\\.)*""#,
                r"\b(fn|let|mut|if|else)\b",
                r"\b[0-9]+\b",
                r"[a-zA-Z_][a-zA-Z0-9_]*",
            ],
            &options_builder,
        )
        .unwrap();

        let mut matches = set
            .find_input(RegexInput::new(
                "let x = 42; // a comment\nlet s = \"hello world\";",
            ))
            .unwrap()
            .unwrap();

        let first = matches.next().unwrap().unwrap();
        assert_eq!(2, first.pattern());
        assert_eq!(0, first.start());
        assert_eq!(3, first.end());
        assert_eq!("let", first.as_str());
    }

    #[test]
    fn find_input_yields_each_pattern_at_match_start_once() {
        let set = RegexSet::new(&[r"a+", r"a"]).unwrap();
        let mut matches = set.find_input(RegexInput::new("aaa")).unwrap().unwrap();

        let first = matches.next().unwrap().unwrap();
        assert_eq!(0, first.pattern());
        assert_eq!(0, first.start());
        assert_eq!(3, first.end());
        assert_eq!("aaa", first.as_str());

        let second = matches.next().unwrap().unwrap();
        assert_eq!(1, second.pattern());
        assert_eq!(0, second.start());
        assert_eq!(1, second.end());
        assert_eq!("a", second.as_str());

        assert!(matches.next().is_none());
    }

    #[test]
    fn test_no_captures_returns_group_0() {
        let set = RegexSet::new(&[r"\w+"]).unwrap();
        let mut matches = set.find_input(RegexInput::new("abc")).unwrap().unwrap();

        let only = matches.next().unwrap().unwrap();
        assert_eq!(0, only.pattern());
        assert_eq!(1, only.captures().len());
        assert_eq!("abc", only.as_str());
    }

    #[test]
    fn word_boundary_matches_correctly_with_unicode_text() {
        // \bbar\b uses a unicode word boundary; the DFA may encounter quit bytes
        // for multi-byte UTF-8 characters such as 'é' (0xC3 0xA9). The RegexSet
        // must not panic and must still return the correct match.
        let set = RegexSet::new([r"foo", r"\bbar\b"]).unwrap();
        let mut matches = set
            .find_input(RegexInput::new("fooé bar"))
            .unwrap()
            .unwrap();

        // "foo" is the earliest match (position 0)
        let first = matches.next().unwrap().unwrap();
        assert_eq!(0, first.pattern());
        assert_eq!("foo", first.as_str());
        assert!(matches.next().is_none());

        // "bar" is matched later in the string (after the unicode character)
        let mut matches2 = set
            .find_input(RegexInput::new("fooé bar").from_pos(first.end()))
            .unwrap()
            .unwrap();
        let bar_match = matches2.next().unwrap().unwrap();
        assert_eq!(1, bar_match.pattern());
        assert_eq!("bar", bar_match.as_str());
        assert!(matches2.next().is_none());
    }

    #[test]
    fn find_input_continue_from_prev_match_inside_negative_lookbehind() {
        let options = &mut RegexOptionsBuilder::new();
        let options = options.allow_input_assertion_overrides(true);
        let set = RegexSet::new_with_options([r"(?<!\G)b"], &options).unwrap();
        let mut matches = set
            .find_input(RegexInput::new("ab").continue_from_previous_match_end(false))
            .unwrap()
            .unwrap();

        let first = matches.next().unwrap().unwrap();
        assert_eq!(0, first.pattern());
        assert_eq!("b", first.as_str());
        assert!(matches.next().is_none());
    }

    #[test]
    fn continue_from_prev_match_works_as_expected_when_match_is_not_at_search_start() {
        use crate::Arc;
        use crate::RegexBuilder;

        let (pat, hay) = (r"\Gx", "yx");

        // `\G` holds only at the search start (index 0); 'x' is at 1 -> no match.
        let re = RegexBuilder::new(pat).build().unwrap();
        let single = re
            .find_input(RegexInput::new(hay).from_pos(0))
            .unwrap()
            .map(|m| (m.start(), m.end()));

        // Same regex, same input, via a RegexSet:
        let set = RegexSet::from_regexes([Arc::new(re)], Default::default()).unwrap();
        let via_set = set
            .find_input(RegexInput::new(hay).from_pos(0))
            .unwrap()
            .and_then(|mut it| it.next())
            .map(|m| {
                let m = m.unwrap();
                (m.start(), m.end())
            });

        assert_eq!(
            single, via_set,
            "RegexSet should behave the same as a standalone regex"
        );
        assert_eq!(single, None);
    }

    #[test]
    fn continue_from_prev_match_works_as_expected_when_match_is_at_search_start() {
        use crate::Arc;
        use crate::RegexBuilder;

        let (pat, hay) = (r"\Gx", "yx");

        let re = RegexBuilder::new(pat).build().unwrap();
        let single = re
            .find_input(RegexInput::new(hay).from_pos(1))
            .unwrap()
            .map(|m| (m.start(), m.end()));

        // Same regex, same input, via a RegexSet:
        let set = RegexSet::from_regexes([Arc::new(re)], Default::default()).unwrap();
        let via_set = set
            .find_input(RegexInput::new(hay).from_pos(1))
            .unwrap()
            .and_then(|mut it| it.next())
            .map(|m| {
                let m = m.unwrap();
                (m.start(), m.end())
            });

        assert_eq!(
            single, via_set,
            "RegexSet should behave the same as a standalone regex"
        );
        assert_eq!(single, Some((1, 2)));
    }
}
