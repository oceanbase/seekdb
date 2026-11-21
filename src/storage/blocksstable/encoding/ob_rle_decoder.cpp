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

#include "ob_rle_decoder.h"
#include "storage/access/ob_pushdown_aggregate.h"

namespace oceanbase
{
namespace blocksstable
{
using namespace common;
const ObColumnHeader::Type ObRLEDecoder::type_;

int ObRLEDecoder::decode(const ObColumnDecoderCtx &ctx, ObDatum &datum, const int64_t row_id,
    const ObBitStream &bs, const char *data, const int64_t len) const
{
  UNUSED(bs);
  UNUSED(data);
  UNUSED(len);
  int ret = OB_SUCCESS;
  if (OB_UNLIKELY(!is_inited())) {
    ret = OB_NOT_INIT;
  } else {
    // get ref value
    const int64_t pos = ObIntArrayFuncTable::instance(meta_header_->row_id_byte_)
        .upper_bound_(meta_header_->payload_, 0, meta_header_->count_, row_id);
    const int64_t ref = ObIntArrayFuncTable::instance(meta_header_->ref_byte_)
        .at_(meta_header_->payload_ + ref_offset_, pos - 1);

    if (OB_SUCC(ret)) {
      const int64_t dict_meta_length = ctx.col_header_->length_ - meta_header_->offset_;
      if (OB_FAIL(dict_decoder_.decode(ctx.obj_meta_.get_type(), datum, ref, dict_meta_length))) {
      }
    }
  }
  return ret;
}

// Internal call, not check parameters for performance
// row_ids should be monotonically increasing or decreasing
int ObRLEDecoder::batch_decode(
    const ObColumnDecoderCtx &ctx,
    const ObIRowIndex* row_index,
    const int32_t *row_ids,
    const char **cell_datas,
    const int64_t row_cap,
    common::ObDatum *datums) const
{
  UNUSED(row_index);
  int ret = OB_SUCCESS;
  int64_t unused_null_cnt;
  if (OB_UNLIKELY(!is_inited())) {
    ret = OB_NOT_INIT;
  } else if (OB_FAIL(extract_ref_and_null_count(row_ids, row_cap, datums, unused_null_cnt))) {
  } else if (OB_FAIL(dict_decoder_.batch_decode_dict(
      ctx.col_header_->get_store_obj_type(),
      cell_datas,
      row_cap,
      ctx.col_header_->length_ - meta_header_->offset_,
      datums))) {
  }
  return ret;
}

int ObRLEDecoder::get_null_count(
    const ObColumnDecoderCtx &ctx,
    const ObIRowIndex *row_index,
    const int32_t *row_ids,
    const int64_t row_cap,
    int64_t &null_count) const
{
  UNUSEDx(ctx, row_index);
  int ret = OB_SUCCESS;
  null_count = 0;
  if (OB_UNLIKELY(!is_inited())) {
    ret = OB_NOT_INIT;
  } else if (OB_FAIL(extract_ref_and_null_count(row_ids, row_cap, nullptr, null_count))) {
  }
  return ret;
}

template<typename T>
int ObRLEDecoder::extract_ref_and_null_count(
    const int32_t *row_ids,
    const int64_t row_cap,
    T ref_buf,
    int64_t &null_count) const
{
  int ret = OB_SUCCESS;
  const ObIntArrayFuncTable &row_id_array
      = ObIntArrayFuncTable::instance(meta_header_->row_id_byte_);
  const ObIntArrayFuncTable &ref_array
      = ObIntArrayFuncTable::instance(meta_header_->ref_byte_);
  const int64_t ref_count = meta_header_->count_;

  // Generate dict refs with minimum binary search call
  int64_t row_id = 0;
  int64_t ref_table_pos = 0;
  int64_t next_ref_row_id = row_id_array.at_(meta_header_->payload_, ref_table_pos);
  int64_t curr_ref = ref_array.at_(meta_header_->payload_ + ref_offset_, ref_table_pos);
  // Traverse @row_ids by direction that row_id is monotonically increasing
  bool monotonic_inc = true;
  if (row_cap > 1) {
    monotonic_inc = row_ids[1] > row_ids[0];
  }
  int64_t step = monotonic_inc ? 1 : -1;
  int64_t trav_idx = monotonic_inc ? 0 : row_cap - 1;
  int64_t trav_cnt = 0;
  const int64_t dict_count = dict_decoder_.get_dict_header()->count_;
  while (trav_cnt < row_cap) {
    row_id = row_ids[trav_idx];
    if (ref_table_pos == ref_count || row_id < next_ref_row_id) {
    } else if (row_id == next_ref_row_id) {
      ++ref_table_pos;
      if (ref_table_pos < ref_count) {
        next_ref_row_id = row_id_array.at_(meta_header_->payload_, ref_table_pos);
      }
      curr_ref = ref_array.at_(meta_header_->payload_ + ref_offset_, ref_table_pos - 1);
    } else {
      ref_table_pos =
          row_id_array.upper_bound_(meta_header_->payload_, 0, ref_count, row_id);
      if (ref_table_pos < ref_count) {
        next_ref_row_id = row_id_array.at_(meta_header_->payload_, ref_table_pos);
      }
      curr_ref = ref_array.at_(meta_header_->payload_ + ref_offset_, ref_table_pos - 1);
    }

    load_ref_to_buf(ref_buf, trav_idx, curr_ref);
    if (curr_ref >= dict_count) {
      null_count++;
    }

    ++trav_cnt;
    trav_idx += step;
  }
  return ret;
}

template<>
void ObRLEDecoder::load_ref_to_buf(std::nullptr_t ref_buf, const int64_t trav_idx, const uint32_t ref) const
{
  return;
}

template<>
void ObRLEDecoder::load_ref_to_buf(ObDatum *ref_buf, const int64_t trav_idx, const uint32_t ref) const
{
  ref_buf[trav_idx].pack_ = ref;
}

template<>
void ObRLEDecoder::load_ref_to_buf(uint32_t *ref_buf, const int64_t trav_idx, const uint32_t ref) const
{
  ref_buf[trav_idx] = ref;
}

int ObRLEDecoder::get_distinct_count(int64_t &distinct_count) const
{
  int ret = OB_SUCCESS;
  distinct_count = dict_decoder_.get_dict_header()->count_;
  LOG_DEBUG("[GROUP BY PUSHDOWN]", K(dict_decoder_.get_dict_header()->count_));
  return ret;
}

int ObRLEDecoder::read_distinct(
    const ObColumnDecoderCtx &ctx,
    const char **cell_datas,
    storage::ObGroupByCellBase &group_by_cell) const
{
  int ret = OB_SUCCESS;
  if (OB_FAIL(dict_decoder_.batch_read_distinct(
      ctx,
      cell_datas,
      ctx.col_header_->length_ - meta_header_->offset_,
      group_by_cell))) {
  } else if (has_null_value()) {
    group_by_cell.add_distinct_null_value();
  }
  return ret;
}

int ObRLEDecoder::read_reference(
    const ObColumnDecoderCtx &ctx,
    const int32_t *row_ids,
    const int64_t row_cap,
    storage::ObGroupByCellBase &group_by_cell) const
{
  int ret = OB_SUCCESS;
  int64_t null_cnt = 0;
  if (OB_FAIL(extract_ref_and_null_count(row_ids, row_cap, group_by_cell.get_refs_buf(), null_cnt))) {
  }
  return ret;
}

bool ObRLEDecoder::has_null_value() const
{
  int ret = OB_SUCCESS;
  bool has_null = false;
  int64_t ref;
  const int64_t dict_count = dict_decoder_.get_dict_header()->count_;
  const ObIntArrayFuncTable &refs = ObIntArrayFuncTable::instance(meta_header_->ref_byte_);
  for (int64_t i = 0; !has_null && i < meta_header_->count_; ++i) {
    ref = refs.at_(meta_header_->payload_ + ref_offset_, i);
    has_null = ref >= dict_count;
  }
  return has_null;
}

} // end namespace blocksstable
} // end namespace oceanbase
