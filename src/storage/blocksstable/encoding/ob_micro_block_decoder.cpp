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
#include "ob_micro_block_decoder.h"

#include "lib/charset/ob_ctype.h"
#include "lib/charset/str_uca_type.h"
#include "rust/micro_block.h"
#include "storage/access/ob_pushdown_aggregate.h"
#include "storage/access/ob_table_access_context.h"
#include "storage/access/ob_table_access_param.h"
#include "storage/access/ob_table_access_context.h"
#include "storage/truncate_info/ob_truncate_filter_evaluator.h"

namespace oceanbase
{
using namespace lib;
using namespace common;
using namespace storage;
namespace blocksstable
{
enum class RustNopMode
{
  AS_NULL,
  AS_EXTENDED,
};

static int materialize_rust_cell(
    const micro_block::CellDesc &cell,
    const rust::Vec<uint8_t> &decoded_data,
    const char *block_data,
    const int64_t block_size,
    const ObObjType obj_type,
    const int64_t borrowed_integer_mask,
    const int64_t decoded_integer_mask,
    const RustNopMode nop_mode,
    common::ObIAllocator &allocator,
    ObDatum &datum)
{
  int ret = OB_SUCCESS;
  switch (cell.kind) {
    case micro_block::CellKind::Null:
      datum.set_null();
      break;
    case micro_block::CellKind::Nop:
      if (RustNopMode::AS_EXTENDED == nop_mode) {
        datum.set_ext();
        const_cast<ObObj *>(datum.extend_obj_)->set_ext(ObActionFlag::OP_NOP);
      } else {
        datum.set_null();
      }
      break;
    case micro_block::CellKind::UInt: {
      uint32_t datum_len = 0;
      if (OB_FAIL(get_uint_data_datum_len(
              common::ObDatum::get_obj_datum_map_type(obj_type), datum_len))) {
      } else {
        datum.pack_ = datum_len;
        MEMCPY(const_cast<char *>(datum.ptr_), &cell.value, datum_len);
      }
      break;
    }
    case micro_block::CellKind::Decoded: {
      if (cell.offset > decoded_data.size()
          || cell.length > decoded_data.size() - cell.offset) {
        ret = OB_INVALID_DATA;
      } else {
        char *decoded = static_cast<char *>(allocator.alloc(std::max<uint64_t>(cell.length, 1)));
        if (OB_ISNULL(decoded)) {
          ret = OB_ALLOCATE_MEMORY_FAILED;
        } else {
          if (cell.length > 0) {
            MEMCPY(decoded, decoded_data.data() + cell.offset, cell.length);
          }
          ret = load_data_to_datum(
              obj_type, decoded, cell.length, decoded_integer_mask, datum);
        }
      }
      break;
    }
    case micro_block::CellKind::Borrowed:
      if (cell.offset > static_cast<uint64_t>(block_size)
          || cell.length > static_cast<uint64_t>(block_size) - cell.offset) {
        ret = OB_INVALID_DATA;
      } else {
        ret = load_data_to_datum(
            obj_type, block_data + cell.offset, cell.length, borrowed_integer_mask, datum);
      }
      break;
    default:
      ret = OB_INVALID_DATA;
  }
  return ret;
}

//////////////////////////ObIEncodeBlockGetReader/////////////////////////
ObIEncodeBlockReader::ObIEncodeBlockReader()
  : request_cnt_(0),
    header_(NULL), col_header_(NULL),
    block_data_(NULL), block_size_(0),
    decoder_allocator_(ObMemAttr(common::ObModIds::OB_DECODER_CTX), OB_MALLOC_NORMAL_BLOCK_SIZE),
    store_id_array_(NULL), default_store_ids_()
{}

ObIEncodeBlockReader::~ObIEncodeBlockReader()
{
}

void ObIEncodeBlockReader::reuse()
{
  header_ = nullptr;
  request_cnt_ = 0;
  col_header_ = nullptr;
  block_data_ = nullptr;
  block_size_ = 0;
  store_id_array_ = nullptr;
  decoder_allocator_.reuse();
}

void ObIEncodeBlockReader::reset()
{
  header_ = NULL;
  request_cnt_ = 0;
  col_header_ = NULL;
  block_data_ = NULL;
  block_size_ = 0;
  store_id_array_ = NULL;
  decoder_allocator_.reset();
}

int ObIEncodeBlockReader::prepare(const int64_t column_cnt)
{
  int ret = OB_SUCCESS;
  if (column_cnt > DEFAULT_DECODER_CNT) {
    const int64_t store_ids_size = sizeof(store_id_array_[0]) * column_cnt;
    char *buf = nullptr;
    if (nullptr
        == (buf = reinterpret_cast<char*>(decoder_allocator_.alloc(
          store_ids_size)))) {
      ret = OB_ALLOCATE_MEMORY_FAILED;
    } else {
      store_id_array_ = reinterpret_cast<int64_t *>(buf);
    }
  } else {
    store_id_array_ = &default_store_ids_[0];
  }
  return ret;
}

int ObIEncodeBlockReader::do_init(
    const ObMicroBlockData &block_data,
    const int64_t request_cnt)
{
  int ret = OB_SUCCESS;
  if (OB_UNLIKELY(!block_data.is_valid()) || request_cnt <= 0) {
    ret = OB_INVALID_ARGUMENT;
  } else {
    request_cnt_ = request_cnt;
    block_data_ = block_data.get_buf();
    block_size_ = block_data.get_buf_size();
    if (OB_UNLIKELY(!header_->is_valid())) {
      ret = OB_INNER_STAT_ERROR;
      LOG_ERROR("invalid micro_block_header", K(ret), "header", *header_);
    }
  }
  if (OB_FAIL(ret)) {
    reset();
  }
  return ret;
}

int ObIEncodeBlockReader::get_micro_metas(const ObMicroBlockHeader *&header,
    const ObColumnHeader *&col_header,
    const char *block, const int64_t block_size)
{
  int ret = OB_SUCCESS;
  if (NULL == block || block_size < static_cast<int64_t>(sizeof(ObMicroBlockHeader))) {

    ret = OB_INVALID_ARGUMENT;
  } else {
    header = reinterpret_cast<const ObMicroBlockHeader *>(block);
    if (header->header_size_ < sizeof(ObMicroBlockHeader)
        || header->header_size_ > block_size
        || header->column_count_ > (block_size - header->header_size_) / sizeof(*col_header)
        || header->row_data_offset_ > block_size) {
      ret = OB_INVALID_ARGUMENT;
    } else {
      col_header = reinterpret_cast<const ObColumnHeader *>(block + header->header_size_);
    }
  }
  return ret;
}

//////////////////////////////// ObEncodeBlockReaderV2 /////////////////////////////////
void ObEncodeBlockGetReader::reuse()
{
  ObIEncodeBlockReader::reuse();
  read_info_ = nullptr;
}

int ObEncodeBlockGetReader::init_by_read_info(
    const ObMicroBlockData &block_data,
    const ObITableReadInfo &read_info)
{
  int ret = OB_SUCCESS;
  if (OB_UNLIKELY(!block_data.is_valid() ||
                  !read_info.is_valid())) {
    ret = OB_INVALID_ARGUMENT;
  } else {
    const int64_t request_cnt = read_info.get_request_count();
    if (OB_FAIL(prepare(request_cnt))) {
    } else if (OB_FAIL(get_micro_metas(header_, col_header_,
        block_data.get_buf(), block_data.get_buf_size()))) {
    } else if (typeid(ObRowkeyReadInfo) == typeid(read_info)) {
      int64_t i = 0;
      for (; i < header_->column_count_; ++i) {
        store_id_array_[i] = i;
      }
      for (; i < request_cnt; ++i) {
        store_id_array_[i] = i;
      }
    } else {
      const ObColumnIndexArray &cols_index = read_info.get_columns_index();
      for (int64_t idx = 0; idx < request_cnt; idx++) {
        store_id_array_[idx] = cols_index.at(idx);
      }
    }

    if (OB_FAIL(ret)) {
    } else if (OB_FAIL(do_init(block_data, request_cnt))) {
    } else {
      row_count_ = header_->row_count_;
      original_data_length_ = header_->original_length_;
    }
  }
  return ret;
}

int ObEncodeBlockGetReader::get_row(
    const ObMicroBlockData &block_data,
    const ObDatumRowkey &rowkey,
    const ObITableReadInfo &read_info,
    ObDatumRow &row)
{
  int ret = OB_SUCCESS;
  bool found = false;
  int64_t row_id = -1;

  reuse();
  if (OB_FAIL(init_by_read_info(block_data, read_info))) {
  } else if (OB_FAIL(locate_row(rowkey, read_info, row_id, found))) {
  } else {
    row.row_flag_.reset();
    row.mvcc_row_flag_.reset();
    if (found) {
      if (OB_FAIL(get_all_columns(row_id, row))) {
      } else {
        row.count_ = request_cnt_;
        row.row_flag_.set_flag(ObDmlFlag::DF_INSERT);
      }
    } else {
      // not found
      row.row_flag_.set_flag(ObDmlFlag::DF_NOT_EXIST);
      ret = OB_BEYOND_THE_RANGE;
    }
  }
  return ret;
}

int ObEncodeBlockGetReader::get_all_columns(
    const int64_t row_id,
    ObDatumRow &row)
{
  if (OB_UNLIKELY(row.get_capacity() < request_cnt_)) {
    return OB_BUF_NOT_ENOUGH;
  }
  return get_columns_rust(row_id, request_cnt_, row.storage_datums_);
}

int ObEncodeBlockGetReader::get_columns_rust(
    const int64_t row_id,
    const int64_t column_count,
    ObStorageDatum *datums)
{
  if (row_id < 0 || row_id >= header_->row_count_ || row_id > UINT32_MAX
      || column_count <= 0 || column_count > request_cnt_ || nullptr == datums
      || nullptr == block_data_ || block_size_ <= 0) {
    return OB_INVALID_ARGUMENT;
  }
  std::vector<uint32_t> columns;
  std::vector<uint32_t> result_columns(column_count, UINT32_MAX);
  columns.reserve(column_count);
  for (int64_t i = 0; i < column_count; ++i) {
    const int64_t store_idx = store_id_array_[i];
    if (store_idx < 0 || store_idx >= header_->column_count_) {
      datums[i].set_nop();
      continue;
    }
    result_columns[i] = columns.size();
    columns.push_back(static_cast<uint32_t>(store_idx));
  }
  if (columns.empty()) {
    return OB_SUCCESS;
  }
  const uint32_t rust_row = static_cast<uint32_t>(row_id);
  const rust::Slice<const uint8_t> block(
      reinterpret_cast<const uint8_t *>(block_data_), static_cast<std::size_t>(block_size_));
  const rust::Slice<const uint32_t> rows(&rust_row, 1);
  const rust::Slice<const uint32_t> rust_columns(columns.data(), columns.size());
  micro_block::BatchResult result = micro_block::project_pax_block(block, rows, rust_columns);
  int ret = OB_SUCCESS;
  switch (result.code) {
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
  if (result.cells.size() != columns.size()) {
    return OB_INVALID_DATA;
  }
  for (int64_t i = 0; OB_SUCC(ret) && i < column_count; ++i) {
    if (UINT32_MAX == result_columns[i]) {
      continue;
    }
    const uint32_t result_column = result_columns[i];
    const micro_block::CellDesc &cell = result.cells[result_column];
    ObStorageDatum &datum = datums[i];
    const ObObjType obj_type = col_header_[columns[result_column]].get_store_obj_type();
    int64_t integer_mask = 0;
    if (ObIntTC == ob_obj_type_class(obj_type)) {
      integer_mask = ~INTEGER_MASK_TABLE[get_type_size_map()[obj_type]];
    }
    datum.reuse();
    if (micro_block::CellKind::Nop == cell.kind) {
      datum.set_nop();
    } else {
      ret = materialize_rust_cell(
          cell, result.data, block_data_, block_size_, obj_type, integer_mask, 0,
          RustNopMode::AS_NULL, decoder_allocator_, datum);
    }
  }
  return ret;
}

int ObEncodeBlockGetReader::locate_row(
    const ObDatumRowkey &rowkey,
    const ObITableReadInfo &read_info,
    int64_t &row_id,
    bool &found)
{
  int ret = OB_SUCCESS;
  const ObStorageDatumUtils &datum_utils = read_info.get_datum_utils();

  if (OB_UNLIKELY(rowkey.get_datum_cnt() > datum_utils.get_rowkey_count())) {
    ret = OB_INVALID_ARGUMENT;
    STORAGE_LOG(WARN, "Invalid argument to locate row", K(ret), K(rowkey), K(datum_utils));
  } else {
    found = false;
    row_id = -1;
    const int64_t rowkey_cnt = rowkey.get_datum_cnt();
    const ObStorageDatum *datums = rowkey.datums_;
    ObSEArray<ObStorageDatum, 16> store_datums;
    if (OB_FAIL(store_datums.prepare_allocate(rowkey_cnt))) {
      return ret;
    }
    bool located_by_rust = false;
    bool equal = false;
    if (OB_FAIL(try_locate_rowkey_bound(
            block_data_, block_size_, header_, col_header_, rowkey, &read_info,
            true, true, 0, header_->row_count_, located_by_rust, row_id, equal))) {
    } else if (located_by_rust && equal) {
      found = true;
    }
    //binary search
    int32_t high = header_->row_count_ - 1;
    int32_t low = 0;
    int32_t middle = 0;
    int32_t cmp_result = 0;

    while (OB_SUCC(ret) && !located_by_rust && low <= high) {
      middle = (low + high) >> 1;
      cmp_result = 0;
      if (OB_FAIL(get_columns_rust(middle, rowkey_cnt, &store_datums.at(0)))) {
      } else {
        for (int64_t i = 0; OB_SUCC(ret) && 0 == cmp_result && i < rowkey_cnt; ++i) {
          if (OB_FAIL(datum_utils.get_cmp_funcs().at(i).compare(
                  datums[i], store_datums.at(i), cmp_result))) {
          }
        }

        if (OB_SUCC(ret)) {
          if (cmp_result < 0) {
            high = middle - 1;
          } else if (cmp_result > 0) {
            low = middle + 1;
          } else {
            found = true;
            row_id = middle;
            break;
          }
        }
      }
    }
  }

  return ret;
}

int ObEncodeBlockGetReader::init_by_columns_desc(
    const ObMicroBlockData &block_data,
    const int64_t schema_rowkey_cnt,
    const ObColDescIArray &cols_desc,
    const int64_t request_cnt)
{
  int ret = OB_SUCCESS;
  UNUSED(cols_desc);
  if (OB_FAIL(prepare(MAX(schema_rowkey_cnt, request_cnt)))) {
  } else {
    int64_t idx = 0;
    for (idx = 0; idx < schema_rowkey_cnt; idx++) {
      store_id_array_[idx] = idx;
    }
    for (; idx < request_cnt; ++idx) {
      store_id_array_[idx] = idx;
    }
    if (OB_FAIL(get_micro_metas(header_, col_header_,
        block_data.get_buf(), block_data.get_buf_size()))) {
    } else if (OB_FAIL(do_init(block_data, request_cnt))) {
    }
  }
  return ret;
}

int ObEncodeBlockGetReader::exist_row(
    const ObMicroBlockData &block_data,
    const ObDatumRowkey &rowkey,
    const ObITableReadInfo &read_info,
    bool &exist,
    bool &found)
{
  int ret = OB_SUCCESS;
  found = false;
  exist = false;
  int64_t row_id = -1;
  const int64_t rowkey_cnt = rowkey.get_datum_cnt();

  reuse();
  if (OB_UNLIKELY(!read_info.is_valid())) {
    ret = OB_INVALID_ARGUMENT;
  } else if (OB_FAIL(init_by_columns_desc(
              block_data,
              read_info.get_schema_rowkey_count(),
              read_info.get_columns_desc(),
              rowkey_cnt))) {
  } else if (OB_FAIL(locate_row(rowkey, read_info, row_id, found))) {
  } else if (found) {
    exist = true;
  }
  return ret;
}

int ObEncodeBlockGetReader::get_row(
    const ObMicroBlockData &block_data,
    const ObITableReadInfo &read_info,
    const uint32_t row_idx,
    ObDatumRow &row)
{
  int ret = OB_SUCCESS;

  reuse();
  if (OB_FAIL(init_by_read_info(block_data, read_info))) {
  } else {
    row.row_flag_.reset();
    row.mvcc_row_flag_.reset();
    if (OB_FAIL(get_all_columns(row_idx, row))) {
    } else {
      row.count_ = request_cnt_;
      row.row_flag_.set_flag(ObDmlFlag::DF_INSERT);
    }
  }
  return ret;
}

int ObEncodeBlockGetReader::get_row_id(
    const ObMicroBlockData &block_data,
    const ObDatumRowkey &rowkey,
    const ObITableReadInfo &read_info,
    int64_t &row_id)
{
  int ret = OB_SUCCESS;
  bool found = false;

  reuse();
  if (OB_UNLIKELY(!read_info.is_valid())) {
    ret = OB_INVALID_ARGUMENT;
  } else if (OB_FAIL(init_by_read_info(block_data, read_info))) {
  } else if (OB_FAIL(locate_row(rowkey, read_info, row_id, found))) {
  } else if (!found) {
    ret = OB_BEYOND_THE_RANGE;
  }
  return ret;
}

////////////////////////// ObMicroBlockDecoderV2 ////////////////////////////////

ObMicroBlockDecoder::ObMicroBlockDecoder()
  : header_(nullptr),
    request_cnt_(0),
    col_header_(nullptr),
    block_data_(nullptr),
    block_size_(0),
    columns_(nullptr),
    flat_row_reader_(),
    decoder_allocator_(ObModIds::OB_DECODER_CTX),
    buf_allocator_(ObMemAttr("OB_MICB_DECODER"), OB_MALLOC_NORMAL_BLOCK_SIZE),
    column_buf_(nullptr),
    column_buf_size_(0)
{
  reader_type_ = Decoder;
}

ObMicroBlockDecoder::~ObMicroBlockDecoder()
{
  reset();
}

void ObMicroBlockDecoder::inner_reset()
{
  header_ = NULL;
  col_header_ = NULL;
  block_data_ = NULL;
  block_size_ = 0;
  ObIMicroBlockReader::reset();
  decoder_allocator_.reuse();
  flat_row_reader_.reset();
  columns_ = nullptr;
}

void ObMicroBlockDecoder::reset()
{
  inner_reset();
  request_cnt_ = 0;
  buf_allocator_.reset();
  decoder_allocator_.reset();
  column_buf_ = nullptr;
  column_buf_size_ = 0;
}

int ObMicroBlockDecoder::init_columns()
{
  int ret = OB_SUCCESS;
  if (OB_UNLIKELY(nullptr == header_ || nullptr == col_header_
      || (nullptr != read_info_ && !read_info_->is_valid()))) {
    ret = OB_INNER_STAT_ERROR;
  } else {
    const int64_t size = request_cnt_ * sizeof(ColumnReadInfo);
    if (size > column_buf_size_) {
      buf_allocator_.reuse();
      column_buf_ = nullptr;
      column_buf_size_ = 0;
      if (OB_ISNULL(column_buf_ = static_cast<char *>(buf_allocator_.alloc(size)))) {
        ret = OB_ALLOCATE_MEMORY_FAILED;
      } else {
        column_buf_size_ = size;
      }
    }
    columns_ = reinterpret_cast<ColumnReadInfo *>(column_buf_);
  }
  if (OB_FAIL(ret)) {
  } else if (nullptr == read_info_ || typeid(ObRowkeyReadInfo) == typeid(*read_info_)) {
    if (OB_UNLIKELY((header_->column_count_ < request_cnt_ && nullptr == read_info_)
        || header_->column_count_ > request_cnt_)) {
      ret = OB_ERR_UNEXPECTED;
    }
    for (int64_t i = 0; OB_SUCC(ret) && i < request_cnt_; ++i) {
      columns_[i].col_header_ = i < header_->column_count_ ? &col_header_[i] : nullptr;
      columns_[i].col_param_ = nullptr;
    }
  } else {
    const ObColumnIndexArray &cols_index = read_info_->get_columns_index();
    const ObColumnParamIArray *cols_param = read_info_->get_columns();
    const bool has_params = nullptr != cols_param && cols_param->count() > 0;
    for (int64_t i = 0; i < request_cnt_; ++i) {
      const int64_t store_idx = cols_index.at(i);
      columns_[i].col_header_ = nullptr;
      columns_[i].col_param_ = nullptr;
      if (store_idx < 0) {
      } else if (store_idx >= header_->column_count_) {
        columns_[i].col_param_ = has_params ? cols_param->at(i) : nullptr;
      } else {
        columns_[i].col_header_ = &col_header_[store_idx];
      }
    }
  }
  return ret;
}

int ObMicroBlockDecoder::get_micro_metas(
    const ObMicroBlockHeader *&header,
    const ObColumnHeader *&col_header,
    const char *block,
    const int64_t block_size)
{
  int ret = OB_SUCCESS;
  if (nullptr == block || block_size < static_cast<int64_t>(sizeof(ObMicroBlockHeader))) {
    ret = OB_INVALID_ARGUMENT;
  } else {
    header = reinterpret_cast<const ObMicroBlockHeader *>(block);
    if (header->header_size_ < sizeof(ObMicroBlockHeader)
        || header->header_size_ > block_size
        || header->column_count_ > (block_size - header->header_size_) / sizeof(*col_header)
        || header->row_data_offset_ > block_size) {
      ret = OB_INVALID_ARGUMENT;
    } else {
      col_header = reinterpret_cast<const ObColumnHeader *>(block + header->header_size_);
    }
  }
  return ret;
}

int ObMicroBlockDecoder::init(
    const ObMicroBlockData &block_data,
    const ObITableReadInfo &read_info)
{
  int ret = OB_SUCCESS;
  // can be init twice
  if (OB_UNLIKELY(block_data.get_buf_size() <= 0 || !read_info.is_valid())) {
    ret = OB_INVALID_ARGUMENT;
  } else {
    if (is_inited_) {
      inner_reset();
    }
    read_info_ = &read_info;
    datum_utils_ = &(read_info_->get_datum_utils());
    request_cnt_ = read_info.get_request_count();
    if (OB_FAIL(do_init(block_data))) {
    }
  }
  return ret;
}

int ObMicroBlockDecoder::init(
    const ObMicroBlockData &block_data,
    const ObStorageDatumUtils *datum_utils)
{
  int ret = OB_SUCCESS;
  // can be init twice
  if (OB_UNLIKELY(block_data.get_buf_size() <= 0)) {
    ret = OB_INVALID_ARGUMENT;
  } else {
    if (is_inited_) {
      inner_reset();
    }
    read_info_ = nullptr;
    datum_utils_ = datum_utils;
    if (OB_FAIL(do_init(block_data))) {
    }
  }
  return ret;
}


int ObMicroBlockDecoder::do_init(const ObMicroBlockData &block_data)
{
  int ret = OB_SUCCESS;
  if (OB_FAIL(get_micro_metas(header_, col_header_,
      block_data.get_buf(), block_data.get_buf_size()))) {
  } else {
    if (NULL == read_info_) {
      request_cnt_ = header_->column_count_;
    } else {
      request_cnt_ = read_info_->get_request_count();
    }
    row_count_ = header_->row_count_;
    original_data_length_ = header_->original_length_;
    block_data_ = block_data.get_buf();
    block_size_ = block_data.get_buf_size();
  }

  if (OB_SUCC(ret)) {
    if (OB_UNLIKELY(!header_->is_valid())) {
      ret = OB_INNER_STAT_ERROR;
      LOG_ERROR("invalid micro_block_header", K(ret), "header", *header_);
    } else if (OB_FAIL(init_columns())) {
    } else {
      is_inited_ = true;
    }
  }
  if (OB_FAIL(ret)) {
    reset();
  }
  return ret;
}

int ObMicroBlockDecoder::get_row(const int64_t index, ObDatumRow &row)
{
  int ret = OB_SUCCESS;
  decoder_allocator_.reuse();
  if (OB_FAIL(get_row_impl(index, row))) {
  }
  return ret;
}

OB_INLINE int ObMicroBlockDecoder::get_row_impl(int64_t index, ObDatumRow &row)
{
  int ret = OB_SUCCESS;
  if (IS_NOT_INIT) {
    ret = OB_NOT_INIT;
  } else if (OB_UNLIKELY(index < 0 || index >= row_count_ || !row.is_valid())) {
    ret = OB_INVALID_ARGUMENT;
  } else if (row.get_capacity() < request_cnt_) {
    ret = OB_BUF_NOT_ENOUGH;
  } else {
    ObSEArray<int32_t, 16> cols;
    ObSEArray<ObSqlDatumInfo, 16> datum_infos;
    const int32_t row_id = static_cast<int32_t>(index);
    for (int64_t i = 0; OB_SUCC(ret) && i < request_cnt_; ++i) {
      row.storage_datums_[i].reuse();
      if (OB_FAIL(cols.push_back(static_cast<int32_t>(i)))) {
      } else {
        ret = datum_infos.push_back(ObSqlDatumInfo(row.storage_datums_ + i, nullptr));
      }
    }
    if (OB_FAIL(ret)) {
    } else {
      ret = try_get_rows_rust(cols, &row_id, nullptr, 1, datum_infos, 0, true);
    }
    if (OB_SUCC(ret)) {
      row.row_flag_.reset();
      row.row_flag_.set_flag(ObDmlFlag::DF_INSERT);
      row.count_ = request_cnt_;
      row.mvcc_row_flag_.reset();
      row.fast_filter_skipped_ = false;
    }
  }
  return ret;
}

const ObRowHeader ObMicroBlockDecoder::init_major_store_row_header()
{
  ObRowHeader rh;
  rh.set_row_flag(ObDmlFlag::DF_INSERT);
  return rh;
}

int ObMicroBlockDecoder::compare_rowkey(const ObDatumRowkey &rowkey, const int64_t index, int32_t &compare_result)
{
  int ret = OB_SUCCESS;
  compare_result = 0;
  if (OB_UNLIKELY(index < 0 || index >= row_count_ || nullptr == datum_utils_)) {
    ret = OB_INVALID_ARGUMENT;
  } else {
    const ObStorageDatumUtils &datum_utils = *datum_utils_;
    const int64_t compare_column_count = rowkey.get_datum_cnt();
    if (OB_UNLIKELY(datum_utils.get_rowkey_count() < compare_column_count)) {
      ret = OB_ERR_UNEXPECTED;
      STORAGE_LOG(WARN, "Unexpected datum utils to compare rowkey", K(ret), K(compare_column_count), K(datum_utils));
    } else {
      ObSEArray<int32_t, 16> cols;
      ObSEArray<ObStorageDatum, 16> store_datums;
      ObSEArray<ObSqlDatumInfo, 16> datum_infos;
      const int32_t row_id = static_cast<int32_t>(index);
      if (OB_FAIL(store_datums.prepare_allocate(compare_column_count))) {
      }
      for (int64_t i = 0; OB_SUCC(ret) && i < compare_column_count; ++i) {
        if (OB_FAIL(cols.push_back(static_cast<int32_t>(i)))) {
        } else {
          ret = datum_infos.push_back(ObSqlDatumInfo(&store_datums.at(i), nullptr));
        }
      }
      if (OB_FAIL(ret)) {
      } else {
        ret = try_get_rows_rust(cols, &row_id, nullptr, 1, datum_infos, 0);
      }
      for (int64_t i = 0; OB_SUCC(ret) && i < compare_column_count && 0 == compare_result; ++i) {
        if (OB_FAIL(datum_utils.get_cmp_funcs().at(i).compare(
                store_datums.at(i), rowkey.datums_[i], compare_result))) {
        }
      }
    }
  }
  return ret;
}

int ObMicroBlockDecoder::compare_rowkey(const ObDatumRange &range,
                                        const int64_t index,
                                        int32_t &start_key_compare_result,
                                        int32_t &end_key_compare_result)
{
  int ret = OB_SUCCESS;
  start_key_compare_result = 0;
  end_key_compare_result = 0;
  const ObDatumRowkey &start_rowkey = range.get_start_key();
  const ObDatumRowkey &end_rowkey = range.get_end_key();
  if (OB_UNLIKELY(index < 0 || index >= row_count_ || nullptr == datum_utils_)) {
    ret = OB_INVALID_ARGUMENT;
  } else {
    const ObStorageDatumUtils &datum_utils = *datum_utils_;
    const int64_t compare_column_count = start_rowkey.get_datum_cnt();
    if (OB_UNLIKELY(datum_utils.get_rowkey_count() < compare_column_count)) {
      ret = OB_ERR_UNEXPECTED;
      STORAGE_LOG(WARN, "Unexpected datum utils to compare rowkey", K(ret), K(compare_column_count), K(datum_utils));
    } else {
      ObSEArray<int32_t, 16> cols;
      ObSEArray<ObStorageDatum, 16> store_datums;
      ObSEArray<ObSqlDatumInfo, 16> datum_infos;
      const int32_t row_id = static_cast<int32_t>(index);
      if (OB_FAIL(store_datums.prepare_allocate(compare_column_count))) {
      }
      for (int64_t i = 0; OB_SUCC(ret) && i < compare_column_count; ++i) {
        if (OB_FAIL(cols.push_back(static_cast<int32_t>(i)))) {
        } else {
          ret = datum_infos.push_back(ObSqlDatumInfo(&store_datums.at(i), nullptr));
        }
      }
      if (OB_FAIL(ret)) {
      } else {
        ret = try_get_rows_rust(cols, &row_id, nullptr, 1, datum_infos, 0);
      }
      for (int64_t i = 0; OB_SUCC(ret) && i < compare_column_count && 0 == start_key_compare_result; ++i) {
        if (OB_FAIL(datum_utils.get_cmp_funcs().at(i).compare(
                store_datums.at(i), start_rowkey.datums_[i], start_key_compare_result))) {
        } else if (start_key_compare_result >= 0 && 0 == end_key_compare_result) {
          if (OB_FAIL(datum_utils.get_cmp_funcs().at(i).compare(
                  store_datums.at(i), end_rowkey.datums_[i], end_key_compare_result))) {
          }
        }
      }
    }
  }
  return ret;
}

int ObMicroBlockDecoder::find_bound(
    const ObDatumRowkey &key,
    const bool lower_bound,
    const int64_t begin_idx,
    int64_t &row_idx,
    bool &equal)
{
  int ret = OB_SUCCESS;
  bool located_by_rust = false;
  if (begin_idx < row_count_
      && OB_FAIL(try_locate_rowkey_bound(
          block_data_, block_size_, header_, col_header_, key, read_info_,
          false, lower_bound, begin_idx, row_count_, located_by_rust, row_idx, equal))) {
  } else if (!located_by_rust) {
    ret = ObIMicroBlockDecoder::find_bound(key, lower_bound, begin_idx, row_idx, equal);
  }
  return ret;
}

int ObMicroBlockDecoder::find_bound(
    const ObDatumRowkey &key,
    const bool lower_bound,
    const int64_t begin_idx,
    const int64_t end_idx,
    int64_t &row_idx,
    bool &equal)
{
  int ret = OB_SUCCESS;
  bool located_by_rust = false;
  if (OB_FAIL(try_locate_rowkey_bound(
          block_data_, block_size_, header_, col_header_, key, read_info_,
          false, lower_bound, begin_idx, end_idx, located_by_rust, row_idx, equal))) {
  } else if (!located_by_rust) {
    ret = ObIMicroBlockDecoder::find_bound(key, lower_bound, begin_idx, end_idx, row_idx, equal);
  }
  return ret;
}

int ObMicroBlockDecoder::find_bound(
    const ObDatumRange &range,
    const int64_t begin_idx,
    int64_t &row_idx,
    bool &equal,
    int64_t &end_key_begin_idx,
    int64_t &end_key_end_idx)
{
  int ret = OB_SUCCESS;
  bool located_by_rust = false;
  if (OB_FAIL(try_locate_rowkey_bound(
          block_data_, block_size_, header_, col_header_, range.get_start_key(), read_info_,
          false, true, begin_idx, row_count_, located_by_rust, row_idx, equal))) {
  } else if (!located_by_rust) {
    ret = ObIMicroBlockDecoder::find_bound(
        range, begin_idx, row_idx, equal, end_key_begin_idx, end_key_end_idx);
  } else {
    end_key_begin_idx = row_idx;
    end_key_end_idx = row_count_;
  }
  return ret;
}

int ObMicroBlockDecoder::get_row_header(
    const int64_t row_idx,
    const ObRowHeader *&row_header)
{
  UNUSEDx(row_idx);
  int ret = OB_SUCCESS;
  if (IS_NOT_INIT) {
    ret = OB_NOT_INIT;
  } else {
    row_header = &get_major_store_row_header();
  }
  return ret;
}

int ObMicroBlockDecoder::get_row_count(int64_t &row_count)
{
  int ret = OB_SUCCESS;
  if (IS_NOT_INIT) {
    ret = OB_NOT_INIT;
  } else {
    row_count = header_->row_count_;
  }
  return ret;
}

int ObMicroBlockDecoder::get_multi_version_info(
    const int64_t row_idx,
    const int64_t schema_rowkey_cnt,
    const ObRowHeader *&row_header,
    int64_t &trans_version,
    int64_t &sql_sequence)
{
  UNUSEDx(row_idx, schema_rowkey_cnt, row_header, trans_version, sql_sequence);
  return OB_NOT_SUPPORTED;
}

int ObMicroBlockDecoder::filter_truncate_evaluator(
    storage::ObTruncateFilterEvaluator &evaluator,
    const int64_t start,
    const int64_t count,
    const common::ObBitmap *candidate_rows,
    common::ObBitmap &result_bitmap)
{
  int ret = OB_SUCCESS;
  if (OB_UNLIKELY(start < 0 || count <= 0 || start + count > row_count_ ||
                  !evaluator.is_valid())) {
    ret = OB_INVALID_ARGUMENT;
  } else if (OB_UNLIKELY(nullptr == header_ || nullptr == columns_)) {
    ret = OB_ERR_UNEXPECTED;
  } else {
    const int64_t column_count = evaluator.referenced_column_count();
    ObArenaAllocator &allocator = decoder_allocator_.get_inner_allocator();
    int32_t *row_ids = nullptr;
    int32_t *result_offsets = nullptr;
    const char **cell_datas = nullptr;
    ObDatum *column_datums = nullptr;
    char *datum_payloads = nullptr;
    ObStorageDatum *projected_row = nullptr;
    decoder_allocator_.reuse();
    if (OB_UNLIKELY(column_count <= 0) ||
        OB_ISNULL(row_ids = static_cast<int32_t *>(allocator.alloc(sizeof(int32_t) * count))) ||
        OB_ISNULL(result_offsets = static_cast<int32_t *>(allocator.alloc(sizeof(int32_t) * count))) ||
        OB_ISNULL(cell_datas = static_cast<const char **>(allocator.alloc(sizeof(char *) * count))) ||
        OB_ISNULL(column_datums = static_cast<ObDatum *>(
            allocator.alloc(sizeof(ObDatum) * column_count * count))) ||
        OB_ISNULL(datum_payloads = static_cast<char *>(
            allocator.alloc(OBJ_DATUM_DECIMALINT_MAX_RES_SIZE * column_count * count))) ||
        OB_ISNULL(projected_row = static_cast<ObStorageDatum *>(
            allocator.alloc(sizeof(ObStorageDatum) * column_count)))) {
      ret = OB_ALLOCATE_MEMORY_FAILED;
    } else {
      MEMSET(cell_datas, 0, sizeof(char *) * count);
      for (int64_t i = 0; i < column_count * count; ++i) {
        new (column_datums + i) ObDatum();
        column_datums[i].ptr_ = datum_payloads + i * OBJ_DATUM_DECIMALINT_MAX_RES_SIZE;
        column_datums[i].pack_ = 0;
      }
      for (int64_t i = 0; i < column_count; ++i) {
        new (projected_row + i) ObStorageDatum();
      }
    }

    int64_t active_count = 0;
    for (int64_t offset = 0; OB_SUCC(ret) && offset < count; ++offset) {
      if (nullptr != candidate_rows && !candidate_rows->test(offset)) {
      } else {
        row_ids[active_count] = static_cast<int32_t>(start + offset);
        result_offsets[active_count] = static_cast<int32_t>(offset);
        ++active_count;
      }
    }
    for (int64_t column = 0; OB_SUCC(ret) && column < column_count && active_count > 0; ++column) {
      const int64_t column_index = evaluator.referenced_column(column);
      ObDatum *datums = column_datums + column * count;
      if (OB_UNLIKELY(column_index < 0 || column_index >= header_->column_count_)) {
        ret = OB_INDEX_OUT_OF_RANGE;
      } else if (OB_FAIL(get_col_datums(
                     column_index, row_ids, cell_datas, active_count, datums))) {
      }
    }
    for (int64_t row = 0; OB_SUCC(ret) && row < active_count; ++row) {
      for (int64_t column = 0; column < column_count; ++column) {
        projected_row[column].reuse();
        projected_row[column].shallow_copy_from_datum(column_datums[column * count + row]);
      }
      if (OB_SUCC(ret)) {
        bool filtered = false;
        if (OB_FAIL(evaluator.filter_projected(projected_row, column_count, filtered))) {
        } else if (!filtered) {
          if (OB_FAIL(result_bitmap.set(result_offsets[row]))) {
          }
        }
      }
    }
    LOG_TRACE("[TRUNCATE INFO] encoded micro block truncate filter", K(ret), K(start), K(count),
              K(result_bitmap.popcnt()), K(result_bitmap), KPC_(header));
  }
  return ret;
}

int ObMicroBlockDecoder::get_rows(
    const common::ObIArray<int32_t> &cols,
    const common::ObIArray<const share::schema::ObColumnParam *> &col_params,
    const bool is_padding_mode,
    const int32_t *row_ids,
    const char **cell_datas,
    const int64_t row_cap,
    common::ObIArray<ObSqlDatumInfo> &datum_infos,
    const int64_t datum_offset)
{
  int ret = OB_SUCCESS;
  decoder_allocator_.reuse();
  if (IS_NOT_INIT) {
    ret = OB_NOT_INIT;
  } else if (OB_UNLIKELY(nullptr == row_ids || nullptr == cell_datas ||
                         cols.count() != datum_infos.count())) {
    ret = OB_INVALID_ARGUMENT;
  } else {
    ret = try_get_rows_rust(cols, row_ids, cell_datas, row_cap, datum_infos, datum_offset,
                            false, true);
    for (int64_t i = 0; OB_SUCC(ret) && i < cols.count(); i++) {
      common::ObDatum *col_datums = datum_infos.at(i).datum_ptr_ + datum_offset;
      if (nullptr != col_params.at(i) && need_padding(is_padding_mode, col_params.at(i)->get_meta_type())) {
        // need padding
        if (OB_FAIL(storage::pad_on_datums(
                    col_params.at(i)->get_accuracy(),
                    col_params.at(i)->get_meta_type().get_collation_type(),
                    decoder_allocator_.get_inner_allocator(),
                    row_cap,
                    col_datums))) {
        }
      }
    }
  }
  return ret;
}

int ObMicroBlockDecoder::try_get_rows_rust(
    const common::ObIArray<int32_t> &cols,
    const int32_t *row_ids,
    const char **cell_datas,
    const int64_t row_cap,
    common::ObIArray<ObSqlDatumInfo> &datum_infos,
    const int64_t datum_offset,
    const bool preserve_nop,
    const bool reverse_trans_version)
{
  int ret = OB_SUCCESS;
  UNUSED(cell_datas);
  if (row_cap < 0 || row_cap > UINT32_MAX || cols.count() <= 0
      || block_size_ <= 0 || nullptr == block_data_) {
    return OB_INVALID_ARGUMENT;
  }
  std::vector<uint32_t> rust_rows;
  std::vector<uint32_t> rust_columns;
  std::vector<uint32_t> rust_result_columns(cols.count(), UINT32_MAX);
  std::vector<ObObjType> rust_obj_types(cols.count(), ObNullType);
  rust_rows.reserve(row_cap);
  rust_columns.reserve(cols.count());
  for (int64_t i = 0; i < row_cap; ++i) {
    if (row_ids[i] < 0 || row_ids[i] >= row_count_) {
      return OB_INVALID_ARGUMENT;
    }
    rust_rows.push_back(static_cast<uint32_t>(row_ids[i]));
  }
  for (int64_t i = 0; i < cols.count(); ++i) {
    const int32_t col_id = cols.at(i);
    if (col_id < 0 || col_id >= request_cnt_) {
      return OB_INVALID_ARGUMENT;
    }
    const ObColumnHeader *column = columns_[col_id].col_header_;
    if (nullptr == column) {
      continue;
    }
    const int64_t store_idx = column - col_header_;
    if (store_idx < 0 || store_idx >= header_->column_count_) {
      return OB_INVALID_DATA;
    }
    rust_result_columns[i] = rust_columns.size();
    rust_obj_types[i] = column->get_store_obj_type();
    rust_columns.push_back(static_cast<uint32_t>(store_idx));
  }
  if (!rust_columns.empty()) {
    const rust::Slice<const uint8_t> block(
        reinterpret_cast<const uint8_t *>(block_data_), static_cast<std::size_t>(block_size_));
    const rust::Slice<const uint32_t> rows(rust_rows.data(), rust_rows.size());
    const rust::Slice<const uint32_t> columns(rust_columns.data(), rust_columns.size());
    micro_block::BatchResult result = micro_block::project_pax_block(block, rows, columns);
    switch (result.code) {
      case micro_block::Code::Ok:
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
    if (result.cells.size() != rust_rows.size() * rust_columns.size()) {
      return OB_INVALID_DATA;
    }
    for (int64_t col_idx = 0; OB_SUCC(ret) && col_idx < cols.count(); ++col_idx) {
      if (UINT32_MAX == rust_result_columns[col_idx]) {
        continue;
      }
      common::ObDatum *datums = datum_infos.at(col_idx).datum_ptr_ + datum_offset;
      const ObObjType obj_type = rust_obj_types[col_idx];
      int64_t integer_mask = 0;
      if (ObIntTC == ob_obj_type_class(obj_type)) {
        integer_mask = ~INTEGER_MASK_TABLE[get_type_size_map()[obj_type]];
      }
      for (int64_t row_idx = 0; OB_SUCC(ret) && row_idx < row_cap; ++row_idx) {
        const micro_block::CellDesc &cell =
            result.cells[row_idx * rust_columns.size() + rust_result_columns[col_idx]];
        common::ObDatum &datum = datums[row_idx];
        ret = materialize_rust_cell(
            cell, result.data, block_data_, block_size_, obj_type, integer_mask, integer_mask,
            preserve_nop ? RustNopMode::AS_EXTENDED : RustNopMode::AS_NULL,
            decoder_allocator_.get_inner_allocator(), datum);
      }
      if (OB_SUCC(ret) && reverse_trans_version && header_->is_trans_version_column_idx(
              static_cast<int64_t>(rust_columns[rust_result_columns[col_idx]]))) {
        ret = storage::reverse_trans_version_val(datums, row_cap);
      }
    }
  }
  for (int64_t col_idx = 0; OB_SUCC(ret) && col_idx < cols.count(); ++col_idx) {
    if (UINT32_MAX == rust_result_columns[col_idx]) {
      const ObColumnParam *new_col_param = columns_[cols.at(col_idx)].col_param_;
      common::ObDatum *datums = datum_infos.at(col_idx).datum_ptr_ + datum_offset;
      if (nullptr != new_col_param) {
        ObNewColumnCommonDecoder decoder;
        ret = decoder.batch_decode(
            new_col_param, &decoder_allocator_.get_inner_allocator(), row_cap, datums);
      } else {
        for (int64_t row_idx = 0; row_idx < row_cap; ++row_idx) {
          if (preserve_nop) {
            datums[row_idx].set_ext();
            const_cast<ObObj *>(datums[row_idx].extend_obj_)->set_ext(ObActionFlag::OP_NOP);
          } else {
            datums[row_idx].set_null();
          }
        }
      }
    }
  }
  return ret;
}

int ObMicroBlockDecoder::get_row_count(
    int32_t col_id,
    const int32_t *row_ids,
    const int64_t row_cap,
    const bool contains_null,
    const share::schema::ObColumnParam *col_param,
    int64_t &count)
{
  UNUSED(col_param);
  int ret = OB_SUCCESS;
  decoder_allocator_.reuse();
  if (IS_NOT_INIT) {
    ret = OB_NOT_INIT;
  } else if (OB_UNLIKELY(col_id < 0 || col_id >= request_cnt_ || row_cap < 0
             || (row_cap > 0 && nullptr == row_ids))) {
    ret = OB_INVALID_ARGUMENT;
  } else if (contains_null || 0 == row_cap) {
    count = row_cap;
  } else {
    const ObColumnHeader *column = columns_[col_id].col_header_;
    std::vector<uint32_t> rust_rows;
    rust_rows.reserve(row_cap);
    for (int64_t i = 0; i < row_cap; ++i) {
      if (row_ids[i] < 0 || row_ids[i] >= row_count_) {
        return OB_INVALID_ARGUMENT;
      }
      rust_rows.push_back(static_cast<uint32_t>(row_ids[i]));
    }
    if (nullptr == column) {
      const ObColumnParam *new_col_param = columns_[col_id].col_param_;
      if (nullptr != new_col_param) {
        int64_t null_count = 0;
        ObNewColumnCommonDecoder decoder;
        if (OB_FAIL(decoder.get_null_count(new_col_param, row_cap, null_count))) {
        } else {
          count = row_cap - null_count;
        }
      } else {
        count = 0;
      }
    } else {
      const int64_t store_idx = column - col_header_;
      if (store_idx < 0 || store_idx >= header_->column_count_) {
        return OB_INVALID_DATA;
      }
      const rust::Slice<const uint8_t> block(
          reinterpret_cast<const uint8_t *>(block_data_), static_cast<std::size_t>(block_size_));
      const rust::Slice<const uint32_t> rows(rust_rows.data(), rust_rows.size());
      const micro_block::CountResult result = micro_block::count_pax_non_null(
          block, rows, static_cast<uint32_t>(store_idx));
      switch (result.code) {
        case micro_block::Code::Ok:
          count = static_cast<int64_t>(result.count);
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
    }
  }
  return ret;
}

int ObMicroBlockDecoder::get_aggregate_result(
    const ObTableIterParam &iter_param,
    const ObTableAccessContext &context,
    const int32_t col_offset,
    const share::schema::ObColumnParam &col_param,
    const int32_t *row_ids,
    const int64_t row_cap,
    storage::ObAggDatumBuf &agg_datum_buf,
    storage::ObAggCell &agg_cell)
{
  int ret = OB_SUCCESS;
  decoder_allocator_.reuse();
  const char **cell_datas = agg_datum_buf.get_cell_datas();
  ObDatum *datum_buf = agg_datum_buf.get_datums();
  if (OB_FAIL(get_col_datums(col_offset, row_ids, cell_datas, row_cap, datum_buf))) {
  } else if (agg_cell.need_padding() && OB_FAIL(storage::pad_on_datums(
                                          col_param.get_accuracy(),
                                          col_param.get_meta_type().get_collation_type(),
                                          decoder_allocator_.get_inner_allocator(),
                                          row_cap,
                                          datum_buf))) {
  } else if (col_param.get_meta_type().is_lob_storage() && header_->has_lob_out_row() &&
        OB_FAIL(fill_datums_lob_locator(iter_param, context, col_param, row_cap, datum_buf))) {
  } else if (OB_FAIL(agg_cell.eval_batch(datum_buf, row_cap))) {
  }
  return ret;
}

int ObMicroBlockDecoder::get_col_datums(
    int32_t col_id,
    const int32_t *row_ids,
    const char **cell_datas,
    const int64_t row_cap,
    common::ObDatum *col_datums)
{
  int ret = OB_SUCCESS;
  ObSEArray<int32_t, 1> rust_cols;
  ObSEArray<ObSqlDatumInfo, 1> rust_infos;
  if (OB_FAIL(rust_cols.push_back(col_id))) {
  } else if (OB_FAIL(rust_infos.push_back(ObSqlDatumInfo(col_datums, nullptr)))) {
  } else {
    ret = try_get_rows_rust(rust_cols, row_ids, cell_datas, row_cap, rust_infos, 0, false, true);
  }
  return ret;
}

int ObMicroBlockDecoder::get_column_datum(
    const ObTableIterParam &iter_param,
    const ObTableAccessContext &context,
    const share::schema::ObColumnParam &col_param,
    const int32_t col_offset,
    const int64_t row_index,
    ObStorageDatum &datum)
{
  int ret = OB_SUCCESS;
  if (IS_NOT_INIT) {
    ret = OB_NOT_INIT;
  } else if (OB_UNLIKELY(col_offset < 0 || col_offset >= header_->column_count_
             || row_index < 0 || row_index >= row_count_ || row_index > INT32_MAX)) {
    ret = OB_INVALID_ARGUMENT;
  } else {
    const int32_t row_id = static_cast<int32_t>(row_index);
    const char *cell_data = nullptr;
    datum.reuse();
    if (OB_FAIL(get_col_datums(col_offset, &row_id, &cell_data, 1, &datum))) {
    } else if (col_param.get_meta_type().is_lob_storage() && !datum.is_null() && !datum.get_lob_data().in_row_) {
      if (OB_FAIL(context.lob_locator_helper_->fill_lob_locator_v2(datum, col_param, iter_param, context))) {
      }
    }
  }
  return ret;
}

} // namespace blocksstable
} // namespace oceanbase
