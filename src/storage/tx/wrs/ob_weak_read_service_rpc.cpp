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

#define USING_LOG_PREFIX  TRANS

#include "ob_weak_read_service_rpc.h"
#include "observer/ob_ex_rpc.h"               // ex_rpc::sync_call

namespace oceanbase
{
using namespace obcall;
using namespace common;

namespace transaction
{

ObWrsRpc::ObWrsRpc() :
    inited_(false),
    wrs_(NULL)
{
}

int ObWrsRpc::init(ObIWeakReadService &wrs)
{
  int ret = OB_SUCCESS;
  if (OB_UNLIKELY(inited_)) {
    ret = OB_INIT_TWICE;
  } else {
    wrs_ = &wrs;
    inited_ = true;
  }
  return ret;
}

int ObWrsRpc::get_cluster_version(const common::ObAddr &server, const uint64_t tenant_id,
    const obcall::ObWrsGetClusterVersionRequest &req,
    obcall::ObWrsGetClusterVersionResponse &res)
{
  int ret = OB_SUCCESS;
  UNUSED(server);
  if (OB_UNLIKELY(! inited_) || OB_ISNULL(wrs_)) {
    ret = OB_NOT_INIT;
  } else {
    // single-replica: cluster service master is always self, dispatch in-process
    ex_rpc::sync_call([&]() {
      wrs_->process_get_cluster_version_rpc(tenant_id, req, res);
    });
  }
  return ret;
}

int ObWrsRpc::post_cluster_heartbeat(const common::ObAddr &server,
    const uint64_t tenant_id,
    const obcall::ObWrsClusterHeartbeatRequest &req)
{
  int ret = OB_SUCCESS;
  if (OB_UNLIKELY(! inited_) || OB_ISNULL(wrs_)) {
    ret = OB_NOT_INIT;
  } else {
    // single-replica: cluster service master is always self; dispatch async
    // in-process (ex-RPC), restoring the original async .post(req, &ClusterHeartbeatCB)
    // decoupling. req is serialized (deep-copied); the heartbeat handler + the former
    // AsyncCB logic both run on the worker thread (cb-equivalent inline).
    auto *wrs = wrs_;
    (void)ex_rpc::async_call<void>(req,
        [wrs, tenant_id, server](const obcall::ObWrsClusterHeartbeatRequest &r) {
      obcall::ObWrsClusterHeartbeatResponse res;
      wrs->process_cluster_heartbeat_rpc(tenant_id, r, res);
      rpc::frame::ObResultCode rcode;
      rcode.rcode_ = OB_SUCCESS;
      wrs->process_cluster_heartbeat_rpc_cb(tenant_id, rcode, res, server);
    });
  }
  return ret;
}

}
}
