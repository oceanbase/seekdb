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

#ifndef OB_TRANSFORM_SIMPILFY_SUBQUERY_H
#define OB_TRANSFORM_SIMPILFY_SUBQUERY_H

#include "sql/rewrite/ob_transform_rule.h"
#include "sql/rewrite/ob_transform_utils.h"

namespace oceanbase {
namespace sql {

class ObTransformSimplifySubquery : public ObTransformRule
{
public:
  ObTransformSimplifySubquery(ObTransformerCtx *ctx)
    : ObTransformRule(ctx, TransMethod::POST_ORDER, T_SIMPLIFY_SUBQUERY)
  {}

  virtual ~ObTransformSimplifySubquery() {}

  virtual int transform_one_stmt(common::ObIArray<ObParentDMLStmt> &parent_stmts,
                                 ObDMLStmt *&stmt,
                                 bool &trans_happened) override;
  virtual int check_rule_bypass(const ObDMLStmt &stmt, bool &reject) override;
private:
  int transform_subquery_as_expr(ObDMLStmt *stmt, bool &trans_happened);                                               
  int try_trans_subquery_in_expr(ObDMLStmt *stmt,
                                 ObRawExpr *&expr,
                                 bool &trans_happened);
  int do_trans_subquery_as_expr(ObDMLStmt *stmt,
                                ObRawExpr *&expr,
                                bool &trans_happened);
  int is_subquery_to_expr_valid(const ObSelectStmt *stmt,
                                bool &is_valid);
  int transform_not_expr(ObDMLStmt *stmt, bool &trans_happened);
  int do_transform_not_expr(ObRawExpr *&expr, bool &trans_happened);
  int remove_redundant_select(ObDMLStmt *&stmt, bool &trans_happened);
  int try_remove_redundant_select(ObSelectStmt &stmt, ObSelectStmt *&new_stmt);
  int check_subquery_valid(ObSelectStmt &stmt, bool &is_valid);
  int push_down_outer_join_condition(ObDMLStmt *stmt, bool &trans_happened);
  int push_down_outer_join_condition(ObDMLStmt *stmt,
                                     TableItem *join_table,
                                     bool &trans_happened);
  int try_push_down_outer_join_conds(ObDMLStmt *stmt,
                                     JoinedTable *join_table,
                                     bool &trans_happened);

  int add_limit_for_exists_subquery(ObDMLStmt *stmt,bool &trans_happened);

  int recursive_add_limit_for_exists_expr(ObRawExpr *expr, bool &trans_happened);

  /**
   * @brief transform_any_all
   * Traverse expressions in different parts of stmt and attempt to rewrite them
   */
  int transform_any_all(ObDMLStmt *stmt, bool &trans_happened);

  /**
   * @brief try_trans_any_all
   * Try to rewrite the expression expr itself or its parameter expressions
   */
  int try_transform_any_all(ObDMLStmt *stmt, ObRawExpr *&expr, bool &trans_happened);

  /**
   * @brief do_trans_any_all
   * Determine if an expression can be rewritten, if so, perform the rewrite
   */
  int do_transform_any_all(ObDMLStmt *stmt, ObRawExpr *&expr, bool &trans_happened);

  int check_any_all_as_min_max(ObRawExpr *expr, bool &is_valid);


  int transform_any_all_as_min_max(ObDMLStmt *stmt, ObRawExpr *expr, bool &trans_happened);

  int do_transform_any_all_as_min_max(ObSelectStmt *sel_stmt, const ObItemType aggr_type,
                                      bool is_with_all, bool &trans_happened);

  /**
   * @brief ObTransformAnyAll::eliminate_any_all_for_scalar_query
   * If subquery returns 0 or 1 record, then any can be eliminated
   * If subquery returns 1 record, then all can be eliminated
   *   rel.c > all empty set  is semantically different from rel.c > empty set
   */
  int eliminate_any_all_before_subquery(ObDMLStmt *stmt, ObRawExpr *&expr, bool &trans_happened);

  /**
   * @brief is_any_all_removeable
   * Determine if the any/all flag of a subquery compare expression can be removed
   */
  int check_any_all_removeable(ObRawExpr *expr, bool &can_be_removed);

  /**
   * @brief clear_any_all_flag
   * Replace a subquery compare expression expr with the corresponding common compare expression
   */
  int clear_any_all_flag(ObDMLStmt *stmt, ObRawExpr *&expr, ObQueryRefRawExpr *query_ref);

  ObItemType get_aggr_type(ObItemType op_type, bool is_with_all);

  ObItemType query_cmp_to_value_cmp(const ObItemType cmp_type);

  int transform_exists_query(ObDMLStmt *stmt, bool &trans_happened);

  int try_eliminate_subquery(ObDMLStmt *stmt, ObRawExpr *&expr, bool &trans_happened);
  
    /**
   * Simplify subuqery in exists, any, all (subq)
   * 1. Eliminate subquery if possible
   * 2. Eliminate select list to const 1
   * 3. Eliminate group by
   */
  int recursive_eliminate_subquery(ObDMLStmt *stmt, ObRawExpr *&expr,
                                   bool &trans_happened);
  int eliminate_subquery(ObDMLStmt *stmt,
                         ObRawExpr *&expr,
                         bool &trans_happened);


  /**
   * When [not] exists(subq) satisfies all of the following conditions, the subquery can be eliminated
   * 1. The current stmt is not a set stmt
   * 2. The current stmt does not contain having or group by
   * 3. Stmt contains aggregate functions
   */
  static bool is_subquery_not_empty(const ObSelectStmt &stmt);

  int subquery_can_be_eliminated_in_exists(const ObItemType op_type,
                                           const ObSelectStmt *stmt,
                                           bool &can_be_eliminated) const;
  /**
   * When [not] exists(subq) satisfies all of the following conditions, the select list can be replaced with 1
   * 1. The current stmt is not a set stmt
   * 2. Distinct and limit do not exist simultaneously
   */
  int select_items_can_be_simplified(const ObItemType op_type,
                                    const ObSelectStmt *stmt,
                                    bool &can_be_simplified) const;

  /**
   * When [not] exists(subq) satisfies all of the following conditions, the group by clause can be eliminated:
   * 1. The current stmt is not a set stmt
   * 2. There is no having clause
   * 3. There is no limit clause
   */
  int groupby_can_be_eliminated_in_exists(const ObItemType op_type,
                                          const ObSelectStmt *stmt,
                                          bool &can_be_eliminated) const;

  /**
   * When Any/all/in(subq) meets all of the following conditions, the group by clause can be eliminated:
   * 1. The current stmt is not a set stmt
   * 2. There is no having clause
   * 3. There is no limit clause
   * 4. No aggregate functions (in select items)
   * 5. Non-constant select item columns are all included in group exprs
   */
  int eliminate_groupby_in_any_all(ObSelectStmt *stmt, bool &trans_happened);

  int eliminate_subquery_in_exists(ObDMLStmt *stmt,
                                   ObRawExpr *&expr,
                                   bool &trans_happened);

  int simplify_select_items(ObDMLStmt *stmt,
                            const ObItemType op_type,
                            ObSelectStmt *subquery,
                            bool parent_is_set_query,
                            bool &trans_happened);
  int eliminate_groupby_in_exists(ObDMLStmt *stmt,
                                  const ObItemType op_type,
                                  ObSelectStmt *&subquery,
                                  bool &trans_happened);
  int eliminate_groupby_distinct_in_any_all(ObRawExpr *expr, bool &trans_happened);
  int eliminate_distinct_in_any_all(ObSelectStmt *subquery,bool &trans_happened);
  int check_need_add_limit(ObSelectStmt *subquery, bool &need_add_limit);
  int check_limit(const ObItemType op_type,
                  const ObSelectStmt *subquery,
                  bool &has_limit) const;
  int check_has_limit_1(const ObSelectStmt *stmt,
                        bool &has_limit_1) const;
  int check_const_select(const ObSelectStmt &stmt, bool &is_const_select) const;
  int get_push_down_conditions(ObDMLStmt *stmt,
                               JoinedTable *join_table,
                               ObIArray<ObRawExpr *> &join_conds,
                               ObIArray<ObRawExpr *> &push_down_conds);
  int try_trans_any_all_as_exists(ObDMLStmt *stmt, 
                                  ObRawExpr *&expr, 
                                  ObNotNullContext *not_null_ctx, 
                                  bool used_as_condition,
                                  bool &trans_happened);
  int add_limit_for_any_all_subquery(ObRawExpr *stmt,bool &trans_happened);
  int transform_any_all_as_exists(ObDMLStmt *stmt, bool &trans_happened);
  int transform_any_all_as_exists_joined_table(ObDMLStmt* stmt, 
                                               TableItem *table,
                                               bool &trans_happened);
  int try_trans_any_all_as_exists(ObDMLStmt *stmt,
                                  ObIArray<ObRawExpr* > &exprs,
                                  ObNotNullContext *not_null_cxt,
                                  bool used_as_condition,
                                  bool &trans_happened);
  int empty_table_subquery_can_be_eliminated_in_exists(ObRawExpr *expr,
                                                       bool &is_valid);
  int do_trans_empty_table_subquery_as_expr(ObRawExpr *&expr,
                                            bool &trans_happened);
};

}
}
#endif // OB_TRANSFORM_SIMPILFY_SUBQUERY_H
