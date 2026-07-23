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
#include "ob_tenant_tablet_scheduler.h"
#include "share/rc/ob_module_provider.h"
#include "storage/ob_bloom_filter_task.h"
#include "ob_schedule_dag_func.h"
#include "storage/tx_storage/ob_ls_service.h"
#include "storage/compaction/ob_medium_compaction_func.h"
#include "storage/compaction/ob_sstable_merge_info_mgr.h"
#include "storage/compaction/ob_tenant_freeze_info_mgr.h"
#include "storage/ddl/ob_ddl_merge_task.h"
#include "storage/compaction/ob_sstable_merge_info_mgr.h"
#include "storage/ob_gc_upper_trans_helper.h"
#include "share/schema/ob_tenant_schema_service.h"
#include "storage/compaction/ob_schedule_tablet_func.h"

namespace oceanbase
{
using namespace storage;
using namespace common;
using namespace share;

namespace compaction
{
ERRSIM_POINT_DEF(EN_COMPACTION_DISABLE_META_MERGE_AFTER_MINI);
/********************************************ObFastFreezeChecker impl******************************************/
ObFastFreezeChecker::ObFastFreezeChecker()
  : store_map_(),
    enable_fast_freeze_(false)
{
}

ObFastFreezeChecker::~ObFastFreezeChecker()
{
  reset();
}

int ObFastFreezeChecker::init()
{
  int ret = OB_SUCCESS;
  if (OB_FAIL(store_map_.create(FAST_FREEZE_TABLET_STAT_KEY_BUCKET_NUM, "FastFrezCkr", "FastFrezCkr"))) {
    LOG_WARN("failed to init fast freeze checker", K(ret));
  }
  return ret;
}

void ObFastFreezeChecker::reset()
{
  enable_fast_freeze_ = false;
  store_map_.destroy();
}

void ObFastFreezeChecker::reload_config(const bool enable_fast_freeze)
{
  enable_fast_freeze_ = enable_fast_freeze;
}

int ObFastFreezeChecker::check_need_fast_freeze(
    const ObTablet &tablet,
    bool &need_fast_freeze)
{
  int ret = OB_SUCCESS;
  int tmp_ret = OB_SUCCESS;
  need_fast_freeze = false;
  ObTableHandleV2 table_handle;
  ObITabletMemtable *memtable = nullptr;
  const common::ObTabletID &tablet_id = tablet.get_tablet_meta().tablet_id_;
  ObTableQueuingModeCfg queuing_cfg;
  if (OB_TMP_FAIL(share::g_mp->tenant_tablet_stat_mgr()->get_queuing_cfg(tablet_id, queuing_cfg))) {
    LOG_WARN_RET(tmp_ret, "[FastFreeze] failed to get table queuing mode, treat it as normal table", K(tablet_id));
  }
  const int64_t memtable_alive_threshold = queuing_cfg.get_memtable_alive_threshold(FAST_FREEZE_INTERVAL_US);
  if (OB_FAIL(tablet.get_active_memtable(table_handle))) {
    if (OB_ENTRY_NOT_EXIST == ret) {
      ret = OB_SUCCESS;
    } else {
      LOG_WARN("[FastFreeze] failed to get active memtable", K(ret));
    }
  } else if (OB_FAIL(table_handle.get_tablet_memtable(memtable))) {
    LOG_WARN("[FastFreeze] failed to get memtalbe", K(ret), K(table_handle));
  } else if (OB_ISNULL(memtable)) {
    ret = OB_ERR_UNEXPECTED;
    LOG_WARN("[FastFreeze] get unexpected null memtable", K(ret), KPC(memtable));
  } else if (!memtable->is_active_memtable()) {
    // do nothing
  } else if (!memtable->is_data_memtable()) {
    // do nothing
  } else if (ObTimeUtility::current_time() < memtable->get_timestamp() + memtable_alive_threshold) {
    if (REACH_THREAD_TIME_INTERVAL(PRINT_LOG_INTERVAL)) {
      LOG_INFO("[FastFreeze] memtable is just created, no need to check", K(memtable_alive_threshold), K(tablet_id), KPC(memtable));
    }
  } else {
    memtable::ObMemtable *mt = static_cast<memtable::ObMemtable *>(memtable);
    check_hotspot_need_fast_freeze(*mt, need_fast_freeze);
    if (need_fast_freeze) {
      FLOG_INFO("[FastFreeze] tablet detects hotspot row, need fast freeze", K(tablet_id));
    } else {
      // Only queuing table need tombstone fast freeze in 4.2.x, but 4.3.0 has this before, so open it
      check_tombstone_need_fast_freeze(tablet, queuing_cfg, *mt, need_fast_freeze);
      if (need_fast_freeze) {
        FLOG_INFO("[FastFreeze] tablet detects tombstone, need fast freeze", K(tablet_id));
      }
    }
  }
  return ret;
}

void ObFastFreezeChecker::check_hotspot_need_fast_freeze(
    memtable::ObMemtable &memtable,
    bool &need_fast_freeze)
{
  need_fast_freeze = false;
  if (memtable.is_active_memtable()) {
    need_fast_freeze = memtable.has_hotspot_row();
  }
}

void ObFastFreezeChecker::check_tombstone_need_fast_freeze(
    const ObTablet &tablet,
    const ObTableQueuingModeCfg &queuing_cfg,
    memtable::ObMemtable &memtable,
    bool &need_fast_freeze)
{
  need_fast_freeze = false;
  if (memtable.is_active_memtable()) {
    const common::ObTabletID &tablet_id = tablet.get_tablet_meta().tablet_id_;
    const ObMtStat &mt_stat = memtable.get_mt_stat(); // dirty read
    int64_t adaptive_threshold = queuing_cfg.get_tombstone_row_threshold(TOMBSTONE_DEFAULT_ROW_COUNT);
    if (!queuing_cfg.is_queuing_mode()) {
      // dynamically change adaptive_threshold by merge cnt in recent 10 mins
      try_update_tablet_threshold(ObTabletStatKey(tablet_id), mt_stat, memtable.get_timestamp(), queuing_cfg, adaptive_threshold);
    }
    need_fast_freeze = (mt_stat.update_row_count_ + mt_stat.delete_row_count_) >= adaptive_threshold
                     || mt_stat.delete_row_count_ > queuing_cfg.total_delete_row_cnt_;

    if (!need_fast_freeze) {
      need_fast_freeze =
        // tombstoned row count(empty ObMvccRow) is larger than 1000(hardcoded)
        (mt_stat.empty_mvcc_row_count_ >= EMPTY_MVCC_ROW_COUNT)
        // tombstoned row precentage(empty ObMvccRow) is larger than 50%(hardcoded)
        && (mt_stat.empty_mvcc_row_count_ >= INT64_MAX / 100 // prevent numerical overflow
            || mt_stat.empty_mvcc_row_count_ * 100 / memtable.get_physical_row_cnt()
               >= EMPTY_MVCC_ROW_PERCENTAGE);
      if (need_fast_freeze) {
        LOG_INFO("[FastFreeze] trigger by empty mvcc row tombstone", K(memtable), K(mt_stat),
                 K(memtable.get_physical_row_cnt()));
      } else {
        ObAdaptiveMergePolicy::AdaptiveMergeReason adaptive_merge_reason = ObAdaptiveMergePolicy::NONE;
        int tmp_ret = OB_SUCCESS;
        if (OB_TMP_FAIL(ObAdaptiveMergePolicy::check_tombstone_reason(tablet, adaptive_merge_reason))) {
          LOG_WARN_RET(tmp_ret, "failed to check tombstone by historical stats");
        } else if (ObAdaptiveMergePolicy::NONE != adaptive_merge_reason) {
          need_fast_freeze = true;
        }
      }
    }
  }
}

void ObFastFreezeChecker::try_update_tablet_threshold(
    const ObTabletStatKey &key,
    const ObMtStat &mt_stat,
    const int64_t memtable_create_timestamp,
    const ObTableQueuingModeCfg &queuing_cfg,
    int64_t &adaptive_threshold)
{
  int tmp_ret = OB_SUCCESS;
  const int64_t base_adaptive_threshold = queuing_cfg.get_tombstone_row_threshold(TOMBSTONE_DEFAULT_ROW_COUNT);
  adaptive_threshold = base_adaptive_threshold;
  int64_t old_threshold = adaptive_threshold;

  if (OB_TMP_FAIL(store_map_.get_refactored(key, adaptive_threshold))) {
    // use default threshold at first
    if (OB_HASH_NOT_EXIST != tmp_ret) {
      LOG_WARN_RET(tmp_ret, "[FastFreeze] failed to find store map", K(key));
    }
  } else {
    old_threshold = adaptive_threshold;
  }

  ObTabletStat tablet_stat;
  ObTabletStat total_stat;
  ObTableModeFlag mode = ObTableModeFlag::TABLE_MODE_NORMAL;
  if (OB_TMP_FAIL(share::g_mp->tenant_tablet_stat_mgr()->get_latest_tablet_stat(key.tablet_id_, tablet_stat, total_stat, mode))) {
    if (OB_HASH_NOT_EXIST != tmp_ret) {
      LOG_WARN_RET(tmp_ret, "[FastFreeze] failed to get tablet stat", K(key));
    }
    // not hot tablet, reset threshold
    adaptive_threshold = base_adaptive_threshold;
  } else if (tablet_stat.merge_cnt_ >= 2) {
    // too many mini compaction occurs during the past 10 mins, inc threshold to dec mini merge count
    adaptive_threshold = MIN(adaptive_threshold + TOMBSTONE_STEP_ROW_COUNT, TOMBSTONE_MAX_ROW_COUNT);
  } else if (0 == tablet_stat.merge_cnt_) {
    const int64_t inc_row_cnt = mt_stat.update_row_count_ + mt_stat.delete_row_count_;
    if (inc_row_cnt >= adaptive_threshold) {
      // do nothing
    } else if (inc_row_cnt >= TOMBSTONE_DEFAULT_ROW_COUNT && ObTimeUtility::fast_current_time() - memtable_create_timestamp >= FAST_FREEZE_INTERVAL_US * 4) {
      adaptive_threshold = base_adaptive_threshold;
    }
  }

  if (old_threshold != adaptive_threshold) {
    if (base_adaptive_threshold == adaptive_threshold) {
      (void) store_map_.erase_refactored(key);
    } else {
      (void) store_map_.set_refactored(key, adaptive_threshold);
    }
  }
}

/********************************************ObTenantTabletScheduler impl******************************************/
constexpr ObMergeType ObTenantTabletScheduler::MERGE_TYPES[];

ObTenantTabletScheduler::ObTenantTabletScheduler()
 : ObBasicMergeScheduler(),
   is_inited_(false),
   bf_queue_(),
   fast_freeze_checker_(),
   minor_tablet_iter_(false/*is_major*/),
   gc_sst_tablet_iter_(false/*is_major*/),
   timer_task_mgr_(),
   batch_size_mgr_()
{
  STATIC_ASSERT(static_cast<int64_t>(NO_MAJOR_MERGE_TYPE_CNT) == ARRAYSIZEOF(MERGE_TYPES), "merge type array len is mismatch");
}

ObTenantTabletScheduler::~ObTenantTabletScheduler()
{
  destroy();
}

void ObTenantTabletScheduler::destroy()
{
  if (IS_INIT) {
    reset();
  }
}
void ObTenantTabletScheduler::reset()
{
  stop();
  wait();

  is_inited_ = false;
  ObBasicMergeScheduler::reset();
  bf_queue_.destroy();
  minor_tablet_iter_.reset();
  gc_sst_tablet_iter_.reset();
  LOG_INFO("The ObTenantTabletScheduler destroy");
}

int ObTenantTabletScheduler::init()
{
  int ret = OB_SUCCESS;
  bool enable_adaptive_compaction = false;
  bool enable_adaptive_merge_schedule = false;
  int64_t schedule_interval = ObTenantTabletSchedulerTaskMgr::DEFAULT_COMPACTION_SCHEDULE_INTERVAL;
  int64_t schedule_batch_size = ObScheduleBatchSizeMgr::DEFAULT_TABLET_BATCH_CNT;

  {

    schedule_interval = GCONF.ob_compaction_schedule_interval;
    enable_adaptive_compaction = GCONF._enable_adaptive_compaction;
    enable_adaptive_merge_schedule = GCONF._enable_adaptive_merge_schedule;
    fast_freeze_checker_.reload_config(GCONF._ob_enable_fast_freeze);
    schedule_batch_size = GCONF.compaction_schedule_tablet_batch_cnt;

  } // end of ObTenantConfigGuard
#ifdef ERRSIM
  schedule_interval = 1000L * 1000L; // 1s
#endif
  if (IS_INIT) {
    ret = OB_INIT_TWICE;
    LOG_WARN("ObTenantTabletScheduler has inited", K(ret));
  } else if (FALSE_IT(bf_queue_.set_run_wrapper(MTL_CTX()))) {
  } else if (OB_FAIL(bf_queue_.init(BLOOM_FILTER_LOAD_BUILD_THREAD_CNT,
                                    "BFBuildTask",
                                    BF_TASK_QUEUE_SIZE,
                                    BF_TASK_MAP_SIZE,
                                    BF_TASK_TOTAL_LIMIT,
                                    BF_TASK_HOLD_LIMIT,
                                    BF_TASK_PAGE_SIZE,
                                    "bf_queue"))) {
    LOG_WARN("Fail to init bloom filter queue", K(ret));
  } else if (OB_FAIL(fast_freeze_checker_.init())) {
    LOG_WARN("Fail to create fast freeze checker", K(ret));
  } else {
    IGNORE_RETURN tenant_status_.refresh_tenant_config(enable_adaptive_compaction, enable_adaptive_merge_schedule);
    timer_task_mgr_.set_scheduler_interval(schedule_interval);
    batch_size_mgr_.set_tablet_batch_size(schedule_batch_size);
    is_inited_ = true;
  }
  return ret;
}

int ObTenantTabletScheduler::start()
{
  int ret = OB_SUCCESS;
  if (IS_NOT_INIT) {
    ret = OB_NOT_INIT;
    LOG_WARN("The ObTenantTabletScheduler has not been inited", K(ret));
  } else {
    ret = timer_task_mgr_.start();
  }
  return ret;
}

int ObTenantTabletScheduler::reload_tenant_config()
{
  int ret = OB_SUCCESS;
  int tmp_ret = OB_SUCCESS;
  if (IS_NOT_INIT) {
    ret = OB_NOT_INIT;
    LOG_WARN("The ObTenantTabletScheduler has not been inited", K(ret));
  } else if (is_stop_) {
    // do nothing
  } else {
    bool enable_adaptive_compaction = false;
    bool enable_adaptive_merge_schedule = false;
    int64_t merge_schedule_interval = ObTenantTabletSchedulerTaskMgr::DEFAULT_COMPACTION_SCHEDULE_INTERVAL;
    int64_t schedule_batch_size = ObScheduleBatchSizeMgr::DEFAULT_TABLET_BATCH_CNT;
    {

      merge_schedule_interval = GCONF.ob_compaction_schedule_interval;
      enable_adaptive_compaction = GCONF._enable_adaptive_compaction;
      enable_adaptive_merge_schedule = GCONF._enable_adaptive_merge_schedule;
      fast_freeze_checker_.reload_config(GCONF._ob_enable_fast_freeze);
      schedule_batch_size = GCONF.compaction_schedule_tablet_batch_cnt;

    } // end of ObTenantConfigGuard
    (void) tenant_status_.refresh_tenant_config(
      enable_adaptive_compaction,
      enable_adaptive_merge_schedule);

    if (OB_FAIL(timer_task_mgr_.restart_scheduler_timer_task(merge_schedule_interval))) {
      LOG_WARN("failed to restart scheduler timer", K(ret));
    } else {
      batch_size_mgr_.set_tablet_batch_size(schedule_batch_size);
    }
  }
  return ret;
}

int ObTenantTabletScheduler::mtl_init(ObTenantTabletScheduler* &scheduler)
{
  return scheduler->init();
}

void ObTenantTabletScheduler::stop()
{
  is_stop_ = true;
  timer_task_mgr_.stop();
  stop_major_merge();
}

int ObTenantTabletScheduler::update_upper_trans_version_and_gc_sstable()
{
  int ret = OB_SUCCESS;
  int tmp_ret = OB_SUCCESS;
  if (IS_NOT_INIT) {
    ret = OB_NOT_INIT;
    LOG_WARN("ObTenantTabletScheduler not init", K(ret));
  } else if (OB_FAIL(gc_sst_tablet_iter_.build_iter(get_schedule_batch_size()))) {
    LOG_WARN("failed to init iterator", K(ret));
  } else {
    gc_sst_tablet_iter_.set_tablet_get_mode(storage::ObMDSGetTabletMode::READ_WITHOUT_CHECK);
  }

  if (OB_SUCC(ret) && !gc_sst_tablet_iter_.is_scan_finish()) {
    ObLS *ls = gc_sst_tablet_iter_.get_ls();
    if (ls->is_stopped()) {
      gc_sst_tablet_iter_.finish_scan();
    } else if (OB_TMP_FAIL(try_update_upper_trans_version_and_gc_sstable(
            *ls, gc_sst_tablet_iter_))) {
      gc_sst_tablet_iter_.finish_scan();
      LOG_ERROR("failed to update upper trans version", K(tmp_ret));
    }
  }
  return ret;
}

int ObTenantTabletScheduler::try_update_upper_trans_version_and_gc_sstable(
    ObLS &ls,
    ObCompactionScheduleIterator &iter)
{
  int ret = OB_SUCCESS;
  ObTabletHandle tablet_handle;
  ObTablet *tablet = nullptr;
  common::ObTabletID tablet_id;
  while (OB_SUCC(ret)) {
      if (OB_FAIL(iter.get_next_tablet(tablet_handle))) {
        if (OB_ITER_END == ret) {
          ret = OB_SUCCESS;
          break;
        } else {
          LOG_WARN("failed to get tablet", K(ret), K(tablet_handle));
        }
      } else if (OB_UNLIKELY(!tablet_handle.is_valid())) {
        ret = OB_ERR_UNEXPECTED;
        LOG_WARN("invalid tablet handle", K(ret), K(tablet_handle));
      } else if (FALSE_IT(tablet = tablet_handle.get_obj())) {
      } else if (FALSE_IT(tablet_id = tablet->get_tablet_meta().tablet_id_)) {
      } else if (tablet_id.is_special_merge_tablet()) {
      } else if (!tablet->get_tablet_meta().restore_state_.check_allow_read()) {
      } else {
        int64_t multi_version_start = 0;
        int64_t max_resolved_upper_trans_version = 0;
        int tmp_ret = OB_SUCCESS;
        bool need_update = false; // need update table store
        // new_upper_trans comes from the old table store; the last minor end_scn
        // detects concurrent table-store updates.
        ObSEArray<int64_t, 8> new_upper_trans;
        new_upper_trans.set_attr(ObMemAttr("NewUpTxnVer"));
        UpdateUpperTransParam upper_trans_param;
        upper_trans_param.new_upper_trans_ = &new_upper_trans;
        if (OB_TMP_FAIL(ObGCUpperTransHelper::check_need_gc_or_update_upper_trans_version(
            ls, *tablet, multi_version_start, upper_trans_param, need_update,
            max_resolved_upper_trans_version))) {
          LOG_WARN("faild to check need gc or update", K(tmp_ret), K(tablet_id));
        } else if (need_update) {
          ObArenaAllocator tmp_arena("RmOldTblTmp", OB_MALLOC_NORMAL_BLOCK_SIZE);
          ObStorageSchema *storage_schema = nullptr;
          if (OB_TMP_FAIL(tablet->load_storage_schema(tmp_arena, storage_schema))) {
            LOG_WARN("failed to load storage schema", K(tmp_ret), K(tablet));
          } else {
            ObUpdateTableStoreParam param(tablet->get_snapshot_version(), multi_version_start, storage_schema, upper_trans_param);
            ObTabletHandle new_tablet_handle; // no use here
            if (OB_TMP_FAIL(ls.update_tablet_table_store(tablet_id, param, new_tablet_handle))) {
              LOG_WARN("failed to update table store", K(tmp_ret), K(param), K(tablet_id));
            } else {
              ObTenantFreezeInfoMgr *freeze_info_mgr = nullptr;
              FLOG_INFO("success to remove old table in table store", K(tmp_ret),
                  K(tablet_id), K(multi_version_start), KPC(tablet));
              if (max_resolved_upper_trans_version > 0
                  && INT64_MAX != max_resolved_upper_trans_version) {
                if (OB_ISNULL(share::g_mp)
                    || OB_ISNULL(freeze_info_mgr = share::g_mp->tenant_freeze_info_mgr())) {
                  LOG_WARN_RET(OB_ERR_UNEXPECTED, "tenant freeze info mgr is null",
                      K(tablet_id), K(max_resolved_upper_trans_version));
                } else {
                  freeze_info_mgr->get_snapshot_gc_scn_renewal_state()
                      .update_target_scn(max_resolved_upper_trans_version);
                  LOG_INFO("update snapshot gc renewal target after resolving upper trans version",
                      K(tablet_id), K(max_resolved_upper_trans_version));
                }
              }
            }
          }
          ObTabletObjLoadHelper::free(tmp_arena, storage_schema);
        }
      }
  } // end while
  return ret;
}

int ObTenantTabletScheduler::schedule_all_tablets_minor()
{
  int ret = OB_SUCCESS;
  int tmp_ret = OB_SUCCESS;
  if (IS_NOT_INIT) {
    ret = OB_NOT_INIT;
    LOG_WARN("The ObTenantTabletScheduler has not been inited", K(ret));
  } else if (OB_FAIL(minor_tablet_iter_.build_iter(get_schedule_batch_size()))) {
    LOG_WARN("failed to init iterator", K(ret));
  } else {
    LOG_INFO("start schedule all tablet minor merge", K(minor_tablet_iter_));
  }

  if (OB_SUCC(ret) && !minor_tablet_iter_.is_scan_finish()) {
    if (OB_TMP_FAIL(schedule_minor_merge(minor_tablet_iter_.get_ls()))) {
      LOG_TRACE("meet error when schedule", K(tmp_ret), K(minor_tablet_iter_));
      minor_tablet_iter_.finish_scan();
      if (!schedule_ignore_error(tmp_ret)) {
        LOG_ERROR("failed to schedule minor merge", K(tmp_ret));
      }
    }
  }
  return ret;
}

int ObTenantTabletScheduler::check_ls_compaction_finish()
{
  int ret = OB_SUCCESS;
  bool exist = false;
  if (OB_FAIL(share::g_mp->tenant_dag_scheduler()->check_compaction_dag_exist_with_cancel(exist))) {
    LOG_WARN("failed to check compaction dag", K(ret));
  } else if (exist) {
    // the compaction dag exists, need retry later.
    ret = OB_EAGAIN;
  }
  return ret;
}

int ObTenantTabletScheduler::gc_info()
{
  int ret = OB_SUCCESS;
  if (IS_NOT_INIT) {
    ret = OB_NOT_INIT;
    LOG_WARN("The ObTenantTabletScheduler has not been inited", K(ret));
  } else if (OB_FAIL(share::g_mp->schedule_suspect_info_mgr()->gc_info())) {
    LOG_WARN("failed to gc in ObScheduleSuspectInfoMgr", K(ret));
  } else if (OB_FAIL(share::g_mp->dag_warning_history_manager()->gc_info())) {
    LOG_WARN("failed to gc in ObDagWarningHistoryManager", K(ret));
  } else if (OB_FAIL(share::g_mp->tenant_ss_table_merge_info_mgr()->gc_info())) {
    LOG_WARN("failed to gc in ObTenantSSTableMergeInfoMgr", K(ret));
  }
  return ret;
}

int ObTenantTabletScheduler::set_max()
{
  int ret = OB_SUCCESS;
  if (IS_NOT_INIT) {
    ret = OB_NOT_INIT;
    LOG_WARN("The ObTenantTabletScheduler has not been inited", K(ret));
  } else if (OB_FAIL(share::g_mp->schedule_suspect_info_mgr()->set_max(ObScheduleSuspectInfoMgr::cal_max()))) {
    LOG_WARN("failed to set_max int ObScheduleSuspectInfoMgr", K(ret));
  } else if (OB_FAIL(share::g_mp->dag_warning_history_manager()->set_max(ObDagWarningHistoryManager::cal_max()))) {
    LOG_WARN("failed to set_max in ObDagWarningHistoryManager", K(ret));
  } else if (OB_FAIL(share::g_mp->tenant_ss_table_merge_info_mgr()->set_max(ObTenantSSTableMergeInfoMgr::cal_max()))) {
    LOG_WARN("failed to set_max int ObTenantSSTableMergeInfoMgr", K(ret));
  }
  return ret;
}

int ObTenantTabletScheduler::refresh_tenant_status()
{
  int ret = OB_SUCCESS;
  if (IS_NOT_INIT) {
    ret = OB_NOT_INIT;
    LOG_WARN("The ObTenantTabletScheduler has not been inited", K(ret));
  } else {
    IGNORE_RETURN tenant_status_.init_or_refresh();
  }
  return ret;
}

int ObTenantTabletScheduler::schedule_build_bloomfilter(
    const uint64_t table_id,
    const blocksstable::MacroBlockId &macro_id,
    const int64_t prefix_len)
{
  int ret = OB_SUCCESS;
  if (IS_NOT_INIT) {
    ret = OB_NOT_INIT;
    LOG_WARN("The ObTenantTabletScheduler has not been inited", K(ret));
  } else if (OB_UNLIKELY(!macro_id.is_valid() || prefix_len <= 0)) {
    ret = OB_INVALID_ARGUMENT;
    LOG_WARN("Invalid argument", K(ret), K(macro_id), K(prefix_len));
  } else {
    ObBloomFilterBuildTask task(table_id, macro_id, prefix_len);
    if (OB_FAIL(bf_queue_.add_task(task))) {
      if (OB_LIKELY(OB_EAGAIN == ret)) {
        ret = OB_SUCCESS;
      } else {
        LOG_WARN("Failed to add bloomfilter build task", K(ret));
      }
    }
  }
  return ret;
}

int ObTenantTabletScheduler::schedule_merge(const int64_t broadcast_version)
{
  int ret = OB_SUCCESS;
  int tmp_ret = OB_SUCCESS;

  if (IS_NOT_INIT) {
    ret = OB_NOT_INIT;
    LOG_WARN("ObTenantTabletScheduler has not been inited", K(ret));
  } else if (OB_UNLIKELY(broadcast_version <= 0)) {
    ret = OB_INVALID_ARGUMENT;
    LOG_WARN("Invalid argument, ", K(broadcast_version), K(ret));
  } else if (broadcast_version > get_frozen_version()) {
    update_frozen_version_and_merge_progress(broadcast_version);
    LOG_INFO("schedule merge major version", K(broadcast_version));

    share::g_mp->tenant_medium_checker()->clear_error_tablet_cnt();

    medium_loop_.start_merge(broadcast_version); // set all statistics
    if (OB_TMP_FAIL(timer_task_mgr_.set_active_medium_loop(true/*active*/, true/*immediate*/))) {
      LOG_WARN_RET(tmp_ret, "failed to wakeup medium loop", K(broadcast_version));
    }
  }
  return ret;
}

bool ObTenantTabletScheduler::check_tx_table_ready(ObLS &ls, const SCN &check_scn)
{
  int ret = OB_SUCCESS;
  bool tx_table_ready = false;
  SCN max_decided_scn;
  if (OB_FAIL(ls.get_max_decided_scn(max_decided_scn))) {
    LOG_WARN("failed to get max decided log_ts", K(ret));
  } else if (check_scn <= max_decided_scn) {
    tx_table_ready = true;
    LOG_INFO("tx table ready", "sstable_end_scn", check_scn, K(max_decided_scn));
  }

  return tx_table_ready;
}

int ObTenantTabletScheduler::check_ready_for_major_merge(
    const storage::ObTablet &tablet,
    const ObMergeType merge_type)
{
  UNUSED(tablet);
  UNUSED(merge_type);
  return OB_SUCCESS;
}

int ObTenantTabletScheduler::schedule_merge_dag(
    const storage::ObTablet &tablet,
    const ObMergeType merge_type,
    const int64_t &merge_snapshot_version,
    const ObExecMode exec_mode,
    const ObDagId *dag_net_id /*= nullptr*/)
{
  int ret = OB_SUCCESS;
  if (OB_FAIL(check_ready_for_major_merge(tablet, merge_type))) {
    LOG_WARN("failed to check ready for major merge", K(ret), K(tablet), K(merge_type));
  } else {
    UNUSED(dag_net_id);
    ObTabletMergeDagParam param;
    if (OB_FAIL(ObDagParamFunc::fill_param(
      tablet, merge_type, merge_snapshot_version, exec_mode, param))) {
      LOG_WARN("failed to fill param", KR(ret));
    } else if (OB_FAIL(ObScheduleDagFunc::schedule_tablet_merge_dag(param))) {
      if (OB_EAGAIN != ret && OB_SIZE_OVERFLOW != ret) {
        LOG_ERROR("failed to schedule tablet merge dag", K(ret));
      }
    }
    FLOG_INFO("schedule merge dag", K(ret), K(param), K(merge_type));
  }
  return ret;
}

int ObTenantTabletScheduler::schedule_tablet_meta_merge(
    ObLS *ls,
    ObTabletHandle &tablet_handle,
    bool &has_created_dag)
{
  int ret = OB_SUCCESS;
  has_created_dag = false;

  if (OB_UNLIKELY(OB_ISNULL(ls) || !tablet_handle.is_valid())) {
    ret = OB_INVALID_ARGUMENT;
    LOG_WARN("invalid argument", K(ret), KP(ls), K(tablet_handle));
  } else {
    ObTablet *tablet = tablet_handle.get_obj();
    const ObTabletID &tablet_id = tablet->get_tablet_meta().tablet_id_;
    const int64_t last_major_snapshot_version = tablet->get_last_major_snapshot_version();
    int64_t max_sync_medium_scn = 0;
    ObArenaAllocator allocator("GetMediumList", OB_MALLOC_NORMAL_BLOCK_SIZE);
    const compaction::ObMediumCompactionInfoList *medium_list = nullptr;
    ObGetMergeTablesParam param;
    param.merge_type_ = META_MAJOR_MERGE;
    ObGetMergeTablesResult result;

    // check medium list
    if (OB_FAIL(tablet->read_medium_info_list(allocator, medium_list))) {
      LOG_WARN("failed to read medium info list", K(ret), K(tablet_id));
    } else if (OB_FAIL(ObMediumCompactionScheduleFunc::get_max_sync_medium_scn(
        *tablet, *medium_list, max_sync_medium_scn))) {
      LOG_WARN("failed to get max sync medium snapshot", K(ret), K(tablet_id));
    } else if ((nullptr != medium_list && medium_list->size() > 0)
             || max_sync_medium_scn > last_major_snapshot_version) {
      ret = OB_NO_NEED_MERGE;
      LOG_WARN("tablet exists unfinished medium info, no need to do meta merge", K(ret), K(tablet_id),
          K(last_major_snapshot_version), K(max_sync_medium_scn), KPC(medium_list));
    } else {
      LOG_INFO("start schedule meta merge", K(tablet_id), KPC(tablet)); // tmp log, remove later
      ObGetMergeTablesParam param;
      ObGetMergeTablesResult result;
      param.merge_type_ = META_MAJOR_MERGE;
      if (OB_FAIL(ObAdaptiveMergePolicy::get_meta_merge_tables(
              param,
              *ls,
              *tablet,
              result))) {
        if (OB_NO_NEED_MERGE != ret) {
          LOG_WARN("failed to get meta merge tables", K(ret), K(param), K(tablet_id));
        }
      } else if (FALSE_IT(result.merge_version_ = result.version_range_.snapshot_version_)) {
      } else if (OB_UNLIKELY(tablet->get_multi_version_start() > result.merge_version_)) {
        ret = OB_SNAPSHOT_DISCARDED;
        LOG_WARN("multi version data is discarded, should not compaction now", K(ret), K(tablet_id),
          K(result.merge_version_));
      } else {
        ObTabletMergeDagParam dag_param;
        if (OB_FAIL(ObDagParamFunc::fill_param(
          *tablet, META_MAJOR_MERGE, result.merge_version_, EXEC_MODE_LOCAL, dag_param))) {
          LOG_WARN("failed to fill param", KR(ret));
        } else if (OB_FAIL(schedule_merge_execute_dag<ObTabletMergeExecuteDag>(
                dag_param, ls, tablet_handle, result))) {
          if (OB_SIZE_OVERFLOW != ret && OB_EAGAIN != ret) {
            LOG_ERROR("failed to schedule tablet meta merge dag", K(ret), K(dag_param));
          }
        } else {
          has_created_dag = true;
        }
      }

      if (OB_SUCC(ret) && has_created_dag) {
        share::g_mp->tenant_tablet_stat_mgr()->clear_tablet_stat(tablet_id);
        LOG_INFO("success to schedule meta merge", K(ret), K(tablet_id));
      }
    }
  }
  return ret;
}

int ObTenantTabletScheduler::fill_minor_compaction_param(
    const ObTabletHandle &tablet_handle,
    const ObGetMergeTablesResult &result,
    const int64_t total_sstable_cnt,
    const int64_t parallel_dag_cnt,
    const int64_t create_time,
    ObTabletMergeDagParam &param)
{
  int ret = OB_SUCCESS;
  ObCompactionParam &compaction_param = param.compaction_param_;
  compaction_param.add_time_ = create_time;
  compaction_param.sstable_cnt_ = total_sstable_cnt;
  compaction_param.parallel_dag_cnt_ = parallel_dag_cnt;
  ObProtectedMemtableMgrHandle *protected_handle = NULL;

  ObITable *table = nullptr;
  int64_t row_count = 0;
  int64_t macro_count = 0;
  for (int64_t i = 0; OB_SUCC(ret) && i < result.handle_.get_count(); ++i) {
    table = result.handle_.get_table(i);
    if (OB_UNLIKELY(NULL == table || !table->is_multi_version_minor_sstable())) {
      ret = OB_ERR_UNEXPECTED;
      LOG_WARN("get unexpected table", K(ret), KPC(table), K(result));
    } else {
      ObSSTable *sstable = static_cast<ObSSTable *>(table);
      compaction_param.occupy_size_ += sstable->get_occupy_size();
      row_count += sstable->get_row_count();
      macro_count += sstable->get_data_macro_block_count();
      compaction_param.parallel_sstable_cnt_++;
    }
  }

  if (OB_SUCC(ret)) {
    if (OB_FAIL(tablet_handle.get_obj()->get_protected_memtable_mgr_handle(protected_handle))) {
      LOG_WARN("failed to get_protected_memtable_mgr_handle", K(ret), KPC(tablet_handle.get_obj()));
    } else {
      compaction_param.estimate_concurrent_count(MINOR_MERGE);
      param.need_swap_tablet_flag_ = ObBasicTabletMergeCtx::need_swap_tablet(*protected_handle, row_count, macro_count);
      param.merge_version_ = result.handle_.get_table(result.handle_.get_count() - 1)->get_end_scn().get_val_for_tx();
    }
  }
  return ret;
}

template <class T>
int ObTenantTabletScheduler::schedule_tablet_minor_merge(
    ObLS *ls,
    ObTabletHandle &tablet_handle)
{
  int ret = OB_SUCCESS;
  const ObTabletID &tablet_id = tablet_handle.get_obj()->get_tablet_meta().tablet_id_;
  const int64_t schedule_type_cnt = tablet_id.is_special_merge_tablet() ? TX_TABLE_NO_MAJOR_MERGE_TYPE_CNT : NO_MAJOR_MERGE_TYPE_CNT;
  for (int i = 0; OB_SUCC(ret) && i < schedule_type_cnt; ++i) {
    if (OB_FAIL(schedule_tablet_minor_merge<T>(MERGE_TYPES[i], ls, tablet_handle))) {
      LOG_WARN("fail to schdule minor merge", K(ret), "merge_type", MERGE_TYPES[i], K(tablet_id));
    }
  }
  return ret;
}

template <class T>
int ObTenantTabletScheduler::schedule_tablet_minor_merge(
    const ObMergeType &merge_type,
    ObLS *ls,
    ObTabletHandle &tablet_handle)
{
  int ret = OB_SUCCESS;
  const ObTabletID &tablet_id = tablet_handle.get_obj()->get_tablet_meta().tablet_id_;
  ObGetMergeTablesParam param;
  ObGetMergeTablesResult result;
  param.merge_type_ = merge_type;
  if (OB_FAIL(ObPartitionMergePolicy::get_merge_tables[merge_type](
          param,
          *ls,
          *tablet_handle.get_obj(),
          result))) {
    if (OB_NO_NEED_MERGE == ret) {
      ret = OB_SUCCESS;
      LOG_DEBUG("tablet no need merge", K(ret), K(merge_type), K(tablet_id), K(tablet_handle));
    } else {
      LOG_WARN("failed to check need merge", K(ret), K(merge_type), K(tablet_id), K(tablet_handle));
    }
  } else {
    int64_t minor_compact_trigger = ObPartitionMergePolicy::DEFAULT_MINOR_COMPACT_TRIGGER;
    {

      minor_compact_trigger = GCONF.minor_compact_trigger;

    }

    ObMinorExecuteRangeMgr minor_range_mgr;
    MinorParallelResultArray parallel_results;
    if (result.handle_.get_count() < minor_compact_trigger) {
      ret = OB_NO_NEED_MERGE;
    } else if (OB_FAIL(minor_range_mgr.get_merge_ranges(tablet_id))) {
      LOG_WARN("failed to get merge range", K(ret), K(tablet_id));
    } else if (OB_FAIL(ObPartitionMergePolicy::generate_parallel_minor_interval(param.merge_type_, minor_compact_trigger, result, minor_range_mgr, parallel_results))) {
      if (OB_NO_NEED_MERGE != ret) {
        LOG_WARN("failed to generate parallel minor dag", K(ret), K(result));
      }
    } else if (parallel_results.empty()) {
      LOG_DEBUG("parallel results is empty, cannot schedule parallel minor merge", K(tablet_id),
          K(result), K(minor_range_mgr.exe_range_array_));
    } else {
      const int64_t parallel_dag_cnt = minor_range_mgr.exe_range_array_.count() + parallel_results.count();
      const int64_t total_sstable_cnt = result.handle_.get_count();
      const int64_t create_time = common::ObTimeUtility::fast_current_time();
      ObTabletMergeDagParam dag_param(merge_type, tablet_id);
      for (int64_t k = 0; OB_SUCC(ret) && k < parallel_results.count(); ++k) {
        if (OB_UNLIKELY(parallel_results.at(k).handle_.get_count() <= 1)) {
          LOG_WARN("invalid parallel result", K(ret), K(k), K(parallel_results));
        } else if (OB_FAIL(fill_minor_compaction_param(tablet_handle, parallel_results.at(k), total_sstable_cnt, parallel_dag_cnt, create_time, dag_param))) {
          LOG_WARN("failed to fill compaction param for ranking dags later", K(ret), K(k), K(parallel_results.at(k)));
        } else if (OB_FAIL(schedule_merge_execute_dag<T>(dag_param, ls, tablet_handle, parallel_results.at(k)))) {
          LOG_WARN("failed to schedule minor execute dag", K(ret), K(k), K(parallel_results.at(k)));
        } else {
          LOG_INFO("success to schedule tablet minor merge", K(ret), K(tablet_id),
            "table_cnt", parallel_results.at(k).handle_.get_count(),
            "merge_scn_range", parallel_results.at(k).scn_range_, K(merge_type));
        }
      } // end of for
    }
  }
  return ret;
}

int ObTenantTabletScheduler::schedule_tablet_ddl_major_merge(
    ObLS *ls,
    ObTabletHandle &tablet_handle)
{
  int ret = OB_SUCCESS;
  ObDDLTableMergeDagParam param;
  ObTabletDirectLoadMgrHandle direct_load_mgr_handle;
  ObDDLKvMgrHandle ddl_kv_mgr_handle;
  ObTenantDirectLoadMgr *tenant_direct_load_mgr = share::g_mp->tenant_direct_load_mgr();
  bool is_major_sstable_exist = false;
  bool has_freezed_ddl_kv = false;
  SCN ddl_commit_scn;
  if (OB_UNLIKELY(!tablet_handle.is_valid())) {
    ret = OB_INVALID_ARGUMENT;
    LOG_WARN("invalid argument", K(ret), K(tablet_handle));
  } else if (OB_ISNULL(tenant_direct_load_mgr)) {
    ret = OB_ERR_UNEXPECTED;
    LOG_WARN("unexpected error", K(ret));
  } else if (OB_FAIL(tenant_direct_load_mgr->get_tablet_mgr_and_check_major(
          tablet_handle.get_obj()->get_tablet_meta().tablet_id_,
          true, /* is_full_direct_load */
          direct_load_mgr_handle,
          is_major_sstable_exist))) {
    if (OB_ENTRY_NOT_EXIST == ret && is_major_sstable_exist) {
      ret = OB_SUCCESS;
    } else {
      LOG_WARN("get tablet direct load mgr failed", K(ret), "tablet_id", tablet_handle.get_obj()->get_tablet_meta().tablet_id_);
    }
  } else if (OB_FAIL(tablet_handle.get_obj()->get_ddl_kv_mgr(ddl_kv_mgr_handle))) {
    LOG_WARN("get ddl kv mgr failed", K(ret));
  } else if (FALSE_IT(ddl_commit_scn = direct_load_mgr_handle.get_full_obj()->get_commit_scn(tablet_handle.get_obj()->get_tablet_meta()))) {
  } else if (OB_FAIL(ddl_kv_mgr_handle.get_obj()->try_flush_ddl_commit_scn(ls, tablet_handle, direct_load_mgr_handle, ddl_commit_scn))) {
    LOG_WARN("try flush ddl commit scn failed", K(ret), "tablet_id", tablet_handle.get_obj()->get_tablet_meta().tablet_id_);
  } else if (OB_FAIL(ddl_kv_mgr_handle.get_obj()->check_has_freezed_ddl_kv(has_freezed_ddl_kv))) {
    LOG_WARN("check has freezed ddl kv failed", K(ret));
  } else if (OB_FAIL(direct_load_mgr_handle.get_full_obj()->prepare_ddl_merge_param(*tablet_handle.get_obj(), param))) {
    if (OB_EAGAIN != ret) {
      LOG_WARN("prepare major merge param failed", K(ret), "tablet_id", tablet_handle.get_obj()->get_tablet_meta().tablet_id_);
    }
  } else if (has_freezed_ddl_kv || param.is_commit_) {
    if (OB_FAIL(compaction::ObScheduleDagFunc::schedule_ddl_table_merge_dag(param))) {
      if (OB_SIZE_OVERFLOW != ret && OB_EAGAIN != ret) {
        LOG_WARN("schedule ddl merge dag failed", K(ret), K(param));
      }
    }
  }
  return ret;
}

// for minor dag, only hold table key array, should not hold tablet(memtable)
template <class T>
int ObTenantTabletScheduler::schedule_merge_execute_dag(
    const ObTabletMergeDagParam &param,
    ObLS *ls,
    ObTabletHandle &tablet_handle,
    const ObGetMergeTablesResult &result)
{
  int ret = OB_SUCCESS;
  const bool emergency = tablet_handle.get_obj()->get_tablet_meta().tablet_id_.is_ls_inner_tablet();
  T *merge_exe_dag = nullptr;

  if (result.handle_.get_count() > 1 &&
      !ObTenantTabletScheduler::check_tx_table_ready(*ls, result.scn_range_.end_scn_)) {
    ret = OB_EAGAIN;
    LOG_INFO("tx table is not ready. waiting for max_decided_log_ts ...", KR(ret),
             "merge_scn", result.scn_range_.end_scn_);
  } else if (OB_FAIL(share::g_mp->tenant_dag_scheduler()->alloc_dag(merge_exe_dag))) {
    LOG_WARN("failed to alloc dag", K(ret), K(param));
  } else if (OB_FAIL(merge_exe_dag->prepare_init(param,
                                                 result,
                                                 ls))) {
    LOG_WARN("failed to init dag", K(ret), K(result));
  } else if (OB_FAIL(share::g_mp->tenant_dag_scheduler()->add_dag(merge_exe_dag, emergency))) {
    if (OB_EAGAIN != ret) {
      LOG_WARN("failed to add dag", K(ret), KPC(merge_exe_dag));
    }
  } else {
    LOG_INFO("success to scheudle merge execute dag", K(ret), KP(merge_exe_dag), K(emergency));
  }
  if (OB_FAIL(ret) && nullptr != merge_exe_dag) {
    share::g_mp->tenant_dag_scheduler()->free_dag(*merge_exe_dag);
    merge_exe_dag = nullptr;
  }
  return ret;
}

int ObTenantTabletScheduler::schedule_minor_merge(
    ObLS *ls)
{
  int ret = OB_SUCCESS;
  ObLSStatusCache::LSState state = ObLSStatusCache::STATE_MAX;
  (void) ObLSStatusCache::check_ls_state(*ls, state);
  if (ObLSStatusCache::CAN_MERGE != state) {
    // no need to merge, do nothing
    ret = OB_STATE_NOT_MATCH;
  } else {
    ObTabletID tablet_id;
    ObTabletHandle tablet_handle;
    int tmp_ret = OB_SUCCESS;
    bool schedule_minor_flag = true;
    ObSEArray<ObTabletID, MERGE_BACTH_FREEZE_CNT> need_fast_freeze_tablets;
    need_fast_freeze_tablets.set_attr(ObMemAttr("MinorBatch"));
    int64_t start_time_us = 0;
    while (OB_SUCC(ret)) { // process the remaining tablets
      bool need_fast_freeze_flag = false;
      if (OB_FAIL(minor_tablet_iter_.get_next_tablet(tablet_handle))) {
        if (OB_ITER_END == ret) {
          ret = OB_SUCCESS;
          break;
        } else if (OB_LS_NOT_EXIST != ret) {
          LOG_WARN("failed to get tablet", K(ret), K(tablet_handle));
        }
      } else if (OB_UNLIKELY(!tablet_handle.is_valid())) {
        ret = OB_ERR_UNEXPECTED;
        LOG_WARN("invalid tablet handle", K(ret), K(tablet_handle));
      } else if (FALSE_IT(tablet_id = tablet_handle.get_obj()->get_tablet_meta().tablet_id_)) {
      } else if (OB_TMP_FAIL(schedule_tablet_minor(ls, tablet_handle, schedule_minor_flag, need_fast_freeze_flag))) {
        LOG_WARN("failed to schedule tablet minor", KR(tmp_ret), K(tablet_id));
      }
      if (need_fast_freeze_flag) {
        if (OB_TMP_FAIL(need_fast_freeze_tablets.push_back(tablet_id))) {
          LOG_WARN("failed to push back tablet_id for batch_freeze", KR(tmp_ret), K(tablet_id));
        }
      }
    } // end of while

    // ATTENTION! : do not use sync freeze because cyclic dependencies exist
    const bool is_sync = false;
    start_time_us = ObClockGenerator::getClock();
    if (need_fast_freeze_tablets.empty()) {
      // empty array. do not need freeze
    } else if (OB_TMP_FAIL(ls->tablet_freeze(need_fast_freeze_tablets,
                                            is_sync,
                                            0, /*timeout, useless for async one*/
                                            false, /*need_rewrite_meta*/
                                            ObFreezeSourceFlag::FAST_FREEZE))) {
      LOG_WARN("failt to batch freeze tablet", KR(tmp_ret), K(need_fast_freeze_tablets));
    } else {
      LOG_INFO("fast freeze by batch_tablet_freeze finish",
               KR(tmp_ret),
               "freeze cnt",
               need_fast_freeze_tablets.count(),
               "cost time(ns)",
               common::ObTimeUtility::current_time() - start_time_us);

      // Trigger TxData freeze after fast freeze
      ObTxTableGuard tx_table_guard;
      LOG_INFO("Trigger tx data freeze by fast freeze", K(tablet_id));
      if (OB_TMP_FAIL(ls->get_tx_table()->get_tx_table_guard(tx_table_guard))) {
        LOG_WARN("get tx table guard failed", KR(tmp_ret), K(tx_table_guard));
      } else {
        (void)tx_table_guard.self_freeze_task();
      }
    }
  } // else
  return ret;
}

// schedule_minor_flag = false means minor dag array is full
// but still need to loop tablet for ddl major & fast freeze
int ObTenantTabletScheduler::schedule_tablet_minor(
  ObLS *ls,
  ObTabletHandle tablet_handle,
  bool &schedule_minor_flag,
  bool &need_fast_freeze_flag)
{
  int ret = OB_SUCCESS;
  int tmp_ret = OB_SUCCESS;
  need_fast_freeze_flag = false;
  const ObTablet &tablet = *tablet_handle.get_obj();
  const ObTabletID &tablet_id = tablet.get_tablet_meta().tablet_id_;
  if (tablet.is_empty_shell()) {
    if (REACH_THREAD_TIME_INTERVAL(PRINT_LOG_INTERVAL)) {
      LOG_INFO("can't schedule minor for empty shell tablet", K(ret), K(tablet_id));
    }
  } else if (schedule_minor_flag
      && OB_TMP_FAIL(schedule_tablet_minor_merge<ObTabletMergeExecuteDag>(ls, tablet_handle))) {
    if (OB_SIZE_OVERFLOW == tmp_ret) {
      schedule_minor_flag = false;
    } else if (OB_EAGAIN != tmp_ret) {
      LOG_ERROR("failed to schedule tablet merge", K(tmp_ret), K(tablet_id));
    }
  }
  if (!tablet_id.is_ls_inner_tablet()) { // data tablet
    if (OB_TMP_FAIL(schedule_ddl_tablet_merge(ls, tablet_handle))) {
      if (OB_SIZE_OVERFLOW != tmp_ret && OB_EAGAIN != tmp_ret) {
        LOG_ERROR("failed to schedule tablet ddl merge", K(tmp_ret), K(tablet_handle));
      }
    }

    if (!fast_freeze_checker_.need_check() || tablet_id.is_inner_tablet() || tablet_id.is_ls_inner_tablet()) {
    } else if (OB_TMP_FAIL(fast_freeze_checker_.check_need_fast_freeze(tablet, need_fast_freeze_flag))) {
      LOG_WARN("failed to check need fast freeze", K(tmp_ret), K(tablet_handle));
    }

    if (share::g_mp->tenant_tablet_stat_mgr()->contain_extreme_tablet()) {
      bool unused_create_dag = false; // unused
      if (OB_TMP_FAIL(ObTenantTabletScheduler::try_schedule_adaptive_merge(ls, tablet_handle,
            ObAdaptiveMergePolicy::SCHEDULE_META, 0 /*update_cnt*/, 0 /*delete_cnt*/, unused_create_dag))) {
        LOG_WARN("failed to schedule tablet meta merge", K(tmp_ret), K(tablet_id));
      }
    }
  }
  return ret;
}

int ObTenantTabletScheduler::schedule_ddl_tablet_merge(
    ObLS *ls,
    ObTabletHandle &tablet_handle)
{
  int ret = OB_SUCCESS;
  ObDDLKvMgrHandle ddl_kv_mgr_handle;
  const ObTabletID tablet_id = tablet_handle.is_valid() ? tablet_handle.get_obj()->get_tablet_meta().tablet_id_ : ObTabletID();
  if (OB_UNLIKELY(!tablet_handle.is_valid())) {
    ret = OB_INVALID_ARGUMENT;
    LOG_WARN("invalid arg", K(ret), K(tablet_handle));
  } else if (OB_FAIL(tablet_handle.get_obj()->get_ddl_kv_mgr(ddl_kv_mgr_handle))) {
    if (OB_ENTRY_NOT_EXIST == ret) {
      LOG_TRACE("kv mgr not exist", K(ret), K(tablet_id));
      ret = OB_SUCCESS; /* for empty table, ddl kv may not exist*/
    } else {
      LOG_WARN("get ddl kv mgr failed", K(ret), K(tablet_id));
    }
  } else {
    if (OB_FAIL(schedule_tablet_ddl_major_merge(ls, tablet_handle))) {
      if (OB_SIZE_OVERFLOW != ret && OB_EAGAIN != ret) {
        LOG_ERROR("failed to schedule tablet ddl merge", K(ret), K(tablet_id));
      } else {
        LOG_TRACE("schedule ddl major merge failed", K(ret), K(tablet_id));
      }
    }
  }
  LOG_TRACE("schedule ddl tablet merge", K(ret), K(tablet_id));
  return ret;
}

int ObTenantTabletScheduler::schedule_all_tablets_medium()
{
  int ret = OB_SUCCESS;
  if (IS_NOT_INIT) {
    ret = OB_NOT_INIT;
    LOG_WARN("ObTenantTabletScheduler has not been inited", K(ret));
  } else if (!tenant_status_.is_inited() && OB_FAIL(tenant_status_.init_or_refresh())) {
    if (OB_NEED_WAIT != ret) {
      LOG_WARN("failed to init tenant_status", KR(ret), K_(tenant_status));
    }
  } else {
    const int64_t merge_version = get_frozen_version();
    if (merge_version > merged_version_) {
      try_finish_merge_progress(merge_version);
    }
    if (OB_FAIL(medium_loop_.init(get_schedule_batch_size()))) {
      LOG_WARN("failed to init medium loop", K(ret));
    } else {
      LOG_INFO("start schedule all tablet merge", K(merge_version));
      if (OB_FAIL(medium_loop_.loop())) {
        LOG_WARN("failed to medium loop", K(ret));
      }
    }
  }
  return ret;
}

bool ObTenantTabletScheduler::need_fast_medium_loop() const
{
  const int64_t frozen_version = get_frozen_version();
  return !is_stop_
      && could_major_merge_start()
      && frozen_version > ObBasicMergeScheduler::INIT_COMPACTION_SCN
      && frozen_version > get_merged_version();
}

int ObTenantTabletScheduler::user_request_schedule_medium_merge(
  const common::ObTabletID &tablet_id)
{
  int ret = OB_SUCCESS;
  ObLS *ls = nullptr;
  ObTabletHandle tablet_handle;

  LOG_INFO("user_request_schedule_medium_merge", K(ret), K(tablet_id));
  if (IS_NOT_INIT) {
    ret = OB_NOT_INIT;
    LOG_WARN("ObTenantTabletScheduler has not been inited", K(ret));
  } else if (OB_UNLIKELY(!tablet_id.is_valid())) {
    ret = OB_INVALID_ARGUMENT;
    LOG_WARN("invalid argument", K(ret), K(tablet_id));
  } else if (OB_UNLIKELY(tablet_id.is_ls_inner_tablet())) {
    ret = OB_NOT_SUPPORTED;
    LOG_WARN("not supported to schedule medium for ls inner tablet", K(ret), K(tablet_id));
  } else if (!could_major_merge_start()) {
    ret = OB_MAJOR_FREEZE_NOT_ALLOW;
    LOG_WARN("major compaction is suspended", K(ret), K(tablet_id));
  } else if (OB_FAIL(share::g_mp->ls_service()->get_ls(ls))) {
    LOG_WARN("failed to get ls", K(ret));
  } else {
    const int64_t merge_version = get_frozen_version();
    const ObAdaptiveMergePolicy::AdaptiveMergeReason reason = ObAdaptiveMergePolicy::USER_REQUEST;
    ObScheduleTabletFunc func(merge_version, reason);
    if (OB_FAIL(func.init(ls))) {
      if (OB_STATE_NOT_MATCH != ret) {
        LOG_ERROR("failed to initialize compaction status", KR(ret), K(func));
      } else {
        LOG_WARN("not support schedule medium for ls", K(ret), K(tablet_id), K(func));
      }
    } else if (OB_FAIL(ls->get_tablet_svr()->get_tablet(
                 tablet_id, tablet_handle, 0 /*timeout_us*/))) {
      LOG_WARN("get tablet failed", K(ret), K(tablet_id));
    } else if (OB_FAIL(func.request_schedule_new_round(tablet_handle, true/*user_request*/))) {
      LOG_WARN("failed to request schedule new round", K(ret), K(tablet_id));
    }
  }
  return ret;
}

int ObTenantTabletScheduler::get_min_dependent_schema_version(int64_t &min_schema_version)
{
  int ret = OB_SUCCESS;
  min_schema_version = OB_INVALID_VERSION;
  share::ObFreezeInfo freeze_info;
  if (OB_FAIL(share::g_mp->tenant_freeze_info_mgr()->get_min_dependent_freeze_info(freeze_info))) {
    if (OB_ENTRY_NOT_EXIST == ret) {
      LOG_WARN("freeze info is not exist", K(ret));
    } else {
      LOG_WARN("failed to get freeze info", K(ret));
    }
  } else {
    min_schema_version = freeze_info.schema_version_;
  }
  return ret;
}

#ifdef ERRSIM
void ObTenantTabletScheduler::errsim_after_mini_schedule_adaptive(
    const ObTabletID &tablet_id,
    const ObAdaptiveMergePolicy::AdaptiveCompactionEvent &event,
    bool &medium_is_cooling_down,
    ObAdaptiveMergePolicy::AdaptiveMergeReason &reason)
{
  int ret = OB_SUCCESS;
  // ATTENTION !!!: 2 tracepoint can only hit one at once
  #define SCHEDULE_META_MEDIUM_ERRSIM(tracepoint, cooling_down)              \
    do {                                                                     \
      if (OB_SUCC(ret)) {                                                    \
        ret = OB_E((EventTable::tracepoint)) OB_SUCCESS;                     \
        if (OB_FAIL(ret)) {                                                  \
          ret = OB_SUCCESS;                                                  \
          STORAGE_LOG(INFO, "ERRSIM " #tracepoint);                          \
          reason = ObAdaptiveMergePolicy::TOMBSTONE_SCENE;                   \
          medium_is_cooling_down = cooling_down;                             \
        }                                                                    \
      }                                                                      \
    } while(0);
  SCHEDULE_META_MEDIUM_ERRSIM(EN_COMPACTION_SCHEDULE_MEDIUM_MERGE_AFTER_MINI, false /*cooling_down*/);
  SCHEDULE_META_MEDIUM_ERRSIM(EN_COMPACTION_SCHEDULE_META_MERGE, true /*cooling_down*/);
  #undef SCHEDULE_META_MEDIUM_ERRSIM

  if (OB_FAIL(ret)) {
  } else if (OB_FAIL(EN_COMPACTION_DISABLE_META_MERGE_AFTER_MINI)) {
    reason = ObAdaptiveMergePolicy::NONE;
    LOG_INFO("ERRSIM EN_COMPACTION_DISABLE_META_MERGE_AFTER_MINI: disable meta merge after mini", K(ret), K(tablet_id));
  } 

  bool is_tombstone_scene = ObAdaptiveMergePolicy::NONE != reason;
  STORAGE_LOG(INFO, "try_schedule_adaptive_merge hit errsim", K(ret), K(is_tombstone_scene), K(medium_is_cooling_down));
}
#endif

int ObTenantTabletScheduler::try_schedule_adaptive_merge(
    ObLS *ls,
    ObTabletHandle &tablet_handle,
    const ObAdaptiveMergePolicy::AdaptiveCompactionEvent &event,
    const int64_t update_row_cnt,
    const int64_t delete_row_cnt, 
    bool &create_dag)
{
  int ret = OB_SUCCESS;
  int tmp_ret = OB_SUCCESS;
  create_dag = false;
  if (OB_UNLIKELY(OB_ISNULL(ls) || !tablet_handle.is_valid() || !ObAdaptiveMergePolicy::need_schedule_meta(event))) {
    ret = OB_INVALID_ARGUMENT;
    LOG_WARN("invalid argument", K(ret), KP(ls), K(tablet_handle), K(event));
  } else {
    ObTableModeFlag mode = ObTableModeFlag::TABLE_MODE_NORMAL;
    ObAdaptiveMergePolicy::AdaptiveMergeReason reason = ObAdaptiveMergePolicy::NONE;
    const ObTablet *tablet = tablet_handle.get_obj();
    const ObTabletID &tablet_id = tablet->get_tablet_id();
    bool medium_is_cooling_down = tablet->get_last_major_snapshot_version() + ObAdaptiveMergePolicy::MEDIUM_COOLING_TIME_THRESHOLD_NS > ObTimeUtility::current_time_ns();
    if (OB_FAIL(ObAdaptiveMergePolicy::check_adaptive_merge_reason_for_event(
        *ls,
        *tablet,
        event,
        update_row_cnt,
        delete_row_cnt,
        mode,
        reason))) {
      LOG_WARN("failed to check adaptive merge reason", K(ret), KP(ls), K(tablet_handle));
#ifdef ERRSIM
    } else if (ObAdaptiveMergePolicy::AdaptiveCompactionEvent::SCHEDULE_AFTER_MINI ==event 
            && FALSE_IT(errsim_after_mini_schedule_adaptive(tablet_id, event, medium_is_cooling_down, reason))) {
#endif
    } else if (ObAdaptiveMergePolicy::NONE == reason) {
    } else if (ObAdaptiveMergePolicy::is_schedule_medium(mode) && ObAdaptiveMergePolicy::need_schedule_medium(event) && !medium_is_cooling_down) {
      ObScheduleTabletFunc func(0/*merge_version*/, reason);
      bool unused_tablet_merge_finish = false;
      if (OB_TMP_FAIL(func.init(ls))) {
        if (OB_STATE_NOT_MATCH != tmp_ret) {
          LOG_ERROR("failed to initialize compaction status", KR(tmp_ret));
        }
      } else if (OB_TMP_FAIL(func.schedule_tablet(tablet_handle, unused_tablet_merge_finish))) {
        LOG_WARN("failed to schedule tablet", KR(tmp_ret), K(tablet_id));
      }
    } else if (ObAdaptiveMergePolicy::is_schedule_meta(mode)) {
      if (OB_TMP_FAIL(ObTenantTabletScheduler::schedule_tablet_meta_merge(ls, tablet_handle, create_dag))) {
        LOG_ERROR_RET(tmp_ret, "failed to schedule meta merge for tablet", K(tablet_id));
      } else if (create_dag) {
         LOG_INFO("[Buffer-Opt] Try to schedule tablet meta merge background", K(ret), K(tablet_id));
      }
    }

    if (ObAdaptiveMergePolicy::AdaptiveCompactionEvent::SCHEDULE_AFTER_MINI == event) {
      LOG_INFO("[Buffer-Opt] Try to schedule tablet medium/meta after mini", K(ret), K(tmp_ret), K(tablet_id), "is_tombstone_scene", ObAdaptiveMergePolicy::NONE != reason,
        "mode", table_mode_flag_to_str(mode), K(medium_is_cooling_down), K(event), K(update_row_cnt), K(delete_row_cnt), K(create_dag));
    } else if (REACH_THREAD_TIME_INTERVAL(30 * 1000 * 1000 /*30s*/)) {
      LOG_INFO("Try schedule tablet adaptive merge", K(ret), K(tmp_ret), K(tablet_id), "is_tombstone_scene", ObAdaptiveMergePolicy::NONE != reason, K(event));
    }
  }
  return ret;
}

} // namespace storage
} // namespace oceanbase
