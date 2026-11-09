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

use crate::{decimal::Decimal, Document, Number};
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

pub const fn is_document_header(value: u8) -> bool { value == DOC_HEADER }
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
    pub(crate) fn from_binary(value: u8, offset: usize) -> Result<Self, BinaryError> {
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

    pub fn is_string(self) -> bool {
        matches!(
            self,
            Self::String
                | Self::OBinary
                | Self::OOid
                | Self::ORawHex
                | Self::ORawId
                | Self::ODaySecond
                | Self::OYearMonth
        )
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
    InvalidNumber,
}

#[derive(Clone, Debug, Eq, PartialEq)]
pub struct BinaryError {
    pub kind: BinaryErrorKind,
    pub byte_offset: usize,
}

impl BinaryError {
    pub(crate) fn new(kind: BinaryErrorKind, byte_offset: usize) -> Self {
        Self { kind, byte_offset }
    }
}

impl fmt::Display for BinaryError {
    fn fmt(&self, formatter: &mut fmt::Formatter) -> fmt::Result {
        write!(
            formatter,
            "invalid binary JSON at byte {}: {:?}",
            self.byte_offset, self.kind
        )
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

pub(crate) struct BinaryEntries<'a> {
    value: BinaryValue<'a>,
    container: ContainerMeta,
    indices: std::ops::Range<usize>,
}
impl<'a> BinaryEntries<'a> {
    fn get(&self, index: usize) -> BinaryResult<(&'a [u8], BinaryValue<'a>)> {
        let key = if self.container.kind == JsonType::Object { self.value.key_in(self.container, index as u64)? } else { &[] };
        Ok((key, self.value.value_in(self.container, index as u64)?))
    }
}
impl<'a> Iterator for BinaryEntries<'a> {
    type Item = BinaryResult<(&'a [u8], BinaryValue<'a>)>;
    fn next(&mut self) -> Option<Self::Item> { self.indices.next().map(|index| self.get(index)) }
    fn size_hint(&self) -> (usize, Option<usize>) { self.indices.size_hint() }
}
impl DoubleEndedIterator for BinaryEntries<'_> {
    fn next_back(&mut self) -> Option<Self::Item> { self.indices.next_back().map(|index| self.get(index)) }
}
impl ExactSizeIterator for BinaryEntries<'_> {}

#[derive(Clone)]
enum MemberOrder {
    Original(std::ops::Range<u64>),
    Sorted(std::vec::IntoIter<u64>),
}

impl Iterator for MemberOrder {
    type Item = u64;
    fn next(&mut self) -> Option<u64> {
        match self { Self::Original(indices) => indices.next(), Self::Sorted(indices) => indices.next() }
    }
    fn size_hint(&self) -> (usize, Option<usize>) {
        match self { Self::Original(indices) => indices.size_hint(), Self::Sorted(indices) => indices.size_hint() }
    }
}

impl<'a> BinaryValue<'a> {
    pub(crate) fn lexicographical(&self) -> bool {
        self.bytes.first() == Some(&DOC_HEADER) && self.bytes.get(1).is_some_and(|flags| flags & 1 != 0)
    }

    fn member_order(self, count: u64, lexicographical: bool) -> BinaryResult<MemberOrder> {
        let indices = 0..count;
        if self.raw_type & !INLINE != OBJECT || self.lexicographical() == lexicographical {
            return Ok(MemberOrder::Original(indices));
        }
        let keys: Vec<_> = indices.clone().map(|index| self.key(index)).collect::<BinaryResult<_>>()?;
        let mut indices: Vec<_> = indices.collect();
        indices.sort_by(|&left, &right| {
            let left = keys[left as usize];
            let right = keys[right as usize];
            if lexicographical { left.cmp(right) } else { left.len().cmp(&right.len()).then_with(|| left.cmp(right)) }
        });
        Ok(MemberOrder::Sorted(indices.into_iter()))
    }

    pub(crate) fn location(self) -> ValueLocation {
        ValueLocation { offset: self.offset, raw_type: self.raw_type, entry_type: self.entry_type }
    }

    fn inline_encoding(&self, entry_type: u8) -> BinaryResult<Option<(u64, u8)>> {
        let kind = self.json_type()?;
        let encoded = match kind {
            JsonType::Null => Some((0, NULL | INLINE)),
            JsonType::Boolean => Some((u64::from(self.boolean()?), BOOLEAN | INLINE)),
            JsonType::Int => match self.number()? {
                Number::I64(value) => signed_inline(value, entry_type),
                _ => None,
            },
            JsonType::Uint => match self.number()? {
                Number::U64(value) if var_type(value) <= entry_type => Some((value, UINT | INLINE)),
                _ => None,
            },
            _ => None,
        };
        Ok(encoded)
    }

    pub fn json_type(&self) -> BinaryResult<JsonType> {
        JsonType::from_binary(self.raw_type & !INLINE, self.offset)
    }

    pub(crate) fn storage_offset(&self) -> usize { self.offset }

    pub(crate) fn traversal_limit(&self) -> usize {
        self.bytes.len().saturating_add(1)
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
            return Err(BinaryError::new(
                BinaryErrorKind::InvalidHeader,
                self.offset,
            ));
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

    pub fn is_empty(&self) -> BinaryResult<bool> {
        Ok(self.len()? == 0)
    }

    pub fn value(&self, index: u64) -> BinaryResult<BinaryValue<'a>> {
        let container = self
            .container()?
            .ok_or_else(|| BinaryError::new(BinaryErrorKind::InvalidType, self.offset))?;
        self.value_in(container, index)
    }

    fn value_in(&self, container: ContainerMeta, index: u64) -> BinaryResult<BinaryValue<'a>> {
        if index >= container.count {
            return Err(BinaryError::new(
                BinaryErrorKind::InvalidOffset,
                self.offset,
            ));
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
        self.key_in(container, index)
    }

    fn key_in(&self, container: ContainerMeta, index: u64) -> BinaryResult<&'a [u8]> {
        if container.kind != JsonType::Object || index >= container.count {
            return Err(BinaryError::new(
                BinaryErrorKind::InvalidOffset,
                self.offset,
            ));
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

    pub(crate) fn entries(self) -> BinaryResult<BinaryEntries<'a>> {
        let container = self.container()?.ok_or_else(|| BinaryError::new(BinaryErrorKind::InvalidType, self.offset))?;
        Ok(BinaryEntries { value: self, container, indices: 0..container.count as usize })
    }

    pub fn member(&self, key: &[u8]) -> BinaryResult<Option<BinaryValue<'a>>> {
        Ok(self.member_index(key)?.map(|(_, value)| value))
    }

    pub(crate) fn member_index(&self, key: &[u8]) -> BinaryResult<Option<(u64, BinaryValue<'a>)>> {
        for index in 0..self.len()? {
            if self.key(index)? == key {
                return self.value(index).map(|value| Some((index, value)));
            }
        }
        Ok(None)
    }

    pub(crate) fn entries_in_order(self, lexicographical: bool)
        -> BinaryResult<impl Iterator<Item = BinaryResult<(&'a [u8], Self)>> + 'a> {
        let entries = self.entries()?;
        Ok(self.member_order(entries.len() as u64, lexicographical)?
            .map(move |index| entries.get(index as usize)))
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

    pub fn decimal(&self) -> BinaryResult<Decimal> {
        if !matches!(self.json_type()?, JsonType::Decimal | JsonType::ODecimal) || self.is_inline()
        {
            return Err(BinaryError::new(BinaryErrorKind::InvalidType, self.offset));
        }
        Decimal::decode(self.bytes, self.offset)
    }

    pub(crate) fn decimal_bytes(&self) -> BinaryResult<&'a [u8]> {
        read_bytes(self.bytes, self.offset, self.decimal()?.serialized_size())
    }

    pub(crate) fn temporal(&self) -> BinaryResult<i64> {
        match self.json_type()? {
            JsonType::Date | JsonType::MySqlDate | JsonType::JsonDateExt =>
                Ok(i32::from_le_bytes(read_bytes(self.bytes, self.offset, 4)?.try_into().unwrap()) as i64),
            JsonType::Time | JsonType::Datetime | JsonType::MySqlDatetime | JsonType::Timestamp
            | JsonType::ODate | JsonType::OTimestamp | JsonType::OTimestampTz =>
                Ok(i64::from_le_bytes(read_bytes(self.bytes, self.offset, 8)?.try_into().unwrap())),
            _ => Err(BinaryError::new(BinaryErrorKind::InvalidType, self.offset)),
        }
    }

    pub fn string_bytes(&self) -> BinaryResult<&'a [u8]> {
        if !self.json_type()?.is_string() || self.is_inline() {
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

    pub fn opaque_bytes(&self) -> BinaryResult<&'a [u8]> {
        self.opaque_field_type()?;
        let length = i64::from_le_bytes(
            read_bytes(self.bytes, self.offset + 3, 8)?
                .try_into()
                .expect("eight bytes were requested"),
        );
        let length = usize::try_from(length)
            .map_err(|_| BinaryError::new(BinaryErrorKind::InvalidOffset, self.offset + 3))?;
        read_bytes(self.bytes, self.offset + 11, length)
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
            JsonType::Decimal | JsonType::ODecimal => {
                self.decimal()?;
                Ok(())
            }
            JsonType::String
            | JsonType::OBinary
            | JsonType::OOid
            | JsonType::ORawHex
            | JsonType::ORawId
            | JsonType::ODaySecond
            | JsonType::OYearMonth => {
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
    write_ordered_binary_value(output, value, false)
}

fn write_ordered_binary_value(output: &mut Vec<u8>, value: BinaryValue<'_>, lexicographical: bool) -> BinaryResult<()> {
    if !value.json_type()?.is_container() {
        return write_planned_value(output, value, &mut std::iter::empty(), lexicographical);
    }
    let mut sizes = Vec::new();
    plan_binary_value(value, &mut sizes, lexicographical)?;
    write_planned_value(output, value, &mut sizes.into_iter(), lexicographical)
}

fn plan_binary_value(
    value: BinaryValue<'_>,
    sizes: &mut Vec<(u64, u8)>,
    lexicographical: bool,
) -> BinaryResult<u64> {
    if !value.json_type()?.is_container() {
        return scalar_binary_size(value);
    }
    struct Frame<'a> {
        value: BinaryValue<'a>,
        container: ContainerMeta,
        order: MemberOrder,
        payload: [u64; 4],
        plan_index: usize,
    }
    fn frame<'a>(
        value: BinaryValue<'a>,
        sizes: &mut Vec<(u64, u8)>,
        lexicographical: bool,
    ) -> BinaryResult<Frame<'a>> {
        let container = value
            .container()?
            .ok_or_else(|| BinaryError::new(BinaryErrorKind::InvalidType, value.offset))?;
        let plan_index = sizes.len();
        sizes.push((0, 0));
        Ok(Frame {
            value,
            container,
            order: value.member_order(container.count, lexicographical)?,
            payload: [0; 4],
            plan_index,
        })
    }
    fn append(frame: &mut Frame<'_>, child: BinaryValue<'_>, size: u64) -> BinaryResult<()> {
        for entry_type in 0..4 {
            if child.inline_encoding(entry_type)?.is_none() {
                frame.payload[entry_type as usize] += size;
            }
        }
        Ok(())
    }
    let mut active = std::collections::HashSet::from([value.offset]);
    let mut frames = vec![frame(value, sizes, lexicographical)?];
    loop {
        let current = frames
            .last_mut()
            .ok_or_else(|| BinaryError::new(BinaryErrorKind::InvalidType, value.offset))?;
        if let Some(index) = current.order.next() {
            if current.container.kind == JsonType::Object {
                let length = current.value.key(index)?.len() as u64;
                for size in &mut current.payload {
                    *size += length;
                }
            }
            let child = current.value.value(index)?;
            if child.json_type()?.is_container() {
                if !active.insert(child.offset) {
                    return Err(BinaryError::new(
                        BinaryErrorKind::InvalidDepth,
                        child.offset,
                    ));
                }
                frames.push(frame(child, sizes, lexicographical)?);
            } else {
                append(current, child, scalar_binary_size(child)?)?;
            }
            continue;
        }
        let completed = frames
            .pop()
            .ok_or_else(|| BinaryError::new(BinaryErrorKind::InvalidType, value.offset))?;
        active.remove(&completed.value.offset);
        let mut size_type = 0;
        let size = loop {
            let width = var_size(size_type) as u64;
            let entries = if completed.container.kind == JsonType::Object {
                3 * width + 1
            } else {
                width + 1
            };
            let size = 2
                + var_size(var_type(completed.container.count)) as u64
                + width
                + completed.container.count * entries
                + completed.payload[size_type as usize];
            let needed = var_type(size);
            if needed <= size_type {
                break size;
            }
            size_type = needed;
        };
        sizes[completed.plan_index] = (size, size_type);
        if let Some(parent) = frames.last_mut() {
            append(parent, completed.value, size)?;
        } else {
            return Ok(size);
        }
    }
}

fn scalar_binary_size(value: BinaryValue<'_>) -> BinaryResult<u64> {
    let size = match value.json_type()? {
        JsonType::Null | JsonType::Boolean => {
            if value.json_type()? == JsonType::Boolean {
                value.boolean()?;
            }
            1
        }
        JsonType::Int | JsonType::Uint | JsonType::OInt | JsonType::OLong => {
            match value.number()? {
                Number::I64(number) => vi64_len(number as u64),
                Number::U64(number) => vi64_len(number),
                Number::F64(_) => {
                    return Err(BinaryError::new(BinaryErrorKind::InvalidType, value.offset))
                }
            }
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
        JsonType::Decimal | JsonType::ODecimal => value.decimal()?.serialized_size() as u64,
        JsonType::String
        | JsonType::OBinary
        | JsonType::OOid
        | JsonType::ORawHex
        | JsonType::ORawId
        | JsonType::ODaySecond
        | JsonType::OYearMonth => {
            let (length, payload) = read_vi64(value.bytes, value.offset + 1)?;
            let length = to_usize(length, payload)?;
            (payload - value.offset + length) as u64
        }
        JsonType::Opaque => {
            let length = i64::from_le_bytes(
                read_bytes(value.bytes, value.offset + 3, 8)?
                    .try_into()
                    .expect("eight bytes were requested"),
            );
            let length = usize::try_from(length)
                .map_err(|_| BinaryError::new(BinaryErrorKind::InvalidOffset, value.offset + 3))?;
            11usize
                .checked_add(length)
                .ok_or_else(|| BinaryError::new(BinaryErrorKind::InvalidOffset, value.offset))?
                as u64
        }
        _ => return Err(BinaryError::new(BinaryErrorKind::InvalidType, value.offset)),
    };
    if !value.is_inline()
        && !matches!(
            value.json_type()?,
            JsonType::Null
                | JsonType::Boolean
                | JsonType::Int
                | JsonType::Uint
                | JsonType::OInt
                | JsonType::OLong
        )
    {
        read_bytes(value.bytes, value.offset, to_usize(size, value.offset)?)?;
    }
    Ok(size)
}

fn write_planned_value(
    output: &mut Vec<u8>,
    value: BinaryValue<'_>,
    sizes: &mut dyn Iterator<Item = (u64, u8)>,
    lexicographical: bool,
) -> BinaryResult<()> {
    struct Frame<'a> {
        value: BinaryValue<'a>,
        order: MemberOrder,
        next: usize,
        start: usize,
        entries: usize,
        entry_type: u8,
        size_position: usize,
        estimated_size: u64,
    }
    let mut frames: Vec<Frame<'_>> = Vec::new();
    let mut next = Some(value);
    loop {
        if let Some(value) = next.take() {
            if value.json_type()?.is_container() {
                let (estimated_size, entry_type) = sizes
                    .next()
                    .ok_or_else(|| BinaryError::new(BinaryErrorKind::InvalidType, value.offset))?;
                let container = value
                    .container()?
                    .ok_or_else(|| BinaryError::new(BinaryErrorKind::InvalidType, value.offset))?;
                let order = value.member_order(container.count, lexicographical)?;
                let start = output.len();
                let count = to_usize(container.count, value.offset)?;
                let count_type = var_type(container.count);
                let width = var_size(entry_type);
                output.push(container.kind as u8);
                output.push(entry_type | count_type << 2 | entry_type << 4 | 0x40);
                push_var(output, container.count, count_type);
                let size_position = output.len();
                push_var(output, estimated_size, entry_type);
                let keys = output.len();
                if container.kind == JsonType::Object {
                    output.resize(output.len() + count * width * 2, 0);
                }
                let entries = output.len();
                output.resize(output.len() + count * (width + 1), 0);
                if container.kind == JsonType::Object {
                    for (index, source_index) in order.clone().enumerate() {
                        let key = value.key(source_index)?;
                        let offset = (output.len() - start) as u64;
                        let position = keys + index * width * 2;
                        set_var(output, position, offset, entry_type);
                        set_var(output, position + width, key.len() as u64, entry_type);
                        output.extend_from_slice(key);
                    }
                }
                frames.push(Frame {
                    value,
                    order,
                    next: 0,
                    start,
                    entries,
                    entry_type,
                    size_position,
                    estimated_size,
                });
            } else {
                write_scalar_value(output, value)?;
            }
        }
        let Some(frame) = frames.last_mut() else {
            return Ok(());
        };
        let Some(index) = frame.order.next() else {
            let size = (output.len() - frame.start) as u64;
            debug_assert_eq!(size, frame.estimated_size);
            set_var(output, frame.size_position, size, frame.entry_type);
            frames.pop();
            continue;
        };
        let child = frame.value.value(index)?;
        let width = var_size(frame.entry_type);
        let position = frame.entries + frame.next * (width + 1);
        frame.next += 1;
        if let Some((encoded, kind)) = child.inline_encoding(frame.entry_type)? {
            set_var(output, position, encoded, frame.entry_type);
            output[position + width] = kind;
        } else {
            let offset = (output.len() - frame.start) as u64;
            set_var(output, position, offset, frame.entry_type);
            output[position + width] = child.raw_type & !INLINE;
            next = Some(child);
        }
    }
}

fn write_scalar_value(output: &mut Vec<u8>, value: BinaryValue<'_>) -> BinaryResult<()> {
    let kind = value.json_type()?;
    match kind {
        JsonType::Null => output.push(NULL),
        JsonType::Boolean => output.push(u8::from(value.boolean()?)),
        JsonType::Int | JsonType::Uint | JsonType::OInt | JsonType::OLong => {
            let number = value.number()?;
            let encoded = match number {
                Number::I64(number) => number as u64,
                Number::U64(number) => number,
                Number::F64(_) => {
                    return Err(BinaryError::new(BinaryErrorKind::InvalidType, value.offset))
                }
            };
            push_vi64(output, encoded);
        }
        JsonType::Double | JsonType::ODouble => {
            output.extend_from_slice(read_bytes(value.bytes, value.offset, 8)?);
        }
        JsonType::OFloat => {
            output.extend_from_slice(read_bytes(value.bytes, value.offset, 4)?);
        }
        JsonType::Decimal | JsonType::ODecimal => {
            let size = value.decimal()?.serialized_size();
            output.extend_from_slice(read_bytes(value.bytes, value.offset, size)?);
        }
        JsonType::String
        | JsonType::OBinary
        | JsonType::OOid
        | JsonType::ORawHex
        | JsonType::ORawId
        | JsonType::ODaySecond
        | JsonType::OYearMonth => {
            let (length, payload) = read_vi64(value.bytes, value.offset + 1)?;
            let length = to_usize(length, payload)?;
            let bytes = read_bytes(value.bytes, value.offset, payload - value.offset + length)?;
            output.extend_from_slice(bytes);
        }
        JsonType::Object | JsonType::Array => {
            return Err(BinaryError::new(BinaryErrorKind::InvalidType, value.offset))
        }
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
    }
    Ok(())
}
pub fn serialize_value(value: BinaryValue<'_>) -> BinaryResult<Vec<u8>> {
    serialize_value_in_order(value, value.lexicographical())
}

pub(crate) fn serialize_value_in_order(value: BinaryValue<'_>, lexicographical: bool) -> BinaryResult<Vec<u8>> {
    let mut output = Vec::new();
    if value.json_type()?.is_container() {
        output.resize(8, 0);
        write_ordered_binary_value(&mut output, value, lexicographical)?;
        let header = DOC_HEADER as u64 | (u64::from(lexicographical) << 8) | ((output.len() as u64) << 22);
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

/// Build an object without converting database scalar payloads to text.
pub fn serialize_object(values: &[(&[u8], BinaryValue<'_>)]) -> BinaryResult<Vec<u8>> {
    fn write(
        output: &mut Vec<u8>,
        values: &[(&[u8], BinaryValue<'_>)],
        estimate: u64,
    ) -> BinaryResult<()> {
        let start = output.len();
        let count_type = var_type(values.len() as u64);
        let size_type = var_type(estimate);
        let entry_size = var_size(size_type);
        output.push(OBJECT);
        output.push(size_type | (count_type << 2) | (size_type << 4) | 0x40);
        push_var(output, values.len() as u64, count_type);
        let size_position = output.len();
        push_var(output, estimate, size_type);
        let keys = output.len();
        output.resize(keys + values.len() * entry_size * 2, 0);
        let entries = output.len();
        output.resize(entries + values.len() * (entry_size + 1), 0);
        for (index, (key, _)) in values.iter().enumerate() {
            let offset = (output.len() - start) as u64;
            set_var(output, keys + index * entry_size * 2, offset, size_type);
            set_var(
                output,
                keys + index * entry_size * 2 + entry_size,
                key.len() as u64,
                size_type,
            );
            output.extend_from_slice(key);
        }
        for (index, (_, child)) in values.iter().enumerate() {
            let position = entries + index * (entry_size + 1);
            if let Some((encoded, kind)) = child.inline_encoding(size_type)? {
                set_var(output, position, encoded, size_type);
                output[position + entry_size] = kind;
            } else {
                let offset = (output.len() - start) as u64;
                set_var(output, position, offset, size_type);
                output[position + entry_size] = child.raw_type & !INLINE;
                write_binary_value(output, *child)?;
            }
        }
        let size = (output.len() - start) as u64;
        if var_type(size) > size_type {
            output.truncate(start);
            write(output, values, size)
        } else {
            set_var(output, size_position, size, size_type);
            Ok(())
        }
    }
    let mut sorted = values.to_vec();
    sorted.sort_by(|(a, _), (b, _)| a.len().cmp(&b.len()).then_with(|| a.cmp(b)));
    let mut output = vec![0; 8];
    write(&mut output, &sorted, 0)?;
    let header = DOC_HEADER as u64 | ((output.len() as u64) << 22);
    output[..8].copy_from_slice(&header.to_le_bytes());
    Ok(output)
}

#[derive(Clone, Copy)]
pub(crate) enum ConstructedScalar<'a> {
    Binary(BinaryValue<'a>),
    Document(&'a Document),
}
impl ConstructedScalar<'_> {
    pub(crate) fn kind(self) -> BinaryResult<JsonType> {
        match self { Self::Binary(value) => value.json_type(), Self::Document(value) => JsonType::from_binary(value_type(value), 0) }
    }
    fn size(self) -> BinaryResult<u64> {
        match self { Self::Binary(value) => scalar_binary_size(value), Self::Document(value) => Ok(serialized_size(value)) }
    }
    fn inline(self, width: u8) -> BinaryResult<Option<(u64, u8)>> {
        match self { Self::Binary(value) => value.inline_encoding(width), Self::Document(value) => Ok(inline_value(value, width)) }
    }
    fn tag(self) -> u8 {
        match self { Self::Binary(value) => value.raw_type & !INLINE, Self::Document(value) => value_type(value) }
    }
    fn write(self, output: &mut Vec<u8>) -> BinaryResult<()> {
        match self {
            Self::Binary(value) => write_scalar_value(output, value),
            Self::Document(value) => { write_value(output, value); Ok(()) }
        }
    }
    pub(crate) fn serialize_root(self) -> BinaryResult<Vec<u8>> {
        match self {
            Self::Binary(value) => serialize_value(value),
            Self::Document(value) => {
                let mut output = Vec::with_capacity(serialized_size(value) as usize + 1);
                if !matches!(value, Document::String(_)) { output.push(value_type(value)); }
                write_value(&mut output, value);
                Ok(output)
            }
        }
    }
}

/// A construction source presents children in their required object-key order.
/// Its values and key/scalar borrows remain valid throughout one synchronous encoding.
pub(crate) trait ConstructedSource {
    const CHECK_CYCLES: bool = true;
    type Value: Copy;
    fn lexicographical(&self) -> bool { false }
    fn identity(&self, value: Self::Value) -> (usize, usize);
    fn traversal_limit(&self) -> usize;
    fn kind(&self, value: Self::Value) -> BinaryResult<JsonType>;
    fn scalar(&self, value: Self::Value) -> BinaryResult<ConstructedScalar<'_>>;
    fn entries(&self, value: Self::Value) -> BinaryResult<Vec<(&[u8], Self::Value)>>;
}

struct BinaryConstruction<'a>(BinaryValue<'a>);

impl<'a> ConstructedSource for BinaryConstruction<'a> {
    type Value = BinaryValue<'a>;
    fn lexicographical(&self) -> bool { self.0.lexicographical() }
    fn identity(&self, value: Self::Value) -> (usize, usize) { (value.bytes.as_ptr() as usize, value.offset) }
    fn traversal_limit(&self) -> usize { self.0.traversal_limit() }
    fn kind(&self, value: Self::Value) -> BinaryResult<JsonType> { value.json_type() }
    fn scalar(&self, value: Self::Value) -> BinaryResult<ConstructedScalar<'_>> { Ok(ConstructedScalar::Binary(value)) }
    fn entries(&self, value: Self::Value) -> BinaryResult<Vec<(&[u8], Self::Value)>> {
        value.entries()?.collect()
    }
}

#[derive(Clone, Copy)]
enum BatchValue<'a> { Root, Value(BinaryValue<'a>) }
struct BatchConstruction<'a> { object: bool, values: &'a [(&'a [u8], BinaryValue<'a>)] }
impl<'a> ConstructedSource for BatchConstruction<'a> {
    type Value = BatchValue<'a>;
    fn identity(&self, value: Self::Value) -> (usize, usize) {
        match value { BatchValue::Root => (usize::MAX, 0), BatchValue::Value(value) => (value.bytes.as_ptr() as usize, value.offset) }
    }
    fn traversal_limit(&self) -> usize {
        self.values.iter().fold(1usize, |limit, (_, value)| limit.saturating_add(value.traversal_limit()))
    }
    fn kind(&self, value: Self::Value) -> BinaryResult<JsonType> {
        match value { BatchValue::Root => Ok(if self.object { JsonType::Object } else { JsonType::Array }), BatchValue::Value(value) => value.json_type() }
    }
    fn scalar(&self, value: Self::Value) -> BinaryResult<ConstructedScalar<'_>> {
        match value { BatchValue::Root => Err(BinaryError::new(BinaryErrorKind::InvalidType, 0)), BatchValue::Value(value) => Ok(ConstructedScalar::Binary(value)) }
    }
    fn entries(&self, value: Self::Value) -> BinaryResult<Vec<(&[u8], Self::Value)>> {
        match value {
            BatchValue::Root => Ok(self.values.iter().map(|(key, value)| (*key, BatchValue::Value(*value))).collect()),
            BatchValue::Value(value) => {
                let entries = value.entries()?;
                value.member_order(entries.len() as u64, false)?.map(|index| {
                    let (key, value) = entries.get(index as usize)?;
                    Ok((key, BatchValue::Value(value)))
                }).collect()
            }
        }
    }
}

struct ConstructedLayout<'a> {
    kind: JsonType,
    scalar: Option<ConstructedScalar<'a>>,
    children: Vec<(&'a [u8], usize)>,
    estimate: u64,
    size: u64,
    width: u8,
}

/// Encode with the established constructor size estimates, without recursive traversal.
pub fn serialize_constructed_value(value: BinaryValue<'_>) -> BinaryResult<Vec<u8>> {
    let source = BinaryConstruction(value);
    serialize_constructed_source(&source, value)
}

pub(crate) fn serialize_constructed(object: bool, values: &[(&[u8], BinaryValue<'_>)]) -> BinaryResult<Vec<u8>> {
    serialize_constructed_source(&BatchConstruction { object, values }, BatchValue::Root)
}

pub(crate) fn serialize_constructed_source<'a, S: ConstructedSource>(source: &'a S, root: S::Value) -> BinaryResult<Vec<u8>>
where S::Value: 'a {
    let kind = source.kind(root)?;
    if !kind.is_container() { return source.scalar(root)?.serialize_root(); }
    let entries = source.entries(root)?;
    let mut scalar_only = true;
    for (_, value) in &entries {
        if source.kind(*value)?.is_container() { scalar_only = false; break; }
    }
    if scalar_only {
        if source.traversal_limit() <= entries.len() { return Err(BinaryError::new(BinaryErrorKind::InvalidDepth, 0)); }
        let scalars = entries.iter().map(|(key, value)| {
            let scalar = source.scalar(*value)?;
            Ok((*key, scalar, scalar.size()?))
        }).collect::<BinaryResult<Vec<_>>>()?;
        let (_, size, width) = constructed_container_layout(kind, scalars.iter().map(|(key, _, size)| (*key, *size)), |width| {
            let mut payload = 0;
            for (_, scalar, size) in &scalars { if scalar.inline(width)?.is_none() { payload += size; } }
            Ok(payload)
        })?;
        let mut output = constructed_output(size)?;
        let (start, values) = write_constructed_header(&mut output, kind, size, width, scalars.iter().map(|(key, _, _)| *key));
        for (index, (_, scalar, _)) in scalars.iter().enumerate() {
            let position = values + index * (var_size(width) + 1);
            if let Some((encoded, tag)) = scalar.inline(width)? {
                set_var(&mut output, position, encoded, width);
                output[position + var_size(width)] = tag;
            } else {
                let offset = (output.len() - start) as u64;
                set_var(&mut output, position, offset, width);
                output[position + var_size(width)] = scalar.tag();
                scalar.write(&mut output)?;
            }
        }
        debug_assert_eq!(output.len() as u64, size + 8);
        finish_constructed_output(&mut output, source.lexicographical());
        return Ok(output);
    }
    let mut root_entries = Some(entries);
    enum Planning<V> { Enter(V, Option<(usize, usize)>), Exit((usize, usize)) }
    let mut layouts: Vec<ConstructedLayout<'a>> = Vec::new();
    let mut pending = vec![Planning::Enter(root, None)];
    let mut active = std::collections::HashSet::new();
    let mut remaining = source.traversal_limit();
    while let Some(task) = pending.pop() {
        let (value, parent) = match task {
            Planning::Enter(value, parent) => (value, parent),
            Planning::Exit(identity) => { active.remove(&identity); continue; }
        };
        if remaining == 0 { return Err(BinaryError::new(BinaryErrorKind::InvalidDepth, 0)); }
        remaining -= 1;
        let kind = source.kind(value)?;
        if S::CHECK_CYCLES && kind.is_container() {
            let identity = source.identity(value);
            if !active.insert(identity) { return Err(BinaryError::new(BinaryErrorKind::InvalidDepth, identity.1)); }
            pending.push(Planning::Exit(identity));
        }
        let entries = if kind.is_container() {
            if parent.is_none() { root_entries.take().unwrap() } else { source.entries(value)? }
        } else { Vec::new() };
        let scalar = if kind.is_container() { None } else { Some(source.scalar(value)?) };
        let scalar_size = scalar.map(ConstructedScalar::size).transpose()?.unwrap_or(0);
        let index = layouts.len();
        if let Some((parent, position)) = parent { layouts[parent].children[position].1 = index; }
        let children = entries.iter().map(|(key, _)| (*key, 0)).collect();
        layouts.push(ConstructedLayout { kind, scalar, children, estimate: scalar_size, size: scalar_size, width: 0 });
        for (position, (_, child)) in entries.into_iter().enumerate().rev() { pending.push(Planning::Enter(child, Some((index, position)))); }
    }
    for index in (0..layouts.len()).rev() {
        if layouts[index].scalar.is_some() { continue; }
        let (estimate, size, width) = constructed_container_layout(layouts[index].kind,
            layouts[index].children.iter().map(|(key, child)| {
                let child = &layouts[*child];
                (*key, child.estimate)
            }), |width| {
                let mut payload = 0;
                for (_, child) in &layouts[index].children {
                    let child = &layouts[*child];
                    if child.scalar.map(|value| value.inline(width)).transpose()?.flatten().is_none() { payload += child.size; }
                }
                Ok(payload)
            })?;
        layouts[index].estimate = estimate;
        layouts[index].size = size;
        layouts[index].width = width;
    }
    let mut output = constructed_output(layouts[0].size)?;
    struct Writing { index: usize, start: usize, entries: usize, next: usize }
    let mut frames = Vec::<Writing>::new();
    let mut next = Some(0);
    loop {
        if let Some(index) = next.take() {
            let layout = &layouts[index];
            if let Some(value) = layout.scalar {
                value.write(&mut output)?;
            } else {
                let (start, entries) = write_constructed_header(&mut output, layout.kind, layout.size, layout.width,
                    layout.children.iter().map(|(key, _)| *key));
                frames.push(Writing { index, start, entries, next: 0 });
            }
        }
        let Some(frame) = frames.last_mut() else { break; };
        let layout = &layouts[frame.index];
        if frame.next >= layout.children.len() {
            debug_assert_eq!((output.len() - frame.start) as u64, layout.size);
            frames.pop();
            continue;
        }
        let child = layout.children[frame.next].1;
        let width = var_size(layout.width);
        let position = frame.entries + frame.next * (width + 1);
        frame.next += 1;
        if let Some((encoded, kind)) = layouts[child].scalar.map(|value| value.inline(layout.width)).transpose()?.flatten() {
            set_var(&mut output, position, encoded, layout.width);
            output[position + width] = kind;
        } else {
            let offset = (output.len() - frame.start) as u64;
            set_var(&mut output, position, offset, layout.width);
            output[position + width] = layouts[child].scalar.map_or(layouts[child].kind as u8, ConstructedScalar::tag);
            next = Some(child);
        }
    }
    debug_assert_eq!(output.len() as u64, layouts[0].size + 8);
    finish_constructed_output(&mut output, source.lexicographical());
    Ok(output)
}

#[inline]
fn constructed_container_layout<'a>(kind: JsonType, children: impl Iterator<Item = (&'a [u8], u64)>, mut payload_size: impl FnMut(u8) -> BinaryResult<u64>) -> BinaryResult<(u64, u64, u8)> {
    let object = kind == JsonType::Object;
    let mut count = 0;
    let mut key_bytes = 0;
    let mut key_lengths = 0;
    let mut value_bytes = 0;
    let mut last_value = 0;
    for (key, estimate) in children {
        count += 1;
        key_bytes += key.len() as u64;
        if object { key_lengths += var_size(var_type(key.len() as u64)) as u64; }
        value_bytes += estimate;
        last_value = estimate;
    }
    let estimate = container_size(count, key_bytes, key_lengths, value_bytes, last_value, object);
    let mut width = var_type(estimate);
    let size = loop {
        let bytes = var_size(width) as u64;
        let payload = key_bytes + payload_size(width)?;
        let size = 2 + var_size(var_type(count as u64)) as u64 + bytes
            + count as u64 * if object { 3 * bytes + 1 } else { bytes + 1 } + payload;
        let needed = var_type(size);
        if needed <= width { break size; }
        width = needed;
    };
    Ok((estimate, size, width))
}

#[inline]
fn constructed_output(size: u64) -> BinaryResult<Vec<u8>> {
    let total = size.checked_add(8).and_then(|size| usize::try_from(size).ok())
        .ok_or_else(|| BinaryError::new(BinaryErrorKind::InvalidOffset, 0))?;
    let mut output = Vec::with_capacity(total);
    output.resize(8, 0);
    Ok(output)
}

#[inline]
fn write_constructed_header<'a>(output: &mut Vec<u8>, kind: JsonType, size: u64, entry_type: u8,
    keys: impl ExactSizeIterator<Item = &'a [u8]>) -> (usize, usize) {
    let object = kind == JsonType::Object;
    let start = output.len();
    let count = keys.len();
    let count_type = var_type(count as u64);
    let width = var_size(entry_type);
    output.push(kind as u8);
    output.push(entry_type | count_type << 2 | entry_type << 4 | 0x40);
    push_var(output, count as u64, count_type);
    push_var(output, size, entry_type);
    let key_entries = output.len();
    if object { output.resize(key_entries + count * width * 2, 0); }
    let entries = output.len();
    output.resize(entries + count * (width + 1), 0);
    if object {
        for (position, key) in keys.enumerate() {
            let entry = key_entries + position * width * 2;
            let offset = (output.len() - start) as u64;
            set_var(output, entry, offset, entry_type);
            set_var(output, entry + width, key.len() as u64, entry_type);
            output.extend_from_slice(key);
        }
    }
    (start, entries)
}

#[inline]
fn finish_constructed_output(output: &mut [u8], lexicographical: bool) {
    let header = DOC_HEADER as u64 | (u64::from(lexicographical) << 8) | ((output.len() as u64) << 22);
    output[..8].copy_from_slice(&header.to_le_bytes());
}

pub(crate) fn encode_scalar(value: &crate::construction::Scalar<'_>) -> BinaryResult<Vec<u8>> {
    use crate::construction::Scalar;
    let mut output = Vec::new();
    match value {
        Scalar::Null => output.extend_from_slice(&[NULL, NULL]),
        Scalar::Boolean(value) => output.extend_from_slice(&[BOOLEAN, u8::from(*value)]),
        Scalar::Signed(value) => { output.push(INT); push_vi64(&mut output, *value as u64); }
        Scalar::Unsigned(value) => { output.push(UINT); push_vi64(&mut output, *value); }
        Scalar::Float(value) => {
            if !value.is_finite() { return Err(BinaryError::new(BinaryErrorKind::InvalidNumber, 0)); }
            output.push(DOUBLE); output.extend_from_slice(&value.to_le_bytes());
        }
        Scalar::String(value) => {
            output.push(STRING); push_vi64(&mut output, value.len() as u64); output.extend_from_slice(value);
        }
        Scalar::Opaque { field_type, bytes } => {
            output.push(JsonType::Opaque as u8); output.extend_from_slice(&field_type.to_le_bytes());
            output.extend_from_slice(&(bytes.len() as u64).to_le_bytes()); output.extend_from_slice(bytes);
        }
        Scalar::Database { kind, payload } => {
            let json_type = JsonType::from_binary(*kind, 0)?;
            if json_type.is_container() { return Err(BinaryError::new(BinaryErrorKind::InvalidType, 0)); }
            output.push(*kind); output.extend_from_slice(payload);
        }
    }
    BinaryDocument::new(output.as_slice())?.depth()?;
    Ok(output)
}

pub(crate) fn aggregate_payload_length(value: BinaryValue<'_>) -> BinaryResult<u64> {
    if value.json_type()?.is_container() {
        if !value.lexicographical() && (value.extend_seg_offset == 0 || value.extend_seg_offset == value.bytes.len()) {
            let flags = read_bytes(value.bytes, value.offset + 1, 1)?[0];
            let count_type = (flags >> 2) & 3;
            return read_var(value.bytes, value.offset + 2 + var_size(count_type), (flags >> 4) & 3);
        }
        return plan_binary_value(value, &mut Vec::new(), false);
    }
    scalar_binary_size(value)
}

pub(crate) fn aggregate_payload(value: BinaryValue<'_>) -> BinaryResult<Vec<u8>> {
    let mut output = Vec::new();
    if value.json_type()?.is_container() && !value.lexicographical() && (value.extend_seg_offset == 0 || value.extend_seg_offset == value.bytes.len()) {
        let flags = read_bytes(value.bytes, value.offset + 1, 1)?[0];
        let count_type = (flags >> 2) & 3;
        let size_type = (flags >> 4) & 3;
        let length = read_var(value.bytes, value.offset + 2 + var_size(count_type), size_type)?;
        output.extend_from_slice(read_bytes(value.bytes, value.offset, to_usize(length, value.offset)?)?);
    } else { write_binary_value(&mut output, value)?; }
    Ok(output)
}

fn aggregate_var_type(value: u64) -> u8 {
    if value <= i8::MAX as u64 { 0 }
    else if value <= i16::MAX as u64 { 1 }
    else if value <= i32::MAX as u64 { 2 }
    else { 3 }
}

pub(crate) fn serialize_aggregate(object: bool, values: &[(&[u8], BinaryValue<'_>)]) -> BinaryResult<Vec<u8>> {
    let payloads = values.iter().map(|(_, value)| aggregate_payload(*value)).collect::<BinaryResult<Vec<_>>>()?;
    let key_length = values.iter().map(|(key, _)| key.len() as u64).sum::<u64>();
    let payload_length = payloads.iter().map(|value| value.len() as u64).sum::<u64>();
    let count = values.len() as u64;
    let count_type = aggregate_var_type(count);
    let base_length = key_length + payload_length;
    let mut size_type = aggregate_var_type(base_length);
    loop {
        let width = var_size(size_type) as u64;
        let estimate = base_length + (1 + width) * count + 2 * width * count
            + 2 + var_size(count_type) as u64 + width;
        let next = aggregate_var_type(estimate);
        if next <= size_type { break; }
        size_type = next;
    }
    let width = var_size(size_type);
    let header_size = 2 + var_size(count_type) + width;
    let value_entries = header_size + if object { values.len() * width * 2 } else { 0 };
    let key_start = value_entries + values.len() * (width + 1);
    let mut output = Vec::new();
    output.push(if object { OBJECT } else { ARRAY });
    output.push(size_type | (count_type << 2) | (size_type << 4) | 0x40);
    push_var(&mut output, count, count_type);
    let size_position = output.len();
    push_var(&mut output, 0, size_type);
    output.resize(key_start, 0);
    let mut key_offset = key_start;
    let mut value_offset = key_start + key_length as usize;
    for (index, ((key, value), payload)) in values.iter().zip(&payloads).enumerate() {
        if object {
            set_var(&mut output, header_size + index * width * 2, key_offset as u64, size_type);
            set_var(&mut output, header_size + index * width * 2 + width, key.len() as u64, size_type);
            output.extend_from_slice(key);
            key_offset += key.len();
        }
        set_var(&mut output, value_entries + index * (width + 1), value_offset as u64, size_type);
        output[value_entries + index * (width + 1) + width] = value.raw_type & !INLINE;
        value_offset += payload.len();
    }
    for payload in payloads { output.extend_from_slice(&payload); }
    let size = output.len() as u64;
    set_var(&mut output, size_position, size, size_type);
    Ok(output)
}

pub fn serialize_strings(values: &[String]) -> Vec<u8> {
    fn write(output: &mut Vec<u8>, values: &[String], estimated_size: u64) {
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
        for (index, value) in values.iter().enumerate() {
            let value_offset = (output.len() - start) as u64;
            let position = value_entries + index * (entry_size + 1);
            set_var(output, position, value_offset, entry_type);
            output[position + entry_size] = STRING;
            write_string(output, value);
        }
        let actual_size = (output.len() - start) as u64;
        if var_type(actual_size) > size_type {
            output.truncate(start);
            write(output, values, actual_size)
        } else {
            set_var(output, size_position, actual_size, size_type);
        }
    }

    if values.len() == 1 {
        let mut output = Vec::new();
        write_string(&mut output, &values[0]);
        output
    } else {
        let mut output = vec![0; 8];
        write(&mut output, values, 0);
        let header = DOC_HEADER as u64 | ((output.len() as u64) << 22);
        output[..8].copy_from_slice(&header.to_le_bytes());
        output
    }
}

pub fn serialize_object_keys(value: BinaryValue<'_>) -> BinaryResult<Vec<u8>> {
    fn write(
        output: &mut Vec<u8>,
        value: BinaryValue<'_>,
        estimated_size: u64,
    ) -> BinaryResult<()> {
        let start = output.len();
        let count = to_usize(value.len()?, value.offset)?;
        let count_type = var_type(count as u64);
        let size_type = var_type(estimated_size);
        let entry_type = size_type;
        let entry_size = var_size(entry_type);
        output.push(ARRAY);
        output.push(entry_type | (count_type << 2) | (size_type << 4) | 0x40);
        push_var(output, count as u64, count_type);
        let size_position = output.len();
        push_var(output, estimated_size, size_type);
        let value_entries = output.len();
        output.resize(output.len() + count * (entry_size + 1), 0);
        for index in 0..count {
            let key = value.key(index as u64)?;
            let value_offset = (output.len() - start) as u64;
            let position = value_entries + index * (entry_size + 1);
            set_var(output, position, value_offset, entry_type);
            output[position + entry_size] = STRING;
            output.push(STRING);
            push_vi64(output, key.len() as u64);
            output.extend_from_slice(key);
        }
        let actual_size = (output.len() - start) as u64;
        if var_type(actual_size) > size_type {
            output.truncate(start);
            write(output, value, actual_size)
        } else {
            set_var(output, size_position, actual_size, size_type);
            Ok(())
        }
    }

    if value.json_type()? != JsonType::Object {
        return Err(BinaryError::new(BinaryErrorKind::InvalidType, value.offset));
    }
    let mut output = vec![0; 8];
    write(&mut output, value, 0)?;
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

#[derive(Clone, Copy)]
pub(crate) struct ValueLocation {
    offset: usize,
    raw_type: u8,
    entry_type: u8,
}

impl<B: AsRef<[u8]>> BinaryDocument<B> {
    pub(crate) fn value_at(&self, location: ValueLocation) -> BinaryValue<'_> {
        BinaryValue {
            bytes: self.bytes.as_ref(),
            extend_seg_offset: self.extend_seg_offset,
            offset: location.offset,
            raw_type: location.raw_type,
            entry_type: location.entry_type,
        }
    }

    pub fn new(bytes: B) -> BinaryResult<Self> {
        let input = bytes.as_ref();
        let first = *input
            .first()
            .ok_or_else(|| BinaryError::new(BinaryErrorKind::Truncated, 0))?;
        let (extend_seg_offset, root_offset) = if first == DOC_HEADER {
            let header = u64::from_le_bytes(
                read_bytes(input, 0, 8)?
                    .try_into()
                    .expect("eight bytes were requested"),
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
                return Err(BinaryError::new(
                    BinaryErrorKind::InvalidDepth,
                    child.offset,
                ));
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

pub(crate) fn needs_type_prefix(value_type: u8) -> bool {
    matches!(
        value_type,
        STRING | OBJECT | ARRAY | 13 | 20 | 21 | 22 | 23 | 28 | 29
    )
}

pub(crate) fn read_bytes(bytes: &[u8], offset: usize, length: usize) -> BinaryResult<&[u8]> {
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

pub(crate) fn read_vi64(bytes: &[u8], offset: usize) -> BinaryResult<(u64, usize)> {
    let mut value = 0u64;
    for index in 0..10 {
        let byte = read_bytes(bytes, offset + index, 1)?[0];
        if index == 9 && byte > 1 {
            return Err(BinaryError::new(
                BinaryErrorKind::InvalidOffset,
                offset + index,
            ));
        }
        value |= u64::from(byte & 0x7f) << (index * 7);
        if byte & 0x80 == 0 {
            return Ok((value, offset + index + 1));
        }
    }
    Err(BinaryError::new(BinaryErrorKind::InvalidOffset, offset + 9))
}

pub(crate) fn var_type(value: u64) -> u8 {
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

pub(crate) fn signed_var_type(value: i64) -> u8 {
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

pub(crate) fn var_size(kind: u8) -> usize {
    1usize << kind
}

pub(crate) fn push_var(output: &mut Vec<u8>, value: u64, kind: u8) {
    let bytes = value.to_le_bytes();
    output.extend_from_slice(&bytes[..var_size(kind)]);
}

pub(crate) fn set_var(output: &mut [u8], position: usize, value: u64, kind: u8) {
    let size = var_size(kind);
    output[position..position + size].copy_from_slice(&value.to_le_bytes()[..size]);
}

fn vi64_len(value: u64) -> u64 {
    let bits = 64 - value.leading_zeros() as u64;
    bits.max(1).div_ceil(7).min(10)
}

pub(crate) fn push_vi64(output: &mut Vec<u8>, mut value: u64) {
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

fn signed_inline(value: i64, entry_type: u8) -> Option<(u64, u8)> {
    (signed_var_type(value) <= entry_type).then_some((value as u64, INT | INLINE))
}

fn inline_value(document: &Document, entry_type: u8) -> Option<(u64, u8)> {
    match document {
        Document::Null => Some((0, NULL | INLINE)),
        Document::Bool(value) => Some((*value as u64, BOOLEAN | INLINE)),
        Document::Number(Number::I64(value)) => signed_inline(*value, entry_type),
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

pub(crate) fn sort_objects(document: &mut Document) {
    let mut pending = vec![document];
    while let Some(document) = pending.pop() {
        match document {
            Document::Array(values) => pending.extend(values.iter_mut()),
            Document::Object(values) => {
                values.sort_by(|(left, _), (right, _)| left.len().cmp(&right.len())
                    .then_with(|| left.as_bytes().cmp(right.as_bytes())));
                pending.extend(values.iter_mut().map(|(_, value)| value));
            }
            _ => {}
        }
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
