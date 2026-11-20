//! Byte-level matching types for `fancy_regex`.
//!
//! This module provides types for working with regex matches on raw byte
//! sequences (`&[u8]`) that may not be valid UTF-8.

use core::ops::Range;

/// A single match of a regex in a byte slice.
///
/// Similar to [`crate::Match`] but operates on `&[u8]` instead of `&str`.
#[derive(Copy, Clone, Debug, Eq, PartialEq)]
pub struct MatchBytes<'t> {
    pub(crate) bytes: &'t [u8],
    pub(crate) match_start: usize,
    pub(crate) match_end: usize,
}

impl<'t> MatchBytes<'t> {
    /// Returns the starting byte offset of the match.
    #[inline]
    pub fn start(&self) -> usize {
        self.match_start
    }

    /// Returns the ending byte offset of the match.
    #[inline]
    pub fn end(&self) -> usize {
        self.match_end
    }

    /// Returns the range over the starting and ending byte offsets.
    #[inline]
    pub fn range(&self) -> Range<usize> {
        self.match_start..self.match_end
    }

    /// Returns the matched bytes.
    #[inline]
    pub fn as_bytes(&self) -> &'t [u8] {
        &self.bytes[self.match_start..self.match_end]
    }

    /// Creates a new match from the given bytes and byte offsets.
    pub(crate) fn new(bytes: &'t [u8], start: usize, end: usize) -> MatchBytes<'t> {
        MatchBytes {
            bytes,
            match_start: start,
            match_end: end,
        }
    }
}

impl<'t> From<MatchBytes<'t>> for &'t [u8] {
    fn from(m: MatchBytes<'t>) -> &'t [u8] {
        m.as_bytes()
    }
}

impl<'t> From<MatchBytes<'t>> for Range<usize> {
    fn from(m: MatchBytes<'t>) -> Range<usize> {
        m.range()
    }
}
