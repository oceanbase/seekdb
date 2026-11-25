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

use crate::binary::{self, var_size, var_type};

#[derive(Clone, Copy, Debug, Eq, PartialEq)]
pub struct ContainerId(pub u64);

#[derive(Clone, Copy, Debug, Eq, PartialEq)]
pub enum WriteError { Allocation, State, Width }
pub type Result<T> = std::result::Result<T, WriteError>;

struct Frame {
    id: ContainerId,
    start: usize,
    object: bool,
    count: u64,
    entry_type: u8,
    size_position: usize,
    keys: usize,
    values: usize,
    next_key: u64,
    next_value: u64,
}

/// Streaming construction owns bytes and open containers. An error prevents publication.
pub struct BinaryWriter {
    bytes: Vec<u8>,
    frames: Vec<Frame>,
    next_id: u64,
    root_started: bool,
    failed: bool,
}

impl Default for BinaryWriter {
    fn default() -> Self {
        Self { bytes: Vec::new(), frames: Vec::new(), next_id: 1, root_started: false, failed: false }
    }
}

impl BinaryWriter {
    fn apply<T>(&mut self, operation: impl FnOnce(&mut Self) -> Result<T>) -> Result<T> {
        if self.failed { return Err(WriteError::State); }
        let result = operation(self);
        self.failed = result.is_err();
        result
    }

    fn reserve(&mut self, size: usize) -> Result<()> {
        self.bytes.try_reserve(size).map_err(|_| WriteError::Allocation)
    }

    fn attach(&mut self, raw_type: u8) -> Result<()> {
        let frame = self.frames.last_mut().ok_or(WriteError::State)?;
        if frame.next_value >= frame.count || (frame.object && frame.next_key != frame.count) {
            return Err(WriteError::State);
        }
        let offset = (self.bytes.len() - frame.start) as u64;
        if var_type(offset) > frame.entry_type { return Err(WriteError::Width); }
        let width = var_size(frame.entry_type);
        let position = frame.values + frame.next_value as usize * (width + 1);
        binary::set_var(&mut self.bytes, position, offset, frame.entry_type);
        self.bytes[position + width] = raw_type;
        frame.next_value += 1;
        Ok(())
    }

    pub fn begin(&mut self, object: bool, count: u64, entry_type: u8) -> Result<ContainerId> {
        self.apply(|writer| {
            if entry_type > 3 { return Err(WriteError::Width); }
            let width = var_size(entry_type);
            let count_type = var_type(count);
            let count_usize = usize::try_from(count).map_err(|_| WriteError::Width)?;
            let entries = count_usize.checked_mul(if object { width * 3 + 1 } else { width + 1 })
                .ok_or(WriteError::Width)?;
            let header = 2 + var_size(count_type) + width;
            let size = header.checked_add(entries).ok_or(WriteError::Width)?;
            writer.reserve(size.checked_add(8).ok_or(WriteError::Width)?)?;
            writer.frames.try_reserve(1).map_err(|_| WriteError::Allocation)?;
            let raw_type = if object { 6 } else { 7 };
            if writer.frames.is_empty() {
                if writer.root_started { return Err(WriteError::State); }
                writer.bytes.resize(8, 0);
                writer.root_started = true;
            } else { writer.attach(raw_type)?; }
            let start = writer.bytes.len();
            writer.bytes.push(raw_type);
            writer.bytes.push(entry_type | count_type << 2 | entry_type << 4 | 0x40);
            binary::push_var(&mut writer.bytes, count, count_type);
            let size_position = writer.bytes.len();
            binary::push_var(&mut writer.bytes, 0, entry_type);
            let keys = writer.bytes.len();
            if object { writer.bytes.resize(keys + count_usize * width * 2, 0); }
            let values = writer.bytes.len();
            writer.bytes.resize(values + count_usize * (width + 1), 0);
            let id = ContainerId(writer.next_id);
            writer.next_id = writer.next_id.checked_add(1).ok_or(WriteError::State)?;
            writer.frames.push(Frame { id, start, object, count, entry_type, size_position,
                keys, values, next_key: 0, next_value: 0 });
            Ok(id)
        })
    }

    pub fn key(&mut self, key: &[u8]) -> Result<()> {
        self.apply(|writer| {
            writer.reserve(key.len())?;
            let frame = writer.frames.last_mut().ok_or(WriteError::State)?;
            if !frame.object || frame.next_value != 0 || frame.next_key >= frame.count {
                return Err(WriteError::State);
            }
            let offset = (writer.bytes.len() - frame.start) as u64;
            if var_type(offset) > frame.entry_type || var_type(key.len() as u64) > frame.entry_type {
                return Err(WriteError::Width);
            }
            let width = var_size(frame.entry_type);
            let position = frame.keys + frame.next_key as usize * width * 2;
            binary::set_var(&mut writer.bytes, position, offset, frame.entry_type);
            binary::set_var(&mut writer.bytes, position + width, key.len() as u64, frame.entry_type);
            writer.bytes.extend_from_slice(key);
            frame.next_key += 1;
            Ok(())
        })
    }

    pub fn string(&mut self, value: &[u8]) -> Result<()> {
        self.apply(|writer| {
            writer.reserve(value.len().checked_add(11).ok_or(WriteError::Width)?)?;
            writer.attach(5)?;
            writer.bytes.push(5);
            binary::push_vi64(&mut writer.bytes, value.len() as u64);
            writer.bytes.extend_from_slice(value);
            Ok(())
        })
    }

    pub fn double(&mut self, value: f64) -> Result<()> {
        self.apply(|writer| {
            writer.reserve(8)?;
            writer.attach(4)?;
            writer.bytes.extend_from_slice(&value.to_le_bytes());
            Ok(())
        })
    }

    pub fn end(&mut self, id: ContainerId) -> Result<()> {
        self.apply(|writer| {
            let frame = writer.frames.last().ok_or(WriteError::State)?;
            if frame.id != id || frame.next_value != frame.count
                || (frame.object && frame.next_key != frame.count) { return Err(WriteError::State); }
            let size = (writer.bytes.len() - frame.start) as u64;
            if var_type(size) > frame.entry_type { return Err(WriteError::Width); }
            binary::set_var(&mut writer.bytes, frame.size_position, size, frame.entry_type);
            writer.frames.pop();
            Ok(())
        })
    }

    pub fn finish(mut self, lexicographical: bool) -> Result<Vec<u8>> {
        if self.failed || !self.root_started || !self.frames.is_empty() { return Err(WriteError::State); }
        if self.bytes.len() as u64 >= 1u64 << 42 { return Err(WriteError::Width); }
        let header = ((self.bytes.len() as u64) << 22) | (u64::from(lexicographical) << 8) | 30;
        self.bytes[..8].copy_from_slice(&header.to_le_bytes());
        Ok(self.bytes)
    }
}
