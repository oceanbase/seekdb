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

#ifndef OB_DTL_RPC_PROCESSOR_H
#define OB_DTL_RPC_PROCESSOR_H

#include "rpc/obrpc/ob_rpc_processor.h"
#include "sql/dtl/ob_dtl_rpc_proxy.h"
#include "rpc/obrpc/ob_rpc_proxy.h"
#include "ob_dtl_interm_result_manager.h"

namespace oceanbase {
namespace sql {
namespace dtl {

class ObDtlLinkedBuffer;
class ObDtlChannel;

class ObDtlSendMessageP
    : public obrpc::ObRpcProcessor< obrpc::ObDtlRpcProxy::ObRpc<obrpc::OB_DTL_SEND> >
{
public:
  virtual int process() final;
  static int process_msg(ObDtlRpcDataResponse &response, ObDtlSendArgs &args);
  static int process_interm_result(ObDtlSendArgs &arg);
  static int process_interm_result_inner(ObDtlLinkedBuffer &buffer,
                                        ObDTLIntermResultKey &key,
                                        int64_t start_pos,
                                        int64_t length,
                                        int64_t rows,
                                        bool is_eof,
                                        bool append_whole_block);
private:
  static int process_px_bloom_filter_data(ObDtlLinkedBuffer *&buffer);
};

class ObDtlBCSendMessageP
    : public obrpc::ObRpcProcessor< obrpc::ObDtlRpcProxy::ObRpc<obrpc::OB_DTL_BC_SEND> >
{
public:
  virtual int process() final;
};

}  // dtl
}  // sql
}  // oceanbase

#endif /* OB_DTL_RPC_PROCESSOR_H */
