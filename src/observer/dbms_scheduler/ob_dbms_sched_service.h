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

#ifndef OCEANBASE_ROOTSERVER_OB_DBMS_SCHEDULER_SERVICE_H
#define OCEANBASE_ROOTSERVER_OB_DBMS_SCHEDULER_SERVICE_H

#include "share/ob_define.h"
#include "logservice/ob_log_base_type.h"                        //ObILocalLogHandler ObICheckpointSubHandler ObIReplaySubHandler
#include "observer/dbms_scheduler/ob_dbms_sched_job_master.h"
#include "rootserver/ob_server_thread_helper.h" // for ObServerThreadHelper
#include "share/ob_background_task_executor.h"

namespace oceanbase
{
namespace rootserver
{
class ObDBMSSchedService : public ObServerThreadHelper,
                           public logservice::ObICheckpointSubHandler,
                           public logservice::ObIReplaySubHandler,
                           public share::ObIBackgroundTaskSource
{
public:
  ObDBMSSchedService()
      : job_master_(),
        use_shared_executor_(false),
        background_executor_(NULL),
        source_handle_()
  {}
  virtual ~ObDBMSSchedService()
  {
    destroy();
  }

  static int server_module_init(ObDBMSSchedService *&dbms_sched_service);
  static void wakeup_scheduler();
  int init();
  int start();
  virtual void do_work() override;
  int process_one_quantum(
      const share::ObBackgroundTaskPriority priority,
      share::ObBackgroundTaskRunResult &result) override;
  void stop();
  void wait();
  void destroy();
  bool is_leader() { return job_master_.is_leader(); }
  bool is_stop() { return job_master_.is_stop(); }

public:
  // for replay, do nothing
  int replay(const void *buffer, const int64_t nbytes, const palf::LSN &lsn, const share::SCN &scn) override
  {
    UNUSED(buffer);
    UNUSED(nbytes);
    UNUSED(lsn);
    UNUSED(scn);
    return OB_SUCCESS;
  }
  // for checkpoint, do nothing
  virtual share::SCN get_rec_scn() override
  {
    return share::SCN::max_scn();
  }
  virtual int flush(share::SCN &scn) override
  {
    return OB_SUCCESS;
  }

  // for role change
  void deactivate() override;
  int activate() override;

private:
  int register_background_source_();
  int unregister_background_source_(const bool wait_running);
  int notify_background_source_();

  dbms_scheduler::ObDBMSSchedJobMaster job_master_;
  bool use_shared_executor_;
  share::ObBackgroundTaskExecutor *background_executor_;
  share::ObBackgroundTaskSourceHandle source_handle_;
};
}  // namespace rootserver
}  // namespace oceanbase

#endif /* !OCEANBASE_ROOTSERVER_OB_DBMS_SCHEDULER_SERVICE_H */
