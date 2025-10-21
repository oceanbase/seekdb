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

#ifndef OCEANBASE_OBSERVER_OB_VIRTUAL_SQL_MONITOR_H
#define OCEANBASE_OBSERVER_OB_VIRTUAL_SQL_MONITOR_H
#include "share/ob_virtual_table_projector.h"
#include "lib/container/ob_se_array.h"
#include "common/ob_range.h"
#include "observer/mysql/ob_ra_queue.h"

namespace oceanbase
{
namespace share
{
namespace schema
{
class ObTableSchema;
class ObMultiVersionSchemaService;
}
}
namespace sql
{
class ObMonitorInfoManager;
class ObPhyPlanMonitorInfo;
}
namespace observer
{
class ObVirtualSqlMonitor : public common::ObVirtualTableProjector
{
public:
  ObVirtualSqlMonitor();
  virtual ~ObVirtualSqlMonitor();
  int inner_open();
  virtual int inner_get_next_row(common::ObNewRow *&row);
  virtual void reset();
  void set_tenant_id(int64_t tenant_id) { tenant_id_ = tenant_id; }
private:
enum COLUMN_ID
  {
    TENANT_ID = common::OB_APP_MIN_COLUMN_ID,
    SVR_IP,
    SVR_PORT,
    REQUEST_ID,
    JOB_ID,
    TASK_ID,
    PLAN_ID,
    SCHEDULER_IP,
    SCHEDULER_PORT,
    MONITOR_INFO,
    EXTEND_INFO,
    SQL_EXEC_START,
  };

  DISALLOW_COPY_AND_ASSIGN(ObVirtualSqlMonitor);
  int get_next_monitor_info();
  static const int64_t OB_MAX_INFO_LENGTH = 1024;
private:
  sql::ObMonitorInfoManager *monitor_manager_;
  int64_t start_id_;
  int64_t end_id_;
  common::ObRaQueue::Ref ref_;
  sql::ObPhyPlanMonitorInfo *plan_info_;
  int64_t tenant_id_;
  int64_t request_id_;
  int64_t plan_id_;
  char scheduler_ipstr_[common::OB_IP_STR_BUFF];
  int32_t scheduler_port_;
  common::ObString ipstr_;
  int32_t port_;
  char info_buf_[OB_MAX_INFO_LENGTH];
  // according to the schema, the length of extend_info is
  // OB_MAX_MONITOR_INFO_LENGTH (i.e. 65535),
  // but __all_virtual_sql_monitor is deprecated since 4.0
  char extend_info_buf_[OB_MAX_MONITOR_INFO_LENGTH + 1];
  int64_t execution_time_;
};
} //namespace observer
} //namespace oceanbase
#endif
