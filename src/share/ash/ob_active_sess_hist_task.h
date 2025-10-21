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

#ifndef _OB_SHARE_ASH_ACTIVE_SESSION_TASK_H_
#define _OB_SHARE_ASH_ACTIVE_SESSION_TASK_H_

#include "lib/task/ob_timer.h"
#include "sql/session/ob_sql_session_mgr.h"
#include "observer/net/ob_net_queue_traver.h"
#include "share/wr/ob_wr_snapshot_rpc_processor.h"

namespace oceanbase
{
namespace share
{

using ObNetInfo = rpc::ObNetTraverProcessAutoDiag::ObNetQueueTraRes;

class ObActiveSessHistTask : public common::ObTimerTask
{
public:
  ObActiveSessHistTask()
      : is_inited_(false), sample_time_(OB_INVALID_TIMESTAMP), tsc_sample_time_(0) {}
  virtual ~ObActiveSessHistTask() = default;
  static ObActiveSessHistTask &get_instance();
  int start();
  void stop();
  void wait();
  void destroy();
  virtual void runTimerTask() override;
private:
  bool process_running_di(const SessionID &session_id, ObDiagnosticInfo *di);
  bool is_inited_;
  int64_t sample_time_;
  int64_t tsc_sample_time_;
};

}
}
#endif /* _OB_SHARE_ASH_ACTIVE_SESSION_TASK_H_ */
//// end of header file
