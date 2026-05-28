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

#include "sql/engine/basic/ob_diff_table_op.h"
#include "sql/engine/ob_exec_context.h"
#include "sql/engine/expr/ob_expr_lob_utils.h"
#include "sql/engine/basic/ob_diff_table_op_compute.h"
#include "sql/resolver/cmd/ob_diff_table_stmt.h"
#include "sql/session/ob_sql_session_info.h"
#include "share/schema/ob_schema_getter_guard.h"

namespace oceanbase
{
using namespace common;
using namespace share;
using namespace share::schema;
namespace sql
{

OB_DEF_SERIALIZE(ObDiffOutColMeta)
{
  int ret = OB_SUCCESS;
  LST_DO_CODE(OB_UNIS_ENCODE,
              kind_, col_id_, obj_type_, collation_type_,
              length_, subschema_id_);
  return ret;
}
OB_DEF_DESERIALIZE(ObDiffOutColMeta)
{
  int ret = OB_SUCCESS;
  LST_DO_CODE(OB_UNIS_DECODE,
              kind_, col_id_, obj_type_, collation_type_,
              length_, subschema_id_);
  return ret;
}
OB_DEF_SERIALIZE_SIZE(ObDiffOutColMeta)
{
  int64_t len = 0;
  LST_DO_CODE(OB_UNIS_ADD_LEN,
              kind_, col_id_, obj_type_, collation_type_,
              length_, subschema_id_);
  return len;
}

OB_SERIALIZE_MEMBER((ObDiffTableSpec, ObOpSpec),
                    tenant_id_,
                    cur_table_id_, inc_table_id_,
                    cur_db_name_, cur_table_name_,
                    inc_db_name_, inc_table_name_,
                    pk_col_ids_, val_col_ids_,
                    out_col_metas_);

ObDiffTableOp::ObDiffTableOp(ObExecContext &exec_ctx, const ObOpSpec &spec, ObOpInput *input)
  : ObOperator(exec_ctx, spec, input),
    op_alloc_("DiffTblOp", OB_MALLOC_NORMAL_BLOCK_SIZE,
              MTL_ID()),
    rows_(),
    cursor_(0)
{
}

int ObDiffTableOp::inner_open()
{
  int ret = OB_SUCCESS;
  cursor_ = 0;
  rows_.reset();
  if (OB_FAIL(compute_())) {
    LOG_WARN("compute diff rows failed", K(ret));
  }
  return ret;
}

int ObDiffTableOp::inner_rescan()
{
  // Re-execute from scratch on rescan: discard cached rows and recompute.
  if (OB_SUCCESS != ObOperator::inner_rescan()) {
    // intentional: even if base rescan reports an issue we still try to
    // re-init our state — matches what other leaf ops do.
  }
  op_alloc_.reset();
  rows_.reset();
  cursor_ = 0;
  return compute_();
}

int ObDiffTableOp::inner_close()
{
  rows_.reset();
  cursor_ = 0;
  op_alloc_.reset();
  return OB_SUCCESS;
}

void ObDiffTableOp::destroy()
{
  rows_.reset();
  op_alloc_.reset();
  ObOperator::destroy();
}

int ObDiffTableOp::inner_get_next_row()
{
  int ret = OB_SUCCESS;
  if (cursor_ >= rows_.count()) {
    ret = OB_ITER_END;
  } else if (OB_FAIL(project_current_row_())) {
    LOG_WARN("project current row failed", K(ret), K_(cursor));
  } else {
    cursor_++;
  }
  return ret;
}

int ObDiffTableOp::project_current_row_()
{
  int ret = OB_SUCCESS;
  const ObNewRow *row = rows_.at(cursor_);
  clear_evaluated_flag();
  if (OB_ISNULL(row) || row->count_ != MY_SPEC.output_.count()) {
    ret = OB_ERR_UNEXPECTED;
    LOG_WARN("row shape mismatch", K(ret), KP(row),
             "out_cnt", MY_SPEC.output_.count());
  }
  for (int64_t i = 0; OB_SUCC(ret) && i < MY_SPEC.output_.count(); ++i) {
    const ObObj &cell = row->cells_[i];
    ObExpr *expr = MY_SPEC.output_.at(i);
    ObDatum &datum = expr->locate_datum_for_write(eval_ctx_);
    if (cell.is_null()) {
      datum.set_null();
    } else if (cell.get_type() != expr->datum_meta_.type_) {
      ret = OB_ERR_UNEXPECTED;
      LOG_WARN("type mismatch", K(ret), K(i),
               "cell_type", cell.get_type(),
               "expr_type", expr->datum_meta_.type_);
    } else if (OB_FAIL(datum.from_obj(cell, expr->obj_datum_map_))) {
      LOG_WARN("from_obj failed", K(ret), K(i));
    } else if (is_lob_storage(cell.get_type())
               && OB_FAIL(ob_adjust_lob_datum(cell, expr->obj_meta_,
                                              expr->obj_datum_map_,
                                              get_exec_ctx().get_allocator(),
                                              datum))) {
      LOG_WARN("adjust lob datum failed", K(ret));
    } else {
      expr->set_evaluated_projected(eval_ctx_);
    }
  }
  return ret;
}

int ObDiffTableOp::compute_()
{
  int ret = OB_SUCCESS;
  ObSQLSessionInfo *session = ctx_.get_my_session();
  ObSqlCtx *sql_ctx = ctx_.get_sql_ctx();
  ObSchemaGetterGuard *guard = (sql_ctx != nullptr) ? sql_ctx->schema_guard_ : nullptr;
  if (OB_ISNULL(session) || OB_ISNULL(guard)) {
    ret = OB_ERR_UNEXPECTED;
    LOG_WARN("session or schema guard not ready", K(ret), KP(session), KP(guard));
  } else {
    // Build a transient ObDiffTableStmt-shaped param container from spec
    // so we can reuse the existing compute helpers without further
    // refactor. The container's lifetime is this method only.
    ObDiffTableStmt param;
    param.set_tenant_id(MY_SPEC.tenant_id_);
    param.set_cur_table_id(MY_SPEC.cur_table_id_);
    param.set_inc_table_id(MY_SPEC.inc_table_id_);
    param.set_cur_db(MY_SPEC.cur_db_name_);
    param.set_cur_table(MY_SPEC.cur_table_name_);
    param.set_inc_db(MY_SPEC.inc_db_name_);
    param.set_inc_table(MY_SPEC.inc_table_name_);
    for (int64_t i = 0; OB_SUCC(ret) && i < MY_SPEC.pk_col_ids_.count(); ++i) {
      // Compute helper looks up col by name, but we have only ids in
      // spec. Translate ids → names from cur_schema.
      const ObTableSchema *s = nullptr;
      const ObColumnSchemaV2 *col = nullptr;
      if (OB_FAIL(guard->get_table_schema(MY_SPEC.tenant_id_,
                                          MY_SPEC.cur_table_id_, s))) {
        LOG_WARN("get cur schema failed", K(ret));
      } else if (OB_ISNULL(s)) {
        ret = OB_TABLE_NOT_EXIST;
      } else if (OB_ISNULL(col = s->get_column_schema(MY_SPEC.pk_col_ids_.at(i)))) {
        ret = OB_ERR_UNEXPECTED;
      } else if (OB_FAIL(param.pk_cols().push_back(col->get_column_name_str()))) {
        LOG_WARN("push pk col failed", K(ret));
      }
    }
    for (int64_t i = 0; OB_SUCC(ret) && i < MY_SPEC.val_col_ids_.count(); ++i) {
      const ObTableSchema *s = nullptr;
      const ObColumnSchemaV2 *col = nullptr;
      if (OB_FAIL(guard->get_table_schema(MY_SPEC.tenant_id_,
                                          MY_SPEC.cur_table_id_, s))) {
        LOG_WARN("get cur schema failed", K(ret));
      } else if (OB_ISNULL(s)) {
        ret = OB_TABLE_NOT_EXIST;
      } else if (OB_ISNULL(col = s->get_column_schema(MY_SPEC.val_col_ids_.at(i)))) {
        ret = OB_ERR_UNEXPECTED;
      } else if (OB_FAIL(param.val_cols().push_back(col->get_column_name_str()))) {
        LOG_WARN("push val col failed", K(ret));
      }
    }
    for (int64_t i = 0; OB_SUCC(ret) && i < MY_SPEC.out_col_metas_.count(); ++i) {
      const ObDiffOutColMeta &m = MY_SPEC.out_col_metas_.at(i);
      ObDiffOutputCol c;
      c.obj_type_ = m.obj_type_;
      c.collation_type_ = m.collation_type_;
      c.length_ = m.length_;
      c.col_id_ = m.col_id_;
      c.is_synth_ = (m.kind_ == ObDiffOutColMeta::K_TABLE || m.kind_ == ObDiffOutColMeta::K_FLAG);
      c.is_pk_ = (m.kind_ == ObDiffOutColMeta::K_PK);
      c.subschema_id_ = m.subschema_id_;
      if (OB_FAIL(param.out_cols().push_back(c))) {
        LOG_WARN("push out_col failed", K(ret));
      }
    }
    if (OB_SUCC(ret)) {
      if (OB_FAIL(ObDiffTableOpCompute::compute_diff_rows(param, *guard, *session,
                                                         op_alloc_, rows_))) {
        LOG_WARN("compute diff rows failed", K(ret));
      }
    }
  }
  return ret;
}

} // namespace sql
} // namespace oceanbase
