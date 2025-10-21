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
#include "logservice/ob_log_base_type.h"                        //ObIRoleChangeSubHandler ObICheckpointSubHandler ObIReplaySubHandler
#include "observer/dbms_scheduler/ob_dbms_sched_job_master.h"
#include "rootserver/ob_primary_ls_service.h" // ObTenantThreadHelper

namespace oceanbase
{
namespace rootserver
{
class ObDBMSSchedService : public ObTenantThreadHelper,
                           public logservice::ObICheckpointSubHandler,
                           public logservice::ObIReplaySubHandler
{
public:
  ObDBMSSchedService()
      : tenant_id_(OB_INVALID_TENANT_ID),
        job_master_()
  {}
  virtual ~ObDBMSSchedService()
  {
    destroy();
  }

  static int mtl_init(ObDBMSSchedService *&dbms_sched_service);
  int init();
  int start();
  virtual void do_work() override;
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
  virtual void switch_to_follower_forcedly() override;
  virtual int switch_to_leader() override;
  virtual int switch_to_follower_gracefully() override;
  virtual int resume_leader() override;

private:
  uint64_t tenant_id_;
  dbms_scheduler::ObDBMSSchedJobMaster job_master_;
};
}  // namespace rootserver
}  // namespace oceanbase

#endif /* !OCEANBASE_ROOTSERVER_OB_DBMS_SCHEDULER_SERVICE_H */
