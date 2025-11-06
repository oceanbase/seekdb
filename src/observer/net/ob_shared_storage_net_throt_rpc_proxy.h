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

#ifndef OCEANBASE_STORAGE_NET_THROT_RPC_PROXY_H_
#define OCEANBASE_STORAGE_NET_THROT_RPC_PROXY_H_

#include "observer/net/ob_shared_storage_net_throt_rpc_struct.h"
#include "observer/ob_server_struct.h"
#include "rpc/obrpc/ob_rpc_proxy.h"
#include "share/ob_define.h"
#include "share/rpc/ob_async_rpc_proxy.h"

namespace oceanbase
{

namespace obrpc
{
RPC_F(OB_SHARED_STORAGE_NET_THROT_PREDICT, obrpc::ObSSNTEndpointArg, obrpc::ObSharedDeviceResourceArray,
    ObSharedStorageNetThrotPredictProxy);
RPC_F(OB_SHARED_STORAGE_NET_THROT_SET, obrpc::ObSharedDeviceResourceArray, obrpc::ObSSNTSetRes,
    ObSharedStorageNetThrotSetProxy);
class ObSSNTRpcProxy : public obrpc::ObRpcProxy
{
public:
  DEFINE_TO(ObSSNTRpcProxy);
  RPC_S(PR5 shared_storage_net_throt_register, OB_SHARED_STORAGE_NET_THROT_REGISTER, (obrpc::ObSSNTEndpointArg));
};
}  // namespace obrpc
}  // namespace oceanbase
#endif /* OCEANBASE_STORAGE_NET_THROT_RPC_PROXY_H_ */
