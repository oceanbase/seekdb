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

#ifndef OCEANBASE_SQL_ENGINE_CMD_OB_MERGE_TABLE_EXECUTOR_H_
#define OCEANBASE_SQL_ENGINE_CMD_OB_MERGE_TABLE_EXECUTOR_H_

#include "sql/engine/ob_exec_context.h"

namespace oceanbase
{
namespace sql
{

class ObMergeTableStmt;
class ObMergeTableExecutor
{
public:
  ObMergeTableExecutor() {}
  virtual ~ObMergeTableExecutor() {}
  int execute(ObExecContext &ctx, ObMergeTableStmt &stmt);
private:
  DISALLOW_COPY_AND_ASSIGN(ObMergeTableExecutor);
};

} // namespace sql
} // namespace oceanbase

#endif /* OCEANBASE_SQL_ENGINE_CMD_OB_MERGE_TABLE_EXECUTOR_H_ */
