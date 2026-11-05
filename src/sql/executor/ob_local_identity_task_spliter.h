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

#ifndef OCEANBASE_SQL_EXECUTOR_OB_LOCAL_IDENTITY_TASK_SPLITER_
#define OCEANBASE_SQL_EXECUTOR_OB_LOCAL_IDENTITY_TASK_SPLITER_

#include "sql/executor/ob_task_spliter.h"

namespace oceanbase
{
namespace sql
{
class ObPhysicalPlan;
class ObTaskInfo;
// This class is only used for generating a single local task, and during the executor stage if it is determined that
// Split type is ObTaskSpliter::LOCAL_IDENTITY_SPLIT will be directly optimized out, not going through the split job process,
// Equivalent to most functions of this class not being called
class ObLocalIdentityTaskSpliter : public ObTaskSpliter
{
public:
  ObLocalIdentityTaskSpliter();
  virtual ~ObLocalIdentityTaskSpliter();
  virtual int get_next_task(ObTaskInfo *&task);
  inline virtual TaskSplitType get_type() const { return ObTaskSpliter::LOCAL_IDENTITY_SPLIT; }
private:
  // disallow copy
  DISALLOW_COPY_AND_ASSIGN(ObLocalIdentityTaskSpliter);
private:
  ObTaskInfo *task_;
};
}
}
#endif /* OCEANBASE_SQL_EXECUTOR_OB_LOCAL_IDENTITY_TASK_SPLITER_ */
//// end of header file

