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

#define USING_LOG_PREFIX SQL_REWRITE
#include "sql/rewrite/ob_filter_canonicalizer.h"

namespace oceanbase
{
namespace sql
{

namespace
{

/**
 * The upper bound of the number of values a single IN list may hold while it
 * is still worth folding into a range.  Folding a very long list would cost
 * more range nodes than the estimate can distinguish, so it is skipped.
 */
const int64_t MAX_IN_LIST_FOLD_SIZE = 1024;

} // namespace

int ObFilterCanonicalizer::canonicalize(ObIArray<ObRawExpr *> &conditions,
                                        bool &trans_happened)
{
  int ret = OB_SUCCESS;
  trans_happened = false;
  if (conditions.count() == 0) {
    // nothing to do
  } else {
    ObSEArray<ObRawExpr *, 8> new_conditions;
    if (OB_FAIL(new_conditions.reserve(conditions.count()))) {
    } else {
      for (int64_t i = 0; OB_SUCC(ret) && i < conditions.count(); ++i) {
        ObRawExpr *cond = conditions.at(i);
        bool cond_changed = false;
        if (OB_ISNULL(cond)) {
          ret = OB_ERR_UNEXPECTED;
          LOG_WARN("null condition expr", K(ret));
        } else if (OB_FAIL(fold_in_list_to_range(cond, cond_changed))) {
        } else if (cond_changed) {
          trans_happened = true;
        }
        if (OB_SUCC(ret) && OB_FAIL(new_conditions.push_back(cond))) {
        }
      }
      if (OB_SUCC(ret) && trans_happened) {
        if (OB_FAIL(conditions.assign(new_conditions))) {
        }
      }
    }
  }
  return ret;
}

int ObFilterCanonicalizer::fold_in_list_to_range(ObRawExpr *&expr, bool &trans_happened)
{
  int ret = OB_SUCCESS;
  trans_happened = false;
  if (OB_ISNULL(expr)) {
    ret = OB_ERR_UNEXPECTED;
  } else if (T_OP_IN == expr->get_expr_type()) {
    ObRawExpr *left_expr = nullptr;
    ObRawExpr *right_expr = nullptr;
    if (OB_UNLIKELY(2 != expr->get_param_count())
        || OB_ISNULL(left_expr = expr->get_param_expr(0))
        || OB_ISNULL(right_expr = expr->get_param_expr(1))
        || T_OP_ROW != right_expr->get_expr_type()) {
      // not a well formed IN list, leave it alone
    } else if (!left_expr->is_column_ref_expr()) {
      // only fold a plain column against a value list
    } else if (right_expr->get_param_count() < 2
               || right_expr->get_param_count() > MAX_IN_LIST_FOLD_SIZE) {
      // a single value is already an equality, and a very long list is not
      // worth folding
    } else if (!is_ascending_integer_list(right_expr)) {
      // only fold a list that is strictly ascending, so that the resulting
      // range covers exactly the same values
    } else {
      ObRawExpr *ge_expr = nullptr;
      ObRawExpr *le_expr = nullptr;
      ObRawExpr *and_expr = nullptr;
      ObRawExpr *first_value = right_expr->get_param_expr(0);
      ObRawExpr *last_value = right_expr->get_param_expr(right_expr->get_param_count() - 1);
      if (OB_FAIL(create_op_expr(T_OP_GE, ge_expr, left_expr, first_value))) {
      } else if (OB_FAIL(create_op_expr(T_OP_LE, le_expr, left_expr, last_value))) {
      } else if (OB_FAIL(create_op_expr(T_OP_AND, and_expr, ge_expr, le_expr))) {
      } else {
        expr = and_expr;
        trans_happened = true;
      }
    }
  }
  return ret;
}

int ObFilterCanonicalizer::create_op_expr(ObItemType op_type,
                                          ObRawExpr *&op_expr,
                                          ObRawExpr *left_expr,
                                          ObRawExpr *right_expr)
{
  int ret = OB_SUCCESS;
  ObOpRawExpr *tmp_expr = nullptr;
  if (OB_ISNULL(expr_factory_)) {
    ret = OB_ERR_UNEXPECTED;
  } else if (OB_FAIL(expr_factory_->create_raw_expr(op_type, tmp_expr))) {
  } else if (OB_FAIL(tmp_expr->init_param_exprs(2))) {
  } else if (OB_FAIL(tmp_expr->add_param_expr(left_expr))
             || OB_FAIL(tmp_expr->add_param_expr(right_expr))) {
    LOG_WARN("failed to add param for op expr", KPC(left_expr), KPC(right_expr), K(op_type));
  } else {
    op_expr = tmp_expr;
  }
  return ret;
}

bool ObFilterCanonicalizer::is_ascending_integer_list(const ObRawExpr *row_expr)
{
  bool is_ascending = true;
  int64_t previous = 0;
  bool has_previous = false;
  for (int64_t i = 0; i < row_expr->get_param_count(); ++i) {
    const ObRawExpr *value_expr = row_expr->get_param_expr(i);
    int64_t value = 0;
    if (OB_ISNULL(value_expr) || !value_expr->is_const_raw_expr()) {
      is_ascending = false;
      break;
    } else if (!get_integer_value(value_expr, value)) {
      is_ascending = false;
      break;
    } else if (has_previous && value != previous + 1) {
      // The list has to be a contiguous ascending run of integers without
      // duplicates.  A list that skips a value, for example (0, 5, 10), does
      // not select the same rows as the range between its first and its last
      // value, so it must not be folded.
      is_ascending = false;
      break;
    } else {
      previous = value;
      has_previous = true;
    }
  }
  return is_ascending;
}

bool ObFilterCanonicalizer::get_integer_value(const ObRawExpr *expr, int64_t &value)
{
  bool found = false;
  if (OB_NOT_NULL(expr) && expr->is_const_raw_expr()) {
    const ObConstRawExpr *const_expr = static_cast<const ObConstRawExpr *>(expr);
    const ObObj &val = const_expr->get_value();
    if (val.is_integer_type()) {
      value = val.get_int();
      found = true;
    }
  }
  return found;
}

} // end namespace sql
} // end namespace oceanbase
