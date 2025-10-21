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

#define USING_LOG_PREFIX SQL_RESV
#include "sql/resolver/cmd/ob_kill_resolver.h"
#include "sql/resolver/cmd/ob_kill_stmt.h"
#include "sql/resolver/ob_resolver_utils.h"
namespace oceanbase
{
using namespace oceanbase::common;
namespace sql
{
int ObKillResolver::resolve(const ParseNode &parse_tree)
{
  int ret = OB_SUCCESS;
  ObKillStmt *kill_stmt = NULL;
  ObRawExpr *tmp_expr = NULL;
  if (OB_UNLIKELY(!(parse_tree.type_ == T_KILL || parse_tree.type_ == T_ALTER_SYSTEM_KILL)
                  || parse_tree.num_child_ != 2
                  || NULL == parse_tree.children_[0]
                  || parse_tree.children_[0]->type_ != T_BOOL
                  || NULL == parse_tree.children_[1])) {
    ret = OB_INVALID_ARGUMENT;
    LOG_WARN("invalid parse tree", K(ret), K(parse_tree.type_), K(parse_tree.num_child_));
  } else if (OB_UNLIKELY(NULL == (kill_stmt = create_stmt<ObKillStmt>()))) {
    ret = OB_ALLOCATE_MEMORY_FAILED;
    LOG_ERROR("failed to create kill stmt");
  } else if (OB_FAIL(ObResolverUtils::resolve_const_expr(params_, *(parse_tree.children_[1]), tmp_expr, NULL))) {
    LOG_WARN("resolve const expr failed", K(ret));
  } else if (parse_tree.type_ == T_KILL) {
    kill_stmt->set_is_query(1 == parse_tree.children_[0]->value_);
    kill_stmt->set_value_expr(tmp_expr);
  } else if (parse_tree.type_ == T_ALTER_SYSTEM_KILL) {
    if (1 == parse_tree.children_[0]->value_) {
      // TO DO.
    }
    kill_stmt->set_is_alter_system_kill(true);
    kill_stmt->set_value_expr(tmp_expr);
  }
  return ret;
}
} // sql
} // oceanbase
