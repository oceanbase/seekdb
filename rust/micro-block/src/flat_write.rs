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
    header_checksum, write_u16, write_u32, write_u64, Header, ALL_LOB_IN_ROW,
    CONTAINS_HASH_INDEX, FIXED_HEADER_SIZE, FLAT_ROW_STORE, HAS_COLUMN_CHECKSUM,
    HASH_INDEX_OFFSET_SHIFT, MICRO_BLOCK_MAGIC, MICRO_BLOCK_VERSION,
};
use crate::{AppendOutcome, Error, FlatRowInfo, Result};

const ROW_HEADER_SIZE: usize = 16;
const CLUSTER_COLUMNS: usize = 32;
const ROWKEY_INDEPENDENT_MAX: usize = 48;
const MAX_CLUSTER_COLUMN_COUNT: usize = 256;
const MAX_SPARSE_COLUMNS: usize = 3;
const SPARSE_NOP_THRESHOLD: usize = CLUSTER_COLUMNS - MAX_SPARSE_COLUMNS;
const HAS_STRING_OUT_ROW: u16 = 1 << 1;
const HAS_MIN_MERGED_TRANS_VERSION: u16 = 1 << 14;
const VAL_NORMAL: u8 = 0;
const VAL_OUTROW: u8 = 1;
const VAL_NOP: u8 = 2;
const VAL_NULL: u8 = 3;
const VAL_ENCODING_NORMAL: u8 = 4;

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
            },
            row_data: Vec::new(),
            row_offsets: vec![0],
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
            frozen: false,
        })
    }

    pub fn row_count(&self) -> usize {
        self.row_offsets.len() - 1
    }

    pub fn original_size(&self) -> usize {
        self.row_data.len()
    }

    pub fn block_size(&self, hash_index_size: usize) -> Result<usize> {
        block_size(
            self.config,
            self.row_data.len(),
            self.row_count(),
            hash_index_size,
        )
    }

    pub fn append_row(
        &mut self,
        cells: &[FlatDatum<'_>],
        info: FlatRowInfo,
        hash_index_size: usize,
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
        let new_size = block_size(self.config, new_data_size, accepted_rows + 1, hash_index_size)?;
        if (accepted_rows > 0 && new_size > self.config.micro_block_size_limit)
            || new_size > self.config.block_size_upper_bound
        {
            return Ok(AppendOutcome {
                accepted_rows,
                is_full: true,
            });
        }
        self.row_data
            .try_reserve(encoded.len())
            .map_err(|_| Error::SizeOverflow)?;
        self.row_offsets
            .try_reserve(1)
            .map_err(|_| Error::SizeOverflow)?;
        self.row_data.extend_from_slice(&encoded);
        self.row_offsets.push(
            u32::try_from(new_data_size).map_err(|_| Error::SizeOverflow)?,
        );
        Ok(AppendOutcome {
            accepted_rows: accepted_rows + 1,
            is_full: false,
        })
    }

    pub fn finish(
        &mut self,
        options: FlatFinishOptions,
        column_checksums: &[u64],
        hash_index: &[u8],
    ) -> Result<Vec<u8>> {
        if self.frozen {
            return Err(Error::Frozen);
        }
        if self.row_count() == 0
            || (self.config.need_column_checksums
                && column_checksums.len() != self.config.column_count)
            || (!self.config.need_column_checksums && !column_checksums.is_empty())
            || hash_index.len() > 0x3ff
        {
            return Err(Error::InvalidArgument);
        }
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
        let opt = u8::from(options.single_version_rows)
            | (u8::from(options.contain_uncommitted_rows) << 1)
            | (u8::from(options.is_last_row_last_flag) << 2);
        let total_size = self.block_size(hash_index.len())?;
        let data_length = total_size - header_size;
        let mut header = Header {
            header_size,
            column_count: self.config.column_count,
            rowkey_column_count: self.config.rowkey_column_count,
            flags,
            row_count: self.row_count(),
            row_store_type: FLAT_ROW_STORE,
            opt,
            var_column_count: 0,
            row_data_offset: row_index_offset,
            original_length: i32::try_from(self.row_data.len()).map_err(|_| Error::SizeOverflow)?,
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
        write_u16(&mut block, 0, MICRO_BLOCK_MAGIC)?;
        write_u16(&mut block, 2, MICRO_BLOCK_VERSION)?;
        write_u32(&mut block, 4, header_size as u32)?;
        write_u16(&mut block, 10, self.config.column_count as u16)?;
        write_u16(&mut block, 12, self.config.rowkey_column_count as u16)?;
        write_u16(&mut block, 14, flags)?;
        write_u32(&mut block, 16, self.row_count() as u32)?;
        block[20] = FLAT_ROW_STORE;
        block[21] = opt;
        write_u32(&mut block, 24, row_index_offset as u32)?;
        write_u32(&mut block, 28, header.original_length as u32)?;
        write_u64(
            &mut block,
            32,
            options.max_merged_trans_version as u64,
        )?;
        write_u32(&mut block, 40, data_length as u32)?;
        write_u32(&mut block, 44, data_length as u32)?;
        if self.config.need_column_checksums {
            for (column, checksum) in column_checksums.iter().enumerate() {
                write_u64(&mut block, FIXED_HEADER_SIZE + column * 8, *checksum)?;
            }
        } else if options.has_min_merged_trans_version {
            write_u64(&mut block, 56, options.min_merged_trans_version as u64)?;
        }
        let mut position = header_size;
        block[position..position + self.row_data.len()].copy_from_slice(&self.row_data);
        position += self.row_data.len();
        for offset in &self.row_offsets {
            write_u32(&mut block, position, *offset)?;
            position += 4;
        }
        block[position..].copy_from_slice(hash_index);
        header.data_checksum = crc32c(0, &block[header_size..]);
        write_u64(&mut block, 48, header.data_checksum)?;
        let checksum = header_checksum(&block, &header);
        write_u16(&mut block, 8, checksum)?;
        self.frozen = true;
        Ok(block)
    }
}

fn header_size(config: FlatWriterConfig) -> Result<usize> {
    FIXED_HEADER_SIZE
        .checked_add(if config.need_column_checksums {
            config
                .column_count
                .checked_mul(8)
                .ok_or(Error::SizeOverflow)?
        } else {
            0
        })
        .ok_or(Error::SizeOverflow)
}

fn block_size(
    config: FlatWriterConfig,
    data_size: usize,
    row_count: usize,
    hash_index_size: usize,
) -> Result<usize> {
    let index_size = row_count
        .checked_add(1)
        .and_then(|count| count.checked_mul(4))
        .ok_or(Error::SizeOverflow)?;
    header_size(config)?
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
    if cells.len() - nop_count <= MAX_SPARSE_COLUMNS
        && cells.len() < MAX_CLUSTER_COLUMN_COUNT
    {
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
    row.push(if single_cluster { u8::MAX } else { cells.len() as u8 });
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
                if bytes.len() == 8
                    && integer_bytes(u64::from_le_bytes((*bytes).try_into().unwrap())) < 8
                {
                    return Err(Error::Unsupported);
                }
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
        | if sparse { (serialized_count as u8) << 4 } else { 0 };
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
    use super::{FlatDatum, FlatFinishOptions, FlatWriter, FlatWriterConfig};
    use crate::{FlatBlock, FlatRowInfo};

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

    fn write_single(cells: &[FlatDatum<'_>], rowkey_column_count: usize) -> Vec<u8> {
        let config = FlatWriterConfig {
            column_count: cells.len(),
            rowkey_column_count,
            micro_block_size_limit: usize::MAX,
            block_size_upper_bound: usize::MAX,
            need_column_checksums: false,
        };
        let mut writer = FlatWriter::new(config).unwrap();
        let outcome = writer
            .append_row(cells, info(cells.len(), rowkey_column_count), 0)
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
                &[],
            )
            .unwrap()
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
        };
        let mut writer = FlatWriter::new(config).unwrap();
        assert!(!writer.append_row(&cells, info(1, 1), 0).unwrap().is_full);
        let full = writer.append_row(&cells, info(1, 1), 0).unwrap();
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
                &[],
            )
            .unwrap();
        let parsed = FlatBlock::new(&block).unwrap();
        assert_eq!(1, parsed.row_count());
        assert_eq!(crate::Cell::Bytes(b"row"), parsed.cell(0, 0).unwrap());
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
        };
        let mut writer = FlatWriter::new(config).unwrap();
        writer.append_row(&cells, info(1, 1), 3).unwrap();
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
                &[0, 2, 0xff],
            )
            .unwrap();
        let parsed = FlatBlock::new(&block).unwrap();
        assert_eq!(crate::Cell::Bytes(b"row"), parsed.cell(0, 0).unwrap());
    }
}
