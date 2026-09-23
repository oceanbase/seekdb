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

#ifndef _OBMP_DISCONNECT_H_
#define _OBMP_DISCONNECT_H_

#include "rpc/frame/ob_req_processor.h"
#include "sql/session/ob_sql_session_mgr.h"
namespace oceanbase
{
namespace sql
{
class ObFreeSessionCtx;
}
namespace observer
{

// Before coming into this class, all information about this
// connection maybe invalid.
class ObMPDisconnect
    : public rpc::frame::ObReqProcessor
{
public:
  explicit ObMPDisconnect(const sql::ObFreeSessionCtx &ctx);
  virtual ~ObMPDisconnect();

protected:
  int run();
private:
  int kill_unfinished_session(uint32_t sessid);

private:
  DISALLOW_COPY_AND_ASSIGN(ObMPDisconnect);
  sql::ObFreeSessionCtx ctx_;
}; // end of class ObMPDisconnect

} // end of namespace observer
} // end of namespace oceanbase

#endif /* _OBMP_DISCONNECT_H_ */
