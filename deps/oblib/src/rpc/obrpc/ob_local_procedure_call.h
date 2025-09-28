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
#ifndef OCEANBASE_LOCAL_PROCEDURE_CALL_H_
#define OCEANBASE_LOCAL_PROCEDURE_CALL_H_
#include "rpc/ob_request.h"
#include "rpc/obrpc/ob_poc_rpc_server.h"
#include "rpc/obrpc/ob_poc_rpc_proxy.h"
#include "lib/oblog/ob_log_module.h"
#include "rpc/frame/ob_req_processor.h"
#include "rpc/frame/ob_req_transport.h"
#include "rpc/obrpc/ob_rpc_result_code.h"

using namespace oceanbase::obrpc;
using namespace oceanbase::rpc;
using namespace oceanbase::rpc::frame;
namespace oceanbase
{
namespace obrpc
{
class ObRpcProxy;
extern int init_packet(ObRpcProxy& proxy, ObRpcPacket& pkt, ObRpcPacketCode pcode, const ObRpcOpts &opts,
                const bool unneed_response);
extern int64_t calc_extra_payload_size();
extern int fill_extra_payload(ObRpcPacket& pkt, char* buf, int64_t len, int64_t &pos);

}
namespace oblpc
{
extern rpc::frame::ObReqTranslator* translator;
extern rpc::frame::ObReqDeliver* deliver;
void init_ucb(ObRpcProxy& proxy, UAsyncCB* ucb, int64_t send_ts, int64_t payload_sz);
class ObLocalProcedureCallContext
{
public:
  ObLocalProcedureCallContext(ObRpcMemPool& pool)
    :pool_(pool), resp_pkt_(NULL) {}
  ~ObLocalProcedureCallContext() {}
  void* alloc(int64_t sz) { return pool_.alloc(sz); }
  virtual int handle_resp(ObRpcPacket *pkt, const char* buf, int64_t sz) = 0;
  TO_STRING_KV(KP(resp_pkt_));
protected:
  ObRpcMemPool& pool_;
public:
  ObRpcPacket* resp_pkt_; // recored response packet when processor responsing
};


class ObSyncLocalProcedureCallContext : public ObLocalProcedureCallContext
{
public:
  ObSyncLocalProcedureCallContext(ObRpcMemPool& pool)
    :ObLocalProcedureCallContext(pool), resp_(NULL), resp_sz_(0), cond_(0) {}
  ~ObSyncLocalProcedureCallContext() {}
  int wait(const int64_t wait_timeout_us, const int64_t pcode, const int64_t req_sz);
  const char* get_resp(int64_t& sz) {
    sz = resp_sz_;
    return resp_;
  }
  virtual int handle_resp(ObRpcPacket *pkt, const char* buf, int64_t sz);
  TO_STRING_KV(KP(resp_), K(resp_sz_), K(cond_), KP(resp_pkt_));
public:
  const char* resp_;
  int64_t resp_sz_;
  int cond_;
};

class ObAsyncLocalProcedureCallContext : public ObLocalProcedureCallContext
{
public:
  ObAsyncLocalProcedureCallContext(ObRpcMemPool& pool)
    :ObLocalProcedureCallContext(pool), aync_cb_(NULL) {}
  ~ObAsyncLocalProcedureCallContext() {}
  ObReqTransport::AsyncCB* clone_async_cb(ObReqTransport::AsyncCB* ucb);
  virtual int handle_resp(ObRpcPacket *pkt, const char* buf, int64_t sz);
  void destroy() {
    ObLocalProcedureCallContext::pool_.destroy();
  }
  TO_STRING_KV(KP(aync_cb_), KP(resp_pkt_));
private:
  ObReqTransport::AsyncCB* aync_cb_;
};


template<typename Input, typename Output>
int send(ObRpcProxy& proxy, ObRpcPacketCode pcode, const Input& args, Output& out, Handle* handle, const ObRpcOpts& opts)
{
  int ret = OB_SUCCESS;
  const int64_t start_ts = common::ObTimeUtility::current_time();
  int64_t args_len = common::serialization::encoded_length(args);
  int64_t extra_payload_size = calc_extra_payload_size();
  int64_t payload_sz = extra_payload_size + args_len;

  auto &set = obrpc::ObRpcPacketSet::instance();
  const char* pcode_label = set.name_of_idx(set.idx_of_pcode(pcode));
  ObRpcMemPool pool(OB_SERVER_TENANT_ID, pcode_label);
  ObSyncLocalProcedureCallContext ctx(pool);

  // allocate buffer for rpc packet and rpc request
  int64_t pos = 0;
  char *buf = reinterpret_cast<char *>(pool.alloc(sizeof(ObRequest) + sizeof(ObRpcPacket) + args_len + extra_payload_size));
  char *payload_buf = buf + sizeof(ObRequest) + sizeof(ObRpcPacket);
  if (OB_ISNULL(buf)) {
    ret = OB_ALLOCATE_MEMORY_FAILED;
    RPC_OBRPC_LOG(WARN, "alloc buffer fail", K(args_len));
  } else if (OB_FAIL(common::serialization::encode(payload_buf, args_len, pos, args))) {
    RPC_OBRPC_LOG(WARN, "serialize argument fail", K(pos), K(args_len), K(ret));
  } else if (OB_UNLIKELY(args_len < pos)) {
  #ifdef ENABLE_SERIALIZATION_CHECK
      lib::begin_check_serialization();
      common::serialization::encoded_length(args);
      lib::finish_check_serialization();
  #endif
      ret = OB_ERR_UNEXPECTED;
      RPC_OBRPC_LOG(ERROR, "arg encoded length greater than arg length", K(ret), K(payload_sz),
                    K(args_len), K(extra_payload_size), K(pos), K(pcode));
  } else {
    ObRequest *req = new (buf) ObRequest(ObRequest::OB_RPC, ObRequest::TRANSPORT_PROTO_LOCAL_SYNC);
    ObRpcPacket *pkt = new (buf + sizeof(ObRequest)) ObRpcPacket();
    if (OB_FAIL(fill_extra_payload(*pkt, payload_buf, payload_sz, pos))) {
      RPC_OBRPC_LOG(WARN, "fill extra payload fail", K(ret), K(pos), K(payload_sz), K(args_len),
                    K(extra_payload_size), K(pcode));
    } else {
      int64_t header_pos = 0;
      pkt->set_content(payload_buf, payload_sz);
      if (OB_FAIL(init_packet(proxy, *pkt, pcode, opts, false /* unneed_resp */))) {
        RPC_OBRPC_LOG(WARN, "init packet fail", K(ret));
      } else {
        int64_t receive_ts = ObTimeUtility::current_time();
        pkt->set_receive_ts(receive_ts);
        req->set_packet(pkt);
        req->set_request_arrival_time(pkt->get_receive_ts());
        req->set_arrival_push_diff(common::ObTimeUtility::current_time());
        req->set_receive_timestamp(pkt->get_receive_ts());
        req->set_server_handle_context(static_cast<ObLocalProcedureCallContext*>(&ctx));
        // deliver rpc request
        if (OB_FAIL(deliver->deliver(*req))) {
          RPC_OBRPC_LOG(WARN, "deliver rpc request fail", K(ret));
        }
      }
    }
  }

  // wait for cond wake up and decode result
  if (OB_SUCC(ret)) {
    ObRpcResultCode rcode;
    int64_t decode_pos = 0;
    int64_t timeout = ObPocClientStub::get_proxy_timeout(proxy);
    if (OB_FAIL(ctx.wait(timeout, pcode, payload_sz))) {
      RPC_OBRPC_LOG(WARN, "wait for rpc response timeout, it should not actually reach here.", K(ret));
    } else if (OB_FAIL(rcode.deserialize(ctx.resp_, ctx.resp_sz_, decode_pos))) {
      RPC_LOG(WARN, "deserialize result code fail", K(ctx));
    } else if (rcode.rcode_ != common::OB_SUCCESS) {
      ret = rcode.rcode_;
      RPC_LOG(WARN, "execute rpc fail", K(pcode), K(ret));
    } else if (OB_FAIL(common::serialization::decode(ctx.resp_, ctx.resp_sz_, decode_pos, out))) {
      RPC_LOG(WARN, "deserialize result fail", K(ctx));
    }
    int wb_ret = OB_SUCCESS;
    if (common::OB_SUCCESS != (wb_ret = ObPocClientStub::log_user_error_and_warn(rcode))) {
      RPC_OBRPC_LOG(WARN, "fail to log user error and warn", K(ret), K(wb_ret), K((rcode)));
    }
    ObPocClientStub::set_rcode(proxy, rcode);
    if (OB_SUCC(ret) && handle) {
      if (OB_ISNULL(ctx.resp_pkt_)) {
        ret = OB_ERR_UNEXPECTED;
        RPC_OBRPC_LOG(ERROR, "resp pkt is NULL", K(ret));
      } else {
        int64_t pkt_id = 0;// no need to stream rpc keepalive
        ObPocClientStub::set_handle(proxy, handle, pcode, opts,
                                    ctx.resp_pkt_->is_stream_next(),
                                    ctx.resp_pkt_->get_session_id(),
                                    pkt_id,
                                    start_ts);
      }
    }

  }
  return ret;
}

template<typename Input, typename UCB>
int post(ObRpcProxy& proxy, ObRpcPacketCode pcode, const Input& args, UCB* ucb, const ObRpcOpts& opts)
{
  int ret = OB_SUCCESS;
  const int64_t start_ts = common::ObTimeUtility::current_time();
  ObRpcMemPool* pool = NULL;
  ObAsyncLocalProcedureCallContext* ctx = NULL;
  auto &set = obrpc::ObRpcPacketSet::instance();
  const char* pcode_label = set.name_of_idx(set.idx_of_pcode(pcode));
  if (OB_ISNULL(pool = ObRpcMemPool::create(OB_SERVER_TENANT_ID, pcode_label, 0))) {
    ret = OB_ALLOCATE_MEMORY_FAILED;
    RPC_OBRPC_LOG(WARN, "alloc memory failed", K(ret));
  } else {
    int64_t args_len = common::serialization::encoded_length(args);
    int64_t extra_payload_size = calc_extra_payload_size();
    int64_t payload_sz = extra_payload_size + args_len;
    char *buf = reinterpret_cast<char *>(pool->alloc(sizeof(ObRequest) + sizeof(ObRpcPacket) + args_len + extra_payload_size));
    char *payload_buf = buf + sizeof(ObRequest) + sizeof(ObRpcPacket);
    ObAsyncLocalProcedureCallContext* ctx = reinterpret_cast<ObAsyncLocalProcedureCallContext*>(pool->alloc(sizeof(ObAsyncLocalProcedureCallContext)));
    int64_t pos = 0;

    if (OB_ISNULL(buf) || OB_ISNULL(ctx)) {
      ret = OB_ALLOCATE_MEMORY_FAILED;
      RPC_LOG(WARN, "alloc memory failed", K(ret));
    } else if (OB_FAIL(common::serialization::encode(payload_buf, args_len, pos, args))) {
      RPC_OBRPC_LOG(WARN, "serialize argument fail", K(pos), K(args_len), K(ret));
    } else if (OB_UNLIKELY(args_len < pos)) {
    #ifdef ENABLE_SERIALIZATION_CHECK
        lib::begin_check_serialization();
        common::serialization::encoded_length(args);
        lib::finish_check_serialization();
    #endif
        ret = OB_ERR_UNEXPECTED;
        RPC_OBRPC_LOG(ERROR, "arg encoded length greater than arg length", K(ret), K(payload_sz),
                      K(args_len), K(extra_payload_size), K(pos), K(pcode));
    } else {
      ObRequest *req = new (buf) ObRequest(ObRequest::OB_RPC, ObRequest::TRANSPORT_PROTO_LOCAL_ASYNC);
      ObRpcPacket *pkt = new (buf + sizeof(ObRequest)) ObRpcPacket();
      if (OB_FAIL(fill_extra_payload(*pkt, payload_buf, payload_sz, pos))) {
        RPC_OBRPC_LOG(WARN, "fill extra payload fail", K(ret), K(pos), K(payload_sz), K(args_len),
                      K(extra_payload_size), K(pcode));
      } else {
        new (ctx) ObAsyncLocalProcedureCallContext(*pool);
        int64_t header_pos = 0;
        ObReqTransport::AsyncCB* new_cb = NULL;
        pkt->set_content(payload_buf, payload_sz);
        if (OB_FAIL(init_packet(proxy, *pkt, pcode, opts, ucb == NULL /* unneed_resp */))) {
          RPC_OBRPC_LOG(WARN, "init packet fail", K(ret));
        } else if (ucb && OB_ISNULL(new_cb = ctx->clone_async_cb(ucb))) {
          ret = OB_ALLOCATE_MEMORY_FAILED;
          RPC_OBRPC_LOG(WARN, "clone callback fail", K(ret));
        } else {
          if (OB_NOT_NULL(new_cb)) {
            auto *newcb = reinterpret_cast<UCB*>(new_cb);
            init_ucb(proxy, newcb, start_ts, payload_sz);
          }
          int64_t receive_ts = ObTimeUtility::current_time();
          pkt->set_receive_ts(receive_ts);
          req->set_packet(pkt);
          req->set_request_arrival_time(pkt->get_receive_ts());
          req->set_arrival_push_diff(common::ObTimeUtility::current_time());
          req->set_receive_timestamp(pkt->get_receive_ts());
          req->set_server_handle_context(static_cast<ObLocalProcedureCallContext*>(ctx));
          // deliver rpc request
          if (OB_FAIL(deliver->deliver(*req))) {
            RPC_OBRPC_LOG(WARN, "deliver rpc request fail", K(ret));
          }
        }
      }
    }
  }
  if (OB_FAIL(ret)) {
    if (OB_NOT_NULL(pool)) {
      pool->destroy();
      pool = NULL;
    }
  }
  return ret;
}

}; // end namespace oblpc
}; // end namespace oceanbase
#endif /* OCEANBASE_LOCAL_PROCEDURE_CALL_H_ */
