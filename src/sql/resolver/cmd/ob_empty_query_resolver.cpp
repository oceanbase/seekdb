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

#define USING_LOG_PREFIX  SQL_ENG
#include "sql/resolver/cmd/ob_empty_query_resolver.h"
#include "sql/resolver/cmd/ob_empty_query_stmt.h"

namespace oceanbase
{
using namespace oceanbase::common;
namespace sql
{
int ObEmptyQueryResolver::resolve(const peg::Node &statement)
{
  int ret = OB_SUCCESS;
  if (statement.kind() == peg::NodeKind::EmptyQuery && !peg::empty_query_has_comment(statement)) ret = OB_ERR_EMPTY_QUERY;
  else if (OB_ISNULL(create_stmt<ObEmptyQueryStmt>())) ret = OB_SQL_RESOLVER_NO_MEMORY;
  return ret;
}
} // namespace sql
} // namespace oceanbase
