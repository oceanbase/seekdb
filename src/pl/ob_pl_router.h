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

#ifndef OCEANBASE_SRC_PL_OB_PL_ROUTER_H_
#define OCEANBASE_SRC_PL_OB_PL_ROUTER_H_

#include "ob_pl_stmt.h"

namespace oceanbase {
namespace pl {

class ObPLRouter
{
public:
  ObPLRouter(const share::schema::ObRoutineInfo &routine_info,
             sql::ObSQLSessionInfo &session_info,
             share::schema::ObSchemaGetterGuard &schema_guard,
             common::ObMySQLProxy &sql_proxy)
    : routine_info_(routine_info),
      session_info_(session_info),
      schema_guard_(schema_guard),
      sql_proxy_(sql_proxy),
      inner_allocator_(ObModIds::OB_PL_TEMP, OB_MALLOC_NORMAL_BLOCK_SIZE, MTL_ID()),
      expr_factory_(inner_allocator_) {}
  virtual ~ObPLRouter() {}

  int analyze(ObString &route_sql, common::ObIArray<share::schema::ObDependencyInfo> &dep_infos, ObRoutineInfo &routine_info, obrpc::ObDDLArg *ddl_arg);
  int simple_resolve(ObPLFunctionAST &func_ast);
  static int analyze_stmt(const ObPLStmt *stmt, ObString &route_sql);

private:
  static int check_route_sql(const ObPLSql *pl_sql, ObString &route_sql);
  static int check_error_in_resolve(int code);
private:
  const share::schema::ObRoutineInfo &routine_info_;
  sql::ObSQLSessionInfo &session_info_;
  share::schema::ObSchemaGetterGuard &schema_guard_;
  common::ObMySQLProxy &sql_proxy_;
  ObArenaAllocator inner_allocator_;
  sql::ObRawExprFactory expr_factory_;
};


}
};


#endif /* OCEANBASE_SRC_PL_OB_PL_ROUTER_H_ */
