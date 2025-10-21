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

#include "share/diagnosis/ob_sql_monitor_statname.h"

namespace oceanbase
{
namespace sql
{

namespace sql_monitor_statname {
enum MONITOR_STAT_TYPE {
  INVALID,
  INT,
  CAPACITY,
  TIMESTAMP
};
}

const ObMonitorStat OB_MONITOR_STATS[] = {
#define SQL_MONITOR_STATNAME_DEF(def, type, name, desc) \
  {type, name, desc},
#include "share/diagnosis/ob_sql_monitor_statname.h"
#undef SQL_MONITOR_STATNAME_DEF
};
}
}

