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

#include "observer/mysql/obmp_process_kill.h"

namespace oceanbase
{
using namespace common;
using namespace obmysql;
using namespace rpc;

namespace observer
{
ObMPProcessKill::ObMPProcessKill(const ObGlobalContext &gctx)
    : ObMPQuery(gctx)
{
}

ObMPProcessKill::~ObMPProcessKill()
{
}

int ObMPProcessKill::deserialize()
{
  int ret = OB_SUCCESS;
  const char *kill_sql_fmt = "/*+TRIGGERED BY COM_PROCESS_INFO*/ KILL %u";
  uint32_t sessid = 0;
  if ( (OB_ISNULL(req_)) || (req_->get_type() != ObRequest::OB_MYSQL)) {
    ret = OB_INVALID_ARGUMENT;
    LOG_ERROR("invalid request", K(ret), K(req_));
  } else {
    const ObMySQLRawPacket &pkt = reinterpret_cast<const ObMySQLRawPacket&>(req_->get_packet());
    sessid = *(reinterpret_cast<const uint32_t *>(pkt.get_cdata()));
    snprintf(kill_sql_buf_, KILL_SQL_BUF_SIZE, kill_sql_fmt, sessid);
    assign_sql(kill_sql_buf_, STRLEN(kill_sql_buf_));
  }
  return ret;
}


} // namespace observer
} // namespace oceanbase
