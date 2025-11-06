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

#ifndef OB_PX_SQC_ASYNC_PROXY_H_
#define OB_PX_SQC_ASYNC_PROXY_H_

#include "lib/allocator/ob_mod_define.h"
#include "lib/container/ob_array.h"
#include "lib/lock/ob_thread_cond.h"
#include "lib/profile/ob_trace_id.h"
#include "lib/utility/ob_print_utils.h"
#include "share/ob_define.h"
#include "sql/engine/ob_exec_context.h"
#include "sql/engine/ob_physical_plan.h"
#include "sql/engine/ob_physical_plan_ctx.h"
#include "sql/engine/px/ob_dfo.h"
#include "sql/engine/px/ob_px_rpc_proxy.h"
#include "sql/session/ob_sql_session_info.h"

namespace oceanbase {

using namespace common;

namespace sql {

class ObSqcAsyncCB
    : public obrpc::ObPxRpcProxy::AsyncCB<obrpc::OB_PX_ASYNC_INIT_SQC> {
public:
  ObSqcAsyncCB(ObThreadCond &cond, const ObCurTraceId::TraceId trace_id)
      : cond_(cond), trace_id_(trace_id) {
    reset();
  }
  ~ObSqcAsyncCB(){};
  virtual int process() override;
  virtual void on_invalid() override;
  virtual void on_timeout() override;
  const ObPxRpcInitSqcResponse &get_result() const { return result_; }
  virtual rpc::frame::ObReqTransport::AsyncCB *
  clone(const rpc::frame::SPAlloc &alloc) const override;
  void set_args(const AsyncCB::Request &arg) override { UNUSED(arg); }
  void reset() {
    is_processed_ = false;
    is_timeout_ = false;
    is_invalid_ = false;
    is_visited_ = false;
  }
  void set_visited(bool value) { is_visited_ = value; }
  bool is_visited() const { return is_visited_; }
  bool is_timeout() const { return is_timeout_; }
  void set_invalid(bool value) { is_invalid_ = value; }
  bool is_invalid() const { return is_invalid_; }
  bool is_processed() const { return is_processed_; }
  const ObPxUserErrorMsg get_ret_code() const { return rcode_; }
  const common::ObAddr &get_dst() const { return dst_; }
  int64_t get_timeout() const { return timeout_; }
  ObThreadCond &get_cond() { return cond_; }
  // to string
  TO_STRING_KV("dst", get_dst(), "timeout", get_timeout(), "ret_code",
               get_ret_code(), "result", get_result(), "is_visited",
               is_visited(), "is_timeout", is_timeout(), "is_processed",
               is_processed(), "is_invalid", is_invalid());

private:
  // rpc returns resp, rpc asynchronous thread calling the callback's process method needs to be set to true; otherwise it should be false;
  bool is_processed_;
  // rpc call timeout, rpc asynchronous thread calling callback's on_timeout method needs to be set to true; otherwise false;
  bool is_timeout_;
  // rpc returns message package, message package decode failed, rpc asynchronous thread calls callback's on_invalid method needs to be set to true; otherwise false;
  bool is_invalid_;
  // Identify whether the callback object is accessed by the main thread, it should be set to true when first accessed by the main thread; otherwise, it should be false;
  bool is_visited_;
  ObThreadCond &cond_;
  ObCurTraceId::TraceId trace_id_;
};

class ObPxSqcAsyncProxy {
public:
  ObPxSqcAsyncProxy(obrpc::ObPxRpcProxy &proxy, ObDfo &dfo,
                    ObExecContext &exec_ctx, ObPhysicalPlanCtx *phy_plan_ctx,
                    ObSQLSessionInfo *session, const ObPhysicalPlan *phy_plan,
                    ObIArray<ObPxSqcMeta> &sqcs)
      : proxy_(proxy), dfo_(dfo), exec_ctx_(exec_ctx),
        phy_plan_ctx_(phy_plan_ctx), session_(session), phy_plan_(phy_plan),
        sqcs_(sqcs), allocator_(ObModIds::OB_SQL_PX_ASYNC_SQC_RPC),
        return_cb_count_(0), error_index_(0) {
    cond_.init(common::ObWaitEventIds::DEFAULT_COND_WAIT);
  }

  ~ObPxSqcAsyncProxy() { destroy(); }
  // Asynchronous request all sqc rpc tasks
  int launch_all_rpc_request();
  // Synchronous wait for all asynchronous sqc rpc tasks to return results; internal errors that can be handled will be retried.
  int wait_all();

  const ObArray<ObSqcAsyncCB *> &get_callbacks() const { return callbacks_; }

  int get_error_index() const { return error_index_; }

private:
  void destroy();
  // Asynchronous request for a single sqc rpc task
  int launch_one_rpc_request(ObPxRpcInitSqcArgs &args, int64_t idx, ObSqcAsyncCB *cb);
  // After an internal error occurs, process the callback that has not yet received a response
  void fail_process();

private:
  // rpc proxy, provide rpc synchronous/asynchronous call methods
  obrpc::ObPxRpcProxy &proxy_;
  ObDfo &dfo_;
  ObExecContext &exec_ctx_;
  ObPhysicalPlanCtx *phy_plan_ctx_;
  ObSQLSessionInfo *session_;
  const ObPhysicalPlan *phy_plan_;
  // The collection of sqc that requires asynchronous requests
  ObIArray<ObPxSqcMeta> &sqcs_;
  // asynchronous init
  // sqc obtains the result set, every response is from the corresponding
  // callback obtain the result from the correct response
  ObArray<const ObPxRpcInitSqcResponse *> results_;
  // Asynchronous request corresponding callback
  ObArray<ObSqcAsyncCB *> callbacks_;
  ObArenaAllocator allocator_;
  // Get the number of callback for response result
  int64_t return_cb_count_;
  // The index of the first async sqc request that encountered an error
  int64_t error_index_;
  ObThreadCond cond_;
};
} // namespace sql
} // namespace oceanbase

#endif
