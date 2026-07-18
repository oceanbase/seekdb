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

#include "observer/mysql/obmp_debug.h"
#include "observer/mysql/ob_mysql_result_set.h"


namespace oceanbase
{
using namespace common;
using namespace obmysql;

namespace observer
{
ObMPDebug::ObMPDebug(const ObGlobalContext &gctx)
    : ObMPBase(gctx)
{
}

ObMPDebug::~ObMPDebug()
{
}

int ObMPDebug::deserialize()
{
  int ret = 0;
  return OB_SUCCESS;
}

int ObMPDebug::process()
{
  int ret = OB_SUCCESS;
  sql::ObSQLSessionInfo *session = NULL;
  bool need_response_error = true; //temporary placeholder
  const ObMySQLRawPacket &pkt = reinterpret_cast<const ObMySQLRawPacket&>(req_->get_packet());
  if (OB_FAIL(get_session(session))) {
    LOG_WARN("get session fail", K(ret));
  } else if (OB_ISNULL(session)) {
    ret = OB_ERR_UNEXPECTED;
    LOG_WARN("sql session info is null", K(ret));
  } else if (OB_FAIL(process_extra_info(*session, pkt, need_response_error))) {
    LOG_WARN("fail get process extra info", K(ret));
  } else if (OB_FAIL(update_transmission_checksum_flag(*session))) {
    LOG_WARN("update transmisson checksum flag failed", K(ret));
  } else if (FALSE_IT(session->update_last_active_time())) {
  } else {
    ObArenaAllocator allocator; // no use, just a param for ObMySQLResultSet()
    SMART_VAR(ObMySQLResultSet, result, *session, allocator) {// use default values
      if (OB_FAIL(send_eof_packet(*session, result))) {
        LOG_WARN("fail to send eof pakcet in debug response",  K(ret));
      }
    }
  }
  if (OB_LIKELY(NULL != session)) {
    revert_session(session);
  }
  if (OB_FAIL(ret)) {
    if (OB_FAIL(send_error_packet(ret, NULL))) { // overwrite ret ?
      OB_LOG(WARN,"response debug packet fail", K(ret));
    }
  }
  return ret;
}

} // namespace observer
} // namespace oceanbase
