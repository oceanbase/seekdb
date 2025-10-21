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

#ifndef OCEANBASE_OBSERVER_MYSQL_SYNC_PLAN_DRIVER_
#define OCEANBASE_OBSERVER_MYSQL_SYNC_PLAN_DRIVER_

#include "observer/mysql/ob_query_driver.h"
#include "common/object/ob_object.h"

namespace oceanbase
{

namespace sql
{
struct ObSqlCtx;
class ObSQLSessionInfo;
class ObPhysicalPlan;
class ObExecContext;
}

namespace observer
{

class ObIMPPacketSender;
class ObMySQLResultSet;
class ObQueryRetryCtrl;
class ObSyncPlanDriver : public ObQueryDriver
{
public:
  ObSyncPlanDriver(const ObGlobalContext &gctx,
                   const sql::ObSqlCtx &ctx,
                   sql::ObSQLSessionInfo &session,
                   ObQueryRetryCtrl &retry_ctrl,
                   ObIMPPacketSender &sender,
                   bool is_prexecute = false,
                   int32_t iteration_count = common::OB_INVALID_COUNT);
  virtual ~ObSyncPlanDriver();

  virtual int response_result(ObMySQLResultSet &result);
protected:
  int enter_query_admission(sql::ObSQLSessionInfo &session,
                            sql::ObExecContext &exec_ctx,
                            sql::ObPhysicalPlan &plan,
                            int64_t &worker_count);
  void exit_query_admission(int64_t worker_count);

  /* disallow copy & assign */
  int32_t iteration_count_;
  DISALLOW_COPY_AND_ASSIGN(ObSyncPlanDriver);
};
}
}
#endif /* OCEANBASE_OBSERVER_MYSQL_SYNC_PLAN_DRIVER_ */
//// end of header file
