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

#ifndef OB_TRANSFORM_PREDICATE_MOVE_AROUND_H
#define OB_TRANSFORM_PREDICATE_MOVE_AROUND_H

#include "sql/rewrite/ob_transform_rule.h"
#include "sql/rewrite/ob_transform_utils.h"
#include "sql/resolver/dml/ob_select_stmt.h"
#include "sql/rewrite/ob_stmt_comparer.h"

namespace oceanbase
{
namespace sql
{
class ObDelUpdStmt;

struct ObTempTableColumnCheckContext : public ObStmtCompareContext {
  ObTempTableColumnCheckContext() :
    ObStmtCompareContext() {
      override_column_compare_ = true;
    }
  virtual bool compare_column(const ObColumnRefRawExpr &left,
                              const ObColumnRefRawExpr &right) override;
  void init(int64_t first, int64_t second,
            const ObIArray<ObHiddenColumnItem> *calculable_items) {
    first_temp_table_id_ = first;
    second_temp_table_id_ = second;
    ObStmtCompareContext::init(calculable_items);
  }
  int64_t first_temp_table_id_;
  int64_t second_temp_table_id_;

private:
  DISABLE_COPY_ASSIGN(ObTempTableColumnCheckContext);
};

class ObTransformPredicateMoveAround : public ObTransformRule
{
public:
  ObTransformPredicateMoveAround(ObTransformerCtx *ctx);

  virtual ~ObTransformPredicateMoveAround();

  virtual int transform_one_stmt(ObIArray<ObParentDMLStmt> &parent_stmts,
                                 ObDMLStmt *&stmt,
                                 bool &trans_happened) override;

  virtual int transform_one_stmt_with_outline(ObIArray<ObParentDMLStmt> &parent_stmts,
                                              ObDMLStmt *&stmt,
                                              bool &trans_happened) override;

  virtual int construct_transform_hint(ObDMLStmt &stmt, void *trans_params) override;
private:
  int do_transform_predicate_move_around(ObDMLStmt *&stmt, bool &trans_happened);

  virtual int need_transform(const common::ObIArray<ObParentDMLStmt> &parent_stmts,
                             const int64_t current_level,
                             const ObDMLStmt &stmt,
                             bool &need_trans) override;

  int inner_do_transfrom(ObDMLStmt *stmt, bool &trans_happened);

  int adjust_transed_stmts();

  int check_outline_valid_to_transform(const ObDMLStmt &stmt, bool &need_trans);

  int pullup_predicates(ObDMLStmt *stmt,
                        ObIArray<int64_t> &select_list,
                        ObIArray<ObRawExpr *> &properties);

  int pullup_predicates_from_view(ObDMLStmt &stmt,
                                  ObIArray<int64_t> &sel_ids,
                                  ObIArray<ObRawExpr *> &input_pullup_preds);

  int generate_set_pullup_predicates(ObSelectStmt &stmt,
                                    ObIArray<int64_t> &sel_ids,
                                    ObIArray<ObRawExpr *> &input_pullup_preds,
                                    ObIArray<ObRawExpr *> &output_pullup_preds);

  /**
   * @brief pullup_predicates_from_set
   * pull up predicates from the subqueries of set stmt
   * @param stmt
   * @param pullup_preds predicates to be pulled up
   * @param parent_stmt the upper stmt of set stmt, used for rewriting pulled-up predicates
   */
  int pullup_predicates_from_set(ObSelectStmt *stmt,
                                ObIArray<ObRawExpr *> &pullup_preds);
  
  /**
   * @brief check_pullup_predicates
   * Select the predicates to output based on the set op type
   * union: select isomorphic predicates;
   * intersect: merge predicates from both sides;
   * except: select predicates from the left side
   */
  int check_pullup_predicates(ObSelectStmt *stmt,
                              ObIArray<ObRawExpr *> &left_pullup_preds,
                              ObIArray<ObRawExpr *> &right_pullup_preds,
                              ObIArray<ObRawExpr *> &output_pullup_preds);

  int generate_pullup_predicates(ObSelectStmt &select_stmt,
                                 ObIArray<int64_t> &sel_ids,
                                 ObIArray<ObRawExpr *> &input_pullup_preds,
                                 ObIArray<ObRawExpr *> &output_pullup_preds);
  int gather_pullup_preds_from_semi_outer_join(ObDMLStmt &stmt,
                                               ObIArray<ObRawExpr*> &preds,
                                               bool remove_preds = false);
  int gather_pullup_preds_from_join_table(TableItem *table,
                                          ObIArray<ObRawExpr*> &preds,
                                          bool remove_preds);

  int remove_pullup_union_predicates(ObIArray<ObRawExpr *> &exprs);

  int remove_useless_equal_const_preds(ObSelectStmt *stmt,
                                       ObIArray<ObRawExpr *> &exprs,
                                       ObIArray<ObRawExpr *> &equal_const_preds);

  int choose_pullup_columns(TableItem &table,
                            ObIArray<ObRawExpr *> &columns,
                            ObIArray<int64_t> &view_sel_list);

  int compute_pullup_predicates(ObSelectStmt &view,
                                const ObIArray<int64_t> &select_list,
                                ObIArray<ObRawExpr *> &original_preds,
                                ObIArray<ObRawExpr *> &input_pullup_preds,
                                ObIArray<ObRawExpr *> &pull_up_preds);

  int check_expr_pullup_validity(ObRawExpr *expr,
                                 const ObIArray<ObRawExpr *> &pullup_list,
                                 int64_t &state);

  int recursive_check_expr_pullup_validity(ObRawExpr *expr,
                                           const ObIArray<ObRawExpr *> &pullup_list,
                                           ObIArray<ObRawExpr *> &parent_exprs,
                                           int64_t &state);

  int rename_pullup_predicates(ObDMLStmt &stmt,
                               TableItem &view,
                               const ObIArray<int64_t> &view_sel_list,
                               ObIArray<ObRawExpr *> &preds);

  int rename_pullup_predicates(ObDMLStmt &stmt,
                               TableItem &view,
                               ObIArray<ObRawExpr *> &preds);

  int pullup_predicates_from_const_select(ObSelectStmt *parent_stmt,
                                          ObSelectStmt *child_stmt,
                                          ObIArray<ObRawExpr*> &pullup_preds);

  int pushdown_predicates(ObDMLStmt *stmt,
                          ObIArray<ObRawExpr *> &predicates);

  int pushdown_into_tables_skip_current_level_stmt(ObDMLStmt &stmt);
  int pushdown_into_joined_table_skip_current_level_stmt(TableItem *table_item);

  /**
   * @brief pushdown_into_set_stmt
   * Push down predicates into the left and right subqueries of the set stmt
   * @param stmt
   * @param predicates Predicates to be pushed down
   */
  int pushdown_into_set_stmt(ObSelectStmt *stmt,
                            ObIArray<ObRawExpr *> &pullup_preds,
                            ObIArray<ObRawExpr *> &pushdown_preds);

  /**
   * @brief check_pushdown_predicates
   * According to the situation of predicate pushdown on both sides, decide which predicates need to be added back to the upper layer stmt
   * union: merge predicates that were not pushed down on both sides; (union requires successful pushdown on both sides)
   * intersect: select predicates that were not pushed down on both sides; (intersect only needs to be pushed down to either side)
   * except: select predicates that were not pushed down on the left side; (except only needs successful pushdown on the left side)
   */
  int check_pushdown_predicates(ObSelectStmt *stmt,
                                ObIArray<ObRawExpr *> &left_pushdown_preds,
                                ObIArray<ObRawExpr *> &right_pushdown_preds,
                                ObIArray<ObRawExpr *> &output_pushdown_preds);

  int extract_valid_preds(ObSelectStmt *stmt,
                          ObSelectStmt *child_stmt,
                          ObIArray<ObRawExpr *> &all_preds,
                          ObIArray<ObRawExpr *> &valid_exprs,
                          ObIArray<ObRawExpr *> &invalid_exprs);

  /**
   * @brief pushdown_into_set_stmt
   * Push down predicates into the subqueries of set stmt
   * @param stmt
   * @param predicates Predicates to be pushed down
   * @param Return predicates that were not pushed down, need to be added back to the upper layer
   */
  int pushdown_into_set_stmt(ObSelectStmt *stmt,
                            ObIArray<ObRawExpr *> &pullup_preds,
                            ObIArray<ObRawExpr *> &pushdown_preds,
                            ObSelectStmt *parent_stmt);

  /**
   * @brief rename_set_op_predicates
   * Rename predicates
   * @param child_stmt 
   * @param parent_stmt
   * @param preds Predicates to be rewritten
   * @param is_pullup
   * If it is a pullup predicate, then it needs to be rewritten from child_stmt to parent_stmt
   * If it is a pushdown predicate, then it needs to be rewritten from parent_stmt to child_stmt
   */
  int rename_set_op_predicates(ObSelectStmt &child_stmt,
                              ObSelectStmt &parent_stmt,
                              ObIArray<ObRawExpr *> &preds,
                              bool is_pullup);

  int pushdown_into_having(ObSelectStmt &stmt,
                           ObIArray<ObRawExpr *> &pullup_preds,
                           ObIArray<ObRawExpr *> &pushdown_preds);

  int pushdown_into_where(ObDMLStmt &stmt,
                          ObIArray<ObRawExpr *> &pullup_preds,
                          ObIArray<ObRawExpr *> &pushdown_preds);

  int pushdown_into_semi_info(ObDMLStmt *stmt,
                              SemiInfo *semi_info,
                              ObIArray<ObRawExpr *> &pullup_preds,
                              ObIArray<ObRawExpr *> &pushdown_preds);

  int pushdown_semi_info_right_filter(ObDMLStmt *stmt,
                                      ObTransformerCtx *ctx,
                                      SemiInfo *semi_info,
                                      ObIArray<ObRawExpr *> &pullup_preds);
  
  int check_has_shared_query_ref(ObRawExpr *expr, bool &has);
  
  int extract_semi_right_table_filter(ObDMLStmt *stmt,
                                      SemiInfo *semi_info,
                                      ObIArray<ObRawExpr *> &right_filters);
  int pushdown_into_table(ObDMLStmt *stmt,
                          TableItem *table,
                          ObIArray<ObRawExpr *> &pullup_preds,
                          ObIArray<ObRawExpr *> &preds,
                          ObIArray<ObRawExprCondition *> &pred_conditions);

  int get_pushdown_predicates(ObDMLStmt &stmt,
                              TableItem &table,
                              ObIArray<ObRawExpr *> &preds,
                              ObIArray<ObRawExpr *> &table_filters,
                              ObIArray<ObRawExprCondition *> *pred_conditions = NULL,
                              ObIArray<ObRawExprCondition *> *table_conditions = NULL);

  int get_pushdown_predicates(ObDMLStmt &stmt,
                              ObSqlBitSet<> &table_set,
                              ObIArray<ObRawExpr *> &preds,
                              ObIArray<ObRawExpr *> &table_filters,
                              ObIArray<ObRawExprCondition *> *pred_conditions = NULL,
                              ObIArray<ObRawExprCondition *> *table_conditions = NULL);

  int pushdown_into_joined_table(ObDMLStmt *stmt,
                                 JoinedTable *joined_table,
                                 ObIArray<ObRawExpr *> &pullup_preds,
                                 ObIArray<ObRawExpr *> &pushdown_preds,
                                 ObIArray<ObRawExprCondition *> &pred_conditions);

  int store_all_preds(const ObDMLStmt &stmt, ObIArray<ObSEArray<ObRawExpr*, 16>> &all_preds);
  int store_join_conds(const TableItem *table, ObIArray<ObSEArray<ObRawExpr*, 16>> &all_preds);
  int check_transform_happened(const ObIArray<ObSEArray<ObRawExpr*, 16>> &all_preds,
                               ObDMLStmt &stmt,
                               bool &is_happened);
  int check_join_conds_deduced(const ObIArray<ObSEArray<ObRawExpr*, 16>> &all_preds,
                               uint64_t &idx,
                               TableItem *table,
                               bool &is_happened);
  int check_conds_deduced(const ObIArray<ObRawExpr *> &old_conditions,
                          ObIArray<ObRawExpr *> &new_conditions,
                          bool &is_happened);

  int pushdown_through_winfunc(ObSelectStmt &stmt,
                               ObIArray<ObRawExpr *> &predicates,
                               ObIArray<ObRawExpr *> &down_preds);
  int pushdown_into_qualify_filter(ObIArray<ObRawExpr *> &predicates,
                                   ObSelectStmt &sel_stmt,
                                   bool &is_happened);

  int pushdown_through_groupby(ObSelectStmt &stmt,
                               ObIArray<ObRawExpr *> &output_predicates);

  int check_pushdown_through_groupby_validity(ObSelectStmt &stmt,
                                          ObRawExpr *having_expr,
                                          bool &is_valid);

  int check_pushdown_through_rollup_validity(ObRawExpr *having_expr,
                               const ObIArray<ObRawExpr *> &rollup_exprs,
                               bool &is_valid);

  int deduce_param_cond_from_aggr_cond(ObItemType expr_type,
                                       ObRawExpr *first,
                                       ObRawExpr *second,
                                       ObRawExpr *&new_predicate);

  int split_or_having_expr(ObSelectStmt &stmt,
                          ObOpRawExpr &or_qual,
                          ObRawExpr *&new_expr);

  int check_having_expr(ObSelectStmt &stmt,
                        ObOpRawExpr &or_qual,
                        ObIArray<ObSEArray<ObRawExpr *, 16> > &sub_exprs,
                        bool &all_contain);

  int inner_split_or_having_expr(ObSelectStmt &stmt,
                                ObIArray<ObSEArray<ObRawExpr *, 16> > &sub_exprs,
                                ObRawExpr *&new_expr);     

  int extract_leaf_filters(ObRawExpr *expr, ObIArray<ObRawExpr *> &leaf_filters);

  int choose_pushdown_preds(ObIArray<ObRawExpr *> &preds,
                            ObIArray<ObRawExpr *> &invalid_preds,
                            ObIArray<ObRawExpr *> &valid_preds);

  int rename_pushdown_predicates(ObDMLStmt &stmt,
                                 TableItem &view,
                                 ObIArray<ObRawExpr *> &preds);

  int transform_predicates(ObDMLStmt &stmt,
                           ObIArray<ObRawExpr *> &original_preds,
                           ObIArray<ObRawExpr *> &other_preds,
                           ObIArray<ObRawExpr *> &target_exprs,
                           ObIArray<ObRawExpr *> &output_preds,
                           bool &is_happened,
                           bool is_pullup = false);
  int check_need_transform_predicates(ObIArray<ObRawExpr *> &exprs, bool &is_needed);
  int accept_outjoin_predicates(ObDMLStmt &stmt,
                                ObIArray<ObRawExpr *> &conds,
                                ObSqlBitSet <> &filter_table_set,
                                ObIArray<ObRawExpr *> &properties,
                                ObIArray<ObRawExpr *> &new_conds);
  int accept_predicates(ObDMLStmt &stmt,
                        ObIArray<ObRawExpr *> &conds,
                        ObIArray<ObRawExpr *> &properties,
                        ObIArray<ObRawExpr *> &new_conds,
                        const bool preserve_conds = false);

  int extract_generalized_column(ObRawExpr *expr,
                                 ObIArray<ObRawExpr *> &output);

  int acquire_transform_params(ObDMLStmt *stmt, ObIArray<ObRawExpr *> *&preds);

  int get_columns_in_filters(ObDMLStmt &stmt,
                             ObIArray<int64_t> &sel_items,
                             ObIArray<ObRawExpr *> &columns);

  int create_equal_exprs_for_insert(ObDelUpdStmt *del_upd_stmt);

  int print_debug_info(const char *str, ObDMLStmt *stmt, ObIArray<ObRawExpr *> &preds);

  int generate_pullup_predicates_for_dual_stmt(ObDMLStmt &stmt,
                                               TableItem &view,
                                               const ObIArray<int64_t> &sel_ids,
                                               ObIArray<ObRawExpr *> &preds);

  int check_false_condition(ObSelectStmt *stmt, bool &false_cond_exists);

  int check_enable_no_pred_deduce(ObDMLStmt &stmt, bool &enable_no_pred_deduce);

  int get_stmt_to_trans(ObDMLStmt *stmt, ObIArray<ObDMLStmt *> &stmt_to_trans);

  int pullup_predicates_from_set_stmt(ObDMLStmt *stmt, ObIArray<int64_t> &sel_ids, ObIArray<ObRawExpr *> &output_pullup_preds);

  int generate_pullup_predicates_for_subquery(ObDMLStmt &stmt, ObIArray<ObRawExpr *> &pullup_preds);

  int choose_and_rename_predicates_for_subquery(ObQueryRefRawExpr *subquery,
                                     ObIArray<ObRawExpr *> &preds,
                                     ObIArray<ObRawExpr *> &renamed_preds);


  int update_current_property(ObDMLStmt &stmt, 
                              ObIArray<ObRawExpr *> &exprs,
                              ObIArray<ObRawExpr *> &push_down_exprs);

  int get_exprs_cnt_exec(ObDMLStmt &stmt,
                         ObIArray<ObRawExpr *> &pullup_preds,
                         ObIArray<ObRawExpr *> &conds);

  int update_subquery_pullup_preds(ObIArray<ObQueryRefRawExpr *> &subquery_exprs,
                                  ObIArray<ObRawExpr *> &current_exprs_can_push);

  int remove_simple_op_null_condition(ObSelectStmt &stmt, ObIArray<ObRawExpr *> &pullup_preds);

  int is_column_expr_null(ObDMLStmt *stmt, const ObColumnRefRawExpr *expr, bool &is_null, ObIArray<ObRawExpr *> &constraints);

  int extract_filter_column_exprs_for_insert(ObDelUpdStmt &del_upd_stmt, ObIArray<ObRawExpr *> &columns);

  int inner_push_down_cte_filter(ObSqlTempTableInfo& info, ObIArray<ObRawExpr *> &filters);

  int push_down_cte_filter(ObIArray<ObSqlTempTableInfo *> &temp_table_info, bool &trans_happened);

  int append_condition_array(ObIArray<ObRawExprCondition *> &conditions, int count, ObRawExprCondition *value);

  int gather_basic_qualify_filter(ObSelectStmt &stmt, ObIArray<ObRawExpr*> &preds);
  int filter_lateral_correlated_preds(TableItem &table_item, ObIArray<ObRawExpr*> &preds);
  void reset();

private:
  typedef ObSEArray<ObRawExpr *, 4> PullupPreds;
  ObArenaAllocator allocator_;
  hash::ObHashMap<uint64_t, int64_t> stmt_map_;
  Ob2DArray<PullupPreds *> stmt_pullup_preds_;
  ObSEArray<ObDMLStmt *, 8> transed_stmts_;
  ObSEArray<ObHint *, 4> applied_hints_;
  ObSEArray<ObSqlTempTableInfo *, 2> temp_table_infos_;
  ObSEArray<ObRawExpr *, 4> null_constraints_;
  ObSEArray<ObRawExpr *, 4> not_null_constraints_;
  ObSEArray<ObPCParamEqualInfo, 4> equal_param_constraints_;
  bool real_happened_;
};

}
}

#endif // OB_TRANSFORM_PREDICATE_MOVE_AROUND_H
