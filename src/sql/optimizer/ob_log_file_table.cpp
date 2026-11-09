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

#include "sql/optimizer/ob_log_file_table.h"

using namespace oceanbase::common;

namespace oceanbase
{
namespace sql
{

int ObLogFileTable::generate_access_exprs()
{
  int ret = OB_SUCCESS;
  const ObFileTableDef *file_table_def = get_file_table_def();
  if (OB_ISNULL(file_table_def)) {
    ret = OB_ERR_UNEXPECTED;
  } else {
    // The statement may drop its column items while it is rewritten, so the
    // file table uses the expressions it keeps in its own definition.
    for (int64_t i = 0; OB_SUCC(ret) && i < file_table_def->column_exprs_.count(); ++i) {
      ObRawExpr *expr = file_table_def->column_exprs_.at(i);
      if (OB_ISNULL(expr)) {
        ret = OB_ERR_UNEXPECTED;
      } else if (OB_FAIL(access_exprs_.push_back(expr))) {
      } else { /* do nothing */ }
    }
  }
  return ret;
}

int ObLogFileTable::get_op_exprs(ObIArray<ObRawExpr *> &all_exprs)
{
  int ret = OB_SUCCESS;
  if (OB_FAIL(generate_access_exprs())) {
  } else if (OB_FAIL(append(all_exprs, access_exprs_))) {
  } else if (OB_FAIL(ObLogicalOperator::get_op_exprs(all_exprs))) {
  } else { /* do nothing */ }
  return ret;
}

int ObLogFileTable::allocate_expr_post(ObAllocExprContext &ctx)
{
  int ret = OB_SUCCESS;
  for (int64_t i = 0; OB_SUCC(ret) && i < access_exprs_.count(); ++i) {
    ObRawExpr *value_col = access_exprs_.at(i);
    if (OB_FAIL(mark_expr_produced(value_col, branch_id_, id_, ctx))) {
    } else if (!is_plan_root() && OB_FAIL(add_var_to_array_no_dup(output_exprs_, value_col))) {
    } else { /* do nothing */ }
  }
  if (OB_SUCC(ret)) {
    if (OB_FAIL(ObLogicalOperator::allocate_expr_post(ctx))) {
    }
  }
  return ret;
}

int ObLogFileTable::get_plan_item_info(PlanText &plan_text, ObSqlPlanItem &plan_item)
{
  int ret = OB_SUCCESS;
  if (OB_FAIL(ObLogicalOperator::get_plan_item_info(plan_text, plan_item))) {
  } else {
    const ObString &name = get_table_name();
    BUF_PRINT_OB_STR(name.ptr(),
                     name.length(),
                     plan_item.object_alias_,
                     plan_item.object_alias_len_);
    BUF_PRINT_STR("FILE TABLE",
                  plan_item.object_type_,
                  plan_item.object_type_len_);
  }
  return ret;
}

uint64_t ObLogFileTable::hash(uint64_t seed) const
{
  seed = do_hash(table_name_, seed);
  seed = ObLogicalOperator::hash(seed);
  return seed;
}

int ObLogFileTable::is_my_fixed_expr(const ObRawExpr *expr, bool &is_fixed)
{
  is_fixed = ObOptimizerUtil::find_item(access_exprs_, expr);
  return OB_SUCCESS;
}

} // namespace sql
} // namespace oceanbase
