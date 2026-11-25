/*
 * Copyright (c) 2026 OceanBase.
 *
 * Licensed under the Apache License, Version 2.0 (the "License");
 * you may not use this file except in compliance with the License.
 * You may obtain a copy of the License at
 *
 *     http://www.apache.org/licenses/LICENSE-2.0
 *
 * Unless required by applicable law or agreed to in writing, software
 * distributed under the License is distributed on an "AS IS" BASIS,
 * WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
 * See the License for the specific language governing permissions and
 * limitations under the License.
 */

//! Allocation-free numeric prefix parsing for Rust and C++ callers.
//!
//! Float syntax follows C++ `from_chars` general format: leading whitespace
//! and a leading plus sign are rejected, and an incomplete exponent is not
//! consumed. Range errors cover overflow and underflow to zero.

#[cfg(not(test))]
mod bridge;

#[derive(Clone, Copy, Debug, Eq, PartialEq)]
pub enum ErrorKind {
    Invalid,
    OutOfRange,
}

#[derive(Clone, Copy, Debug, Eq, PartialEq)]
pub struct ParseError {
    pub kind: ErrorKind,
    pub consumed: usize,
}

#[derive(Clone, Copy, Debug, PartialEq)]
pub struct Parsed<T> {
    pub value: T,
    pub consumed: usize,
}

type Result<T> = std::result::Result<Parsed<T>, ParseError>;

fn invalid() -> ParseError {
    ParseError {
        kind: ErrorKind::Invalid,
        consumed: 0,
    }
}

fn out_of_range(consumed: usize) -> ParseError {
    ParseError {
        kind: ErrorKind::OutOfRange,
        consumed,
    }
}

fn float_consumed(
    input: &[u8],
    value: f64,
    mut consumed: usize,
) -> std::result::Result<usize, ParseError> {
    if value.is_nan() {
        if input.get(consumed) == Some(&b'(') {
            for (offset, byte) in input[consumed + 1..].iter().enumerate() {
                if *byte == b')' {
                    consumed += offset + 2;
                    break;
                }
                if !byte.is_ascii_alphanumeric() && *byte != b'_' {
                    break;
                }
            }
        }
    } else if value.is_infinite() {
        let start = usize::from(input[0] == b'-');
        if input.len() < start + 3 || !input[start..start + 3].eq_ignore_ascii_case(b"inf") {
            return Err(out_of_range(consumed));
        }
    } else if value == 0.0
        && input[..consumed]
            .iter()
            .take_while(|byte| !matches!(byte, b'e' | b'E'))
            .any(|byte| matches!(byte, b'1'..=b'9'))
    {
        return Err(out_of_range(consumed));
    }
    Ok(consumed)
}

pub fn parse_f64(input: &[u8]) -> Result<f64> {
    if input.is_empty() || input[0] == b'+' {
        return Err(invalid());
    }
    let (value, consumed) = fast_float2::parse_partial(input).map_err(|_| invalid())?;
    let consumed = float_consumed(input, value, consumed)?;
    Ok(Parsed { value, consumed })
}

pub fn parse_f32(input: &[u8]) -> Result<f32> {
    if input.is_empty() || input[0] == b'+' {
        return Err(invalid());
    }
    let (value, consumed) = fast_float2::parse_partial(input).map_err(|_| invalid())?;
    let consumed = float_consumed(input, f64::from(value), consumed)?;
    Ok(Parsed { value, consumed })
}

pub fn parse_u32(input: &[u8]) -> Result<u32> {
    let mut consumed = 0;
    let mut value = 0_u32;
    let mut overflow = false;
    while let Some(byte) = input.get(consumed).filter(|byte| byte.is_ascii_digit()) {
        if !overflow {
            match value
                .checked_mul(10)
                .and_then(|value| value.checked_add(u32::from(*byte - b'0')))
            {
                Some(parsed) => value = parsed,
                None => overflow = true,
            }
        }
        consumed += 1;
    }
    if consumed == 0 {
        return Err(invalid());
    }
    if overflow {
        return Err(out_of_range(consumed));
    }
    Ok(Parsed { value, consumed })
}

#[cfg(test)]
mod tests;
