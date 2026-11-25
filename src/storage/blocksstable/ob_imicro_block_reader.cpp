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
#include "ob_imicro_block_reader.h"
#include "index_block/ob_index_block_row_struct.h"
#include "lib/charset/ob_ctype.h"
#include "lib/charset/str_uca_type.h"
#include "rust/micro_block.h"
#include "storage/blocksstable/ob_datum_row.h"

namespace oceanbase
{
namespace blocksstable
{

int try_locate_rowkey_bound(
    const char *block_data,
    const int64_t block_size,
    const ObMicroBlockHeader *header,
    const ObColumnHeader *col_header,
    const ObDatumRowkey &rowkey,
    const ObITableReadInfo *read_info,
    const bool require_full_rowkey,
    const bool lower_bound,
    const int64_t begin_idx,
    const int64_t end_idx,
    bool &located_by_rust,
    int64_t &row_idx,
    bool &equal)
{
  int ret = OB_SUCCESS;
  located_by_rust = false;
  row_idx = ObIMicroBlockReaderInfo::INVALID_ROW_INDEX;
  equal = false;
  const int64_t rowkey_cnt = rowkey.get_datum_cnt();
  bool rust_eligible = nullptr != block_data && block_size > 0 && nullptr != read_info
      && rowkey.is_valid()
      && (!require_full_rowkey || rowkey_cnt == read_info->get_schema_rowkey_count())
      && begin_idx >= 0 && begin_idx <= end_idx && nullptr != header
      && end_idx <= header->row_count_ && rowkey_cnt > 0 && header->column_count_ >= rowkey_cnt
      && (FLAT_ROW_STORE == header->row_store_type_ || nullptr != col_header);
  const ObStorageDatum *datums = rowkey.datums_;
  rust::Vec<micro_block::InputCell> rust_keys;
  rust::Vec<uint8_t> rust_key_kinds;
  if (rust_eligible) {
    rust_keys.reserve(rowkey_cnt);
    rust_key_kinds.reserve(rowkey_cnt);
  }
  for (int64_t i = 0; rust_eligible && i < rowkey_cnt; ++i) {
    const share::schema::ObColDesc &desc = read_info->get_columns_desc().at(i);
    if (desc.col_order_ != common::ASC || datums[i].is_null() || datums[i].is_ext()) {
      rust_eligible = false;
      break;
    }
    micro_block::InputCell key = {micro_block::InputKind::Bytes, 0, 0, 0};
    if (desc.col_type_.is_integer_type() && sizeof(int64_t) == datums[i].len_) {
      key.kind = micro_block::InputKind::UInt;
      if (desc.col_type_.is_unsigned_integer()) {
        key.value = datums[i].get_uint64();
        rust_key_kinds.push_back(2);
      } else {
        key.value = static_cast<uint64_t>(datums[i].get_int());
        rust_key_kinds.push_back(1);
      }
    } else if (ObStringTC == ob_obj_type_class(desc.col_type_.get_type())
               && CS_TYPE_UTF8MB4_UNICODE_CI == desc.col_type_.get_collation_type()
               && datums[i].len_ >= 0
               && (0 == datums[i].len_ || nullptr != datums[i].ptr_)) {
      key.pointer = 0 == datums[i].len_
          ? reinterpret_cast<size_t>(block_data)
          : reinterpret_cast<size_t>(datums[i].ptr_);
      key.length = datums[i].len_;
      rust_key_kinds.push_back(0);
    } else {
      rust_eligible = false;
      break;
    }
    rust_keys.push_back(key);
  }
  if (rust_eligible) {
    const rust::Slice<const uint8_t> block(
        reinterpret_cast<const uint8_t *>(block_data), static_cast<std::size_t>(block_size));
    const micro_block::UcaTableDesc uca = {
        static_cast<uint64_t>(ob_uca_v400.maxchar),
        reinterpret_cast<size_t>(ob_uca_v400.lengths),
        reinterpret_cast<size_t>(ob_uca_v400.weights)};
    const micro_block::LocateResult result = micro_block::locate_rowkey(
        block,
        header->row_store_type_,
        std::move(rust_keys),
        std::move(rust_key_kinds),
        uca,
        lower_bound,
        static_cast<uint32_t>(begin_idx),
        static_cast<uint32_t>(end_idx));
    switch (result.code) {
      case micro_block::Code::Ok:
        if (result.row < begin_idx || result.row > end_idx) {
          ret = OB_INVALID_DATA;
        } else {
          located_by_rust = true;
          row_idx = result.row;
          equal = result.equal;
        }
        break;
      case micro_block::Code::Unsupported:
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
  }
  return ret;
}

int ObIMicroBlockReader::read_column_values(
    const int32_t col_offset,
    const int64_t dense_begin,
    const int32_t *row_ids,
    const int64_t value_count,
    common::ObIAllocator &allocator,
    common::ObDatum *datums)
{
  int ret = OB_SUCCESS;
  ObDatumRow row;
  if (OB_UNLIKELY(!is_inited_ || nullptr == read_info_)) {
    ret = OB_NOT_INIT;
  } else if (OB_UNLIKELY(col_offset < 0
                         || col_offset >= read_info_->get_request_count()
                         || value_count < 0
                         || (value_count > 0 && nullptr == datums)
                         || (nullptr == row_ids && dense_begin < 0))) {
    ret = OB_INVALID_ARGUMENT;
  } else if (value_count > 0
             && OB_FAIL(row.init(allocator, read_info_->get_request_count()))) {
  }
  for (int64_t i = 0; OB_SUCC(ret) && i < value_count; ++i) {
    const int64_t row_id = nullptr == row_ids ? dense_begin + i : row_ids[i];
    if (OB_UNLIKELY(row_id < 0 || row_id >= row_count_)) {
      ret = OB_INVALID_ARGUMENT;
    } else if (OB_FAIL(get_row(row_id, row))) {
    } else if (OB_UNLIKELY(col_offset >= row.get_column_count())) {
      ret = OB_ERR_UNEXPECTED;
    } else if (OB_FAIL(datums[i].deep_copy(row.storage_datums_[col_offset], allocator))) {
    }
  }
  return ret;
}

int ObIMicroBlockReader::locate_range(
    const ObDatumRange &range,
    const bool is_left_border,
    const bool is_right_border,
    int64_t &begin_idx,
    int64_t &end_idx,
    const bool is_index_block)
{
  int ret = OB_SUCCESS;
  begin_idx = ObIMicroBlockReaderInfo::INVALID_ROW_INDEX;
  end_idx = ObIMicroBlockReaderInfo::INVALID_ROW_INDEX;
  bool equal = false;
  int64_t end_key_begin_idx = 0;
  int64_t end_key_end_idx = row_count_;
  if (OB_UNLIKELY(0 > row_count_)) {
    ret = OB_ERR_UNEXPECTED;
  } else if (0 == row_count_) {
  } else if (OB_ISNULL(datum_utils_)){
    ret = OB_INVALID_ARGUMENT;
  } else {
    if (!is_left_border || range.get_start_key().is_min_rowkey()) {
      begin_idx = 0;
    } else if (OB_FAIL(find_bound(range, 0, begin_idx, equal, end_key_begin_idx, end_key_end_idx))) {
    } else if (begin_idx == row_count_) {
      ret = OB_BEYOND_THE_RANGE;
    } else if (!range.get_border_flag().inclusive_start()) {
      if (equal) {
        ++begin_idx;
        if (begin_idx == row_count_) {
          ret = OB_BEYOND_THE_RANGE;
        }
      }
    }
    if (OB_SUCC(ret)) {
      if (!is_right_border || range.get_end_key().is_max_rowkey()) {
        end_idx = row_count_ - 1;
      } else if (OB_UNLIKELY(end_key_begin_idx > end_key_end_idx)) {
        ret = OB_ERR_UNEXPECTED;
      } else  {
        const bool is_percise_rowkey = datum_utils_->get_rowkey_count() == range.get_end_key().get_datum_cnt();
        // we should use upper_bound if the range include endkey
        if (OB_FAIL(find_bound(range.get_end_key(),
                               !range.get_border_flag().inclusive_end()/*lower_bound*/,
                               end_key_begin_idx > begin_idx ? end_key_begin_idx : begin_idx,
                               end_idx,
                               equal))) {
        } else if (end_idx == row_count_) {
          --end_idx;
        } else if (is_index_block && !(equal && range.get_border_flag().inclusive_end() && is_percise_rowkey)) {
          // Skip
          // When right border is closed and found rowkey is equal to end key of range, do --end_idx
        } else if (end_idx == 0) {
          ret = OB_BEYOND_THE_RANGE;
        } else {
          --end_idx;
        }
      }
    }
  }
  return ret;
}

int ObIMicroBlockReader::locate_border_row_id(
    const ObDatumRowkey &rowkey,
    const int64_t begin_idx,
    const int64_t end_idx,
    int64_t &border_row_idx,
    bool &is_equal)
{
  int ret = OB_SUCCESS;
  border_row_idx = ObIMicroBlockReaderInfo::INVALID_ROW_INDEX;
  is_equal = false;
  if (OB_UNLIKELY(0 >= row_count_ || begin_idx >= end_idx ||
                  0 > begin_idx || row_count_ < end_idx)) {
    ret = OB_INVALID_ARGUMENT;
  } else if (OB_ISNULL(datum_utils_)) {
    ret = OB_ERR_UNEXPECTED;
  } else if (rowkey.is_min_rowkey()) {
    border_row_idx = begin_idx;
  } else if (rowkey.is_max_rowkey()) {
    border_row_idx = end_idx;
  } else if (OB_FAIL(find_bound(rowkey, true, begin_idx, end_idx, border_row_idx, is_equal))) {
  }
  return ret;
}

}
}
