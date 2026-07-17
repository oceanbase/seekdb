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

#ifndef OCEANBASE_SQL_EXECUTOR_OB_EXECUTOR_RPC_PROCESSOR_
#define OCEANBASE_SQL_EXECUTOR_OB_EXECUTOR_RPC_PROCESSOR_

#include "observer/virtual_table/ob_virtual_table_iterator_factory.h"
#include "observer/ob_server_struct.h"
#include "sql/monitor/ob_phy_plan_monitor_info.h"
#include "sql/engine/ob_physical_plan.h"
#include "sql/engine/ob_des_exec_context.h"
#include "sql/ob_sql_trans_control.h"
#include "share/schema/ob_schema_getter_guard.h"

namespace oceanbase
{
namespace share
{
namespace schema
{
}
}
namespace sql
{
class ObExecContext;
class ObPhysicalPlan;

class ObWorkerSessionGuard
{
public:
  ObWorkerSessionGuard(ObSQLSessionInfo *session);
  ~ObWorkerSessionGuard();
};

}
}
#endif /* OCEANBASE_SQL_EXECUTOR_OB_EXECUTOR_RPC_PROCESSOR_ */
