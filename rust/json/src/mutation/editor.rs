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

use super::{Delta, DeltaError, Operation, PhysicalDiff};
use crate::binary::{self, BinaryError, BinaryErrorKind, BinaryValue, JsonType};
use crate::Number;
use std::io;

mod sql;
pub use sql::Session;
use super::sql::{PreparedEdit, SqlOperation};

/// A stable binary JSON snapshot. Reads must fill the requested range or fail.
pub trait BinarySource {
    fn len(&self) -> u64;
    fn is_empty(&self) -> bool {
        self.len() == 0
    }
    /// Returns the complete immutable snapshot when its storage can be borrowed.
    fn contiguous(&self) -> Option<&[u8]> { None }
    fn read_exact(&self, offset: u64, output: &mut [u8]) -> io::Result<()>;
}

impl BinarySource for [u8] {
    fn contiguous(&self) -> Option<&[u8]> { Some(self) }
    fn len(&self) -> u64 {
        <[u8]>::len(self) as u64
    }
    fn read_exact(&self, offset: u64, output: &mut [u8]) -> io::Result<()> {
        let offset = usize::try_from(offset).map_err(|_| io::ErrorKind::UnexpectedEof)?;
        let end = offset
            .checked_add(output.len())
            .ok_or(io::ErrorKind::UnexpectedEof)?;
        let bytes = self.get(offset..end).ok_or(io::ErrorKind::UnexpectedEof)?;
        output.copy_from_slice(bytes);
        Ok(())
    }
}

#[derive(Debug)]
pub enum EditError {
    Source(io::Error),
    Binary(BinaryError),
    Delta(DeltaError),
    InvalidIndex,
    InvalidObjectKey,
    EmptyObjectKey,
    InvalidPath,
    VacuousPath,
    StalePlan,
}

impl From<io::Error> for EditError {
    fn from(error: io::Error) -> Self {
        Self::Source(error)
    }
}
impl From<BinaryError> for EditError {
    fn from(error: BinaryError) -> Self {
        Self::Binary(error)
    }
}
impl From<DeltaError> for EditError {
    fn from(error: DeltaError) -> Self {
        Self::Delta(error)
    }
}

pub(crate) type Result<T> = std::result::Result<T, EditError>;
impl From<super::owned::ValueError> for EditError {
    fn from(error: super::owned::ValueError) -> Self { match error {
        super::owned::ValueError::Binary(error) => Self::Binary(error),
        super::owned::ValueError::Allocation => Self::Delta(DeltaError::Allocation),
    } }
}

#[derive(Debug)]
pub struct Write {
    pub reset: bool,
    pub offset: u64,
    pub bytes: Vec<u8>,
}

#[derive(Debug)]
pub struct Update {
    pub writes: Vec<Write>,
    pub delta: Delta,
}

/// One editor owns the overlay and publishes no writes before `finish`.
pub struct Editor<'a, S: BinarySource + ?Sized> {
    source: &'a S,
    length: u64,
    extension: u64,
    lexicographical: bool,
    generation: u64,
    root: Node,
    writes: Vec<Write>,
    delta: Delta,
}

#[derive(Clone, Copy)]
pub(crate) struct Node {
    offset: u64,
    raw_type: u8,
    entry_type: u8,
}

#[derive(Clone, Copy, Debug, Eq, PartialEq)]
pub struct StoredPosition {
    pub offset: u64,
    pub raw_type: u8,
    pub entry_type: u8,
}

pub struct StoredDocumentInfo {
    pub root: StoredPosition,
    pub extension: u64,
    pub lexicographical: bool,
}

pub struct ContainerInfo {
    pub count: u64,
    pub entry_type: u8,
    pub count_type: u8,
    pub size_type: u8,
    pub continuous: bool,
    pub size: u64,
    pub keys: u64,
    pub values: u64,
}

pub enum StoredKind {
    Null,
    Int(i64),
    Uint(u64),
    Double(f64),
    Float(f32),
    Boolean(bool),
    Text(std::ops::Range<u64>),
    Opaque { field_type: u16, bytes: std::ops::Range<u64> },
    Container(ContainerInfo),
    Decimal(std::ops::Range<u64>),
    Temporal(i64),
}

pub struct StoredMetadata {
    pub value_type: JsonType,
    pub kind: StoredKind,
    pub encoded_size: u64,
}

impl From<Node> for StoredPosition {
    fn from(node: Node) -> Self {
        Self { offset: node.offset, raw_type: node.raw_type, entry_type: node.entry_type }
    }
}

/// A borrowed stored value supports sparse sources and retains its original binary entry widths.
pub struct StoredValue<'a, S: BinarySource + ?Sized> {
    editor: Editor<'a, S>,
    node: Node,
}

impl<'a, S: BinarySource + ?Sized> StoredValue<'a, S> {
    pub fn open(source: &'a S, offset: u64, raw_type: u8, entry_type: u8) -> Result<Self> {
        if entry_type > 3 || offset >= source.len() {
            return Err(EditError::InvalidIndex);
        }
        let editor = Editor::open(source)?;
        let node = Node { offset, raw_type, entry_type };
        editor.kind(node)?;
        Ok(Self { editor, node })
    }

    pub fn encoded_position(source: &S, offset: u64) -> Result<StoredPosition> {
        let editor = Editor::open(source)?;
        if offset == 0 { return Ok(editor.root.into()); }
        let raw_type = editor.read(offset, 1)?[0];
        let offset = offset.checked_add(u64::from(!binary::needs_type_prefix(raw_type)))
            .ok_or(EditError::InvalidIndex)?;
        let node = Node { offset, raw_type, entry_type: 0 };
        editor.kind(node)?;
        Ok(node.into())
    }

    pub fn document_info(&self) -> StoredDocumentInfo {
        StoredDocumentInfo { root: self.editor.root.into(), extension: self.editor.extension,
            lexicographical: self.editor.lexicographical }
    }

    pub fn child_position(&self, index: u64) -> Result<StoredPosition> {
        let child = self.editor.child(self.editor.container(self.node)?, index)?;
        Ok(StoredPosition { offset: child.offset, raw_type: child.raw_type, entry_type: child.entry_type })
    }

    pub fn key_range(&self, index: u64) -> Result<std::ops::Range<u64>> {
        if self.editor.kind(self.node)? != JsonType::Object {
            return Err(EditError::InvalidIndex);
        }
        let container = self.editor.container(self.node)?;
        if index >= container.count { return Err(EditError::InvalidIndex); }
        let entry = self.node.offset + container.key_entry(index);
        let offset = self.editor.var(entry, container.entry_type)?;
        let length = self.editor.var(entry + binary::var_size(container.entry_type) as u64, container.entry_type)?;
        let start = self.node.offset.checked_add(offset).ok_or(EditError::InvalidIndex)?;
        let end = start.checked_add(length).ok_or(EditError::InvalidIndex)?;
        if end > self.editor.length { return Err(EditError::InvalidIndex); }
        Ok(start..end)
    }

    pub fn find_key(&self, key: &[u8]) -> Result<Option<u64>> {
        if self.editor.kind(self.node)? != JsonType::Object {
            return Err(EditError::InvalidIndex);
        }
        let (found, index) = self.editor.find_key(self.editor.container(self.node)?, key)?;
        Ok(found.then_some(index))
    }

    pub fn depth(&self) -> Result<u32> {
        if !self.editor.kind(self.node)?.is_container() { return Ok(1); }
        let mut frames = Vec::new();
        frames.try_reserve(1).map_err(|_| DeltaError::Allocation)?;
        frames.push((self.editor.container(self.node)?, 0));
        let mut maximum = 1;
        while let Some((container, next)) = frames.last_mut() {
            if *next == container.count {
                frames.pop();
                continue;
            }
            let child = self.editor.child(*container, *next)?;
            *next += 1;
            let depth = u32::try_from(frames.len() + 1).map_err(|_| EditError::InvalidIndex)?;
            maximum = maximum.max(depth);
            if self.editor.kind(child)?.is_container() {
                if frames.iter().any(|(container, _)| container.node.offset == child.offset) {
                    return Err(EditError::InvalidIndex);
                }
                frames.try_reserve(1).map_err(|_| DeltaError::Allocation)?;
                frames.push((self.editor.container(child)?, 0));
            }
        }
        Ok(maximum)
    }

    pub fn metadata(&self) -> Result<StoredMetadata> {
        let editor = &self.editor;
        let node = self.node;
        let inline = node.raw_type & 0x80 != 0;
        let encoded_size = if inline { 0 } else { editor.payload_size(node)? };
        let kind = match editor.kind(node)? {
            JsonType::Null => StoredKind::Null,
            JsonType::Int | JsonType::OInt => {
                let value = if inline {
                    let value = editor.var(node.offset, node.entry_type)?;
                    let shift = 64 - binary::var_size(node.entry_type) * 8;
                    (value << shift) as i64 >> shift
                } else { editor.vi64(node.offset)?.0 as i64 };
                StoredKind::Int(value)
            }
            JsonType::Uint | JsonType::OLong => StoredKind::Uint(if inline {
                editor.var(node.offset, node.entry_type)?
            } else { editor.vi64(node.offset)?.0 }),
            JsonType::Double | JsonType::ODouble => StoredKind::Double(f64::from_le_bytes(
                editor.read(node.offset, 8)?.as_ref().try_into().unwrap())),
            JsonType::OFloat => StoredKind::Float(f32::from_le_bytes(
                editor.read(node.offset, 4)?.as_ref().try_into().unwrap())),
            JsonType::Boolean => StoredKind::Boolean(if inline {
                editor.var(node.offset, node.entry_type)? != 0
            } else { editor.read(node.offset, 1)?[0] != 0 }),
            JsonType::String | JsonType::OBinary | JsonType::OOid | JsonType::ORawHex
            | JsonType::ORawId | JsonType::ODaySecond | JsonType::OYearMonth => {
                let (length, prefix) = editor.vi64(node.offset + 1)?;
                let start = node.offset + 1 + prefix;
                StoredKind::Text(start..start + length)
            }
            JsonType::Opaque => {
                let field_type = editor.var(node.offset + 1, 1)? as u16;
                let length = editor.var(node.offset + 3, 3)?;
                StoredKind::Opaque { field_type, bytes: node.offset + 11..node.offset + 11 + length }
            }
            JsonType::Array | JsonType::Object => {
                let container = editor.container(node)?;
                let flags = editor.read(node.offset + 1, 1)?[0];
                StoredKind::Container(ContainerInfo {
                    count: container.count, entry_type: container.entry_type,
                    count_type: container.count_type, size_type: flags >> 4 & 3,
                    continuous: flags & 0x40 != 0, size: container.size,
                    keys: container.keys, values: container.values,
                })
            }
            JsonType::Decimal | JsonType::ODecimal => StoredKind::Decimal(node.offset..node.offset + encoded_size),
            JsonType::Date | JsonType::MySqlDate | JsonType::JsonDateExt => StoredKind::Temporal(
                i32::from_le_bytes(editor.read(node.offset, 4)?.as_ref().try_into().unwrap()) as i64),
            JsonType::Time | JsonType::Datetime | JsonType::MySqlDatetime | JsonType::Timestamp
            | JsonType::ODate | JsonType::OTimestamp | JsonType::OTimestampTz => StoredKind::Temporal(
                i64::from_le_bytes(editor.read(node.offset, 8)?.as_ref().try_into().unwrap())),
        };
        Ok(StoredMetadata { value_type: editor.kind(node)?, kind, encoded_size })
    }

    pub fn area_size(&self) -> Result<u64> {
        if self.node.raw_type & 0x80 != 0 {
            return Err(EditError::InvalidIndex);
        }
        self.editor.payload_size(self.node)
    }

    pub fn serialized_size(&self) -> Result<u64> {
        if self.node.raw_type & 0x80 != 0 {
            return Err(EditError::InvalidIndex);
        }
        self.editor.serialized_size(self.node)
    }

    pub fn pack_payload(&self) -> Result<Vec<u8>> {
        Ok(self.editor.rebuild(self.node)?.bytes)
    }
    pub fn free_space(&self) -> Result<u64> {
        if self.node.offset != self.editor.root.offset || self.node.raw_type != self.editor.root.raw_type {
            return Err(EditError::InvalidIndex);
        }
        let used = self.serialized_size()?.checked_add(self.node.offset).ok_or(EditError::InvalidIndex)?;
        self.editor.length.checked_sub(used).ok_or(EditError::InvalidIndex)
    }

    pub fn to_document(&self) -> Result<Vec<u8>> {
        let value = self.editor.rebuild(self.node)?;
        let container = self.editor.kind(self.node)?.is_container();
        let prefix = if container { 8 } else { usize::from(!binary::needs_type_prefix(value.raw_type)) };
        let mut output = Vec::new();
        output.try_reserve_exact(value.bytes.len().checked_add(prefix).ok_or(EditError::InvalidIndex)?)
            .map_err(|_| DeltaError::Allocation)?;
        if container { output.resize(8, 0); }
        else if prefix != 0 { output.push(value.raw_type); }
        output.extend_from_slice(&value.bytes);
        if container {
            if output.len() as u64 >= 1u64 << 42 { return Err(EditError::InvalidIndex); }
            let header = ((output.len() as u64) << 22)
                | (u64::from(self.editor.lexicographical) << 8) | 30;
            output[..8].copy_from_slice(&header.to_le_bytes());
        }
        Ok(output)
    }

}

#[derive(Clone, Copy)]
struct Container {
    node: Node,
    count: u64,
    count_type: u8,
    entry_type: u8,
    size: u64,
    keys: u64,
    values: u64,
}

struct Value {
    raw_type: u8,
    bytes: Vec<u8>,
    inline: Option<(u64, bool)>,
}

enum ContainerChange<'a> {
    Keep,
    Replace {
        index: u64,
        value: &'a Value,
    },
    Insert {
        index: u64,
        key: &'a [u8],
        value: &'a Value,
    },
}

impl Value {
    fn from_binary(value: BinaryValue<'_>, lexicographical: bool) -> Result<Self> {
        let mut bytes = binary::serialize_value_in_order(value, lexicographical)?;
        let kind = value.json_type()?;
        let raw_type = if kind == JsonType::MySqlDate {
            32
        } else if kind == JsonType::MySqlDatetime {
            33
        } else {
            kind as u8
        };
        if kind.is_container() {
            bytes.drain(..8);
        } else if !binary::needs_type_prefix(raw_type) {
            bytes.remove(0);
        }
        let inline = match kind {
            JsonType::Null => Some((0, false)),
            JsonType::Boolean => Some((u64::from(value.boolean()?), false)),
            JsonType::Int | JsonType::OInt => match value.number()? {
                Number::I64(value) => Some((value as u64, true)),
                _ => None,
            },
            JsonType::Uint | JsonType::OLong => match value.number()? {
                Number::U64(value) => Some((value, false)),
                _ => None,
            },
            _ => None,
        };
        Ok(Self {
            raw_type,
            bytes,
            inline,
        })
    }

    fn inline_at(&self, entry_type: u8) -> Option<(u64, u8)> {
        let (value, signed) = self.inline?;
        let kind = match self.raw_type {
            18 => 2,
            19 => 3,
            kind => kind,
        };
        let width = if signed {
            binary::signed_var_type(value as i64)
        } else {
            binary::var_type(value)
        };
        (width <= entry_type).then_some((value, kind | 0x80))
    }
}

impl<'a, S: BinarySource + ?Sized> Editor<'a, S> {
    pub fn open(source: &'a S) -> Result<Self> {
        if source.len() > i64::MAX as u64 {
            return Err(EditError::InvalidIndex);
        }
        let mut editor = Self {
            source,
            length: source.len(),
            extension: 0,
            lexicographical: false,
            generation: 0,
            root: Node {
                offset: 0,
                raw_type: 0,
                entry_type: 0,
            },
            writes: Vec::new(),
            delta: Delta::default(),
        };
        let raw_type = editor.read(0, 1)?[0];
        let offset = if raw_type == 30 {
            let bytes = editor.read(0, 8)?;
            let bits = u64::from_le_bytes(bytes.as_ref().try_into().unwrap());
            editor.extension = bits >> 22;
            editor.lexicographical = bits & (1 << 8) != 0;
            if editor.extension > editor.length || editor.extension < 8 {
                return editor.invalid(0);
            }
            8
        } else {
            0
        };
        let raw_type = editor.read(offset, 1)?[0];
        let payload = if binary::needs_type_prefix(raw_type) {
            offset
        } else {
            offset + 1
        };
        editor.root = Node {
            offset: payload,
            raw_type,
            entry_type: 0,
        };
        editor.kind(editor.root)?;
        Ok(editor)
    }

    pub fn replace(&mut self, ancestors: &[u64], index: u64, value: BinaryValue<'_>) -> Result<()> {
        let value = Value::from_binary(value, self.lexicographical)?;
        self.atomic(|editor| {
            let (container, parents) = editor.locate(ancestors)?;
            editor.replace_child(container, &parents, index, value)
        })
    }

    pub fn insert(
        &mut self,
        ancestors: &[u64],
        index: u64,
        key: &[u8],
        value: BinaryValue<'_>,
    ) -> Result<()> {
        let value = Value::from_binary(value, self.lexicographical)?;
        self.atomic(|editor| {
            let (container, parents) = editor.locate(ancestors)?;
            let mut index = index.min(container.count);
            if editor.kind(container.node)? == JsonType::Object {
                if key.is_empty() {
                    return Err(EditError::EmptyObjectKey);
                }
                let (found, position) = editor.find_key(container, key)?;
                if found {
                    return editor.replace_child(container, &parents, position, value);
                }
                index = position;
            }
            let rebuilt = editor.rebuild_container(
                container,
                ContainerChange::Insert {
                    index,
                    key,
                    value: &value,
                },
            )?;
            editor.publish_rebuilt(container, &parents, rebuilt)
        })
    }

    pub fn remove(&mut self, ancestors: &[u64], index: u64) -> Result<()> {
        self.atomic(|editor| {
            let (container, parents) = editor.locate(ancestors)?;
            if index >= container.count {
                return Err(EditError::InvalidIndex);
            }
            if editor.kind(container.node)? == JsonType::Object {
                let mut header =
                    editor.read(container.node.offset, container.header_end() as usize)?.into_owned();
                let width = binary::var_size(container.entry_type);
                let key_entry = container.key_entry(index) as usize;
                let key_end = container.key_entry(container.count) as usize;
                header.copy_within(key_entry + width * 2..key_end, key_entry);
                let value_start = container.values as usize;
                let value_entry = container.value_entry(index) as usize;
                let value_end = container.header_end() as usize;
                let new_values = container.key_entry(container.count - 1) as usize;
                header.copy_within(value_start..value_entry, new_values);
                header.copy_within(
                    value_entry + width + 1..value_end,
                    new_values + value_entry - value_start,
                );
                put_var(&mut header, 2, container.count - 1, container.count_type);
                let path = editor.path(&parents, container, Some(index))?;
                editor.record_logical(Operation::Remove, 0, 0, &path, &[])?;
                editor.delta.record_physical(PhysicalDiff::new(
                    container.node.offset,
                    container.header_end(),
                )?)?;
                editor.write(container.node.offset, header)?;
                Ok(())
            } else {
                let mut header =
                    editor.read(container.node.offset, container.header_end() as usize)?.into_owned();
                let width = binary::var_size(container.entry_type);
                let entry = container.value_entry(index) as usize;
                let end = container.header_end() as usize;
                header.copy_within(entry + width + 1..end, entry);
                put_var(&mut header, 2, container.count - 1, container.count_type);
                editor.write(container.node.offset, header)?;
                let reduced = editor.container(container.node)?;
                let rebuilt = editor.rebuild_container(reduced, ContainerChange::Keep)?;
                if let Some((&(parent, child_index), grandparents)) = parents.split_last() {
                    editor.replace_child(parent, grandparents, child_index, rebuilt)
                } else {
                    editor.reset_root(rebuilt)
                }
            }
        })
    }

    pub fn finish(self) -> Update {
        Update {
            writes: self.writes,
            delta: self.delta,
        }
    }

    fn atomic(&mut self, apply: impl FnOnce(&mut Self) -> Result<()>) -> Result<()> {
        let checkpoint = (
            self.writes.len(),
            self.length,
            self.extension,
            self.delta.logical.len(),
            self.delta.physical.len(),
            self.delta.rebuilt,
            self.root,
            self.lexicographical,
        );
        if let Err(error) = apply(self) {
            self.writes.truncate(checkpoint.0);
            self.length = checkpoint.1;
            self.extension = checkpoint.2;
            self.delta.logical.truncate(checkpoint.3);
            self.delta.physical.truncate(checkpoint.4);
            self.delta.rebuilt = checkpoint.5;
            self.root = checkpoint.6;
            self.lexicographical = checkpoint.7;
            return Err(error);
        }
        self.generation = self.generation.wrapping_add(1);
        Ok(())
    }

    fn invalid<T>(&self, offset: u64) -> Result<T> {
        Err(BinaryError::new(BinaryErrorKind::InvalidOffset, offset as usize).into())
    }

    fn read(&self, offset: u64, length: usize) -> Result<std::borrow::Cow<'a, [u8]>> {
        let end = offset
            .checked_add(length as u64)
            .ok_or(EditError::InvalidIndex)?;
        if end > self.length {
            return self.invalid(offset);
        }
        if !self.writes.iter().any(|write| write.reset
            || (write.offset < end && offset < write.offset + write.bytes.len() as u64)) {
            if let Some(bytes) = self.source.contiguous() {
                let start = usize::try_from(offset).map_err(|_| EditError::InvalidIndex)?;
                let end = start.checked_add(length).ok_or(EditError::InvalidIndex)?;
                if let Some(bytes) = bytes.get(start..end) {
                    return Ok(std::borrow::Cow::Borrowed(bytes));
                }
            }
        }
        let mut bytes = Vec::new();
        bytes
            .try_reserve_exact(length)
            .map_err(|_| DeltaError::Allocation)?;
        bytes.resize(length, 0);
        let reset = self.writes.iter().rposition(|write| write.reset);
        if reset.is_none() && offset < self.source.len() {
            let original = (self.source.len() - offset).min(length as u64) as usize;
            self.source.read_exact(offset, &mut bytes[..original])?;
        }
        for write in &self.writes[reset.unwrap_or(0)..] {
            let start = offset.max(write.offset);
            let end = end.min(write.offset + write.bytes.len() as u64);
            if start < end {
                bytes[(start - offset) as usize..(end - offset) as usize].copy_from_slice(
                    &write.bytes[(start - write.offset) as usize..(end - write.offset) as usize],
                );
            }
        }
        Ok(std::borrow::Cow::Owned(bytes))
    }

    fn write(&mut self, offset: u64, bytes: Vec<u8>) -> Result<()> {
        if offset > self.length {
            return self.invalid(offset);
        }
        self.length = self.length.max(
            offset
                .checked_add(bytes.len() as u64)
                .ok_or(EditError::InvalidIndex)?,
        );
        self.writes
            .try_reserve(1)
            .map_err(|_| DeltaError::Allocation)?;
        self.writes.push(Write {
            reset: false,
            offset,
            bytes,
        });
        Ok(())
    }

    fn kind(&self, node: Node) -> Result<JsonType> {
        Ok(JsonType::from_binary(
            node.raw_type & 0x7f,
            node.offset as usize,
        )?)
    }

    fn var(&self, offset: u64, kind: u8) -> Result<u64> {
        let bytes = self.read(offset, binary::var_size(kind))?;
        let mut value = [0; 8];
        value[..bytes.len()].copy_from_slice(&bytes);
        Ok(u64::from_le_bytes(value))
    }

    fn vi64(&self, offset: u64) -> Result<(u64, u64)> {
        let available = self
            .length
            .checked_sub(offset)
            .ok_or(EditError::InvalidIndex)?
            .min(10) as usize;
        let bytes = self.read(offset, available)?;
        let (value, end) = binary::read_vi64(&bytes, 0)?;
        Ok((value, end as u64))
    }

    fn container(&self, node: Node) -> Result<Container> {
        if !self.kind(node)?.is_container() || node.raw_type & 0x80 != 0 {
            return self.invalid(node.offset);
        }
        let flags = self.read(node.offset, 2)?;
        if flags[0] != node.raw_type {
            return self.invalid(node.offset);
        }
        let flags = flags[1];
        let count_type = (flags >> 2) & 3;
        let size_type = (flags >> 4) & 3;
        let entry_type = flags & 3;
        let count = self.var(node.offset + 2, count_type)?;
        let size_offset = 2 + binary::var_size(count_type) as u64;
        let size = self.var(node.offset + size_offset, size_type)?;
        let keys = size_offset + binary::var_size(size_type) as u64;
        let key_size = if self.kind(node)? == JsonType::Object {
            count
                .checked_mul(binary::var_size(entry_type) as u64 * 2)
                .ok_or(EditError::InvalidIndex)?
        } else {
            0
        };
        let values = keys.checked_add(key_size).ok_or(EditError::InvalidIndex)?;
        let entries_size = count
            .checked_mul(binary::var_size(entry_type) as u64 + 1)
            .and_then(|size| values.checked_add(size))
            .ok_or(EditError::InvalidIndex)?;
        let container = Container {
            node,
            count,
            count_type,
            entry_type,
            size,
            keys,
            values,
        };
        if node
            .offset
            .checked_add(size)
            .is_none_or(|end| end > self.length)
            || entries_size > size
        {
            return self.invalid(node.offset);
        }
        Ok(container)
    }

    fn child(&self, container: Container, index: u64) -> Result<Node> {
        if index >= container.count {
            return Err(EditError::InvalidIndex);
        }
        let entry = container.node.offset + container.value_entry(index);
        let width = binary::var_size(container.entry_type) as u64;
        let offset = self.var(entry, container.entry_type)?;
        let raw_type = self.read(entry + width, 1)?[0];
        let (offset, raw_type) = if raw_type & 0x80 != 0 {
            (entry, raw_type)
        } else if raw_type == 31 {
            let offset = self
                .extension
                .checked_add(offset)
                .ok_or(EditError::InvalidIndex)?;
            let kind = self.read(offset, 1)?[0];
            (offset + u64::from(!binary::needs_type_prefix(kind)), kind)
        } else {
            if offset < container.header_end() {
                return self.invalid(entry);
            }
            (
                container
                    .node
                    .offset
                    .checked_add(offset)
                    .ok_or(EditError::InvalidIndex)?,
                raw_type,
            )
        };
        let node = Node {
            offset,
            raw_type,
            entry_type: container.entry_type,
        };
        self.kind(node)?;
        if node.offset >= self.length {
            return self.invalid(node.offset);
        }
        Ok(node)
    }

    fn key(&self, container: Container, index: u64) -> Result<Vec<u8>> {
        if self.kind(container.node)? != JsonType::Object || index >= container.count {
            return Err(EditError::InvalidIndex);
        }
        let entry = container.node.offset + container.key_entry(index);
        let offset = self.var(entry, container.entry_type)?;
        let length = self.var(
            entry + binary::var_size(container.entry_type) as u64,
            container.entry_type,
        )?;
        self.read(
            container
                .node
                .offset
                .checked_add(offset)
                .ok_or(EditError::InvalidIndex)?,
            length.try_into().map_err(|_| EditError::InvalidIndex)?,
        ).map(std::borrow::Cow::into_owned)
    }

    fn find_key(&self, container: Container, key: &[u8]) -> Result<(bool, u64)> {
        let mut low = 0;
        let mut high = container.count;
        while low < high {
            let mid = low + (high - low - 1) / 2;
            let candidate = self.key(container, mid)?;
            let order = if self.lexicographical {
                candidate.as_slice().cmp(key)
            } else {
                candidate
                    .len()
                    .cmp(&key.len())
                    .then_with(|| candidate.as_slice().cmp(key))
            };
            match order {
                std::cmp::Ordering::Equal => return Ok((true, mid)),
                std::cmp::Ordering::Greater => high = mid,
                std::cmp::Ordering::Less => low = mid + 1,
            }
        }
        Ok((false, low))
    }

    fn locate(&self, ancestors: &[u64]) -> Result<(Container, Vec<(Container, u64)>)> {
        let mut node = self.root;
        let mut parents = Vec::new();
        parents
            .try_reserve_exact(ancestors.len())
            .map_err(|_| DeltaError::Allocation)?;
        for index in ancestors {
            let container = self.container(node)?;
            node = self.child(container, *index)?;
            parents.push((container, *index));
        }
        Ok((self.container(node)?, parents))
    }

    fn payload_size(&self, node: Node) -> Result<u64> {
        if node.raw_type & 0x80 != 0 {
            return Ok(binary::var_size(node.entry_type) as u64);
        }
        let size = match self.kind(node)? {
            JsonType::Object | JsonType::Array => self.container(node)?.size,
            JsonType::Null | JsonType::Boolean => 1,
            JsonType::Int | JsonType::Uint | JsonType::OInt | JsonType::OLong => {
                self.vi64(node.offset)?.1
            }
            JsonType::Double
            | JsonType::ODouble
            | JsonType::Time
            | JsonType::Datetime
            | JsonType::MySqlDatetime
            | JsonType::Timestamp
            | JsonType::ODate
            | JsonType::OTimestamp
            | JsonType::OTimestampTz => 8,
            JsonType::OFloat | JsonType::Date | JsonType::MySqlDate | JsonType::JsonDateExt => 4,
            JsonType::Decimal | JsonType::ODecimal => {
                let header = self.read(node.offset, 8)?;
                8 + u64::from(header[7]) * 4
            }
            JsonType::String
            | JsonType::OBinary
            | JsonType::OOid
            | JsonType::ORawHex
            | JsonType::ORawId
            | JsonType::ODaySecond
            | JsonType::OYearMonth => {
                let (length, prefix) = self.vi64(node.offset + 1)?;
                length
                    .checked_add(prefix + 1)
                    .ok_or(EditError::InvalidIndex)?
            }
            JsonType::Opaque => self
                .var(node.offset + 3, 3)?
                .checked_add(11)
                .ok_or(EditError::InvalidIndex)?,
        };
        if node
            .offset
            .checked_add(size)
            .is_none_or(|end| end > self.length)
        {
            return self.invalid(node.offset);
        }
        Ok(size)
    }

    fn scalar(&self, node: Node) -> Result<Value> {
        let raw_type = node.raw_type & 0x7f;
        let kind = self.kind(node)?;
        let inline = match kind {
            JsonType::Null => Some((0, false)),
            JsonType::Boolean => Some((u64::from(self.read(node.offset, 1)?[0] != 0), false)),
            JsonType::Int | JsonType::OInt => {
                let value = if node.raw_type & 0x80 != 0 {
                    let value = self.var(node.offset, node.entry_type)?;
                    let shift = 64 - binary::var_size(node.entry_type) * 8;
                    ((value << shift) as i64 >> shift) as u64
                } else {
                    self.vi64(node.offset)?.0
                };
                Some((value, true))
            }
            JsonType::Uint | JsonType::OLong => Some((
                if node.raw_type & 0x80 != 0 {
                    self.var(node.offset, node.entry_type)?
                } else {
                    self.vi64(node.offset)?.0
                },
                false,
            )),
            _ => None,
        };
        let bytes = if node.raw_type & 0x80 != 0 {
            let mut bytes = Vec::new();
            let mut value = inline.ok_or(EditError::InvalidIndex)?.0;
            while value > 0x7f {
                bytes.push(value as u8 | 0x80);
                value >>= 7;
            }
            bytes.push(value as u8);
            bytes
        } else {
            self.read(node.offset, self.payload_size(node)? as usize)?.into_owned()
        };
        Ok(Value {
            raw_type,
            bytes,
            inline,
        })
    }

    fn record_logical(
        &mut self,
        operation: Operation,
        value_type: u8,
        flags: u16,
        path: &[u8],
        value: &[u8],
    ) -> Result<()> {
        self.delta.record(operation, value_type, flags, path, value)?;
        Ok(())
    }

    fn path(
        &self,
        parents: &[(Container, u64)],
        container: Container,
        index: Option<u64>,
    ) -> Result<Vec<u8>> {
        let mut path = vec![b'$'];
        for (container, index) in parents
            .iter()
            .copied()
            .chain(index.map(|index| (container, index)))
        {
            if self.kind(container.node)? == JsonType::Array {
                path.extend_from_slice(format!("[{index}]").as_bytes());
            } else {
                let key = self.key(container, index)?;
                if key.is_empty() {
                    return Err(EditError::InvalidObjectKey);
                }
                path.extend_from_slice(b".\"");
                for byte in key {
                    if byte == b'"' {
                        path.push(b'\\');
                    }
                    path.push(byte);
                }
                path.push(b'"');
            }
        }
        Ok(path)
    }

    fn set_entry(
        &mut self,
        container: Container,
        index: u64,
        offset: u64,
        raw_type: u8,
    ) -> Result<()> {
        let width = binary::var_size(container.entry_type);
        let mut bytes = offset.to_le_bytes()[..width].to_vec();
        bytes.push(raw_type);
        self.write(container.node.offset + container.value_entry(index), bytes)
    }

    fn replace_child(
        &mut self,
        container: Container,
        parents: &[(Container, u64)],
        index: u64,
        value: Value,
    ) -> Result<()> {
        let child = self.child(container, index)?;
        let entry = container.node.offset + container.value_entry(index);
        let width = binary::var_size(container.entry_type) as u64;
        let path = self.path(parents, container, Some(index))?;
        if let Some((inline, raw_type)) = value.inline_at(container.entry_type) {
            self.set_entry(container, index, inline, raw_type)?;
            self.record_logical(
                Operation::Replace,
                raw_type,
                u16::from(container.entry_type),
                &path,
                &inline.to_le_bytes()[..width as usize],
            )?;
            self.delta
                .record_physical(PhysicalDiff::new(entry, width + 1)?)?;
            return Ok(());
        }
        let old_type = self.read(entry + width, 1)?[0];
        let old_offset = self.var(entry, container.entry_type)?;
        if old_type == 31 {
            let start = self.extension + old_offset;
            let available = self.payload_size(child)? + child.offset - start;
            let prefix = u64::from(!binary::needs_type_prefix(value.raw_type));
            if available >= value.bytes.len() as u64 + prefix {
                let mut bytes = Vec::new();
                if prefix != 0 {
                    bytes.push(value.raw_type);
                }
                bytes.extend_from_slice(&value.bytes);
                let length = bytes.len() as u64;
                self.write(start, bytes)?;
                self.record_logical(Operation::Replace, value.raw_type, 0, &path, &value.bytes)?;
                self.delta
                    .record_physical(PhysicalDiff::new(start, length)?)?;
                return Ok(());
            }
        } else if child.raw_type & 0x80 == 0
            && self.payload_size(child)? >= value.bytes.len() as u64
        {
            self.write(child.offset, value.bytes.clone())?;
            self.record_logical(Operation::Replace, value.raw_type, 0, &path, &value.bytes)?;
            self.delta
                .record_physical(PhysicalDiff::new(child.offset, value.bytes.len() as u64)?)?;
            if old_type != value.raw_type {
                self.set_entry(container, index, old_offset, value.raw_type)?;
                self.delta
                    .record_physical(PhysicalDiff::new(entry, width + 1)?)?;
            }
            return Ok(());
        }
        if binary::var_type(self.length - self.extension) <= container.entry_type {
            self.append_child(container, index, value, path)
        } else {
            let rebuilt = self.rebuild_container(
                container,
                ContainerChange::Replace {
                    index,
                    value: &value,
                },
            )?;
            self.publish_rebuilt(container, parents, rebuilt)
        }
    }

    fn append_child(
        &mut self,
        container: Container,
        index: u64,
        value: Value,
        path: Vec<u8>,
    ) -> Result<()> {
        let start = self.length;
        self.set_entry(container, index, start - self.extension, 31)?;
        let mut bytes = Vec::new();
        if !binary::needs_type_prefix(value.raw_type) {
            bytes.push(value.raw_type);
        }
        bytes.extend_from_slice(&value.bytes);
        let length = bytes.len() as u64;
        self.write(start, bytes)?;
        self.record_logical(Operation::Replace, value.raw_type, 0, &path, &value.bytes)?;
        self.delta
            .record_physical(PhysicalDiff::new(start, length)?)?;
        self.delta.record_physical(PhysicalDiff::new(
            container.node.offset + container.value_entry(index),
            binary::var_size(container.entry_type) as u64 + 1,
        )?)?;
        Ok(())
    }

    fn publish_rebuilt(
        &mut self,
        _container: Container,
        parents: &[(Container, u64)],
        mut value: Value,
    ) -> Result<()> {
        for (position, (parent, index)) in parents.iter().copied().enumerate().rev() {
            if binary::var_type(self.length - self.extension) <= parent.entry_type {
                let path = self.path(&parents[..position], parent, Some(index))?;
                return self.append_child(parent, index, value, path);
            }
            value = self.rebuild_container(
                parent,
                ContainerChange::Replace {
                    index,
                    value: &value,
                },
            )?;
        }
        self.reset_root(value)
    }

    fn reset_document(&mut self, value: Value) -> Result<()> {
        let raw_type = value.raw_type;
        let mut bytes = Vec::new();
        if binary::needs_type_prefix(raw_type) {
            if matches!(raw_type, 6 | 7) {
                bytes.resize(8, 0);
                bytes.extend_from_slice(&value.bytes);
                let header = 30 | ((bytes.len() as u64) << 22);
                bytes[..8].copy_from_slice(&header.to_le_bytes());
                self.extension = bytes.len() as u64;
                self.root = Node {
                    offset: 8,
                    raw_type,
                    entry_type: 0,
                };
            } else {
                bytes = value.bytes;
                self.extension = 0;
                self.root = Node {
                    offset: 0,
                    raw_type,
                    entry_type: 0,
                };
            }
        } else {
            bytes.push(raw_type);
            bytes.extend_from_slice(&value.bytes);
            self.extension = 0;
            self.root = Node {
                offset: 1,
                raw_type,
                entry_type: 0,
            };
        }
        self.lexicographical = false;
        self.length = bytes.len() as u64;
        self.writes
            .try_reserve(1)
            .map_err(|_| DeltaError::Allocation)?;
        self.writes.push(Write {
            reset: true,
            offset: 0,
            bytes,
        });
        self.delta.mark_rebuilt();
        Ok(())
    }

    fn reset_root(&mut self, value: Value) -> Result<()> {
        self.write(self.root.offset, value.bytes)?;
        if self.root.offset == 0 {
            self.delta.mark_rebuilt();
            return Ok(());
        }
        let mut header = self.read(0, 8)?.into_owned();
        let mut bits = u64::from_le_bytes(header.as_slice().try_into().unwrap());
        bits = (bits & ((1 << 22) - 1)) | (self.length << 22);
        header.copy_from_slice(&bits.to_le_bytes());
        self.write(0, header)?;
        self.extension = self.length;
        self.delta.mark_rebuilt();
        Ok(())
    }

    fn serialized_size(&self, node: Node) -> Result<u64> {
        if !self.kind(node)?.is_container() {
            return self.payload_size(node);
        }
        struct Frame {
            container: Container,
            next: u64,
            payload: u64,
        }
        let mut frames = Vec::new();
        frames.try_reserve(1).map_err(|_| DeltaError::Allocation)?;
        frames.push(Frame {
            container: self.container(node)?,
            next: 0,
            payload: 0,
        });
        loop {
            let frame = frames.last_mut().unwrap();
            if frame.next < frame.container.count {
                let index = frame.next;
                frame.next += 1;
                if self.kind(frame.container.node)? == JsonType::Object {
                    let entry = frame.container.node.offset + frame.container.key_entry(index);
                    let length = self.var(
                        entry + binary::var_size(frame.container.entry_type) as u64,
                        frame.container.entry_type,
                    )?;
                    frame.payload = frame
                        .payload
                        .checked_add(length)
                        .ok_or(EditError::InvalidIndex)?;
                }
                let child = self.child(frame.container, index)?;
                if child.raw_type & 0x80 != 0 {
                    continue;
                }
                if self.kind(child)?.is_container() {
                    if frames
                        .iter()
                        .any(|frame| frame.container.node.offset == child.offset)
                    {
                        return self.invalid(child.offset);
                    }
                    frames.try_reserve(1).map_err(|_| DeltaError::Allocation)?;
                    frames.push(Frame {
                        container: self.container(child)?,
                        next: 0,
                        payload: 0,
                    });
                } else {
                    frame.payload = frame
                        .payload
                        .checked_add(self.payload_size(child)?)
                        .ok_or(EditError::InvalidIndex)?;
                }
                continue;
            }
            let frame = frames.pop().unwrap();
            let container = frame.container;
            let width = binary::var_size(container.entry_type) as u64;
            let fields = container
                .count
                .checked_mul(if self.kind(container.node)? == JsonType::Object {
                    3
                } else {
                    1
                })
                .ok_or(EditError::InvalidIndex)?;
            let header = 2 + binary::var_size(container.count_type) as u64 + width;
            let size = fields
                .checked_mul(width)
                .and_then(|size| size.checked_add(header))
                .and_then(|size| size.checked_add(container.count))
                .and_then(|size| size.checked_add(frame.payload))
                .ok_or(EditError::InvalidIndex)?;
            let size = expand_size(size, container.entry_type, fields)?.0;
            if let Some(parent) = frames.last_mut() {
                parent.payload = parent
                    .payload
                    .checked_add(size)
                    .ok_or(EditError::InvalidIndex)?;
            } else {
                return Ok(size);
            }
        }
    }

    fn rebuild(&self, node: Node) -> Result<Value> {
        if self.kind(node)?.is_container() {
            self.rebuild_container(self.container(node)?, ContainerChange::Keep)
        } else {
            self.scalar(node)
        }
    }

    fn rebuild_container(
        &self,
        container: Container,
        change: ContainerChange<'_>,
    ) -> Result<Value> {
        let inserted = matches!(change, ContainerChange::Insert { .. });
        let count = container.count + u64::from(inserted);
        let object = self.kind(container.node)? == JsonType::Object;
        let mut estimate = self.serialized_size(container.node)?;
        let old_width = binary::var_size(container.entry_type) as u64;
        let count_type = if matches!(change, ContainerChange::Replace { .. }) {
            container.count_type
        } else {
            binary::var_type(count)
        };
        let mut minimum_entry = container.entry_type;
        match change {
            ContainerChange::Keep => {}
            ContainerChange::Replace { index, value } => {
                estimate += value.bytes.len() as u64;
                let old_child = self.child(container, index)?;
                if old_child.raw_type & 0x80 == 0 {
                    estimate -= self.serialized_size(old_child)?;
                }
                minimum_entry = 0;
            }
            ContainerChange::Insert { key, value, .. } => {
                estimate += value.bytes.len() as u64
                    + old_width
                    + 1
                    + if object {
                        key.len() as u64 + old_width * 2
                    } else {
                        0
                    };
                estimate += (binary::var_size(count_type) as u64)
                    .saturating_sub(binary::var_size(container.count_type) as u64);
                minimum_entry = 0;
            }
        }
        let fields = count * if object { 3 } else { 1 };
        let (estimate, planned_entry) = expand_size(estimate, container.entry_type, fields)?;
        let entry_type = planned_entry.max(minimum_entry);
        let width = binary::var_size(entry_type);
        let mut bytes = Vec::new();
        bytes
            .try_reserve_exact(estimate as usize)
            .map_err(|_| DeltaError::Allocation)?;
        bytes.push(container.node.raw_type);
        bytes.push(entry_type | (count_type << 2) | (entry_type << 4) | 0x40);
        push_var(&mut bytes, count, count_type);
        let size_position = bytes.len();
        push_var(&mut bytes, estimate, entry_type);
        let keys = bytes.len();
        if object {
            bytes.resize(bytes.len() + count as usize * width * 2, 0);
        }
        let values = bytes.len();
        bytes.resize(bytes.len() + count as usize * (width + 1), 0);
        let source_index = |position: u64| -> Option<u64> {
            match change {
                ContainerChange::Keep => Some(position),
                ContainerChange::Replace { index, .. } if position == index => None,
                ContainerChange::Insert { index, .. } if position == index => None,
                ContainerChange::Insert { index, .. } if position > index => Some(position - 1),
                _ => Some(position),
            }
        };
        if object {
            for position in 0..count {
                let key = match source_index(position) {
                    Some(index) => self.key(container, index)?,
                    None => match change {
                        ContainerChange::Replace { .. } => self.key(container, position)?,
                        ContainerChange::Insert { key, .. } => key.to_vec(),
                        ContainerChange::Keep => unreachable!(),
                    },
                };
                let offset = bytes.len() as u64;
                put_var(
                    &mut bytes,
                    keys + position as usize * width * 2,
                    offset,
                    entry_type,
                );
                put_var(
                    &mut bytes,
                    keys + position as usize * width * 2 + width,
                    key.len() as u64,
                    entry_type,
                );
                bytes.extend_from_slice(&key);
            }
        }
        for position in 0..count {
            let owned;
            let value = if let Some(index) = source_index(position) {
                owned = self.rebuild(self.child(container, index)?)?;
                &owned
            } else {
                match change {
                    ContainerChange::Replace { value, .. }
                    | ContainerChange::Insert { value, .. } => value,
                    ContainerChange::Keep => unreachable!(),
                }
            };
            let entry = values + position as usize * (width + 1);
            if let Some((value, kind)) = value.inline_at(entry_type) {
                put_var(&mut bytes, entry, value, entry_type);
                bytes[entry + width] = kind;
            } else {
                let offset = bytes.len() as u64;
                put_var(&mut bytes, entry, offset, entry_type);
                bytes[entry + width] = value.raw_type;
                bytes.extend_from_slice(&value.bytes);
            }
        }
        let size = bytes.len() as u64;
        if binary::var_type(size) > entry_type {
            return self.invalid(container.node.offset);
        }
        put_var(&mut bytes, size_position, size, entry_type);
        Ok(Value {
            raw_type: container.node.raw_type,
            bytes,
            inline: None,
        })
    }
}

impl Container {
    fn key_entry(self, index: u64) -> u64 {
        self.keys + index * binary::var_size(self.entry_type) as u64 * 2
    }
    fn value_entry(self, index: u64) -> u64 {
        self.values + index * (binary::var_size(self.entry_type) as u64 + 1)
    }
    fn header_end(self) -> u64 {
        self.value_entry(self.count)
    }
}

fn put_var(bytes: &mut [u8], position: usize, value: u64, kind: u8) {
    let width = binary::var_size(kind);
    bytes[position..position + width].copy_from_slice(&value.to_le_bytes()[..width]);
}
fn push_var(bytes: &mut Vec<u8>, value: u64, kind: u8) {
    bytes.extend_from_slice(&value.to_le_bytes()[..binary::var_size(kind)]);
}
fn expand_size(mut size: u64, mut old_type: u8, fields: u64) -> Result<(u64, u8)> {
    let mut new_type = binary::var_type(size);
    for _ in 0..3 {
        if new_type <= old_type {
            break;
        }
        let increment = (binary::var_size(new_type) - binary::var_size(old_type)) as u64;
        size = fields
            .checked_add(1)
            .and_then(|fields| fields.checked_mul(increment))
            .and_then(|increment| size.checked_add(increment))
            .ok_or(EditError::InvalidIndex)?;
        old_type = new_type;
        new_type = binary::var_type(size);
    }
    Ok((size, new_type))
}
