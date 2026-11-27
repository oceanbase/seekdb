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

use crate::checksum::{crc32c, fold_u32, fold_u64};
use crate::hash_index::Buckets;
use crate::{Error, Result};

pub(crate) const MICRO_BLOCK_MAGIC: u16 = 0x03ed;
pub(crate) const MICRO_BLOCK_VERSION: u16 = 3;
pub(crate) const FIXED_HEADER_SIZE: usize = 64;
pub(crate) const COLUMN_HEADER_SIZE: usize = 16;
pub(crate) const ENCODING_ROW_STORE: u8 = 1;
pub(crate) const SELECTIVE_ENCODING_ROW_STORE: u8 = 2;
pub(crate) const FLAT_ROW_STORE: u8 = 0;
pub(crate) const HAS_COLUMN_CHECKSUM: u16 = 1;
pub(crate) const HAS_STRING_OUT_ROW: u16 = 1 << 1;
pub(crate) const ALL_LOB_IN_ROW: u16 = 1 << 2;
pub(crate) const CONTAINS_HASH_INDEX: u16 = 1 << 3;
pub(crate) const HASH_INDEX_OFFSET_SHIFT: u16 = 4;
pub(crate) const HASH_INDEX_OFFSET_MASK: u16 = 0x3ff;
pub(crate) const HAS_MIN_MERGED_TRANS_VERSION: u16 = 1 << 14;
pub(crate) const SINGLE_VERSION_ROWS: u8 = 1;
pub(crate) const CONTAIN_UNCOMMITTED_ROWS: u8 = 1 << 1;
pub(crate) const IS_LAST_ROW_LAST_FLAG: u8 = 1 << 2;
pub(crate) const COLUMN_FIX_LENGTH: u8 = 1;
pub(crate) const COLUMN_HAS_EXTEND_VALUE: u8 = 2;
pub(crate) const COLUMN_BIT_PACKING: u8 = 4;
pub(crate) const COLUMN_LAST_VAR_FIELD: u8 = 8;

pub(crate) fn is_signed_store_type(obj_type: u8) -> bool {
    matches!(obj_type, 1..=5 | 17..=20 | 31..=34 | 41 | 42)
}

pub(crate) fn integer_store_size(obj_type: u8) -> Option<usize> {
    match obj_type {
        1 | 6 | 21 => Some(1),
        2 | 7 => Some(2),
        3 | 4 | 8 | 9 | 11 | 13 | 19 | 41 => Some(4),
        5 | 10 | 12 | 14 | 17 | 18 | 20 | 31..=34 | 42 => Some(8),
        _ => None,
    }
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

    pub fn has_min_merged_trans_version(self) -> bool {
        self.flags & HAS_MIN_MERGED_TRANS_VERSION != 0
    }

    pub fn min_merged_trans_version(self, bytes: &[u8]) -> Result<i64> {
        if self.has_min_merged_trans_version() && !self.has_column_checksum() {
            Ok(u64_at(bytes, 56)? as i64)
        } else {
            Ok(0)
        }
    }

    pub fn column_checksum_count(self) -> usize {
        if self.has_column_checksum() {
            self.column_count
        } else {
            0
        }
    }

    pub fn column_checksum(self, bytes: &[u8], column: usize) -> Result<u64> {
        if column >= self.column_checksum_count() {
            return Err(Error::InvalidArgument);
        }
        u64_at(bytes, FIXED_HEADER_SIZE + column * 8)
    }

    pub fn hash_index_size(self) -> u16 {
        (self.flags >> HASH_INDEX_OFFSET_SHIFT) & HASH_INDEX_OFFSET_MASK
    }

    pub fn flat_hash_trailer(self, bytes: &[u8]) -> Result<&[u8]> {
        let start = bytes
            .len()
            .checked_sub(self.hash_index_size().into())
            .ok_or(Error::InvalidData)?;
        Ok(&bytes[start..])
    }

    pub fn validate_flat_hash_trailer(self, bytes: &[u8]) -> Result<()> {
        let trailer = self.flat_hash_trailer(bytes)?;
        if !trailer.is_empty() {
            Buckets::parse(trailer)?.validate_rows(self.row_count)?;
        }
        Ok(())
    }

    pub fn check_header_checksum(self, bytes: &[u8]) -> Result<()> {
        if stored_header_checksum(bytes)? != header_checksum(bytes, &self) {
            return Err(Error::ChecksumMismatch);
        }
        Ok(())
    }

    pub fn check_expanded_record(self, bytes: &[u8]) -> Result<()> {
        self.check_header_checksum(bytes)?;
        if self.data_length == self.data_zlength {
            check_record_payload(
                checked_slice(bytes, self.header_size, self.data_length)?,
                self.data_length,
                self.data_zlength,
                self.data_checksum,
            )?;
        }
        Ok(())
    }

    pub fn with_stored_payload(mut self, payload: &[u8]) -> Result<Self> {
        if payload.len() > i32::MAX as usize {
            return Err(Error::SizeOverflow);
        }
        if self.data_length != 0 && payload.is_empty() {
            return Err(Error::InvalidArgument);
        }
        self.data_zlength = payload.len();
        self.data_checksum = crc32c(0, payload);
        Ok(self)
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

pub(crate) fn record_header_size(column_count: usize, has_column_checksum: bool) -> Result<usize> {
    let checksum_size = if has_column_checksum {
        column_count.checked_mul(8).ok_or(Error::SizeOverflow)?
    } else {
        0
    };
    FIXED_HEADER_SIZE
        .checked_add(checksum_size)
        .ok_or(Error::SizeOverflow)
}

pub(crate) fn read_record_header(bytes: &[u8]) -> Result<Header> {
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
    validate_record_header(&header)?;
    if bytes.len() < header.header_size {
        return Err(Error::InvalidData);
    }
    Ok(header)
}

pub(crate) fn check_record_payload(
    payload: &[u8],
    data_length: usize,
    data_zlength: usize,
    data_checksum: u64,
) -> Result<()> {
    if payload.len() != data_zlength
        || (payload.is_empty() && (data_length != 0 || data_checksum != 0))
    {
        return Err(Error::InvalidData);
    }
    if crc32c(0, payload) != data_checksum {
        return Err(Error::ChecksumMismatch);
    }
    Ok(())
}

pub(crate) fn check_stored_record(bytes: &[u8]) -> Result<Header> {
    check_stored_record_with_trailing_bytes(bytes, false)
}

pub(crate) fn check_stored_record_prefix(bytes: &[u8]) -> Result<Header> {
    check_stored_record_with_trailing_bytes(bytes, true)
}

fn check_stored_record_with_trailing_bytes(bytes: &[u8], allow_trailing: bool) -> Result<Header> {
    let header = read_record_header(bytes)?;
    header.check_header_checksum(bytes)?;
    let payload = &bytes[header.header_size..];
    let payload = if allow_trailing {
        payload
            .get(..header.data_zlength)
            .ok_or(Error::InvalidData)?
    } else {
        payload
    };
    check_record_payload(
        payload,
        header.data_length,
        header.data_zlength,
        header.data_checksum,
    )?;
    if header.row_store_type == FLAT_ROW_STORE && header.data_length == header.data_zlength {
        let record = &bytes[..header.header_size + payload.len()];
        validate_flat_header(record, header)?.validate_flat_hash_trailer(record)?;
    }
    Ok(header)
}

pub(crate) fn write_record_header(
    out: &mut [u8],
    header: &Header,
    min_merged_trans_version: i64,
    column_checksums: impl ExactSizeIterator<Item = Result<u64>>,
) -> Result<u16> {
    validate_record_header(header)?;
    if column_checksums.len() != header.column_checksum_count() {
        return Err(Error::InvalidArgument);
    }
    let out = out
        .get_mut(..header.header_size)
        .ok_or(Error::OutputTooSmall)?;
    for (column, checksum) in column_checksums.enumerate() {
        write_u64(out, FIXED_HEADER_SIZE + column * 8, checksum?)?;
    }
    encode_fixed_header(out, header, min_merged_trans_version)
}

pub(crate) fn rewrite_record_column_checksums(
    out: &mut [u8],
    source_prefix: &[u8],
    column_count: u16,
    has_column_checksum: bool,
    checksums: &[u64],
) -> Result<()> {
    let mut header = read_record_header(source_prefix)?;
    if usize::from(column_count) != header.column_count {
        return Err(Error::InvalidArgument);
    }
    let min_version = header.min_merged_trans_version(source_prefix)?;
    let next_header_size = record_header_size(column_count.into(), has_column_checksum)?;
    // The row offset is relative to the beginning of the entire block.
    header.row_data_offset = header
        .row_data_offset
        .checked_sub(header.header_size)
        .and_then(|offset| offset.checked_add(next_header_size))
        .ok_or(Error::InvalidData)?;
    header.header_size = next_header_size;
    if has_column_checksum {
        header.flags |= HAS_COLUMN_CHECKSUM;
    } else {
        header.flags &= !HAS_COLUMN_CHECKSUM;
    }
    write_record_header(out, &header, min_version, checksums.iter().copied().map(Ok))?;
    Ok(())
}

pub(crate) fn accumulate_record_column_checksums(prefix: &[u8], sums: &mut [i64]) -> Result<()> {
    let header = read_record_header(prefix)?;
    if !header.has_column_checksum() || sums.len() < header.column_checksum_count() {
        return Err(Error::InvalidArgument);
    }
    for (column, sum) in sums[..header.column_checksum_count()]
        .iter_mut()
        .enumerate()
    {
        *sum = sum.wrapping_add(header.column_checksum(prefix, column)? as i64);
    }
    Ok(())
}

pub(crate) fn reseal_record_header_in_place(
    out: &mut [u8],
    stored_payload: &[u8],
) -> Result<Header> {
    let header = read_record_header(out)?.with_stored_payload(stored_payload)?;
    let min_version = header.min_merged_trans_version(out)?;
    encode_fixed_header(out, &header, min_version)?;
    Ok(header)
}

fn encode_fixed_header(
    out: &mut [u8],
    header: &Header,
    min_merged_trans_version: i64,
) -> Result<u16> {
    write_u16(out, 0, MICRO_BLOCK_MAGIC)?;
    write_u16(out, 2, MICRO_BLOCK_VERSION)?;
    write_u32(
        out,
        4,
        u32::try_from(header.header_size).map_err(|_| Error::SizeOverflow)?,
    )?;
    write_u16(
        out,
        10,
        u16::try_from(header.column_count).map_err(|_| Error::SizeOverflow)?,
    )?;
    write_u16(
        out,
        12,
        u16::try_from(header.rowkey_column_count).map_err(|_| Error::SizeOverflow)?,
    )?;
    write_u16(out, 14, header.flags)?;
    write_u32(
        out,
        16,
        u32::try_from(header.row_count).map_err(|_| Error::SizeOverflow)?,
    )?;
    out[20] = header.row_store_type;
    out[21] = header.opt;
    write_u16(
        out,
        22,
        u16::try_from(header.var_column_count).map_err(|_| Error::SizeOverflow)?,
    )?;
    write_u32(
        out,
        24,
        u32::try_from(header.row_data_offset).map_err(|_| Error::SizeOverflow)?,
    )?;
    write_u32(out, 28, header.original_length as u32)?;
    write_u64(out, 32, header.max_merged_trans_version as u64)?;
    write_u32(out, 40, header.data_length as u32)?;
    write_u32(out, 44, header.data_zlength as u32)?;
    write_u64(out, 48, header.data_checksum)?;
    let transaction_or_reserved =
        if header.has_min_merged_trans_version() && !header.has_column_checksum() {
            min_merged_trans_version as u64
        } else {
            0
        };
    write_u64(out, 56, transaction_or_reserved)?;
    let checksum = header_checksum(out, header);
    write_u16(out, 8, checksum)?;
    Ok(checksum)
}

fn validate_record_header(header: &Header) -> Result<()> {
    if header.header_size != record_header_size(header.column_count, header.has_column_checksum())?
        || header.column_count < header.rowkey_column_count
        || header.row_store_type > SELECTIVE_ENCODING_ROW_STORE
        || header.data_length > i32::MAX as usize
        || header.data_zlength > i32::MAX as usize
    {
        return Err(Error::InvalidData);
    }
    Ok(())
}

fn validate_expanded_header(bytes: &[u8], header: &Header) -> Result<()> {
    if header.row_count == 0
        || header.original_length < 0
        || header.header_size.checked_add(header.data_length) != Some(bytes.len())
    {
        return Err(Error::InvalidData);
    }
    Ok(())
}

pub(crate) fn parse_header(bytes: &[u8]) -> Result<Header> {
    validate_pax_header(bytes, read_record_header(bytes)?)
}

fn validate_pax_header(bytes: &[u8], header: Header) -> Result<Header> {
    validate_expanded_header(bytes, &header)?;
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
    validate_flat_header(bytes, read_record_header(bytes)?)
}

pub(crate) fn read_expanded_record_header(bytes: &[u8]) -> Result<Header> {
    let header = read_record_header(bytes)?;
    if header.row_store_type == FLAT_ROW_STORE {
        validate_flat_header(bytes, header)?.validate_flat_hash_trailer(bytes)?;
        Ok(header)
    } else {
        validate_pax_header(bytes, header)
    }
}

fn validate_flat_header(bytes: &[u8], header: Header) -> Result<Header> {
    validate_expanded_header(bytes, &header)?;
    let encoded_hash_size = header.hash_index_size();
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
    for column in 0..header.column_checksum_count() {
        fold_u64(
            &mut checksum,
            header.column_checksum(bytes, column).unwrap_or(0),
        );
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

#[cfg(test)]
mod tests {
    use super::*;

    const CHECKSUM: u64 = 0x0123_4567_89ab_cdef;
    const PREFIX: [u8; 72] = [
        0xed, 0x03, 0x03, 0x00, 0x48, 0x00, 0x00, 0x00, 0x92, 0xed, 0x01, 0x00, 0x01, 0x00, 0x05,
        0x00, 0x03, 0x00, 0x00, 0x00, 0x02, 0x02, 0x00, 0x00, 0x50, 0x00, 0x00, 0x00, 0x7b, 0x00,
        0x00, 0x00, 0x9a, 0x78, 0x56, 0x34, 0x12, 0x00, 0x00, 0x00, 0x09, 0x00, 0x00, 0x00, 0x09,
        0x00, 0x00, 0x00, 0x20, 0xfa, 0xe3, 0x58, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
        0x00, 0x00, 0x00, 0x00, 0xef, 0xcd, 0xab, 0x89, 0x67, 0x45, 0x23, 0x01,
    ];
    const PAYLOAD: &[u8] = b"123456789";

    #[test]
    fn record_header_preserves_fixed_and_column_checksum_bytes() {
        let header = read_record_header(&PREFIX).unwrap();
        assert_eq!(header.row_store_type, SELECTIVE_ENCODING_ROW_STORE);
        assert_eq!(header.row_index_byte(), 2);
        assert_eq!(header.original_length, 123);
        assert_eq!(header.max_merged_trans_version, 0x12_3456_789a);
        assert_eq!(header.column_checksum(&PREFIX, 0), Ok(CHECKSUM));
        assert_eq!(
            header.column_checksum(&PREFIX, 1),
            Err(Error::InvalidArgument)
        );

        let mut output = [0xff; 72];
        let sealed = header.with_stored_payload(PAYLOAD).unwrap();
        let checksum = write_record_header(&mut output, &sealed, 0, [Ok(CHECKSUM)].into_iter());
        assert_eq!(checksum, Ok(0xed92));
        assert_eq!(output, PREFIX);
        let record = [output.as_slice(), PAYLOAD].concat();
        assert!(check_stored_record(&record).is_ok());
    }

    #[test]
    fn a_stored_record_uses_the_compressed_length_and_checksum() {
        let mut prefix = PREFIX;
        let stored = &PAYLOAD[..5];
        reseal_record_header_in_place(&mut prefix, stored).unwrap();
        let mut record = [prefix.as_slice(), stored].concat();
        let header = check_stored_record(&record).unwrap();
        assert_eq!(header.data_length, 9);
        assert_eq!(header.data_zlength, 5);
        assert_eq!(header.column_checksum(&record, 0), Ok(CHECKSUM));
        assert_eq!(
            read_expanded_record_header(&record).unwrap_err(),
            Error::InvalidData
        );
        assert!(read_expanded_record_header(&[prefix.as_slice(), PAYLOAD].concat()).is_ok());

        record.push(0);
        assert_eq!(
            check_stored_record(&record).unwrap_err(),
            Error::InvalidData
        );
        assert!(check_stored_record_prefix(&record).is_ok());
        assert_eq!(
            check_stored_record_prefix(&record[..76]).unwrap_err(),
            Error::InvalidData
        );
        record.pop();
        record[76] ^= 0x80;
        assert_eq!(
            check_stored_record(&record).unwrap_err(),
            Error::ChecksumMismatch
        );
        record[76] ^= 0x80;
        record[26] ^= 1;
        assert_eq!(
            check_stored_record(&record).unwrap_err(),
            Error::ChecksumMismatch
        );
        assert_eq!(
            read_record_header(&record[..71]).unwrap_err(),
            Error::InvalidData
        );
    }

    #[test]
    fn minor_header_tracks_minimum_merged_transaction_version() {
        let mut header = read_record_header(&PREFIX).unwrap();
        header.header_size = record_header_size(1, false).unwrap();
        header.flags = HAS_MIN_MERGED_TRANS_VERSION;
        let mut output = [0xff; FIXED_HEADER_SIZE];
        write_record_header(&mut output, &header, -71, [].into_iter()).unwrap();
        let header = read_record_header(&output).unwrap();
        assert_eq!(header.min_merged_trans_version(&output), Ok(-71));
        assert!(header.check_header_checksum(&output).is_ok());
    }

    #[test]
    fn rewriting_column_checksums_rebases_the_absolute_row_offset() {
        let mut minor = [0; FIXED_HEADER_SIZE];
        assert_eq!(
            rewrite_record_column_checksums(&mut minor, &PREFIX, 2, false, &[]),
            Err(Error::InvalidArgument)
        );
        rewrite_record_column_checksums(&mut minor, &PREFIX, 1, false, &[]).unwrap();
        let header = read_record_header(&minor).unwrap();
        assert_eq!(header.row_data_offset, 72);
        assert_eq!(header.data_length, 9);
        assert_eq!(header.data_checksum, 0x58e3_fa20);
        assert!(header.check_header_checksum(&minor).is_ok());

        let mut major = [0; 72];
        rewrite_record_column_checksums(&mut major, &minor, 1, true, &[CHECKSUM]).unwrap();
        assert_eq!(major, PREFIX);

        let mut sums = [i64::MAX, 7];
        accumulate_record_column_checksums(&major, &mut sums).unwrap();
        assert_eq!(sums, [i64::MAX.wrapping_add(CHECKSUM as i64), 7]);
    }

    #[test]
    fn record_ingress_rejects_flat_hash_bucket_outside_row_count() {
        let mut header = read_record_header(&PREFIX).unwrap();
        header.header_size = FIXED_HEADER_SIZE;
        header.row_count = 1;
        header.row_store_type = FLAT_ROW_STORE;
        header.row_data_offset = FIXED_HEADER_SIZE + 1;
        header.flags = CONTAINS_HASH_INDEX | (4 << HASH_INDEX_OFFSET_SHIFT);

        let mut payload = [0; 13];
        payload[5] = 1;
        payload[10] = 1;
        header.data_length = payload.len();
        let mut prefix = [0; FIXED_HEADER_SIZE];
        let header = header.with_stored_payload(&payload).unwrap();
        write_record_header(&mut prefix, &header, 0, [].into_iter()).unwrap();
        assert!(check_stored_record(&[prefix.as_slice(), &payload].concat()).is_ok());

        payload[12] = 1;
        reseal_record_header_in_place(&mut prefix, &payload).unwrap();
        let corrupt = [prefix.as_slice(), &payload].concat();
        assert_eq!(
            check_stored_record(&corrupt).unwrap_err(),
            Error::InvalidData
        );
        assert_eq!(
            read_expanded_record_header(&corrupt).unwrap_err(),
            Error::InvalidData
        );
    }
}
