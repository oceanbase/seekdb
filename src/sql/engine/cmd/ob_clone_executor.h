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

#ifndef OCEANBASE_SQL_OB_CLONE_EXECUTOR_
#define OCEANBASE_SQL_OB_CLONE_EXECUTOR_

namespace oceanbase
{
namespace sql
{
class ObExecContext;
class ObCloneTenantStmt;

class ObCloneTenantExecutor
{
public:
  ObCloneTenantExecutor() {}
  virtual ~ObCloneTenantExecutor() {}
  int execute(ObExecContext &ctx, ObCloneTenantStmt &stmt);
  int wait_clone_tenant_finished_(ObExecContext &ctx,
                                  const int64_t job_id);
private:
  DISALLOW_COPY_AND_ASSIGN(ObCloneTenantExecutor);
};

} //end namespace sql
} //end namespace oceanbase


#endif //OCEANBASE_SQL_OB_CLONE_EXECUTOR_
