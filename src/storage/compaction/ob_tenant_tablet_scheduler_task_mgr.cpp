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
#define USING_LOG_PREFIX STORAGE_COMPACTION
#include "ob_tenant_tablet_scheduler_task_mgr.h"
#include "share/rc/ob_module_provider.h"
#include "observer/report/ob_tablet_table_updater.h" // for ObTabletTableUpdater
#include "storage/compaction/ob_tenant_tablet_scheduler.h"
namespace oceanbase
{
using namespace storage;
using namespace common;
using namespace share;
namespace compaction
{
ObTenantTabletSchedulerTaskMgr::ObTenantTabletSchedulerTaskMgr()
  : merge_loop_timer_(),
    medium_loop_timer_(),
    sstable_gc_timer_(),
    compaction_refresh_timer_(),
    schedule_interval_(-1),
    is_active_medium_loop_(false),
    merge_loop_task_(),
    medium_loop_task_(),
    sstable_gc_task_(),
    info_pool_resize_task_(),
    tablet_updater_refresh_task_(),
    medium_check_task_()
{}

ObTenantTabletSchedulerTaskMgr::~ObTenantTabletSchedulerTaskMgr()
{
  destroy();
}

void ObTenantTabletSchedulerTaskMgr::destroy()
{
  merge_loop_timer_.destroy();
  medium_loop_timer_.destroy();
  sstable_gc_timer_.destroy();
  compaction_refresh_timer_.destroy();
  schedule_interval_ = -1;
  is_active_medium_loop_ = false;
}

void ObTenantTabletSchedulerTaskMgr::stop()
{
  merge_loop_timer_.stop();
  medium_loop_timer_.stop();
  sstable_gc_timer_.stop();
  compaction_refresh_timer_.stop();
}

void ObTenantTabletSchedulerTaskMgr::wait()
{
  merge_loop_timer_.wait();
  medium_loop_timer_.wait();
  sstable_gc_timer_.wait();
  compaction_refresh_timer_.wait();
}

void ObTenantTabletSchedulerTaskMgr::MergeLoopTask::runTimerTask()
{
  int ret = OB_SUCCESS;
  int64_t cost_ts = ObTimeUtility::fast_current_time();
  ObCurTraceId::init(GCONF.self_addr_);
  if (ObBasicMergeScheduler::could_start_loop_task()) {
    if (OB_FAIL(share::g_mp->tenant_tablet_scheduler()->schedule_all_tablets_minor())) {
      LOG_WARN("Fail to merge all partition", K(ret));
    }
    cost_ts = ObTimeUtility::fast_current_time() - cost_ts;
    LOG_INFO("MergeLoopTask", K(cost_ts));
  }
}

void ObTenantTabletSchedulerTaskMgr::MediumLoopTask::runTimerTask()
{
  int ret = OB_SUCCESS;
  int tmp_ret = OB_SUCCESS;
  int64_t cost_ts = ObTimeUtility::fast_current_time();
  ObCurTraceId::init(GCONF.self_addr_);
  if (ObBasicMergeScheduler::could_start_loop_task()) {
    ObTenantTabletScheduler *scheduler = share::g_mp->tenant_tablet_scheduler();
    if (OB_FAIL(scheduler->schedule_all_tablets_medium())) {
      LOG_WARN("Fail to merge all partition", K(ret));
    }
    const bool active = scheduler->need_fast_medium_loop();
    if (OB_TMP_FAIL(scheduler->timer_task_mgr_.set_active_medium_loop(active, false/*immediate*/))) {
      LOG_WARN_RET(tmp_ret, "failed to switch medium loop", K(active));
    }
    cost_ts = ObTimeUtility::fast_current_time() - cost_ts;
    LOG_INFO("MediumLoopTask", K(cost_ts));
  }
}

void ObTenantTabletSchedulerTaskMgr::SSTableGCTask::runTimerTask()
{
  int ret = OB_SUCCESS;
  if (ObBasicMergeScheduler::could_start_loop_task()) {
    // use tenant config to loop minor && medium task
    share::g_mp->tenant_tablet_scheduler()->reload_tenant_config();
    int64_t cost_ts = ObTimeUtility::fast_current_time();
    ObCurTraceId::init(GCONF.self_addr_);
    if (OB_FAIL(share::g_mp->tenant_tablet_scheduler()->update_upper_trans_version_and_gc_sstable())) {
      LOG_WARN("Fail to update upper_trans_version and gc sstable", K(ret));
    }
    cost_ts = ObTimeUtility::fast_current_time() - cost_ts;
    LOG_INFO("SSTableGCTask", K(cost_ts));
  }
}

void ObTenantTabletSchedulerTaskMgr::InfoPoolResizeTask::runTimerTask()
{
  int ret = OB_SUCCESS;
  int64_t cost_ts = ObTimeUtility::fast_current_time();
  ObCurTraceId::init(GCONF.self_addr_);
  if (OB_FAIL(share::g_mp->tenant_tablet_scheduler()->set_max())) {
    LOG_WARN("Fail to resize info pool", K(ret));
  }
  if (OB_FAIL(share::g_mp->tenant_tablet_scheduler()->gc_info())) {
    LOG_WARN("Fail to gc info", K(ret));
  }
  if (OB_FAIL(share::g_mp->tenant_tablet_scheduler()->refresh_tenant_status())) {
    LOG_WARN("Fail to refresh tenant status", K(ret));
  }
  cost_ts = ObTimeUtility::fast_current_time() - cost_ts;
  LOG_INFO("InfoPoolResizeTask", K(cost_ts));
}

void ObTenantTabletSchedulerTaskMgr::TabletUpdaterRefreshTask::runTimerTask()
{
  int ret = OB_SUCCESS;
  int64_t cost_ts = ObTimeUtility::fast_current_time();
  ObCurTraceId::init(GCONF.self_addr_);
  if (OB_FAIL(share::g_mp->tablet_table_updater()->set_thread_count())) {
    LOG_WARN("Fail to reset thread count", K(ret));
  }
  cost_ts = ObTimeUtility::fast_current_time() - cost_ts;
  LOG_INFO("TabletUpdaterRefreshTask", K(cost_ts));
}

void ObTenantTabletSchedulerTaskMgr::MediumCheckTask::runTimerTask()
{
  int ret = OB_SUCCESS;
  int64_t cost_ts = ObTimeUtility::fast_current_time();
  ObCurTraceId::init(GCONF.self_addr_);
  if (OB_FAIL(share::g_mp->tenant_medium_checker()->check_medium_finish_schedule())) {
    LOG_WARN("Fail to check_medium_finish and schedule", K(ret));
  }
  cost_ts = ObTimeUtility::fast_current_time() - cost_ts;
  LOG_INFO("MediumCheckTask", K(cost_ts));
}

int ObTenantTabletSchedulerTaskMgr::start()
{
  int ret = OB_SUCCESS;
  const bool repeat = true;
  if (OB_FAIL(merge_loop_timer_.init("MergeLoop", ObMemAttr("MergeLoop")))) {
    LOG_WARN("failed to init merge loop timer", K(ret));
  } else if (OB_FAIL(merge_loop_timer_.schedule(merge_loop_task_, schedule_interval_, repeat))) {
    LOG_WARN("Fail to schedule minor merge scan task", K(ret));
  } else if (OB_FAIL(sstable_gc_timer_.init("SSTableGC", ObMemAttr("SSTableGC")))) {
    LOG_WARN("failed to init sstable gc timer", K(ret));
  } else if (OB_FAIL(sstable_gc_timer_.schedule(sstable_gc_task_, SSTABLE_GC_INTERVAL, repeat))) {
    LOG_WARN("Fail to schedule sstable gc task", K(ret));
  } else if (OB_FAIL(compaction_refresh_timer_.init("CompRefresh", ObMemAttr("CompRefresh")))) {
    LOG_WARN("failed to init compaction refresh timer", K(ret));
  } else if (OB_FAIL(compaction_refresh_timer_.schedule(info_pool_resize_task_, INFO_POOL_RESIZE_INTERVAL, repeat))) {
    LOG_WARN("Fail to schedule info pool resize task", K(ret));
  } else if (OB_FAIL(compaction_refresh_timer_.schedule(tablet_updater_refresh_task_, TABLET_UPDATER_REFRESH_INTERVAL, repeat))) {
    LOG_WARN("Fail to schedule tablet updater refresh task", K(ret));
  } else if (OB_FAIL(medium_loop_timer_.init("MediumLoop", ObMemAttr("MediumLoop")))) {
    LOG_WARN("failed to init medium loop timer", K(ret));
  } else if (OB_FAIL(medium_loop_timer_.schedule(medium_loop_task_, schedule_interval_, repeat))) {
    LOG_WARN("Fail to schedule medium merge scan task", K(ret));
  } else {
    ATOMIC_STORE(&is_active_medium_loop_, false);
  }
  return ret;
}

int ObTenantTabletSchedulerTaskMgr::restart_scheduler_timer_task(
  const int64_t merge_schedule_interval)
{
  int ret = OB_SUCCESS;
  if (schedule_interval_ == merge_schedule_interval) {
  } else if (OB_FAIL(restart_schedule_timer_task(merge_schedule_interval, merge_loop_timer_, merge_loop_task_))) {
    LOG_WARN("failed to reload new merge schedule interval", K(merge_schedule_interval));
  } else if (!ATOMIC_LOAD(&is_active_medium_loop_)
      && OB_FAIL(restart_schedule_timer_task(merge_schedule_interval, medium_loop_timer_, medium_loop_task_))) {
    LOG_WARN("failed to reload new merge schedule interval", K(merge_schedule_interval));
  } else {
    schedule_interval_ = merge_schedule_interval;
    LOG_INFO("succeeded to reload new merge schedule interval", K(merge_schedule_interval));
  }
  return ret;
}

int ObTenantTabletSchedulerTaskMgr::set_active_medium_loop(
  const bool active,
  const bool immediate)
{
  int ret = OB_SUCCESS;
  if (active == ATOMIC_LOAD(&is_active_medium_loop_)) {
  } else if (OB_FAIL(restart_schedule_timer_task(
      active ? ACTIVE_MEDIUM_LOOP_INTERVAL : schedule_interval_,
      medium_loop_timer_, medium_loop_task_, immediate))) {
    LOG_WARN("failed to switch medium loop", K(ret), K(active), K(immediate));
  } else {
    ATOMIC_STORE(&is_active_medium_loop_, active);
    LOG_INFO("switch medium loop", K(active), K(immediate));
  }
  return ret;
}

} // namespace compaction
} // namespace oceanbase
