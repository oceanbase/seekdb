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

#include "ob_micro_block_writer.h"
#include <new>
#include "rust/micro_block.h"
#include "storage/blocksstable/ob_data_store_desc.h"

namespace oceanbase
{
using namespace common;
using namespace storage;
namespace blocksstable
{
struct ObMicroBlockWriter::RustBlockBuffer
{
  RustBlockBuffer()
    : writer(micro_block::new_flat_writer()), bytes()
  {}
  rust::Box<micro_block::FlatWriter> writer;
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

ObMicroBlockWriter::ObMicroBlockWriter()
  :micro_block_size_limit_(0),
   column_count_(0),
   rowkey_column_count_(0),
   col_desc_array_(nullptr),
   row_count_(0),
   rust_block_(),
   column_checksums_(),
   hash_index_store_desc_(nullptr),
   previous_row_ended_key_(false),
   is_major_(false),
   is_inited_(false)
{
}

ObMicroBlockWriter::~ObMicroBlockWriter()
{
}

int ObMicroBlockWriter::init(const int64_t micro_block_size_limit,
                             const int64_t rowkey_column_count,
                             const int64_t column_count)
{
  int ret = OB_SUCCESS;
  reset();
  if (micro_block_size_limit <= 0 || rowkey_column_count <= 0 || column_count <= 0
      || column_count < rowkey_column_count) {
    ret = OB_INVALID_ARGUMENT;
    STORAGE_LOG(WARN,
                "micro block writer fail to check input param.",
                K(ret),
                K(micro_block_size_limit),
                K(column_count),
                K(rowkey_column_count));
  } else {
    micro_block_size_limit_ = micro_block_size_limit;
    rowkey_column_count_ = rowkey_column_count;
    column_count_ = column_count;
    is_major_ = false;
    need_check_lob_ = true;
    col_desc_array_ = nullptr;
    is_inited_ = true;
  }
  return ret;
}

int ObMicroBlockWriter::init(const ObDataStoreDesc *data_store_desc)
{
  int ret = OB_SUCCESS;
  reset();
  if (OB_ISNULL(data_store_desc) || OB_UNLIKELY(!data_store_desc->is_valid())) {
    ret = OB_INVALID_ARGUMENT;
    STORAGE_LOG(WARN,
                "micro block writer fail to check input param.",
                KR(ret),
                KPC(data_store_desc));
  } else {
    micro_block_size_limit_ = data_store_desc->get_micro_block_size_limit();
    rowkey_column_count_ = data_store_desc->get_rowkey_column_count();
    column_count_ = data_store_desc->get_row_column_count();
    // major need calc column checksum
    is_major_ = data_store_desc->is_major_merge_type();
    // there are only rowkey column descs during minor merge, so we should alwasy check lob storage -_-
    need_check_lob_ = true;
    if (is_major_) {
      if (OB_NOT_NULL(col_desc_array_ = &data_store_desc->get_rowkey_col_descs())) {
        for (int64_t i = 0; OB_SUCC(ret) && !need_check_lob_ && i < col_desc_array_->count(); i++) {
          need_check_lob_ = col_desc_array_->at(i).col_type_.is_lob_storage();
        }
      } else {
        need_check_lob_ = false;
      }
    }
    if (OB_NOT_NULL(col_desc_array_ = &data_store_desc->get_rowkey_col_descs())) {
      if (FAILEDx(checksum_helper_.init(col_desc_array_, data_store_desc->contain_full_col_descs()))) {
      }
    }
    if (OB_SUCC(ret)) {
      if (OB_FAIL(init_hash_index_policy(*data_store_desc))) {
      } else {
        is_inited_ = true;
      }
    }
  }
  return ret;
}

int ObMicroBlockWriter::inner_init()
{
  int ret = OB_SUCCESS;
  if (OB_UNLIKELY(!is_inited_)) {
    ret = OB_NOT_INIT;
    STORAGE_LOG(WARN, "not init", K(ret));
  } else if (0 == row_count_) {
    column_checksums_.reuse();
    if (!rust_block_) {
      rust_block_.reset(new (std::nothrow) RustBlockBuffer());
      if (!rust_block_) {
        ret = OB_ALLOCATE_MEMORY_FAILED;
      }
    }
    if (OB_SUCC(ret)) {
      if (OB_FAIL(rust_code_to_ret(micro_block::init_flat_writer(
          *rust_block_->writer,
          static_cast<uint32_t>(column_count_),
          static_cast<uint32_t>(rowkey_column_count_),
          static_cast<uint64_t>(micro_block_size_limit_),
          static_cast<uint64_t>(block_size_upper_bound_),
          is_major_, nullptr != hash_index_store_desc_)))) {
      } else if (is_major_ && OB_FAIL(column_checksums_.reserve(column_count_))) {
      } else if (is_major_) {
        for (int64_t i = 0; OB_SUCC(ret) && i < column_count_; ++i) {
          if (OB_FAIL(column_checksums_.push_back(0))) {
          }
        }
      }
    }
  }
  return ret;
}

int ObMicroBlockWriter::process_out_row_columns(const ObDatumRow &row)
{
  int ret = OB_SUCCESS;

  if (!need_check_lob_) {
  } else if (!is_major_) {
    has_lob_out_row_ = true;
  } else if (OB_UNLIKELY(nullptr == col_desc_array_ || row.get_column_count() != col_desc_array_->count())) {
    ret = OB_ERR_UNEXPECTED;
    STORAGE_LOG(WARN ,"unexpected column count not match", K(ret), K(need_check_lob_), K(row), KPC(col_desc_array_));
  } else if (!has_lob_out_row_) {
    for (int64_t i = 0; !has_lob_out_row_ && OB_SUCC(ret) && i < row.get_column_count(); ++i) {
      ObStorageDatum &datum = row.storage_datums_[i];
      if (col_desc_array_->at(i).col_type_.is_lob_storage()) {
        if (datum.is_nop() || datum.is_null()) {
        } else if (datum.len_ < sizeof(ObLobCommon)) {
          ret = OB_ERR_UNEXPECTED;
          STORAGE_LOG(WARN, "Unexpected lob datum len", K(ret), K(i), K(col_desc_array_->at(i).col_type_), K(datum));
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

int ObMicroBlockWriter::append_row(const ObDatumRow &row)
{
  int ret = OB_SUCCESS;
  common::ObSEArray<micro_block::InputCell, 64> cells;
  if(!is_inited_) {
    ret = OB_NOT_INIT;
    STORAGE_LOG(WARN, "should init writer before append row", K(ret));
  } else if (!row.is_valid()) {
    ret = OB_INVALID_ARGUMENT;
    STORAGE_LOG(WARN, "row was invalid", K(row), K(ret));
  } else if (OB_FAIL(inner_init())) {
  } else if (OB_FAIL(process_out_row_columns(row))) {
  } else if (OB_UNLIKELY(row.get_column_count() != column_count_)) {
    ret = OB_INVALID_ARGUMENT;
    STORAGE_LOG(WARN, "append row column count is not consistent with init column count",
        K(column_count_), K(row.get_column_count()), K(ret));
  } else if (OB_FAIL(cells.reserve(column_count_))) {
  } else {
    for (int64_t i = 0; OB_SUCC(ret) && i < row.get_column_count(); ++i) {
      const ObStorageDatum &datum = row.storage_datums_[i];
      micro_block::InputCell cell;
      cell.kind = datum.is_null() ? micro_block::InputKind::Null
          : (datum.is_nop() ? micro_block::InputKind::Nop
          : (datum.is_outrow() ? micro_block::InputKind::Outrow : micro_block::InputKind::Bytes));
      cell.value = 0;
      if (OB_UNLIKELY(datum.len_ < 0)) {
        ret = OB_INVALID_DATA;
      } else {
        cell.length = micro_block::InputKind::Bytes == cell.kind
                || micro_block::InputKind::Outrow == cell.kind ? datum.len_ : 0;
        cell.pointer = 0 == cell.length ? 0 : reinterpret_cast<std::size_t>(datum.ptr_);
        if (OB_UNLIKELY(cell.length > 0 && 0 == cell.pointer)) {
          ret = OB_INVALID_DATA;
        } else if (OB_FAIL(cells.push_back(cell))) {
        }
      }
    }
    if (OB_FAIL(ret)) {
      return ret;
    }
    micro_block::FlatRowMeta row_meta;
    row_meta.row_flag = row.row_flag_.get_serialize_flag();
    row_meta.multi_version_flag = row.mvcc_row_flag_.flag_;
    row_meta.trans_id = row.trans_id_.get_id();
    const rust::Slice<const micro_block::InputCell> input(cells.begin(), cells.count());
    micro_block::FlatAppendResult result = micro_block::append_flat_row(
        *rust_block_->writer, input, row_meta,
        static_cast<uint32_t>(rowkey_column_count_),
        static_cast<uint64_t>(block_size_upper_bound_));
    if (OB_FAIL(rust_code_to_ret(result.code))) {
    } else if (OB_UNLIKELY(result.accepted_rows != static_cast<std::size_t>(row_count_)
                           + (result.is_full ? 0 : 1))) {
      ret = OB_INVALID_DATA;
    } else if (result.is_full) {
      ret = OB_BUF_NOT_ENOUGH;
    } else if (FALSE_IT(++row_count_)) {
    } else if (micro_block::FlatHashWriteState::Disabled != result.hash_state
               && OB_FAIL(append_row_to_hash_index(
                   row, micro_block::FlatHashWriteState::Empty == result.hash_state))) {
    } else if (is_major_ && OB_FAIL(checksum_helper_.cal_column_checksum(
        row, &column_checksums_[0]))) {
      STORAGE_LOG(WARN, "fail to cal column chksum", K(ret), K(row));
    } else {
      cal_row_stat(row);
      if (need_cal_row_checksum()
          && OB_FAIL(checksum_helper_.cal_row_checksum(row.storage_datums_, row.get_column_count()))) {
        STORAGE_LOG(WARN, "fail to cal row chksum", K(ret), K(row));
      }
    }
  }
  return ret;
}

int ObMicroBlockWriter::build_block(char *&buf, int64_t &size)
{
  int ret = OB_SUCCESS;
  if(!is_inited_){
    ret = OB_NOT_INIT;
    STORAGE_LOG(WARN, "should init writer before append row", K(ret));
  } else if (OB_UNLIKELY(!rust_block_ || 0 == row_count_)) {
    ret = OB_ERR_UNEXPECTED;
    STORAGE_LOG(WARN, "unexpected empty block", K(ret));
  } else {
    static const uint64_t empty_checksum = 0;
    micro_block::FlatFinishOptionsDesc options;
    options.single_version_rows = last_rows_count_ == row_count_;
    options.contain_uncommitted_rows = contain_uncommitted_row_;
    options.is_last_row_last_flag = is_last_row_last_flag_;
    options.has_string_out_row = has_string_out_row_;
    options.all_lob_in_row = !has_lob_out_row_;
    options.has_min_merged_trans_version = !is_major_;
    options.min_merged_trans_version = min_merged_trans_version_;
    options.max_merged_trans_version = max_merged_trans_version_;
    const uint64_t *checksums = is_major_
        ? reinterpret_cast<const uint64_t *>(&column_checksums_[0]) : &empty_checksum;
    micro_block::BuildResult result = micro_block::finish_flat_writer(
        *rust_block_->writer, options,
        rust::Slice<const uint64_t>(checksums, column_checksums_.count()));
    if (OB_FAIL(rust_code_to_ret(result.code))) {
    } else if (OB_UNLIKELY(result.accepted_rows != static_cast<std::size_t>(row_count_)
                           || result.block.empty()
                           || result.block.size() > static_cast<std::size_t>(INT64_MAX))) {
      ret = OB_INVALID_DATA;
    } else {
      rust_block_->bytes = std::move(result.block);
      buf = reinterpret_cast<char *>(rust_block_->bytes.data());
      size = rust_block_->bytes.size();
    }
  }
  return ret;
}

void ObMicroBlockWriter::reset()
{
  ObIMicroBlockWriter::reset();
  micro_block_size_limit_ = 0;
  column_count_ = 0;
  rowkey_column_count_ = 0;
  row_count_ = 0;
  is_major_ = false;
  rust_block_.reset();
  column_checksums_.reset();
  hash_index_store_desc_ = nullptr;
  previous_row_ended_key_ = false;
  col_desc_array_ = nullptr;
  is_inited_ = false;
}

void ObMicroBlockWriter::reuse()
{
  ObIMicroBlockWriter::reuse();
  if (rust_block_) {
    rust_block_->bytes = rust::Vec<uint8_t>();
  }
  column_checksums_.reuse();
  previous_row_ended_key_ = false;
  row_count_ = 0;
}

int64_t ObMicroBlockWriter::get_block_size() const
{
  int64_t size = 0;
  if (0 == row_count_) {
    const uint64_t empty_size = micro_block::flat_writer_empty_size(column_count_, is_major_);
    if (empty_size <= static_cast<uint64_t>(INT64_MAX)) {
      size = empty_size;
    }
  } else if (rust_block_) {
    const micro_block::WriterStatsResult stats = micro_block::flat_writer_stats(*rust_block_->writer);
    if (micro_block::Code::Ok == stats.code && stats.block_size <= static_cast<uint64_t>(INT64_MAX)) {
      size = stats.block_size;
    }
  }
  return size;
}

int64_t ObMicroBlockWriter::get_original_size() const
{
  int64_t size = 0;
  if (row_count_ > 0 && rust_block_) {
    const micro_block::WriterStatsResult stats = micro_block::flat_writer_stats(*rust_block_->writer);
    if (micro_block::Code::Ok == stats.code && stats.original_size <= static_cast<uint64_t>(INT64_MAX)) {
      size = stats.original_size;
    }
  }
  return size;
}

int ObMicroBlockWriter::init_hash_index_policy(const ObDataStoreDesc &data_store_desc)
{
  int ret = OB_SUCCESS;
  if (data_store_desc.get_tablet_id().is_user_tablet()
      && !data_store_desc.is_major_or_meta_merge_type()
      && !data_store_desc.is_for_index_or_meta()) {
    const auto &rowkey_cols = data_store_desc.get_rowkey_col_descs();
    const int64_t rowkey_count = data_store_desc.get_schema_rowkey_col_cnt();
    if (OB_UNLIKELY(rowkey_count > rowkey_cols.count())) {
      ret = OB_ERR_UNEXPECTED;
      STORAGE_LOG(WARN, "unexpected schema rowkey column count", K(ret), K(rowkey_count), K(rowkey_cols));
    } else {
      constexpr int64_t MIN_INTEGER_COLUMNS_FOR_HASH_INDEX = 3;
      bool should_index = rowkey_count >= MIN_INTEGER_COLUMNS_FOR_HASH_INDEX;
      for (int64_t i = 0; !should_index && i < rowkey_count; ++i) {
        should_index = !rowkey_cols.at(i).col_type_.is_integer_type();
      }
      if (should_index) {
        hash_index_store_desc_ = &data_store_desc;
      }
    }
  }
  return ret;
}

int ObMicroBlockWriter::append_row_to_hash_index(const ObDatumRow &row, bool hash_index_empty)
{
  int ret = OB_SUCCESS;
  const int64_t rowkey_count = hash_index_store_desc_->get_schema_rowkey_col_cnt();
  if (OB_UNLIKELY(!row.is_valid() || row.get_column_count() < rowkey_count)) {
    ret = OB_INVALID_ARGUMENT;
  } else if (!row.is_ghost_row() && (previous_row_ended_key_ || hash_index_empty)) {
    ObDatumRowkey rowkey;
    uint64_t hash = 0;
    if (OB_FAIL(rowkey.assign(row.storage_datums_, rowkey_count))) {
    } else if (OB_FAIL(rowkey.murmurhash(0, hash_index_store_desc_->get_datum_utils(), hash))) {
    } else if (OB_FAIL(rust_code_to_ret(micro_block::add_flat_hash_entry(
        *rust_block_->writer, static_cast<std::size_t>(row_count_ - 1), hash)))) {
    }
  }
  if (OB_SUCC(ret)) {
    previous_row_ended_key_ = row.mvcc_row_flag_.is_last_multi_version_row();
  } else {
    micro_block::disable_flat_hash_index(*rust_block_->writer);
    hash_index_store_desc_ = nullptr;
    STORAGE_LOG(WARN, "failed to add row to flat hash index", KR(ret), K(row));
  }
  return ret;
}

}//end namespace blocksstable
}//end namespace oceanbase

