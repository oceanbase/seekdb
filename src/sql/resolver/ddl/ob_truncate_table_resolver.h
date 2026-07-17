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

#ifndef OCEANBASE_SQL_OB_TRUNCATE_TABLE_RESOLVER_
#define OCEANBASE_SQL_OB_TRUNCATE_TABLE_RESOLVER_

#include "sql/resolver/ddl/ob_truncate_table_stmt.h"
#include "sql/resolver/ddl/ob_ddl_resolver.h"

namespace oceanbase
{
namespace sql
{

class ObTruncateTableResolver : public ObDDLResolver
{
public:
  explicit ObTruncateTableResolver(ObResolverParams &params);
  virtual ~ObTruncateTableResolver();
  virtual int resolve(const ParseNode &parse_tree);
  ObTruncateTableStmt *get_truncate_table_stmt()
  {
    return static_cast<ObTruncateTableStmt*>(stmt_);
  };

private:
  static const int64_t TABLE_NODE = 0;         /* 0. table_node  */
  DISALLOW_COPY_AND_ASSIGN(ObTruncateTableResolver);
};


} //end namespace sql
} //end namespace oceanbase

#endif // OCEANBASE_SQL_OB_TRUNCATE_TABLE_RESOLVER_

