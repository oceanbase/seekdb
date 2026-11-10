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

#define USING_LOG_PREFIX SQL_ENG
#include "sql/engine/px/p2p_datahub/ob_runtime_filter_msg.h"
#include "sql/engine/px/p2p_datahub/ob_p2p_dh_mgr.h"
#include "sql/engine/expr/ob_expr_hash.h"
#include "sql/engine/basic/ob_pushdown_filter.h"

#include "common/datum/datum_value_projection.h"
#include "common/datum/datum_parameter_codec.h"
#include "share/datum/typed_datum_compare.h"

using namespace oceanbase::common;
using namespace oceanbase::sql;
using namespace oceanbase::share;

namespace
{
int hash_native_filter_value(const ObExpr &expr, const Datum &value,
                              common::ObDatumHashFuncType function, uint64_t seed,
                              uint64_t &result, const ObDatumAccessContext *access)
{
  alignas(8) char scratch[64];
  ObDatum legacy;
  int ret = expr.get_datum_copy_plan().legacy_view(value, legacy, scratch, sizeof(scratch));
  if (OB_SUCC(ret)) { ret = function(legacy, seed, result, access); }
  return ret;
}
int compare_filter_values(const ObCmpFunc &function, const Datum &left,
                          const Datum &right, const SqlTypeMeta &meta,
                          int &result, const ObDatumAccessContext *access)
{
  alignas(8) char left_scratch[64], right_scratch[64];
  ObDatum left_view, right_view;
  int ret = DatumParameterCodec::export_native_value(left, meta, left_view,
      left_scratch, sizeof(left_scratch));
  if (OB_FAIL(ret)) {
  } else if (OB_FAIL(DatumParameterCodec::export_native_value(right, meta, right_view,
      right_scratch, sizeof(right_scratch)))) {
  } else if (OB_ISNULL(function.cmp_func_)) {
    ret = OB_ERR_UNEXPECTED;
  } else {
    ret = function.cmp_func_(left_view, right_view, result, access);
  }
  return ret;
}

int compare_probe_filter_value(const ObCmpFunc &function, const ObDatum &probe,
                               const Datum &bound, const SqlTypeMeta &build_meta,
                               int &result, const ObDatumAccessContext *access)
{
  alignas(8) char scratch[64];
  ObDatum view;
  int ret = DatumParameterCodec::export_native_value(bound, build_meta, view, scratch, sizeof(scratch));
  if (OB_SUCC(ret)) { ret = function.cmp_func_(probe, view, result, access); }
  return ret;
}

int project_owned_filter_value(const Datum &source, const SqlTypeMeta &source_meta,
                               const SqlTypeMeta &target_meta, DatumValue &target,
                               ObIAllocator &owner)
{
  alignas(8) char scratch[64];
  ObDatum legacy;
  DatumValue projected;
  int ret = DatumParameterCodec::export_native_value(source, source_meta, legacy, scratch, sizeof(scratch));
  if (OB_FAIL(ret)) {
  } else if (OB_FAIL(project_datum_value_view(legacy, target_meta,
      ObDatum::get_sql_value_datum_map_type(target_meta.get_type()), target.meta_, projected, true))) {
  } else {
    ret = clone_typed_value(projected, target, owner);
  }
  return ret;
}

int hash_native_filter_batch(const ObExpr &expr, common::ObDatumHashFuncType function,
                             uint64_t *results, const Datum *values,
                             const ObBitVector &skip, int64_t count,
                             const uint64_t *seeds, bool batch_seeds,
                             const ObDatumAccessContext *access)
{
  common::DatumHashOp operation;
  int ret = common::ObDatumFuncs::bind_legacy_hash(expr.get_sql_type(), function, operation);
  if (OB_SUCC(ret)) { ret = operation(results, values, expr.is_batch_result(), skip,
                                      count, seeds, batch_seeds, access); }
  return ret;
}
// This scanner is specific to the existing RF v1 nested records. It validates
// the entire body before any legacy deserializer or function can observe it.
// In particular the generic varint reader must never see an incomplete varint.
int rf_integer(const char *buf, int64_t length, int64_t &pos, int64_t &out, int bits = 64)
{
  if (buf == nullptr || pos < 0 || pos >= length) { return OB_DESERIALIZE_ERROR; }
  uint64_t value = 0;
  const int64_t start = pos;
  const int limit = (bits + 6) / 7;
  for (int i = 0; i < limit && start + i < length; ++i) {
    const uint8_t byte = static_cast<uint8_t>(buf[start + i]);
    const int shift = i * 7;
    if (shift + 7 > bits && (byte & 0x7f) >= (1U << (bits - shift))) {
      return OB_DESERIALIZE_ERROR;
    }
    value |= static_cast<uint64_t>(byte & 0x7f) << shift;
    if ((byte & 0x80) == 0) { out = static_cast<int64_t>(value); pos = start + i + 1; return OB_SUCCESS; }
  }
  return OB_DESERIALIZE_ERROR;
}

int rf_envelope(const char *buf, int64_t length, int64_t &pos,
                int64_t &version, int64_t &begin, int64_t &end)
{
  int64_t cursor = pos, bytes = 0;
  int ret = rf_integer(buf, length, cursor, version);
  if (OB_FAIL(ret)) {
  } else if (OB_FAIL(rf_integer(buf, length, cursor, bytes))) {
  } else if (bytes < 0 || bytes > length - cursor) {
    ret = OB_DESERIALIZE_ERROR;
  } else { begin = cursor; end = cursor + bytes; pos = end; }
  return ret;
}

int rf_count(const char *buf, int64_t length, int64_t &pos, int64_t &count, int64_t minimum = 1)
{
  int ret = rf_integer(buf, length, pos, count);
  if (OB_SUCC(ret) && (count < 0 || count > UINT32_MAX || count > (length - pos) / minimum)) {
    ret = OB_DESERIALIZE_ERROR;
  }
  return ret;
}

int rf_numbers_record(const char *buf, int64_t length, int64_t &pos, int64_t count,
                      bool nonnegative = false)
{
  int64_t version = 0, cursor = 0, end = 0, value = 0;
  int ret = rf_envelope(buf, length, pos, version, cursor, end);
  if (OB_SUCC(ret) && version != 1) { ret = OB_NOT_SUPPORTED; }
  for (int64_t i = 0; OB_SUCC(ret) && i < count; ++i) {
    if (OB_FAIL(rf_integer(buf, end, cursor, value))) {
    } else if (nonnegative && value < 0) { ret = OB_DESERIALIZE_ERROR; }
  }
  if (OB_SUCC(ret) && cursor != end) { ret = OB_DESERIALIZE_ERROR; }
  return ret;
}

int rf_base_record(const char *buf, int64_t length, int64_t &pos)
{
  int64_t version = 0, cursor = 0, end = 0, value = 0;
  int ret = rf_envelope(buf, length, pos, version, cursor, end);
  if (OB_SUCC(ret) && version != 1) { ret = OB_NOT_SUPPORTED; }
  if (OB_SUCC(ret)) { ret = rf_numbers_record(buf, end, cursor, 4); } // TraceId
  for (int i = 0; OB_SUCC(ret) && i < 5; ++i) { ret = rf_integer(buf, end, cursor, value, i == 4 ? 32 : 64); }
  if (OB_SUCC(ret) && end - cursor != 2) { ret = OB_DESERIALIZE_ERROR; }
  if (OB_SUCC(ret)) {
    if (static_cast<uint8_t>(buf[cursor]) > 1 || static_cast<uint8_t>(buf[cursor + 1]) > 1) {
      ret = OB_DESERIALIZE_ERROR;
    }
  }
  return ret;
}

int rf_cell(const char *buf, int64_t length, int64_t &pos, ObDatum *output = nullptr)
{
  int64_t version = 0, cursor = 0, end = 0, pack = 0;
  int ret = rf_envelope(buf, length, pos, version, cursor, end);
  if (OB_SUCC(ret) && version != 1) { ret = OB_NOT_SUPPORTED; }
  if (OB_SUCC(ret)) { ret = rf_integer(buf, end, cursor, pack, 32); }
  if (OB_SUCC(ret)) {
    ObDatum cell;
    cell.pack_ = static_cast<uint32_t>(pack);
    cell.ptr_ = buf + cursor;
    if (cell.len_ != end - cursor || (cell.is_ext()
        && cell.len_ != DATUM_EXTEND_PAYLOAD_SIZE && cell.len_ != DATUM_LEGACY_EXTEND_SIZE)) {
      ret = OB_DESERIALIZE_ERROR;
    } else if (output != nullptr) { *output = cell; }
  }
  return ret;
}

int rf_cells(const char *buf, int64_t length, int64_t &pos, int64_t &count)
{
  int ret = rf_count(buf, length, pos, count, 3);
  for (int64_t i = 0; OB_SUCC(ret) && i < count; ++i) { ret = rf_cell(buf, length, pos); }
  return ret;
}

int rf_record_array(const char *buf, int64_t length, int64_t &pos,
                    int64_t &count, int64_t numbers, bool nonnegative = false)
{
  int ret = rf_count(buf, length, pos, count, 2 + numbers);
  for (int64_t i = 0; OB_SUCC(ret) && i < count; ++i) {
    ret = rf_numbers_record(buf, length, pos, numbers, nonnegative);
  }
  return ret;
}

int rf_flags(const char *buf, int64_t length, int64_t &pos, int64_t &count)
{
  int ret = rf_count(buf, length, pos, count);
  for (int64_t i = 0; OB_SUCC(ret) && i < count; ++i) {
    if (static_cast<uint8_t>(buf[pos++]) > 1) { ret = OB_DESERIALIZE_ERROR; }
  }
  return ret;
}

int rf_metas(const char *buf, int64_t length, int64_t &pos, int64_t &count)
{
  int ret = rf_count(buf, length, pos, count, 4);
  for (int64_t i = 0; OB_SUCC(ret) && i < count; ++i) {
    // SqlTypeMeta is exactly four i8 fields in this physical protocol, not a
    // semantic SqlType. Validate the kind even for all-NULL/empty messages.
    if (static_cast<uint8_t>(buf[pos]) >= ObMaxType) { ret = OB_DESERIALIZE_ERROR; }
    pos += 4;
  }
  return ret;
}

// SQL range columns are index/rowkey columns, not storage MVCC columns.
// Empty-prefix query info uses -1 by default and never allocates a rowkey.
bool rf_valid_query_columns(int64_t range_columns, int64_t prefix_columns)
{
  return prefix_columns >= 0 && range_columns >= -1
      && range_columns <= OB_MAX_ROWKEY_COLUMN_NUMBER
      && range_columns <= INT64_MAX / static_cast<int64_t>(sizeof(DatumValue))
      && (prefix_columns == 0 || range_columns >= prefix_columns);
}

int rf_query_info(const char *buf, int64_t length, int64_t &pos, int64_t columns)
{
  int64_t version = 0, cursor = 0, end = 0, value = 0, range_columns = 0;
  int64_t indices = 0, metas = 0;
  int ret = rf_envelope(buf, length, pos, version, cursor, end);
  if (OB_SUCC(ret) && version != 1) { ret = OB_NOT_SUPPORTED; }
  if (OB_FAIL(ret)) {
  } else if (OB_FAIL(rf_integer(buf, end, cursor, value))) { // table ID
  } else if (OB_FAIL(rf_integer(buf, end, cursor, range_columns))) {
  } else if (OB_FAIL(rf_count(buf, end, cursor, indices))) {
  }
  for (int64_t i = 0; OB_SUCC(ret) && i < indices; ++i) {
    if (OB_FAIL(rf_integer(buf, end, cursor, value))) {
    } else if (value < 0 || value >= columns) { ret = OB_DESERIALIZE_ERROR; }
  }
  if (OB_FAIL(ret)) {
  } else if (OB_FAIL(rf_metas(buf, end, cursor, metas))) {
  } else if (metas != indices || cursor != end || !rf_valid_query_columns(range_columns, indices)) {
    ret = OB_DESERIALIZE_ERROR;
  }
  return ret;
}

struct FilterWireLayout
{
  int64_t lower_begin_ = 0;
  int64_t upper_begin_ = 0;
  int64_t rows_begin_ = 0;
  int64_t query_begin_ = 0;
  int64_t meta_begin_ = 0;
  int64_t columns_ = 0;
  int64_t rows_ = 0;
  bool has_metadata_ = false;
};

int rf_preflight(const char *buf, int64_t length, bool range, FilterWireLayout &layout)
{
  int64_t pos = 0, flags = 0, compares = 0, hashes = 0, capacities = 0, columns = 0, maximum = 0;
  int ret = rf_base_record(buf, length, pos);
  if (range) {
    layout.lower_begin_ = pos;
    if (OB_SUCC(ret)) { ret = rf_cells(buf, length, pos, layout.columns_); }
    layout.upper_begin_ = pos;
    if (OB_SUCC(ret)) { ret = rf_cells(buf, length, pos, columns); }
    if (OB_SUCC(ret)) { ret = rf_flags(buf, length, pos, flags); }
    if (OB_SUCC(ret)) { ret = rf_record_array(buf, length, pos, capacities, 2, true); }
    if (OB_SUCC(ret)) { ret = rf_record_array(buf, length, pos, compares, 1); }
    if (OB_SUCC(ret) && (columns != layout.columns_ || flags != columns || capacities != columns || compares != columns)) {
      ret = OB_DESERIALIZE_ERROR;
    }
  } else {
    if (OB_SUCC(ret)) { ret = rf_count(buf, length, pos, layout.rows_); }
    if (OB_SUCC(ret)) { ret = rf_record_array(buf, length, pos, compares, 1); }
    if (OB_SUCC(ret)) { ret = rf_record_array(buf, length, pos, hashes, 2); }
    if (OB_SUCC(ret)) { ret = rf_integer(buf, length, pos, layout.columns_); }
    if (OB_SUCC(ret)) { ret = rf_integer(buf, length, pos, maximum); }
    if (OB_SUCC(ret)) { ret = rf_flags(buf, length, pos, flags); }
    if (OB_SUCC(ret) && (layout.columns_ < 0 || layout.columns_ > UINT32_MAX || maximum < 0
        || flags != layout.columns_ || compares != flags || hashes != flags)) { ret = OB_DESERIALIZE_ERROR; }
    layout.rows_begin_ = pos;
    for (int64_t i = 0; OB_SUCC(ret) && i < layout.rows_; ++i) {
      if (OB_FAIL(rf_cells(buf, length, pos, columns))) {
      } else if (columns != layout.columns_) { ret = OB_DESERIALIZE_ERROR; }
    }
  }
  layout.query_begin_ = pos;
  if (OB_SUCC(ret)) { ret = rf_query_info(buf, length, pos, layout.columns_); }
  layout.meta_begin_ = pos;
  layout.has_metadata_ = pos != length;
  if (OB_SUCC(ret) && layout.has_metadata_) {
    if (OB_FAIL(rf_metas(buf, length, pos, columns))) {
    } else if (columns != layout.columns_) { ret = OB_DESERIALIZE_ERROR; }
  }
  if (OB_SUCC(ret) && pos != length) { ret = OB_DESERIALIZE_ERROR; }
  return ret;
}

int rf_encode_row(const ObIArray<Datum> &row, const ObIArray<SqlTypeMeta> &metas,
                  char *buf, int64_t length, int64_t &pos)
{
  int ret = row.count() != metas.count() ? OB_ERR_UNEXPECTED
      : serialization::encode_vi64(buf, length, pos, row.count());
  for (int64_t i = 0; OB_SUCC(ret) && i < row.count(); ++i) {
    alignas(8) char scratch[64];
    ObDatum legacy;
    if (OB_FAIL(DatumParameterCodec::export_native_value(row.at(i), metas.at(i), legacy, scratch, sizeof(scratch)))) {
    } else if (OB_FAIL(legacy.serialize(buf, length, pos))) {
    }
  }
  return ret;
}

int64_t rf_row_size(const ObIArray<Datum> &row, const ObIArray<SqlTypeMeta> &metas)
{
  OB_ASSERT(row.count() == metas.count());
  int64_t size = serialization::encoded_length_vi64(row.count());
  for (int64_t i = 0; i < row.count(); ++i) {
    alignas(8) char scratch[64];
    ObDatum legacy;
    const int ret = DatumParameterCodec::export_native_value(row.at(i), metas.at(i), legacy, scratch, sizeof(scratch));
    OB_ASSERT(ret == OB_SUCCESS);
    size += legacy.get_serialize_size();
  }
  return size;
}

int rf_decode_row(const char *buf, int64_t length, int64_t &pos,
                  const ObIArray<SqlTypeMeta> &metas, ObIAllocator &owner,
                  ObFixedArray<Datum, ObIAllocator> &row, bool unused_legacy_bounds = false)
{
  int64_t count = 0;
  int ret = rf_count(buf, length, pos, count, 3);
  if (OB_FAIL(ret)) {
  } else if (count != metas.count()) { ret = OB_DESERIALIZE_ERROR;
  } else if (OB_FAIL(row.prepare_allocate(count))) {
  }
  for (int64_t i = 0; OB_SUCC(ret) && i < count; ++i) {
    ObDatum legacy;
    if (OB_FAIL(rf_cell(buf, length, pos, &legacy))) {
    } else if (unused_legacy_bounds) {
      // Old range constructors serialized unused, present len=0 slots before
      // the first insert. Consume their validated record without interpreting
      // it as a typed value; an empty range never reads these endpoints.
      row.at(i).set_null();
    } else if (OB_FAIL(DatumParameterCodec::import_native_value(legacy, metas.at(i), owner, row.at(i)))) {
    }
  }
  return ret;
}

}

OB_SERIALIZE_MEMBER(ObRFRangeFilterMsg::MinMaxCellSize, min_datum_buf_size_, max_datum_buf_size_);

OB_DEF_SERIALIZE(ObRFBloomFilterMsg)
{
  int ret = OB_SUCCESS;
  BASE_SER((ObRFBloomFilterMsg, ObP2PDatahubMsgBase));
  LST_DO_CODE(OB_UNIS_ENCODE,
              bloom_filter_,
              use_hash_join_seed_);
  return ret;
}

OB_DEF_DESERIALIZE(ObRFBloomFilterMsg)
{
  int ret = OB_SUCCESS;
  BASE_DESER((ObRFBloomFilterMsg, ObP2PDatahubMsgBase));
  
  bloom_filter_.allocator_.set_label("ObPxBFDESER");

  LST_DO_CODE(OB_UNIS_DECODE,
              bloom_filter_,
              use_hash_join_seed_);
  return ret;
}

OB_DEF_SERIALIZE_SIZE(ObRFBloomFilterMsg)
{
  int64_t len = 0;
  BASE_ADD_LEN((ObRFBloomFilterMsg, ObP2PDatahubMsgBase));
  LST_DO_CODE(OB_UNIS_ADD_LEN,
              bloom_filter_,
              use_hash_join_seed_);
  return len;
}

OB_DEF_SERIALIZE(ObRFRangeFilterMsg)
{
  int ret = OB_SUCCESS;
  if (!rf_valid_query_columns(query_range_info_.range_column_cnt_,
                               query_range_info_.prefix_col_idxs_.count())) {
    return OB_INVALID_ARGUMENT;
  }
  BASE_SER((ObRFRangeFilterMsg, ObP2PDatahubMsgBase));
  if (OB_SUCC(ret)) { ret = rf_encode_row(lower_bounds_, build_obj_metas_, buf, buf_len, pos); }
  if (OB_SUCC(ret)) { ret = rf_encode_row(upper_bounds_, build_obj_metas_, buf, buf_len, pos); }
  LST_DO_CODE(OB_UNIS_ENCODE, need_null_cmp_flags_, cells_size_, cmp_funcs_, query_range_info_, build_obj_metas_);
  return ret;
}

OB_DEF_SERIALIZE_SIZE(ObRFRangeFilterMsg)
{
  int64_t len = 0;
  BASE_ADD_LEN((ObRFRangeFilterMsg, ObP2PDatahubMsgBase));
  len += rf_row_size(lower_bounds_, build_obj_metas_) + rf_row_size(upper_bounds_, build_obj_metas_);
  LST_DO_CODE(OB_UNIS_ADD_LEN, need_null_cmp_flags_, cells_size_, cmp_funcs_, query_range_info_, build_obj_metas_);
  return len;
}

int ObRFRangeFilterMsg::deserialize(const char *buf, const int64_t data_len, int64_t &pos)
{
  int64_t cursor = pos, version = 0, begin = 0, end = 0;
  int ret = rf_envelope(buf, data_len, cursor, version, begin, end);
  if (OB_SUCC(ret) && version != 1 && version != 2) { ret = OB_NOT_SUPPORTED; }
  if (OB_SUCC(ret)) {
    int64_t body_pos = 0;
    ret = deserialize_body(buf + begin, end - begin, body_pos, version);
  }
  if (OB_SUCC(ret)) { pos = end; }
  return ret;
}

int ObRFRangeFilterMsg::deserialize_(const char *buf, const int64_t data_len, int64_t &pos)
{
  return deserialize_body(buf, data_len, pos, 2);
}

int ObRFRangeFilterMsg::deserialize_body(const char *buf, int64_t data_len,
                                        int64_t &pos, int64_t version)
{
  FilterWireLayout layout;
  int64_t cursor = pos;
  if (buf == nullptr || cursor < 0 || cursor > data_len) { return OB_DESERIALIZE_ERROR; }
  const char *body = buf + cursor;
  const int64_t length = data_len - cursor;
  int ret = rf_preflight(body, length, true, layout);
  // A message is deserialized before the datahub publishes it. A ready object
  // may already have readers and must not be mutated under them.
  if (OB_SUCC(ret) && is_ready_) { ret = OB_STATE_NOT_MATCH; }
  if (OB_SUCC(ret) && !layout.has_metadata_ && version != 1) { ret = OB_DESERIALIZE_ERROR; }
  if (OB_FAIL(ret)) {
    // Preflight errors do not touch an existing object.
  } else {
    (void)destroy();
    int64_t p = 0;
    if (OB_FAIL(ObP2PDatahubMsgBase::deserialize(body, length, p))) {
    } else if (get_msg_type() != RANGE_FILTER_MSG) { ret = OB_DESERIALIZE_ERROR;
    } else if (!layout.has_metadata_) {
      // Only an exact, complete old-v1 optional-tail omission reaches here.
      is_active_ = false;
      is_empty_ = true;
    } else {
      const bool active = is_active_;
      const bool unused_legacy_bounds = version == 1 && is_empty_;
      is_active_ = false;
      int64_t meta_pos = layout.meta_begin_;
      if (OB_FAIL(build_obj_metas_.deserialize(body, length, meta_pos))) {
      } else if (OB_FAIL(rf_decode_row(body, length, p, build_obj_metas_, allocator_, lower_bounds_, unused_legacy_bounds))) {
      } else if (OB_FAIL(rf_decode_row(body, length, p, build_obj_metas_, allocator_, upper_bounds_, unused_legacy_bounds))) {
      } else if (OB_FAIL(need_null_cmp_flags_.deserialize(body, length, p))) {
      } else if (OB_FAIL(cells_size_.deserialize(body, length, p))) {
      } else if (OB_FAIL(cmp_funcs_.deserialize(body, length, p))) {
      } else if (OB_FAIL(query_range_info_.deserialize(body, length, p))) {
      } else if (OB_FAIL(adjust_cell_size())) {
      }
      for (int64_t i = 0; OB_SUCC(ret) && i < cmp_funcs_.count(); ++i) {
        if (OB_ISNULL(cmp_funcs_.at(i).cmp_func_)) { ret = OB_DESERIALIZE_ERROR; }
      }
      if (OB_SUCC(ret)) { is_active_ = active; }
    }
    is_ready_ = false;
    if (OB_FAIL(ret)) {
      (void)destroy();
      is_active_ = false;
      is_empty_ = true;
    } else { pos = data_len; }
  }
  return ret;
}

OB_DEF_SERIALIZE(ObRFInFilterMsg)
{
  int ret = OB_SUCCESS;
  if (!rf_valid_query_columns(query_range_info_.range_column_cnt_,
                               query_range_info_.prefix_col_idxs_.count())) {
    return OB_INVALID_ARGUMENT;
  }
  BASE_SER((ObRFInFilterMsg, ObP2PDatahubMsgBase));
  const int64_t count = is_active_ ? serial_rows_.count() : 0;
  LST_DO_CODE(OB_UNIS_ENCODE, count, cmp_funcs_, hash_funcs_for_insert_, col_cnt_, max_in_num_, need_null_cmp_flags_);
  for (int64_t i = 0; OB_SUCC(ret) && i < count; ++i) {
    if (OB_ISNULL(serial_rows_.at(i))) { ret = OB_ERR_UNEXPECTED;
    } else { ret = rf_encode_row(*serial_rows_.at(i), build_obj_metas_, buf, buf_len, pos); }
  }
  LST_DO_CODE(OB_UNIS_ENCODE, query_range_info_, build_obj_metas_);
  return ret;
}

OB_DEF_SERIALIZE_SIZE(ObRFInFilterMsg)
{
  int64_t len = 0;
  BASE_ADD_LEN((ObRFInFilterMsg, ObP2PDatahubMsgBase));
  const int64_t count = is_active_ ? serial_rows_.count() : 0;
  LST_DO_CODE(OB_UNIS_ADD_LEN, count, cmp_funcs_, hash_funcs_for_insert_, col_cnt_, max_in_num_, need_null_cmp_flags_);
  for (int64_t i = 0; i < count; ++i) {
    OB_ASSERT(serial_rows_.at(i) != nullptr);
    len += rf_row_size(*serial_rows_.at(i), build_obj_metas_);
  }
  LST_DO_CODE(OB_UNIS_ADD_LEN, query_range_info_, build_obj_metas_);
  return len;
}

int ObRFInFilterMsg::deserialize(const char *buf, const int64_t data_len, int64_t &pos)
{
  int64_t cursor = pos, version = 0, begin = 0, end = 0;
  int ret = rf_envelope(buf, data_len, cursor, version, begin, end);
  if (OB_SUCC(ret) && version != 1 && version != 2) { ret = OB_NOT_SUPPORTED; }
  if (OB_SUCC(ret)) {
    int64_t body_pos = 0;
    ret = deserialize_body(buf + begin, end - begin, body_pos, version);
  }
  if (OB_SUCC(ret)) { pos = end; }
  return ret;
}

int ObRFInFilterMsg::deserialize_(const char *buf, const int64_t data_len, int64_t &pos)
{
  return deserialize_body(buf, data_len, pos, 2);
}

int ObRFInFilterMsg::deserialize_body(const char *buf, int64_t data_len,
                                     int64_t &pos, int64_t version)
{
  FilterWireLayout layout;
  int64_t cursor = pos;
  if (buf == nullptr || cursor < 0 || cursor > data_len) { return OB_DESERIALIZE_ERROR; }
  const char *body = buf + cursor;
  const int64_t length = data_len - cursor;
  int ret = rf_preflight(body, length, false, layout);
  if (OB_SUCC(ret) && is_ready_) { ret = OB_STATE_NOT_MATCH; }
  if (OB_SUCC(ret) && !layout.has_metadata_ && version != 1) { ret = OB_DESERIALIZE_ERROR; }
  if (OB_FAIL(ret)) {
  } else {
    (void)destroy();
    int64_t p = 0, count = 0;
    if (OB_FAIL(ObP2PDatahubMsgBase::deserialize(body, length, p))) {
    } else if (get_msg_type() != IN_FILTER_MSG) { ret = OB_DESERIALIZE_ERROR;
    } else if (!layout.has_metadata_) {
      is_active_ = false;
      is_empty_ = true;
      col_cnt_ = 0;
      max_in_num_ = 0;
    } else {
      const bool active = is_active_;
      is_active_ = false;
      int64_t meta_pos = layout.meta_begin_;
      if (OB_FAIL(build_obj_metas_.deserialize(body, length, meta_pos))) {
      } else if (OB_FAIL(rf_integer(body, length, p, count))) {
      } else if (OB_FAIL(cmp_funcs_.deserialize(body, length, p))) {
      } else if (OB_FAIL(hash_funcs_for_insert_.deserialize(body, length, p))) {
      } else if (OB_FAIL(rf_integer(body, length, p, col_cnt_))) {
      } else if (OB_FAIL(rf_integer(body, length, p, max_in_num_))) {
      } else if (OB_FAIL(need_null_cmp_flags_.deserialize(body, length, p))) {
      } else if (OB_FAIL(cur_row_.prepare_allocate(col_cnt_))) {
      } else if (OB_FAIL(serial_rows_.reserve(count))) {
      } else if ((!active && count != 0) || (active && is_empty_ != (count == 0))) {
        ret = OB_DESERIALIZE_ERROR;
      }
      for (int64_t i = 0; OB_SUCC(ret) && i < col_cnt_; ++i) {
        if (OB_ISNULL(cmp_funcs_.at(i).cmp_func_) || OB_ISNULL(hash_funcs_for_insert_.at(i).hash_func_)) {
          ret = OB_DESERIALIZE_ERROR;
        }
      }
      // Import every physical cell using complete per-column metadata first.
      // No hash callback or row index is published while metadata is missing.
      for (int64_t i = 0; OB_SUCC(ret) && i < count; ++i) {
        void *bytes = allocator_.alloc(sizeof(ObFixedArray<Datum, ObIAllocator>));
        if (OB_ISNULL(bytes)) { ret = OB_ALLOCATE_MEMORY_FAILED;
        } else {
          auto *row = new (bytes) ObFixedArray<Datum, ObIAllocator>(allocator_);
          if (OB_FAIL(rf_decode_row(body, length, p, build_obj_metas_, allocator_, *row))) {
          } else if (OB_FAIL(serial_rows_.push_back(row))) {
          }
        }
      }
      if (OB_FAIL(ret)) {
      } else if (OB_FAIL(query_range_info_.deserialize(body, length, p))) {
      } else if (OB_FAIL(rows_set_.create(std::max<int64_t>(1, count) * 2, "RFDEInFilter", "RFDEInFilter"))) {
      }
      for (int64_t i = 0; OB_SUCC(ret) && i < count; ++i) {
        ObRFInFilterNode node(&cmp_funcs_, &hash_funcs_for_insert_, serial_rows_.at(i),
            &build_obj_metas_, datum_access_ctx_);
        if (OB_FAIL(rows_set_.set_refactored(node, 0))) {
          if (ret == OB_HASH_EXIST) { ret = OB_DESERIALIZE_ERROR; }
        }
      }
      if (OB_SUCC(ret)) { is_active_ = active; }
    }
    is_ready_ = false;
    if (OB_FAIL(ret)) {
      (void)destroy();
      is_active_ = false;
      is_empty_ = true;
    } else { pos = data_len; }
  }
  return ret;
}


//ObRFBloomFilterMsg
int ObRFBloomFilterMsg::reuse()
{
  int ret = OB_SUCCESS;
  is_empty_ = true;
  bloom_filter_.reset_filter();
  is_active_ = true;
  return ret;
}

int ObRFBloomFilterMsg::deep_copy_msg(ObP2PDatahubMsgBase *&new_msg_ptr)
{
  int ret = OB_SUCCESS;
  ObRFBloomFilterMsg *bf_msg = nullptr;
  ObMemAttr attr("PxBfMsg");
  if (OB_FAIL(PX_P2P_DH.alloc_msg<ObRFBloomFilterMsg>(attr, bf_msg))) {
  } else if (OB_FAIL(bf_msg->assign(*this))) {
  } else {
    new_msg_ptr = bf_msg;
  }
  if (OB_FAIL(ret) && OB_NOT_NULL(bf_msg)) {
    bf_msg->destroy();
    ob_free(bf_msg);
  }
  return ret;
}

int ObRFBloomFilterMsg::assign(const ObP2PDatahubMsgBase &msg)
{
  int ret = OB_SUCCESS;
  const ObRFBloomFilterMsg &other_msg = static_cast<const ObRFBloomFilterMsg &>(msg);
  use_hash_join_seed_ = other_msg.use_hash_join_seed_;
  if (OB_FAIL(ObP2PDatahubMsgBase::assign(msg))) {
  } else if (OB_FAIL(bloom_filter_.assign(other_msg.bloom_filter_))) {
  }
  return ret;
}

// the merge process of bloom_filter_ is atomic by using CAS
int ObRFBloomFilterMsg::merge(ObP2PDatahubMsgBase &msg)
{
  int ret = OB_SUCCESS;
  ObRFBloomFilterMsg &bf_msg = static_cast<ObRFBloomFilterMsg &>(msg);
  if (bf_msg.is_empty_) {
  } else if (OB_FAIL(bloom_filter_.merge_filter(&bf_msg.bloom_filter_))) {
  } else {
    is_empty_ = false;
  }
  return ret;
}

int ObRFBloomFilterMsg::destroy()
{
  int ret = OB_SUCCESS;
  bloom_filter_.reset();
  allocator_.reset();
  return ret;
}

int ObRFBloomFilterMsg::might_contain(const ObExpr &expr,
    ObEvalCtx &ctx,
    ObExprJoinFilter::ObExprJoinFilterContext &filter_ctx,
    Datum &res)
{
  int ret = OB_SUCCESS;
  uint64_t hash_val = ObExprJoinFilter::JOIN_FILTER_SEED;
  const ObDatumAccessContext *access_ctx = nullptr;
  if (use_hash_join_seed_) {
    // hash value explained in:
    //        hash join            simd block bloom filter
    //  10001111....1011010         10001111....1011010
    //  ||_______63_______|          |___32___||__32___|
    //  |     hash               _______|_______   |-->locate block
    //  |                        |    |    |    |
    // is match                  8bit 8bit 8bit 8bit -> split to 4 byte
    //                           |    |    |    |
    //                           6bit 6bit 6bit 6bit -> each low 6bit are used, high 2 bit is useless
    // the highest bit of hash values is not used in bloom filter
    hash_val = ObExprHash::HASH_SEED;
  }
  Datum *datum = nullptr;
  ObHashFunc hash_func;
  if (!is_active_) {
    res.set_int(1);
  } else if (OB_UNLIKELY(is_empty_)) {
    res.set_int(0);
    filter_ctx.filter_count_++;
    filter_ctx.check_count_++;
  } else if (OB_FAIL(ctx.get_datum_access_ctx(access_ctx))) {
  } else {
    for (int i = 0; OB_SUCC(ret) && i < expr.arg_cnt_; ++i) {
      if (OB_FAIL(expr.args_[i]->eval(ctx, datum))) {
      } else {
        hash_func.hash_func_ = filter_ctx.hash_funcs_.at(i).hash_func_;
        if (OB_FAIL(hash_native_filter_value(*expr.args_[i], *datum, hash_func.hash_func_, hash_val, hash_val, access_ctx))) {
        }
      }
    }
    if (OB_SUCC(ret)) {
      bool is_match = true;
      if (OB_FAIL(bloom_filter_.might_contain(hash_val, is_match))) {
      } else {
        if (!is_match) {
          filter_ctx.filter_count_++;
        }
        filter_ctx.check_count_++;
        res.set_int(is_match ? 1 : 0);
        filter_ctx.collect_sample_info(!is_match, 1);
      }
    }
  }
  return ret;
}

int ObRFBloomFilterMsg::might_contain_batch(
    const ObExpr &expr,
    ObEvalCtx &ctx,
    const ObBitVector &skip,
    const int64_t batch_size,
    ObExprJoinFilter::ObExprJoinFilterContext &filter_ctx)
{
  int ret = OB_SUCCESS;
  bool is_match = true;
  uint64_t seed = ObExprJoinFilter::JOIN_FILTER_SEED;
  Datum *results = expr.locate_batch_datums(ctx);
  ObBitVector &eval_flags = expr.get_evaluated_flags(ctx);
  if (filter_ctx.native_hash_values_.count() < batch_size) { return OB_ERR_UNEXPECTED; }
  uint64_t *hash_values = filter_ctx.native_hash_values_.get_data();
  int64_t total_count = 0;
  int64_t filter_count = 0;
  const ObDatumAccessContext *access_ctx = nullptr;
  if (OB_UNLIKELY(is_empty_)) {
    if (OB_FAIL(ObBitVector::flip_foreach(skip, batch_size,
        [&](int64_t idx) __attribute__((always_inline)) {
      results[idx].set_int(0);
      ++filter_count;
      ++total_count;
      return OB_SUCCESS;
    }))) {
    }
    if (OB_SUCC(ret)) {
      eval_flags.set_all(true);
      filter_ctx.filter_count_ += filter_count;
      filter_ctx.check_count_ += total_count;
      filter_ctx.total_count_ += total_count;
    }
  } else if (OB_FAIL(ctx.get_datum_access_ctx(access_ctx))) {
  } else {
    for (int64_t i = 0; OB_SUCC(ret) && i < expr.arg_cnt_; ++i) {
      ObExpr *e = expr.args_[i];
      if (OB_FAIL(e->eval_batch(ctx, skip, batch_size))) {
      } else {
        const bool is_batch_seed = (i > 0);
        ret = hash_native_filter_batch(*e, filter_ctx.hash_funcs_.at(i).hash_func_, hash_values,
            e->locate_batch_datums(ctx), skip, batch_size,
            is_batch_seed ? hash_values : &seed, is_batch_seed, access_ctx);
      }
    }
    if (OB_FAIL(ret)) {
    } else if (OB_FAIL(ObBitVector::flip_foreach(skip, batch_size,
          [&](int64_t idx) __attribute__((always_inline)) {
            bloom_filter_.prefetch_bits_block(hash_values[idx]); return OB_SUCCESS;
          }))) {
    } else if (OB_FAIL(ObBitVector::flip_foreach(skip, batch_size,
        [&](int64_t idx) __attribute__((always_inline)) {
          int tmp_ret = bloom_filter_.might_contain(hash_values[idx], is_match);
          if (OB_SUCCESS == tmp_ret) {
            filter_count += !is_match;
            ++total_count;
            results[idx].set_int(is_match);
          }
          return tmp_ret;
        }))) {
    } else {
      eval_flags.set_all(true);
      filter_ctx.filter_count_ += filter_count;
      filter_ctx.check_count_ += total_count;
      filter_ctx.total_count_ += total_count;
      filter_ctx.collect_sample_info(filter_count, total_count);
    }
  }
  return ret;
}

int ObRFBloomFilterMsg::insert_by_row_batch(
  const ObBatchRows *child_brs,
  const common::ObIArray<ObExpr *> &expr_array,
  const common::ObHashFuncs &hash_funcs,
  const ObExpr *calc_tablet_id_expr,
  ObEvalCtx &eval_ctx,
  uint64_t *batch_hash_values)
{
  int ret = OB_SUCCESS;
  const ObDatumAccessContext *access_ctx = nullptr;
  if (child_brs->size_ > 0) {
    uint64_t seed = ObExprJoinFilter::JOIN_FILTER_SEED;
    if (OB_FAIL(eval_ctx.get_datum_access_ctx(access_ctx))) {
    } else if (OB_NOT_NULL(calc_tablet_id_expr)) {
      if (OB_ISNULL(calc_tablet_id_expr) || hash_funcs.count() != 1) {
        ret = OB_ERR_UNEXPECTED;
      } else if (OB_FAIL(calc_tablet_id_expr->eval_batch(eval_ctx,
        *(child_brs->skip_), child_brs->size_))) {
      } else {
        ret = hash_native_filter_batch(*calc_tablet_id_expr, hash_funcs.at(0).hash_func_,
            batch_hash_values, calc_tablet_id_expr->locate_batch_datums(eval_ctx),
            *child_brs->skip_, child_brs->size_, &seed, false, access_ctx);
      }
    } else {
      for (int64_t i = 0; OB_SUCC(ret) && i < expr_array.count(); ++i) {
        ObExpr *expr = expr_array.at(i); // expr ptr check in cg, not check here
        if (OB_FAIL(expr->eval_batch(eval_ctx, *(child_brs->skip_), child_brs->size_))) {
        } else {
          const bool is_batch_seed = (i > 0);
          ret = hash_native_filter_batch(*expr, hash_funcs.at(i).hash_func_, batch_hash_values,
              expr->locate_batch_datums(eval_ctx), *child_brs->skip_, child_brs->size_,
              is_batch_seed ? batch_hash_values : &seed, is_batch_seed, access_ctx);
        }
      }
    }
    for (int64_t i = 0; OB_SUCC(ret) && i < child_brs->size_; ++i) {
      if (OB_NOT_NULL(calc_tablet_id_expr)) {
        Datum &datum = calc_tablet_id_expr->locate_expr_datum(eval_ctx, i);
        if (ObExprCalcPartitionId::NONE_PARTITION_ID == datum.get_int()) {
          continue;
        }
      }
      if (OB_SUCC(ret)) {
        if (child_brs->skip_->at(i)) {
          continue;
        } else if (OB_FAIL(bloom_filter_.put(batch_hash_values[i]))) {
        } else if (is_empty_) {
          is_empty_ = false;
        }
      }
    }
  }
  return ret;
}
int ObRFBloomFilterMsg::insert_by_row(
    const common::ObIArray<ObExpr *> &expr_array,
    const common::ObHashFuncs &hash_funcs,
    const ObExpr *calc_tablet_id_expr,
    ObEvalCtx &eval_ctx)
{
  int ret = OB_SUCCESS;
  uint64_t hash_value = 0;
  bool ignore = false;
  if (OB_FAIL(calc_hash_value(expr_array,
    hash_funcs, calc_tablet_id_expr,
    eval_ctx, hash_value, ignore))) {
  } else if (ignore) {
      /*do nothing*/
  } else if (OB_FAIL(bloom_filter_.put(hash_value))) {
  } else if (is_empty_) {
    is_empty_ = false;
  }
  return ret;
}

int ObRFBloomFilterMsg::calc_hash_value(
    const common::ObIArray<ObExpr *> &expr_array,
    const common::ObHashFuncs &hash_funcs,
    const ObExpr *calc_tablet_id_expr,
    ObEvalCtx &eval_ctx,
    uint64_t &hash_value, bool &ignore)
{
  int ret = OB_SUCCESS;
  hash_value = ObExprJoinFilter::JOIN_FILTER_SEED;
  ignore = false;
  Datum *datum = nullptr;
  const ObDatumAccessContext *access_ctx = nullptr;
  if (OB_FAIL(eval_ctx.get_datum_access_ctx(access_ctx))) {
  } else if (OB_NOT_NULL(calc_tablet_id_expr)) {
    int64_t partition_id = 0;
    if (hash_funcs.count() != 1) {
      ret = OB_ERR_UNEXPECTED;
    } else if (OB_FAIL(calc_tablet_id_expr->eval(eval_ctx, datum))) {
    } else if (ObExprCalcPartitionId::NONE_PARTITION_ID == (partition_id = datum->get_int())) {
      ignore = true;
    } else if (OB_FAIL(
                   hash_native_filter_value(*calc_tablet_id_expr, *datum, hash_funcs.at(0).hash_func_, hash_value, hash_value, access_ctx))) {
    }
  } else {
    for (int64_t idx = 0; OB_SUCC(ret) && idx < expr_array.count() ; ++idx) {
      if (OB_FAIL(expr_array.at(idx)->eval(eval_ctx, datum))) {
      } else if (OB_FAIL(
                     hash_native_filter_value(*expr_array.at(idx), *datum, hash_funcs.at(idx).hash_func_, hash_value, hash_value, access_ctx))) {
      }
    }
  }
  return ret;
}

//end ObRFBloomFilterMsg

int ObRFRangeFilterMsg::init_query_range_info(const ObPxQueryRangeInfo &query_range_info)
{
  return rf_valid_query_columns(query_range_info.range_column_cnt_,
      query_range_info.prefix_col_idxs_.count())
      ? query_range_info_.assign(query_range_info) : OB_INVALID_ARGUMENT;
}

int ObRFInFilterMsg::init_query_range_info(const ObPxQueryRangeInfo &query_range_info)
{
  return rf_valid_query_columns(query_range_info.range_column_cnt_,
      query_range_info.prefix_col_idxs_.count())
      ? query_range_info_.assign(query_range_info) : OB_INVALID_ARGUMENT;
}

//ObRFRangeFilterMsg
ObRFRangeFilterMsg::ObRFRangeFilterMsg()
: ObP2PDatahubMsgBase(), lower_bounds_(allocator_), upper_bounds_(allocator_),
  need_null_cmp_flags_(allocator_), cells_size_(allocator_),
  cmp_funcs_(allocator_), query_range_info_(allocator_),
  query_range_(), is_query_range_ready_(false), query_range_allocator_(),
  datum_access_ctx_(nullptr),
  build_obj_metas_(allocator_)
{
}

int ObRFRangeFilterMsg::reuse()
{
  int ret = OB_SUCCESS;
  is_empty_ = true;
  lower_bounds_.reset();
  upper_bounds_.reset();
  cells_size_.reset();
  if (OB_FAIL(lower_bounds_.prepare_allocate(cmp_funcs_.count()))) {
  } else if (OB_FAIL(upper_bounds_.prepare_allocate(cmp_funcs_.count()))) {
  } else if (OB_FAIL(cells_size_.prepare_allocate(cmp_funcs_.count()))) {
  }
  (void)reuse_query_range();
  return ret;
}

int ObRFRangeFilterMsg::assign(const ObP2PDatahubMsgBase &msg)
{
  int ret = OB_SUCCESS;
  const ObRFRangeFilterMsg &other_msg = static_cast<const ObRFRangeFilterMsg &>(msg);
  if (OB_FAIL(ObP2PDatahubMsgBase::assign(msg))) {
  } else if (OB_FAIL(lower_bounds_.assign(other_msg.lower_bounds_))) {
  } else if (OB_FAIL(upper_bounds_.assign(other_msg.upper_bounds_))) {
  } else if (OB_FAIL(cmp_funcs_.assign(other_msg.cmp_funcs_))) {
  } else if (OB_FAIL(build_obj_metas_.assign(other_msg.build_obj_metas_))) {
  } else if (OB_FAIL(need_null_cmp_flags_.assign(other_msg.need_null_cmp_flags_))) {
  } else if (OB_FAIL(cells_size_.assign(other_msg.cells_size_))) {
  } else if (OB_FAIL(adjust_cell_size())) {
  } else if (OB_FAIL(query_range_info_.assign(other_msg.query_range_info_))) {
  } else {
    datum_access_ctx_ = other_msg.datum_access_ctx_;
  }
  return ret;
}

int ObRFRangeFilterMsg::deep_copy_msg(ObP2PDatahubMsgBase *&new_msg_ptr)
{
  int ret = OB_SUCCESS;
  ObRFRangeFilterMsg *rf_msg = nullptr;
  ObMemAttr attr("PxRangeMsg");
  if (OB_FAIL(PX_P2P_DH.alloc_msg<ObRFRangeFilterMsg>(attr, rf_msg))) {
  } else if (OB_FAIL(rf_msg->assign(*this))) {
  } else {
    for (int i = 0; i < rf_msg->lower_bounds_.count() && OB_SUCC(ret); ++i) {
      if (OB_FAIL(DatumParameterCodec::copy_native_value_span(lower_bounds_.at(i), build_obj_metas_.at(i),
          rf_msg->get_allocator(), rf_msg->lower_bounds_.at(i)))) {
      } else if (OB_FAIL(DatumParameterCodec::copy_native_value_span(upper_bounds_.at(i), build_obj_metas_.at(i),
          rf_msg->get_allocator(), rf_msg->upper_bounds_.at(i)))) {
      }
    }
    if (OB_SUCC(ret)) {
      new_msg_ptr = rf_msg;
    }
  }
  if (OB_FAIL(ret) && OB_NOT_NULL(rf_msg)) {
    rf_msg->destroy();
    ob_free(rf_msg);
  }
  return ret;
}

int ObRFRangeFilterMsg::merge(ObP2PDatahubMsgBase &msg)
{
  int ret = OB_SUCCESS;
  ObRFRangeFilterMsg &range_msg = static_cast<ObRFRangeFilterMsg &>(msg);
  if (!range_msg.is_active_) {
    // A valid old-v1 optional-tail omission intentionally has no native rows.
    is_active_ = false;
  } else if (!is_active_ || range_msg.is_empty_) {
    /*do nothing*/
  } else if (range_msg.lower_bounds_.count() != lower_bounds_.count()
      || range_msg.upper_bounds_.count() != upper_bounds_.count()) {
    ret = OB_ERR_UNEXPECTED;
  } else {
    ObSpinLockGuard guard(lock_);
    if (OB_ISNULL(datum_access_ctx_)) {
      datum_access_ctx_ = range_msg.datum_access_ctx_;
    }
    if (OB_FAIL(get_min(range_msg.lower_bounds_, datum_access_ctx_))) {
    } else if (OB_FAIL(get_max(range_msg.upper_bounds_, datum_access_ctx_))) {
    } else if (is_empty_) {
      is_empty_ = false;
    }
  }
  return ret;
}

int ObRFRangeFilterMsg::get_min(
    ObIArray<Datum> &vals,
    const ObDatumAccessContext *access_ctx)
{
  int ret = OB_SUCCESS;
  for (int i = 0; i < vals.count() && OB_SUCC(ret); ++i) {
    // null value is also suitable
    if (OB_FAIL(get_min(cmp_funcs_.at(i), lower_bounds_.at(i),
        vals.at(i), cells_size_.at(i).min_datum_buf_size_, build_obj_metas_.at(i), access_ctx))) {
    }
  }
  return ret;
}

int ObRFRangeFilterMsg::get_max(
    ObIArray<Datum> &vals,
    const ObDatumAccessContext *access_ctx)
{
  int ret = OB_SUCCESS;
  for (int i = 0; i < vals.count() && OB_SUCC(ret); ++i) {
    // null value is also suitable
    if (OB_FAIL(get_max(cmp_funcs_.at(i), upper_bounds_.at(i),
        vals.at(i), cells_size_.at(i).max_datum_buf_size_, build_obj_metas_.at(i), access_ctx))) {
    }
  }
  return ret;
}

int ObRFRangeFilterMsg::get_min(
    ObCmpFunc &func,
    Datum &l,
    const Datum &r,
    int64_t &cell_size,
    const SqlTypeMeta &meta,
    const ObDatumAccessContext *access_ctx)
{
  int ret = OB_SUCCESS;
  int cmp = 0;
  // when [null, null] merge [a, b], the expect result in mysql mode is [null, b]
  // A NULL lower bound is not overwritten by a non-NULL value. An empty
  // reference is compared normally, regardless of its nullable byte pointer.
  if (is_empty_) {
    if (OB_FAIL(dynamic_copy_cell(r, l, cell_size, meta))) {
    }
  } else if (OB_FAIL(compare_filter_values(func, l, r, meta, cmp, access_ctx))) {
  } else if (cmp > 0) {
    if (OB_FAIL(dynamic_copy_cell(r, l, cell_size, meta))) {
    }
  }
  return ret;
}

int ObRFRangeFilterMsg::prepare_query_range()
{
  int ret = OB_SUCCESS;
  (void)reuse_query_range();
  if (!rf_valid_query_columns(query_range_info_.range_column_cnt_,
                              query_range_info_.prefix_col_idxs_.count())) {
    ret = OB_INVALID_ARGUMENT;
  } else if (!query_range_info_.can_extract() || !is_active_) {
    is_query_range_ready_ = false;
  } else if (is_empty_) {
    // make empty range
    if (OB_FAIL(fill_empty_query_range(query_range_info_, query_range_allocator_, query_range_))) {
    } else {
      is_query_range_ready_ = true;
    }
  } else {
    // only extract the first column
    int64_t prefix_col_idx = query_range_info_.prefix_col_idxs_.at(0);
    int64_t range_column_cnt = query_range_info_.range_column_cnt_;
    const SqlTypeMeta &prefix_col_obj_meta = query_range_info_.prefix_col_obj_metas_.at(0);

    query_range_.table_id_ = query_range_info_.table_id_;
    query_range_.border_flag_.set_inclusive_start();
    query_range_.border_flag_.set_inclusive_end();

    const Datum &lower_bound = lower_bounds_.at(prefix_col_idx);
    const Datum &upper_bound = upper_bounds_.at(prefix_col_idx);
    DatumValue *start = NULL;
    DatumValue *end = NULL;
    if (OB_ISNULL(start = static_cast<DatumValue *>(
                      query_range_allocator_.alloc(sizeof(DatumValue) * range_column_cnt)))) {
      ret = OB_ALLOCATE_MEMORY_FAILED;
    } else if (OB_ISNULL(end = static_cast<DatumValue *>(
                             query_range_allocator_.alloc(sizeof(DatumValue) * range_column_cnt)))) {
      ret = OB_ALLOCATE_MEMORY_FAILED;
    } else {
      new(start) DatumValue();
      new(end) DatumValue();
      if (OB_FAIL(project_owned_filter_value(lower_bound, build_obj_metas_.at(prefix_col_idx),
          prefix_col_obj_meta, *start, query_range_allocator_))) {
      } else if (OB_FAIL(project_owned_filter_value(upper_bound, build_obj_metas_.at(prefix_col_idx),
          prefix_col_obj_meta, *end, query_range_allocator_))) {
      }
      // fill left coloumn with (min, max)
      for (int64_t i = 1; OB_SUCC(ret) && i < range_column_cnt; ++i) {
        new(start + i) DatumValue();
        new(end + i) DatumValue();
        (start + i)->set_min_value();
        (end + i)->set_max_value();
      }
      ObRowkey start_key(start, range_column_cnt);
      ObRowkey end_key(end, range_column_cnt);
      if (OB_SUCC(ret)) { query_range_.start_key_ = start_key; query_range_.end_key_ = end_key; }
    }

    if (OB_SUCC(ret)) {
      is_query_range_ready_ = true;
    }
  }
  return ret;
}

void ObRFRangeFilterMsg::after_process()
{
  // prepare_query_range can be failed, but rf still worked
  (void)prepare_query_range();
}

int ObRFRangeFilterMsg::try_extract_query_range(bool &has_extract, ObIArray<ObNewRange> &ranges,
                                                bool need_deep_copy, common::ObIAllocator *allocator)
{
  int ret = OB_SUCCESS;
  if (!is_query_range_ready_) {
    has_extract = false;
  } else {
    // overwrite ranges
    ranges.reset();
    if (need_deep_copy) {
      if (OB_FAIL(ranges.prepare_allocate(1))) {
      } else if (OB_FAIL(deep_copy_range(*allocator, query_range_, ranges.at(0)))) {
      }
    } else {
      if (OB_FAIL(ranges.push_back(query_range_))) {
      }
    }
    if (OB_SUCC(ret)) {
      has_extract = true;
    }
  }
  return ret;
}

int ObRFRangeFilterMsg::adjust_cell_size()
{
  int ret = OB_SUCCESS;
  CK(cells_size_.count() == lower_bounds_.count() && lower_bounds_.count() == upper_bounds_.count()
     && build_obj_metas_.count() == lower_bounds_.count());
  for (int64_t i = 0; OB_SUCC(ret) && i < cells_size_.count(); ++i) {
    int64_t lower_size = 0, upper_size = 0;
    if (OB_FAIL(DatumParameterCodec::native_span_size(lower_bounds_.at(i), build_obj_metas_.at(i), lower_size))) {
    } else if (OB_FAIL(DatumParameterCodec::native_span_size(upper_bounds_.at(i), build_obj_metas_.at(i), upper_size))) {
    } else {
      cells_size_.at(i).min_datum_buf_size_ = lower_size;
      cells_size_.at(i).max_datum_buf_size_ = upper_size;
    }
  }
  return ret;
}

int ObRFRangeFilterMsg::dynamic_copy_cell(const Datum &src, Datum &target,
                                        int64_t &cell_size, const SqlTypeMeta &meta)
{
  int64_t size = 0;
  int ret = DatumParameterCodec::native_span_size(src, meta, size);
  if (OB_FAIL(ret)) {
  } else if (size == 0) {
    target = src;
    cell_size = 0;
  } else if (size <= cell_size) {
    MEMMOVE(const_cast<void *>(target.get_reference()), src.get_reference(), size);
    ret = target.set_reference(target.get_reference(), size, src.reference_flags());
  } else {
    const int64_t capacity = size * 2;
    void *bytes = allocator_.alloc(capacity);
    if (OB_ISNULL(bytes)) {
      ret = OB_ALLOCATE_MEMORY_FAILED;
    } else {
      MEMCPY(bytes, src.get_reference(), size);
      Datum candidate;
      if (OB_FAIL(candidate.set_reference(bytes, size, src.reference_flags()))) {
      } else {
        target = candidate;
        cell_size = capacity;
      }
    }
  }
  return ret;
}

int ObRFRangeFilterMsg::get_max(
    ObCmpFunc &func,
    Datum &l,
    const Datum &r,
    int64_t &cell_size,
    const SqlTypeMeta &meta,
    const ObDatumAccessContext *access_ctx)
{
  int ret = OB_SUCCESS;
  int cmp = 0;
  if (is_empty_) {
    if (OB_FAIL(dynamic_copy_cell(r, l, cell_size, meta))) {
    }
  } else if (OB_FAIL(compare_filter_values(func, l, r, meta, cmp, access_ctx))) {
  } else if (cmp < 0) {
    if (OB_FAIL(dynamic_copy_cell(r, l, cell_size, meta))) {
    }
  }
  return ret;
}

int ObRFRangeFilterMsg::insert_by_row(
    const common::ObIArray<ObExpr *> &expr_array,
    const common::ObHashFuncs &hash_funcs,
    const ObExpr *calc_tablet_id_expr,
    ObEvalCtx &eval_ctx)
{
  int ret = OB_SUCCESS;
  UNUSED(hash_funcs);
  Datum *datum = nullptr;
  if (!is_active_) {
  } else if (OB_FAIL(eval_ctx.get_datum_access_ctx(datum_access_ctx_))) {
  } else if (is_empty_) {
    bool ignore_null = false;
    for (int64_t i = 0; OB_SUCC(ret) && i < expr_array.count(); ++i) {
      ObExpr *expr = expr_array.at(i);
      alignas(8) char scratch[64];
      ObDatum legacy;
      Datum physical;
      if (OB_FAIL(expr->eval(eval_ctx, datum))) {
      } else if (OB_FAIL(expr->get_datum_copy_plan().legacy_view(*datum, legacy, scratch, sizeof(scratch)))) {
      } else if (OB_FAIL(DatumParameterCodec::borrow_native_value(legacy, build_obj_metas_.at(i), physical))) {
      } else if (datum->is_null() && !need_null_cmp_flags_.at(i)) {
        ignore_null = true;
        break;
      } else if (OB_FAIL(dynamic_copy_cell(physical, lower_bounds_.at(i), cells_size_.at(i).min_datum_buf_size_, build_obj_metas_.at(i)))) {
      } else if (OB_FAIL(dynamic_copy_cell(physical, upper_bounds_.at(i), cells_size_.at(i).max_datum_buf_size_, build_obj_metas_.at(i)))) {
      }
    }
    if (OB_SUCC(ret) && !ignore_null) {
      is_empty_ = false;
    }
  } else {
    for (int64_t i = 0; OB_SUCC(ret) && i < expr_array.count(); ++i) {
      ObExpr *expr = expr_array.at(i);
      alignas(8) char scratch[64];
      ObDatum legacy;
      Datum physical;
      if (OB_FAIL(expr->eval(eval_ctx, datum))) {
      } else if (OB_FAIL(expr->get_datum_copy_plan().legacy_view(*datum, legacy, scratch, sizeof(scratch)))) {
      } else if (OB_FAIL(DatumParameterCodec::borrow_native_value(legacy, build_obj_metas_.at(i), physical))) {
      } else if (datum->is_null() && !need_null_cmp_flags_.at(i)) {
        /*do nothing*/
        break;
      } else if (OB_FAIL(get_min(cmp_funcs_.at(i), lower_bounds_.at(i), physical,
                                 cells_size_.at(i).min_datum_buf_size_, build_obj_metas_.at(i), datum_access_ctx_))) {
      } else if (OB_FAIL(get_max(cmp_funcs_.at(i), upper_bounds_.at(i), physical,
                                 cells_size_.at(i).max_datum_buf_size_, build_obj_metas_.at(i), datum_access_ctx_))) {
      }
    }
  }
  return ret;
}

int ObRFRangeFilterMsg::insert_by_row_batch(
  const ObBatchRows *child_brs,
  const common::ObIArray<ObExpr *> &expr_array,
  const common::ObHashFuncs &hash_funcs,
  const ObExpr *calc_tablet_id_expr,
  ObEvalCtx &eval_ctx,
  uint64_t *batch_hash_values)
{
  int ret = OB_SUCCESS;
  UNUSED(batch_hash_values);
  UNUSED(calc_tablet_id_expr);
  if (child_brs->size_ > 0) {
    ObEvalCtx::BatchInfoScopeGuard batch_info_guard(eval_ctx);
    batch_info_guard.set_batch_size(child_brs->size_);
    for (int64_t idx = 0; OB_SUCC(ret) && idx < child_brs->size_; ++idx) {
      if (child_brs->skip_->at(idx)) {
        continue;
      } else {
        batch_info_guard.set_batch_idx(idx);
        if (OB_FAIL(insert_by_row(expr_array, hash_funcs,
            calc_tablet_id_expr, eval_ctx))) {
        }
      }
    }
  }
  return ret;
}

int ObRFRangeFilterMsg::might_contain(const ObExpr &expr,
      ObEvalCtx &ctx,
      ObExprJoinFilter::ObExprJoinFilterContext &filter_ctx,
      Datum &res)
{
  int ret = OB_SUCCESS;
  Datum *datum = nullptr;
  ObCmpFunc cmp_func;
  int cmp_min = 0;
  int cmp_max = 0;
  bool is_match = true;
  if (!is_active_) {
    res.set_int(1);
  } else if (OB_UNLIKELY(is_empty_)) {
    res.set_int(0);
    filter_ctx.filter_count_++;
    filter_ctx.check_count_++;
  } else if (OB_FAIL(ctx.get_datum_access_ctx(datum_access_ctx_))) {
  } else {
    for (int i = 0; OB_SUCC(ret) && i < expr.arg_cnt_; ++i) {
      if (OB_FAIL(expr.args_[i]->eval(ctx, datum))) {
      } else {
        alignas(8) char scratch[64];
        ObDatum legacy;
        cmp_min = 0;
        cmp_max = 0;
        cmp_func.cmp_func_ = filter_ctx.cmp_funcs_.at(i).cmp_func_;
        if (OB_FAIL(expr.args_[i]->get_datum_copy_plan().legacy_view(*datum, legacy, scratch, sizeof(scratch)))) {
        } else if (OB_FAIL(compare_probe_filter_value(cmp_func,
                legacy, lower_bounds_.at(i), build_obj_metas_.at(i), cmp_min, datum_access_ctx_))) {
        } else if (cmp_min < 0) {
          is_match = false;
          break;
        } else if (OB_FAIL(compare_probe_filter_value(cmp_func,
                       legacy, upper_bounds_.at(i), build_obj_metas_.at(i), cmp_max, datum_access_ctx_))) {
        } else if (cmp_max > 0) {
          is_match = false;
          break;
        }
      }
    }
    if (OB_SUCC(ret)) {
      if (!is_match) {
        filter_ctx.filter_count_++;
      }
      filter_ctx.check_count_++;
      res.set_int(is_match ? 1 : 0);
      filter_ctx.collect_sample_info(!is_match, 1);
    }
  }
  return ret;
}

int ObRFRangeFilterMsg::do_might_contain_batch(const ObExpr &expr,
    ObEvalCtx &ctx,
    const ObBitVector &skip,
    const int64_t batch_size,
    ObExprJoinFilter::ObExprJoinFilterContext &filter_ctx) {
  int ret = OB_SUCCESS;
  int64_t filter_count = 0;
  int64_t total_count = 0;
  Datum *results = expr.locate_batch_datums(ctx);
  for (int idx = 0; OB_SUCC(ret) && idx < expr.arg_cnt_; ++idx) {
    if (OB_FAIL(expr.args_[idx]->eval_batch(ctx, skip, batch_size))) {
    }
  }
  if (OB_SUCC(ret)) {
    int cmp_min = 0;
    int cmp_max = 0;
    Datum *datum = nullptr;
    bool is_match = true;
    for (int64_t batch_i = 0; OB_SUCC(ret) && batch_i < batch_size; ++batch_i) {
      if (skip.at(batch_i)) {
        continue;
      }
      cmp_min = 0;
      cmp_max = 0;
      is_match = true;
      total_count++;
      for (int arg_i = 0; OB_SUCC(ret) && arg_i < expr.arg_cnt_; ++arg_i) {
        datum = &expr.args_[arg_i]->locate_expr_datum(ctx, batch_i);
        alignas(8) char scratch[64];
        ObDatum legacy;
        if (OB_FAIL(expr.args_[arg_i]->get_datum_copy_plan().legacy_view(*datum, legacy, scratch, sizeof(scratch)))) {
        } else if (OB_FAIL(compare_probe_filter_value(filter_ctx.cmp_funcs_.at(arg_i),
                legacy, lower_bounds_.at(arg_i), build_obj_metas_.at(arg_i), cmp_min, datum_access_ctx_))) {
        } else if (cmp_min < 0) {
          filter_count++;
          is_match = false;
          break;
        } else if (OB_FAIL(compare_probe_filter_value(filter_ctx.cmp_funcs_.at(arg_i),
                       legacy, upper_bounds_.at(arg_i), build_obj_metas_.at(arg_i), cmp_max, datum_access_ctx_))) {
        } else if (cmp_max > 0) {
          filter_count++;
          is_match = false;
          break;
        }
      }
      results[batch_i].set_int(is_match ? 1 : 0);
    }
  }
  if (OB_SUCC(ret)) {
    filter_ctx.filter_count_ += filter_count;
    filter_ctx.total_count_ += total_count;
    filter_ctx.check_count_ += total_count;
    filter_ctx.collect_sample_info(filter_count, total_count);
  }
  return ret;
}

int ObRFRangeFilterMsg::might_contain_batch(
    const ObExpr &expr,
    ObEvalCtx &ctx,
    const ObBitVector &skip,
    const int64_t batch_size,
    ObExprJoinFilter::ObExprJoinFilterContext &filter_ctx)
{
  int ret = OB_SUCCESS;
  Datum *results = expr.locate_batch_datums(ctx);
  ObBitVector &eval_flags = expr.get_evaluated_flags(ctx);
  ObEvalCtx::BatchInfoScopeGuard batch_info_guard(ctx);
  batch_info_guard.set_batch_size(batch_size);
  if (!is_active_) {
    for (int64_t i = 0; i < batch_size; ++i) { results[i].set_int(1); }
  } else if (OB_UNLIKELY(is_empty_)) {
    for (int64_t i = 0; i < batch_size; i++) {
      results[i].set_int(0);
    }
  } else if (OB_FAIL(ctx.get_datum_access_ctx(datum_access_ctx_))) {
  } else if (OB_FAIL(do_might_contain_batch(expr, ctx, skip, batch_size, filter_ctx))) {
  }
  if (OB_SUCC(ret)) {
    eval_flags.set_all(batch_size);
  }
  return ret;
}

int ObRFRangeFilterMsg::prepare_storage_white_filter_data(ObDynamicFilterExecutor &dynamic_filter,
                                ObEvalCtx &eval_ctx,
                                ObRuntimeFilterParams &params,
                                bool &is_data_prepared)
{
  int ret = OB_SUCCESS;
  int col_idx = dynamic_filter.get_col_idx();
  ObDatum lower, upper;
  if (!is_active_) {
    dynamic_filter.set_filter_action(DynamicFilterAction::PASS_ALL);
    is_data_prepared = true;
  } else if (is_empty_) {
    dynamic_filter.set_filter_action(DynamicFilterAction::FILTER_ALL);
    is_data_prepared = true;
  } else if (OB_FAIL(DatumParameterCodec::copy_legacy_value(lower_bounds_.at(col_idx),
      build_obj_metas_.at(col_idx), dynamic_filter.get_allocator(), lower))) {
  } else if (OB_FAIL(DatumParameterCodec::copy_legacy_value(upper_bounds_.at(col_idx),
      build_obj_metas_.at(col_idx), dynamic_filter.get_allocator(), upper))) {
  } else if (OB_FAIL(params.push_back(lower))) {
  } else if (OB_FAIL(params.push_back(upper))) {
  } else {
    dynamic_filter.set_filter_val_meta(build_obj_metas_.at(col_idx));
    is_data_prepared = true;
  }
  return ret;
}

// end ObRFRangeFilterMsg

// ObRFInFilterMsg

int ObRFInFilterMsg::assign(const ObP2PDatahubMsgBase &msg)
{
  int ret = OB_SUCCESS;
  const ObRFInFilterMsg &other_msg = static_cast<const ObRFInFilterMsg &>(msg);
  if (OB_FAIL(ObP2PDatahubMsgBase::assign(msg))) {
  } else if (OB_FAIL(cmp_funcs_.assign(other_msg.cmp_funcs_))) {
  } else if (OB_FAIL(hash_funcs_for_insert_.assign(other_msg.hash_funcs_for_insert_))) {
  } else if (OB_FAIL(cur_row_.prepare_allocate(other_msg.col_cnt_))) {
  } else if (OB_FAIL(need_null_cmp_flags_.assign(other_msg.need_null_cmp_flags_))) {
  } else if (OB_FAIL(query_range_info_.assign(other_msg.query_range_info_))) {
  } else if (OB_FAIL(build_obj_metas_.assign(other_msg.build_obj_metas_))) {
  } else {
    col_cnt_ = other_msg.col_cnt_;
    max_in_num_ = other_msg.max_in_num_;
    datum_access_ctx_ = other_msg.datum_access_ctx_;
  }
  return ret;
}

int ObRFInFilterMsg::deep_copy_msg(ObP2PDatahubMsgBase *&new_msg_ptr)
{
  int ret = OB_SUCCESS;
  ObRFInFilterMsg *in_msg = nullptr;
  int64_t row_cnt = max(serial_rows_.count(), 1);
  ObMemAttr attr("PxInMsg");
  if (OB_FAIL(PX_P2P_DH.alloc_msg<ObRFInFilterMsg>(attr, in_msg))) {
  } else if (OB_FAIL(in_msg->assign(*this))) {
  } else if (OB_FAIL(in_msg->rows_set_.create(row_cnt * 2,
        "RFCPInFilter",
        "RFCPInFilter"))) {
  } else {
    int64_t row_cnt = serial_rows_.count();
    if (0 == row_cnt) {
    } else {
      for (int i = 0; i < row_cnt && OB_SUCC(ret); ++i) {
        for (int j = 0; j < col_cnt_ && OB_SUCC(ret); ++j) {
          in_msg->cur_row_.at(j) = serial_rows_.at(i)->at(j);
        }
        if (OB_SUCC(ret)) {
          if (OB_FAIL(in_msg->append_row())) {
          }
        }
      }
    }
    if (OB_SUCC(ret)) {
      new_msg_ptr = in_msg;
    }
  }
  if (OB_FAIL(ret) && OB_NOT_NULL(in_msg)) {
    in_msg->destroy();
    ob_free(in_msg);
  }
  return ret;
}

int ObRFInFilterMsg::insert_by_row_batch(
  const ObBatchRows *child_brs,
  const common::ObIArray<ObExpr *> &expr_array,
  const common::ObHashFuncs &hash_funcs,
  const ObExpr *calc_tablet_id_expr,
  ObEvalCtx &eval_ctx,
  uint64_t *batch_hash_values)
{
  int ret = OB_SUCCESS;
  UNUSED(batch_hash_values);
  UNUSED(calc_tablet_id_expr);
  if (child_brs->size_ > 0 && is_active_
      && OB_FAIL(eval_ctx.get_datum_access_ctx(datum_access_ctx_))) {
  } else if (child_brs->size_ > 0 && is_active_) {
    ObEvalCtx::BatchInfoScopeGuard batch_info_guard(eval_ctx);
    batch_info_guard.set_batch_size(child_brs->size_);
    for (int64_t idx = 0; OB_SUCC(ret) && idx < child_brs->size_; ++idx) {
      if (child_brs->skip_->at(idx)) {
        continue;
      } else {
        batch_info_guard.set_batch_idx(idx);
        Datum *datum = nullptr;
        bool ignore_null_row = false;
        for (int64_t i = 0; OB_SUCC(ret) && i < expr_array.count(); ++i) {
          ObExpr *expr = expr_array.at(i);
          if (OB_FAIL(expr->eval(eval_ctx, datum))) {
          } else if (datum->is_null() && !need_null_cmp_flags_.at(i)) {
            ignore_null_row = true;
            break;
          } else {
            cur_row_.at(i) = *datum;
          }
        }
        if (OB_SUCC(ret) && !ignore_null_row) {
          if (OB_FAIL(insert_node(expr_array.get_data()))) {
          }
        }
      }
    }
  }
  return ret;
}

int ObRFInFilterMsg::insert_node(ObExpr *const *expressions)
{
  int ret = OB_SUCCESS;
  ObRFInFilterNode node(
      &cmp_funcs_, &hash_funcs_for_insert_, &cur_row_, &build_obj_metas_, datum_access_ctx_, expressions);
  if (OB_FAIL(rows_set_.exist_refactored(node))) {
    if (OB_HASH_NOT_EXIST == ret) {
      ret = OB_SUCCESS;
      if (serial_rows_.count() > max_in_num_) {
        is_active_ = false;
      } else if (OB_FAIL(append_row(expressions))) {
      } else if (is_empty_) {
        is_empty_ = false;
      }
    } else if (OB_HASH_EXIST == ret) {
      ret = OB_SUCCESS;
    } else {
    }
  }
  return ret;
}

int ObRFInFilterMsg::insert_by_row(
    const common::ObIArray<ObExpr *> &expr_array,
    const common::ObHashFuncs &hash_funcs,
    const ObExpr *calc_tablet_id_expr,
    ObEvalCtx &eval_ctx)
{
  int ret = OB_SUCCESS;
  Datum *datum = nullptr;
  if (is_active_ && OB_FAIL(eval_ctx.get_datum_access_ctx(datum_access_ctx_))) {
  } else if (is_active_) {
    bool ignore_null_row = false;
    for (int64_t idx = 0; OB_SUCC(ret) && idx < expr_array.count() ; ++idx) {
      datum = nullptr;
      if (OB_FAIL(expr_array.at(idx)->eval(eval_ctx, datum))) {
      } else if (datum->is_null() && !need_null_cmp_flags_.at(idx)) {
        ignore_null_row = true;
        break;
      } else {
        cur_row_.at(idx) = *datum;
      }
    }
    if (OB_SUCC(ret) && !ignore_null_row) {
      if (OB_FAIL(insert_node(expr_array.get_data()))) {
      }
    }
  }

  return ret;
}

int ObRFInFilterMsg::append_row(ObExpr *const *expressions)
{
  int ret = OB_SUCCESS;
  ObFixedArray<Datum, ObIAllocator> *new_row = nullptr;
  void *array_ptr = nullptr;
  if (OB_FAIL(serial_rows_.reserve(serial_rows_.count() + 1))) {
  } else if (OB_ISNULL(array_ptr = allocator_.alloc(sizeof(ObFixedArray<Datum, ObIAllocator>)))) {
    ret = OB_ALLOCATE_MEMORY_FAILED;
  } else {
    new_row = new(array_ptr) ObFixedArray<Datum, ObIAllocator>(allocator_);
    ObRFInFilterNode input(&cmp_funcs_, &hash_funcs_for_insert_, &cur_row_,
        &build_obj_metas_, datum_access_ctx_, expressions);
    if (OB_FAIL(new_row->init(cur_row_.count()))) {
    }
    for (int64_t i = 0; OB_SUCC(ret) && i < cur_row_.count(); ++i) {
      alignas(8) char scratch[64];
      ObDatum legacy;
      Datum physical, owned;
      if (OB_FAIL(input.export_cell(i, legacy, scratch, sizeof(scratch)))) {
      } else if (OB_FAIL(DatumParameterCodec::borrow_native_value(legacy, build_obj_metas_.at(i), physical))) {
      } else if (OB_FAIL(DatumParameterCodec::copy_native_value_span(physical,
          build_obj_metas_.at(i), allocator_, owned))) {
      } else if (OB_FAIL(new_row->push_back(owned))) {
      }
    }
    if (OB_SUCC(ret)) {
      ObRFInFilterNode node(&cmp_funcs_, &hash_funcs_for_insert_, new_row,
          &build_obj_metas_, datum_access_ctx_);
      if (OB_FAIL(rows_set_.set_refactored(node))) {
      } else if (OB_FAIL(serial_rows_.push_back(new_row))) {
        // reserve succeeded before insertion, but retain error atomicity if
        // the container ever adds another failure mode.
        (void)rows_set_.erase_refactored(node);
      }
    }
  }
  return ret;
}

int ObRFInFilterMsg::ObRFInFilterNode::export_cell(
    int64_t index, ObDatum &out, char *scratch, int64_t capacity) const
{
  int ret = OB_SUCCESS;
  if (OB_ISNULL(row_) || index < 0 || index >= row_->count()) {
    ret = OB_ERR_UNEXPECTED;
  } else if (expressions_ != nullptr) {
    if (OB_ISNULL(expressions_[index])) {
      ret = OB_ERR_UNEXPECTED;
    } else {
      ret = expressions_[index]->get_datum_copy_plan().legacy_view(row_->at(index), out, scratch, capacity);
    }
  } else if (OB_ISNULL(metas_) || metas_->count() != row_->count()) {
    ret = OB_ERR_UNEXPECTED;
  } else {
    ret = DatumParameterCodec::export_native_value(row_->at(index), metas_->at(index), out, scratch, capacity);
  }
  return ret;
}

int ObRFInFilterMsg::ObRFInFilterNode::hash(uint64_t &hash_ret) const
{
  int ret = OB_SUCCESS;
  uint64_t result = hash_val_;
  if (hash_funcs_ != nullptr) {
    result = ObExprJoinFilter::JOIN_FILTER_SEED;
    for (int64_t i = 0; OB_SUCC(ret) && i < row_->count(); ++i) {
      alignas(8) char scratch[64];
      ObDatum legacy;
      if (OB_FAIL(export_cell(i, legacy, scratch, sizeof(scratch)))) {
      } else if (OB_ISNULL(hash_funcs_->at(i).hash_func_)) {
        ret = OB_ERR_UNEXPECTED;
      } else if (OB_FAIL(hash_funcs_->at(i).hash_func_(legacy, result, result, datum_access_ctx_))) {
      }
    }
  }
  if (OB_SUCC(ret)) { hash_ret = result; }
  return ret;
}

// the ObRFInFilterNode stores in ObRFInFilter always be the datum of build table,
// while the other node can be the build table(during insert or merge process)
// or the probe table(during filter process),
// so the compare process relies on the other node, always using other's cmp_func_.
bool ObRFInFilterMsg::ObRFInFilterNode::operator==(const ObRFInFilterNode &other) const
{
  int cmp_ret = 0;
  bool ret = true;
  for (int i = 0; i < other.row_->count(); ++i) {
    if (row_->at(i).is_null() && other.row_->at(i).is_null()) {
      continue;
    } else {
      // because cmp_func is chosen as compare(probe_data/build_data, build_data)
      // so the other's data must be placed at first
      alignas(8) char left_scratch[64], right_scratch[64];
      ObDatum left, right;
      const int left_ret = other.export_cell(i, left, left_scratch, sizeof(left_scratch));
      const int right_ret = export_cell(i, right, right_scratch, sizeof(right_scratch));
      // All keys are checked by the preceding hash/import before publication.
      // A broken internal pair cannot silently become an unequal key.
      OB_ASSERT(OB_SUCCESS == left_ret && OB_SUCCESS == right_ret);
      int tmp_ret = other.cmp_funcs_->at(i).cmp_func_(left, right, cmp_ret, other.datum_access_ctx_);
      if (OB_SUCCESS != tmp_ret || cmp_ret != 0) {
        if (OB_SUCCESS != tmp_ret) {
        }
        ret = false;
        break;
      }
    }
  }
  return ret;
}

int ObRFInFilterMsg::merge(ObP2PDatahubMsgBase &msg)
{
  int ret = OB_SUCCESS;
  ObRFInFilterMsg &in_msg = static_cast<ObRFInFilterMsg &>(msg);
  if (!msg.is_active()) {
    is_active_ = false;
  } else if (!msg.is_empty() && is_active_) {
    ObSpinLockGuard guard(lock_);
    if (OB_ISNULL(datum_access_ctx_)) {
      datum_access_ctx_ = in_msg.datum_access_ctx_;
    }
    for (int i = 0; i < in_msg.serial_rows_.count() && OB_SUCC(ret); ++i) {
      for (int j = 0; j < in_msg.serial_rows_.at(i)->count(); ++j) {
        cur_row_.at(j) = in_msg.serial_rows_.at(i)->at(j);
      }
      if (OB_FAIL(insert_node())) {
      }
    }
  }
  return ret;
}

int ObRFInFilterMsg::might_contain(const ObExpr &expr,
      ObEvalCtx &ctx,
      ObExprJoinFilter::ObExprJoinFilterContext &filter_ctx,
      Datum &res)
{
  int ret = OB_SUCCESS;
  Datum *datum = nullptr;
  bool is_match = true;
  uint64_t hash_val = ObExprJoinFilter::JOIN_FILTER_SEED;
  ObIArray<Datum> &cur_row = filter_ctx.cur_row_;
  if (OB_UNLIKELY(!is_active_)) {
    res.set_int(1);
  } else if (OB_UNLIKELY(is_empty_)) {
    res.set_int(0);
    filter_ctx.filter_count_++;
    filter_ctx.check_count_++;
  } else if (OB_FAIL(ctx.get_datum_access_ctx(datum_access_ctx_))) {
  } else {
    for (int i = 0; OB_SUCC(ret) && i < expr.arg_cnt_; ++i) {
      if (OB_FAIL(expr.args_[i]->eval(ctx, datum))) {
      } else {
        cur_row.at(i) = *datum;
        ObHashFunc hash_func;
        hash_func.hash_func_ = filter_ctx.hash_funcs_.at(i).hash_func_;
        if (OB_FAIL(ret)) {
        } else if (OB_FAIL(hash_native_filter_value(*expr.args_[i], *datum, hash_func.hash_func_, hash_val, hash_val, datum_access_ctx_))) {
        }
      }
    }
    if (OB_SUCC(ret)) {
      ObRFInFilterNode node(
          &filter_ctx.cmp_funcs_, nullptr, &cur_row, nullptr, datum_access_ctx_, expr.args_, hash_val);
      if (OB_FAIL(rows_set_.exist_refactored(node))) {
        if (OB_HASH_NOT_EXIST == ret) {
          is_match = false;
          ret = OB_SUCCESS;
        } else if (OB_HASH_EXIST == ret) {
          is_match = true;
          ret = OB_SUCCESS;
        } else {
        }
      }
    }
    if (OB_SUCC(ret)) {
      if (!is_match) {
        filter_ctx.filter_count_++;
      }
      filter_ctx.check_count_++;
      res.set_int(is_match ? 1 : 0);
      filter_ctx.collect_sample_info(!is_match, 1);
    }
  }
  return ret;
}

int ObRFInFilterMsg::reuse()
{
  int ret = OB_SUCCESS;
  is_empty_ = true;
  serial_rows_.reset();
  rows_set_.reuse();
  (void)reuse_query_range();
  is_active_ = true;
  return ret;
}

int ObRFInFilterMsg::do_might_contain_batch(const ObExpr &expr,
    ObEvalCtx &ctx,
    const ObBitVector &skip,
    const int64_t batch_size,
    ObExprJoinFilter::ObExprJoinFilterContext &filter_ctx) {
  int ret = OB_SUCCESS;
  int64_t filter_count = 0;
  int64_t total_count = 0;
  if (filter_ctx.native_hash_values_.count() < batch_size) { return OB_ERR_UNEXPECTED; }
  uint64_t *right_hash_vals = filter_ctx.native_hash_values_.get_data();
  uint64_t seed = ObExprJoinFilter::JOIN_FILTER_SEED;
  for (int idx = 0; OB_SUCC(ret) && idx < expr.arg_cnt_; ++idx) {
    if (OB_FAIL(expr.args_[idx]->eval_batch(ctx, skip, batch_size))) {
    } else {
      const bool is_batch_seed = (idx > 0);
      ret = hash_native_filter_batch(*expr.args_[idx], filter_ctx.hash_funcs_.at(idx).hash_func_,
          right_hash_vals, expr.args_[idx]->locate_batch_datums(ctx), skip, batch_size,
          is_batch_seed ? right_hash_vals : &seed, is_batch_seed, datum_access_ctx_);
    }
  }
  ObIArray<Datum> &cur_row = filter_ctx.cur_row_;
  ObRFInFilterNode node(
      &filter_ctx.cmp_funcs_, nullptr, &cur_row, nullptr, datum_access_ctx_, expr.args_, 0);
  Datum *res_datums = expr.locate_batch_datums(ctx);
  for (int64_t batch_i = 0; OB_SUCC(ret) && batch_i < batch_size; ++batch_i) {
    if (skip.at(batch_i)) {
      continue;
    }
    total_count++;
    node.hash_val_ = right_hash_vals[batch_i];
    for (int64_t arg_i = 0; OB_SUCC(ret) && arg_i < expr.arg_cnt_; ++arg_i) {
      cur_row.at(arg_i) = expr.args_[arg_i]->locate_expr_datum(ctx, batch_i);
    } 
    if (OB_FAIL(ret)) {
    } else if (OB_FAIL(rows_set_.exist_refactored(node))) {
      if (OB_HASH_NOT_EXIST == ret) {
        res_datums[batch_i].set_int(0);
        filter_count++;
        ret = OB_SUCCESS;
      } else if (OB_HASH_EXIST == ret) {
        res_datums[batch_i].set_int(1);
        ret = OB_SUCCESS;
      } else {
      }
    }
  }
  if (OB_SUCC(ret)) {
    filter_ctx.filter_count_ += filter_count;
    filter_ctx.total_count_ += total_count;
    filter_ctx.check_count_ += total_count;
    filter_ctx.collect_sample_info(filter_count, total_count);
  }
  return ret;
}

int ObRFInFilterMsg::might_contain_batch(
    const ObExpr &expr,
    ObEvalCtx &ctx,
    const ObBitVector &skip,
    const int64_t batch_size,
    ObExprJoinFilter::ObExprJoinFilterContext &filter_ctx)
{
  int ret = OB_SUCCESS;
  Datum *results = expr.locate_batch_datums(ctx);
  ObBitVector &eval_flags = expr.get_evaluated_flags(ctx);
  ObEvalCtx::BatchInfoScopeGuard batch_info_guard(ctx);
  batch_info_guard.set_batch_size(batch_size);
  if (!is_active_) {
    for (int64_t i = 0; i < batch_size; i++) {
      results[i].set_int(1);
    }
  } else if (OB_UNLIKELY(is_empty_)) {
    for (int64_t i = 0; i < batch_size; i++) {
      results[i].set_int(0);
    }
  } else if (OB_FAIL(ctx.get_datum_access_ctx(datum_access_ctx_))) {
  } else if (OB_FAIL(do_might_contain_batch(expr, ctx, skip, batch_size, filter_ctx))) {
  }
  if (OB_SUCC(ret)) {
    eval_flags.set_all(batch_size);
  }
  return ret;
}

int ObRFInFilterMsg::prepare_storage_white_filter_data(ObDynamicFilterExecutor &dynamic_filter,
                                ObEvalCtx &eval_ctx,
                                ObRuntimeFilterParams &params,
                                bool &is_data_prepared)
{
  int ret = OB_SUCCESS;
  int col_idx = dynamic_filter.get_col_idx();
  if (!is_active_) {
    dynamic_filter.set_filter_action(DynamicFilterAction::PASS_ALL);
    is_data_prepared = true;
  } else if (is_empty_) {
    dynamic_filter.set_filter_action(DynamicFilterAction::FILTER_ALL);
    is_data_prepared = true;
  } else {
    for (int64_t i = 0; i < serial_rows_.count() && OB_SUCC(ret); ++i) {
      ObDatum physical;
      if (OB_FAIL(DatumParameterCodec::copy_legacy_value(serial_rows_.at(i)->at(col_idx),
          build_obj_metas_.at(col_idx), dynamic_filter.get_allocator(), physical))) {
      } else if (OB_FAIL(params.push_back(physical))) {
      }
    }
    if (OB_SUCC(ret)) {
      dynamic_filter.set_filter_val_meta(build_obj_metas_.at(col_idx));
      is_data_prepared = true;
    }
  }
  return ret;
}

int ObRFInFilterMsg::destroy()
{
  int ret = OB_SUCCESS;
  rows_set_.destroy();
  hash_funcs_for_insert_.reset();
  cmp_funcs_.reset();
  need_null_cmp_flags_.reset();
  build_obj_metas_.reset();
  cur_row_.reset();
  for (int i = 0; i < serial_rows_.count(); ++i) {
    if (OB_NOT_NULL(serial_rows_.at(i))) {
      serial_rows_.at(i)->reset();
    }
  }
  serial_rows_.reset();
  query_range_info_.destroy();
  query_range_.destroy();
  is_query_range_ready_ = false;
  datum_access_ctx_ = nullptr;
  query_range_allocator_.reset();
  allocator_.reset();
  return ret;
}

int ObRFInFilterMsg::prepare_query_ranges()
{
  int ret = OB_SUCCESS;
  (void)reuse_query_range();
  if (!rf_valid_query_columns(query_range_info_.range_column_cnt_,
                              query_range_info_.prefix_col_idxs_.count())) {
    ret = OB_INVALID_ARGUMENT;
  } else if (!query_range_info_.can_extract() || !is_active_) {
    is_query_range_ready_ = false;
  } else if (is_empty_) {
    // make empty range
    ObNewRange query_range;
    if (OB_FAIL(fill_empty_query_range(query_range_info_, query_range_allocator_, query_range))) {
    } else if (OB_FAIL(query_range_.push_back(query_range))) {
    } else {
      is_query_range_ready_ = true;
    }
  } else if (query_range_info_.prefix_col_idxs_.count() == col_cnt_) {
    // col count matches, the hashmap make sure all rows in the filter are different
    // so not need to deduplicate
    ret = process_query_ranges_without_deduplicate();
  } else {
    // prefix col less than index column, need do deduplicate
    // for example:
    // there are three rows int the filter :{[1,2,3], [1,2,4], [1,2,5]}
    // and the range column is c1,c2
    // final query range extracted should be: range(1,2; 1,2)
    // we need to deduplicate to avoid duplicate range
    ret = process_query_ranges_with_deduplicate();
  }
  LOG_TRACE("in filter prepare query range", K(ret), K(is_query_range_ready_),
            K(query_range_.count()), K(rows_set_.size()),
            K(query_range_info_.prefix_col_idxs_.count()), K(col_cnt_), K(query_range_),
            K(query_range_info_), K(is_empty_));
  return ret;
}

int ObRFInFilterMsg::process_query_ranges_with_deduplicate()
{
  int ret = OB_SUCCESS;
  int64_t max_in_filter_query_range_count = ObPxQueryRangeInfo::MAX_IN_FILTER_QUERY_RANGE_COUNT;

#ifdef ERRSIM
  int tmp_ret = OB_E(EventTable::EN_PX_MAX_IN_FILTER_QR_COUNT) OB_SUCCESS;
  if (OB_SUCCESS != tmp_ret) {
    max_in_filter_query_range_count = max_in_num_;
  }
#endif

  hash::ObHashSet<ObRFInFilterNode, hash::NoPthreadDefendMode> tmp_rows_set;
  ObArenaAllocator tmp_allocator;
  ObHashFuncs hash_func(tmp_allocator);
  ObCmpFuncs cmp_funcs(tmp_allocator);
  ObFixedArray<SqlTypeMeta, ObIAllocator> metas(tmp_allocator);
  const ObIArray<int64_t> &prefix_col_idxs = query_range_info_.prefix_col_idxs_;

  if (OB_FAIL(tmp_rows_set.create(rows_set_.size() * 2, "RFInTmpHashSet", "RFInTmpHashSet"))) {
  } else if (OB_FAIL(hash_func.init(prefix_col_idxs.count()))) {
  } else if (OB_FAIL(cmp_funcs.init(prefix_col_idxs.count()))) {
  } else if (OB_FAIL(metas.init(prefix_col_idxs.count()))) {
  }
  // reorder compare function and hash function
  for (int64_t j = 0; j < prefix_col_idxs.count() && OB_SUCC(ret); ++j) {
    int64_t col_idx = prefix_col_idxs.at(j);
    if (OB_FAIL(hash_func.push_back(hash_funcs_for_insert_.at(col_idx)))) {
    } else if (OB_FAIL(cmp_funcs.push_back(cmp_funcs_.at(col_idx)))) {
    } else if (OB_FAIL(metas.push_back(build_obj_metas_.at(col_idx)))) {
    }
  }
  ObTMArray<ObTMArray<Datum>> tmp_rows;
  ObTMArray<int64_t> effective_row_idxs;
  if (OB_FAIL(ret)) {
  } else if (OB_FAIL(tmp_rows.prepare_allocate(serial_rows_.count()))) {
  } else if (OB_FAIL(effective_row_idxs.reserve(serial_rows_.count()))) {
  }
  for (int64_t row_idx = 0; row_idx < serial_rows_.count() && OB_SUCC(ret); ++row_idx) {
    if (OB_ISNULL(serial_rows_.at(row_idx))) {
      ret = OB_ERR_UNEXPECTED;
    } else {
      ObTMArray<Datum> &tmp_row = tmp_rows.at(row_idx);
      if (OB_FAIL(tmp_row.prepare_allocate(prefix_col_idxs.count()))) {
      }
      for (int64_t j = 0; j < prefix_col_idxs.count() && OB_SUCC(ret); ++j) {
        int64_t col_idx = prefix_col_idxs.at(j);
        tmp_row.at(j) = serial_rows_.at(row_idx)->at(col_idx);
      }
      bool is_duplicate = true;
      if (OB_SUCC(ret)) {
        ObRFInFilterNode node(
            &cmp_funcs, &hash_func, &tmp_row, &metas, datum_access_ctx_);
        if (OB_FAIL(tmp_rows_set.set_refactored(node, 0/*not cover*/))) {
          if (ret != OB_HASH_EXIST) {
            LOG_WARN("failed to set_refactored");
          } else {
            ret = OB_SUCCESS;
          }
        } else {
          is_duplicate = false;
        }
      }
      if (!is_duplicate) {
        OZ(effective_row_idxs.push_back(row_idx));
        if (effective_row_idxs.count() > max_in_filter_query_range_count) {
          // no more than MAX_IN_FILTER_QUERY_RANGE_COUNT can be extracted
          // TODO[zhouhaiyu.zhy]: if the data of create table' prefix columns shows a high rate of
          // duplication and the final count of effective rows still exceeds
          // max_in_filter_query_range_count(128) the execution of the "prepare_query_ranges"
          // becomes redundant and may result in a decrease in performance.
          break;
        }
      }
    }
  }
  if (OB_SUCC(ret)) {
    if (effective_row_idxs.count() > max_in_filter_query_range_count) {
      is_query_range_ready_ = false;
    } else {
      if (OB_FAIL(query_range_.reserve(effective_row_idxs.count()))) {
      }
      for (int64_t i = 0; OB_SUCC(ret) && i < effective_row_idxs.count(); ++i) {
        OZ(generate_one_range(effective_row_idxs.at(i)));
      }
      if (OB_SUCC(ret)) {
        is_query_range_ready_ = true;
        LOG_DEBUG("TBDelete in filter succ extract query range", K(query_range_.count()),
                  K(serial_rows_.count()), K(query_range_));
      }
    }
  }
  return ret;
}

int ObRFInFilterMsg::process_query_ranges_without_deduplicate()
{
  int ret = OB_SUCCESS;
  int64_t max_in_filter_query_range_count = ObPxQueryRangeInfo::MAX_IN_FILTER_QUERY_RANGE_COUNT;

#ifdef ERRSIM
  int tmp_ret = OB_E(EventTable::EN_PX_MAX_IN_FILTER_QR_COUNT) OB_SUCCESS;
  if (OB_SUCCESS != tmp_ret) {
    max_in_filter_query_range_count = max_in_num_;
  }
#endif

  if (serial_rows_.count() > max_in_filter_query_range_count) {
    is_query_range_ready_ = false;
  } else {
    if (OB_FAIL(query_range_.reserve(serial_rows_.count()))) {
    }
    for (int64_t row_idx = 0; row_idx < serial_rows_.count() && OB_SUCC(ret); ++row_idx) {
      if (OB_ISNULL(serial_rows_.at(row_idx))) {
        ret = OB_ERR_UNEXPECTED;
      } else {
        OZ(generate_one_range(row_idx));
      }
    }
    if (OB_SUCC(ret)) {
      is_query_range_ready_ = true;
      LOG_DEBUG("TBDelete in filter succ extract query range", K(serial_rows_.count()),
                K(query_range_));
    }
  }
  return ret;
}

int ObRFInFilterMsg::generate_one_range(int row_idx)
{
  int ret = OB_SUCCESS;
  int64_t range_column_cnt = query_range_info_.range_column_cnt_;
  const ObIArray<int64_t> &prefix_col_idxs = query_range_info_.prefix_col_idxs_;
  const ObIArray<SqlTypeMeta> &prefix_col_obj_metas = query_range_info_.prefix_col_obj_metas_;

  ObNewRange query_range;
  query_range.table_id_ = query_range_info_.table_id_;
  query_range.border_flag_.set_inclusive_start();
  query_range.border_flag_.set_inclusive_end();
  DatumValue *start = NULL;
  DatumValue *end = NULL;
  if (OB_ISNULL(start = static_cast<DatumValue *>(
                    query_range_allocator_.alloc(sizeof(DatumValue) * range_column_cnt)))) {
    ret = OB_ALLOCATE_MEMORY_FAILED;
  } else if (OB_ISNULL(end = static_cast<DatumValue *>(
                           query_range_allocator_.alloc(sizeof(DatumValue) * range_column_cnt)))) {
    ret = OB_ALLOCATE_MEMORY_FAILED;
  }
  for (int64_t j = 0; j < prefix_col_idxs.count() && OB_SUCC(ret); ++j) {
    int64_t col_idx = prefix_col_idxs.at(j);
    const SqlTypeMeta &obj_meta = prefix_col_obj_metas.at(j);
    const Datum &datum = serial_rows_.at(row_idx)->at(col_idx);
    new (start + j) DatumValue();
    new (end + j) DatumValue();
    if (OB_FAIL(project_owned_filter_value(datum, build_obj_metas_.at(col_idx),
        obj_meta, start[j], query_range_allocator_))) {
    } else if (OB_FAIL(clone_typed_value(start[j], end[j], query_range_allocator_))) {
    }
  }
  for (int64_t j = prefix_col_idxs.count(); j < range_column_cnt && OB_SUCC(ret); ++j) {
    new (start + j) DatumValue();
    new (end + j) DatumValue();
    (start + j)->set_min_value();
    (end + j)->set_max_value();
  }
  if (OB_SUCC(ret)) {
    ObRowkey start_key(start, range_column_cnt);
    ObRowkey end_key(end, range_column_cnt);
    query_range.start_key_ = start_key;
    query_range.end_key_ = end_key;
    if (OB_FAIL(query_range_.push_back(query_range))) {
    }
  }
  return ret;
}

void ObRFInFilterMsg::after_process()
{
  // prepare_query_ranges can be failed, but rf still worked
  (void)prepare_query_ranges();
}

int ObRFInFilterMsg::try_extract_query_range(bool &has_extract, ObIArray<ObNewRange> &ranges,
                                             bool need_deep_copy, common::ObIAllocator *allocator)
{
  int ret = OB_SUCCESS;
  if (!is_query_range_ready_) {
    has_extract = false;
  } else {
    // overwrite ranges
    ranges.reset();
    if (need_deep_copy) {
      if (OB_FAIL(ranges.prepare_allocate(query_range_.count()))) {
      } else if (need_deep_copy) {
        for (int64_t i = 0; i < ranges.count() && OB_SUCC(ret); ++i) {
          if (OB_FAIL(deep_copy_range(*allocator, query_range_.at(i), ranges.at(i)))) {
          }
        }
      }
    } else {
      if (OB_FAIL(ranges.assign(query_range_))) {
      }
    }
    if (OB_SUCC(ret)) {
      has_extract = true;
    }
  }
  return ret;
}

//end ObRFInFilterMsg
