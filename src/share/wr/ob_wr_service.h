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

#ifndef OCEANBASE_WR_OB_WORKLOAD_REPOSITORY_SERVICE_H_
#define OCEANBASE_WR_OB_WORKLOAD_REPOSITORY_SERVICE_H_

#include "logservice/ob_log_base_type.h"
#include "share/wr/ob_wr_task.h"

namespace oceanbase
{
namespace share
{

class ObWorkloadRepositoryContext {
public:
  ObWorkloadRepositoryContext(): is_inited_(false), mutex_() {}
  static int mtl_init(ObWorkloadRepositoryContext* &ptr);
  void destroy();
  int try_lock();
  int lock(const int64_t abs_timeout_us = INT64_MAX);
  void release_lock();
private:
  bool is_inited_;
  lib::ObMutex mutex_;
};

class ObWorkloadRepositoryService : public logservice::ObIReplaySubHandler,
                                    public logservice::ObIRoleChangeSubHandler,
                                    public logservice::ObICheckpointSubHandler
{
public:
  ObWorkloadRepositoryService();
  virtual ~ObWorkloadRepositoryService() {};
  DISABLE_COPY_ASSIGN(ObWorkloadRepositoryService);
  // used for ObIRoleChangeSubHandler
  virtual void switch_to_follower_forcedly() override final;
  virtual int switch_to_leader() override final;
  virtual int switch_to_follower_gracefully() override final;
  virtual int resume_leader() override final;
  // for replay, do nothing
  virtual int replay(const void *buffer,
                     const int64_t nbytes,
                     const palf::LSN &lsn,
                     const share::SCN &scn) override final;
  // for checkpoint, do nothing
  virtual share::SCN get_rec_scn() override final;
  virtual int flush(share::SCN &scn) override final;
  int cancel_current_task();
  int schedule_new_task(const int64_t interval);
  bool is_running_task() const {return wr_timer_task_.is_running_task();};
  int64_t get_snapshot_interval(bool is_laze_load = true) {return wr_timer_task_.get_snapshot_interval(is_laze_load);};
  // when is_lazy_load is false, the func will send inner SQL, avoid frequently calling
  WorkloadRepositoryTask& get_wr_timer_task() {return wr_timer_task_;};
  INHERIT_TO_STRING_KV("ObIRoleChangeSubHandler", ObIRoleChangeSubHandler,
                        K_(is_inited),
                        K_(wr_timer_task));
  // used for ObServer class
  int init();
  int start();
  void stop();
  void wait();
  void destroy();
  
private:
  int inner_switch_to_leader();
  int inner_switch_to_follower();
  bool is_inited_;
  WorkloadRepositoryTask wr_timer_task_;
};

}  // end namespace share
}  // end namespace oceanbase

#endif //OCEANBASE_WR_OB_WORKLOAD_REPOSITORY_SERVICE_H_
