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

#ifndef OB_TRANSFORM_GROUPBY_PULLUP_H
#define OB_TRANSFORM_GROUPBY_PULLUP_H

#include "sql/rewrite/ob_transform_rule.h"
#include "sql/resolver/dml/ob_select_stmt.h"
#include "sql/optimizer/ob_log_join.h"

namespace oceanbase
{
namespace sql
{

/**
 * @brief The ObTransformGroupByReplacement class
 * References:
 *  [1] Including Group-By in Query Optimization
 *  [2] Eager Aggregation and Lazy Aggregation
 */
class ObTransformGroupByPullup : public ObTransformRule
{
public:
  ObTransformGroupByPullup(ObTransformerCtx *ctx)
    : ObTransformRule(ctx, TransMethod::POST_ORDER, T_MERGE_HINT)
  {}

  virtual ~ObTransformGroupByPullup() {}
  virtual int construct_transform_hint(ObDMLStmt &stmt, void *trans_params) override;
  virtual int transform_one_stmt(common::ObIArray<ObParentDMLStmt> &parent_stmts,
                                 ObDMLStmt *&stmt,
                                 bool &trans_happened) override;
protected:
  virtual int adjust_transform_types(uint64_t &transform_types) override;
  virtual int is_expected_plan(ObLogPlan *plan, void *check_ctx, bool is_trans_plan, bool &is_valid) override;
private:
  struct PullupHelper {
    PullupHelper():
      parent_table_(NULL),
      table_id_(OB_INVALID_ID),
      not_null_column_table_id_(OB_INVALID_ID),
      not_null_column_id_(OB_INVALID_ID),
      need_check_null_propagate_(false),
      need_check_having_(false),
      need_merge_(false)
      {}
    virtual ~PullupHelper(){}

    JoinedTable *parent_table_;
    uint64_t table_id_;
    uint64_t not_null_column_table_id_;
    uint64_t not_null_column_id_;
    // If it is on the right side of an outer join or both sides of a full join, a not_null_column needs to be found as a backup;
    bool need_check_null_propagate_;
    // If it is the right table of an outer join or the child table of a full join
    // No having condition
    bool need_check_having_;
    // Whether there is a merge hint, force group by pull up
    // Do not consider the cost
    bool need_merge_;
    TO_STRING_KV(K_(parent_table),
                 K_(table_id),
                 K_(not_null_column_id),
                 K_(not_null_column_table_id),
                 K_(need_check_null_propagate),
                 K_(need_check_having),
                 K_(need_merge));
  };

  struct ObCostBasedPullupCtx {
    ObCostBasedPullupCtx() {};
    uint64_t view_table_id_;
  };

  int check_groupby_validity(const ObSelectStmt &stmt, bool &is_valid);

  int check_collation_validity(const ObDMLStmt &stmt, bool &is_valid);

  /////////////////////    transform group by pull up    //////////////////////////////

  int check_groupby_pullup_validity(ObDMLStmt *stmt,
                                    ObIArray<PullupHelper> &valid_views);

  int check_groupby_pullup_validity(ObDMLStmt *stmt,
                                    TableItem *table,
                                    PullupHelper &helper,
                                    bool contain_inner_table,
                                    ObSqlBitSet<> &ignore_tables,
                                    ObIArray<PullupHelper> &valid_views,
                                    bool &is_valid);

  int check_on_conditions(ObDMLStmt &stmt,
                          ObSqlBitSet<> &ignore_tables);
  int check_where_conditions(ObDMLStmt &stmt,
                             ObSqlBitSet<> &ignore_tables);
  int extract_info_from_column_expr(const ObDMLStmt &stmt,
                                    const ObRawExpr &outer_expr,
                                    TableItem *&table,
                                    ObRawExpr *&select_expr);

  int is_valid_group_stmt(ObSelectStmt *sub_stmt,
                          bool &is_valid_group);

  int check_null_propagate(ObDMLStmt *parent_stmt,
                          ObSelectStmt* child_stmt,
                          PullupHelper &helper,
                          bool &is_valid);

  int find_not_null_column(ObDMLStmt &parent_stmt,
                          ObSelectStmt &child_stmt,
                          PullupHelper &helper,
                          ObIArray<ObRawExpr *> &column_exprs,
                          ObRawExpr *&not_null_column);

  int find_not_null_column_with_condition(ObDMLStmt &parent_stmt,
                                          ObSelectStmt &child_stmt,
                                          PullupHelper &helper,
                                          ObIArray<ObRawExpr *> &column_exprs,
                                          ObRawExpr *&not_null_column);
                                          
  int find_null_propagate_column(ObRawExpr *condition,
                                ObIArray<ObRawExpr*> &columns,
                                ObRawExpr *&null_propagate_column,
                                bool &is_valid);
  int check_table_items(ObDMLStmt *stmt,
                        ObSelectStmt *child_stmt,
                        bool &is_valid);

  int do_groupby_pull_up(ObSelectStmt *stmt,
                         PullupHelper &helper,
                         StmtUniqueKeyProvider &unique_key_provider);

  int get_trans_view(ObDMLStmt *stmt, ObSelectStmt *&view_stmt);

  int wrap_case_when_if_necessary(ObSelectStmt &child_stmt,
                                  PullupHelper &helper,
                                  ObIArray<ObRawExpr *> &exprs);
  /**
   * @brief wrap_case_when
   * If the current view is the right table of a left outer join or the left table of a right outer join
   * need to wrap the new column expr for null reject with a case when
   * need to find a non-null column in the view, if null_reject_columns is not empty,
   * directly use the first one, otherwise need to find a non-null column in stmt,
   * it can also be the pk of the base table within the view, but cannot be the null-complementing side of the outer join
   */
  int wrap_case_when(ObSelectStmt &child_stmt,
                    ObRawExpr *not_null_column,
                    ObRawExpr *&expr);

  int has_group_by_op(ObLogicalOperator *op,
                      bool &bret);

  int check_group_by_subset(ObRawExpr *expr, const ObIArray<ObRawExpr *> &group_exprs, bool &bret);

  int check_hint_valid(const ObDMLStmt &stmt,
                        const ObSelectStmt &ref_query,
                        bool &is_valid);

  virtual int need_transform(const common::ObIArray<ObParentDMLStmt> &parent_stmts,
                             const int64_t current_level,
                             const ObDMLStmt &stmt,
                             bool &need_trans) override;

  int check_original_plan_validity(ObLogicalOperator* root,
                                   uint64_t view_table_id,
                                   bool &is_valid);

  int find_operator(ObLogicalOperator* root,
                    ObIArray<ObLogicalOperator*> &parents,
                    uint64_t view_table_id,
                    ObLogicalOperator *&subplan_op);

  int find_base_operator(ObLogicalOperator *&root);

  int extract_columns_in_join_conditions(ObIArray<ObLogicalOperator*> &parent_ops,
                                         uint64_t table_id,
                                         ObIArray<ObRawExpr*> &column_exprs);

  int get_group_by_subset(ObRawExpr *expr, 
                          const ObIArray<ObRawExpr *> &group_exprs, 
                          ObIArray<ObRawExpr *> &subset_group_exprs);

  int get_group_by_subset(ObIArray<ObRawExpr *> &exprs, 
                          const ObIArray<ObRawExpr *> &group_exprs, 
                          ObIArray<ObRawExpr *> &subset_group_exprs);

  int calc_group_exprs_ndv(const ObIArray<ObRawExpr*> &group_exprs,
                            ObLogicalOperator *subplan_root,
                            double &group_ndv,
                            double &card);

  int check_all_table_has_statistics(ObLogicalOperator *op, bool &has_stats);
  int check_view_table_in_inner_path(ObIArray<ObLogicalOperator*> &parent_ops,
                                     const ObDMLStmt &stmt,
                                     uint64_t table_id,
                                     bool &is_inner_path);

private:
  // help functions
  int64_t get_count_sum_num(const ObIArray<ObRawExpr *> &exprs)
  {
    int64_t num = 0;
    for (int64_t i = 0; i < exprs.count(); ++i) {
      if (OB_ISNULL(exprs.at(i))) {
        // do nothing
      } else if (exprs.at(i)->get_expr_type() == T_FUN_SUM ||
                 exprs.at(i)->get_expr_type() == T_FUN_COUNT) {
        ++num;
      }
    }
    return num;
  }


};

}
}

#endif // OB_TRANSFORM_GROUPBY_REPLACEMENT_H
