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

#ifndef OCEANBASE_SQL_EXECUTOR_OB_REMOTE_IDENTITY_TASK_SPLITER_
#define OCEANBASE_SQL_EXECUTOR_OB_REMOTE_IDENTITY_TASK_SPLITER_

#include "sql/executor/ob_task_spliter.h"
#include "lib/container/ob_array.h"

namespace oceanbase
{
namespace sql
{
class ObPhysicalPlan;
class ObTaskInfo;
class ObRemoteIdentityTaskSpliter : public ObTaskSpliter
{
public:
  ObRemoteIdentityTaskSpliter();
  virtual ~ObRemoteIdentityTaskSpliter();
  virtual int get_next_task(ObTaskInfo *&task);
  inline virtual TaskSplitType get_type() const { return ObTaskSpliter::REMOTE_IDENTITY_SPLIT; }
private:
  ObTaskInfo *task_;
private:
  DISALLOW_COPY_AND_ASSIGN(ObRemoteIdentityTaskSpliter);
};
}
}
#endif /* OCEANBASE_SQL_EXECUTOR_OB_REMOTE_IDENTITY_TASK_SPLITER_ */

