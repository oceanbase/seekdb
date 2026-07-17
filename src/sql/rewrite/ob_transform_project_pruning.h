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

#ifndef OCEANBASE_SQL_REWRITE_OB_TRANSFORM_PROJECT_PRUNING_
#define OCEANBASE_SQL_REWRITE_OB_TRANSFORM_PROJECT_PRUNING_
#include "lib/container/ob_se_array.h"
#include "lib/container/ob_bit_set.h"
#include "sql/rewrite/ob_transform_rule.h"
#include "sql/resolver/dml/ob_select_stmt.h"
namespace oceanbase
{
namespace sql
{

class ObTransformProjectPruning : public ObTransformRule
{
public:
  explicit ObTransformProjectPruning(ObTransformerCtx *ctx)
    : ObTransformRule(ctx, TransMethod::PRE_ORDER, T_PROJECT_PRUNE) {}
  virtual ~ObTransformProjectPruning() {}
  virtual int transform_one_stmt(common::ObIArray<ObParentDMLStmt> &parent_stmts,
                                 ObDMLStmt *&stmt,
                                 bool &trans_happened) override;
  virtual int transform_one_stmt_with_outline(common::ObIArray<ObParentDMLStmt> &parent_stmts,
                                              ObDMLStmt *&stmt,
                                              bool &trans_happened) override;
private:
  int transform_table_items(ObDMLStmt *&stmt,
                            bool with_outline,
                            bool &trans_happened);
  // Do project pruning on the subquery of can_prune_select_item
  int project_pruning(const uint64_t table_id,
                      ObSelectStmt &child_stmt,
                      ObDMLStmt &upper_stmt,
                      bool &trans_happened);

  virtual int check_hint_status(const ObDMLStmt &stmt, bool &need_trans) override;                                                                                                                                                                                 
  virtual int construct_transform_hint(ObDMLStmt &stmt, void *trans_params) override;

  int is_const_expr(ObRawExpr* expr, bool &is_const);
 
  int check_transform_validity(const ObSelectStmt &stmt, bool &is_valid); 
 
  int check_hint_allowed_prune(ObSelectStmt &ref_query, bool &allowed);
private:
  DISALLOW_COPY_AND_ASSIGN(ObTransformProjectPruning);
};

}
}

#endif
