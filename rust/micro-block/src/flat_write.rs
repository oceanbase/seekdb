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
    record_header_size, write_record_header, write_u32, Header, ALL_LOB_IN_ROW, CONTAINS_HASH_INDEX,
    CONTAIN_UNCOMMITTED_ROWS, FLAT_ROW_STORE, HASH_INDEX_OFFSET_SHIFT, HAS_COLUMN_CHECKSUM,
    HAS_MIN_MERGED_TRANS_VERSION, HAS_STRING_OUT_ROW, IS_LAST_ROW_LAST_FLAG, SINGLE_VERSION_ROWS,
};
use crate::hash_index;
use crate::{AppendOutcome, Error, FlatRowInfo, Result};

const ROW_HEADER_SIZE: usize = 16;
const CLUSTER_COLUMNS: usize = 32;
const ROWKEY_INDEPENDENT_MAX: usize = 48;
const MAX_CLUSTER_COLUMN_COUNT: usize = 256;
const MAX_SPARSE_COLUMNS: usize = 3;
const SPARSE_NOP_THRESHOLD: usize = CLUSTER_COLUMNS - MAX_SPARSE_COLUMNS;
const VAL_NORMAL: u8 = 0;
const VAL_OUTROW: u8 = 1;
const VAL_NOP: u8 = 2;
const VAL_NULL: u8 = 3;
const VAL_ENCODING_NORMAL: u8 = 4;

#[cfg(test)]
thread_local! {
    static FAIL_NEXT_ROW_RESERVE: std::cell::Cell<bool> = const { std::cell::Cell::new(false) };
}

#[derive(Clone, Copy, Debug, Eq, PartialEq)]
pub enum FlatDatum<'a> {
    Bytes(&'a [u8]),
    Outrow(&'a [u8]),
    Null,
    Nop,
}

#[derive(Clone, Copy, Debug)]
pub struct FlatWriterConfig {
    pub column_count: usize,
    pub rowkey_column_count: usize,
    pub micro_block_size_limit: usize,
    pub block_size_upper_bound: usize,
    pub need_column_checksums: bool,
    pub build_hash_index: bool,
}

#[derive(Clone, Copy, Debug)]
pub struct FlatFinishOptions {
    pub single_version_rows: bool,
    pub contain_uncommitted_rows: bool,
    pub is_last_row_last_flag: bool,
    pub has_string_out_row: bool,
    pub all_lob_in_row: bool,
    pub has_min_merged_trans_version: bool,
    pub min_merged_trans_version: i64,
    pub max_merged_trans_version: i64,
}

pub struct FlatWriter {
    config: FlatWriterConfig,
    row_data: Vec<u8>,
    row_offsets: Vec<u32>,
    hash_index: Option<hash_index::Builder>,
    finished_hash_index_size: usize,
    frozen: bool,
}

impl Default for FlatWriter {
    fn default() -> Self {
        Self {
            config: FlatWriterConfig {
                column_count: 0,
                rowkey_column_count: 0,
                micro_block_size_limit: 0,
                block_size_upper_bound: 0,
                need_column_checksums: false,
                build_hash_index: false,
            },
            row_data: Vec::new(),
            row_offsets: vec![0],
            hash_index: None,
            finished_hash_index_size: 0,
            frozen: false,
        }
    }
}

impl FlatWriter {
    pub fn new(config: FlatWriterConfig) -> Result<Self> {
        if config.column_count == 0
            || config.column_count > u16::MAX as usize
            || config.rowkey_column_count == 0
            || config.rowkey_column_count > config.column_count
            || config.rowkey_column_count > u8::MAX as usize
            || config.micro_block_size_limit == 0
            || config.block_size_upper_bound == 0
        {
            return Err(Error::InvalidArgument);
        }
        Ok(Self {
            config,
            row_data: Vec::new(),
            row_offsets: vec![0],
            hash_index: config.build_hash_index.then(hash_index::Builder::new),
            finished_hash_index_size: 0,
            frozen: false,
        })
    }

    pub fn row_count(&self) -> usize {
        self.row_offsets.len() - 1
    }

    pub(crate) fn empty_block_size(column_count: usize, need_column_checksums: bool) -> Result<usize> {
        let header_size = record_header_size(column_count, need_column_checksums)?;
        block_size(header_size, 0, 0, 0)
    }

    pub fn original_size(&self) -> Result<usize> {
        if 0 == self.row_count() {
            Ok(0)
        } else {
            Ok(self.block_size()? - header_size(self.config)?)
        }
    }

    pub fn block_size(&self) -> Result<usize> {
        block_size(
            header_size(self.config)?,
            self.row_data.len(),
            self.row_count(),
            self.hash_index_size(false),
        )
    }

    fn hash_index_size(&self, next_row: bool) -> usize {
        if self.frozen {
            self.finished_hash_index_size
        } else {
            self.hash_index
                .as_ref()
                .map_or(0, |index| index.estimated_size(next_row))
        }
    }

    pub(crate) fn hash_index_empty(&self) -> Option<bool> {
        self.hash_index.as_ref().map(hash_index::Builder::is_empty)
    }

    pub(crate) fn disable_hash_index(&mut self) {
        self.hash_index = None;
    }

    pub fn add_hash_entry(&mut self, row: usize, hash: u64) -> Result<()> {
        if self.frozen {
            return Err(Error::Frozen);
        }
        if row.checked_add(1) != Some(self.row_count()) {
            return Err(Error::InvalidArgument);
        }
        if let Some(index) = &mut self.hash_index {
            match index.append(hash, row) {
                Ok(()) => {}
                Err(Error::Unsupported) => self.disable_hash_index(),
                Err(error) => return Err(error),
            }
        }
        Ok(())
    }

    pub fn set_block_size_upper_bound(&mut self, block_size_upper_bound: usize) -> Result<()> {
        if 0 == block_size_upper_bound {
            return Err(Error::InvalidArgument);
        }
        self.config.block_size_upper_bound = block_size_upper_bound;
        Ok(())
    }

    pub fn append_row(
        &mut self,
        cells: &[FlatDatum<'_>],
        info: FlatRowInfo,
    ) -> Result<AppendOutcome> {
        if self.frozen {
            return Err(Error::Frozen);
        }
        if cells.len() != self.config.column_count
            || info.column_count != self.config.column_count
            || info.rowkey_column_count != self.config.rowkey_column_count
            || self.row_count() >= u32::MAX as usize
        {
            return Err(Error::InvalidArgument);
        }
        let encoded = encode_row(cells, info)?;
        let accepted_rows = self.row_count();
        let new_data_size = self
            .row_data
            .len()
            .checked_add(encoded.len())
            .ok_or(Error::SizeOverflow)?;
        let new_size = block_size(
            header_size(self.config)?,
            new_data_size,
            accepted_rows + 1,
            self.hash_index_size(true),
        )?;
        if (accepted_rows > 0 && new_size > self.config.micro_block_size_limit)
            || new_size > self.config.block_size_upper_bound
        {
            return Ok(AppendOutcome {
                accepted_rows,
                is_full: true,
            });
        }
        #[cfg(test)]
        if FAIL_NEXT_ROW_RESERVE.with(|fail| fail.replace(false)) {
            return Err(Error::SizeOverflow);
        }
        self.row_data
            .try_reserve(encoded.len())
            .map_err(|_| Error::SizeOverflow)?;
        self.row_offsets
            .try_reserve(1)
            .map_err(|_| Error::SizeOverflow)?;
        self.row_data.extend_from_slice(&encoded);
        self.row_offsets
            .push(u32::try_from(new_data_size).map_err(|_| Error::SizeOverflow)?);
        Ok(AppendOutcome {
            accepted_rows: accepted_rows + 1,
            is_full: false,
        })
    }

    pub fn finish(
        &mut self,
        options: FlatFinishOptions,
        column_checksums: &[u64],
    ) -> Result<Vec<u8>> {
        if self.frozen {
            return Err(Error::Frozen);
        }
        if self.row_count() == 0
            || (self.config.need_column_checksums
                && column_checksums.len() != self.config.column_count)
            || (!self.config.need_column_checksums && !column_checksums.is_empty())
        {
            return Err(Error::InvalidArgument);
        }
        let hash_index = self
            .hash_index
            .as_ref()
            .filter(|_| !options.contain_uncommitted_rows)
            .and_then(hash_index::Builder::build);
        let hash_index = hash_index.as_ref().map_or(&[][..], hash_index::Trailer::as_bytes);
        let header_size = header_size(self.config)?;
        let row_index_offset = header_size
            .checked_add(self.row_data.len())
            .ok_or(Error::SizeOverflow)?;
        let hash_offset = if hash_index.is_empty() {
            0
        } else {
            hash_index.len() as u16
        };
        let mut flags = if self.config.need_column_checksums {
            HAS_COLUMN_CHECKSUM
        } else {
            0
        };
        flags |= if options.has_string_out_row {
            HAS_STRING_OUT_ROW
        } else {
            0
        };
        flags |= if options.all_lob_in_row {
            ALL_LOB_IN_ROW
        } else {
            0
        };
        flags |= if hash_index.is_empty() {
            0
        } else {
            CONTAINS_HASH_INDEX | (hash_offset << HASH_INDEX_OFFSET_SHIFT)
        };
        flags |= if options.has_min_merged_trans_version {
            HAS_MIN_MERGED_TRANS_VERSION
        } else {
            0
        };
        let opt = u8::from(options.single_version_rows) * SINGLE_VERSION_ROWS
            | u8::from(options.contain_uncommitted_rows) * CONTAIN_UNCOMMITTED_ROWS
            | u8::from(options.is_last_row_last_flag) * IS_LAST_ROW_LAST_FLAG;
        let total_size = block_size(
            header_size,
            self.row_data.len(),
            self.row_count(),
            hash_index.len(),
        )?;
        let data_length = total_size - header_size;
        let header = Header {
            header_size,
            column_count: self.config.column_count,
            rowkey_column_count: self.config.rowkey_column_count,
            flags,
            row_count: self.row_count(),
            row_store_type: FLAT_ROW_STORE,
            opt,
            var_column_count: 0,
            row_data_offset: row_index_offset,
            original_length: i32::try_from(data_length).map_err(|_| Error::SizeOverflow)?,
            max_merged_trans_version: options.max_merged_trans_version,
            data_length,
            data_zlength: data_length,
            data_checksum: 0,
        };
        let mut block = Vec::new();
        block
            .try_reserve_exact(total_size)
            .map_err(|_| Error::SizeOverflow)?;
        block.resize(total_size, 0);
        let mut position = header_size;
        block[position..position + self.row_data.len()].copy_from_slice(&self.row_data);
        position += self.row_data.len();
        for offset in &self.row_offsets {
            write_u32(&mut block, position, *offset)?;
            position += 4;
        }
        block[position..].copy_from_slice(hash_index);
        let (prefix, payload) = block.split_at_mut(header_size);
        let header = header.with_stored_payload(payload)?;
        write_record_header(
            prefix,
            &header,
            options.min_merged_trans_version,
            column_checksums.iter().copied().map(Ok),
        )?;
        self.finished_hash_index_size = hash_index.len();
        self.frozen = true;
        Ok(block)
    }
}

fn header_size(config: FlatWriterConfig) -> Result<usize> {
    record_header_size(config.column_count, config.need_column_checksums)
}

fn block_size(
    header_size: usize,
    data_size: usize,
    row_count: usize,
    hash_index_size: usize,
) -> Result<usize> {
    let index_size = row_count
        .checked_add(1)
        .and_then(|count| count.checked_mul(4))
        .ok_or(Error::SizeOverflow)?;
    header_size
        .checked_add(data_size)
        .and_then(|size| size.checked_add(index_size))
        .and_then(|size| size.checked_add(hash_index_size))
        .ok_or(Error::SizeOverflow)
}

pub(crate) fn encode_row(cells: &[FlatDatum<'_>], info: FlatRowInfo) -> Result<Vec<u8>> {
    let (single_cluster, sparse_whole_row) = choose_layout(cells, info.rowkey_column_count);
    let cluster_count = if single_cluster {
        1
    } else if (CLUSTER_COLUMNS..=ROWKEY_INDEPENDENT_MAX).contains(&info.rowkey_column_count) {
        1 + (cells.len() - info.rowkey_column_count).div_ceil(CLUSTER_COLUMNS)
    } else {
        cells.len().div_ceil(CLUSTER_COLUMNS)
    };
    let cell_bytes = cells.iter().try_fold(0usize, |size, cell| {
        let length = match cell {
            FlatDatum::Bytes(bytes) | FlatDatum::Outrow(bytes) => bytes.len(),
            FlatDatum::Null | FlatDatum::Nop => 0,
        };
        size.checked_add(length).ok_or(Error::SizeOverflow)
    })?;
    let reserve = cell_bytes
        .checked_add(cells.len().checked_mul(6).ok_or(Error::SizeOverflow)?)
        .and_then(|size| size.checked_add(cluster_count * 16 + ROW_HEADER_SIZE))
        .ok_or(Error::SizeOverflow)?;
    let mut row = Vec::new();
    row.try_reserve_exact(reserve)
        .map_err(|_| Error::SizeOverflow)?;
    row.resize(ROW_HEADER_SIZE, 0);
    row[1] = info.row_flag;
    row[2] = info.multi_version_flag;
    row[3] = info.rowkey_column_count as u8;
    row[4..6].copy_from_slice(&(info.column_count as u16).to_le_bytes());
    row[8..16].copy_from_slice(&info.trans_id.to_le_bytes());
    let mut cluster_offsets = Vec::new();
    cluster_offsets
        .try_reserve_exact(cluster_count)
        .map_err(|_| Error::SizeOverflow)?;
    let rowkey_independent = !single_cluster
        && (CLUSTER_COLUMNS..=ROWKEY_INDEPENDENT_MAX).contains(&info.rowkey_column_count);
    let mut start = 0usize;
    for cluster in 0..cluster_count {
        cluster_offsets.push(row.len());
        let end = if single_cluster {
            cells.len()
        } else if rowkey_independent && cluster == 0 {
            info.rowkey_column_count
        } else {
            (start + CLUSTER_COLUMNS).min(cells.len())
        };
        let sparse = if single_cluster {
            sparse_whole_row
        } else {
            !(rowkey_independent && cluster == 0)
                && cells[start..end]
                    .iter()
                    .filter(|cell| matches!(cell, FlatDatum::Nop))
                    .count()
                    >= SPARSE_NOP_THRESHOLD
        };
        encode_cluster(&mut row, &cells[start..end], single_cluster, sparse)?;
        start = end;
    }
    let (offset_kind, offset_width) = integer_width(*cluster_offsets.last().unwrap());
    for offset in cluster_offsets {
        append_uint(&mut row, offset as u64, offset_width);
    }
    row[6] = offset_kind | if single_cluster { 0x4 } else { 0 };
    Ok(row)
}

fn choose_layout(cells: &[FlatDatum<'_>], rowkey_column_count: usize) -> (bool, bool) {
    if cells.len() <= ROWKEY_INDEPENDENT_MAX {
        return (true, false);
    }
    let rowkey_independent =
        (CLUSTER_COLUMNS..=ROWKEY_INDEPENDENT_MAX).contains(&rowkey_column_count);
    let nop_start = if rowkey_independent {
        rowkey_column_count
    } else {
        0
    };
    let nop_count = cells[nop_start..]
        .iter()
        .filter(|cell| matches!(cell, FlatDatum::Nop))
        .count();
    if cells.len() - nop_count <= MAX_SPARSE_COLUMNS && cells.len() < MAX_CLUSTER_COLUMN_COUNT {
        (true, true)
    } else {
        (false, false)
    }
}

fn encode_cluster(
    row: &mut Vec<u8>,
    cells: &[FlatDatum<'_>],
    single_cluster: bool,
    sparse: bool,
) -> Result<()> {
    let cluster_start = row.len();
    row.push(if single_cluster {
        u8::MAX
    } else {
        cells.len() as u8
    });
    let mask_position = row.len();
    row.push(0);
    let mut offsets = [0usize; ROWKEY_INDEPENDENT_MAX];
    let mut column_indices = [0usize; ROWKEY_INDEPENDENT_MAX];
    let mut special_values = [0u8; ROWKEY_INDEPENDENT_MAX];
    let mut serialized_count = 0usize;
    for (column, cell) in cells.iter().enumerate() {
        if sparse && matches!(cell, FlatDatum::Nop) {
            continue;
        }
        if serialized_count >= offsets.len() || (sparse && serialized_count >= MAX_SPARSE_COLUMNS) {
            return Err(Error::InvalidData);
        }
        offsets[serialized_count] = row.len() - cluster_start;
        column_indices[serialized_count] = column;
        match cell {
            FlatDatum::Nop => special_values[serialized_count] = VAL_NOP,
            FlatDatum::Null => special_values[serialized_count] = VAL_NULL,
            FlatDatum::Bytes(bytes) => {
                special_values[serialized_count] = VAL_NORMAL;
                append_cell(row, bytes, &mut special_values[serialized_count]);
            }
            FlatDatum::Outrow(bytes) => {
                special_values[serialized_count] = VAL_OUTROW;
                row.extend_from_slice(bytes);
            }
        }
        serialized_count += 1;
    }
    let column_index_kind = if sparse && serialized_count > 0 { 1 } else { 0 };
    if sparse {
        for index in &column_indices[..serialized_count] {
            row.push(*index as u8);
        }
    }
    let (offset_kind, offset_width) = if serialized_count == 0 {
        (0, 0)
    } else {
        integer_width(offsets[serialized_count - 1])
    };
    for offset in &offsets[..serialized_count] {
        append_uint(row, *offset as u64, offset_width);
    }
    for index in 0..serialized_count.div_ceil(2) {
        let low = special_values[index * 2];
        let high = special_values.get(index * 2 + 1).copied().unwrap_or(0);
        row.push(low | (high << 4));
    }
    row[mask_position] = offset_kind
        | if sparse { 0x4 } else { 0 }
        | (column_index_kind << 3)
        | if sparse {
            (serialized_count as u8) << 4
        } else {
            0
        };
    Ok(())
}

fn append_cell(row: &mut Vec<u8>, bytes: &[u8], special: &mut u8) {
    if bytes.len() == 8 {
        let value = u64::from_le_bytes(bytes.try_into().unwrap());
        let width = integer_bytes(value);
        append_uint(row, value, width);
        if width < 8 {
            *special = VAL_ENCODING_NORMAL;
        }
    } else {
        row.extend_from_slice(bytes);
    }
}

fn integer_bytes(value: u64) -> usize {
    if value <= u8::MAX as u64 {
        1
    } else if value <= u16::MAX as u64 {
        2
    } else if value <= u32::MAX as u64 {
        4
    } else {
        8
    }
}

fn integer_width(value: usize) -> (u8, usize) {
    match integer_bytes(value as u64) {
        1 => (1, 1),
        2 => (2, 2),
        4 => (3, 4),
        _ => unreachable!(),
    }
}

fn append_uint(row: &mut Vec<u8>, value: u64, width: usize) {
    row.extend_from_slice(&value.to_le_bytes()[..width]);
}

#[cfg(test)]
mod tests {
    use super::{
        FlatDatum, FlatFinishOptions, FlatWriter, FlatWriterConfig, FAIL_NEXT_ROW_RESERVE,
    };
    use crate::checksum::crc32c;
    use crate::format::{header_checksum, parse_flat_header, write_u16, write_u32, write_u64};
    use crate::{Error, FlatBlock, FlatRowInfo};

    fn info(column_count: usize, rowkey_column_count: usize) -> FlatRowInfo {
        FlatRowInfo {
            row_flag: 3,
            multi_version_flag: 1,
            rowkey_column_count,
            column_count,
            trans_id: 42,
        }
    }

    #[test]
    fn writes_dense_sparse_and_multi_cluster_rows() {
        let dense = [
            FlatDatum::Bytes(&7u64.to_le_bytes()),
            FlatDatum::Null,
            FlatDatum::Nop,
            FlatDatum::Bytes(b"xy"),
        ];
        let dense_block = write_single(&dense, 1);
        let parsed = FlatBlock::new(&dense_block).unwrap();
        assert_eq!(crate::Cell::UInt(7), parsed.cell(0, 0).unwrap());
        assert_eq!(crate::Cell::Null, parsed.cell(0, 1).unwrap());
        assert_eq!(crate::Cell::Nop, parsed.cell(0, 2).unwrap());
        assert_eq!(crate::Cell::Bytes(b"xy"), parsed.cell(0, 3).unwrap());

        let mut sparse = vec![FlatDatum::Nop; 56];
        let one = 1u64.to_le_bytes();
        sparse[0] = FlatDatum::Bytes(&one);
        sparse[1] = FlatDatum::Bytes(b"key");
        let sparse_block = write_single(&sparse, 2);
        let parsed = FlatBlock::new(&sparse_block).unwrap();
        assert_eq!(crate::Cell::Bytes(b"key"), parsed.cell(0, 1).unwrap());
        assert_eq!(crate::Cell::Nop, parsed.cell(0, 55).unwrap());

        let mut wide = sparse;
        for column in 32..40 {
            wide[column] = FlatDatum::Bytes(b"value");
        }
        let wide_block = write_single(&wide, 2);
        let parsed = FlatBlock::new(&wide_block).unwrap();
        assert_eq!(crate::Cell::Bytes(b"value"), parsed.cell(0, 39).unwrap());
        assert_eq!(crate::Cell::Nop, parsed.cell(0, 55).unwrap());
    }

    #[test]
    fn preserves_outrow_cells_without_integer_packing() {
        let outrow = 7u64.to_le_bytes();
        let cells = [FlatDatum::Bytes(&outrow), FlatDatum::Outrow(&outrow)];
        let block = write_single(&cells, 1);
        let parsed = FlatBlock::new(&block).unwrap();
        assert_eq!(crate::Cell::UInt(7), parsed.cell(0, 0).unwrap());
        assert_eq!(crate::Cell::Outrow(&outrow), parsed.cell(0, 1).unwrap());
    }

    #[test]
    fn finds_bounds_using_the_physical_rowkey_prefix() {
        let bytes = write_single(
            &[
                FlatDatum::Bytes(&2u64.to_le_bytes()),
                FlatDatum::Bytes(b"key"),
                FlatDatum::Bytes(&7u64.to_le_bytes()),
                FlatDatum::Bytes(&1u64.to_le_bytes()),
                FlatDatum::Null,
            ],
            4,
        );
        let block = FlatBlock::new(&bytes).unwrap();
        let range = crate::RowRange::new(0, 1).unwrap();
        for (lower, expected) in [(true, 0), (false, 1)] {
            let bound = block
                .find_rowkey_bound_by::<crate::Error>(3, lower, range, |key| {
                    assert_eq!(key.len(), 3);
                    assert_eq!(key.get(0)?.as_u64()?, 2);
                    assert_eq!(key.get(1)?.bytes()?, b"key");
                    Ok(7.cmp(&key.get(2)?.as_u64()?))
                })
                .unwrap();
            assert_eq!(bound, crate::RowkeyBound { row: expected, equal: true });
        }
        assert_eq!(
            block.find_rowkey_bound_by(3, true, range, |_| Err(crate::Error::InvalidData)),
            Err(crate::Error::InvalidData),
        );
    }

    fn write_single(cells: &[FlatDatum<'_>], rowkey_column_count: usize) -> Vec<u8> {
        let config = FlatWriterConfig {
            column_count: cells.len(),
            rowkey_column_count,
            micro_block_size_limit: usize::MAX,
            block_size_upper_bound: usize::MAX,
            need_column_checksums: false,
            build_hash_index: false,
        };
        let mut writer = FlatWriter::new(config).unwrap();
        let outcome = writer
            .append_row(cells, info(cells.len(), rowkey_column_count))
            .unwrap();
        assert_eq!(1, outcome.accepted_rows);
        writer
            .finish(
                FlatFinishOptions {
                    single_version_rows: false,
                    contain_uncommitted_rows: false,
                    is_last_row_last_flag: false,
                    has_string_out_row: false,
                    all_lob_in_row: true,
                    has_min_merged_trans_version: true,
                    min_merged_trans_version: 0,
                    max_merged_trans_version: 0,
                },
                &[],
            )
            .unwrap()
    }

    fn reseal(bytes: &mut [u8]) {
        let header = parse_flat_header(bytes).unwrap();
        let checksum = crc32c(0, &bytes[header.header_size..]);
        write_u64(bytes, 48, checksum).unwrap();
        let header = parse_flat_header(bytes).unwrap();
        let checksum = header_checksum(bytes, &header);
        write_u16(bytes, 8, checksum).unwrap();
    }

    #[test]
    fn rejects_truncation_checksum_failures_and_invalid_row_offsets() {
        let cells = [
            FlatDatum::Bytes(&7u64.to_le_bytes()),
            FlatDatum::Null,
            FlatDatum::Nop,
            FlatDatum::Bytes(b"xy"),
        ];
        let bytes = write_single(&cells, 1);
        for end in 0..bytes.len() {
            assert!(FlatBlock::new(&bytes[..end]).is_err(), "prefix {end}");
        }

        let mut bad_header = bytes.clone();
        write_u16(&mut bad_header, 12, 0).unwrap();
        assert!(matches!(
            FlatBlock::new(&bad_header),
            Err(Error::ChecksumMismatch)
        ));

        let mut bad_payload = bytes.clone();
        let last = bad_payload.len() - 1;
        bad_payload[last] ^= 1;
        assert!(matches!(
            FlatBlock::new(&bad_payload),
            Err(Error::ChecksumMismatch)
        ));

        let header = parse_flat_header(&bytes).unwrap();
        let mut bad_offset = bytes.clone();
        write_u32(&mut bad_offset, header.row_data_offset, 1).unwrap();
        reseal(&mut bad_offset);
        assert!(matches!(
            FlatBlock::new(&bad_offset),
            Err(Error::InvalidData)
        ));

        let mut negative_offset = bytes.clone();
        write_u32(&mut negative_offset, header.row_data_offset, u32::MAX).unwrap();
        reseal(&mut negative_offset);
        assert!(matches!(
            FlatBlock::new(&negative_offset),
            Err(Error::InvalidData)
        ));

        let mut bad_row = bytes;
        bad_row[header.header_size] = 1;
        reseal(&mut bad_row);
        assert!(matches!(FlatBlock::new(&bad_row), Err(Error::InvalidData)));
    }

    #[test]
    fn block_full_is_transactional() {
        let cells = [FlatDatum::Bytes(b"row")];
        let config = FlatWriterConfig {
            column_count: 1,
            rowkey_column_count: 1,
            micro_block_size_limit: 75,
            block_size_upper_bound: 1024,
            need_column_checksums: false,
            build_hash_index: false,
        };
        let mut writer = FlatWriter::new(config).unwrap();
        assert!(!writer.append_row(&cells, info(1, 1)).unwrap().is_full);
        let full = writer.append_row(&cells, info(1, 1)).unwrap();
        assert!(full.is_full);
        assert_eq!(1, full.accepted_rows);
        assert_eq!(1, writer.row_count());
        let block = writer
            .finish(
                FlatFinishOptions {
                    single_version_rows: false,
                    contain_uncommitted_rows: false,
                    is_last_row_last_flag: false,
                    has_string_out_row: false,
                    all_lob_in_row: true,
                    has_min_merged_trans_version: true,
                    min_merged_trans_version: 0,
                    max_merged_trans_version: 0,
                },
                &[],
            )
            .unwrap();
        let parsed = FlatBlock::new(&block).unwrap();
        assert_eq!(1, parsed.row_count());
        assert_eq!(crate::Cell::Bytes(b"row"), parsed.cell(0, 0).unwrap());
    }

    #[test]
    fn allocation_failure_is_transactional_and_retryable() {
        let config = FlatWriterConfig {
            column_count: 1,
            rowkey_column_count: 1,
            micro_block_size_limit: 1024,
            block_size_upper_bound: 1024,
            need_column_checksums: false,
            build_hash_index: false,
        };
        let mut writer = FlatWriter::new(config).unwrap();
        writer
            .append_row(&[FlatDatum::Bytes(b"first")], info(1, 1))
            .unwrap();
        FAIL_NEXT_ROW_RESERVE.with(|fail| fail.set(true));
        assert_eq!(
            writer.append_row(&[FlatDatum::Bytes(b"second")], info(1, 1)),
            Err(Error::SizeOverflow)
        );
        assert_eq!(writer.row_count(), 1);
        let outcome = writer
            .append_row(&[FlatDatum::Bytes(b"second")], info(1, 1))
            .unwrap();
        assert_eq!(outcome.accepted_rows, 2);
        assert!(!outcome.is_full);
        let block = writer
            .finish(
                FlatFinishOptions {
                    single_version_rows: false,
                    contain_uncommitted_rows: false,
                    is_last_row_last_flag: false,
                    has_string_out_row: false,
                    all_lob_in_row: true,
                    has_min_merged_trans_version: true,
                    min_merged_trans_version: 0,
                    max_merged_trans_version: 0,
                },
                &[],
            )
            .unwrap();
        let parsed = FlatBlock::new(&block).unwrap();
        assert_eq!(parsed.row_count(), 2);
        assert_eq!(parsed.cell(0, 0).unwrap(), crate::Cell::Bytes(b"first"));
        assert_eq!(parsed.cell(1, 0).unwrap(), crate::Cell::Bytes(b"second"));
    }

    #[test]
    fn reads_hash_index_trailer() {
        let cells = [FlatDatum::Bytes(b"row")];
        let config = FlatWriterConfig {
            column_count: 1,
            rowkey_column_count: 1,
            micro_block_size_limit: 1024,
            block_size_upper_bound: 1024,
            need_column_checksums: false,
            build_hash_index: true,
        };
        let mut writer = FlatWriter::new(config).unwrap();
        for row in 0..17 {
            writer.append_row(&cells, info(1, 1)).unwrap();
            let hash = if row == 2 { 24 } else { row as u64 };
            writer.add_hash_entry(row, hash).unwrap();
        }
        let estimated_size = writer.block_size().unwrap();
        let block = writer
            .finish(
                FlatFinishOptions {
                    single_version_rows: false,
                    contain_uncommitted_rows: false,
                    is_last_row_last_flag: false,
                    has_string_out_row: false,
                    all_lob_in_row: true,
                    has_min_merged_trans_version: true,
                    min_merged_trans_version: 0,
                    max_merged_trans_version: 0,
                },
                &[],
            )
            .unwrap();
        let parsed = FlatBlock::new(&block).unwrap();
        assert_eq!(estimated_size, block.len());
        assert_eq!(block.len(), writer.block_size().unwrap());
        let expected = [
            0, 23, 0, 0, 254, 255, 3, 4, 5, 6, 7, 8, 9, 10, 11, 12, 13, 14, 15, 16, 255, 255,
            255, 255, 255, 255,
        ];
        let trailer_start = block.len() - expected.len();
        assert_eq!(&block[trailer_start..], &expected);
        assert_eq!(crate::Cell::Bytes(b"row"), parsed.cell(0, 0).unwrap());
        use crate::hash_index::Bucket;
        let header = parse_flat_header(&block).unwrap();
        assert_eq!(header.original_length as usize, block.len() - header.header_size);
        assert!(matches!(parsed.hash_bucket(1u64 << 32), Ok(Bucket::Row(0))));
        assert!(matches!(parsed.hash_bucket(1), Ok(Bucket::Collision)));
        assert!(matches!(parsed.hash_bucket(2), Ok(Bucket::Vacant)));

        let mut corrupt = block;
        corrupt[trailer_start + 3] = 17;
        assert!(matches!(FlatBlock::new(&corrupt), Err(Error::ChecksumMismatch)));
        reseal(&mut corrupt);
        assert!(matches!(FlatBlock::new(&corrupt), Err(Error::InvalidData)));
        let corrupt = FlatBlock::open(&corrupt).unwrap();
        assert!(matches!(corrupt.hash_bucket(0), Err(Error::InvalidData)));
    }

    #[test]
    fn original_size_covers_the_complete_uncompressed_payload() {
        let config = FlatWriterConfig {
            column_count: 1,
            rowkey_column_count: 1,
            micro_block_size_limit: 1024,
            block_size_upper_bound: 1024,
            need_column_checksums: false,
            build_hash_index: false,
        };
        let mut writer = FlatWriter::new(config).unwrap();
        assert_eq!(0, writer.original_size().unwrap());
        writer
            .append_row(&[FlatDatum::Bytes(b"first")], info(1, 1))
            .unwrap();
        writer
            .append_row(&[FlatDatum::Bytes(b"second")], info(1, 1))
            .unwrap();
        assert_eq!(writer.row_data.len() + 12, writer.original_size().unwrap());
        assert_eq!(
            writer.block_size().unwrap() - 64,
            writer.original_size().unwrap()
        );
    }

    #[test]
    fn dynamic_upper_bound_is_honored_and_retryable() {
        let config = FlatWriterConfig {
            column_count: 1,
            rowkey_column_count: 1,
            micro_block_size_limit: 1024,
            block_size_upper_bound: 1024,
            need_column_checksums: false,
            build_hash_index: false,
        };
        let mut writer = FlatWriter::new(config).unwrap();
        writer
            .append_row(&[FlatDatum::Bytes(b"first")], info(1, 1))
            .unwrap();
        writer
            .set_block_size_upper_bound(writer.block_size().unwrap())
            .unwrap();
        let full = writer
            .append_row(&[FlatDatum::Bytes(b"second")], info(1, 1))
            .unwrap();
        assert!(full.is_full);
        assert_eq!(1, full.accepted_rows);
        writer.set_block_size_upper_bound(1024).unwrap();
        let retry = writer
            .append_row(&[FlatDatum::Bytes(b"second")], info(1, 1))
            .unwrap();
        assert!(!retry.is_full);
        assert_eq!(2, retry.accepted_rows);
    }
}
