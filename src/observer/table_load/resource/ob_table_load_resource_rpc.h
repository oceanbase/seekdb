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

#pragma once

#include "ob_table_load_resource_rpc_struct.h"
#include "observer/table_load/ob_table_load_rpc_executor.h"
#include "observer/ob_ex_rpc.h"

namespace oceanbase
{
namespace observer
{
class ObDirectLoadResourceApplyExecutor;
class ObDirectLoadResourceReleaseExecutor;
class ObDirectLoadResourceUpdateExecutor;
class ObDirectLoadResourceCheckExecutor;

class ObTableLoadResourceRpcProxy
{
public:
  static const int64_t DEFAULT_TIMEOUT_US = 10LL * 1000 * 1000; // 10s
  template <ObDirectLoadResourceCommandType pcode, typename IGNORE = void>
  struct ObTableLoadResourceRpc
  {
  };

#define OB_DEFINE_TABLE_LOAD_RESOURCE_RPC_CALL_1(prio, name, pcode, Arg)                              \
  int name(const Arg &arg)                                                                            \
  {                                                                                                   \
    int ret = OB_SUCCESS;                                                                             \
    ObDirectLoadResourceOpRequest request;                                                            \
    ObDirectLoadResourceOpResult result;                                                              \
    request.command_type_ = pcode;                                                                    \
    result.allocator_ = &allocator_;                                                                  \
    if (OB_FAIL(request.set_arg(arg, allocator_))) {                                                  \
      SERVER_LOG(WARN, "fail to set arg", K(ret), K(arg));                                            \
    } else if (OB_FAIL(ex_rpc::sync_call([&]{ return dispatch(request, result, allocator_); }))) {                           \
      SERVER_LOG(WARN, "fail to rpc call direct load resource", K(ret), K_(addr), K(arg));            \
    } else if (OB_UNLIKELY(result.command_type_ != pcode)) {                                          \
      ret = OB_ERR_UNEXPECTED;                                                                        \
      SERVER_LOG(WARN, "unexpected command type", K(ret), K(request), K(result));                     \
    } else if (OB_UNLIKELY(!result.res_content_.empty())) {                                           \
      ret = OB_ERR_UNEXPECTED;                                                                        \
      SERVER_LOG(WARN, "unexpected non empty res content", K(ret), K(result));                        \
    }                                                                                                 \
    return ret;                                                                                       \
  }

#define OB_DEFINE_TABLE_LOAD_RESOURCE_RPC_CALL_2(prio, name, pcode, Arg, Res)                         \
  int name(const Arg &arg, Res &res)                                                                  \
  {                                                                                                   \
    int ret = OB_SUCCESS;                                                                             \
    ObDirectLoadResourceOpRequest request;                                                            \
    ObDirectLoadResourceOpResult result;                                                              \
    request.command_type_ = pcode;                                                                    \
    result.allocator_ = &allocator_;                                                                  \
    if (OB_FAIL(request.set_arg(arg, allocator_))) {                                                  \
      SERVER_LOG(WARN, "fail to set arg", K(ret), K(arg));                                            \
    } else if (OB_FAIL(ex_rpc::sync_call([&]{ return dispatch(request, result, allocator_); }))) {                           \
      SERVER_LOG(WARN, "fail to rpc call direct load resource", K(ret), K_(addr), K(arg));            \
    } else if (OB_UNLIKELY(result.command_type_ != pcode)) {                                          \
      ret = OB_ERR_UNEXPECTED;                                                                        \
      SERVER_LOG(WARN, "unexpected command type", K(ret), K(request), K(result));                     \
    } else if (OB_FAIL(result.get_res(res))) {                                                        \
      SERVER_LOG(WARN, "fail to get res", K(ret), K(result));                                         \
    }                                                                                                 \
    return ret;                                                                                       \
  }

#define OB_DEFINE_TABLE_LOAD_RESOURCE_RPC_CALL(prio, name, pcode, ...)                                \
  CONCAT(OB_DEFINE_TABLE_LOAD_RESOURCE_RPC_CALL_, ARGS_NUM(__VA_ARGS__))(prio, name, pcode, __VA_ARGS__)

#define OB_DEFINE_TABLE_LOAD_RESOURCE_RPC(prio, name, pcode, Processor, ...)                          \
  OB_DEFINE_TABLE_LOAD_RPC(ObTableLoadResourceRpc, pcode, Processor, ObDirectLoadResourceOpRequest,   \
                           ObDirectLoadResourceOpResult, __VA_ARGS__)                                 \
  OB_DEFINE_TABLE_LOAD_RESOURCE_RPC_CALL(ObTableLoadRpcPriority::prio, name, pcode, __VA_ARGS__)

public:
  ObTableLoadResourceRpcProxy()
    : allocator_("TLD_RpcProxy"),
      timeout_(DEFAULT_TIMEOUT_US)
  {
    
  }

  ObTableLoadResourceRpcProxy &to(ObAddr addr)
  {
    addr_ = addr;
    return *this;
  }
  ObTableLoadResourceRpcProxy &timeout(int64_t timeout)
  {
    timeout_ = timeout;
    return *this;
  }
  ObTableLoadResourceRpcProxy &by()
  {
    return *this;
  }

  static int dispatch(const ObDirectLoadResourceOpRequest &request, 
                      ObDirectLoadResourceOpResult &result,
                      common::ObIAllocator &allocator);

  // apply_resource
  OB_DEFINE_TABLE_LOAD_RESOURCE_RPC(NORMAL_PRIO,
                                    apply_resource, 
                                    ObDirectLoadResourceCommandType::APPLY,
                                    ObDirectLoadResourceApplyExecutor,
                                    ObDirectLoadResourceApplyArg,
                                    ObDirectLoadResourceOpRes);
  // release_resource
  OB_DEFINE_TABLE_LOAD_RESOURCE_RPC(NORMAL_PRIO,
                                    release_resource, 
                                    ObDirectLoadResourceCommandType::RELEASE,
                                    ObDirectLoadResourceReleaseExecutor,
                                    ObDirectLoadResourceReleaseArg);
  // update_resource
  OB_DEFINE_TABLE_LOAD_RESOURCE_RPC(NORMAL_PRIO,
                                    update_resource, 
                                    ObDirectLoadResourceCommandType::UPDATE,
                                    ObDirectLoadResourceUpdateExecutor,
                                    ObDirectLoadResourceUpdateArg);
  // check_resource
  OB_DEFINE_TABLE_LOAD_RESOURCE_RPC(HIGH_PRIO,
                                    check_resource, 
                                    ObDirectLoadResourceCommandType::CHECK,
                                    ObDirectLoadResourceCheckExecutor,
                                    ObDirectLoadResourceCheckArg,
                                    ObDirectLoadResourceOpRes);

private:
  ObArenaAllocator allocator_;
  ObAddr addr_;
  int64_t timeout_;
};

#define TABLE_LOAD_RESOURCE_RPC_CALL(name, addr, arg, ...)                                                     \
  ({                                                                                                           \
    ObTableLoadResourceRpcProxy proxy;                                                   \
    ObTimeoutCtx ctx;                                                                                          \
    if (OB_FAIL(ObShareUtil::set_default_timeout_ctx(ctx, ObTableLoadResourceRpcProxy::DEFAULT_TIMEOUT_US))) { \
      LOG_WARN("fail to set default timeout ctx", KR(ret));                                                    \
    } else if (OB_FAIL(proxy.to(addr)                                                                          \
                            .timeout(ctx.get_timeout())                                                        \
                            .by()                                                                      \
                            .name(arg, ##__VA_ARGS__))) {                                                                \
      LOG_WARN("fail to rpc call " #name, KR(ret), K(addr), K(arg));                                           \
    }                                                                                                          \
  })

} // namespace observer
} // namespace oceanbase
