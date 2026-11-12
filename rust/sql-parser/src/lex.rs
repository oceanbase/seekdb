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

pub(crate) fn space(byte: u8) -> bool {
    matches!(byte, b' ' | b'\t' | b'\n' | b'\r' | 0x0b | 0x0c)
}

pub(crate) fn identifier(byte: u8) -> bool {
    byte.is_ascii_alphanumeric() || matches!(byte, b'_' | b'$' | 0x80..=0xff)
}

pub fn identifier_requires_quotes(input: &[u8]) -> bool {
    const KEYWORDS: &[&[u8]] = &[
        b"alter",
        b"analyze",
        b"binary",
        b"binlog",
        b"cache",
        b"call",
        b"change",
        b"checksum",
        b"commit",
        b"create",
        b"database",
        b"delete",
        b"do",
        b"drop",
        b"errors",
        b"flush",
        b"function",
        b"get",
        b"grant",
        b"index",
        b"insert",
        b"into",
        b"kill",
        b"load",
        b"lock",
        b"logs",
        b"master",
        b"optimize",
        b"privileges",
        b"procedure",
        b"query",
        b"read",
        b"rename",
        b"replace",
        b"reset",
        b"revoke",
        b"select",
        b"set",
        b"show",
        b"slave",
        b"start",
        b"stop",
        b"table",
        b"tables",
        b"truncate",
        b"update",
        b"view",
        b"warnings",
        b"with",
        b"prepare",
        b"execute",
        b"deallocate",
        b"unique",
        b"temporary",
        b"savepoint",
        b"rollback",
        b"desc",
        b"describe",
        b"explain",
        b"release",
    ];
    input.first() == Some(&b'$')
        || input.iter().all(u8::is_ascii_digit)
        || input
            .iter()
            .any(|&byte| !byte.is_ascii() || !identifier(byte))
        || KEYWORDS
            .iter()
            .any(|keyword| input.eq_ignore_ascii_case(keyword))
}

pub fn normalize_numeric_sign(
    kind: crate::bridge::ffi::LiteralKind,
    integer: u64,
    text: &[u8],
    mode: crate::bridge::ffi::NumericSignMode,
) -> crate::bridge::ffi::NumericSignResult {
    use crate::bridge::ffi::{LiteralKind, NumericSignMode, NumericSignResult};
    let mut result = NumericSignResult {
        valid: true,
        integer,
        text_offset: 0,
        compact_text: Vec::new(),
    };
    match mode {
        NumericSignMode::Source => {}
        NumericSignMode::PositiveOperand => {
            let sign = text
                .iter()
                .position(|&byte| !space(byte))
                .unwrap_or(text.len());
            if text.get(sign) != Some(&b'-') {
                result.valid = false;
            } else {
                result.text_offset = sign + 1;
                while text
                    .get(result.text_offset)
                    .is_some_and(|&byte| space(byte))
                {
                    result.text_offset += 1;
                }
                if kind != LiteralKind::Decimal && (integer as i64) < 0 {
                    result.integer = integer.wrapping_neg();
                }
            }
        }
        NumericSignMode::CompactNegative => {
            if text.first() == Some(&b'-') {
                let mut offset = 1;
                while text.get(offset).is_some_and(|&byte| space(byte)) {
                    offset += 1;
                }
                if offset > 1 {
                    result.compact_text.reserve(text.len() - offset + 1);
                    result.compact_text.push(b'-');
                    result.compact_text.extend_from_slice(&text[offset..]);
                }
            }
        }
        _ => unreachable!("unknown numeric sign mode"),
    }
    result
}

pub fn integer_literal_is_nonnegative(value: i64, text: &[u8]) -> bool {
    value > 0 || (value == 0 && text.first() != Some(&b'-'))
}

pub(crate) fn quoted_end(input: &[u8], start: usize, backslash_escapes: bool) -> Option<usize> {
    let quote = input[start];
    let mut pos = start + 1;
    while let Some(&byte) = input.get(pos) {
        pos += 1;
        if byte == quote {
            if input.get(pos) == Some(&quote) {
                pos += 1;
            } else {
                return Some(pos);
            }
        } else if byte == b'\\' && backslash_escapes {
            pos += 1;
        }
    }
    None
}

pub(crate) fn decode_string(text: &[u8], no_backslash_escapes: bool) -> Vec<u8> {
    let mut result = Vec::with_capacity(text.len() - 2);
    decode_string_into(text, no_backslash_escapes, &mut result);
    result
}

pub(crate) fn decode_string_into(text: &[u8], no_backslash_escapes: bool, result: &mut Vec<u8>) {
    let quote = text[0];
    let bytes = &text[1..text.len() - 1];
    let mut pos = 0;
    while pos < bytes.len() {
        let byte = bytes[pos];
        pos += 1;
        if byte == quote {
            pos += 1;
            result.push(byte);
        } else if byte == b'\\' && !no_backslash_escapes {
            let escaped = bytes[pos];
            pos += 1;
            let decoded = match escaped {
                b'0' => 0,
                b'b' => 8,
                b'n' => b'\n',
                b'r' => b'\r',
                b't' => b'\t',
                b'Z' => 26,
                b'%' | b'_' => {
                    result.push(b'\\');
                    escaped
                }
                byte => byte,
            };
            result.push(decoded);
        } else {
            result.push(byte);
        }
    }
}

pub fn decode_binary_literal(
    text: &[u8],
    notation: crate::bridge::ffi::BinaryNotation,
) -> Vec<u8> {
    let mut result = Vec::new();
    decode_binary_into(text, notation, &mut result);
    result
}

pub(crate) fn decode_binary_into(
    text: &[u8],
    notation: crate::bridge::ffi::BinaryNotation,
    result: &mut Vec<u8>,
) {
    use crate::bridge::ffi::BinaryNotation;
    let bits = match notation {
        BinaryNotation::Hex => 4,
        BinaryNotation::Bit => 1,
        _ => unreachable!(),
    };
    let start = result.len();
    let length = (text.len() * bits).div_ceil(8);
    result.resize(start + length, 0);
    let bytes = &mut result[start..];
    let padding = length * 8 - text.len() * bits;
    for (i, byte) in text.iter().copied().enumerate() {
        let value = if byte.is_ascii_digit() {
            byte - b'0'
        } else {
            byte.to_ascii_lowercase() - b'a' + 10
        };
        let offset = padding + i * bits;
        bytes[offset / 8] |= value << (8 - bits - offset % 8);
    }
}
