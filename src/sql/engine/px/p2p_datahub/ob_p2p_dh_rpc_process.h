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
#ifndef __SQL_ENG_P2P_DH_RPC_PROCESS_H__
#define __SQL_ENG_P2P_DH_RPC_PROCESS_H__
#include "rpc/obrpc/ob_rpc_processor.h"
#include "sql/engine/px/p2p_datahub/ob_p2p_dh_rpc_proxy.h"
#include "observer/ob_server_struct.h"
#include "lib/ob_define.h"
namespace oceanbase {
namespace sql {

class ObPxP2pDhMsgP
    : public obrpc::ObRpcProcessor<obrpc::ObP2PDhRpcProxy::ObRpc<obrpc::OB_PX_P2P_DH_MSG>>
{
public:
  ObPxP2pDhMsgP(const observer::ObGlobalContext &gctx) { UNUSED(gctx);}
  virtual ~ObPxP2pDhMsgP() = default;
  //virtual int init() final;
  //virtual void destroy() final;
  virtual int process() final;
  DISALLOW_COPY_AND_ASSIGN(ObPxP2pDhMsgP);
};

class ObPxP2pDhMsgCB
      : public obrpc::ObP2PDhRpcProxy::AsyncCB<obrpc::OB_PX_P2P_DH_MSG>
{
public:
    ObPxP2pDhMsgCB(const common::ObAddr &server,
                   const common::ObCurTraceId::TraceId &trace_id,
                   int64_t start_time,
                   int64_t timeout_ts,
                   int64_t p2p_datahub_id)
        : addr_(server),
          start_time_(start_time),
          timeout_ts_(timeout_ts),
          p2p_datahub_id_(p2p_datahub_id)
  {
    trace_id_.set(trace_id);
  }
  virtual ~ObPxP2pDhMsgCB() {}
public:
  virtual int process() { return OB_SUCCESS; }
  virtual void on_invalid() {}
  virtual void on_timeout();
  rpc::frame::ObReqTransport::AsyncCB *clone(
      const rpc::frame::SPAlloc &alloc) const
  {
    void *buf = alloc(sizeof(*this));
    rpc::frame::ObReqTransport::AsyncCB *newcb = NULL;
    if (NULL != buf) {
      newcb = new (buf) ObPxP2pDhMsgCB(addr_, trace_id_,
          start_time_, timeout_ts_, p2p_datahub_id_);
    }
    return newcb;
  }
  virtual void set_args(const Request &arg) { UNUSED(arg); }
private:
  common::ObCurTraceId::TraceId trace_id_;
  common::ObAddr addr_;
  int64_t start_time_;
  int64_t timeout_ts_;
  int64_t p2p_datahub_id_;
  DISALLOW_COPY_AND_ASSIGN(ObPxP2pDhMsgCB);
};


class ObPxP2pDhClearMsgP
    : public obrpc::ObRpcProcessor<obrpc::ObP2PDhRpcProxy::ObRpc<obrpc::OB_PX_CLAER_DH_MSG>>
{
public:
  ObPxP2pDhClearMsgP(const observer::ObGlobalContext &gctx) { UNUSED(gctx);}
  virtual ~ObPxP2pDhClearMsgP() = default;
  //virtual int init() final;
  //virtual void destroy() final;
  virtual int process() final;
  DISALLOW_COPY_AND_ASSIGN(ObPxP2pDhClearMsgP);
};


}
}

#endif
