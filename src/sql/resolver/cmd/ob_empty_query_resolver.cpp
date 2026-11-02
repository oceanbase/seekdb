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
int ObEmptyQueryResolver::resolve(const ParseNode &parse_tree)
{
  int ret = OB_SUCCESS;
  ObEmptyQueryStmt *empty_query_stmt = NULL;
  if (T_EMPTY_QUERY != parse_tree.type_) {
    ret = OB_ERR_UNEXPECTED;
    LOG_ERROR("parser tree type is not T_EMPTY_QUERY", K(parse_tree.type_), K(ret));
  } else if (0 == parse_tree.value_) {
    //empty query with no comment
    ret = OB_ERR_EMPTY_QUERY;

  } else if (OB_ISNULL(empty_query_stmt = create_stmt<ObEmptyQueryStmt>())) {
    ret = OB_SQL_RESOLVER_NO_MEMORY;
    LOG_WARN("failed to create empty query stmt", K(ret));
  } else {}
  return ret;
}
} // sql
} // oceanbase
