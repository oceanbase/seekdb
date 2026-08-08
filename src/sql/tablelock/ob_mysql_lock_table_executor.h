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

#ifndef OCEANBASE_OB_MYSQL_LOCK_TABLE_EXECUTOR_H_
#define OCEANBASE_OB_MYSQL_LOCK_TABLE_EXECUTOR_H_

#include "data_plane/tablelock/ob_table_lock_target.h"
#include "sql/tablelock/ob_lock_executor.h"
#include "sql/session/ob_basic_session_info.h"

namespace oceanbase
{
namespace sql
{
class ObSQLSessionInfo;
class ObExecContext;
}

namespace transaction
{

namespace tablelock
{
class ObMySQLLockTableExecutor : public ObLockExecutor
{
public:
  ObMySQLLockTableExecutor() = default;
  int execute(sql::ObExecContext &ctx,
              const ObIArray<data_plane::ObTableLockTarget> &lock_targets);
private:
  DISALLOW_COPY_AND_ASSIGN(ObMySQLLockTableExecutor);
};

class ObMySQLUnlockTableExecutor : public ObUnLockExecutor
{
public:
  int execute(sql::ObExecContext &ctx);
};

} // tablelock
} // transaction
} // oceanbase
#endif
