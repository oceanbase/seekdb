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

// common scheduled-job utility(extracted from q_stat's ObDbmsStatsMaintenanceWindow:
// time-zone offset calculation and job-existence checks are independent of statistics,normal share consumers should not depend on the isolated zone)
#ifndef OCEANBASE_SHARE_OB_SCHEDULED_JOB_UTILS_H_
#define OCEANBASE_SHARE_OB_SCHEDULED_JOB_UTILS_H_

#include "common/mysqlclient/ob_mysql_proxy.h"
#include "share/schema/ob_schema_struct.h"

// scheduling-window time vocabulary(same text as the q_stat window header; redefining the same-value macro is legal)
#define USEC_OF_HOUR (60 * 60 * 1000000LL)

namespace oceanbase
{
namespace share
{

class ObScheduledJobUtils
{
public:
  static int get_time_zone_offset(const share::schema::ObSysVariableSchema &sys_variable,
                                  int32_t &offset_sec);
  static int check_job_exists(common::ObMySQLProxy *sql_proxy,
                              const char* job_name,
                              bool &is_join_exists);
};

} // namespace share
} // namespace oceanbase
#endif
