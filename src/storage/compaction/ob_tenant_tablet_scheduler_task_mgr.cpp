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
  : merge_loop_tg_id_(-1),
    medium_loop_tg_id_(-1),
    sstable_gc_tg_id_(-1),
    compaction_refresh_tg_id_(-1),
    schedule_interval_(-1),
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
  DESTROY_THREAD(merge_loop_tg_id_);
  DESTROY_THREAD(medium_loop_tg_id_);
  DESTROY_THREAD(sstable_gc_tg_id_);
  DESTROY_THREAD(compaction_refresh_tg_id_);
  schedule_interval_ = -1;
}

void ObTenantTabletSchedulerTaskMgr::stop()
{
  STOP_THREAD(merge_loop_tg_id_);
  STOP_THREAD(medium_loop_tg_id_);
  STOP_THREAD(sstable_gc_tg_id_);
  STOP_THREAD(compaction_refresh_tg_id_);
}

void ObTenantTabletSchedulerTaskMgr::wait()
{
  WAIT_THREAD(merge_loop_tg_id_);
  WAIT_THREAD(medium_loop_tg_id_);
  WAIT_THREAD(sstable_gc_tg_id_);
  WAIT_THREAD(compaction_refresh_tg_id_);
}

void ObTenantTabletSchedulerTaskMgr::MergeLoopTask::runTimerTask()
{
  int ret = OB_SUCCESS;
  int64_t cost_ts = ObTimeUtility::fast_current_time();
  ObCurTraceId::init(GCONF.self_addr_);
  if (ObBasicMergeScheduler::could_start_loop_task()) {
    if (OB_FAIL(share::g_mp->tenant_tablet_scheduler()->schedule_all_tablets_minor())) {
    }
    cost_ts = ObTimeUtility::fast_current_time() - cost_ts;
    LOG_INFO("MergeLoopTask", K(cost_ts));
  }
}

void ObTenantTabletSchedulerTaskMgr::MediumLoopTask::runTimerTask()
{
  int ret = OB_SUCCESS;
  int64_t cost_ts = ObTimeUtility::fast_current_time();
  ObCurTraceId::init(GCONF.self_addr_);
  if (ObBasicMergeScheduler::could_start_loop_task()) {
    if (OB_FAIL(share::g_mp->tenant_tablet_scheduler()->schedule_all_tablets_medium())) {
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
  }
  if (OB_FAIL(share::g_mp->tenant_tablet_scheduler()->gc_info())) {
  }
  if (OB_FAIL(share::g_mp->tenant_cg_read_info_mgr()->gc_cg_info_array())) {
  }
  if (OB_FAIL(share::g_mp->tenant_tablet_scheduler()->refresh_tenant_status())) {
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
  }
  cost_ts = ObTimeUtility::fast_current_time() - cost_ts;
  LOG_INFO("MediumCheckTask", K(cost_ts));
}

int ObTenantTabletSchedulerTaskMgr::start()
{
  int ret = OB_SUCCESS;
  const bool repeat = true;
  if (OB_FAIL(TG_CREATE_TENANT(lib::TGDefIDs::MergeLoop, merge_loop_tg_id_))) {
  } else if (OB_FAIL(TG_START(merge_loop_tg_id_))) {
  } else if (OB_FAIL(TG_SCHEDULE(merge_loop_tg_id_, merge_loop_task_, schedule_interval_, repeat))) {
  } else if (OB_FAIL(TG_CREATE_TENANT(lib::TGDefIDs::SSTableGC, sstable_gc_tg_id_))) {
  } else if (OB_FAIL(TG_START(sstable_gc_tg_id_))) {
  } else if (OB_FAIL(TG_SCHEDULE(sstable_gc_tg_id_, sstable_gc_task_, SSTABLE_GC_INTERVAL, repeat))) {
  } else if (OB_FAIL(TG_CREATE_TENANT(lib::TGDefIDs::CompactionRefresh, compaction_refresh_tg_id_))) {
  } else if (OB_FAIL(TG_START(compaction_refresh_tg_id_))) {
  } else if (OB_FAIL(TG_SCHEDULE(compaction_refresh_tg_id_, info_pool_resize_task_, INFO_POOL_RESIZE_INTERVAL, repeat))) {
  } else if (OB_FAIL(TG_SCHEDULE(compaction_refresh_tg_id_, tablet_updater_refresh_task_, TABLET_UPDATER_REFRESH_INTERVAL, repeat))) {
  } else if (OB_FAIL(TG_CREATE_TENANT(lib::TGDefIDs::MediumLoop, medium_loop_tg_id_))) {
  } else if (OB_FAIL(TG_START(medium_loop_tg_id_))) {
  } else if (OB_FAIL(TG_SCHEDULE(medium_loop_tg_id_, medium_loop_task_, schedule_interval_, repeat))) {
  }
  return ret;
}

int ObTenantTabletSchedulerTaskMgr::restart_scheduler_timer_task(
  const int64_t merge_schedule_interval)
{
  int ret = OB_SUCCESS;
  if (schedule_interval_ == merge_schedule_interval) {
  } else if (OB_FAIL(restart_schedule_timer_task(merge_schedule_interval, merge_loop_tg_id_, merge_loop_task_))) {
  } else if (OB_FAIL(restart_schedule_timer_task(merge_schedule_interval, medium_loop_tg_id_, medium_loop_task_))) {
  } else {
    schedule_interval_ = merge_schedule_interval;
    LOG_INFO("succeeded to reload new merge schedule interval", K(merge_schedule_interval));
  }
  return ret;
}

} // namespace compaction
} // namespace oceanbase
