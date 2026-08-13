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

#include "sql/rewrite/ob_expand_aggregate_utils.h"
#include "sql/optimizer/ob_optimizer_util.h"

namespace oceanbase {
using namespace common;
using namespace share::schema;
namespace sql {

int ObExpandAggregateUtils::expand_aggr_expr(ObDMLStmt *stmt,
                                             bool &trans_happened)
{
  int ret = OB_SUCCESS;
  ObSEArray<ObRawExpr*, 4> candi_aggr_items;
  ObSEArray<ObRawExpr*, 4> replace_exprs;
  ObSEArray<ObAggFunRawExpr*, 4> new_aggr_items;
  trans_happened = false;
  if (OB_ISNULL(stmt)) {
    ret = OB_ERR_UNEXPECTED;
    LOG_WARN("get unexpected null", K(ret), K(stmt));
  } else if (OB_FAIL(extract_candi_aggr(stmt,
                                        candi_aggr_items,
                                        new_aggr_items))) {
  } else if (candi_aggr_items.empty()) {
    /*do nothing */
  } else {
    for (int64_t i = 0; OB_SUCC(ret) && i < candi_aggr_items.count(); ++i) {
      ObRawExpr *replace_expr = NULL;
      ObAggFunRawExpr* aggr_expr = static_cast<ObAggFunRawExpr*>(candi_aggr_items.at(i));
      if (OB_ISNULL(aggr_expr)) {
        ret = OB_ERR_UNEXPECTED;
        LOG_WARN("get unexpected null", K(ret), K(aggr_expr));
      } else if (is_var_expr_type(aggr_expr->get_expr_type()) &&
                 OB_FAIL(expand_var_expr(aggr_expr, replace_expr, new_aggr_items))) {
        LOG_WARN("failed to expand var expr", K(ret));
      } else if (is_common_aggr_type(aggr_expr->get_expr_type()) &&
                 OB_FAIL(expand_common_aggr_expr(aggr_expr, replace_expr, new_aggr_items))) {
        LOG_WARN("failed to expand common aggr expr", K(ret));
      } else if (OB_ISNULL(replace_expr)) {
        ret = OB_ERR_UNEXPECTED;
        LOG_WARN("get unexpected error", K(ret), K(replace_expr), K(aggr_expr->get_expr_type()));
      } else if (OB_FAIL(replace_expr->formalize(session_info_))) {
      } else if (aggr_expr->get_result_type() != replace_expr->get_result_type() &&
                 OB_FAIL(add_cast_expr(replace_expr, aggr_expr->get_result_type(), replace_expr))) {
        LOG_WARN("failed to add cast expr", K(ret));
      } else if (OB_FAIL(replace_expr->pull_relation_id())) {
      } else if (OB_FAIL(replace_exprs.push_back(replace_expr))) {
      } else {/*do nothing*/}
    }
    if (OB_SUCC(ret)) {
      if (stmt->is_select_stmt() &&
          OB_FAIL(static_cast<ObSelectStmt *>(stmt)->get_aggr_items().assign(new_aggr_items))) {
        LOG_WARN("failed to assign expr", K(ret));
      } else if (OB_FAIL(stmt->replace_relation_exprs(candi_aggr_items, replace_exprs))) {
      } else {
        trans_happened = true;
      }
    }
  }
  return ret;
}

int ObExpandAggregateUtils::expand_window_aggr_expr(ObDMLStmt *stmt, bool &trans_happened)
{
  int ret = OB_SUCCESS;
  ObSEArray<ObRawExpr*, 4> candi_win_items;
  ObSEArray<ObAggFunRawExpr*, 4> new_aggr_items;
  ObSEArray<ObRawExpr*, 4> replace_exprs;
  ObSEArray<ObWinFunRawExpr*, 4> new_win_exprs;
  trans_happened = false;
  if (OB_ISNULL(stmt)) {
    ret = OB_ERR_UNEXPECTED;
    LOG_WARN("table item is null", K(ret), K(stmt));
  } else if (!stmt->is_select_stmt()) {
    /*do nothing*/
  } else if (OB_FAIL(extract_candi_window_aggr(static_cast<ObSelectStmt *>(stmt),
                                               candi_win_items,
                                               new_win_exprs))) {
  } else if (candi_win_items.empty()) {
    /*do nothing */
  } else {
    ObSelectStmt *select_stmt = static_cast<ObSelectStmt *>(stmt);
    for (int64_t i = 0; OB_SUCC(ret) && i < candi_win_items.count(); ++i) {
      ObRawExpr *replace_expr = NULL;
      ObWinFunRawExpr* win_expr = static_cast<ObWinFunRawExpr*>(candi_win_items.at(i));
      new_aggr_items.reset();
      if (OB_ISNULL(win_expr) || OB_ISNULL(win_expr->get_agg_expr())) {
        ret = OB_ERR_UNEXPECTED;
        LOG_WARN("get unexpected null", K(ret), K(win_expr));
      } else if (is_var_expr_type(win_expr->get_agg_expr()->get_expr_type()) &&
                 OB_FAIL(expand_var_expr(win_expr->get_agg_expr(),
                                         replace_expr, new_aggr_items))) {
        LOG_WARN("failed to expand var expr", K(ret));
      } else if (is_common_aggr_type(win_expr->get_agg_expr()->get_expr_type()) &&
                 OB_FAIL(expand_common_aggr_expr(win_expr->get_agg_expr(),
                                                 replace_expr, new_aggr_items))) {
        LOG_WARN("failed to common aggr exprs", K(ret));
      } else if (OB_ISNULL(replace_expr)) {
        ret = OB_ERR_UNEXPECTED;
        LOG_WARN("get unexpected error", K(ret), K(replace_expr),
                                         K(win_expr->get_agg_expr()->get_expr_type()));
      } else if (OB_FAIL(replace_expr->formalize(session_info_))) {
      } else if (win_expr->get_agg_expr()->get_result_type() != replace_expr->get_result_type() &&
                 OB_FAIL(add_cast_expr(replace_expr,
                                       win_expr->get_agg_expr()->get_result_type(),
                                       replace_expr))) {
        LOG_WARN("failed to add cast expr", K(ret));
      } else if (OB_FAIL(ObRawExprUtils::process_window_complex_agg_expr(session_info_,
                                                                         expr_factory_,
                                                                         replace_expr->get_expr_type(),
                                                                         win_expr,
                                                                         replace_expr,
                                                                         &new_win_exprs))) {
      } else if (replace_expr->is_aggr_expr() &&
                 OB_FAIL(replace_exprs.push_back(new_win_exprs.at(new_win_exprs.count() - 1)))) {
        LOG_WARN("failed to push back expr", K(ret));
      } else if (!replace_expr->is_aggr_expr() && OB_FAIL(replace_exprs.push_back(replace_expr))) {
        LOG_WARN("failed to push back expr", K(ret));
      } else {/*do nothing*/}
    }
    if (OB_SUCC(ret)) {
      if (OB_FAIL(add_win_exprs(select_stmt, replace_exprs, new_win_exprs))) {
      } else if (OB_FAIL(select_stmt->replace_relation_exprs(candi_win_items, replace_exprs))) {
      } else {
        trans_happened = true;
      }
    }
  }
  return ret;
}
int ObExpandAggregateUtils::extract_candi_aggr(ObDMLStmt *stmt,
                                               ObIArray<ObRawExpr*> &candi_aggr_items,
                                               ObIArray<ObAggFunRawExpr*> &new_aggr_items)
{
  int ret = OB_SUCCESS;
  if (OB_ISNULL(stmt)) {
    ret = OB_ERR_UNEXPECTED;
    LOG_WARN("get unexpected null", K(ret), K(stmt));
  } else if (stmt->is_dml_stmt()) {
    ObSEArray<ObAggFunRawExpr*, 4> aggr_items;
    if (stmt->is_select_stmt() &&
        OB_FAIL(append(aggr_items, static_cast<ObSelectStmt *>(stmt)->get_aggr_items()))) {
      LOG_WARN("failed to append aggr items", K(ret));
    } else {
      for (int64_t i = 0; OB_SUCC(ret) && i < aggr_items.count(); ++i) {
        if (OB_ISNULL(aggr_items.at(i))) {
          ret = OB_ERR_UNEXPECTED;
          LOG_WARN("get unexpected null", K(ret), K(aggr_items.at(i)));
        } else if (is_valid_aggr_type(aggr_items.at(i)->get_expr_type())) {
          if (OB_FAIL(candi_aggr_items.push_back(aggr_items.at(i)))) {
          } else {/*do nothing*/}
        } else if (OB_FAIL(new_aggr_items.push_back(aggr_items.at(i)))) {
        }
      }
    }
  }
  return ret;
}

int ObExpandAggregateUtils::extract_candi_window_aggr(ObSelectStmt *select_stmt,
                                                      ObIArray<ObRawExpr*> &candi_win_items,
                                                      ObIArray<ObWinFunRawExpr*> &new_win_exprs)
{
  int ret = OB_SUCCESS;
  if (OB_ISNULL(select_stmt)) {
    ret = OB_ERR_UNEXPECTED;
    LOG_WARN("get unexpected null", K(ret), K(select_stmt));
  } else {
    ObIArray<ObWinFunRawExpr *> &win_exprs = select_stmt->get_window_func_exprs();
    for (int64_t i = 0; OB_SUCC(ret) && i < win_exprs.count(); ++i) {
      ObWinFunRawExpr* win_expr = win_exprs.at(i);
      if (OB_ISNULL(win_expr)) {
        ret = OB_ERR_UNEXPECTED;
        LOG_WARN("get unexpected null", K(ret), K(win_expr));
      } else if (win_expr->get_agg_expr() != NULL &&
                 is_valid_aggr_type(win_expr->get_agg_expr()->get_expr_type())) {
        if (OB_FAIL(candi_win_items.push_back(win_expr))) {
        } else {/*do nothing*/}
      } else if (OB_FAIL(new_win_exprs.push_back(win_expr))) {
      }
    }
  }
  return ret;
}

int ObExpandAggregateUtils::add_aggr_item(ObIArray<ObAggFunRawExpr*> &new_aggr_items,
                                          ObAggFunRawExpr *&aggr_expr,
                                          const bool need_strict_check /* = true */)
{
  int ret = OB_SUCCESS;
  if (OB_ISNULL(aggr_expr)) {
    ret = OB_ERR_UNEXPECTED;
    LOG_WARN("get unexpected null", K(ret), K(aggr_expr));
  } else if (OB_FAIL(aggr_expr->calc_hash())) {
  } else {
    int64_t i = 0;
    for (; OB_SUCC(ret) && i < new_aggr_items.count(); ++i) {
      if (OB_ISNULL(new_aggr_items.at(i))) {
        ret = OB_ERR_UNEXPECTED;
        LOG_WARN("get unexpected null", K(ret), K(new_aggr_items.at(i)));
      } else if (need_strict_check) {
        if (aggr_expr->same_as(*new_aggr_items.at(i))) {
          aggr_expr = new_aggr_items.at(i);
          break;
        }
      } else if (aggr_expr == new_aggr_items.at(i)) {
        break;
      }
    }
    if (OB_SUCC(ret) && i == new_aggr_items.count()) {
      if (OB_FAIL(new_aggr_items.push_back(aggr_expr))) {
      }
    }
  }
  return ret;
}

int ObExpandAggregateUtils::add_win_expr(common::ObIArray<ObWinFunRawExpr*> &new_win_exprs,
                                         ObWinFunRawExpr *&win_expr,
                                         const bool need_strict_check /* = true */)
{
  int ret = OB_SUCCESS;
  if (OB_ISNULL(win_expr)) {
    ret = OB_ERR_UNEXPECTED;
    LOG_WARN("get unexpected null", K(ret), K(win_expr));
  } else {
    int64_t i = 0;
    for (; OB_SUCC(ret) && i < new_win_exprs.count(); ++i) {
      if (OB_ISNULL(new_win_exprs.at(i))) {
        ret = OB_ERR_UNEXPECTED;
        LOG_WARN("get unexpected null", K(ret), K(new_win_exprs.at(i)));
      } else if (need_strict_check) {
        if (win_expr->same_as(*new_win_exprs.at(i))) {
          win_expr = new_win_exprs.at(i);
          break;
        }
      } else if (win_expr == new_win_exprs.at(i)) {
        break;
      }
    }
    if (OB_SUCC(ret) && i == new_win_exprs.count()) {
      if (OB_FAIL(new_win_exprs.push_back(win_expr))) {
      }
    }
  }
  return ret;
}

//T_FUN_VAR_POP == node->type_: (SUM(expr*expr) - SUM(expr)* SUM(expr)/ COUNT(expr)) / COUNT(expr)
//T_FUN_VAR_SAMP== node->type_: (SUM(expr*expr) - SUM(expr)* SUM(expr)/ COUNT(expr)) / (COUNT(expr) - 1)
int ObExpandAggregateUtils::expand_var_expr(ObAggFunRawExpr *aggr_expr,
                                            ObRawExpr *&replace_expr,
                                            ObIArray<ObAggFunRawExpr*> &new_aggr_items)
{
  int ret = OB_SUCCESS;
  ObRawExpr *parma_expr = NULL;
  if (OB_ISNULL(aggr_expr) ||
      OB_UNLIKELY(!is_var_expr_type(aggr_expr->get_expr_type()) ||
                  aggr_expr->get_real_param_exprs().count() != 1) ||
      OB_ISNULL(parma_expr = aggr_expr->get_real_param_exprs().at(0))) {
    ret = OB_ERR_UNEXPECTED;
    LOG_WARN("get unexpected null", K(ret), K(aggr_expr));
  } else if (aggr_expr->get_expr_type() == T_FUN_VAR_POP) {
  // In mysql mode, VAR_POP() has the same implementation as VARIANCE()
    if (OB_FAIL(expand_mysql_variance_expr(aggr_expr,
                                           replace_expr,
                                           new_aggr_items))) {
    } else {/*do nothing*/}
  } else {
    ObRawExpr *multi_expr = NULL;
    ObRawExpr *multi_sum_expr = NULL;
    ObAggFunRawExpr *sum_expr = NULL;
    ObRawExpr *cast_sum_expr = NULL;
    ObAggFunRawExpr *sum_product_expr = NULL;
    ObRawExpr *cast_sum_product_expr = NULL;
    ObAggFunRawExpr *count_expr = NULL;
    ObRawExpr *div_expr = NULL;
    ObRawExpr *minus_expr = NULL;
    ObRawExpr *div_minus_expr = NULL;
    // Due to the current issue with division implementation in mysql mode, there are some inconsistencies in precision, so we temporarily add cast to explicitly convert to maximum precision
    ObRawExprResType dst_type;
    dst_type.set_number();
    dst_type.set_scale(ObAccuracy::MAX_ACCURACY2[0][ObNumberType].get_scale());
    dst_type.set_precision(ObAccuracy::MAX_ACCURACY2[0][ObNumberType].get_precision());
    if (OB_FAIL(ObRawExprUtils::build_common_binary_op_expr(expr_factory_,
                                                            T_OP_MUL,
                                                            parma_expr,
                                                            parma_expr,
                                                            multi_expr))) {
    } else if (OB_FAIL(ObRawExprUtils::build_common_aggr_expr(expr_factory_,
                                                              session_info_,
                                                              T_FUN_SUM,
                                                              parma_expr,
                                                              sum_expr))) {
    } else if (OB_FAIL(add_aggr_item(new_aggr_items, sum_expr))) {
    } else if (               OB_FAIL(add_cast_expr(sum_expr, dst_type, cast_sum_expr))) {
    } else if (               OB_FAIL(ObRawExprUtils::build_common_binary_op_expr(expr_factory_,
                                                                   T_OP_MUL,
                                                                   cast_sum_expr,
                                                                   cast_sum_expr,
                                                                   multi_sum_expr))) {
    } else if (OB_FAIL(ObRawExprUtils::build_common_aggr_expr(expr_factory_,
                                                              session_info_,
                                                              T_FUN_SUM,
                                                              multi_expr,
                                                              sum_product_expr))) {
    } else if (OB_FAIL(add_aggr_item(new_aggr_items, sum_product_expr))) {
    } else if (               OB_FAIL(add_cast_expr(sum_product_expr, dst_type, cast_sum_product_expr))) {
    } else if (OB_FAIL(ObRawExprUtils::build_common_aggr_expr(expr_factory_,
                                                              session_info_,
                                                              T_FUN_COUNT,
                                                              parma_expr,
                                                              count_expr))) {
    } else if (OB_FAIL(add_aggr_item(new_aggr_items, count_expr))) {
    } else if (OB_FAIL(ObRawExprUtils::build_common_binary_op_expr(expr_factory_,
                                                                   T_OP_DIV,
                                                                   multi_sum_expr,
                                                                   count_expr,
                                                                   div_expr))) {
    } else if (               OB_FAIL(ObRawExprUtils::build_common_binary_op_expr(expr_factory_,
                                                                   T_OP_MINUS,
                                                                   cast_sum_product_expr,
                                                                   div_expr,
                                                                   minus_expr))) {
    } else if (aggr_expr->get_expr_type() == T_FUN_VAR_POP) {
      if (OB_FAIL(ObRawExprUtils::build_common_binary_op_expr(expr_factory_,
                                                              T_OP_DIV,
                                                              minus_expr,
                                                              count_expr,
                                                              div_minus_expr))) {
      } else {
        replace_expr = div_minus_expr;
      }
    } else {
      ObConstRawExpr *one_expr = NULL;
      ObConstRawExpr *zero_expr = NULL;
      ObRawExpr *minus_expr2 = NULL;
      ObRawExpr *ne_expr = NULL;
      ObRawExpr *case_when_expr = NULL;
      ObRawExpr *null_expr = NULL;
      if (OB_FAIL(ObRawExprUtils::build_null_expr(expr_factory_, null_expr))) {
      } else if (OB_FAIL(ObRawExprUtils::build_const_int_expr(expr_factory_,
                                                              ObIntType,
                                                              1,
                                                              one_expr))) {
      } else if (OB_FAIL(ObRawExprUtils::build_const_int_expr(expr_factory_,
                                                              ObIntType,
                                                              0,
                                                              zero_expr))) {
      } else if (OB_FAIL(ObRawExprUtils::build_common_binary_op_expr(expr_factory_,
                                                                     T_OP_MINUS,
                                                                     count_expr,
                                                                     one_expr,
                                                                     minus_expr2))) {
      } else if (OB_FAIL(ObRawExprUtils::build_common_binary_op_expr(expr_factory_,
                                                                     T_OP_NE,
                                                                     minus_expr2,
                                                                     zero_expr,
                                                                     ne_expr))) {
      } else if (OB_FAIL(ObRawExprUtils::build_case_when_expr(expr_factory_,
                                                              ne_expr,
                                                              minus_expr2,
                                                              null_expr,
                                                              case_when_expr))) {
      } else if (OB_FAIL(ObRawExprUtils::build_common_binary_op_expr(expr_factory_,
                                                                     T_OP_DIV,
                                                                     minus_expr,
                                                                     case_when_expr,
                                                                     div_minus_expr))) {
      } else {
        replace_expr = div_minus_expr;
      }
    }
  }
  return ret;
}

bool ObExpandAggregateUtils::is_valid_aggr_type(const ObItemType aggr_type)
{
  return aggr_type == T_FUN_VAR_POP ||
         aggr_type == T_FUN_VAR_SAMP ||
         aggr_type == T_FUN_AVG ||
         aggr_type == T_FUN_VARIANCE ||
         aggr_type == T_FUN_STDDEV ||
         aggr_type == T_FUN_STDDEV_POP ||
         aggr_type == T_FUN_STDDEV_SAMP ||
         aggr_type == T_FUN_APPROX_COUNT_DISTINCT;
}

int ObExpandAggregateUtils::expand_common_aggr_expr(ObAggFunRawExpr *aggr_expr,
                                                    ObRawExpr *&replace_expr,
                                                    ObIArray<ObAggFunRawExpr*> &new_aggr_items)
{
  int ret = OB_SUCCESS;
  if (OB_ISNULL(aggr_expr) || OB_UNLIKELY(!is_common_aggr_type(aggr_expr->get_expr_type()))) {
    ret = OB_ERR_UNEXPECTED;
    LOG_WARN("get unexpected null", K(ret), K(aggr_expr));
  } else if (aggr_expr->get_expr_type() == T_FUN_AVG) {
    if (OB_FAIL(expand_avg_expr(aggr_expr,
                                replace_expr,
                                new_aggr_items))) {
    }
  } else if (aggr_expr->get_expr_type() == T_FUN_STDDEV) {
    if (OB_FAIL(expand_stddev_expr(aggr_expr,
                                   replace_expr,
                                   new_aggr_items))) {
    }
  } else if (aggr_expr->get_expr_type() == T_FUN_VARIANCE) {
    if (OB_FAIL(expand_mysql_variance_expr(aggr_expr,
                                                                            replace_expr,
                                                                            new_aggr_items))) {
    }
  } else if (aggr_expr->get_expr_type() == T_FUN_STDDEV_POP) {
    if (OB_FAIL(expand_stddev_pop_expr(aggr_expr,
                                      replace_expr,
                                      new_aggr_items))) {
    }
  } else if (aggr_expr->get_expr_type() == T_FUN_STDDEV_SAMP) {
    if (OB_FAIL(expand_stddev_samp_expr(aggr_expr,
                                        replace_expr,
                                        new_aggr_items))) {
    }
  } else if (aggr_expr->get_expr_type() == T_FUN_APPROX_COUNT_DISTINCT) {
    if (OB_FAIL(expand_approx_count_distinct_expr(aggr_expr,
                                                  replace_expr,
                                                  new_aggr_items))) {
    }
  } else {/*do nothing*/}
  return ret;
}

/*avg(expr) keep(...) <==> sum(expr) keep(...) / count(expr) keep(...)
 */
int ObExpandAggregateUtils::expand_avg_expr(ObAggFunRawExpr *aggr_expr,
                                            ObRawExpr *&replace_expr,
                                            ObIArray<ObAggFunRawExpr*> &new_aggr_items)
{
  int ret = OB_SUCCESS;
  ObRawExpr *parma_expr = NULL;
  if (OB_ISNULL(aggr_expr) ||
      OB_UNLIKELY(aggr_expr->get_expr_type() != T_FUN_AVG ||
                  aggr_expr->get_real_param_exprs().count() != 1) ||
      OB_ISNULL(parma_expr = aggr_expr->get_real_param_exprs().at(0))) {
    ret = OB_ERR_UNEXPECTED;
    LOG_WARN("get unexpected null", K(ret), K(aggr_expr));
  } else {
    ObAggFunRawExpr *sum_expr = NULL;
    ObAggFunRawExpr *count_expr = NULL;
    ObRawExpr *div_expr = NULL;
    if (OB_FAIL(ObRawExprUtils::build_common_aggr_expr(expr_factory_,
                                                       session_info_,
                                                       T_FUN_SUM,
                                                       parma_expr,
                                                       sum_expr))) {
    } else {
      sum_expr->set_param_distinct(aggr_expr->is_param_distinct());
      if (OB_FAIL(add_aggr_item(new_aggr_items, sum_expr))) {
      } else if (OB_FAIL(ObRawExprUtils::build_common_aggr_expr(expr_factory_,
                                                                session_info_,
                                                                T_FUN_COUNT,
                                                                parma_expr,
                                                                count_expr))) {
      } else {
        count_expr->set_param_distinct(aggr_expr->is_param_distinct());
        if (OB_FAIL(add_aggr_item(new_aggr_items, count_expr))) {
        } else if (OB_FAIL(ObRawExprUtils::build_common_binary_op_expr(expr_factory_,
                                                                      T_OP_AGG_DIV,
                                                                      sum_expr,
                                                                      count_expr,
                                                                      div_expr))) {
        } else {
          replace_expr = div_expr;
        }
      }
    }
  }
  return ret;
}
// mysql mode variance calculation formula: avg(expr1*expr1) - avg(expr1)*avg(expr1)
int ObExpandAggregateUtils::expand_mysql_variance_expr(ObAggFunRawExpr *aggr_expr,
                                                       ObRawExpr *&replace_expr,
                                                       ObIArray<ObAggFunRawExpr*> &new_aggr_items)
{
  int ret = OB_SUCCESS;
  ObRawExpr *parma_expr = NULL;
  if (OB_ISNULL(aggr_expr) ||
      OB_UNLIKELY((aggr_expr->get_expr_type() != T_FUN_VARIANCE &&
                   aggr_expr->get_expr_type() != T_FUN_VAR_POP) ||
                  aggr_expr->get_real_param_exprs().count() != 1) ||
      OB_ISNULL(parma_expr = aggr_expr->get_real_param_exprs().at(0))) {
    ret = OB_ERR_UNEXPECTED;
    LOG_WARN("get unexpected null", K(ret), K(aggr_expr));
  } else {
    ObAggFunRawExpr *sum_expr = NULL;
    ObRawExpr *cast_sum_expr = NULL;
    ObAggFunRawExpr *count_expr = NULL;
    ObAggFunRawExpr *sum_product_expr = NULL;
    ObRawExpr *cast_sum_product_expr = NULL;
    ObAggFunRawExpr *count_product_expr = NULL;
    ObRawExpr *minus_expr = NULL;
    ObRawExpr *multi_expr = NULL;
    ObRawExpr *multi_sum_expr = NULL;
    ObRawExpr *multi_count_expr = NULL;
    ObRawExpr *div_expr = NULL;
    ObRawExpr *div_multi_expr = NULL;
    ObRawExpr *cast_minus_expr = NULL;
    // Due to the current issue with division implementation in mysql mode, there are some inconsistencies in precision, so we temporarily add cast for explicit conversion to avoid it
    ObRawExprResType dst_type;
    dst_type.set_number();
    dst_type.set_scale(ObAccuracy::MAX_ACCURACY2[0][ObNumberType].get_scale());
    dst_type.set_precision(ObAccuracy::MAX_ACCURACY2[0][ObNumberType].get_precision());
    ObRawExprResType result_type;
    result_type.set_double();
    result_type.set_scale(ObAccuracy(PRECISION_UNKNOWN_YET, SCALE_UNKNOWN_YET).get_scale());
    result_type.set_precision(ObAccuracy(PRECISION_UNKNOWN_YET, SCALE_UNKNOWN_YET).get_precision());
    if (OB_FAIL(ObRawExprUtils::build_common_binary_op_expr(expr_factory_,
                                                            T_OP_AGG_MUL,
                                                            parma_expr,
                                                            parma_expr,
                                                            multi_expr))) {
    } else if (OB_FAIL(ObRawExprUtils::build_common_aggr_expr(expr_factory_,
                                                              session_info_,
                                                              T_FUN_SUM,
                                                              parma_expr,
                                                              sum_expr))) {
    } else if (OB_FAIL(add_aggr_item(new_aggr_items, sum_expr))) {
    } else if (OB_FAIL(add_cast_expr(sum_expr, dst_type, cast_sum_expr))) {
    } else if (OB_FAIL(ObRawExprUtils::build_common_aggr_expr(expr_factory_,
                                                              session_info_,
                                                              T_FUN_SUM,
                                                              multi_expr,
                                                              sum_product_expr))) {
    } else if (OB_FAIL(add_aggr_item(new_aggr_items, sum_product_expr))) {
    } else if (OB_FAIL(add_cast_expr(sum_product_expr, dst_type, cast_sum_product_expr))) {
    } else if (OB_FAIL(ObRawExprUtils::build_common_aggr_expr(expr_factory_,
                                                              session_info_,
                                                              T_FUN_COUNT,
                                                              parma_expr,
                                                              count_expr))) {
    } else if (OB_FAIL(add_aggr_item(new_aggr_items, count_expr))) {
    } else if (OB_FAIL(ObRawExprUtils::build_common_aggr_expr(expr_factory_,
                                                              session_info_,
                                                              T_FUN_COUNT,
                                                              multi_expr,
                                                              count_product_expr))) {
    } else if (OB_FAIL(add_aggr_item(new_aggr_items, count_product_expr))) {
    } else if (OB_FAIL(ObRawExprUtils::build_common_binary_op_expr(expr_factory_,
                                                            T_OP_AGG_MUL,
                                                            cast_sum_expr,
                                                            cast_sum_expr,
                                                            multi_sum_expr))) {
    } else if (OB_FAIL(ObRawExprUtils::build_common_binary_op_expr(expr_factory_,
                                                                   T_OP_AGG_MUL,
                                                                   count_expr,
                                                                   count_expr,
                                                                   multi_count_expr))) {
    } else if (OB_FAIL(ObRawExprUtils::build_common_binary_op_expr(expr_factory_,
                                                                   T_OP_AGG_DIV,
                                                                   multi_sum_expr,
                                                                   multi_count_expr,
                                                                   div_expr))) {
    } else if (OB_FAIL(ObRawExprUtils::build_common_binary_op_expr(expr_factory_,
                                                                   T_OP_AGG_DIV,
                                                                   cast_sum_product_expr,
                                                                   count_product_expr,
                                                                   div_multi_expr))) {
    } else if (OB_FAIL(ObRawExprUtils::build_common_binary_op_expr(expr_factory_,
                                                                   T_OP_AGG_MINUS,
                                                                   div_multi_expr,
                                                                   div_expr,
                                                                   minus_expr))) {
    } else if (OB_FAIL(add_cast_expr(minus_expr, result_type, cast_minus_expr))) {
    } else {
      replace_expr = cast_minus_expr;
    }
  }
  return ret;
}

//stddev(expr) <==> sqrt(variance(expr))
int ObExpandAggregateUtils::expand_stddev_expr(ObAggFunRawExpr *aggr_expr,
                                               ObRawExpr *&replace_expr,
                                               ObIArray<ObAggFunRawExpr*> &new_aggr_items)
{
  int ret = OB_SUCCESS;
  ObRawExpr *parma_expr = NULL;
  if (OB_ISNULL(aggr_expr) ||
      OB_UNLIKELY(aggr_expr->get_expr_type() != T_FUN_STDDEV ||
                  aggr_expr->get_real_param_exprs().count() != 1) ||
      OB_ISNULL(parma_expr = aggr_expr->get_real_param_exprs().at(0))) {
    ret = OB_ERR_UNEXPECTED;
    LOG_WARN("get unexpected null", K(ret), K(aggr_expr));
  } else {
    ObSysFunRawExpr *sqrt_expr = NULL;
    ObRawExpr *sqrt_param_expr = NULL;
    ObAggFunRawExpr *variance_expr = NULL;
    if (OB_FAIL(ObRawExprUtils::build_common_aggr_expr(expr_factory_,
                                                       session_info_,
                                                       T_FUN_VARIANCE,
                                                       parma_expr,
                                                       variance_expr))) {
    } else {
      variance_expr->set_param_distinct(aggr_expr->is_param_distinct());
      if (                 OB_FAIL(expand_mysql_variance_expr(variance_expr,
                                                    sqrt_param_expr, new_aggr_items))) {
      } else if (OB_FAIL(expr_factory_.create_raw_expr(T_FUN_SYS_SQRT, sqrt_expr))) {
      } else if (OB_ISNULL(sqrt_expr)) {
        ret = OB_ERR_UNEXPECTED;
        LOG_WARN("add expr is null", K(ret), K(sqrt_expr));
      } else if (OB_FAIL(sqrt_expr->set_param_expr(sqrt_param_expr))) {
      } else {
        ObString func_name = ObString::make_string("sqrt");
        sqrt_expr->set_func_name(func_name);
        sqrt_expr->set_aggr_type(aggr_expr->get_expr_type());
        replace_expr = sqrt_expr;
      }
    }
  }
  return ret;
}

//stddev_pop(expr) <==> sqrt(var_pop(expr))
int ObExpandAggregateUtils::expand_stddev_pop_expr(ObAggFunRawExpr *aggr_expr,
                                                   ObRawExpr *&replace_expr,
                                                   ObIArray<ObAggFunRawExpr*> &new_aggr_items)
{
  int ret = OB_SUCCESS;
  ObRawExpr *parma_expr = NULL;
  if (OB_ISNULL(aggr_expr) ||
      OB_UNLIKELY(aggr_expr->get_expr_type() != T_FUN_STDDEV_POP ||
                  aggr_expr->get_real_param_exprs().count() != 1) ||
      OB_ISNULL(parma_expr = aggr_expr->get_real_param_exprs().at(0))) {
    ret = OB_ERR_UNEXPECTED;
    LOG_WARN("get unexpected null", K(ret), K(aggr_expr));
  } else {
    ObSysFunRawExpr *sqrt_expr = NULL;
    ObRawExpr *sqrt_param_expr = NULL;
    ObAggFunRawExpr *var_expr = NULL;
    if (OB_FAIL(ObRawExprUtils::build_common_aggr_expr(expr_factory_,
                                                       session_info_,
                                                       T_FUN_VAR_POP,
                                                       parma_expr,
                                                       var_expr))) {
    } else {
      if (OB_FAIL(expand_var_expr(var_expr,
                                  sqrt_param_expr, new_aggr_items))) {
      } else if (OB_FAIL(expr_factory_.create_raw_expr(T_FUN_SYS_SQRT, sqrt_expr))) {
      } else if (OB_ISNULL(sqrt_expr)) {
        ret = OB_ERR_UNEXPECTED;
        LOG_WARN("add expr is null", K(ret), K(sqrt_expr));
      } else if (OB_FAIL(sqrt_expr->set_param_expr(sqrt_param_expr))) {
      } else {
        ObString func_name = ObString::make_string("sqrt");
        sqrt_expr->set_func_name(func_name);
        replace_expr = sqrt_expr;
      }
    }
  }
  return ret;
}

//stddev_samp(expr) <==> sqrt(var_samp(expr))
int ObExpandAggregateUtils::expand_stddev_samp_expr(ObAggFunRawExpr *aggr_expr,
                                                    ObRawExpr *&replace_expr,
                                                    ObIArray<ObAggFunRawExpr*> &new_aggr_items)
{
  int ret = OB_SUCCESS;
  ObRawExpr *parma_expr = NULL;
  if (OB_ISNULL(aggr_expr) ||
      OB_UNLIKELY(aggr_expr->get_expr_type() != T_FUN_STDDEV_SAMP ||
                  aggr_expr->get_real_param_exprs().count() != 1) ||
      OB_ISNULL(parma_expr = aggr_expr->get_real_param_exprs().at(0))) {
    ret = OB_ERR_UNEXPECTED;
    LOG_WARN("get unexpected null", K(ret), K(aggr_expr));
  } else {
    ObSysFunRawExpr *sqrt_expr = NULL;
    ObRawExpr *expand_var_expr_inner = NULL;
    ObRawExpr *case_when_expr = NULL;
    ObConstRawExpr *zero_expr = NULL;
    ObAggFunRawExpr *var_expr = NULL;
    ObRawExpr *lt_expr = NULL;
    if (OB_FAIL(ObRawExprUtils::build_common_aggr_expr(expr_factory_,
                                                       session_info_,
                                                       T_FUN_VAR_SAMP,
                                                       parma_expr,
                                                       var_expr))) {
    } else {
      if (OB_FAIL(expand_var_expr(var_expr,
                                  expand_var_expr_inner, new_aggr_items))) {
      } else if (OB_FAIL(expr_factory_.create_raw_expr(T_FUN_SYS_SQRT, sqrt_expr))) {
      } else if (OB_ISNULL(sqrt_expr)) {
        ret = OB_ERR_UNEXPECTED;
        LOG_WARN("add expr is null", K(ret), K(sqrt_expr));
      } else {
        if (OB_SUCC(ret)) {
          if (OB_FAIL(ObRawExprUtils::build_const_int_expr(expr_factory_,
                                                          ObIntType,
                                                          0,
                                                          zero_expr))) {
          } else if (OB_FAIL(ObRawExprUtils::build_common_binary_op_expr(expr_factory_,
                                                                        T_OP_LT,
                                                                        expand_var_expr_inner,
                                                                        zero_expr,
                                                                        lt_expr))) {
          } else if (OB_FAIL(ObRawExprUtils::build_case_when_expr(expr_factory_,
                                                                  lt_expr,
                                                                  zero_expr,
                                                                  expand_var_expr_inner,
                                                                  case_when_expr))) {
          } else if (OB_FAIL(sqrt_expr->set_param_expr(case_when_expr))) {
          } 
        }
        if (OB_SUCC(ret)) {
          ObString func_name = ObString::make_string("sqrt");
          sqrt_expr->set_func_name(func_name);
          replace_expr = sqrt_expr;
        }
      }
    }
  }
  return ret;
}

int ObExpandAggregateUtils::expand_approx_count_distinct_expr(ObAggFunRawExpr *aggr_expr,
                                                              ObRawExpr *&replace_expr,
                                                              ObIArray<ObAggFunRawExpr *> &new_aggr_items)
{
  int ret = OB_SUCCESS;
  ObSysFunRawExpr *sys_func_expr = NULL;
  ObAggFunRawExpr *synopsis = NULL;
  if (OB_ISNULL(aggr_expr) ||
      OB_UNLIKELY(aggr_expr->get_expr_type() != T_FUN_APPROX_COUNT_DISTINCT)) {
    ret = OB_ERR_UNEXPECTED;
    LOG_WARN("params are invalid", K(ret), K(aggr_expr));
  } else if (OB_FAIL(expr_factory_.create_raw_expr(T_FUN_APPROX_COUNT_DISTINCT_SYNOPSIS,
                                                         synopsis))) {
  } else if (OB_ISNULL(synopsis)) {
    ret = OB_ERR_UNEXPECTED;
    LOG_WARN("synopsis expr is null", K(ret));
  } else if (OB_FAIL(synopsis->get_real_param_exprs_for_update().assign(
                       aggr_expr->get_real_param_exprs()))) {
  } else if (OB_FAIL(add_aggr_item(new_aggr_items, synopsis))) {
  } else if (OB_FAIL(expr_factory_.create_raw_expr(T_FUN_SYS_ESTIMATE_NDV,
                                                         sys_func_expr))) {
  } else if (OB_ISNULL(sys_func_expr)) {
    ret = OB_ERR_UNEXPECTED;
    LOG_WARN("sys func expr is null", K(ret), K(sys_func_expr));
  } else if (OB_FAIL(sys_func_expr->set_param_expr(synopsis))) {
  } else if (OB_FAIL(sys_func_expr->formalize(session_info_))) {
  } else {
    ObString func_name = ObString::make_string("ESTIMATE_NDV");
    sys_func_expr->set_func_name(func_name);
    replace_expr = sys_func_expr;
  }
  return ret;
}

int ObExpandAggregateUtils::add_cast_expr(ObRawExpr *expr,
                                          const ObRawExprResType &dst_type,
                                          ObRawExpr *&new_expr)
{
  int ret = OB_SUCCESS;
  ObSysFunRawExpr *cast_expr = NULL;
  if (OB_ISNULL(expr)) {
    ret = OB_ERR_UNEXPECTED;
    LOG_WARN("get unexpected null", K(ret), K(expr));
  } else if (OB_FAIL(ObRawExprUtils::create_cast_expr(expr_factory_,
                                                      expr,
                                                      dst_type,
                                                      cast_expr,
                                                      session_info_))) {
  } else if (OB_ISNULL(cast_expr)) {
    ret = OB_ERR_UNEXPECTED;
    LOG_WARN("get unexpected null", K(ret), K(cast_expr));
  } else if (OB_FAIL(cast_expr->add_flag(IS_OP_OPERAND_IMPLICIT_CAST))) {
  } else {
    new_expr = cast_expr;
  }
  return ret;
}

int ObExpandAggregateUtils::add_win_exprs(ObSelectStmt *select_stmt,
                                          ObIArray<ObRawExpr*> &replace_exprs,
                                          ObIArray<ObWinFunRawExpr*> &new_win_exprs)
{
  int ret = OB_SUCCESS;
  if (OB_ISNULL(select_stmt)) {
    ret = OB_ERR_UNEXPECTED;
    LOG_WARN("get unexpected null", K(ret), K(select_stmt));
  } else if (replace_exprs.count() > 0 && new_win_exprs.count() > 0) {
    select_stmt->get_window_func_exprs().reset();
    for (int64_t i = 0; OB_SUCC(ret) && i < new_win_exprs.count(); ++i) {
      ObWinFunRawExpr *win_expr = NULL;
      if (OB_ISNULL(new_win_exprs.at(i))) {
        ret = OB_ERR_UNEXPECTED;
        LOG_WARN("get unexpected null", K(ret), K(new_win_exprs.at(i)));
      } else if (OB_FAIL(select_stmt->get_same_win_func_item(new_win_exprs.at(i), win_expr))) {
      } else if (OB_ISNULL(win_expr)) {
        if (OB_FAIL(select_stmt->add_window_func_expr(new_win_exprs.at(i)))) {
        }
      } else {
        for (int64_t j = 0; OB_SUCC(ret) && j < replace_exprs.count(); ++j) {
          if (OB_FAIL(ObRawExprUtils::replace_ref_column(replace_exprs.at(j),
                                                         new_win_exprs.at(i),
                                                         win_expr))) {
          }
        }
      }
    }
  }
  return ret;
}

} // namespace sql
} // namespace oceanbase
