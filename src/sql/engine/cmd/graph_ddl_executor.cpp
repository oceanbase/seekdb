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

#define USING_LOG_PREFIX SQL_ENG
#include "sql/engine/cmd/graph_ddl_executor.h"
#include "sql/resolver/ddl/graph_ddl_stmt.h"
#include "sql/engine/ob_exec_context.h"
#include "query/command/ob_root_command_service.h"
#include "query/command/ob_root_service_serialization.h"
namespace oceanbase
{
using namespace common;
namespace sql
{
int GraphDDLExecutor::execute(ObExecContext &ctx, GraphDDLStmt &stmt)
{
  int ret = OB_SUCCESS;
  ObString ddl;
  if (OB_FAIL(stmt.get_first_stmt(ddl))) {
  } else if (stmt.get_stmt_type() == stmt::T_CREATE_PROPERTY_GRAPH) {
    ret = query::serialize_root_service_call([&] {
      return ctx.root_command_service().create_property_graph(stmt.graph_, ddl);
    });
  } else {
    ret = query::serialize_root_service_call([&] {
      return ctx.root_command_service().drop_property_graph(
          stmt.graph_.get_database_id(), stmt.graph_.get_name(), stmt.if_exists_, ddl);
    });
  }
  return ret;
}
} // namespace sql
} // namespace oceanbase
