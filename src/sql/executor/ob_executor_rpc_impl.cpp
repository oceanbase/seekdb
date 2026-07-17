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


#include "ob_executor_rpc_impl.h"
using namespace oceanbase::common;
namespace oceanbase
{
namespace sql
{

int ObExecutorRpcImpl::init()
{
  return OB_SUCCESS;
}

// Remote SQL plan execution has been removed on single-replica seekdb.
// The optimizer never emits a remote plan (all data is local on self), so
// these paths are unreachable; they fail loudly if ever hit.
int ObExecutorRpcImpl::task_execute(ObExecutorRpcCtx &rpc_ctx,
                                    ObTask &task,
                                    const common::ObAddr &svr,
                                    RemoteExecuteStreamHandle &handler,
                                    bool &has_sent_task,
                                    bool &has_transfer_err)
{
  UNUSEDx(rpc_ctx, task);
  int ret = OB_NOT_SUPPORTED;
  has_sent_task = false;
  has_transfer_err = false;
  handler.set_result_code(ret);
  LOG_ERROR("remote task_execute is not supported on single replica", K(ret), K(svr));
  return ret;
}

int ObExecutorRpcImpl::task_execute_v2(ObExecutorRpcCtx &rpc_ctx,
                                       ObRemoteTask &task,
                                       const common::ObAddr &svr,
                                       RemoteExecuteStreamHandle &handler,
                                       bool &has_sent_task,
                                       bool &has_transfer_err)
{
  UNUSEDx(rpc_ctx, task);
  int ret = OB_NOT_SUPPORTED;
  has_sent_task = false;
  has_transfer_err = false;
  handler.set_result_code(ret);
  LOG_ERROR("remote task_execute_v2 is not supported on single replica", K(ret), K(svr));
  return ret;
}

int ObExecutorRpcImpl::task_kill(
    ObExecutorRpcCtx &rpc_ctx,
    const ObTaskID &task_id,
    const common::ObAddr &svr)
{
  UNUSEDx(rpc_ctx, task_id, svr);
  // Remote streaming never happens on single replica, so there is nothing to
  // kill. Treat as a no-op.
  return OB_SUCCESS;
}

}/* ns sql*/
}/* ns oceanbase */
