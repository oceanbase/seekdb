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

use crate::checksum::{fold_u32, fold_u64};
use crate::{Error, Result};

pub(crate) const MICRO_BLOCK_MAGIC: u16 = 0x03ed;
pub(crate) const MICRO_BLOCK_VERSION: u16 = 3;
pub(crate) const FIXED_HEADER_SIZE: usize = 64;
pub(crate) const COLUMN_HEADER_SIZE: usize = 16;
pub(crate) const ENCODING_ROW_STORE: u8 = 1;
pub(crate) const SELECTIVE_ENCODING_ROW_STORE: u8 = 2;
pub(crate) const FLAT_ROW_STORE: u8 = 0;
pub(crate) const HAS_COLUMN_CHECKSUM: u16 = 1;
pub(crate) const ALL_LOB_IN_ROW: u16 = 1 << 2;
pub(crate) const CONTAINS_HASH_INDEX: u16 = 1 << 3;
pub(crate) const HASH_INDEX_OFFSET_SHIFT: u16 = 4;
pub(crate) const HASH_INDEX_OFFSET_MASK: u16 = 0x3ff;
pub(crate) const COLUMN_FIX_LENGTH: u8 = 1;
pub(crate) const COLUMN_HAS_EXTEND_VALUE: u8 = 2;
pub(crate) const COLUMN_BIT_PACKING: u8 = 4;
pub(crate) const COLUMN_LAST_VAR_FIELD: u8 = 8;

pub(crate) fn is_signed_store_type(obj_type: u8) -> bool {
    matches!(obj_type, 1..=5 | 17..=20 | 31..=34 | 41 | 42)
}

#[derive(Clone, Copy, Debug)]
pub(crate) struct Header {
    pub header_size: usize,
    pub column_count: usize,
    pub rowkey_column_count: usize,
    pub flags: u16,
    pub row_count: usize,
    pub row_store_type: u8,
    pub opt: u8,
    pub var_column_count: usize,
    pub row_data_offset: usize,
    pub original_length: i32,
    pub max_merged_trans_version: i64,
    pub data_length: usize,
    pub data_zlength: usize,
    pub data_checksum: u64,
}

impl Header {
    pub fn row_index_byte(self) -> usize {
        usize::from(self.opt & 0x7)
    }

    pub fn extend_value_bit(self) -> usize {
        usize::from((self.opt >> 3) & 0x7)
    }

    pub fn has_column_checksum(self) -> bool {
        self.flags & HAS_COLUMN_CHECKSUM != 0
    }
}

#[derive(Clone, Copy, Debug)]
pub(crate) struct ColumnHeader {
    pub kind: u8,
    pub attr: u8,
    pub obj_type: u8,
    pub extend_value_offset: usize,
    pub offset: usize,
    pub length: usize,
}

impl ColumnHeader {
    pub fn is_fixed(self) -> bool {
        self.attr & COLUMN_FIX_LENGTH != 0
    }

    pub fn has_extend_value(self) -> bool {
        self.attr & COLUMN_HAS_EXTEND_VALUE != 0
    }

    pub fn is_bit_packed(self) -> bool {
        self.attr & COLUMN_BIT_PACKING != 0
    }

    pub fn is_last_var_field(self) -> bool {
        self.attr & COLUMN_LAST_VAR_FIELD != 0
    }
}

pub(crate) fn u16_at(bytes: &[u8], offset: usize) -> Result<u16> {
    let raw = bytes
        .get(offset..offset.checked_add(2).ok_or(Error::SizeOverflow)?)
        .ok_or(Error::InvalidData)?;
    Ok(u16::from_le_bytes(
        raw.try_into().map_err(|_| Error::InvalidData)?,
    ))
}

pub(crate) fn i32_at(bytes: &[u8], offset: usize) -> Result<i32> {
    Ok(u32_at(bytes, offset)? as i32)
}

pub(crate) fn u32_at(bytes: &[u8], offset: usize) -> Result<u32> {
    let raw = bytes
        .get(offset..offset.checked_add(4).ok_or(Error::SizeOverflow)?)
        .ok_or(Error::InvalidData)?;
    Ok(u32::from_le_bytes(
        raw.try_into().map_err(|_| Error::InvalidData)?,
    ))
}

pub(crate) fn u64_at(bytes: &[u8], offset: usize) -> Result<u64> {
    let raw = bytes
        .get(offset..offset.checked_add(8).ok_or(Error::SizeOverflow)?)
        .ok_or(Error::InvalidData)?;
    Ok(u64::from_le_bytes(
        raw.try_into().map_err(|_| Error::InvalidData)?,
    ))
}

pub(crate) fn write_u16(bytes: &mut [u8], offset: usize, value: u16) -> Result<()> {
    let target = bytes
        .get_mut(offset..offset.checked_add(2).ok_or(Error::SizeOverflow)?)
        .ok_or(Error::OutputTooSmall)?;
    target.copy_from_slice(&value.to_le_bytes());
    Ok(())
}

pub(crate) fn write_u32(bytes: &mut [u8], offset: usize, value: u32) -> Result<()> {
    let target = bytes
        .get_mut(offset..offset.checked_add(4).ok_or(Error::SizeOverflow)?)
        .ok_or(Error::OutputTooSmall)?;
    target.copy_from_slice(&value.to_le_bytes());
    Ok(())
}

pub(crate) fn write_u64(bytes: &mut [u8], offset: usize, value: u64) -> Result<()> {
    let target = bytes
        .get_mut(offset..offset.checked_add(8).ok_or(Error::SizeOverflow)?)
        .ok_or(Error::OutputTooSmall)?;
    target.copy_from_slice(&value.to_le_bytes());
    Ok(())
}

fn decode_header(bytes: &[u8]) -> Result<Header> {
    if bytes.len() < FIXED_HEADER_SIZE
        || u16_at(bytes, 0)? != MICRO_BLOCK_MAGIC
        || u16_at(bytes, 2)? != MICRO_BLOCK_VERSION
    {
        return Err(Error::InvalidData);
    }
    let header = Header {
        header_size: u32_at(bytes, 4)? as usize,
        column_count: u16_at(bytes, 10)? as usize,
        rowkey_column_count: u16_at(bytes, 12)? as usize,
        flags: u16_at(bytes, 14)?,
        row_count: u32_at(bytes, 16)? as usize,
        row_store_type: *bytes.get(20).ok_or(Error::InvalidData)?,
        opt: *bytes.get(21).ok_or(Error::InvalidData)?,
        var_column_count: u16_at(bytes, 22)? as usize,
        row_data_offset: u32_at(bytes, 24)? as usize,
        original_length: i32_at(bytes, 28)?,
        max_merged_trans_version: u64_at(bytes, 32)? as i64,
        data_length: u32_at(bytes, 40)? as usize,
        data_zlength: u32_at(bytes, 44)? as usize,
        data_checksum: u64_at(bytes, 48)?,
    };
    Ok(header)
}

fn validate_common_header(bytes: &[u8], header: &Header) -> Result<()> {
    let expected_header = FIXED_HEADER_SIZE
        .checked_add(if header.has_column_checksum() {
            header
                .column_count
                .checked_mul(8)
                .ok_or(Error::SizeOverflow)?
        } else {
            0
        })
        .ok_or(Error::SizeOverflow)?;
    if header.header_size != expected_header
        || header.column_count < header.rowkey_column_count
        || header.row_count == 0
        || header.original_length < 0
        || header.header_size.checked_add(header.data_length) != Some(bytes.len())
    {
        return Err(Error::InvalidData);
    }
    Ok(())
}

pub(crate) fn parse_header(bytes: &[u8]) -> Result<Header> {
    let header = decode_header(bytes)?;
    validate_common_header(bytes, &header)?;
    if !matches!(
        header.row_store_type,
        ENCODING_ROW_STORE | SELECTIVE_ENCODING_ROW_STORE
    ) || header.row_data_offset < header.header_size
        || header.row_data_offset > bytes.len()
    {
        return Err(Error::InvalidData);
    }
    Ok(header)
}

pub(crate) fn parse_flat_header(bytes: &[u8]) -> Result<Header> {
    let header = decode_header(bytes)?;
    validate_common_header(bytes, &header)?;
    let encoded_hash_size = (header.flags >> HASH_INDEX_OFFSET_SHIFT) & HASH_INDEX_OFFSET_MASK;
    let has_hash_index = header.flags & CONTAINS_HASH_INDEX != 0;
    if (has_hash_index && encoded_hash_size == 0) || (!has_hash_index && encoded_hash_size != 0) {
        return Err(Error::InvalidData);
    }
    let hash_index_size = encoded_hash_size as usize;
    let index_length = header
        .row_count
        .checked_add(1)
        .and_then(|count| count.checked_mul(core::mem::size_of::<i32>()))
        .ok_or(Error::SizeOverflow)?;
    if header.row_store_type != FLAT_ROW_STORE
        || header.row_data_offset < header.header_size
        || header
            .row_data_offset
            .checked_add(index_length)
            .and_then(|offset| offset.checked_add(hash_index_size))
            .ok_or(Error::SizeOverflow)?
            != bytes.len()
    {
        return Err(Error::InvalidData);
    }
    Ok(header)
}

pub(crate) fn parse_column_header(bytes: &[u8], offset: usize) -> Result<ColumnHeader> {
    if bytes.get(offset).copied() != Some(0) {
        return Err(Error::InvalidData);
    }
    let header = ColumnHeader {
        kind: *bytes.get(offset + 1).ok_or(Error::InvalidData)?,
        attr: *bytes.get(offset + 2).ok_or(Error::InvalidData)?,
        obj_type: *bytes.get(offset + 3).ok_or(Error::InvalidData)?,
        extend_value_offset: u32_at(bytes, offset + 4)? as usize,
        offset: u32_at(bytes, offset + 8)? as usize,
        length: u32_at(bytes, offset + 12)? as usize,
    };
    if header.kind >= 10 {
        return Err(Error::Unsupported);
    }
    Ok(header)
}

pub(crate) fn header_checksum(bytes: &[u8], header: &Header) -> u16 {
    let mut checksum = 0u16;
    checksum ^= MICRO_BLOCK_MAGIC ^ MICRO_BLOCK_VERSION;
    checksum ^= u16::from(header.row_store_type) ^ u16::from(header.opt);
    for value in [
        header.column_count,
        header.rowkey_column_count,
        header.var_column_count,
    ] {
        fold_u32(&mut checksum, value as u32);
    }
    fold_u32(&mut checksum, u32::from(header.has_column_checksum()));
    fold_u64(&mut checksum, header.header_size as u64);
    fold_u64(&mut checksum, header.row_count as u64);
    fold_u64(&mut checksum, header.row_data_offset as u64);
    fold_u64(
        &mut checksum,
        header.original_length as u32 as i32 as i64 as u64,
    );
    fold_u64(&mut checksum, header.max_merged_trans_version as u64);
    fold_u64(&mut checksum, header.data_length as u64);
    fold_u64(&mut checksum, header.data_zlength as u64);
    fold_u64(&mut checksum, header.data_checksum);
    if header.has_column_checksum() {
        for column in 0..header.column_count {
            fold_u64(
                &mut checksum,
                u64_at(bytes, FIXED_HEADER_SIZE + column * 8).unwrap_or(0),
            );
        }
    }
    checksum
}

pub(crate) fn stored_header_checksum(bytes: &[u8]) -> Result<u16> {
    u16_at(bytes, 8)
}

pub(crate) fn checked_slice(bytes: &[u8], offset: usize, length: usize) -> Result<&[u8]> {
    let end = offset.checked_add(length).ok_or(Error::SizeOverflow)?;
    bytes.get(offset..end).ok_or(Error::InvalidData)
}

pub(crate) fn read_uint(bytes: &[u8], offset: usize, width: usize) -> Result<usize> {
    if width > 8 {
        return Err(Error::InvalidData);
    }
    let raw = checked_slice(bytes, offset, width)?;
    let mut value = [0u8; 8];
    value[..width].copy_from_slice(raw);
    usize::try_from(u64::from_le_bytes(value)).map_err(|_| Error::SizeOverflow)
}
