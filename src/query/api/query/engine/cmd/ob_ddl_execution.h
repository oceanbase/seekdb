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

#ifndef OCEANBASE_QUERY_API_ENGINE_CMD_OB_DDL_EXECUTION_H_
#define OCEANBASE_QUERY_API_ENGINE_CMD_OB_DDL_EXECUTION_H_

#include <stdint.h>

namespace oceanbase
{
namespace sql
{
class ObSQLSessionInfo;
}
namespace query
{
class ObIQueryRuntimeEnvironment;
class ObILocalCommandService;

// Transitional public facade for query-owned DDL lifecycle behavior used by
// data-plane orchestration. The concrete SQL utility remains private.
class ObDDLExecution
{
public:
  static int wait_ddl_finish(
      const int64_t task_id,
      const bool ddl_need_retry_at_executor,
      sql::ObSQLSessionInfo *session,
      ObIQueryRuntimeEnvironment &runtime_environment,
      ObILocalCommandService &local_command_service,
      const bool is_support_cancel = true);
  static int handle_session_exception(sql::ObSQLSessionInfo &session);
};

} // namespace query
} // namespace oceanbase

#endif // OCEANBASE_QUERY_API_ENGINE_CMD_OB_DDL_EXECUTION_H_
