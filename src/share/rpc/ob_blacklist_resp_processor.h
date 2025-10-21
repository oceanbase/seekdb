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

#ifndef OCEANBASE_RPC_OB_BLACKLIST_RESP_PROCESSOR_H_
#define OCEANBASE_RPC_OB_BLACKLIST_RESP_PROCESSOR_H_

#include "rpc/obrpc/ob_rpc_processor.h"
#include "share/rpc/ob_blacklist_proxy.h"

namespace oceanbase
{
namespace obrpc
{
class ObBlacklistRespP : public ObRpcProcessor< obrpc::ObBlacklistRpcProxy::ObRpc<OB_SERVER_BLACKLIST_RESP> >
{
public:
  ObBlacklistRespP() {}
  ~ObBlacklistRespP() {}
protected:
  int process();
private:
  DISALLOW_COPY_AND_ASSIGN(ObBlacklistRespP);
};
}; // end namespace rpc
}; // end namespace oceanbase

#endif /* OCEANBASE_RPC_OB_BLACKLIST_RESP_PROCESSOR_H_ */
