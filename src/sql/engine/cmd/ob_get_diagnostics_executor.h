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

#ifndef OCEANBASE_SRC_SQL_ENGINE_CMD_OB_GET_DIAGNOSTICS_EXECUTOR_H_
#define OCEANBASE_SRC_SQL_ENGINE_CMD_OB_GET_DIAGNOSTICS_EXECUTOR_H_
#include "sql/resolver/cmd/ob_variable_set_stmt.h"
#include "sql/engine/cmd/ob_variable_set_executor.h"
#include "sql/resolver/cmd/ob_get_diagnostics_stmt.h"
#include "sql/resolver/cmd/ob_get_diagnostics_resolver.h"
#include "share/ob_define.h"
#include "sql/session/ob_session_val_map.h"

namespace oceanbase
{
namespace common
{
class ObIAllocator;
class ObExprCtx;
// namespace sqlclient
// {
class ObMySQLProxy;
// }
}
namespace sql
{
class ObExecContext;
class ObSQLSessionInfo;
class ObPhysicalPlanCtx;
class ObGetDiagnosticsExecutor : public ObVariableSetExecutor
{
public:
  ObGetDiagnosticsExecutor();
  virtual ~ObGetDiagnosticsExecutor();
  int execute(ObExecContext &ctx, ObGetDiagnosticsStmt &stmt);
  int get_condition_num(ObExecContext &ctx, ObGetDiagnosticsStmt &stmt, int64_t &num);
  int assign_condition_val(ObExecContext &ctx, ObGetDiagnosticsStmt &stmt,
                          ObSQLSessionInfo *session_info,
                          sqlclient::ObISQLConnection *conn_write,
                          int64_t err_ret, ObString err_msg_c, ObString sql_state_c);
  
private:
  DISALLOW_COPY_AND_ASSIGN(ObGetDiagnosticsExecutor);
};
}
}
#endif /* OCEANBASE_SRC_SQL_ENGINE_CMD_OB_GET_DIAGNOSTICS_EXECUTOR_ */

