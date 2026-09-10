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

#ifndef OCEANBASE_SQL_RESOLVER_GRAPH_DDL_STMT_H_
#define OCEANBASE_SQL_RESOLVER_GRAPH_DDL_STMT_H_
#include "sql/resolver/ddl/ob_ddl_stmt.h"
#include "share/schema/graph_schema.h"
namespace oceanbase
{
namespace sql
{
class GraphDDLStmt final : public ObDDLStmt
{
public:
  GraphDDLStmt() : ObDDLStmt(stmt::T_CREATE_PROPERTY_GRAPH), if_exists_(false) {}
  obcall::ObDDLArg &get_ddl_arg() override { return ddl_arg_; }
  share::schema::GraphSchema graph_;
  common::ObString database_name_;
  bool if_exists_;
private:
  obcall::ObDDLArg ddl_arg_;
  DISALLOW_COPY_AND_ASSIGN(GraphDDLStmt);
};
} // namespace sql
} // namespace oceanbase
#endif
