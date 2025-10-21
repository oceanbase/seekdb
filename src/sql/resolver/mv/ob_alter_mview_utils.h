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

#ifndef OCEANBASE_SQL_RESOLVER_ALTER_MVIEW_UTILS_H_
#define OCEANBASE_SQL_RESOLVER_ALTER_MVIEW_UTILS_H_

#include "lib/ob_define.h"
#include "sql/resolver/ddl/ob_ddl_resolver.h"

namespace oceanbase 
{
namespace sql 
{

class ObAlterMviewUtils
{
public:
  static int resolve_mv_options(const ParseNode &node, 
                                ObSQLSessionInfo *session_info,
                                ObAlterTableStmt *alter_table_stmt,
                                const share::schema::ObTableSchema *table_schema,
                                ObSchemaGetterGuard *schema_guard,
                                common::ObIAllocator *allocator,
                                ObResolverParams &resolver_params);

  static int resolve_mlog_options(const ParseNode &node,
                                  ObSQLSessionInfo *session_info,
                                  ObAlterTableStmt *alter_table_stmt,
                                  common::ObIAllocator *allocator,
                                  ObResolverParams &resolver_params);
  static int check_column_option_for_mlog_master(const ObTableSchema &table_schema, 
                                                 const ObItemType type);
  static int check_action_node_for_mlog_master(const ObTableSchema &table_schema, 
                                               const ObItemType type);
private:
  template<typename T>
  static int resolve_interval_node(const ParseNode &node,
                                   ObSQLSessionInfo *session_info,
                                   common::ObIAllocator *allocator,
                                   ObResolverParams &resolver_params,
                                   T &arg);
};

} // namespace sql
} // namespace oceanbase
#endif
