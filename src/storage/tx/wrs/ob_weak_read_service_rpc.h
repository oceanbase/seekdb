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

#include "ob_weak_read_service_rpc_define.h"    // request/response structs

namespace oceanbase
{
namespace obcall
{
}
namespace rpc { namespace frame { struct ObResultCode; } }

namespace transaction
{

class ObIWrsRpc
{
public:
  virtual ~ObIWrsRpc()  {}

public:
  virtual int get_cluster_version(const common::ObAddr &server,
      const uint64_t tenant_id,
      const obcall::ObWrsGetClusterVersionRequest &req,
      obcall::ObWrsGetClusterVersionResponse &res) = 0;

  virtual int post_cluster_heartbeat(const common::ObAddr &server,
      const uint64_t tenant_id,
      const obcall::ObWrsClusterHeartbeatRequest &req) = 0;
};


class ObWrsRpc : public ObIWrsRpc
{
  static const int64_t MAX_RPC_PROCESS_HANDLER_TIME = 100 * 1000L;  // report warn threshold
public:
  ObWrsRpc();
  virtual ~ObWrsRpc() {}

  int init(ObIWeakReadService &wrs);

  virtual int get_cluster_version(const common::ObAddr &server,
      const uint64_t tenant_id,
      const obcall::ObWrsGetClusterVersionRequest &req,
      obcall::ObWrsGetClusterVersionResponse &res);

  virtual int post_cluster_heartbeat(const common::ObAddr &server,
      const uint64_t tenant_id,
      const obcall::ObWrsClusterHeartbeatRequest &req);

private:
  bool                  inited_;
  ObIWeakReadService   *wrs_;
};
} // transaction

} // oceanbase

#endif
