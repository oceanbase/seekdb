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

#ifndef OCEANBASE_SQL_EXECUTOR_REMOTE_TASK_EXECUTOR_
#define OCEANBASE_SQL_EXECUTOR_REMOTE_TASK_EXECUTOR_
#include "sql/executor/ob_task_executor.h"
#include "sql/executor/ob_executor_rpc_impl.h"
namespace oceanbase
{
namespace sql
{

class ObExecutorRpcImpl;
class ObExecContext;
class ObTaskInfo;
class ObTask;
class ObRemoteTaskExecutor : public ObTaskExecutor
{
public:
  ObRemoteTaskExecutor();
  virtual ~ObRemoteTaskExecutor();
  virtual int execute(ObExecContext &query_ctx, ObJob *job, ObTaskInfo *task_info);
  inline virtual void reset() { ObTaskExecutor::reset(); }
  static int handle_tx_after_rpc(ObScanner *scanner,
                                 ObSQLSessionInfo *session,
                                 const bool has_sent_task,
                                 const bool has_transfer_err,
                                 const ObPhysicalPlan *phy_plan,
                                 ObExecContext &exec_ctx);
private:
  int build_task(ObExecContext &query_ctx,
                 ObJob &job,
                 ObTaskInfo &task_info,
                 ObTask &task);
private:
  DISALLOW_COPY_AND_ASSIGN(ObRemoteTaskExecutor);
};
}
}
#endif /* OCEANBASE_SQL_EXECUTOR_REMOTE_TASK_EXECUTOR_ */
//// end of header file

