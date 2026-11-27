/*
 * Copyright (c) 2025 OceanBase.
 *
 * Licensed under the Apache License, Version 2.0 (the "License");
 * you may not use this file except in compliance with the License.
 * You may obtain a copy of the License at
 *
 *     http://www.apache.org/licenses/LICENSE-2.0
 *
 * Unless required by applicable law or agreed to in writing, software
 * distributed under the License is distributed on an "AS IS" BASIS,
 * WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
 * See the License for the specific language governing permissions and
 * limitations under the License.
 */

#define USING_LOG_PREFIX STORAGE

#include "ob_micro_block_encoder.h"

#include <new>

#include "lib/container/ob_se_array.h"
#include "rust/micro_block.h"

namespace oceanbase
{
using namespace storage;

namespace blocksstable
{
using namespace common;

struct ObMicroBlockEncoder::RustBlockBuffer
{
  RustBlockBuffer()
    : writer(micro_block::new_pax_writer()), bytes()
  {}
  rust::Box<micro_block::PaxWriter> writer;
  rust::Vec<uint8_t> bytes;
};

static int rust_code_to_ret(const micro_block::Code code)
{
  switch (code) {
    case micro_block::Code::Ok:
      return OB_SUCCESS;
    case micro_block::Code::InvalidArgument:
      return OB_INVALID_ARGUMENT;
    case micro_block::Code::InvalidData:
      return OB_INVALID_DATA;
    case micro_block::Code::ChecksumMismatch:
      return OB_CHECKSUM_ERROR;
    case micro_block::Code::Unsupported:
      return OB_NOT_SUPPORTED;
    case micro_block::Code::OutputTooSmall:
      return OB_BUF_NOT_ENOUGH;
    case micro_block::Code::SizeOverflow:
      return OB_SIZE_OVERFLOW;
    default:
      return OB_ERR_UNEXPECTED;
  }
}

ObMicroBlockEncoder::ObMicroBlockEncoder()
  : ctx_(),
    rust_block_(),
    column_checksums_(),
    expand_pct_(DEFAULT_ESTIMATE_REAL_SIZE_PCT),
    is_inited_(false)
{
  column_checksums_.set_attr(ObMemAttr("MicroBlkEncoder"));
}

ObMicroBlockEncoder::~ObMicroBlockEncoder()
{
  reset();
}

int ObMicroBlockEncoder::init(const ObMicroBlockEncodingCtx &ctx)
{
  int ret = OB_SUCCESS;
  if (is_inited_) {
    reuse();
    is_inited_ = false;
  }

  if (OB_UNLIKELY(!ctx.is_valid())) {
    ret = OB_INVALID_ARGUMENT;
  } else if (OB_FAIL(checksum_helper_.init(ctx.col_descs_, true/*need_opt_row_checksum*/))) {
  } else {
    need_check_lob_ = false;
    for (int64_t i = 0; i < ctx.column_cnt_; ++i) {
      if (!need_check_lob_ && ctx.col_descs_->at(i).col_type_.is_lob_storage()) {
        need_check_lob_ = true;
      }
    }
    ctx_ = ctx;
    is_inited_ = true;
  }
  return ret;
}

int ObMicroBlockEncoder::inner_init()
{
  int ret = OB_SUCCESS;
  if (OB_UNLIKELY(!is_inited_)) {
    ret = OB_NOT_INIT;
  } else if (!rust_block_) {
    std::unique_ptr<RustBlockBuffer> block(new (std::nothrow) RustBlockBuffer());
    if (!block) {
      ret = OB_ALLOCATE_MEMORY_FAILED;
    } else {
      rust::Vec<micro_block::BridgeColumn> columns;
      columns.reserve(ctx_.column_cnt_);
      for (int64_t col_idx = 0; col_idx < ctx_.column_cnt_; ++col_idx) {
        const ObObjType type = ctx_.col_descs_->at(col_idx).col_type_.get_type();
        const int64_t fixed_length = get_type_size_map()[type];
        micro_block::BridgeColumn column;
        column.obj_type = static_cast<uint8_t>(type);
        column.fixed_length = fixed_length > 0 ? static_cast<uint32_t>(fixed_length) : 0;
        columns.push_back(column);
      }
      expand_pct_ = estimate_expand_percent();
      const micro_block::PaxWriterConfig options = {
          static_cast<uint32_t>(ctx_.rowkey_column_cnt_),
          static_cast<uint8_t>(ctx_.row_store_type_),
          ctx_.need_calc_column_chksum_,
          static_cast<uint64_t>(ctx_.micro_block_size_),
          static_cast<uint64_t>(ctx_.macro_block_size_),
          DEFAULT_MICRO_MAX_SIZE,
          expand_pct_,
          ctx_.minimum_rows_,
          ctx_.encoding_granularity_};
      if (OB_FAIL(rust_code_to_ret(micro_block::init_pax_writer(
          *block->writer, std::move(columns), options)))) {
      } else if (ctx_.need_calc_column_chksum_) {
        column_checksums_.reuse();
        if (OB_FAIL(column_checksums_.reserve(ctx_.column_cnt_))) {
        } else {
          for (int64_t i = 0; OB_SUCC(ret) && i < ctx_.column_cnt_; ++i) {
            ret = column_checksums_.push_back(0);
          }
        }
      }
      if (OB_SUCC(ret)) {
        rust_block_ = std::move(block);
      }
    }
  }
  return ret;
}

void ObMicroBlockEncoder::reset()
{
  ObIMicroBlockWriter::reset();
  rust_block_.reset();
  column_checksums_.reset();
  is_inited_ = false;
  expand_pct_ = DEFAULT_ESTIMATE_REAL_SIZE_PCT;
}

void ObMicroBlockEncoder::reuse()
{
  ObIMicroBlockWriter::reuse();
  rust_block_.reset();
  column_checksums_.reuse();
}

void ObMicroBlockEncoder::dump_diagnose_info()
{
  int ret = OB_SUCCESS;
  if (IS_NOT_INIT) {
    ret = OB_NOT_INIT;
    STORAGE_LOG(WARN, "micro block encoder is not initialized");
  } else if (OB_UNLIKELY(!ctx_.is_valid())) {
    ret = OB_ERR_UNEXPECTED;
    STORAGE_LOG(WARN, "unexpected encoding ctx", K_(ctx));
  } else {
    ObIMicroBlockWriter::dump_diagnose_info();
    print_micro_block_encoder_status();
  }
}

void ObMicroBlockEncoder::print_micro_block_encoder_status() const
{
  FLOG_INFO("Build micro block failed, print encoder status: ",
    K_(ctx), K_(expand_pct), "row_count", get_row_count());
}

uint64_t ObMicroBlockEncoder::estimate_expand_percent() const
{
  return ctx_.real_block_size_ > 0
      ? std::max<int64_t>(1, ctx_.estimate_block_size_ * 100 / ctx_.real_block_size_)
      : DEFAULT_ESTIMATE_REAL_SIZE_PCT;
}

int ObMicroBlockEncoder::append_row(const ObDatumRow &row)
{
  int ret = OB_SUCCESS;
  common::ObSEArray<micro_block::InputCell, 64> cells;
  bool has_lob_out_row = has_lob_out_row_;
  if (IS_NOT_INIT) {
    ret = OB_NOT_INIT;
  } else if (OB_UNLIKELY(!row.is_valid())) {
    ret = OB_INVALID_ARGUMENT;
  } else if (OB_UNLIKELY(row.get_column_count() != ctx_.column_cnt_)) {
    ret = OB_INVALID_ARGUMENT;
  } else if (OB_FAIL(inner_init())) {
  } else if (OB_FAIL(check_out_row_columns(row, has_lob_out_row))) {
  } else if (OB_FAIL(cells.reserve(ctx_.column_cnt_))) {
  } else {
    for (int64_t col_idx = 0; OB_SUCC(ret) && col_idx < ctx_.column_cnt_; ++col_idx) {
      const ObStorageDatum &datum = row.storage_datums_[col_idx];
      const ObObjType type = ctx_.col_descs_->at(col_idx).col_type_.get_type();
      const int64_t fixed_length = get_type_size_map()[type];
      micro_block::InputCell cell;
      cell.kind = datum.is_null() ? micro_block::InputKind::Null
          : (datum.is_nop() ? micro_block::InputKind::Nop : micro_block::InputKind::Bytes);
      cell.pointer = 0;
      cell.length = 0;
      cell.value = 0;
      if (OB_UNLIKELY(datum.len_ < 0)) {
        ret = OB_INVALID_DATA;
      } else if (OB_UNLIKELY(!datum.is_null() && !datum.is_nop() && datum.is_ext())) {
        ret = OB_NOT_SUPPORTED;
      } else {
        if (micro_block::InputKind::Bytes == cell.kind) {
          const int64_t length = fixed_length > 0 && datum.len_ > fixed_length
              ? fixed_length : datum.len_;
          cell.length = static_cast<uint32_t>(length);
          cell.pointer = 0 == cell.length ? 0 : reinterpret_cast<std::size_t>(datum.ptr_);
          if (OB_UNLIKELY(cell.length > 0 && 0 == cell.pointer)) {
            ret = OB_INVALID_DATA;
          }
        }
        if (OB_SUCC(ret) && OB_FAIL(cells.push_back(cell))) {
        }
      }
    }
    if (OB_FAIL(ret)) {
      return ret;
    }
    const rust::Slice<const micro_block::InputCell> input(cells.begin(), cells.count());
    micro_block::AppendResult result = micro_block::append_pax_row(
        *rust_block_->writer, input, static_cast<uint64_t>(block_size_upper_bound_));
    if (OB_FAIL(rust_code_to_ret(result.code))) {
    } else if (result.is_full) {
      ret = OB_BUF_NOT_ENOUGH;
    } else {
      if (ctx_.need_calc_column_chksum_ && OB_FAIL(checksum_helper_.cal_column_checksum(
          row, &column_checksums_[0]))) {
        LOG_ERROR("cal column checksum failed", K(ret), K(row));
      } else if (need_cal_row_checksum()
                 && OB_FAIL(checksum_helper_.cal_row_checksum(
                     row.storage_datums_, row.get_column_count()))) {
        STORAGE_LOG(WARN, "fail to cal row chksum", K(ret), K(row), K_(checksum_helper));
      }
      if (OB_SUCC(ret)) {
        has_lob_out_row_ = has_lob_out_row;
        cal_row_stat(row);
      } else {
        // Rust has accepted the row. The block cannot be published with incomplete metadata.
        reset();
      }
    }
  }
  return ret;
}

int ObMicroBlockEncoder::check_out_row_columns(const ObDatumRow &row, bool &has_lob_out_row) const
{
  int ret = OB_SUCCESS;
  for (int64_t i = 0; need_check_lob_ && !has_lob_out_row && OB_SUCC(ret)
       && i < row.get_column_count(); ++i) {
    const ObStorageDatum &datum = row.storage_datums_[i];
    if (ctx_.col_descs_->at(i).col_type_.is_lob_storage() && !datum.is_nop() && !datum.is_null()) {
      if (datum.len_ < sizeof(ObLobCommon)) {
        ret = OB_ERR_UNEXPECTED;
      } else {
        has_lob_out_row = !datum.get_lob_data().in_row_;
      }
    }
  }
  return ret;
}

int ObMicroBlockEncoder::build_block(char *&buf, int64_t &size)
{
  int ret = OB_SUCCESS;
  if (IS_NOT_INIT) {
    ret = OB_NOT_INIT;
  } else if (OB_UNLIKELY(!rust_block_ || 0 == get_row_count())) {
    ret = OB_INNER_STAT_ERROR;
  } else {
    static const uint64_t empty_checksum = 0;
    const uint64_t *column_checksums = ctx_.need_calc_column_chksum_
        ? reinterpret_cast<const uint64_t *>(&column_checksums_[0]) : &empty_checksum;
    const std::size_t column_checksum_count = ctx_.need_calc_column_chksum_
        ? static_cast<std::size_t>(ctx_.column_cnt_) : 0;
    const std::size_t header_size = micro_block::pax_writer_header_size(*rust_block_->writer);
    const micro_block::WriterStatsResult stats = micro_block::pax_writer_stats(
        *rust_block_->writer, static_cast<uint64_t>(expand_pct_));
    micro_block::BuildResult result = micro_block::finish_pax_writer(
        *rust_block_->writer,
        rust::Slice<const uint64_t>(column_checksums, column_checksum_count),
        !has_lob_out_row_, max_merged_trans_version_);
    if (OB_FAIL(rust_code_to_ret(stats.code))) {
    } else if (OB_FAIL(rust_code_to_ret(result.code))) {
    } else if (OB_UNLIKELY(stats.original_size > static_cast<uint64_t>(INT64_MAX)
                           || result.block.size() > static_cast<std::size_t>(INT64_MAX)
                           || result.block.size() < header_size)) {
      ret = OB_INVALID_DATA;
    } else {
      ctx_.estimate_block_size_ += stats.original_size;
      ctx_.real_block_size_ += result.block.size() - header_size;
      ctx_.micro_block_cnt_++;
      rust_block_->bytes = std::move(result.block);
      buf = reinterpret_cast<char *>(rust_block_->bytes.data());
      size = static_cast<int64_t>(rust_block_->bytes.size());
    }
  }
  return ret;
}

int64_t ObMicroBlockEncoder::get_row_count() const
{
  return rust_block_ ? micro_block::pax_writer_row_count(*rust_block_->writer) : 0;
}

int64_t ObMicroBlockEncoder::get_block_size() const
{
  int64_t size = 0;
  if (rust_block_) {
    const micro_block::WriterStatsResult stats = micro_block::pax_writer_stats(
        *rust_block_->writer, static_cast<uint64_t>(expand_pct_));
    if (micro_block::Code::Ok == stats.code && stats.block_size <= static_cast<uint64_t>(INT64_MAX)) {
      size = stats.block_size;
    }
  }
  return size;
}

int64_t ObMicroBlockEncoder::get_original_size() const
{
  int64_t size = 0;
  if (rust_block_) {
    const micro_block::WriterStatsResult stats = micro_block::pax_writer_stats(
        *rust_block_->writer, static_cast<uint64_t>(expand_pct_));
    if (micro_block::Code::Ok == stats.code
        && stats.original_size <= static_cast<uint64_t>(INT64_MAX)) {
      size = stats.original_size;
    }
  }
  return size;
}

} // end namespace blocksstable
} // end namespace oceanbase
