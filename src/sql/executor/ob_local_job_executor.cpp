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
#include "sql/executor/ob_local_job_executor.h"
namespace oceanbase
{
namespace sql
{

using namespace oceanbase::common;

ObLocalJobExecutor::ObLocalJobExecutor()
  : job_(NULL),
    executor_(NULL)
{
}

ObLocalJobExecutor::~ObLocalJobExecutor()
{
}

int ObLocalJobExecutor::execute(ObExecContext &query_ctx)
{
  int ret = OB_SUCCESS;
  ObTaskInfo *task_info = NULL;

  if (OB_I(t1) (OB_ISNULL(job_) || OB_ISNULL(executor_))) {
    ret = OB_ERR_UNEXPECTED;
    LOG_WARN("job_ or executor_ is NULL", K(ret), K(job_), K(executor_));
  } else if (OB_FAIL(OB_I(t2) get_executable_task(query_ctx, task_info))) { // obtain a task
    LOG_WARN("fail get a executable task.", K(ret));
  } else if (OB_ISNULL(task_info)) {
    ret = OB_ERR_UNEXPECTED;
    LOG_WARN("task info is NULL", K(ret));
  } else if (OB_FAIL(OB_I(t3) executor_->execute(query_ctx, job_, task_info))) {
    LOG_WARN("fail execute task.", K(ret), K(*task_info));
  } else {}
  return ret;
}

int ObLocalJobExecutor::get_executable_task(ObExecContext &ctx, ObTaskInfo *&task_info)
{
  int ret = OB_SUCCESS;
  ObTaskControl *tq = NULL;
  ObSEArray<ObTaskInfo *, 1> ready_tasks;

  if (OB_I(t1) (OB_ISNULL(job_) || OB_ISNULL(executor_))) {
    ret = OB_ERR_UNEXPECTED;
    LOG_WARN("job_ or executor_ is NULL", K(ret), K(job_), K(executor_));
  } else if (OB_FAIL(OB_I(t1) job_->get_task_control(ctx, tq))) {
    LOG_WARN("fail get task control.", K(ret));
  } else if (OB_ISNULL(tq)) {
    ret = OB_ERR_UNEXPECTED;
    LOG_WARN("succ to get task control, but task control is NULL", K(ret));
  } else if (OB_FAIL(OB_I(t2) tq->get_ready_tasks(ready_tasks))) {
    LOG_WARN("fail get ready task.", K(ret));
  } else if (OB_I(t3) OB_UNLIKELY(1 != ready_tasks.count())) {
    ret = OB_ERR_UNEXPECTED;
    LOG_WARN("unexpected ready task count", "ready_tasks_count", ready_tasks.count());
  } else if (OB_FAIL(OB_I(t4) ready_tasks.at(0, task_info))) {
    LOG_WARN("fail get task from array", K(ret));
  } else {}
  return ret;
}

} /* ns sql */
} /* ns oceanbase */
