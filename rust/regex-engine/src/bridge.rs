// Copyright (c) 2026 OceanBase.
//
// Licensed under the Apache License, Version 2.0 (the "License");
// you may not use this file except in compliance with the License.
// You may obtain a copy of the License at
//
//     http://www.apache.org/licenses/LICENSE-2.0
//
// Unless required by applicable law or agreed to in writing, software
// distributed under the License is distributed on an "AS IS" BASIS,
// WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
// See the License for the specific language governing permissions and
// limitations under the License.

//! Generated C++ interface to immutable compiled regular expressions.
//!
//! Text buffers contain UTF-16BE without a BOM; positions count zero-based
//! UTF-16 code units. A position inside a surrogate pair is invalid. A null
//! pointer is valid only for a zero-length buffer. Output pointers must be
//! non-null and writable; error may be null. Operations return zero on success
//! and one on error. A handle may be searched concurrently but must not be freed
//! during a call. The caller frees every returned handle and output buffer once.
//!
//! Limits must be nonnegative and deliberately differ from ICU: stack_limit
//! sets per-delegate compilation/cache budgets and a VM instruction budget;
//! time_limit sets a backtracking-count budget, not a wall-clock timeout.

use crate::{regex_buffer_free, regex_compile, regex_find, regex_free, regex_replace, RegexEngine};

// Keep pointer/length pairs and SQL search options explicit at this boundary.
#[allow(clippy::too_many_arguments)]
#[cxx::bridge(namespace = "oceanbase::sql")]
pub mod ffi {
    #[repr(u32)]
    enum RegexFlags {
        CaseInsensitive = 1,
        Multiline = 2,
        DotAll = 4,
        UnixLines = 8,
    }

    #[repr(i32)]
    enum RegexErrorKind {
        None = 0,
        Generic = 1,
        Paren = 2,
        Bracket = 3,
        Escape = 4,
        InvalidEncoding = 5,
        Limit = 6,
        Allocation = 7,
        Argument = 8,
    }

    struct RegexError {
        kind: i32,
        message: [c_char; 256],
    }

    #[derive(Debug, PartialEq, Eq)]
    struct RegexMatch {
        matched: i32,
        start: i64,
        end: i64,
    }

    struct RegexBuffer {
        data: *mut u8,
        len: usize,
    }

    extern "Rust" {
        type RegexEngine;

        unsafe fn regex_compile(
            pattern: *const u8,
            pattern_len: usize,
            flags: u32,
            stack_limit: i64,
            time_limit: i64,
            out: *mut *mut RegexEngine,
            error: *mut RegexError,
        ) -> i32;

        unsafe fn regex_free(regex: *mut RegexEngine);

        /// occurrence is one-based; group zero selects the whole match. A
        /// missing match or unmatched optional group yields {0, -1, -1}.
        /// A group beyond the capture count errors only when the selected
        /// whole-match occurrence exists. Negative groups are always invalid.
        unsafe fn regex_find(
            regex: *const RegexEngine,
            text: *const u8,
            text_len: usize,
            start: i64,
            occurrence: i64,
            group: i64,
            out: *mut RegexMatch,
            error: *mut RegexError,
        ) -> i32;

        /// occurrence zero replaces all matches at/after start; otherwise only
        /// the selected one-based occurrence is replaced. The prefix is kept.
        /// ICU-style $n, ${name}, backslash quoting, and Unicode replacement
        /// escapes are supported. Runtime errors are propagated. The caller
        /// must release the returned UTF-16BE buffer with buffer_free.
        unsafe fn regex_replace(
            regex: *const RegexEngine,
            text: *const u8,
            text_len: usize,
            replacement: *const u8,
            replacement_len: usize,
            start: i64,
            occurrence: i64,
            out: *mut RegexBuffer,
            error: *mut RegexError,
        ) -> i32;

        unsafe fn regex_buffer_free(buffer: *mut RegexBuffer);
    }
}
