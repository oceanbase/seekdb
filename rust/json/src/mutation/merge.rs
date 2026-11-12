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

use crate::{BinaryError, BinaryValue, Document};
use super::owned::{OwnedValue, MergeStyle};

#[derive(Clone, Copy, Debug, Eq, PartialEq)]
pub enum MergeMode { Preserve, Patch, OraclePatch }

pub enum MergeInput<'a> { SqlNull, Value(BinaryValue<'a>) }

#[derive(Clone, Debug)]
pub enum MergeError { Binary(BinaryError), Allocation, InvalidPatch }

impl From<BinaryError> for MergeError {
    fn from(error: BinaryError) -> Self { Self::Binary(error) }
}

impl From<super::owned::ValueError> for MergeError {
    fn from(error: super::owned::ValueError) -> Self { match error {
        super::owned::ValueError::Binary(error) => Self::Binary(error),
        super::owned::ValueError::Allocation => Self::Allocation,
    } }
}
type Result<T> = std::result::Result<T, MergeError>;
pub struct MergeArgument(PreparedInput);
enum PreparedInput { SqlNull, Value(OwnedValue) }

impl MergeArgument {
    pub fn from_document(document: Document) -> Result<Self> {
        Ok(Self(PreparedInput::Value(OwnedValue::merge_document(document)?)))
    }

    pub fn prepare(input: MergeInput<'_>) -> Result<Self> {
        Ok(Self(match input {
            MergeInput::SqlNull => PreparedInput::SqlNull,
            MergeInput::Value(value) => {
                PreparedInput::Value(OwnedValue::merge_binary(value)?)
            }
        }))
    }
}

enum Accumulated { Empty, SqlNull, Value(OwnedValue) }

pub struct MergeAccumulator {
    mode: MergeMode,
    state: Accumulated,
}

impl MergeAccumulator {
    pub fn new(mode: MergeMode) -> Self { Self { mode, state: Accumulated::Empty } }

    pub fn needs_input(&self) -> bool {
        !(matches!(self.mode, MergeMode::Preserve | MergeMode::OraclePatch)
            && matches!(self.state, Accumulated::SqlNull))
    }

    /// Consumes the accumulator. Failure leaves no publishable result.
    pub fn push(self, input: MergeInput<'_>) -> Result<Self> {
        if !self.needs_input() { return Ok(self); }
        self.push_prepared(MergeArgument::prepare(input)?)
    }

    pub fn push_prepared(self, input: MergeArgument) -> Result<Self> {
        if !self.needs_input() { return Ok(self); }
        let mode = self.mode;
        let state = match input.0 {
            PreparedInput::SqlNull
                if mode == MergeMode::OraclePatch && !matches!(self.state, Accumulated::Empty) => {
                    return Err(MergeError::InvalidPatch);
                }
            PreparedInput::SqlNull => Accumulated::SqlNull,
            PreparedInput::Value(value) => match (self.state, mode) {
                (Accumulated::Empty, _) => Accumulated::Value(value),
                (Accumulated::SqlNull, MergeMode::Preserve) => Accumulated::SqlNull,
                (Accumulated::SqlNull, MergeMode::Patch | MergeMode::OraclePatch)
                    if value.is_object() => Accumulated::SqlNull,
                (Accumulated::SqlNull, MergeMode::Patch | MergeMode::OraclePatch) => Accumulated::Value(value),
                (Accumulated::Value(previous), _) => Accumulated::Value(previous.merge(value, if mode == MergeMode::Preserve { MergeStyle::Preserve } else { MergeStyle::Patch })?),
            }
        };
        Ok(Self { mode, state })
    }

    pub fn finish(self) -> Result<Option<Vec<u8>>> {
        match self.state {
            Accumulated::Empty | Accumulated::SqlNull => Ok(None),
            Accumulated::Value(value) => value.encode().map(Some).map_err(MergeError::from),
        }
    }
}
