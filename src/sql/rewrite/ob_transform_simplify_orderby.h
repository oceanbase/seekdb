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

#ifndef OB_TRANSFORM_SIMPLIFY_ORDER_BY_H
#define OB_TRANSFORM_SIMPLIFY_ORDER_BY_H

#include "sql/rewrite/ob_transform_rule.h"

namespace oceanbase {
namespace sql {

class ObTransformSimplifyOrderby : public ObTransformRule
{
public:
  ObTransformSimplifyOrderby(ObTransformerCtx *ctx)
    : ObTransformRule(ctx, TransMethod::POST_ORDER,
                      T_SIMPLIFY_ORDER_BY)
  {}

  virtual ~ObTransformSimplifyOrderby() {}

  virtual int transform_one_stmt(common::ObIArray<ObParentDMLStmt> &parent_stmts,
                                 ObDMLStmt *&stmt,
                                 bool &trans_happened) override;

  int remove_order_by_for_subquery(ObDMLStmt *stmt, bool &trans_happened);

  int remove_order_by_for_view_stmt(ObDMLStmt *stmt, bool &trans_happened, bool &force_serial_set_order);

  int do_remove_stmt_order_by(ObSelectStmt *select_stmt, bool &trans_happened);

  int remove_order_by_duplicates(ObDMLStmt *stmt,
                                 bool &trans_happened);

  int exist_item_by_expr(ObRawExpr *expr,
                         ObIArray<OrderItem> &order_items,
                         bool &is_exist);

  int remove_order_by_for_set_stmt(ObDMLStmt *&stmt, bool &trans_happened, bool& force_serial_set_order);
};

}
}

#endif // OB_TRANSFORM_SIMPLIFY_ORDER_BY_H
