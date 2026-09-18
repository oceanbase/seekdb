// Numeric conversion adapted from RapidJSON reader.h and internal/strtod.h.
// Copyright (C) 2015 THL A29 Limited, a Tencent company, and Milo Yip.
// Distributed under the MIT License; see ../LICENSE-RAPIDJSON.
// Rust adaptation Copyright (c) 2026 OceanBase.

use crate::{ParseError, Result};

mod powers;

// Preserve the bundled parser's normal-precision conversion. Parsing a decimal
// using Rust's correctly rounded str::parse can change the eventual f32 bits.
// Integer tokens must convert directly to f32, without an intermediate f64.
pub(super) fn parse(input: &[u8]) -> Result<(f32, usize)> {
    let mut cursor = Cursor { input, pos: 0 };
    let negative = cursor.consume(b'-');
    let first = cursor.digit().ok_or(ParseError::InvalidJson)?;
    cursor.pos += 1;
    let mut integer = u64::from(first);
    let limit = if negative { 1_u64 << 63 } else { u64::MAX };
    let mut significant = 0;
    let mut double = None;
    if first != 0 {
        while let Some(digit) = cursor.digit() {
            let digit = u64::from(digit);
            if integer > (limit - digit) / 10 {
                double = Some(integer as f64);
                break;
            }
            integer = integer * 10 + digit;
            significant += 1;
            cursor.pos += 1;
        }
    }
    if let Some(value) = double.as_mut() {
        while let Some(digit) = cursor.digit() {
            *value = *value * 10.0 + f64::from(digit);
            cursor.pos += 1;
        }
    }

    let mut fraction_exp = 0_i64;
    if cursor.consume(b'.') {
        if cursor.digit().is_none() {
            return Err(ParseError::InvalidJson);
        }
        if double.is_none() {
            while let Some(digit) = cursor.digit() {
                if integer > (1_u64 << 53) - 1 {
                    break;
                }
                integer = integer * 10 + u64::from(digit);
                cursor.pos += 1;
                fraction_exp -= 1;
                if integer != 0 {
                    significant += 1;
                }
            }
            double = Some(integer as f64);
        }
        let value = double.as_mut().ok_or(ParseError::InvalidJson)?;
        while let Some(digit) = cursor.digit() {
            if significant < 17 {
                *value = *value * 10.0 + f64::from(digit);
                fraction_exp -= 1;
                if *value > 0.0 {
                    significant += 1;
                }
            }
            cursor.pos += 1;
        }
    }

    let mut exponent = 0_i64;
    if cursor.consume(b'e') || cursor.consume(b'E') {
        double.get_or_insert(integer as f64);
        let exp_negative = if cursor.consume(b'+') {
            false
        } else {
            cursor.consume(b'-')
        };
        exponent = i64::from(cursor.digit().ok_or(ParseError::InvalidJson)?);
        cursor.pos += 1;
        let max_exp = if exp_negative {
            (fraction_exp + 2_147_483_639) / 10
        } else {
            308 - fraction_exp
        };
        while let Some(digit) = cursor.digit() {
            exponent = exponent * 10 + i64::from(digit);
            cursor.pos += 1;
            if exponent > max_exp {
                if !exp_negative {
                    return Err(ParseError::InvalidJson);
                }
                while cursor.digit().is_some() {
                    cursor.pos += 1;
                }
            }
        }
        if exp_negative {
            exponent = -exponent;
        }
    }

    let value = if let Some(mut value) = double {
        let exp = exponent + fraction_exp;
        if exp < -308 {
            value = scale(value, -308);
            value = scale(value, exp + 308);
        } else {
            value = scale(value, exp);
        }
        if !value.is_finite() {
            return Err(ParseError::InvalidJson);
        }
        (if negative { -value } else { value }) as f32
    } else if negative {
        // wrapping_neg also handles -9223372036854775808; integer -0 is +0.
        (integer as i64).wrapping_neg() as f32
    } else {
        integer as f32
    };
    Ok((value, cursor.pos))
}

fn scale(value: f64, exponent: i64) -> f64 {
    if exponent < -308 {
        0.0
    } else if exponent >= 0 {
        value * powers::POW10[exponent as usize]
    } else {
        value / powers::POW10[(-exponent) as usize]
    }
}

struct Cursor<'a> {
    input: &'a [u8],
    pos: usize,
}

impl Cursor<'_> {
    fn digit(&self) -> Option<u8> {
        self.input
            .get(self.pos)
            .copied()
            .filter(u8::is_ascii_digit)
            .map(|byte| byte - b'0')
    }

    fn consume(&mut self, byte: u8) -> bool {
        if self.input.get(self.pos) == Some(&byte) {
            self.pos += 1;
            true
        } else {
            false
        }
    }
}
