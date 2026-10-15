/*
 * Copyright (c) 2026 OceanBase.
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
#include "sql/engine/graph/graph_das_access_utils.h"

#include "sql/das/ob_das_scan_op.h"
#include "sql/engine/ob_exec_context.h"
#include "sql/engine/ob_operator.h"
#include "sql/engine/ob_physical_plan_ctx.h"
#include "sql/session/ob_sql_session_info.h"

namespace oceanbase
{
using namespace common;
namespace sql
{
namespace graph_das_detail
{

int close_das_tasks(ObDASRef &das_ref, int ret)
{
  const int close_ret = das_ref.close_all_task();
  if (OB_SUCCESS != close_ret) {
    LOG_WARN("failed to close graph DAS tasks", K(close_ret), K(ret));
    if (OB_SUCCESS == ret) {
      ret = close_ret;
    }
  }
  return ret;
}

int init_scan_rtdef(
    ObExecContext &exec_ctx,
    ObEvalCtx &eval_ctx,
    const GraphExpandScanDesc &scan_desc,
    const ObDASScanCtDef &scan_ctdef,
    const ObDASTableLocMeta *loc_meta,
    bool uses_index_back,
    ObIAllocator &scan_allocator,
    ObDASScanRtDef &scan_rtdef)
{
  int ret = OB_SUCCESS;
  ObPhysicalPlanCtx *plan_ctx = exec_ctx.get_physical_plan_ctx();
  ObSQLSessionInfo *session = exec_ctx.get_my_session();
  if (OB_ISNULL(plan_ctx) || OB_ISNULL(session)) {
    ret = OB_ERR_UNEXPECTED;
    LOG_WARN("graph DAS scan runtime context is incomplete", K(ret),
             K(plan_ctx), K(session));
  } else {
    const ObTableScanCtDef &tsc_ctdef = scan_desc.get_tsc_ctdef();
    const bool is_lookup = &scan_ctdef == tsc_ctdef.lookup_ctdef_;
    scan_rtdef.ctdef_ = &scan_ctdef;
    scan_rtdef.timeout_ts_ = plan_ctx->get_ps_timeout_timestamp();
    scan_rtdef.tx_lock_timeout_ = session->get_trx_lock_timeout();
    scan_rtdef.scan_flag_ = tsc_ctdef.scan_flags_;
    scan_rtdef.scan_flag_.scan_order_ = uses_index_back && is_lookup
        ? ObQueryFlag::KeepOrder : ObQueryFlag::NoOrder;
    scan_rtdef.scan_flag_.index_back_ = uses_index_back;
    scan_rtdef.scan_flag_.is_need_feedback_ = false;
    scan_rtdef.need_check_output_datum_ = scan_desc.need_check_output_datum_;
    scan_rtdef.sql_mode_ = session->get_sql_mode();
    scan_rtdef.stmt_allocator_.set_alloc(&scan_allocator);
    scan_rtdef.scan_allocator_.set_alloc(&scan_allocator);
    scan_rtdef.eval_ctx_ = &eval_ctx;
    scan_rtdef.frozen_version_ = scan_desc.frozen_version_;
    scan_rtdef.scan_op_id_ = scan_desc.get_id();
    scan_rtdef.scan_rows_size_ = scan_desc.rows_ * scan_desc.width_;
    scan_rtdef.runtime_schema_version_
        = exec_ctx.get_sql_exec_ctx().get_query_begin_schema_version();
    if (OB_FAIL(scan_rtdef.init_pd_op(exec_ctx, scan_ctdef))) {
      LOG_WARN("failed to initialize graph DAS scan pushdown", K(ret),
               K(scan_ctdef));
    } else if (OB_FAIL(tsc_ctdef.snapshot_item_
                           .set_snapshot_query_info(eval_ctx, scan_rtdef))) {
      LOG_WARN("failed to initialize graph DAS scan snapshot", K(ret),
               K(scan_desc.get_id()));
    } else {
      scan_rtdef.table_loc_ = DAS_CTX(exec_ctx).get_table_loc_by_id(
          scan_desc.get_table_loc_id(), scan_ctdef.ref_table_id_);
      if (scan_rtdef.table_loc_ == nullptr
          && loc_meta != nullptr) {
        ret = DAS_CTX(exec_ctx).extended_table_loc(
            *loc_meta, scan_rtdef.table_loc_);
      } else if (scan_rtdef.table_loc_ == nullptr
                 && &scan_ctdef == tsc_ctdef.lookup_ctdef_
                 && tsc_ctdef.lookup_loc_meta_ != nullptr) {
        ret = DAS_CTX(exec_ctx).extended_table_loc(
            *tsc_ctdef.lookup_loc_meta_, scan_rtdef.table_loc_);
      }
      if (OB_SUCC(ret)
          && (OB_ISNULL(scan_rtdef.table_loc_)
              || OB_ISNULL(scan_rtdef.table_loc_->loc_meta_))) {
        ret = OB_ERR_UNEXPECTED;
        LOG_WARN("graph DAS scan table location metadata is missing", K(ret),
                 K(scan_desc.get_table_loc_id()), K(scan_ctdef.ref_table_id_));
      } else if (OB_SUCC(ret)
                 && scan_rtdef.table_loc_->loc_meta_->is_weak_read_) {
        scan_rtdef.scan_flag_.set_is_select_follower();
      }
      if (OB_SUCC(ret) && scan_desc.ref_table_id_ != scan_ctdef.ref_table_id_) {
        scan_rtdef.need_scn_ = false;
      }
    }
  }
  return ret;
}

int materialize_identity(
    ObEvalCtx &eval_ctx,
    ObIAllocator &allocator,
    uint64_t graph_id,
    uint64_t element_id,
    int64_t key_count,
    ObExpr *const *key_exprs,
    GraphElementIdentity &identity)
{
  int ret = OB_SUCCESS;
  identity.reset();
  if (OB_UNLIKELY(element_id == OB_INVALID_ID || key_count <= 0
                  || key_count > OB_USER_MAX_ROWKEY_COLUMN_NUMBER
                  || key_exprs == nullptr)) {
    ret = OB_INVALID_ARGUMENT;
  } else {
    identity.graph_id_ = graph_id;
    identity.element_id_ = element_id;
    ObObj *keys = static_cast<ObObj *>(
        allocator.alloc(sizeof(ObObj) * key_count));
    if (OB_ISNULL(keys)) {
      ret = OB_ALLOCATE_MEMORY_FAILED;
    } else {
      new(keys) ObObj[key_count];
      identity.rowkey_.assign(keys, key_count);
    }
  }
  for (int64_t key = 0; OB_SUCC(ret) && key < key_count; ++key) {
    ObDatum *datum = nullptr;
    ObObj value;
    ObExpr *expr = key_exprs[key];
    if (OB_ISNULL(expr)) {
      ret = OB_ERR_UNEXPECTED;
    } else if (OB_FAIL(expr->eval(eval_ctx, datum))) {
    } else if (OB_ISNULL(datum) || datum->is_null()) {
      ret = OB_ERR_UNEXPECTED;
    } else if (OB_FAIL(datum->to_obj(value, expr->obj_meta_,
                                     expr->obj_datum_map_))) {
    } else if (OB_FAIL(ob_write_obj(allocator, value,
                                    identity.rowkey_.get_obj_ptr()[key]))) {
    }
  }
  return ret;
}
} // namespace graph_das_detail

} // namespace sql
} // namespace oceanbase
