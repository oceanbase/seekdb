/*
 * Copyright (c) 2026 OceanBase.
 * Licensed under the Apache License, Version 2.0.
 */
#define USING_LOG_PREFIX SQL_OPT
#include "sql/optimizer/ob_log_file_scan.h"

using namespace oceanbase::common;

namespace oceanbase
{
namespace sql
{
int ObLogFileScan::generate_access_exprs()
{
  int ret = OB_SUCCESS;
  const ObDMLStmt *stmt = get_stmt();
  if (OB_ISNULL(stmt)) {
    ret = OB_ERR_UNEXPECTED;
    LOG_WARN("statement is null", K(ret));
  }
  for (int64_t i = 0; OB_SUCC(ret) && i < stmt->get_column_size(); ++i) {
    const ColumnItem *column = stmt->get_column_item(i);
    if (OB_ISNULL(column) || OB_ISNULL(column->expr_)) {
      ret = OB_ERR_UNEXPECTED;
      LOG_WARN("column item is null", K(ret), K(i));
    } else if (column->table_id_ == table_id_
               && column->expr_->is_explicited_reference()
               && OB_FAIL(access_exprs_.push_back(column->expr_))) {
      LOG_WARN("failed to append file access expression", K(ret), K(i));
    }
  }
  return ret;
}

int ObLogFileScan::get_op_exprs(ObIArray<ObRawExpr *> &all_exprs)
{
  int ret = OB_SUCCESS;
  if (OB_FAIL(generate_access_exprs())) {
    LOG_WARN("failed to generate file access expressions", K(ret));
  } else if (OB_FAIL(append(all_exprs, access_exprs_))) {
    LOG_WARN("failed to append file access expressions", K(ret));
  } else if (OB_FAIL(ObLogicalOperator::get_op_exprs(all_exprs))) {
    LOG_WARN("failed to get logical operator expressions", K(ret));
  }
  return ret;
}

int ObLogFileScan::allocate_expr_post(ObAllocExprContext &ctx)
{
  int ret = OB_SUCCESS;
  for (int64_t i = 0; OB_SUCC(ret) && i < access_exprs_.count(); ++i) {
    ObRawExpr *expr = access_exprs_.at(i);
    if (OB_FAIL(mark_expr_produced(expr, branch_id_, id_, ctx))) {
      LOG_WARN("failed to mark file expression produced", K(ret));
    } else if (!is_plan_root() && OB_FAIL(add_var_to_array_no_dup(output_exprs_, expr))) {
      LOG_WARN("failed to add file output expression", K(ret));
    }
  }
  if (OB_SUCC(ret) && OB_FAIL(ObLogicalOperator::allocate_expr_post(ctx))) {
    LOG_WARN("failed to allocate file expressions", K(ret));
  }
  return ret;
}

int ObLogFileScan::get_plan_item_info(PlanText &plan_text, ObSqlPlanItem &plan_item)
{
  int ret = OB_SUCCESS;
  if (OB_FAIL(ObLogicalOperator::get_plan_item_info(plan_text, plan_item))) {
    LOG_WARN("failed to get file scan plan item", K(ret));
  } else {
    BUF_PRINT_OB_STR(table_name_.ptr(), table_name_.length(),
                     plan_item.object_alias_, plan_item.object_alias_len_);
    BUF_PRINT_STR("FILE SCAN", plan_item.object_type_, plan_item.object_type_len_);
    if (OB_NOT_NULL(file_table_def_)) {
      BUF_PRINT_OB_STR(file_table_def_->canonical_path_.ptr(),
                       file_table_def_->canonical_path_.length(),
                       plan_item.special_predicates_,
                       plan_item.special_predicates_len_);
    }
  }
  return ret;
}

uint64_t ObLogFileScan::hash(uint64_t seed) const
{
  seed = do_hash(table_name_, seed);
  if (OB_NOT_NULL(file_table_def_)) {
    seed = do_hash(file_table_def_->canonical_path_, seed);
    seed = do_hash(file_table_def_->device_, seed);
    seed = do_hash(file_table_def_->inode_, seed);
    seed = do_hash(file_table_def_->file_size_, seed);
    seed = do_hash(file_table_def_->modified_time_ns_, seed);
  }
  return ObLogicalOperator::hash(seed);
}

int ObLogFileScan::is_my_fixed_expr(const ObRawExpr *expr, bool &is_fixed)
{
  is_fixed = ObOptimizerUtil::find_item(access_exprs_, expr);
  return OB_SUCCESS;
}
} // namespace sql
} // namespace oceanbase
