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

#define USING_LOG_PREFIX SERVER

#include "ob_table_direct_load_processor.h"
#include "observer/table_load/ob_table_load_client_service.h"

namespace oceanbase
{
namespace observer
{
using namespace table;

ObTableDirectLoadP::ObTableDirectLoadP(const ObGlobalContext &gctx)
  : ObTableRpcProcessor(gctx),
    exec_ctx_(allocator_)
{
  allocator_.set_attr(ObMemAttr(MTL_ID(), "TbDirectP", ObCtxIds::DEFAULT_CTX_ID));
}

int ObTableDirectLoadP::check_arg()
{
  int ret = OB_SUCCESS;
  if (OB_UNLIKELY(arg_.header_.operation_type_ == ObTableDirectLoadOperationType::MAX_TYPE ||
                  arg_.arg_content_.empty())) {
    ret = OB_INVALID_ARGUMENT;
    LOG_WARN("invalid args", KR(ret), K(arg_));
  }
  return ret;
}

int ObTableDirectLoadP::try_process()
{
  int ret = OB_SUCCESS;
  exec_ctx_.set_tenant_id(credential_.tenant_id_);
  exec_ctx_.set_user_id(credential_.user_id_);
  exec_ctx_.set_database_id(credential_.database_id_);
  exec_ctx_.set_user_client_addr(user_client_addr_);
  if (OB_FAIL(ObTableLoadClientService::direct_load_operate(exec_ctx_, arg_, result_))) {
    LOG_WARN("fail to do direct load operate", KR(ret), K(arg_));
  }
  return ret;
}

ObTableAPITransCb *ObTableDirectLoadP::new_callback(rpc::ObRequest *req)
{
  UNUSED(req);
  return nullptr;
}

uint64_t ObTableDirectLoadP::get_request_checksum()
{
  uint64_t checksum = 0;
  checksum = ob_crc64(checksum, &result_.header_, sizeof(result_.header_));
  checksum = ob_crc64(checksum, result_.res_content_.ptr(), result_.res_content_.length());
  return checksum;
}

} // namespace observer
} // namespace oceanbase
