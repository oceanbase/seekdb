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

#ifndef OB_PX_LOCAL_SQC_LAUNCHER_H
#define OB_PX_LOCAL_SQC_LAUNCHER_H

#include "share/interrupt/ob_global_interrupt_call.h"
#include "share/rc/ob_server_runtime.h"
#include "sql/engine/px/ob_dfo.h"
#include "sql/engine/ob_des_exec_context.h"
#include "sql/engine/ob_physical_plan.h"
#include "share/ob_server_struct.h"


namespace oceanbase {
namespace sql {

class ObPxSqcHandler;

// In-process SQC launcher for the parallel (async) scheduler.
// QC and SQC execute in the same process. Each launch clones the execution
// state into a fresh handler and starts the SQC worker threads.
class ObLocalSqcLauncher
{
public:
  ObLocalSqcLauncher(const share::ObGlobalContext &gctx)
    : exec_ctx_(CURRENT_CONTEXT->get_arena_allocator(),
          share::server_service<ObSQLSessionMgr>()),
      phy_plan_(),
      unregister_interrupt_(false)
  {}
  ~ObLocalSqcLauncher() = default;
  int init(const ObExecContext::RuntimeServices &runtime_services);
  void destroy();
  int process();
  int after_process(int error_code);
  // in-process entry: decode the serialized args buffer into the sqc handler.
  int decode_arg(const char *buf, const int64_t len, int64_t &pos);
  ObPxInitSqcResponse &get_result() { return result_; }
  ObPxInitSqcArgs &get_arg() { return arg_; }
private:
  int pre_setup_op_input(ObPxSqcHandler &sqc_handler);
  int startup_normal_sqc(ObPxSqcHandler &sqc_handler);
private:
  ObPxInitSqcArgs arg_;
  ObPxInitSqcResponse result_;
  sql::ObDesExecContext exec_ctx_;
  sql::ObPhysicalPlan phy_plan_;
  bool unregister_interrupt_;
};


// The fast scheduler runs its single SQC task on the local launcher thread.
class ObLocalFastSqcLauncher
{
public:
  ObLocalFastSqcLauncher(const share::ObGlobalContext &gctx)
    : exec_ctx_(CURRENT_CONTEXT->get_arena_allocator(),
          share::server_service<ObSQLSessionMgr>()),
      phy_plan_()
  {}
  ~ObLocalFastSqcLauncher() = default;
  int init(const ObExecContext::RuntimeServices &runtime_services);
  void destroy();
  int process();
  int decode_arg(const char *buf, const int64_t len, int64_t &pos);
  ObPxInitSqcArgs &get_arg() { return arg_; }
private:
  int startup_normal_sqc(ObPxSqcHandler &sqc_handler);
private:
  ObPxInitSqcArgs arg_;
  ObPxInitSqcResponse result_;
  sql::ObDesExecContext exec_ctx_;
  sql::ObPhysicalPlan phy_plan_;
};


class ObLocalSqcFailureReporter
{
public:
  ObLocalSqcFailureReporter(ObPxSqcMeta *sqc,
      int err,
      int64_t timeout_ts) : sqc_(sqc), err_(err),
      need_interrupt_(false), timeout_ts_(timeout_ts)
  {
    need_interrupt_ = true;
  }
  ~ObLocalSqcFailureReporter() = default;
  void operator() (hash::HashMapPair<ObInterruptibleTaskID,
      ObInterruptCheckerNode *> &entry);
  int mock_sqc_finish_msg();
public:
  ObPxSqcMeta *sqc_;
  int err_;
  bool need_interrupt_;
  int64_t timeout_ts_;
};

// Local SQC launch entry points. The buffer is a private deep-clone boundary
// between QC and the independently owned SQC execution context.
int launch_sqc_async_local(
    const char *buf,
    const int64_t len,
    const ObExecContext::RuntimeServices &runtime_services,
    ObPxInitSqcResponse &resp);
int launch_sqc_fast_local(
    const char *buf,
    const int64_t len,
    const ObExecContext::RuntimeServices &runtime_services);

}  // sql
}  // oceanbase

#endif /* OB_PX_LOCAL_SQC_LAUNCHER_H */
