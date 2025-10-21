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

#ifndef _OB_ELIMINATE_TASK_
#define _OB_ELIMINATE_TASK_

#include "lib/task/ob_timer.h"
#include "lib/allocator/ob_fifo_allocator.h"
#include "sql/monitor/flt/ob_flt_span_mgr.h"

namespace oceanbase
{
namespace obmysql
{

class ObMySQLRequestManager;
class ObEliminateTask : public common::ObTimerTask
{
public:
  ObEliminateTask();
  virtual ~ObEliminateTask();

  void runTimerTask();
  int init(const ObMySQLRequestManager *request_manager);
  int check_config_mem_limit(bool &is_change);
  int calc_evict_mem_level(int64_t &low, int64_t &high);

private:
  ObMySQLRequestManager *request_manager_;
  int64_t config_mem_limit_;
  sql::ObFLTSpanMgr* flt_mgr_;
  bool is_tp_trigger_;
};

} // end of namespace obmysql
} // end of namespace oceanbase
#endif
