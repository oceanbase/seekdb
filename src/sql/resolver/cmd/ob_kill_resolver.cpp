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
int ObKillResolver::resolve(const peg::Node &statement)
{
  int ret = OB_SUCCESS;
  ObKillStmt *kill = create_stmt<ObKillStmt>();
  ObRawExpr *target = nullptr;
  if (kill == nullptr) {
    ret = OB_ALLOCATE_MEMORY_FAILED;
  } else if (OB_FAIL(ObResolverUtils::resolve_const_expr(params_, peg::kill_target(statement), target, nullptr))) {
  } else {
    kill->set_is_query(peg::kill_query_only(statement));
    kill->set_value_expr(target);
  }
  return ret;
}
} // sql
} // oceanbase
