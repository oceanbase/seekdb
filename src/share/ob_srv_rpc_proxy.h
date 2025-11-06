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

#ifndef _OCEABASE_COMMON_OB_SRV_RPC_PROXY_H_
#define _OCEABASE_COMMON_OB_SRV_RPC_PROXY_H_

#include "sql/engine/cmd/ob_kill_session_arg.h"
#include "storage/tablelock/ob_table_lock_rpc_struct.h"
#include "rpc/obrpc/ob_rpc_proxy.h"
#include "share/ob_common_id.h"
#include "share/ob_rpc_struct.h"
#include "share/ob_server_struct.h"
#include "share/ob_lonely_table_clean_rpc_struct.h"
#include "observer/net/ob_net_endpoint_ingress_rpc_struct.h"
#include "observer/net/ob_shared_storage_net_throt_rpc_struct.h"
#include "share/ob_heartbeat_struct.h"
#include "share/resource_limit_calculator/ob_resource_commmon.h"
#include "observer/table_load/control/ob_table_load_control_rpc_struct.h"
#include "observer/table_load/resource/ob_table_load_resource_rpc_struct.h"
#include "rpc/obrpc/ob_rpc_reverse_keepalive_struct.h"

namespace oceanbase
{
namespace obrpc
{

class ObSrvRpcProxy
    : public ObRpcProxy
{
public:
  DEFINE_TO(ObSrvRpcProxy);
#define OB_RPC_DECLARATIONS
#include "ob_srv_rpc_proxy.ipp"
#undef OB_RPC_DECLARATIONS

}; // end of class ObSrvRpcProxy

} // end of namespace rpc
} // end of namespace oceanbase
// rollback defines
#include "rpc/obrpc/ob_rpc_proxy_macros.h"

#endif /* _OCEABASE_COMMON_OB_SRV_RPC_PROXY_H_ */
