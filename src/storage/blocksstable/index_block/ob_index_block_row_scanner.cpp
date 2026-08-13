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
#include "ob_index_block_row_scanner.h"
#include "storage/access/ob_rows_info.h"
#include "storage/tablet/ob_tablet.h"

namespace oceanbase
{
using namespace storage;
using namespace common;
namespace blocksstable
{

int ObIndexBlockDataHeader::get_index_data(
    const int64_t row_idx, const char *&index_ptr, int64_t &index_len) const
{
  int ret = OB_SUCCESS;
  if (OB_UNLIKELY(!is_valid() || row_idx >= row_cnt_ || row_idx < 0)) {
    ret = OB_INVALID_ARGUMENT;
    LOG_WARN("Invalid row count", K(ret), K(row_idx), K_(row_cnt), KPC(this));
  } else {
    const ObStorageDatum &datum = index_datum_array_[row_idx];
    ObString index_data_buf = datum.get_string();
    if (OB_UNLIKELY(index_data_buf.empty())) {
      ret = OB_ERR_UNEXPECTED;
      LOG_WARN("Unexpected null index data buf", K(ret), K(datum), K(row_idx));
    } else {
      index_ptr = index_data_buf.ptr();
      index_len = index_data_buf.length();
    }
  }
  if (OB_SUCC(ret)) {
  }
  return ret;
}

int ObIndexBlockDataHeader::deep_copy_transformed_index_block(
    const ObIndexBlockDataHeader &header,
    const int64_t buf_size,
    char *buf,
    int64_t &pos)
{
  int ret = OB_SUCCESS;
  if (OB_UNLIKELY(!header.is_valid() || buf_size < 0 || pos >= buf_size || header.data_buf_size_ > buf_size)
      || OB_ISNULL(buf)) {
    ret = OB_INVALID_ARGUMENT;
    LOG_WARN("Invalid argument for copy transformed index block", K(ret), KP(buf),
        K(header), K(buf_size), K(pos));
  } else {
    char *data_buf = buf + pos;
    ObStorageDatum *index_datum_array = new (buf + pos) ObStorageDatum [header.row_cnt_];
    pos += sizeof(ObStorageDatum) * header.row_cnt_;
    const int64_t align_inc = common::upper_align(reinterpret_cast<uint64_t>(buf + pos), ObMicroBlockData::ALIGN_SIZE) - reinterpret_cast<uint64_t>(buf + pos);
    pos += align_inc;
    common::ObPointerSwizzleNode *ps_node_arr = new (buf + pos) common::ObPointerSwizzleNode[header.row_cnt_];
    pos += sizeof(common::ObPointerSwizzleNode) * header.row_cnt_;
    ObRowkeyVector *rowkey_vector = new (buf + pos) ObRowkeyVector();
    pos += sizeof(ObRowkeyVector);
    if (OB_FAIL(rowkey_vector->deep_copy(buf, pos, buf_size, *header.rowkey_vector_))) {
    } else {
      for (int64_t row_idx = 0; OB_SUCC(ret) && row_idx < header.row_cnt_; ++row_idx) {
        ps_node_arr[row_idx] = header.ps_node_array_[row_idx];
        if (OB_FAIL(index_datum_array[row_idx].deep_copy(header.index_datum_array_[row_idx], buf, buf_size, pos))) {
        }
      }
    }
    if (OB_SUCC(ret)) {
      pos += ObMicroBlockData::ALIGN_REDUNDANCY_SIZE - align_inc;
      rowkey_vector_ = rowkey_vector;
      index_datum_array_ = index_datum_array;
      row_cnt_ = header.row_cnt_;
      col_cnt_ = header.col_cnt_;
      ps_node_array_ = ps_node_arr;
      data_buf_ = data_buf;
      data_buf_size_ = header.data_buf_size_;
    }
  }
  return ret;
}

ObIndexBlockDataTransformer::ObIndexBlockDataTransformer()
  : allocator_(lib::ObMemAttr("IdxBlkDataTrans")), micro_reader_helper_() {}

ObIndexBlockDataTransformer::~ObIndexBlockDataTransformer()
{
}

// Transform block data to look-up format and store in transform buffer
int ObIndexBlockDataTransformer::transform(
    const ObMicroBlockData &block_data,
    ObMicroBlockData &transformed_data,
    ObIAllocator &allocator,
    char *&allocated_buf,
    const ObITableReadInfo *table_read_info)
{
  int ret = OB_SUCCESS;
  ObDatumRow row;
  char *block_buf = nullptr; // transformed block buf
  int64_t pos = 0;
  ObIMicroBlockReader *micro_reader = nullptr;
  ObMicroBlockHeader *new_micro_header = nullptr;
  const ObMicroBlockHeader *micro_block_header =
      reinterpret_cast<const ObMicroBlockHeader *>(block_data.get_buf());
  const int64_t col_cnt = micro_block_header->column_count_;
  const int64_t row_cnt = micro_block_header->row_count_;
  int64_t mem_limit = 0;
  if (OB_UNLIKELY(nullptr != table_read_info && col_cnt - 1 > table_read_info->get_rowkey_count())) {
    ret = OB_ERR_UNEXPECTED;
    LOG_WARN("Unexpected rowkey count", K(ret), K(col_cnt), KPC(table_read_info));
  } else if (OB_UNLIKELY(!block_data.is_valid() || !micro_block_header->is_valid())) {
    ret = OB_INVALID_ARGUMENT;
    LOG_WARN("Invalid argument", K(ret), K(block_data), KPC(micro_block_header));
  } else if (OB_FAIL(get_reader(block_data.get_store_type(), micro_reader))) {
  } else if (OB_FAIL(micro_reader->init(block_data, nullptr))) {
  } else if (OB_FAIL(row.init(allocator, col_cnt))) {
  } else if (OB_FAIL(get_transformed_upper_mem_size(table_read_info, block_data.get_buf(), mem_limit))) {
    } else if (OB_ISNULL(block_buf = static_cast<char *>(allocator.alloc(mem_limit)))) {
    ret = OB_ALLOCATE_MEMORY_FAILED;
    LOG_WARN("Failed to allocate memory for transformed block buf", K(ret), K(mem_limit));
  } else if (OB_FAIL(micro_block_header->deep_copy(block_buf, mem_limit, pos, new_micro_header))) {
  } else if (OB_UNLIKELY(!new_micro_header->is_valid())) {
    ret = OB_ERR_UNEXPECTED;
    LOG_WARN("invalid copied micro block header", K(ret), KPC(new_micro_header));
  } else {
    const int64_t micro_header_size = pos;
    ObIndexBlockDataHeader *idx_header = new (block_buf + pos) ObIndexBlockDataHeader();
    pos += sizeof(ObIndexBlockDataHeader);
    ObStorageDatum *index_datum_array = new (block_buf + pos) ObStorageDatum [row_cnt];
    pos += sizeof(ObStorageDatum) * row_cnt;
    // The ps_node_arr undergoes atomic operations during usage and thus requires alignment to 
    // ObMicroBlockData::ALIGN_SIZE bytes on ARM architecture for proper functioning.
    const int64_t align_inc = common::upper_align(reinterpret_cast<uint64_t>(block_buf + pos), ObMicroBlockData::ALIGN_SIZE) - reinterpret_cast<uint64_t>(block_buf + pos);
    pos += align_inc;
    common::ObPointerSwizzleNode *ps_node_arr = new (block_buf + pos) common::ObPointerSwizzleNode[row_cnt];
    pos += sizeof(common::ObPointerSwizzleNode) * row_cnt;
    ObRowkeyVector *rowkey_vector = nullptr;
    if (OB_FAIL(ObRowkeyVector::construct_rowkey_vector(row_cnt,
                                                        col_cnt - 1,
                                                        table_read_info,
                                                        block_buf,
                                                        pos,
                                                        mem_limit,
                                                        rowkey_vector))) {
    } else {
      for (int64_t row_idx = 0; OB_SUCC(ret) && row_idx < row_cnt; ++row_idx) {
        row.reuse();
        if (OB_FAIL(micro_reader->get_row(row_idx, row))) {
        } else {
          for (int64_t col_idx = 0; OB_SUCC(ret) && col_idx < col_cnt - 1; ++col_idx) {
            if (OB_FAIL(rowkey_vector->columns_[col_idx].fill_column_datum(block_buf,
                                                                           pos,
                                                                           mem_limit,
                                                                           row_idx,
                                                                           row.storage_datums_[col_idx]))) {
            }
          }
          if (FAILEDx(index_datum_array[row_idx].deep_copy(row.storage_datums_[col_cnt - 1], block_buf, mem_limit, pos))) {
            LOG_WARN("Failed to deep copy storage datum to buf", K(ret), K(row_idx), K(col_cnt));
          }
        }
      }
      if (FAILEDx(rowkey_vector->set_construct_finished())) {
        LOG_WARN("Failed to set construct finished", K(ret));
      }
    }

    if (OB_SUCC(ret)) {
      idx_header->row_cnt_ = row_cnt;
      idx_header->col_cnt_ = col_cnt;
      idx_header->rowkey_vector_ = rowkey_vector;
      idx_header->index_datum_array_ = index_datum_array;
      idx_header->ps_node_array_ = ps_node_arr;
      idx_header->data_buf_ = block_buf + micro_header_size;
      // Ensure that extra_buf can at most accommodate ObMicroBlockData::ALIGN_REDUNDANCY_SIZE additional bytes for redundancy.
      idx_header->data_buf_size_ = pos + (ObMicroBlockData::ALIGN_REDUNDANCY_SIZE - align_inc) - micro_header_size;
      transformed_data.buf_ = block_buf;
      transformed_data.size_ = micro_header_size;
      transformed_data.extra_buf_ = idx_header->data_buf_;
      transformed_data.extra_size_ = idx_header->data_buf_size_;
      transformed_data.type_ = ObMicroBlockData::INDEX_BLOCK;
      allocated_buf = block_buf;
    }
  }

  if (OB_FAIL(ret)) {
    LOG_WARN("fail to transform index block to in_memory format", K(ret),
        KPC(micro_block_header), KPC(new_micro_header), K(block_data));
    if (nullptr != block_buf) {
      allocator.free(block_buf);
    }
  }
  return ret;
}

int ObIndexBlockDataTransformer::get_transformed_upper_mem_size(
    const ObITableReadInfo *table_read_info,
    const char *raw_block_data,
    int64_t &mem_limit)
{
  int ret = OB_SUCCESS;
  mem_limit = 0;
  const ObMicroBlockHeader *micro_header =
      reinterpret_cast<const ObMicroBlockHeader *>(raw_block_data);
  if (OB_ISNULL(raw_block_data) || OB_UNLIKELY(!micro_header->is_valid())) {
    ret = OB_INVALID_ARGUMENT;
    LOG_WARN("Invalid argument", K(ret), KP(raw_block_data), KPC(micro_header));
  } else {
    int64_t rowkey_vector_size = 0;
    mem_limit += micro_header->get_serialize_size();
    mem_limit += sizeof(ObIndexBlockDataHeader);
    mem_limit += micro_header->row_count_ * sizeof(ObStorageDatum);
    mem_limit += micro_header->row_count_ * sizeof(common::ObPointerSwizzleNode);
    if (OB_FAIL(ObRowkeyVector::get_occupied_size(micro_header->row_count_,
                                                  micro_header->column_count_ - 1,
                                                  table_read_info,
                                                  rowkey_vector_size))) {
    } else {
      mem_limit += rowkey_vector_size;
      mem_limit += micro_header->original_length_;
      mem_limit += ObMicroBlockData::ALIGN_REDUNDANCY_SIZE;
    }
  }
  return ret;
}

int ObIndexBlockDataTransformer::get_reader(
    const ObRowStoreType store_type,
    ObIMicroBlockReader *&micro_reader)
{
  int ret = OB_SUCCESS;
  if (!micro_reader_helper_.is_inited() && OB_FAIL(micro_reader_helper_.init(allocator_))) {
    LOG_WARN("Fail to init micro reader helper", K(ret));
  } else if (OB_FAIL(micro_reader_helper_.get_reader(store_type, micro_reader))) {
  }
  return ret;
}

/******************             ObIndexBlockIterParam              **********************/
ObIndexBlockIterParam::ObIndexBlockIterParam()
  : sstable_(nullptr),
    tablet_(nullptr)
{
}

ObIndexBlockIterParam::ObIndexBlockIterParam(const ObSSTable *sstable, const ObTablet *tablet)
  : sstable_(sstable),
    tablet_(tablet)
{
}

ObIndexBlockIterParam::~ObIndexBlockIterParam()
{
  reset();
}

ObIndexBlockIterParam &ObIndexBlockIterParam::operator=(const ObIndexBlockIterParam &other)
{
  sstable_ = other.sstable_;
  tablet_ = other.tablet_;
  return *this;
}


void ObIndexBlockIterParam::reset()
{
  sstable_ = nullptr;
  tablet_ = nullptr;
}

bool ObIndexBlockIterParam::is_valid() const
{
  return OB_NOT_NULL(sstable_) && OB_NOT_NULL(tablet_);
}

/******************             ObIndexBlockRowIterator              **********************/
ObIndexBlockRowIterator::ObIndexBlockRowIterator()
  : is_inited_(false),
    is_reverse_scan_(false),
    advance_scan_state_(),
    iter_step_(1),
    idx_row_parser_(),
    datum_utils_(nullptr)
{

}

ObIndexBlockRowIterator::~ObIndexBlockRowIterator()
{
  reset();
}

void ObIndexBlockRowIterator::reset()
{
  iter_step_ = 1;
  datum_utils_ = nullptr;
  is_reverse_scan_ = false;
  advance_scan_state_.reset();
  idx_row_parser_.reset();
  is_inited_ = false;
}

void ObIndexBlockRowIterator::reuse()
{
  advance_scan_state_.reset();
}

/******************             ObRAWIndexBlockRowIterator              **********************/
ObRAWIndexBlockRowIterator::ObRAWIndexBlockRowIterator()
  : current_(ObIMicroBlockReaderInfo::INVALID_ROW_INDEX),
    start_(ObIMicroBlockReaderInfo::INVALID_ROW_INDEX),
    end_(ObIMicroBlockReaderInfo::INVALID_ROW_INDEX),
    micro_reader_(nullptr),
    allocator_(nullptr),
    datum_row_(nullptr),
    micro_reader_helper_(),
    endkey_()
{

}

ObRAWIndexBlockRowIterator::~ObRAWIndexBlockRowIterator()
{
  reset();
}

void ObRAWIndexBlockRowIterator::reset()
{
  ObIndexBlockRowIterator::reset();
  current_ = ObIMicroBlockReaderInfo::INVALID_ROW_INDEX;
  start_ = ObIMicroBlockReaderInfo::INVALID_ROW_INDEX;
  end_ = ObIMicroBlockReaderInfo::INVALID_ROW_INDEX;
  micro_reader_ = nullptr;
  if (nullptr != datum_row_) {
    datum_row_->~ObDatumRow();
    if (nullptr != allocator_) {
      allocator_->free(datum_row_);
    }
    datum_row_ = nullptr;
  }
  micro_reader_helper_.reset();
  allocator_ = nullptr;
  endkey_.reset();
}

void ObRAWIndexBlockRowIterator::reuse()
{
  ObIndexBlockRowIterator::reuse();
  current_ = ObIMicroBlockReaderInfo::INVALID_ROW_INDEX;
  start_ = ObIMicroBlockReaderInfo::INVALID_ROW_INDEX;
  end_ = ObIMicroBlockReaderInfo::INVALID_ROW_INDEX;
  idx_row_parser_.reset();
  endkey_.reset();
}

int ObRAWIndexBlockRowIterator::init(const ObMicroBlockData &idx_block_data,
                                     const ObStorageDatumUtils *datum_utils,
                                     ObIAllocator *allocator,
                                     const bool is_reverse_scan,
                                     const ObIndexBlockIterParam &iter_param)
{
  int ret = OB_SUCCESS;
  if (OB_ISNULL(allocator) || OB_ISNULL(datum_utils) || !datum_utils->is_valid()) {
    ret = OB_INVALID_ARGUMENT;
    LOG_WARN("invalid arguement", K(ret), KP(allocator), KPC(datum_utils));
  } else if (!micro_reader_helper_.is_inited() && OB_FAIL(micro_reader_helper_.init(*allocator))) {
    LOG_WARN("Fail to init micro reader helper", K(ret), KP(allocator));
  } else if (OB_FAIL(micro_reader_helper_.get_reader(idx_block_data.get_store_type(), micro_reader_))) {
  } else if (OB_FAIL(micro_reader_->init(idx_block_data, datum_utils))) {
  } else if (OB_FAIL(init_datum_row(*datum_utils, allocator))) {
  } else {
    is_reverse_scan_ = is_reverse_scan;
    iter_step_ = is_reverse_scan_ ? -1 : 1;
    datum_utils_ = datum_utils;
    allocator_ = allocator;
    is_inited_ = true;
  }
  return ret;
}


int ObRAWIndexBlockRowIterator::locate_key(const ObDatumRowkey &rowkey)
{
  int ret = OB_SUCCESS;
  int64_t begin_idx = -1;
  int64_t end_idx = -1;
  ObDatumRange range;
  if (IS_NOT_INIT) {
    ret = OB_NOT_INIT;
    LOG_WARN("Iter not opened yet", K(ret), KPC(this));
  } else if (OB_UNLIKELY(!rowkey.is_valid() || OB_ISNULL(micro_reader_))) {
    ret = OB_INVALID_ARGUMENT;
    LOG_WARN("invalid rowkey", K(ret), K(rowkey), KP(micro_reader_));
  } else {
    range.set_start_key(rowkey);
    range.end_key_.set_max_rowkey();
    range.set_left_closed();
    range.set_right_open();
    if (OB_FAIL(micro_reader_->locate_range(range, true, false, begin_idx, end_idx, true))) {
      if (OB_UNLIKELY(OB_BEYOND_THE_RANGE != ret)) {
        LOG_WARN("Fail to locate range in micro data", K(ret));
      } else {
        current_ = ObIMicroBlockReaderInfo::INVALID_ROW_INDEX;
      }
    }
  }
  if (OB_SUCC(ret)) {
    current_ = begin_idx;
    start_ = begin_idx;
    end_ = begin_idx;
  }
  return ret;
}

int ObRAWIndexBlockRowIterator::locate_range(const ObDatumRange &range,
                                             const bool is_left_border,
                                             const bool is_right_border)
{
  int ret = OB_SUCCESS;
  int64_t begin_idx = -1;
  int64_t end_idx = -1;
  current_ = ObIMicroBlockReaderInfo::INVALID_ROW_INDEX;
  if (IS_NOT_INIT) {
    ret = OB_NOT_INIT;
    LOG_WARN("Iter not opened yet", K(ret), KPC(this));
  } else if (OB_UNLIKELY(!range.is_valid() || OB_ISNULL(micro_reader_))) {
    ret = OB_INVALID_ARGUMENT;
    LOG_WARN("invalid range", K(ret), K(range), KP(micro_reader_));
  } else if (OB_FAIL(micro_reader_->locate_range(
          range, is_left_border, is_right_border, begin_idx, end_idx, true))) {
    if (OB_UNLIKELY(OB_BEYOND_THE_RANGE != ret)) {
      LOG_WARN("Fail to locate range with micro reader", K(ret));
    }
  } else {
  }

  if (OB_SUCC(ret)) {
    start_ = begin_idx;
    end_ = end_idx;
    current_ = is_reverse_scan_ ? end_idx : begin_idx;
  }
  return ret;
}

int ObRAWIndexBlockRowIterator::locate_range()
{
  int ret = OB_SUCCESS;
  int64_t row_count = 0;
  if (IS_NOT_INIT) {
    ret = OB_NOT_INIT;
    LOG_WARN("Iter not opened yet", K(ret), KPC(this));
  } else if (OB_FAIL(micro_reader_->get_row_count(row_count))) {
  } else {
    start_ = 0;
    end_ = row_count - 1;
    current_ = 0;
  }
  return ret;
}

int ObRAWIndexBlockRowIterator::skip_to_next_valid_position(const ObDatumRowkey &rowkey)
{
  int ret = OB_SUCCESS;
  bool equal = false;
  if (IS_NOT_INIT) {
    ret = OB_NOT_INIT;
    LOG_WARN("Iter not opened yet", K(ret), KPC(this));
  } else if (OB_FAIL(micro_reader_->find_bound(rowkey, true, current_, current_, equal))) {
  } else if (current_ == (end_ + 1)) {
    ret = OB_ITER_END;
  }
  return ret;
}

int ObRAWIndexBlockRowIterator::find_rowkeys_belong_to_same_idx_row(ObMicroIndexInfo &idx_block_row, int64_t &rowkey_begin_idx, int64_t &rowkey_end_idx, const ObRowsInfo *&rows_info)
{
  int ret = OB_SUCCESS;
  if (IS_NOT_INIT) {
    ret = OB_NOT_INIT;
    LOG_WARN("Iter not opened yet", K(ret), KPC(this));
  } else if (OB_ISNULL(rows_info)) {
    ret = OB_INVALID_ARGUMENT;
    LOG_WARN("invalid rows info", K(ret));
  } else {
    bool is_decided = false;
    for (; OB_SUCC(ret) && rowkey_begin_idx < rowkey_end_idx; ++rowkey_begin_idx) {
      if (rows_info->is_row_skipped(rowkey_begin_idx)) {
        continue;
      }
      const ObDatumRowkey &rowkey = rows_info->get_rowkey(rowkey_begin_idx);
      int32_t cmp_ret = 0;
      if (OB_FAIL(compare_rowkey(rowkey, cmp_ret))) {
      } else {
        cmp_ret = -cmp_ret;
      }

      if (OB_FAIL(ret)) {
      } else if (cmp_ret > 0) {
        idx_block_row.rowkey_end_idx_ = rowkey_begin_idx;
        is_decided = true;
        break;
      } else if (cmp_ret == 0) {
        idx_block_row.rowkey_end_idx_ = rowkey_begin_idx + 1;
        is_decided = true;
        break;
      }
    }

    if (OB_SUCC(ret) && !is_decided) {
      idx_block_row.rowkey_end_idx_ = rowkey_begin_idx;
    }
  }
  return ret;
}

int ObRAWIndexBlockRowIterator::compare_rowkey(const ObDatumRowkey &rowkey, int32_t &cmp_ret)
{
  int ret = OB_SUCCESS;
  cmp_ret = 0;
  if (IS_NOT_INIT) {
    ret = OB_NOT_INIT;
    LOG_WARN("Iter not opened yet", K(ret), KPC(this));
  } else if (OB_FAIL(micro_reader_->compare_rowkey(rowkey, current_, cmp_ret))) {
  }
  return ret;
}

int ObRAWIndexBlockRowIterator::check_blockscan(const ObDatumRowkey &rowkey, bool &can_blockscan)
{
  int ret = OB_SUCCESS;
  int cmp_ret = 0;
  ObDatumRowkey last_endkey;
  ObDatumRow tmp_datum_row; // Normally will use local datum buf, won't allocate memory
  const int64_t request_cnt = datum_utils_->get_rowkey_count() + 1;
  if (IS_NOT_INIT) {
    ret = OB_NOT_INIT;
    LOG_WARN("Iter not opened yet", K(ret), KPC(this));
  } else if (OB_UNLIKELY(!rowkey.is_valid())) {
    ret = OB_INVALID_ARGUMENT;
    LOG_WARN("invalid rowkey", K(ret), K(rowkey));
  } else if (OB_FAIL(tmp_datum_row.init(request_cnt))) {
  } else if (OB_FAIL(micro_reader_->get_row(end_, tmp_datum_row))) {
  } else if (OB_FAIL(last_endkey.assign(tmp_datum_row.storage_datums_, datum_utils_->get_rowkey_count()))) {
  } else if (OB_FAIL(last_endkey.compare(rowkey, *datum_utils_, cmp_ret, false))) {
  } else {
    can_blockscan = cmp_ret < 0;
  }
  return ret;
}

bool ObRAWIndexBlockRowIterator::end_of_block() const
{
  return current_ < start_
      || current_ > end_
      || current_ == ObIMicroBlockReaderInfo::INVALID_ROW_INDEX;
}

int ObRAWIndexBlockRowIterator::get_current(const ObIndexBlockRowHeader *&idx_row_header,
                                            ObCommonDatumRowkey &endkey)
{
  int ret = OB_SUCCESS;
  idx_row_header = nullptr;
  endkey.reset();
  const int64_t rowkey_column_count = datum_utils_->get_rowkey_count();
  idx_row_parser_.reset();
  endkey_.reset();
  if (IS_NOT_INIT) {
    ret = OB_NOT_INIT;
    LOG_WARN("Iter not opened yet", K(ret), KPC(this));
  } else if (OB_ISNULL(datum_row_)) {
    ret = OB_ERR_UNEXPECTED;
    LOG_WARN("Unexpected null pointer to index row", K(ret));
  } else if (OB_FAIL(micro_reader_->get_row(current_, *datum_row_))) {
  } else if (OB_FAIL(idx_row_parser_.init(rowkey_column_count, *datum_row_))) {
  } else if (OB_FAIL(idx_row_parser_.get_header(idx_row_header))) {
  } else if (OB_FAIL(endkey_.assign(datum_row_->storage_datums_, rowkey_column_count))) {
  } else {
    endkey.set_compact_rowkey(&endkey_);
  }
  return ret;
}

int ObRAWIndexBlockRowIterator::get_next(const ObIndexBlockRowHeader *&idx_row_header,
                                         ObCommonDatumRowkey &endkey,
                                         bool &is_scan_left_border,
                                         bool &is_scan_right_border,
                                         const ObIndexBlockRowMinorMetaInfo *&idx_minor_info,
                                         const char *&agg_row_buf,
                                         int64_t &agg_buf_size,
                                         int64_t &row_offset)
{
  int ret = OB_SUCCESS;
  idx_row_header = nullptr;
  endkey.reset();
  idx_minor_info = nullptr;
  agg_row_buf = nullptr;
  agg_buf_size = 0;
  row_offset = 0;
  if (IS_NOT_INIT) {
    ret = OB_NOT_INIT;
    LOG_WARN("Iter not opened yet", K(ret), KPC(this));
  } else if (OB_FAIL(get_current(idx_row_header, endkey))) {
  } else if (OB_UNLIKELY(nullptr == idx_row_header || !endkey.is_valid())) {
    ret = OB_ERR_UNEXPECTED;
    LOG_WARN("Unexpected null index block row header/endkey", K(ret), KP(idx_row_header), K(endkey));
  } else if (OB_FAIL(idx_row_parser_.parse_minor_meta_and_agg_row(idx_minor_info, agg_row_buf, agg_buf_size))) {
  } else {
    row_offset = idx_row_parser_.get_row_offset();
    is_scan_left_border = current_ == start_;
    is_scan_right_border = current_ == end_;
    current_ += iter_step_;
  }
  return ret;
}

int ObRAWIndexBlockRowIterator::init_datum_row(const ObStorageDatumUtils &datum_utils, ObIAllocator *allocator)
{
  int ret = OB_SUCCESS;
  if (nullptr != datum_row_ && datum_row_->is_valid()) {
    // row allocated
  } else if (nullptr != datum_row_) {
    if (OB_ISNULL(allocator)) {
      ret = OB_INVALID_ARGUMENT;
      LOG_WARN("allocator is null", K(ret), KP(allocator));
    } else {
      datum_row_->~ObDatumRow();
      allocator->free(datum_row_);
      datum_row_ = nullptr;
    }
  }

  if (OB_SUCC(ret)) {
    if (nullptr == datum_row_) {
      const int64_t request_cnt = datum_utils.get_rowkey_count() + 1;
      void *buf = nullptr;
      if (OB_ISNULL(allocator)) {
        ret = OB_INVALID_ARGUMENT;
        LOG_WARN("allocator is null", K(ret), KP(allocator));
      } else if (OB_ISNULL(buf = allocator->alloc(sizeof(ObDatumRow)))) {
        ret = OB_ALLOCATE_MEMORY_FAILED;
        LOG_WARN("Fail to allocate memory for datum row", K(ret));
      } else if (FALSE_IT(datum_row_ = new (buf) ObDatumRow())) {
      } else if (OB_FAIL(datum_row_->init(*allocator, request_cnt))) {
      }

      if (OB_FAIL(ret) && nullptr != buf) {
        if (OB_NOT_NULL(datum_row_)) {
          datum_row_->~ObDatumRow();
        }
        allocator->free(buf);
        datum_row_ = nullptr;
      }
    }
  }
  return ret;
}


int ObRAWIndexBlockRowIterator::get_index_row_count(const ObDatumRange &range,
                                                    const bool is_left_border,
                                                    const bool is_right_border,
                                                    int64_t &index_row_count,
                                                    int64_t &data_row_count)
{
  int ret = OB_SUCCESS;
  index_row_count = 0;
  if (IS_NOT_INIT) {
    ret = OB_NOT_INIT;
    LOG_WARN("Iter not opened yet", K(ret), KPC(this));
  } else {
    if (start_ < 0 || end_ < 0) {
      index_row_count = 0;
    } else {
      index_row_count = end_ - start_ + 1;
    }
  }
  return ret;
}

/******************             ObTFMIndexBlockRowIterator              **********************/
ObTFMIndexBlockRowIterator::ObTFMIndexBlockRowIterator()
  : idx_data_header_(nullptr),
    cur_node_index_(0)
{

}

ObTFMIndexBlockRowIterator::~ObTFMIndexBlockRowIterator()
{
  reset();
}

void ObTFMIndexBlockRowIterator::reset()
{
  ObRAWIndexBlockRowIterator::reset();
  idx_data_header_ = nullptr;
  cur_node_index_ = 0;
}

void ObTFMIndexBlockRowIterator::reuse()
{
  ObRAWIndexBlockRowIterator::reuse();
  idx_data_header_ = nullptr;
}

int ObTFMIndexBlockRowIterator::init(const ObMicroBlockData &idx_block_data,
                                     const ObStorageDatumUtils *datum_utils,
                                     ObIAllocator *allocator,
                                     const bool is_reverse_scan,
                                     const ObIndexBlockIterParam &iter_param)
{
  int ret = OB_SUCCESS;
  idx_data_header_ = reinterpret_cast<const ObIndexBlockDataHeader *>(idx_block_data.get_extra_buf());
  if (OB_ISNULL(allocator) || OB_ISNULL(datum_utils) || !datum_utils->is_valid()) {
    ret = OB_INVALID_ARGUMENT;
    LOG_WARN("invalid arguement", K(ret), KP(allocator), KPC(datum_utils));
  } else if (!micro_reader_helper_.is_inited() && OB_FAIL(micro_reader_helper_.init(*allocator_))) {
    LOG_WARN("Fail to init micro reader helper", K(ret));
  } else if (OB_UNLIKELY(!idx_data_header_->is_valid())) {
    ret = OB_INVALID_ARGUMENT;
    LOG_WARN("Invalid index block data header", K(ret), KPC(idx_data_header_));
  } else {
    is_reverse_scan_ = is_reverse_scan;
    iter_step_ = is_reverse_scan_ ? -1 : 1;
    datum_utils_ = datum_utils;
    is_inited_ = true;
  }
  return ret;
}

int ObTFMIndexBlockRowIterator::locate_key(const ObDatumRowkey &rowkey)
{
  int ret = OB_SUCCESS;
  int64_t begin_idx = -1;
  int64_t end_idx = -1;
  if (IS_NOT_INIT) {
    ret = OB_NOT_INIT;
    LOG_WARN("Iter not opened yet", K(ret), KPC(this));
  } else if (OB_UNLIKELY(!rowkey.is_valid() || OB_ISNULL(idx_data_header_) || !idx_data_header_->is_valid())) {
    ret = OB_INVALID_ARGUMENT;
    LOG_WARN("invalid rowkey", K(ret), K(rowkey), KPC(idx_data_header_));
  } else if (OB_FAIL(idx_data_header_->rowkey_vector_->locate_key(0,
                                                                  idx_data_header_->row_cnt_,
                                                                  rowkey,
                                                                  *datum_utils_,
                                                                  begin_idx))) {
  } else if (begin_idx == idx_data_header_->row_cnt_) {
    begin_idx = ObIMicroBlockReaderInfo::INVALID_ROW_INDEX;
  }
  if (OB_SUCC(ret)) {
    current_ = begin_idx;
    start_ = begin_idx;
    end_ = begin_idx;
  }
  return ret;
}

int ObTFMIndexBlockRowIterator::locate_range(const ObDatumRange &range,
                                             const bool is_left_border,
                                             const bool is_right_border)
{
  int ret = OB_SUCCESS;
  int64_t begin_idx = -1;
  int64_t end_idx = -1;
  current_ = ObIMicroBlockReaderInfo::INVALID_ROW_INDEX;
  if (IS_NOT_INIT) {
    ret = OB_NOT_INIT;
    LOG_WARN("Iter not opened yet", K(ret), KPC(this));
  } else if (OB_UNLIKELY(!range.is_valid() || OB_ISNULL(idx_data_header_) || !idx_data_header_->is_valid())) {
    ret = OB_INVALID_ARGUMENT;
    LOG_WARN("invalid range", K(ret), K(range), KPC(idx_data_header_));
  } else if (OB_FAIL(locate_range_by_rowkey_vector(range, is_left_border, is_right_border, begin_idx, end_idx))) {
    if (OB_UNLIKELY(OB_BEYOND_THE_RANGE != ret)) {
      LOG_WARN("Failed to locate range by rowkey vector", K(ret));
    }
  } else {
    start_ = begin_idx;
    end_ = end_idx;
    current_ = is_reverse_scan_ ? end_idx : begin_idx;
  }
  return ret;
}

int ObTFMIndexBlockRowIterator::locate_range()
{
  int ret = OB_SUCCESS;
  if (IS_NOT_INIT) {
    ret = OB_NOT_INIT;
    LOG_WARN("Iter not opened yet", K(ret), KPC(this));
  } else {
    start_ = 0;
    end_ = idx_data_header_->row_cnt_ - 1;
    current_ = 0;
  }
  return ret;
}

int ObTFMIndexBlockRowIterator::check_blockscan(const ObDatumRowkey &rowkey, bool &can_blockscan)
{
  int ret = OB_SUCCESS;
  int cmp_ret = 0;
  if (IS_NOT_INIT) {
    ret = OB_NOT_INIT;
    LOG_WARN("Iter not opened yet", K(ret), KPC(this));
  } else if (OB_UNLIKELY(!rowkey.is_valid())) {
    ret = OB_INVALID_ARGUMENT;
    LOG_WARN("invalid rowkey", K(ret), K(rowkey));
  } else if (OB_UNLIKELY(!idx_data_header_->is_valid())) {
    ret = OB_ERR_UNEXPECTED;
    LOG_WARN("Unexpected index data header", K(ret), KPC(idx_data_header_));
  } else if (OB_FAIL(idx_data_header_->rowkey_vector_->compare_rowkey(rowkey, end_, *datum_utils_, cmp_ret, false))) {
  } else {
    can_blockscan = cmp_ret < 0;
  }
  return ret;
}

int ObTFMIndexBlockRowIterator::get_current(const ObIndexBlockRowHeader *&idx_row_header,
                                            ObCommonDatumRowkey &endkey)
{
  int ret = OB_SUCCESS;
  idx_row_header = nullptr;
  endkey.reset();
  const int64_t rowkey_column_count = datum_utils_->get_rowkey_count();
  idx_row_parser_.reset();
  const char *idx_data_buf = nullptr;
  int64_t idx_data_len = 0;
  if (IS_NOT_INIT) {
    ret = OB_NOT_INIT;
    LOG_WARN("Iter not opened yet", K(ret), KPC(this));
  } else if (OB_FAIL(idx_data_header_->get_index_data(current_, idx_data_buf, idx_data_len))) {
  } else if (OB_FAIL(idx_row_parser_.init(idx_data_buf, idx_data_len))) {
  } else if (OB_FAIL(idx_row_parser_.get_header(idx_row_header))) {
  } else if (OB_UNLIKELY(!idx_data_header_->is_valid())) {
    ret = OB_ERR_UNEXPECTED;
    LOG_WARN("Invalid idx data header", K(ret), KPC(idx_data_header_));
  } else if (OB_FAIL(idx_data_header_->rowkey_vector_->get_rowkey(current_, endkey))) {
  } else {
    cur_node_index_ = current_;
  }
  return ret;
}

int ObTFMIndexBlockRowIterator::get_next(const ObIndexBlockRowHeader *&idx_row_header,
                                         ObCommonDatumRowkey &endkey,
                                         bool &is_scan_left_border,
                                         bool &is_scan_right_border,
                                         const ObIndexBlockRowMinorMetaInfo *&idx_minor_info,
                                         const char *&agg_row_buf,
                                         int64_t &agg_buf_size,
                                         int64_t &row_offset)
{
  int ret = OB_SUCCESS;
  idx_row_header = nullptr;
  endkey.reset();
  idx_minor_info = nullptr;
  agg_row_buf = nullptr;
  agg_buf_size = 0;
  if (IS_NOT_INIT) {
    ret = OB_NOT_INIT;
    LOG_WARN("Iter not opened yet", K(ret), KPC(this));
  } else if (OB_FAIL(get_current(idx_row_header, endkey))) {
  } else if (OB_UNLIKELY(nullptr == idx_row_header || !endkey.is_valid())) {
    ret = OB_ERR_UNEXPECTED;
    LOG_WARN("Unexpected null index block row header/endkey", K(ret), KP(idx_row_header), K(endkey));
  } else if (OB_FAIL(idx_row_parser_.parse_minor_meta_and_agg_row(idx_minor_info, agg_row_buf, agg_buf_size))) {
  } else {
    row_offset = idx_row_parser_.get_row_offset();
    is_scan_left_border = current_ == start_;
    is_scan_right_border = current_ == end_;
    current_ += iter_step_;
  }
  return ret;
}

int ObTFMIndexBlockRowIterator::advance_to_border(const ObDatumRowkey &rowkey,
                                                  const bool is_left_border,
                                                  const bool is_right_border,
                                                  const ObMicroBlockRowIdRange &parent_row_range,
                                                  ObMicroBlockRowIdRange &row_id_range)
{
  int ret = OB_SUCCESS;
  if (IS_NOT_INIT) {
    ret = OB_NOT_INIT;
    LOG_WARN("Iter not opened yet", K(ret), KPC(this));
  } else if (OB_UNLIKELY(end_of_block())) {
    ret = OB_ERR_UNEXPECTED;
    LOG_WARN("Unexpected error", K(ret), K(end_of_block()));
  } else if (OB_UNLIKELY(!rowkey.is_valid())) {
    ret = OB_INVALID_ARGUMENT;
    LOG_WARN("invalid rowkey", K(ret), K(rowkey));
  } else if (OB_UNLIKELY(!idx_data_header_->is_valid())) {
    ret = OB_ERR_UNEXPECTED;
    LOG_WARN("Unexpected index data header", K(ret), KPC(idx_data_header_));
  } else if (OB_FAIL(advance_to_border_by_rowkey_vector(rowkey, is_left_border, is_right_border, parent_row_range, row_id_range))) {
  }
  return ret;
}

int ObTFMIndexBlockRowIterator::advance_to_border_by_rowkey_vector(const ObDatumRowkey &rowkey,
                                                                   const bool is_left_border,
                                                                   const bool is_right_border,
                                                                   const ObMicroBlockRowIdRange &parent_row_range,
                                                                   ObMicroBlockRowIdRange &row_id_range)
{
  int ret = OB_SUCCESS;
  const bool is_range_end = is_reverse_scan_ ? is_left_border : is_right_border;
  const int64_t begin = is_reverse_scan_ ? start_ : current_;
  const int64_t end = is_reverse_scan_ ? current_ + 1 : end_ + 1;
  int64_t found_pos = ObIMicroBlockReaderInfo::INVALID_ROW_INDEX;
  // do not need upper_bound for reverse scan, as only co sstable reach here.
  if (OB_FAIL(idx_data_header_->rowkey_vector_->locate_key(begin,
                                                           end,
                                                           rowkey,
                                                           *datum_utils_,
                                                           found_pos))) {
  } else if (!is_reverse_scan_) {
    // found_pos is safe to skip(end_key < border_rowkey).
    found_pos--;
    if (is_range_end && found_pos == end_) {
      // if is_range_end is true, we cannot skip all rowids because only subset of rowids statisy query range.
      found_pos--;
    }
    if (found_pos >= current_) {
      current_ = found_pos + 1;
      if (OB_FAIL(get_cur_row_id_range(parent_row_range, row_id_range))) {
      }
    }
  } else {
    // found_pos is safe to skip(end_key > border_rowkey).
    found_pos++;
    // found_pos != start_, there is no need to check is_range_end.
    if (found_pos <= current_ + 1) {
      current_ = found_pos - 1;
      if (OB_FAIL(get_cur_row_id_range(parent_row_range, row_id_range))) {
      }
    }
  }
  return ret;
}

int ObTFMIndexBlockRowIterator::get_end_key(ObCommonDatumRowkey &endkey)
{
  int ret = OB_SUCCESS;
  if (IS_NOT_INIT) {
    ret = OB_NOT_INIT;
    LOG_WARN("Iter not opened yet", K(ret), KPC(this));
  } else if (OB_UNLIKELY(!idx_data_header_->is_valid())) {
    ret = OB_ERR_UNEXPECTED;
    LOG_WARN("Unexpected index data header", K(ret), KPC(idx_data_header_));
  } else {
    endkey.set_compact_rowkey(idx_data_header_->rowkey_vector_->get_last_rowkey());
  }
  return ret;
}

int ObTFMIndexBlockRowIterator::get_cur_row_id_range(const ObMicroBlockRowIdRange &parent_row_range,
                                                     ObMicroBlockRowIdRange &row_id_range)
{
  int ret = OB_SUCCESS;
  const ObIndexBlockRowHeader *idx_row_header = nullptr;
  ObCommonDatumRowkey endkey;
  bool is_scan_left_border = false;
  bool is_scan_right_border = false;
  if (IS_NOT_INIT) {
    ret = OB_NOT_INIT;
    LOG_WARN("Iter not opened yet", K(ret), KPC(this));
  } else if (end_of_block()) {
    ret = OB_ERR_UNEXPECTED;
    LOG_WARN("Unexpected end of index block scanner", KPC(this));
  } else if (OB_FAIL(get_current(idx_row_header, endkey))) {
  } else if (OB_ISNULL(idx_row_header)) {
    ret = OB_ERR_UNEXPECTED;
    LOG_WARN("Unexpected null index block row header", K(ret));
  } else {
    row_id_range.start_row_id_ = idx_row_parser_.get_row_offset() - idx_row_header->get_row_count() + 1;
    row_id_range.end_row_id_ = idx_row_parser_.get_row_offset();
    if (idx_row_header->is_data_block()) {
      row_id_range.start_row_id_ += parent_row_range.start_row_id_;
      row_id_range.end_row_id_ += parent_row_range.start_row_id_;
    }
  }
  return ret;
}

int ObTFMIndexBlockRowIterator::skip_to_next_valid_position(const ObDatumRowkey &rowkey)
{
  int ret = OB_SUCCESS;
  if (IS_NOT_INIT) {
    ret = OB_NOT_INIT;
    LOG_WARN("Iter not opened yet", K(ret), KPC(this));
  } else if (OB_UNLIKELY(!idx_data_header_->is_valid())) {
    ret = OB_ERR_UNEXPECTED;
    LOG_WARN("Invalid idx data header", K(ret), KPC(idx_data_header_));
  } else {
    int64_t found_idx = ObIMicroBlockReaderInfo::INVALID_ROW_INDEX;
    if (OB_FAIL(idx_data_header_->rowkey_vector_->locate_key(current_,
                                                             end_ + 1,
                                                             rowkey,
                                                             *datum_utils_,
                                                             found_idx))) {
    } else if (end_ + 1 == found_idx) {
      ret = OB_ITER_END;
    } else {
      current_ = found_idx;
    }
  }
  return ret;
}

int ObTFMIndexBlockRowIterator::find_rowkeys_belong_to_same_idx_row(ObMicroIndexInfo &idx_block_row, int64_t &rowkey_begin_idx, int64_t &rowkey_end_idx, const ObRowsInfo *&rows_info)
{
  int ret = OB_SUCCESS;
  if (IS_NOT_INIT) {
    ret = OB_NOT_INIT;
    LOG_WARN("Iter not opened yet", K(ret), KPC(this));
  } else if (OB_UNLIKELY(nullptr == rows_info || !idx_data_header_->is_valid())) {
    ret = OB_INVALID_ARGUMENT;
    LOG_WARN("invalid rows info or header", K(ret), KP(rows_info), KPC(idx_data_header_));
  } else {
    bool is_decided = false;
    for (; OB_SUCC(ret) && rowkey_begin_idx < rowkey_end_idx; ++rowkey_begin_idx) {
      if (rows_info->is_row_skipped(rowkey_begin_idx)) {
        continue;
      }
      const ObDatumRowkey &rowkey = rows_info->get_rowkey(rowkey_begin_idx);
      int32_t cmp_ret = 0;
      if (OB_FAIL(idx_data_header_->rowkey_vector_->compare_rowkey(rowkey, current_, *datum_utils_, cmp_ret, false))) {
      } else if (cmp_ret < 0) {
        idx_block_row.rowkey_end_idx_ = rowkey_begin_idx;
        is_decided = true;
        break;
      } else if (cmp_ret == 0) {
        idx_block_row.rowkey_end_idx_ = rowkey_begin_idx + 1;
        is_decided = true;
        break;
      }
    }

    if (OB_SUCC(ret) && !is_decided) {
      idx_block_row.rowkey_end_idx_ = rowkey_begin_idx;
    }
  }
  return ret;
}

int ObTFMIndexBlockRowIterator::find_rowkeys_belong_to_curr_idx_row(ObMicroIndexInfo &idx_block_row, const int64_t rowkey_end_idx, const ObRowKeysInfo *rowkeys_info)
{
  int ret = OB_SUCCESS;
  if (IS_NOT_INIT) {
    ret = OB_NOT_INIT;
    LOG_WARN("Iter not opened yet", K(ret), KPC(this));
  } else if (OB_UNLIKELY(!idx_block_row.endkey_.is_valid() ||
                         nullptr == rowkeys_info)) {
    ret = OB_INVALID_ARGUMENT;
    LOG_WARN("invalid rowkeys info", K(ret), K(idx_block_row.endkey_), KP(rowkeys_info));
  } else {
    for (; OB_SUCC(ret) && idx_block_row.rowkey_end_idx_ < rowkey_end_idx; ++idx_block_row.rowkey_end_idx_) {
      if (rowkeys_info->is_rowkey_not_exist(idx_block_row.rowkey_end_idx_)) {
        continue;
      }
      const ObDatumRowkey &rowkey = rowkeys_info->get_rowkey(idx_block_row.rowkey_end_idx_);
      int cmp_ret = 0;
      if (OB_FAIL(rowkey.compare(idx_block_row.endkey_, *datum_utils_, cmp_ret, false))) {
      } else if (cmp_ret >= 0) {
        break;
      }
    }
  }
  return ret;
}

int ObTFMIndexBlockRowIterator::locate_range_by_rowkey_vector(
    const ObDatumRange &range,
    const bool is_left_border,
    const bool is_right_border,
    int64_t &begin_idx,
    int64_t &end_idx)
{
  int ret = OB_SUCCESS;
  if (OB_FAIL(idx_data_header_->rowkey_vector_->locate_range(range,
                                                             is_left_border,
                                                             is_right_border,
                                                             *datum_utils_,
                                                             begin_idx,
                                                             end_idx))) {
    if (OB_UNLIKELY(OB_BEYOND_THE_RANGE != ret)) {
      LOG_WARN("Failed to locate range by rowkey vector", K(ret));
    }
  }
  return ret;
}

/******************             ObIndexBlockRowScanner              **********************/
ObIndexBlockRowScanner::ObIndexBlockRowScanner()
  : query_range_(nullptr), macro_id_(), allocator_(nullptr), raw_iter_(nullptr), transformed_iter_(nullptr),
    ddl_iter_(nullptr), iter_(nullptr), datum_utils_(nullptr),
    range_idx_(0), nested_offset_(0), curr_rowkey_begin_idx_(0), rowkey_end_idx_(0),
    index_format_(ObIndexFormat::INVALID), parent_row_range_(), is_get_(false), is_reverse_scan_(false),
    is_left_border_(false), is_right_border_(false), is_inited_(false),
    iter_param_(), table_read_info_(nullptr)
{}

ObIndexBlockRowScanner::~ObIndexBlockRowScanner()
{
  reset();
}

void ObIndexBlockRowScanner::reuse()
{
  query_range_ = nullptr;
  if (OB_NOT_NULL(raw_iter_)) {
    raw_iter_->reuse();
  }
  if (OB_NOT_NULL(transformed_iter_)) {
    transformed_iter_->reuse();
  }
  if (OB_NOT_NULL(ddl_iter_)) {
    ddl_iter_->reuse();
  }
  is_left_border_ = false;
  is_right_border_ = false;
  parent_row_range_.reset();
  table_read_info_ = nullptr;
}

void ObIndexBlockRowScanner::reset()
{
  query_range_ = nullptr;
  parent_row_range_.reset();
  if (nullptr != raw_iter_) {
    raw_iter_->reset();
    if (nullptr != allocator_) {
      allocator_->free(raw_iter_);
      raw_iter_ = nullptr;
    }
  }
  if (nullptr != transformed_iter_) {
    transformed_iter_->reset();
    if (nullptr != allocator_) {
      allocator_->free(transformed_iter_);
      transformed_iter_ = nullptr;
    }
  }
  if (nullptr != ddl_iter_) {
    ddl_iter_->reset();
    if (nullptr != allocator_) {
      allocator_->free(ddl_iter_);
      ddl_iter_ = nullptr;
    }
  }
  iter_ = nullptr;
  datum_utils_ = nullptr;
  range_idx_ = 0;
  nested_offset_ = 0;
  curr_rowkey_begin_idx_ = 0;
  rowkey_end_idx_ = 0;
  index_format_ = ObIndexFormat::INVALID;
  is_get_ = false;
  is_reverse_scan_ = false;
  is_left_border_ = false;
  is_right_border_ = false;
  is_inited_ = false;
  iter_param_.reset();
  allocator_ = nullptr;
  table_read_info_ = nullptr;
}

int ObIndexBlockRowScanner::init(
    const ObStorageDatumUtils &datum_utils,
    ObIAllocator &allocator,
    const common::ObQueryFlag &query_flag,
    const int64_t nested_offset,
    const ObITableReadInfo *table_read_info)
{
  int ret = OB_SUCCESS;
  if (IS_INIT) {
    ret = OB_INIT_TWICE;
    LOG_WARN("Already inited", K(ret));
  } else if (OB_UNLIKELY(!datum_utils.is_valid())) {
    ret = OB_INVALID_ARGUMENT;
    LOG_WARN("invalid datum utils", K(ret), K(datum_utils));
  } else {
    allocator_ = &allocator;
    is_reverse_scan_ = query_flag.is_reverse_scan();
    datum_utils_ = &datum_utils;
    nested_offset_ = nested_offset;
    table_read_info_ = table_read_info;
    is_inited_ = true;
  }
  return ret;
}

int ObIndexBlockRowScanner::open(
    const MacroBlockId &macro_id,
    const ObMicroBlockData &idx_block_data,
    const ObDatumRowkey &rowkey,
    const int64_t range_idx,
    const ObMicroIndexInfo *idx_info)
{
  int ret = OB_SUCCESS;
  if (IS_NOT_INIT) {
    ret = OB_NOT_INIT;
    LOG_WARN("Not inited", K(ret));
  } else if (OB_UNLIKELY(!macro_id.is_valid() || !idx_block_data.is_valid() || !rowkey.is_valid()
      || !idx_block_data.is_index_block())) {
    ret = OB_INVALID_ARGUMENT;
    LOG_WARN("Invalid argument to open an index micro block", K(ret),
        K(macro_id), K(idx_block_data), K(rowkey), KP(idx_info));
  } else if (OB_FAIL(init_by_micro_data(idx_block_data))) {
  } else if (OB_ISNULL(iter_)) {
    ret = OB_ERR_UNEXPECTED;
    LOG_WARN("iter is null", K(index_format_), K(ret));
  } else if (OB_FAIL(iter_->locate_key(rowkey))) {
    if (OB_UNLIKELY(OB_BEYOND_THE_RANGE != ret)) {
      LOG_WARN("Fail to locate rowkey", K(ret), K(idx_block_data), K(rowkey), KPC(iter_));
    } else {
      ret = OB_SUCCESS; // return OB_ITER_END on get_next() for get
    }
  }
  if (OB_SUCC(ret)) {
    macro_id_ = macro_id;
    range_idx_ = range_idx;
    rowkey_ = &rowkey;
    is_get_ = true;
    if (nullptr != idx_info) {
      parent_row_range_ = idx_info->get_row_range();
    } else {
      parent_row_range_.reset();
    }
  }
  return ret;
}

int ObIndexBlockRowScanner::open(
    const MacroBlockId &macro_id,
    const ObMicroBlockData &idx_block_data,
    const ObRowsInfo *rows_info,
    const int64_t rowkey_begin_idx,
    const int64_t rowkey_end_idx)
{
  int ret = OB_SUCCESS;
  if (IS_NOT_INIT) {
    ret = OB_NOT_INIT;
    LOG_WARN("Not inited", K(ret));
  } else if (OB_UNLIKELY(!macro_id.is_valid() || !idx_block_data.is_valid() || nullptr == rows_info)) {
    ret = OB_INVALID_ARGUMENT;
    LOG_WARN("Invalid argument to open an index micro block", K(ret), K(macro_id), K(idx_block_data),
              KP(rows_info));
  } else if (OB_FAIL(init_by_micro_data(idx_block_data))) {
  } else if (OB_ISNULL(iter_)) {
    ret = OB_ERR_UNEXPECTED;
    LOG_WARN("iter is null", K(index_format_), K(ret), KPC(iter_));
  } else if (OB_FAIL(iter_->locate_range())) {
  } else {
    macro_id_ = macro_id;
    rows_info_ = rows_info;
    curr_rowkey_begin_idx_ = rowkey_begin_idx;
    rowkey_end_idx_ = rowkey_end_idx;
    is_get_ = false;
  }
  return ret;
}

int ObIndexBlockRowScanner::open(
    const MacroBlockId &macro_id,
    const ObMicroBlockData &idx_block_data,
    const ObRowKeysInfo *row_keys_info,
    const int64_t rowkey_begin_idx,
    const int64_t rowkey_end_idx)
{
  int ret = OB_SUCCESS;
  if (IS_NOT_INIT) {
    ret = OB_NOT_INIT;
    LOG_WARN("Not inited", K(ret));
  } else if (OB_UNLIKELY(nullptr == row_keys_info || rowkey_begin_idx >= rowkey_end_idx)) {
    ret = OB_INVALID_ARGUMENT;
    LOG_WARN("Invalid argument to open an index micro block", K(ret), KP(row_keys_info), K(rowkey_begin_idx), K(rowkey_end_idx));
  } else {
    const ObDatumRowkey &first_rowkey = row_keys_info->get_rowkey(rowkey_begin_idx);
    if (OB_FAIL(open(macro_id, idx_block_data, first_rowkey, rowkey_begin_idx))) {
    } else {
      rowkeys_info_ = row_keys_info;
      curr_rowkey_begin_idx_ = rowkey_begin_idx;
      rowkey_end_idx_ = rowkey_end_idx;
    }
  }
  return ret;
}

int ObIndexBlockRowScanner::open(
    const MacroBlockId &macro_id,
    const ObMicroBlockData &idx_block_data,
    const ObDatumRange &range,
    const int64_t range_idx,
    const bool is_left_border,
    const bool is_right_border,
    const ObMicroIndexInfo *idx_info)
{
  int ret = OB_SUCCESS;
  if (IS_NOT_INIT) {
    ret = OB_NOT_INIT;
    LOG_WARN("Not inited", K(ret));
  } else if (OB_UNLIKELY(!macro_id.is_valid() || !idx_block_data.is_valid() || !range.is_valid()
      || !idx_block_data.is_index_block())) {
    ret = OB_INVALID_ARGUMENT;
    LOG_WARN("Invalid argument to open an index micro block", K(ret), K(idx_block_data), K(range), KP(idx_info));
  } else if (OB_FAIL(init_by_micro_data(idx_block_data))) {
  } else if (OB_ISNULL(iter_)) {
    ret = OB_ERR_UNEXPECTED;
    LOG_WARN("iter is null", K(index_format_), K(ret));
  } else if (OB_FAIL(locate_range(range, is_left_border, is_right_border))) {
    if (OB_UNLIKELY(OB_BEYOND_THE_RANGE != ret)) {
      LOG_WARN("Fail to locate range", K(ret), K(range), K(is_left_border), K(is_right_border));
    }
  } else {
    macro_id_ = macro_id;
    is_left_border_ = is_left_border;
    is_right_border_ = is_right_border;
    range_idx_ = range_idx;
    is_get_ = false;
    if (nullptr != idx_info) {
      parent_row_range_ = idx_info->get_row_range();
    } else {
      parent_row_range_.reset();
    }
  }
  return ret;
}

int ObIndexBlockRowScanner::open(const MacroBlockId &macro_id,
                                 const ObMicroBlockData &idx_block_data)
{
  int ret = OB_SUCCESS;
  ObDatumRange range;
  if (IS_NOT_INIT) {
    ret = OB_NOT_INIT;
    LOG_WARN("Not inited", K(ret));
  } else if (OB_UNLIKELY(!macro_id.is_valid() || !idx_block_data.is_valid()
      || !idx_block_data.is_index_block())) {
    ret = OB_INVALID_ARGUMENT;
    LOG_WARN("Invalid argument to open an index micro block", K(ret), K(idx_block_data), K(macro_id));
  } else if (OB_FAIL(init_by_micro_data(idx_block_data))) {
  } else if (OB_ISNULL(iter_)) {
    ret = OB_ERR_UNEXPECTED;
    LOG_WARN("iter is null", K(index_format_), K(ret));
  } else if (FALSE_IT(range.set_whole_range())) {
  } else if (OB_FAIL(locate_range(range, true /* is_left_border */, true /* is_right_border */))) {
    if (OB_UNLIKELY(OB_BEYOND_THE_RANGE != ret)) {
      LOG_WARN("Fail to locate range", K(ret));
    }
  } else {
    macro_id_ = macro_id;
    parent_row_range_.reset();
    parent_row_range_.start_row_id_ = 0;
    is_left_border_ = true;
    is_right_border_ = true;
    range_idx_ = 0;
    is_get_ = false;
  }
  return ret;
}

int ObIndexBlockRowScanner::get_next(
    ObMicroIndexInfo &idx_block_row,
    const bool is_multi_check,
    const bool is_sorted_multi_get,
    storage::ObAdvanceScanHelper *advance_scan_helper)
{
  int ret = OB_SUCCESS;
  idx_block_row.reset();
  if (IS_NOT_INIT) {
    ret = OB_NOT_INIT;
    LOG_WARN("Not inited", K(ret));
  } else {
    const bool has_advance_scan_helper = nullptr != advance_scan_helper;
    do {
      if (end_of_block()) {
        ret = OB_ITER_END;
      } else if (is_multi_check && OB_FAIL(skip_to_next_valid_position(idx_block_row))) {
        if (OB_UNLIKELY(OB_ITER_END != ret)) {
          LOG_WARN("Failed to skip to next valid position", K(ret), K(curr_rowkey_begin_idx_), K(rowkey_end_idx_), KPC(rows_info_));
        } else if (OB_ISNULL(iter_)) {
          ret = OB_ERR_UNEXPECTED;
          LOG_WARN("iter is null", K(index_format_), K(ret));
        } else {
          iter_->set_iter_end();
        }
      } else if (OB_FAIL(get_next_idx_row(idx_block_row))) {
      } else if (is_sorted_multi_get) {
        idx_block_row.rowkeys_info_ = rowkeys_info_;
        idx_block_row.rowkey_begin_idx_ = curr_rowkey_begin_idx_;
        idx_block_row.rowkey_end_idx_ = curr_rowkey_begin_idx_ + 1;
        if (OB_ISNULL(iter_)) {
          ret = OB_ERR_UNEXPECTED;
          LOG_WARN("iter is null", K(index_format_), K(ret));
        } else if (OB_FAIL(iter_->find_rowkeys_belong_to_curr_idx_row(idx_block_row, rowkey_end_idx_, rowkeys_info_))) {
        }
      }
      if (OB_SUCC(ret) && has_advance_scan_helper) {
        ObAdvanceScanState &advance_scan_state = iter_->get_advance_scan_state();
        if (OB_FAIL(advance_scan_helper->filter_index_node(
            idx_block_row, advance_scan_state, idx_block_row.advance_scan_state_))) {
        }
      }
    } while (OB_SUCC(ret) && has_advance_scan_helper && idx_block_row.advance_scan_state_.is_before_range());
  }
  return ret;
}

void ObIndexBlockRowScanner::set_iter_param(const blocksstable::ObSSTable *sstable,
                                            const ObTablet *tablet)
{
  iter_param_.sstable_ = sstable;
  iter_param_.tablet_ = tablet;
}

bool ObIndexBlockRowScanner::end_of_block() const
{
  int ret = OB_SUCCESS;
  bool bret = true;
  if (IS_NOT_INIT) {
    ret = OB_NOT_INIT;
    LOG_WARN("Not inited", K(ret));
  } else if (OB_ISNULL(iter_)) {
    ret = OB_ERR_UNEXPECTED;
    LOG_WARN("iter is null", K(index_format_), K(ret));
  } else {
    bret = iter_->end_of_block();
  }
  return bret;
}

int ObIndexBlockRowScanner::get_index_row_count(int64_t &index_row_count) const
{
  int ret = OB_SUCCESS;
  index_row_count = 0;
  int64_t data_row_count = 0;
  if (IS_NOT_INIT) {
    ret = OB_NOT_INIT;
    LOG_WARN("Not inited", K(ret));
  } else if (OB_ISNULL(iter_) || OB_ISNULL(range_)) {
    ret = OB_ERR_UNEXPECTED;
    LOG_WARN("iter is null", K(index_format_), K(ret), KP(iter_), KP(range_));
  } else if (OB_FAIL(iter_->get_index_row_count(*range_, is_left_border_, is_right_border_, index_row_count, data_row_count))) {
  }
 return ret;
}

int ObIndexBlockRowScanner::check_blockscan(
    const ObDatumRowkey &rowkey,
    bool &can_blockscan)
{
  int ret = OB_SUCCESS;
  if (IS_NOT_INIT) {
    ret = OB_NOT_INIT;
    LOG_WARN("Not init", K(ret));
  } else if (is_reverse_scan_) {
    if (rowkey.is_min_rowkey()) {
      can_blockscan = true;
    } else {
      // TODO(yuanzhe) opt this
      can_blockscan = false;
    }
  } else if (rowkey.is_max_rowkey()) {
    can_blockscan = true;
  } else {
    if (OB_ISNULL(iter_)) {
      ret = OB_ERR_UNEXPECTED;
      LOG_WARN("iter is null", K(index_format_), K(ret));
    } else if (OB_FAIL(iter_->check_blockscan(rowkey, can_blockscan))) {
    }
  }
  return ret;
}

int ObIndexBlockRowScanner::init_by_micro_data(const ObMicroBlockData &idx_block_data)
{
  int ret = OB_SUCCESS;
  void *iter_buf = nullptr;
  if (ObMicroBlockData::INDEX_BLOCK == idx_block_data.type_ || ObMicroBlockData::DDL_MERGE_INDEX_BLOCK == idx_block_data.type_) {
    if (nullptr == idx_block_data.get_extra_buf()) {
        if (OB_NOT_NULL(raw_iter_)) {
          iter_ = raw_iter_;
          index_format_ = ObIndexFormat::RAW_DATA;
        } else {
          if (OB_ISNULL(iter_buf = allocator_->alloc(sizeof(ObRAWIndexBlockRowIterator)))) {
            ret = OB_ALLOCATE_MEMORY_FAILED;
            LOG_WARN("allocate memory failed", K(ret), K(sizeof(ObRAWIndexBlockRowIterator)));
          } else if (FALSE_IT(raw_iter_ = new (iter_buf) ObRAWIndexBlockRowIterator)) {
          } else {
            iter_ = raw_iter_;
            index_format_ = ObIndexFormat::RAW_DATA;
          }
        }
      } else {
        if (OB_NOT_NULL(transformed_iter_)) {
          iter_ = transformed_iter_;
          index_format_ = ObIndexFormat::TRANSFORMED;
        } else {
          if (OB_ISNULL(iter_buf = allocator_->alloc(sizeof(ObTFMIndexBlockRowIterator)))) {
            ret = OB_ALLOCATE_MEMORY_FAILED;
            LOG_WARN("allocate memory failed", K(ret), K(sizeof(ObTFMIndexBlockRowIterator)));
          } else if (FALSE_IT(transformed_iter_ = new (iter_buf) ObTFMIndexBlockRowIterator)) {
          } else {
            iter_ = transformed_iter_;
            index_format_ = ObIndexFormat::TRANSFORMED;
          }
        }
    }
  } else if (ObMicroBlockData::DDL_BLOCK_TREE == idx_block_data.type_) {
    if (OB_NOT_NULL(ddl_iter_)) {
      iter_ = ddl_iter_;
      index_format_ = ObIndexFormat::BLOCK_TREE;
    } else {
      if (OB_ISNULL(iter_buf = allocator_->alloc(sizeof(ObDDLIndexBlockRowIterator)))) {
        ret = OB_ALLOCATE_MEMORY_FAILED;
        LOG_WARN("allocate memory failed", K(ret), K(sizeof(ObDDLIndexBlockRowIterator)));
      } else if (FALSE_IT(ddl_iter_ = new (iter_buf) ObDDLIndexBlockRowIterator)) {
      } else {
        iter_ = ddl_iter_;
        index_format_ = ObIndexFormat::BLOCK_TREE;
      }
    }
  }
  if (OB_SUCC(ret)) {
    if (OB_ISNULL(iter_)) {
      ret = OB_ERR_UNEXPECTED;
      LOG_WARN("iter is null", K(index_format_), K(ret));
    } else if (OB_FAIL(iter_->init(idx_block_data, datum_utils_, allocator_, is_reverse_scan_, iter_param_))) {
    }
  }
  return ret;
}

int ObIndexBlockRowScanner::locate_range(
    const ObDatumRange &range,
    const bool is_left_border,
    const bool is_right_border)
{
  int ret = OB_SUCCESS;
  if (OB_ISNULL(iter_)) {
    ret = OB_ERR_UNEXPECTED;
    LOG_WARN("iter is null", K(index_format_), K(ret), KPC(iter_));
  } else if (OB_FAIL(iter_->locate_range(range, is_left_border, is_right_border))) {
    if (OB_UNLIKELY(OB_BEYOND_THE_RANGE != ret)) {
      LOG_WARN("Fail to locate range", K(ret), K(range), K(is_left_border), K(is_right_border), KPC(iter_));
    }
  } else {
    range_ = &range;
  }
  return ret;
}

int ObIndexBlockRowScanner::advance_to_border(
    const ObDatumRowkey &rowkey,
    const int32_t range_idx,
    ObMicroBlockRowIdRange &row_id_range)
{
  int ret = OB_SUCCESS;
  if (OB_UNLIKELY(index_format_ != ObIndexFormat::TRANSFORMED) || OB_ISNULL(iter_)) {
    ret = OB_ERR_UNEXPECTED;
    LOG_WARN("Unexpected error", K(ret), K(index_format_), KP(iter_));
  } else if (OB_UNLIKELY(end_of_block())) {
    ret = OB_ERR_UNEXPECTED;
    LOG_WARN("Unexpected error", K(ret), K(end_of_block()));
  } else if (range_idx == range_idx_) {
    if(OB_FAIL(iter_->advance_to_border(rowkey, is_left_border_, is_right_border_, parent_row_range_, row_id_range))) {
    }
  }
  return ret;
}

int ObIndexBlockRowScanner::get_end_key(ObCommonDatumRowkey &endkey) const
{
  int ret = OB_SUCCESS;
  endkey.reset();
  if (IS_NOT_INIT) {
    ret = OB_NOT_INIT;
    LOG_WARN("Not inited", K_(is_inited));
  } else if (OB_ISNULL(iter_) || OB_UNLIKELY(index_format_ != ObIndexFormat::TRANSFORMED)) {
    ret = OB_ERR_UNEXPECTED;
    LOG_WARN("iter is null or wrong format", KP(iter_), K(index_format_), K(ret));
  } else if (OB_FAIL(iter_->get_end_key(endkey))) {
  }
  return ret;
}

void ObIndexBlockRowScanner::switch_context(const ObSSTable &sstable,
                                            const ObTablet *tablet,
                                            const ObStorageDatumUtils &datum_utils,
                                            const ObQueryFlag &query_flag,
                                            const ObITableReadInfo *table_read_info)
{
  nested_offset_ = sstable.get_macro_offset();
  datum_utils_ = &datum_utils;
  is_reverse_scan_ = query_flag.is_reverse_scan();
  table_read_info_ = table_read_info;
  iter_param_.sstable_ = &sstable;
  iter_param_.tablet_ = tablet;
  int ret = OB_SUCCESS;
  if (OB_NOT_NULL(iter_)) {
    ObStorageDatumUtils *switch_datum_utils = const_cast<ObStorageDatumUtils *>(datum_utils_);
    iter_->switch_context(switch_datum_utils);
  }
}

int ObIndexBlockRowScanner::get_next_idx_row(ObMicroIndexInfo &idx_block_row)
{
  int ret = OB_SUCCESS;
  const ObIndexBlockRowHeader *idx_row_header = nullptr;
  const ObIndexBlockRowMinorMetaInfo *idx_minor_info = nullptr;
  const char *idx_data_buf = nullptr;
  const char *agg_row_buf = nullptr;
  int64_t agg_buf_size = 0;
  int64_t row_offset = 0;
  bool is_scan_left_border = false;
  bool is_scan_right_border = false;
  if (OB_ISNULL(iter_)) {
    ret = OB_ERR_UNEXPECTED;
    LOG_WARN("iter is null", K(ret), K(index_format_), KP(iter_));
  } else {
    if (OB_FAIL(iter_->get_next(idx_row_header, idx_block_row.endkey_, is_scan_left_border, is_scan_right_border, idx_minor_info, agg_row_buf, agg_buf_size, row_offset))) {
    } else if (OB_UNLIKELY(nullptr == idx_row_header || !idx_block_row.endkey_.is_valid())) {
      ret = OB_ERR_UNEXPECTED;
      LOG_WARN("Unexpected null index block row header/endkey", K(ret), KPC(iter_),
              K(index_format_), KP(idx_row_header), K(idx_block_row.endkey_));
    }
  }

  if (OB_SUCC(ret)) {
    idx_block_row.ps_node_ = iter_->get_cur_ps_node();
    idx_block_row.flag_ = 0;
    idx_block_row.row_header_ = idx_row_header;
    idx_block_row.minor_meta_info_ = idx_minor_info;
    idx_block_row.is_get_ = is_get_;
    idx_block_row.is_left_border_ = is_left_border_ && is_scan_left_border;
    idx_block_row.is_right_border_ = is_right_border_ && is_scan_right_border;
    idx_block_row.copy_lob_out_row_flag();
    idx_block_row.range_idx_ = range_idx_;
    idx_block_row.query_range_ = query_range_;
    idx_block_row.parent_macro_id_ = macro_id_;
    idx_block_row.nested_offset_ = nested_offset_;
    idx_block_row.agg_row_buf_ = agg_row_buf;
    idx_block_row.agg_buf_size_ = agg_buf_size;
    idx_block_row.table_read_info_ = table_read_info_;
    idx_block_row.row_id_range_.start_row_id_ = row_offset - idx_block_row.get_row_count() + 1;
    idx_block_row.row_id_range_.end_row_id_ = row_offset;
    if (OB_SUCC(ret) && idx_block_row.is_data_block()) {
      idx_block_row.row_id_range_.start_row_id_ += parent_row_range_.start_row_id_;
      idx_block_row.row_id_range_.end_row_id_ += parent_row_range_.start_row_id_;
    }
  }
  return ret;
}

int ObIndexBlockRowScanner::skip_to_next_valid_position(ObMicroIndexInfo &idx_block_row)
{
  int ret = OB_SUCCESS;
  for (; curr_rowkey_begin_idx_ < rowkey_end_idx_; ++curr_rowkey_begin_idx_) {
    if (!rows_info_->is_row_skipped(curr_rowkey_begin_idx_)) {
      break;
    }
  }
  if (IS_NOT_INIT) {
    ret = OB_NOT_INIT;
    LOG_WARN("Not inited", K(ret));
  } else if (curr_rowkey_begin_idx_ == rowkey_end_idx_) {
    ret = OB_ITER_END;
  } else if (OB_ISNULL(iter_)) {
    ret = OB_ERR_UNEXPECTED;
    LOG_WARN("iter is null", K(index_format_), K(ret));
  } else if (OB_FAIL(iter_->skip_to_next_valid_position(rows_info_->get_rowkey(curr_rowkey_begin_idx_)))) {
    if (OB_ITER_END != ret) {
      LOG_WARN("fail to skip to next valid position", K(ret), K(curr_rowkey_begin_idx_), K(rowkey_end_idx_), KPC(rows_info_), KPC(iter_));
    }
  } else {
    idx_block_row.rows_info_ = rows_info_;
    idx_block_row.rowkey_begin_idx_ = curr_rowkey_begin_idx_;
    // If a rowkey happens to be the endkey of the microblock, the rowkey idx must also be included in the rowkey idx range of next index row,
    // because there may be multiple versions of one row across the microblock. Otherwise, some multi-version rows may be missed when do check_rows_lock.
    // Preserve the boundary row because one multi-version row can span adjacent
    // microblocks.
    if (OB_FAIL(iter_->find_rowkeys_belong_to_same_idx_row(idx_block_row, curr_rowkey_begin_idx_, rowkey_end_idx_, rows_info_))) {
    }
  }
  return ret;
}

} // namespace blocksstable
} // namespace oceanbase
