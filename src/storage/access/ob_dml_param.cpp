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
#include "ob_dml_param.h"

namespace oceanbase
{
namespace sql
{
namespace
{
// One immutable complete type, with no registry and no per-row construction.
struct NativeBigintType
{
  NativeBigintType() : type_(), result_(common::OB_SUCCESS)
  {
    common::SqlTypeMeta meta;
    meta.set_int();
    result_ = common::SqlType::from_legacy(meta, common::ObAccuracy(20, 0), type_);
  }
  common::SqlType type_;
  int result_;
};
}

NativeBigintOutput::NativeBigintOutput()
  : values_(), output_exprs_(), storage_indexes_(), type_(nullptr), count_(0),
    generation_(0), layout_bound_(false), candidate_(false), ready_(false)
{}

void NativeBigintOutput::invalidate()
{
  ++generation_;
  candidate_ = false;
  ready_ = false;
}

int NativeBigintOutput::prepare(const common::ObIArray<ObExpr *> &outputs, bool &supported)
{
  int ret = common::OB_SUCCESS;
  supported = false;
  invalidate();
  count_ = 0;
  layout_bound_ = false;
  bool valid = outputs.count() > 0 && outputs.count() <= MAX_COLUMNS;
  for (int64_t i = 0; valid && i < outputs.count(); ++i) {
    const ObExpr *expr = outputs.at(i);
    valid = expr != nullptr && expr->type_ == T_REF_COLUMN && expr->arg_cnt_ == 0
         && expr->obj_meta_.get_type() == common::ObIntType
         && expr->datum_meta_.type_ == common::ObIntType
         && expr->obj_datum_map_ == common::OBJ_DATUM_8BYTE_DATA;
  }
  if (valid) {
    static const NativeBigintType bigint;
    ret = bigint.result_;
    if (ret == common::OB_SUCCESS) {
      type_ = &bigint.type_;
      count_ = outputs.count();
      for (int64_t i = 0; i < count_; ++i) { output_exprs_[i] = outputs.at(i); }
      supported = true;
    }
  }
  return ret;
}

void NativeBigintOutput::bind_projector(const common::ObIArray<ObExpr *> &access_exprs,
                                       const common::ObIArray<int32_t> &projector)
{
  invalidate();
  layout_bound_ = count_ > 0 && access_exprs.count() == projector.count();
  for (int64_t i = 0; layout_bound_ && i < count_; ++i) {
    int32_t storage_index = -1;
    for (int64_t j = 0; layout_bound_ && j < access_exprs.count(); ++j) {
      if (access_exprs.at(j) == output_exprs_[i]) {
        const int32_t next = projector.at(j);
        // Ambiguous or virtual mappings are a native fallback, not a change to
        // the existing storage projector's error/virtual-column behavior.
        if (next < 0 || (storage_index >= 0 && next != storage_index)) {
          layout_bound_ = false;
        } else {
          storage_index = next;
        }
      }
    }
    if (storage_index < 0) { layout_bound_ = false; }
    storage_indexes_[i] = storage_index;
  }
}

void NativeBigintOutput::project(const blocksstable::ObStorageDatum *datums, int64_t batch_idx)
{
  invalidate();
  bool valid = layout_bound_ && datums != nullptr && batch_idx == 0;
  // Preflight the entire row before replacing any slot. NOP must continue via
  // default fusion in the legacy path, never appear as a native NULL/value.
  for (int64_t i = 0; valid && i < count_; ++i) {
    const blocksstable::ObStorageDatum &datum = datums[storage_indexes_[i]];
    valid = datum.flag_ == common::ObDatum::FlagType::NONE
         && (datum.is_null() || (datum.len_ == sizeof(int64_t) && datum.ptr_ != nullptr));
  }
  if (valid) {
    for (int64_t i = 0; i < count_; ++i) {
      const blocksstable::ObStorageDatum &datum = datums[storage_indexes_[i]];
      if (datum.is_null()) {
        values_[i].set_null();
      } else {
        int64_t value;
        // Storage scalar buffers do not promise 8-byte alignment.
        MEMCPY(&value, datum.ptr_, sizeof(value));
        values_[i].set_int(value);
      }
    }
    candidate_ = true;
  }
}

void NativeBigintOutput::publish(int result, int64_t row_count, bool end, bool skipped)
{
  if (candidate_ && result == common::OB_SUCCESS && row_count == 1 && !end && !skipped) {
    ready_ = true;
  } else {
    invalidate();
  }
}

bool NativeBigintOutput::try_get(int64_t batch_idx, const common::Datum *&values,
                                const common::SqlType *&type, int64_t &count,
                                uint64_t &generation, const uint64_t *&current_generation) const
{
  values = nullptr;
  type = nullptr;
  count = 0;
  generation = 0;
  current_generation = nullptr;
  const bool valid = ready_ && batch_idx == 0;
  if (valid) {
    values = values_;
    type = type_;
    count = count_;
    generation = generation_;
    current_generation = &generation_;
  }
  return valid;
}

} // namespace sql
namespace storage
{
using namespace common;
using namespace share::schema;

int ObRow2ExprsProjector::init(const sql::ObExprPtrIArray &exprs,
                               sql::ObPushdownOperator &op,
                               const common::ObIArray<int32_t> &projector)
{
  int ret = OB_SUCCESS;
  op_ = &op;
  if (outputs_.empty()) { // not inited
    // first traverse, treat MapConvert::start_ as count
    if (op.is_vectorized()) {
      if (has_virtual_) {
        ret = OB_ERR_UNEXPECTED;
      } else {
        // reset datum pointers to reserved buffer.
        FOREACH_CNT_X(e, exprs, OB_SUCC(ret)) {
          if (!(*e)->is_batch_result()) {
            ret = OB_ERR_UNEXPECTED;
          } else {
            (*e)->locate_datums_for_update(op.get_eval_ctx(), op.get_batch_size());
          }
        }
      }
    }


    if (OB_FAIL(ret)) {
    } else if (OB_FAIL(outputs_.prepare_allocate(exprs.count()))) {
    } else {
      for (int64_t i = 0; i < exprs.count(); i++) {
        sql::ObExpr *e = exprs.at(i);
        // output should always be T_COLUMN_REF, only virtual column has argument.
        if (e->arg_cnt_ > 0) {
          has_virtual_ = true;
        } else {
          const int32_t obj_idx = projector.at(i);
          if (obj_idx >= 0) {
            common::SqlValueDatumMapType map_type = e->obj_datum_map_;
            if (num_.map_type_ == map_type) {
              num_.start_++;
            } else if (str_.map_type_ == map_type) {
              str_.start_++;
            } else if (int_.map_type_ == map_type) {
              int_.start_++;
            }
          }
        }
      }

      // setup MapConvert::start_ and assign %start_ to %end_
      other_idx_ = num_.start_ + str_.start_ + int_.start_;
      int_.start_ = int_.end_ = num_.start_ + str_.start_;
      str_.start_ = str_.end_ = num_.start_;
      num_.start_ = num_.end_ = 0;

      int64_t other_end = other_idx_;
      // second traverse, setup MapConvert::end_ && outputs_
      for (int64_t i = 0; i < exprs.count(); i++) {
        sql::ObExpr *e = exprs.at(i);
        const int32_t obj_idx = projector.at(i);
        int64_t item_idx = 0;
        if (e->arg_cnt_ == 0 && obj_idx >= 0) {
          common::SqlValueDatumMapType map_type = e->obj_datum_map_;
          if (num_.map_type_ == map_type) {
            item_idx = num_.end_++;
          } else if (str_.map_type_ == map_type) {
            item_idx = str_.end_++;
          } else if (int_.map_type_ == map_type) {
            item_idx = int_.end_++;
          } else {
            item_idx = other_end++;
          }
        } else {
          item_idx = other_end++;
        }
        Item &item = outputs_[item_idx];
        item.obj_idx_ = obj_idx;
        item.expr_idx_ = i;
        if (!op.is_vectorized()) {
          item.datum_ = &e->locate_datum_for_write(op_->get_eval_ctx());
        } else {
          item.datum_ = e->locate_batch_datums(op_->get_eval_ctx());
          item.eval_flags_ = &e->get_evaluated_flags(op_->get_eval_ctx());
        }
        item.eval_info_ = &e->get_eval_info(op_->get_eval_ctx());
        item.data_ = item.datum_->ptr_;
      }
    }
  }
  if (native_output_ != nullptr) {
    if (OB_SUCC(ret)) { native_output_->bind_projector(exprs, projector); }
    else { native_output_->invalidate(); }
  }
  return ret;
};

template <common::SqlValueDatumMapType OBJ_DATUM_MAP_TYPE, bool NEED_RESET_PTR>
OB_INLINE void ObRow2ExprsProjector::MapConvert<OBJ_DATUM_MAP_TYPE, NEED_RESET_PTR>::project(
    const ObRow2ExprsProjector::Item *items, const blocksstable::ObStorageDatum *datums,
    int16_t *nop_pos, int64_t &nop_cnt) const
{
  // performance critical, no parameter validity check.
  for (int32_t i = start_; i < end_; i++) {
    const Item &item = items[i];
    const blocksstable::ObStorageDatum &datum = datums[item.obj_idx_];
    if (OB_UNLIKELY(datum.is_nop())) {
      nop_pos[nop_cnt++] = item.expr_idx_;
    } else if (OB_UNLIKELY(datum.is_null())) {
      item.datum_->set_null();
    } else {
      if (NEED_RESET_PTR) {
        if (OB_UNLIKELY(item.datum_->ptr_ != item.data_)) {
          item.datum_->ptr_ = item.data_;
        }
      }
      item.datum_->datum2datum<OBJ_DATUM_MAP_TYPE>(datum);
    }
  }
}

template <common::SqlValueDatumMapType OBJ_DATUM_MAP_TYPE, bool NEED_RESET_PTR>
OB_INLINE void ObRow2ExprsProjector::MapConvert<OBJ_DATUM_MAP_TYPE,
          NEED_RESET_PTR>::project_batch_datum(
              const ObRow2ExprsProjector::Item *items, const blocksstable::ObStorageDatum *datums,
              int16_t *nop_pos, int64_t &nop_cnt, const int64_t idx) const
{
  // performance critical, no parameter validity check.
  for (int32_t i = start_; i < end_; i++) {
    const Item &item = items[i];
    const blocksstable::ObStorageDatum &datum = datums[item.obj_idx_];
    if (OB_UNLIKELY(datum.is_nop())) {
      nop_pos[nop_cnt++] = item.expr_idx_;
    } else if (OB_UNLIKELY(datum.is_null())) {
      item.datum_[idx].set_null();
    } else {
      item.datum_[idx].datum2datum<OBJ_DATUM_MAP_TYPE>(datum);
    }
  }
}

int ObRow2ExprsProjector::project(const sql::ObExprPtrIArray &exprs,
                                  const blocksstable::ObStorageDatum *datums,
                                  int16_t *nop_pos,
                                  int64_t &nop_cnt)
{
  // performance critical, no parameter validity check.
  int ret = OB_SUCCESS;
  if (!op_->is_vectorized()) {
    num_.project(outputs_.get_data(), datums, nop_pos, nop_cnt);
    str_.project(outputs_.get_data(), datums, nop_pos, nop_cnt);
    int_.project(outputs_.get_data(), datums, nop_pos, nop_cnt);

    for (int64_t i = other_idx_; OB_SUCC(ret) && i < outputs_.count(); i++) {
      const Item &item = outputs_.at(i);
      const blocksstable::ObStorageDatum *datum = NULL;
      if (OB_UNLIKELY(item.obj_idx_ < 0
                      || (datum = &datums[item.obj_idx_])->is_nop())) {
        nop_pos[nop_cnt++] = item.expr_idx_;
        item.eval_info_->evaluated_ = false;
      } else if (OB_UNLIKELY(datum->is_null())) {
        item.datum_->set_null();
        item.eval_info_->evaluated_ = true;
        item.eval_info_->projected_ = true;
      } else {
        if (OB_UNLIKELY(item.datum_->ptr_ != item.data_)) {
          item.datum_->ptr_ = item.data_;
        }
        if (OB_FAIL(item.datum_->from_storage_datum(*datum, exprs.at(item.expr_idx_)->obj_datum_map_))) {
        } else {
          // the other items may contain virtual columns, set evaluated flag.
          item.eval_info_->evaluated_ = true;
          item.eval_info_->projected_ = true;
        }
      }
    }
  } else {
    const int64_t idx = op_->get_eval_ctx().get_batch_idx();
    num_.project_batch_datum(outputs_.get_data(), datums, nop_pos, nop_cnt, idx);
    str_.project_batch_datum(outputs_.get_data(), datums, nop_pos, nop_cnt, idx);
    int_.project_batch_datum(outputs_.get_data(), datums, nop_pos, nop_cnt, idx);

    for (int64_t i = other_idx_; OB_SUCC(ret) && i < outputs_.count(); i++) {
      const Item &item = outputs_.at(i);
      const blocksstable::ObStorageDatum *datum = NULL;
      item.eval_info_->evaluated_ = true;
      if (OB_UNLIKELY(item.obj_idx_ < 0
                      || (datum = &datums[item.obj_idx_])->is_nop())) {
        nop_pos[nop_cnt++] = item.expr_idx_;
        item.eval_flags_->unset(idx);
      } else if (OB_UNLIKELY(datum->is_null())) {
        item.datum_[idx].set_null();
        item.eval_flags_->set(idx);
      } else {
        if (OB_FAIL(item.datum_[idx].from_storage_datum(
                    *datum, exprs.at(item.expr_idx_)->obj_datum_map_))) {
        } else {
          // the other items may contain virtual columns, set evaluated flag.
          item.eval_flags_->set(idx);
        }
      }
    }
  }

  if (native_output_ != nullptr) {
    if (OB_SUCC(ret)) {
      native_output_->project(datums, op_->is_vectorized() ? op_->get_eval_ctx().get_batch_idx() : 0);
    } else {
      native_output_->invalidate();
    }
  }
  return ret;
}

DEF_TO_STRING(ObDMLBaseParam)
{
  int64_t pos = 0;
  J_OBJ_START();
  J_KV(N_TIMEOUT, timeout_,
       N_SCHEMA_VERSION, schema_version_,
       N_SQL_MODE, sql_mode_,
       N_IS_TOTAL_QUANTITY_LOG, is_total_quantity_log_,
       KPC_(table_param),
       K_(runtime_schema_version),
       K_(is_ignore),
       K_(prelock),
       K_(is_batch_stmt),
       K_(write_flag),
       K_(spec_seq_no),
       K_(snapshot),
       K_(branch_id),
       K_(check_schema_version),
       K_(ddl_task_id),
       K_(is_main_table_in_fts_ddl),
       K_(has_async_index));
  J_OBJ_END();
  return pos;
}

DEF_TO_STRING(ObRow2ExprsProjector::Item)
{
  int64_t pos = 0;
  J_OBJ_START();
  J_KV(K(obj_idx_), K(expr_idx_), K(datum_), K(eval_info_), K(data_));
  J_OBJ_END();
  return pos;
}

int ScanResumePoint::init(bool *is_paused)
{
  int ret = OB_SUCCESS;
  if (OB_ISNULL(is_paused)) {
    ret = OB_ERR_UNEXPECTED;
  } else {
    is_paused_ = is_paused;
    ATOMIC_STORE(is_paused_, false);
    
    allocator_.set_label("ScanResumePoint");
  }
  return ret;
}

DEF_TO_STRING(ObTableScanParam)
{
  int64_t pos = 0;
  J_OBJ_START();
  J_KV(K_(tablet_id),
       N_COLUMN_IDS, column_ids_,
       N_INDEX_ID, index_id_,
       N_KEY_RANGES, key_ranges_,
       KPC_(datum_lookup_keys),
       K_(range_array_pos),
       N_TIMEOUT, timeout_,
       N_SCAN_FLAG, scan_flag_,
       N_SQL_MODE, sql_mode_,
       N_RESERVED_CELL_COUNT, reserved_cell_count_,
       N_SCHEMA_VERSION, schema_version_,
       N_QUERY_BEGIN_SCHEMA_VERSION, runtime_schema_version_,
       N_LIMIT_OFFSET, limit_param_,
       N_FOR_UPDATE, for_update_,
       N_WAIT, for_update_wait_timeout_,
       N_FROZEN_VERSION, frozen_version_,
       K_(is_get),
       KPC_(output_exprs),
       KPC_(op_filters),
       KPC_(trans_desc),
       K_(snapshot),
       KPC_(table_param),
       K_(sample_info),
       K_(need_scn),
       K_(need_switch_param),
       K_(is_mds_query),
       K_(tx_seq_base),
       K_(need_update_tablet_param),
       KPC_(mds_collector));
  J_OBJ_END();
  return pos;
}

int ScanResumePoint::add_range(const ObITableReadInfo& read_info, const blocksstable::ObDatumRange& datum_range)
{  
  INIT_SUCC(ret);
  SqlTypeMeta* obj_metas;
  ObDatumRange tmp_range;
  ObNewRange new_range;
  int rowkey_cnt = read_info.get_schema_rowkey_count();

  if (OB_ISNULL(obj_metas = (SqlTypeMeta*)allocator_.alloc(rowkey_cnt * sizeof(SqlTypeMeta)))) {
    ret = OB_ALLOCATE_MEMORY_FAILED;
    LOG_WARN("failed to allocate memory for obj_metas", K(rowkey_cnt));
  } else {
    const common::ObIArray<ObColDesc>& col_desc = read_info.get_columns_desc();
    for (int i = 0; i < rowkey_cnt; ++i) {
      obj_metas[i] = col_desc.at(i).col_type_;
    }
  }

  if (OB_FAIL(ret)) {
  } else if (OB_FAIL(tmp_range.deep_copy(datum_range, allocator_))) {
  } else if (OB_FAIL(tmp_range.to_new_range(new_range, obj_metas, allocator_))) {
  } else if (OB_FAIL(ranges_.push_back(new_range))) {
  }

  return ret;
}

}
}
