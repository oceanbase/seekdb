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

#ifndef _OB_TRANSFORM_VIEW_MERGE_H
#define _OB_TRANSFORM_VIEW_MERGE_H

#include "sql/rewrite/ob_transform_rule.h"
#include "sql/resolver/dml/ob_select_stmt.h"
#include "sql/resolver/dml/ob_update_stmt.h"


namespace oceanbase
{

namespace common
{
class ObIAllocator;
}

namespace sql
{

class ObDelUpdStmt;

class ObTransformViewMerge : public ObTransformRule
{
public:
  ObTransformViewMerge(ObTransformerCtx *ctx)
    : ObTransformRule(ctx, TransMethod::POST_ORDER, T_MERGE_HINT)
  {}
  virtual ~ObTransformViewMerge() {}

  virtual int transform_one_stmt(common::ObIArray<ObParentDMLStmt> &parent_stmts,
                                 ObDMLStmt *&stmt,
                                 bool &trans_happened) override;
  virtual int transform_one_stmt_with_outline(common::ObIArray<ObParentDMLStmt> &parent_stmts,
                                              ObDMLStmt *&stmt,
                                              bool &trans_happened) override;
  virtual int construct_transform_hint(ObDMLStmt &stmt, void *trans_params) override;
private:
  struct ViewMergeHelper {
    ViewMergeHelper():
      parent_table(NULL),
      trans_table(NULL),
      not_null_column(NULL),
      need_check_null_propagate(false),
      can_push_where(true)
      {}
    virtual ~ViewMergeHelper(){}

    JoinedTable *parent_table;
    TableItem *trans_table;
    ObRawExpr *not_null_column;
    bool need_check_null_propagate;
    bool can_push_where;
  };
  int check_can_be_merged(ObDMLStmt *parent_stmt,
                          ObSelectStmt* child_stmt,
                          ViewMergeHelper &helper,
                          bool need_check_subquery,
                          bool in_joined_table,
                          bool &can_be);

  int check_left_join_right_view_need_merge(ObDMLStmt *parent_stmt,
                                            ObSelectStmt* child_stmt,
                                            TableItem *view_table,
                                            JoinedTable *joined_table,
                                            bool &need_merge);

  int check_semi_right_table_can_be_merged(ObDMLStmt *stmt,
                                           ObSelectStmt *ref_query,
                                           bool &can_be);

  int check_basic_validity(ObDMLStmt *parent_stmt,
                           ObSelectStmt *child_stmt,
                           bool in_joined_table,
                           bool &can_be);

  int check_contain_inner_table(const ObSelectStmt &stmt,
                                bool &contain);

  int find_not_null_column(ObDMLStmt &parent_stmt,
                          ObSelectStmt &child_stmt,
                          ViewMergeHelper &helper,
                          ObIArray<ObRawExpr *> &column_exprs,
                          bool &can_be);

  int find_not_null_column_with_condition(ObDMLStmt &parent_stmt,
                                          ObSelectStmt &child_stmt,
                                          ViewMergeHelper &helper,
                                          ObIArray<ObRawExpr *> &column_exprs);

  int find_null_propagate_column(ObRawExpr *condition,
                                ObIArray<ObRawExpr*> &columns,
                                ObRawExpr *&null_propagate_column,
                                bool &is_valid);

  int do_view_merge(ObDMLStmt *parent_stmt,
                                   ObSelectStmt *child_stmt,
                                   TableItem *table_item,
                                   ViewMergeHelper &helper);

  int do_view_merge_for_semi_right_table(ObDMLStmt *parent_stmt,
                                         ObSelectStmt *child_stmt,
                                         SemiInfo *semi_info);

  int replace_stmt_exprs(ObDMLStmt *parent_stmt,
                         ObSelectStmt *child_stmt,
                         TableItem *table,
                         ViewMergeHelper &helper,
                         bool need_wrap_case_when);

  int wrap_case_when_if_necessary(ObSelectStmt &child_stmt,
                                  ViewMergeHelper &helper,
                                  ObIArray<ObRawExpr *> &exprs);

  /**
   * @brief wrap_case_when
   * If the current view is the right table of a left outer join or the left table of a right outer join
   * need to wrap the new column expr for null reject with a case when
   * need to find a non-null column in the view, if null_reject_columns is not empty,
   * directly use the first one, otherwise need to find a non-null column in stmt,
   * it can also be the pk of the base table within the view, but cannot be the null side of the outer join
   */
  int wrap_case_when(ObSelectStmt &child_stmt,
                    ObRawExpr *not_null_column,
                    ObRawExpr *&expr);
  int adjust_stmt_semi_infos(ObDMLStmt *parent_stmt,
                             ObSelectStmt *child_stmt,
                             uint64_t table_id);

  int adjust_updatable_view(ObDMLStmt *parent_stmt, TableItem *table_item);

  int transform_joined_table(ObDMLStmt *stmt,
                             JoinedTable *parent_table,
                             bool can_push_where,
                             ObIArray<ObSelectStmt*> &merged_stmts,
                             bool &trans_happened);
  int create_joined_table_for_view(ObSelectStmt *child_stmt,
                                   TableItem *&new_table);
  int transform_generated_table(ObDMLStmt *parent_stmt,
                                JoinedTable *parent_table,
                                TableItem *table_item,
                                bool need_check_where_condi,
                                bool can_push_where,
                                ObIArray<ObSelectStmt*> &merged_stmts,
                                bool &trans_happened);
  int transform_generated_table(ObDMLStmt *parent_stmt,
                                TableItem *table_item,
                                ObIArray<ObSelectStmt*> &merged_stmts,
                                bool &trans_happened);
  int transform_in_from_item(ObDMLStmt *stmt,
                             ObIArray<ObSelectStmt*> &merged_stmts,
                             bool &trans_happened);
  int transform_in_semi_info(ObDMLStmt *stmt, 
                             ObIArray<ObSelectStmt*> &merged_stmts,
                             bool &trans_happened);
  virtual int need_transform(const common::ObIArray<ObParentDMLStmt> &parent_stmts,
                             const int64_t current_level,
                             const ObDMLStmt &stmt,
                             bool &need_trans) override;
  int check_hint_allowed_merge(ObDMLStmt &stmt, 
                               ObSelectStmt &ref_query, 
                               bool &force_merge,
                               bool &force_no_merge);
  DISALLOW_COPY_AND_ASSIGN(ObTransformViewMerge);
};

} //namespace sql
} //namespace oceanbase

#endif /* _OB_TRANSFORM_VIEW_MERGE_H */
