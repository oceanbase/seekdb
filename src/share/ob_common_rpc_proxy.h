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

#ifndef _OCEABASE_SHARE_OB_COMMON_RPC_PROXY_H_
#define _OCEABASE_SHARE_OB_COMMON_RPC_PROXY_H_

#include "rpc/obrpc/ob_rpc_proxy.h"
#include "share/ob_lease_struct.h"
#include "share/partition_table/ob_partition_location.h"
#include "share/ob_rpc_struct.h"
#include "share/ob_lonely_table_clean_rpc_struct.h"
#include "share/ob_time_zone_info_manager.h"
#include "common/storage/ob_freeze_define.h" // for ObFrozenStatus
#include "share/config/ob_server_config.h"
#include "observer/ob_server_struct.h"

namespace oceanbase
{
namespace obrpc
{
class ObCommonRpcProxy
    : public obrpc::ObRpcProxy
{
public:
  DEFINE_TO(ObCommonRpcProxy);
#define OB_RPC_DECLARATIONS
#include "ob_common_rpc_proxy.ipp"
#undef OB_RPC_DECLARATIONS
public:
  //send to rs, only need set rs_mgr, no need set dst_server
  inline ObCommonRpcProxy to_rs() const
  {
    ObCommonRpcProxy proxy = this->to();
    return proxy;
  }

  //send to addr, if failed, it will not retry to send to rs according to rs_mgr
  //the interface emphasize no retry
  inline ObCommonRpcProxy to_addr(const ::oceanbase::common::ObAddr &dst) const
  {
    ObCommonRpcProxy proxy = this->to(dst);
    return proxy;
  }

protected:

#define CALL_WITH_RETRY(call_stmt)                                      \
  do {                                                                  \
    int ret = common::OB_SUCCESS;                                       \
    set_server(GCTX.self_addr());                                       \
    if (OB_SUCC(ret)) {                                                 \
      ret = call_stmt;                                                  \
      const int64_t RETRY_TIMES = 3;                                    \
      for (int64_t i = 0;                                               \
           (common::OB_RS_NOT_MASTER == ret                             \
            || common::OB_SERVER_IS_INIT == ret)                        \
           && i < RETRY_TIMES;                                          \
           ++i) {                                                       \
        set_server(GCTX.self_addr());                                   \
        ret = call_stmt;                                                \
      }                                                                 \
    }                                                                   \
    return ret;                                                         \
  } while (false)

  template <typename Input, typename Out>
  int rpc_call(ObRpcPacketCode pcode, const Input &args,
               Out &result, Handle *handle, const ObRpcOpts &opts)
  {
    CALL_WITH_RETRY(ObRpcProxy::rpc_call(pcode, args, result, handle, opts));
  }

  template <typename Input>
  int rpc_post(ObRpcPacketCode pcode, const Input &args,
               rpc::frame::ObReqTransport::AsyncCB *cb, const ObRpcOpts &opts)
  {
    CALL_WITH_RETRY(rpc_post(pcode, args, cb, opts));
  }

  template <class pcodeStruct>
  int rpc_post(const typename pcodeStruct::Request &args,
               obrpc::ObRpcProxy::AsyncCB<pcodeStruct> *cb,
               const ObRpcOpts &opts)
  {
    CALL_WITH_RETRY(ObRpcProxy::rpc_post<pcodeStruct>(args, cb, opts));
  }

  int rpc_post(
      ObRpcPacketCode pcode,
      rpc::frame::ObReqTransport::AsyncCB *cb,
      const ObRpcOpts &opts)
  {
    CALL_WITH_RETRY(rpc_post(pcode, cb, opts));
  }
#undef CALL_WIRTH_RETRY

}; // end of class ObCommonRpcProxy

} // end of namespace share
} // end of namespace oceanbase

// rollback defines
#include "rpc/obrpc/ob_rpc_proxy_macros.h"

#endif /* _OCEABASE_SHARE_OB_COMMON_RPC_PROXY_H_ */
