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

#ifndef OCRANBASE_SQL_ENGINE_CMD_OB_LOCK_TABLE_EXECUTOR_
#define OCRANBASE_SQL_ENGINE_CMD_OB_LOCK_TABLE_EXECUTOR_

#include "share/ob_define.h"

namespace oceanbase
{
namespace sql
{

class ObExecContext;
class ObLockTableStmt;
class ObLockTableExecutor
{
public:
  ObLockTableExecutor() {}
  virtual ~ObLockTableExecutor() {}
  int execute(ObExecContext &ctx, ObLockTableStmt &stmt);
private:
  int execute_oracle_(ObExecContext &ctx, ObLockTableStmt &stmt);
  int execute_mysql_(ObExecContext &ctx, ObLockTableStmt &stmt);
  DISALLOW_COPY_AND_ASSIGN(ObLockTableExecutor);
};

} //sql
} // oceanbase
#endif
