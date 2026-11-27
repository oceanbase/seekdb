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
#include "ob_micro_block_reader.h"
#include "rust/micro_block.h"
#include <optional>
#include "storage/access/ob_aggregated_store.h"
#include "storage/access/ob_table_access_context.h"
#include "storage/access/ob_table_access_param.h"
#include "storage/access/ob_table_access_context.h"
#include "storage/truncate_info/ob_truncate_filter_evaluator.h"

namespace oceanbase
{
using namespace common;
using namespace storage;
namespace blocksstable
{
namespace
{
int flat_reader_status(micro_block::Code code)
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
    case micro_block::Code::OutputTooSmall:
      return OB_BUF_NOT_ENOUGH;
    case micro_block::Code::SizeOverflow:
      return OB_SIZE_OVERFLOW;
    case micro_block::Code::Unsupported:
      return OB_NOT_SUPPORTED;
    default:
      return OB_ERR_UNEXPECTED;
  }
}

rust::Slice<const uint8_t> flat_block_bytes(const ObMicroBlockData &block)
{
  return {reinterpret_cast<const uint8_t *>(block.get_buf()),
          static_cast<size_t>(block.get_buf_size())};
}

int read_flat_row_metadata(
    const ObMicroBlockHeader &header,
    const ObMicroBlockData &block_data,
    int64_t row_idx,
    MicroBlockRowMeta &row_meta)
{
  if (OB_UNLIKELY(!block_data.is_valid() || row_idx < 0 || row_idx >= header.row_count_)) {
    return OB_INVALID_ARGUMENT;
  }
  const auto result = micro_block::read_flat_row_meta(flat_block_bytes(block_data), row_idx);
  const int ret = flat_reader_status(result.code);
  if (OB_SUCCESS == ret) {
    row_meta.row_flag_ = ObDmlRowFlag(result.meta.row_flag);
    row_meta.mvcc_flag_ = ObMultiVersionRowFlag(result.meta.multi_version_flag);
    row_meta.trans_id_ = result.meta.trans_id;
  }
  return ret;
}

int materialize_flat_cell(
    rust::Slice<const uint8_t> block,
    const micro_block::CellDesc &cell,
    ObStorageDatum &datum)
{
  int ret = OB_SUCCESS;
  datum.reuse();
  switch (cell.kind) {
    case micro_block::CellKind::Null:
      datum.set_null();
      break;
    case micro_block::CellKind::Nop:
      datum.set_nop();
      break;
    case micro_block::CellKind::UInt:
      datum.set_uint(cell.value);
      break;
    case micro_block::CellKind::Borrowed:
    case micro_block::CellKind::Outrow:
      if (cell.offset > block.size() || cell.length > block.size() - cell.offset) {
        ret = OB_INVALID_DATA;
      } else if (OB_FAIL(datum.from_buf_enhance(
                     reinterpret_cast<const char *>(block.data() + cell.offset), cell.length))) {
      } else if (micro_block::CellKind::Outrow == cell.kind) {
        datum.set_outrow();
      }
      break;
    default:
      ret = OB_INVALID_DATA;
      break;
  }
  return ret;
}

int read_flat_column(
    const ObMicroBlockHeader &header,
    const ObMicroBlockData &block_data,
    int64_t row_idx,
    int64_t column_idx,
    ObStorageDatum &datum)
{
  int ret = OB_SUCCESS;
  if (OB_UNLIKELY(row_idx < 0 || row_idx >= header.row_count_)) {
    ret = OB_INVALID_ARGUMENT;
  } else if (column_idx < 0 || column_idx >= header.column_count_) {
    datum.set_nop();
  } else {
    const auto block = flat_block_bytes(block_data);
    const auto result = micro_block::read_flat_cell(block, row_idx, column_idx);
    if (OB_FAIL(flat_reader_status(result.code))) {
    } else {
      ret = materialize_flat_cell(block, result.cell, datum);
    }
  }
  return ret;
}

struct FlatRowkeyComparison
{
  static int32_t compare(
      void *context,
      rust::Slice<const micro_block::CellDesc> cells,
      rust::Slice<const uint8_t> decoded,
      int32_t &ordering) noexcept
  {
    auto &self = *static_cast<FlatRowkeyComparison *>(context);
    if (cells.size() != self.key_.get_datum_cnt() || !decoded.empty()) {
      return OB_INVALID_DATA;
    }
    int ret = OB_SUCCESS;
    int32_t stored_ordering = 0;
    for (size_t i = 0; OB_SUCC(ret) && stored_ordering == 0 && i < cells.size(); ++i) {
      const auto &cell = cells[i];
      ObStorageDatum datum;
      switch (cell.kind) {
        case micro_block::CellKind::Null:
          datum.set_null();
          break;
        case micro_block::CellKind::Nop:
          datum.set_nop();
          break;
        case micro_block::CellKind::UInt:
          datum.set_uint(cell.value);
          break;
        case micro_block::CellKind::Borrowed:
          if (cell.offset > self.block_size_ || cell.length > self.block_size_ - cell.offset) {
            ret = OB_INVALID_DATA;
          } else {
            ret = datum.from_buf_enhance(self.block_ + cell.offset, cell.length);
          }
          break;
        default:
          ret = OB_ERR_UNEXPECTED;
          break;
      }
      if (OB_SUCC(ret)) {
        ret = self.datum_utils_.get_cmp_funcs().at(i).compare(
            datum, self.key_.datums_[i], stored_ordering);
      }
    }
    ordering = (stored_ordering < 0) - (stored_ordering > 0);
    return ret;
  }

  const char *block_;
  size_t block_size_;
  const ObDatumRowkey &key_;
  const ObStorageDatumUtils &datum_utils_;
};
int search_flat_rowkey(
    const ObMicroBlockHeader &header,
    const ObMicroBlockData &block_data,
    const ObDatumRowkey &key,
    const ObStorageDatumUtils &datum_utils,
    micro_block::RowkeySearch search,
    int64_t begin_idx,
    int64_t end_idx,
    micro_block::LocateResult &result)
{
  if (OB_UNLIKELY(!block_data.is_valid())) {
    return OB_NOT_INIT;
  } else if (OB_UNLIKELY(!key.is_valid() || !datum_utils.is_valid()
                         || key.get_datum_cnt() > datum_utils.get_rowkey_count()
                         || begin_idx < 0 || begin_idx > end_idx || end_idx > header.row_count_)) {
    return OB_INVALID_ARGUMENT;
  }
  FlatRowkeyComparison context{
      block_data.get_buf(), static_cast<size_t>(block_data.get_buf_size()), key, datum_utils};
  const micro_block::RowkeyComparator comparator(&context, FlatRowkeyComparison::compare);
  result = micro_block::locate_rowkey_by(
      flat_block_bytes(block_data),
      header.row_store_type_, key.get_datum_cnt(), search, begin_idx, end_idx, comparator);
  if (OB_SUCCESS != result.compare_error) {
    return result.compare_error;
  }
  return flat_reader_status(result.code);
}

int compare_flat_rowkey(
    const ObMicroBlockHeader &header,
    const ObMicroBlockData &block_data,
    const ObDatumRowkey &rowkey,
    const ObStorageDatumUtils &datum_utils,
    int64_t row_idx,
    int32_t &ordering)
{
  micro_block::LocateResult result;
  const int ret = search_flat_rowkey(
      header, block_data, rowkey, datum_utils, micro_block::RowkeySearch::Compare,
      row_idx, row_idx + 1, result);
  if (OB_SUCCESS == ret) {
    ordering = -result.ordering;
  }
  return ret;
}
} // namespace

ObIMicroBlockFlatReader::ObIMicroBlockFlatReader()
  : allocator_(ObModIds::OB_STORE_ROW_GETTER)
{
}

ObIMicroBlockFlatReader::~ObIMicroBlockFlatReader()
{
  reset();
}

void ObIMicroBlockFlatReader::reset()
{
  metadata_.reset();
  block_data_.reset();
}

int ObIMicroBlockFlatReader::find_bound_(
    const ObDatumRowkey &key,
    const bool lower_bound,
    const int64_t begin_idx,
    const int64_t end_idx,
    const ObStorageDatumUtils &datum_utils,
    int64_t &row_idx,
    bool &equal)
{
  equal = false;
  row_idx = ObIMicroBlockReaderInfo::INVALID_ROW_INDEX;
  micro_block::LocateResult result;
  const auto search = lower_bound
      ? micro_block::RowkeySearch::LowerBound
      : micro_block::RowkeySearch::UpperBound;
  const int ret = search_flat_rowkey(
      metadata_, block_data_, key, datum_utils, search, begin_idx, end_idx, result);
  if (OB_SUCCESS == ret) {
    row_idx = result.row;
    equal = result.equal;
  }
  return ret;
}

int ObIMicroBlockFlatReader::init(const ObMicroBlockData &block_data)
{
  ObIMicroBlockFlatReader::reset();
  if (OB_UNLIKELY(!block_data.is_valid())) {
    return OB_INVALID_ARGUMENT;
  }
  if (OB_UNLIKELY(OB_SUCCESS != block_data.get_expanded_micro_header(metadata_)
                  || FLAT_ROW_STORE != metadata_.row_store_type_)) {
    return OB_INVALID_DATA;
  }
  block_data_ = block_data;
  return OB_SUCCESS;
}

/*
 * ObMicroBlockGetReader
 * */
int ObMicroBlockGetReader::inner_init(
    const ObMicroBlockData &block_data,
    const ObITableReadInfo &read_info,
    const ObDatumRowkey &rowkey)
{
  int ret = OB_SUCCESS;
  if (OB_FAIL(ObIMicroBlockFlatReader::init(block_data))) {
  } else {
    row_count_ = metadata_.row_count_;
    original_data_length_ = metadata_.original_length_;
    read_info_ = &read_info;
    is_inited_ = true;
  }
  return ret;
}

int ObMicroBlockGetReader::get_row(
    const ObMicroBlockData &block_data,
    const ObDatumRowkey &rowkey,
    const ObITableReadInfo &read_info,
    ObDatumRow &row)
{
  int ret = OB_SUCCESS;
  int64_t row_idx;
  if (OB_UNLIKELY(!read_info.is_valid())) {
    ret = OB_INVALID_ARGUMENT;
  } else if (OB_FAIL(inner_init(block_data, read_info, rowkey))) {
  } else if (OB_FAIL(locate_rowkey(rowkey, row_idx))){
    if (OB_BEYOND_THE_RANGE != ret) {
    }
  } else if (OB_FAIL(read_row(&read_info, row_idx, row))) {
  } else {
    row.fast_filter_skipped_ = false;
  }
  return ret;
}

int ObMicroBlockGetReader::exist_row(
    const ObMicroBlockData &block_data,
    const ObDatumRowkey &rowkey,
    const ObITableReadInfo &read_info,
    bool &exist,
    bool &found)
{
  int ret = OB_SUCCESS;
  if (OB_FAIL(inner_init(block_data, read_info, rowkey))) {
  } else {
    int64_t row_idx;
    exist = false;
    found = false;
    if (OB_FAIL(locate_rowkey(rowkey, row_idx))){
      if (OB_UNLIKELY(OB_BEYOND_THE_RANGE != ret)) {
      } else {
        ret = OB_SUCCESS;
      }
    } else {
      MicroBlockRowMeta row_meta;
      if (OB_FAIL(read_flat_row_metadata(metadata_, block_data_, row_idx, row_meta))) {
      } else {
        exist = !row_meta.get_row_flag().is_delete();
        found = true;
      }
    }
  }
  return ret;
}

int ObMicroBlockGetReader::locate_rowkey(const ObDatumRowkey &rowkey, int64_t &row_idx)
{
  int ret = OB_SUCCESS;
  bool need_binary_search = false;
  bool found = false;
  if (IS_NOT_INIT) {
    ret = OB_NOT_INIT;
  } else if (OB_FAIL(locate_rowkey_fast_path(rowkey, row_idx, need_binary_search, found))) {
  } else if (need_binary_search) {
    bool is_equal = false;
    if (OB_FAIL(ObIMicroBlockFlatReader::find_bound_(rowkey, true/*lower_bound*/, 0, row_count_,
        read_info_->get_datum_utils(), row_idx, is_equal))) {
    } else if (row_count_ == row_idx || !is_equal) {
      row_idx = ObIMicroBlockReaderInfo::INVALID_ROW_INDEX;
      ret = OB_BEYOND_THE_RANGE;
    } else {
      MicroBlockRowMeta row_meta;
      if (OB_FAIL(read_flat_row_metadata(metadata_, block_data_, row_idx, row_meta))) {
      } else if (row_meta.get_row_multi_version_flag().is_ghost_row()) {
        row_idx = ObIMicroBlockReaderInfo::INVALID_ROW_INDEX;
        ret = OB_BEYOND_THE_RANGE;
      }
    }
  } else if (!found) {
    ret = OB_BEYOND_THE_RANGE;
  }
  return ret;
}

int ObMicroBlockGetReader::locate_rowkey_fast_path(const ObDatumRowkey &rowkey,
                                                   int64_t &row_idx,
                                                   bool &need_binary_search,
                                                   bool &found)
{
  int ret = OB_SUCCESS;
  need_binary_search = false;
  if (!metadata_.is_contain_hash_index()) {
    need_binary_search = true;
  } else {
    uint64_t hash_value = 0;
    const auto &datum_utils = read_info_->get_datum_utils();
    if (OB_FAIL(rowkey.murmurhash(0, datum_utils, hash_value))) {
    } else {
      const auto bucket = micro_block::lookup_flat_hash(flat_block_bytes(block_data_), hash_value);
      if (OB_FAIL(flat_reader_status(bucket.code))) {
      } else {
        switch (bucket.kind) {
          case micro_block::FlatHashKind::Vacant:
            row_idx = ObIMicroBlockReaderInfo::INVALID_ROW_INDEX;
            found = false;
            break;
          case micro_block::FlatHashKind::Row: {
            int32_t compare_result = 0;
            if (OB_FAIL(compare_flat_rowkey(metadata_, block_data_, rowkey, datum_utils, bucket.row, compare_result))) {
            } else {
              found = compare_result == 0;
              row_idx = found ? bucket.row : ObIMicroBlockReaderInfo::INVALID_ROW_INDEX;
            }
            break;
          }
          case micro_block::FlatHashKind::Collision:
            need_binary_search = true;
            break;
          default:
            ret = OB_ERR_UNEXPECTED;
            break;
        }
      }
    }
  }
  return ret;
}

int ObMicroBlockGetReader::get_row(
    const ObMicroBlockData &block_data,
    const ObITableReadInfo &read_info,
    const uint32_t row_idx,
    ObDatumRow &row)
{
  int ret = OB_SUCCESS;
  if (OB_UNLIKELY(!read_info.is_valid())) {
    ret = OB_INVALID_ARGUMENT;
  } else if (OB_FAIL(ObIMicroBlockFlatReader::init(block_data))) {
  } else {
    row_count_ = metadata_.row_count_;
    read_info_ = &read_info;
    is_inited_ = true;

    if (OB_UNLIKELY(row_idx >= row_count_)) {
      ret = OB_ERR_UNEXPECTED;
    } else if (OB_FAIL(read_row(&read_info, row_idx, row))) {
    } else {
      row.fast_filter_skipped_ = false;
    }
  }
  return ret;
}

int ObMicroBlockGetReader::get_row_id(
    const ObMicroBlockData &block_data,
    const ObDatumRowkey &rowkey,
    const ObITableReadInfo &read_info,
    int64_t &row_id)
{
  int ret = OB_SUCCESS;
  if (OB_UNLIKELY(!read_info.is_valid())) {
    ret = OB_INVALID_ARGUMENT;
  } else if (OB_FAIL(inner_init(block_data, read_info, rowkey))) {
  } else if (OB_FAIL(locate_rowkey(rowkey, row_id))){
    if (OB_BEYOND_THE_RANGE != ret) {
    }
  }
  return ret;
}

/***************             ObMicroBlockReader              ****************/
void ObMicroBlockReader::reset()
{
  ObIMicroBlockFlatReader::reset();
  ObIMicroBlockReader::reset();
  allocator_.reuse();
}

int ObMicroBlockReader::init(
    const ObMicroBlockData &block_data,
    const ObITableReadInfo &read_info)
{
  int ret = OB_SUCCESS;
  if (IS_INIT) {
    reset();
  }
  if (OB_UNLIKELY(!read_info.is_valid())) {
    ret = OB_INVALID_ARGUMENT;
  } else if (OB_FAIL(ObIMicroBlockFlatReader::init(block_data))) {
  } else {
    row_count_ = metadata_.row_count_;
    original_data_length_ = metadata_.original_length_;
    read_info_ = &read_info;
    datum_utils_ = &(read_info.get_datum_utils());
    is_inited_ = true;
  }

  if (IS_NOT_INIT) {
    reset();
  }
  return ret;
}

int ObMicroBlockReader::init(
    const ObMicroBlockData &block_data,
	const ObStorageDatumUtils *datum_utils)
{
  int ret = OB_SUCCESS;
  if (IS_INIT) {
    reset();
  }
  if (OB_FAIL(ObIMicroBlockFlatReader::init(block_data))) {
  } else {
    row_count_ = metadata_.row_count_;
    original_data_length_ = metadata_.original_length_;
    read_info_ = nullptr;
    datum_utils_ = datum_utils;
    is_inited_ = true;
  }

  if (IS_NOT_INIT) {
    reset();
  }
  return ret;
}

int ObMicroBlockReader::compare_rowkey(
    const ObDatumRowkey &rowkey,
    const int64_t idx,
    int32_t &compare_result)
{
  int ret = OB_SUCCESS;
  if (IS_NOT_INIT) {
    ret = OB_NOT_INIT;
  } else if (OB_UNLIKELY(!rowkey.is_valid() || idx < 0 || idx >= row_count_)) {
    ret = OB_INVALID_ARGUMENT;
  } else if (OB_UNLIKELY(nullptr == datum_utils_)) {
    ret = OB_INVALID_ARGUMENT;
  } else {
    ret = compare_flat_rowkey(metadata_, block_data_, rowkey, *datum_utils_, idx, compare_result);
  }
  return ret;
}

int ObMicroBlockReader::find_bound(
                 const ObDatumRowkey &key,
                 const bool lower_bound,
                 const int64_t begin_idx,
                 const int64_t end_idx,
                 int64_t &row_idx,
                 bool &equal)
{
  int ret = OB_SUCCESS;
  if (IS_NOT_INIT) {
    ret = OB_NOT_INIT;
  } else if (OB_UNLIKELY(!key.is_valid() || begin_idx < 0 || end_idx > row_count_ || nullptr == read_info_)) {
    ret = OB_INVALID_ARGUMENT;
  } else if (OB_FAIL(ObIMicroBlockFlatReader::find_bound_(
          key,
          lower_bound,
          begin_idx,
          end_idx,
          *datum_utils_,
          row_idx,
          equal))) {
  }
  return ret;
}

int ObMicroBlockReader::find_bound(
    const ObDatumRowkey &key,
    const bool lower_bound,
    const int64_t begin_idx,
    int64_t &row_idx,
    bool &equal)
{
  int ret = OB_SUCCESS;
  if (IS_NOT_INIT) {
    ret = OB_NOT_INIT;
  } else if (OB_UNLIKELY(!key.is_valid() || begin_idx < 0 || nullptr == datum_utils_ || begin_idx >= row_count_)) {
    ret = OB_INVALID_ARGUMENT;
  } else if (OB_FAIL(ObIMicroBlockFlatReader::find_bound_(
          key,
          lower_bound,
          begin_idx,
          row_count_,
          *datum_utils_,
          row_idx,
          equal))) {
  }
  return ret;
}

int ObMicroBlockReader::find_bound_through_linear_search(
    const ObDatumRowkey &rowkey,
    const int64_t begin_idx,
    int64_t &row_idx)
{
  int ret = OB_SUCCESS;
  if (IS_NOT_INIT) {
    ret = OB_NOT_INIT;
  } else if (OB_UNLIKELY(!rowkey.is_valid() || nullptr == datum_utils_
                         || begin_idx < -1 || begin_idx >= row_count_)) {
    ret = OB_INVALID_ARGUMENT;
  } else {
    micro_block::LocateResult result;
    if (OB_FAIL(search_flat_rowkey(
            metadata_, block_data_, rowkey, *datum_utils_, micro_block::RowkeySearch::EqualRunEnd,
            begin_idx + 1, row_count_, result))) {
    } else {
      row_idx = static_cast<int64_t>(result.row) - 1;
    }
  }
  return ret;
}

int ObMicroBlockReader::find_bound(
    const ObDatumRange &range,
    const int64_t begin_idx,
    int64_t &row_idx,
    bool &equal,
    int64_t &end_key_begin_idx,
    int64_t &end_key_end_idx)
{
  UNUSEDx(end_key_begin_idx, end_key_end_idx);
  return find_bound(range.get_start_key(), true, begin_idx, row_idx, equal);
}

int ObMicroBlockReader::get_row(const int64_t index, ObDatumRow &row)
{
  int ret = OB_SUCCESS;
  if(IS_NOT_INIT){
    ret = OB_NOT_INIT;
  } else if(OB_UNLIKELY(!block_data_.is_valid() ||
                        index < 0 || index >= metadata_.row_count_ ||
                        !row.is_valid())) {
    ret = OB_INVALID_ARGUMENT;
  } else if (OB_FAIL(read_row(read_info_, index, row))) {
  } else {
    row.fast_filter_skipped_ = false;
  }
  return ret;
}

int ObMicroBlockReader::get_row_metadata(
    const int64_t row_idx,
    MicroBlockRowMeta &row_meta)
{
  int ret = OB_SUCCESS;
  if (IS_NOT_INIT) {
    ret = OB_NOT_INIT;
  } else {
    ret = read_flat_row_metadata(metadata_, block_data_, row_idx, row_meta);
  }
  return ret;
}

int ObMicroBlockReader::get_logical_row_cnt(
    const int64_t last,
    int64_t &row_idx,
    int64_t &row_cnt) const
{
  int ret = OB_SUCCESS;
  MicroBlockRowMeta row_meta;
  if (IS_NOT_INIT) {
    ret = OB_NOT_INIT;
  } else if (OB_UNLIKELY(!block_data_.is_valid() || last >= metadata_.row_count_)) {
    ret = OB_INVALID_ARGUMENT;
  } else {
    while (OB_SUCC(ret) && row_idx <= last) {
      if (OB_FAIL(read_flat_row_metadata(metadata_, block_data_, row_idx, row_meta))) {
      } else if (row_meta.get_row_multi_version_flag().is_first_multi_version_row()) {
        row_cnt += row_meta.get_row_flag().get_delta();
      }
      row_idx++;
    }
  }
  return ret;
}

int ObMicroBlockReader::get_row_count(int64_t &row_count)
{
  int ret = OB_SUCCESS;
  if (IS_NOT_INIT) {
    ret = OB_NOT_INIT;
  } else {
    row_count = metadata_.row_count_;
  }
  return ret;
}

// notice, trans_version of ghost row is min(0)
int ObMicroBlockReader::get_multi_version_info(
    const int64_t row_idx,
    const int64_t schema_rowkey_cnt,
    MicroBlockRowMeta &row_meta,
    int64_t &trans_version,
    int64_t &sql_sequence)
{
  int ret = OB_SUCCESS;
  if (IS_NOT_INIT) {
    ret = OB_NOT_INIT;
  } else if (OB_UNLIKELY(!block_data_.is_valid() ||
                         row_idx < 0 || row_idx >= row_count_ ||
                         0 > schema_rowkey_cnt ||
                         metadata_.column_count_ < schema_rowkey_cnt + 2)) {
    ret = OB_INVALID_ARGUMENT;
  } else if (OB_FAIL(read_flat_row_metadata(metadata_, block_data_, row_idx, row_meta))) {
  } else {
    ObStorageDatum datum;
    const auto flags = row_meta.get_row_multi_version_flag();
    const int64_t column_idx = schema_rowkey_cnt + flags.is_uncommitted_row();
    if (OB_FAIL(read_flat_column(metadata_, block_data_, row_idx, column_idx, datum))) {
    } else if (flags.is_uncommitted_row()) {
      trans_version = INT64_MAX;
      sql_sequence = -datum.get_int();
    } else {
      trans_version = flags.is_ghost_row() ? 0 : -datum.get_int();
      sql_sequence = 0;
    }
  }
  return ret;
}

struct ObIMicroBlockFlatReader::RustFlatBatch
{
  std::vector<uint32_t> rows;
  std::vector<uint32_t> columns;
  std::optional<micro_block::BatchResult> result;
};

int ObMicroBlockReader::filter_truncate_evaluator(
    storage::ObTruncateFilterEvaluator &evaluator,
    const int64_t start,
    const int64_t count,
    const common::ObBitmap *candidate_rows,
    common::ObBitmap &result_bitmap)
{
  int ret = OB_SUCCESS;
  allocator_.reuse();
  if (OB_UNLIKELY(start < 0 || count <= 0 || start + count > row_count_ ||
                  !evaluator.is_valid())) {
    ret = OB_INVALID_ARGUMENT;
  } else if (OB_UNLIKELY(nullptr == read_info_ || !block_data_.is_valid())) {
    ret = OB_ERR_UNEXPECTED;
  } else {
    const int64_t column_count = evaluator.referenced_column_count();
    ObStorageDatum *datum_buf = nullptr;
    const ObColDescIArray &cols_desc = read_info_->get_columns_desc();
    if (OB_UNLIKELY(column_count <= 0) ||
        OB_ISNULL(datum_buf = static_cast<ObStorageDatum *>(
            allocator_.get_inner_allocator().alloc(sizeof(ObStorageDatum) * column_count)))) {
      ret = OB_ALLOCATE_MEMORY_FAILED;
    } else {
      for (int64_t i = 0; i < column_count; ++i) {
        new (datum_buf + i) ObStorageDatum();
      }
    }
    std::vector<int32_t> row_ids;
    std::vector<int32_t> result_offsets;
    row_ids.reserve(count);
    result_offsets.reserve(count);
    for (int64_t offset = 0; OB_SUCC(ret) && offset < count; ++offset) {
      if (nullptr != candidate_rows && !candidate_rows->test(offset)) {
      } else {
        row_ids.push_back(static_cast<int32_t>(start + offset));
        result_offsets.push_back(static_cast<int32_t>(offset));
      }
    }
    RustFlatBatch rust_batch;
    if (OB_SUCC(ret) && !row_ids.empty()) {
      rust_batch.columns.reserve(column_count);
      for (int64_t i = 0; i < column_count; ++i) {
        const int64_t column_index = evaluator.referenced_column(i);
        if (OB_UNLIKELY(column_index < 0 || column_index >= cols_desc.count())) {
          ret = OB_INDEX_OUT_OF_RANGE;
          break;
        }
        rust_batch.columns.push_back(static_cast<uint32_t>(column_index));
      }
      if (OB_SUCC(ret)) {
        ret = try_project_rust(row_ids.data(), row_ids.size(), rust_batch);
      }
    }
    for (int64_t row = 0; OB_SUCC(ret) && row < row_ids.size(); ++row) {
      ObStorageDatum tmp_datum; // used for deep copy decimalint
      for (int64_t i = 0; OB_SUCC(ret) && i < column_count; ++i) {
        ObStorageDatum &datum = datum_buf[i];
        const int64_t column_index = evaluator.referenced_column(i);
        datum.reuse();
        tmp_datum.reuse();
        if (OB_FAIL(read_rust_cell(rust_batch, row, i, tmp_datum))) {
        } else if (OB_UNLIKELY(metadata_.is_trans_version_column_idx(column_index))) {
          datum.set_int(-tmp_datum.get_int());
        } else {
          const ObObjType obj_type = cols_desc.at(column_index).col_type_.get_type();
          const ObObjDatumMapType map_type = ObDatum::get_obj_datum_map_type(obj_type);
          if (OB_FAIL(datum.from_storage_datum(tmp_datum, map_type))) {
          }
        }
      }
      if (OB_SUCC(ret)) {
        bool filtered = false;
        if (OB_FAIL(evaluator.filter_projected(datum_buf, column_count, filtered))) {
        } else if (!filtered) {
          if (OB_FAIL(result_bitmap.set(result_offsets[row]))) {
          }
        }
      }
    }
    LOG_TRACE("[TRUNCATE INFO] flat micro block truncate filter", K(ret), K(start), K(count),
              K(result_bitmap.popcnt()), K(result_bitmap), K_(metadata));
  }
  return ret;
}

int ObIMicroBlockFlatReader::read_row(
    const ObITableReadInfo *read_info,
    const int64_t index,
    ObDatumRow &row)
{
  int ret = OB_SUCCESS;
  if (OB_UNLIKELY(!block_data_.is_valid() || index < 0 || index >= metadata_.row_count_)) {
    ret = OB_INVALID_ARGUMENT;
  } else if (nullptr != read_info && !read_info->is_valid()) {
    ret = OB_INVALID_ARGUMENT;
  } else {
    const int32_t row_id = static_cast<int32_t>(index);
    RustFlatBatch batch;
    const int64_t column_count = nullptr == read_info
        ? metadata_.column_count_ : read_info->get_request_count();
    if (nullptr != read_info) {
      ret = try_get_rows_rust(*read_info, &row_id, 1, batch);
    } else {
      batch.columns.reserve(column_count);
      for (int64_t column = 0; column < column_count; ++column) {
        batch.columns.push_back(static_cast<uint32_t>(column));
      }
      ret = try_project_rust(&row_id, 1, batch);
    }
    if (OB_SUCC(ret)) {
      ret = row.is_valid() ? row.reserve(column_count) : row.init(column_count);
      if (OB_SUCC(ret)) {
        ret = read_rust_row(batch, 0, row);
      }
    }
  }
  return ret;
}

int ObMicroBlockReader::get_rows(
    const common::ObIArray<int32_t> &cols_projector,
    const common::ObIArray<const share::schema::ObColumnParam *> &col_params,
    const common::ObIArray<blocksstable::ObStorageDatum> *default_datums,
    const bool is_padding_mode,
    const int32_t *row_ids,
    const int64_t row_cap,
    ObDatumRow &row_buf,
    common::ObIArray<ObSqlDatumInfo> &datum_infos,
    const int64_t datum_offset,
    const sql::ObExprPtrIArray &exprs,
    sql::ObEvalCtx &eval_ctx,
    const bool use_scratch_datums)
{
  int ret = OB_SUCCESS;
  int64_t row_idx = common::OB_INVALID_INDEX;
  allocator_.reuse();
  if (OB_UNLIKELY(!block_data_.is_valid() ||
                  nullptr == read_info_ ||
                  row_cap > metadata_.row_count_ ||
                  !row_buf.is_valid())) {
    ret = OB_INVALID_ARGUMENT;
  } else if (OB_FAIL(row_buf.reserve(read_info_->get_request_count()))) {
  } else {
    RustFlatBatch rust_batch;
    const int64_t trans_col_idx = metadata_.rowkey_column_count_ > 0 ? read_info_->get_schema_rowkey_count() : INT32_MIN;
    const ObColumnIndexArray &cols_index = read_info_->get_columns_index();
    ret = try_get_rows_rust(*read_info_, row_ids, row_cap, rust_batch);
    for (int64_t idx = 0; OB_SUCC(ret) && idx < row_cap; ++idx) {
      row_idx = row_ids[idx];
      if (OB_UNLIKELY(row_idx < 0 || row_idx >= metadata_.row_count_)) {
        ret = OB_ERR_UNEXPECTED;
      } else if (OB_FAIL(read_rust_row(rust_batch, idx, row_buf))) {
      } else {
        for (int64_t i = 0; OB_SUCC(ret) && i < cols_projector.count(); ++i) {
          common::ObDatum &datum = datum_infos.at(i).datum_ptr_[idx + datum_offset];
          int32_t col_idx = cols_projector.at(i);
          if (col_idx >= read_info_->get_request_count()) {
            ret = OB_ERR_UNEXPECTED;
          } else if (OB_UNLIKELY(trans_col_idx == cols_index.at(col_idx))) {
            datum.set_int(-row_buf.storage_datums_[col_idx].get_int());
          } else if (row_buf.storage_datums_[col_idx].is_null()) {
            datum.set_null();
          } else if (row_buf.storage_datums_[col_idx].is_nop()) {
            if (OB_ISNULL(default_datums)) {
              ret = OB_ERR_UNEXPECTED;
              STORAGE_LOG(WARN, "Unexpected null default row", K(ret), KP(default_datums));
            } else if (default_datums->at(i).is_nop()) {
              // virtual columns will be calculated in sql
            } else if (OB_FAIL(datum.from_storage_datum(default_datums->at(i), datum_infos.at(i).get_obj_datum_map()))) {
            }
          } else {
            bool need_copy = false;
            if (row_buf.storage_datums_[col_idx].need_copy_for_encoding_column_with_flat_format(datum_infos.at(i).get_obj_datum_map())) {
              need_copy = true;
              const int64_t length = row_buf.storage_datums_[col_idx].len_;
              datum.ptr_ = use_scratch_datums
                  ? static_cast<char *>(allocator_.get_inner_allocator().alloc(length))
                  : exprs.at(i)->get_str_res_mem(eval_ctx, length, idx + datum_offset);
            }
            if (need_copy && OB_ISNULL(datum.ptr_)) {
              ret = OB_ALLOCATE_MEMORY_FAILED;
            } else if (OB_FAIL(datum.from_storage_datum(row_buf.storage_datums_[col_idx], datum_infos.at(i).get_obj_datum_map(), need_copy))) {
            }
          }
        }
      }
    }

    if (OB_SUCC(ret)) {
      for (int64_t i = 0; OB_SUCC(ret) && i < cols_projector.count(); ++i) {
        if (nullptr != col_params.at(i) && need_padding(is_padding_mode, col_params.at(i)->get_meta_type())) {
          ObDatum *col_datums = datum_infos.at(i).datum_ptr_ + datum_offset;
          if (OB_FAIL(storage::pad_on_datums(
                      col_params.at(i)->get_accuracy(),
                      col_params.at(i)->get_meta_type().get_collation_type(),
                      allocator_.get_inner_allocator(),
                      row_cap,
                      col_datums))) {
          }
        }
      }
    }
  }
  return ret;
}

int ObIMicroBlockFlatReader::try_get_rows_rust(
    const ObITableReadInfo &read_info,
    const int32_t *row_ids,
    const int64_t row_cap,
    RustFlatBatch &batch)
{
  const int64_t request_count = read_info.get_request_count();
  batch.columns.reserve(request_count);
  const ObColumnIndexArray &cols_index = read_info.get_columns_index();
  for (int64_t i = 0; i < request_count; ++i) {
    batch.columns.push_back(static_cast<uint32_t>(cols_index.at(i)));
  }
  return try_project_rust(row_ids, row_cap, batch);
}

int ObIMicroBlockFlatReader::try_project_rust(
    const int32_t *row_ids,
    const int64_t row_cap,
    RustFlatBatch &batch)
{
  int ret = OB_NOT_SUPPORTED;
  if (!block_data_.is_valid() || (nullptr == row_ids && row_cap > 0) || row_cap < 0 || row_cap > UINT32_MAX
      || row_cap > metadata_.row_count_) {
    return OB_INVALID_ARGUMENT;
  }
  if (0 == row_cap) {
    return OB_SUCCESS;
  }
  batch.rows.reserve(row_cap);
  for (int64_t i = 0; i < row_cap; ++i) {
    if (row_ids[i] < 0 || row_ids[i] >= metadata_.row_count_) {
      return OB_ERR_UNEXPECTED;
    }
    batch.rows.push_back(static_cast<uint32_t>(row_ids[i]));
  }
  const auto block = flat_block_bytes(block_data_);
  const rust::Slice<const uint32_t> rows(batch.rows.data(), batch.rows.size());
  const rust::Slice<const uint32_t> columns(batch.columns.data(), batch.columns.size());
  batch.result.emplace(micro_block::project_flat_block(block, rows, columns));
  if (OB_FAIL(flat_reader_status(batch.result->code))) {
    return ret;
  }
  if (batch.result->rows.size() != batch.rows.size()
      || batch.result->cells.size() != batch.rows.size() * batch.columns.size()
      || batch.result->row_meta.size() != batch.rows.size()) {
    return OB_INVALID_DATA;
  }
  return ret;
}

int ObIMicroBlockFlatReader::read_rust_cell(
    const RustFlatBatch &batch,
    const int64_t row_idx,
    const int64_t col_idx,
    ObStorageDatum &datum)
{
  if (!batch.result.has_value() || row_idx < 0 || row_idx >= batch.rows.size()
      || col_idx < 0 || col_idx >= batch.columns.size()
      || batch.result->rows[row_idx] != batch.rows[row_idx]) {
    return OB_INVALID_DATA;
  }
  const auto &cell = batch.result->cells[row_idx * batch.columns.size() + col_idx];
  return materialize_flat_cell(flat_block_bytes(block_data_), cell, datum);
}

int ObIMicroBlockFlatReader::read_rust_row(
    const RustFlatBatch &batch,
    const int64_t row_idx,
    ObDatumRow &row_buf)
{
  int ret = OB_SUCCESS;
  if (!batch.result.has_value() || row_idx < 0 || row_idx >= batch.rows.size()
      || batch.result->rows[row_idx] != batch.rows[row_idx]) {
    return OB_INVALID_DATA;
  }
  const int64_t request_count = batch.columns.size();
  for (int64_t col_idx = 0; OB_SUCC(ret) && col_idx < request_count; ++col_idx) {
    ObStorageDatum &datum = row_buf.storage_datums_[col_idx];
    ret = read_rust_cell(batch, row_idx, col_idx, datum);
  }
  if (OB_SUCC(ret)) {
    const micro_block::FlatRowMeta &meta = batch.result->row_meta[row_idx];
    row_buf.row_flag_ = ObDmlRowFlag(meta.row_flag);
    row_buf.mvcc_row_flag_.flag_ = meta.multi_version_flag;
    row_buf.trans_id_ = meta.trans_id;
    row_buf.count_ = request_count;
  }
  return ret;
}

int ObMicroBlockReader::get_row_count(
    int32_t col,
    const int32_t *row_ids,
    const int64_t row_cap,
    const bool contains_null,
    const share::schema::ObColumnParam *col_param,
    int64_t &count)
{
  int ret = OB_SUCCESS;
  if (OB_UNLIKELY(!block_data_.is_valid() ||
                  nullptr == read_info_ ||
                  row_cap > metadata_.row_count_)) {
    ret = OB_INVALID_ARGUMENT;
  } else if (contains_null) {
    count = row_cap;
  } else {
    count = 0;
    int64_t row_idx = common::OB_INVALID_INDEX;
    const ObColumnIndexArray &cols_index = read_info_->get_columns_index();
    int64_t col_idx = cols_index.at(col);
    ObStorageDatum datum;
    RustFlatBatch rust_batch;
    rust_batch.columns.push_back(static_cast<uint32_t>(col_idx));
    ret = try_project_rust(row_ids, row_cap, rust_batch);
    for (int64_t i = 0; OB_SUCC(ret) && i < row_cap; ++i) {
      row_idx = row_ids[i];
      if (OB_UNLIKELY(row_idx < 0 || row_idx >= metadata_.row_count_)) {
        ret = OB_ERR_UNEXPECTED;
      } else if (OB_FAIL(read_rust_cell(rust_batch, i, 0, datum))) {
      } else if (datum.is_nop()) {
        if (OB_UNLIKELY(nullptr == col_param || col_param->get_orig_default_value().is_nop_value())) {
          ret = OB_ERR_UNEXPECTED;
        } else if (col_param->get_orig_default_value().is_null()) {
          datum.set_null();
        }
      }
      if (OB_FAIL(ret)) {
      } else if (!datum.is_null()) {
        ++count;
      }
    }
  }
  return ret;
}

int ObMicroBlockReader::get_aggregate_result(
    const ObTableIterParam &iter_param,
    const ObTableAccessContext &context,
    const int32_t col_offset,
    const share::schema::ObColumnParam &col_param,
    const int32_t *row_ids,
    const int64_t row_cap,
    storage::ObAggDatumBuf &agg_datum_buf,
    storage::ObAggCell &agg_cell)
{
  UNUSED(agg_datum_buf);
  int ret = OB_SUCCESS;
  if (OB_UNLIKELY(!block_data_.is_valid() ||
                  nullptr == read_info_ ||
                  row_cap > metadata_.row_count_)) {
    ret = OB_INVALID_ARGUMENT;
  } else {
    int64_t row_idx = common::OB_INVALID_INDEX;
    const ObColumnIndexArray &cols_index = read_info_->get_columns_index();
    const ObColDescIArray &cols_desc = read_info_->get_columns_desc();
    int64_t col_idx = cols_index.at(col_offset);
    const ObObjType obj_type = cols_desc.at(col_offset).col_type_.get_type();
    const ObObjDatumMapType map_type = ObDatum::get_obj_datum_map_type(obj_type);
    ObStorageDatum datum;
    ObStorageDatum tmp_datum; // used for deep copy decimalint
    const bool has_lob_out_row = col_param.get_meta_type().is_lob_storage() && metadata_.has_lob_out_row();
    bool need_reuse_lob_locator = false;
    RustFlatBatch rust_batch;
    rust_batch.columns.push_back(static_cast<uint32_t>(col_idx));
    ret = try_project_rust(row_ids, row_cap, rust_batch);
    for (int64_t i = 0; OB_SUCC(ret) && i < row_cap; ++i) {
      row_idx = row_ids[i];
      datum.set_nop();
      if (OB_UNLIKELY(row_idx < 0 || row_idx >= metadata_.row_count_)) {
        ret = OB_ERR_UNEXPECTED;
      } else if (OB_FAIL(read_rust_cell(rust_batch, i, 0, tmp_datum))) {
      } else if (!tmp_datum.is_nop() && OB_FAIL(datum.from_storage_datum(tmp_datum, map_type))) {
      } else if (has_lob_out_row && !datum.is_nop() && !datum.is_null() && !datum.get_lob_data().in_row_) {
        if (OB_FAIL(context.lob_locator_helper_->fill_lob_locator_v2(datum, col_param, iter_param, context))) {
        } else {
          need_reuse_lob_locator = true;
        }
      }
      if (OB_SUCC(ret)) {
        if (OB_FAIL(agg_cell.eval(datum))) {
        }
        if (need_reuse_lob_locator) {
          context.lob_locator_helper_->reuse();
          need_reuse_lob_locator = false;
        }
      }
    }
  }
  return ret;
}

int ObMicroBlockReader::get_aggregate_result(
    const ObTableIterParam &iter_param,
    const ObTableAccessContext &context,
    const int32_t *row_ids,
    const int64_t row_cap,
    ObDatumRow &row_buf,
    common::ObIArray<storage::ObAggCell*> &agg_cells)
{
  int ret = OB_SUCCESS;
  if (OB_UNLIKELY(!block_data_.is_valid() ||
                  nullptr == read_info_ ||
                  row_cap > metadata_.row_count_ ||
                  !row_buf.is_valid() ||
                  agg_cells.empty())) {
    ret = OB_INVALID_ARGUMENT;
  } else if (OB_FAIL(row_buf.reserve(read_info_->get_request_count()))) {
  } else {
    const ObColDescIArray &cols_desc = read_info_->get_columns_desc();
    ObStorageDatum tmp_datum; // used for deep copy decimalint
    int64_t row_idx = common::OB_INVALID_INDEX;
    const common::ObIArray<ObColumnParam *> *col_params = read_info_->get_columns();
    const bool has_lob_out_row = iter_param.has_lob_column_out() && metadata_.has_lob_out_row();
    bool need_reuse_lob_locator = false;
    RustFlatBatch rust_batch;
    if (OB_ISNULL(col_params)) {
      ret = OB_ERR_UNEXPECTED;
    } else {
      ret = try_get_rows_rust(*read_info_, row_ids, row_cap, rust_batch);
    }
    for (int64_t idx = 0; OB_SUCC(ret) && idx < row_cap; ++idx) {
      row_idx = row_ids[idx];
      if (OB_UNLIKELY(row_idx < 0 || row_idx >= metadata_.row_count_)) {
        ret = OB_ERR_UNEXPECTED;
      } else if (OB_FAIL(read_rust_row(rust_batch, idx, row_buf))) {
      } else {
        for (int64_t i = 0; OB_SUCC(ret) && i < agg_cells.count(); ++i) {
          const int32_t col_offset = agg_cells.at(i)->get_col_offset();
          tmp_datum.set_nop();
          if (OB_COUNT_AGG_PD_COLUMN_ID != col_offset) {
            const ObObjMeta &obj_meta = cols_desc.at(col_offset).col_type_;
            const ObObjDatumMapType map_type = ObDatum::get_obj_datum_map_type(obj_meta.get_type());
            if (row_buf.storage_datums_[col_offset].is_nop()) {
            } else if (row_buf.storage_datums_[col_offset].is_null()) {
              tmp_datum.set_null();
            } else if (agg_cells.at(i)->need_padding() && 
                       OB_FAIL(pad_column(col_params->at(col_offset)->get_meta_type(), col_params->at(col_offset)->get_accuracy(),
                                          allocator_.get_inner_allocator(), row_buf.storage_datums_[col_offset]))) {
            } else if (OB_FAIL(tmp_datum.from_storage_datum(row_buf.storage_datums_[col_offset], map_type))) {
            } else if (has_lob_out_row && obj_meta.is_lob_storage()) {
              if (OB_FAIL(context.lob_locator_helper_->fill_lob_locator_v2(tmp_datum, *col_params->at(col_offset), iter_param, context))) {
              } else {
                need_reuse_lob_locator = true;
              }
            }

            if (OB_SUCC(ret)) {
              if (OB_FAIL(agg_cells.at(i)->eval(tmp_datum))) {
              }
              if (need_reuse_lob_locator) {
                context.lob_locator_helper_->reuse();
                need_reuse_lob_locator = false;
              }
            }
          } else if (OB_FAIL(agg_cells.at(i)->eval(tmp_datum))) {
          }
        }
      }
    }
  }
  return ret;
}

int ObMicroBlockReader::get_column_datum(
    const ObTableIterParam &iter_param,
    const ObTableAccessContext &context,
    const share::schema::ObColumnParam &col_param,
    const int32_t col_offset,
    const int64_t row_index,
    ObStorageDatum &datum)
{
  int ret = OB_SUCCESS;
  if(IS_NOT_INIT){
    ret = OB_NOT_INIT;
  } else if(OB_UNLIKELY(!block_data_.is_valid() ||
                        nullptr == read_info_ ||
                        col_offset >= read_info_->get_columns_index().count() ||
                        row_index < 0 || row_index >= metadata_.row_count_)) {
    ret = OB_INVALID_ARGUMENT;
  } else {
    const ObColumnIndexArray &cols_index = read_info_->get_columns_index();
    int64_t col_idx = cols_index.at(col_offset);
    const ObColDescIArray &cols_desc = read_info_->get_columns_desc();
    const ObObjType obj_type = cols_desc.at(col_offset).col_type_.get_type();
    const ObObjDatumMapType map_type = ObDatum::get_obj_datum_map_type(obj_type);
    ObStorageDatum tmp_datum; // used for deep copy decimalint
    if (OB_UNLIKELY(col_idx >= metadata_.column_count_)) {
      ret = OB_ERR_UNEXPECTED;
    } else {
      ret = read_flat_column(metadata_, block_data_, row_index, col_idx, tmp_datum);
    }
    if (OB_FAIL(ret)) {
    } else if (tmp_datum.is_nop()) {
      datum.set_nop();
    } else if (tmp_datum.is_null()) {
      datum.set_null();
    } else if (OB_FAIL(datum.from_storage_datum(tmp_datum, map_type))) {
    } else if (col_param.get_meta_type().is_lob_storage() && !datum.get_lob_data().in_row_) {
      if (OB_FAIL(context.lob_locator_helper_->fill_lob_locator_v2(datum, col_param, iter_param, context))) {
      }
    }
  }
  return ret;
}

}
}
