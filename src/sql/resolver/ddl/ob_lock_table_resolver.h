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

#ifndef OCEANBASE_SQL_RESOLVER_DML_OB_LOCK_TABLE_RESOLVER_
#define OCEANBASE_SQL_RESOLVER_DML_OB_LOCK_TABLE_RESOLVER_

#include "sql/resolver/ddl/ob_lock_table_stmt.h"
#include "sql/resolver/dml/ob_dml_resolver.h"

namespace oceanbase
{
namespace sql
{

class ObLockTableStmt;

// NOTE: yanyuan.cxf LOCK TABLE is dml at oracle, but it does not have
// SQL plan, so we treat it as ddl operator.
class ObLockTableResolver : public ObDMLResolver
{
public:
  static const int64_t TABLE_LIST = 0;
  static const int64_t LOCK_MODE = 1;
  static const int64_t WAIT = 2;
  static const int64_t MYSQL_LOCK_LIST = 0;
  static const int64_t LOCK_TABLE_NODE = 0;
public:
  explicit ObLockTableResolver(ObResolverParams &params)
    : ObDMLResolver(params)
    {}
  virtual ~ObLockTableResolver()
    {}
  virtual int resolve(const ParseNode &parse_tree);
  inline ObLockTableStmt *get_lock_table_stmt() { return static_cast<ObLockTableStmt*>(stmt_); }
private:
  int resolve_mysql_mode_(const ParseNode &parse_tree);
  int resolve_oracle_mode_(const ParseNode &parse_tree);
  int resolve_oracle_table_list_(const ParseNode &table_list);
  int resolve_oracle_lock_mode_(const ParseNode &parse_tree);
  int resolve_oracle_wait_lock_(const ParseNode &parse_tree);

  int resolve_mysql_lock_node_(const ParseNode &parse_node);

  DISALLOW_COPY_AND_ASSIGN(ObLockTableResolver);
};

} // namespace sql
} // namespace oceanbase

#endif // OCEANBASE_SQL_RESOLVER_DML_OB_LOCK_TABLE_RESOLVER_
