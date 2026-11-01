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

use crate::{Document, Number};
use std::fmt;

const NULL: u8 = 0;
const INT: u8 = 2;
const UINT: u8 = 3;
const DOUBLE: u8 = 4;
const STRING: u8 = 5;
const OBJECT: u8 = 6;
const ARRAY: u8 = 7;
const BOOLEAN: u8 = 8;
const DOC_HEADER: u8 = 30;
const FORWARD: u8 = 31;
const INLINE: u8 = 0x80;

#[derive(Clone, Copy, Debug, Eq, PartialEq)]
#[repr(u8)]
pub enum JsonType {
    Null = 0,
    Decimal = 1,
    Int = 2,
    Uint = 3,
    Double = 4,
    String = 5,
    Object = 6,
    Array = 7,
    Boolean = 8,
    Date = 9,
    Time = 10,
    Datetime = 11,
    Timestamp = 12,
    Opaque = 13,
    OFloat = 15,
    ODouble = 16,
    ODecimal = 17,
    OInt = 18,
    OLong = 19,
    OBinary = 20,
    OOid = 21,
    ORawHex = 22,
    ORawId = 23,
    JsonDateExt = 24,
    ODate = 25,
    OTimestamp = 26,
    OTimestampTz = 27,
    ODaySecond = 28,
    OYearMonth = 29,
    MySqlDate = 30,
    MySqlDatetime = 31,
}

impl JsonType {
    fn from_binary(value: u8, offset: usize) -> Result<Self, BinaryError> {
        let kind = match value {
            0 => Self::Null,
            1 => Self::Decimal,
            2 => Self::Int,
            3 => Self::Uint,
            4 => Self::Double,
            5 => Self::String,
            6 => Self::Object,
            7 => Self::Array,
            8 => Self::Boolean,
            9 => Self::Date,
            10 => Self::Time,
            11 => Self::Datetime,
            12 => Self::Timestamp,
            13 => Self::Opaque,
            15 => Self::OFloat,
            16 => Self::ODouble,
            17 => Self::ODecimal,
            18 => Self::OInt,
            19 => Self::OLong,
            20 => Self::OBinary,
            21 => Self::OOid,
            22 => Self::ORawHex,
            23 => Self::ORawId,
            24 => Self::JsonDateExt,
            25 => Self::ODate,
            26 => Self::OTimestamp,
            27 => Self::OTimestampTz,
            28 => Self::ODaySecond,
            29 => Self::OYearMonth,
            32 => Self::MySqlDate,
            33 => Self::MySqlDatetime,
            _ => return Err(BinaryError::new(BinaryErrorKind::InvalidType, offset)),
        };
        Ok(kind)
    }

    pub fn is_container(self) -> bool {
        matches!(self, Self::Array | Self::Object)
    }
}

#[derive(Clone, Copy, Debug, Eq, PartialEq)]
pub enum BinaryErrorKind {
    Truncated,
    InvalidType,
    InvalidHeader,
    InvalidOffset,
    InvalidDepth,
}

#[derive(Clone, Debug, Eq, PartialEq)]
pub struct BinaryError {
    pub kind: BinaryErrorKind,
    pub byte_offset: usize,
}

impl BinaryError {
    fn new(kind: BinaryErrorKind, byte_offset: usize) -> Self {
        Self { kind, byte_offset }
    }
}

impl fmt::Display for BinaryError {
    fn fmt(&self, formatter: &mut fmt::Formatter) -> fmt::Result {
        write!(formatter, "invalid binary JSON at byte {}: {:?}", self.byte_offset, self.kind)
    }
}

impl std::error::Error for BinaryError {}

pub type BinaryResult<T> = std::result::Result<T, BinaryError>;

#[derive(Clone, Copy, Debug)]
struct ContainerMeta {
    kind: JsonType,
    count: u64,
    entry_type: u8,
    key_entries: usize,
    value_entries: usize,
}

#[derive(Clone, Copy, Debug)]
pub struct BinaryValue<'a> {
    bytes: &'a [u8],
    extend_seg_offset: usize,
    offset: usize,
    raw_type: u8,
    entry_type: u8,
}

impl<'a> BinaryValue<'a> {
    fn inline_encoding(&self, entry_type: u8) -> BinaryResult<Option<(u64, u8)>> {
        let kind = self.json_type()?;
        let encoded = match kind {
            JsonType::Null => Some((0, NULL | INLINE)),
            JsonType::Boolean => Some((u64::from(self.boolean()?), BOOLEAN | INLINE)),
            JsonType::Int => match self.number()? {
                Number::I64(value) if signed_var_type(value) <= entry_type => {
                    let encoded = match signed_var_type(value) {
                        0 => value as i8 as u8 as u64,
                        1 => value as i16 as u16 as u64,
                        2 => value as i32 as u32 as u64,
                        _ => value as u64,
                    };
                    Some((encoded, INT | INLINE))
                }
                _ => None,
            },
            JsonType::Uint => match self.number()? {
                Number::U64(value) if var_type(value) <= entry_type => {
                    Some((value, UINT | INLINE))
                }
                _ => None,
            },
            _ => None,
        };
        Ok(encoded)
    }

    pub fn json_type(&self) -> BinaryResult<JsonType> {
        JsonType::from_binary(self.raw_type & !INLINE, self.offset)
    }

    pub fn is_inline(&self) -> bool {
        self.raw_type & INLINE != 0
    }

    fn container(&self) -> BinaryResult<Option<ContainerMeta>> {
        let kind = self.json_type()?;
        if !kind.is_container() {
            return Ok(None);
        }
        if self.is_inline() {
            return Err(BinaryError::new(BinaryErrorKind::InvalidType, self.offset));
        }
        let header = read_bytes(self.bytes, self.offset, 2)?;
        if header[0] != kind as u8 {
            return Err(BinaryError::new(BinaryErrorKind::InvalidHeader, self.offset));
        }
        let flags = header[1];
        let entry_type = flags & 0x03;
        let count_type = (flags >> 2) & 0x03;
        let size_type = (flags >> 4) & 0x03;
        let count_offset = self.offset + 2;
        let count = read_var(self.bytes, count_offset, count_type)?;
        let size_offset = count_offset
            .checked_add(var_size(count_type))
            .ok_or_else(|| BinaryError::new(BinaryErrorKind::InvalidOffset, count_offset))?;
        let size = to_usize(read_var(self.bytes, size_offset, size_type)?, size_offset)?;
        let entries_offset = size_offset
            .checked_add(var_size(size_type))
            .ok_or_else(|| BinaryError::new(BinaryErrorKind::InvalidOffset, size_offset))?;
        let container_end = self
            .offset
            .checked_add(size)
            .ok_or_else(|| BinaryError::new(BinaryErrorKind::InvalidOffset, self.offset))?;
        if container_end > self.bytes.len() || entries_offset > container_end {
            return Err(BinaryError::new(BinaryErrorKind::Truncated, self.offset));
        }
        let count_usize = usize::try_from(count)
            .map_err(|_| BinaryError::new(BinaryErrorKind::InvalidOffset, count_offset))?;
        let key_entries = if kind == JsonType::Object {
            count_usize.checked_mul(var_size(entry_type) * 2)
        } else {
            Some(0)
        }
        .ok_or_else(|| BinaryError::new(BinaryErrorKind::InvalidOffset, entries_offset))?;
        let value_entries = entries_offset
            .checked_add(key_entries)
            .ok_or_else(|| BinaryError::new(BinaryErrorKind::InvalidOffset, entries_offset))?;
        let value_entries_size = count_usize
            .checked_mul(var_size(entry_type) + 1)
            .ok_or_else(|| BinaryError::new(BinaryErrorKind::InvalidOffset, value_entries))?;
        if value_entries
            .checked_add(value_entries_size)
            .is_none_or(|end| end > container_end)
        {
            return Err(BinaryError::new(BinaryErrorKind::Truncated, value_entries));
        }
        Ok(Some(ContainerMeta {
            kind,
            count,
            entry_type,
            key_entries: entries_offset,
            value_entries,
        }))
    }

    pub fn len(&self) -> BinaryResult<u64> {
        Ok(self.container()?.map_or(1, |container| container.count))
    }

    pub fn value(&self, index: u64) -> BinaryResult<BinaryValue<'a>> {
        let container = self
            .container()?
            .ok_or_else(|| BinaryError::new(BinaryErrorKind::InvalidType, self.offset))?;
        if index >= container.count {
            return Err(BinaryError::new(BinaryErrorKind::InvalidOffset, self.offset));
        }
        let index = usize::try_from(index)
            .map_err(|_| BinaryError::new(BinaryErrorKind::InvalidOffset, self.offset))?;
        let entry_size = var_size(container.entry_type);
        let entry_offset = container.value_entries + index * (entry_size + 1);
        let value_offset = to_usize(
            read_var(self.bytes, entry_offset, container.entry_type)?,
            entry_offset,
        )?;
        let raw_type = *read_bytes(self.bytes, entry_offset + entry_size, 1)?
            .first()
            .expect("one byte was requested");
        let (offset, raw_type, entry_type) = if raw_type & INLINE != 0 {
            (entry_offset, raw_type, container.entry_type)
        } else if raw_type == FORWARD {
            let offset = self
                .extend_seg_offset
                .checked_add(value_offset)
                .ok_or_else(|| BinaryError::new(BinaryErrorKind::InvalidOffset, entry_offset))?;
            let raw_type = read_bytes(self.bytes, offset, 1)?[0];
            let payload_offset = if needs_type_prefix(raw_type) {
                offset
            } else {
                offset + 1
            };
            (payload_offset, raw_type, container.entry_type)
        } else {
            let offset = self
                .offset
                .checked_add(value_offset)
                .ok_or_else(|| BinaryError::new(BinaryErrorKind::InvalidOffset, entry_offset))?;
            (offset, raw_type, container.entry_type)
        };
        let value = BinaryValue {
            bytes: self.bytes,
            extend_seg_offset: self.extend_seg_offset,
            offset,
            raw_type,
            entry_type,
        };
        value.validate_scalar()?;
        Ok(value)
    }

    pub fn key(&self, index: u64) -> BinaryResult<&'a [u8]> {
        let container = self
            .container()?
            .ok_or_else(|| BinaryError::new(BinaryErrorKind::InvalidType, self.offset))?;
        if container.kind != JsonType::Object || index >= container.count {
            return Err(BinaryError::new(BinaryErrorKind::InvalidOffset, self.offset));
        }
        let index = usize::try_from(index)
            .map_err(|_| BinaryError::new(BinaryErrorKind::InvalidOffset, self.offset))?;
        let entry_size = var_size(container.entry_type);
        let entry_offset = container.key_entries + index * entry_size * 2;
        let key_offset = to_usize(
            read_var(self.bytes, entry_offset, container.entry_type)?,
            entry_offset,
        )?;
        let key_length = to_usize(
            read_var(self.bytes, entry_offset + entry_size, container.entry_type)?,
            entry_offset + entry_size,
        )?;
        let key_offset = self
            .offset
            .checked_add(key_offset)
            .ok_or_else(|| BinaryError::new(BinaryErrorKind::InvalidOffset, entry_offset))?;
        read_bytes(self.bytes, key_offset, key_length)
    }

    pub fn member(&self, key: &[u8]) -> BinaryResult<Option<BinaryValue<'a>>> {
        let count = self.len()?;
        for index in 0..count {
            if self.key(index)? == key {
                return self.value(index).map(Some);
            }
        }
        Ok(None)
    }

    pub fn boolean(&self) -> BinaryResult<bool> {
        if self.json_type()? != JsonType::Boolean {
            return Err(BinaryError::new(BinaryErrorKind::InvalidType, self.offset));
        }
        if self.is_inline() {
            Ok(read_var(self.bytes, self.offset, self.entry_type)? != 0)
        } else {
            Ok(read_bytes(self.bytes, self.offset, 1)?[0] != 0)
        }
    }

    pub fn number(&self) -> BinaryResult<Number> {
        let kind = self.json_type()?;
        match kind {
            JsonType::Int | JsonType::OInt => {
                let value = if self.is_inline() {
                    let value = read_var(self.bytes, self.offset, self.entry_type)?;
                    match self.entry_type {
                        0 => value as u8 as i8 as i64,
                        1 => value as u16 as i16 as i64,
                        2 => value as u32 as i32 as i64,
                        _ => value as i64,
                    }
                } else {
                    read_vi64(self.bytes, self.offset)?.0 as i64
                };
                Ok(Number::I64(value))
            }
            JsonType::Uint | JsonType::OLong => {
                let value = if self.is_inline() {
                    read_var(self.bytes, self.offset, self.entry_type)?
                } else {
                    read_vi64(self.bytes, self.offset)?.0
                };
                Ok(Number::U64(value))
            }
            JsonType::Double | JsonType::ODouble => {
                let bytes = read_bytes(self.bytes, self.offset, 8)?;
                Ok(Number::F64(f64::from_le_bytes(
                    bytes.try_into().expect("eight bytes were requested"),
                )))
            }
            JsonType::OFloat => {
                let bytes = read_bytes(self.bytes, self.offset, 4)?;
                Ok(Number::F64(f32::from_le_bytes(
                    bytes.try_into().expect("four bytes were requested"),
                ) as f64))
            }
            _ => Err(BinaryError::new(BinaryErrorKind::InvalidType, self.offset)),
        }
    }

    pub fn string_bytes(&self) -> BinaryResult<&'a [u8]> {
        if self.json_type()? != JsonType::String || self.is_inline() {
            return Err(BinaryError::new(BinaryErrorKind::InvalidType, self.offset));
        }
        let (length, payload) = read_vi64(self.bytes, self.offset + 1)?;
        let length = usize::try_from(length)
            .map_err(|_| BinaryError::new(BinaryErrorKind::InvalidOffset, payload))?;
        read_bytes(self.bytes, payload, length)
    }

    pub fn opaque_field_type(&self) -> BinaryResult<u16> {
        if self.json_type()? != JsonType::Opaque || self.is_inline() {
            return Err(BinaryError::new(BinaryErrorKind::InvalidType, self.offset));
        }
        let bytes = read_bytes(self.bytes, self.offset + 1, 2)?;
        Ok(u16::from_le_bytes([bytes[0], bytes[1]]))
    }

    fn validate_scalar(&self) -> BinaryResult<()> {
        let kind = self.json_type()?;
        if kind.is_container() {
            self.container()?;
            return Ok(());
        }
        if self.is_inline() {
            read_var(self.bytes, self.offset, self.entry_type)?;
            return Ok(());
        }
        match kind {
            JsonType::Null => Ok(()),
            JsonType::Boolean => {
                read_bytes(self.bytes, self.offset, 1)?;
                Ok(())
            }
            JsonType::Int | JsonType::Uint | JsonType::OInt | JsonType::OLong => {
                read_vi64(self.bytes, self.offset)?;
                Ok(())
            }
            JsonType::Double | JsonType::ODouble => {
                read_bytes(self.bytes, self.offset, 8)?;
                Ok(())
            }
            JsonType::OFloat => {
                read_bytes(self.bytes, self.offset, 4)?;
                Ok(())
            }
            JsonType::String => {
                let (length, payload) = read_vi64(self.bytes, self.offset + 1)?;
                let length = usize::try_from(length)
                    .map_err(|_| BinaryError::new(BinaryErrorKind::InvalidOffset, payload))?;
                read_bytes(self.bytes, payload, length)?;
                Ok(())
            }
            _ => Ok(()),
        }
    }
}

fn write_binary_value(output: &mut Vec<u8>, value: BinaryValue<'_>) -> BinaryResult<()> {
    let kind = value.json_type()?;
    match kind {
        JsonType::Null => output.push(NULL),
        JsonType::Boolean => output.push(u8::from(value.boolean()?)),
        JsonType::Int | JsonType::Uint | JsonType::OInt | JsonType::OLong => {
            let number = value.number()?;
            let encoded = match number {
                Number::I64(number) => number as u64,
                Number::U64(number) => number,
                Number::F64(_) => return Err(BinaryError::new(BinaryErrorKind::InvalidType, value.offset)),
            };
            push_vi64(output, encoded);
        }
        JsonType::Double | JsonType::ODouble => {
            output.extend_from_slice(read_bytes(value.bytes, value.offset, 8)?);
        }
        JsonType::OFloat => {
            output.extend_from_slice(read_bytes(value.bytes, value.offset, 4)?);
        }
        JsonType::String => {
            let (length, payload) = read_vi64(value.bytes, value.offset + 1)?;
            let length = to_usize(length, payload)?;
            let bytes = read_bytes(value.bytes, value.offset, payload - value.offset + length)?;
            output.extend_from_slice(bytes);
        }
        JsonType::Object | JsonType::Array => write_binary_container(output, value, 0)?,
        JsonType::Opaque => {
            let length = i64::from_le_bytes(
                read_bytes(value.bytes, value.offset + 3, 8)?
                    .try_into()
                    .expect("eight bytes were requested"),
            );
            let length = usize::try_from(length)
                .map_err(|_| BinaryError::new(BinaryErrorKind::InvalidOffset, value.offset + 3))?;
            let size = 11usize
                .checked_add(length)
                .ok_or_else(|| BinaryError::new(BinaryErrorKind::InvalidOffset, value.offset))?;
            output.extend_from_slice(read_bytes(value.bytes, value.offset, size)?);
        }
        JsonType::Date | JsonType::MySqlDate | JsonType::JsonDateExt => {
            output.extend_from_slice(read_bytes(value.bytes, value.offset, 4)?);
        }
        JsonType::Time
        | JsonType::Datetime
        | JsonType::MySqlDatetime
        | JsonType::Timestamp
        | JsonType::ODate
        | JsonType::OTimestamp
        | JsonType::OTimestampTz => {
            output.extend_from_slice(read_bytes(value.bytes, value.offset, 8)?);
        }
        _ => return Err(BinaryError::new(BinaryErrorKind::InvalidType, value.offset)),
    }
    Ok(())
}

fn write_binary_container(
    output: &mut Vec<u8>,
    value: BinaryValue<'_>,
    estimated_size: u64,
) -> BinaryResult<()> {
    let container = value
        .container()?
        .ok_or_else(|| BinaryError::new(BinaryErrorKind::InvalidType, value.offset))?;
    let start = output.len();
    let count = to_usize(container.count, value.offset)?;
    let count_type = var_type(container.count);
    let size_type = var_type(estimated_size);
    let entry_type = size_type;
    let entry_size = var_size(entry_type);
    output.push(container.kind as u8);
    output.push(entry_type | (count_type << 2) | (size_type << 4) | 0x40);
    push_var(output, container.count, count_type);
    let size_position = output.len();
    push_var(output, estimated_size, size_type);
    let key_entries = output.len();
    if container.kind == JsonType::Object {
        output.resize(output.len() + count * entry_size * 2, 0);
    }
    let value_entries = output.len();
    output.resize(output.len() + count * (entry_size + 1), 0);

    if container.kind == JsonType::Object {
        for index in 0..count {
            let key = value.key(index as u64)?;
            let key_offset = (output.len() - start) as u64;
            let position = key_entries + index * entry_size * 2;
            set_var(output, position, key_offset, entry_type);
            set_var(output, position + entry_size, key.len() as u64, entry_type);
            output.extend_from_slice(key);
        }
    }
    for index in 0..count {
        let child = value.value(index as u64)?;
        let value_offset = (output.len() - start) as u64;
        let position = value_entries + index * (entry_size + 1);
        if let Some((encoded, kind)) = child.inline_encoding(entry_type)? {
            set_var(output, position, encoded, entry_type);
            output[position + entry_size] = kind;
        } else {
            set_var(output, position, value_offset, entry_type);
            output[position + entry_size] = child.raw_type & !INLINE;
            write_binary_value(output, child)?;
        }
    }

    let actual_size = (output.len() - start) as u64;
    if var_type(actual_size) > size_type {
        output.truncate(start);
        write_binary_container(output, value, actual_size)
    } else {
        set_var(output, size_position, actual_size, size_type);
        Ok(())
    }
}

pub fn serialize_value(value: BinaryValue<'_>) -> BinaryResult<Vec<u8>> {
    let mut output = Vec::new();
    if value.json_type()?.is_container() {
        output.resize(8, 0);
        write_binary_value(&mut output, value)?;
        let header = DOC_HEADER as u64 | ((output.len() as u64) << 22);
        output[..8].copy_from_slice(&header.to_le_bytes());
    } else {
        if !needs_type_prefix(value.raw_type & !INLINE) {
            output.push(value.raw_type & !INLINE);
        }
        write_binary_value(&mut output, value)?;
    }
    Ok(output)
}

pub fn serialize_array(values: &[BinaryValue<'_>]) -> BinaryResult<Vec<u8>> {
    fn write(
        output: &mut Vec<u8>,
        values: &[BinaryValue<'_>],
        estimated_size: u64,
    ) -> BinaryResult<()> {
        let start = output.len();
        let count_type = var_type(values.len() as u64);
        let size_type = var_type(estimated_size);
        let entry_type = size_type;
        let entry_size = var_size(entry_type);
        output.push(ARRAY);
        output.push(entry_type | (count_type << 2) | (size_type << 4) | 0x40);
        push_var(output, values.len() as u64, count_type);
        let size_position = output.len();
        push_var(output, estimated_size, size_type);
        let value_entries = output.len();
        output.resize(output.len() + values.len() * (entry_size + 1), 0);
        for (index, child) in values.iter().copied().enumerate() {
            let value_offset = (output.len() - start) as u64;
            let position = value_entries + index * (entry_size + 1);
            if let Some((encoded, kind)) = child.inline_encoding(entry_type)? {
                set_var(output, position, encoded, entry_type);
                output[position + entry_size] = kind;
            } else {
                set_var(output, position, value_offset, entry_type);
                output[position + entry_size] = child.raw_type & !INLINE;
                write_binary_value(output, child)?;
            }
        }
        let actual_size = (output.len() - start) as u64;
        if var_type(actual_size) > size_type {
            output.truncate(start);
            write(output, values, actual_size)
        } else {
            set_var(output, size_position, actual_size, size_type);
            Ok(())
        }
    }

    let mut output = vec![0; 8];
    write(&mut output, values, 0)?;
    let header = DOC_HEADER as u64 | ((output.len() as u64) << 22);
    output[..8].copy_from_slice(&header.to_le_bytes());
    Ok(output)
}

#[derive(Clone, Debug)]
pub struct BinaryDocument<B> {
    bytes: B,
    extend_seg_offset: usize,
    root_offset: usize,
    root_type: u8,
}

impl<B: AsRef<[u8]>> BinaryDocument<B> {
    pub fn new(bytes: B) -> BinaryResult<Self> {
        let input = bytes.as_ref();
        let first = *input
            .first()
            .ok_or_else(|| BinaryError::new(BinaryErrorKind::Truncated, 0))?;
        let (extend_seg_offset, root_offset) = if first == DOC_HEADER {
            let header = u64::from_le_bytes(
                read_bytes(input, 0, 8)?.try_into().expect("eight bytes were requested"),
            );
            let extend = to_usize((header >> 22) & ((1u64 << 42) - 1), 0)?;
            if extend > input.len() {
                return Err(BinaryError::new(BinaryErrorKind::InvalidOffset, 0));
            }
            (extend, 8)
        } else {
            (0, 0)
        };
        let root_type = read_bytes(input, root_offset, 1)?[0];
        let root_payload = if needs_type_prefix(root_type) {
            root_offset
        } else {
            root_offset + 1
        };
        let document = Self {
            bytes,
            extend_seg_offset,
            root_offset: root_payload,
            root_type,
        };
        document.root()?.validate_scalar()?;
        Ok(document)
    }

    pub fn root(&self) -> BinaryResult<BinaryValue<'_>> {
        Ok(BinaryValue {
            bytes: self.bytes.as_ref(),
            extend_seg_offset: self.extend_seg_offset,
            offset: self.root_offset,
            raw_type: self.root_type,
            entry_type: 0,
        })
    }

    pub fn depth(&self) -> BinaryResult<u32> {
        let root = self.root()?;
        let depth_bound = self.bytes.as_ref().len().saturating_add(1);
        struct Frame<'a> {
            value: BinaryValue<'a>,
            container: ContainerMeta,
            next_index: u64,
            depth: u32,
        }
        let mut stack = Vec::new();
        let mut maximum = 1;
        if let Some(container) = root.container()? {
            stack.push(Frame {
                value: root,
                container,
                next_index: 0,
                depth: 1,
            });
        }
        while !stack.is_empty() {
            let child = {
                let frame = stack.last_mut().expect("stack is not empty");
                if frame.next_index == frame.container.count {
                    stack.pop();
                    continue;
                }
                let index = frame.next_index;
                frame.next_index += 1;
                (frame.value.value(index)?, frame.depth + 1)
            };
            let (child, depth) = child;
            if depth as usize > depth_bound {
                return Err(BinaryError::new(BinaryErrorKind::InvalidDepth, child.offset));
            }
            maximum = maximum.max(depth);
            if let Some(container) = child.container()? {
                stack.push(Frame {
                    value: child,
                    container,
                    next_index: 0,
                    depth,
                });
            }
        }
        Ok(maximum)
    }

    pub fn as_bytes(&self) -> &[u8] {
        self.bytes.as_ref()
    }

    pub fn into_bytes(self) -> B {
        self.bytes
    }
}

fn needs_type_prefix(value_type: u8) -> bool {
    matches!(value_type, STRING | OBJECT | ARRAY | 13 | 20 | 21 | 22 | 23 | 28 | 29)
}

fn read_bytes(bytes: &[u8], offset: usize, length: usize) -> BinaryResult<&[u8]> {
    let end = offset
        .checked_add(length)
        .ok_or_else(|| BinaryError::new(BinaryErrorKind::InvalidOffset, offset))?;
    bytes
        .get(offset..end)
        .ok_or_else(|| BinaryError::new(BinaryErrorKind::Truncated, offset))
}

fn read_var(bytes: &[u8], offset: usize, kind: u8) -> BinaryResult<u64> {
    let input = read_bytes(bytes, offset, var_size(kind))?;
    let mut buffer = [0u8; 8];
    buffer[..input.len()].copy_from_slice(input);
    Ok(u64::from_le_bytes(buffer))
}

fn to_usize(value: u64, offset: usize) -> BinaryResult<usize> {
    usize::try_from(value).map_err(|_| BinaryError::new(BinaryErrorKind::InvalidOffset, offset))
}

fn read_vi64(bytes: &[u8], offset: usize) -> BinaryResult<(u64, usize)> {
    let mut value = 0u64;
    for index in 0..10 {
        let byte = read_bytes(bytes, offset + index, 1)?[0];
        if index == 9 && byte > 1 {
            return Err(BinaryError::new(BinaryErrorKind::InvalidOffset, offset + index));
        }
        value |= u64::from(byte & 0x7f) << (index * 7);
        if byte & 0x80 == 0 {
            return Ok((value, offset + index + 1));
        }
    }
    Err(BinaryError::new(BinaryErrorKind::InvalidOffset, offset + 9))
}

fn var_type(value: u64) -> u8 {
    if value <= u8::MAX as u64 {
        0
    } else if value <= u16::MAX as u64 {
        1
    } else if value <= u32::MAX as u64 {
        2
    } else {
        3
    }
}

fn signed_var_type(value: i64) -> u8 {
    if i8::try_from(value).is_ok() {
        0
    } else if i16::try_from(value).is_ok() {
        1
    } else if i32::try_from(value).is_ok() {
        2
    } else {
        3
    }
}

fn var_size(kind: u8) -> usize {
    1usize << kind
}

fn push_var(output: &mut Vec<u8>, value: u64, kind: u8) {
    let bytes = value.to_le_bytes();
    output.extend_from_slice(&bytes[..var_size(kind)]);
}

fn set_var(output: &mut [u8], position: usize, value: u64, kind: u8) {
    let size = var_size(kind);
    output[position..position + size].copy_from_slice(&value.to_le_bytes()[..size]);
}

fn vi64_len(value: u64) -> u64 {
    let bits = 64 - value.leading_zeros() as u64;
    ((bits.max(1) + 6) / 7).min(10)
}

fn push_vi64(output: &mut Vec<u8>, mut value: u64) {
    while value > 0x7f {
        output.push(value as u8 | 0x80);
        value >>= 7;
    }
    output.push(value as u8);
}

fn serialized_size(document: &Document) -> u64 {
    match document {
        Document::Null | Document::Bool(_) => 1,
        Document::Number(Number::I64(value)) => vi64_len(*value as u64),
        Document::Number(Number::U64(value)) => vi64_len(*value),
        Document::Number(Number::F64(_)) => 8,
        Document::String(value) => 1 + vi64_len(value.len() as u64) + value.len() as u64,
        Document::Array(values) => array_size(values),
        Document::Object(values) => object_size(values),
    }
}

fn container_size(
    count: usize,
    key_bytes: u64,
    key_length_bytes: u64,
    value_bytes: u64,
    last_value_bytes: u64,
    object: bool,
) -> u64 {
    let count_bytes = var_size(var_type(count as u64)) as u64;
    let estimated_entry_bytes = if object {
        (4 + 4 + 1) * count as u64 + key_length_bytes
    } else {
        (4 + 1) * count as u64
    };
    let estimated_total = 2 + count_bytes + 4 + estimated_entry_bytes + key_bytes + value_bytes;
    let last_offset = estimated_total - last_value_bytes;
    let entry_type = var_type(last_offset);
    let entry_bytes = var_size(entry_type) as u64;
    let entry_array_bytes = if object {
        (entry_bytes * 2 + 1) * count as u64 + key_length_bytes
    } else {
        (entry_bytes + 1) * count as u64
    };
    let total_with_entries = 2 + count_bytes + 4 + entry_array_bytes + key_bytes + value_bytes;
    let size_bytes = var_size(var_type(total_with_entries)) as u64;
    2 + count_bytes + size_bytes + entry_array_bytes + key_bytes + value_bytes
}

fn array_size(values: &[Document]) -> u64 {
    let mut value_bytes = 0;
    let mut last_value_bytes = 0;
    for value in values {
        last_value_bytes = serialized_size(value);
        value_bytes += last_value_bytes;
    }
    container_size(values.len(), 0, 0, value_bytes, last_value_bytes, false)
}

fn object_size(values: &[(String, Document)]) -> u64 {
    let key_bytes = values.iter().map(|(key, _)| key.len() as u64).sum();
    let key_length_bytes = values
        .iter()
        .map(|(key, _)| var_size(var_type(key.len() as u64)) as u64)
        .sum();
    let mut value_bytes = 0;
    let mut last_value_bytes = 0;
    for (_, value) in values {
        last_value_bytes = serialized_size(value);
        value_bytes += last_value_bytes;
    }
    container_size(
        values.len(),
        key_bytes,
        key_length_bytes,
        value_bytes,
        last_value_bytes,
        true,
    )
}

fn value_type(document: &Document) -> u8 {
    match document {
        Document::Null => NULL,
        Document::Bool(_) => BOOLEAN,
        Document::Number(Number::I64(_)) => INT,
        Document::Number(Number::U64(_)) => UINT,
        Document::Number(Number::F64(_)) => DOUBLE,
        Document::String(_) => STRING,
        Document::Array(_) => ARRAY,
        Document::Object(_) => OBJECT,
    }
}

fn inline_value(document: &Document, entry_type: u8) -> Option<(u64, u8)> {
    match document {
        Document::Null => Some((0, NULL | INLINE)),
        Document::Bool(value) => Some((*value as u64, BOOLEAN | INLINE)),
        Document::Number(Number::I64(value)) if signed_var_type(*value) <= entry_type => {
            let encoded = match signed_var_type(*value) {
                0 => *value as i8 as u8 as u64,
                1 => *value as i16 as u16 as u64,
                2 => *value as i32 as u32 as u64,
                _ => *value as u64,
            };
            Some((encoded, INT | INLINE))
        }
        Document::Number(Number::U64(value)) if var_type(*value) <= entry_type => {
            Some((*value, UINT | INLINE))
        }
        _ => None,
    }
}

fn write_string(output: &mut Vec<u8>, value: &str) {
    output.push(STRING);
    push_vi64(output, value.len() as u64);
    output.extend_from_slice(value.as_bytes());
}

fn write_value(output: &mut Vec<u8>, document: &Document) {
    match document {
        Document::Null => output.push(NULL),
        Document::Bool(value) => output.push(*value as u8),
        Document::Number(Number::I64(value)) => push_vi64(output, *value as u64),
        Document::Number(Number::U64(value)) => push_vi64(output, *value),
        Document::Number(Number::F64(value)) => output.extend_from_slice(&value.to_le_bytes()),
        Document::String(value) => write_string(output, value),
        Document::Array(values) => write_array(output, values),
        Document::Object(values) => write_object(output, values),
    }
}

#[derive(Clone, Copy)]
enum Container<'a> {
    Array(&'a [Document]),
    Object(&'a [(String, Document)]),
}

impl<'a> Container<'a> {
    fn len(self) -> usize {
        match self {
            Self::Array(values) => values.len(),
            Self::Object(values) => values.len(),
        }
    }

    fn value(self, index: usize) -> &'a Document {
        match self {
            Self::Array(values) => &values[index],
            Self::Object(values) => &values[index].1,
        }
    }
}

fn write_container(output: &mut Vec<u8>, container: Container<'_>, estimated_size: u64) {
    let start = output.len();
    let count = container.len();
    let count_type = var_type(count as u64);
    let size_type = var_type(estimated_size);
    let entry_type = size_type;
    let entry_size = var_size(entry_type);
    let kind = match container {
        Container::Array(_) => ARRAY,
        Container::Object(_) => OBJECT,
    };
    output.push(kind);
    output.push(entry_type | (count_type << 2) | (size_type << 4) | 0x40);
    push_var(output, count as u64, count_type);
    let size_position = output.len();
    push_var(output, estimated_size, size_type);
    let key_entries = output.len();
    if matches!(container, Container::Object(_)) {
        output.resize(output.len() + count * entry_size * 2, 0);
    }
    let value_entries = output.len();
    output.resize(output.len() + count * (entry_size + 1), 0);

    if let Container::Object(entries) = container {
        for (index, (key, _)) in entries.iter().enumerate() {
            let key_offset = (output.len() - start) as u64;
            let position = key_entries + index * entry_size * 2;
            set_var(output, position, key_offset, entry_type);
            set_var(output, position + entry_size, key.len() as u64, entry_type);
            output.extend_from_slice(key.as_bytes());
        }
    }

    for index in 0..count {
        let value = container.value(index);
        let value_offset = (output.len() - start) as u64;
        let position = value_entries + index * (entry_size + 1);
        if let Some((encoded, kind)) = inline_value(value, entry_type) {
            set_var(output, position, encoded, entry_type);
            output[position + entry_size] = kind;
        } else {
            set_var(output, position, value_offset, entry_type);
            output[position + entry_size] = value_type(value);
            write_value(output, value);
        }
    }

    let actual_size = (output.len() - start) as u64;
    if var_type(actual_size) > size_type {
        output.truncate(start);
        write_container(output, container, actual_size);
    } else {
        set_var(output, size_position, actual_size, size_type);
    }
}

fn write_array(output: &mut Vec<u8>, values: &[Document]) {
    write_container(output, Container::Array(values), array_size(values));
}

fn write_object(output: &mut Vec<u8>, values: &[(String, Document)]) {
    write_container(output, Container::Object(values), object_size(values));
}

fn sort_objects(document: &mut Document) {
    match document {
        Document::Array(values) => {
            for value in values {
                sort_objects(value);
            }
        }
        Document::Object(values) => {
            for (_, value) in values.iter_mut() {
                sort_objects(value);
            }
            values.sort_by(|(left, _), (right, _)| {
                left.len()
                    .cmp(&right.len())
                    .then_with(|| left.as_bytes().cmp(right.as_bytes()))
            });
        }
        _ => {}
    }
}

pub(crate) fn serialize(mut document: Document) -> Vec<u8> {
    sort_objects(&mut document);
    let mut output = Vec::with_capacity(serialized_size(&document) as usize + 8);
    match &document {
        Document::Array(_) | Document::Object(_) => {
            output.resize(8, 0);
            write_value(&mut output, &document);
            let header = DOC_HEADER as u64 | ((output.len() as u64) << 22);
            output[..8].copy_from_slice(&header.to_le_bytes());
        }
        Document::String(value) => write_string(&mut output, value),
        _ => {
            output.push(value_type(&document));
            write_value(&mut output, &document);
        }
    }
    output
}
