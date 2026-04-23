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

#ifndef OCEANBASE_RPC_OB_BATCH_RPC_H_
#define OCEANBASE_RPC_OB_BATCH_RPC_H_

#include "ob_batch_proxy.h"
#include "lib/ob_define.h"
#include "share/ob_ls_id.h"

namespace oceanbase
{
namespace obrpc
{

// Standalone version: each post() directly builds a single-message batch packet
// and sends it via ObBatchRpcProxy, bypassing the former ring-buffer batching.
class ObBatchRpc
{
public:
  typedef ObBatchRpcProxy Rpc;
  typedef ObIFill Req;
  ObBatchRpc() : is_inited_(false) {}
  ~ObBatchRpc() {}
  int init(rpc::frame::ObReqTransport *transport, const common::ObAddr &self_addr);
  void stop() {}
  void wait() {}
  void destroy() {}
  int post(const uint64_t tenant_id, const common::ObAddr &dest, const int64_t dst_cluster_id,
           const uint32_t batch_type, const uint32_t sub_type,
           const Req& req);
  int post(const uint64_t tenant_id, const common::ObAddr &dest, const int64_t dst_cluster_id,
           const uint32_t batch_type, const int16_t sub_type, const share::ObLSID& ls,
           const Req& req);
private:
  bool is_inited_;
  Rpc rpc_;
  common::ObAddr self_;
};

}; // end namespace obrpc
}; // end namespace oceanbase

#endif /* OCEANBASE_RPC_OB_BATCH_RPC_H_ */
