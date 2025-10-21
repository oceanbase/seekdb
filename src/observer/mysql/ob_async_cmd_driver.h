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

#ifndef OCEANBASE_OBSERVER_MYSQL_ASYNC_CMD_DRIVER_
#define OCEANBASE_OBSERVER_MYSQL_ASYNC_CMD_DRIVER_

#include "observer/mysql/ob_query_driver.h"

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
class ObAsyncCmdDriver : public ObQueryDriver
{
public:
  ObAsyncCmdDriver(const ObGlobalContext &gctx,
                  const sql::ObSqlCtx &ctx,
                  sql::ObSQLSessionInfo &session,
                  ObQueryRetryCtrl &retry_ctrl,
                  ObIMPPacketSender &sender,
                  bool is_prexecute = false);
  virtual ~ObAsyncCmdDriver();

  virtual int response_result(ObMySQLResultSet &result);

private:
  /* disallow copy & assign */
  DISALLOW_COPY_AND_ASSIGN(ObAsyncCmdDriver);
};


}
}
#endif /* OCEANBASE_OBSERVER_MYSQL_ASYNC_CMD_DRIVER_ */
//
