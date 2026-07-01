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

#include "lib/allocator/page_arena.h"
#include "lib/string/ob_string.h"
#include "share/rc/ob_tenant_base.h"

namespace oceanbase
{
namespace observer
{
// Empty serializable placeholder for RPC structs without a separate Res
// (formerly ObTableLoadRpcNoneT).
struct ObTableLoadRpcNoneT
{
  int serialize(SERIAL_PARAMS) const { UNF_UNUSED_SER; return common::OB_SUCCESS; }
  int deserialize(DESERIAL_PARAMS) { UNF_UNUSED_DES; return common::OB_SUCCESS; }
  int64_t get_serialize_size() const { return 0; }
  TO_STRING_EMPTY();
};

enum class ObTableLoadRpcPriority
{
  NORMAL_PRIO = 0,
  HIGH_PRIO = 1,
};

// template <typename pcode, typename IGNORE = void>
// struct ObTableLoadRpc
// {
// };

#define OB_DEFINE_TABLE_LOAD_RPC_STRUCT(RpcType, pcode, Processor, Request, Result, Arg, Res) \
  template <typename IGNORE>                                                                  \
  struct RpcType<pcode, IGNORE>                                                               \
  {                                                                                           \
    static constexpr auto PCODE = pcode;                                                      \
    typedef Processor ProcessorType;                                                          \
    typedef Request RequestType;                                                              \
    typedef Result ResultType;                                                                \
    typedef Arg ArgType;                                                                      \
    typedef Res ResType;                                                                      \
  };

#define OB_DEFINE_TABLE_LOAD_RPC_S1(RpcType, pcode, Processor, Request, Result, Arg) \
  OB_DEFINE_TABLE_LOAD_RPC_STRUCT(RpcType, pcode, Processor, Request, Result, Arg,   \
                                  ObTableLoadRpcNoneT)

#define OB_DEFINE_TABLE_LOAD_RPC_S2(RpcType, pcode, Processor, Request, Result, Arg, Res) \
  OB_DEFINE_TABLE_LOAD_RPC_STRUCT(RpcType, pcode, Processor, Request, Result, Arg, Res)

#define OB_DEFINE_TABLE_LOAD_RPC(RpcType, pcode, Processor, Request, Result, ...) \
  CONCAT(OB_DEFINE_TABLE_LOAD_RPC_S, ARGS_NUM(__VA_ARGS__))                       \
  (RpcType, pcode, Processor, Request, Result, __VA_ARGS__)

#define OB_TABLE_LOAD_RPC_PROCESS_WITHOUT_ARG(RpcType, pcode, request, result) \
  ({                                                                           \
    typename RpcType<pcode>::ProcessorType processor(request, result);         \
    if (OB_FAIL(processor.execute())) {                                        \
      SERVER_LOG(WARN, "fail to execute", K(ret));                             \
    }                                                                          \
  })

#define OB_TABLE_LOAD_RPC_PROCESS_WITH_ARG(RpcType, pcode, request, result, ...)    \
  ({                                                                                \
    typename RpcType<pcode>::ProcessorType processor(__VA_ARGS__, request, result); \
    if (OB_FAIL(processor.execute())) {                                             \
      SERVER_LOG(WARN, "fail to execute", K(ret));                                  \
    }                                                                               \
  })

#define OB_TABLE_LOAD_RPC_PROCESS_ARG0(RpcType, pcode, request, result) \
  OB_TABLE_LOAD_RPC_PROCESS_WITHOUT_ARG(RpcType, pcode, request, result)
#define OB_TABLE_LOAD_RPC_PROCESS_ARG1(RpcType, pcode, request, result, ...) \
  OB_TABLE_LOAD_RPC_PROCESS_WITH_ARG(RpcType, pcode, request, result, __VA_ARGS__)
#define OB_TABLE_LOAD_RPC_PROCESS_ARG2(RpcType, pcode, request, result, ...) \
  OB_TABLE_LOAD_RPC_PROCESS_WITH_ARG(RpcType, pcode, request, result, __VA_ARGS__)
#define OB_TABLE_LOAD_RPC_PROCESS_ARG3(RpcType, pcode, request, result, ...) \
  OB_TABLE_LOAD_RPC_PROCESS_WITH_ARG(RpcType, pcode, request, result, __VA_ARGS__)
#define OB_TABLE_LOAD_RPC_PROCESS_ARG4(RpcType, pcode, request, result, ...) \
  OB_TABLE_LOAD_RPC_PROCESS_WITH_ARG(RpcType, pcode, request, result, __VA_ARGS__)
#define OB_TABLE_LOAD_RPC_PROCESS_ARG5(RpcType, pcode, request, result, ...) \
  OB_TABLE_LOAD_RPC_PROCESS_WITH_ARG(RpcType, pcode, request, result, __VA_ARGS__)
#define OB_TABLE_LOAD_RPC_PROCESS_ARG6(RpcType, pcode, request, result, ...) \
  OB_TABLE_LOAD_RPC_PROCESS_WITH_ARG(RpcType, pcode, request, result, __VA_ARGS__)

#define OB_TABLE_LOAD_RPC_PROCESS(RpcType, pcode, request, result, ...) \
  CONCAT(OB_TABLE_LOAD_RPC_PROCESS_ARG, ARGS_NUM(__VA_ARGS__))          \
  (RpcType, pcode, request, result, ##__VA_ARGS__)

template <class Rpc>
class ObTableLoadRpcExecutor
{
  typedef typename Rpc::RequestType RequestType;
  typedef typename Rpc::ResultType ResultType;
  typedef typename Rpc::ArgType ArgType;
  typedef typename Rpc::ResType ResType;

public:
  ObTableLoadRpcExecutor(const RequestType &request, ResultType &result)
    : request_(request), result_(result)
  {
  }
  virtual ~ObTableLoadRpcExecutor() = default;
  int execute()
  {
    int ret = OB_SUCCESS;
    if (OB_FAIL(deserialize())) {
    } else if (OB_FAIL(check_args())) {
    } else if (OB_FAIL(process())) {
    } else if (OB_FAIL(set_result_header())) {
    } else if (OB_FAIL(serialize())) {
    }
    return ret;
  }

protected:
  // deserialize arg from request
  virtual int deserialize() = 0;
  virtual int check_args() = 0;
  virtual int process() = 0;
  virtual int set_result_header() = 0;
  // serialize res to result
  virtual int serialize() = 0;

protected:
  const RequestType &request_;
  ResultType &result_;
  ArgType arg_;
  ResType res_;
};

} // namespace observer
} // namespace oceanbase
