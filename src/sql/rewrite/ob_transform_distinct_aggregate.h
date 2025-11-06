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

#ifndef _OB_TRANSFORM_DISTINCT_AGGREGATE_H
#define _OB_TRANSFORM_DISTINCT_AGGREGATE_H

#include "sql/rewrite/ob_transform_rule.h"
#include "sql/resolver/dml/ob_select_stmt.h"

namespace oceanbase
{
namespace sql
{

class ObTransformDistinctAggregate : public ObTransformRule
{
public:
  explicit ObTransformDistinctAggregate(ObTransformerCtx *ctx)
  : ObTransformRule(ctx, TransMethod::POST_ORDER, T_TRANSFORM_DISTINCT_AGG) {}

  virtual ~ObTransformDistinctAggregate() {}

  virtual int transform_one_stmt(common::ObIArray<ObParentDMLStmt> &parent_stmts,
                                 ObDMLStmt *&stmt,
                                 bool &trans_happened) override;
private:
  int check_transform_validity(const ObDMLStmt *stmt, bool &is_valid);
  int do_transform(ObSelectStmt *stmt, bool &trans_happened);
  int classify_aggr_exprs(const ObIArray<ObAggFunRawExpr*> &aggr_exprs,
                          ObIArray<ObAggFunRawExpr*> &non_distinct_aggr,
                          ObIArray<ObAggFunRawExpr*> &distinct_aggr);
  int construct_view_select_exprs(const ObIArray<ObAggFunRawExpr*> &non_distinct_aggr,
                                  ObIArray<ObRawExpr*> &view_select_exprs);
  int construct_view_group_exprs(const ObIArray<ObRawExpr*> &ori_group_expr,
                                 const ObIArray<ObAggFunRawExpr*> &distinct_aggr,
                                 ObIArray<ObRawExpr*> &view_group_exprs);
  int replace_aggr_func(ObSelectStmt *stmt,
                        TableItem *view_table,
                        const ObIArray<ObAggFunRawExpr*> &distinct_aggr);
private:
  DISALLOW_COPY_AND_ASSIGN(ObTransformDistinctAggregate);
};
} // namespace sql
} // namespace oceanbase

#endif
