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

#include "rust/micro_block.h"
#include "storage/blocksstable/index_block/ob_index_block_aggregator.h"

namespace oceanbase
{
using namespace storage;

namespace blocksstable
{
using namespace common;

#define MICRO_BLOCK_PAGE_ALLOCATOR ModulePageAllocator(common::ObModIds::OB_ENCODER_ALLOCATOR)
struct ObMicroBlockEncoder::RustBlockBuffer
{
  rust::Vec<uint8_t> bytes;
};

ObMicroBlockEncoder::ObMicroBlockEncoder() : ctx_(),
    data_buffer_(),
    rust_block_(),
    datum_rows_(OB_MALLOC_NORMAL_BLOCK_SIZE, MICRO_BLOCK_PAGE_ALLOCATOR),
    all_col_datums_(OB_MALLOC_NORMAL_BLOCK_SIZE, MICRO_BLOCK_PAGE_ALLOCATOR),
    pivot_allocator_(lib::ObMemAttr(blocksstable::OB_ENCODING_LABEL_PIVOT), OB_MALLOC_MIDDLE_BLOCK_SIZE),
    estimate_size_(0), estimate_size_limit_(0),
    header_size_(0), expand_pct_(DEFAULT_ESTIMATE_REAL_SIZE_PCT),
    row_buf_holder_(),
    deep_copy_indexes_(OB_MALLOC_NORMAL_BLOCK_SIZE, MICRO_BLOCK_PAGE_ALLOCATOR),
    estimate_base_store_size_(0),
    pre_agg_columns_(OB_MALLOC_NORMAL_BLOCK_SIZE, MICRO_BLOCK_PAGE_ALLOCATOR),
    length_(0),
    encoder_freezed_(false),
    is_inited_(false),
    block_generated_(false)
{
  datum_rows_.set_attr(ObMemAttr("MicroBlkEncoder"));
  all_col_datums_.set_attr(ObMemAttr("MicroBlkEncoder"));
  deep_copy_indexes_.set_attr(ObMemAttr("MicroBlkEncoder"));
  pre_agg_columns_.set_attr(ObMemAttr("MicroBlkEncoder"));
}
#undef MICRO_BLOCK_PAGE_ALLOCATOR

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
  } else if (OB_FAIL(pre_agg_columns_.reserve(ctx.column_cnt_))) {
  } else if (OB_FAIL(init_all_col_values(ctx))) {
  } else if (OB_FAIL(checksum_helper_.init(ctx.col_descs_, true/*need_opt_row_checksum*/))) {
  } else {
    estimate_base_store_size_ = 0;
    for (int64_t i = 0; i < ctx.column_cnt_; ++i) {
      estimate_base_store_size_
          += get_estimate_base_store_size_map()[ctx.col_descs_->at(i).col_type_.get_type()];
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
  if (OB_UNLIKELY(!is_inited_)) {
    ret = OB_NOT_INIT;
  } else if (data_buffer_.length() > 0) {
    // has been inner_inited, do nothing
  } else {
    if (OB_UNLIKELY(!ctx_.is_valid())) {
      ret = OB_ERR_UNEXPECTED;
    } else if (!data_buffer_.is_inited()) {
      if (OB_FAIL(data_buffer_.init(DEFAULT_DATA_BUFFER_SIZE))) {
      } else if (OB_FAIL(row_buf_holder_.init(2 * DEFAULT_DATA_BUFFER_SIZE))) {
      }
    }
    if (OB_FAIL(ret)) {
    } else if (OB_FAIL(reserve_header(ctx_))) {
    } else {
      update_estimate_size_limit(ctx_);
    }
  }
  return ret;
}

void ObMicroBlockEncoder::reset()
{
  ObIMicroBlockWriter::reset();
  rust_block_.reset();
  is_inited_ = false;
  encoder_freezed_ = false;
  data_buffer_.reset();
  datum_rows_.reset();
  FOREACH(cv, all_col_datums_) {
    ObColDatums *p = *cv;
    if (nullptr != p) {
      p->~ObColDatums();
      pivot_allocator_.free(p);
    }
  }
  all_col_datums_.reset();
  pivot_allocator_.reset();
  estimate_size_ = 0;
  estimate_size_limit_ = 0;
  header_size_ = 0;
  expand_pct_ = DEFAULT_ESTIMATE_REAL_SIZE_PCT;
  estimate_base_store_size_ = 0;
  row_buf_holder_.reset();
  deep_copy_indexes_.reset();
  pre_agg_columns_.reset();
  length_ = 0;
  block_generated_ = false;
}

void ObMicroBlockEncoder::reuse()
{
  ObIMicroBlockWriter::reuse();
  if (rust_block_) {
    rust_block_->bytes = rust::Vec<uint8_t>();
  }
  data_buffer_.reuse();
  datum_rows_.reuse();
  FOREACH(c, all_col_datums_) {
    (*c)->reuse();
  }
  row_buf_holder_.reuse();
  estimate_size_ = 0;
  deep_copy_indexes_.reuse();
  pre_agg_columns_.reuse();
  length_ = 0;
  encoder_freezed_ = false;
  block_generated_ = false;
}

void ObMicroBlockEncoder::dump_diagnose_info()
{
  int ret = OB_SUCCESS;
  int tmp_ret = OB_SUCCESS;
  if (IS_NOT_INIT) {
    ret = OB_NOT_INIT;
  } else if (OB_UNLIKELY(!ctx_.is_valid())) {
    ret = OB_ERR_UNEXPECTED;
    LOG_WARN("unexpected encoding ctx", K_(ctx));
  } else {
    ObIMicroBlockWriter::dump_diagnose_info();
    print_micro_block_encoder_status();
    ObDatumRow datum_row;
    ObStoreRow store_row;
    const int64_t column_cnt = ctx_.column_cnt_;
    ObArenaAllocator allocator("DumpMicroInfo");
    ObMicroBlockChecksumHelper tmp_checksum_helper;
    blocksstable::ObNewRowBuilder new_row_builder;
    encoder_freezed_ = true;
    if (OB_FAIL(set_datum_rows_ptr())) {
    } else if (OB_FAIL(tmp_checksum_helper.init(ctx_.col_descs_, true))) {
    } else if (OB_FAIL(new_row_builder.init(*ctx_.col_descs_, allocator))) {
    } else if (OB_FAIL(datum_row.init(column_cnt))) {
    } else {
      for (int64_t it = 0; OB_SUCC(ret) && it < datum_rows_.count(); ++it) {
        for (int64_t col_idx = 0; col_idx < column_cnt; ++col_idx) {
          datum_row.storage_datums_[col_idx].ptr_ = datum_rows_.at(it).datums_[col_idx].ptr_;
          datum_row.storage_datums_[col_idx].pack_ = datum_rows_.at(it).datums_[col_idx].pack_;
        }
        if (OB_TMP_FAIL(tmp_checksum_helper.cal_row_checksum(datum_row.storage_datums_, column_cnt))) {
        }
        FLOG_WARN("error micro block row (original)", K(it), K(datum_row),
            "orig_checksum", tmp_checksum_helper.get_row_checksum());
        if (OB_FAIL(new_row_builder.build_store_row(datum_row, store_row))) {
        } else {
          FLOG_WARN("error micro block store_row (original)", K(it), K(store_row));
        }
      }
    }
  }
  // ignore ret
}
int ObMicroBlockEncoder::init_all_col_values(const ObMicroBlockEncodingCtx &ctx)
{
  int ret = OB_SUCCESS;
  if (OB_FAIL(all_col_datums_.reserve(ctx.column_cnt_))) {
  }
  for (int64_t i = all_col_datums_.count(); i < ctx.column_cnt_ && OB_SUCC(ret); ++i) {
    ObColDatums *c = OB_NEWx(ObColDatums, &pivot_allocator_, pivot_allocator_);
    if (OB_ISNULL(c)) {
      ret = OB_ALLOCATE_MEMORY_FAILED;
    } else if (OB_FAIL(all_col_datums_.push_back(c))) {
      if (nullptr != c) {
        c->~ObColDatums();
        pivot_allocator_.free(c);
      }
    }
  }
  return ret;
}

void ObMicroBlockEncoder::print_micro_block_encoder_status() const
{
  FLOG_INFO("Build micro block failed, print encoder status: ", K_(ctx),
    K_(estimate_size), K_(estimate_size_limit), K_(header_size),
    K_(expand_pct), K_(estimate_base_store_size), K_(length), K(get_row_count()));
}

void ObMicroBlockEncoder::update_estimate_size_limit(const ObMicroBlockEncodingCtx &ctx)
{
  expand_pct_ = DEFAULT_ESTIMATE_REAL_SIZE_PCT;
  if (ctx.real_block_size_ > 0) {
    const int64_t prev_expand_pct = ctx.estimate_block_size_ * 100 / ctx.real_block_size_;
    expand_pct_ = prev_expand_pct != 0 ? prev_expand_pct : 1;
  }

  // We should make sure expand_pct_ never equal to 0
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

int ObMicroBlockEncoder::try_to_append_row(const int64_t &store_size)
{
  int ret = OB_SUCCESS;
  // header_size_ = micro_header_size + column_header_size
  if (OB_UNLIKELY(store_size + estimate_size_ + header_size_ > block_size_upper_bound_)) {
    ret = OB_BUF_NOT_ENOUGH;
  }
  return ret;
}

int ObMicroBlockEncoder::append_row(const ObDatumRow &row)
{
  int ret = OB_SUCCESS;
  if (IS_NOT_INIT) {
    ret = OB_NOT_INIT;
  } else if (OB_UNLIKELY(!row.is_valid())) {
    ret = OB_INVALID_ARGUMENT;
  } else if (OB_UNLIKELY(row.get_column_count() != ctx_.column_cnt_)) {
    ret = OB_INVALID_ARGUMENT;
  } else if (OB_UNLIKELY(encoder_freezed_)) {
    ret = OB_ERR_UNEXPECTED;
  } else if (OB_FAIL(inner_init())) {
  } else {
    if (datum_rows_.empty()) {
      if (OB_FAIL(init_pre_agg_columns())) {
      }
    }
  }

  if (OB_SUCC(ret)) {
    int64_t store_size = 0;
    ObConstDatumRow datum_row;
    ObMicroBlockHeader *header = get_header(data_buffer_);
    uint64_t row_count = (uint64_t)(datum_rows_.count());
    if (OB_UNLIKELY(row_count >= MAX_MICRO_BLOCK_ROW_CNT || row_count > ctx_.encoding_granularity_)) {
      ret = OB_BUF_NOT_ENOUGH;
      LOG_INFO("Try to encode more rows than maximum of row cnt in header, force to build a block",
          K(datum_rows_.count()), K(row), K(ctx_.encoding_granularity_));
    } else if (OB_FAIL(process_out_row_columns(row))) {
    } else if (OB_FAIL(copy_and_append_row(row, store_size))) {
      if (OB_UNLIKELY(OB_BUF_NOT_ENOUGH != ret)) {
      }
    } else if (header->has_column_checksum_ && OB_FAIL(checksum_helper_.cal_column_checksum(
        row, header->column_checksums_))) {
      LOG_ERROR("cal column checksum failed", K(ret), K(row));
    } else {
      if (need_cal_row_checksum()
          && OB_FAIL(checksum_helper_.cal_row_checksum(row.storage_datums_, row.get_column_count()))) {
        STORAGE_LOG(WARN, "fail to cal row chksum", K(ret), K(row), K_(checksum_helper));
      }
      cal_row_stat(row);
      estimate_size_ += store_size;
    }
  }
  return ret;
}

int ObMicroBlockEncoder::pivot()
{
  int ret = OB_SUCCESS;
  if (IS_NOT_INIT) {
    ret = OB_NOT_INIT;
  } else if (OB_UNLIKELY(datum_rows_.empty())) {
    ret = OB_INNER_STAT_ERROR;
  } else {
    for (int64_t i = 0; OB_SUCC(ret) && i < ctx_.column_cnt_; ++i) {
      ObColDatums &c = *all_col_datums_.at(i);
      if (OB_FAIL(c.resize(datum_rows_.count()))) {
      } else {
        PreAggColumn &pre_agg = pre_agg_columns_.at(i);
        pre_agg.col_datums_ = &c;
        pre_agg.null_cnt_ = 0;
        int64_t pos = 0;
        for (; pos + 8 <= datum_rows_.count(); pos += 8) {
          c.at(pos + 0) = datum_rows_.at(pos + 0).get_datum(i);
          c.at(pos + 1) = datum_rows_.at(pos + 1).get_datum(i);
          c.at(pos + 2) = datum_rows_.at(pos + 2).get_datum(i);
          c.at(pos + 3) = datum_rows_.at(pos + 3).get_datum(i);
          c.at(pos + 4) = datum_rows_.at(pos + 4).get_datum(i);
          c.at(pos + 5) = datum_rows_.at(pos + 5).get_datum(i);
          c.at(pos + 6) = datum_rows_.at(pos + 6).get_datum(i);
          c.at(pos + 7) = datum_rows_.at(pos + 7).get_datum(i);
        }
        for (; pos < datum_rows_.count(); ++pos) {
          c.at(pos) = datum_rows_.at(pos).get_datum(i);
        }
        for (int64_t row_idx = 0; row_idx < c.count(); ++row_idx) {
          pre_agg.null_cnt_ += c.at(row_idx).is_null() ? 1 : 0;
        }
      }
    }
  }
  return ret;
}

int ObMicroBlockEncoder::reserve_header(const ObMicroBlockEncodingCtx &ctx)
{
  int ret = OB_SUCCESS;
  if (ctx.column_cnt_ < 0) {
    ret = OB_INVALID_ARGUMENT;
  } else {
    int32_t header_size = ObMicroBlockHeader::get_serialize_size(
        ctx.column_cnt_, ctx.need_calc_column_chksum_);
    if (OB_FAIL(data_buffer_.write_nop(header_size, true))) {
    } else {
      ObMicroBlockHeader *header = get_header(data_buffer_);
      header->magic_ = MICRO_BLOCK_HEADER_MAGIC;
      header->version_ = MICRO_BLOCK_HEADER_VERSION;
      header->header_size_ = header_size;
      header->row_store_type_ = ctx.row_store_type_;
      header->column_count_ = ctx.column_cnt_;
      header->rowkey_column_count_ = ctx.rowkey_column_cnt_;
      header->has_column_checksum_ = ctx.need_calc_column_chksum_;
      if (header->has_column_checksum_) {
        header->column_checksums_ = reinterpret_cast<int64_t *>(
            data_buffer_.data() + ObMicroBlockHeader::COLUMN_CHECKSUM_PTR_OFFSET);
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
  } else if (OB_UNLIKELY(datum_rows_.empty())) {
    ret = OB_INNER_STAT_ERROR;
  } else if (OB_UNLIKELY(encoder_freezed_)) {
    ret = OB_ERR_UNEXPECTED;
  } else if (FALSE_IT(encoder_freezed_ = true)) {
  } else if (OB_FAIL(set_datum_rows_ptr())) {
  } else if (OB_FAIL(pivot())) {
  } else if (OB_FAIL(build_rust_block(buf, size))) {
  }
  return ret;
}

int ObMicroBlockEncoder::build_rust_block(char *&buf, int64_t &size)
{
  int ret = OB_SUCCESS;
  rust::Vec<micro_block::BridgeColumn> columns;
  rust::Vec<micro_block::InputCell> cells;
  if (ENCODING_ROW_STORE != ctx_.row_store_type_
      && SELECTIVE_ENCODING_ROW_STORE != ctx_.row_store_type_) {
    ret = OB_NOT_SUPPORTED;
  } else if (!rust_block_) {
    rust_block_.reset(new (std::nothrow) RustBlockBuffer());
    if (!rust_block_) {
      ret = OB_ALLOCATE_MEMORY_FAILED;
    }
  }
  if (OB_SUCC(ret)) {
    columns.reserve(ctx_.column_cnt_);
    cells.reserve(datum_rows_.count() * ctx_.column_cnt_);
  }
  for (int64_t col_idx = 0; OB_SUCC(ret) && col_idx < ctx_.column_cnt_; ++col_idx) {
    const ObObjType type = ctx_.col_descs_->at(col_idx).col_type_.get_type();
    const int64_t fixed_length = get_type_size_map()[type];
    micro_block::BridgeColumn column;
    column.obj_type = static_cast<uint8_t>(type);
    column.fixed_length = fixed_length > 0 ? static_cast<uint32_t>(fixed_length) : 0;
    column.enable_bit_packing = false;
    columns.push_back(column);
  }
  for (int64_t row_idx = 0; OB_SUCC(ret) && row_idx < datum_rows_.count(); ++row_idx) {
    for (int64_t col_idx = 0; OB_SUCC(ret) && col_idx < ctx_.column_cnt_; ++col_idx) {
      const ObDatum &datum = datum_rows_.at(row_idx).get_datum(col_idx);
      micro_block::InputCell cell;
      if (OB_UNLIKELY(!datum.is_null() && !datum.is_nop()
                      && datum.len_ > 0 && nullptr == datum.ptr_)) {
        ret = OB_INVALID_DATA;
      } else {
        cell.kind = datum.is_null() ? 1 : (datum.is_nop() ? 2 : 0);
        cell.pointer = reinterpret_cast<std::size_t>(datum.ptr_);
        cell.length = datum.len_;
        const uint32_t fixed_length = columns[col_idx].fixed_length;
        if (0 == cell.kind && fixed_length > 0 && datum.len_ > fixed_length) {
          cell.length = fixed_length;
        }
        cells.push_back(cell);
      }
    }
  }
  if (OB_SUCC(ret)) {
    static const uint64_t empty_checksum = 0;
    const ObMicroBlockHeader *header = get_header(data_buffer_);
    const uint64_t *column_checksums = ctx_.need_calc_column_chksum_
        ? reinterpret_cast<const uint64_t *>(header->column_checksums_) : &empty_checksum;
    const std::size_t column_checksum_count = ctx_.need_calc_column_chksum_
        ? static_cast<std::size_t>(ctx_.column_cnt_) : 0;
    micro_block::BuildResult result = micro_block::build_pax_block(
        std::move(columns),
        std::move(cells),
        static_cast<uint32_t>(ctx_.rowkey_column_cnt_),
        static_cast<uint8_t>(ctx_.row_store_type_),
        ctx_.need_calc_column_chksum_,
        rust::Slice<const uint64_t>(column_checksums, column_checksum_count),
        !has_lob_out_row_,
        static_cast<uint64_t>(estimate_size_),
        max_merged_trans_version_);
    switch (result.code) {
      case micro_block::Code::Ok:
        break;
      case micro_block::Code::Unsupported:
        ret = OB_NOT_SUPPORTED;
        break;
      case micro_block::Code::InvalidArgument:
        ret = OB_INVALID_ARGUMENT;
        break;
      case micro_block::Code::InvalidData:
        ret = OB_INVALID_DATA;
        break;
      case micro_block::Code::ChecksumMismatch:
        ret = OB_CHECKSUM_ERROR;
        break;
      case micro_block::Code::OutputTooSmall:
        ret = OB_BUF_NOT_ENOUGH;
        break;
      case micro_block::Code::SizeOverflow:
        ret = OB_SIZE_OVERFLOW;
        break;
      default:
        ret = OB_ERR_UNEXPECTED;
    }
    if (OB_SUCC(ret)) {
      const int64_t metadata_size = header_size_;
      if (OB_UNLIKELY(result.accepted_rows != static_cast<std::size_t>(datum_rows_.count())
                      || result.block.size() > static_cast<std::size_t>(INT64_MAX)
                      || result.block.size() < static_cast<std::size_t>(metadata_size))) {
        ret = OB_INVALID_DATA;
      } else {
        ctx_.estimate_block_size_ += estimate_size_;
        ctx_.real_block_size_ += result.block.size() - metadata_size;
        ctx_.micro_block_cnt_++;
        rust_block_->bytes = std::move(result.block);
        buf = reinterpret_cast<char *>(rust_block_->bytes.data());
        size = static_cast<int64_t>(rust_block_->bytes.size());
        ObMicroBlockHeader *micro_header = reinterpret_cast<ObMicroBlockHeader *>(buf);
        if (micro_header->has_column_checksum_) {
          micro_header->column_checksums_ = reinterpret_cast<int64_t *>(
              buf + ObMicroBlockHeader::COLUMN_CHECKSUM_PTR_OFFSET);
        }
        block_generated_ = true;
        STORAGE_LOG(DEBUG, "built micro block with Rust", K(size), K(datum_rows_.count()));
      }
    }
  }
  return ret;
}

int ObMicroBlockEncoder::init_pre_agg_columns()
{
  int ret = OB_SUCCESS;
  if (IS_NOT_INIT) {
    ret = OB_NOT_INIT;
  } else {
    PreAggColumn column;
    for (int64_t i = 0; OB_SUCC(ret) && i < ctx_.column_cnt_; ++i) {
      if (OB_FAIL(pre_agg_columns_.push_back(column))) {
      }
    }
  }
  return ret;
}

int ObMicroBlockEncoder::set_datum_rows_ptr()
{
  int ret = OB_SUCCESS;

  if (OB_UNLIKELY(row_buf_holder_.has_expand())) {
    for (int64_t i = 0; OB_SUCC(ret) && i < datum_rows_.count(); i++) {
      ObConstDatumRow &datum_row = datum_rows_.at(i);
      if (OB_FAIL(datum_row.set_datums_ptr(row_buf_holder_.data()))) {
      }
    }
  }

  return ret;
}

int ObMicroBlockEncoder::process_out_row_columns(const ObDatumRow &row)
{
  // make sure in&out row status of all values in a column are same
  int ret = OB_SUCCESS;
  if (!need_check_lob_) {
  } else if (OB_UNLIKELY(row.get_column_count() != pre_agg_columns_.count())) {
    ret = OB_ERR_UNEXPECTED;
  } else if (!has_lob_out_row_) {
    for (int64_t i = 0; !has_lob_out_row_ && OB_SUCC(ret) && i < row.get_column_count(); ++i) {
      ObStorageDatum &datum = row.storage_datums_[i];
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
  // uncomment this after varchar overflow supported
  //} else if (need_check_string_out) {
  //  if (!has_string_out_row_ && row.storage_datums_[i].is_outrow()) {
  //    has_string_out_row_ = true;
  //   }
  //}
  return ret;
}

int ObMicroBlockEncoder::copy_and_append_row(const ObDatumRow &src, int64_t &store_size)
{
  int ret = OB_SUCCESS;
  // performance critical, do not double check parameters in private method
  const int64_t datums_len = sizeof(ObDatum) * src.get_column_count();
  bool is_large_row = false;
  const int64_t datum_row_offset = length_;
  ObDatum *datum_arr = nullptr;
  if ((datum_rows_.count() >= ctx_.minimum_rows_) /* FORCE APPEND if rows count is less than minimum rows */
      && (length_ + datums_len >= estimate_size_limit_ || estimate_size_ >= estimate_size_limit_)) {
    ret = OB_BUF_NOT_ENOUGH;
  } else if (0 == datum_rows_.count() && length_ + datums_len >= estimate_size_limit_) {
    is_large_row = true;
  } else if (OB_FAIL(row_buf_holder_.ensure_space(datums_len))) {
  } else {
    bool is_finish = false;
    while(OB_SUCC(ret) && !is_finish) {
      char *datum_arr_buf = row_buf_holder_.data() + datum_row_offset;
      const int64_t buffer_size = row_buf_holder_.size();
      MEMSET(datum_arr_buf, 0, datums_len);
      datum_arr = reinterpret_cast<ObDatum *>(datum_arr_buf);
      length_ = datum_row_offset + datums_len;
      store_size = 0;
      is_finish = true;
      bool is_buffer_not_enough = false;
      for (int64_t col_idx = 0;
          OB_SUCC(ret) && col_idx < src.get_column_count() && !is_large_row; ++col_idx) {
        if (OB_FAIL(copy_cell(
            ctx_.col_descs_->at(col_idx),
            src.storage_datums_[col_idx],
            datum_arr[col_idx],
            store_size,
            is_large_row,
            is_buffer_not_enough))) {
          if (OB_UNLIKELY(OB_BUF_NOT_ENOUGH != ret)) {
          }
        } else if (is_buffer_not_enough) {
          int64_t var_len_column_cnt = 0;
          int64_t need_size = calc_datum_row_size(src, var_len_column_cnt);
          need_size = need_size + var_len_column_cnt * sizeof(uint64_t);

          if (OB_UNLIKELY(need_size <= row_buf_holder_.size() - row_buf_holder_.length())) {
            ret = OB_ERR_UNEXPECTED;
            STORAGE_LOG(WARN, "unexpected buffer not enough", K(ret), K(need_size), K(row_buf_holder_));
          } else if (OB_FAIL(row_buf_holder_.ensure_space(need_size))) {
          } else {
            is_finish = false;
            break;
          }
        }
      }
    }
    if (OB_SUCC(ret) && !is_large_row) {
      if (OB_FAIL(row_buf_holder_.write_nop(length_ - datum_row_offset))) {
      }
    }
  }

  if (OB_FAIL(ret)) {
  } else if (is_large_row && OB_FAIL(process_large_row(src, datum_arr, store_size))) {
  } else if (OB_FAIL(try_to_append_row(store_size))) {
    if (OB_UNLIKELY(OB_BUF_NOT_ENOUGH != ret)) {
    }
  } else {
    ObConstDatumRow datum_row(datum_arr, src.get_column_count(), datum_row_offset);
    if (OB_FAIL(datum_rows_.push_back(datum_row))) {
    }
  }
  return ret;
}

int ObMicroBlockEncoder::copy_cell(
    const ObColDesc &col_desc,
    const ObStorageDatum &src,
    ObDatum &dest,
    int64_t &store_size,
    bool &is_large_row,
    bool &is_buffer_not_enough)
{
  // For IntSC and UIntSC, normalize to 8 byte
  int ret = OB_SUCCESS;
  ObObjTypeStoreClass store_class = get_store_class_map()[col_desc.col_type_.get_type_class()];
  const bool is_int_sc = store_class == ObIntSC || store_class == ObUIntSC;
  int64_t datum_size = 0;
  is_buffer_not_enough = false;
  dest.ptr_ = row_buf_holder_.data() + length_; //reset by set_datum_rows_ptr
  dest.pack_ = src.pack_;
  if (src.is_null()) {
    dest.set_null();
    datum_size = sizeof(uint64_t);
  } else if (src.is_nop()) {
    datum_size = dest.len_;
  } else if (OB_UNLIKELY(src.is_ext())) {
    ret = OB_NOT_SUPPORTED;
  } else {
    datum_size = is_int_sc ? sizeof(uint64_t) : dest.len_;
    if (is_var_length_type(store_class)) {
      // For var-length type column, we need to add extra 8 bytes for estimate safety
      // e.g: ref size for dictionary, meta data for encoding etc.
      datum_size += sizeof(uint64_t);
    }
  }

  if (OB_FAIL(ret)) {
  } else if (FALSE_IT(store_size += datum_size)) {
  } else if ((datum_rows_.count() >= ctx_.minimum_rows_) /* FORCE APPEND if rows count is less than minimum rows */
             && (estimate_size_ + store_size >= estimate_size_limit_)) {
    ret = OB_BUF_NOT_ENOUGH;
    // for large row whose size larger than a micro block default size,
    // we still use micro block to store it, but its size is unknown, need special treat
  } else if (datum_rows_.count() == 0 && estimate_size_ + store_size >= estimate_size_limit_) {
    is_large_row = true;
  } else if (row_buf_holder_.size() < length_ + datum_size) {
    is_buffer_not_enough = true;
  } else {
    if (is_int_sc) {
      MEMSET(const_cast<char *>(dest.ptr_), 0, datum_size);
    }
    MEMCPY(const_cast<char *>(dest.ptr_), src.ptr_, dest.len_);
    length_ += datum_size;
  }
  return ret;
}

int ObMicroBlockEncoder::process_large_row(
    const ObDatumRow &src,
    ObDatum *&datum_arr,
    int64_t &store_size)
{
  int ret = OB_SUCCESS;
  // copy_size is the size in memory, including the ObDatum struct and extra space for data.
  // store_size represents for the serialized data size on disk,
  const int64_t datums_len = sizeof(ObDatum) * src.get_column_count();
  int64_t var_len_column_cnt = 0;
  int64_t copy_size = calc_datum_row_size(src, var_len_column_cnt);

  store_size = copy_size - datums_len + var_len_column_cnt * sizeof(uint64_t);
  if (OB_UNLIKELY(row_buf_holder_.length() != 0)) {
    ret = OB_ERR_UNEXPECTED;
    STORAGE_LOG(WARN, "unexpected row buffer len", K(ret), K(row_buf_holder_));
  } else if (OB_FAIL(row_buf_holder_.write_nop(copy_size))) {
  } else {
    char *buf = row_buf_holder_.data();
    MEMSET(buf, 0, datums_len);
    datum_arr = reinterpret_cast<ObDatum *>(buf);
    buf += datums_len;

    for (int64_t col_idx = 0; col_idx < src.get_column_count(); ++col_idx) {
      const ObColDesc &col_desc = ctx_.col_descs_->at(col_idx);
      ObObjTypeStoreClass store_class = get_store_class_map()[col_desc.col_type_.get_type_class()];
      const bool is_int_sc = (store_class == ObIntSC || store_class == ObUIntSC);
      ObStorageDatum &src_datum = src.storage_datums_[col_idx];
      ObDatum &dest_datum = datum_arr[col_idx];
      if (src_datum.is_null()) {
        dest_datum.set_null();
        MEMSET(buf, 0, sizeof(uint64_t));
        buf += sizeof(uint64_t);
      } else {
        dest_datum.ptr_ = buf;
        dest_datum.pack_ = src_datum.pack_;
        const int64_t datum_data_size = is_int_sc ? sizeof(uint64_t) : src_datum.len_;
        if (is_int_sc) {
          MEMSET(const_cast<char *>(dest_datum.ptr_), 0, datum_data_size);
        }
        MEMCPY(const_cast<char *>(dest_datum.ptr_), src_datum.ptr_, src_datum.len_);
        buf += datum_data_size;
      }
    }
  }
  return ret;
}

int64_t ObMicroBlockEncoder::calc_datum_row_size(const ObDatumRow &src, int64_t &var_len_column_cnt) const
{
  int64_t need_size = sizeof(ObDatum) * src.get_column_count();
  var_len_column_cnt = 0;

  for (int64_t col_idx = 0; col_idx < src.get_column_count(); ++col_idx) {
    const ObColDesc &col_desc = ctx_.col_descs_->at(col_idx);
    ObObjTypeStoreClass store_class = get_store_class_map()[col_desc.col_type_.get_type_class()];
    ObStorageDatum &datum = src.storage_datums_[col_idx];
    if (!datum.is_null()) {
      if (store_class == ObIntSC || store_class == ObUIntSC) {
        need_size += sizeof(uint64_t);
      } else {
        need_size += datum.len_;
        if (is_var_length_type(store_class)) {
          ++var_len_column_cnt;
        }
      }
    } else {
      need_size += sizeof(uint64_t);
    }
  }
  return need_size;
}

int ObMicroBlockEncoder::get_pre_agg_param(const int64_t col_idx, ObMicroDataPreAggParam &pre_agg_param) const
{
  int ret = OB_SUCCESS;
  if (IS_NOT_INIT) {
    ret = OB_NOT_INIT;
  } else if (OB_UNLIKELY(!block_generated_)) {
    ret = OB_ERR_UNEXPECTED;
  } else if (OB_UNLIKELY(col_idx >= ctx_.column_cnt_)) {
    ret = OB_INVALID_ARGUMENT;
  } else {
    pre_agg_param.reset();
    const PreAggColumn &pre_agg = pre_agg_columns_.at(col_idx);
    pre_agg_param.null_cnt_ = pre_agg.null_cnt_;
    pre_agg_param.col_datums_ = pre_agg.col_datums_;
  }
  return ret;
}

} // end namespace blocksstable
} // end namespace oceanbase
