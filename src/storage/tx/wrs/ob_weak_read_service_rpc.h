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

#ifndef OCEANBASE_TRANSACTION_OB_WEAK_READ_SERVICE_RPC_H_
#define OCEANBASE_TRANSACTION_OB_WEAK_READ_SERVICE_RPC_H_

#include "ob_i_weak_read_service.h"             // ObIWeakReadService

#include "ob_weak_read_service_rpc_define.h"    // ObWrsRpcProxy

namespace oceanbase
{
namespace obrpc
{
struct ObRpcResultCode;
}

namespace transaction
{

class ObIWrsRpc
{
public:
  virtual ~ObIWrsRpc()  {}

public:
  virtual int get_cluster_version(const common::ObAddr &server,
      const uint64_t tenant_id,
      const obrpc::ObWrsGetClusterVersionRequest &req,
      obrpc::ObWrsGetClusterVersionResponse &res) = 0;

  virtual int post_cluster_heartbeat(const common::ObAddr &server,
      const uint64_t tenant_id,
      const obrpc::ObWrsClusterHeartbeatRequest &req) = 0;
};


class ObWrsRpc : public ObIWrsRpc
{
  static const int64_t MAX_RPC_PROCESS_HANDLER_TIME = 100 * 1000L;  // report warn threshold
public:
  ObWrsRpc();
  virtual ~ObWrsRpc() {}

  int init(const rpc::frame::ObReqTransport *transport, ObIWeakReadService &wrs);

  virtual int get_cluster_version(const common::ObAddr &server,
      const uint64_t tenant_id,
      const obrpc::ObWrsGetClusterVersionRequest &req,
      obrpc::ObWrsGetClusterVersionResponse &res);

  virtual int post_cluster_heartbeat(const common::ObAddr &server,
      const uint64_t tenant_id,
      const obrpc::ObWrsClusterHeartbeatRequest &req);

public:
  class ClusterHeartbeatCB : public obrpc::ObWrsRpcProxy::AsyncCB<obrpc::OB_WRS_CLUSTER_HEARTBEAT>
  {
  public:
    ClusterHeartbeatCB() : wrs_(NULL) {}
    virtual ~ClusterHeartbeatCB() {}

  public:
    void init(ObIWeakReadService *wrs) { wrs_ = wrs; }
    void set_args(const Request &args) { UNUSED(args); }
    rpc::frame::ObReqTransport::AsyncCB *clone(const rpc::frame::SPAlloc &alloc) const;
    int process();
    void on_timeout();
    void on_invalid();
  private:
    int do_process_(const obrpc::ObRpcResultCode &rcode);
  private:
    ObIWeakReadService *wrs_;
  };

private:
  bool                  inited_;
  obrpc::ObWrsRpcProxy  proxy_;
  ClusterHeartbeatCB    cluster_heartbeat_cb_;
};
} // transaction

} // oceanbase

#endif
