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
#include "sql/optimizer/ob_join_order.h"

using namespace oceanbase::common;

namespace oceanbase
{
namespace sql
{

/**
 * @brief Print the columns of the file table into the plan text.
 */
#define EXPLAIN_PRINT_FILE_TABLE(access, type)                                     \
  {                                                                                \
    if (OB_FAIL(BUF_PRINTF(#access"("))) {                                         \
      LOG_WARN("fail to print to buf", K(ret));                                    \
    } else  {                                                                      \
      int64_t N = access.count();                                                  \
      if (N == 0) {                                                                \
        if (OB_FAIL(BUF_PRINTF("nil"))) {                                          \
          LOG_WARN("fail to print to buf", K(ret));                                \
        }                                                                          \
      } else {                                                                     \
        for (int64_t i = 0; OB_SUCC(ret) && i < N; i++) {                          \
          if (OB_ISNULL(access.at(i))) {                                           \
            ret = OB_ERR_UNEXPECTED;                                               \
          } else if (OB_FAIL(BUF_PRINTF("["))) {                                   \
            LOG_WARN("fail to print to buf", K(ret));                              \
          } else if (OB_FAIL(access.at(i)->get_name(buf, buf_len, pos, type))) {   \
          } else {                                                                 \
            if (i < N - 1) {                                                       \
              if (OB_FAIL(BUF_PRINTF("], "))) {                                   \
                LOG_WARN("fail to print to buf", K(ret));                          \
              }                                                                    \
            } else if (OB_FAIL(BUF_PRINTF("]"))) {                                 \
              LOG_WARN("fail to print to buf", K(ret));                            \
            }                                                                      \
          }                                                                        \
        }                                                                          \
      }                                                                            \
      if (OB_SUCC(ret)) {                                                          \
        if (OB_FAIL(BUF_PRINTF(")"))) {                                            \
          LOG_WARN("fail to print to buf", K(ret));                                \
        }                                                                          \
      }                                                                            \
    }                                                                              \
  }

int ObLogFileTable::compute_equal_set()
{
  int ret = OB_SUCCESS;
  set_output_equal_sets(&empty_expr_sets_);
  return ret;
}

int ObLogFileTable::compute_table_set()
{
  int ret = OB_SUCCESS;
  set_table_set(&empty_table_set_);
  return ret;
}

int ObLogFileTable::est_cost()
{
  int ret = OB_SUCCESS;
  double card = 0.0;
  double op_cost = 0.0;
  double cost = 0.0;
  EstimateCostInfo param;
  param.need_parallel_ = get_parallel();
  if (OB_FAIL(do_re_est_cost(param, card, op_cost, cost))) {
  } else {
    set_card(card);
    set_op_cost(op_cost);
    set_cost(cost);
  }
  return ret;
}

int ObLogFileTable::do_re_est_cost(EstimateCostInfo &param, double &card, double &op_cost, double &cost)
{
  int ret = OB_SUCCESS;
  if (OB_ISNULL(get_plan()) || OB_ISNULL(table_def_)) {
    ret = OB_ERR_UNEXPECTED;
  } else {
    ObOptimizerContext &opt_ctx = get_plan()->get_optimizer_context();
    double read_rows = static_cast<double>(table_def_->row_cnt_);
    card = read_rows;
    if (param.need_row_count_ >= 0 && param.need_row_count_ < card) {
      read_rows = read_rows * param.need_row_count_ / card;
      card = param.need_row_count_;
    }
    op_cost = ObOptEstCost::cost_values_table(read_rows, filter_exprs_, opt_ctx);
    cost = op_cost;
  }
  return ret;
}

int ObLogFileTable::get_op_exprs(ObIArray<ObRawExpr*> &all_exprs)
{
  int ret = OB_SUCCESS;
  if (OB_ISNULL(table_def_)) {
    ret = OB_ERR_UNEXPECTED;
  } else if (OB_FAIL(ObLogicalOperator::get_op_exprs(all_exprs))) {
  } else if (OB_FAIL(append(all_exprs, column_exprs_))) {
  } else { /*do nothing*/ }
  return ret;
}

int ObLogFileTable::allocate_expr_post(ObAllocExprContext &ctx)
{
  int ret = OB_SUCCESS;
  for (int64_t i = 0; OB_SUCC(ret) && i < column_exprs_.count(); ++i) {
    ObColumnRefRawExpr *file_col = column_exprs_.at(i);
    if (OB_FAIL(mark_expr_produced(file_col, branch_id_, id_, ctx))) {
    } else if (!is_plan_root() && OB_FAIL(output_exprs_.push_back(file_col))) {
    } else { /*do nothing*/ }
  }
  if (OB_FAIL(ret)) {
  } else if (OB_FAIL(ObLogicalOperator::allocate_expr_post(ctx))) {
  } else if (get_output_exprs().empty() && OB_FAIL(allocate_dummy_output())) {
  } else { /*do nothing*/ }
  return ret;
}

int ObLogFileTable::get_plan_item_info(PlanText &plan_text,
                                       ObSqlPlanItem &plan_item)
{
  int ret = OB_SUCCESS;
  if (OB_FAIL(ObLogicalOperator::get_plan_item_info(plan_text, plan_item))) {
  } else {
    const ObIArray<ObColumnRefRawExpr*> &access = get_column_exprs();
    BEGIN_BUF_PRINT;
    EXPLAIN_PRINT_FILE_TABLE(access, type);
    END_BUF_PRINT(plan_item.special_predicates_,
                  plan_item.special_predicates_len_);
    if (OB_SUCC(ret)) {
      const ObString &name = get_table_name();
      BUF_PRINT_OB_STR(name.ptr(),
                       name.length(),
                       plan_item.object_alias_,
                       plan_item.object_alias_len_);
      BUF_PRINT_STR("FILE TABLE", plan_item.object_type_, plan_item.object_type_len_);
    }
  }
  return ret;
}

int ObLogFileTable::allocate_dummy_output()
{
  int ret = OB_SUCCESS;
  ObConstRawExpr *dummy_expr = NULL;
  if (OB_ISNULL(get_plan())) {
    ret = OB_ERR_UNEXPECTED;
  } else if (OB_FAIL(ObRawExprUtils::build_const_int_expr(
                                             get_plan()->get_optimizer_context().get_expr_factory(),
                                             ObIntType,
                                             1,
                                             dummy_expr))) {
  } else if (OB_ISNULL(dummy_expr)) {
    ret = OB_ERR_UNEXPECTED;
  } else if (OB_FAIL(dummy_expr->extract_info())) {
  } else if (OB_FAIL(output_exprs_.push_back(dummy_expr))) {
  } else if (OB_FAIL(get_plan()->get_optimizer_context().get_all_exprs().append(dummy_expr))) {
  } else { /*do nothing*/ }
  return ret;
}

int ObLogFileTable::inner_replace_op_exprs(ObRawExprReplacer &replacer)
{
  int ret = OB_SUCCESS;
  if (OB_ISNULL(table_def_)) {
    ret = OB_ERR_UNEXPECTED;
  } else if (OB_FAIL(ObLogicalOperator::inner_replace_op_exprs(replacer))) {
  } else { /*do nothing*/ }
  return ret;
}

int ObLogFileTable::is_my_fixed_expr(const ObRawExpr *expr, bool &is_fixed)
{
  is_fixed = ObOptimizerUtil::find_item(column_exprs_, expr);
  return OB_SUCCESS;
}

} // namespace sql
}// namespace oceanbase
