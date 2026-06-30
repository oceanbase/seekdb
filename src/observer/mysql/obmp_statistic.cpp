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

#include "observer/mysql/obmp_statistic.h"
#include "rpc/obmysql/packet/ompk_string.h"

namespace oceanbase
{
using namespace common;
using namespace obmysql;

namespace observer
{
int ObMPStatistic::process()
{
  int ret = common::OB_SUCCESS;
  bool need_disconnect = true;
  bool need_response_error = true;
  //Attention::it is BUG when using like followers (build with release):
  //  obmysql::OMPKString pkt(ObString("Active threads not support"));
  //
  const common::ObString tmp_string("Active threads not support");
  obmysql::OMPKString pkt(tmp_string);
  ObSMConnection *conn = NULL;
  const ObMySQLRawPacket &mysql_pkt = reinterpret_cast<const ObMySQLRawPacket&>(req_->get_packet());

  if (OB_FAIL(packet_sender_.alloc_ezbuf())) {
  } else if (OB_FAIL(packet_sender_.update_last_pkt_pos())) {
  } else if (OB_ISNULL(conn = get_conn())) {
    ret = OB_ERR_UNEXPECTED;
    LOG_WARN("get connection fail", K(conn), K(ret));
  } else if (conn->proxy_cap_flags_.is_extra_ok_packet_for_statistics_support()) {
    sql::ObSQLSessionInfo *session = NULL;
    if (OB_FAIL(get_session(session))) {
    } else if (OB_ISNULL(session)) {
      ret = OB_ERR_UNEXPECTED;
      LOG_WARN("sql session info is null", K(ret));
    } else if (OB_FAIL(process_kill_client_session(*session))) {
    } else if (OB_FAIL(process_extra_info(*session, mysql_pkt, need_response_error))) {
    } else if (OB_FAIL(update_transmission_checksum_flag(*session))) {
    } else {
      ObOKPParam ok_param; // use default values
      if (OB_FAIL(send_ok_packet(*session, ok_param, &pkt))) {
      }
    }
    if (OB_LIKELY(NULL != session)) {
      revert_session(session);
    }
  } else if (OB_FAIL(response_packet(pkt, NULL))) {
  } else {
    // do nothing
  }

  if (OB_FAIL(ret) && need_response_error) {
    send_error_packet(ret, NULL);
  }
  if (OB_FAIL(ret) && need_disconnect) {
    force_disconnect();
    LOG_WARN("disconnect connection", KR(ret));
  }
  return ret;
}


} // namespace observer
} // namespace oceanbase
