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
#include "rpc/obmysql/packet/ompk_eof.h"

namespace oceanbase
{

namespace sql
{
struct ObSqlCtx;
class ObSQLSessionInfo;
}


namespace observer
{

class ObIMPPacketSender;
class ObMySQLResultSet;
class ObQueryRetryCtrl;
class ObSyncCmdDriver : public ObQueryDriver
{
public:
  ObSyncCmdDriver(const ObGlobalContext &gctx,
                  const sql::ObSqlCtx &ctx,
                  sql::ObSQLSessionInfo &session,
                  ObQueryRetryCtrl &retry_ctrl,
                  ObIMPPacketSender &sender,
                  bool is_prexecute = false);
  virtual ~ObSyncCmdDriver();

  int send_eof_packet(bool has_more_result);
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
  int check_and_refresh_schema(uint64_t tenant_id);
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

