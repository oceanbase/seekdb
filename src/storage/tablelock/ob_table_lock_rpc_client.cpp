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

#define USING_LOG_PREFIX TABLELOCK

#include "ob_table_lock_rpc_client.h"
#include "share/location_cache/ob_location_service.h"
#include "observer/ob_srv_network_frame.h"

namespace oceanbase
{
using namespace share;

namespace transaction
{
namespace tablelock
{

static inline int get_ls_leader(
    const int64_t cluster_id,
    const uint64_t tenant_id,
    const ObLSID &ls_id,
    const int64_t abs_timeout_ts,
    ObAddr &addr)
{
  int ret = OB_SUCCESS;
  ObLocationService *location_service = GCTX.location_service_;
  if (OB_ISNULL(location_service)) {
    ret = OB_NOT_INIT;
    LOG_WARN("location_service not inited", K(ret));
  } else if (OB_FAIL(location_service->get_leader_with_retry_until_timeout(
      cluster_id,
      tenant_id,
      ls_id,
      addr,
      abs_timeout_ts))) {
    LOG_WARN("failed to get ls leader with retry until timeout",
        K(ret), K(cluster_id), K(tenant_id), K(ls_id), K(addr), K(abs_timeout_ts));
  } else {
    LOG_DEBUG("get ls leader from location_service",
        K(ret), K(cluster_id), K(tenant_id), K(ls_id), K(addr), K(abs_timeout_ts));
  }
  return ret;
}

int ObTableLockRpcClient::init()
{
  int ret = OB_SUCCESS;
  if (OB_FAIL(table_lock_rpc_proxy_.init(GCTX.net_frame_->get_req_transport(),
                                         GCTX.self_addr()))) {
    LOG_WARN("failed to init rpc proxy", K(ret));
  } else {
    table_lock_rpc_proxy_.set_detect_session_killed(true);
  }
  return ret;
}

ObTableLockRpcClient &ObTableLockRpcClient::get_instance()
{
  static ObTableLockRpcClient instance_;
  return instance_;
}



}
}
}
