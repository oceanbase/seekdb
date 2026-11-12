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

//! Ordered binary JSON changes and the persisted logical delta format.
//!
//! Values retain their binary tag, inline entry width and exact scalar payload.
//! Physical changes describe writes to a caller-owned LOB cursor. The delta owns
//! every logical record, so recording never borrows cursor scratch buffers.

pub mod editor;
pub mod merge;
pub mod owned;
pub mod sql;

#[derive(Clone, Copy, Debug, Eq, PartialEq)]
#[repr(u8)]
pub enum Operation {
    Invalid = 0,
    Replace = 1,
    Insert = 2,
    Remove = 3,
    Rename = 4,
}

impl TryFrom<u64> for Operation {
    type Error = DeltaError;

    fn try_from(value: u64) -> Result<Self, Self::Error> {
        match value {
            0 => Ok(Self::Invalid),
            1 => Ok(Self::Replace),
            2 => Ok(Self::Insert),
            3 => Ok(Self::Remove),
            4 => Ok(Self::Rename),
            _ => Err(DeltaError::InvalidRecord),
        }
    }
}

#[derive(Debug, Eq, PartialEq)]
pub struct LogicalDiff {
    pub operation: Operation,
    pub value_type: u8,
    pub flags: u16,
    pub path: Vec<u8>,
    pub value: Vec<u8>,
}

#[derive(Clone, Copy, Debug, Eq, PartialEq)]
pub struct PhysicalDiff {
    pub offset: u64,
    pub length: u64,
}

impl PhysicalDiff {
    pub fn new(offset: u64, length: u64) -> Result<Self, DeltaError> {
        if offset > i64::MAX as u64 || length > i64::MAX as u64 {
            return Err(DeltaError::InvalidRange);
        }
        offset
            .checked_add(length)
            .filter(|end| *end <= i64::MAX as u64)
            .ok_or(DeltaError::InvalidRange)?;
        Ok(Self { offset, length })
    }

    pub fn intersects(self, offset: u64, length: u64) -> bool {
        let Some(end) = offset.checked_add(length) else {
            return false;
        };
        let Some(diff_end) = self.offset.checked_add(self.length) else {
            return false;
        };
        (self.offset >= offset && self.offset < end)
            || (diff_end > offset && diff_end <= end)
            || (self.offset <= offset && end <= diff_end)
    }
}

#[derive(Clone, Copy, Debug, Eq, PartialEq)]
pub enum DeltaError {
    Truncated,
    UnsupportedVersion,
    InvalidRecord,
    InvalidRange,
    Allocation,
}

#[derive(Clone, Copy, Debug, Eq, PartialEq)]
pub enum UpdateState {
    Unchanged,
    Partial,
    Rebuilt,
}

impl UpdateState {
    pub fn from_flags(rebuilt: bool, unchanged: bool) -> Self {
        if rebuilt {
            Self::Rebuilt
        } else if unchanged {
            Self::Unchanged
        } else {
            Self::Partial
        }
    }
}

pub(crate) fn should_pack_state(
    state: UpdateState,
    total: u64,
    extension: u64,
    root_offset: u64,
) -> Result<bool, DeltaError> {
    if total == 0 || extension == 0 || extension > total {
        return Err(DeltaError::InvalidRange);
    }
    if state == UpdateState::Rebuilt || root_offset == 0 {
        return Ok(false);
    }
    let appended = total - extension;
    Ok(appended == 0
        || state == UpdateState::Unchanged
        || (appended as f64 / extension as f64) < 0.3)
}

#[derive(Debug, Default)]
pub struct Delta {
    logical: Vec<LogicalDiff>,
    physical: Vec<PhysicalDiff>,
    rebuilt: bool,
}

impl Delta {
    pub fn append(&mut self, other: &mut Delta) -> Result<(), DeltaError> {
        self.logical
            .try_reserve(other.logical.len())
            .map_err(|_| DeltaError::Allocation)?;
        self.physical
            .try_reserve(other.physical.len())
            .map_err(|_| DeltaError::Allocation)?;
        self.logical.append(&mut other.logical);
        self.physical.append(&mut other.physical);
        self.rebuilt |= other.rebuilt;
        other.rebuilt = false;
        Ok(())
    }

    pub fn logical(&self) -> &[LogicalDiff] {
        &self.logical
    }
    pub fn physical(&self) -> &[PhysicalDiff] {
        &self.physical
    }
    pub fn is_unchanged(&self) -> bool {
        self.physical.is_empty()
    }
    pub fn is_rebuilt(&self) -> bool {
        self.rebuilt
    }
    pub fn mark_rebuilt(&mut self) {
        self.rebuilt = true;
    }

    pub fn record(
        &mut self,
        operation: Operation,
        value_type: u8,
        flags: u16,
        path: &[u8],
        value: &[u8],
    ) -> Result<(), DeltaError> {
        let path = owned_bytes(path)?;
        let value = owned_bytes(value)?;
        self.logical
            .try_reserve(1)
            .map_err(|_| DeltaError::Allocation)?;
        self.logical.push(LogicalDiff {
            operation,
            value_type,
            flags,
            path,
            value,
        });
        Ok(())
    }

    pub fn record_physical(&mut self, diff: PhysicalDiff) -> Result<(), DeltaError> {
        PhysicalDiff::new(diff.offset, diff.length)?;
        self.physical
            .try_reserve(1)
            .map_err(|_| DeltaError::Allocation)?;
        self.physical.push(diff);
        Ok(())
    }

    pub fn state(&self) -> UpdateState {
        UpdateState::from_flags(self.rebuilt, self.is_unchanged())
    }

    pub fn should_pack(
        &self,
        total: u64,
        extension: u64,
        root_offset: u64,
    ) -> Result<bool, DeltaError> {
        should_pack_state(self.state(), total, extension, root_offset)
    }

    pub fn intersects(&self, offset: u64, length: u64) -> bool {
        self.physical
            .iter()
            .any(|diff| diff.intersects(offset, length))
    }

    pub fn encoded_logical_size(&self) -> usize {
        2 + self
            .logical
            .iter()
            .map(LogicalDiff::encoded_size)
            .sum::<usize>()
    }

    pub fn encode_logical(&self) -> Result<Vec<u8>, DeltaError> {
        let mut bytes = Vec::new();
        bytes
            .try_reserve_exact(self.encoded_logical_size())
            .map_err(|_| DeltaError::Allocation)?;
        bytes.resize(self.encoded_logical_size(), 0);
        self.write_logical(&mut bytes)?;
        Ok(bytes)
    }

    pub fn write_logical(&self, output: &mut [u8]) -> Result<usize, DeltaError> {
        let length = self.encoded_logical_size();
        if output.len() < length {
            return Err(DeltaError::Truncated);
        }
        let mut writer = Writer {
            bytes: output,
            position: 0,
        };
        writer.byte(0);
        // The existing wire header stores the record count modulo 256.
        writer.byte(self.logical.len() as u8);
        for diff in &self.logical {
            diff.encode(&mut writer);
        }
        Ok(length)
    }

    /// Decode into temporary ownership and publish both lists together.
    /// Bytes after the header's count are left for the enclosing storage record.
    pub fn restore(
        &mut self,
        bytes: &[u8],
        physical: &[PhysicalDiff],
    ) -> Result<usize, DeltaError> {
        let mut reader = Reader { bytes, position: 0 };
        reader.byte()?;
        let count = usize::from(reader.byte()?);
        let mut logical = Vec::new();
        logical
            .try_reserve_exact(count)
            .map_err(|_| DeltaError::Allocation)?;
        for _ in 0..count {
            logical.push(LogicalDiff::decode(&mut reader)?);
        }
        for diff in physical {
            PhysicalDiff::new(diff.offset, diff.length)?;
        }
        self.logical
            .try_reserve(logical.len())
            .map_err(|_| DeltaError::Allocation)?;
        self.physical
            .try_reserve(physical.len())
            .map_err(|_| DeltaError::Allocation)?;
        self.logical.extend(logical);
        self.physical.extend_from_slice(physical);
        Ok(reader.position)
    }
}

impl LogicalDiff {
    fn body_size(&self) -> usize {
        1 + 1
            + varint_size(u64::from(self.flags))
            + varint_size(self.path.len() as u64)
            + self.path.len()
            + 1
            + varint_size(self.value.len() as u64)
            + self.value.len()
            + 1
    }

    fn encoded_size(&self) -> usize {
        6 + self.body_size()
    }

    fn encode(&self, writer: &mut Writer<'_>) {
        writer.byte(1);
        let mut length = self.body_size() as u64;
        for index in 0..5 {
            writer.byte((length as u8 & 0x7f) | if index < 4 { 0x80 } else { 0 });
            length >>= 7;
        }
        writer.byte(self.operation as u8);
        writer.byte(self.value_type);
        writer.varint(u64::from(self.flags));
        writer.string(&self.path);
        writer.string(&self.value);
    }

    fn decode(reader: &mut Reader<'_>) -> Result<Self, DeltaError> {
        if reader.varint()? != 1 {
            return Err(DeltaError::UnsupportedVersion);
        }
        let length = usize::try_from(reader.varint()?).map_err(|_| DeltaError::InvalidRecord)?;
        let body = reader.take(length)?;
        let mut body = Reader {
            bytes: body,
            position: 0,
        };
        let mut diff = Self {
            operation: Operation::Invalid,
            value_type: 0,
            flags: 0,
            path: Vec::new(),
            value: Vec::new(),
        };
        // UNIS permits missing trailing fields and skips fields from newer writers.
        if body.remaining() {
            diff.operation = Operation::try_from(body.varint()?)?;
        }
        if body.remaining() {
            diff.value_type = body.byte()?;
        }
        if body.remaining() {
            diff.flags = body.varint()? as u16;
        }
        if body.remaining() {
            diff.path = owned_bytes(body.string()?)?;
        }
        if body.remaining() {
            diff.value = owned_bytes(body.string()?)?;
        }
        Ok(diff)
    }
}

fn owned_bytes(bytes: &[u8]) -> Result<Vec<u8>, DeltaError> {
    let mut owned = Vec::new();
    owned
        .try_reserve_exact(bytes.len())
        .map_err(|_| DeltaError::Allocation)?;
    owned.extend_from_slice(bytes);
    Ok(owned)
}

fn varint_size(mut value: u64) -> usize {
    let mut size = 1;
    while value > 0x7f {
        value >>= 7;
        size += 1;
    }
    size
}

struct Writer<'a> {
    bytes: &'a mut [u8],
    position: usize,
}

impl Writer<'_> {
    fn byte(&mut self, value: u8) {
        self.bytes[self.position] = value;
        self.position += 1;
    }
    fn varint(&mut self, mut value: u64) {
        while value > 0x7f {
            self.byte(value as u8 | 0x80);
            value >>= 7;
        }
        self.byte(value as u8);
    }
    fn string(&mut self, value: &[u8]) {
        self.varint(value.len() as u64);
        self.bytes[self.position..self.position + value.len()].copy_from_slice(value);
        self.position += value.len();
        self.byte(0);
    }
}

struct Reader<'a> {
    bytes: &'a [u8],
    position: usize,
}

impl<'a> Reader<'a> {
    fn remaining(&self) -> bool {
        self.position < self.bytes.len()
    }
    fn take(&mut self, length: usize) -> Result<&'a [u8], DeltaError> {
        let end = self
            .position
            .checked_add(length)
            .ok_or(DeltaError::Truncated)?;
        let bytes = self
            .bytes
            .get(self.position..end)
            .ok_or(DeltaError::Truncated)?;
        self.position = end;
        Ok(bytes)
    }
    fn byte(&mut self) -> Result<u8, DeltaError> {
        Ok(self.take(1)?[0])
    }
    fn varint(&mut self) -> Result<u64, DeltaError> {
        let mut value = 0_u64;
        for shift in (0..70).step_by(7) {
            let byte = self.byte()?;
            if shift == 63 && byte > 1 {
                return Err(DeltaError::InvalidRecord);
            }
            value |= u64::from(byte & 0x7f) << shift;
            if byte & 0x80 == 0 {
                return Ok(value);
            }
        }
        Err(DeltaError::InvalidRecord)
    }
    fn string(&mut self) -> Result<&'a [u8], DeltaError> {
        let length = usize::try_from(self.varint()?).map_err(|_| DeltaError::InvalidRecord)?;
        let bytes = self.take(length)?;
        self.byte()?;
        Ok(bytes)
    }
}
