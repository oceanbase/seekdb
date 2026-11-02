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

#ifndef OCEANBASE_SQL_OB_ALTER_TABLEGROUP_RESOLVER_
#define OCEANBASE_SQL_OB_ALTER_TABLEGROUP_RESOLVER_

#include "sql/resolver/ddl/ob_alter_tablegroup_stmt.h"
#include "sql/resolver/ddl/ob_tablegroup_resolver.h"

namespace oceanbase
{
namespace sql
{
class ObAlterTablegroupResolver : public ObTableGroupResolver
{
  static const int TG_NAME = 0;
  static const int TABLE_LIST = 1;
public:
  explicit ObAlterTablegroupResolver(ObResolverParams &params);
  virtual ~ObAlterTablegroupResolver();
  virtual int resolve(const ParseNode &parse_tree);
  ObAlterTablegroupStmt *get_alter_tablegroup_stmt() { return static_cast<ObAlterTablegroupStmt*>(stmt_); };

private:
  DISALLOW_COPY_AND_ASSIGN(ObAlterTablegroupResolver);
};


} //end namespace sql
} //end namespace oceanbase

#endif // OCEANBASE_SQL_OB_ALTER_TABLEGROUP_RESOLVER_
