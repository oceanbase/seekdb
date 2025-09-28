/**
 * Copyright (c) 2021 OceanBase
 * OceanBase CE is licensed under Mulan PubL v2.
 * You can use this software according to the terms and conditions of the Mulan PubL v2.
 * You may obtain a copy of Mulan PubL v2 at:
 *          http://license.coscl.org.cn/MulanPubL-2.0
 * THIS SOFTWARE IS PROVIDED ON AN "AS IS" BASIS, WITHOUT WARRANTIES OF ANY KIND,
 * EITHER EXPRESS OR IMPLIED, INCLUDING BUT NOT LIMITED TO NON-INFRINGEMENT,
 * MERCHANTABILITY OR FIT FOR A PARTICULAR PURPOSE.
 * See the Mulan PubL v2 for more details.
 */
#define USING_LOG_PREFIX RPC_OBRPC

#include "lib/oblog/ob_log_module.h"
#include "rpc/obrpc/ob_local_procedure_call.h"
#include "rpc/frame/ob_req_processor.h"
#include "rpc/obrpc/ob_rpc_proxy.h"


using namespace oceanbase::rpc::frame;
using namespace oceanbase::rpc;
namespace oceanbase
{
namespace oblpc
{

frame::ObReqDeliver* deliver;


int ObSyncLocalProcedureCallContext::handle_resp(ObRpcPacket *pkt, const char* buf, int64_t sz)
{
  resp_sz_ = sz;
  resp_ = buf;
  resp_pkt_ = pkt;
  ATOMIC_STORE(&cond_, 1);
  futex_wake(&cond_, 1);
  return OB_SUCCESS;
}

int ObSyncLocalProcedureCallContext::wait(const int64_t wait_timeout_us, const int64_t pcode, const int64_t req_sz)
{
  ObWaitEventGuard wait_guard(ObWaitEventIds::SYNC_RPC, wait_timeout_us / 1000, pcode, req_sz);
  int ret = OB_SUCCESS;
  const struct timespec ts = {1, 0};
  bool has_terminated = false;
  // TODO: detect_session_killed
  while(ATOMIC_LOAD(&cond_) == 0) {
    futex_wait(&cond_, 0, &ts);
  }
  return ret;
}

class ObLocalProcedureCallSPAlloc: public rpc::frame::SPAlloc
{
public:
  ObLocalProcedureCallSPAlloc(ObRpcMemPool& pool): pool_(pool) {}
  virtual ~ObLocalProcedureCallSPAlloc() {}
  void* alloc(int64_t sz) const {
    return pool_.alloc(sz);
  }
private:
  ObRpcMemPool& pool_;
};

ObReqTransport::AsyncCB* ObAsyncLocalProcedureCallContext::clone_async_cb(ObReqTransport::AsyncCB* ucb)
{
  ObLocalProcedureCallSPAlloc sp_alloc(ObLocalProcedureCallContext::pool_);
  if (OB_ISNULL(aync_cb_ = ucb->clone(sp_alloc))) {
    RPC_OBRPC_LOG_RET(WARN, OB_ALLOCATE_MEMORY_FAILED, "clone callback fail");
  } else {
    if (aync_cb_ != ucb) {
      aync_cb_->set_cloned(true);
    }
  }
  return aync_cb_;
}

int ObAsyncLocalProcedureCallContext::handle_resp(ObRpcPacket *pkt, const char* buf, int64_t sz)
{
  int ret = OB_SUCCESS;
  resp_pkt_ = pkt;
  if (OB_NOT_NULL(aync_cb_)) {
    bool cb_cloned = aync_cb_->get_cloned();
    if (OB_FAIL(aync_cb_->decode(pkt))) {
      aync_cb_->set_error(ret);
      aync_cb_->on_invalid();
      RPC_LOG(WARN, "ucb.decode fail", K(ret));
    } else {
      int tmp_ret = OB_SUCCESS;
      if (OB_SUCCESS != (tmp_ret = aync_cb_->process())) {
        RPC_LOG(WARN, "ucb.process fail", K(tmp_ret));
      }
    }
    if (cb_cloned) {
      aync_cb_->~AsyncCB();
    }
  }
  return ret;
}

void init_ucb(ObRpcProxy& proxy, UAsyncCB* ucb, int64_t send_ts, int64_t payload_sz)
{
  ucb->set_dst(ObRpcProxy::myaddr_);
  ucb->set_tenant_id(proxy.get_tenant());
  ucb->set_timeout(proxy.timeout());
  ucb->set_send_ts(send_ts);
  ucb->set_payload(payload_sz);
}

}; // end namespace oblpc
}; // end namespace oceanbase
