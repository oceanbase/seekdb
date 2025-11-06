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

#ifndef SQL_ENGINE_CMD_OB_TRIGGER_STORAGE_CACHE_EXECUTOR_H_
#define SQL_ENGINE_CMD_OB_TRIGGER_STORAGE_CACHE_EXECUTOR_H_
#include "sql/engine/ob_exec_context.h"

namespace oceanbase
{
namespace sql
{
class ObTriggerStorageCacheStmt;

class ObTriggerStorageCacheExecutor
{
public:
  ObTriggerStorageCacheExecutor() {}
  virtual ~ObTriggerStorageCacheExecutor() {}
  int execute(ObExecContext &ctx, ObTriggerStorageCacheStmt &stmt);
private:
  DISALLOW_COPY_AND_ASSIGN(ObTriggerStorageCacheExecutor);
};
} // namespace sql
} // namespace oceanbase
#endif /* OCEANBASE_SQL_OB_TRIGGER_EXECUTOR_H_ */
