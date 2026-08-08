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

#ifndef OCEANBASE_SHARE_RPC_OB_SERVER_TASK_H_
#define OCEANBASE_SHARE_RPC_OB_SERVER_TASK_H_

#include "rpc/ob_request.h"

namespace oceanbase
{
namespace rpc
{

class ObSrvTask : public ObRequest
{
public:
  ObSrvTask()
      : ObRequest(ObRequest::OB_TASK)
  {}

  virtual frame::ObReqProcessor &get_processor() = 0;
};

} // namespace rpc
} // namespace oceanbase

#endif // OCEANBASE_SHARE_RPC_OB_SERVER_TASK_H_
