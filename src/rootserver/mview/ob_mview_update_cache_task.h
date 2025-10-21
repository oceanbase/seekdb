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

#pragma once

#include "lib/task/ob_timer.h"
#include "rootserver/mview/ob_mview_timer_task.h"
#include "lib/container/ob_iarray.h"
#include "lib/hash/ob_hashmap.h" //ObHashMap
#include "src/sql/session/ob_sql_session_info.h" // ObSQLSessionInfo
#include "src/sql/engine/expr/ob_expr_last_refresh_scn.h"
#include "lib/atomic/ob_atomic.h"

namespace oceanbase
{

namespace rootserver
{
class ObMViewMaintenanceService;

class ObMviewUpdateCacheTask : public ObMViewTimerTask
{
public:
  static const uint64_t TaskDelay = 5 * 1000 * 1000; // 5s
public:
  ObMviewUpdateCacheTask();
  virtual ~ObMviewUpdateCacheTask();
  int get_mview_refresh_scn_sql(const int refresh_mode, ObSqlString &sql);
  int init();
  int start();
  void stop();
  void wait();
  void destroy();
  void clean_up();
  void runTimerTask() override;
  DISABLE_COPY_ASSIGN(ObMviewUpdateCacheTask);
  int extract_sql_result(sqlclient::ObMySQLResult *mysql_result,
                         ObIArray<uint64_t> &mview_ids,
                         ObIArray<uint64_t> &last_refresh_scns,
                         ObIArray<uint64_t> &mview_refresh_modes);
private:
  bool is_inited_;
  bool is_stop_;
  bool in_sched_;
};


} // namespace rootserver
} // namespace oceanbase
