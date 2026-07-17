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

#ifndef OB_TRANSFORM_SIMPLIFY_LIMIT_H
#define OB_TRANSFORM_SIMPLIFY_LIMIT_H

#include "sql/rewrite/ob_transform_rule.h"

namespace oceanbase {
namespace sql {

class ObTransformSimplifyLimit : public ObTransformRule
{
public:
  ObTransformSimplifyLimit(ObTransformerCtx *ctx)
    : ObTransformRule(ctx, TransMethod::POST_ORDER,
                      T_SIMPLIFY_LIMIT)
  {}

  virtual ~ObTransformSimplifyLimit() {}

  virtual int transform_one_stmt(common::ObIArray<ObParentDMLStmt> &parent_stmts,
                                 ObDMLStmt *&stmt,
                                 bool &trans_happened) override;
private:
  int add_limit_to_semi_right_table(ObDMLStmt *stmt,
                                    bool &trans_happened);

  int check_need_add_limit_to_semi_right_table(ObDMLStmt *stmt,
                                               SemiInfo *semi_info,
                                               bool &need_add);

  int pushdown_limit_order_for_union(ObDMLStmt *stmt, bool& trans_happened);

  int check_can_pushdown_limit_order(ObSelectStmt& upper_stmt,
                                     ObSelectStmt*& view_stmt,
                                     bool& can_push);

  int do_pushdown_limit_order_for_union(ObSelectStmt& upper_stmt, ObSelectStmt* view_stmt);
};

}
}
#endif // OB_TRANSFORM_SIMPLIFY_LIMIT_H
