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

use crate::{BinaryError, BinaryValue, JsonType, Number};
use std::fmt;

#[derive(Clone, Debug, Eq, PartialEq)]
pub enum CompareError {
    Binary(BinaryError),
    NumericOverflow,
}

pub type CompareResult<T> = std::result::Result<T, CompareError>;

impl From<BinaryError> for CompareError {
    fn from(error: BinaryError) -> Self {
        Self::Binary(error)
    }
}

impl fmt::Display for CompareError {
    fn fmt(&self, formatter: &mut fmt::Formatter) -> fmt::Result {
        match self {
            Self::Binary(error) => error.fmt(formatter),
            Self::NumericOverflow => {
                formatter.write_str("JSON number exceeds decimal comparison range")
            }
        }
    }
}

impl std::error::Error for CompareError {
    fn source(&self) -> Option<&(dyn std::error::Error + 'static)> {
        match self {
            Self::Binary(error) => Some(error),
            Self::NumericOverflow => None,
        }
    }
}

pub fn equal(left: BinaryValue<'_>, right: BinaryValue<'_>) -> CompareResult<bool> {
    let left_type = left.json_type()?;
    let right_type = right.json_type()?;
    if is_number(left_type) && is_number(right_type) {
        if matches!(left_type, JsonType::Decimal | JsonType::ODecimal)
            || matches!(right_type, JsonType::Decimal | JsonType::ODecimal)
        {
            return decimal_equal(left, right);
        }
        return Ok(number_equal(left.number()?, right.number()?));
    }
    match (left_type, right_type) {
        (JsonType::Null, JsonType::Null) => Ok(true),
        (JsonType::Boolean, JsonType::Boolean) => Ok(left.boolean()? == right.boolean()?),
        (JsonType::String, JsonType::String) => Ok(left.string_bytes()? == right.string_bytes()?),
        (JsonType::Array, JsonType::Array) => {
            if left.len()? != right.len()? {
                return Ok(false);
            }
            for index in 0..left.len()? {
                if !equal(left.value(index)?, right.value(index)?)? {
                    return Ok(false);
                }
            }
            Ok(true)
        }
        (JsonType::Object, JsonType::Object) => {
            if left.len()? != right.len()? {
                return Ok(false);
            }
            for index in 0..left.len()? {
                if left.key(index)? != right.key(index)?
                    || !equal(left.value(index)?, right.value(index)?)?
                {
                    return Ok(false);
                }
            }
            Ok(true)
        }
        (JsonType::Opaque, JsonType::Opaque) => {
            if left.opaque_field_type()? != right.opaque_field_type()? {
                return Ok(false);
            }
            let left = crate::binary::serialize_value(left)?;
            let right = crate::binary::serialize_value(right)?;
            Ok(left == right)
        }
        _ if left_type == right_type => {
            let left = crate::binary::serialize_value(left)?;
            let right = crate::binary::serialize_value(right)?;
            Ok(left == right)
        }
        _ => Ok(false),
    }
}

pub fn contains(target: BinaryValue<'_>, candidate: BinaryValue<'_>) -> CompareResult<bool> {
    let mut result = false;
    contains_with_state(target, candidate, &mut result)?;
    Ok(result)
}

pub fn overlaps(left: BinaryValue<'_>, right: BinaryValue<'_>) -> CompareResult<bool> {
    match (left.json_type()?, right.json_type()?) {
        (JsonType::Array, _) => overlaps_array(left, right),
        (_, JsonType::Array) => overlaps_array(right, left),
        (JsonType::Object, JsonType::Object) => {
            if left.len()? == 0 && right.len()? == 0 {
                return Ok(true);
            }
            for index in 0..right.len()? {
                if let Some(left_value) = left.member(right.key(index)?)? {
                    if equal(left_value, right.value(index)?)? {
                        return Ok(true);
                    }
                }
            }
            Ok(false)
        }
        _ => equal(left, right),
    }
}

pub fn member_of(candidate: BinaryValue<'_>, target: BinaryValue<'_>) -> CompareResult<bool> {
    if target.json_type()? != JsonType::Array {
        return equal(candidate, target);
    }
    for index in 0..target.len()? {
        if equal(candidate, target.value(index)?)? {
            return Ok(true);
        }
    }
    Ok(false)
}

fn contains_with_state(
    target: BinaryValue<'_>,
    candidate: BinaryValue<'_>,
    result: &mut bool,
) -> CompareResult<()> {
    match target.json_type()? {
        JsonType::Array => contains_array(target, candidate, result),
        JsonType::Object => contains_object(target, candidate, result),
        _ => {
            *result = equal(target, candidate)?;
            Ok(())
        }
    }
}

fn contains_array(
    target: BinaryValue<'_>,
    candidate: BinaryValue<'_>,
    result: &mut bool,
) -> CompareResult<()> {
    let candidate_count = if candidate.json_type()? == JsonType::Array {
        candidate.len()?
    } else {
        1
    };
    let mut state = true;
    for candidate_index in 0..candidate_count {
        let candidate_value = if candidate.json_type()? == JsonType::Array {
            candidate.value(candidate_index)?
        } else {
            candidate
        };
        let candidate_is_array = candidate_value.json_type()? == JsonType::Array;
        let mut found = false;
        if candidate_value.json_type()? == JsonType::Object {
            for target_index in 0..target.len()? {
                let target_value = target.value(target_index)?;
                if target_value.json_type()? == JsonType::Object
                    && (target_value.len()? == 0 || candidate_value.len()? == 0)
                {
                    state = true;
                    found = true;
                    break;
                }
            }
        }
        for target_index in 0..target.len()? {
            if found {
                break;
            }
            let target_value = target.value(target_index)?;
            let target_type = target_value.json_type()?;
            if candidate_is_array {
                if target_type != JsonType::Array {
                    continue;
                }
                contains_with_state(target_value, candidate_value, &mut state)?;
                found = state;
            } else if target_type.is_container() {
                contains_with_state(target_value, candidate_value, &mut state)?;
                found = state;
            } else if equal(target_value, candidate_value)? {
                found = true;
            }
            if found {
                break;
            }
        }
        if !found {
            state = false;
        }
        if !state {
            break;
        }
    }
    *result = state;
    Ok(())
}

fn contains_object(
    target: BinaryValue<'_>,
    candidate: BinaryValue<'_>,
    result: &mut bool,
) -> CompareResult<()> {
    if candidate.json_type()? != JsonType::Object {
        *result = false;
    } else if target.len()? == 0 && candidate.len()? == 0 {
        *result = true;
    } else if target.len()? == 0 || candidate.len()? == 0 {
    } else {
        for index in 0..candidate.len()? {
            let Some(target_value) = target.member(candidate.key(index)?)? else {
                *result = false;
                break;
            };
            contains_with_state(target_value, candidate.value(index)?, result)?;
            if !*result {
                break;
            }
        }
    }
    Ok(())
}

fn overlaps_array(array: BinaryValue<'_>, other: BinaryValue<'_>) -> CompareResult<bool> {
    if other.json_type()? == JsonType::Array {
        for other_index in 0..other.len()? {
            let other_value = other.value(other_index)?;
            for array_index in 0..array.len()? {
                if equal(array.value(array_index)?, other_value)? {
                    return Ok(true);
                }
            }
        }
        Ok(false)
    } else {
        for array_index in 0..array.len()? {
            if equal(array.value(array_index)?, other)? {
                return Ok(true);
            }
        }
        Ok(false)
    }
}

fn is_number(kind: JsonType) -> bool {
    matches!(
        kind,
        JsonType::Decimal
            | JsonType::ODecimal
            | JsonType::Int
            | JsonType::Uint
            | JsonType::Double
            | JsonType::OFloat
            | JsonType::ODouble
            | JsonType::OInt
            | JsonType::OLong
    )
}

fn number_equal(left: Number, right: Number) -> bool {
    match (left, right) {
        (Number::I64(left), Number::I64(right)) => left == right,
        (Number::U64(left), Number::U64(right)) => left == right,
        (Number::F64(left), Number::F64(right)) => left == right,
        (Number::I64(left), Number::U64(right)) | (Number::U64(right), Number::I64(left)) => {
            left >= 0 && left as u64 == right
        }
        (Number::I64(left), Number::F64(right)) | (Number::F64(right), Number::I64(left)) => {
            double_equals_int(right, left)
        }
        (Number::U64(left), Number::F64(right)) | (Number::F64(right), Number::U64(left)) => {
            double_equals_uint(right, left)
        }
    }
}

fn decimal_equal(left: BinaryValue<'_>, right: BinaryValue<'_>) -> CompareResult<bool> {
    match (left.json_type()?, right.json_type()?) {
        (JsonType::Decimal | JsonType::ODecimal, JsonType::Decimal | JsonType::ODecimal) => Ok(
            crate::decimal::equal_decimals(left.decimal()?, right.decimal()?),
        ),
        (JsonType::Decimal | JsonType::ODecimal, _) => {
            crate::decimal::equal_decimal_number(left.decimal()?, right.number()?)
                .map_err(|_| CompareError::NumericOverflow)
        }
        (_, JsonType::Decimal | JsonType::ODecimal) => {
            crate::decimal::equal_decimal_number(right.decimal()?, left.number()?)
                .map_err(|_| CompareError::NumericOverflow)
        }
        _ => unreachable!("one side must be a decimal"),
    }
}

fn double_equals_int(double: f64, integer: i64) -> bool {
    const I64_UPPER_EXCLUSIVE: f64 = 9_223_372_036_854_775_808.0;
    double >= i64::MIN as f64
        && double < I64_UPPER_EXCLUSIVE
        && double.fract() == 0.0
        && double as i64 == integer
}

fn double_equals_uint(double: f64, integer: u64) -> bool {
    const U64_UPPER_EXCLUSIVE: f64 = 18_446_744_073_709_551_616.0;
    double >= 0.0
        && double < U64_UPPER_EXCLUSIVE
        && double.fract() == 0.0
        && double as u64 == integer
}
