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
 
#pragma once

#include "lib/ob_define.h"

namespace oceanbase
{
namespace observer
{
class ObTableLoadTableCtx;
}
namespace sql
{
class ObExecContext;
class ObPhysicalPlan;
class ObOptimizerContext;
class ObDMLStmt;

class ObTableDirectInsertService
{
public:
  // all insert-tasks within an insert into select clause are wrapped by a single direct insert instance
  static int start_direct_insert(ObExecContext &ctx, ObPhysicalPlan &plan);
  static int commit_direct_insert(ObExecContext &ctx, ObPhysicalPlan &plan);
  static int finish_direct_insert(ObExecContext &ctx, ObPhysicalPlan &plan, const bool commit);
  // each insert-task is processed in a single thread and is wrapped by a table load trans
  static int open_task(const uint64_t table_id,
                       const int64_t px_task_id,
                       const int64_t ddl_task_id,
                       observer::ObTableLoadTableCtx *&table_ctx);
  static int close_task(const uint64_t table_id,
                        const int64_t px_task_id,
                        const int64_t ddl_task_id,
                        observer::ObTableLoadTableCtx *table_ctx,
                        const int error_code = OB_SUCCESS);
};
} // namespace sql
} // namespace oceanbase
