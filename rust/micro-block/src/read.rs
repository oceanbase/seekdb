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
    checked_slice, header_checksum, parse_column_header, parse_header, read_uint,
    stored_header_checksum, ColumnHeader, Header, COLUMN_HEADER_SIZE,
};
use crate::{Error, Result};

const RAW: u8 = 0;
const CONST: u8 = 3;
const CONST_META_HEADER_SIZE: usize = 6;

/// A decoded cell. Integer bit-packing is represented as an immediate value;
/// every other value borrows the pinned input block without copying it.
#[derive(Clone, Copy, Debug, Eq, PartialEq)]
pub enum Cell<'a> {
    Null,
    Nop,
    Bytes(&'a [u8]),
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
            Self::Null | Self::Nop | Self::Bytes(_) => Err(Error::InvalidArgument),
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
            Self::Null | Self::Nop | Self::Bytes(_) => Err(Error::InvalidData),
        }
    }

    pub fn bytes(self) -> Result<&'a [u8]> {
        match self {
            Self::Bytes(bytes) => Ok(bytes),
            _ => Err(Error::InvalidArgument),
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

/// Owned batch descriptors borrowing cells from a `Block`. The C++ cache pin
/// must outlive both this batch and any datums constructed from its byte cells.
#[derive(Debug)]
pub struct Projection<'a> {
    row_count: usize,
    column_count: usize,
    cells: Vec<Cell<'a>>,
}

impl<'a> Projection<'a> {
    pub(crate) fn new(row_count: usize, column_count: usize, cells: Vec<Cell<'a>>) -> Self {
        Self {
            row_count,
            column_count,
            cells,
        }
    }

    pub fn row_count(&self) -> usize {
        self.row_count
    }

    pub fn column_count(&self) -> usize {
        self.column_count
    }

    pub fn cells(&self) -> &[Cell<'a>] {
        &self.cells
    }

    pub fn get(&self, row: usize, column: usize) -> Result<Cell<'a>> {
        if row >= self.row_count || column >= self.column_count {
            return Err(Error::InvalidArgument);
        }
        self.cells
            .get(row * self.column_count + column)
            .copied()
            .ok_or(Error::InvalidData)
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
                CONST => {
                    let meta = self.column_meta(column)?;
                    if meta.len() < CONST_META_HEADER_SIZE || meta[0] != 0 {
                        return Err(Error::InvalidData);
                    }
                    if meta[1] != 0 || meta[3] != 0 || meta[4] != 6 || meta[5] != 0 {
                        return Err(Error::Unsupported);
                    }
                    if meta[2] > 2 {
                        return Err(Error::InvalidData);
                    }
                }
                _ => {}
            }
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
            CONST => self.decode_const(column),
            _ => Err(Error::Unsupported),
        }
    }

    fn decode_const(&self, column: ColumnHeader) -> Result<Cell<'a>> {
        let meta = self.column_meta(&column)?;
        match meta[2] {
            0 => Ok(Cell::Bytes(&meta[CONST_META_HEADER_SIZE..])),
            1 => Ok(Cell::Null),
            2 => Ok(Cell::Nop),
            _ => Err(Error::InvalidData),
        }
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
            return Ok(Cell::UInt(read_bits(self.meta, bit_offset, column.length)?));
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

    pub fn project(&self, rows: &[u32], columns: &[u32]) -> Result<Projection<'a>> {
        let cell_count = rows
            .len()
            .checked_mul(columns.len())
            .ok_or(Error::SizeOverflow)?;
        let mut cells = Vec::new();
        cells
            .try_reserve_exact(cell_count)
            .map_err(|_| Error::SizeOverflow)?;
        for &row in rows {
            for &column in columns {
                cells.push(self.cell(row as usize, column as usize)?);
            }
        }
        Ok(Projection {
            row_count: rows.len(),
            column_count: columns.len(),
            cells,
        })
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
        let bound = self.find_rowkey_bound(
            keys,
            kinds,
            uca,
            true,
            RowRange::new(0, self.row_count())?,
        )?;
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
        if keys.is_empty() || keys.len() != kinds.len() || keys.len() > self.column_count() {
            return Err(Error::InvalidArgument);
        }
        self.check_range(range)?;
        let mut low = range.start;
        let mut high = range.end;
        let mut equal = false;
        while low < high {
            let middle = low + (high - low) / 2;
            let mut ordering = core::cmp::Ordering::Equal;
            for (index, (&query, &kind)) in keys.iter().zip(kinds).enumerate() {
                ordering =
                    crate::rowkey::compare_cell(query, self.cell(middle, index)?, kind, uca)?;
                if ordering != core::cmp::Ordering::Equal {
                    break;
                }
            }
            match ordering {
                core::cmp::Ordering::Less => high = middle,
                core::cmp::Ordering::Greater => low = middle + 1,
                core::cmp::Ordering::Equal => {
                    equal = true;
                    if lower_bound {
                        high = middle;
                    } else {
                        low = middle + 1;
                    }
                }
            }
        }
        Ok(RowkeyBound { row: low, equal })
    }

    fn check_range(&self, range: RowRange) -> Result<()> {
        if range.start > range.end || range.end > self.row_count() {
            Err(Error::InvalidArgument)
        } else {
            Ok(())
        }
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
    use super::{Block, Cell, RowRange};
    use crate::format::{header_checksum, parse_header, write_u16, write_u32};
    use crate::{BlockWriter, ColumnSpec, OwnedDatum, WriterConfig};
    use std::path::PathBuf;

    fn fixture(name: &str) -> Option<Vec<u8>> {
        let directory = std::env::var_os("MICRO_BLOCK_FIXTURE_DIR")?;
        std::fs::read(PathBuf::from(directory).join(name)).ok()
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
            let block = Block::new(&bytes).unwrap();
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
}
