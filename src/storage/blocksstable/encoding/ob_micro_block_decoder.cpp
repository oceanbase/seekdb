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
ObNoneExistColumnDecoder ObMicroBlockDecoder::none_exist_column_decoder_;
ObColumnDecoderCtx ObMicroBlockDecoder::none_exist_column_decoder_ctx_;
ObNewColumnDecoder ObMicroBlockDecoder::new_column_decoder_;

// performance critical, do not check parameters
int ObColumnDecoder::decode(common::ObDatum &datum, const int64_t row_id,
    const ObBitStream &bs, const char *data, const int64_t len)
{
  int ret = OB_SUCCESS;
  if (OB_FAIL(decoder_->decode(*ctx_, datum, row_id, bs, data, len))) {
  }
  return ret;
}

int ObColumnDecoder::batch_decode(
    const ObIRowIndex *row_index,
    const int32_t *row_ids,
    const char **cell_datas,
    const int64_t row_cap,
    common::ObDatum *datums)
{
  int ret = OB_SUCCESS;
  if (OB_UNLIKELY(nullptr == row_index
                  || nullptr == row_ids
                  || nullptr == cell_datas
                  || nullptr == datums
                  || 0 >= row_cap)) {
    ret = OB_INVALID_ARGUMENT;
  } else if (OB_UNLIKELY(!decoder_->can_vectorized())) {
    ret = OB_ERR_UNEXPECTED;
  } else if (OB_FAIL(decoder_->batch_decode(
              *ctx_, row_index, row_ids, cell_datas, row_cap, datums))) {
  } else if (OB_UNLIKELY(ctx_->is_trans_version_col()) &&
             OB_FAIL(storage::reverse_trans_version_val(datums, row_cap))) {
  }

  LOG_DEBUG("[Batch decode] Batch decoded datums: ",
      K(ret), K(row_cap), K(*ctx_), K(ObArrayWrap<int32_t>(row_ids, row_cap)),
      K(ObArrayWrap<ObDatum>(datums, row_cap)));
  return ret;
}

int ObColumnDecoder::get_row_count(
    const ObIRowIndex *row_index,
    const int32_t *row_ids,
    const int64_t row_cap,
    const bool contains_null,
    int64_t &count)
{
  int ret = OB_SUCCESS;
  int64_t null_count = 0;
  if (contains_null) {
    count = row_cap;
  } else if (OB_FAIL(decoder_->get_null_count(*ctx_, row_index, row_ids, row_cap, null_count))) {
  } else {
    count = row_cap - null_count;
  }
  return ret;
}

// performance critical, do not check parameters
int ObColumnDecoder::quick_compare(const ObStorageDatum &left, const ObStorageDatumCmpFunc &cmp_func, const int64_t row_id,
    const ObBitStream &bs, const char *data, const int64_t len, int32_t &cmp_ret)
{
  int ret = OB_SUCCESS;
  ObStorageDatum right_datum;
  cmp_ret = 0;
  if (OB_FAIL(decoder_->decode(*ctx_, right_datum, row_id, bs, data, len))) {
  } else if (OB_FAIL(cmp_func.compare(left, right_datum, cmp_ret))) {
  }
  return ret;
}

typedef int (*new_decoder_func)(char *buf,
                                const ObMicroBlockHeader &header,
                                const ObColumnHeader &col_header,
                                const char *meta_data,
                                const ObIColumnDecoder *&decoder);

static new_decoder_func new_decoder_funcs_[ObColumnHeader::MAX_TYPE] =
{
    new_decoder_with_allocated_buf<ObRawDecoder>,
    new_decoder_with_allocated_buf<ObDictDecoder>,
    new_decoder_with_allocated_buf<ObRLEDecoder>,
    new_decoder_with_allocated_buf<ObConstDecoder>,
    new_decoder_with_allocated_buf<ObIntegerBaseDiffDecoder>,
    new_decoder_with_allocated_buf<ObStringDiffDecoder>,
    new_decoder_with_allocated_buf<ObHexStringDecoder>,
    new_decoder_with_allocated_buf<ObStringPrefixDecoder>,
    new_decoder_with_allocated_buf<ObColumnEqualDecoder>,
    new_decoder_with_allocated_buf<ObInterColSubStrDecoder>
};


template <class Decoder>
int new_decoder_with_allocated_buf(char *buf,
                                   const ObMicroBlockHeader &header,
                                   const ObColumnHeader &col_header,
                                   const char *meta_data,
                                   const ObIColumnDecoder *&decoder)
{
  int ret = OB_SUCCESS;
  Decoder *d = nullptr;
  if (OB_UNLIKELY(!col_header.is_valid())) {
    ret = OB_INVALID_ARGUMENT;
  } else if (OB_ISNULL(d = new(buf) Decoder())) {
    ret = OB_ERR_UNEXPECTED;
  } else if (OB_FAIL(d->init(header, col_header, meta_data))) {
  } else {
    decoder = d;
  }
  return ret;
}

//////////////////////////ObIEncodeBlockGetReader/////////////////////////
ObNoneExistColumnDecoder ObIEncodeBlockReader::none_exist_column_decoder_;
ObColumnDecoderCtx ObIEncodeBlockReader::none_exist_column_decoder_ctx_;
ObIEncodeBlockReader::ObIEncodeBlockReader()
  : request_cnt_(0),
    header_(NULL), col_header_(NULL), meta_data_(NULL), row_data_(NULL),
    block_data_(NULL), block_size_(0),
    var_row_index_(), fix_row_index_(), row_index_(&var_row_index_),
    decoders_(nullptr),
    default_decoders_(),
    ctxs_(NULL),
    decoder_allocator_(ObMemAttr(common::ObModIds::OB_DECODER_CTX), OB_MALLOC_NORMAL_BLOCK_SIZE),
    buf_allocator_(ObMemAttr("OB_IENB_READER"), OB_MALLOC_NORMAL_BLOCK_SIZE),
    allocated_decoders_buf_(nullptr),
    allocated_decoders_buf_size_(0),
    store_id_array_(NULL), column_type_array_(NULL),
    default_store_ids_(), default_column_types_(),
    need_cast_(false)
{}

ObIEncodeBlockReader::~ObIEncodeBlockReader()
{
}

void ObIEncodeBlockReader::reuse()
{
  header_ = nullptr;
  request_cnt_ = 0;
  decoders_ = nullptr;
  need_cast_ = false;
  col_header_ = nullptr;
  meta_data_ = nullptr;
  row_data_ = nullptr;
  block_data_ = nullptr;
  block_size_ = 0;
  ctxs_ = nullptr;
  store_id_array_ = nullptr;
  column_type_array_ = nullptr;
  decoder_allocator_.reuse();
}

void ObIEncodeBlockReader::reset()
{
  header_ = NULL;
  request_cnt_ = 0;
  decoders_ = nullptr;
  need_cast_ = false;
  col_header_ = NULL;
  meta_data_ = NULL;
  row_data_ = NULL;
  block_data_ = NULL;
  block_size_ = 0;
  ctxs_ = NULL;
  store_id_array_ = NULL;
  column_type_array_ = NULL;
  decoder_allocator_.reset();
  buf_allocator_.reset();
  allocated_decoders_buf_ = nullptr;
  allocated_decoders_buf_size_ = 0;
}

int ObIEncodeBlockReader::prepare(const int64_t column_cnt)
{
  int ret = OB_SUCCESS;
  if (column_cnt > DEFAULT_DECODER_CNT) {
    const int64_t store_ids_size = sizeof(store_id_array_[0]) * column_cnt;
    const int64_t column_types_size = ALIGN_UP(sizeof(ObObjMeta) * column_cnt, 8);
    const int64_t col_decoder_size = sizeof(ObColumnDecoder) * column_cnt;
    char *buf = nullptr;
    if (nullptr
        == (buf = reinterpret_cast<char*>(decoder_allocator_.alloc(
          store_ids_size + column_types_size + col_decoder_size)))) {
      ret = OB_ALLOCATE_MEMORY_FAILED;
    } else {
      store_id_array_ = reinterpret_cast<int64_t *>(buf);
      column_type_array_ = reinterpret_cast<ObObjMeta *>(buf + store_ids_size);
      decoders_ =  reinterpret_cast<ObColumnDecoder *>(buf + store_ids_size + column_types_size);
    }
  } else {
    store_id_array_ = &default_store_ids_[0];
    column_type_array_ = &default_column_types_[0];
    decoders_ = &default_decoders_[0];
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
    row_data_ = block_data.get_buf() + header_->row_data_offset_;
    const int64_t row_data_len = block_data.get_buf_size() - header_->row_data_offset_;

    if (header_->row_index_byte_ > 0) {
      if (OB_FAIL(var_row_index_.init(row_data_, row_data_len,
          header_->row_count_, header_->row_index_byte_))) {
      }
      row_index_ = &var_row_index_;
    } else {
      if (OB_FAIL(fix_row_index_.init(row_data_, row_data_len, header_->row_count_))) {
      }
      row_index_ = &fix_row_index_;
    }

    if (OB_SUCC(ret)) {
      if (OB_UNLIKELY(!header_->is_valid())) {
        ret = OB_INNER_STAT_ERROR;
        LOG_ERROR("invalid micro_block_header", K(ret), "header", *header_);
      } else if (OB_FAIL(init_decoders())) {
      }
    }
  }
  if (OB_FAIL(ret)) {
    reset();
  }
  return ret;
}

int ObIEncodeBlockReader::init_decoders()
{
  int ret = OB_SUCCESS;
  int64_t decoders_buf_pos = 0;

  if (OB_UNLIKELY(NULL == store_id_array_ || NULL == column_type_array_
      || nullptr == decoders_ || NULL == header_ || NULL == col_header_)) {
    ret = OB_INNER_STAT_ERROR;
  } else if (OB_FAIL(alloc_decoders_buf(decoders_buf_pos))) {
  } else {
    for (int64_t i = 0; OB_SUCC(ret) && i < request_cnt_; ++i) {
      if (OB_FAIL(add_decoder(store_id_array_[i], column_type_array_[i], decoders_buf_pos, decoders_[i]))) {
      }
    }
  }
  return ret;
}

int ObIEncodeBlockReader::get_micro_metas(const ObMicroBlockHeader *&header,
    const ObColumnHeader *&col_header, const char *&meta_data,
    const char *block, const int64_t block_size)
{
  int ret = OB_SUCCESS;
  if (NULL == block || block_size <= 0) {

    ret = OB_INVALID_ARGUMENT;
  } else {
    header = reinterpret_cast<const ObMicroBlockHeader *>(block);
    block += header->header_size_;
    col_header = reinterpret_cast<const ObColumnHeader *>(block);
    meta_data = block + sizeof(*col_header) * header->column_count_;
    if (meta_data - block > block_size
        || header->row_data_offset_ > block_size) {
      ret = OB_INVALID_ARGUMENT;
    }
  }
  return ret;
}

int ObIEncodeBlockReader::add_decoder(
    const int64_t store_idx, const ObObjMeta &obj_meta,
    int64_t &decoders_buf_pos, ObColumnDecoder &dest)
{
  int ret = OB_SUCCESS;
  if (store_idx >= 0 && !obj_meta.is_valid()) {
    ret = OB_INVALID_ARGUMENT;
  } else {
    if (store_idx < 0 || store_idx >= header_->column_count_) { // non exist column
      dest.decoder_ = &none_exist_column_decoder_;
      dest.ctx_ = &none_exist_column_decoder_ctx_;
    } else {
      const ObIColumnDecoder *decoder = nullptr;
      const ObIColumnDecoder *ref_decoder = nullptr;
      if (OB_FAIL(acquire(store_idx, decoders_buf_pos, decoder))) {
      } else {
        dest.decoder_ = decoder;
        dest.ctx_ = &ctxs_[store_idx];
        dest.ctx_->fill(obj_meta, header_, &col_header_[store_idx], &decoder_allocator_, store_idx);

        int64_t ref_col_idx = -1;
        if (OB_FAIL(decoder->get_ref_col_idx(ref_col_idx))) {
        }
        if (OB_SUCC(ret) && ref_col_idx >= 0) {
          if (OB_FAIL(acquire(ref_col_idx, decoders_buf_pos, ref_decoder))) {
          } else {
            dest.ctx_->ref_decoder_ = ref_decoder;
            dest.ctx_->ref_ctx_ = &ctxs_[ref_col_idx];
            dest.ctx_->ref_ctx_->fill(obj_meta, header_, &col_header_[ref_col_idx], &decoder_allocator_, ref_col_idx);
          }
        }
      }
    }
  }
  return ret;
}

// called before inited
// performance critical, do not check parameters
int ObIEncodeBlockReader::acquire(const int64_t store_idx,
                                  int64_t &decoders_buf_pos,
                                  const ObIColumnDecoder *&decoder)
{
  int ret = OB_SUCCESS;
  const int64_t decoder_size = decoder_sizes[col_header_[store_idx].type_];
  if (OB_UNLIKELY(decoders_buf_pos + decoder_size > allocated_decoders_buf_size_)) {
    ret = OB_ERR_UNEXPECTED;
    LOG_ERROR("decoders buf not enough", K(ret), K(decoders_buf_pos),
        K(decoder_size), K(allocated_decoders_buf_size_), K(store_idx));
  } else if (OB_FAIL(new_decoder_funcs_[col_header_[store_idx].type_](
      allocated_decoders_buf_ + decoders_buf_pos,
      *header_, col_header_[store_idx], meta_data_, decoder))) {
  } else {
    decoders_buf_pos += decoder_size;
  }
  return ret;
}

int ObIEncodeBlockReader::alloc_decoders_buf(int64_t &decoders_buf_pos)
{
  int ret = OB_SUCCESS;
  int64_t size = 0;
  const int64_t decoder_ctx_cnt = MAX(request_cnt_, header_->column_count_);
  size += sizeof(ObColumnDecoderCtx) * decoder_ctx_cnt; 

  for (int64_t i = 0; i < request_cnt_; ++i) {
    const int64_t store_idx = store_id_array_[i];
    if (store_idx < 0 || store_idx >= header_->column_count_) {
      // non exist column
    } else {
      size += decoder_sizes[col_header_[store_idx].type_];
      if (col_header_[store_idx].type_ == ObColumnHeader::COLUMN_EQUAL ||
          col_header_[store_idx].type_ == ObColumnHeader::COLUMN_SUBSTR) {
        // for decoder which has ref deocoder, we don't know the exact type of ref decoder,
        // so use the maximum size.
        size += MAX_DECODER_SIZE;
      }
    }
  }

  if (size > allocated_decoders_buf_size_) {
    if (allocated_decoders_buf_size_ > 0) {
      allocated_decoders_buf_ = nullptr;
      allocated_decoders_buf_size_ = 0;
      buf_allocator_.reuse();
    }
    if (OB_ISNULL(allocated_decoders_buf_ = (char*)buf_allocator_.alloc(size))) {
      ret = OB_ALLOCATE_MEMORY_FAILED;
    } else {
      allocated_decoders_buf_size_ = size;
    }
  }

  if (OB_SUCC(ret)) {
    ctxs_ = reinterpret_cast<ObColumnDecoderCtx*>(allocated_decoders_buf_);
    decoders_buf_pos = sizeof(ObColumnDecoderCtx) * decoder_ctx_cnt; 
  }

  return ret;
}

int ObIEncodeBlockReader::setup_row(const uint64_t row_id, int64_t &row_len, const char *&row_data)
{
  int ret = OB_SUCCESS;
  if (OB_UNLIKELY(nullptr == header_ || row_id >= header_->row_count_)) {
    ret = OB_INVALID_ARGUMENT;
  } else if (OB_FAIL(row_index_->get(row_id, row_data, row_len))) {
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
    } else if (OB_FAIL(get_micro_metas(header_, col_header_, meta_data_,
        block_data.get_buf(), block_data.get_buf_size()))) {
    } else if (typeid(ObRowkeyReadInfo) == typeid(read_info)) {
      ObObjMeta col_type;
      int64_t i = 0;
      for (; i < header_->column_count_; ++i) {
        col_type.set_type(static_cast<ObObjType>(col_header_[i].obj_type_));
        store_id_array_[i] = i;
        column_type_array_[i] = col_type;
      }
      for (; i < request_cnt; ++i) {
        col_type.set_type(ObNullType);
        store_id_array_[i] = i;
        column_type_array_[i] = col_type;
      }
    } else {
      const ObColDescIArray &cols_desc = read_info.get_columns_desc();
      const ObColumnIndexArray &cols_index = read_info.get_columns_index();
      for (int64_t idx = 0; idx < request_cnt; idx++) {
        store_id_array_[idx] = cols_index.at(idx);
        column_type_array_[idx] = cols_desc.at(idx).col_type_;
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
  const char *row_data = NULL;
  int64_t row_len = 0;
  int64_t row_id = -1;

  reuse();
  if (OB_FAIL(init_by_read_info(block_data, read_info))) {
  } else if (OB_FAIL(locate_row(rowkey, read_info, row_data, row_len, row_id, found))) {
  } else {
    row.row_flag_.reset();
    row.mvcc_row_flag_.reset();
    if (found) {
      if (OB_FAIL(get_all_columns(row_data, row_len, row_id, row))) {
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
    const char *row_data,
    const int64_t row_len,
    const int64_t row_id,
    ObDatumRow &row)
{
  if (OB_UNLIKELY(row.get_capacity() < request_cnt_)) {
    return OB_BUF_NOT_ENOUGH;
  }
  int ret = try_get_all_columns_rust(row_id, row);
  if (OB_NOT_SUPPORTED != ret) {
    return ret;
  }
  ret = OB_SUCCESS;
  ObBitStream bs(reinterpret_cast<unsigned char *>(const_cast<char *>(row_data)), row_len);
  for (int64_t i = 0; OB_SUCC(ret) && i < request_cnt_; ++i) {
    row.storage_datums_[i].reuse();
    if (OB_FAIL(decoders_[i].decode(row.storage_datums_[i], row_id, bs, row_data, row_len))) {
    }
  }
  return ret;
}

int ObEncodeBlockGetReader::try_get_all_columns_rust(const int64_t row_id, ObDatumRow &row)
{
  int ret = OB_NOT_SUPPORTED;
  if (row_id < 0 || row_id >= header_->row_count_ || row_id > UINT32_MAX
      || request_cnt_ <= 0 || nullptr == block_data_ || block_size_ <= 0) {
    return ret;
  }
  std::vector<uint32_t> columns;
  columns.reserve(request_cnt_);
  for (int64_t i = 0; i < request_cnt_; ++i) {
    const int64_t store_idx = store_id_array_[i];
    if (store_idx < 0 || store_idx >= header_->column_count_) {
      return ret;
    }
    const ObColumnHeader &column = col_header_[store_idx];
    if ((ObColumnHeader::RAW != column.type_ && ObColumnHeader::CONST != column.type_)
        || column.is_bit_packing()) {
      return ret;
    }
    switch (get_store_class_map()[ob_obj_type_class(column.get_store_obj_type())]) {
      case ObIntSC:
      case ObUIntSC:
      case ObNumberSC:
      case ObDecimalIntSC:
      case ObStringSC:
      case ObTextSC:
      case ObJsonSC:
      case ObGeometrySC:
        break;
      default:
        return ret;
    }
    columns.push_back(static_cast<uint32_t>(store_idx));
  }
  const uint32_t rust_row = static_cast<uint32_t>(row_id);
  const rust::Slice<const uint8_t> block(
      reinterpret_cast<const uint8_t *>(block_data_), static_cast<std::size_t>(block_size_));
  const rust::Slice<const uint32_t> rows(&rust_row, 1);
  const rust::Slice<const uint32_t> rust_columns(columns.data(), columns.size());
  micro_block::BatchResult result = micro_block::project_pax_block(block, rows, rust_columns);
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
  for (int64_t i = 0; OB_SUCC(ret) && i < request_cnt_; ++i) {
    const micro_block::CellDesc &cell = result.cells[i];
    ObStorageDatum &datum = row.storage_datums_[i];
    datum.reuse();
    if (1 == cell.kind) {
      datum.set_null();
    } else if (2 == cell.kind) {
      datum.set_nop();
    } else if (0 != cell.kind || cell.offset > static_cast<uint64_t>(block_size_)
               || cell.length > static_cast<uint64_t>(block_size_) - cell.offset) {
      ret = OB_INVALID_DATA;
    } else {
      const ObObjType obj_type = col_header_[columns[i]].get_store_obj_type();
      int64_t integer_mask = 0;
      if (ObIntTC == ob_obj_type_class(obj_type)) {
        integer_mask = ~INTEGER_MASK_TABLE[get_type_size_map()[obj_type]];
      }
      ret = load_data_to_datum(obj_type, block_data_ + cell.offset, cell.length, integer_mask, datum);
    }
  }
  return ret;
}

int ObEncodeBlockGetReader::locate_row(
    const ObDatumRowkey &rowkey,
    const ObITableReadInfo &read_info,
    const char *&row_data,
    int64_t &row_len,
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
    row_data = NULL;
    row_len = 0;
    row_id = -1;
    const int64_t rowkey_cnt = rowkey.get_datum_cnt();
    const ObStorageDatum *datums = rowkey.datums_;
    bool located_by_rust = false;
    bool equal = false;
    if (OB_FAIL(try_locate_rowkey_bound(
            block_data_, block_size_, header_, col_header_, rowkey, &read_info,
            true, true, 0, header_->row_count_, located_by_rust, row_id, equal))) {
    } else if (located_by_rust && equal) {
      found = true;
      ret = setup_row(row_id, row_len, row_data);
    }
    //binary search
    int32_t high = header_->row_count_ - 1;
    int32_t low = 0;
    int32_t middle = 0;
    int32_t cmp_result = 0;

    while (OB_SUCC(ret) && !located_by_rust && low <= high) {
      middle = (low + high) >> 1;
      cmp_result = 0;
      if (OB_FAIL(setup_row(middle, row_len, row_data))) {
      } else {
        ObBitStream bs(reinterpret_cast<unsigned char *>(const_cast<char *>(row_data)), row_len);
        for (int64_t i = 0; OB_SUCC(ret) && 0 == cmp_result && i < rowkey_cnt; ++i) {
          if (OB_FAIL(decoders_[i].quick_compare(datums[i], datum_utils.get_cmp_funcs().at(i),
                                                 middle, bs, row_data, row_len, cmp_result))) {
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
  if (OB_FAIL(prepare(MAX(schema_rowkey_cnt, request_cnt)))) {
  } else {
    int64_t idx = 0;
    for (idx = 0; idx < schema_rowkey_cnt; idx++) {
      store_id_array_[idx] = idx;
      column_type_array_[idx] = cols_desc.at(idx).col_type_;
    }
    for (; idx < request_cnt; ++idx) {
      store_id_array_[idx] = idx;
      column_type_array_[idx] = cols_desc.at(idx).col_type_;
    }
    if (OB_FAIL(get_micro_metas(header_, col_header_, meta_data_,
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
  const char *row_data = NULL;
  int64_t row_len = 0;
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
  } else if (OB_FAIL(locate_row(rowkey, read_info, row_data, row_len, row_id, found))) {
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
  const char *row_data = NULL;
  int64_t row_len = 0;

  reuse();
  if (OB_FAIL(init_by_read_info(block_data, read_info))) {
  } else if (OB_FAIL(setup_row(row_idx, row_len, row_data))) {
  } else {
    row.row_flag_.reset();
    row.mvcc_row_flag_.reset();
    if (OB_FAIL(get_all_columns(row_data, row_len, row_idx, row))) {
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
  const char *row_data = NULL;
  int64_t row_len = 0;

  reuse();
  if (OB_UNLIKELY(!read_info.is_valid())) {
    ret = OB_INVALID_ARGUMENT;
  } else if (OB_FAIL(init_by_read_info(block_data, read_info))) {
  } else if (OB_FAIL(locate_row(rowkey, read_info, row_data, row_len, row_id, found))) {
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
    meta_data_(nullptr),
    row_data_(nullptr),
    block_data_(nullptr),
    block_size_(0),
    var_row_index_(),
    fix_row_index_(),
    row_index_(&var_row_index_),
    decoders_(nullptr),
    flat_row_reader_(),
    ctxs_(nullptr),
    decoder_allocator_(ObModIds::OB_DECODER_CTX),
    buf_allocator_(ObMemAttr("OB_MICB_DECODER"), OB_MALLOC_NORMAL_BLOCK_SIZE),
    allocated_decoders_buf_(nullptr),
    allocated_decoders_buf_size_(0)
{
  reader_type_ = Decoder;
}

ObMicroBlockDecoder::~ObMicroBlockDecoder()
{
  reset();
}

template <typename Allocator>
int ObMicroBlockDecoder::acquire(
    Allocator &allocator,
    const ObMicroBlockHeader &header,
    const ObColumnHeader &col_header,
    const char *meta_data,
    const ObIColumnDecoder *&decoder)
{
  int ret = OB_SUCCESS;
  decoder = NULL;
  if (OB_UNLIKELY(!col_header.is_valid())) {
    ret = OB_INVALID_ARGUMENT;
  } else {
    switch (col_header.type_)
    {
      case ObColumnHeader::RAW: {
        ObRawDecoder *d = NULL;
        if (OB_FAIL(allocator.alloc(d))) {
        } else if (OB_FAIL(d->init(header, col_header, meta_data))) {
        } else {
          decoder = d;
        }
        break;
      }
      case ObColumnHeader::DICT: {
        ObDictDecoder *d = NULL;
        if (OB_FAIL(allocator.alloc(d))) {
        } else {
          if (OB_FAIL(d->init(header, col_header, meta_data))) {
          } else {
            decoder = d;
          }
        }
        break;
      }
      case ObColumnHeader::INTEGER_BASE_DIFF: {
        ObIntegerBaseDiffDecoder *d = NULL;
        if (OB_FAIL(allocator.alloc(d))) {
        } else {
          if (OB_FAIL(d->init(header, col_header, meta_data))) {
          } else {
            decoder = d;
          }
        }
        break;
      }
      case ObColumnHeader::STRING_DIFF: {
        ObStringDiffDecoder *d = NULL;
        if (OB_FAIL(allocator.alloc(d))) {
        } else if (OB_FAIL(d->init(header, col_header, meta_data))) {
        } else {
          decoder = d;
        }
        break;
      }
      case ObColumnHeader::HEX_PACKING: {
        ObHexStringDecoder *d = NULL;
        if (OB_FAIL(allocator.alloc(d))) {
        } else if (OB_FAIL(d->init(header, col_header, meta_data))) {
        } else {
          decoder = d;
        }
        break;
      }
      case ObColumnHeader::RLE: {
        ObRLEDecoder *d = NULL;
        if (OB_FAIL(allocator.alloc(d))) {
        } else if (OB_FAIL(d->init(header, col_header, meta_data))) {
        } else {
          decoder = d;
        }
        break;
      }
      case ObColumnHeader::CONST: {
        ObConstDecoder *d = NULL;
        if (OB_FAIL(allocator.alloc(d))) {
        } else if (OB_FAIL(d->init(header, col_header, meta_data))) {
        } else {
          decoder = d;
        }
        break;
      }
      case ObColumnHeader::STRING_PREFIX: {
        ObStringPrefixDecoder *d = NULL;
        if (OB_FAIL(allocator.alloc(d))) {
        } else if (OB_FAIL(d->init(header, col_header, meta_data))) {
        } else {
          decoder = d;
        }
        break;
      }
      case ObColumnHeader::COLUMN_EQUAL: {
        ObColumnEqualDecoder *d = NULL;
        if (OB_FAIL(allocator.alloc(d))) {
        } else if (OB_FAIL(d->init(header, col_header, meta_data))) {
        } else {
          decoder = d;
        }
        break;
      }
      case ObColumnHeader::COLUMN_SUBSTR: {
        ObInterColSubStrDecoder *d = NULL;
        if (OB_FAIL(allocator.alloc(d))) {
        } else if (OB_FAIL(d->init(header, col_header, meta_data))) {
        } else {
          decoder = d;
        }
        break;
      }
      default:
        ret = OB_INNER_STAT_ERROR;
    }
  }
  if (OB_FAIL(ret) && NULL != decoder) {
    allocator.free(const_cast<ObIColumnDecoder *>(decoder));
    decoder = NULL;
  }
  return ret;
}

// called before inited
// performance critical, do not check parameters
int ObMicroBlockDecoder::acquire(const int64_t store_idx,
                                 int64_t &decoders_buf_pos,
                                 const ObIColumnDecoder *&decoder)
{
  int ret = OB_SUCCESS;
  decoder = nullptr;
  const int64_t decoder_size = decoder_sizes[col_header_[store_idx].type_];
  if (OB_UNLIKELY(decoders_buf_pos + decoder_size > allocated_decoders_buf_size_)) {
    ret = OB_ERR_UNEXPECTED;
    LOG_ERROR("decoders buf not enough", K(ret), K(decoders_buf_pos),
        K(decoder_size), K(allocated_decoders_buf_size_));
  } else if (OB_FAIL(new_decoder_funcs_[col_header_[store_idx].type_](
      allocated_decoders_buf_ + decoders_buf_pos,
      *header_, col_header_[store_idx], meta_data_, decoder))) {
  } else {
    decoders_buf_pos += decoder_size;
  }
  return ret;
}

int ObMicroBlockDecoder::alloc_decoders_buf(const bool by_read_info, int64_t &decoders_buf_pos)
{
  int ret = OB_SUCCESS;
  int64_t size = 0;
  decoders_buf_pos = 0;

  if (OB_UNLIKELY(by_read_info && read_info_ == nullptr)) {
    ret = OB_ERR_UNEXPECTED;
  } else {
    size += request_cnt_ * sizeof(ObColumnDecoder); // for decoders_

    int64_t decoder_ctx_cnt = nullptr == read_info_ ? header_->column_count_ : MAX(header_->column_count_, 
        read_info_->get_schema_column_count() + storage::ObMultiVersionRowkeyHelpper::get_extra_rowkey_col_cnt());

    size += sizeof(ObColumnDecoderCtx) * decoder_ctx_cnt;  // for decoder ctxs

    const ObColumnIndexArray *cols_index = by_read_info ? &(read_info_->get_columns_index()) : nullptr;
    int64_t decoders_cnt = by_read_info ? request_cnt_ : header_->column_count_;
    for (int64_t i = 0; i < decoders_cnt; ++i) {
      const int64_t store_idx = by_read_info ? cols_index->at(i) : i;
      if (store_idx < 0 || store_idx >= header_->column_count_) {
        // non exist column
      } else {
        size += decoder_sizes[col_header_[store_idx].type_];
        if (col_header_[store_idx].type_ == ObColumnHeader::COLUMN_EQUAL ||
            col_header_[store_idx].type_ == ObColumnHeader::COLUMN_SUBSTR) {
          // for decoder which has ref deocoder, we don't know the exact type of ref decoder,
          // so use the maximum size.
          size += MAX_DECODER_SIZE;
        }
      }
    }
    if (size > allocated_decoders_buf_size_) {
      if (allocated_decoders_buf_size_ > 0) {
        allocated_decoders_buf_ = nullptr;
        allocated_decoders_buf_size_ = 0;
        buf_allocator_.reuse();
      }
      if (OB_ISNULL(allocated_decoders_buf_ = (char*)buf_allocator_.alloc(size))) {
        ret = OB_ALLOCATE_MEMORY_FAILED;
      } else {
        allocated_decoders_buf_size_ = size;
      }
    }
    if (OB_SUCC(ret)) {
      decoders_ = reinterpret_cast<ObColumnDecoder *>(allocated_decoders_buf_);
      decoders_buf_pos = request_cnt_ * sizeof(ObColumnDecoder);

      ctxs_ = reinterpret_cast<ObColumnDecoderCtx*>(allocated_decoders_buf_ + decoders_buf_pos);
      decoders_buf_pos += sizeof(ObColumnDecoderCtx) * decoder_ctx_cnt; 
    }
  }
  return ret;
}

void ObMicroBlockDecoder::inner_reset()
{
  header_ = NULL;
  col_header_ = NULL;
  meta_data_ = NULL;
  row_data_ = NULL;
  block_data_ = NULL;
  block_size_ = 0;
  ObIMicroBlockReader::reset();
  decoder_allocator_.reuse();
  flat_row_reader_.reset();
  ctxs_ = nullptr;
}

void ObMicroBlockDecoder::reset()
{
  inner_reset();
  request_cnt_ = 0;
  buf_allocator_.reset();
  decoder_allocator_.reset();
  allocated_decoders_buf_ = nullptr;
  allocated_decoders_buf_size_ = 0;
}

int ObMicroBlockDecoder::init_decoders()
{
  int ret = OB_SUCCESS;
  if (OB_UNLIKELY(NULL == header_ || NULL == col_header_ ||
     (NULL != read_info_ && !read_info_->is_valid()))) {
    ret = OB_INNER_STAT_ERROR;
  } else {
    // perfetch meta data
    /*
    const int64_t meta_len = row_data_ - meta_data_;
    for (int64_t i = 0; i < meta_len - CPU_CACHE_LINE_SIZE; i += CPU_CACHE_LINE_SIZE) {
      __builtin_prefetch(meta_data_ + i);
    }
    */
    int64_t decoders_buf_pos = 0;
    if (OB_ISNULL(read_info_) || typeid(ObRowkeyReadInfo) == typeid(*read_info_)) {
      ObObjMeta col_type;
      if (OB_UNLIKELY((header_->column_count_ < request_cnt_ && nullptr == read_info_) ||
                      (header_->column_count_ > request_cnt_))) {
        ret = OB_ERR_UNEXPECTED;
      } else if (OB_FAIL(alloc_decoders_buf(false/*by_read_info*/, decoders_buf_pos))) {
      } else {
        int64_t i = 0;
        for ( ; OB_SUCC(ret) && i < header_->column_count_; ++i) {
          col_type.set_type(static_cast<ObObjType>(col_header_[i].obj_type_));
          if (OB_FAIL(add_decoder(i, col_type, nullptr, decoders_buf_pos, decoders_[i]))) {
          }
        }
        for ( ; OB_SUCC(ret) && i < request_cnt_; ++i) { // add nop decoder for not-exist col
          decoders_[i].decoder_ = &none_exist_column_decoder_;
          decoders_[i].ctx_ = &none_exist_column_decoder_ctx_;
        }
      }
    } else if (OB_FAIL(alloc_decoders_buf(true/*by_read_info*/, decoders_buf_pos))) {
    } else {
      const ObColumnIndexArray &cols_index = read_info_->get_columns_index();
      const ObColDescIArray &cols_desc = read_info_->get_columns_desc();

      if (nullptr == read_info_->get_columns() || read_info_->get_columns()->count() < 1) {
        FOREACH_ADD_DECODER(nullptr)
      } else {
        const ObColumnParamIArray *cols_param = read_info_->get_columns();
        FOREACH_ADD_DECODER(cols_param->at(i))
      }
    }
  }
  return ret;
}

int ObMicroBlockDecoder::add_decoder(
    const int64_t store_idx, 
    const ObObjMeta &obj_meta, 
    const ObColumnParam *col_param, 
    int64_t &decoders_buf_pos,
    ObColumnDecoder &dest)
{
  int ret = OB_SUCCESS;
  if (store_idx < header_->column_count_ && !obj_meta.is_valid()) {
    ret = OB_INVALID_ARGUMENT;
  } else {
    if (store_idx < 0) {
      dest.decoder_ = &none_exist_column_decoder_;
      dest.ctx_ = &none_exist_column_decoder_ctx_;
    } else if (store_idx >= header_->column_count_) { // new added column
      dest.decoder_ = &new_column_decoder_;
      dest.ctx_ = &ctxs_[store_idx];
      dest.ctx_->fill_for_new_column(col_param, &decoder_allocator_.get_inner_allocator());
    } else {
      const ObIColumnDecoder *decoder = NULL;
      if (OB_FAIL(acquire(store_idx, decoders_buf_pos, decoder))) {
      } else {
        dest.decoder_ = decoder;
        dest.ctx_ = &ctxs_[store_idx];
        dest.ctx_->fill(obj_meta, header_, &col_header_[store_idx], &decoder_allocator_.get_inner_allocator(), store_idx);

        int64_t ref_col_idx = -1;
        if (OB_FAIL(decoder->get_ref_col_idx(ref_col_idx))) {
        }

        if (OB_SUCC(ret) && ref_col_idx >= 0) {
          if (OB_FAIL(acquire(ref_col_idx, decoders_buf_pos, decoder))) {
          } else {
            dest.ctx_->ref_decoder_ = decoder;
            dest.ctx_->ref_ctx_ = &ctxs_[ref_col_idx];
            dest.ctx_->ref_ctx_->fill(obj_meta, header_, &col_header_[ref_col_idx], &decoder_allocator_.get_inner_allocator(), ref_col_idx);
          }
        }
      }
    }
  }
  return ret;
}

int ObMicroBlockDecoder::get_micro_metas(
    const ObMicroBlockHeader *&header,
    const ObColumnHeader *&col_header,
    const char *&meta_data,
    const char *block,
    const int64_t block_size)
{
  int ret = OB_SUCCESS;
  if (nullptr == block || block_size <= 0) {
    ret = OB_INVALID_ARGUMENT;
  } else {
    header = reinterpret_cast<const ObMicroBlockHeader *>(block);
    block += header->header_size_;
    col_header = reinterpret_cast<const ObColumnHeader *>(block);
    meta_data = block + sizeof(*col_header) * header->column_count_;
    if (meta_data - block > block_size || header->row_data_offset_ > block_size) {
      ret = OB_INVALID_ARGUMENT;
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
  int16_t col_cnt = 0;
  if (OB_FAIL(get_micro_metas(header_, col_header_, meta_data_,
                                     block_data.get_buf(), block_data.get_buf_size()))) {
  } else {
    if (NULL == read_info_) {
      request_cnt_ = header_->column_count_;
    } else {
      request_cnt_ = read_info_->get_request_count();
    }
    row_count_ = header_->row_count_;
    original_data_length_ = header_->original_length_;
    row_data_ = block_data.get_buf() + header_->row_data_offset_;
    block_data_ = block_data.get_buf();
    block_size_ = block_data.get_buf_size();
    const int64_t row_data_len = block_data.get_buf_size() - header_->row_data_offset_;

    if (header_->row_index_byte_ > 0) {
      if (OB_FAIL(var_row_index_.init(row_data_, row_data_len,
                                      header_->row_count_, header_->row_index_byte_))) {
      }
      row_index_ = &var_row_index_;
    } else {
      if (OB_FAIL(fix_row_index_.init(row_data_, row_data_len, header_->row_count_))) {
      }
      row_index_ = &fix_row_index_;
    }
  }

  if (OB_SUCC(ret)) {
    if (OB_UNLIKELY(!header_->is_valid())) {
      ret = OB_INNER_STAT_ERROR;
      LOG_ERROR("invalid micro_block_header", K(ret), "header", *header_);
    } else if (OB_FAIL(init_decoders())) {
    } else {
      is_inited_ = true;
    }
  }
  if (OB_FAIL(ret)) {
    reset();
  }
  return ret;
}

int ObMicroBlockDecoder::decode_cells(const uint64_t row_id,
                                        const int64_t row_len,
                                        const char *row_data,
                                        const int64_t col_begin,
                                        const int64_t col_end,
                                        ObStorageDatum *datums)
{
  int ret = OB_SUCCESS;
  if (IS_NOT_INIT) {
    ret = OB_NOT_INIT;
  } else if (OB_UNLIKELY(col_begin < 0 || col_begin > col_end || col_end > request_cnt_)) {
    ret = OB_INVALID_ARGUMENT;
  } else {
    ObBitStream bs(reinterpret_cast<unsigned char *>(const_cast<char *>(row_data)), row_len);
    for (int64_t i = col_begin; OB_SUCC(ret) && i < col_end; ++i) {
      datums[i].reuse();
      if (OB_FAIL(decoders_[i].decode(datums[i], row_id, bs, row_data, row_len))) {
      }
    }
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
  } else if (OB_UNLIKELY(index >= row_count_ || !row.is_valid())) {
    ret = OB_INVALID_ARGUMENT;
  } else {
    int64_t row_len = 0;
    const char *row_data = NULL;
    if (OB_FAIL(row_index_->get(index, row_data, row_len))) {
    } else if (row.get_capacity() < request_cnt_) {
      ret = OB_BUF_NOT_ENOUGH;
    } else if (OB_FAIL(decode_cells(index, row_len, row_data, 0, request_cnt_, row.storage_datums_))) {
    } else {
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
  int64_t row_len = 0;
  const char *row_data = nullptr;
  if (OB_UNLIKELY(index  >= row_count_ || nullptr == datum_utils_)) {
    ret = OB_INVALID_ARGUMENT;
  } else if (OB_FAIL(row_index_->get(index, row_data, row_len))) {
  } else {
    const ObStorageDatumUtils &datum_utils = *datum_utils_;
    ObBitStream bs(reinterpret_cast<unsigned char *>(const_cast<char *>(row_data)), row_len);
    int64_t compare_column_count = rowkey.get_datum_cnt();
    if (OB_UNLIKELY(datum_utils.get_rowkey_count() < compare_column_count)) {
      ret = OB_ERR_UNEXPECTED;
      STORAGE_LOG(WARN, "Unexpected datum utils to compare rowkey", K(ret), K(compare_column_count), K(datum_utils));
    } else {
      ObStorageDatum store_datum;
      for (int64_t i = 0; OB_SUCC(ret) && i < compare_column_count && 0 == compare_result; ++i) {
        // before calling decode, datum ptr should point to the local buffer
        store_datum.reuse();
        if (OB_FAIL((decoders_ + i)->decode(store_datum, index, bs, row_data, row_len))) {
        } else if (OB_FAIL(datum_utils.get_cmp_funcs().at(i).compare(store_datum, rowkey.datums_[i], compare_result))) {
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
  int64_t row_len = 0;
  const char *row_data = nullptr;
  const ObDatumRowkey &start_rowkey = range.get_start_key();
  const ObDatumRowkey &end_rowkey = range.get_end_key();
  if (OB_UNLIKELY(index >= row_count_ || nullptr == datum_utils_)) {
    ret = OB_INVALID_ARGUMENT;
  } else if (OB_FAIL(row_index_->get(index, row_data, row_len))) {
  } else {
    const ObStorageDatumUtils &datum_utils = *datum_utils_;
    ObBitStream bs(reinterpret_cast<unsigned char *>(const_cast<char *>(row_data)), row_len);
    int64_t compare_column_count = start_rowkey.get_datum_cnt();
    if (OB_UNLIKELY(datum_utils.get_rowkey_count() < compare_column_count)) {
      ret = OB_ERR_UNEXPECTED;
      STORAGE_LOG(WARN, "Unexpected datum utils to compare rowkey", K(ret), K(compare_column_count), K(datum_utils));
    } else {
      ObStorageDatum store_datum;
      for (int64_t i = 0; OB_SUCC(ret) && i < compare_column_count && 0 == start_key_compare_result; ++i) {
        // before calling decode, datum ptr should point to the local buffer
        store_datum.reuse();
        if (OB_FAIL((decoders_ + i)->decode(store_datum, index, bs, row_data, row_len))) {
        } else if (OB_FAIL(datum_utils.get_cmp_funcs().at(i).compare(
                store_datum, start_rowkey.datums_[i], start_key_compare_result))) {
        } else {
          if (start_key_compare_result >= 0 && 0 == end_key_compare_result) {
            if (OB_FAIL(datum_utils.get_cmp_funcs().at(i).compare(
                    store_datum, end_rowkey.datums_[i], end_key_compare_result))) {
            }
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
  } else if (OB_UNLIKELY(nullptr == header_ || nullptr == row_index_ || nullptr == decoders_)) {
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
      } else if (!decoders_[column_index].decoder_->can_vectorized() &&
                 header_->is_trans_version_column_idx(column_index) &&
                 OB_FAIL(storage::reverse_trans_version_val(datums, active_count))) {
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
    ret = try_get_rows_rust(cols, row_ids, row_cap, datum_infos, datum_offset);
    if (OB_NOT_SUPPORTED == ret) {
      ret = OB_SUCCESS;
      for (int64_t i = 0; OB_SUCC(ret) && i < cols.count(); i++) {
        common::ObDatum *col_datums = datum_infos.at(i).datum_ptr_ + datum_offset;
        if (OB_FAIL(get_col_datums(cols.at(i), row_ids, cell_datas, row_cap, col_datums))) {
        }
      }
    }
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
    const int64_t row_cap,
    common::ObIArray<ObSqlDatumInfo> &datum_infos,
    const int64_t datum_offset)
{
  int ret = OB_NOT_SUPPORTED;
  if (row_cap < 0 || row_cap > UINT32_MAX || cols.count() <= 0
      || block_size_ <= 0 || nullptr == block_data_) {
    return ret;
  }
  std::vector<uint32_t> rust_rows;
  std::vector<uint32_t> rust_columns;
  rust_rows.reserve(row_cap);
  rust_columns.reserve(cols.count());
  for (int64_t i = 0; i < row_cap; ++i) {
    if (row_ids[i] < 0 || row_ids[i] >= row_count_) {
      return ret;
    }
    rust_rows.push_back(static_cast<uint32_t>(row_ids[i]));
  }
  for (int64_t i = 0; i < cols.count(); ++i) {
    const int32_t col_id = cols.at(i);
    if (col_id < 0 || col_id >= request_cnt_) {
      return ret;
    }
    const ObColumnHeader *column = decoders_[col_id].ctx_->col_header_;
    if (nullptr == column) {
      return ret;
    }
    const int64_t store_idx = column - col_header_;
    if (store_idx < 0 || store_idx >= header_->column_count_) {
      return ret;
    }
    if ((ObColumnHeader::RAW != column->type_ && ObColumnHeader::CONST != column->type_)
        || column->is_bit_packing()) {
      return ret;
    }
    switch (get_store_class_map()[ob_obj_type_class(column->get_store_obj_type())]) {
      case ObIntSC:
      case ObUIntSC:
      case ObNumberSC:
      case ObDecimalIntSC:
      case ObStringSC:
      case ObTextSC:
      case ObJsonSC:
      case ObGeometrySC:
        break;
      default:
        return ret;
    }
    rust_columns.push_back(static_cast<uint32_t>(store_idx));
  }

  const rust::Slice<const uint8_t> block(
      reinterpret_cast<const uint8_t *>(block_data_), static_cast<std::size_t>(block_size_));
  const rust::Slice<const uint32_t> rows(rust_rows.data(), rust_rows.size());
  const rust::Slice<const uint32_t> columns(rust_columns.data(), rust_columns.size());
  micro_block::BatchResult result = micro_block::project_pax_block(block, rows, columns);
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
  if (result.cells.size() != rust_rows.size() * rust_columns.size()) {
    return OB_INVALID_DATA;
  }
  for (int64_t col_idx = 0; OB_SUCC(ret) && col_idx < cols.count(); ++col_idx) {
    common::ObDatum *datums = datum_infos.at(col_idx).datum_ptr_ + datum_offset;
    const ObObjType obj_type = decoders_[cols.at(col_idx)].ctx_->col_header_->get_store_obj_type();
    int64_t integer_mask = 0;
    if (ObIntTC == ob_obj_type_class(obj_type)) {
      integer_mask = ~INTEGER_MASK_TABLE[get_type_size_map()[obj_type]];
    }
    for (int64_t row_idx = 0; OB_SUCC(ret) && row_idx < row_cap; ++row_idx) {
      const micro_block::CellDesc &cell = result.cells[row_idx * cols.count() + col_idx];
      common::ObDatum &datum = datums[row_idx];
      if (1 == cell.kind || 2 == cell.kind) {
        datum.set_null();
        datum.len_ = 0;
      } else if (0 != cell.kind || cell.offset > static_cast<uint64_t>(block_size_)
                 || cell.length > static_cast<uint64_t>(block_size_) - cell.offset) {
        ret = OB_INVALID_DATA;
      } else {
        const char *data = block_data_ + cell.offset;
        if (OB_FAIL(load_data_to_datum(obj_type, data, cell.length, integer_mask, datum))) {
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
  } else if (OB_FAIL(decoders_[col_id].get_row_count(
              row_index_,
              row_ids,
              row_cap,
              contains_null,
              count))) {
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
    ret = try_get_rows_rust(rust_cols, row_ids, row_cap, rust_infos, 0);
  }
  if (OB_NOT_SUPPORTED == ret) {
    ret = OB_SUCCESS;
  } else {
    return ret;
  }
  if (!decoders_[col_id].decoder_->can_vectorized()) {
    // normal path
    common::ObObj cell;
    int64_t row_len = 0;
    const char *row_data = NULL;
    const int row_header_size = ObRowHeader::get_serialized_size();
    int64_t row_id = common::OB_INVALID_INDEX;
    for (int64_t idx = 0; OB_SUCC(ret) && idx < row_cap; idx++) {
      row_id = row_ids[idx];
      if (OB_FAIL(row_index_->get(row_id, row_data, row_len))) {
      } else {
        ObBitStream bs(reinterpret_cast<unsigned char *>(const_cast<char *>(row_data)), row_len);
        if (OB_FAIL(decoders_[col_id].decode(col_datums[idx], row_id, bs, row_data, row_len))) {
        }
      }
    }
  } else if (OB_FAIL(decoders_[col_id].batch_decode(
              row_index_,
              row_ids,
              cell_datas,
              row_cap,
              col_datums))) {
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
  } else if (OB_UNLIKELY(col_offset >= header_->column_count_ || row_index >= row_count_)) {
    ret = OB_INVALID_ARGUMENT;
  } else {
    int64_t row_len = 0;
    const char *row_data = NULL;
    const int row_header_size = ObRowHeader::get_serialized_size();
    if (OB_FAIL(row_index_->get(row_index, row_data, row_len))) {
    } else {
      ObBitStream bs(reinterpret_cast<unsigned char *>(const_cast<char *>(row_data)), row_len);
      datum.reuse();
      if (OB_FAIL(decoders_[col_offset].decode(datum, row_index, bs, row_data, row_len))) {
      } else if (col_param.get_meta_type().is_lob_storage() && !datum.is_null() && !datum.get_lob_data().in_row_) {
        if (OB_FAIL(context.lob_locator_helper_->fill_lob_locator_v2(datum, col_param, iter_param, context))) {
        }
      }
    }
  }
  return ret;
}

int ObMicroBlockDecoder::get_distinct_count(const int32_t group_by_col, int64_t&distinct_cnt) const
{
  int ret = OB_SUCCESS;
  if (IS_NOT_INIT) {
    ret = OB_NOT_INIT;
  } else {
     ret = decoders_[group_by_col].get_distinct_count(distinct_cnt);
  }
  return ret;
}

int ObMicroBlockDecoder::read_distinct(
    const int32_t group_by_col,
    const char **cell_datas,
    const bool is_padding_mode,
    storage::ObGroupByCellBase &group_by_cell) const
{
  UNUSED(is_padding_mode);
  int ret = OB_SUCCESS;
  if (IS_NOT_INIT) {
    ret = OB_NOT_INIT;
  } else {
    if (OB_FAIL(decoders_[group_by_col].read_distinct(cell_datas, group_by_cell))) {
    }
  }
  return ret;
}

int ObMicroBlockDecoder::read_reference(
    const int32_t group_by_col,
    const int32_t *row_ids,
    const int64_t row_cap,
    storage::ObGroupByCellBase &group_by_cell) const
{
  int ret = OB_SUCCESS;
  if (IS_NOT_INIT) {
    ret = OB_NOT_INIT;
  } else if (OB_FAIL(decoders_[group_by_col].read_reference(row_ids, row_cap, group_by_cell))) {
  } else {
    group_by_cell.set_ref_cnt(row_cap);
  }
  return ret;
}

int ObMicroBlockDecoder::get_group_by_aggregate_result(
    const ObTableIterParam &iter_param,
    const ObTableAccessContext &context,
    const int32_t *row_ids,
    const char **cell_datas,
    const int64_t row_cap,
    storage::ObGroupByCell &group_by_cell)
{
  int ret = OB_SUCCESS;
  if (IS_NOT_INIT) {
    ret = OB_NOT_INIT;
  } else {
    const int32_t group_by_col = group_by_cell.get_group_by_col_offset();
    int32_t last_agg_col_offset = INT32_MIN;
    for (int64_t i = 0; OB_SUCC(ret) && i < group_by_cell.get_agg_cells().count(); ++i) {
      storage::ObAggCell *agg_cell =  group_by_cell.get_agg_cells().at(i);
      const int32_t agg_col_offset = agg_cell->get_col_offset();
      common::ObDatum *col_datums = agg_cell->get_col_datums();
      const share::schema::ObColumnParam *col_param = agg_cell->get_col_param();
      bool need_get_col_datum = (0 == i || agg_col_offset != last_agg_col_offset) && agg_cell->need_access_data();
      if (agg_col_offset == group_by_col) {
        // agg on group by column
        if (OB_FAIL(group_by_cell.eval_batch(group_by_cell.get_group_by_col_datums(), row_cap, i, true))) {
        }
      } else {
        if (need_get_col_datum) {
          if (OB_FAIL(get_col_datums(agg_col_offset, row_ids, cell_datas, row_cap, col_datums))) {
          } else if (iter_param.has_lob_column_out() && has_lob_out_row()
                    && nullptr != col_param && col_param->get_meta_type().is_lob_storage()
                    && OB_FAIL(fill_datums_lob_locator(iter_param, context, *col_param, row_cap, col_datums, false))) {
          }
        }
        if (OB_FAIL(ret)) {
        } else if (OB_FAIL(group_by_cell.eval_batch(col_datums, row_cap, i))) {
        } else if (need_get_col_datum) {
          last_agg_col_offset = agg_col_offset;
        }
      }
    }
  }
  return ret;
}

} // namespace blocksstable
} // namespace oceanbase
