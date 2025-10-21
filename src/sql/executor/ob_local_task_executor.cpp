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

#include "sql/executor/ob_local_task_executor.h"
#include "sql/engine/ob_exec_context.h"

using namespace oceanbase::common;
using namespace oceanbase::sql;

ObLocalTaskExecutor::ObLocalTaskExecutor()
{
}

ObLocalTaskExecutor::~ObLocalTaskExecutor()
{
}
// Execute Task, responsible for controlling the execution flow of Task, reporting/outputting results to the client through ObTransmit.
// execute internally handles retry logic management, does not expose the retry() interface
int ObLocalTaskExecutor::execute(ObExecContext &ctx, ObJob *job, ObTaskInfo *task_info)
{
  UNUSED(job);
  int ret = OB_SUCCESS;
  ObOpSpec *root_spec_ = NULL; // for static engine
  ObTaskExecutorCtx &task_exec_ctx = ctx.get_task_exec_ctx();
  ObExecuteResult &exec_result = task_exec_ctx.get_execute_result();

  if (OB_ISNULL(task_info)) {
    ret = OB_NOT_INIT;
    LOG_WARN("job or taskinfo not set", K(task_info));
  } else {
    if (OB_ISNULL(root_spec_ = task_info->get_root_spec())) {
      ret = OB_NOT_INIT;
      LOG_WARN("fail execute task. no query found.", K(ret));
    } else if (OB_FAIL(build_task_op_input(ctx, *task_info, *root_spec_))) {
      LOG_WARN("fail to build op input", K(ret));
    } else {
      LOG_DEBUG("static engine remote execute");
      ObOperator *op = NULL;
      if (OB_FAIL(root_spec_->create_operator(ctx, op))) {
        LOG_WARN("create operator from spec failed", K(ret));
      } else if (OB_ISNULL(op)) {
        ret = OB_ERR_UNEXPECTED;
        LOG_WARN("created operator is NULL", K(ret));
      } else {
        exec_result.set_static_engine_root(op);
      }
      // Set task_info to OB_TASK_STATE_RUNNING state,
      // If a retry is needed later, it will use this status to retrieve the partition information from this task_info
      // Add to the partition that needs retry
      task_info->set_state(OB_TASK_STATE_RUNNING);
    }

    if (OB_FAIL(ret)) {
      // If failed, then set task_info to OB_TASK_STATE_FAILED state,
      // This way if a retry is needed later, it will use this status to retrieve the partition information from this task_info
      // Add to the partition that needs retry
      task_info->set_state(OB_TASK_STATE_FAILED);
    }
  }
  NG_TRACE_EXT(local_task_completed, OB_ID(ret), ret);
  return ret;
}
