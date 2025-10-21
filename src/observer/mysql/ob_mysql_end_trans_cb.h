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

#ifndef SRC_OBSERVER_MYSQL_OB_MYSQL_END_TRANS_CB_CPP_
#define SRC_OBSERVER_MYSQL_OB_MYSQL_END_TRANS_CB_CPP_
#include "sql/ob_i_end_trans_callback.h"
#include "sql/ob_end_trans_cb_packet_param.h"
#include "rpc/obmysql/ob_mysql_packet.h"
#include "rpc/obmysql/ob_mysql_request_utils.h"
#include "lib/allocator/ob_allocator.h"
#include "lib/objectpool/ob_tc_factory.h"
#include "lib/allocator/ob_mod_define.h"
#include "storage/tx/ob_trans_result.h"
#include "rpc/obmysql/ob_2_0_protocol_utils.h"
#include "observer/mysql/obmp_packet_sender.h"


namespace oceanbase
{
namespace sql
{
class ObSQLSessionInfo;
}
namespace obmysql
{
class ObMySQLPacket;
}
namespace rpc
{
class ObRequest;
}
namespace observer
{
struct ObOKPParam;
class ObMySQLResultSet;
struct ObSMConnection;
class ObSqlEndTransCb
{
public:
  ObSqlEndTransCb();
  virtual ~ObSqlEndTransCb();

  void callback(int cb_param);
  int init(ObMPPacketSender& packet_sender, 
           sql::ObSQLSessionInfo *sess_info, 
           int32_t stmt_id = 0,
           uint64_t params_num = 0,
           int64_t com_offset = 0);
  int set_packet_param(const sql::ObEndTransCbPacketParam &pkt_param);
  void destroy();
  void reset();
  void set_need_disconnect(bool need_disconnect) { need_disconnect_ = need_disconnect; }
  void set_stmt_id(int32_t id) { stmt_id_ = id; }
  void set_param_num(uint64_t num) { params_num_ = num; }
  sql::ObSQLSessionInfo* get_sess_info_ptr() { return sess_info_; }

protected:
  ObMPPacketSender packet_sender_;
  sql::ObSQLSessionInfo *sess_info_;
  sql::ObEndTransCbPacketParam pkt_param_;
  bool need_disconnect_;
  int32_t stmt_id_;
  uint64_t params_num_;
private:
  DISALLOW_COPY_AND_ASSIGN(ObSqlEndTransCb);
};
} //end of namespace obmysql
} //end of namespace oceanbase



#endif /* SRC_OBSERVER_MYSQL_OB_MYSQL_END_TRANS_CB_CPP_ */
