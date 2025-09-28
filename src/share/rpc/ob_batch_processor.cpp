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

#include "ob_batch_processor.h"
#include "sql/ob_sql.h"
#include "sql/ob_sql_task.h"
#include "observer/omt/ob_tenant.h"
#include "logservice/ob_log_service.h"
#include "logservice/palf/log_request_handler.h"

namespace oceanbase
{
using namespace common;
namespace obrpc
{
int ObBatchP::process()
{
  int ret = OB_SUCCESS;
  {
    int64_t req_pos = 0;
    ObBatchPacket::Req* req = NULL;
    common::ObAddr sender;
    if (arg_.src_addr_.is_valid()) {
      sender = arg_.src_addr_;
    } else {
      sender.set_ipv4_server_id(arg_.src_);
    }
    // Get the cluster_id of the source
    const int64_t src_cluster_id = get_src_cluster_id();
    share::ObLSID ls_id;
    int64_t clog_batch_cnt = 0;
    int64_t trx_batch_cnt = 0;
    while(NULL != (req = arg_.next(req_pos))) {
      // rewrite ret
      ret = OB_SUCCESS;
      const uint32_t flag = (req->type_ >> 24);
      const uint32_t batch_type = ((req->type_ >> 16) & 0xff);
      const uint32_t msg_type = req->type_ & 0xffff;
      ObCurTraceId::TraceId trace_id;
      char *buf = (char*)(req + 1);
      int64_t pos = 0;
      if (1 == flag) {
        if (OB_FAIL(trace_id.deserialize(buf, req->size_, pos))) {
          RPC_LOG(WARN, "decode trace id failed", K(ret));
        } else {
          ObCurTraceId::set(trace_id);
        }
      }
      if (OB_SUCC(ret)) {
        switch (batch_type) {
          case CLOG_BATCH_REQ:
            if (OB_SUCCESS == ls_id.deserialize(buf, req->size_, pos)) {
              handle_log_req(sender, msg_type, ls_id, buf + pos, req->size_ - (int32_t)pos);
            }
            clog_batch_cnt++;
            break;
          case TRX_BATCH_REQ_NODELAY:
            trx_batch_cnt++;
            if (OB_SUCCESS == ls_id.deserialize(buf, req->size_, pos)) {
              handle_tx_req(msg_type, buf + pos, req->size_ - (int32_t)pos);
            }
            break;

          default:
            RPC_LOG(ERROR, "unknown batch req type", K(req->type_));
            break;
        }
      }
      if (OB_FAIL(ret)) {
        RPC_LOG(WARN, "process batch rpc",
            K(ret), K(sender), K(ls_id), K(src_cluster_id), K(flag), K(batch_type), K(msg_type), K(trace_id));
      }
    }
    if (REACH_TIME_INTERVAL(3000000)) {
      RPC_LOG(INFO, "batch rpc statistics", K(clog_batch_cnt), K(trx_batch_cnt));
    }
  }
  return ret;
}

int ObBatchP::handle_tx_req(int type, const char* buf, int32_t size)
{
  int ret = OB_SUCCESS;
  transaction::ObTransService *txs = MTL(transaction::ObTransService *);
  if (OB_ISNULL(txs)) {
    ret = OB_ERR_UNEXPECTED;
  } else {
    ret = txs->handle_tx_batch_req(type, buf, size);
  }
  return ret;
}

int ObBatchP::handle_log_req(const common::ObAddr& sender, int type, const share::ObLSID &ls_id, const char* buf, int32_t size)
{
  int ret = OB_SUCCESS;
  ObReqTimestamp req_ts;
  req_ts.receive_timestamp_ = get_receive_timestamp();
  req_ts.enqueue_timestamp_ = get_enqueue_timestamp();
  req_ts.run_timestamp_ = get_run_timestamp();
  logservice::ObLogService *log_service = MTL(logservice::ObLogService*);
  palf::IPalfEnvImpl *palf_env_impl = NULL;

  #define __LOG_BATCH_PROCESS_REQ(TYPE)                                                 \
  palf::LogRequestHandler log_request_handler(palf_env_impl);                           \
  TYPE req;                                                                             \
  int64_t pos = 0;                                                                      \
  if (OB_FAIL(req.deserialize(buf, size, pos))) {                                       \
    RPC_LOG(ERROR, "deserialize rpc failed", K(ret), KP(buf), K(size));                 \
  } else if (OB_FAIL(log_request_handler.handle_request(ls_id.id(), sender, req))) {    \
    RPC_LOG(TRACE, "handle_request failed", K(ret), K(ls_id), K(sender), K(req));       \
  } else {                                                                              \
    RPC_LOG(TRACE, "handle_log_request success", K(ret), K(ls_id), K(sender), K(req));  \
  }
  if (OB_ISNULL(log_service)) {
    ret = OB_ERR_UNEXPECTED;
    RPC_LOG(ERROR, "log_service is nullptr", K(ret), K(log_service));
  } else if (OB_ISNULL(log_service->get_palf_env())) {
    ret = OB_ERR_UNEXPECTED;
    RPC_LOG(ERROR, "palf_env is nullptr", K(ret), KP(log_service));
  } else if (FALSE_IT(palf_env_impl = log_service->get_palf_env()->get_palf_env_impl())) {
  } else if (palf::LOG_BATCH_PUSH_LOG_REQ == type) {
    __LOG_BATCH_PROCESS_REQ(palf::LogPushReq);
  } else if (palf::LOG_BATCH_PUSH_LOG_RESP == type) {
    __LOG_BATCH_PROCESS_REQ(palf::LogPushResp);
  } else {
    RPC_LOG(ERROR, "invalid sub_type", K(ret), K(type));
  }
  #undef __LOG_BATCH_PROCESS_REQ
  return ret;
}
}; // end namespace rpc
}; // end namespace oceanbase
