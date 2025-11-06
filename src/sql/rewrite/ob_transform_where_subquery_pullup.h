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

#ifndef _OB_WHERE_SUBQUERY_PULLUP_H_
#define _OB_WHERE_SUBQUERY_PULLUP_H_

#include "sql/rewrite/ob_transform_rule.h"
#include "sql/resolver/dml/ob_select_stmt.h"
#include "sql/rewrite/ob_union_find.h"

namespace oceanbase
{
namespace sql
{

/**
 * This class mainly performs SQL rewriting related to subqueries, which is divided into two categories:
 * 1. eliminate_subquery Simplify part of the subquery, or remove the entire redundant subquery
 *    (1) Remove the entire redundant subquery
 *         1. For EXISTS / NOT EXISTS statements that can be evaluated as TRUE or FALSE before execution,
 *            they can be eliminated at this stage and replaced with the corresponding TRUE or FALSE. The judgment rule is seen in
 *            subquery_can_be_eliminated_in_exists()
 *    (2) Simplify part of the subquery
 *         1. For EXISTS / NOT EXISTS that cannot be eliminated, its SELECT LIST and ORDER BY can be eliminated;
 *            under certain conditions, GROUP BY can also be eliminated. The judgment rule is seen in
 *            groupby_can_be_eliminated_in_exists()
 *         2. For EXISTS / NOT EXISTS that cannot be eliminated, if there is no LIMIT statement itself, a LIMIT 1 will be added to it.
 *         3. For ANY / ALL subqueries, under certain conditions, GROUP BY can also be eliminated. The judgment rule is seen in
 *            groupby_can_be_eliminated_in_any_all()
 *
 * 2. pullup_subquery Pull up the subquery as a view or merge it into the main query
 *    (1) Merge the subquery into the main query, rewrite it as SEMI JOIN or ANTI JOIN
 *         It is generally considered that rewriting (2) is better than rewriting (3). If it can be directly merged into the main query, rewriting (3) will not be performed again
 *     e.g.
 *        (Example 1: The subquery is associated with the main query)
 *        SELECT * FROM T1 WHERE T1.c1 in (SELECT T2.c1 FROM T2 WHERE T1.c2 < T2.c2)
 *        ->
 *        SELECT *
 *        FROM T1 SEMI JOIN T2
 *        WHERE T1.c1 = T2.c1 AND T1.c2 < T2.c2
 *    (2) Pull up the subquery unrelated to the main query as a view, rewrite it as SEMI JOIN or ANTI JOIN
 *     e.g.
 *        SELECT * FROM T1 WHERE T1.c1 in (SELECT T2.c1 FROM T2 LIMIT 3)
 *        ->
 *        SELECT *
 *        FROM T1 SEMI JOIN (SELECT T2.c1 FROM T2 LIMIT 3) AS SUBQ
 *        WHERE T1.c1 = SUBQ.c1
 *
 */

class ObWhereSubQueryPullup : public ObTransformRule
{
public:
  ObWhereSubQueryPullup(ObTransformerCtx *ctx)
    : ObTransformRule(ctx, TransMethod::POST_ORDER, T_UNNEST)
  {}
  virtual ~ObWhereSubQueryPullup() {}
  virtual int transform_one_stmt(common::ObIArray<ObParentDMLStmt> &parent_stmts,
                                 ObDMLStmt *&stmt,
                                 bool &trans_happened) override;
  virtual int transform_one_stmt_with_outline(common::ObIArray<ObParentDMLStmt> &parent_stmts,
                                              ObDMLStmt *&stmt,
                                              bool &trans_happened) override;

private:
struct SingleSetParam {
    SingleSetParam():
      query_ref_expr_(NULL),
      not_null_column_(NULL),
      null_reject_select_idx_(),
      use_outer_join_(false)
      {}
    virtual ~SingleSetParam(){}
    TO_STRING_KV(
      K_(query_ref_expr),
      K_(not_null_column),
      K_(null_reject_select_idx),
      K_(use_outer_join)
    );

    ObQueryRefRawExpr *query_ref_expr_;
    ObRawExpr *not_null_column_;
    ObSqlBitSet<> null_reject_select_idx_;
    bool use_outer_join_;
  };
 struct TransformParam {
    TransformParam() : op_(NULL),
                       subquery_expr_(NULL),
                       subquery_(NULL),
                       left_hand_(NULL),
                       can_be_transform_(true),
                       is_correlated_(false),
                       need_create_spj_(false),
                       need_add_limit_constraint_(false)
    {}

    ObOpRawExpr *op_;  // the all/any/exists contain subquery expr;
    ObQueryRefRawExpr *subquery_expr_;//the query expr
    ObSelectStmt *subquery_; //subquery select stmt
    ObRawExpr *left_hand_; //the all/any op left expr
    bool can_be_transform_; // can be rewritten
    bool is_correlated_;//whether directly correlated
    bool need_create_spj_;
    //Just in case different parameters hit same plan, firstly we need add const param constraint
    bool need_add_limit_constraint_;

    TO_STRING_KV(K_(op),
                 K_(subquery_expr),
                 K_(subquery),
                 K_(left_hand),
                 K_(can_be_transform),
                 K_(is_correlated),
                 K_(need_create_spj),
                 K_(need_add_limit_constraint));
  };

  struct SemiInfoSplitHelper {
    SemiInfoSplitHelper() : can_split_(false)
    {}

    bool can_split_;
    ObSEArray<ObSEArray<TableItem*, 4>, 4> connected_tables_;

    TO_STRING_KV(K_(can_split),
                 K_(connected_tables));
  };

  int transform_anyall_query(ObDMLStmt *stmt,
                             ObIArray<ObSelectStmt*> &unnest_stmts,
                             bool &trans_happened);

  int transform_single_set_query(ObDMLStmt *stmt,
                                 ObIArray<ObSelectStmt*> &unnest_stmts,
                                 bool &trans_happened);

  int transform_one_expr(ObDMLStmt *stmt,
                         ObRawExpr *expr,
                         ObIArray<ObSelectStmt*> &unnest_stmts,
                         bool &trans_happened);
  int gather_transform_params(ObDMLStmt *stmt,
                              ObRawExpr *expr,
                              TransformParam &trans_param);

  int can_be_unnested(const ObItemType op_type,
                      const ObSelectStmt *subquery,
                      bool &can_be,
                      bool &need_add_limit_constraint);

  int check_limit(const ObItemType op_type,
                  const ObSelectStmt *subquery,
                  bool &has_limit,
                  bool &need_add_limit_constraint) const;
  
  int do_transform_pullup_subquery(ObDMLStmt* stmt, ObRawExpr* expr, TransformParam &trans_param, bool &trans_happened);

  int get_single_set_subquery(ObDMLStmt &stmt, 
                              ObRawExpr *root_expr,
                              ObRawExpr *expr,
                              bool in_where_cond,
                              ObIArray<SingleSetParam> &queries);

  int check_subquery_validity(ObDMLStmt &stmt, 
                              ObRawExpr *root_expr,
                              ObQueryRefRawExpr *subquery_expr,
                              bool in_where_cond, 
                              ObIArray<SingleSetParam> &queries);


  int unnest_single_set_subquery(ObDMLStmt *stmt,
                                 SingleSetParam& param,
                                 const bool is_vector_assign);

  int wrap_case_when_for_select_expr(SingleSetParam& param,
                                     ObIArray<ObRawExpr *> &select_exprs);

  int wrap_case_when(ObSelectStmt &child_stmt,
                    ObRawExpr *not_null_column,
                    ObRawExpr *&expr);                                                       

  int pull_up_semi_info(ObDMLStmt* stmt,
                        ObSelectStmt* subquery);

  int trans_from_list(ObDMLStmt *stmt,
                      ObSelectStmt *subquery,
                      const bool use_outer_join);

  int pullup_non_correlated_subquery_as_view(ObDMLStmt *stmt,
                                             ObSelectStmt *subquery,
                                             ObRawExpr *expr,
                                             ObQueryRefRawExpr *subquery_expr);

  int pullup_correlated_subquery_as_view(ObDMLStmt *stmt,
                                         ObSelectStmt *subquery,
                                         ObRawExpr *expr,
                                         ObQueryRefRawExpr *subquery_expr);

  int check_transform_validity(ObDMLStmt *stmt, const ObRawExpr* expr, TransformParam &trans_param);

  int check_basic_validity(ObDMLStmt *stmt, const ObRawExpr* expr, TransformParam &trans_param);

  int check_subquery_validity(ObQueryRefRawExpr *query_ref, 
                              ObSelectStmt *subquery,
                              bool is_correlated, 
                              bool &is_valid);

  int pull_up_tables_and_columns(ObDMLStmt* stmt, ObSelectStmt* subquery);

  int generate_conditions(ObDMLStmt *stmt,
                          ObIArray<ObRawExpr *> &subq_exprs,
                          ObSelectStmt* subquery,
                          ObRawExpr *expr,
                          ObIArray<ObRawExpr*> &semi_conds,
                          ObIArray<ObRawExpr*> &right_conds);

  int get_semi_oper_type(ObRawExpr *op, ObItemType &oper_type);

  int generate_anti_condition(ObDMLStmt *stmt,
                              const ObSelectStmt *subquery,
                              ObRawExpr* expr,
                              ObRawExpr* left_arg,
                              ObRawExpr* right_arg,
                              ObRawExpr* generated_column_,
                              ObRawExpr* &anti_expr);

  int make_null_test(ObDMLStmt *stmt, ObRawExpr *in_expr, ObRawExpr *&out_expr);

  int generate_semi_info(ObDMLStmt* stmt,
                         TableItem *right_table,
                         ObIArray<ObRawExpr*> &semi_conditions,
                         ObJoinType join_type,
                         SemiInfo *&semi_info);
  int fill_semi_left_table_ids(ObDMLStmt *stmt,
                               SemiInfo *info);

  int is_where_having_subquery_correlated(const ObIArray<ObExecParamRawExpr *> &exec_params,
                                          const ObSelectStmt &subquery, 
                                          bool &is_correlated);

  virtual int check_hint_status(const ObDMLStmt &stmt, bool &need_trans) override;
  virtual int construct_transform_hint(ObDMLStmt &stmt, void *trans_params) override;

  int check_hint_allowed_unnest(const ObDMLStmt &stmt,
                                const ObSelectStmt &subquery,
                                bool &allowed);
  int check_can_split(ObSelectStmt *subquery,
                      ObIArray<ObRawExpr*> &semi_conditions,
                      ObJoinType join_type,
                      SemiInfoSplitHelper &helper);

private:
  DISALLOW_COPY_AND_ASSIGN(ObWhereSubQueryPullup);
};

}
}

#endif /* _OB_WHERE_SUBQUERY_PULLUP_H_ */
