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
#include "observer/dbms_scheduler/ob_dbms_sched_job_master.h"
#include "rootserver/ob_server_thread_helper.h" // for ObServerThreadHelper
#include "query/scheduler/ob_scheduler_service.h"

namespace oceanbase
{
namespace rootserver
{
class ObDBMSSchedService : public ObServerThreadHelper,
                           public query::ObISchedulerService
{
public:
  ObDBMSSchedService()
      : job_master_()
  {}
  virtual ~ObDBMSSchedService()
  {
    destroy();
  }

  static void wakeup_scheduler(uint64_t namespace_id);
  int allocate_job_id(int64_t &job_id) override;
  int create_job(
      common::ObISQLClient &sql_client,
      int64_t job_id,
      const dbms_scheduler::ObDBMSSchedJobInfo &job_info) override;
  void notify_scheduler() override { job_master_.wakeup(); }
  int init(common::ObMySQLProxy &sql_proxy,
           share::schema::ObMultiVersionSchemaService &schema_service);
  int start();
  virtual void do_work() override;
  void stop();
  void wait();
  void destroy();
  bool is_leader() { return job_master_.is_leader(); }
  bool is_stop() { return job_master_.is_stop(); }

public:
  void deactivate();
  int activate();

private:
  dbms_scheduler::ObDBMSSchedJobMaster job_master_;
};
}  // namespace rootserver
}  // namespace oceanbase

#endif /* !OCEANBASE_ROOTSERVER_OB_DBMS_SCHEDULER_SERVICE_H */
