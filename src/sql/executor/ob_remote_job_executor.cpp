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

#define USING_LOG_PREFIX SQL_EXE

#include "sql/engine/ob_exec_context.h"
#include "sql/executor/ob_remote_job_executor.h"

namespace oceanbase
{
namespace sql
{

using namespace oceanbase::common;

ObRemoteJobExecutor::ObRemoteJobExecutor()
  : job_(NULL),
    executor_(NULL)
{
}

ObRemoteJobExecutor::~ObRemoteJobExecutor()
{
}

int ObRemoteJobExecutor::execute(ObExecContext &query_ctx)
{
  int ret = OB_SUCCESS;
  // ObTask is only used for serialization, so it only needs to be a stack variable
  ObTaskInfo *task_info = NULL;

  if (OB_ISNULL(job_) || OB_ISNULL(executor_)) {
    ret = OB_ERR_UNEXPECTED;
    LOG_WARN("job_ or executor_ is NULL", K(ret), K(job_), K(executor_));
  } else if (OB_FAIL(get_executable_task(query_ctx, task_info))) { // get a task info
    LOG_WARN("fail get a executable task", K(ret));
  } else if (OB_ISNULL(task_info)) {
    ret = OB_ERR_UNEXPECTED;
    LOG_WARN("task info is NULL", K(ret));
  } else if (OB_FAIL(executor_->execute(query_ctx,
                                        job_,
                                        task_info))) { // job_ + task_info as the skeleton and parameters of the task
    LOG_WARN("fail execute task", K(ret), K(*task_info));
  } else {}
  return ret;
}

/**
 * Task are all used to read single table physical data, therefore the task division rules are related to LocationCache
 * The obtained Locations are all saved in the Task structure
 *
 * At the same time, the Jobs executed by RemoteJobExecutor will only read data from a single partition, so TaskControl will still only output one Task
 * How to divide the Task is determined by the task_spliter in TaskControl
 */
int ObRemoteJobExecutor::get_executable_task(ObExecContext &ctx, ObTaskInfo *&task_info)
{
  int ret = OB_SUCCESS;
  ObTaskControl *tq = NULL;
  ObArray<ObTaskInfo *> ready_tasks;

  if (OB_ISNULL(job_) || OB_ISNULL(executor_)) {
    ret = OB_ERR_UNEXPECTED;
    LOG_WARN("job_ or executor_ is NULL", K(ret), K(job_), K(executor_));
  } else if (OB_FAIL(job_->get_task_control(ctx, tq))) {
    LOG_WARN("fail get task control", K(ret));
  } else if (OB_ISNULL(tq)) {
    ret = OB_ERR_UNEXPECTED;
    LOG_WARN("succ to get task control, but task control is NULL", K(ret));
  } else if (OB_FAIL(tq->get_ready_tasks(ready_tasks))) {
    LOG_WARN("fail get ready task", K(ret));
  } else if (OB_UNLIKELY(1 != ready_tasks.count())) {
    LOG_WARN("unexpected ready task count", "count", ready_tasks.count());
  } else if (OB_FAIL(ready_tasks.at(0, task_info))) {
    LOG_WARN("fail get task from array", K(ret));
  } else {}
  return ret;
}

} /* ns sql */
} /* ns oceanbase */
