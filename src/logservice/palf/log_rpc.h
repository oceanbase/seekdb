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

#ifndef OCEANBASE_LOGSERVICE_LOG_RPC_
#define OCEANBASE_LOGSERVICE_LOG_RPC_

#include "lib/ob_errno.h"
#include "lib/utility/ob_macro_utils.h"            // IS_NOT_INIT
#include "lib/net/ob_addr.h"                       // ObAddr
#include "palf_options.h"                          // PalfTransportCompressOptions

namespace oceanbase
{
namespace rpc
{
namespace frame
{
class ObReqTransport;
}
}
namespace common
{
class ObAddr;
}
namespace palf
{
// NB: in single-replica seekdb inter-replica palf/election RPC never fires.
// The obcall send/recv plumbing (LogRpcProxyV2, processors, packet/macros) has
// been removed. LogRpc is retained only as the wiring shell that holds the
// transport compress options consumed by the rest of palf; all send paths in
// LogNetService are neutered no-ops.
class LogRpc {
public:
  LogRpc();
  ~LogRpc();
  int init(const common::ObAddr &self,
           const int64_t cluster_id);
  void destroy();
  int update_transport_compress_options(const PalfTransportCompressOptions &compress_opt);
  const PalfTransportCompressOptions& get_compress_opts() const;

  TO_STRING_KV(K_(self), K_(is_inited));
private:
  ObAddr self_;
  mutable ObSpinLock opt_lock_;
  PalfTransportCompressOptions options_;
  
  int64_t cluster_id_;
  bool is_inited_;
};
} // end namespace palf
} // end namespace oceanbase

#endif
