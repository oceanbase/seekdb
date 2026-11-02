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

#ifndef OB_PX_RPC_PROCESSOR_H
#define OB_PX_RPC_PROCESSOR_H

#include "share/interrupt/ob_global_interrupt_call.h"
#include "sql/engine/px/ob_dfo.h"
#include "sql/engine/ob_des_exec_context.h"
#include "sql/engine/ob_physical_plan.h"
#include "observer/ob_server_struct.h"


namespace oceanbase {
namespace sql {

class ObPxSqcHandler;

// In-process SQC launcher for the parallel (async) scheduler.
// Single-replica seekdb: the QC and SQC are always on the same node, so the
// old OB_PX_ASYNC_INIT_SQC rpc + ObInitSqcP processor are replaced by a direct
// in-process run. The QC serializes ObPxRpcInitSqcArgs into a buffer (exactly as
// the proxy would), then this class deserializes it into a fresh ObPxSqcHandler
// (its own ObDesExecContext / ObPhysicalPlan), registers the SQC interrupt, and
// spawns the SQC worker threads -- preserving the original processor semantics.
class ObInitSqcP
{
public:
  ObInitSqcP(const observer::ObGlobalContext &gctx)
    : exec_ctx_(CURRENT_CONTEXT->get_arena_allocator(), gctx.session_mgr_),
      phy_plan_(),
      unregister_interrupt_(false)
  {}
  ~ObInitSqcP() = default;
  int init();
  void destroy();
  int process();
  int after_process(int error_code);
  // in-process entry: decode the serialized args buffer into the sqc handler.
  int decode_arg(const char *buf, const int64_t len, int64_t &pos);
  ObPxRpcInitSqcResponse &get_result() { return result_; }
  ObPxRpcInitSqcArgs &get_arg() { return arg_; }
private:
  int pre_setup_op_input(ObPxSqcHandler &sqc_handler);
  int startup_normal_sqc(ObPxSqcHandler &sqc_handler);
private:
  ObPxRpcInitSqcArgs arg_;
  ObPxRpcInitSqcResponse result_;
  sql::ObDesExecContext exec_ctx_;
  sql::ObPhysicalPlan phy_plan_;
  bool unregister_interrupt_;
};


// In-process SQC launcher for the serial (fast) scheduler. Same rationale as
// ObInitSqcP; the fast path runs the single task inline on the calling thread.
class ObInitFastSqcP
{
public:
  ObInitFastSqcP(const observer::ObGlobalContext &gctx)
    : exec_ctx_(CURRENT_CONTEXT->get_arena_allocator(), gctx.session_mgr_),
      phy_plan_()
  {}
  ~ObInitFastSqcP() = default;
  int init();
  void destroy();
  int process();
  int decode_arg(const char *buf, const int64_t len, int64_t &pos);
  ObPxRpcInitSqcArgs &get_arg() { return arg_; }
private:
  int startup_normal_sqc(ObPxSqcHandler &sqc_handler);
private:
  ObPxRpcInitSqcArgs arg_;
  ObPxRpcInitSqcResponse result_;
  sql::ObDesExecContext exec_ctx_;
  sql::ObPhysicalPlan phy_plan_;
};


class ObFastInitSqcReportQCMessageCall
{
public:
  ObFastInitSqcReportQCMessageCall(ObPxSqcMeta *sqc,
      int err,
      int64_t timeout_ts,
      bool need_set_not_alive) : sqc_(sqc), err_(err),
      need_interrupt_(false), timeout_ts_(timeout_ts),
      need_set_not_alive_(need_set_not_alive)
  {
    need_interrupt_ = true;
  }
  ~ObFastInitSqcReportQCMessageCall() = default;
  void operator() (hash::HashMapPair<ObInterruptibleTaskID,
      ObInterruptCheckerNode *> &entry);
  int mock_sqc_finish_msg();
public:
  ObPxSqcMeta *sqc_;
  int err_;
  bool need_interrupt_;
  int64_t timeout_ts_;
  bool need_set_not_alive_;
};

// In-process SQC launch entry points (single-replica). The caller serializes
// ObPxRpcInitSqcArgs into buf (as the proxy did) and invokes these directly.
int px_init_sqc_async_in_proc(const char *buf, const int64_t len, ObPxRpcInitSqcResponse &resp);
int px_init_sqc_fast_in_proc(const char *buf, const int64_t len);

}  // sql
}  // oceanbase

#endif /* OB_PX_RPC_PROCESSOR_H */
