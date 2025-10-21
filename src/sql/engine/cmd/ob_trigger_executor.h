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

#ifndef OCEANBASE_SQL_OB_TRIGGER_EXECUTOR_H_
#define OCEANBASE_SQL_OB_TRIGGER_EXECUTOR_H_

#include "lib/container/ob_vector.h"
#include "sql/parser/parse_node.h"
#include "sql/resolver/ob_stmt_type.h"
#include "share/schema/ob_schema_getter_guard.h"
#include "share/ob_rpc_struct.h"
#include "lib/mysqlclient/ob_mysql_proxy.h"

namespace oceanbase
{
namespace sql
{
class ObExecContext;
class ObCreateTriggerStmt;
class ObAlterTriggerStmt;
class ObDropTriggerStmt;

class ObCreateTriggerExecutor
{
public:
  ObCreateTriggerExecutor() {}
  virtual ~ObCreateTriggerExecutor() {}
  int execute(ObExecContext &ctx, ObCreateTriggerStmt &stmt);
  int analyze_dependencies(share::schema::ObSchemaGetterGuard &schema_guard,
                           ObSQLSessionInfo *session_info,
                           common::ObMySQLProxy *sql_proxy,
                           ObIAllocator &allocator,
                           obrpc::ObCreateTriggerArg &arg);
private:
  ObCreateTriggerExecutor(const ObCreateTriggerExecutor&);
  void operator=(const ObCreateTriggerExecutor&);
};

class ObDropTriggerExecutor
{
public:
  ObDropTriggerExecutor() {}
  virtual ~ObDropTriggerExecutor() {}
  int execute(ObExecContext &ctx, ObDropTriggerStmt &stmt);
private:
  ObDropTriggerExecutor(const ObDropTriggerExecutor&);
  void operator=(const ObDropTriggerExecutor&);
};

class ObAlterTriggerExecutor
{
public:
  ObAlterTriggerExecutor() {}
  virtual ~ObAlterTriggerExecutor() {}
  int execute(ObExecContext &ctx, ObAlterTriggerStmt &stmt);
private:
  ObAlterTriggerExecutor(const ObAlterTriggerExecutor&);
  void operator=(const ObAlterTriggerExecutor&);
};

} // namespace sql
} // namespace oceanbase
#endif /* OCEANBASE_SQL_OB_TRIGGER_EXECUTOR_H_ */
