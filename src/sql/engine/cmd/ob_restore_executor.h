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

#ifndef OCEANBASE_SQL_OB_RESTORE_EXECUTOR_
#define OCEANBASE_SQL_OB_RESTORE_EXECUTOR_

namespace oceanbase
{
namespace sql
{
class ObExecContext;
class ObPhysicalRestoreTenantStmt;

class ObPhysicalRestoreTenantExecutor
{
public:
  ObPhysicalRestoreTenantExecutor();
  virtual ~ObPhysicalRestoreTenantExecutor();
  int execute(ObExecContext &ctx, ObPhysicalRestoreTenantStmt &stmt);
private:
  int sync_wait_tenant_created_(ObExecContext &ctx, const ObString &tenant_name, const int64_t job_id);
  int physical_restore_preview(ObExecContext &ctx, ObPhysicalRestoreTenantStmt &stmt);
};
} //end namespace sql
} //end namespace oceanbase


#endif //OCEANBASE_SQL_OB_RESTORE_EXECUTOR_
