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

#ifndef OCEANBASE_OBSERVER_MYSQL_SYNC_CMD_DRIVER_
#define OCEANBASE_OBSERVER_MYSQL_SYNC_CMD_DRIVER_

#include "observer/mysql/ob_query_driver.h"
#include "query/protocol/ob_client_protocol.h"
#include "rpc/obmysql/packet/ompk_eof.h"

namespace oceanbase
{

namespace sql
{
struct ObSqlCtx;
class ObSQLSessionInfo;
class ObQueryRetryCtrl;
}


namespace observer
{

class ObMPPacketSender;
class ObMySQLResultSet;
class ObSyncCmdDriver : public ObQueryDriver, public sql::ObIQueryResultSender
{
public:
  ObSyncCmdDriver(const share::ObGlobalContext &gctx,
                  const sql::ObSqlCtx &ctx,
                  sql::ObSQLSessionInfo &session,
                  sql::ObQueryRetryCtrl &retry_ctrl,
                  ObMPPacketSender &sender);
  virtual ~ObSyncCmdDriver();

  int send_eof_packet(bool has_more_result) override;
  sql::ObIClientPacketChannel &get_packet_sender() override;
  int seal_eof_packet(bool has_more_result, obmysql::OMPKEOF& eofp);
  virtual int response_query_result(sql::ObResultSet &result,
                                    bool is_ps_protocol,
                                    bool has_more_result,
                                    bool &can_retry,
                                    int64_t fetch_limit  = common::OB_INVALID_COUNT);
  virtual int response_result(ObMySQLResultSet &result);

private:
  /* functions */
  int process_schema_version_changes(const ObMySQLResultSet &result);
  int response_query_result(ObMySQLResultSet &result);
  void free_output_row(ObMySQLResultSet &result);
  /* variables */
  /* const */
  /* disallow copy & assign */
  DISALLOW_COPY_AND_ASSIGN(ObSyncCmdDriver);
};


}
}
#endif /* OCEANBASE_OBSERVER_MYSQL_SYNC_CMD_DRIVER_ */
//// end of header file
