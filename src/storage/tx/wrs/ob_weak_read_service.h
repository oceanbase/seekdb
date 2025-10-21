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

#ifndef OCEANBASE_TRANSACTION_OB_WRS_SERVICE_
#define OCEANBASE_TRANSACTION_OB_WRS_SERVICE_

#include "share/ob_thread_pool.h"             // ObThreadPool
#include "rpc/obrpc/ob_rpc_result_code.h"     // ObRpcResultCode
#include "lib/net/ob_addr.h"                  // ObAddr

#include "ob_i_weak_read_service.h"           // ObIWeakReadService
#include "ob_tenant_weak_read_service.h"      // ObTenantWeakReadService
#include "ob_weak_read_service_rpc_define.h"  // obrpc::
#include "ob_weak_read_service_rpc.h"         // ObWrsRpc
#include "storage/ls/ob_ls.h"

namespace oceanbase
{
namespace storage { class ObIPartitionGroup; }
namespace rpc { namespace frame { class ObReqTransport; } }

namespace transaction
{

class ObWeakReadService : public ObIWeakReadService
{
public:
  ObWeakReadService() :
      inited_(false),
      wrs_rpc_()
  {}
  ~ObWeakReadService() { destroy(); }
  int init(const rpc::frame::ObReqTransport *transport);
  void destroy();
  int start();
  void stop();
  void wait();
public:
  /// get SERVER level weak read version
  int get_server_version(const uint64_t tenant_id, share::SCN &version) const;
  /// get CLUSTER level weak read version
  int get_cluster_version(const uint64_t tenant_id, share::SCN &version);

  int check_tenant_can_start_service(const uint64_t tenant_id, bool &can_start_service, SCN &version) const;

  ///////////// RPC process functions /////////////////
  void process_get_cluster_version_rpc(const uint64_t tenant_id,
      const obrpc::ObWrsGetClusterVersionRequest &req,
      obrpc::ObWrsGetClusterVersionResponse &res);

  void process_cluster_heartbeat_rpc(const uint64_t tenant_id,
      const obrpc::ObWrsClusterHeartbeatRequest &req,
      obrpc::ObWrsClusterHeartbeatResponse &res);

  void process_cluster_heartbeat_rpc_cb(const uint64_t tenant_id,
      const obrpc::ObRpcResultCode &rcode,
      const obrpc::ObWrsClusterHeartbeatResponse &res,
      const common::ObAddr &dst);

  ObIWrsRpc &get_wrs_rpc() { return wrs_rpc_; }

private:
  bool      inited_;
  ObWrsRpc  wrs_rpc_;
};

} // transaction
} // oceanbase

#endif  // OCEANBASE_TRANSACTION_OB_WRS_SERVICE_
