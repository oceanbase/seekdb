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

#include "ob_integer_base_diff_decoder.h"
#include "ob_encoding_query_util.h"

namespace oceanbase
{
namespace blocksstable
{
using namespace common;
const ObColumnHeader::Type ObIntegerBaseDiffDecoder::type_;

int ObIntegerBaseDiffDecoder::decode(const ObColumnDecoderCtx &ctx, common::ObDatum &datum, const int64_t row_id,
    const ObBitStream &bs, const char *data, const int64_t len) const
{
  int ret = OB_SUCCESS;
  uint64_t val = STORED_NOT_EXT;
  const unsigned char *col_data = reinterpret_cast<const unsigned char *>(header_) + ctx.col_header_->length_;
  int64_t data_offset = 0;

  if (OB_UNLIKELY(!is_inited())) {
    ret = OB_NOT_INIT;
  } else if (OB_UNLIKELY(NULL == data || len < 0)) {
    ret = OB_INVALID_ARGUMENT;
  } else {
    // read extend value bit
    if (ctx.has_extend_value()) {
      data_offset = ctx.micro_block_header_->row_count_ * ctx.micro_block_header_->extend_value_bit_;
      if (OB_FAIL(ObBitStream::get(col_data, row_id * ctx.micro_block_header_->extend_value_bit_,
          ctx.micro_block_header_->extend_value_bit_, val))) {
      }
    }
  }
  if (OB_FAIL(ret)) {
  } else if (STORED_NOT_EXT != val) {
    set_stored_ext_value(datum, static_cast<ObStoredExtValue>(val));
  } else {
    uint32_t datum_len = 0;
    uint64_t v = 0;
    if (OB_FAIL(get_uint_data_datum_len(
        ObDatum::get_obj_datum_map_type(ctx.obj_meta_.get_type()),
        datum_len))){
    } else if (ctx.is_bit_packing()) {
      if (OB_FAIL(ObBitStream::get(col_data, data_offset + row_id * header_->length_,
          header_->length_, v))) {
      } else {
        v += base_;
        MEMCPY(const_cast<char *>(datum.ptr_), &v, datum_len);
        datum.pack_ = datum_len;
      }
    } else { // always fix length store
      data_offset = (data_offset + CHAR_BIT - 1) / CHAR_BIT;
      MEMCPY(&v, col_data + data_offset + row_id * header_->length_, header_->length_);
      v += base_;
      MEMCPY(const_cast<char *>(datum.ptr_), &v, datum_len);
      datum.pack_ = datum_len;
    }
  }
  return ret;
}

#define INT_DIFF_UNPACK_VALUES(ctx, row_ids, row_cap, datums, datum_len, data_offset, unpack_type) \
  int64_t row_id = 0; \
  bool has_ext_val = ctx.has_extend_value(); \
  int64_t bs_len = header_->length_ * ctx.micro_block_header_->row_count_; \
  int64_t value = 0; \
  const unsigned char *col_data = reinterpret_cast<const unsigned char *>(header_) \
                                  + ctx.col_header_->length_; \
  if (!has_ext_val) { \
    for (int64_t i = 0; i < row_cap; ++i) { \
      row_id = row_ids[i];  \
      value = 0; \
      ObBitStream::get<unpack_type>( \
          col_data, data_offset + row_id * header_->length_, header_->length_, \
          bs_len, value); \
      value += base_;  \
      ENCODING_ADAPT_MEMCPY(const_cast<char *>(datums[i].ptr_), &value, datum_len); \
      datums[i].pack_ = datum_len; \
    } \
  } else { \
    for (int64_t i = 0; i < row_cap; ++i) { \
      if (datums[i].is_null()) { \
      } else { \
        row_id = row_ids[i];  \
        value = 0; \
        ObBitStream::get<unpack_type>( \
            col_data, data_offset + row_id * header_->length_, header_->length_, \
            bs_len, value); \
        value += base_;  \
        ENCODING_ADAPT_MEMCPY(const_cast<char *>(datums[i].ptr_), &value, datum_len); \
        datums[i].pack_ = datum_len; \
      } \
    } \
  }

int ObIntegerBaseDiffDecoder::batch_get_bitpacked_values(
    const ObColumnDecoderCtx &ctx,
    const int32_t *row_ids,
    const int64_t row_cap,
    const int64_t datum_len,
    const int64_t data_offset,
    common::ObDatum *datums) const
{
  int ret = OB_SUCCESS;
  int64_t packed_len = header_->length_;
  if (packed_len < 10) {
    INT_DIFF_UNPACK_VALUES(
        ctx, row_ids, row_cap, datums, datum_len,
        data_offset, ObBitStream::PACKED_LEN_LESS_THAN_10)
  } else if (packed_len < 26) {
    INT_DIFF_UNPACK_VALUES(
        ctx, row_ids, row_cap, datums, datum_len,
        data_offset, ObBitStream::PACKED_LEN_LESS_THAN_26)
  } else if (packed_len <= 64) {
    INT_DIFF_UNPACK_VALUES(
        ctx, row_ids, row_cap, datums, datum_len, data_offset, ObBitStream::DEFAULT)
  } else {
    ret = OB_ERR_UNEXPECTED;
  }
  return ret;
}

#undef INT_DIFF_UNPACK_REFS

// Internal call, not check parameters for performance
// Potential optimization: SIMD batch add @base_ to packed delta values
int ObIntegerBaseDiffDecoder::batch_decode(
    const ObColumnDecoderCtx &ctx,
    const ObIRowIndex* row_index,
    const int32_t *row_ids,
    const char **cell_datas,
    const int64_t row_cap,
    common::ObDatum *datums) const
{
  UNUSEDx(row_index, cell_datas);
  int ret = OB_SUCCESS;
  if (OB_UNLIKELY(!is_inited())) {
    ret = OB_NOT_INIT;
  } else {
    int64_t data_offset = 0;
    const unsigned char *col_data = reinterpret_cast<const unsigned char *>(header_)
                                    + ctx.col_header_->length_;
    uint32_t datum_len = 0;
    if (ctx.has_extend_value()) {
      data_offset = ctx.micro_block_header_->row_count_
          * ctx.micro_block_header_->extend_value_bit_;
      if (OB_FAIL(set_null_datums_from_fixed_column(
          ctx, row_ids, row_cap, col_data, datums))) {
      }
    }

    if (OB_FAIL(ret)) {
    } else if (OB_FAIL(get_uint_data_datum_len(
        ObDatum::get_obj_datum_map_type(ctx.obj_meta_.get_type()),
        datum_len))) {
    } else if (ctx.is_bit_packing()) {
      if (OB_FAIL(batch_get_bitpacked_values(
          ctx, row_ids, row_cap, datum_len, data_offset, datums))) {
      }
    } else {
      // Fixed store data
      data_offset = (data_offset + CHAR_BIT - 1) / CHAR_BIT;
      int64_t row_id = 0;
      uint64_t value = 0;
      for (int64_t i = 0; i < row_cap; ++i) {
        if (ctx.has_extend_value() && datums[i].is_null()) {
          // Skip
        } else {
          row_id = row_ids[i];
          value = 0;
          MEMCPY(&value, col_data + data_offset + row_id * header_->length_, header_->length_);
          value += base_;
          MEMCPY(const_cast<char *>(datums[i].ptr_), &value, datum_len);
          datums[i].pack_ = datum_len;
        }
      }
    }
  }
  return ret;
}

int ObIntegerBaseDiffDecoder::get_null_count(
    const ObColumnDecoderCtx &ctx,
    const ObIRowIndex *row_index,
    const int32_t *row_ids,
    const int64_t row_cap,
    int64_t &null_count) const
{
  int ret = OB_SUCCESS;
  const char *col_data = reinterpret_cast<const char *>(header_) + ctx.col_header_->length_;
  if (OB_UNLIKELY(!is_inited())) {
    ret = OB_NOT_INIT;
  } else if OB_FAIL(ObIColumnDecoder::get_null_count_from_extend_value(
      ctx,
      row_index,
      row_ids,
      row_cap,
      col_data,
      null_count)) {
  }
  return ret;
}

} // end namespace blocksstable
} // end namespace oceanbase
