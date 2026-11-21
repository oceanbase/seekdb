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

#include "ob_icolumn_decoder.h"
#include "ob_encoding_bitset.h"
#include "storage/access/ob_aggregate_base.h"

namespace oceanbase
{
namespace blocksstable
{
using namespace common;

int ObIColumnDecoder::set_null_datums_from_fixed_column(
    const ObColumnDecoderCtx &ctx,
    const int32_t *row_ids,
    const int64_t row_cap,
    const unsigned char *col_data,
    common::ObDatum *datums) const
{
  int ret = OB_SUCCESS;
  int64_t row_id = 0;
  uint64_t val = STORED_NOT_EXT;
  for (int64_t i = 0; OB_SUCC(ret) && i < row_cap; ++i) {
    row_id = row_ids[i];
    if (OB_FAIL(ObBitStream::get(col_data, row_id * ctx.micro_block_header_->extend_value_bit_,
        ctx.micro_block_header_->extend_value_bit_, val))) {
    } else {
      datums[i].null_ = STORED_NOT_EXT != val;
      datums[i].len_ = 0;
    }
  }
  return ret;
}

int ObIColumnDecoder::set_null_datums_from_var_column(
    const ObColumnDecoderCtx &ctx,
    const ObIRowIndex* row_index,
    const int32_t *row_ids,
    const int64_t row_cap,
    common::ObDatum *datums) const
{
  int ret = OB_SUCCESS;
  int64_t row_id = 0;
  uint64_t val = STORED_NOT_EXT;
  const char *row_data = nullptr;
  int64_t row_len = 0;
  for (int64_t i = 0; OB_SUCC(ret) && i < row_cap; ++i) {
    row_id = row_ids[i];
    if (OB_FAIL(locate_row_data(ctx, row_index, row_id, row_data, row_len))) {
    } else if (OB_FAIL(ObBitStream::get(
        reinterpret_cast<const unsigned char *>(row_data),
        ctx.col_header_->extend_value_index_,
        ctx.micro_block_header_->extend_value_bit_,
        val))) {
    } else {
      datums[i].null_ = STORED_NOT_EXT != val;
      datums[i].len_ = 0;
    }
  }
  return ret;
}

int ObIColumnDecoder::get_null_count(
    const ObColumnDecoderCtx &ctx,
    const ObIRowIndex *row_index,
    const int32_t *row_ids,
    const int64_t row_cap,
    int64_t &null_count) const
{
  // Default implementation, decode the column data and check whether is null
  int ret = OB_SUCCESS;
  null_count = 0;
  common::ObObj cell;
  ObStorageDatum datum;
  const char *row_data = NULL;
  int64_t row_len = 0;
  int64_t row_id = 0;
  for (int64_t i = 0; OB_SUCC(ret) && i < row_cap; ++i) {
    row_id = row_ids[i];
    if (OB_FAIL(row_index->get(row_id, row_data, row_len))) {
    }
    ObBitStream bs(reinterpret_cast<unsigned char *>(const_cast<char *>(row_data)), row_len);
    if (OB_FAIL(ret)) {
    } else if (OB_FAIL(decode(const_cast<ObColumnDecoderCtx &>(ctx), datum, row_id, bs, row_data, row_len))) {
    } else if (datum.is_null()) {
      null_count++;
    }
  }
  return ret;
}

int ObIColumnDecoder::get_null_count_from_fixed_column(
    const ObColumnDecoderCtx &ctx,
    const int32_t *row_ids,
    const int64_t row_cap,
    const unsigned char *col_data,
    int64_t &null_count) const
{
  int ret = OB_SUCCESS;
  int64_t row_id = 0;
  uint64_t val = STORED_NOT_EXT;
  for (int64_t i = 0; OB_SUCC(ret) && i < row_cap; ++i) {
    row_id = row_ids[i];
    if (OB_FAIL(ObBitStream::get(col_data, row_id * ctx.micro_block_header_->extend_value_bit_,
        ctx.micro_block_header_->extend_value_bit_, val))) {
    } else if (STORED_NOT_EXT != val) {
      null_count++;
    }
  }
  return ret;
}

int ObIColumnDecoder::get_null_count_from_var_column(
    const ObColumnDecoderCtx &ctx,
    const ObIRowIndex* row_index,
    const int32_t *row_ids,
    const int64_t row_cap,
    int64_t &null_count) const
{
  int ret = OB_SUCCESS;
  int64_t row_id = 0;
  uint64_t val = STORED_NOT_EXT;
  const char *row_data = nullptr;
  int64_t row_len = 0;
  for (int64_t i = 0; OB_SUCC(ret) && i < row_cap; ++i) {
    row_id = row_ids[i];
    if (OB_FAIL(locate_row_data(ctx, row_index, row_id, row_data, row_len))) {
    } else if (OB_FAIL(ObBitStream::get(
        reinterpret_cast<const unsigned char *>(row_data),
        ctx.col_header_->extend_value_index_,
        ctx.micro_block_header_->extend_value_bit_,
        val))) {
    } else if (STORED_NOT_EXT != val) {
      null_count++;
    }
  }
  return ret;
}

int ObIColumnDecoder::get_null_count_from_extend_value(
    const ObColumnDecoderCtx &ctx,
    const ObIRowIndex* row_index,
    const int32_t *row_ids,
    const int64_t row_cap,
    const char *meta_data_,
    int64_t &null_count) const
{
  int ret = OB_SUCCESS;
  null_count = 0;
  const unsigned char *col_data = reinterpret_cast<const unsigned char *>(meta_data_);
  if (ctx.has_extend_value()) {
    if (ctx.is_fix_length() || ctx.is_bit_packing()) {
      if (OB_FAIL(get_null_count_from_fixed_column(
          ctx, row_ids, row_cap, col_data, null_count))) {
      }
    } else if (OB_FAIL(get_null_count_from_var_column(
        ctx, row_index, row_ids, row_cap, null_count))) {
    }
  } else {
    null_count = 0;
  }
  return ret;
}

} // end of namespace oceanbase
} // end of namespace oceanbase
