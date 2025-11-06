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

#ifndef OCEANBASE_LOGSERVICE_LOG_REQUEST_HANDLER_
#define OCEANBASE_LOGSERVICE_LOG_REQUEST_HANDLER_

#include "lib/ob_errno.h"                   // OB_SUCCESS...
#include <stdint.h>

namespace oceanbase
{
namespace common
{
class ObAddr;
}
namespace palf
{
class IPalfEnvImpl;
class LogRequestHandler
{
public:
  LogRequestHandler(IPalfEnvImpl *palf_env_impl);
  ~LogRequestHandler();
  template <typename ReqType>
  int handle_request(const int64_t palf_id,
                     const common::ObAddr &server,
                     const ReqType &req);
  template <typename ReqType, typename RespType>
  int handle_sync_request(const int64_t palf_id,
                          const common::ObAddr &server,
                          const ReqType &req,
                          RespType &resp);
private:
  IPalfEnvImpl *palf_env_impl_;
};
} // end namespace palf
} // end namespace oceanbase

#endif
