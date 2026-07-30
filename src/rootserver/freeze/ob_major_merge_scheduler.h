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

#ifndef OCEANBASE_ROOTSERVER_FREEZE_OB_MERGE_SCHEDULER_H_
#define OCEANBASE_ROOTSERVER_FREEZE_OB_MERGE_SCHEDULER_H_

#include "lib/lock/ob_mutex.h"

#include "share/ob_background_task_executor.h"
#include "share/ob_merge_info.h"
#include "rootserver/ob_thread_idling.h"
#include "rootserver/freeze/ob_major_merge_progress_checker.h"
#include "rootserver/freeze/ob_checksum_validator.h"
#include "rootserver/freeze/ob_freeze_reentrant_thread.h"

namespace oceanbase
{
namespace share
{
namespace schema
{
class ObMultiVersionSchemaService;
}
}
namespace common
{
class ObServerConfig;
};

namespace rootserver
{
class ObGlobalMergeManager;
class ObMajorMergeInfoManager;
class ObMajorMergeScheduler;

class ObMajorMergeIdling : public ObThreadIdling
{
public:
  ObMajorMergeIdling(
      volatile bool &stop,
      ObMajorMergeScheduler &scheduler)
    : ObThreadIdling(stop),
      scheduler_(scheduler)
  {}
  int init();
  virtual void wakeup() override;
  virtual int64_t get_idle_interval_us() override;

public:
  const static int64_t DEFAULT_SCHEDULE_IDLE_US = 10 * 60 * 1000L * 1000L; // 10m

private:
  ObMajorMergeScheduler &scheduler_;
};

class ObMajorMergeScheduler
    : public ObFreezeReentrantThread,
      public share::ObIBackgroundTaskSource
{
public:
  ObMajorMergeScheduler();
  virtual ~ObMajorMergeScheduler();

  int init(const bool is_primary_service,
           ObMajorMergeInfoManager &merge_info_mgr,
           share::schema::ObMultiVersionSchemaService &schema_service,
           common::ObServerConfig &config,
           common::ObMySQLProxy &sql_proxy);

  virtual int start() override;
  virtual void stop() override;
  virtual void wait() override;
  int destroy();
  virtual void pause() override;
  virtual void resume() override;
  virtual void run3() override;
  virtual int process_one_quantum(
      const share::ObBackgroundTaskPriority priority,
      share::ObBackgroundTaskRunResult &result) override;

  virtual int blocking_run() override { BLOCKING_RUN_IMPLEMENT(); }

  ObMajorMergeIdling &get_major_scheduler_idling() { return idling_; }

  int try_update_epoch_and_reload();
  int get_uncompacted_tablets(
    common::ObArray<share::ObTabletRuntimeInfo> &uncompacted_tablets,
    common::ObArray<uint64_t> &uncompacted_table_ids) const;

protected:
  virtual int try_idle(const int64_t ori_idle_time_us, const int work_ret) override;

private:
  int do_work();
  int do_work_one_quantum(bool &merge_in_progress);

  int do_before_major_merge(const bool start_merge);
  int do_one_round_major_merge();
  int do_one_round_major_merge_step(bool &merge_finished);

  int generate_next_global_broadcast_scn();

  int update_merge_status(
    const share::SCN &global_broadcast_scn);
  int handle_merge_progress(const compaction::ObBasicMergeProgress &progress,
                            const share::SCN &global_broadcast_scn);
  int try_update_global_merged_scn();

  // including tablets about can_not_read index and permanent offline server
  int update_all_tablets_report_scn(const uint64_t global_broadcast_scn_val);

  void check_merge_interval_time(const bool is_merging);
  int64_t update_fail_count_and_get_idle_time(
      const int64_t ori_idle_time_us,
      const int work_ret);
  int process_paused_quantum(
      share::ObBackgroundTaskRunResult &result);
  int notify_background_source_();
  int unregister_background_source_(const bool wait_running);
private:
  const static int64_t DEFAULT_IDLE_US = 10 * 1000L * 1000L; // 10s
  const static int64_t IN_MERGE_IDLE_US = 1 * 1000L * 1000L; // 1s
  static const int64_t MAJOR_MERGE_SCHEDULER_THREAD_CNT = 1;
  static const int64_t ADD_EVENT_INTERVAL = 10L * 60 * 1000 * 1000;  // record every 10 minutes
  const static int64_t PAUSED_WAITING_CLEAR_MEMORY_THRESHOLD = 30L * 60 * 1000 * 1000; // 30 mins

  bool is_inited_;
  bool is_primary_service_;  // identify ObMajorFreezeServiceType::SERVICE_TYPE_PRIMARY
  bool use_shared_executor_;
  bool shared_merge_active_;
  bool paused_cache_cleared_;
  int64_t fail_count_;
  int64_t first_check_merge_us_;
  int64_t paused_since_us_;

  mutable lib::ObMutex epoch_update_lock_;
  mutable ObMajorMergeIdling idling_;

  ObMajorMergeInfoManager *merge_info_mgr_;
  common::ObServerConfig *config_;
  common::ObMySQLProxy *sql_proxy_;
  ObBasicMergeProgressChecker *progress_checker_;
  share::ObBackgroundTaskExecutor *background_executor_;
  share::ObBackgroundTaskSourceHandle source_handle_;
  friend class ObMajorMergeIdling;
  DISALLOW_COPY_AND_ASSIGN(ObMajorMergeScheduler);
};

} // end namespace rootserver
} // end namespace oceanbase

#endif // OCEANBASE_ROOTSERVER_FREEZE_OB_MERGE_SCHEDULER_H_
