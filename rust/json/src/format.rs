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

use crate::{BinaryError, BinaryErrorKind, BinaryValue, JsonType, Number};
use std::io::Write;

#[derive(Clone, Copy, Debug)]
pub struct FormatOptions {
    pub quoted: bool,
    pub pretty: bool,
    pub depth: usize,
}

impl Default for FormatOptions {
    fn default() -> Self {
        Self {
            quoted: true,
            pretty: false,
            depth: 0,
        }
    }
}

#[derive(Debug)]
pub enum FormatError<E> {
    Binary(BinaryError),
    Scalar(E),
}

impl<E> From<BinaryError> for FormatError<E> {
    fn from(error: BinaryError) -> Self {
        Self::Binary(error)
    }
}

/// Formats borrowed binary JSON without interpreting database scalar payloads.
/// The scalar formatter runs synchronously and returns complete scalar text,
/// including quotes when requested. It must not retain the borrowed value.
pub fn format<E>(
    value: BinaryValue<'_>,
    options: FormatOptions,
    scalar: &mut impl FnMut(BinaryValue<'_>, bool) -> Result<Vec<u8>, E>,
) -> Result<Vec<u8>, FormatError<E>> {
    enum Task<'a> {
        Value(BinaryValue<'a>, usize, bool),
        Entries(BinaryValue<'a>, u64, usize),
    }
    let depth_bound = value.traversal_limit();
    let mut output = Vec::new();
    let mut tasks = vec![Task::Value(value, options.depth, options.quoted)];
    while let Some(task) = tasks.pop() {
        match task {
            Task::Value(value, depth, quoted) => {
                if depth - options.depth > depth_bound {
                    return Err(BinaryError::new(BinaryErrorKind::InvalidDepth, 0).into());
                }
                let kind = value.json_type()?;
                match kind {
                    JsonType::Object | JsonType::Array => {
                        output.push(if kind == JsonType::Object { b'{' } else { b'[' });
                        tasks.push(Task::Entries(value, 0, depth));
                    }
                    JsonType::Null => output.extend_from_slice(b"null"),
                    JsonType::Boolean => {
                        output.extend_from_slice(if value.boolean()? { b"true" } else { b"false" })
                    }
                    JsonType::Int | JsonType::Uint | JsonType::OInt | JsonType::OLong => {
                        match value.number()? {
                            Number::I64(value) => {
                                write!(&mut output, "{value}").expect("writing to a Vec")
                            }
                            Number::U64(value) => {
                                write!(&mut output, "{value}").expect("writing to a Vec")
                            }
                            Number::F64(_) => unreachable!(),
                        };
                    }
                    JsonType::Double | JsonType::ODouble => {
                        let Number::F64(value) = value.number()? else {
                            unreachable!()
                        };
                        format_double(value, &mut output);
                    }
                    _ if kind.is_string() => {
                        append_string(value.string_bytes()?, quoted, &mut output)
                    }
                    _ => output.extend(scalar(value, quoted).map_err(FormatError::Scalar)?),
                }
            }
            Task::Entries(value, index, depth) => {
                let object = value.json_type()? == JsonType::Object;
                let count = value.len()?;
                if index == count {
                    if options.pretty && count != 0 {
                        indent(depth, &mut output)?;
                    }
                    output.push(if object { b'}' } else { b']' });
                } else {
                    if index != 0 {
                        output.push(b',');
                        if !options.pretty {
                            output.push(b' ');
                        }
                    }
                    let child_depth = depth
                        .checked_add(1)
                        .ok_or_else(|| BinaryError::new(BinaryErrorKind::InvalidDepth, 0))?;
                    if options.pretty {
                        indent(child_depth, &mut output)?;
                    }
                    if object {
                        append_string(value.key(index)?, true, &mut output);
                        output.extend_from_slice(b": ");
                    }
                    tasks.push(Task::Entries(value, index + 1, depth));
                    tasks.push(Task::Value(value.value(index)?, child_depth, true));
                }
            }
        }
    }
    Ok(output)
}

fn indent(depth: usize, output: &mut Vec<u8>) -> Result<(), BinaryError> {
    output.push(b'\n');
    let size = depth
        .checked_mul(2)
        .and_then(|size| size.checked_add(output.len()))
        .ok_or_else(|| BinaryError::new(BinaryErrorKind::InvalidDepth, 0))?;
    output.resize(size, b' ');
    Ok(())
}

pub fn append_string(value: &[u8], quoted: bool, output: &mut Vec<u8>) {
    if !quoted {
        output.extend_from_slice(value);
        return;
    }
    output.push(b'"');
    const HEX: &[u8] = b"0123456789abcdef";
    for &byte in value {
        match byte {
            b'"' => output.extend_from_slice(b"\\\""),
            b'\\' => output.extend_from_slice(b"\\\\"),
            b'\x08' => output.extend_from_slice(b"\\b"),
            b'\t' => output.extend_from_slice(b"\\t"),
            b'\n' => output.extend_from_slice(b"\\n"),
            b'\x0c' => output.extend_from_slice(b"\\f"),
            b'\r' => output.extend_from_slice(b"\\r"),
            0..=31 => output.extend_from_slice(&[
                b'\\',
                b'u',
                b'0',
                b'0',
                HEX[(byte >> 4) as usize],
                HEX[(byte & 15) as usize],
            ]),
            _ => output.push(byte),
        }
    }
    output.push(b'"');
}

fn format_double(value: f64, output: &mut Vec<u8>) {
    if !value.is_finite() {
        output.extend_from_slice(b"0.0");
        return;
    }
    let mut buffer = ryu::Buffer::new();
    let text = buffer.format_finite(value);
    let text = if let Some(text) = text.strip_prefix('-') {
        output.push(b'-');
        text
    } else {
        text
    };
    let (mantissa, exponent) = match text.split_once('e') {
        Some((mantissa, exponent)) => (mantissa, exponent.parse::<i32>().expect("ryu exponent")),
        None => (text, 0),
    };
    let point = mantissa.find('.').unwrap_or(mantissa.len()) as i32;
    let mut storage = [0u8; 24];
    let mut length = 0;
    for digit in mantissa.bytes().filter(|byte| *byte != b'.') {
        storage[length] = digit;
        length += 1;
    }
    let leading = storage[..length]
        .iter()
        .position(|byte| *byte != b'0')
        .unwrap_or(length - 1);
    let decimal_point = point + exponent - leading as i32;
    while length > leading + 1 && storage[length - 1] == b'0' {
        length -= 1;
    }
    let digits = &storage[leading..length];
    if decimal_point >= -14 && (decimal_point <= 15 || digits.len() as i32 > decimal_point) {
        if decimal_point <= 0 {
            output.extend_from_slice(b"0.");
            output.resize(output.len() + (-decimal_point) as usize, b'0');
            output.extend_from_slice(digits);
        } else {
            let point = decimal_point as usize;
            if point >= digits.len() {
                output.extend_from_slice(digits);
                output.resize(output.len() + point - digits.len(), b'0');
                output.extend_from_slice(b".0");
            } else {
                output.extend_from_slice(&digits[..point]);
                output.push(b'.');
                output.extend_from_slice(&digits[point..]);
            }
        }
    } else {
        output.push(digits[0]);
        if digits.len() > 1 {
            output.push(b'.');
            output.extend_from_slice(&digits[1..]);
        }
        output.push(b'e');
        write!(output, "{}", decimal_point - 1).expect("writing to a Vec");
    }
}
