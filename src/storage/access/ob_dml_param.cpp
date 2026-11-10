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
  if (exprs.count() != projector.count()) {
    ret = OB_INVALID_ARGUMENT;
  } else if (outputs_.empty() && OB_FAIL(outputs_.prepare_allocate(exprs.count()))) {
  } else {
    for (int64_t i = 0; OB_SUCC(ret) && i < exprs.count(); ++i) {
      sql::ObExpr *expr = exprs.at(i);
      if (expr == nullptr) {
        ret = OB_INVALID_ARGUMENT;
      } else {
        Item &item = outputs_.at(i);
        item.obj_idx_ = projector.at(i);
        item.expr_idx_ = i;
        if (OB_FAIL(item.target_.prepare(*expr, op.get_eval_ctx()))) { break; }
        item.eval_info_ = &expr->get_eval_info(op.get_eval_ctx());
        item.eval_flags_ = expr->is_batch_result()
            ? &expr->get_evaluated_flags(op.get_eval_ctx()) : nullptr;
        has_virtual_ |= expr->arg_cnt_ > 0;
      }
    }
  }
  return ret;
}

int ObRow2ExprsProjector::project(const sql::ObExprPtrIArray &exprs,
                                  const blocksstable::ObStorageDatum *datums,
                                  int16_t *nop_pos, int64_t &nop_cnt)
{
  int ret = OB_SUCCESS;
  const int64_t batch_idx = op_->is_vectorized() ? op_->get_eval_ctx().get_batch_idx() : 0;
  // Every column is imported into its sole selected execution frame. Storage NOPs
  // retain the existing default/virtual-column path; there is no second output
  // owner that can bypass this projection or generic expression evaluation.
  for (int64_t i = 0; OB_SUCC(ret) && i < outputs_.count(); ++i) {
    const Item &item = outputs_.at(i);
    const sql::ObExpr &expr = *exprs.at(item.expr_idx_);
    const int64_t idx = expr.is_batch_result() ? batch_idx : 0;
    if (item.obj_idx_ < 0 || datums[item.obj_idx_].is_nop()) {
      nop_pos[nop_cnt++] = item.expr_idx_;
      if (item.eval_flags_ != nullptr) { item.eval_flags_->unset(idx); }
      else { item.eval_info_->evaluated_ = false; }
    } else if (OB_FAIL(item.target_.import_legacy(
                   expr.get_datum_copy_plan(), datums[item.obj_idx_], idx))) {
    } else {
      item.eval_info_->evaluated_ = true;
      item.eval_info_->projected_ = true;
      if (item.eval_flags_ != nullptr) { item.eval_flags_->set(idx); }
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
  J_KV(K(obj_idx_), K(expr_idx_), "native_i64", target_.is_native(), K(eval_info_));
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
