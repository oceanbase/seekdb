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
// ex_rpc in-process dispatch.
//  - sync_call: runs the op on the server runtime ReqWorker (real ObThWorker:
//    full check_status / interrupt / trace / big stack / per-op timeout) and waits,
//    exactly reproducing the former loopback sync RPC (recv_request to self + wait).
//  - async_call: fire-and-forget on the same runtime ReqWorker (no separate pool).
#include "share/ob_ex_rpc.h"
#include "observer/ob_srv_task.h"
#include "observer/omt/ob_server_runtime_controller.h"
#include "observer/ob_server_struct.h"
#include "lib/worker.h"
#include "lib/lock/ob_futex.h"
#include "lib/time/ob_clock_generator.h"
#include "lib/profile/ob_trace_id.h"

namespace oceanbase {
namespace ex_rpc {
using namespace oceanbase::common;
using namespace oceanbase::lib;

// ===================== sync_call -> runtime ReqWorker =====================

// Caller-owned completion context: created on the caller's stack, lives across the
// wait, so it survives the framework freeing the task after run().
struct SyncCtx {
  int ret_;
  int done_;   // futex word: 0 pending, 1 done
  SyncCtx() : ret_(OB_SUCCESS), done_(0) {}
};

class ExRpcProcessor : public rpc::frame::ObReqProcessor {
public:
  ExRpcProcessor() : timeout_ts_(INT64_MAX), sync_ctx_(nullptr) {}
  int run() override {
    // Propagate the caller trace id onto the dispatched worker (the former loopback
    // RPC carried it in the packet). Without this the worker keeps the null trace
    // Y0-0..0, which breaks anything derived from it -- e.g. the index-build PX dag
    // id, rejected as invalid (OB_INVALID_ERROR / -4055).
    ObCurTraceId::set(trace_id_);
    THIS_WORKER.set_timeout_ts(timeout_ts_);
    const int op_ret = fn_ ? fn_() : OB_SUCCESS;
    if (sync_ctx_ != nullptr) {
      sync_ctx_->ret_ = op_ret;
      ATOMIC_STORE(&sync_ctx_->done_, 1);
      futex_wake(reinterpret_cast<int *>(&sync_ctx_->done_), 1);
    }
    return OB_SUCCESS;
  }
  std::function<int()> fn_;
  int64_t timeout_ts_;
  SyncCtx *sync_ctx_;
  ObCurTraceId::TraceId trace_id_;
};

class ExRpcTask : public observer::ObSrvTask {
public:
  rpc::frame::ObReqProcessor &get_processor() override { return proc_; }
  ExRpcProcessor proc_;
};

static int dispatch_(int64_t timeout_us, std::function<int()> fn, SyncCtx *sync_ctx)
{
  int ret = OB_SUCCESS;
  
  
  ExRpcTask *task = OB_NEW(ExRpcTask, ObMemAttr("ExRpcTask"));
  if (OB_ISNULL(task)) {
    ret = OB_ALLOCATE_MEMORY_FAILED;
  } else {
    task->proc_.fn_ = std::move(fn);
    task->proc_.timeout_ts_ = ObClockGenerator::getClock() + timeout_us;
    task->proc_.sync_ctx_ = sync_ctx;
    task->proc_.trace_id_.set(*ObCurTraceId::get_trace_id());
    task->set_receive_timestamp(ObClockGenerator::getClock());
    task->proc_.set_ob_request(*task);
    if (OB_ISNULL(GCTX.server_runtime_controller_)) {
      ret = OB_ERR_UNEXPECTED;
      ob_delete(task);
    } else if (OB_FAIL(GCTX.server_runtime_controller_->recv_request(*task))) {
      ob_delete(task);
    }
  }
  return ret;
}

int sync_call_internal(int64_t timeout_us, std::function<int()> fn)
{
  SyncCtx ctx;
  int ret = dispatch_(timeout_us, std::move(fn), &ctx);
  if (OB_SUCC(ret)) {
    while (0 == ATOMIC_LOAD(&ctx.done_)) {
      futex_wait(reinterpret_cast<int *>(&ctx.done_), 0, nullptr);
    }
    ret = ctx.ret_;
  }
  return ret;
}

// ===================== async_call -> runtime ReqWorker =====================

// Fire-and-forget on the server runtime ReqWorker (real ObThWorker, empty session),
// the same dispatch path sync_call uses -- faithful to the former async RPC's
// receive-side execution context. No sync_ctx: the task carries its own completion
// signalling via the captured AsyncHandle.
int async_call_internal(std::function<void()> fn) {
    return dispatch_(EX_RPC_DEFAULT_TIMEOUT_US,
                     [f = std::move(fn)]() -> int { f(); return oceanbase::common::OB_SUCCESS; },
                     nullptr);
}

} // namespace ex_rpc
} // namespace oceanbase
