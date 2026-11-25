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
    Block, BlockWriter, Cell, ColumnSpec, Error, FlatBlock, FlatDatum, FlatRowInfo, FlatWriter,
    FlatWriterConfig, OwnedDatum, Result, RowRange, WriterConfig,
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

    enum InputKind {
        Bytes,
        Null,
        Nop,
        UInt,
        Outrow,
    }

    enum CellKind {
        Borrowed,
        Null,
        Nop,
        UInt,
        Decoded,
        Outrow,
    }

    struct BridgeColumn {
        obj_type: u8,
        fixed_length: u32,
    }

    struct InputCell {
        kind: InputKind,
        pointer: usize,
        length: u32,
        value: u64,
    }

    struct CellDesc {
        kind: CellKind,
        offset: u64,
        length: u32,
        value: u64,
    }

    struct FlatRowMeta {
        row_flag: u8,
        multi_version_flag: u8,
        trans_id: i64,
    }

    struct FlatFinishOptionsDesc {
        single_version_rows: bool,
        contain_uncommitted_rows: bool,
        is_last_row_last_flag: bool,
        has_string_out_row: bool,
        all_lob_in_row: bool,
        has_min_merged_trans_version: bool,
        min_merged_trans_version: i64,
        max_merged_trans_version: i64,
    }

    struct AppendResult {
        code: Code,
        accepted_rows: usize,
        is_full: bool,
    }

    struct WriterStatsResult {
        code: Code,
        block_size: u64,
        original_size: u64,
        row_count: u64,
    }

    struct BuildResult {
        code: Code,
        block: Vec<u8>,
        accepted_rows: usize,
    }

    struct BatchResult {
        code: Code,
        rows: Vec<u32>,
        cells: Vec<CellDesc>,
        data: Vec<u8>,
        row_meta: Vec<FlatRowMeta>,
    }

    struct CountResult {
        code: Code,
        count: u64,
    }

    struct LocateResult {
        code: Code,
        row: u32,
        equal: bool,
    }

    struct UcaTableDesc {
        max_char: u64,
        lengths: usize,
        weights: usize,
    }

    extern "Rust" {
        type PaxWriter;
        type FlatWriter;

        fn new_pax_writer() -> Box<PaxWriter>;

        fn init_pax_writer(
            writer: &mut PaxWriter,
            columns: Vec<BridgeColumn>,
            rowkey_column_count: u32,
            row_store_type: u8,
            need_column_checksums: bool,
            block_size_limit: u64,
            minimum_rows: u64,
        ) -> Code;

        /// Cells borrow C++ datum storage only for this call. Rust copies an
        /// accepted row before returning.
        unsafe fn append_pax_row(
            writer: &mut PaxWriter,
            cells: &[InputCell],
            block_size_upper_bound: u64,
        ) -> AppendResult;

        fn finish_pax_writer(
            writer: &mut PaxWriter,
            column_checksums: &[u64],
            all_lob_in_row: bool,
            max_merged_trans_version: i64,
        ) -> BuildResult;

        fn pax_writer_stats(writer: &PaxWriter, expand_percent: u64) -> WriterStatsResult;

        fn new_flat_writer() -> Box<FlatWriter>;

        fn init_flat_writer(
            writer: &mut FlatWriter,
            column_count: u32,
            rowkey_column_count: u32,
            micro_block_size_limit: u64,
            block_size_upper_bound: u64,
            need_column_checksums: bool,
        ) -> Code;

        /// Cells borrow C++ datum storage only for this call. Rust copies
        /// accepted rows into its writer before returning.
        unsafe fn append_flat_row(
            writer: &mut FlatWriter,
            cells: &[InputCell],
            row_meta: FlatRowMeta,
            rowkey_column_count: u32,
            hash_index_size: usize,
            block_size_upper_bound: u64,
        ) -> AppendResult;

        /// The returned block owns its storage. C++ keeps it alive until the
        /// macro writer finishes consuming the block.
        fn finish_flat_writer(
            writer: &mut FlatWriter,
            options: FlatFinishOptionsDesc,
            column_checksums: &[u64],
            hash_index: &[u8],
        ) -> BuildResult;

        fn flat_writer_stats(writer: &FlatWriter, hash_index_size: usize) -> WriterStatsResult;

        /// Output offsets are relative to `block`, whose allocation and cache
        /// pin must remain valid while C++ uses the returned descriptors.
        fn project_pax_block(block: &[u8], rows: &[u32], columns: &[u32]) -> BatchResult;

        fn count_pax_non_null(block: &[u8], rows: &[u32], column: u32) -> CountResult;

        /// Missing physical columns are returned as NOP. All byte cells borrow
        /// the pinned block and remain valid only for the caller's batch use.
        fn project_flat_block(block: &[u8], rows: &[u32], columns: &[u32]) -> BatchResult;

        fn filter_project_uint(
            block: &[u8],
            filter_column: u32,
            value: u64,
            range_start: u32,
            range_end: u32,
            columns: &[u32],
        ) -> BatchResult;

        /// Key bytes and UCA pages remain valid for this call. The UCA arrays
        /// contain 256 entries and are immutable process-lifetime tables.
        unsafe fn locate_rowkey(
            block: &[u8],
            row_store_type: u8,
            keys: Vec<InputCell>,
            key_kinds: Vec<u8>,
            uca: UcaTableDesc,
            lower_bound: bool,
            range_start: u32,
            range_end: u32,
        ) -> LocateResult;
    }
}

struct PaxWriter {
    writer: Option<BlockWriter>,
}

fn new_pax_writer() -> Box<PaxWriter> {
    Box::new(PaxWriter { writer: None })
}

fn init_pax_writer(
    writer: &mut PaxWriter,
    columns: Vec<ffi::BridgeColumn>,
    rowkey_column_count: u32,
    row_store_type: u8,
    need_column_checksums: bool,
    block_size_limit: u64,
    minimum_rows: u64,
) -> ffi::Code {
    let result = (|| {
        let row_store_type = crate::RowStoreType::try_from(row_store_type)?;
        let specs = columns
            .iter()
            .map(|column| ColumnSpec {
                obj_type: column.obj_type,
                fixed_length: (column.fixed_length != 0).then_some(column.fixed_length as usize),
                enable_bit_packing: row_store_type == crate::RowStoreType::Encoding,
            })
            .collect();
        let mut config = WriterConfig::new(specs, rowkey_column_count as usize);
        config.row_store_type = row_store_type;
        config.need_column_checksums = need_column_checksums;
        config.block_size_limit =
            usize::try_from(block_size_limit).map_err(|_| Error::SizeOverflow)?;
        config.minimum_rows = usize::try_from(minimum_rows).map_err(|_| Error::SizeOverflow)?;
        BlockWriter::new(config)
    })();
    match result {
        Ok(new_writer) => {
            writer.writer = Some(new_writer);
            ffi::Code::Ok
        }
        Err(error) => {
            writer.writer = None;
            code(error)
        }
    }
}

unsafe fn append_pax_row(
    writer: &mut PaxWriter,
    cells: &[ffi::InputCell],
    block_size_upper_bound: u64,
) -> ffi::AppendResult {
    let Some(writer) = writer.writer.as_mut() else {
        return ffi::AppendResult {
            code: ffi::Code::InvalidArgument,
            accepted_rows: 0,
            is_full: false,
        };
    };
    let result = (|| {
        writer.set_block_size_upper_bound(
            usize::try_from(block_size_upper_bound).map_err(|_| Error::SizeOverflow)?,
        );
        let mut row = Vec::new();
        row.try_reserve_exact(cells.len())
            .map_err(|_| Error::SizeOverflow)?;
        for cell in cells {
            let bytes = || {
                if cell.length > 0 && cell.pointer == 0 {
                    return Err(Error::InvalidArgument);
                }
                Ok(if cell.length == 0 {
                    &[][..]
                } else {
                    std::slice::from_raw_parts(cell.pointer as *const u8, cell.length as usize)
                })
            };
            row.push(match cell.kind {
                ffi::InputKind::Bytes => OwnedDatum::Bytes(bytes()?.to_vec()),
                ffi::InputKind::Null => OwnedDatum::Null,
                ffi::InputKind::Nop => OwnedDatum::Nop,
                ffi::InputKind::UInt => OwnedDatum::UInt(cell.value),
                _ => return Err(Error::InvalidArgument),
            });
        }
        writer.append_row(&row)
    })();
    match result {
        Ok(outcome) => ffi::AppendResult {
            code: ffi::Code::Ok,
            accepted_rows: outcome.accepted_rows,
            is_full: outcome.is_full,
        },
        Err(error) => ffi::AppendResult {
            code: code(error),
            accepted_rows: writer.row_count(),
            is_full: false,
        },
    }
}

fn finish_pax_writer(
    writer: &mut PaxWriter,
    column_checksums: &[u64],
    all_lob_in_row: bool,
    max_merged_trans_version: i64,
) -> ffi::BuildResult {
    let result = (|| {
        let writer = writer.writer.as_mut().ok_or(Error::InvalidArgument)?;
        writer.set_finish_options(all_lob_in_row, max_merged_trans_version);
        let accepted_rows = writer.row_count();
        let block = writer.finish_with_column_checksums(column_checksums)?;
        Block::new(&block)?;
        Ok(BuildSuccess {
            block,
            accepted_rows,
        })
    })();
    build_result(result)
}

fn pax_writer_stats(writer: &PaxWriter, expand_percent: u64) -> ffi::WriterStatsResult {
    let result = (|| {
        let writer = writer.writer.as_ref().ok_or(Error::InvalidArgument)?;
        let expand_percent = usize::try_from(expand_percent).map_err(|_| Error::SizeOverflow)?;
        Ok((
            writer.estimated_block_size(expand_percent)?,
            writer.original_size(),
            writer.row_count(),
        ))
    })();
    match result {
        Ok((block_size, original_size, row_count)) => ffi::WriterStatsResult {
            code: ffi::Code::Ok,
            block_size: block_size as u64,
            original_size: original_size as u64,
            row_count: row_count as u64,
        },
        Err(error) => ffi::WriterStatsResult {
            code: code(error),
            block_size: 0,
            original_size: 0,
            row_count: 0,
        },
    }
}

fn new_flat_writer() -> Box<FlatWriter> {
    Box::new(FlatWriter::default())
}

fn init_flat_writer(
    writer: &mut FlatWriter,
    column_count: u32,
    rowkey_column_count: u32,
    micro_block_size_limit: u64,
    block_size_upper_bound: u64,
    need_column_checksums: bool,
) -> ffi::Code {
    let config = (|| {
        Ok(FlatWriterConfig {
            column_count: column_count as usize,
            rowkey_column_count: rowkey_column_count as usize,
            micro_block_size_limit: usize::try_from(micro_block_size_limit)
                .map_err(|_| Error::SizeOverflow)?,
            block_size_upper_bound: usize::try_from(block_size_upper_bound)
                .map_err(|_| Error::SizeOverflow)?,
            need_column_checksums,
        })
    })();
    match config.and_then(FlatWriter::new) {
        Ok(new_writer) => {
            *writer = new_writer;
            ffi::Code::Ok
        }
        Err(error) => code(error),
    }
}

unsafe fn append_flat_row(
    writer: &mut FlatWriter,
    cells: &[ffi::InputCell],
    row_meta: ffi::FlatRowMeta,
    rowkey_column_count: u32,
    hash_index_size: usize,
    block_size_upper_bound: u64,
) -> ffi::AppendResult {
    let result = (|| {
        writer.set_block_size_upper_bound(
            usize::try_from(block_size_upper_bound).map_err(|_| Error::SizeOverflow)?,
        )?;
        let mut datums = Vec::new();
        datums
            .try_reserve_exact(cells.len())
            .map_err(|_| Error::SizeOverflow)?;
        for cell in cells {
            let bytes = || {
                if cell.length > 0 && cell.pointer == 0 {
                    return Err(Error::InvalidArgument);
                }
                Ok(if cell.length == 0 {
                    &[][..]
                } else {
                    std::slice::from_raw_parts(cell.pointer as *const u8, cell.length as usize)
                })
            };
            let datum = match cell.kind {
                ffi::InputKind::Bytes => FlatDatum::Bytes(bytes()?),
                ffi::InputKind::Null => FlatDatum::Null,
                ffi::InputKind::Nop => FlatDatum::Nop,
                ffi::InputKind::Outrow => FlatDatum::Outrow(bytes()?),
                _ => return Err(Error::InvalidArgument),
            };
            datums.push(datum);
        }
        writer.append_row(
            &datums,
            FlatRowInfo {
                row_flag: row_meta.row_flag,
                multi_version_flag: row_meta.multi_version_flag,
                rowkey_column_count: rowkey_column_count as usize,
                column_count: cells.len(),
                trans_id: row_meta.trans_id,
            },
            hash_index_size,
        )
    })();
    match result {
        Ok(outcome) => ffi::AppendResult {
            code: ffi::Code::Ok,
            accepted_rows: outcome.accepted_rows,
            is_full: outcome.is_full,
        },
        Err(error) => ffi::AppendResult {
            code: code(error),
            accepted_rows: writer.row_count(),
            is_full: false,
        },
    }
}

fn finish_flat_writer(
    writer: &mut FlatWriter,
    options: ffi::FlatFinishOptionsDesc,
    column_checksums: &[u64],
    hash_index: &[u8],
) -> ffi::BuildResult {
    let accepted_rows = writer.row_count();
    let result = writer.finish(
        crate::FlatFinishOptions {
            single_version_rows: options.single_version_rows,
            contain_uncommitted_rows: options.contain_uncommitted_rows,
            is_last_row_last_flag: options.is_last_row_last_flag,
            has_string_out_row: options.has_string_out_row,
            all_lob_in_row: options.all_lob_in_row,
            has_min_merged_trans_version: options.has_min_merged_trans_version,
            min_merged_trans_version: options.min_merged_trans_version,
            max_merged_trans_version: options.max_merged_trans_version,
        },
        column_checksums,
        hash_index,
    );
    match result {
        Ok(block) => ffi::BuildResult {
            code: ffi::Code::Ok,
            block,
            accepted_rows,
        },
        Err(error) => build_result(Err(error)),
    }
}

fn flat_writer_stats(writer: &FlatWriter, hash_index_size: usize) -> ffi::WriterStatsResult {
    match writer.block_size(hash_index_size).and_then(|block_size| {
        writer
            .original_size(hash_index_size)
            .map(|original_size| (block_size, original_size))
    }) {
        Ok((block_size, original_size)) => ffi::WriterStatsResult {
            code: ffi::Code::Ok,
            block_size: block_size as u64,
            original_size: original_size as u64,
            row_count: writer.row_count() as u64,
        },
        Err(error) => ffi::WriterStatsResult {
            code: code(error),
            block_size: 0,
            original_size: 0,
            row_count: 0,
        },
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
            accepted_rows: success.accepted_rows,
        },
        Err(error) => ffi::BuildResult {
            code: code(error),
            block: Vec::new(),
            accepted_rows: 0,
        },
    }
}

struct BuildSuccess {
    block: Vec<u8>,
    accepted_rows: usize,
}

fn batch_result(result: Result<(Vec<u32>, Vec<ffi::CellDesc>, Vec<u8>)>) -> ffi::BatchResult {
    match result {
        Ok((rows, cells, data)) => ffi::BatchResult {
            code: ffi::Code::Ok,
            rows,
            cells,
            data,
            row_meta: Vec::new(),
        },
        Err(error) => ffi::BatchResult {
            code: code(error),
            rows: Vec::new(),
            cells: Vec::new(),
            data: Vec::new(),
            row_meta: Vec::new(),
        },
    }
}

fn describe_cells(
    block: &[u8],
    projection: crate::Projection<'_>,
) -> (Vec<ffi::CellDesc>, Vec<u8>) {
    let (cells, data) = projection.into_parts();
    let descriptions = cells
        .iter()
        .map(|cell| match cell {
            crate::read::ProjectionCell::Borrowed(cell) => match cell {
                Cell::Null => ffi::CellDesc {
                    kind: ffi::CellKind::Null,
                    offset: 0,
                    length: 0,
                    value: 0,
                },
                Cell::Nop => ffi::CellDesc {
                    kind: ffi::CellKind::Nop,
                    offset: 0,
                    length: 0,
                    value: 0,
                },
                Cell::Bytes(bytes) => ffi::CellDesc {
                    kind: ffi::CellKind::Borrowed,
                    offset: (bytes.as_ptr() as usize - block.as_ptr() as usize) as u64,
                    length: bytes.len() as u32,
                    value: 0,
                },
                Cell::Outrow(bytes) => ffi::CellDesc {
                    kind: ffi::CellKind::Outrow,
                    offset: (bytes.as_ptr() as usize - block.as_ptr() as usize) as u64,
                    length: bytes.len() as u32,
                    value: 0,
                },
                Cell::UInt(value) => ffi::CellDesc {
                    kind: ffi::CellKind::UInt,
                    offset: 0,
                    length: 0,
                    value: *value,
                },
            },
            crate::read::ProjectionCell::Decoded { offset, length } => ffi::CellDesc {
                kind: ffi::CellKind::Decoded,
                offset: *offset as u64,
                length: *length as u32,
                value: 0,
            },
        })
        .collect();
    (descriptions, data)
}

fn project_pax_block(block: &[u8], rows: &[u32], columns: &[u32]) -> ffi::BatchResult {
    batch_result((|| {
        let parsed = Block::new(block)?;
        let projection = parsed.project(rows, columns)?;
        let (cells, data) = describe_cells(block, projection);
        Ok((rows.to_vec(), cells, data))
    })())
}

fn count_pax_non_null(block: &[u8], rows: &[u32], column: u32) -> ffi::CountResult {
    match Block::new(block).and_then(|parsed| parsed.count_non_null(rows, column as usize)) {
        Ok(count) => ffi::CountResult {
            code: ffi::Code::Ok,
            count: count as u64,
        },
        Err(error) => ffi::CountResult {
            code: code(error),
            count: 0,
        },
    }
}

fn project_flat_block(block: &[u8], rows: &[u32], columns: &[u32]) -> ffi::BatchResult {
    let result = (|| {
        let parsed = FlatBlock::new(block)?;
        let (projection, row_info) = parsed.project(rows, columns)?;
        let (cells, data) = describe_cells(block, projection);
        let row_meta = row_info
            .into_iter()
            .map(|info| ffi::FlatRowMeta {
                row_flag: info.row_flag,
                multi_version_flag: info.multi_version_flag,
                trans_id: info.trans_id,
            })
            .collect();
        Ok((rows.to_vec(), cells, data, row_meta))
    })();
    match result {
        Ok((rows, cells, data, row_meta)) => ffi::BatchResult {
            code: ffi::Code::Ok,
            rows,
            cells,
            data,
            row_meta,
        },
        Err(error) => batch_result(Err(error)),
    }
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
        let (cells, data) = describe_cells(block, projection);
        Ok((rows, cells, data))
    })())
}

fn locate_result(result: Result<crate::RowkeyBound>) -> ffi::LocateResult {
    match result {
        Ok(bound) => ffi::LocateResult {
            code: ffi::Code::Ok,
            row: bound.row as u32,
            equal: bound.equal,
        },
        Err(error) => ffi::LocateResult {
            code: code(error),
            row: u32::MAX,
            equal: false,
        },
    }
}

struct CppUcaTable {
    desc: ffi::UcaTableDesc,
}

impl crate::UcaTable for CppUcaTable {
    fn max_char(&self) -> u32 {
        self.desc.max_char as u32
    }

    fn page_length(&self, page: usize) -> Option<usize> {
        if page < 256 && self.desc.lengths != 0 {
            Some(unsafe { *(self.desc.lengths as *const u8).add(page) } as usize)
        } else {
            None
        }
    }

    fn page(&self, page: usize) -> Option<&[u16]> {
        if page >= 256 || self.desc.weights == 0 {
            return None;
        }
        let address = unsafe { *(self.desc.weights as *const usize).add(page) };
        let length = self.page_length(page)?.checked_mul(256)?;
        if address == 0 || length == 0 {
            None
        } else {
            Some(unsafe { core::slice::from_raw_parts(address as *const u16, length) })
        }
    }
}

fn locate_rowkey(
    block: &[u8],
    row_store_type: u8,
    keys: Vec<ffi::InputCell>,
    key_kinds: Vec<u8>,
    desc: ffi::UcaTableDesc,
    lower_bound: bool,
    range_start: u32,
    range_end: u32,
) -> ffi::LocateResult {
    locate_result((|| {
        if keys.len() != key_kinds.len() {
            return Err(Error::InvalidArgument);
        }
        let mut values = Vec::new();
        let mut kinds = Vec::new();
        values
            .try_reserve_exact(keys.len())
            .map_err(|_| Error::SizeOverflow)?;
        kinds
            .try_reserve_exact(key_kinds.len())
            .map_err(|_| Error::SizeOverflow)?;
        for (cell, kind) in keys.iter().zip(key_kinds) {
            let value = match kind {
                0 => {
                    if cell.kind != ffi::InputKind::Bytes || cell.pointer == 0 {
                        return Err(Error::InvalidArgument);
                    }
                    crate::RowkeyValue::Bytes(unsafe {
                        core::slice::from_raw_parts(cell.pointer as *const u8, cell.length as usize)
                    })
                }
                1 | 2 if cell.kind != ffi::InputKind::UInt => {
                    return Err(Error::InvalidArgument);
                }
                1 => crate::RowkeyValue::Int(cell.value as i64),
                2 => crate::RowkeyValue::UInt(cell.value),
                _ => return Err(Error::Unsupported),
            };
            values.push(value);
            kinds.push(match kind {
                0 => crate::RowkeyKind::UnicodeCi,
                1 => crate::RowkeyKind::Int,
                2 => crate::RowkeyKind::UInt,
                _ => unreachable!(),
            });
        }
        let range = crate::RowRange::new(range_start as usize, range_end as usize)?;
        match row_store_type {
            crate::format::FLAT_ROW_STORE => FlatBlock::new(block)?.find_rowkey_bound(
                &values,
                &kinds,
                &CppUcaTable { desc },
                lower_bound,
                range,
            ),
            crate::format::ENCODING_ROW_STORE | crate::format::SELECTIVE_ENCODING_ROW_STORE => {
                Block::new(block)?.find_rowkey_bound(
                    &values,
                    &kinds,
                    &CppUcaTable { desc },
                    lower_bound,
                    range,
                )
            }
            _ => Err(Error::Unsupported),
        }
    })())
}
