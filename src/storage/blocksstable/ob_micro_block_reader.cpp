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


template<typename ReaderType>
class PreciseCompare
{
public:
  PreciseCompare(
      int &ret,
      bool &equal,
      ReaderType *reader,
      const char *data_begin,
      const int32_t *index_data,
      const ObStorageDatumUtils *datum_utils)
      : ret_(ret),
      equal_(equal),
      reader_(reader),
      data_begin_(data_begin),
      index_data_(index_data),
      datum_utils_(datum_utils) {}
  ~PreciseCompare() {}
  inline bool operator() (const int64_t row_idx, const ObDatumRowkey &rowkey)
  {
    return compare(row_idx, rowkey, true);
  }
  inline bool operator() (const ObDatumRowkey &rowkey, const int64_t row_idx)
  {
    return compare(row_idx, rowkey, false);
  }
private:
  inline bool compare(const int64_t row_idx, const ObDatumRowkey &rowkey, const bool lower_bound)
  {
    bool bret = false;
    int &ret = ret_;
    int32_t compare_result = 0;
    if (OB_FAIL(ret)) {
      // do nothing
    } else if (OB_FAIL(reader_->compare_meta_rowkey(
                rowkey,
                *datum_utils_,
                data_begin_ + index_data_[row_idx],
                index_data_[row_idx + 1] - index_data_[row_idx],
                compare_result))) {
    } else {
      bret = lower_bound ? compare_result < 0 : compare_result > 0;
      // binary search will keep searching after find the first equal item,
      // if we need the equal reuslt, must prevent it from being modified again
      if (0 == compare_result && !equal_) {
        equal_ = true;
      }
    }
    return bret;
  }

private:
  int &ret_;
  bool &equal_;
  ReaderType *reader_;
  const char *data_begin_;
  const int32_t *index_data_;
  const ObStorageDatumUtils *datum_utils_;
};

ObIMicroBlockFlatReader::ObIMicroBlockFlatReader()
  : header_(nullptr),
    data_begin_(nullptr),
    data_end_(nullptr),
    index_data_(nullptr),
    allocator_(ObModIds::OB_STORE_ROW_GETTER),
    flat_row_reader_()
{
}

ObIMicroBlockFlatReader::~ObIMicroBlockFlatReader()
{
  reset();
}

void ObIMicroBlockFlatReader::reset()
{
  header_ = nullptr;
  data_begin_ = nullptr;
  data_end_ = nullptr;
  index_data_ = nullptr;
  flat_row_reader_.reset();
}

int ObIMicroBlockFlatReader::find_bound_(
    const ObDatumRowkey &key,
    const bool lower_bound,
    const int64_t begin_idx,
    const int64_t end_idx,
    const ObStorageDatumUtils &datum_utils,
    const ObITableReadInfo *read_info,
    int64_t &row_idx,
    bool &equal)
{
  int ret = OB_SUCCESS;
  bool located_by_rust = false;
  equal = false;
  row_idx = ObIMicroBlockReaderInfo::INVALID_ROW_INDEX;
  if (OB_UNLIKELY(nullptr == data_begin_ || nullptr == index_data_)) {
    ret = OB_ERR_UNEXPECTED;
  } else if (OB_FAIL(try_locate_rowkey_bound(
          reinterpret_cast<const char *>(header_),
          header_->header_size_ + header_->data_length_,
          header_,
          nullptr,
          key,
          read_info,
          false,
          lower_bound,
          begin_idx,
          end_idx,
          located_by_rust,
          row_idx,
          equal))) {
  } else if (!located_by_rust) {
    PreciseCompare<ObRowReader> flat_compare(
        ret,
        equal,
        &flat_row_reader_,
        data_begin_,
        index_data_,
        &datum_utils);
    ObRowIndexIterator begin_iter(begin_idx);
    ObRowIndexIterator end_iter(end_idx);
    ObRowIndexIterator found_iter;
    if (lower_bound) {
      found_iter = std::lower_bound(begin_iter, end_iter, key, flat_compare);
    } else {
      found_iter = std::upper_bound(begin_iter, end_iter, key, flat_compare);
    }
    if (OB_FAIL(ret)) {
    } else {
      row_idx = *found_iter;
    }
  }
  return ret;
}

int ObIMicroBlockFlatReader::init(const ObMicroBlockData &block_data)
{
  int ret = OB_SUCCESS;
  if(OB_UNLIKELY(!block_data.is_valid())){
    ret = OB_INVALID_ARGUMENT;
  } else {
    const char *buf = block_data.get_buf();
    header_ = reinterpret_cast<const ObMicroBlockHeader*>(buf);
    data_begin_ = buf + header_->header_size_;
    data_end_ = buf + header_->row_index_offset_;
    index_data_ = reinterpret_cast<const int32_t *>(buf + header_->row_index_offset_);
  }
  return ret;
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
    row_count_ = header_->row_count_;
    original_data_length_ = header_->original_length_;
    read_info_ = &read_info;
    // Fail to init hash index means the reader can not use it, but reader can still work
    if (OB_FAIL(ObIMicroBlockGetReader::init_hash_index(block_data, hash_index_, header_))) {
      ret = OB_SUCCESS;
      hash_index_.reset();
    }
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
      const ObRowHeader *row_header =
          reinterpret_cast<const ObRowHeader*>(data_begin_ + index_data_[row_idx]);
      exist = !row_header->get_row_flag().is_delete();
      found = true;
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
        read_info_->get_datum_utils(), read_info_, row_idx, is_equal))) {
    } else if (row_count_ == row_idx || !is_equal) {
      row_idx = ObIMicroBlockReaderInfo::INVALID_ROW_INDEX;
      ret = OB_BEYOND_THE_RANGE;
    } else {
      const ObRowHeader *row_header =
          reinterpret_cast<const ObRowHeader*>(data_begin_ + index_data_[row_idx]);
      if (row_header->get_row_multi_version_flag().is_ghost_row()) {
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
  if (hash_index_.is_inited()) {
    uint64_t hash_value = 0;
    const blocksstable::ObStorageDatumUtils &datum_utils = read_info_->get_datum_utils();
    if (OB_FAIL(rowkey.murmurhash(0, datum_utils, hash_value))) {
    } else  {
      const uint8_t tmp_row_idx = hash_index_.find(hash_value);
      if (tmp_row_idx == ObMicroBlockHashIndex::NO_ENTRY) {
        row_idx = ObIMicroBlockReaderInfo::INVALID_ROW_INDEX;
        found = false;
      } else if (tmp_row_idx == ObMicroBlockHashIndex::COLLISION) {
        need_binary_search = true;
      } else {
        int32_t compare_result = 0;
        if (OB_UNLIKELY(tmp_row_idx >= row_count_)) {
          ret = OB_ERR_UNEXPECTED;
        } else if (OB_FAIL(flat_row_reader_.compare_meta_rowkey(
                      rowkey,
                      read_info_->get_datum_utils(),
                      data_begin_ + index_data_[tmp_row_idx],
                      index_data_[tmp_row_idx + 1] - index_data_[tmp_row_idx],
                      compare_result))) {
        } else if (0 != compare_result) {
          row_idx = ObIMicroBlockReaderInfo::INVALID_ROW_INDEX;
          found = false;
        } else {
          row_idx = tmp_row_idx;
          found = true;
        }
      }
    }
  } else {
    need_binary_search = true;
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
    row_count_ = header_->row_count_;
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
  header_ = NULL;
  data_begin_ = NULL;
  data_end_ = NULL;
  index_data_ = NULL;
  flat_row_reader_.reset();
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
    row_count_ = header_->row_count_;
    original_data_length_ = header_->original_length_;
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
    row_count_ = header_->row_count_;
    original_data_length_ = header_->original_length_;
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
  } else if (OB_FAIL(flat_row_reader_.compare_meta_rowkey(rowkey,
                                                          *datum_utils_,
                                                          data_begin_ + index_data_[idx],
                                                          index_data_[idx + 1] - index_data_[idx],
                                                          compare_result))) {
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
          read_info_,
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
          read_info_,
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
  } else if (OB_UNLIKELY(!rowkey.is_valid())) {
    ret = OB_INVALID_ARGUMENT;
  } else {
    int32_t cmp_result = 0;
    int64_t idx = begin_idx + 1;
    for (; OB_SUCC(ret) && idx < row_count_; ++idx) {
      if (OB_FAIL(flat_row_reader_.compare_meta_rowkey(rowkey,
                                                       *datum_utils_,
                                                       data_begin_ + index_data_[idx],
                                                       index_data_[idx + 1] - index_data_[idx],
                                                       cmp_result))) {
      } else if (cmp_result != 0) {
        break;
      }
    }
    row_idx = idx - 1;
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
  } else if(OB_UNLIKELY(nullptr == header_ ||
                        index < 0 || index >= header_->row_count_ ||
                        !row.is_valid())) {
    ret = OB_INVALID_ARGUMENT;
  } else if (OB_FAIL(read_row(read_info_, index, row))) {
  } else {
    row.fast_filter_skipped_ = false;
  }
  return ret;
}

int ObMicroBlockReader::get_row_header(
    const int64_t row_idx,
    const ObRowHeader *&row_header)
{
  int ret = OB_SUCCESS;
  if (IS_NOT_INIT) {
    ret = OB_NOT_INIT;
  } else if (OB_UNLIKELY(nullptr == header_ || row_idx >= header_->row_count_)) {
    ret = OB_INVALID_ARGUMENT;
  } else if (OB_FAIL(flat_row_reader_.read_row_header(
              data_begin_ + index_data_[row_idx],
              index_data_[row_idx + 1] - index_data_[row_idx],
              row_header))) {
  }
  return ret;
}

int ObMicroBlockReader::get_logical_row_cnt(
    const int64_t last,
    int64_t &row_idx,
    int64_t &row_cnt) const
{
  int ret = OB_SUCCESS;
  const ObRowHeader *row_header = nullptr;
  if (IS_NOT_INIT) {
    ret = OB_NOT_INIT;
  } else if (OB_UNLIKELY(nullptr == header_ || last >= header_->row_count_)) {
    ret = OB_INVALID_ARGUMENT;
  } else {
    while (OB_SUCC(ret) && row_idx <= last) {
      if (OB_ISNULL(row_header = reinterpret_cast<const ObRowHeader*>(data_begin_ + index_data_[row_idx]))) {
        ret = OB_INVALID_ARGUMENT;
      } else if (row_header->get_row_multi_version_flag().is_first_multi_version_row()) {
        row_cnt += row_header->get_row_flag().get_delta();
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
    row_count = header_->row_count_;
  }
  return ret;
}

// notice, trans_version of ghost row is min(0)
int ObMicroBlockReader::get_multi_version_info(
    const int64_t row_idx,
    const int64_t schema_rowkey_cnt,
    const ObRowHeader *&row_header,
    int64_t &trans_version,
    int64_t &sql_sequence)
{
  int ret = OB_SUCCESS;
  row_header = nullptr;
  if (IS_NOT_INIT) {
    ret = OB_NOT_INIT;
  } else if (OB_UNLIKELY(nullptr == header_ ||
                         row_idx < 0 || row_idx >= row_count_ ||
                         0 > schema_rowkey_cnt ||
                         header_->column_count_ < schema_rowkey_cnt + 2)) {
    ret = OB_INVALID_ARGUMENT;
  } else if (OB_FAIL(flat_row_reader_.read_row_header(
              data_begin_ + index_data_[row_idx],
              index_data_[row_idx + 1] - index_data_[row_idx],
              row_header))) {
  } else {
    ObStorageDatum datum;
    const int64_t read_col_idx =
      row_header->get_row_multi_version_flag().is_uncommitted_row()
      ? schema_rowkey_cnt + 1 : schema_rowkey_cnt;
    if (OB_FAIL(flat_row_reader_.read_column(
                data_begin_ + index_data_[row_idx],
                index_data_[row_idx + 1] - index_data_[row_idx],
                read_col_idx,
                datum))) {
    } else {
      if (!row_header->get_row_multi_version_flag().is_uncommitted_row()) {
        // get trans_version for committed row
        sql_sequence = 0;
        trans_version = row_header->get_row_multi_version_flag().is_ghost_row() ? 0 : -datum.get_int();
      } else {
        // get sql_sequence for uncommitted row
        trans_version = INT64_MAX;
        sql_sequence = -datum.get_int();
      }
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
  } else if (OB_UNLIKELY(nullptr == read_info_ || nullptr == header_)) {
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
    bool read_by_rust = false;
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
        if (OB_NOT_SUPPORTED == ret) {
          ret = OB_SUCCESS;
        } else if (OB_SUCC(ret)) {
          read_by_rust = true;
        }
      }
    }
    for (int64_t row = 0; OB_SUCC(ret) && row < row_ids.size(); ++row) {
      ObStorageDatum tmp_datum; // used for deep copy decimalint
      for (int64_t i = 0; OB_SUCC(ret) && i < column_count; ++i) {
        ObStorageDatum &datum = datum_buf[i];
        const int64_t column_index = evaluator.referenced_column(i);
        datum.reuse();
        tmp_datum.reuse();
        if (read_by_rust) {
          ret = read_rust_cell(rust_batch, row, i, tmp_datum);
        } else {
          const int64_t row_idx = row_ids[row];
          ret = flat_row_reader_.read_column(
              data_begin_ + index_data_[row_idx],
              index_data_[row_idx + 1] - index_data_[row_idx],
              column_index,
              tmp_datum);
        }
        if (OB_FAIL(ret)) {
        } else if (OB_UNLIKELY(header_->is_trans_version_column_idx(column_index))) {
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
              K(result_bitmap.popcnt()), K(result_bitmap), KPC_(header));
  }
  return ret;
}

int ObIMicroBlockFlatReader::read_row(
    const ObITableReadInfo *read_info,
    const int64_t index,
    ObDatumRow &row)
{
  int ret = OB_SUCCESS;
  if (OB_UNLIKELY(nullptr == header_ || index < 0 || index >= header_->row_count_)) {
    ret = OB_INVALID_ARGUMENT;
  } else if (nullptr != read_info && read_info->is_valid()) {
    const int32_t row_id = static_cast<int32_t>(index);
    RustFlatBatch batch;
    ret = try_get_rows_rust(*read_info, &row_id, 1, batch);
    if (OB_NOT_SUPPORTED == ret) {
      ret = flat_row_reader_.read_row(
          data_begin_ + index_data_[index],
          index_data_[index + 1] - index_data_[index],
          read_info,
          row);
    } else if (OB_SUCC(ret)) {
      if (row.is_valid()) {
        ret = row.reserve(read_info->get_request_count());
      } else {
        ret = row.init(read_info->get_request_count());
      }
      if (OB_SUCC(ret)) {
        ret = read_rust_row(batch, 0, row);
      }
    }
  } else {
    ret = flat_row_reader_.read_row(
        data_begin_ + index_data_[index],
        index_data_[index + 1] - index_data_[index],
        read_info,
        row);
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
    sql::ObExprPtrIArray &exprs,
    sql::ObEvalCtx &eval_ctx)
{
  int ret = OB_SUCCESS;
  int64_t row_idx = common::OB_INVALID_INDEX;
  allocator_.reuse();
  if (OB_UNLIKELY(nullptr == header_ ||
                  nullptr == read_info_ ||
                  row_cap > header_->row_count_ ||
                  !row_buf.is_valid())) {
    ret = OB_INVALID_ARGUMENT;
  } else if (OB_FAIL(row_buf.reserve(read_info_->get_request_count()))) {
  } else {
    bool read_by_rust = false;
    RustFlatBatch rust_batch;
    const int64_t trans_col_idx = header_->rowkey_column_count_ > 0 ? read_info_->get_schema_rowkey_count() : INT32_MIN;
    const ObColumnIndexArray &cols_index = read_info_->get_columns_index();
    ret = try_get_rows_rust(*read_info_, row_ids, row_cap, rust_batch);
    if (OB_NOT_SUPPORTED == ret) {
      ret = OB_SUCCESS;
    } else if (OB_SUCC(ret)) {
      read_by_rust = true;
    }
    for (int64_t idx = 0; OB_SUCC(ret) && idx < row_cap; ++idx) {
      row_idx = row_ids[idx];
      if (OB_UNLIKELY(row_idx < 0 || row_idx >= header_->row_count_)) {
        ret = OB_ERR_UNEXPECTED;
      } else if (read_by_rust && OB_FAIL(read_rust_row(rust_batch, idx, row_buf))) {
      } else if (!read_by_rust && OB_FAIL(flat_row_reader_.read_row(
          data_begin_ + index_data_[row_idx],
          index_data_[row_idx + 1] - index_data_[row_idx],
          read_info_,
          row_buf))) {
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
              datum.ptr_ = exprs.at(i)->get_str_res_mem(eval_ctx, row_buf.storage_datums_[col_idx].len_, idx);
            }
            if (OB_FAIL(datum.from_storage_datum(row_buf.storage_datums_[col_idx], datum_infos.at(i).get_obj_datum_map(), need_copy))) {
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
  if ((nullptr == row_ids && row_cap > 0) || row_cap < 0 || row_cap > UINT32_MAX
      || row_cap > header_->row_count_ || header_->data_length_ < 0) {
    return OB_INVALID_ARGUMENT;
  }
  if (0 == row_cap) {
    return OB_SUCCESS;
  }
  const int64_t block_size = header_->header_size_ + header_->data_length_;
  batch.rows.reserve(row_cap);
  for (int64_t i = 0; i < row_cap; ++i) {
    if (row_ids[i] < 0 || row_ids[i] >= header_->row_count_) {
      return OB_ERR_UNEXPECTED;
    }
    batch.rows.push_back(static_cast<uint32_t>(row_ids[i]));
  }
  const rust::Slice<const uint8_t> block(
      reinterpret_cast<const uint8_t *>(header_), static_cast<std::size_t>(block_size));
  const rust::Slice<const uint32_t> rows(batch.rows.data(), batch.rows.size());
  const rust::Slice<const uint32_t> columns(batch.columns.data(), batch.columns.size());
  batch.result.emplace(micro_block::project_flat_block(block, rows, columns));
  switch (batch.result->code) {
    case micro_block::Code::Ok:
      ret = OB_SUCCESS;
      break;
    case micro_block::Code::Unsupported:
      return OB_NOT_SUPPORTED;
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
    default:
      return OB_ERR_UNEXPECTED;
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
  int ret = OB_SUCCESS;
  if (!batch.result.has_value() || row_idx < 0 || row_idx >= batch.rows.size()
      || col_idx < 0 || col_idx >= batch.columns.size()
      || batch.result->rows[row_idx] != batch.rows[row_idx]) {
    return OB_INVALID_DATA;
  }
  const int64_t block_size = header_->header_size_ + header_->data_length_;
  const micro_block::CellDesc &cell =
      batch.result->cells[row_idx * batch.columns.size() + col_idx];
  datum.reuse();
  if (micro_block::CellKind::Null == cell.kind) {
    datum.set_null();
  } else if (micro_block::CellKind::Nop == cell.kind) {
    datum.set_nop();
  } else if (micro_block::CellKind::UInt == cell.kind) {
    datum.set_uint(cell.value);
  } else if ((micro_block::CellKind::Borrowed != cell.kind
              && micro_block::CellKind::Outrow != cell.kind)
             || cell.offset > static_cast<uint64_t>(block_size)
             || cell.length > static_cast<uint64_t>(block_size) - cell.offset) {
    ret = OB_INVALID_DATA;
  } else if (OB_FAIL(datum.from_buf_enhance(
      reinterpret_cast<const char *>(header_) + cell.offset, cell.length))) {
  } else if (micro_block::CellKind::Outrow == cell.kind) {
    datum.set_outrow();
  }
  return ret;
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
  if (OB_UNLIKELY(nullptr == header_ ||
                  nullptr == read_info_ ||
                  row_cap > header_->row_count_)) {
    ret = OB_INVALID_ARGUMENT;
  } else if (contains_null) {
    count = row_cap;
  } else {
    count = 0;
    int64_t row_idx = common::OB_INVALID_INDEX;
    const ObColumnIndexArray &cols_index = read_info_->get_columns_index();
    int64_t col_idx = cols_index.at(col);
    ObStorageDatum datum;
    bool read_by_rust = false;
    RustFlatBatch rust_batch;
    if (col_idx >= 0) {
      rust_batch.columns.push_back(static_cast<uint32_t>(col_idx));
      ret = try_project_rust(row_ids, row_cap, rust_batch);
      if (OB_NOT_SUPPORTED == ret) {
        ret = OB_SUCCESS;
      } else if (OB_SUCC(ret)) {
        read_by_rust = true;
      }
    }
    for (int64_t i = 0; OB_SUCC(ret) && i < row_cap; ++i) {
      row_idx = row_ids[i];
      if (OB_UNLIKELY(row_idx < 0 || row_idx >= header_->row_count_)) {
        ret = OB_ERR_UNEXPECTED;
      } else if (read_by_rust && OB_FAIL(read_rust_cell(rust_batch, i, 0, datum))) {
      } else if (!read_by_rust && OB_FAIL(flat_row_reader_.read_column(
          data_begin_ + index_data_[row_idx],
          index_data_[row_idx + 1] - index_data_[row_idx],
          col_idx,
          datum))) {
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
  if (OB_UNLIKELY(nullptr == header_ ||
                  nullptr == read_info_ ||
                  row_cap > header_->row_count_)) {
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
    const bool has_lob_out_row = col_param.get_meta_type().is_lob_storage() && header_->has_lob_out_row();
    bool need_reuse_lob_locator = false;
    bool read_by_rust = false;
    RustFlatBatch rust_batch;
    if (col_idx >= 0) {
      rust_batch.columns.push_back(static_cast<uint32_t>(col_idx));
      ret = try_project_rust(row_ids, row_cap, rust_batch);
      if (OB_NOT_SUPPORTED == ret) {
        ret = OB_SUCCESS;
      } else if (OB_SUCC(ret)) {
        read_by_rust = true;
      }
    }
    for (int64_t i = 0; OB_SUCC(ret) && i < row_cap; ++i) {
      row_idx = row_ids[i];
      datum.set_nop();
      if (OB_UNLIKELY(row_idx < 0 || row_idx >= header_->row_count_)) {
        ret = OB_ERR_UNEXPECTED;
      } else if (read_by_rust && OB_FAIL(read_rust_cell(rust_batch, i, 0, tmp_datum))) {
      } else if (!read_by_rust && OB_FAIL(flat_row_reader_.read_column(
          data_begin_ + index_data_[row_idx],
          index_data_[row_idx + 1] - index_data_[row_idx],
          col_idx,
          tmp_datum))) {
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
  if (OB_UNLIKELY(nullptr == header_ ||
                  nullptr == read_info_ ||
                  row_cap > header_->row_count_ ||
                  !row_buf.is_valid() ||
                  agg_cells.empty())) {
    ret = OB_INVALID_ARGUMENT;
  } else if (OB_FAIL(row_buf.reserve(read_info_->get_request_count()))) {
  } else {
    const ObColDescIArray &cols_desc = read_info_->get_columns_desc();
    ObStorageDatum tmp_datum; // used for deep copy decimalint
    int64_t row_idx = common::OB_INVALID_INDEX;
    const common::ObIArray<ObColumnParam *> *col_params = read_info_->get_columns();
    const bool has_lob_out_row = iter_param.has_lob_column_out() && header_->has_lob_out_row();
    bool need_reuse_lob_locator = false;
    bool read_by_rust = false;
    RustFlatBatch rust_batch;
    if (OB_ISNULL(col_params)) {
      ret = OB_ERR_UNEXPECTED;
    } else {
      ret = try_get_rows_rust(*read_info_, row_ids, row_cap, rust_batch);
      if (OB_NOT_SUPPORTED == ret) {
        ret = OB_SUCCESS;
      } else if (OB_SUCC(ret)) {
        read_by_rust = true;
      }
    }
    for (int64_t idx = 0; OB_SUCC(ret) && idx < row_cap; ++idx) {
      row_idx = row_ids[idx];
      if (OB_UNLIKELY(row_idx < 0 || row_idx >= header_->row_count_)) {
        ret = OB_ERR_UNEXPECTED;
      } else if (read_by_rust && OB_FAIL(read_rust_row(rust_batch, idx, row_buf))) {
      } else if (!read_by_rust && OB_FAIL(flat_row_reader_.read_row(
          data_begin_ + index_data_[row_idx],
          index_data_[row_idx + 1] - index_data_[row_idx],
          read_info_,
          row_buf))) {
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
  } else if(OB_UNLIKELY(nullptr == header_ ||
                        nullptr == read_info_ ||
                        col_offset >= read_info_->get_columns_index().count() ||
                        row_index < 0 || row_index >= header_->row_count_)) {
    ret = OB_INVALID_ARGUMENT;
  } else {
    const ObColumnIndexArray &cols_index = read_info_->get_columns_index();
    int64_t col_idx = cols_index.at(col_offset);
    const ObColDescIArray &cols_desc = read_info_->get_columns_desc();
    const ObObjType obj_type = cols_desc.at(col_offset).col_type_.get_type();
    const ObObjDatumMapType map_type = ObDatum::get_obj_datum_map_type(obj_type);
    ObStorageDatum tmp_datum; // used for deep copy decimalint
    if (OB_UNLIKELY(col_idx >= header_->column_count_)) {
      ret = OB_ERR_UNEXPECTED;
    } else if (col_idx >= 0) {
      const int32_t row_id = static_cast<int32_t>(row_index);
      RustFlatBatch rust_batch;
      rust_batch.columns.push_back(static_cast<uint32_t>(col_idx));
      ret = try_project_rust(&row_id, 1, rust_batch);
      if (OB_NOT_SUPPORTED == ret) {
        ret = flat_row_reader_.read_column(
            data_begin_ + index_data_[row_index],
            index_data_[row_index + 1] - index_data_[row_index],
            col_idx,
            tmp_datum);
      } else if (OB_SUCC(ret)) {
        ret = read_rust_cell(rust_batch, 0, 0, tmp_datum);
      }
    } else {
      ret = flat_row_reader_.read_column(
          data_begin_ + index_data_[row_index],
          index_data_[row_index + 1] - index_data_[row_index],
          col_idx,
          tmp_datum);
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
