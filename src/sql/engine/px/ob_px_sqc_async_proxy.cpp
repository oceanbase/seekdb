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

#define USING_LOG_PREFIX SQL_ENG

#include "sql/engine/px/ob_px_sqc_async_proxy.h"
#include "sql/engine/px/ob_px_util.h"

namespace oceanbase {
using namespace common;
namespace sql {
/* ObSqcAsyncCB */
int ObSqcAsyncCB::process() {
  ObThreadCondGuard guard(cond_);
  int ret = OB_SUCCESS;
  is_processed_ = true;
  ret = cond_.broadcast();
  return ret;
}

void ObSqcAsyncCB::on_invalid() {
  ObThreadCondGuard guard(cond_);
  int ret = OB_SUCCESS;
  is_invalid_ = true;
  ret = cond_.broadcast();
  LOG_WARN("ObSqcAsyncCB invalid, check object serialization impl or oom",
           K(trace_id_), K(ret));
}

void ObSqcAsyncCB::on_timeout() {
  ObThreadCondGuard guard(cond_);
  int ret = OB_SUCCESS;
  is_timeout_ = true;
  ret = cond_.broadcast();
  LOG_WARN("ObSqcAsyncCB timeout, check timeout value, peer cpu load, network "
           "packet drop rate",
           K(trace_id_), K(ret));
}

rpc::frame::ObReqTransport::AsyncCB *
ObSqcAsyncCB::clone(const rpc::frame::SPAlloc &alloc) const {
  UNUSED(alloc);
  return const_cast<rpc::frame::ObReqTransport::AsyncCB *>(
      static_cast<const rpc::frame::ObReqTransport::AsyncCB *const>(this));
}

/* ObPxSqcAsyncProxy */
int ObPxSqcAsyncProxy::launch_all_rpc_request() {
  int ret = OB_SUCCESS;
  // prepare allocate the results_ array
  if (OB_FAIL(results_.prepare_allocate(sqcs_.count()))) {
    LOG_WARN("fail to prepare allocate result array");
  }

  if (OB_SUCC(ret)) {
    int64_t cluster_id = GCONF.cluster_id;
    SMART_VAR(ObPxRpcInitSqcArgs, args) {
      if (sqcs_.count() > 1) {
        args.enable_serialize_cache();
      }
      ARRAY_FOREACH_X(sqcs_, idx, count, OB_SUCC(ret)) {
        if (OB_UNLIKELY(ObPxCheckAlive::is_in_blacklist(sqcs_.at(idx).get_exec_addr(),
                        session_->get_process_query_time()))) {
          ret = OB_RPC_CONNECT_ERROR;
          LOG_WARN("peer no in communication, maybe crashed", K(ret),
                  K(sqcs_.at(idx)), K(cluster_id), K(session_->get_process_query_time()));
        } else {
          ret = launch_one_rpc_request(args, idx, NULL);
        }
      }
    }
  }
  if (OB_FAIL(ret)) {
    LOG_WARN("fail to launch all sqc rpc request", K(ret));
    fail_process();
  }
  return ret;
}

int ObPxSqcAsyncProxy::launch_one_rpc_request(ObPxRpcInitSqcArgs &args, int64_t idx, ObSqcAsyncCB *cb) {
  int ret = OB_SUCCESS;
  ObCurTraceId::TraceId *trace_id = NULL;
  ObPxSqcMeta &sqc = sqcs_.at(idx);
  const ObAddr &addr = sqc.get_exec_addr();
  int64_t timeout_us =
      phy_plan_ctx_->get_timeout_timestamp() - ObTimeUtility::current_time();
  args.set_serialize_param(exec_ctx_, const_cast<ObOpSpec &>(*dfo_.get_root_op_spec()), *phy_plan_);
  if (OB_FAIL(ret)) {
  } else if (timeout_us < 0) {
    ret = OB_TIMEOUT;
  } else if (OB_FAIL(args.sqc_.assign(sqc))) {
    LOG_WARN("fail assign sqc", K(ret));
  } else if (OB_ISNULL(trace_id = ObCurTraceId::get_trace_id())) {
    ret = OB_ERR_UNEXPECTED;
    LOG_WARN("fail to get trace id");
  } else {
    // allocate SqcAsync callback
    if (cb == NULL) {
      void *mem = NULL;
      if (NULL == (mem = allocator_.alloc(sizeof(ObSqcAsyncCB)))) {
        ret = OB_ALLOCATE_MEMORY_FAILED;
        LOG_WARN("alloc memory failed", "size", sizeof(ObSqcAsyncCB), K(ret));
      } else {
        cb = new (mem) ObSqcAsyncCB(cond_, *trace_id);
        if (OB_FAIL(callbacks_.push_back(cb))) {
          // free the callback
          LOG_WARN("callback obarray push back failed.");
          cb->~ObSqcAsyncCB();
          allocator_.free(cb);
          cb = NULL;
        }
      }
    }
    if (cb != NULL) {
      if (OB_SUCC(ret)) {
        if (OB_FAIL(proxy_.to(addr)
                        .by(THIS_WORKER.get_rpc_tenant()?: session_->get_effective_tenant_id())
                        .timeout(timeout_us)
                        .async_init_sqc(args, cb))) {
          LOG_WARN("fail to call asynchronous sqc rpc", K(sqc), K(timeout_us),
                   K(ret));
          // error_index_ = idx;
        } else {
          LOG_DEBUG("send the sqc request successfully.", K(idx), K(sqc),
                    K(args), K(cb));
        }
      }
      // ret is TIME_OUT, or when the asynchronous rpc is resent and fails, the corresponding callback needs to be recycled
      // If removing the corresponding callback fails, the callback cannot be destructed
      if (OB_FAIL(ret) && cb != NULL) {
        // The reason for using temp_ret is to retain the original ret error code
        int temp_ret = callbacks_.remove(idx);
        if (temp_ret != OB_SUCCESS) {
          // Here we need to mark callback as invalid, waiting for `fail_process` to handle
          cb->set_invalid(true);
          LOG_WARN("callback obarray remove element failed", K(ret));
        } else {
          cb->~ObSqcAsyncCB();
          allocator_.free(cb);
          cb = NULL;
        }
      }
    }
  }
  return ret;
}

int ObPxSqcAsyncProxy::wait_all() {
  int ret = OB_SUCCESS;
  LOG_TRACE("wail all async sqc rpc to end", K(dfo_));
  // Exit the while loop condition: exit the while loop if any of the 3 conditions are met
  // 1. Obtain enough and correct callback results within the valid time frame
  // 2. Timeout, ret = OB_TIMEOUT
  // 3. retry an rpc failure
  oceanbase::lib::Thread::WaitGuard guard(oceanbase::lib::Thread::WAIT_FOR_PX_MSG);
  while (return_cb_count_ < sqcs_.count() && OB_SUCC(ret)) {

    ObThreadCondGuard guard(cond_);
    // wait for timeout or until notified.
    cond_.wait_us(500);

    if (OB_FAIL(exec_ctx_.fast_check_status())) {
      LOG_WARN("check status failed", K(ret));
    }

    ARRAY_FOREACH_X(callbacks_, idx, count, OB_SUCC(ret)) {
      ObSqcAsyncCB &callback = *callbacks_.at(idx);
      if (!callback.is_visited() && callback.is_timeout()) {
        // callback timeout, no need to retry
        // It might just be an RPC timeout, but not a QUERY timeout, implementation-wise they need to be distinguished
        // This situation needs to be marked as RPC CONNECT ERROR for retry
        return_cb_count_++;
        if (phy_plan_ctx_->get_timeout_timestamp() -
          ObTimeUtility::current_time() > 0) {
          error_index_ = idx;
          ret = OB_RPC_CONNECT_ERROR;
        } else {
          ret = OB_TIMEOUT;
        }
        callback.set_visited(true);
      } else if (!callback.is_visited() && callback.is_invalid()) {
        // rpc parsing pack failed, callback calls on_invalid method, no need to retry
        return_cb_count_++;
        ret = callback.get_error() == OB_ALLOCATE_MEMORY_FAILED ?
              OB_ALLOCATE_MEMORY_FAILED : OB_RPC_PACKET_INVALID;
        LOG_WARN("callback invalid", K(ret), K(callback.get_error()));
        callback.set_visited(true);
      } else if (!callback.is_visited() && callback.is_processed()) {
        return_cb_count_++;
        callback.set_visited(true);
        if (OB_SUCC(callback.get_ret_code().rcode_)) {
          const ObPxRpcInitSqcResponse &cb_result = callback.get_result();
          if (cb_result.rc_ == OB_ERR_INSUFFICIENT_PX_WORKER) {
            // No sufficient px worker obtained, no need to retry internal SQC to prevent deadlock
            // SQC if it does not obtain enough workers, the outer layer directly performs query-level retry
            // 
            LOG_INFO("can't get enough worker resource, and not retry",
                K(cb_result.rc_), K(sqcs_.at(idx)));
          }
          if (OB_FAIL(cb_result.rc_)) {
            // Error may contain is_data_not_readable_err or other types of errors
            if (is_data_not_readable_err(ret)) {
              error_index_ = idx;
            }
          } else {
            // Obtain the correct return result
            results_.at(idx) = &cb_result;
          }
        } else {
          // RPC framework error, directly return the corresponding error code, current SQC does not need to retry again
          ret = callback.get_ret_code().rcode_;
          LOG_WARN("call rpc failed", K(ret), K(callback.get_ret_code()));
        }
      }
    }
  }
  // wait_all result:
  // 1. sqc corresponds to all callbacks returning the correct result, return_cb_count_=sqcs_.count(), directly return OB_SUCCESS;
  // 2. Due to timeout or retry sqc rpc failure, in this case, we need to wait for all callback responses to end before returning ret.
  if (return_cb_count_ < callbacks_.count()) {
    // There are still unprocessed callbacks, need to wait for all callbacks to respond before exiting the `wait_all` method
    fail_process();
  }
  return ret;
}

void ObPxSqcAsyncProxy::destroy() {
  int ret = OB_SUCCESS;
  LOG_DEBUG("async sqc proxy deconstruct, the callbacklist is ", K(callbacks_));
  ARRAY_FOREACH(callbacks_, idx) {
    ObSqcAsyncCB *callback = callbacks_.at(idx);
    LOG_DEBUG("async sqc proxy deconstruct, the callback status is ", K(idx), K(*callback));
    callback->~ObSqcAsyncCB();
  }
  allocator_.reuse();
  callbacks_.reuse();
  results_.reuse();
}

void ObPxSqcAsyncProxy::fail_process() {
  LOG_WARN_RET(OB_SUCCESS,
      "async sqc fails, process the callbacks that have not yet got results",
      K(return_cb_count_), K(callbacks_.count()));
  ARRAY_FOREACH_X(callbacks_, idx, count, true) {
    ObSqcAsyncCB &callback = *callbacks_.at(idx);
    if (!callback.is_visited() &&
        !(callback.is_processed() || callback.is_timeout() || callback.is_invalid())) {
      // unregister async callbacks that have not received response.
      uint64_t gtid = callback.gtid_;
      uint32_t pkt_id = callback.pkt_id_;
      int err = 0;
      if ((err = pn_terminate_pkt(gtid, pkt_id)) != 0) {
        int ret = tranlate_to_ob_error(err);
        LOG_WARN("terminate pkt failed", K(ret), K(err));
      }
    }
  }
  while (return_cb_count_ < callbacks_.count()) {
    ObThreadCondGuard guard(cond_);
    ARRAY_FOREACH_X(callbacks_, idx, count, true) {
      ObSqcAsyncCB &callback = *callbacks_.at(idx);
      if (!callback.is_visited()) {
        if (callback.is_processed() || callback.is_timeout() || callback.is_invalid()) {
          return_cb_count_++;
          LOG_DEBUG("async sql fails, wait all callbacks", K(return_cb_count_),
              K(callbacks_.count()));
          callback.set_visited(true);
        }
      }
    }
    cond_.wait_us(500);
  }
  LOG_WARN_RET(OB_SUCCESS, "async sqc fails, all callbacks have been processed");
}

} // namespace sql
} // namespace oceanbase
