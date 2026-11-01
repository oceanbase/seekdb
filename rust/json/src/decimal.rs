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

use crate::binary::{read_bytes, BinaryError, BinaryErrorKind, BinaryResult};

const BASE: u64 = 1_000_000_000;
const MAX_DIGITS: usize = 9;
const POSITIVE_EXP_BOUNDARY: i32 = 0xc0;
const NEGATIVE_EXP_BOUNDARY: i32 = 0x40;
const MAX_SCIENTIFIC_EXPONENT: i32 = 125;

#[derive(Clone, Copy, Debug, Eq, PartialEq)]
pub(crate) struct DecimalOverflow;

#[derive(Clone, Copy, Debug)]
pub struct Decimal {
    negative: bool,
    exponent: i32,
    len: usize,
    digits: [u32; MAX_DIGITS],
}

impl Decimal {
    pub(crate) fn decode(bytes: &[u8], offset: usize) -> BinaryResult<Self> {
        let header = read_bytes(bytes, offset, 8)?;
        let desc = u32::from_be_bytes(header[4..8].try_into().expect("four bytes are present"));
        let len = (desc & 0xff) as usize;
        if len > MAX_DIGITS {
            return Err(BinaryError::new(BinaryErrorKind::InvalidNumber, offset + 4));
        }
        let negative = desc & 0x8000_0000 == 0;
        let sign_exponent = ((desc >> 24) & 0xff) as i32;
        let exponent = if negative {
            NEGATIVE_EXP_BOUNDARY - sign_exponent
        } else {
            sign_exponent - POSITIVE_EXP_BOUNDARY
        };
        let mut digits = [0; MAX_DIGITS];
        for (index, digit) in digits.iter_mut().take(len).enumerate() {
            let digit_offset = offset + 8 + index * 4;
            *digit = u32::from_le_bytes(
                read_bytes(bytes, digit_offset, 4)?
                    .try_into()
                    .expect("four bytes were requested"),
            );
            if u64::from(*digit) >= BASE {
                return Err(BinaryError::new(
                    BinaryErrorKind::InvalidNumber,
                    digit_offset,
                ));
            }
        }
        Ok(Self {
            negative,
            exponent,
            len,
            digits,
        })
    }

    pub(crate) fn serialized_size(self) -> usize {
        8 + self.len * 4
    }

    fn from_i64(value: i64) -> Self {
        Self::from_magnitude(value < 0, value.unsigned_abs())
    }

    fn from_u64(value: u64) -> Self {
        Self::from_magnitude(false, value)
    }

    fn from_magnitude(negative: bool, mut value: u64) -> Self {
        let mut digits = [0; MAX_DIGITS];
        let mut len = 0;
        while value != 0 {
            digits[len] = (value % BASE) as u32;
            value /= BASE;
            len += 1;
        }
        digits[..len].reverse();
        Self {
            negative: negative && len != 0,
            exponent: len as i32 - 1,
            len,
            digits,
        }
    }

    fn from_f64(value: f64) -> Result<Self, DecimalOverflow> {
        if !value.is_finite() {
            return Err(DecimalOverflow);
        }
        let mut buffer = ryu::Buffer::new();
        Self::parse(buffer.format_finite(value).as_bytes()).ok_or(DecimalOverflow)
    }

    fn parse(input: &[u8]) -> Option<Self> {
        let (negative, input) = match input.first() {
            Some(b'-') => (true, &input[1..]),
            _ => (false, input),
        };
        let exponent_marker = input.iter().position(|byte| matches!(byte, b'e' | b'E'));
        let (mantissa, explicit_exponent) = match exponent_marker {
            Some(index) => {
                let exponent = std::str::from_utf8(&input[index + 1..])
                    .ok()?
                    .parse()
                    .ok()?;
                (&input[..index], exponent)
            }
            None => (input, 0),
        };
        let point = mantissa.iter().position(|byte| *byte == b'.');
        let digits_before = point.unwrap_or(mantissa.len());
        let mut significant = [0u8; 32];
        let mut significant_len = 0;
        for (index, byte) in mantissa.iter().copied().enumerate() {
            if Some(index) == point {
                continue;
            }
            if !byte.is_ascii_digit() || significant_len == significant.len() {
                return None;
            }
            significant[significant_len] = byte - b'0';
            significant_len += 1;
        }
        let leading_zeros = significant[..significant_len]
            .iter()
            .position(|digit| *digit != 0);
        let Some(leading_zeros) = leading_zeros else {
            return Some(Self::from_magnitude(false, 0));
        };
        let trailing = significant[..significant_len]
            .iter()
            .rposition(|digit| *digit != 0)?
            + 1;
        significant.copy_within(leading_zeros..trailing, 0);
        significant_len = trailing - leading_zeros;
        let scientific_exponent =
            explicit_exponent + digits_before as i32 - 1 - leading_zeros as i32;
        if scientific_exponent > MAX_SCIENTIFIC_EXPONENT {
            return None;
        }
        let exponent = scientific_exponent.div_euclid(9);
        let first_group = scientific_exponent.rem_euclid(9) as usize + 1;
        let mut digits = [0; MAX_DIGITS];
        let mut source = 0;
        let mut len = 0;
        while source < significant_len {
            if len == MAX_DIGITS {
                return None;
            }
            let group_size = if len == 0 { first_group } else { 9 };
            let mut digit = 0u32;
            for index in 0..group_size {
                digit = digit * 10
                    + u32::from(if source + index < significant_len {
                        significant[source + index]
                    } else {
                        0
                    });
            }
            digits[len] = digit;
            source += group_size;
            len += 1;
        }
        Some(Self {
            negative,
            exponent,
            len,
            digits,
        })
    }

    fn equal(mut self, mut other: Self) -> bool {
        self.normalize();
        other.normalize();
        if self.len == 0 || other.len == 0 {
            return self.len == other.len;
        }
        self.negative == other.negative
            && self.exponent == other.exponent
            && self.len == other.len
            && self.digits[..self.len] == other.digits[..other.len]
    }

    fn normalize(&mut self) {
        while self.len != 0 && self.digits[self.len - 1] == 0 {
            self.len -= 1;
        }
        if self.len == 0 {
            self.negative = false;
            self.exponent = 0;
        }
    }
}

pub(crate) fn equal_decimal_number(
    left: Decimal,
    right: crate::Number,
) -> Result<bool, DecimalOverflow> {
    match right {
        crate::Number::I64(right) => Ok(left.equal(Decimal::from_i64(right))),
        crate::Number::U64(right) => Ok(left.equal(Decimal::from_u64(right))),
        crate::Number::F64(right) => Decimal::from_f64(right).map(|right| left.equal(right)),
    }
}

pub(crate) fn equal_decimals(left: Decimal, right: Decimal) -> bool {
    left.equal(right)
}
