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
#include "sql/rewrite/ob_transform_simplify_limit.h"
#include "sql/rewrite/ob_transform_utils.h"

using namespace oceanbase::sql;

int ObTransformSimplifyLimit::transform_one_stmt(common::ObIArray<ObParentDMLStmt> &parent_stmts,
                                                 ObDMLStmt *&stmt,
                                                 bool &trans_happened)
{
  int ret = OB_SUCCESS;
  bool is_happened = false;
  UNUSED(parent_stmts);
  if (OB_FAIL(add_limit_to_semi_right_table(stmt, is_happened))) {
    LOG_WARN("failed to add limit to semi right table", K(ret));
  } else {
    trans_happened = is_happened;
    OPT_TRACE("add limit to semi right table:", is_happened);
  }

  if (OB_SUCC(ret)) {
    if (OB_FAIL(pushdown_limit_order_for_union(stmt, is_happened))) {
      LOG_WARN("failed to push down limit order for union", K(ret));
    } else {
      trans_happened = (trans_happened || is_happened);
      OPT_TRACE("push down limit order for union:", is_happened);
    }
  }

  if (OB_SUCC(ret) && trans_happened) {
    if (OB_FAIL(add_transform_hint(*stmt))) {
      LOG_WARN("failed to add transform hint", K(ret));
    }
  }
  return ret;
}

// if no use semi right table in semi conditions, add limit 1 to semi right table
int ObTransformSimplifyLimit::add_limit_to_semi_right_table(ObDMLStmt *stmt,
                                                            bool &trans_happened)
{
  int ret = OB_SUCCESS;
  trans_happened = false;
  if (OB_ISNULL(stmt)) {
    ret = OB_ERR_UNEXPECTED;
    LOG_WARN("stmt is null", K(ret), K(stmt));
  } else {
    bool need_add = false;
    for (int64_t i = 0; OB_SUCC(ret) && i < stmt->get_semi_info_size(); ++i) {
      if (OB_FAIL(check_need_add_limit_to_semi_right_table(stmt, stmt->get_semi_infos().at(i),
                                                           need_add))) {
        LOG_WARN("failed to check ", K(ret), K(stmt));
      } else if (!need_add) {
        /* do nothing */
      } else if (OB_FAIL(ObTransformUtils::add_limit_to_semi_right_table(stmt, ctx_,
                                                                         stmt->get_semi_infos().at(i)))) {
        LOG_WARN("failed to add limit to semi right table", K(ret), K(*stmt));
      } else {
        trans_happened = true;
      }
    }
  }
  return ret;
}

int ObTransformSimplifyLimit::check_need_add_limit_to_semi_right_table(ObDMLStmt *stmt,
                                                                       SemiInfo *semi_info,
                                                                       bool &need_add)
{
  int ret = OB_SUCCESS;
  need_add = true;
  TableItem *right_table = NULL;
  ObSelectStmt *ref_query = NULL;
  if (OB_ISNULL(stmt) || OB_ISNULL(semi_info) ||
      OB_ISNULL(right_table = stmt->get_table_item_by_id(semi_info->right_table_id_))) {
    ret = OB_ERR_UNEXPECTED;
    LOG_WARN("unexpected null", K(ret), K(stmt), K(semi_info));
  } else if (!right_table->is_generated_table()) {
    need_add = !right_table->is_link_type();
  } else if (OB_ISNULL(ref_query = right_table->ref_query_)) {
    ret = OB_ERR_UNEXPECTED;
    LOG_WARN("unexpected null", K(ret), K(ref_query));
  } else if (NULL != ref_query->get_limit_expr() ||
             NULL != ref_query->get_limit_percent_expr()) {
    need_add = false;
  }
  if (OB_SUCC(ret)) {
    ObRawExpr *expr = NULL;
    int64_t right_idx = stmt->get_table_bit_index(semi_info->right_table_id_);
    for (int64_t i = 0; OB_SUCC(ret) && need_add && i < semi_info->semi_conditions_.count(); ++i) {
      if (OB_ISNULL(expr = semi_info->semi_conditions_.at(i))) {
        ret = OB_ERR_UNEXPECTED;
        LOG_WARN("expr is null", K(ret), K(expr));
      } else if (expr->get_relation_ids().has_member(right_idx)) {
        need_add = false;
      }
    }
  }
  return ret;
}

/**
 * try push down limit and order by from non set stmt to union set stmt.
 */
int ObTransformSimplifyLimit::pushdown_limit_order_for_union(ObDMLStmt *stmt, bool& trans_happened)
{
  int ret = OB_SUCCESS;
  trans_happened = false;
  bool can_push = false;
  ObSelectStmt* set_stmt = nullptr;
  if (OB_ISNULL(stmt)) {
    ret = OB_ERR_UNEXPECTED;
    LOG_WARN("get unexpected null", K(ret), K(stmt));
  } else if (!stmt->is_select_stmt()) {
    // do nothing
  } else {
    ObSelectStmt* sel_stmt = static_cast<ObSelectStmt*>(stmt);
    if (OB_FAIL(check_can_pushdown_limit_order(*sel_stmt, set_stmt, can_push))) {
      LOG_WARN("failed to check can pre push", K(ret));
    } else if(!can_push) {
      // do nothing
    } else if (OB_FAIL(do_pushdown_limit_order_for_union(*sel_stmt, set_stmt))) {
      LOG_WARN("failed to pushdown limit order for union", K(ret));
    } else {
      trans_happened = true;
    }
  }
  return ret;
}

int ObTransformSimplifyLimit::check_can_pushdown_limit_order(ObSelectStmt& upper_stmt,
                                                             ObSelectStmt*& view_stmt,
                                                             bool& can_push)
{
  int ret = OB_SUCCESS;
  can_push = false;
  TableItem* table = nullptr;
  if (!upper_stmt.is_single_table_stmt() ||
      OB_ISNULL(table = upper_stmt.get_table_item(0)) ||
      !table->is_generated_table() ||
      OB_ISNULL(view_stmt = table->ref_query_) ||
      !view_stmt->is_set_stmt() ||
      ObSelectStmt::UNION != view_stmt->get_set_op()) {
    can_push = false;
  } else if (view_stmt->has_limit() ||
             NULL == upper_stmt.get_limit_expr() ||
             NULL != upper_stmt.get_limit_percent_expr() ||
             upper_stmt.has_fetch()) {
    can_push = false;
  } else if (0 < upper_stmt.get_condition_size() ||
             upper_stmt.has_group_by() ||
             upper_stmt.has_rollup() ||
             upper_stmt.has_window_function() ||
             upper_stmt.has_distinct() ||
             upper_stmt.has_sequence()) {
    can_push = false;
  } else {
    // only push down generated table column in order by
    can_push = true;
    const uint64_t table_id = table->table_id_;
    ObRawExpr *order_expr = NULL;
    for (int64_t i = 0; OB_SUCC(ret) && can_push && i < upper_stmt.get_order_item_size(); ++i) {
      if (OB_ISNULL(order_expr = upper_stmt.get_order_item(i).expr_)) {
        ret = OB_ERR_UNEXPECTED;
        LOG_WARN("get unexpected null", K(ret), K(order_expr));
      } else if (!order_expr->is_column_ref_expr() ||
                table_id != static_cast<ObColumnRefRawExpr*>(order_expr)->get_table_id()) {
        can_push = false;
      }
    }
  }
  return ret;
}

int ObTransformSimplifyLimit::do_pushdown_limit_order_for_union(ObSelectStmt& upper_stmt,
                                                                ObSelectStmt* view_stmt)
{
  int ret = OB_SUCCESS;
  if (OB_ISNULL(view_stmt)) {
    ret = OB_ERR_UNEXPECTED;
    LOG_WARN("get unexpected null", K(ret), K(view_stmt));
  } else {
    view_stmt->set_limit_offset(upper_stmt.get_limit_expr(), upper_stmt.get_offset_expr());
    upper_stmt.set_limit_offset(NULL, NULL);
    if (upper_stmt.get_order_items().empty()) {
      // do nothing
    } else if (OB_FAIL(view_stmt->get_order_items().assign(upper_stmt.get_order_items()))) {
      LOG_WARN("failed to assign order items", K(ret));
    } else {
      ObRawExpr* expr = NULL;
      int64_t pos = OB_INVALID_INDEX;
      for (int64_t i = 0; OB_SUCC(ret) && i < upper_stmt.get_order_item_size(); ++i) {
        if (OB_ISNULL(expr = upper_stmt.get_order_item(i).expr_) ||
            OB_UNLIKELY(!expr->is_column_ref_expr())) {
          ret = OB_ERR_UNEXPECTED;
          LOG_WARN("get unexpected expr", K(ret), KPC(expr));
        } else if (FALSE_IT(pos = static_cast<ObColumnRefRawExpr*>(expr)->get_column_id()
                                  - OB_APP_MIN_COLUMN_ID)) {
          /*do nothing*/
        } else if (OB_UNLIKELY(pos < 0 || pos >= view_stmt->get_select_item_size())) {
          ret = OB_ERR_UNEXPECTED;
          LOG_WARN("invalid array pos", K(pos), K(view_stmt->get_select_item_size()), K(ret));
        } else {
          view_stmt->get_order_item(i).expr_ = view_stmt->get_select_item(pos).expr_;
        }
      }
      if (OB_SUCC(ret)) {
        upper_stmt.get_order_items().reset();
      }
    }
  }
  return ret;
}
