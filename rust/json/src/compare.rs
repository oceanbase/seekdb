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

use crate::{BinaryError, BinaryErrorKind, BinaryValue, JsonType};
use std::fmt;

#[derive(Clone, Debug, Eq, PartialEq)]
pub enum CompareError {
    Binary(BinaryError),
    NumericOverflow,
    UnsupportedTypes,
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
            Self::UnsupportedTypes => {
                formatter.write_str("JSON types are not comparable without a scalar capability")
            }
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
            Self::NumericOverflow | Self::UnsupportedTypes => None,
        }
    }
}

pub trait ScalarComparator {
    type Error;
    fn compare_temporal(
        &mut self,
        left: BinaryValue<'_>,
        right: BinaryValue<'_>,
    ) -> Result<std::cmp::Ordering, Self::Error>;
}

impl<F, E> ScalarComparator for F
where
    F: FnMut(BinaryValue<'_>, BinaryValue<'_>) -> Result<std::cmp::Ordering, E>,
{
    type Error = E;
    fn compare_temporal(
        &mut self,
        left: BinaryValue<'_>,
        right: BinaryValue<'_>,
    ) -> Result<std::cmp::Ordering, E> {
        self(left, right)
    }
}

pub(crate) struct NoTemporal;
impl ScalarComparator for NoTemporal {
    type Error = CompareError;
    fn compare_temporal(
        &mut self,
        _: BinaryValue<'_>,
        _: BinaryValue<'_>,
    ) -> CompareResult<std::cmp::Ordering> {
        Err(CompareError::UnsupportedTypes)
    }
}

pub(crate) fn without_temporal<T>(result: Result<T, OrderError<CompareError>>) -> CompareResult<T> {
    result.map_err(|error| match error {
        OrderError::Value(error) | OrderError::Scalar(error) => error,
        OrderError::UnsupportedTypes => CompareError::UnsupportedTypes,
    })
}

pub fn equal(left: BinaryValue<'_>, right: BinaryValue<'_>) -> CompareResult<bool> {
    without_temporal(equal_with(left, right, &mut NoTemporal))
}

pub fn equal_with<C: ScalarComparator>(
    left: BinaryValue<'_>,
    right: BinaryValue<'_>,
    scalar: &mut C,
) -> Result<bool, OrderError<C::Error>> {
    order(left, right, scalar).map(|order| order.is_eq())
}

pub fn contains(target: BinaryValue<'_>, candidate: BinaryValue<'_>) -> CompareResult<bool> {
    without_temporal(contains_with(target, candidate, &mut NoTemporal))
}

pub fn overlaps(left: BinaryValue<'_>, right: BinaryValue<'_>) -> CompareResult<bool> {
    without_temporal(overlaps_with(left, right, &mut NoTemporal))
}

pub fn member_of(candidate: BinaryValue<'_>, target: BinaryValue<'_>) -> CompareResult<bool> {
    without_temporal(member_of_with(candidate, target, &mut NoTemporal))
}

pub fn contains_with<C: ScalarComparator>(
    target: BinaryValue<'_>,
    candidate: BinaryValue<'_>,
    scalar: &mut C,
) -> Result<bool, OrderError<C::Error>> {
    let mut result = false;
    contains_with_state(target, candidate, &mut result, scalar)?;
    Ok(result)
}

pub fn overlaps_with<C: ScalarComparator>(
    left: BinaryValue<'_>,
    right: BinaryValue<'_>,
    scalar: &mut C,
) -> Result<bool, OrderError<C::Error>> {
    match (left.json_type()?, right.json_type()?) {
        (JsonType::Array, _) => overlaps_array(left, right, scalar),
        (_, JsonType::Array) => overlaps_array(right, left, scalar),
        (JsonType::Object, JsonType::Object) => {
            if left.len()? == 0 && right.len()? == 0 {
                return Ok(true);
            }
            for index in 0..right.len()? {
                if let Some(left_value) = left.member(right.key(index)?)? {
                    if equal_with(left_value, right.value(index)?, scalar)? {
                        return Ok(true);
                    }
                }
            }
            Ok(false)
        }
        _ => equal_with(left, right, scalar),
    }
}

pub fn member_of_with<C: ScalarComparator>(
    candidate: BinaryValue<'_>,
    target: BinaryValue<'_>,
    scalar: &mut C,
) -> Result<bool, OrderError<C::Error>> {
    if target.json_type()? != JsonType::Array {
        return equal_with(candidate, target, scalar);
    }
    for index in 0..target.len()? {
        if equal_with(candidate, target.value(index)?, scalar)? {
            return Ok(true);
        }
    }
    Ok(false)
}

fn contains_with_state<C: ScalarComparator>(
    target: BinaryValue<'_>,
    candidate: BinaryValue<'_>,
    result: &mut bool,
    scalar: &mut C,
) -> Result<(), OrderError<C::Error>> {
    enum Task<'a, 'b> {
        Value(BinaryValue<'a>, BinaryValue<'b>, usize),
        Object(BinaryValue<'a>, BinaryValue<'b>, u64, usize),
        ArrayCandidate(BinaryValue<'a>, BinaryValue<'b>, u64, usize),
        ArraySearch(BinaryValue<'a>, BinaryValue<'b>, u64, u64, bool, usize),
        ArrayChild(BinaryValue<'a>, BinaryValue<'b>, u64, u64, usize),
        Leave(usize),
    }
    let mut active = std::collections::HashSet::new();
    let depth_bound = target.traversal_limit();
    let mut tasks = vec![Task::Value(target, candidate, 0)];
    while let Some(task) = tasks.pop() {
        match task {
            Task::Value(target, candidate, depth) => {
                if depth > depth_bound {
                    return Err(BinaryError::new(BinaryErrorKind::InvalidDepth, target.storage_offset()).into());
                }
                let kind = target.json_type()?;
                if kind.is_container() {
                    let position = target.storage_offset();
                    if !active.insert(position) {
                        return Err(BinaryError::new(BinaryErrorKind::InvalidDepth, position).into());
                    }
                    tasks.push(Task::Leave(position));
                }
                match kind {
                    JsonType::Array => tasks.push(Task::ArrayCandidate(target, candidate, 0, depth)),
                    JsonType::Object => {
                        if candidate.json_type()? != JsonType::Object {
                            *result = false;
                        } else if target.len()? == 0 && candidate.len()? == 0 {
                            *result = true;
                        } else if target.len()? != 0 && candidate.len()? != 0 {
                            tasks.push(Task::Object(target, candidate, 0, depth));
                        }
                    }
                    _ => *result = equal_with(target, candidate, scalar)?,
                }
            }
            Task::Object(target, candidate, index, depth) => {
                if (index != 0 && !*result) || index == candidate.len()? {
                    continue;
                }
                if let Some(child) = target.member(candidate.key(index)?)? {
                    tasks.push(Task::Object(target, candidate, index + 1, depth));
                    tasks.push(Task::Value(child, candidate.value(index)?, depth + 1));
                } else {
                    *result = false;
                }
            }
            Task::ArrayCandidate(target, candidate, index, depth) => {
                let array = candidate.json_type()? == JsonType::Array;
                let count = if array { candidate.len()? } else { 1 };
                if index == count {
                    *result = true;
                    continue;
                }
                let value = if array { candidate.value(index)? } else { candidate };
                let mut empty_object = false;
                if value.json_type()? == JsonType::Object {
                    for target_index in 0..target.len()? {
                        let child = target.value(target_index)?;
                        if child.json_type()? == JsonType::Object
                            && (child.len()? == 0 || value.len()? == 0)
                        {
                            empty_object = true;
                            break;
                        }
                    }
                }
                if empty_object {
                    tasks.push(Task::ArrayCandidate(target, candidate, index + 1, depth));
                } else {
                    tasks.push(Task::ArraySearch(target, candidate, index, 0, true, depth));
                }
            }
            Task::ArraySearch(target, candidate, candidate_index, mut index, state, depth) => {
                let value = if candidate.json_type()? == JsonType::Array {
                    candidate.value(candidate_index)?
                } else { candidate };
                let candidate_array = value.json_type()? == JsonType::Array;
                let mut scheduled = false;
                while index < target.len()? {
                    let child = target.value(index)?;
                    index += 1;
                    let kind = child.json_type()?;
                    if candidate_array && kind != JsonType::Array {
                        continue;
                    }
                    if kind.is_container() {
                        *result = state;
                        tasks.push(Task::ArrayChild(target, candidate, candidate_index, index, depth));
                        tasks.push(Task::Value(child, value, depth + 1));
                        scheduled = true;
                        break;
                    }
                    if equal_with(child, value, scalar)? {
                        tasks.push(Task::ArrayCandidate(target, candidate, candidate_index + 1, depth));
                        scheduled = true;
                        break;
                    }
                }
                if !scheduled { *result = false; }
            }
            Task::ArrayChild(target, candidate, candidate_index, index, depth) => {
                if *result {
                    tasks.push(Task::ArrayCandidate(target, candidate, candidate_index + 1, depth));
                } else {
                    tasks.push(Task::ArraySearch(target, candidate, candidate_index, index, false, depth));
                }
            }
            Task::Leave(position) => { active.remove(&position); }
        }
    }
    Ok(())
}

fn overlaps_array<C: ScalarComparator>(
    array: BinaryValue<'_>,
    other: BinaryValue<'_>,
    scalar: &mut C,
) -> Result<bool, OrderError<C::Error>> {
    if other.json_type()? == JsonType::Array {
        for other_index in 0..other.len()? {
            let other_value = other.value(other_index)?;
            for array_index in 0..array.len()? {
                if equal_with(array.value(array_index)?, other_value, scalar)? {
                    return Ok(true);
                }
            }
        }
        Ok(false)
    } else {
        for array_index in 0..array.len()? {
            if equal_with(array.value(array_index)?, other, scalar)? {
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

#[derive(Debug)]
pub enum OrderError<E> {
    Value(CompareError),
    UnsupportedTypes,
    Scalar(E),
}

impl<E> From<BinaryError> for OrderError<E> {
    fn from(error: BinaryError) -> Self {
        Self::Value(error.into())
    }
}

/// Orders JSON values, retaining database tag compatibility and object entry order.
/// The scalar capability runs synchronously, only for temporal values. It must not
/// retain the borrowed values beyond the call.
pub fn order<C: ScalarComparator>(
    left: BinaryValue<'_>,
    right: BinaryValue<'_>,
    temporal: &mut C,
) -> Result<std::cmp::Ordering, OrderError<C::Error>> {
    use std::cmp::Ordering;
    enum Task<'a, 'b> {
        Pair(BinaryValue<'a>, BinaryValue<'b>, usize),
        Entries(BinaryValue<'a>, BinaryValue<'b>, u64, u64, u64, usize),
    }
    let depth_bound = left.traversal_limit().min(right.traversal_limit());
    let mut tasks = vec![Task::Pair(left, right, 0)];
    while let Some(task) = tasks.pop() {
        let (left, right, depth) = match task {
            Task::Pair(left, right, depth) => (left, right, depth),
            Task::Entries(left, right, index, count_a, count_b, depth) => {
                if index == count_a.min(count_b) {
                    if count_a != count_b {
                        return Ok(count_a.cmp(&count_b));
                    }
                } else {
                    if left.json_type()? == JsonType::Object {
                        let key_order = left.key(index)?.cmp(right.key(index)?);
                        if key_order != Ordering::Equal {
                            return Ok(key_order);
                        }
                    }
                    tasks.push(Task::Entries(
                        left,
                        right,
                        index + 1,
                        count_a,
                        count_b,
                        depth,
                    ));
                    tasks.push(Task::Pair(
                        left.value(index)?,
                        right.value(index)?,
                        depth + 1,
                    ));
                }
                continue;
            }
        };
        if depth > depth_bound {
            return Err(BinaryError::new(BinaryErrorKind::InvalidDepth, 0).into());
        }
        let a = left.json_type()?;
        let b = right.json_type()?;
        match TYPE_COMPARISON[a as usize][b as usize] {
            -1 => return Ok(Ordering::Less),
            1 => return Ok(Ordering::Greater),
            2 => return Err(OrderError::UnsupportedTypes),
            _ => {}
        }
        let result = if is_number(a) {
            crate::decimal::compare_values(left, right).map_err(OrderError::Value)?
        } else if a.is_string() {
            left.string_bytes()?.cmp(right.string_bytes()?)
        } else {
            match a {
                JsonType::Null => Ordering::Equal,
                JsonType::Boolean => left.boolean()?.cmp(&right.boolean()?),
                JsonType::Array | JsonType::Object => {
                    let count_a = left.len()?;
                    let count_b = right.len()?;
                    if a == JsonType::Object && count_a != count_b {
                        return Ok(count_a.cmp(&count_b));
                    }
                    tasks.push(Task::Entries(left, right, 0, count_a, count_b, depth));
                    continue;
                }
                JsonType::Opaque => {
                    let kind = left.opaque_field_type()?.cmp(&right.opaque_field_type()?);
                    if kind != Ordering::Equal {
                        return Ok(kind);
                    }
                    left.opaque_bytes()?.cmp(right.opaque_bytes()?)
                }
                _ => temporal
                    .compare_temporal(left, right)
                    .map_err(OrderError::Scalar)?,
            }
        };
        if result != Ordering::Equal {
            return Ok(result);
        }
    }
    Ok(Ordering::Equal)
}

const TYPE_COMPARISON: [[i8; 33]; 33] = [
    [
        0, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, 2, 2, 2, 2, 2, 2, 2, 2, 2, 2, 2, 2,
        2, 2, 2, 2, -1, -1, 2,
    ],
    [
        1, 0, 0, 0, 0, -1, -1, -1, -1, -1, -1, -1, -1, -1, 2, 2, 2, 2, 2, 2, 2, 2, 2, 2, 2, 2, 2,
        2, 2, 2, -1, -1, 2,
    ],
    [
        1, 0, 0, 0, 0, -1, -1, -1, -1, -1, -1, -1, -1, -1, 2, 2, 2, 2, 2, 2, 2, 2, 2, 2, 2, 2, 2,
        2, 2, 2, -1, -1, 2,
    ],
    [
        1, 0, 0, 0, 0, -1, -1, -1, -1, -1, -1, -1, -1, -1, 2, 2, 2, 2, 2, 2, 2, 2, 2, 2, 2, 2, 2,
        2, 2, 2, -1, -1, 2,
    ],
    [
        1, 0, 0, 0, 0, -1, -1, -1, -1, -1, -1, -1, -1, -1, 2, 0, 2, 2, 2, 2, 2, 2, 2, 2, 2, 2, 2,
        2, 2, 2, -1, -1, 2,
    ],
    [
        1, 1, 1, 1, 1, 0, -1, -1, -1, -1, -1, -1, -1, -1, 2, 2, 2, 2, 2, 2, 0, 0, 0, 0, 2, 2, 2, 2,
        2, 2, -1, -1, 2,
    ],
    [
        1, 1, 1, 1, 1, 1, 0, -1, -1, -1, -1, -1, -1, -1, 2, 2, 2, 2, 2, 2, 2, 2, 2, 2, 2, 2, 2, 2,
        2, 2, -1, -1, 2,
    ],
    [
        1, 1, 1, 1, 1, 1, 1, 0, -1, -1, -1, -1, -1, -1, 2, 2, 2, 2, 2, 2, 2, 2, 2, 2, 2, 2, 2, 2,
        2, 2, -1, -1, 2,
    ],
    [
        1, 1, 1, 1, 1, 1, 1, 1, 0, -1, -1, -1, -1, -1, 2, 2, 2, 2, 2, 2, 2, 2, 2, 2, 2, 2, 2, 2, 2,
        2, -1, -1, 2,
    ],
    [
        1, 1, 1, 1, 1, 1, 1, 1, 1, 0, -1, -1, -1, -1, 2, 2, 2, 2, 2, 2, 2, 2, 2, 2, 2, 2, 2, 2, 2,
        2, -1, -1, 2,
    ],
    [
        1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 0, -1, -1, -1, 2, 2, 2, 2, 2, 2, 2, 2, 2, 2, 2, 2, 2, 2, 2,
        2, 1, -1, 2,
    ],
    [
        1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 0, 0, -1, 2, 2, 2, 2, 2, 2, 2, 2, 2, 2, 2, 2, 2, 2, 2, 2,
        1, -1, 2,
    ],
    [
        1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 0, 0, -1, 2, 2, 2, 2, 2, 2, 2, 2, 2, 2, 2, 2, 0, 2, 2, 2,
        1, 0, 2,
    ],
    [
        1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 0, 2, 2, 2, 2, 2, 2, 2, 2, 2, 2, 2, 2, 2, 2, 2, 2,
        1, 1, 2,
    ],
    [
        2, 2, 2, 2, 2, 2, 2, 2, 2, 2, 2, 2, 2, 2, 2, 2, 2, 2, 2, 2, 2, 2, 2, 2, 2, 2, 2, 2, 2, 2,
        2, 2, 2,
    ],
    [
        2, 2, 2, 2, 0, 2, 2, 2, 2, 2, 2, 2, 2, 2, 2, 0, 0, 0, 0, 0, 2, 2, 2, 2, 2, 2, 2, 2, 2, 2,
        2, 2, 2,
    ],
    [
        2, 2, 2, 2, 2, 2, 2, 2, 2, 2, 2, 2, 2, 2, 2, 0, 0, 0, 0, 0, 2, 2, 2, 2, 2, 2, 2, 2, 2, 2,
        2, 2, 2,
    ],
    [
        2, 2, 2, 2, 2, 2, 2, 2, 2, 2, 2, 2, 2, 2, 2, 0, 0, 0, 0, 0, 2, 2, 2, 2, 2, 2, 2, 2, 2, 2,
        2, 2, 2,
    ],
    [
        2, 2, 2, 2, 2, 2, 2, 2, 2, 2, 2, 2, 2, 2, 2, 0, 0, 0, 0, 0, 2, 2, 2, 2, 2, 2, 2, 2, 2, 2,
        2, 2, 2,
    ],
    [
        2, 2, 2, 2, 2, 2, 2, 2, 2, 2, 2, 2, 2, 2, 2, 0, 0, 0, 0, 0, 2, 2, 2, 2, 2, 2, 2, 2, 2, 2,
        2, 2, 2,
    ],
    [
        2, 2, 2, 2, 2, 0, 2, 2, 2, 2, 2, 2, 2, 2, 2, 2, 2, 2, 2, 2, 0, 0, 0, 0, 2, 2, 2, 2, 2, 2,
        2, 2, 2,
    ],
    [
        2, 2, 2, 2, 2, 0, 2, 2, 2, 2, 2, 2, 2, 2, 2, 2, 2, 2, 2, 2, 0, 0, 0, 0, 2, 2, 2, 2, 2, 2,
        2, 2, 2,
    ],
    [
        2, 2, 2, 2, 2, 0, 2, 2, 2, 2, 2, 2, 2, 2, 2, 2, 2, 2, 2, 2, 0, 0, 0, 0, 2, 2, 2, 2, 2, 2,
        2, 2, 2,
    ],
    [
        2, 2, 2, 2, 2, 0, 2, 2, 2, 2, 2, 2, 2, 2, 2, 2, 2, 2, 2, 2, 0, 0, 0, 0, 2, 2, 2, 2, 2, 2,
        2, 2, 2,
    ],
    [
        2, 2, 2, 2, 2, 2, 2, 2, 2, 2, 2, 2, 2, 2, 2, 2, 2, 2, 2, 2, 2, 2, 2, 2, 0, 0, 0, 0, 2, 2,
        2, 2, 2,
    ],
    [
        2, 2, 2, 2, 2, 2, 2, 2, 2, 2, 2, 2, 2, 2, 2, 2, 2, 2, 2, 2, 2, 2, 2, 2, 0, 0, 0, 0, 2, 2,
        2, 2, 2,
    ],
    [
        2, 2, 2, 2, 2, 2, 2, 2, 2, 2, 2, 2, 0, 2, 2, 2, 2, 2, 2, 2, 2, 2, 2, 2, 0, 0, 0, 0, 2, 2,
        2, 2, 2,
    ],
    [
        2, 2, 2, 2, 2, 2, 2, 2, 2, 2, 2, 2, 2, 2, 2, 2, 2, 2, 2, 2, 2, 2, 2, 2, 0, 0, 0, 0, 2, 2,
        2, 2, 2,
    ],
    [
        2, 2, 2, 2, 2, 2, 2, 2, 2, 2, 2, 2, 2, 2, 2, 2, 2, 2, 2, 2, 2, 2, 2, 2, 2, 2, 2, 2, 0, 2,
        2, 2, 2,
    ],
    [
        2, 2, 2, 2, 2, 2, 2, 2, 2, 2, 2, 2, 2, 2, 2, 2, 2, 2, 2, 2, 2, 2, 2, 2, 2, 2, 2, 2, 2, 0,
        2, 2, 2,
    ],
    [
        1, 1, 1, 1, 1, 1, 1, 1, 1, 1, -1, -1, -1, -1, 2, 2, 2, 2, 2, 2, 2, 2, 2, 2, 2, 2, 2, 2, 2,
        2, 0, 1, 2,
    ],
    [
        1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 0, 0, -1, 2, 2, 2, 2, 2, 2, 2, 2, 2, 2, 2, 2, 2, 2, 2, 2,
        1, 0, 2,
    ],
    [
        2, 2, 2, 2, 2, 2, 2, 2, 2, 2, 2, 2, 2, 2, 2, 2, 2, 2, 2, 2, 2, 2, 2, 2, 2, 2, 2, 2, 2, 2,
        2, 2, 2,
    ],
];
