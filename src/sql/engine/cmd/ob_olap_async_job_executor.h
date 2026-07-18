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

#ifndef OCEANBASE_SQL_ENGINE_CMD_OLAP_ASYNC_JOB_CMD_EXECUTOR_
#define OCEANBASE_SQL_ENGINE_CMD_OLAP_ASYNC_JOB_CMD_EXECUTOR_
#include "lib/string/ob_string.h"
#include "lib/container/ob_array_serialization.h"
#include "share/schema/ob_schema_struct.h"

namespace oceanbase
{
namespace sql
{
class ObExecContext;
class ObOLAPAsyncCancelJobStmt;

class ObOLAPAsyncCancelJobExecutor
{
public:
  ObOLAPAsyncCancelJobExecutor() {}
  virtual ~ObOLAPAsyncCancelJobExecutor() {}
  int execute(ObExecContext &ctx, ObOLAPAsyncCancelJobStmt &stmt);
private:
  DISALLOW_COPY_AND_ASSIGN(ObOLAPAsyncCancelJobExecutor);
};
}
}
#endif //OCEANBASE_SQL_ENGINE_CMD_OLAP_ASYNC_JOB_CMD_EXECUTOR_
