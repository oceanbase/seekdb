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

#include "ob_const_decoder.h"
#include "storage/access/ob_pushdown_aggregate.h"

namespace oceanbase
{
namespace blocksstable
{
using namespace common;
const ObColumnHeader::Type ObConstDecoder::type_;

int ObConstDecoder::decode_without_dict(
    const ObColumnDecoderCtx &ctx,
    ObDatum &datum,
    const bool need_deep_copy_number) const
{
  int ret = OB_SUCCESS;
  if (OB_UNLIKELY(!is_inited())) {
    ret = OB_NOT_INIT;
  } else {
    int64_t ref = meta_header_->const_ref_;
    if (0 == ref) {
      const int64_t len = ctx.col_header_->length_ - sizeof(*meta_header_);
      const ObObjType obj_type = ctx.col_header_->get_store_obj_type();
      const ObObjTypeClass tc = ob_obj_type_class(obj_type);
      uint64_t integer_mask;
      if (ObIntTC == tc) {
        int64_t type_store_size = get_type_size_map()[obj_type];
        integer_mask = ~INTEGER_MASK_TABLE[type_store_size];
      } else {
        integer_mask = 0;
      }

      if (ObNumberTC == tc && !need_deep_copy_number) {
        datum.ptr_ = meta_header_->payload_;
        datum.pack_ = static_cast<uint32_t>(len);
      } else if (OB_FAIL(load_data_to_datum(
          obj_type,
          meta_header_->payload_,
          len,
          integer_mask,
          datum))) {
      }
    } else if (1 == ref) { // null
      datum.set_null();
    } else if (2 == ref) { // nope
      datum.set_ext();
      datum.no_cv(datum.extend_obj_)->set_ext(common::ObActionFlag::OP_NOP);

    } else {
      ret = OB_ERR_UNEXPECTED;
    }
  }
  return ret;
}

int ObConstDecoder::decode(const ObColumnDecoderCtx &ctx, common::ObDatum &datum, const int64_t row_id,
                           const ObBitStream &bs, const char *data, const int64_t len) const
{
  UNUSEDx(bs, data, len);
  int ret = OB_SUCCESS;
  if (OB_UNLIKELY(!is_inited())) {
    ret = OB_NOT_INIT;
  } else if (0 == meta_header_->count_) {
    if (OB_FAIL(decode_without_dict(ctx, datum))) {
    }
  } else {
    int64_t ref = 0;
    // get ref value
    const ObIntArrayFuncTable &row_ids = ObIntArrayFuncTable::instance(meta_header_->row_id_byte_);
    const int64_t pos = row_ids.lower_bound_(
        meta_header_->payload_ + meta_header_->count_, 0, meta_header_->count_, row_id);
    if (pos == meta_header_->count_
        || row_id != row_ids.at_(meta_header_->payload_ + meta_header_->count_, pos)) {
      ref = meta_header_->const_ref_;
    } else {
      ref = reinterpret_cast<const uint8_t *>(meta_header_->payload_)[pos];
    }

    if (OB_SUCC(ret)) {
      const int64_t dict_meta_length = ctx.col_header_->length_ - meta_header_->offset_;
      if (OB_FAIL(dict_decoder_.decode(ctx.obj_meta_.get_type(), datum, ref, dict_meta_length))) {
      }
    }
  }
  return ret;
}

/**
 * Internal call, not check parameters for performance
 * row_ids should be monotonically increasing or decreasing
 *
 * Potential optimization:
 *  SIMD and batch load data for microblock without exceptions
 */
int ObConstDecoder::batch_decode(
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
  } else if (0 == meta_header_->count_) {
    if (OB_FAIL(batch_decode_without_dict(ctx, row_cap, datums))) {
    }
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

int ObConstDecoder::batch_decode_without_dict(
    const ObColumnDecoderCtx &ctx,
    const int64_t row_cap,
    common::ObDatum *datums) const
{
  int ret = OB_SUCCESS;
  if (OB_ISNULL(datums) || OB_UNLIKELY(0 >= row_cap)) {
    ret = OB_INVALID_ARGUMENT;
  } else {
    int64_t ref = meta_header_->const_ref_;
    if (0 == ref) {
      if (OB_FAIL(decode_without_dict(ctx, datums[0]))) {
      } else {
        switch (get_store_class_map()[ob_obj_type_class(ctx.obj_meta_.get_type())]) {
        case ObIntSC:
        case ObUIntSC:
        case ObOTimestampSC:
        case ObIntervalSC:
        case ObNumberSC: {
          for (int64_t i = 1; i < row_cap; ++i) {
            datums[i].pack_ = datums[0].pack_;
            MEMCPY(const_cast<char *>(datums[i].ptr_), datums[0].ptr_, datums[0].len_);
          }
          break;
        }
        case ObDecimalIntSC:
          for (int64_t i = 1; i < row_cap; ++i) {
            datums[i].pack_ = datums[0].pack_;
            MEMCPY(const_cast<char *>(datums[i].ptr_), datums[0].ptr_, datums[0].len_);
          }
          break;
        case ObStringSC:
        case ObTextSC:
        case ObJsonSC:
        case ObGeometrySC: {
            for (int64_t i = 1; i < row_cap; ++i) {
              datums[i].pack_ = datums[0].pack_;
              datums[i].ptr_ = datums[0].ptr_;
            }
          break;
        }
        default: {
          ret = OB_ERR_UNEXPECTED;
        }
        }
      }
    } else if (1 == ref) {
      for (int64_t i = 0; i < row_cap; ++i) {
        datums[i].set_null();
      }
    } else {
      ret = OB_ERR_UNEXPECTED;
    }
  }
  return ret;
}

int ObConstDecoder::get_null_count(
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
  } else if (0 == meta_header_->count_) {
    int64_t ref = meta_header_->const_ref_;
    if (ref > 0) {
      null_count += row_cap;
    }
  } else if OB_FAIL(extract_ref_and_null_count(row_ids, row_cap, nullptr, null_count)) {
  }
  return ret;
}

template<typename T>
int ObConstDecoder::extract_ref_and_null_count(
    const int32_t *row_ids,
    const int64_t row_cap,
    T len_arr,
    int64_t &null_count,
    uint32_t *ref_buf) const
{
  int ret = OB_SUCCESS;
  const int64_t count = meta_header_->count_;
  const int64_t const_ref = meta_header_->const_ref_;
  const ObIntArrayFuncTable &row_id_arr
      = ObIntArrayFuncTable::instance(meta_header_->row_id_byte_);

  int64_t row_id = 0;
  int64_t except_table_pos = 0;
  int64_t next_except_row_id = row_id_arr.at_(meta_header_->payload_ + count, except_table_pos);

  bool monotonic_inc = true;
  if (row_cap > 1) {
    monotonic_inc = row_ids[1] > row_ids[0];
  }
  int64_t step = monotonic_inc ? 1 : -1;
  int64_t trav_idx = monotonic_inc ? 0 : row_cap - 1;
  int64_t trav_cnt = 0;
  uint32_t ref;
  const int64_t dict_count = dict_decoder_.get_dict_header()->count_;
  while (trav_cnt < row_cap) {
    row_id = row_ids[trav_idx];
    // uint32_t *curr_ref = nullptr == datums ? (nullptr == ref_buf ?  &ref : &ref_buf[trav_idx]) : &datums[trav_idx].pack_;
    // uint32_t *curr_ref = nullptr == datums ? (nullptr == ref_buf ?  &ref : &ref_buf[trav_idx]) : &len_arr[trav_idx];
    uint32_t *curr_ref = get_len_by_type(len_arr, ref, ref_buf, trav_idx);
    if (except_table_pos == count || row_id < next_except_row_id) {
      *curr_ref = static_cast<uint32_t>(const_ref);
    } else if (row_id == next_except_row_id) {
      *curr_ref = reinterpret_cast<const uint8_t *>(meta_header_->payload_)[except_table_pos];
      ++except_table_pos;
      if (except_table_pos < count) {
        next_except_row_id = row_id_arr.at_(meta_header_->payload_ + count, except_table_pos);
      }
    } else {
      except_table_pos = row_id_arr.lower_bound_(
            meta_header_->payload_ + count, 0, count, row_id);
      next_except_row_id = row_id_arr.at_(meta_header_->payload_ + count, except_table_pos);
      if (except_table_pos == count || row_id != next_except_row_id) {
        *curr_ref= static_cast<uint32_t>(const_ref);
      } else {
        *curr_ref = reinterpret_cast<const uint8_t *>(meta_header_->payload_)[except_table_pos];
        ++except_table_pos;
        if (except_table_pos < count) {
          next_except_row_id = row_id_arr.at_(meta_header_->payload_ + count, except_table_pos);
        }
      }
    }
    if (*curr_ref >= dict_count) {
      null_count++;
    }
    ++trav_cnt;
    trav_idx += step;
  }
  return ret;
}

template<>
uint32_t *ObConstDecoder::get_len_by_type(std::nullptr_t vector, uint32_t &ref, uint32_t *ref_buf, const int64_t &trav_idx) const
{
  return nullptr == ref_buf ? &ref : &ref_buf[trav_idx];
}

template<>
uint32_t *ObConstDecoder::get_len_by_type(ObDatum *vector, uint32_t &ref, uint32_t *ref_buf, const int64_t &trav_idx) const
{
  return &vector[trav_idx].pack_;
}

template<>
uint32_t *ObConstDecoder::get_len_by_type(uint32_t *vector, uint32_t &ref, uint32_t *ref_buf, const int64_t &trav_idx) const
{
  return &vector[trav_idx];
}

int ObConstDecoder::get_distinct_count(int64_t &distinct_count) const
{
  int ret = OB_SUCCESS;
  distinct_count = 0 == meta_header_->count_ ? 1 : dict_decoder_.get_dict_header()->count_;
  return ret;
}

int ObConstDecoder::read_distinct(
    const ObColumnDecoderCtx &ctx,
    const char **cell_datas,
    storage::ObGroupByCellBase &group_by_cell) const
{
  int ret = OB_SUCCESS;
  if (0 == meta_header_->count_) {
    common::ObDatum *datums = group_by_cell.get_group_by_col_datums_to_fill();
    if (0 == meta_header_->const_ref_) {
      if (OB_FAIL(decode_without_dict(ctx, datums[0]))) {
      } else {
        group_by_cell.set_distinct_cnt(1);
      }
    } else if (OB_FAIL(group_by_cell.add_distinct_null_value())) {
    }
  } else if (OB_FAIL(dict_decoder_.batch_read_distinct(
      ctx,
      cell_datas,
      ctx.col_header_->length_ - meta_header_->offset_,
      group_by_cell))) {
  } else if (has_null_execption_value()) {
    group_by_cell.add_distinct_null_value();
  }
  return ret;
}

int ObConstDecoder::read_reference(
    const ObColumnDecoderCtx &ctx,
    const int32_t *row_ids,
    const int64_t row_cap,
    storage::ObGroupByCellBase &group_by_cell) const
{
  UNUSED(ctx);
  int ret = OB_SUCCESS;
  int64_t null_cnt = 0;
  uint32_t *ref_buf = group_by_cell.get_refs_buf();
  MEMSET(ref_buf, 0, sizeof(uint32_t) * row_cap);
  if (0 == meta_header_->count_) {
  } else if (OB_FAIL(extract_ref_and_null_count(row_ids, row_cap, nullptr, null_cnt,ref_buf))) {
  }
  return ret;
}

bool ObConstDecoder::has_null_execption_value() const
{
  int ret = OB_SUCCESS;
  bool has_null = false;
  const int64_t dict_count = dict_decoder_.get_dict_header()->count_;
  if (meta_header_->const_ref_ == dict_count) {
    has_null = true;
  } else {
    int64_t ref;
    for (int64_t pos = 0; !has_null && pos < meta_header_->count_; ++pos) {
      ref = reinterpret_cast<const uint8_t*>(meta_header_->payload_)[pos];
      has_null = ref >= dict_count;
    }
  }
  return has_null;
}

} // end namespace blocksstable
} // end namespace oceanbase
