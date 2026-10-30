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

//! UTF-16BE CXX interface to the SQL regular-expression engine.
//!
//! This intentionally implements fancy-regex syntax rather than promising ICU
//! equivalence. In particular Unicode case folding, character classes, newline
//! handling, and execution-budget units differ. C++ types and entry points are
//! generated from `bridge.rs` through the repository's CXX build integration.

mod bridge;

pub use bridge::ffi::{RegexBuffer, RegexError, RegexErrorKind, RegexFlags, RegexMatch};

use fancy_regex::{Captures, CompileError, Error, ParseError, Regex, RegexBuilder};
use std::borrow::Cow;
use std::ffi::c_char;
use std::panic::{catch_unwind, AssertUnwindSafe};
use std::ptr;

const CASE_INSENSITIVE: u32 = RegexFlags::CaseInsensitive.repr;
const MULTILINE: u32 = RegexFlags::Multiline.repr;
const DOTALL: u32 = RegexFlags::DotAll.repr;
const UNIX_LINES: u32 = RegexFlags::UnixLines.repr;
const MAX_BUFFER_BYTES: usize = i32::MAX as usize;
const MAX_PATTERN_BYTES: usize = 1024 * 1024;
const DEFAULT_COMPILED_BYTES: usize = 8 * 1024 * 1024;
const MAX_COMPILED_BYTES: usize = 64 * 1024 * 1024;
const DEFAULT_BACKTRACKS: usize = 1_000_000;
const MAX_BACKTRACKS: usize = 100_000_000;

const GENERIC: i32 = RegexErrorKind::Generic.repr;
const PAREN: i32 = RegexErrorKind::Paren.repr;
const BRACKET: i32 = RegexErrorKind::Bracket.repr;
const ESCAPE: i32 = RegexErrorKind::Escape.repr;
const INVALID_ENCODING: i32 = RegexErrorKind::InvalidEncoding.repr;
const LIMIT: i32 = RegexErrorKind::Limit.repr;
const ALLOCATION: i32 = RegexErrorKind::Allocation.repr;
const ARGUMENT: i32 = RegexErrorKind::Argument.repr;

pub struct RegexEngine {
    regex: Regex,
}

#[derive(Debug)]
struct BridgeError {
    kind: i32,
    message: Cow<'static, str>,
}

type Result<T> = std::result::Result<T, BridgeError>;

fn err(kind: i32, message: impl Into<Cow<'static, str>>) -> BridgeError {
    BridgeError {
        kind,
        message: message.into(),
    }
}

fn regex_error(error: Error) -> BridgeError {
    let kind = match &error {
        Error::RuntimeError(_) => LIMIT,
        Error::ParseError(_, ParseError::UnclosedOpenParen) => PAREN,
        Error::ParseError(_, ParseError::InvalidClass) => BRACKET,
        Error::ParseError(
            _,
            ParseError::TrailingBackslash
            | ParseError::InvalidEscape(_)
            | ParseError::UnclosedUnicodeName
            | ParseError::InvalidHex
            | ParseError::InvalidCodepointValue,
        ) => ESCAPE,
        Error::ParseError(_, ParseError::RecursionExceeded) => LIMIT,
        Error::CompileError(e) if matches!(e.as_ref(), CompileError::PatternTooComplex) => LIMIT,
        Error::CompileError(e)
            if matches!(e.as_ref(), CompileError::InnerError(inner)
            if inner.size_limit().is_some()) =>
        {
            LIMIT
        }
        _ => GENERIC,
    };
    err(kind, error.to_string())
}

// catch_unwind also protects debug/test consumers. Release builds use the
// repository's panic=abort policy; allocation failures inside dependencies can
// still abort rather than returning an allocation error.
unsafe fn ffi_call(error: *mut RegexError, f: impl FnOnce() -> Result<()>) -> i32 {
    let result = match catch_unwind(AssertUnwindSafe(f)) {
        Ok(result) => result,
        Err(_) => Err(err(GENERIC, "regular-expression engine panicked")),
    };
    if !error.is_null() {
        // SAFETY: C caller provides a writable error object when non-null.
        let error = unsafe { &mut *error };
        error.kind = 0;
        error.message.fill(0);
        if let Err(failure) = &result {
            error.kind = failure.kind;
            let message = failure.message.as_bytes();
            let mut len = message.len().min(error.message.len() - 1);
            while !failure.message.is_char_boundary(len) {
                len -= 1;
            }
            for (dst, src) in error.message.iter_mut().zip(&message[..len]) {
                *dst = *src as c_char;
            }
        }
    }
    i32::from(result.is_err())
}

unsafe fn decode_utf16be(data: *const u8, len: usize) -> Result<String> {
    if len > MAX_BUFFER_BYTES {
        return Err(err(
            LIMIT,
            "regular-expression input exceeds INT32_MAX bytes",
        ));
    }
    if !len.is_multiple_of(2) {
        return Err(err(
            INVALID_ENCODING,
            "UTF-16BE input has an odd byte length",
        ));
    }
    if data.is_null() && len != 0 {
        return Err(err(ARGUMENT, "nonempty input has a null pointer"));
    }
    let bytes = if len == 0 {
        &[]
    } else {
        // SAFETY: non-null C input points to len readable bytes by the contract.
        unsafe { std::slice::from_raw_parts(data, len) }
    };
    let mut text = String::new();
    // At most three UTF-8 bytes per UTF-16 code unit, including invalid inputs.
    text.try_reserve(
        len.checked_mul(3)
            .ok_or_else(|| err(LIMIT, "input size overflow"))?
            / 2,
    )
    .map_err(|_| {
        err(
            ALLOCATION,
            "cannot allocate decoded regular-expression input",
        )
    })?;
    let units = bytes
        .as_chunks::<2>()
        .0
        .iter()
        .map(|pair| u16::from_be_bytes([pair[0], pair[1]]));
    for scalar in char::decode_utf16(units) {
        text.push(scalar.map_err(|_| err(INVALID_ENCODING, "unpaired UTF-16 surrogate"))?);
    }
    Ok(text)
}

fn utf16_to_byte(text: &str, start: i64) -> Result<usize> {
    let start = usize::try_from(start).map_err(|_| err(ARGUMENT, "negative start position"))?;
    let mut units = 0;
    for (byte, scalar) in text.char_indices() {
        if units == start {
            return Ok(byte);
        }
        units += scalar.len_utf16();
        if units > start {
            return Err(err(
                ARGUMENT,
                "start position splits a UTF-16 surrogate pair",
            ));
        }
    }
    if units == start {
        Ok(text.len())
    } else {
        Err(err(ARGUMENT, "start position exceeds input"))
    }
}

fn checked_slice(text: &str, start: usize, end: usize) -> Result<&str> {
    text.get(start..end).ok_or_else(|| {
        err(
            GENERIC,
            "regular-expression engine returned an invalid capture span",
        )
    })
}

fn byte_to_utf16(text: &str, byte: usize) -> Result<i64> {
    Ok(checked_slice(text, 0, byte)?.encode_utf16().count() as i64)
}

fn limits(stack_limit: i64, time_limit: i64) -> Result<(usize, usize)> {
    if stack_limit < 0 || time_limit < 0 {
        return Err(err(
            ARGUMENT,
            "regular-expression limits must not be negative",
        ));
    }
    let compiled = if stack_limit == 0 {
        DEFAULT_COMPILED_BYTES
    } else {
        stack_limit.min(MAX_COMPILED_BYTES as i64) as usize
    };
    let backtracks = if time_limit == 0 {
        DEFAULT_BACKTRACKS
    } else {
        time_limit.min((MAX_BACKTRACKS / 10_000) as i64) as usize * 10_000
    };
    Ok((compiled, backtracks))
}

fn compile(pattern: String, flags: u32, stack_limit: i64, time_limit: i64) -> Result<RegexEngine> {
    if flags & !(CASE_INSENSITIVE | MULTILINE | DOTALL | UNIX_LINES) != 0 {
        return Err(err(ARGUMENT, "unknown regular-expression flags"));
    }
    let (compiled_bytes, backtracks) = limits(stack_limit, time_limit)?;
    let mut builder = RegexBuilder::new(&pattern);
    builder
        .icu_octal(true)
        .icu_posix_unicode(true)
        .case_insensitive(flags & CASE_INSENSITIVE != 0)
        .multi_line(flags & MULTILINE != 0)
        .dot_matches_new_line(flags & DOTALL != 0)
        .crlf(flags & UNIX_LINES == 0)
        .backtrack_limit(backtracks)
        .delegate_size_limit(compiled_bytes)
        .delegate_dfa_size_limit(compiled_bytes)
        .delegate_dfa_skip_cache_capacity_check(false)
        .max_prog_size((compiled_bytes / 64).max(1));
    Ok(RegexEngine {
        regex: builder.build().map_err(regex_error)?,
    })
}

/// Compile a UTF-16BE pattern into an immutable, reusable handle.
///
/// # Safety
/// Buffers must be readable for their lengths. `out` must be writable and
/// non-null; `error`, if non-null, must be writable. See the bridge contract.
pub unsafe fn regex_compile(
    pattern: *const u8,
    pattern_len: usize,
    flags: u32,
    stack_limit: i64,
    time_limit: i64,
    out: *mut *mut RegexEngine,
    error: *mut RegexError,
) -> i32 {
    // SAFETY: the caller follows the pointer contract documented above.
    unsafe {
        ffi_call(error, || {
            if out.is_null() {
                return Err(err(ARGUMENT, "null compiled-pattern output"));
            }
            *out = ptr::null_mut();
            if pattern_len > MAX_PATTERN_BYTES {
                return Err(err(LIMIT, "pattern exceeds 1 MiB"));
            }
            let pattern = decode_utf16be(pattern, pattern_len)?;
            *out = Box::into_raw(Box::new(compile(pattern, flags, stack_limit, time_limit)?));
            Ok(())
        })
    }
}

/// Free a compiled handle. Null is accepted.
///
/// # Safety
/// `regex` must be null or a live handle returned by this library, with no calls
/// in progress. It must be freed exactly once.
pub unsafe fn regex_free(regex: *mut RegexEngine) {
    if !regex.is_null() {
        // SAFETY: ownership is returned once by the C caller.
        drop(unsafe { Box::from_raw(regex) });
    }
}

// Search against the full input so lookbehind, ^, and word boundaries retain
// their context when the caller chooses a nonzero SQL position. This differs
// from searching text[start..], which would invent a new beginning of input.
fn next_captures<'t>(
    regex: &Regex,
    text: &'t str,
    next: &mut Option<usize>,
) -> Result<Option<Captures<'t, str>>> {
    let Some(pos) = *next else {
        return Ok(None);
    };
    let captures = regex.captures_from_pos(text, pos).map_err(regex_error)?;
    if let Some(captures) = &captures {
        let found = captures
            .get(0)
            .ok_or_else(|| err(GENERIC, "missing whole-match capture"))?;
        // Extensions such as KeepOut inside lookbehind can make fancy-regex
        // report matches beginning before the requested search position. Such
        // spans break SQL occurrence iteration and can overlap already emitted
        // replacement text. Reject them before advancing or slicing input.
        checked_slice(text, found.start(), found.end())?;
        if found.start() < pos {
            return Err(err(
                GENERIC,
                "regular-expression match starts before the search position",
            ));
        }
        *next = if found.start() != found.end() {
            Some(found.end())
        } else {
            checked_slice(text, found.end(), text.len())?
                .chars()
                .next()
                .map(|ch| found.end() + ch.len_utf8())
        };
    } else {
        *next = None;
    }
    Ok(captures)
}

fn find(regex: &Regex, text: &str, start: i64, occurrence: i64, group: i64) -> Result<RegexMatch> {
    let mut next = Some(utf16_to_byte(text, start)?);
    if occurrence < 1 || group < 0 {
        return Err(err(ARGUMENT, "invalid occurrence or capture-group index"));
    }
    let absent = RegexMatch {
        matched: 0,
        start: -1,
        end: -1,
    };
    for index in 1..=occurrence {
        let Some(captures) = next_captures(regex, text, &mut next)? else {
            return Ok(absent);
        };
        if index == occurrence {
            // ICU only resolves the capture index after the requested whole
            // match exists. Preserve absent results for a missing occurrence
            // even when the positive group index would otherwise be invalid.
            if group as u64 >= regex.captures_len() as u64 {
                return Err(err(ARGUMENT, "invalid capture-group index"));
            }
            return Ok(match captures.get(group as usize) {
                Some(found) => {
                    checked_slice(text, found.start(), found.end())?;
                    RegexMatch {
                        matched: 1,
                        start: byte_to_utf16(text, found.start())?,
                        end: byte_to_utf16(text, found.end())?,
                    }
                }
                None => absent,
            });
        }
    }
    Ok(absent)
}

/// Return the selected occurrence's UTF-16 code-unit span.
///
/// # Safety
/// `regex` must be a live compiled handle, input must be readable, and `out`
/// must be writable. The handle must not be freed during this call.
#[allow(clippy::too_many_arguments)]
pub unsafe fn regex_find(
    regex: *const RegexEngine,
    text: *const u8,
    text_len: usize,
    start: i64,
    occurrence: i64,
    group: i64,
    out: *mut RegexMatch,
    error: *mut RegexError,
) -> i32 {
    // SAFETY: the caller follows the pointer contract documented above.
    unsafe {
        ffi_call(error, || {
            if out.is_null() {
                return Err(err(ARGUMENT, "null match output"));
            }
            *out = RegexMatch {
                matched: 0,
                start: -1,
                end: -1,
            };
            if regex.is_null() {
                return Err(err(ARGUMENT, "null compiled pattern"));
            }
            let text = decode_utf16be(text, text_len)?;
            *out = find(&(*regex).regex, &text, start, occurrence, group)?;
            Ok(())
        })
    }
}

enum Replacement<'a> {
    Literal(&'a str),
    Quoted(char),
    // ICU Unicode replacement escapes can deliberately produce a lone
    // surrogate. Keep that code unit for the caller's charset conversion.
    CodeUnit(u16),
    Group(usize),
}

fn parse_replacement<'a>(regex: &Regex, replacement: &'a str) -> Result<Vec<Replacement<'a>>> {
    let mut tokens = Vec::new();
    let mut iter = replacement.char_indices().peekable();
    let mut literal_start = 0;
    while let Some((byte, ch)) = iter.next() {
        if ch != '$' && ch != '\\' {
            continue;
        }
        tokens
            .try_reserve(2)
            .map_err(|_| err(ALLOCATION, "cannot allocate replacement tokens"))?;
        if literal_start < byte {
            tokens.push(Replacement::Literal(&replacement[literal_start..byte]));
        }
        let next = match iter.next() {
            Some((_, next)) => next,
            // ICU appendReplacement consumes a trailing backslash without
            // emitting it; retain this behavior rather than treating it as
            // the (different) invalid trailing backslash in a pattern.
            None if ch == '\\' => {
                literal_start = replacement.len();
                break;
            }
            None => return Err(err(GENERIC, "incomplete replacement group")),
        };
        if ch == '\\' {
            // ICU decodes valid \uHHHH and \UHHHHHHHH escapes, but quotes
            // only the letter if the escape is malformed.
            let digits = match next {
                'u' => 4,
                'U' => 8,
                _ => 0,
            };
            let mut probe = iter.clone();
            let mut codepoint = Some(0u32);
            for _ in 0..digits {
                codepoint = codepoint.and_then(|value| {
                    let (_, digit) = probe.next()?;
                    value.checked_mul(16)?.checked_add(digit.to_digit(16)?)
                });
            }
            if digits != 0 && codepoint.is_some_and(|value| value <= 0x10ffff) {
                let value = codepoint.unwrap();
                iter = probe;
                if value <= 0xffff {
                    tokens.push(Replacement::CodeUnit(value as u16));
                } else {
                    tokens.push(Replacement::Quoted(char::from_u32(value).unwrap()));
                }
            } else {
                tokens.push(Replacement::Quoted(next));
            }
        } else if next == '{' {
            let name_start = iter
                .peek()
                .map(|(pos, _)| *pos)
                .unwrap_or(replacement.len());
            let mut name_end = None;
            for (pos, ch) in iter.by_ref() {
                if ch == '}' {
                    name_end = Some(pos);
                    break;
                }
            }
            let name = name_end
                .map(|end| &replacement[name_start..end])
                .ok_or_else(|| err(GENERIC, "unclosed replacement group name"))?;
            if !name.as_bytes().first().is_some_and(u8::is_ascii_alphabetic)
                || !name.bytes().all(|ch| ch.is_ascii_alphanumeric())
            {
                return Err(err(GENERIC, "invalid replacement group name"));
            }
            let group = regex
                .capture_names()
                .position(|candidate| candidate == Some(name))
                .ok_or_else(|| err(GENERIC, "unknown replacement group name"))?;
            tokens.push(Replacement::Group(group));
        } else if next.is_ascii_digit() {
            let mut group = (next as u8 - b'0') as usize;
            if group >= regex.captures_len() {
                return Err(err(GENERIC, "replacement capture group does not exist"));
            }
            while let Some((_, next)) = iter.peek() {
                if !next.is_ascii_digit() {
                    break;
                }
                let Some(candidate) = group
                    .checked_mul(10)
                    .and_then(|n| n.checked_add((*next as u8 - b'0') as usize))
                else {
                    break;
                };
                if candidate >= regex.captures_len() {
                    break;
                }
                group = candidate;
                iter.next();
            }
            tokens.push(Replacement::Group(group));
        } else {
            return Err(err(
                GENERIC,
                "replacement $ must introduce a numeric or braced named group",
            ));
        }
        literal_start = iter
            .peek()
            .map(|(pos, _)| *pos)
            .unwrap_or(replacement.len());
    }
    if literal_start < replacement.len() {
        tokens
            .try_reserve(1)
            .map_err(|_| err(ALLOCATION, "cannot allocate replacement tokens"))?;
        tokens.push(Replacement::Literal(&replacement[literal_start..]));
    }
    Ok(tokens)
}

// Build the final encoding directly, so the byte cap applies to every growth
// operation and no intermediate unchecked replacement string is constructed.
fn append_utf16be(out: &mut Vec<u8>, text: &str) -> Result<()> {
    let bytes = text
        .encode_utf16()
        .count()
        .checked_mul(2)
        .ok_or_else(|| err(LIMIT, "replacement size overflow"))?;
    if out
        .len()
        .checked_add(bytes)
        .is_none_or(|len| len > MAX_BUFFER_BYTES)
    {
        return Err(err(LIMIT, "replacement output exceeds INT32_MAX bytes"));
    }
    out.try_reserve(bytes)
        .map_err(|_| err(ALLOCATION, "cannot allocate replacement output"))?;
    for unit in text.encode_utf16() {
        out.extend_from_slice(&unit.to_be_bytes());
    }
    Ok(())
}

fn append_code_unit(out: &mut Vec<u8>, unit: u16) -> Result<()> {
    if out.len() > MAX_BUFFER_BYTES - 2 {
        return Err(err(LIMIT, "replacement output exceeds INT32_MAX bytes"));
    }
    out.try_reserve(2)
        .map_err(|_| err(ALLOCATION, "cannot allocate replacement output"))?;
    out.extend_from_slice(&unit.to_be_bytes());
    Ok(())
}

fn replace(
    regex: &Regex,
    text: &str,
    replacement: &str,
    start: i64,
    occurrence: i64,
) -> Result<Vec<u8>> {
    if occurrence < 0 {
        return Err(err(ARGUMENT, "negative replacement occurrence"));
    }
    let mut next = Some(utf16_to_byte(text, start)?);
    let mut out = Vec::new();
    let mut emitted = 0;
    let mut index = 0i64;
    let mut tokens = None;
    while let Some(captures) = next_captures(regex, text, &mut next)? {
        index += 1;
        if occurrence != 0 && occurrence != index {
            continue;
        }
        let found = captures
            .get(0)
            .ok_or_else(|| err(GENERIC, "missing whole-match capture"))?;
        append_utf16be(&mut out, checked_slice(text, emitted, found.start())?)?;
        let tokens = match &tokens {
            Some(tokens) => tokens,
            None => tokens.insert(parse_replacement(regex, replacement)?),
        };
        for token in tokens {
            match token {
                Replacement::Literal(literal) => append_utf16be(&mut out, literal)?,
                Replacement::Quoted(ch) => append_utf16be(&mut out, ch.encode_utf8(&mut [0; 4]))?,
                Replacement::CodeUnit(unit) => append_code_unit(&mut out, *unit)?,
                Replacement::Group(group) => {
                    if let Some(capture) = captures.get(*group) {
                        append_utf16be(
                            &mut out,
                            checked_slice(text, capture.start(), capture.end())?,
                        )?;
                    }
                }
            }
        }
        emitted = found.end();
        if occurrence != 0 {
            break;
        }
    }
    append_utf16be(&mut out, checked_slice(text, emitted, text.len())?)?;
    Ok(out)
}

/// Replace occurrences and return an owned UTF-16BE output buffer.
///
/// # Safety
/// `regex` must be live, inputs readable for their lengths, and `out` writable
/// and fresh. Free the returned buffer with `regex_buffer_free`.
#[allow(clippy::too_many_arguments)]
pub unsafe fn regex_replace(
    regex: *const RegexEngine,
    text: *const u8,
    text_len: usize,
    replacement: *const u8,
    replacement_len: usize,
    start: i64,
    occurrence: i64,
    out: *mut RegexBuffer,
    error: *mut RegexError,
) -> i32 {
    // SAFETY: the caller follows the pointer contract documented above.
    unsafe {
        ffi_call(error, || {
            if out.is_null() {
                return Err(err(ARGUMENT, "null replacement output"));
            }
            *out = RegexBuffer {
                data: ptr::null_mut(),
                len: 0,
            };
            if regex.is_null() {
                return Err(err(ARGUMENT, "null compiled pattern"));
            }
            let text = decode_utf16be(text, text_len)?;
            let replacement = decode_utf16be(replacement, replacement_len)?;
            let result = replace(&(*regex).regex, &text, &replacement, start, occurrence)?;
            if !result.is_empty() {
                let mut bytes = result.into_boxed_slice();
                *out = RegexBuffer {
                    data: bytes.as_mut_ptr(),
                    len: bytes.len(),
                };
                std::mem::forget(bytes);
            }
            Ok(())
        })
    }
}

/// Free a replacement buffer and reset it to empty. Null is accepted.
///
/// # Safety
/// The object must contain a buffer returned by this library, unmodified and
/// not previously freed, or an empty `{null, 0}` buffer.
pub unsafe fn regex_buffer_free(buffer: *mut RegexBuffer) {
    if !buffer.is_null() {
        // SAFETY: the C caller returns the original pointer and length once.
        unsafe {
            if !(*buffer).data.is_null() {
                drop(Box::from_raw(ptr::slice_from_raw_parts_mut(
                    (*buffer).data,
                    (*buffer).len,
                )));
            }
            *buffer = RegexBuffer {
                data: ptr::null_mut(),
                len: 0,
            };
        }
    }
}

#[cfg(test)]
mod tests;
