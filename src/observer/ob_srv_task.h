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

#ifndef _OCEABASE_OBSERVER_OB_SRV_TASK_H_
#define _OCEABASE_OBSERVER_OB_SRV_TASK_H_

#include "rpc/ob_request.h"
#include "observer/mysql/obmp_disconnect.h"

namespace oceanbase
{

namespace common
{
class ObDiagnosticInfo;
}

namespace sql
{
class ObFreeSessionCtx;
}
namespace observer
{

class ObSrvTask
    : public rpc::ObRequest
{
public:
  ObSrvTask()
      : ObRequest(ObRequest::OB_TASK)
  {}

  virtual rpc::frame::ObReqProcessor &get_processor() = 0;
}; // end of class ObSrvTask

class ObDisconnectTask
    : public ObSrvTask
{
public:
  ObDisconnectTask(const sql::ObFreeSessionCtx &ctx)
       : proc_(ctx)
  {
  }

  virtual rpc::frame::ObReqProcessor &get_processor()
  {
    return proc_;
  }

private:
  ObMPDisconnect proc_;
}; // end of class ObDisconnectTAsk


} // end of namespace observer
} // end of namespace oceanbase

#endif /* _OCEABASE_OBSERVER_OB_SRV_TASK_H_ */
