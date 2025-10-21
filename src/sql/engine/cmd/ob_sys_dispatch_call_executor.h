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

#ifndef OCEANBASE_SRC_SQL_ENGINE_CMD_OB_SYS_DISPATCH_CALL_EXECUTOR_H_
#define OCEANBASE_SRC_SQL_ENGINE_CMD_OB_SYS_DISPATCH_CALL_EXECUTOR_H_

#include <cstdint>

#include "lib/utility/ob_macro_utils.h"
#include "lib/string/ob_string.h"

namespace oceanbase
{

using namespace common;

namespace sql
{
class ObExecContext;
class ObSysDispatchCallStmt;
class ObSQLSessionInfo;
class ObFreeSessionCtx;

class ObSysDispatchCallExecutor
{
public:
  ObSysDispatchCallExecutor() {}
  virtual ~ObSysDispatchCallExecutor() {}
  DISABLE_COPY_ASSIGN(ObSysDispatchCallExecutor);

  int execute(ObExecContext &ctx, ObSysDispatchCallStmt &stmt);

private:
  int create_session(const uint64_t tenant_id,
                     ObFreeSessionCtx &free_session_ctx,
                     ObSQLSessionInfo *&session_info);
  int init_session(ObSQLSessionInfo &session,
                   const uint64_t tenant_id,
                   const ObString &tenant_name,
                   const ObCompatibilityMode compat_mode);
  int destroy_session(ObFreeSessionCtx &free_session_ctx, ObSQLSessionInfo *session_info);
};

}  // namespace sql
}  // namespace oceanbase

#endif  // OCEANBASE_SRC_SQL_ENGINE_CMD_OB_SYS_DISPATCH_CALL_EXECUTOR_H_
