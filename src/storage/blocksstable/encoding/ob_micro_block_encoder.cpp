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
    estimate_size_limit_(0),
    header_size_(0),
    expand_pct_(DEFAULT_ESTIMATE_REAL_SIZE_PCT),
    row_count_(0),
    encoder_freezed_(false),
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
    encoder_freezed_ = false;
    is_inited_ = true;
  }
  return ret;
}

int ObMicroBlockEncoder::inner_init()
{
  int ret = OB_SUCCESS;
  rust::Vec<micro_block::BridgeColumn> columns;
  if (OB_UNLIKELY(!is_inited_)) {
    ret = OB_NOT_INIT;
  } else if (0 != row_count_) {
  } else if (OB_UNLIKELY(!ctx_.is_valid())) {
    ret = OB_ERR_UNEXPECTED;
  } else {
    column_checksums_.reuse();
    update_estimate_size_limit(ctx_);
    if (OB_UNLIKELY(estimate_size_limit_ > INT64_MAX - header_size_)) {
      ret = OB_SIZE_OVERFLOW;
    } else if (!rust_block_) {
      rust_block_.reset(new (std::nothrow) RustBlockBuffer());
      if (!rust_block_) {
        ret = OB_ALLOCATE_MEMORY_FAILED;
      }
    }
    if (OB_SUCC(ret)) {
      columns.reserve(ctx_.column_cnt_);
      for (int64_t col_idx = 0; col_idx < ctx_.column_cnt_; ++col_idx) {
        const ObObjType type = ctx_.col_descs_->at(col_idx).col_type_.get_type();
        const int64_t fixed_length = get_type_size_map()[type];
        micro_block::BridgeColumn column;
        column.obj_type = static_cast<uint8_t>(type);
        column.fixed_length = fixed_length > 0 ? static_cast<uint32_t>(fixed_length) : 0;
        columns.push_back(column);
      }
      if (OB_FAIL(rust_code_to_ret(micro_block::init_pax_writer(
          *rust_block_->writer,
          std::move(columns),
          static_cast<uint32_t>(ctx_.rowkey_column_cnt_),
          static_cast<uint8_t>(ctx_.row_store_type_),
          ctx_.need_calc_column_chksum_,
          static_cast<uint64_t>(header_size_ + estimate_size_limit_),
          ctx_.minimum_rows_)))) {
      } else if (ctx_.need_calc_column_chksum_
                 && OB_FAIL(column_checksums_.reserve(ctx_.column_cnt_))) {
      } else if (ctx_.need_calc_column_chksum_) {
        for (int64_t i = 0; OB_SUCC(ret) && i < ctx_.column_cnt_; ++i) {
          if (OB_FAIL(column_checksums_.push_back(0))) {
          }
        }
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
  encoder_freezed_ = false;
  estimate_size_limit_ = 0;
  header_size_ = 0;
  expand_pct_ = DEFAULT_ESTIMATE_REAL_SIZE_PCT;
  row_count_ = 0;
}

void ObMicroBlockEncoder::reuse()
{
  ObIMicroBlockWriter::reuse();
  if (rust_block_) {
    rust_block_->bytes = rust::Vec<uint8_t>();
  }
  column_checksums_.reuse();
  row_count_ = 0;
  encoder_freezed_ = false;
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
  FLOG_INFO("Build micro block failed, print encoder status: ", K_(ctx),
    K_(estimate_size_limit), K_(header_size), K_(expand_pct), K_(row_count));
}

void ObMicroBlockEncoder::update_estimate_size_limit(const ObMicroBlockEncodingCtx &ctx)
{
  expand_pct_ = DEFAULT_ESTIMATE_REAL_SIZE_PCT;
  if (ctx.real_block_size_ > 0) {
    const int64_t prev_expand_pct = ctx.estimate_block_size_ * 100 / ctx.real_block_size_;
    expand_pct_ = prev_expand_pct != 0 ? prev_expand_pct : 1;
  }

  if (OB_UNLIKELY(0 == expand_pct_)) {
    LOG_ERROR_RET(OB_ERR_UNEXPECTED, "expand_pct equal to zero",
        K_(expand_pct), K(ctx.estimate_block_size_), K(ctx.real_block_size_));
  }

  header_size_ =
      ObMicroBlockHeader::get_serialize_size(ctx.column_cnt_, ctx.need_calc_column_chksum_)
      + (sizeof(ObColumnHeader)) * ctx.column_cnt_;
  int64_t data_size_limit = (2 * ctx.micro_block_size_ - header_size_) > 0
                                  ? (2 * ctx.micro_block_size_ - header_size_)
                                  : (2 * ctx.micro_block_size_);

  data_size_limit = MAX(data_size_limit, DEFAULT_MICRO_MAX_SIZE);

  estimate_size_limit_ = std::min(data_size_limit * expand_pct_ / 100, ctx.macro_block_size_);
}

int ObMicroBlockEncoder::append_row(const ObDatumRow &row)
{
  int ret = OB_SUCCESS;
  common::ObSEArray<micro_block::InputCell, 64> cells;
  if (IS_NOT_INIT) {
    ret = OB_NOT_INIT;
  } else if (OB_UNLIKELY(!row.is_valid())) {
    ret = OB_INVALID_ARGUMENT;
  } else if (OB_UNLIKELY(row.get_column_count() != ctx_.column_cnt_)) {
    ret = OB_INVALID_ARGUMENT;
  } else if (OB_UNLIKELY(encoder_freezed_)) {
    ret = OB_ERR_UNEXPECTED;
  } else if (OB_FAIL(inner_init())) {
  } else if (OB_UNLIKELY(row_count_ >= MAX_MICRO_BLOCK_ROW_CNT
                         || static_cast<uint64_t>(row_count_) > ctx_.encoding_granularity_)) {
    ret = OB_BUF_NOT_ENOUGH;
    LOG_INFO("Try to encode more rows than maximum of row cnt in header, force to build a block",
        K_(row_count), K(row), K(ctx_.encoding_granularity_));
  } else if (OB_FAIL(process_out_row_columns(row))) {
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
    } else if (OB_UNLIKELY(result.accepted_rows != static_cast<std::size_t>(row_count_)
                           + (result.is_full ? 0 : 1))) {
      ret = OB_INVALID_DATA;
    } else if (result.is_full) {
      ret = OB_BUF_NOT_ENOUGH;
    } else if (FALSE_IT(++row_count_)) {
    } else if (ctx_.need_calc_column_chksum_ && OB_FAIL(checksum_helper_.cal_column_checksum(
        row, &column_checksums_[0]))) {
      LOG_ERROR("cal column checksum failed", K(ret), K(row));
    } else {
      if (need_cal_row_checksum()
          && OB_FAIL(checksum_helper_.cal_row_checksum(row.storage_datums_, row.get_column_count()))) {
        STORAGE_LOG(WARN, "fail to cal row chksum", K(ret), K(row), K_(checksum_helper));
      }
      cal_row_stat(row);
    }
  }
  return ret;
}

int ObMicroBlockEncoder::process_out_row_columns(const ObDatumRow &row)
{
  int ret = OB_SUCCESS;
  if (!need_check_lob_) {
  } else if (OB_UNLIKELY(row.get_column_count() != ctx_.column_cnt_)) {
    ret = OB_ERR_UNEXPECTED;
  } else if (!has_lob_out_row_) {
    for (int64_t i = 0; !has_lob_out_row_ && OB_SUCC(ret) && i < row.get_column_count(); ++i) {
      const ObStorageDatum &datum = row.storage_datums_[i];
      if (ctx_.col_descs_->at(i).col_type_.is_lob_storage()) {
        if (datum.is_nop() || datum.is_null()) {
        } else if (datum.len_ < sizeof(ObLobCommon)) {
          ret = OB_ERR_UNEXPECTED;
        } else {
          const ObLobCommon &lob_common = datum.get_lob_data();
          has_lob_out_row_ = !lob_common.in_row_;
        }
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
  } else if (OB_UNLIKELY(!rust_block_ || 0 == row_count_)) {
    ret = OB_INNER_STAT_ERROR;
  } else if (OB_UNLIKELY(encoder_freezed_)) {
    ret = OB_ERR_UNEXPECTED;
  } else if (FALSE_IT(encoder_freezed_ = true)) {
  } else {
    static const uint64_t empty_checksum = 0;
    const uint64_t *column_checksums = ctx_.need_calc_column_chksum_
        ? reinterpret_cast<const uint64_t *>(&column_checksums_[0]) : &empty_checksum;
    const std::size_t column_checksum_count = ctx_.need_calc_column_chksum_
        ? static_cast<std::size_t>(ctx_.column_cnt_) : 0;
    const micro_block::WriterStatsResult stats = micro_block::pax_writer_stats(
        *rust_block_->writer, static_cast<uint64_t>(expand_pct_));
    micro_block::BuildResult result = micro_block::finish_pax_writer(
        *rust_block_->writer,
        rust::Slice<const uint64_t>(column_checksums, column_checksum_count),
        !has_lob_out_row_, max_merged_trans_version_);
    if (OB_FAIL(rust_code_to_ret(stats.code))) {
    } else if (OB_FAIL(rust_code_to_ret(result.code))) {
    } else if (OB_UNLIKELY(result.accepted_rows != static_cast<std::size_t>(row_count_)
                           || stats.original_size > static_cast<uint64_t>(INT64_MAX)
                           || result.block.size() > static_cast<std::size_t>(INT64_MAX)
                           || result.block.size() < static_cast<std::size_t>(header_size_))) {
      ret = OB_INVALID_DATA;
    } else {
      ctx_.estimate_block_size_ += stats.original_size;
      ctx_.real_block_size_ += result.block.size() - header_size_;
      ctx_.micro_block_cnt_++;
      rust_block_->bytes = std::move(result.block);
      buf = reinterpret_cast<char *>(rust_block_->bytes.data());
      size = static_cast<int64_t>(rust_block_->bytes.size());
      ObMicroBlockHeader *micro_header = reinterpret_cast<ObMicroBlockHeader *>(buf);
      if (micro_header->has_column_checksum_) {
        micro_header->column_checksums_ = reinterpret_cast<int64_t *>(
            buf + ObMicroBlockHeader::COLUMN_CHECKSUM_PTR_OFFSET);
      }
      STORAGE_LOG(DEBUG, "built micro block with Rust", K(size), K_(row_count));
    }
  }
  return ret;
}

int64_t ObMicroBlockEncoder::get_block_size() const
{
  int64_t size = header_size_;
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
