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

#ifndef OCEANBASE_SQL_RESOLVER_DDL_OB_FORK_TABLE_RESOLVER_H_
#define OCEANBASE_SQL_RESOLVER_DDL_OB_FORK_TABLE_RESOLVER_H_ 1

#include "sql/resolver/ddl/ob_ddl_resolver.h"
#include "sql/resolver/ddl/ob_fork_table_stmt.h"

namespace oceanbase
{
namespace sql
{
class ObForkTableResolver : public ObDDLResolver
{
public:
  enum node_type {
    DST_TABLE_NODE = 0,
    SRC_TABLE_NODE,
    MAX_NODE
  };
public:
  explicit ObForkTableResolver(ObResolverParams &params);
  virtual ~ObForkTableResolver();

  virtual int resolve(const ParseNode &parse_tree);
private:
  DISALLOW_COPY_AND_ASSIGN(ObForkTableResolver);
};
}  // namespace sql
}  // namespace oceanbase

#endif /* OCEANBASE_SQL_RESOLVER_DDL_OB_FORK_TABLE_RESOLVER_H_ */
