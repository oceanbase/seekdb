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

use crate::format::{
    checked_slice, i32_at, parse_flat_header, read_uint, u16_at, u64_at, Header,
};
use crate::hash_index::{Bucket, Buckets};
use crate::read::ProjectionCell;
use crate::{Cell, Error, Projection, Result, RowRange, RowkeyBound, RowkeyCells};

const ROW_HEADER_SIZE: usize = 16;
const CLUSTER_COLUMNS: usize = 32;
const ROWKEY_INDEPENDENT_MAX: usize = 48;
const VAL_NORMAL: u8 = 0;
const VAL_OUTROW: u8 = 1;
const VAL_NOP: u8 = 2;
const VAL_NULL: u8 = 3;
const VAL_ENCODING_NORMAL: u8 = 4;

#[derive(Clone, Copy, Debug, Eq, PartialEq)]
pub struct FlatRowInfo {
    pub row_flag: u8,
    pub multi_version_flag: u8,
    pub rowkey_column_count: usize,
    pub column_count: usize,
    pub trans_id: i64,
}

/// A validated, uncompressed Flat micro-block borrowing its input storage.
pub struct FlatBlock<'a> {
    bytes: &'a [u8],
    header: Header,
}

struct FlatRow<'a> {
    bytes: &'a [u8],
    info: FlatRowInfo,
    cluster_count: usize,
    cluster_offset_width: usize,
    single_cluster: bool,
    rowkey_independent_cluster: bool,
}

struct Cluster<'a> {
    bytes: &'a [u8],
    column_count: usize,
    serialized_count: usize,
    offset_width: usize,
    column_index_width: usize,
    sparse: bool,
    cell_end: usize,
    offset_array: usize,
    special_array: usize,
}

fn width_from_kind(kind: u8) -> Result<usize> {
    match kind {
        0 => Ok(0),
        1 => Ok(1),
        2 => Ok(2),
        3 => Ok(4),
        _ => Err(Error::InvalidData),
    }
}

impl<'a> FlatBlock<'a> {
    // Storage verifies the block on ingest. A row metadata read validates
    // the layout and the requested row without walking unrelated rows.
    pub(crate) fn open(bytes: &'a [u8]) -> Result<Self> {
        Ok(Self { bytes, header: parse_flat_header(bytes)? })
    }

    pub(crate) fn open_checked(bytes: &'a [u8]) -> Result<Self> {
        let block = Self::open(bytes)?;
        block.header.check_expanded_record(bytes)?;
        Ok(block)
    }

    pub fn new(bytes: &'a [u8]) -> Result<Self> {
        let block = Self::open_checked(bytes)?;
        let header = block.header;
        let mut previous = 0usize;
        for row in 0..=block.row_count() {
            let offset = block.row_offset(row)?;
            if (row == 0 && offset != 0) || offset < previous {
                return Err(Error::InvalidData);
            }
            previous = offset;
        }
        if previous != header.row_data_offset - header.header_size {
            return Err(Error::InvalidData);
        }
        header.validate_flat_hash_trailer(bytes)?;
        for row in 0..block.row_count() {
            let parsed = block.row(row)?;
            if parsed.info.column_count != header.column_count {
                return Err(Error::InvalidData);
            }
        }
        Ok(block)
    }

    pub fn row_count(&self) -> usize {
        self.header.row_count
    }

    pub fn column_count(&self) -> usize {
        self.header.column_count
    }

    pub fn copied_input_bytes(&self) -> usize {
        let _ = self.bytes;
        0
    }

    pub(crate) fn hash_bucket(&self, hash: u64) -> Result<Bucket> {
        Buckets::parse(self.hash_trailer()?)?.lookup(hash, self.row_count())
    }

    fn hash_trailer(&self) -> Result<&'a [u8]> {
        self.header.flat_hash_trailer(self.bytes)
    }

    fn row_offset(&self, row: usize) -> Result<usize> {
        let offset = self
            .header
            .row_data_offset
            .checked_add(row.checked_mul(4).ok_or(Error::SizeOverflow)?)
            .ok_or(Error::SizeOverflow)?;
        let value = i32_at(self.bytes, offset)?;
        usize::try_from(value).map_err(|_| Error::InvalidData)
    }

    fn open_row(&self, row: usize) -> Result<FlatRow<'a>> {
        if row >= self.row_count() {
            return Err(Error::InvalidArgument);
        }
        let offset = self.row_offset(row)?;
        let end = self.row_offset(row + 1)?;
        if end > self.header.row_data_offset - self.header.header_size {
            return Err(Error::InvalidData);
        }
        let start = self
            .header
            .header_size
            .checked_add(offset)
            .ok_or(Error::SizeOverflow)?;
        let row = FlatRow::open(checked_slice(
            self.bytes,
            start,
            end.checked_sub(offset).ok_or(Error::InvalidData)?,
        )?)?;
        if row.info.column_count != self.column_count() {
            return Err(Error::InvalidData);
        }
        Ok(row)
    }

    fn row(&self, row: usize) -> Result<FlatRow<'a>> {
        let row = self.open_row(row)?;
        row.validate_cells()?;
        Ok(row)
    }

    pub fn row_info(&self, row: usize) -> Result<FlatRowInfo> {
        Ok(self.row(row)?.info)
    }

    pub fn cell(&self, row: usize, column: usize) -> Result<Cell<'a>> {
        let parsed = self.row(row)?;
        if column >= self.column_count() {
            return Ok(Cell::Nop);
        }
        parsed.cell(column)
    }

    pub(crate) fn read_rowkey(
        &self,
        row: usize,
        column_count: usize,
        key: &mut RowkeyCells<'a>,
    ) -> Result<()> {
        let parsed = self.open_row(row)?;
        if column_count == 0 || column_count > parsed.info.rowkey_column_count {
            return Err(Error::InvalidArgument);
        }
        key.cells.clear();
        parsed.project_cells(0..column_count, |cell| {
            key.cells.push(ProjectionCell::Borrowed(cell));
        })?;
        Ok(())
    }

    pub fn compare_rowkey_by<E: From<Error>>(
        &self,
        row: usize,
        column_count: usize,
        compare: impl FnOnce(&RowkeyCells<'_>) -> core::result::Result<core::cmp::Ordering, E>,
    ) -> core::result::Result<core::cmp::Ordering, E> {
        if column_count == 0 || column_count > self.header.rowkey_column_count {
            return Err(Error::InvalidArgument.into());
        }
        let mut key = RowkeyCells::new(column_count)?;
        self.read_rowkey(row, column_count, &mut key)?;
        compare(&key)
    }

    /// Return the first row in the range that differs from the query.
    pub fn find_equal_run_end_by<E: From<Error>>(
        &self,
        column_count: usize,
        range: RowRange,
        mut compare: impl FnMut(&RowkeyCells<'_>) -> core::result::Result<core::cmp::Ordering, E>,
    ) -> core::result::Result<usize, E> {
        if range.start > range.end
            || range.end > self.row_count()
            || column_count == 0
            || column_count > self.header.rowkey_column_count
        {
            return Err(Error::InvalidArgument.into());
        }
        let mut key = RowkeyCells::new(column_count)?;
        for row in range.start..range.end {
            self.read_rowkey(row, column_count, &mut key)?;
            if compare(&key)? != core::cmp::Ordering::Equal {
                return Ok(row);
            }
        }
        Ok(range.end)
    }

    pub fn find_rowkey_bound_by<E: From<Error>>(
        &self,
        column_count: usize,
        lower_bound: bool,
        range: RowRange,
        mut compare: impl FnMut(&RowkeyCells<'_>) -> core::result::Result<core::cmp::Ordering, E>,
    ) -> core::result::Result<RowkeyBound, E> {
        if range.start > range.end
            || range.end > self.row_count()
            || column_count == 0
            || column_count > self.header.rowkey_column_count
        {
            return Err(Error::InvalidArgument.into());
        }
        let mut key = RowkeyCells::new(column_count)?;
        crate::rowkey::search_bound(lower_bound, range, |row| {
            self.read_rowkey(row, column_count, &mut key)?;
            compare(&key)
        })
    }

    pub fn find_rowkey_bound(
        &self,
        keys: &[crate::RowkeyValue<'_>],
        kinds: &[crate::RowkeyKind],
        uca: &dyn crate::UcaTable,
        lower_bound: bool,
        range: RowRange,
    ) -> Result<RowkeyBound> {
        if range.end > self.row_count() {
            return Err(Error::InvalidArgument);
        }
        crate::rowkey::find_bound(
            keys,
            kinds,
            lower_bound,
            range,
            self.column_count(),
            |row, column, query, kind| {
                crate::rowkey::compare_cell(query, self.cell(row, column)?, kind, uca)
            },
        )
    }

    pub fn project(
        &self,
        rows: &[u32],
        columns: &[u32],
    ) -> Result<(Projection<'a>, Vec<FlatRowInfo>)> {
        let cell_count = rows
            .len()
            .checked_mul(columns.len())
            .ok_or(Error::SizeOverflow)?;
        let mut cells = Vec::new();
        cells
            .try_reserve_exact(cell_count)
            .map_err(|_| Error::SizeOverflow)?;
        let mut row_info = Vec::new();
        row_info
            .try_reserve_exact(rows.len())
            .map_err(|_| Error::SizeOverflow)?;
        for &row in rows {
            let parsed = self.row(row as usize)?;
            row_info.push(parsed.info);
            parsed.project_cells(columns.iter().map(|&column| column as usize), |cell| {
                cells.push(cell);
            })?;
        }
        Ok((Projection::new(rows.len(), columns.len(), cells), row_info))
    }
}

impl<'a> FlatRow<'a> {
    fn open(bytes: &'a [u8]) -> Result<Self> {
        if bytes.len() <= ROW_HEADER_SIZE || bytes[0] != 0 || bytes[6] & 0xf8 != 0 || bytes[7] != 0
        {
            return Err(Error::InvalidData);
        }
        let info = FlatRowInfo {
            row_flag: bytes[1],
            multi_version_flag: bytes[2],
            rowkey_column_count: bytes[3] as usize,
            column_count: u16_at(bytes, 4)? as usize,
            trans_id: u64_at(bytes, 8)? as i64,
        };
        let cluster_offset_width = width_from_kind(bytes[6] & 0x3)?;
        let single_cluster = bytes[6] & 0x4 != 0;
        let rowkey_independent_cluster =
            (CLUSTER_COLUMNS..=ROWKEY_INDEPENDENT_MAX).contains(&info.rowkey_column_count);
        if info.column_count == 0 || info.rowkey_column_count > info.column_count {
            return Err(Error::InvalidData);
        }
        let cluster_count = if single_cluster {
            1
        } else if rowkey_independent_cluster {
            1 + (info.column_count - info.rowkey_column_count).div_ceil(CLUSTER_COLUMNS)
        } else {
            info.column_count.div_ceil(CLUSTER_COLUMNS)
        };
        let cluster_offset_length = cluster_count
            .checked_mul(cluster_offset_width)
            .ok_or(Error::SizeOverflow)?;
        if ROW_HEADER_SIZE
            .checked_add(cluster_offset_length)
            .ok_or(Error::SizeOverflow)?
            >= bytes.len()
        {
            return Err(Error::InvalidData);
        }
        Ok(Self {
            bytes,
            info,
            cluster_count,
            cluster_offset_width,
            single_cluster,
            rowkey_independent_cluster,
        })
    }

    fn validate_cells(&self) -> Result<()> {
        for cluster in 0..self.cluster_count {
            self.cluster(cluster)?.validate_cells()?;
        }
        Ok(())
    }

    fn cluster_column_count(&self, cluster: usize) -> Result<usize> {
        if self.single_cluster {
            return Ok(self.info.column_count);
        }
        let start = if self.rowkey_independent_cluster {
            if cluster == 0 {
                return Ok(self.info.rowkey_column_count);
            }
            self.info.rowkey_column_count + (cluster - 1) * CLUSTER_COLUMNS
        } else {
            cluster * CLUSTER_COLUMNS
        };
        self.info
            .column_count
            .checked_sub(start)
            .map(|remaining| remaining.min(CLUSTER_COLUMNS))
            .ok_or(Error::InvalidData)
    }

    fn cluster(&self, cluster: usize) -> Result<Cluster<'a>> {
        if cluster >= self.cluster_count {
            return Err(Error::InvalidArgument);
        }
        let offset_array = self.bytes.len() - self.cluster_count * self.cluster_offset_width;
        let start = if cluster == 0 {
            ROW_HEADER_SIZE
        } else {
            read_uint(
                self.bytes,
                offset_array + cluster * self.cluster_offset_width,
                self.cluster_offset_width,
            )?
        };
        let end = if cluster + 1 == self.cluster_count {
            offset_array
        } else {
            read_uint(
                self.bytes,
                offset_array + (cluster + 1) * self.cluster_offset_width,
                self.cluster_offset_width,
            )?
        };
        if start < ROW_HEADER_SIZE || end < start || end > offset_array {
            return Err(Error::InvalidData);
        }
        Cluster::open(
            checked_slice(self.bytes, start, end - start)?,
            self.cluster_column_count(cluster)?,
            self.single_cluster,
        )
    }

    fn cell(&self, column: usize) -> Result<Cell<'a>> {
        if column >= self.info.column_count {
            return Ok(Cell::Nop);
        }
        let (cluster, local_column) = self.column_location(column);
        self.cluster(cluster)?.cell(local_column)
    }

    fn project_cells(
        &self,
        columns: impl IntoIterator<Item = usize>,
        mut push: impl FnMut(Cell<'a>),
    ) -> Result<()> {
        let mut cached_cluster = None;
        for column in columns {
            if column >= self.info.column_count {
                push(Cell::Nop);
                continue;
            }
            let (index, local_column) = self.column_location(column);
            let cluster = match &mut cached_cluster {
                Some((cached_index, cluster)) if *cached_index == index => cluster,
                cache => &mut cache.insert((index, self.cluster(index)?)).1,
            };
            push(cluster.cell(local_column)?);
        }
        Ok(())
    }

    fn column_location(&self, column: usize) -> (usize, usize) {
        if self.single_cluster {
            (0, column)
        } else if self.rowkey_independent_cluster && column < self.info.rowkey_column_count {
            (0, column)
        } else if self.rowkey_independent_cluster {
            let column = column - self.info.rowkey_column_count;
            (1 + column / CLUSTER_COLUMNS, column % CLUSTER_COLUMNS)
        } else {
            (column / CLUSTER_COLUMNS, column % CLUSTER_COLUMNS)
        }
    }
}

impl<'a> Cluster<'a> {
    fn open(bytes: &'a [u8], column_count: usize, single_cluster: bool) -> Result<Self> {
        if bytes.len() < 2 || column_count == 0 {
            return Err(Error::InvalidData);
        }
        let mask = bytes[1];
        if !single_cluster && bytes[0] as usize != column_count {
            return Err(Error::InvalidData);
        }
        let sparse = mask & 0x4 != 0;
        let serialized_count = if sparse {
            ((mask >> 4) & 0x3) as usize
        } else {
            column_count
        };
        if serialized_count > column_count {
            return Err(Error::InvalidData);
        }
        let offset_width = width_from_kind(mask & 0x3)?;
        let column_index_width = if sparse {
            width_from_kind((mask >> 3) & 0x1)?
        } else {
            0
        };
        let special_length = serialized_count.div_ceil(2);
        let offset_array = bytes
            .len()
            .checked_sub(special_length)
            .and_then(|position| position.checked_sub(offset_width.checked_mul(serialized_count)?))
            .ok_or(Error::InvalidData)?;
        let cell_end = offset_array
            .checked_sub(
                column_index_width
                    .checked_mul(serialized_count)
                    .ok_or(Error::SizeOverflow)?,
            )
            .ok_or(Error::InvalidData)?;
        if cell_end < 2 {
            return Err(Error::InvalidData);
        }
        let cluster = Self {
            bytes,
            column_count,
            serialized_count,
            offset_width,
            column_index_width,
            sparse,
            cell_end,
            offset_array,
            special_array: bytes.len() - special_length,
        };
        if sparse {
            let mut previous = None;
            for index in 0..serialized_count {
                let column = cluster.sparse_column(index)?;
                if column >= column_count || previous.is_some_and(|prev| column <= prev) {
                    return Err(Error::InvalidData);
                }
                previous = Some(column);
            }
        }
        Ok(cluster)
    }

    fn validate_cells(&self) -> Result<()> {
        for index in 0..self.serialized_count {
            self.decode_serialized(index)?;
        }
        Ok(())
    }

    fn offset(&self, index: usize) -> Result<usize> {
        if index >= self.serialized_count {
            return Err(Error::InvalidArgument);
        }
        read_uint(
            self.bytes,
            self.offset_array + index * self.offset_width,
            self.offset_width,
        )
    }

    fn sparse_column(&self, index: usize) -> Result<usize> {
        read_uint(
            self.bytes,
            self.cell_end + index * self.column_index_width,
            self.column_index_width,
        )
    }

    fn special_value(&self, index: usize) -> Result<u8> {
        let packed = *self
            .bytes
            .get(self.special_array + index / 2)
            .ok_or(Error::InvalidData)?;
        Ok((packed >> ((index % 2) * 4)) & 0x0f)
    }

    fn decode_serialized(&self, index: usize) -> Result<Cell<'a>> {
        let start = self.offset(index)?;
        let end = if index + 1 < self.serialized_count {
            self.offset(index + 1)?
        } else {
            self.cell_end
        };
        if start < 2 || end > self.cell_end {
            return Err(Error::InvalidData);
        }
        let bytes = checked_slice(
            self.bytes,
            start,
            end.checked_sub(start).ok_or(Error::InvalidData)?,
        )?;
        match self.special_value(index)? {
            VAL_NORMAL => Ok(Cell::Bytes(bytes)),
            VAL_OUTROW => Ok(Cell::Outrow(bytes)),
            VAL_NOP => Ok(Cell::Nop),
            VAL_NULL => Ok(Cell::Null),
            VAL_ENCODING_NORMAL => match bytes.len() {
                1 | 2 | 4 => Ok(Cell::UInt(read_uint(bytes, 0, bytes.len())? as u64)),
                _ => Err(Error::InvalidData),
            },
            _ => Err(Error::InvalidData),
        }
    }

    fn cell(&self, column: usize) -> Result<Cell<'a>> {
        if column >= self.column_count {
            return Err(Error::InvalidArgument);
        }
        let serialized = if self.sparse {
            let mut found = None;
            for index in 0..self.serialized_count {
                let stored_column = self.sparse_column(index)?;
                if stored_column == column {
                    found = Some(index);
                    break;
                } else if stored_column > column {
                    break;
                }
            }
            found
        } else {
            Some(column)
        };
        match serialized {
            Some(index) => self.decode_serialized(index),
            None => Ok(Cell::Nop),
        }
    }
}

#[cfg(test)]
mod tests {
    use super::{FlatBlock, FlatRow};
    use crate::flat_write::{encode_row, FlatDatum};
    use crate::Cell;
    use std::path::PathBuf;

    fn row_with_cluster(cluster: &[u8], column_count: u16) -> Vec<u8> {
        let mut row = vec![0u8; 16];
        row[1] = 3;
        row[3] = 1;
        row[4..6].copy_from_slice(&column_count.to_le_bytes());
        row[6] = 0x4;
        row.extend_from_slice(cluster);
        row
    }

    #[test]
    fn reads_dense_null_nop_and_compressed_integer() {
        let cluster = [255, 1, 7, b'x', b'y', 2, 3, 3, 3, 0x34, 0x02];
        let row = row_with_cluster(&cluster, 4);
        let row = FlatRow::open(&row).unwrap();
        row.validate_cells().unwrap();
        assert_eq!(row.cell(0).unwrap(), Cell::UInt(7));
        assert_eq!(row.cell(1).unwrap(), Cell::Null);
        assert_eq!(row.cell(2).unwrap(), Cell::Nop);
        assert_eq!(row.cell(3).unwrap(), Cell::Bytes(&row.bytes[19..21]));
    }

    #[test]
    fn missing_sparse_cells_are_nop_not_null() {
        let cluster = [255, 0x2d, b'z', 1, 4, 2, 2, 0x03];
        let row = row_with_cluster(&cluster, 6);
        let row = FlatRow::open(&row).unwrap();
        row.validate_cells().unwrap();
        assert_eq!(row.cell(0).unwrap(), Cell::Nop);
        assert_eq!(row.cell(1).unwrap(), Cell::Null);
        assert_eq!(row.cell(2).unwrap(), Cell::Nop);
        assert_eq!(row.cell(4).unwrap(), Cell::Bytes(&row.bytes[18..19]));
        assert_eq!(row.cell(5).unwrap(), Cell::Nop);
    }

    #[test]
    fn rejects_invalid_cluster_offsets_and_tags() {
        let mut cluster = vec![255, 1, 7, b'x', b'y', 2, 3, 3, 3, 0x34, 0x02];
        cluster[5] = 99;
        let row = row_with_cluster(&cluster, 4);
        let row = FlatRow::open(&row).unwrap();
        assert!(row.cell(0).is_err());
        assert!(row.validate_cells().is_err());
        let cluster = [255, 1, 7, b'x', b'y', 1, 3, 3, 3, 0x34, 0x02];
        let row = row_with_cluster(&cluster, 4);
        let row = FlatRow::open(&row).unwrap();
        assert!(row.cell(0).is_err());

        let mut cluster = vec![255, 1, 7, b'x', b'y', 2, 3, 3, 3, 0x34, 0x02];
        cluster[9] = 0xf4;
        let row = row_with_cluster(&cluster, 4);
        let row = FlatRow::open(&row).unwrap();
        assert_eq!(row.cell(0).unwrap(), Cell::UInt(7));
        assert!(row.validate_cells().is_err());
    }

    #[test]
    fn rejects_unsorted_sparse_indices_during_selected_projection() {
        let cluster = [255, 0x2d, b'z', 4, 1, 2, 2, 0x03];
        let row = row_with_cluster(&cluster, 6);
        let row = FlatRow::open(&row).unwrap();
        assert!(row.cell(1).is_err());
    }

    #[test]
    #[ignore = "requires MICRO_BLOCK_FLAT_FIXTURE_DIR with captured C++ blocks"]
    fn reencodes_every_captured_cpp_row_byte_for_byte() {
        let directory = PathBuf::from(std::env::var_os("MICRO_BLOCK_FLAT_FIXTURE_DIR").unwrap());
        for name in [
            "flat0.bin",
            "flat1.bin",
            "flat2.bin",
            "flat3.bin",
            "wide.bin",
        ] {
            let bytes = std::fs::read(directory.join(name)).unwrap();
            let block = FlatBlock::new(&bytes).unwrap();
            for row_index in 0..block.row_count() {
                let row = block.row(row_index).unwrap();
                let mut integers = Vec::new();
                integers.resize(block.column_count(), None);
                for column in 0..block.column_count() {
                    if let Cell::UInt(value) = row.cell(column).unwrap() {
                        integers[column] = Some(value);
                    }
                }
                let integer_bytes: Vec<[u8; 8]> = integers
                    .into_iter()
                    .map(|value| value.unwrap_or(0).to_le_bytes())
                    .collect();
                let cells: Vec<FlatDatum<'_>> = (0..block.column_count())
                    .map(|column| match row.cell(column).unwrap() {
                        Cell::Bytes(bytes) => FlatDatum::Bytes(bytes),
                        Cell::Outrow(bytes) => FlatDatum::Outrow(bytes),
                        Cell::UInt(_) => FlatDatum::Bytes(&integer_bytes[column]),
                        Cell::Null => FlatDatum::Null,
                        Cell::Nop => FlatDatum::Nop,
                    })
                    .collect();
                assert_eq!(
                    row.bytes,
                    encode_row(&cells, row.info).unwrap(),
                    "{name} row {row_index}"
                );
            }
        }
    }

    #[test]
    #[ignore = "requires MICRO_BLOCK_FLAT_FIXTURE_DIR with captured C++ blocks"]
    fn reads_real_compact_and_redundant_blocks() {
        let directory = PathBuf::from(std::env::var_os("MICRO_BLOCK_FLAT_FIXTURE_DIR").unwrap());
        let compact = std::fs::read(directory.join("flat0.bin")).unwrap();
        let block = FlatBlock::new(&compact).unwrap();
        assert_eq!(block.row_count(), 8);
        assert_eq!(block.column_count(), 8);
        assert_eq!(block.copied_input_bytes(), 0);
        assert_eq!(block.cell(0, 0).unwrap().as_i64().unwrap(), -2);
        assert_eq!(block.cell(0, 1).unwrap(), Cell::Bytes(b"Alpha"));
        assert_eq!(block.cell(0, 4).unwrap(), Cell::Null);
        assert_eq!(block.cell(0, 7).unwrap(), Cell::Bytes(b"explicit"));
        assert_eq!(block.cell(1, 0).unwrap(), Cell::UInt(0));
        assert_eq!(block.cell(1, 3).unwrap(), Cell::UInt(0));
        assert_eq!(block.cell(1, 4).unwrap(), Cell::Bytes(b""));
        assert_eq!(block.cell(1, 5).unwrap(), Cell::UInt(0));
        assert_eq!(block.cell(1, 6).unwrap().bytes().unwrap(), &[1, 132, 0, 0]);
        assert_eq!(block.cell(1, 7).unwrap(), Cell::Bytes(b"default-value"));
        assert_eq!(block.cell(2, 1).unwrap().bytes().unwrap(), "中".as_bytes());
        assert_eq!(block.cell(7, 7).unwrap(), Cell::Bytes(b"default-value"));
        let (projection, _) = block.project(&[0, 1, 2], &[0, 1, 4, 7, 8]).unwrap();
        assert_eq!(projection.row_count(), 3);
        assert_eq!(projection.column_count(), 5);
        assert_eq!(projection.get(0, 4).unwrap(), Cell::Nop);
        assert_eq!(block.row_info(0).unwrap().rowkey_column_count, 4);

        let redundant = std::fs::read(directory.join("flat3.bin")).unwrap();
        let block = FlatBlock::new(&redundant).unwrap();
        assert_eq!(block.column_count(), 6);
        assert_eq!(block.cell(2, 0).unwrap(), Cell::UInt(1));
        assert_eq!(block.cell(2, 1).unwrap().bytes().unwrap(), "中".as_bytes());
        assert_eq!(block.cell(2, 4).unwrap().bytes().unwrap().len(), 300);
        assert_eq!(block.cell(2, 5).unwrap(), Cell::UInt(1));
        assert!(FlatBlock::new(&redundant[..redundant.len() - 1]).is_err());
        let mut corrupt = redundant.clone();
        let index = u32::from_le_bytes(corrupt[24..28].try_into().unwrap()) as usize;
        corrupt[index] = 0xff;
        assert!(FlatBlock::new(&corrupt).is_err());
    }

    #[test]
    #[ignore = "requires MICRO_BLOCK_FLAT_FIXTURE_DIR with captured C++ blocks"]
    fn reads_real_multi_cluster_wide_rows() {
        let directory = PathBuf::from(std::env::var_os("MICRO_BLOCK_FLAT_FIXTURE_DIR").unwrap());
        let bytes = std::fs::read(directory.join("wide.bin")).unwrap();
        let block = FlatBlock::new(&bytes).unwrap();
        assert_eq!(block.row_count(), 3);
        assert_eq!(block.column_count(), 56);
        let second_row = block.row(1).unwrap();
        assert_eq!(second_row.cluster_count, 2);
        assert!(!second_row.single_cluster);
        assert_eq!(block.cell(1, 4).unwrap(), Cell::Null);
        assert_eq!(block.cell(1, 5).unwrap(), Cell::UInt(201));
        assert_eq!(block.cell(1, 7).unwrap(), Cell::Null);
        assert_eq!(block.cell(1, 35).unwrap(), Cell::UInt(231));
        assert_eq!(block.cell(1, 36).unwrap(), Cell::UInt(232));
        assert_eq!(block.cell(1, 43).unwrap(), Cell::Bytes(b"wide-default"));
        assert_eq!(block.cell(2, 43).unwrap(), Cell::Null);
        assert_eq!(block.cell(1, 44).unwrap(), Cell::Null);
        assert_eq!(block.cell(1, 45).unwrap(), Cell::UInt(241));
        assert_eq!(block.cell(1, 55).unwrap(), Cell::Bytes(b"wide-default-52"));
        let (projection, row_info) = block
            .project(&[1, 2], &[0, 4, 35, 36, 44, 45, 55, 56])
            .unwrap();
        assert_eq!(row_info.len(), 2);
        assert_eq!(row_info[0], block.row_info(1).unwrap());
        assert_eq!(row_info[1], block.row_info(2).unwrap());
        assert_eq!(projection.row_count(), 2);
        assert_eq!(projection.column_count(), 8);
        assert_eq!(projection.get(0, 2).unwrap(), Cell::UInt(231));
        assert_eq!(projection.get(0, 3).unwrap(), Cell::UInt(232));
        assert_eq!(projection.get(0, 4).unwrap(), Cell::Null);
        assert_eq!(projection.get(0, 5).unwrap(), Cell::UInt(241));
        assert_eq!(projection.get(0, 7).unwrap(), Cell::Nop);
    }
}
