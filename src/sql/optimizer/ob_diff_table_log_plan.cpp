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

#define USING_LOG_PREFIX SQL_OPT
#include "sql/optimizer/ob_diff_table_log_plan.h"
#include "sql/optimizer/ob_log_diff_table.h"
#include "sql/optimizer/ob_log_operator_factory.h"
#include "sql/optimizer/ob_optimizer_context.h"
#include "sql/resolver/cmd/ob_diff_table_stmt.h"
#include "sql/resolver/expr/ob_raw_expr.h"
#include "sql/resolver/expr/ob_raw_expr_util.h"
#include "sql/engine/ob_exec_context.h"
#include "share/schema/ob_schema_getter_guard.h"
#include "share/schema/ob_column_schema.h"

using namespace oceanbase;
using namespace sql;
using namespace oceanbase::common;
using namespace oceanbase::sql::log_op_def;
using namespace oceanbase::share::schema;

namespace {

// Build a typed placeholder ObObj that matches the output column's
// declared SQL type. For collection columns the subschema id is looked
// up from exec_ctx (and thereby registered into plan_ctx). The const
// raw expr created from this ObObj carries the right datum_meta_ and
// subschema_id_, which is what ObDiffTableOp needs at execute time when
// projecting cells into the output expr datums.
int build_placeholder_obj(const ObDiffOutputCol &c,
                          const ObTableSchema *src_schema,
                          ObExecContext *exec_ctx,
                          ObObj &out)
{
  int ret = OB_SUCCESS;
  out.set_null();
  out.set_meta_type(ObObjMeta());
  out.set_type(c.obj_type_);
  if (ob_is_collection_sql_type(c.obj_type_)) {
    if (OB_ISNULL(src_schema) || OB_ISNULL(exec_ctx)
        || c.col_id_ == OB_INVALID_ID) {
      ret = OB_ERR_UNEXPECTED;
      LOG_WARN("missing schema/ctx for collection placeholder", K(ret), K(c));
    } else {
      const ObColumnSchemaV2 *col = src_schema->get_column_schema(c.col_id_);
      if (OB_ISNULL(col) || col->get_extended_type_info().count() != 1) {
        ret = OB_ERR_UNEXPECTED;
        LOG_WARN("collection column missing extended type info", K(ret), K(c));
      } else {
        uint16_t sid = UINT16_MAX;
        if (OB_FAIL(exec_ctx->get_subschema_id_by_type_string(
                col->get_extended_type_info().at(0), sid))) {
          LOG_WARN("get subschema id failed", K(ret), K(c));
        } else {
          out.set_sql_collection(nullptr, 0, sid);
        }
      }
    }
  } else if (ob_is_string_type(c.obj_type_) || ob_is_text_tc(c.obj_type_)) {
    out.set_collation_type(c.collation_type_);
    out.set_collation_level(CS_LEVEL_IMPLICIT);
  }
  return ret;
}

int alloc_diff_outputs(ObLogPlan &plan,
                       const ObDiffTableStmt &diff_stmt,
                       ObLogDiffTable &op)
{
  int ret = OB_SUCCESS;
  ObSQLSessionInfo *sess = plan.get_optimizer_context().get_session_info();
  ObRawExprFactory &factory = plan.get_optimizer_context().get_expr_factory();
  ObExecContext *exec_ctx = plan.get_optimizer_context().get_exec_ctx();
  ObSchemaGetterGuard *guard = plan.get_optimizer_context().get_schema_guard();
  const ObTableSchema *src_schema = nullptr;
  if (OB_ISNULL(sess) || OB_ISNULL(exec_ctx)) {
    ret = OB_ERR_UNEXPECTED;
    LOG_WARN("session or exec_ctx is null", K(ret));
  } else if (OB_NOT_NULL(guard)) {
    (void)guard->get_table_schema(diff_stmt.get_tenant_id(),
                                  diff_stmt.get_cur_table_id(), src_schema);
  }
  for (int64_t i = 0; OB_SUCC(ret) && i < diff_stmt.out_cols().count(); ++i) {
    const ObDiffOutputCol &c = diff_stmt.out_cols().at(i);
    ObObj placeholder;
    ObConstRawExpr *expr = NULL;
    if (OB_FAIL(build_placeholder_obj(c, src_schema, exec_ctx, placeholder))) {
      LOG_WARN("build placeholder obj failed", K(ret), K(c));
    } else if (OB_FAIL(ObRawExprUtils::build_const_obj_expr(factory, placeholder, expr))) {
      LOG_WARN("build const obj expr failed", K(ret), K(c));
    } else if (OB_FAIL(expr->formalize(sess))) {
      LOG_WARN("formalize const expr failed", K(ret), K(c));
    } else if (OB_FAIL(op.get_output_exprs().push_back(expr))) {
      LOG_WARN("push output expr failed", K(ret), K(c));
    } else if (OB_FAIL(plan.get_optimizer_context().get_all_exprs().append(expr))) {
      LOG_WARN("append all_exprs failed", K(ret), K(c));
    }
  }
  return ret;
}

} // anonymous namespace

int ObDiffTableLogPlan::generate_normal_raw_plan()
{
  int ret = OB_SUCCESS;
  ObLogDiffTable *op = NULL;
  set_max_op_id(1);
  const ObDMLStmt *stmt = get_stmt();
  if (OB_ISNULL(stmt) || OB_UNLIKELY(stmt::T_DIFF_TABLE != stmt->get_stmt_type())) {
    ret = OB_ERR_UNEXPECTED;
    LOG_WARN("expected diff table stmt", K(ret), KP(stmt));
  } else if (OB_ISNULL(op = static_cast<ObLogDiffTable *>(
                          get_log_op_factory().allocate(*this, LOG_DIFF_TABLE)))) {
    ret = OB_ALLOCATE_MEMORY_FAILED;
    LOG_WARN("allocate ObLogDiffTable failed", K(ret));
  } else {
    const ObDiffTableStmt *diff_stmt = static_cast<const ObDiffTableStmt *>(stmt);
    op->mark_is_plan_root();
    set_plan_root(op);
    get_optimizer_context().get_all_exprs().reuse();
    if (OB_FAIL(alloc_diff_outputs(*this, *diff_stmt, *op))) {
      LOG_WARN("alloc diff outputs failed", K(ret));
    } else {
      op->set_branch_id(0);
      op->set_id(0);
      op->set_op_id(0);
    }
  }
  return ret;
}
