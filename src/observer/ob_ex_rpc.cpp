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
#include "share/rc/ob_server_runtime.h"
#include "observer/ob_srv_task.h"
#include "observer/omt/ob_server_runtime_controller.h"
#include "share/ob_server_struct.h"
#include "lib/worker.h"
#include "lib/lock/ob_futex.h"
#include "lib/time/ob_clock_generator.h"
#include "lib/profile/ob_trace_id.h"
#include <time.h>

namespace oceanbase {
namespace ex_rpc {
using namespace oceanbase::common;
using namespace oceanbase::lib;

// ===================== sync_call -> runtime ReqWorker =====================

// Completion context shared by the caller and the dispatched task. The caller
// blocks until done_, but a timeout may return early, and the callback captures
// caller stack references (see sync_call() contract). The task must therefore
// either never run the callback or already be running it when the caller leaves.
// The state machine makes that decision race-free:
//   PENDING -> RUNNING  (task starts the callback)
//   PENDING -> CANCELED (caller timed out before the task started)
// Once RUNNING, the caller keeps waiting past the deadline so the captured
// references stay alive.
struct SyncCtx {
  enum State { PENDING = 0, RUNNING = 1, CANCELED = 2 };
  std::atomic<int> state_;
  int ret_;
  int done_;   // futex word: 0 pending, 1 done
  SyncCtx() : state_(PENDING), ret_(OB_SUCCESS), done_(0) {}
};

class ExRpcProcessor : public rpc::frame::ObReqProcessor {
public:
  ExRpcProcessor() : timeout_ts_(INT64_MAX), sync_ctx_() {}
  int run() override {
    // Propagate the caller trace id onto the dispatched worker (the former loopback
    // RPC carried it in the packet). Without this the worker keeps the null trace
    // Y0-0..0, which breaks anything derived from it -- e.g. the index-build PX dag
    // id, rejected as invalid (OB_INVALID_ERROR / -4055).
    ObCurTraceId::set(trace_id_);
    THIS_WORKER.set_timeout_ts(timeout_ts_);
    int op_ret = OB_SUCCESS;
    if (sync_ctx_ != nullptr) {
      int expected = SyncCtx::PENDING;
      if (sync_ctx_->state_.compare_exchange_strong(expected, SyncCtx::RUNNING)) {
        op_ret = fn_ ? fn_() : OB_SUCCESS;
        sync_ctx_->ret_ = op_ret;
      }
      // A canceled context has no waiter left; the caller already returned and
      // no longer owns any captured reference.
      ATOMIC_STORE(&sync_ctx_->done_, 1);
      futex_wake(reinterpret_cast<int *>(&sync_ctx_->done_), 1);
    } else {
      op_ret = fn_ ? fn_() : OB_SUCCESS;
    }
    return OB_SUCCESS;
  }
  std::function<int()> fn_;
  int64_t timeout_ts_;
  std::shared_ptr<SyncCtx> sync_ctx_;
  ObCurTraceId::TraceId trace_id_;
};

class ExRpcTask : public rpc::ObSrvTask {
public:
  rpc::frame::ObReqProcessor &get_processor() override { return proc_; }
  ExRpcProcessor proc_;
};

static int dispatch_(int64_t timeout_us, std::function<int()> fn,
                     std::shared_ptr<SyncCtx> sync_ctx)
{
  int ret = OB_SUCCESS;
  
  
  ExRpcTask *task = OB_NEW(ExRpcTask, ObMemAttr("ExRpcTask"));
  if (OB_ISNULL(task)) {
    ret = OB_ALLOCATE_MEMORY_FAILED;
  } else {
    task->proc_.fn_ = std::move(fn);
    task->proc_.timeout_ts_ = ObClockGenerator::getClock() + timeout_us;
    task->proc_.sync_ctx_ = std::move(sync_ctx);
    task->proc_.trace_id_.set(*ObCurTraceId::get_trace_id());
    task->set_receive_timestamp(ObClockGenerator::getClock());
    task->proc_.set_ob_request(*task);
    if (OB_ISNULL(::oceanbase::share::server_service<::oceanbase::omt::ObServerRuntimeController>())) {
      ret = OB_ERR_UNEXPECTED;
      ob_delete(task);
    } else if (OB_FAIL(::oceanbase::share::server_service<::oceanbase::omt::ObServerRuntimeController>()->recv_request(*task))) {
      ob_delete(task);
    }
  }
  return ret;
}

// Wait for the dispatched callback with a bounded wait. On deadline the caller
// may leave only when the callback never started (PENDING -> CANCELED); if it is
// already RUNNING the caller must stay until it finishes because the callback
// captures caller references.
static int wait_sync_ctx(SyncCtx &ctx, const int64_t timeout_us)
{
  int ret = OB_SUCCESS;
  const int64_t deadline = ObClockGenerator::getClock() + timeout_us;
  while (0 == ATOMIC_LOAD(&ctx.done_)) {
    const int64_t now = ObClockGenerator::getClock();
    if (now < deadline) {
      const int64_t remaining_us = deadline - now;
      struct timespec rel_time;
      rel_time.tv_sec = static_cast<time_t>(remaining_us / 1000000);
      rel_time.tv_nsec = static_cast<long>((remaining_us % 1000000) * 1000);
      futex_wait(reinterpret_cast<int *>(&ctx.done_), 0, &rel_time);
    } else {
      int expected = SyncCtx::PENDING;
      if (ctx.state_.compare_exchange_strong(expected, SyncCtx::CANCELED)) {
        // The callback will never run; the task may still release its own
        // references, but it no longer touches anything the caller owns.
        ret = OB_TIMEOUT;
        break;
      }
      // Already running: keep waiting (unbounded) so captured stack references
      // stay valid even though the deadline passed.
      futex_wait(reinterpret_cast<int *>(&ctx.done_), 0, nullptr);
    }
  }
  if (OB_SUCC(ret)) {
    ret = ctx.ret_;
  }
  return ret;
}

int sync_call_internal(int64_t timeout_us, std::function<int()> fn)
{
  int ret = OB_SUCCESS;
  if (timeout_us <= 0) {
    timeout_us = EX_RPC_DEFAULT_TIMEOUT_US;
  }
  std::shared_ptr<SyncCtx> ctx = std::make_shared<SyncCtx>();
  // dispatch_() enqueues the callback through ObServerRuntime::recv_request,
  // whose foreground expansion runs before this thread blocks: when the queued
  // requests outnumber the idle workers it spawns a worker immediately
  // (high-priority ExRpcTask expands up to max_worker_cnt), so the callback has
  // an executor other than this caller. The wait below is bounded as a safety
  // net, not as the mechanism that guarantees progress.
  ret = dispatch_(timeout_us, std::move(fn), ctx);
  if (OB_SUCC(ret)) {
    ret = wait_sync_ctx(*ctx, timeout_us);
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
                     std::shared_ptr<SyncCtx>());
}

} // namespace ex_rpc
} // namespace oceanbase
