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

#ifndef OCEANBASE_RPC_OBRPC_OB_RPC_PROCESSOR_
#define OCEANBASE_RPC_OBRPC_OB_RPC_PROCESSOR_

#include "lib/runtime.h"
#include "rpc/ob_request.h"
#include "rpc/obrpc/ob_rpc_packet.h"
#include "rpc/frame/ob_req_processor.h"
#include "lib/compress/ob_compressor_pool.h"
#include "rpc/obrpc/ob_rpc_processor_base.h"

namespace oceanbase
{
namespace obrpc
{

template <class T>
class ObRpcProcessor : public ObRpcProcessorBase
{
public:
  static constexpr ObRpcPacketCode PCODE = T::PCODE;
public:
  ObRpcProcessor() {}
  virtual ~ObRpcProcessor() {}
  virtual int check_timeout()
  {
    return m_check_timeout();
  }
protected:
  virtual int process() = 0;
  virtual int preprocess_arg() { return common::OB_SUCCESS; }
protected:
  int decode_base(const char *buf, const int64_t len, int64_t &pos)
  {
    return common::serialization::decode(buf, len, pos, arg_);
  }
  int m_get_pcode() { return PCODE; }
  int encode_base(char *buf, const int64_t len, int64_t &pos)
  {
    return common::serialization::encode(buf, len, pos, result_);
  }
  int64_t m_get_encoded_length()
  {
    return common::serialization::encoded_length(result_);
  }
protected:
  typename T::Request arg_;
  typename T::Response result_;
private:
  DISALLOW_COPY_AND_ASSIGN(ObRpcProcessor);
}; // end of class ObRpcProcessor

} // end of namespace observer
} // end of namespace oceanbase

#endif //OCEANBASE_RPC_OBRPC_OB_RPC_PROCESSOR_
