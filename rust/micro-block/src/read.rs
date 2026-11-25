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

use crate::checksum::crc32c;
use crate::format::{
    checked_slice, header_checksum, integer_store_size, is_signed_store_type, parse_column_header,
    parse_header, read_uint, stored_header_checksum, u16_at, u32_at, ColumnHeader, Header,
    COLUMN_HEADER_SIZE,
};
use crate::{Error, Result};

const RAW: u8 = 0;
const DICT: u8 = 1;
const RLE: u8 = 2;
const CONST: u8 = 3;
const INTEGER_BASE_DIFF: u8 = 4;
const STRING_DIFF: u8 = 5;
const HEX_PACKING: u8 = 6;
const STRING_PREFIX: u8 = 7;
const COLUMN_EQUAL: u8 = 8;
const COLUMN_SUBSTR: u8 = 9;
const CONST_META_HEADER_SIZE: usize = 6;
const DICT_META_HEADER_SIZE: usize = 9;
const RLE_META_HEADER_SIZE: usize = 10;
const STRING_DIFF_META_HEADER_SIZE: usize = 13;
const DICT_FIX_LENGTH: u8 = 1;
const DICT_SORTED: u8 = 2;
const HEX_META_HEADER_SIZE: usize = 13;
const STRING_PREFIX_META_HEADER_SIZE: usize = 15;
const COLUMN_EQUAL_META_HEADER_SIZE: usize = 3;
const COLUMN_SUBSTR_META_HEADER_SIZE: usize = 8;
const EXCEPTION_META_HEADER_SIZE: usize = 4;

/// A decoded cell. Integer bit-packing is represented as an immediate value;
/// every other value borrows the pinned input block without copying it.
#[derive(Clone, Copy, Debug, Eq, PartialEq)]
pub enum Cell<'a> {
    Null,
    Nop,
    Bytes(&'a [u8]),
    Outrow(&'a [u8]),
    UInt(u64),
}

impl<'a> Cell<'a> {
    pub fn as_u64(self) -> Result<u64> {
        match self {
            Self::UInt(value) => Ok(value),
            Self::Bytes(bytes) if bytes.len() <= 8 => {
                let mut value = [0u8; 8];
                value[..bytes.len()].copy_from_slice(bytes);
                Ok(u64::from_le_bytes(value))
            }
            Self::Null | Self::Nop | Self::Bytes(_) | Self::Outrow(_) => {
                Err(Error::InvalidArgument)
            }
        }
    }

    pub fn as_i64(self) -> Result<i64> {
        match self {
            Self::UInt(value) => Ok(value as i64),
            Self::Bytes(bytes) if matches!(bytes.len(), 1 | 2 | 4 | 8) => {
                let negative = bytes[bytes.len() - 1] & 0x80 != 0;
                let mut value = if negative { [0xff; 8] } else { [0; 8] };
                value[..bytes.len()].copy_from_slice(bytes);
                Ok(i64::from_le_bytes(value))
            }
            Self::Null | Self::Nop | Self::Bytes(_) | Self::Outrow(_) => Err(Error::InvalidData),
        }
    }

    pub fn bytes(self) -> Result<&'a [u8]> {
        match self {
            Self::Bytes(bytes) => Ok(bytes),
            Self::Null | Self::Nop | Self::Outrow(_) | Self::UInt(_) => Err(Error::InvalidArgument),
        }
    }
}

#[derive(Clone, Copy, Debug, Eq, PartialEq)]
pub struct RowRange {
    pub start: usize,
    pub end: usize,
}

#[derive(Clone, Copy, Debug, Eq, PartialEq)]
pub struct RowkeyBound {
    pub row: usize,
    pub equal: bool,
}

impl RowRange {
    pub fn new(start: usize, end: usize) -> Result<Self> {
        if start > end {
            return Err(Error::InvalidArgument);
        }
        Ok(Self { start, end })
    }
}

/// Owned batch descriptors. Borrowed cells require the C++ cache pin to stay
/// alive; decoded cells borrow contiguous storage owned by this projection.
#[derive(Debug)]
pub struct Projection<'a> {
    row_count: usize,
    column_count: usize,
    cells: Vec<ProjectionCell<'a>>,
    decoded: Vec<u8>,
}

#[derive(Clone, Copy, Debug)]
pub(crate) enum ProjectionCell<'a> {
    Borrowed(Cell<'a>),
    Decoded { offset: usize, length: usize },
}

impl<'a> Projection<'a> {
    pub(crate) fn new(row_count: usize, column_count: usize, cells: Vec<Cell<'a>>) -> Self {
        Self {
            row_count,
            column_count,
            cells: cells.into_iter().map(ProjectionCell::Borrowed).collect(),
            decoded: Vec::new(),
        }
    }

    pub fn row_count(&self) -> usize {
        self.row_count
    }

    pub fn column_count(&self) -> usize {
        self.column_count
    }

    pub fn get(&self, row: usize, column: usize) -> Result<Cell<'_>> {
        if row >= self.row_count || column >= self.column_count {
            return Err(Error::InvalidArgument);
        }
        match self
            .cells
            .get(row * self.column_count + column)
            .ok_or(Error::InvalidData)?
        {
            ProjectionCell::Borrowed(cell) => Ok(*cell),
            ProjectionCell::Decoded { offset, length } => {
                Ok(Cell::Bytes(checked_slice(&self.decoded, *offset, *length)?))
            }
        }
    }

    pub(crate) fn into_parts(self) -> (Vec<ProjectionCell<'a>>, Vec<u8>) {
        (self.cells, self.decoded)
    }
}

#[derive(Clone, Copy)]
struct RowRef<'a> {
    bytes: &'a [u8],
}

/// A validated, uncompressed PAX micro-block borrowing its input storage.
pub struct Block<'a> {
    bytes: &'a [u8],
    pub(crate) header: Header,
    columns: Vec<ColumnHeader>,
    meta: &'a [u8],
    row_data: &'a [u8],
    row_content_len: usize,
    row_index_offset: usize,
}

#[derive(Clone, Copy)]
struct StringDiffMeta<'a> {
    descriptors: &'a [u8],
    alphabet: &'a [u8],
    common: &'a [u8],
    data_offset: usize,
    data_length: usize,
    string_size: usize,
    diff_size: usize,
}

#[derive(Clone, Copy)]
struct StringPrefixMeta<'a> {
    indexes: &'a [u8],
    prefixes: &'a [u8],
    alphabet: &'a [u8],
    index_width: usize,
    prefix_count: usize,
    data_offset: usize,
    data_length: usize,
    max_string_size: usize,
}

#[derive(Clone, Copy)]
struct ColumnSubstrMeta<'a> {
    exceptions: Option<ExceptionMeta<'a>>,
    ref_col_idx: usize,
    same_start: Option<usize>,
    fixed_length: Option<usize>,
    start_width: usize,
    length_width: usize,
}

#[derive(Clone, Copy)]
struct ExceptionMeta<'a> {
    bytes: &'a [u8],
    count: usize,
    ext_offset: usize,
    index_offset: usize,
    data_offset: usize,
    value_width: usize,
    bit_packed: bool,
}

impl<'a> StringDiffMeta<'a> {
    fn packed_width(self) -> usize {
        if self.alphabet.is_empty() {
            self.diff_size
        } else {
            self.diff_size.div_ceil(2)
        }
    }
}

impl<'a> Block<'a> {
    pub fn new(bytes: &'a [u8]) -> Result<Self> {
        let header = parse_header(bytes)?;
        if stored_header_checksum(bytes)? != header_checksum(bytes, &header) {
            return Err(Error::ChecksumMismatch);
        }
        let payload = checked_slice(bytes, header.header_size, header.data_length)?;
        if header.data_zlength == header.data_length && crc32c(0, payload) != header.data_checksum {
            return Err(Error::ChecksumMismatch);
        }
        let column_headers_len = header
            .column_count
            .checked_mul(COLUMN_HEADER_SIZE)
            .ok_or(Error::SizeOverflow)?;
        let meta_offset = header
            .header_size
            .checked_add(column_headers_len)
            .ok_or(Error::SizeOverflow)?;
        if meta_offset > header.row_data_offset {
            return Err(Error::InvalidData);
        }
        let mut columns = Vec::new();
        columns
            .try_reserve_exact(header.column_count)
            .map_err(|_| Error::SizeOverflow)?;
        for index in 0..header.column_count {
            let offset = header
                .header_size
                .checked_add(index * COLUMN_HEADER_SIZE)
                .ok_or(Error::SizeOverflow)?;
            columns.push(parse_column_header(bytes, offset)?);
        }
        let meta = checked_slice(bytes, meta_offset, header.row_data_offset - meta_offset)?;
        let row_data = checked_slice(
            bytes,
            header.row_data_offset,
            bytes.len() - header.row_data_offset,
        )?;
        let index_width = header.row_index_byte();
        let (row_content_len, row_index_offset) = if index_width == 0 {
            if header.var_column_count != 0 || row_data.len() % header.row_count != 0 {
                return Err(Error::InvalidData);
            }
            (row_data.len(), row_data.len())
        } else {
            if !matches!(index_width, 1 | 2 | 4) || header.var_column_count == 0 {
                return Err(Error::InvalidData);
            }
            let index_len = header
                .row_count
                .checked_add(1)
                .and_then(|count| count.checked_mul(index_width))
                .ok_or(Error::SizeOverflow)?;
            if index_len > row_data.len() {
                return Err(Error::InvalidData);
            }
            let index_offset = row_data.len() - index_len;
            let mut previous = 0usize;
            for row in 0..=header.row_count {
                let offset = read_uint(row_data, index_offset + row * index_width, index_width)?;
                if (row == 0 && offset != 0) || offset < previous || offset > index_offset {
                    return Err(Error::InvalidData);
                }
                previous = offset;
            }
            if previous != index_offset {
                return Err(Error::InvalidData);
            }
            (index_offset, index_offset)
        };
        let block = Self {
            bytes,
            header,
            columns,
            meta,
            row_data,
            row_content_len,
            row_index_offset,
        };
        block.validate_columns()?;
        Ok(block)
    }

    pub fn row_count(&self) -> usize {
        self.header.row_count
    }

    pub fn column_count(&self) -> usize {
        self.header.column_count
    }

    pub fn rowkey_column_count(&self) -> usize {
        self.header.rowkey_column_count
    }

    pub fn copied_input_bytes(&self) -> usize {
        let _ = self.bytes;
        0
    }

    fn validate_columns(&self) -> Result<()> {
        if self.header.var_column_count > self.header.column_count {
            return Err(Error::InvalidData);
        }
        for column in &self.columns {
            match column.kind {
                RAW => self.validate_raw_column(column)?,
                DICT => self.validate_dict_column(column)?,
                RLE => self.validate_rle_column(column)?,
                CONST => {
                    let meta = self.column_meta(column)?;
                    if meta.len() < CONST_META_HEADER_SIZE || meta[0] != 0 {
                        return Err(Error::InvalidData);
                    }
                    if meta[2] > 2 {
                        return Err(Error::InvalidData);
                    }
                    let count = meta[1] as usize;
                    let offset = u16_at(meta, 4)? as usize;
                    if count == 0 {
                        if meta[3] != 0 || offset != CONST_META_HEADER_SIZE {
                            return Err(Error::InvalidData);
                        }
                    } else {
                        let row_id_width = (meta[3] & 7) as usize;
                        if count > 32
                            || meta[3] & !7 != 0
                            || !valid_integer_width(row_id_width, false)
                            || offset != CONST_META_HEADER_SIZE + count * (row_id_width + 1)
                            || offset >= meta.len()
                        {
                            return Err(Error::InvalidData);
                        }
                        let row_ids_offset = CONST_META_HEADER_SIZE + count;
                        let mut previous = 0usize;
                        for index in 0..count {
                            let row_id = read_uint(
                                meta,
                                row_ids_offset + index * row_id_width,
                                row_id_width,
                            )?;
                            if row_id >= self.header.row_count || (index > 0 && row_id <= previous)
                            {
                                return Err(Error::InvalidData);
                            }
                            previous = row_id;
                        }
                        validate_dict_meta(&meta[offset..], false, true)?;
                    }
                }
                INTEGER_BASE_DIFF => self.validate_integer_diff_column(column)?,
                STRING_DIFF => self.validate_string_diff_column(column)?,
                HEX_PACKING => self.validate_hex_column(column)?,
                STRING_PREFIX => self.validate_string_prefix_column(column)?,
                COLUMN_EQUAL => self.validate_column_equal_column(column)?,
                COLUMN_SUBSTR => self.validate_column_substr_column(column)?,
                _ => return Err(Error::Unsupported),
            }
        }
        self.validate_column_references()?;
        Ok(())
    }

    fn string_prefix_meta(&self, column: &ColumnHeader) -> Result<StringPrefixMeta<'a>> {
        let meta = self.column_meta(column)?;
        if meta.len() < STRING_PREFIX_META_HEADER_SIZE || meta[0] != 0 {
            return Err(Error::InvalidData);
        }
        let prefix_count = meta[1] as usize;
        let index_width = (meta[14] & 3) as usize;
        let alphabet_size = ((meta[14] >> 2) & 0x1f) as usize;
        if prefix_count == 0
            || prefix_count > 16
            || !matches!(index_width, 1 | 2)
            || alphabet_size > 16
            || meta[14] & 0x80 != 0
        {
            return Err(Error::InvalidData);
        }
        let alphabet_start = STRING_PREFIX_META_HEADER_SIZE;
        let indexes_start = alphabet_start + alphabet_size;
        let indexes_size = (prefix_count - 1)
            .checked_mul(index_width)
            .ok_or(Error::SizeOverflow)?;
        let prefixes_start = indexes_start
            .checked_add(indexes_size)
            .ok_or(Error::SizeOverflow)?;
        if prefixes_start > meta.len() {
            return Err(Error::InvalidData);
        }
        let indexes = checked_slice(meta, indexes_start, indexes_size)?;
        let prefixes = &meta[prefixes_start..];
        let mut previous = 0usize;
        for index in 0..prefix_count.saturating_sub(1) {
            let offset = read_uint(indexes, index * index_width, index_width)?;
            if offset < previous || offset > prefixes.len() {
                return Err(Error::InvalidData);
            }
            previous = offset;
        }
        let alphabet = checked_slice(meta, alphabet_start, alphabet_size)?;
        let mut seen = [false; 256];
        for &byte in alphabet {
            if seen[byte as usize] {
                return Err(Error::InvalidData);
            }
            seen[byte as usize] = true;
        }
        Ok(StringPrefixMeta {
            indexes,
            prefixes,
            alphabet,
            index_width,
            prefix_count,
            data_offset: u32_at(meta, 2)? as usize,
            data_length: u32_at(meta, 6)? as usize,
            max_string_size: u32_at(meta, 10)? as usize,
        })
    }

    fn validate_string_prefix_column(&self, column: &ColumnHeader) -> Result<()> {
        if column.is_fixed() || column.is_bit_packed() {
            return Err(Error::InvalidData);
        }
        let meta = self.string_prefix_meta(column)?;
        if meta.prefixes.is_empty() || meta.max_string_size > self.header.original_length as usize {
            return Err(Error::InvalidData);
        }
        let raw = ColumnHeader {
            kind: RAW,
            attr: column.attr,
            obj_type: column.obj_type,
            extend_value_offset: column.extend_value_offset,
            offset: meta.data_offset,
            length: meta.data_length,
        };
        self.validate_raw_column(&raw)?;
        if column.has_extend_value() && !matches!(self.header.extend_value_bit(), 1 | 2) {
            return Err(Error::InvalidData);
        }
        Ok(())
    }

    fn column_substr_meta(&self, column: &ColumnHeader) -> Result<ColumnSubstrMeta<'a>> {
        let meta = self.column_meta(column)?;
        if meta.len() < COLUMN_SUBSTR_META_HEADER_SIZE || meta[0] != 0 || meta[1] & 0xc0 != 0 {
            return Err(Error::InvalidData);
        }
        let start_width = (meta[1] & 3) as usize;
        let length_width = ((meta[1] >> 2) & 3) as usize;
        let same_start = meta[1] & 0x10 != 0;
        let fixed_length = meta[1] & 0x20 != 0;
        if same_start != (start_width == 0)
            || fixed_length != (length_width == 0)
            || (!same_start && !matches!(start_width, 1 | 2))
            || (!fixed_length && !matches!(length_width, 1 | 2))
            || (!same_start && u16_at(meta, 2)? != 0)
            || (!fixed_length && u16_at(meta, 4)? != 0)
        {
            return Err(Error::InvalidData);
        }
        let exceptions = if meta.len() == COLUMN_SUBSTR_META_HEADER_SIZE {
            None
        } else {
            Some(ExceptionMeta::new(
                &meta[COLUMN_SUBSTR_META_HEADER_SIZE..],
                self.header.row_count,
                column.is_bit_packed(),
            )?)
        };
        Ok(ColumnSubstrMeta {
            exceptions,
            ref_col_idx: u16_at(meta, 6)? as usize,
            same_start: same_start.then_some(u16_at(meta, 2)? as usize),
            fixed_length: fixed_length.then_some(u16_at(meta, 4)? as usize),
            start_width,
            length_width,
        })
    }

    fn validate_column_equal_column(&self, column: &ColumnHeader) -> Result<()> {
        if column.is_fixed() {
            return Err(Error::InvalidData);
        }
        let meta = self.column_meta(column)?;
        if meta.len() < COLUMN_EQUAL_META_HEADER_SIZE || meta[0] != 0 {
            return Err(Error::InvalidData);
        }
        if meta.len() > COLUMN_EQUAL_META_HEADER_SIZE {
            ExceptionMeta::new(
                &meta[COLUMN_EQUAL_META_HEADER_SIZE..],
                self.header.row_count,
                column.is_bit_packed(),
            )?;
        }
        Ok(())
    }

    fn validate_column_substr_column(&self, column: &ColumnHeader) -> Result<()> {
        if !column.is_fixed() || column.has_extend_value() {
            return Err(Error::InvalidData);
        }
        let meta = self.column_substr_meta(column)?;
        let row_width = meta
            .start_width
            .checked_add(meta.length_width)
            .ok_or(Error::SizeOverflow)?;
        let data_size = self
            .header
            .row_count
            .checked_mul(row_width)
            .ok_or(Error::SizeOverflow)?;
        checked_slice(self.meta, column.offset + column.length, data_size)?;
        Ok(())
    }

    fn reference_column(&self, column: &ColumnHeader) -> Result<Option<usize>> {
        match column.kind {
            COLUMN_EQUAL => Ok(Some(u16_at(self.column_meta(column)?, 1)? as usize)),
            COLUMN_SUBSTR => Ok(Some(self.column_substr_meta(column)?.ref_col_idx)),
            _ => Ok(None),
        }
    }

    fn validate_column_references(&self) -> Result<()> {
        let mut edges = Vec::new();
        edges
            .try_reserve_exact(self.columns.len())
            .map_err(|_| Error::SizeOverflow)?;
        for (column_id, column) in self.columns.iter().enumerate() {
            if let Some(ref_col_idx) = self.reference_column(column)? {
                let referenced = self.columns.get(ref_col_idx).ok_or(Error::InvalidData)?;
                if ref_col_idx == column_id || referenced.obj_type != column.obj_type {
                    return Err(Error::InvalidData);
                }
                if column.kind == COLUMN_SUBSTR && !is_string_type(column.obj_type) {
                    return Err(Error::InvalidData);
                }
                edges.push((column_id, ref_col_idx));
            }
        }
        validate_reference_graph(&edges, self.columns.len())
    }

    fn string_diff_meta(&self, column: &ColumnHeader) -> Result<StringDiffMeta<'a>> {
        let meta = self.column_meta(column)?;
        if meta.len() < STRING_DIFF_META_HEADER_SIZE || meta[0] != 0 {
            return Err(Error::InvalidData);
        }
        let string_size = u16_at(meta, 2)? as usize;
        let data_offset = u32_at(meta, 4)? as usize;
        let data_length = u32_at(meta, 8)? as usize;
        let descriptor_count = meta[12] as usize;
        let alphabet_size = meta[1] as usize;
        if descriptor_count == 0 || alphabet_size > 16 {
            return Err(Error::InvalidData);
        }
        let descriptors_start = STRING_DIFF_META_HEADER_SIZE;
        let alphabet_start = descriptors_start
            .checked_add(descriptor_count)
            .ok_or(Error::SizeOverflow)?;
        let common_start = alphabet_start
            .checked_add(alphabet_size)
            .ok_or(Error::SizeOverflow)?;
        let descriptors = checked_slice(meta, descriptors_start, descriptor_count)?;
        let mut string_size_check = 0usize;
        let mut common_size = 0usize;
        let mut diff_size = 0usize;
        for &descriptor in descriptors {
            let count = (descriptor >> 1) as usize;
            if count == 0 {
                return Err(Error::InvalidData);
            }
            string_size_check = string_size_check
                .checked_add(count)
                .ok_or(Error::SizeOverflow)?;
            if descriptor & 1 == 0 {
                common_size = common_size.checked_add(count).ok_or(Error::SizeOverflow)?;
            } else {
                diff_size = diff_size.checked_add(count).ok_or(Error::SizeOverflow)?;
            }
        }
        if string_size == 0
            || string_size_check != string_size
            || diff_size == 0
            || common_start.checked_add(common_size) != Some(meta.len())
        {
            return Err(Error::InvalidData);
        }
        Ok(StringDiffMeta {
            descriptors,
            alphabet: checked_slice(meta, alphabet_start, alphabet_size)?,
            common: checked_slice(meta, common_start, common_size)?,
            data_offset,
            data_length,
            string_size,
            diff_size,
        })
    }

    fn validate_string_diff_column(&self, column: &ColumnHeader) -> Result<()> {
        if column.is_bit_packed() {
            return Err(Error::InvalidData);
        }
        let meta = self.string_diff_meta(column)?;
        let packed_width = meta.packed_width();
        if packed_width == 0 {
            return Err(Error::InvalidData);
        }
        if column.is_fixed() {
            if meta.data_length != packed_width {
                return Err(Error::InvalidData);
            }
            let data_size = fixed_column_data_size(
                self.header.row_count,
                self.header.extend_value_bit(),
                column.has_extend_value(),
                packed_width,
                false,
            )?;
            checked_slice(self.meta, column.offset + column.length, data_size)?;
        } else if meta.data_length >= self.header.var_column_count {
            return Err(Error::InvalidData);
        }
        Ok(())
    }

    fn validate_integer_diff_column(&self, column: &ColumnHeader) -> Result<()> {
        let store_size = integer_store_size(column.obj_type).ok_or(Error::Unsupported)?;
        if !column.is_fixed() || column.length != 2 + store_size {
            return Err(Error::InvalidData);
        }
        let meta = self.column_meta(column)?;
        if meta[0] != 0 || !valid_integer_width(meta[1] as usize, column.is_bit_packed()) {
            return Err(Error::InvalidData);
        }
        if column.has_extend_value() && !matches!(self.header.extend_value_bit(), 1 | 2) {
            return Err(Error::InvalidData);
        }
        let data_size = fixed_column_data_size(
            self.header.row_count,
            self.header.extend_value_bit(),
            column.has_extend_value(),
            meta[1] as usize,
            column.is_bit_packed(),
        )?;
        checked_slice(self.meta, column.offset + column.length, data_size)?;
        Ok(())
    }

    fn validate_dict_column(&self, column: &ColumnHeader) -> Result<()> {
        if !column.is_fixed() || column.has_extend_value() {
            return Err(Error::InvalidData);
        }
        let meta = self.column_meta(column)?;
        validate_dict_meta(meta, column.is_bit_packed(), false)?;
        let refs_size = fixed_column_data_size(
            self.header.row_count,
            0,
            false,
            meta[1] as usize,
            column.is_bit_packed(),
        )?;
        checked_slice(self.meta, column.offset + column.length, refs_size)?;
        Ok(())
    }

    fn validate_rle_column(&self, column: &ColumnHeader) -> Result<()> {
        if column.has_extend_value() || column.is_bit_packed() {
            return Err(Error::InvalidData);
        }
        let meta = self.column_meta(column)?;
        if meta.len() < RLE_META_HEADER_SIZE + DICT_META_HEADER_SIZE
            || meta[0] != 0
            || meta[1] & 0xc0 != 0
        {
            return Err(Error::InvalidData);
        }
        let row_id_width = (meta[1] & 7) as usize;
        let ref_width = ((meta[1] >> 3) & 7) as usize;
        if !valid_integer_width(row_id_width, false) || !valid_integer_width(ref_width, false) {
            return Err(Error::InvalidData);
        }
        let count = u32_at(meta, 2)? as usize;
        let dict_offset = u32_at(meta, 6)? as usize;
        let expected = RLE_META_HEADER_SIZE
            .checked_add(
                count
                    .checked_mul(row_id_width + ref_width)
                    .ok_or(Error::SizeOverflow)?,
            )
            .ok_or(Error::SizeOverflow)?;
        if count == 0 || dict_offset != expected || dict_offset >= meta.len() {
            return Err(Error::InvalidData);
        }
        let mut previous = 0usize;
        for index in 0..count {
            let row_id = read_uint(
                meta,
                RLE_META_HEADER_SIZE + index * row_id_width,
                row_id_width,
            )?;
            if row_id >= self.header.row_count
                || (index == 0 && row_id != 0)
                || (index > 0 && row_id <= previous)
            {
                return Err(Error::InvalidData);
            }
            previous = row_id;
        }
        validate_dict_meta(&meta[dict_offset..], false, true)
    }

    fn validate_hex_column(&self, column: &ColumnHeader) -> Result<()> {
        if column.is_bit_packed() {
            return Err(Error::InvalidData);
        }
        let meta = self.column_meta(column)?;
        if meta.len() < HEX_META_HEADER_SIZE + 1
            || meta.len() > HEX_META_HEADER_SIZE + 16
            || meta[0] != 0
        {
            return Err(Error::InvalidData);
        }
        let alphabet = &meta[HEX_META_HEADER_SIZE..];
        let mut seen = [false; 256];
        for &byte in alphabet {
            if seen[byte as usize] {
                return Err(Error::InvalidData);
            }
            seen[byte as usize] = true;
        }
        let max_string_size = u32_at(meta, 9)? as usize;
        if max_string_size > self.header.original_length as usize {
            return Err(Error::InvalidData);
        }
        let width = u32_at(meta, 5)? as usize;
        if column.is_fixed() {
            if width != max_string_size.div_ceil(2) {
                return Err(Error::InvalidData);
            }
            let data_size = fixed_column_data_size(
                self.header.row_count,
                self.header.extend_value_bit(),
                column.has_extend_value(),
                width,
                false,
            )?;
            checked_slice(self.meta, column.offset + column.length, data_size)?;
        } else {
            let raw = ColumnHeader {
                kind: RAW,
                attr: column.attr,
                obj_type: column.obj_type,
                extend_value_offset: column.extend_value_offset,
                offset: u32_at(meta, 1)? as usize,
                length: width,
            };
            self.validate_raw_column(&raw)?;
        }
        if column.has_extend_value() && !matches!(self.header.extend_value_bit(), 1 | 2) {
            return Err(Error::InvalidData);
        }
        Ok(())
    }

    fn validate_raw_column(&self, column: &ColumnHeader) -> Result<()> {
        if column.is_fixed() || column.is_bit_packed() {
            if column.is_bit_packed() && !(1..=64).contains(&column.length) {
                return Err(Error::InvalidData);
            }
            let extend_bits = if column.has_extend_value() {
                self.header
                    .row_count
                    .checked_mul(self.header.extend_value_bit())
                    .ok_or(Error::SizeOverflow)?
            } else {
                0
            };
            let byte_len = if column.is_bit_packed() {
                let value_bits = self
                    .header
                    .row_count
                    .checked_mul(column.length)
                    .ok_or(Error::SizeOverflow)?;
                let data_bits = extend_bits
                    .checked_add(value_bits)
                    .ok_or(Error::SizeOverflow)?;
                data_bits.checked_add(7).ok_or(Error::SizeOverflow)? / 8
            } else {
                let value_bytes = self
                    .header
                    .row_count
                    .checked_mul(column.length)
                    .ok_or(Error::SizeOverflow)?;
                extend_bits
                    .checked_add(7)
                    .map(|bits| bits / 8)
                    .and_then(|bytes| bytes.checked_add(value_bytes))
                    .ok_or(Error::SizeOverflow)?
            };
            checked_slice(self.meta, column.offset, byte_len)?;
        } else {
            if column.length >= self.header.var_column_count
                || column.is_last_var_field() != (column.length + 1 == self.header.var_column_count)
            {
                return Err(Error::InvalidData);
            }
        }
        Ok(())
    }

    fn column_meta(&self, column: &ColumnHeader) -> Result<&'a [u8]> {
        checked_slice(self.meta, column.offset, column.length)
    }

    fn row(&self, row_id: usize) -> Result<RowRef<'a>> {
        if row_id >= self.header.row_count {
            return Err(Error::InvalidArgument);
        }
        let width = self.header.row_index_byte();
        if width == 0 {
            let row_size = self.row_content_len / self.header.row_count;
            let start = row_id.checked_mul(row_size).ok_or(Error::SizeOverflow)?;
            return Ok(RowRef {
                bytes: checked_slice(self.row_data, start, row_size)?,
            });
        }
        let start = read_uint(self.row_data, self.row_index_offset + row_id * width, width)?;
        let end = read_uint(
            self.row_data,
            self.row_index_offset + (row_id + 1) * width,
            width,
        )?;
        Ok(RowRef {
            bytes: checked_slice(self.row_data, start, end - start)?,
        })
    }

    pub fn cell(&self, row_id: usize, column_id: usize) -> Result<Cell<'a>> {
        let column = *self.columns.get(column_id).ok_or(Error::InvalidArgument)?;
        let row = self.row(row_id)?;
        match column.kind {
            RAW => self.decode_raw(row_id, row, column),
            DICT => self.decode_dict(row_id, column),
            RLE => self.decode_rle(row_id, column),
            CONST => self.decode_const(row_id, column),
            INTEGER_BASE_DIFF => self.decode_integer_diff(row_id, column),
            COLUMN_EQUAL => self.decode_column_equal_cell(row_id, column),
            COLUMN_SUBSTR => self.decode_column_substr_cell(row_id, column),
            STRING_DIFF => Err(Error::Unsupported),
            _ => Err(Error::Unsupported),
        }
    }

    fn column_equal_exceptions(&self, column: &ColumnHeader) -> Result<Option<ExceptionMeta<'a>>> {
        let meta = self.column_meta(column)?;
        if meta.len() == COLUMN_EQUAL_META_HEADER_SIZE {
            Ok(None)
        } else {
            Ok(Some(ExceptionMeta::new(
                &meta[COLUMN_EQUAL_META_HEADER_SIZE..],
                self.header.row_count,
                column.is_bit_packed(),
            )?))
        }
    }

    fn decode_column_equal_cell(&self, row_id: usize, column: ColumnHeader) -> Result<Cell<'a>> {
        if let Some(exceptions) = self.column_equal_exceptions(&column)? {
            if let Some(cell) = exceptions.decode(row_id)? {
                return Ok(cell);
            }
        }
        let ref_col_idx = u16_at(self.column_meta(&column)?, 1)? as usize;
        self.cell(row_id, ref_col_idx)
    }

    fn column_substr_range(
        &self,
        row_id: usize,
        column: &ColumnHeader,
        meta: &ColumnSubstrMeta,
    ) -> Result<(usize, usize)> {
        let row_width = meta.start_width + meta.length_width;
        let offset = column
            .offset
            .checked_add(column.length)
            .and_then(|offset| offset.checked_add(row_id * row_width))
            .ok_or(Error::SizeOverflow)?;
        let start = match meta.same_start {
            Some(start) => start,
            None => read_uint(self.meta, offset, meta.start_width)?,
        };
        let length = match meta.fixed_length {
            Some(length) => length,
            None => read_uint(self.meta, offset + meta.start_width, meta.length_width)?,
        };
        Ok((start, length))
    }

    fn decode_column_substr_cell(&self, row_id: usize, column: ColumnHeader) -> Result<Cell<'a>> {
        let meta = self.column_substr_meta(&column)?;
        if let Some(exceptions) = meta.exceptions {
            if let Some(cell) = exceptions.decode(row_id)? {
                return Ok(cell);
            }
        }
        let (start, length) = self.column_substr_range(row_id, &column, &meta)?;
        match self.cell(row_id, meta.ref_col_idx)? {
            Cell::Null => Ok(Cell::Null),
            Cell::Nop => Ok(Cell::Nop),
            Cell::Bytes(bytes) => Ok(Cell::Bytes(checked_slice(bytes, start, length)?)),
            Cell::UInt(_) | Cell::Outrow(_) => Err(Error::InvalidData),
        }
    }

    fn decode_integer_diff(&self, row_id: usize, column: ColumnHeader) -> Result<Cell<'a>> {
        let meta = self.column_meta(&column)?;
        let store_size = integer_store_size(column.obj_type).ok_or(Error::Unsupported)?;
        let mut base = read_uint(meta, 2, store_size)? as u64;
        if store_size < 8
            && is_signed_store_type(column.obj_type)
            && base & (1u64 << (store_size * 8 - 1)) != 0
        {
            base |= u64::MAX << (store_size * 8);
        }
        let data_offset = column
            .offset
            .checked_add(column.length)
            .ok_or(Error::SizeOverflow)?;
        let extend_width = self.header.extend_value_bit();
        if column.has_extend_value() {
            let extend = read_bits(
                self.meta,
                data_offset * 8 + row_id * extend_width,
                extend_width,
            )?;
            match extend {
                0 => {}
                1 => return Ok(Cell::Null),
                2 => return Ok(Cell::Nop),
                _ => return Err(Error::InvalidData),
            }
        }
        let width = meta[1] as usize;
        let extend_bits = if column.has_extend_value() {
            self.header.row_count * extend_width
        } else {
            0
        };
        let delta = if column.is_bit_packed() {
            read_bits(
                self.meta,
                data_offset * 8 + extend_bits + row_id * width,
                width,
            )?
        } else {
            read_uint(
                self.meta,
                data_offset + extend_bits.div_ceil(8) + row_id * width,
                width,
            )? as u64
        };
        Ok(Cell::UInt(base.wrapping_add(delta)))
    }

    fn decode_dict(&self, row_id: usize, column: ColumnHeader) -> Result<Cell<'a>> {
        let meta = self.column_meta(&column)?;
        let ref_width = meta[1] as usize;
        let refs_offset = column
            .offset
            .checked_add(column.length)
            .ok_or(Error::SizeOverflow)?;
        let reference = if column.is_bit_packed() {
            read_bits(self.meta, refs_offset * 8 + row_id * ref_width, ref_width)?
        } else {
            read_uint(self.meta, refs_offset + row_id * ref_width, ref_width)? as u64
        };
        let reference = usize::try_from(reference).map_err(|_| Error::SizeOverflow)?;
        self.decode_dict_value(reference, column.offset, column.length)
    }

    fn decode_rle(&self, row_id: usize, column: ColumnHeader) -> Result<Cell<'a>> {
        let meta = self.column_meta(&column)?;
        let count = u32_at(meta, 2)? as usize;
        let row_id_width = (meta[1] & 7) as usize;
        let ref_width = ((meta[1] >> 3) & 7) as usize;
        let mut left = 0usize;
        let mut right = count;
        while left < right {
            let middle = left + (right - left) / 2;
            let start = read_uint(
                meta,
                RLE_META_HEADER_SIZE + middle * row_id_width,
                row_id_width,
            )?;
            if start <= row_id {
                left = middle + 1;
            } else {
                right = middle;
            }
        }
        if left == 0 {
            return Err(Error::InvalidData);
        }
        let refs_offset = RLE_META_HEADER_SIZE + count * row_id_width;
        let reference = read_uint(meta, refs_offset + (left - 1) * ref_width, ref_width)?;
        let dict_offset = u32_at(meta, 6)? as usize;
        self.decode_dict_value(
            reference,
            column.offset + dict_offset,
            column.length - dict_offset,
        )
    }

    fn decode_dict_value(
        &self,
        reference: usize,
        dict_offset: usize,
        dict_length: usize,
    ) -> Result<Cell<'a>> {
        let meta = checked_slice(self.meta, dict_offset, dict_length)?;
        let count = u32_at(meta, 2)? as usize;
        if reference == count {
            return Ok(Cell::Null);
        }
        if reference == count + 1 {
            return Ok(Cell::Nop);
        }
        if reference > count {
            return Err(Error::InvalidData);
        }
        if meta[8] & DICT_FIX_LENGTH != 0 {
            let entry_width = u16_at(meta, 6)? as usize;
            let start = dict_offset + DICT_META_HEADER_SIZE + reference * entry_width;
            return Ok(Cell::Bytes(checked_slice(self.meta, start, entry_width)?));
        }
        let index_width = u16_at(meta, 6)? as usize;
        let index_offset = dict_offset + DICT_META_HEADER_SIZE;
        let values_offset = index_offset + count.saturating_sub(1) * index_width;
        let values_size =
            dict_length - DICT_META_HEADER_SIZE - count.saturating_sub(1) * index_width;
        let start = if reference == 0 {
            0
        } else {
            read_uint(
                self.meta,
                index_offset + (reference - 1) * index_width,
                index_width,
            )?
        };
        let end = if reference + 1 == count {
            values_size
        } else {
            read_uint(
                self.meta,
                index_offset + reference * index_width,
                index_width,
            )?
        };
        if start > end || end > values_size {
            return Err(Error::InvalidData);
        }
        Ok(Cell::Bytes(checked_slice(
            self.meta,
            values_offset + start,
            end - start,
        )?))
    }

    fn decode_const(&self, row_id: usize, column: ColumnHeader) -> Result<Cell<'a>> {
        let meta = self.column_meta(&column)?;
        let count = meta[1] as usize;
        if count == 0 {
            return match meta[2] {
                0 => Ok(Cell::Bytes(&meta[CONST_META_HEADER_SIZE..])),
                1 => Ok(Cell::Null),
                2 => Ok(Cell::Nop),
                _ => Err(Error::InvalidData),
            };
        }
        let row_id_width = (meta[3] & 7) as usize;
        let row_ids_offset = CONST_META_HEADER_SIZE + count;
        let mut left = 0usize;
        let mut right = count;
        while left < right {
            let middle = left + (right - left) / 2;
            let exception_row =
                read_uint(meta, row_ids_offset + middle * row_id_width, row_id_width)?;
            if exception_row < row_id {
                left = middle + 1;
            } else {
                right = middle;
            }
        }
        let reference = if left < count
            && read_uint(meta, row_ids_offset + left * row_id_width, row_id_width)? == row_id
        {
            meta[CONST_META_HEADER_SIZE + left] as usize
        } else {
            meta[2] as usize
        };
        let dict_offset = u16_at(meta, 4)? as usize;
        self.decode_dict_value(
            reference,
            column.offset + dict_offset,
            column.length - dict_offset,
        )
    }

    fn decode_raw(&self, row_id: usize, row: RowRef<'a>, column: ColumnHeader) -> Result<Cell<'a>> {
        let extend_width = self.header.extend_value_bit();
        if column.has_extend_value() {
            let extend = if column.is_fixed() || column.is_bit_packed() {
                read_bits(
                    self.meta,
                    row_id * extend_width + column.offset * 8,
                    extend_width,
                )?
            } else {
                read_bits(row.bytes, column.extend_value_offset, extend_width)?
            };
            match extend {
                0 => {}
                1 => return Ok(Cell::Null),
                2 => return Ok(Cell::Nop),
                _ => return Err(Error::InvalidData),
            }
        }
        if column.is_bit_packed() {
            let extend_bits = if column.has_extend_value() {
                self.header.row_count * extend_width
            } else {
                0
            };
            let bit_offset = column
                .offset
                .checked_mul(8)
                .and_then(|offset| offset.checked_add(extend_bits))
                .and_then(|offset| offset.checked_add(row_id * column.length))
                .ok_or(Error::SizeOverflow)?;
            let value = read_bits(self.meta, bit_offset, column.length)?;
            return Ok(Cell::UInt(value));
        }
        if column.is_fixed() {
            let extend_bytes = if column.has_extend_value() {
                (self.header.row_count * extend_width).div_ceil(8)
            } else {
                0
            };
            let start = column
                .offset
                .checked_add(extend_bytes)
                .and_then(|offset| offset.checked_add(row_id * column.length))
                .ok_or(Error::SizeOverflow)?;
            return Ok(Cell::Bytes(checked_slice(self.meta, start, column.length)?));
        }
        self.decode_var_raw(row, column)
    }

    fn decode_var_raw(&self, row: RowRef<'a>, column: ColumnHeader) -> Result<Cell<'a>> {
        if self.header.var_column_count == 1 {
            return Ok(Cell::Bytes(checked_slice(
                row.bytes,
                column.offset,
                row.bytes
                    .len()
                    .checked_sub(column.offset)
                    .ok_or(Error::InvalidData)?,
            )?));
        }
        let width = *row.bytes.get(column.offset).ok_or(Error::InvalidData)? as usize;
        if !matches!(width, 1 | 2 | 4) {
            return Err(Error::InvalidData);
        }
        let index_offset = column.offset.checked_add(1).ok_or(Error::SizeOverflow)?;
        let index_len = (self.header.var_column_count - 1)
            .checked_mul(width)
            .ok_or(Error::SizeOverflow)?;
        let data_offset = index_offset
            .checked_add(index_len)
            .ok_or(Error::SizeOverflow)?;
        if data_offset > row.bytes.len() {
            return Err(Error::InvalidData);
        }
        let available = row.bytes.len() - data_offset;
        let start = if column.length == 0 {
            0
        } else {
            read_uint(row.bytes, index_offset + (column.length - 1) * width, width)?
        };
        let end = if column.is_last_var_field() {
            available
        } else {
            read_uint(row.bytes, index_offset + column.length * width, width)?
        };
        if start > end || end > available {
            return Err(Error::InvalidData);
        }
        Ok(Cell::Bytes(checked_slice(
            row.bytes,
            data_offset + start,
            end - start,
        )?))
    }

    fn decode_hex(
        &self,
        row_id: usize,
        row: RowRef<'a>,
        column: ColumnHeader,
        decoded: &mut Vec<u8>,
    ) -> Result<ProjectionCell<'a>> {
        let meta = self.column_meta(&column)?;
        let extend_width = self.header.extend_value_bit();
        if column.has_extend_value() {
            let extend = if column.is_fixed() {
                read_bits(
                    self.meta,
                    (column.offset + column.length) * 8 + row_id * extend_width,
                    extend_width,
                )?
            } else {
                read_bits(row.bytes, column.extend_value_offset, extend_width)?
            };
            match extend {
                0 => {}
                1 => return Ok(ProjectionCell::Borrowed(Cell::Null)),
                2 => return Ok(ProjectionCell::Borrowed(Cell::Nop)),
                _ => return Err(Error::InvalidData),
            }
        }
        let max_string_size = u32_at(meta, 9)? as usize;
        let (packed, odd) = if column.is_fixed() {
            let extend_bytes = if column.has_extend_value() {
                (self.header.row_count * extend_width).div_ceil(8)
            } else {
                0
            };
            let width = u32_at(meta, 5)? as usize;
            let start = column.offset + column.length + extend_bytes + row_id * width;
            (checked_slice(self.meta, start, width)?, 0usize)
        } else {
            let raw = ColumnHeader {
                kind: RAW,
                attr: column.attr,
                obj_type: column.obj_type,
                extend_value_offset: column.extend_value_offset,
                offset: u32_at(meta, 1)? as usize,
                length: u32_at(meta, 5)? as usize,
            };
            let Cell::Bytes(cell) = self.decode_var_raw(row, raw)? else {
                return Err(Error::InvalidData);
            };
            if cell.is_empty() || cell[0] > 1 {
                return Err(Error::InvalidData);
            }
            (&cell[1..], cell[0] as usize)
        };
        let decoded_len = if column.is_fixed() {
            max_string_size
        } else {
            packed
                .len()
                .checked_mul(2)
                .and_then(|length| length.checked_sub(odd))
                .ok_or(Error::InvalidData)?
        };
        if decoded_len > max_string_size {
            return Err(Error::InvalidData);
        }
        let output_offset = decoded.len();
        decoded
            .try_reserve(decoded_len)
            .map_err(|_| Error::SizeOverflow)?;
        decoded.resize(
            output_offset
                .checked_add(decoded_len)
                .ok_or(Error::SizeOverflow)?,
            0,
        );
        let alphabet = &meta[HEX_META_HEADER_SIZE..];
        for position in 0..decoded_len {
            let byte = packed[position / 2];
            let index = if position % 2 == 0 {
                byte >> 4
            } else {
                byte & 0xf
            };
            let value = *alphabet.get(index as usize).ok_or(Error::InvalidData)?;
            decoded[output_offset + position] = value;
        }
        Ok(ProjectionCell::Decoded {
            offset: output_offset,
            length: decoded_len,
        })
    }

    fn decode_string_diff(
        &self,
        row_id: usize,
        row: RowRef<'a>,
        column: ColumnHeader,
        decoded: &mut Vec<u8>,
    ) -> Result<ProjectionCell<'a>> {
        let meta = self.string_diff_meta(&column)?;
        let extend_width = self.header.extend_value_bit();
        if column.has_extend_value() {
            let extend = if column.is_fixed() {
                read_bits(
                    self.meta,
                    (column.offset + column.length) * 8 + row_id * extend_width,
                    extend_width,
                )?
            } else {
                read_bits(row.bytes, column.extend_value_offset, extend_width)?
            };
            match extend {
                0 => {}
                1 => return Ok(ProjectionCell::Borrowed(Cell::Null)),
                2 => return Ok(ProjectionCell::Borrowed(Cell::Nop)),
                _ => return Err(Error::InvalidData),
            }
        }
        let packed_width = meta.packed_width();
        let packed = if column.is_fixed() {
            let extend_bytes = if column.has_extend_value() {
                (self.header.row_count * extend_width).div_ceil(8)
            } else {
                0
            };
            let start = column.offset + column.length + extend_bytes + row_id * packed_width;
            checked_slice(self.meta, start, packed_width)?
        } else {
            let raw = ColumnHeader {
                kind: RAW,
                attr: column.attr,
                obj_type: column.obj_type,
                extend_value_offset: column.extend_value_offset,
                offset: meta.data_offset,
                length: meta.data_length,
            };
            let Cell::Bytes(cell) = self.decode_var_raw(row, raw)? else {
                return Err(Error::InvalidData);
            };
            checked_slice(cell, 0, cell.len())?
        };
        if packed.len() != packed_width {
            return Err(Error::InvalidData);
        }
        let output_offset = decoded.len();
        decoded
            .try_reserve(meta.string_size)
            .map_err(|_| Error::SizeOverflow)?;
        decoded.resize(
            output_offset
                .checked_add(meta.string_size)
                .ok_or(Error::SizeOverflow)?,
            0,
        );
        let output = &mut decoded[output_offset..output_offset + meta.string_size];
        let mut string_position = 0usize;
        let mut common_position = 0usize;
        let mut diff_position = 0usize;
        for &descriptor in meta.descriptors {
            let count = (descriptor >> 1) as usize;
            if descriptor & 1 == 0 {
                output[string_position..string_position + count]
                    .copy_from_slice(&meta.common[common_position..common_position + count]);
                common_position += count;
            } else if meta.alphabet.is_empty() {
                output[string_position..string_position + count]
                    .copy_from_slice(&packed[diff_position..diff_position + count]);
                diff_position += count;
            } else {
                for position in 0..count {
                    let byte = packed[(diff_position + position) / 2];
                    let index = if (diff_position + position).is_multiple_of(2) {
                        byte >> 4
                    } else {
                        byte & 0xf
                    };
                    output[string_position + position] = *meta
                        .alphabet
                        .get(index as usize)
                        .ok_or(Error::InvalidData)?;
                }
                diff_position += count;
            }
            string_position += count;
        }
        Ok(ProjectionCell::Decoded {
            offset: output_offset,
            length: meta.string_size,
        })
    }

    fn decode_string_prefix(
        &self,
        _row_id: usize,
        row: RowRef<'a>,
        column: ColumnHeader,
        decoded: &mut Vec<u8>,
    ) -> Result<ProjectionCell<'a>> {
        let meta = self.string_prefix_meta(&column)?;
        if column.has_extend_value() {
            let extend = read_bits(
                row.bytes,
                column.extend_value_offset,
                self.header.extend_value_bit(),
            )?;
            match extend {
                0 => {}
                1 => return Ok(ProjectionCell::Borrowed(Cell::Null)),
                2 => return Ok(ProjectionCell::Borrowed(Cell::Nop)),
                _ => return Err(Error::InvalidData),
            }
        }
        let raw = ColumnHeader {
            kind: RAW,
            attr: column.attr,
            obj_type: column.obj_type,
            extend_value_offset: column.extend_value_offset,
            offset: meta.data_offset,
            length: meta.data_length,
        };
        let Cell::Bytes(cell) = self.decode_var_raw(row, raw)? else {
            return Err(Error::InvalidData);
        };
        if cell.len() < 3 {
            return Err(Error::InvalidData);
        }
        let reference = (cell[0] & 0xf) as usize;
        let odd = (cell[0] >> 4) as usize;
        if reference >= meta.prefix_count || odd > 1 || (!meta.alphabet.is_empty() && odd > 1) {
            return Err(Error::InvalidData);
        }
        let common_length = u16_at(cell, 1)? as usize;
        let prefix_offset = if reference == 0 {
            0
        } else {
            read_uint(
                meta.indexes,
                (reference - 1) * meta.index_width,
                meta.index_width,
            )?
        };
        let prefix_end = if reference + 1 == meta.prefix_count {
            meta.prefixes.len()
        } else {
            read_uint(meta.indexes, reference * meta.index_width, meta.index_width)?
        };
        let prefix = checked_slice(
            meta.prefixes,
            prefix_offset,
            prefix_end
                .checked_sub(prefix_offset)
                .ok_or(Error::InvalidData)?,
        )?;
        let prefix = checked_slice(prefix, 0, common_length)?;
        let packed = &cell[3..];
        let suffix_length = if meta.alphabet.is_empty() {
            if odd != 0 {
                return Err(Error::InvalidData);
            }
            packed.len()
        } else {
            packed
                .len()
                .checked_mul(2)
                .and_then(|length| length.checked_sub(odd))
                .ok_or(Error::InvalidData)?
        };
        let decoded_length = common_length
            .checked_add(suffix_length)
            .ok_or(Error::SizeOverflow)?;
        if decoded_length > meta.max_string_size {
            return Err(Error::InvalidData);
        }
        let output_offset = decoded.len();
        decoded
            .try_reserve(decoded_length)
            .map_err(|_| Error::SizeOverflow)?;
        decoded.extend_from_slice(prefix);
        if meta.alphabet.is_empty() {
            decoded.extend_from_slice(packed);
        } else {
            for position in 0..suffix_length {
                let byte = packed[position / 2];
                let index = if position % 2 == 0 {
                    byte >> 4
                } else {
                    byte & 0xf
                };
                decoded.push(
                    *meta
                        .alphabet
                        .get(index as usize)
                        .ok_or(Error::InvalidData)?,
                );
            }
        }
        Ok(ProjectionCell::Decoded {
            offset: output_offset,
            length: decoded_length,
        })
    }

    fn decode_column_equal_projection(
        &self,
        row_id: usize,
        column: ColumnHeader,
        decoded: &mut Vec<u8>,
        visiting: &mut Vec<usize>,
        column_id: usize,
    ) -> Result<ProjectionCell<'a>> {
        if let Some(exceptions) = self.column_equal_exceptions(&column)? {
            if let Some(cell) = exceptions.decode(row_id)? {
                return Ok(ProjectionCell::Borrowed(cell));
            }
        }
        let ref_col_idx = u16_at(self.column_meta(&column)?, 1)? as usize;
        self.project_cell_nested(row_id, ref_col_idx, decoded, visiting, column_id)
    }

    fn decode_column_substr_projection(
        &self,
        row_id: usize,
        column: ColumnHeader,
        decoded: &mut Vec<u8>,
        visiting: &mut Vec<usize>,
        column_id: usize,
    ) -> Result<ProjectionCell<'a>> {
        let meta = self.column_substr_meta(&column)?;
        if let Some(exceptions) = meta.exceptions {
            if let Some(cell) = exceptions.decode(row_id)? {
                return Ok(ProjectionCell::Borrowed(cell));
            }
        }
        let referenced =
            self.project_cell_nested(row_id, meta.ref_col_idx, decoded, visiting, column_id)?;
        let (start, length) = self.column_substr_range(row_id, &column, &meta)?;
        match referenced {
            ProjectionCell::Borrowed(Cell::Null) => Ok(ProjectionCell::Borrowed(Cell::Null)),
            ProjectionCell::Borrowed(Cell::Nop) => Ok(ProjectionCell::Borrowed(Cell::Nop)),
            ProjectionCell::Borrowed(Cell::Bytes(bytes)) => Ok(ProjectionCell::Borrowed(
                Cell::Bytes(checked_slice(bytes, start, length)?),
            )),
            ProjectionCell::Decoded {
                offset,
                length: available,
            } => {
                if start.checked_add(length).is_none_or(|end| end > available) {
                    return Err(Error::InvalidData);
                }
                Ok(ProjectionCell::Decoded {
                    offset: offset + start,
                    length,
                })
            }
            ProjectionCell::Borrowed(Cell::UInt(_) | Cell::Outrow(_)) => Err(Error::InvalidData),
        }
    }

    fn project_cell(
        &self,
        row_id: usize,
        column_id: usize,
        decoded: &mut Vec<u8>,
    ) -> Result<ProjectionCell<'a>> {
        self.project_cell_nested(row_id, column_id, decoded, &mut Vec::new(), column_id)
    }

    fn project_cell_nested(
        &self,
        row_id: usize,
        column_id: usize,
        decoded: &mut Vec<u8>,
        visiting: &mut Vec<usize>,
        root_column_id: usize,
    ) -> Result<ProjectionCell<'a>> {
        if visiting.contains(&column_id) {
            return Err(Error::InvalidData);
        }
        visiting.push(column_id);
        let result = self.project_cell_inner(row_id, column_id, decoded, visiting, root_column_id);
        visiting.pop();
        result
    }

    fn project_cell_inner(
        &self,
        row_id: usize,
        column_id: usize,
        decoded: &mut Vec<u8>,
        visiting: &mut Vec<usize>,
        root_column_id: usize,
    ) -> Result<ProjectionCell<'a>> {
        let column = *self.columns.get(column_id).ok_or(Error::InvalidArgument)?;
        match column.kind {
            HEX_PACKING => self.decode_hex(row_id, self.row(row_id)?, column, decoded),
            STRING_DIFF => self.decode_string_diff(row_id, self.row(row_id)?, column, decoded),
            STRING_PREFIX => self.decode_string_prefix(row_id, self.row(row_id)?, column, decoded),
            COLUMN_EQUAL => {
                self.decode_column_equal_projection(row_id, column, decoded, visiting, column_id)
            }
            COLUMN_SUBSTR => self.decode_column_substr_projection(
                row_id,
                column,
                decoded,
                visiting,
                root_column_id,
            ),
            _ => Ok(ProjectionCell::Borrowed(self.cell(row_id, column_id)?)),
        }
    }

    pub fn project(&self, rows: &[u32], columns: &[u32]) -> Result<Projection<'a>> {
        let cell_count = rows
            .len()
            .checked_mul(columns.len())
            .ok_or(Error::SizeOverflow)?;
        let mut cells = Vec::new();
        cells
            .try_reserve_exact(cell_count)
            .map_err(|_| Error::SizeOverflow)?;
        let mut decoded = Vec::new();
        for &row in rows {
            for &column in columns {
                cells.push(self.project_cell(row as usize, column as usize, &mut decoded)?);
            }
        }
        Ok(Projection {
            row_count: rows.len(),
            column_count: columns.len(),
            cells,
            decoded,
        })
    }

    pub fn count_non_null(&self, rows: &[u32], column: usize) -> Result<usize> {
        if matches!(
            self.columns.get(column).ok_or(Error::InvalidArgument)?.kind,
            HEX_PACKING | STRING_DIFF | STRING_PREFIX | COLUMN_EQUAL | COLUMN_SUBSTR
        ) {
            let projection = self.project(rows, &[column as u32])?;
            let mut count = 0usize;
            for row in 0..rows.len() {
                if !matches!(projection.get(row, 0)?, Cell::Null | Cell::Nop) {
                    count += 1;
                }
            }
            return Ok(count);
        }
        let mut count = 0usize;
        for &row in rows {
            if !matches!(self.cell(row as usize, column)?, Cell::Null | Cell::Nop) {
                count += 1;
            }
        }
        Ok(count)
    }

    pub fn filter_uint_eq(&self, column: usize, value: u64, range: RowRange) -> Result<Vec<u32>> {
        self.check_range(range)?;
        let mut rows = Vec::new();
        for row in range.start..range.end {
            match self.cell(row, column)? {
                Cell::Null | Cell::Nop => {}
                cell => {
                    if cell.as_u64()? == value {
                        rows.push(row as u32);
                    }
                }
            }
        }
        Ok(rows)
    }

    pub fn lower_bound_uint(&self, column: usize, value: u64) -> Result<usize> {
        let mut low = 0usize;
        let mut high = self.row_count();
        while low < high {
            let middle = low + (high - low) / 2;
            if self.cell(middle, column)?.as_u64()? < value {
                low = middle + 1;
            } else {
                high = middle;
            }
        }
        Ok(low)
    }

    pub fn find_uint(&self, column: usize, value: u64) -> Result<Option<u32>> {
        let row = self.lower_bound_uint(column, value)?;
        if row < self.row_count() && self.cell(row, column)?.as_u64()? == value {
            Ok(Some(row as u32))
        } else {
            Ok(None)
        }
    }

    pub fn lower_bound_int(&self, column: usize, value: i64) -> Result<usize> {
        let mut low = 0usize;
        let mut high = self.row_count();
        while low < high {
            let middle = low + (high - low) / 2;
            if self.cell(middle, column)?.as_i64()? < value {
                low = middle + 1;
            } else {
                high = middle;
            }
        }
        Ok(low)
    }

    pub fn find_int(&self, column: usize, value: i64) -> Result<Option<u32>> {
        let row = self.lower_bound_int(column, value)?;
        if row < self.row_count() && self.cell(row, column)?.as_i64()? == value {
            Ok(Some(row as u32))
        } else {
            Ok(None)
        }
    }

    pub fn find_rowkey(
        &self,
        keys: &[crate::RowkeyValue<'_>],
        kinds: &[crate::RowkeyKind],
        uca: &dyn crate::UcaTable,
    ) -> Result<Option<u32>> {
        let bound =
            self.find_rowkey_bound(keys, kinds, uca, true, RowRange::new(0, self.row_count())?)?;
        Ok(bound.equal.then_some(bound.row as u32))
    }

    pub fn find_rowkey_bound(
        &self,
        keys: &[crate::RowkeyValue<'_>],
        kinds: &[crate::RowkeyKind],
        uca: &dyn crate::UcaTable,
        lower_bound: bool,
        range: RowRange,
    ) -> Result<RowkeyBound> {
        self.check_range(range)?;
        let mut decoded = Vec::new();
        crate::rowkey::find_bound(
            keys,
            kinds,
            lower_bound,
            range,
            self.column_count(),
            |row, column, query, kind| {
                let stored = if matches!(
                    self.columns.get(column).ok_or(Error::InvalidArgument)?.kind,
                    HEX_PACKING | STRING_DIFF | STRING_PREFIX | COLUMN_EQUAL | COLUMN_SUBSTR
                ) {
                    decoded.clear();
                    let decoded_cell = self.project_cell(row, column, &mut decoded)?;
                    match decoded_cell {
                        ProjectionCell::Borrowed(cell) => cell,
                        ProjectionCell::Decoded { offset, length } => {
                            Cell::Bytes(checked_slice(&decoded, offset, length)?)
                        }
                    }
                } else {
                    self.cell(row, column)?
                };
                crate::rowkey::compare_cell(query, stored, kind, uca)
            },
        )
    }

    fn check_range(&self, range: RowRange) -> Result<()> {
        if range.start > range.end || range.end > self.row_count() {
            Err(Error::InvalidArgument)
        } else {
            Ok(())
        }
    }
}

impl<'a> ExceptionMeta<'a> {
    fn new(bytes: &'a [u8], row_count: usize, bit_packed: bool) -> Result<Self> {
        let bitset_size = row_count.checked_add(63).ok_or(Error::SizeOverflow)? / 64 * 8;
        if bytes.len() < EXCEPTION_META_HEADER_SIZE + bitset_size {
            return Err(Error::InvalidData);
        }
        let ext_offset = bytes[0] as usize;
        let index_offset = bytes[1] as usize;
        let data_offset = bytes[2] as usize;
        let control = bytes[3] as usize;
        if ext_offset != bitset_size || index_offset < ext_offset || data_offset < index_offset {
            return Err(Error::InvalidData);
        }
        let bitset = checked_slice(bytes, EXCEPTION_META_HEADER_SIZE, bitset_size)?;
        let mut count = 0usize;
        for (word_index, chunk) in bitset.chunks_exact(8).enumerate() {
            let mut word = u64::from_le_bytes(chunk.try_into().map_err(|_| Error::InvalidData)?);
            let remaining = row_count.saturating_sub(word_index * 64);
            if remaining < 64 {
                let valid_mask = if remaining == 0 {
                    0
                } else {
                    (1u64 << remaining) - 1
                };
                if word & !valid_mask != 0 {
                    return Err(Error::InvalidData);
                }
                word &= valid_mask;
            }
            count = count
                .checked_add(word.count_ones() as usize)
                .ok_or(Error::SizeOverflow)?;
        }
        let has_extend = index_offset > ext_offset;
        let expected_extend_size = if has_extend {
            (count * 2).div_ceil(8)
        } else {
            0
        };
        if count == 0
            || count > u8::MAX as usize
            || index_offset - ext_offset != expected_extend_size
            || EXCEPTION_META_HEADER_SIZE + data_offset > bytes.len()
        {
            return Err(Error::InvalidData);
        }
        let variable = data_offset > index_offset;
        let data_len = bytes.len() - EXCEPTION_META_HEADER_SIZE - data_offset;
        let value_width = if bit_packed {
            if variable || !(1..=64).contains(&control) {
                return Err(Error::InvalidData);
            }
            let expected = (count * control).div_ceil(8);
            if data_len != expected {
                return Err(Error::InvalidData);
            }
            control
        } else if variable {
            if !matches!(control, 1 | 2 | 4 | 8)
                || data_offset - index_offset != (count - 1) * control
            {
                return Err(Error::InvalidData);
            }
            let mut previous = 0usize;
            for index in 0..count - 1 {
                let offset = read_uint(
                    bytes,
                    EXCEPTION_META_HEADER_SIZE + index_offset + index * control,
                    control,
                )?;
                if offset < previous || offset > data_len {
                    return Err(Error::InvalidData);
                }
                previous = offset;
            }
            control
        } else {
            if control != count || data_len % count != 0 {
                return Err(Error::InvalidData);
            }
            data_len / count
        };
        Ok(Self {
            bytes,
            count,
            ext_offset,
            index_offset,
            data_offset,
            value_width,
            bit_packed,
        })
    }

    fn reference(&self, row_id: usize) -> Result<Option<usize>> {
        let byte = EXCEPTION_META_HEADER_SIZE + row_id / 8;
        if self.bytes.get(byte).copied().unwrap_or(0) & (1 << (row_id % 8)) == 0 {
            return Ok(None);
        }
        let mut reference = 0usize;
        for word_index in 0..row_id / 64 {
            let start = EXCEPTION_META_HEADER_SIZE + word_index * 8;
            let word = u64::from_le_bytes(
                self.bytes[start..start + 8]
                    .try_into()
                    .map_err(|_| Error::InvalidData)?,
            );
            reference += word.count_ones() as usize;
        }
        let start = EXCEPTION_META_HEADER_SIZE + (row_id / 64) * 8;
        let word = u64::from_le_bytes(
            self.bytes[start..start + 8]
                .try_into()
                .map_err(|_| Error::InvalidData)?,
        );
        let bit = row_id % 64;
        let mask = if bit == 0 { 0 } else { (1u64 << bit) - 1 };
        reference += (word & mask).count_ones() as usize;
        if reference >= self.count {
            return Err(Error::InvalidData);
        }
        Ok(Some(reference))
    }

    fn decode(&self, row_id: usize) -> Result<Option<Cell<'a>>> {
        let Some(reference) = self.reference(row_id)? else {
            return Ok(None);
        };
        if self.index_offset > self.ext_offset {
            let extend = read_bits(
                self.bytes,
                (EXCEPTION_META_HEADER_SIZE + self.ext_offset) * 8 + reference * 2,
                2,
            )?;
            match extend {
                0 => {}
                1 => return Ok(Some(Cell::Null)),
                2 => return Ok(Some(Cell::Nop)),
                _ => return Err(Error::InvalidData),
            }
        }
        let data_start = EXCEPTION_META_HEADER_SIZE + self.data_offset;
        if self.bit_packed {
            return Ok(Some(Cell::UInt(read_bits(
                self.bytes,
                data_start * 8 + reference * self.value_width,
                self.value_width,
            )?)));
        }
        let variable = self.data_offset > self.index_offset;
        let (start, length) = if variable {
            let start = if reference == 0 {
                0
            } else {
                read_uint(
                    self.bytes,
                    EXCEPTION_META_HEADER_SIZE
                        + self.index_offset
                        + (reference - 1) * self.value_width,
                    self.value_width,
                )?
            };
            let end = if reference + 1 == self.count {
                self.bytes.len() - data_start
            } else {
                read_uint(
                    self.bytes,
                    EXCEPTION_META_HEADER_SIZE + self.index_offset + reference * self.value_width,
                    self.value_width,
                )?
            };
            (start, end.checked_sub(start).ok_or(Error::InvalidData)?)
        } else {
            (reference * self.value_width, self.value_width)
        };
        Ok(Some(Cell::Bytes(checked_slice(
            self.bytes,
            data_start + start,
            length,
        )?)))
    }
}

fn validate_reference_graph(edges: &[(usize, usize)], column_count: usize) -> Result<()> {
    let mut states = vec![0u8; column_count];
    for start in 0..column_count {
        if states[start] == 2 {
            continue;
        }
        let mut path = vec![start];
        states[start] = 1;
        while let Some(&current) = path.last() {
            if let Some((_, next)) = edges.iter().find(|(column, _)| *column == current) {
                match states[*next] {
                    1 => return Err(Error::InvalidData),
                    2 => {
                        path.pop();
                        states[current] = 2;
                    }
                    _ => {
                        states[*next] = 1;
                        path.push(*next);
                    }
                }
            } else {
                path.pop();
                states[current] = 2;
            }
        }
    }
    Ok(())
}

fn is_string_type(obj_type: u8) -> bool {
    matches!(obj_type, 22..=24 | 27..=30 | 36..=38 | 40)
}

fn validate_dict_meta(meta: &[u8], bit_packed: bool, embedded_in_rle: bool) -> Result<()> {
    if meta.len() < DICT_META_HEADER_SIZE
        || meta[0] != 0
        || meta[8] & !(DICT_FIX_LENGTH | DICT_SORTED) != 0
        || if embedded_in_rle {
            meta[1] != 0
        } else {
            !valid_integer_width(meta[1] as usize, bit_packed)
        }
    {
        return Err(Error::InvalidData);
    }
    let count = u32_at(meta, 2)? as usize;
    if meta[8] & DICT_FIX_LENGTH != 0 {
        let entry_width = u16_at(meta, 6)? as usize;
        if count > 0 && entry_width == 0 {
            return Err(Error::InvalidData);
        }
        let expected = DICT_META_HEADER_SIZE
            .checked_add(count.checked_mul(entry_width).ok_or(Error::SizeOverflow)?)
            .ok_or(Error::SizeOverflow)?;
        if meta.len() != expected {
            return Err(Error::InvalidData);
        }
    } else {
        let index_width = u16_at(meta, 6)? as usize;
        if !matches!(index_width, 1 | 2 | 4) {
            return Err(Error::InvalidData);
        }
        let index_size = count
            .saturating_sub(1)
            .checked_mul(index_width)
            .ok_or(Error::SizeOverflow)?;
        if meta.len() < DICT_META_HEADER_SIZE + index_size {
            return Err(Error::InvalidData);
        }
        let values_size = meta.len() - DICT_META_HEADER_SIZE - index_size;
        let mut previous = 0usize;
        for index in 0..count.saturating_sub(1) {
            let offset = read_uint(
                meta,
                DICT_META_HEADER_SIZE + index * index_width,
                index_width,
            )?;
            if offset < previous || offset > values_size {
                return Err(Error::InvalidData);
            }
            previous = offset;
        }
    }
    Ok(())
}

fn valid_integer_width(width: usize, bit_packed: bool) -> bool {
    if bit_packed {
        (1..=64).contains(&width)
    } else {
        matches!(width, 1 | 2 | 4 | 8)
    }
}

fn fixed_column_data_size(
    row_count: usize,
    extend_width: usize,
    has_extend: bool,
    width: usize,
    bit_packed: bool,
) -> Result<usize> {
    let extend_bits = if has_extend {
        row_count
            .checked_mul(extend_width)
            .ok_or(Error::SizeOverflow)?
    } else {
        0
    };
    let value_size = row_count.checked_mul(width).ok_or(Error::SizeOverflow)?;
    if bit_packed {
        extend_bits
            .checked_add(value_size)
            .and_then(|bits| bits.checked_add(7))
            .map(|bits| bits / 8)
            .ok_or(Error::SizeOverflow)
    } else {
        extend_bits
            .div_ceil(8)
            .checked_add(value_size)
            .ok_or(Error::SizeOverflow)
    }
}

fn read_bits(bytes: &[u8], bit_offset: usize, width: usize) -> Result<u64> {
    if width > 64 || bit_offset.checked_add(width).ok_or(Error::SizeOverflow)? > bytes.len() * 8 {
        return Err(Error::InvalidData);
    }
    let mut value = 0u64;
    for bit in 0..width {
        let position = bit_offset + bit;
        value |= u64::from((bytes[position / 8] >> (position % 8)) & 1) << bit;
    }
    Ok(value)
}

#[cfg(test)]
mod tests {
    use super::{Block, Cell, RowRange, RLE};
    use crate::checksum::crc32c;
    use crate::format::{
        header_checksum, parse_column_header, parse_header, write_u16, write_u32, write_u64,
        COLUMN_HEADER_SIZE,
    };
    use crate::{BlockWriter, ColumnSpec, Error, OwnedDatum, WriterConfig};
    use std::path::PathBuf;
    use std::sync::Barrier;

    fn fixture(name: &str) -> Option<Vec<u8>> {
        let directory = std::env::var_os("MICRO_BLOCK_FIXTURE_DIR")?;
        std::fs::read(PathBuf::from(directory).join(name)).ok()
    }

    fn encoded_block(row_count: u64) -> Vec<u8> {
        let mut key = ColumnSpec::fixed(5, 8);
        key.enable_bit_packing = true;
        let config = WriterConfig::new(
            vec![
                key,
                ColumnSpec::fixed(5, 2),
                ColumnSpec::variable(22),
                ColumnSpec::variable(22),
            ],
            1,
        );
        let mut writer = BlockWriter::new(config).unwrap();
        for row in 0..row_count {
            let value = match row % 3 {
                0 => OwnedDatum::Null,
                1 => OwnedDatum::Nop,
                _ => OwnedDatum::Bytes(format!("value-{row:03}!").into_bytes()),
            };
            writer
                .append_row(&[
                    OwnedDatum::UInt(row),
                    OwnedDatum::Bytes((row as u16).to_le_bytes().to_vec()),
                    value,
                    OwnedDatum::Bytes(b"constant".to_vec()),
                ])
                .unwrap();
        }
        writer.finish().unwrap()
    }

    fn reseal(bytes: &mut [u8]) {
        let header = parse_header(bytes).unwrap();
        let checksum = crc32c(0, &bytes[header.header_size..]);
        write_u64(bytes, 48, checksum).unwrap();
        let header = parse_header(bytes).unwrap();
        let checksum = header_checksum(bytes, &header);
        write_u16(bytes, 8, checksum).unwrap();
    }

    fn dictionary_block() -> Vec<u8> {
        let config = WriterConfig::new(vec![ColumnSpec::variable(22)], 0);
        let mut writer = BlockWriter::new(config).unwrap();
        for row in 0..64usize {
            writer
                .append_row(&[OwnedDatum::Bytes(
                    format!(
                        "repeated-dictionary-value-{:02}-{}",
                        row % 5,
                        "p".repeat(row % 5)
                    )
                    .into_bytes(),
                )])
                .unwrap();
        }
        writer.finish().unwrap()
    }

    #[test]
    fn rejects_invalid_dictionary_layouts_and_references() {
        let bytes = dictionary_block();
        let header = parse_header(&bytes).unwrap();
        let column_header = header.header_size;
        assert_eq!(bytes[column_header + 1], 1);
        let column_offset = u32::from_le_bytes(
            bytes[column_header + 8..column_header + 12]
                .try_into()
                .unwrap(),
        ) as usize;
        let column_length = u32::from_le_bytes(
            bytes[column_header + 12..column_header + 16]
                .try_into()
                .unwrap(),
        ) as usize;
        let meta_start = header.header_size + header.column_count * COLUMN_HEADER_SIZE;
        let dict_offset = meta_start + column_offset;
        let count =
            u32::from_le_bytes(bytes[dict_offset + 2..dict_offset + 6].try_into().unwrap()) as u8;
        assert_eq!(bytes[dict_offset + 1], 1);

        let mut bad_ref = bytes.clone();
        bad_ref[dict_offset + column_length] = count + 2;
        reseal(&mut bad_ref);
        let block = Block::new(&bad_ref).unwrap();
        assert_eq!(block.cell(0, 0), Err(Error::InvalidData));

        let mut bad_ref_width = bytes.clone();
        bad_ref_width[dict_offset + 1] = 0;
        reseal(&mut bad_ref_width);
        assert_eq!(Block::new(&bad_ref_width).err(), Some(Error::InvalidData));

        let mut bad_index = bytes.clone();
        bad_index[dict_offset + 9] = u8::MAX;
        reseal(&mut bad_index);
        assert_eq!(Block::new(&bad_index).err(), Some(Error::InvalidData));

        let mut bad_length = bytes;
        write_u32(&mut bad_length, column_header + 12, 8).unwrap();
        reseal(&mut bad_length);
        assert_eq!(Block::new(&bad_length).err(), Some(Error::InvalidData));
    }

    #[test]
    fn rejects_invalid_hex_metadata_cells_and_nibbles() {
        let mut writer =
            BlockWriter::new(WriterConfig::new(vec![ColumnSpec::variable(22)], 0)).unwrap();
        writer
            .append_row(&[OwnedDatum::Bytes(vec![b'0'; 64])])
            .unwrap();
        writer
            .append_row(&[OwnedDatum::Bytes(vec![b'1'; 64])])
            .unwrap();
        let bytes = writer.finish().unwrap();
        let header = parse_header(&bytes).unwrap();
        let column = header.header_size;
        let meta = header.header_size + COLUMN_HEADER_SIZE;
        assert_eq!(bytes[column + 1], 6);

        let mut duplicate_alphabet = bytes.clone();
        duplicate_alphabet[meta + 14] = duplicate_alphabet[meta + 13];
        reseal(&mut duplicate_alphabet);
        assert_eq!(
            Block::new(&duplicate_alphabet).err(),
            Some(Error::InvalidData)
        );

        let mut short_meta = bytes.clone();
        write_u32(&mut short_meta, column + 12, 12).unwrap();
        reseal(&mut short_meta);
        assert_eq!(Block::new(&short_meta).err(), Some(Error::InvalidData));

        let mut bad_width = bytes.clone();
        write_u32(&mut bad_width, meta + 5, 1).unwrap();
        reseal(&mut bad_width);
        assert_eq!(Block::new(&bad_width).err(), Some(Error::InvalidData));

        let mut bad_nibble = bytes;
        bad_nibble[meta + 15] = 0x22;
        reseal(&mut bad_nibble);
        let block = Block::new(&bad_nibble).unwrap();
        assert_eq!(block.project(&[0], &[0]).err(), Some(Error::InvalidData));

        let mut writer =
            BlockWriter::new(WriterConfig::new(vec![ColumnSpec::variable(22)], 0)).unwrap();
        writer
            .append_row(&[OwnedDatum::Bytes(vec![b'0'; 63])])
            .unwrap();
        writer
            .append_row(&[OwnedDatum::Bytes(vec![b'1'; 65])])
            .unwrap();
        let mut bad_odd = writer.finish().unwrap();
        let header = parse_header(&bad_odd).unwrap();
        bad_odd[header.row_data_offset] = 2;
        reseal(&mut bad_odd);
        let block = Block::new(&bad_odd).unwrap();
        assert_eq!(block.project(&[0], &[0]).err(), Some(Error::InvalidData));
    }

    #[test]
    fn rejects_invalid_integer_diff_layouts() {
        let mut spec = ColumnSpec::fixed(5, 8);
        spec.enable_bit_packing = true;
        let mut writer = BlockWriter::new(WriterConfig::new(vec![spec], 1)).unwrap();
        for value in 1_000_000..1_004_096u64 {
            writer.append_row(&[OwnedDatum::UInt(value)]).unwrap();
        }
        let bytes = writer.finish().unwrap();
        let header = parse_header(&bytes).unwrap();
        let column_header = header.header_size;
        assert_eq!(bytes[column_header + 1], 4);
        let column_offset = u32::from_le_bytes(
            bytes[column_header + 8..column_header + 12]
                .try_into()
                .unwrap(),
        ) as usize;
        let meta_start = header.header_size + header.column_count * COLUMN_HEADER_SIZE;
        let diff_offset = meta_start + column_offset;

        let mut bad_width = bytes.clone();
        bad_width[diff_offset + 1] = 0;
        reseal(&mut bad_width);
        assert_eq!(Block::new(&bad_width).err(), Some(Error::InvalidData));

        let mut bad_meta_length = bytes;
        write_u32(&mut bad_meta_length, column_header + 12, 1).unwrap();
        reseal(&mut bad_meta_length);
        assert_eq!(Block::new(&bad_meta_length).err(), Some(Error::InvalidData));
    }

    #[test]
    fn rejects_checksum_failures_and_invalid_column_layouts() {
        let bytes = encoded_block(16);
        let header = parse_header(&bytes).unwrap();

        let mut bad_header = bytes.clone();
        write_u16(&mut bad_header, 12, 0).unwrap();
        assert!(matches!(
            Block::new(&bad_header),
            Err(Error::ChecksumMismatch)
        ));

        let mut bad_payload = bytes.clone();
        let last = bad_payload.len() - 1;
        bad_payload[last] ^= 1;
        assert!(matches!(
            Block::new(&bad_payload),
            Err(Error::ChecksumMismatch)
        ));

        let meta_len =
            header.row_data_offset - header.header_size - header.column_count * COLUMN_HEADER_SIZE;
        let mut bad_offset = bytes.clone();
        let second_column = header.header_size + COLUMN_HEADER_SIZE;
        write_u32(&mut bad_offset, second_column + 8, (meta_len + 1) as u32).unwrap();
        reseal(&mut bad_offset);
        assert!(matches!(Block::new(&bad_offset), Err(Error::InvalidData)));

        let mut bad_width = bytes.clone();
        write_u32(&mut bad_width, header.header_size + 12, 65).unwrap();
        reseal(&mut bad_width);
        assert!(matches!(Block::new(&bad_width), Err(Error::InvalidData)));

        let mut bad_ordinal = bytes.clone();
        let variable_column = header.header_size + 2 * COLUMN_HEADER_SIZE;
        write_u32(
            &mut bad_ordinal,
            variable_column + 12,
            header.var_column_count as u32,
        )
        .unwrap();
        reseal(&mut bad_ordinal);
        assert!(matches!(Block::new(&bad_ordinal), Err(Error::InvalidData)));
    }

    #[test]
    fn rejects_invalid_row_indexes_after_validating_checksums() {
        let mut writer =
            BlockWriter::new(WriterConfig::new(vec![ColumnSpec::variable(22)], 0)).unwrap();
        for row in 0..16u64 {
            let mut value = row.to_le_bytes().to_vec();
            value.extend((0..=255).map(|byte| byte as u8));
            value.extend((0..=255).rev().map(|byte| byte as u8));
            value.resize(value.len() + row as usize, 0);
            writer.append_row(&[OwnedDatum::Bytes(value)]).unwrap();
        }
        let bytes = writer.finish().unwrap();
        let header = parse_header(&bytes).unwrap();
        let index_width = header.row_index_byte();
        assert!(matches!(index_width, 1 | 2 | 4));
        let index_offset = bytes.len() - (header.row_count + 1) * index_width;
        let mut bad_index = bytes;
        match index_width {
            1 => bad_index[index_offset] = 1,
            2 => write_u16(&mut bad_index, index_offset, 1).unwrap(),
            4 => write_u32(&mut bad_index, index_offset, 1).unwrap(),
            _ => unreachable!(),
        }
        reseal(&mut bad_index);
        assert!(matches!(Block::new(&bad_index), Err(Error::InvalidData)));
    }

    #[test]
    fn concurrent_projections_share_an_immutable_block() {
        let bytes = encoded_block(64);
        let block = Block::new(&bytes).unwrap();
        let rows: Vec<u32> = (0..block.row_count() as u32).collect();
        let barrier = Barrier::new(8);
        std::thread::scope(|scope| {
            for _ in 0..8 {
                let block = &block;
                let rows = &rows;
                let barrier = &barrier;
                scope.spawn(move || {
                    barrier.wait();
                    for _ in 0..128 {
                        let projection = block.project(rows, &[0, 2, 3]).unwrap();
                        for row in 0..projection.row_count() {
                            assert_eq!(
                                projection.get(row, 0).unwrap().as_u64().unwrap(),
                                row as u64
                            );
                            match row % 3 {
                                0 => assert_eq!(projection.get(row, 1).unwrap(), Cell::Null),
                                1 => assert_eq!(projection.get(row, 1).unwrap(), Cell::Nop),
                                _ => assert_eq!(
                                    projection.get(row, 1).unwrap().bytes().unwrap(),
                                    format!("value-{row:03}!").as_bytes()
                                ),
                            }
                            assert_eq!(
                                projection.get(row, 2).unwrap().bytes().unwrap(),
                                b"constant"
                            );
                        }
                    }
                });
            }
        });
    }

    #[test]
    #[ignore = "requires MICRO_BLOCK_FIXTURE_DIR with captured C++ blocks"]
    fn reads_cpp_blocks_and_matches_sql_projection() {
        let expected = std::fs::read_to_string(
            PathBuf::from(std::env::var_os("MICRO_BLOCK_FIXTURE_DIR").unwrap()).join("sql.tsv"),
        )
        .unwrap();
        let mut actual = String::new();
        let mut selected = 0;
        for name in ["pax0.bin", "pax1.bin"] {
            let bytes = fixture(name).unwrap();
            let block = Block::new(&bytes)
                .unwrap_or_else(|error| panic!("failed to read {name}: {error:?}"));
            assert_eq!(block.copied_input_bytes(), 0);
            let rows = block
                .filter_uint_eq(3, 7, RowRange::new(0, block.row_count()).unwrap())
                .unwrap();
            let projection = block.project(&rows, &[0, 4]).unwrap();
            for row in 0..projection.row_count() {
                let id = projection.get(row, 0).unwrap().as_u64().unwrap();
                let Cell::Bytes(tag) = projection.get(row, 1).unwrap() else {
                    panic!("constant tag was not returned as bytes")
                };
                actual.push_str(&format!("{}\t{}\n", id, std::str::from_utf8(tag).unwrap()));
            }
            selected += rows.len();
        }
        assert_eq!(selected, 482);
        assert_eq!(actual, expected);
    }

    #[test]
    #[ignore = "requires MICRO_BLOCK_RUST_FIXTURE_DIR with captured production blocks"]
    fn reads_rust_codec_blocks_and_matches_sql_projection() {
        use std::collections::{BTreeMap, BTreeSet};

        let directory = PathBuf::from(std::env::var_os("MICRO_BLOCK_RUST_FIXTURE_DIR").unwrap());
        let expected = std::fs::read_to_string(directory.join("sql.tsv")).unwrap();
        let expected: BTreeMap<u64, &str> = expected
            .lines()
            .map(|line| (line.split('\t').next().unwrap().parse().unwrap(), line))
            .collect();
        let mut paths: Vec<PathBuf> = std::fs::read_dir(&directory)
            .unwrap()
            .map(|entry| entry.unwrap().path())
            .filter(|path| path.extension().is_some_and(|extension| extension == "bin"))
            .collect();
        paths.sort();
        assert_eq!(paths.len(), 64);

        let mut rows_checked = 0;
        let mut kinds = BTreeSet::new();
        for path in paths {
            let bytes = std::fs::read(&path).unwrap();
            let block = Block::new(&bytes)
                .unwrap_or_else(|error| panic!("failed to read {path:?}: {error:?}"));
            assert_eq!(block.copied_input_bytes(), 0);
            assert_eq!(block.header.column_count, 11);
            assert_eq!(block.header.rowkey_column_count, 3);
            for column in 0..block.header.column_count {
                kinds.insert(
                    parse_column_header(
                        &bytes,
                        block.header.header_size + column * COLUMN_HEADER_SIZE,
                    )
                    .unwrap()
                    .kind,
                );
            }
            let rows: Vec<u32> = (0..block.row_count() as u32).collect();
            let projection = block.project(&rows, &[0, 3, 4, 5, 6, 7, 8, 9, 10]).unwrap();
            for row in 0..projection.row_count() {
                let id = projection.get(row, 0).unwrap().as_i64().unwrap() as u64;
                let mut fields = Vec::new();
                for column in 0..projection.column_count() {
                    let cell = projection.get(row, column).unwrap();
                    let field = match cell {
                        Cell::Null => "<NULL>".to_string(),
                        Cell::Bytes(_) if column >= 4 => std::str::from_utf8(cell.bytes().unwrap())
                            .unwrap()
                            .to_string(),
                        Cell::Bytes(_) | Cell::UInt(_) => cell.as_i64().unwrap().to_string(),
                        Cell::Nop | Cell::Outrow(_) => panic!("unexpected cell {cell:?}"),
                    };
                    fields.push(field);
                }
                let actual = fields.join("\t");
                assert_eq!(
                    actual,
                    *expected.get(&id).unwrap(),
                    "row id {id} in {path:?}"
                );
                rows_checked += 1;
            }
        }
        assert_eq!(rows_checked, 4457);
        assert!(kinds.is_superset(&BTreeSet::from([0, 1, 3, 4, 6])));
    }

    #[test]
    #[ignore = "requires MICRO_BLOCK_CPP_FIXTURE_DIR with captured production blocks"]
    fn reads_cpp_codec_blocks_and_matches_sql_projection() {
        use std::collections::{BTreeMap, BTreeSet};

        let directory = PathBuf::from(std::env::var_os("MICRO_BLOCK_CPP_FIXTURE_DIR").unwrap());
        let expected = std::fs::read_to_string(directory.join("sql.tsv")).unwrap();
        let expected: BTreeMap<u64, &str> = expected
            .lines()
            .map(|line| (line.split('\t').next().unwrap().parse().unwrap(), line))
            .collect();
        let mut paths: Vec<PathBuf> = std::fs::read_dir(&directory)
            .unwrap()
            .map(|entry| entry.unwrap().path())
            .filter(|path| path.extension().is_some_and(|extension| extension == "bin"))
            .collect();
        paths.sort();
        assert_eq!(paths.len(), 37);

        let mut rows_checked = 0;
        let mut seen_ids = BTreeSet::new();
        let mut kinds = BTreeSet::new();
        for path in paths {
            let bytes = std::fs::read(&path).unwrap();
            let block = Block::new(&bytes)
                .unwrap_or_else(|error| panic!("failed to read {path:?}: {error:?}"));
            assert_eq!(block.copied_input_bytes(), 0);
            assert_eq!(block.header.column_count, 11);
            assert_eq!(block.header.rowkey_column_count, 3);
            for column in 0..block.header.column_count {
                kinds.insert(
                    parse_column_header(
                        &bytes,
                        block.header.header_size + column * COLUMN_HEADER_SIZE,
                    )
                    .unwrap()
                    .kind,
                );
            }
            let rows: Vec<u32> = (0..block.row_count() as u32).collect();
            let projection = block.project(&rows, &[0, 3, 4, 5, 6, 7, 8, 9, 10]).unwrap();
            for row in 0..projection.row_count() {
                let id = projection.get(row, 0).unwrap().as_i64().unwrap() as u64;
                let mut fields = Vec::new();
                for column in 0..projection.column_count() {
                    let cell = projection.get(row, column).unwrap();
                    let field = match cell {
                        Cell::Null => "NULL".to_string(),
                        Cell::Bytes(_) if column >= 4 => std::str::from_utf8(cell.bytes().unwrap())
                            .unwrap()
                            .to_string(),
                        Cell::Bytes(_) | Cell::UInt(_) => cell.as_i64().unwrap().to_string(),
                        Cell::Nop | Cell::Outrow(_) => panic!("unexpected cell {cell:?}"),
                    };
                    fields.push(field);
                }
                let actual = fields.join("\t");
                let Some(expected_row) = expected.get(&id) else {
                    continue;
                };
                assert_eq!(actual, *expected_row, "row id {id} in {path:?}");
                seen_ids.insert(id);
                rows_checked += 1;
            }
        }
        assert_eq!(seen_ids.len(), 8073);
        assert!(rows_checked >= seen_ids.len());
        assert!(kinds.is_superset(&BTreeSet::from([0, 1, 2, 3, 4, 5, 6])));
    }

    #[test]
    #[ignore = "requires MICRO_BLOCK_RLE_FIXTURE_DIR with captured production blocks"]
    fn reads_rust_rle_blocks_and_matches_sql_projection() {
        use std::collections::BTreeMap;

        let directory = PathBuf::from(std::env::var_os("MICRO_BLOCK_RLE_FIXTURE_DIR").unwrap());
        let expected = std::fs::read_to_string(directory.join("sql.tsv")).unwrap();
        let expected: BTreeMap<u64, &str> = expected
            .lines()
            .map(|line| (line.split('\t').next().unwrap().parse().unwrap(), line))
            .collect();
        let mut rows_checked = 0;
        for index in 0..3 {
            let path = directory.join(format!("pax{index:03}.bin"));
            let bytes = std::fs::read(&path).unwrap();
            let block = Block::new(&bytes).unwrap();
            assert_eq!(block.copied_input_bytes(), 0);
            assert_eq!(block.header.column_count, 5);
            for column in [3, 4] {
                assert_eq!(
                    parse_column_header(
                        &bytes,
                        block.header.header_size + column * COLUMN_HEADER_SIZE,
                    )
                    .unwrap()
                    .kind,
                    RLE
                );
            }
            let rows: Vec<u32> = (0..block.row_count() as u32).collect();
            let projection = block.project(&rows, &[0, 3, 4]).unwrap();
            for row in 0..projection.row_count() {
                let id = projection.get(row, 0).unwrap().as_i64().unwrap() as u64;
                let run_val = projection.get(row, 1).unwrap().as_i64().unwrap();
                let run_text = projection.get(row, 2).unwrap().bytes().unwrap();
                let actual = format!(
                    "{id}\t{run_val}\t{}",
                    std::str::from_utf8(run_text).unwrap()
                );
                assert_eq!(actual, *expected.get(&id).unwrap());
                rows_checked += 1;
            }
        }
        assert_eq!(rows_checked, 8188);
    }

    #[test]
    #[ignore = "requires MICRO_BLOCK_FIXTURE_DIR with captured C++ blocks"]
    fn locates_integer_keys_and_rejects_corruption() {
        let bytes = fixture("pax0.bin").unwrap();
        let block = Block::new(&bytes).unwrap();
        assert_eq!(block.find_uint(0, 7).unwrap(), Some(7));
        assert_eq!(block.find_uint(0, 100_000).unwrap(), None);

        let mut bad_header = bytes.clone();
        bad_header[10] ^= 1;
        assert!(Block::new(&bad_header).is_err());
        let mut bad_payload = bytes.clone();
        let last = bad_payload.len() - 1;
        bad_payload[last] ^= 1;
        assert!(Block::new(&bad_payload).is_err());
        assert!(Block::new(&bytes[..bytes.len() - 1]).is_err());
    }

    #[test]
    fn locates_signed_raw_keys_at_all_widths() {
        for (width, values) in [
            (1, vec![i64::from(i8::MIN), -7, 0, 9, i64::from(i8::MAX)]),
            (
                2,
                vec![i64::from(i16::MIN), -300, 0, 400, i64::from(i16::MAX)],
            ),
            (
                4,
                vec![i64::from(i32::MIN), -70_000, 0, 80_000, i64::from(i32::MAX)],
            ),
            (
                8,
                vec![i64::MIN, -5_000_000_000, 0, 6_000_000_000, i64::MAX],
            ),
        ] {
            let config = WriterConfig::new(vec![ColumnSpec::fixed(5, width)], 1);
            let mut writer = BlockWriter::new(config).unwrap();
            for value in &values {
                writer
                    .append_row(&[OwnedDatum::Bytes(value.to_le_bytes()[..width].to_vec())])
                    .unwrap();
            }
            let bytes = writer.finish().unwrap();
            let block = Block::new(&bytes).unwrap();
            for (row, value) in values.iter().enumerate() {
                assert_eq!(block.find_int(0, *value).unwrap(), Some(row as u32));
            }
            assert_eq!(block.lower_bound_int(0, -1).unwrap(), 2);
            assert_eq!(block.find_int(0, -1).unwrap(), None);
            assert_eq!(block.find_int(0, *values.last().unwrap()).unwrap(), Some(4));
        }
    }

    #[test]
    fn zero_extends_bit_packed_signed_cells_like_cpp() {
        let mut key = ColumnSpec::fixed(5, 8);
        key.enable_bit_packing = true;
        let mut writer = BlockWriter::new(WriterConfig::new(vec![key], 1)).unwrap();
        for value in [0_i64, 1, 64, 127] {
            writer
                .append_row(&[OwnedDatum::UInt(value as u64)])
                .unwrap();
        }
        let bytes = writer.finish().unwrap();
        let header = crate::format::parse_header(&bytes).unwrap();
        assert_eq!(bytes[header.header_size + 2] & 4, 4);
        let block = Block::new(&bytes).unwrap();
        for (row, value) in [0_i64, 1, 64, 127].iter().enumerate() {
            assert_eq!(block.cell(row, 0).unwrap().as_i64().unwrap(), *value);
            assert_eq!(block.find_int(0, *value).unwrap(), Some(row as u32));
        }
    }

    #[test]
    fn reads_decompressed_blocks_with_compressed_checksum_metadata() {
        let config = WriterConfig::new(vec![ColumnSpec::fixed(5, 8)], 1);
        let mut writer = BlockWriter::new(config).unwrap();
        for value in [-9i64, 0, 11] {
            writer
                .append_row(&[OwnedDatum::Bytes(value.to_le_bytes().to_vec())])
                .unwrap();
        }
        let mut bytes = writer.finish().unwrap();
        write_u32(&mut bytes, 44, 1).unwrap();
        let header = parse_header(&bytes).unwrap();
        let checksum = header_checksum(&bytes, &header);
        write_u16(&mut bytes, 8, checksum).unwrap();
        let block = Block::new(&bytes).unwrap();
        assert_eq!(block.find_int(0, -9).unwrap(), Some(0));
        assert_eq!(block.find_int(0, 11).unwrap(), Some(2));
        assert!(Block::new(&bytes[..bytes.len() - 1]).is_err());
    }

    #[test]
    fn counts_values_without_materializing_cells() {
        let config = WriterConfig::new(vec![ColumnSpec::fixed(5, 8)], 1);
        let mut writer = BlockWriter::new(config).unwrap();
        for value in [
            OwnedDatum::Null,
            OwnedDatum::Nop,
            OwnedDatum::UInt(7),
            OwnedDatum::UInt(9),
        ] {
            writer.append_row(&[value]).unwrap();
        }
        let bytes = writer.finish().unwrap();
        let block = Block::new(&bytes).unwrap();
        assert_eq!(block.count_non_null(&[0, 1, 2, 3], 0).unwrap(), 2);
        assert_eq!(block.count_non_null(&[0, 1, 3], 0).unwrap(), 1);
        assert_eq!(block.count_non_null(&[], 0).unwrap(), 0);
        assert_eq!(
            block.count_non_null(&[4], 0),
            Err(crate::Error::InvalidArgument)
        );
    }

    #[test]
    fn rejects_invalid_column_references_and_cycles() {
        let config = WriterConfig::new(vec![ColumnSpec::variable(22), ColumnSpec::variable(22)], 0);
        let mut writer = BlockWriter::new(config).unwrap();
        for row in 0..30usize {
            let referenced = OwnedDatum::Bytes(
                format!("a-long-referenced-value-{row:02}-{}", "x".repeat(50)).into_bytes(),
            );
            let equal = if row == 3 {
                OwnedDatum::Bytes(b"different".to_vec())
            } else {
                referenced.clone()
            };
            writer.append_row(&[referenced, equal]).unwrap();
        }
        let bytes = writer.finish().unwrap();
        let header = parse_header(&bytes).unwrap();
        let column = parse_column_header(&bytes, header.header_size + COLUMN_HEADER_SIZE).unwrap();
        assert_eq!(column.kind, super::COLUMN_EQUAL);
        let meta_start = header.header_size + header.column_count * COLUMN_HEADER_SIZE;
        for invalid_ref in [1usize, 99usize] {
            let mut corrupted = bytes.clone();
            write_u16(
                &mut corrupted,
                meta_start + column.offset + 1,
                invalid_ref as u16,
            )
            .unwrap();
            reseal(&mut corrupted);
            assert!(matches!(Block::new(&corrupted), Err(Error::InvalidData)));
        }
        assert!(super::validate_reference_graph(&[(0, 1), (1, 2)], 3).is_ok());
        assert!(super::validate_reference_graph(&[(0, 1), (1, 0)], 2).is_err());
    }

    #[test]
    fn rejects_prefix_cells_beyond_their_prefix() {
        let mut writer =
            BlockWriter::new(WriterConfig::new(vec![ColumnSpec::variable(22)], 0)).unwrap();
        for row in 0..30usize {
            writer
                .append_row(&[OwnedDatum::Bytes(
                    format!(
                        "a-long-common-prefix-that-repeats/{row:02}{}",
                        "a".repeat(row % 5)
                    )
                    .into_bytes(),
                )])
                .unwrap();
        }
        let mut bytes = writer.finish().unwrap();
        let header = parse_header(&bytes).unwrap();
        assert_eq!(
            parse_column_header(&bytes, header.header_size)
                .unwrap()
                .kind,
            super::STRING_PREFIX
        );
        write_u16(&mut bytes, header.row_data_offset + 1, u16::MAX).unwrap();
        reseal(&mut bytes);
        let block = Block::new(&bytes).unwrap();
        assert!(matches!(block.project(&[0], &[0]), Err(Error::InvalidData)));
        for end in 0..bytes.len() {
            assert!(Block::new(&bytes[..end]).is_err());
        }
    }

    #[test]
    fn rejects_substring_ranges_beyond_the_referenced_cell() {
        let config = WriterConfig::new(vec![ColumnSpec::variable(22), ColumnSpec::variable(22)], 0);
        let mut writer = BlockWriter::new(config).unwrap();
        for row in 0..30usize {
            let referenced =
                OwnedDatum::Bytes(format!("prefix-{row:02}-{}", "z".repeat(70)).into_bytes());
            let substring = match &referenced {
                OwnedDatum::Bytes(bytes) => OwnedDatum::Bytes(bytes[4..12].to_vec()),
                _ => unreachable!(),
            };
            writer.append_row(&[referenced, substring]).unwrap();
        }
        let mut bytes = writer.finish().unwrap();
        let header = parse_header(&bytes).unwrap();
        let column = parse_column_header(&bytes, header.header_size + COLUMN_HEADER_SIZE).unwrap();
        assert_eq!(column.kind, super::COLUMN_SUBSTR);
        let meta_start = header.header_size + header.column_count * COLUMN_HEADER_SIZE;
        write_u16(&mut bytes, meta_start + column.offset + 2, u16::MAX).unwrap();
        reseal(&mut bytes);
        let block = Block::new(&bytes).unwrap();
        assert!(matches!(block.project(&[0], &[1]), Err(Error::InvalidData)));
    }
}
