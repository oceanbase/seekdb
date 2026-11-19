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

use crate::{
    Block, BlockWriter, Cell, ColumnSpec, Error, OwnedDatum, Result, RowRange, WriterConfig,
};

#[cxx::bridge(namespace = "oceanbase::blocksstable::micro_block")]
mod ffi {
    enum Code {
        Ok,
        InvalidArgument,
        InvalidData,
        ChecksumMismatch,
        Unsupported,
        OutputTooSmall,
        SizeOverflow,
        Frozen,
    }

    struct BridgeColumn {
        obj_type: u8,
        fixed_length: u32,
        enable_bit_packing: bool,
    }

    struct InputCell {
        kind: u8,
        pointer: usize,
        length: u32,
        value: u64,
    }

    struct CellDesc {
        kind: u8,
        offset: u64,
        length: u32,
        value: u64,
    }

    struct BuildResult {
        code: Code,
        block: Vec<u8>,
        column_types: Vec<u8>,
        accepted_rows: usize,
    }

    struct BatchResult {
        code: Code,
        rows: Vec<u32>,
        cells: Vec<CellDesc>,
    }

    struct LocateResult {
        code: Code,
        row: u32,
    }

    extern "Rust" {
        /// Every non-null pointer references `length` readable bytes for the
        /// duration of this call. Cells are row-major and owned by Rust for
        /// the duration of this call.
        unsafe fn build_pax_block(
            columns: Vec<BridgeColumn>,
            cells: Vec<InputCell>,
            rowkey_column_count: u32,
            need_column_checksums: bool,
            original_size: u64,
            max_merged_trans_version: i64,
        ) -> BuildResult;

        /// Output offsets are relative to `block`, whose allocation and cache
        /// pin must remain valid while C++ uses the returned descriptors.
        fn project_pax_block(block: &[u8], rows: &[u32], columns: &[u32]) -> BatchResult;

        fn filter_project_uint(
            block: &[u8],
            filter_column: u32,
            value: u64,
            range_start: u32,
            range_end: u32,
            columns: &[u32],
        ) -> BatchResult;

        fn locate_uint(block: &[u8], column: u32, value: u64) -> LocateResult;

        fn locate_int(block: &[u8], column: u32, value: i64) -> LocateResult;
    }
}

fn code(error: Error) -> ffi::Code {
    match error {
        Error::InvalidArgument => ffi::Code::InvalidArgument,
        Error::InvalidData => ffi::Code::InvalidData,
        Error::ChecksumMismatch => ffi::Code::ChecksumMismatch,
        Error::Unsupported => ffi::Code::Unsupported,
        Error::OutputTooSmall => ffi::Code::OutputTooSmall,
        Error::SizeOverflow => ffi::Code::SizeOverflow,
        Error::Frozen => ffi::Code::Frozen,
    }
}

fn build_result(result: Result<BuildSuccess>) -> ffi::BuildResult {
    match result {
        Ok(success) => ffi::BuildResult {
            code: ffi::Code::Ok,
            block: success.block,
            column_types: success.column_types,
            accepted_rows: success.accepted_rows,
        },
        Err(error) => ffi::BuildResult {
            code: code(error),
            block: Vec::new(),
            column_types: Vec::new(),
            accepted_rows: 0,
        },
    }
}

struct BuildSuccess {
    block: Vec<u8>,
    column_types: Vec<u8>,
    accepted_rows: usize,
}

unsafe fn build_pax_block(
    columns: Vec<ffi::BridgeColumn>,
    cells: Vec<ffi::InputCell>,
    rowkey_column_count: u32,
    need_column_checksums: bool,
    original_size: u64,
    max_merged_trans_version: i64,
) -> ffi::BuildResult {
    build_result(build_pax_block_impl(
        &columns,
        &cells,
        rowkey_column_count,
        need_column_checksums,
        original_size,
        max_merged_trans_version,
    ))
}

unsafe fn build_pax_block_impl(
    columns: &[ffi::BridgeColumn],
    cells: &[ffi::InputCell],
    rowkey_column_count: u32,
    need_column_checksums: bool,
    original_size: u64,
    max_merged_trans_version: i64,
) -> Result<BuildSuccess> {
    if columns.is_empty() || cells.len() % columns.len() != 0 {
        return Err(Error::InvalidArgument);
    }
    let specs = columns
        .iter()
        .map(|column| ColumnSpec {
            obj_type: column.obj_type,
            fixed_length: (column.fixed_length != 0).then_some(column.fixed_length as usize),
            enable_bit_packing: column.enable_bit_packing,
        })
        .collect();
    let mut config = WriterConfig::new(specs, rowkey_column_count as usize);
    config.need_column_checksums = need_column_checksums;
    config.max_merged_trans_version = max_merged_trans_version;
    config.original_size_override =
        Some(usize::try_from(original_size).map_err(|_| Error::SizeOverflow)?);
    config.block_size_limit = usize::MAX;
    let mut writer = BlockWriter::new(config)?;
    for input_row in cells.chunks_exact(columns.len()) {
        let mut row = Vec::new();
        row.try_reserve_exact(columns.len())
            .map_err(|_| Error::SizeOverflow)?;
        for (cell, column) in input_row.iter().zip(columns) {
            let datum = match cell.kind {
                0 => {
                    if cell.length > 0 && cell.pointer == 0 {
                        return Err(Error::InvalidArgument);
                    }
                    let bytes = if cell.length == 0 {
                        &[][..]
                    } else {
                        std::slice::from_raw_parts(cell.pointer as *const u8, cell.length as usize)
                    };
                    OwnedDatum::Bytes(bytes.to_vec())
                }
                1 => OwnedDatum::Null,
                2 => OwnedDatum::Nop,
                3 => OwnedDatum::UInt(cell.value),
                _ => return Err(Error::InvalidArgument),
            };
            if column.fixed_length == 0 && matches!(datum, OwnedDatum::UInt(_)) {
                return Err(Error::InvalidArgument);
            }
            row.push(datum);
        }
        let outcome = writer.append_row(&row)?;
        if outcome.is_full {
            return Err(Error::OutputTooSmall);
        }
    }
    let accepted_rows = writer.row_count();
    let block = writer.finish()?;
    let parsed = Block::new(&block)?;
    let column_types = (0..parsed.column_count())
        .map(|column| {
            block[parsed.header.header_size + column * crate::format::COLUMN_HEADER_SIZE + 1]
        })
        .collect();
    Ok(BuildSuccess {
        block,
        column_types,
        accepted_rows,
    })
}

fn batch_result(result: Result<(Vec<u32>, Vec<ffi::CellDesc>)>) -> ffi::BatchResult {
    match result {
        Ok((rows, cells)) => ffi::BatchResult {
            code: ffi::Code::Ok,
            rows,
            cells,
        },
        Err(error) => ffi::BatchResult {
            code: code(error),
            rows: Vec::new(),
            cells: Vec::new(),
        },
    }
}

fn describe_cells(block: &[u8], projection: crate::Projection<'_>) -> Vec<ffi::CellDesc> {
    projection
        .cells()
        .iter()
        .map(|cell| match cell {
            Cell::Null => ffi::CellDesc {
                kind: 1,
                offset: 0,
                length: 0,
                value: 0,
            },
            Cell::Nop => ffi::CellDesc {
                kind: 2,
                offset: 0,
                length: 0,
                value: 0,
            },
            Cell::Bytes(bytes) => ffi::CellDesc {
                kind: 0,
                offset: (bytes.as_ptr() as usize - block.as_ptr() as usize) as u64,
                length: bytes.len() as u32,
                value: 0,
            },
            Cell::UInt(value) => ffi::CellDesc {
                kind: 3,
                offset: 0,
                length: 0,
                value: *value,
            },
        })
        .collect()
}

fn project_pax_block(block: &[u8], rows: &[u32], columns: &[u32]) -> ffi::BatchResult {
    batch_result((|| {
        let parsed = Block::new(block)?;
        let projection = parsed.project(rows, columns)?;
        let cells = describe_cells(block, projection);
        Ok((rows.to_vec(), cells))
    })())
}

fn filter_project_uint(
    block: &[u8],
    filter_column: u32,
    value: u64,
    range_start: u32,
    range_end: u32,
    columns: &[u32],
) -> ffi::BatchResult {
    batch_result((|| {
        let block_ref = Block::new(block)?;
        let rows = block_ref.filter_uint_eq(
            filter_column as usize,
            value,
            RowRange::new(range_start as usize, range_end as usize)?,
        )?;
        let projection = block_ref.project(&rows, columns)?;
        let cells = describe_cells(block, projection);
        Ok((rows, cells))
    })())
}

fn locate_result(result: Result<Option<u32>>) -> ffi::LocateResult {
    match result {
        Ok(Some(row)) => ffi::LocateResult {
            code: ffi::Code::Ok,
            row,
        },
        Ok(None) => ffi::LocateResult {
            code: ffi::Code::Ok,
            row: u32::MAX,
        },
        Err(error) => ffi::LocateResult {
            code: code(error),
            row: u32::MAX,
        },
    }
}

fn locate_uint(block: &[u8], column: u32, value: u64) -> ffi::LocateResult {
    locate_result(Block::new(block).and_then(|block| block.find_uint(column as usize, value)))
}

fn locate_int(block: &[u8], column: u32, value: i64) -> ffi::LocateResult {
    locate_result(Block::new(block).and_then(|block| block.find_int(column as usize, value)))
}
