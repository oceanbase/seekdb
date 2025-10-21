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

#ifndef OCEANBASE_STORAGE_STORAGE_ASYNC_RPC_H_
#define OCEANBASE_STORAGE_STORAGE_ASYNC_RPC_H_

#include "share/ob_rpc_struct.h"
#include "share/ob_srv_rpc_proxy.h"
#include "share/rpc/ob_async_rpc_proxy.h"
#include "rpc/obrpc/ob_rpc_packet.h"
#include "rpc/obrpc/ob_rpc_result_code.h"
#include "rpc/obrpc/ob_rpc_proxy.h"
#include "lib/oblog/ob_log.h"
#include "storage/ob_storage_rpc.h"

namespace oceanbase
{
namespace storage
{

#define RPC_HA(code, arg, result, name) \
  typedef obrpc::ObAsyncRpcProxy<code, arg, result, \
    int (obrpc::ObStorageRpcProxy::*)(const arg &, obrpc::ObStorageRpcProxy::AsyncCB<code> *, const obrpc::ObRpcOpts &), obrpc::ObStorageRpcProxy> name

struct ObHAAsyncRpcArg final
{
public:
  ObHAAsyncRpcArg();
  ~ObHAAsyncRpcArg();
  bool is_valid() const;

  TO_STRING_KV(K_(tenant_id), K_(group_id), K_(rpc_timeout), K_(member_addr_list));

  uint64_t tenant_id_;
  int32_t group_id_;
  int64_t rpc_timeout_;
  common::ObArray<common::ObAddr> member_addr_list_;
private:
  DISALLOW_COPY_AND_ASSIGN(ObHAAsyncRpcArg);
};

class ObHAAsyncRpc final
{
public:
  ObHAAsyncRpc() = default;
  ~ObHAAsyncRpc() {}

  template<class RpcArg, class RpcProxy, class ResponseType>
  static int send_async_rpc(
      const ObHAAsyncRpcArg &async_rpc_arg,
      const RpcArg &arg,
      RpcProxy &batch_rpc_proxy,
      common::ObIArray<ResponseType> &responses);
};

template<class RpcArg, class RpcProxy, class ResponseType>
int ObHAAsyncRpc::send_async_rpc(
    const ObHAAsyncRpcArg &async_rpc_arg,
    const RpcArg &arg,
    RpcProxy &batch_rpc_proxy,
    common::ObIArray<ResponseType> &responses)
{
  int ret = OB_SUCCESS;
  const int64_t cluster_id = GCONF.cluster_id;
  responses.reset();

  if (!async_rpc_arg.is_valid() || !arg.is_valid()) {
    ret = OB_INVALID_ARGUMENT;
    OB_LOG(WARN, "send async rpc get invalid argument", K(ret), K(async_rpc_arg), K(arg));
  } else {
    for (int64_t i = 0; OB_SUCC(ret) && i < async_rpc_arg.member_addr_list_.count(); ++i) {
      const ObAddr &addr = async_rpc_arg.member_addr_list_.at(i);
      if (OB_FAIL(batch_rpc_proxy.call(addr, async_rpc_arg.rpc_timeout_, cluster_id, arg.tenant_id_,
          async_rpc_arg.group_id_, arg))) {
        OB_LOG(WARN, "failed to send async rpc request", K(ret), K(addr), K(async_rpc_arg));
      }
    }

    ObArray<int> return_code_array;
    int tmp_ret = OB_SUCCESS;
    if (OB_TMP_FAIL(batch_rpc_proxy.wait_all(return_code_array))) {
      OB_LOG(WARN, "fail to wait all batch result", KR(ret), KR(tmp_ret));
      ret = OB_SUCC(ret) ? tmp_ret : ret;
    }
    if (OB_FAIL(ret)) {
    } else if (return_code_array.count() != async_rpc_arg.member_addr_list_.count()
        || return_code_array.count() != batch_rpc_proxy.get_results().count()) {
      ret = OB_ERR_UNEXPECTED;
      OB_LOG(WARN, "count not match", K(ret),
               "return_count", return_code_array.count(),
               "result_count", batch_rpc_proxy.get_results().count(),
               "server_count", async_rpc_arg.member_addr_list_.count());
    } else {
      ARRAY_FOREACH_X(batch_rpc_proxy.get_results(), idx, cnt, OB_SUCC(ret)) {
        const ResponseType *tmp_response = batch_rpc_proxy.get_results().at(idx);
        const int res_ret = return_code_array.at(idx);
        if (OB_SUCCESS != res_ret) {
          ret = res_ret;
          OB_LOG(WARN, "rpc execute failed", KR(ret), K(idx));
        } else if (OB_ISNULL(tmp_response)) {
          ret = OB_ERR_UNEXPECTED;
          OB_LOG(WARN, "response is null", K(ret));
        } else if (OB_FAIL(responses.push_back(*tmp_response))) {
          OB_LOG(WARN, "failed to push response into array", K(ret), KPC(tmp_response));
        }
      }
    }
  }
  return ret;
}

}//end namespace storage
}//end namespace oceanbase

#endif //OCEANBASE_STORAGE_STORAGE_ASYNC_RPC_H_
