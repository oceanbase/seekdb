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
#ifndef OB_STORAGE_COMPACTION_TENANT_TABLET_SCHEDULER_TASK_MGR_H_
#define OB_STORAGE_COMPACTION_TENANT_TABLET_SCHEDULER_TASK_MGR_H_

#include "share/compaction/ob_compaction_timer_task_mgr.h"
namespace oceanbase
{
namespace compaction
{

struct ObTenantTabletSchedulerTaskMgr : public ObCompactionTimerTask
{
  ObTenantTabletSchedulerTaskMgr();
  ~ObTenantTabletSchedulerTaskMgr();
  virtual void destroy() override;
  virtual int start() override;
  virtual void stop() override;
  virtual void wait() override;
  void set_scheduler_interval(const int64_t schedule_interval) { schedule_interval_ = schedule_interval; }
  int restart_scheduler_timer_task(const int64_t merge_schedule_interval);
  DEFINE_TIMER_TASK(MergeLoopTask);
  DEFINE_TIMER_TASK_WITHOUT_TIMEOUT_CHECK(SSTableGCTask);
  DEFINE_TIMER_TASK(InfoPoolResizeTask);
  DEFINE_TIMER_TASK(TabletUpdaterRefreshTask);
  DEFINE_TIMER_TASK_WITHOUT_TIMEOUT_CHECK(MediumLoopTask);
  DEFINE_TIMER_TASK_WITHOUT_TIMEOUT_CHECK(MediumCheckTask);
  static const int64_t DEFAULT_COMPACTION_SCHEDULE_INTERVAL = 30 * 1000 * 1000L; // 30s
private:
  static const int64_t SSTABLE_GC_INTERVAL = 30 * 1000 * 1000L; // 30s
  static const int64_t INFO_POOL_RESIZE_INTERVAL = 30 * 1000 * 1000L; // 30s
  static const int64_t TABLET_UPDATER_REFRESH_INTERVAL = 5 * 60 * 1000 * 1000L; // 5min
  static const int64_t MEDIUM_CHECK_INTERVAL = 20 * 1000 * 1000L; // 20s
  // thread TG id
  int merge_loop_tg_id_;
  int medium_loop_tg_id_;
  int sstable_gc_tg_id_;
  int compaction_refresh_tg_id_;
  int64_t schedule_interval_;
  MergeLoopTask merge_loop_task_;
  MediumLoopTask medium_loop_task_;
  SSTableGCTask sstable_gc_task_;
  InfoPoolResizeTask info_pool_resize_task_;
  TabletUpdaterRefreshTask tablet_updater_refresh_task_;
  MediumCheckTask medium_check_task_;
};


} // namespace compaction
} // namespace oceanbase

#endif // OB_STORAGE_COMPACTION_TENANT_TABLET_SCHEDULER_TASK_MGR_H_
