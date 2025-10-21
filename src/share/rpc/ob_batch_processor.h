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

#ifndef OCEANBASE_RPC_OB_BATCH_PROCESSOR_H_
#define OCEANBASE_RPC_OB_BATCH_PROCESSOR_H_

#include "rpc/obrpc/ob_rpc_processor.h"
#include "ob_batch_proxy.h"

namespace oceanbase
{
namespace obrpc
{
class ObBatchP : public ObRpcProcessor< obrpc::ObBatchRpcProxy::ObRpc<OB_BATCH> >
{
public:
  ObBatchP() {}
  ~ObBatchP() {}
  int check_timeout() { return common::OB_SUCCESS; }
protected:
  int process();
  int handle_tx_req(int type, const char* buf, int32_t size);
  int handle_log_req(const common::ObAddr& sender, int type, const share::ObLSID &ls_id, const char* buf, int32_t size);
private:
  DISALLOW_COPY_AND_ASSIGN(ObBatchP);
};

}; // end namespace rpc
}; // end namespace oceanbase

#endif /* OCEANBASE_RPC_OB_BATCH_PROCESSOR_H_ */
