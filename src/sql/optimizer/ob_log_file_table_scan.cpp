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

#include "sql/optimizer/ob_log_file_table_scan.h"
#include "sql/resolver/dml/ob_dml_stmt.h"

using namespace oceanbase::common;

namespace oceanbase
{
namespace sql
{

int ObLogFileTableScan::generate_access_exprs()
{
  int ret = OB_SUCCESS;
  const ObDMLStmt *stmt = get_stmt();
  column_exprs_.reuse();
  if (OB_ISNULL(stmt)) {
    ret = OB_ERR_UNEXPECTED;
    LOG_WARN("stmt is null", K(ret));
  } else if (OB_FAIL(stmt->get_column_exprs(table_id_, column_exprs_))) {
    LOG_WARN("failed to get column exprs of the file table", K(ret), K(table_id_));
  }
  return ret;
}

int ObLogFileTableScan::get_op_exprs(ObIArray<ObRawExpr *> &all_exprs)
{
  int ret = OB_SUCCESS;
  if (OB_FAIL(generate_access_exprs())) {
    LOG_WARN("failed to generate access exprs", K(ret));
  } else if (OB_FAIL(append(all_exprs, column_exprs_))) {
    LOG_WARN("failed to append column exprs", K(ret));
  } else if (OB_FAIL(ObLogicalOperator::get_op_exprs(all_exprs))) {
    LOG_WARN("failed to get op exprs", K(ret));
  }
  return ret;
}

int ObLogFileTableScan::allocate_expr_post(ObAllocExprContext &ctx)
{
  int ret = OB_SUCCESS;
  if (OB_FAIL(generate_access_exprs())) {
    LOG_WARN("failed to generate access exprs", K(ret));
  }
  for (int64_t i = 0; OB_SUCC(ret) && i < column_exprs_.count(); ++i) {
    ObColumnRefRawExpr *col_expr = column_exprs_.at(i);
    if (OB_ISNULL(col_expr)) {
      ret = OB_ERR_UNEXPECTED;
      LOG_WARN("column expr is null", K(ret), K(i));
    } else if (OB_FAIL(mark_expr_produced(col_expr, branch_id_, id_, ctx))) {
      LOG_WARN("failed to mark expr produced", K(ret));
    } else if (!is_plan_root() &&
               OB_FAIL(add_var_to_array_no_dup(output_exprs_,
                                               static_cast<ObRawExpr *>(col_expr)))) {
      LOG_WARN("failed to add output expr", K(ret));
    }
  }
  if (OB_SUCC(ret) && OB_FAIL(ObLogicalOperator::allocate_expr_post(ctx))) {
    LOG_WARN("failed to allocate expr post", K(ret));
  }
  return ret;
}

int ObLogFileTableScan::get_plan_item_info(PlanText &plan_text, ObSqlPlanItem &plan_item)
{
  int ret = OB_SUCCESS;
  if (OB_FAIL(ObLogicalOperator::get_plan_item_info(plan_text, plan_item))) {
    LOG_WARN("failed to get plan item info", K(ret));
  } else {
    BUF_PRINT_OB_STR(file_path_.ptr(),
                     file_path_.length(),
                     plan_item.special_predicates_,
                     plan_item.special_predicates_len_);
    if (OB_SUCC(ret)) {
      BUF_PRINT_OB_STR(table_name_.ptr(),
                       table_name_.length(),
                       plan_item.object_alias_,
                       plan_item.object_alias_len_);
      BUF_PRINT_STR("FILE TABLE",
                    plan_item.object_type_,
                    plan_item.object_type_len_);
    }
  }
  return ret;
}

uint64_t ObLogFileTableScan::hash(uint64_t seed) const
{
  seed = do_hash(table_name_, seed);
  seed = do_hash(file_path_, seed);
  seed = ObLogicalOperator::hash(seed);
  return seed;
}

int ObLogFileTableScan::is_my_fixed_expr(const ObRawExpr *expr, bool &is_fixed)
{
  is_fixed = ObOptimizerUtil::find_item(column_exprs_, expr);
  return OB_SUCCESS;
}

} // namespace sql
} // namespace oceanbase
