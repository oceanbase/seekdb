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

#ifndef OB_TRANSFORM_SIMPLIFY_DISTINCT_H
#define OB_TRANSFORM_SIMPLIFY_DISTINCT_H

#include "sql/rewrite/ob_transform_rule.h"

namespace oceanbase {
namespace sql {

class ObTransformSimplifyDistinct : public ObTransformRule
{
public:
  ObTransformSimplifyDistinct(ObTransformerCtx *ctx)
    : ObTransformRule(ctx, TransMethod::POST_ORDER,
                      T_SIMPLIFY_DISTINCT)
  {}

  virtual ~ObTransformSimplifyDistinct() {}

  virtual int transform_one_stmt(common::ObIArray<ObParentDMLStmt> &parent_stmts,
                                 ObDMLStmt *&stmt,
                                 bool &trans_happened) override;

  int remove_distinct_on_const_exprs(ObSelectStmt *stmt, bool &trans_happened);

  int distinct_can_be_eliminated(ObSelectStmt *stmt, bool &is_valid);

  int remove_distinct_on_unique_exprs(ObSelectStmt *stmt, bool &trans_happened);

  int remove_child_stmt_distinct(ObSelectStmt *set_stmt, bool &trans_happened);

  int try_remove_child_stmt_distinct(ObSelectStmt *stmt, bool &trans_happened);

};

}
}
#endif // OB_TRANSFORM_SIMPLIFY_DISTINCT_H
