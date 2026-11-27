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
    self, Header, ALL_LOB_IN_ROW, CONTAINS_HASH_INDEX, CONTAIN_UNCOMMITTED_ROWS, FLAT_ROW_STORE,
    HAS_STRING_OUT_ROW, IS_LAST_ROW_LAST_FLAG, SINGLE_VERSION_ROWS,
};
use crate::{
    Block, BlockWriter, BorrowedDatum, Cell, ColumnSpec, Error, FlatBlock, FlatDatum, FlatRowInfo,
    FlatWriter, FlatWriterConfig, Result, RowRange, WriterConfig,
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

    #[derive(Default)]
    struct RecordMetadata {
        header_size: u32,
        column_count: u16,
        rowkey_column_count: u16,
        row_count: u32,
        row_store_type: u8,
        original_length: i32,
        max_merged_trans_version: i64,
        data_length: u32,
        data_zlength: u32,
        data_checksum: u64,
        has_column_checksum: bool,
        has_string_out_row: bool,
        all_lob_in_row: bool,
        contains_hash_index: bool,
        has_min_merged_trans_version: bool,
        min_merged_trans_version: i64,
        single_version_rows: bool,
        contain_uncommitted_rows: bool,
        is_last_row_last_flag: bool,
    }

    struct RecordResult {
        code: Code,
        metadata: RecordMetadata,
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
        obj_type: u8,
        offset: u64,
        length: u32,
        value: u64,
    }

    struct FlatCellResult {
        code: Code,
        cell: CellDesc,
    }

    enum FlatHashKind {
        Vacant,
        Collision,
        Row,
    }

    enum FlatHashWriteState {
        Disabled,
        Empty,
        Populated,
    }

    struct FlatAppendResult {
        code: Code,
        accepted_rows: usize,
        is_full: bool,
        hash_state: FlatHashWriteState,
    }

    struct FlatHashResult {
        code: Code,
        kind: FlatHashKind,
        row: u8,
    }

    struct FlatRowMeta {
        row_flag: u8,
        multi_version_flag: u8,
        trans_id: i64,
    }

    struct FlatRowMetaResult {
        code: Code,
        meta: FlatRowMeta,
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

    enum RowkeySearch {
        LowerBound,
        UpperBound,
        Compare,
        EqualRunEnd,
    }

    struct LocateResult {
        code: Code,
        compare_error: i32,
        ordering: i32,
        row: u32,
        equal: bool,
    }

    struct PaxWriterConfig {
        rowkey_column_count: u32,
        row_store_type: u8,
        need_column_checksums: bool,
        micro_block_size: u64,
        macro_block_size: u64,
        minimum_data_limit: u64,
        expand_percent: u64,
        minimum_rows: u64,
        encoding_granularity: u64,
    }

    unsafe extern "C++" {
        include!("rust/micro-block/include/rowkey_comparator.h");
        type RowkeyComparator;

        fn run(
            self: &RowkeyComparator,
            cells: &[CellDesc],
            decoded: &[u8],
            ordering: &mut i32,
        ) -> i32;
    }

    extern "Rust" {
        fn read_record_header(bytes: &[u8]) -> RecordResult;
        fn read_expanded_record_header(bytes: &[u8]) -> RecordResult;
        fn check_record_header(bytes: &[u8]) -> RecordResult;
        fn check_stored_record(bytes: &[u8]) -> RecordResult;
        fn check_stored_record_prefix(bytes: &[u8]) -> RecordResult;
        fn record_header_size(column_count: u16, has_column_checksum: bool) -> u32;
        fn accumulate_record_column_checksums(prefix: &[u8], sums: &mut [i64]) -> Code;
        fn rewrite_record_column_checksums(
            out: &mut [u8],
            source_prefix: &[u8],
            column_count: u16,
            has_column_checksum: bool,
            checksums: &[u64],
        ) -> Code;
        fn reseal_record_header_in_place(out: &mut [u8], stored_payload: &[u8]) -> RecordResult;

        type PaxWriter;
        type FlatWriter;

        fn new_pax_writer() -> Box<PaxWriter>;

        fn init_pax_writer(
            writer: &mut PaxWriter,
            columns: Vec<BridgeColumn>,
            options: PaxWriterConfig,
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

        fn pax_writer_row_count(writer: &PaxWriter) -> u64;
        fn pax_writer_header_size(writer: &PaxWriter) -> u64;
        fn pax_writer_stats(writer: &PaxWriter, expand_percent: u64) -> WriterStatsResult;

        fn new_flat_writer() -> Box<FlatWriter>;

        fn init_flat_writer(
            writer: &mut FlatWriter,
            column_count: u32,
            rowkey_column_count: u32,
            micro_block_size_limit: u64,
            block_size_upper_bound: u64,
            need_column_checksums: bool,
            build_hash_index: bool,
        ) -> Code;

        /// Cells borrow C++ datum storage only for this call. Rust copies
        /// accepted rows into its writer before returning.
        unsafe fn append_flat_row(
            writer: &mut FlatWriter,
            cells: &[InputCell],
            row_meta: FlatRowMeta,
            rowkey_column_count: u32,
            block_size_upper_bound: u64,
        ) -> FlatAppendResult;

        fn add_flat_hash_entry(writer: &mut FlatWriter, row: usize, hash: u64) -> Code;
        fn disable_flat_hash_index(writer: &mut FlatWriter);

        /// The returned block owns its storage. C++ keeps it alive until the
        /// macro writer finishes consuming the block.
        fn finish_flat_writer(
            writer: &mut FlatWriter,
            options: FlatFinishOptionsDesc,
            column_checksums: &[u64],
        ) -> BuildResult;

        fn flat_writer_stats(writer: &FlatWriter) -> WriterStatsResult;
        fn flat_writer_empty_size(column_count: usize, need_column_checksums: bool) -> u64;

        /// Output offsets are relative to `block`, whose allocation and cache
        /// pin must remain valid while C++ uses the returned descriptors.
        fn project_pax_block(block: &[u8], rows: &[u32], columns: &[u32]) -> BatchResult;

        fn count_pax_non_null(block: &[u8], rows: &[u32], column: u32) -> CountResult;

        /// Missing physical columns are returned as NOP. All byte cells borrow
        /// the pinned block and remain valid only for the caller's batch use.
        fn project_flat_block(block: &[u8], rows: &[u32], columns: &[u32]) -> BatchResult;
        fn read_flat_row_meta(block: &[u8], row: u32) -> FlatRowMetaResult;
        fn read_flat_cell(block: &[u8], row: u32, column: u32) -> FlatCellResult;
        fn lookup_flat_hash(block: &[u8], hash: u64) -> FlatHashResult;

        fn filter_project_uint(
            block: &[u8],
            filter_column: u32,
            value: u64,
            range_start: u32,
            range_end: u32,
            columns: &[u32],
        ) -> BatchResult;

        /// The comparator and block are borrowed only for this call. A
        /// negative comparison orders the query before the stored row.
        fn locate_rowkey_by(
            block: &[u8],
            row_store_type: u8,
            column_count: u32,
            search: RowkeySearch,
            range_start: u32,
            range_end: u32,
            comparator: &RowkeyComparator,
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
    options: ffi::PaxWriterConfig,
) -> ffi::Code {
    let result = (|| {
        let row_store_type = crate::RowStoreType::try_from(options.row_store_type)?;
        let specs = columns
            .iter()
            .map(|column| ColumnSpec {
                obj_type: column.obj_type,
                fixed_length: (column.fixed_length != 0).then_some(column.fixed_length as usize),
                enable_bit_packing: row_store_type == crate::RowStoreType::Encoding,
            })
            .collect();
        let to_size = |value| usize::try_from(value).map_err(|_| Error::SizeOverflow);
        let mut config = WriterConfig::new(specs, options.rowkey_column_count as usize);
        config.row_store_type = row_store_type;
        config.need_column_checksums = options.need_column_checksums;
        config.minimum_rows = to_size(options.minimum_rows)?;
        let mut writer = BlockWriter::new(config)?;
        writer.configure_storage_limit(
            to_size(options.micro_block_size)?,
            to_size(options.macro_block_size)?,
            to_size(options.minimum_data_limit)?,
            to_size(options.expand_percent)?,
            options.encoding_granularity,
        )?;
        Ok(writer)
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
                ffi::InputKind::Bytes => BorrowedDatum::Bytes(bytes()?),
                ffi::InputKind::Null => BorrowedDatum::Null,
                ffi::InputKind::Nop => BorrowedDatum::Nop,
                ffi::InputKind::UInt => BorrowedDatum::UInt(cell.value),
                _ => return Err(Error::InvalidArgument),
            });
        }
        writer.append_borrowed_row(&row)
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

fn pax_writer_row_count(writer: &PaxWriter) -> u64 {
    writer
        .writer
        .as_ref()
        .map_or(0, |writer| writer.row_count() as u64)
}

fn pax_writer_header_size(writer: &PaxWriter) -> u64 {
    writer
        .writer
        .as_ref()
        .and_then(|writer| writer.total_header_size().ok())
        .unwrap_or(0) as u64
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
    build_hash_index: bool,
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
            build_hash_index,
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
    block_size_upper_bound: u64,
) -> ffi::FlatAppendResult {
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
        )
    })();
    let hash_state = match writer.hash_index_empty() {
        None => ffi::FlatHashWriteState::Disabled,
        Some(true) => ffi::FlatHashWriteState::Empty,
        Some(false) => ffi::FlatHashWriteState::Populated,
    };
    match result {
        Ok(outcome) => ffi::FlatAppendResult {
            code: ffi::Code::Ok,
            accepted_rows: outcome.accepted_rows,
            is_full: outcome.is_full,
            hash_state,
        },
        Err(error) => ffi::FlatAppendResult {
            code: code(error),
            accepted_rows: writer.row_count(),
            is_full: false,
            hash_state,
        },
    }
}

fn add_flat_hash_entry(writer: &mut FlatWriter, row: usize, hash: u64) -> ffi::Code {
    writer
        .add_hash_entry(row, hash)
        .map_or_else(code, |_| ffi::Code::Ok)
}

fn disable_flat_hash_index(writer: &mut FlatWriter) {
    writer.disable_hash_index();
}

fn finish_flat_writer(
    writer: &mut FlatWriter,
    options: ffi::FlatFinishOptionsDesc,
    column_checksums: &[u64],
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

fn flat_writer_stats(writer: &FlatWriter) -> ffi::WriterStatsResult {
    match writer.block_size().and_then(|block_size| {
        writer
            .original_size()
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

fn flat_writer_empty_size(column_count: usize, need_column_checksums: bool) -> u64 {
    FlatWriter::empty_block_size(column_count, need_column_checksums).unwrap_or(0) as u64
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

fn read_record_header(bytes: &[u8]) -> ffi::RecordResult {
    record_result(format::read_record_header(bytes), bytes)
}

fn read_expanded_record_header(bytes: &[u8]) -> ffi::RecordResult {
    record_result(format::read_expanded_record_header(bytes), bytes)
}

fn check_record_header(bytes: &[u8]) -> ffi::RecordResult {
    record_result(
        format::read_record_header(bytes).and_then(|header| {
            header.check_header_checksum(bytes)?;
            Ok(header)
        }),
        bytes,
    )
}

fn check_stored_record(bytes: &[u8]) -> ffi::RecordResult {
    record_result(format::check_stored_record(bytes), bytes)
}

fn check_stored_record_prefix(bytes: &[u8]) -> ffi::RecordResult {
    record_result(format::check_stored_record_prefix(bytes), bytes)
}

fn record_header_size(column_count: u16, has_column_checksum: bool) -> u32 {
    format::record_header_size(column_count.into(), has_column_checksum)
        .map_or(0, |value| value as u32)
}

fn accumulate_record_column_checksums(prefix: &[u8], sums: &mut [i64]) -> ffi::Code {
    format::accumulate_record_column_checksums(prefix, sums).map_or_else(code, |_| ffi::Code::Ok)
}

fn rewrite_record_column_checksums(
    out: &mut [u8],
    source_prefix: &[u8],
    column_count: u16,
    has_column_checksum: bool,
    checksums: &[u64],
) -> ffi::Code {
    format::rewrite_record_column_checksums(
        out,
        source_prefix,
        column_count,
        has_column_checksum,
        checksums,
    )
    .map_or_else(code, |_| ffi::Code::Ok)
}

fn reseal_record_header_in_place(out: &mut [u8], stored_payload: &[u8]) -> ffi::RecordResult {
    let result = format::reseal_record_header_in_place(out, stored_payload);
    record_result(result, out)
}

fn record_result(result: Result<Header>, bytes: &[u8]) -> ffi::RecordResult {
    let metadata = result.and_then(|header| {
        let flat = header.row_store_type == FLAT_ROW_STORE;
        Ok(ffi::RecordMetadata {
            header_size: header.header_size as u32,
            column_count: header.column_count as u16,
            rowkey_column_count: header.rowkey_column_count as u16,
            row_count: header.row_count as u32,
            row_store_type: header.row_store_type,
            original_length: header.original_length,
            max_merged_trans_version: header.max_merged_trans_version,
            data_length: header.data_length as u32,
            data_zlength: header.data_zlength as u32,
            data_checksum: header.data_checksum,
            has_column_checksum: header.has_column_checksum(),
            has_string_out_row: header.flags & HAS_STRING_OUT_ROW != 0,
            all_lob_in_row: header.flags & ALL_LOB_IN_ROW != 0,
            contains_hash_index: header.flags & CONTAINS_HASH_INDEX != 0,
            has_min_merged_trans_version: header.has_min_merged_trans_version(),
            min_merged_trans_version: header.min_merged_trans_version(bytes)?,
            single_version_rows: flat && header.opt & SINGLE_VERSION_ROWS != 0,
            contain_uncommitted_rows: flat && header.opt & CONTAIN_UNCOMMITTED_ROWS != 0,
            is_last_row_last_flag: flat && header.opt & IS_LAST_ROW_LAST_FLAG != 0,
        })
    });
    match metadata {
        Ok(metadata) => ffi::RecordResult {
            code: ffi::Code::Ok,
            metadata,
        },
        Err(error) => ffi::RecordResult {
            code: code(error),
            metadata: ffi::RecordMetadata::default(),
        },
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
        .map(|cell| describe_cell(block, cell))
        .collect();
    (descriptions, data)
}

fn describe_cell(block: &[u8], cell: &crate::read::ProjectionCell<'_>) -> ffi::CellDesc {
    match cell {
        crate::read::ProjectionCell::Borrowed(cell) => match cell {
            Cell::Null => ffi::CellDesc {
                kind: ffi::CellKind::Null,
                obj_type: 0,
                offset: 0,
                length: 0,
                value: 0,
            },
            Cell::Nop => ffi::CellDesc {
                kind: ffi::CellKind::Nop,
                obj_type: 0,
                offset: 0,
                length: 0,
                value: 0,
            },
            Cell::Bytes(bytes) => ffi::CellDesc {
                kind: ffi::CellKind::Borrowed,
                obj_type: 0,
                offset: (bytes.as_ptr() as usize - block.as_ptr() as usize) as u64,
                length: bytes.len() as u32,
                value: 0,
            },
            Cell::Outrow(bytes) => ffi::CellDesc {
                kind: ffi::CellKind::Outrow,
                obj_type: 0,
                offset: (bytes.as_ptr() as usize - block.as_ptr() as usize) as u64,
                length: bytes.len() as u32,
                value: 0,
            },
            Cell::UInt(value) => ffi::CellDesc {
                kind: ffi::CellKind::UInt,
                obj_type: 0,
                offset: 0,
                length: 0,
                value: *value,
            },
        },
        crate::read::ProjectionCell::Decoded { offset, length } => ffi::CellDesc {
            kind: ffi::CellKind::Decoded,
            obj_type: 0,
            offset: *offset as u64,
            length: *length as u32,
            value: 0,
        },
    }
}

fn describe_pax_cells(
    bytes: &[u8],
    block: &Block<'_>,
    projection: crate::Projection<'_>,
    columns: &[u32],
) -> Result<(Vec<ffi::CellDesc>, Vec<u8>)> {
    let (mut cells, data) = describe_cells(bytes, projection);
    for (index, cell) in cells.iter_mut().enumerate() {
        cell.obj_type = block.column_obj_type(columns[index % columns.len()] as usize)?;
    }
    Ok((cells, data))
}

fn project_pax_block(block: &[u8], rows: &[u32], columns: &[u32]) -> ffi::BatchResult {
    batch_result((|| {
        let parsed = Block::new(block)?;
        let projection = parsed.project(rows, columns)?;
        let (cells, data) = describe_pax_cells(block, &parsed, projection, columns)?;
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

fn flat_row_meta(info: FlatRowInfo) -> ffi::FlatRowMeta {
    ffi::FlatRowMeta {
        row_flag: info.row_flag,
        multi_version_flag: info.multi_version_flag,
        trans_id: info.trans_id,
    }
}

fn read_flat_row_meta(block: &[u8], row: u32) -> ffi::FlatRowMetaResult {
    match FlatBlock::open(block).and_then(|parsed| parsed.row_info(row as usize)) {
        Ok(info) => ffi::FlatRowMetaResult {
            code: ffi::Code::Ok,
            meta: flat_row_meta(info),
        },
        Err(error) => ffi::FlatRowMetaResult {
            code: code(error),
            meta: ffi::FlatRowMeta {
                row_flag: 0,
                multi_version_flag: 0,
                trans_id: 0,
            },
        },
    }
}

fn read_flat_cell(block: &[u8], row: u32, column: u32) -> ffi::FlatCellResult {
    let (status, cell) = match FlatBlock::open(block)
        .and_then(|parsed| parsed.cell(row as usize, column as usize))
    {
        Ok(cell) => (ffi::Code::Ok, cell),
        Err(error) => (code(error), Cell::Nop),
    };
    ffi::FlatCellResult {
        code: status,
        cell: describe_cell(block, &crate::read::ProjectionCell::Borrowed(cell)),
    }
}

fn lookup_flat_hash(block: &[u8], hash: u64) -> ffi::FlatHashResult {
    use crate::hash_index::Bucket;
    use ffi::FlatHashKind;

    let (code, kind, row) = match FlatBlock::open(block).and_then(|block| block.hash_bucket(hash)) {
        Ok(Bucket::Vacant) => (ffi::Code::Ok, FlatHashKind::Vacant, 0),
        Ok(Bucket::Collision) => (ffi::Code::Ok, FlatHashKind::Collision, 0),
        Ok(Bucket::Row(row)) => (ffi::Code::Ok, FlatHashKind::Row, row),
        Err(error) => (code(error), FlatHashKind::Vacant, 0),
    };
    ffi::FlatHashResult { code, kind, row }
}

fn project_flat_block(block: &[u8], rows: &[u32], columns: &[u32]) -> ffi::BatchResult {
    let result = (|| {
        let parsed = FlatBlock::open(block)?;
        let (projection, row_info) = parsed.project(rows, columns)?;
        let (cells, data) = describe_cells(block, projection);
        let row_meta = row_info.into_iter().map(flat_row_meta).collect();
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
        let (cells, data) = describe_pax_cells(block, &block_ref, projection, columns)?;
        Ok((rows, cells, data))
    })())
}

fn locate_result(result: Result<crate::RowkeyBound>) -> ffi::LocateResult {
    match result {
        Ok(bound) => ffi::LocateResult {
            code: ffi::Code::Ok,
            compare_error: 0,
            ordering: 0,
            row: bound.row as u32,
            equal: bound.equal,
        },
        Err(error) => ffi::LocateResult {
            code: code(error),
            compare_error: 0,
            ordering: 0,
            row: u32::MAX,
            equal: false,
        },
    }
}

enum LocateError {
    Block(Error),
    Compare(i32),
}

impl From<Error> for LocateError {
    fn from(error: Error) -> Self {
        Self::Block(error)
    }
}

fn locate_rowkey_by(
    block: &[u8],
    row_store_type: u8,
    column_count: u32,
    search: ffi::RowkeySearch,
    range_start: u32,
    range_end: u32,
    comparator: &ffi::RowkeyComparator,
) -> ffi::LocateResult {
    let result: core::result::Result<_, LocateError> = (|| {
        let range = crate::RowRange::new(range_start as usize, range_end as usize)?;
        let column_count = column_count as usize;
        let mut cells = Vec::new();
        let mut compare = |key: &crate::RowkeyCells<'_>, encoded: Option<&Block<'_>>| {
            cells.clear();
            cells
                .try_reserve_exact(key.len())
                .map_err(|_| Error::SizeOverflow)?;
            for (column, cell) in key.cells.iter().enumerate() {
                let mut cell = describe_cell(block, cell);
                if let Some(encoded) = encoded {
                    cell.obj_type = encoded.column_obj_type(column)?;
                }
                cells.push(cell);
            }
            let mut ordering = 0;
            let ret = comparator.run(&cells, &key.decoded, &mut ordering);
            if ret == 0 {
                Ok(ordering.cmp(&0))
            } else {
                Err(LocateError::Compare(ret))
            }
        };
        let lower_bound = match search {
            ffi::RowkeySearch::LowerBound => true,
            ffi::RowkeySearch::UpperBound => false,
            ffi::RowkeySearch::Compare | ffi::RowkeySearch::EqualRunEnd
                if row_store_type == crate::format::FLAT_ROW_STORE =>
            {
                let parsed = FlatBlock::open(block)?;
                if search == ffi::RowkeySearch::Compare {
                    let ordering = parsed
                        .compare_rowkey_by(range.start, column_count, |key| compare(key, None))?;
                    return Ok(ffi::LocateResult {
                        code: ffi::Code::Ok,
                        compare_error: 0,
                        ordering: ordering as i32,
                        row: range_start,
                        equal: ordering.is_eq(),
                    });
                }
                let row =
                    parsed.find_equal_run_end_by(column_count, range, |key| compare(key, None))?;
                return Ok(locate_result(Ok(crate::RowkeyBound { row, equal: false })));
            }
            _ => return Err(Error::InvalidArgument.into()),
        };
        let bound = match row_store_type {
            crate::format::FLAT_ROW_STORE => FlatBlock::open(block)?.find_rowkey_bound_by(
                column_count,
                lower_bound,
                range,
                |key| compare(key, None),
            ),
            crate::format::ENCODING_ROW_STORE | crate::format::SELECTIVE_ENCODING_ROW_STORE => {
                let encoded = Block::new(block)?;
                encoded.find_rowkey_bound_by(column_count, lower_bound, range, |key| {
                    compare(key, Some(&encoded))
                })
            }
            _ => Err(Error::Unsupported.into()),
        }?;
        Ok(locate_result(Ok(bound)))
    })();
    match result {
        Ok(result) => result,
        Err(LocateError::Block(error)) => locate_result(Err(error)),
        Err(LocateError::Compare(compare_error)) => ffi::LocateResult {
            code: ffi::Code::Ok,
            compare_error,
            ordering: 0,
            row: u32::MAX,
            equal: false,
        },
    }
}
