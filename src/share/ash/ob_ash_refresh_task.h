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

#ifndef _OB_SHARE_ASH_REFRESH_TASK_H_
#define _OB_SHARE_ASH_REFRESH_TASK_H_

#include "lib/task/ob_timer.h"
#include "share/wr/ob_wr_snapshot_rpc_processor.h"

namespace oceanbase
{
namespace share
{

class ObAshRefreshTask : public common::ObTimerTask
{
public:
  ObAshRefreshTask(): is_inited_(false), last_scheduled_snapshot_time_(OB_INVALID_TIMESTAMP), prev_write_pos_(0), prev_sched_time_(0) {}
  virtual ~ObAshRefreshTask() = default;
  static ObAshRefreshTask &get_instance();
  int start();
  virtual void runTimerTask() override;
private:
  bool require_snapshot_ahead();
  bool check_tenant_can_do_wr_task(uint64_t tenant_id);
  obrpc::ObWrRpcProxy wr_proxy_;
  bool is_inited_;
  int64_t last_scheduled_snapshot_time_;
  int64_t prev_write_pos_;
  int64_t prev_sched_time_;

};
}
}
#endif /* _OB_SHARE_ASH_REFRESH_TASK_H_ */
//// end of header file
