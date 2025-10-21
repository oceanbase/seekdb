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

#ifndef OCEANBASE_RPC_OBRPC_OB_RPC_RESULT_CODE_
#define OCEANBASE_RPC_OBRPC_OB_RPC_RESULT_CODE_

#include "lib/ob_define.h"
#include "lib/utility/ob_unify_serialize.h"
#include "lib/utility/ob_print_utils.h"
#include "lib/container/ob_se_array.h"
#include "lib/oblog/ob_warning_buffer.h"
namespace oceanbase
{
namespace obrpc
{

struct ObRpcResultCode
{
  OB_UNIS_VERSION(1);

public:
  ObRpcResultCode() : rcode_(0)
  {
    msg_[0] = '\0';
    warnings_.reset();
  }

  TO_STRING_KV("code", rcode_, "msg", msg_, K_(warnings));

  void reset()
  {
    rcode_ = 0;
    msg_[0] = '\0';
    warnings_.reset();
  }

  int32_t rcode_;
  char msg_[common::OB_MAX_ERROR_MSG_LEN];
  common::ObSEArray<common::ObWarningBuffer::WarningItem, 1> warnings_;
};

} // end of namespace obrpc
} // end of namespace oceanbase

#endif //OCEANBASE_RPC_OBRPC_OB_RPC_RESULT_CODE_
