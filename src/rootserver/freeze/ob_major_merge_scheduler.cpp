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

#define USING_LOG_PREFIX RS_COMPACTION

#include "rootserver/freeze/ob_major_merge_scheduler.h"
#include "rootserver/ob_rs_event_history_table_operator.h" // for ROOTSERVICE_EVENT_ADD
#include "share/ob_tablet_meta_table_compaction_operator.h"
#include "share/ob_global_merge_table_operator.h"

namespace oceanbase
{
namespace rootserver
{
using namespace oceanbase::common;
using namespace oceanbase::share;

///////////////////////////////////////////////////////////////////////////////

int ObMajorMergeIdling::init(const uint64_t tenant_id)
{
  int ret = OB_SUCCESS;
  if (!is_valid_tenant_id(tenant_id)) {
    ret = OB_INVALID_ARGUMENT;
    LOG_WARN("invalid argument", KR(ret), K(tenant_id));
  } else {
    tenant_id_ = tenant_id;
  }
  return ret;
}

int64_t ObMajorMergeIdling::get_idle_interval_us()
{
  int64_t interval_us = DEFAULT_SCHEDULE_IDLE_US;
  omt::ObTenantConfigGuard tenant_config(TENANT_CONF(tenant_id_));
  if (OB_LIKELY(tenant_config.is_valid())) {
    interval_us = tenant_config->merger_check_interval;
  }
  return interval_us;
}

///////////////////////////////////////////////////////////////////////////////

ObMajorMergeScheduler::ObMajorMergeScheduler(const uint64_t tenant_id)
  : ObFreezeReentrantThread(tenant_id),
    is_inited_(false),
    is_primary_service_(true),
    fail_count_(0),
    first_check_merge_us_(0),
    idling_(stop_),
    merge_info_mgr_(nullptr),
    config_(nullptr),
    merge_strategy_(),
    progress_checker_(nullptr)
{
}

ObMajorMergeScheduler::~ObMajorMergeScheduler()
{
  if (OB_NOT_NULL(progress_checker_)) {
    common::ob_delete(progress_checker_);
    progress_checker_ = nullptr;
  }
}

int ObMajorMergeScheduler::init(
    const bool is_primary_service,
    ObMajorMergeInfoManager &merge_info_mgr,
    share::schema::ObMultiVersionSchemaService &schema_service,
    common::ObServerConfig &config,
    common::ObMySQLProxy &sql_proxy)
{
  int ret = OB_SUCCESS;
  void *buf = nullptr;

  if (IS_INIT) {
    ret = OB_INIT_TWICE;
    LOG_WARN("init twice", KR(ret), K_(tenant_id));
  } else if (OB_INVALID_ID == tenant_id_) {
    ret = OB_ERR_UNEXPECTED;
    LOG_WARN("invalid tenant id", KR(ret), K_(tenant_id));
  } else if (OB_FAIL(merge_strategy_.init(tenant_id_, &merge_info_mgr.get_zone_merge_mgr()))) {
    LOG_WARN("fail to init tenant zone merge strategy", KR(ret), K_(tenant_id));
  } else {
    if (OB_ISNULL(buf = common::ob_malloc(sizeof(ObMajorMergeProgressChecker), ObMemAttr(tenant_id_, "mrg_prog_cker")))) {
      ret = OB_ALLOCATE_MEMORY_FAILED;
      LOG_WARN("fail to alloc memory", KR(ret), K_(tenant_id));
    } else {
      progress_checker_ = new(buf) ObMajorMergeProgressChecker(tenant_id_, stop_);
    }
  }

  if (FAILEDx(progress_checker_->init(is_primary_service,
                                      sql_proxy,
                                      schema_service,
                                      merge_info_mgr))) {
    LOG_WARN("fail to init progress_checker", KR(ret));
  } else if (OB_FAIL(idling_.init(tenant_id_))) {
    LOG_WARN("fail to init idling", KR(ret), K_(tenant_id));
  } else {
    first_check_merge_us_ = 0;
    is_primary_service_ = is_primary_service;
    merge_info_mgr_ = &merge_info_mgr;
    config_ = &config;
    sql_proxy_ = &sql_proxy;
    is_inited_ = true;
  }
  return ret;
}

int ObMajorMergeScheduler::start()
{
  int ret = OB_SUCCESS;
  lib::Threads::set_run_wrapper(MTL_CTX());
  if (IS_NOT_INIT) {
    ret = OB_NOT_INIT;
    LOG_WARN("ObMajorMergeScheduler not init", KR(ret));
  } else if (OB_FAIL(create(MAJOR_MERGE_SCHEDULER_THREAD_CNT, "MergeScheduler"))) {
    LOG_WARN("fail to create thread", KR(ret), K_(tenant_id));
  } else if (OB_FAIL(ObRsReentrantThread::start())) {
    LOG_WARN("fail to start thread", KR(ret), K_(tenant_id));
  } else {
    LOG_INFO("succ to start ObMajorMergeScheduler", K_(tenant_id));
  }
  return ret;
}

void ObMajorMergeScheduler::run3()
{
  LOG_INFO("major merge scheduler will run", K_(tenant_id));
  int ret = OB_SUCCESS;
  if (IS_NOT_INIT) {
    ret = OB_NOT_INIT;
    LOG_WARN("not init", KR(ret));
  }

  if (OB_SUCC(ret)) {
    while (!stop_) {
      update_last_run_timestamp();
      if (OB_FAIL(do_work())) {
        LOG_WARN("fail to do major scheduler work", KR(ret), K_(tenant_id), "cur_epoch", get_epoch());
      }
      // out of do_work, there must be no major merge on this server. therefore, here, clear
      // compcation diagnose infos that stored in memory of this server.
      progress_checker_->reset_uncompacted_tablets();

      int tmp_ret = OB_SUCCESS;
      if (OB_TMP_FAIL(try_idle(DEFAULT_IDLE_US, ret))) {
        LOG_WARN("fail to try_idle", KR(ret), KR(tmp_ret));
      }
      ret = OB_SUCCESS; // ignore ret, continue
    }
  }

  LOG_INFO("major merge scheduler will exit", K_(tenant_id));
}

int ObMajorMergeScheduler::try_idle(
    const int64_t ori_idle_time_us,
    const int work_ret)
{
  int ret = OB_SUCCESS;
  const int64_t IMMEDIATE_RETRY_CNT = 3;
  int64_t idle_time_us = ori_idle_time_us;
  int64_t merger_check_interval = idling_.get_idle_interval_us();
  const int64_t start_time_us = ObTimeUtil::current_time();
  bool clear_cached_info = false;

  if (OB_SUCCESS == work_ret) {
    fail_count_ = 0;
  } else {
    ++fail_count_;
  }

  if (0 == fail_count_) {
    // default idle
  } else if (fail_count_ < IMMEDIATE_RETRY_CNT) {
    idle_time_us = fail_count_ * DEFAULT_IDLE_US;
    if (idle_time_us > merger_check_interval) {
      idle_time_us = merger_check_interval;
    }
    LOG_WARN("fail to major merge, will immediate retry", K_(tenant_id),
             K_(fail_count), LITERAL_K(IMMEDIATE_RETRY_CNT), K(idle_time_us));
  } else {
    idle_time_us = merger_check_interval;
    LOG_WARN("major merge failed more than immediate cnt, turn to idle status",
             K_(fail_count), LITERAL_K(IMMEDIATE_RETRY_CNT), K(idle_time_us));
  }

  if (is_paused()) {
    const uint64_t start_ts = ObTimeUtility::fast_current_time();
    while (OB_SUCC(ret) && is_paused() && !stop_) {
      LOG_INFO("major_merge_scheduler is paused", K_(tenant_id), K(idle_time_us),
               "epoch", get_epoch());
      if (OB_FAIL(idling_.idle(idle_time_us))) {
        LOG_WARN("fail to idle", KR(ret), K(idle_time_us));
      } else if (ObTimeUtility::fast_current_time() > start_ts + PAUSED_WAITING_CLEAR_MEMORY_THRESHOLD) {
        (void) progress_checker_->clear_cached_info();
        clear_cached_info = true;
        LOG_INFO("clear cached info when idling", KR(ret), K(start_ts));
      }
    }
    LOG_INFO("major_merge_scheduler is not idling", KR(ret), K_(tenant_id), K(idle_time_us),
             K_(stop), "epoch", get_epoch());
  } else if (OB_FAIL(idling_.idle(idle_time_us))) {
    LOG_WARN("fail to idle", KR(ret), K(idle_time_us));
  }
  if (OB_SUCC(ret) && clear_cached_info) {
    if (OB_FAIL(do_before_major_merge(false/*start_merge*/))) {
      LOG_WARN("failed to set basic info again", KR(ret));
    }
  }
  const int64_t cost_time_us = ObTimeUtil::current_time() - start_time_us;

  return ret;
}

int ObMajorMergeScheduler::get_uncompacted_tablets(
    ObArray<ObTabletReplica> &uncompacted_tablets,
    ObArray<uint64_t> &uncompacted_table_ids) const
{
  int ret = OB_SUCCESS;
  if (IS_NOT_INIT) {
    ret = OB_NOT_INIT;
    LOG_WARN("not init", KR(ret), K_(tenant_id));
  } else {
    if (OB_FAIL(progress_checker_->get_uncompacted_tablets(uncompacted_tablets, uncompacted_table_ids))) {
      LOG_WARN("fail to get uncompacted tablets", KR(ret), K_(tenant_id));
    }
  }
  return ret;
}

int ObMajorMergeScheduler::do_work()
{
  int ret = OB_SUCCESS;

  HEAP_VARS_2((ObZoneMergeInfoArray, info_array), (ObGlobalMergeInfo, global_info)) {
    if (IS_NOT_INIT) {
      ret = OB_NOT_INIT;
      LOG_WARN("not init", KR(ret), K_(tenant_id));
    } else {
      FREEZE_TIME_GUARD;
      if (OB_FAIL(merge_info_mgr_->get_zone_merge_mgr().try_reload())) {
        LOG_WARN("fail to try reload", KR(ret), K_(tenant_id));
      }
    }
    if (FAILEDx(merge_info_mgr_->get_zone_merge_mgr().get_snapshot(global_info, info_array))) {
      LOG_WARN("fail to get merge info", KR(ret), K_(tenant_id));
    } else {
      bool need_merge = true;
      if (global_info.is_merge_error()) {
        need_merge = false;
        LOG_WARN("cannot do this round major merge cuz is_merge_error", K(need_merge), K(global_info));
      } else if (global_info.is_last_merge_complete()) {
        if (global_info.global_broadcast_scn() == global_info.frozen_scn()) {
          need_merge = false;
        } else if (global_info.global_broadcast_scn() < global_info.frozen_scn()) {
          // should do next round merge with higher broadcast_scn
          if (OB_FAIL(generate_next_global_broadcast_scn())) {
            LOG_WARN("fail to generate next broadcast scn", KR(ret), K(global_info));
          }
        } else {
          ret = OB_ERR_UNEXPECTED;
          LOG_WARN("invalid frozen_scn", KR(ret), K(global_info));
        }
      } else {
        // do major freeze with current broadcast_scn
      }

      if (OB_SUCC(ret) && need_merge) {
        if (OB_FAIL(do_before_major_merge(true/*start_merge*/))) {
          LOG_WARN("fail to do before major merge", KR(ret));
        } else if (OB_FAIL(do_one_round_major_merge())) {
          LOG_WARN("fail to do major merge", KR(ret));
        }
      }
    }

    LOG_TRACE("finish do merge scheduler work", KR(ret), K(global_info));
    // is_merging = false, except for switchover
    check_merge_interval_time(false);
  }
  return ret;
}

int ObMajorMergeScheduler::do_before_major_merge(const bool start_merge)
{
  int ret = OB_SUCCESS;
  share::SCN global_broadcast_scn;
  share::ObFreezeInfo freeze_info;
  global_broadcast_scn.set_min();
  FREEZE_TIME_GUARD;
  if (OB_FAIL(merge_info_mgr_->get_zone_merge_mgr().get_global_broadcast_scn(global_broadcast_scn))) {
    LOG_WARN("fail to get global broadcast scn", KR(ret), K_(tenant_id));
  } else if (OB_FAIL(merge_info_mgr_->get_freeze_info_mgr().get_freeze_info(global_broadcast_scn, freeze_info))) {
    LOG_WARN("fail to get freeze info", KR(ret), K_(tenant_id));
  } else if (OB_FAIL(progress_checker_->set_basic_info(freeze_info))) {
    LOG_WARN("failed to set basic info of progress checker", KR(ret), K(global_broadcast_scn));
  } else if (start_merge) {
    if (OB_FAIL(ObColumnChecksumErrorOperator::delete_column_checksum_err_info(
        *sql_proxy_, tenant_id_, global_broadcast_scn))) {
      LOG_WARN("fail to delete column checksum error info", KR(ret), K(global_broadcast_scn));
    }
  }
  return ret;
}

int ObMajorMergeScheduler::do_one_round_major_merge()
{
  int ret = OB_SUCCESS;
  int tmp_ret = OB_SUCCESS;

  HEAP_VAR(ObGlobalMergeInfo, global_info) {
    LOG_INFO("start to do one round major_merge");
    // loop until 'this round major merge finished' or 'epoch changed'
    while (!stop_ && !is_paused()) {
      update_last_run_timestamp();
      ObCurTraceId::init(GCONF.self_addr_);
      ObZoneArray to_merge_zone;
      // Place is_last_merge_complete() to the head of this while loop.
      // So as to break this loop at once, when the last merge is complete.
      // Otherwise, may run one extra loop that should not run, and thus incur error.
      // 
      if (OB_FAIL(merge_info_mgr_->get_zone_merge_mgr().get_snapshot(global_info))) {
        LOG_WARN("fail to get zone global merge info", KR(ret));
      } else if (global_info.is_last_merge_complete()) {
        // this round major merge is complete
        break;
      } else if (OB_FAIL(get_next_merge_zones(to_merge_zone))) {  // get zones to schedule merge
        LOG_WARN("fail to get next merge zones", KR(ret));
      } else if (to_merge_zone.empty()) {
        // no new zone to merge
        LOG_INFO("no more zone need to merge", K_(tenant_id), K(global_info));
      } else if (OB_FAIL(schedule_zones_to_merge(to_merge_zone))) {
        LOG_WARN("fail to schedule zones to merge", KR(ret), K(to_merge_zone));
      }
      // Need to update_merge_status, even though to_merge_zone is empty.
      // E.g., in the 1st loop, already schedule all zones to merge, but not finish major merge.
      // In the 2nd loop, though to_merge_zone is empty, need continue to update_merge_status.
      if (FAILEDx(update_merge_status(global_info.global_broadcast_scn()))) {
        LOG_WARN("fail to update merge status", KR(ret));
        if (TC_REACH_TIME_INTERVAL(ADD_EVENT_INTERVAL)) {
          ROOTSERVICE_EVENT_ADD("daily_merge", "merge_process", K_(tenant_id),
                            "check merge progress fail", ret,
                            "global_broadcast_scn", global_info.global_broadcast_scn_,
                            "service_addr", GCONF.self_addr_);
        }
      }
      // wait some time to merge
      if (OB_SUCCESS != (tmp_ret = try_idle(DEFAULT_IDLE_US, ret))) {
        LOG_WARN("fail to idle", KR(ret));
      }

      ret = OB_SUCCESS;
      // treat as is_merging = true, even though last merge complete
      check_merge_interval_time(true);
      LOG_INFO("finish one round of loop in do_one_round_major_merge", K(global_info));
    }
  }
  LOG_INFO("finish do_one_round_major_merge");

  return ret;
}

int ObMajorMergeScheduler::generate_next_global_broadcast_scn()
{
  int ret = OB_SUCCESS;

  SCN new_global_broadcast_scn;
  // MERGE_STATUS: IDLE -> MERGING
  if (OB_FAIL(merge_info_mgr_->get_zone_merge_mgr().generate_next_global_broadcast_scn(new_global_broadcast_scn))) {
    LOG_WARN("fail to generate next broadcast scn", KR(ret));
  } else {
    LOG_INFO("start to schedule new round merge", K(new_global_broadcast_scn), K_(tenant_id));
    ROOTSERVICE_EVENT_ADD("daily_merge", "merging", K_(tenant_id), "global_broadcast_scn",
                          new_global_broadcast_scn.get_val_for_inner_table_field(),
                          "zone", "global_zone");
  }

  return ret;
}

int ObMajorMergeScheduler::get_next_merge_zones(ObZoneArray &to_merge)
{
  int ret = OB_SUCCESS;
  if (IS_NOT_INIT) {
    ret = OB_NOT_INIT;
    LOG_WARN("not init", KR(ret), K_(is_inited), K_(tenant_id));
  } else if (OB_FAIL(merge_strategy_.get_next_zone(to_merge))) {
    LOG_WARN("fail to get mext merge zones", KR(ret), K_(tenant_id));
  }

  return ret;
}

int ObMajorMergeScheduler::schedule_zones_to_merge(
    const ObZoneArray &to_merge)
{
  int ret = OB_SUCCESS;

  HEAP_VARS_2((ObZoneMergeInfoArray, info_array), (ObGlobalMergeInfo, global_info)) {
    if (to_merge.empty()) {
      ret = OB_INVALID_ARGUMENT;
      LOG_WARN("invalid argument", KR(ret), K(to_merge));
    } else if (OB_FAIL(merge_info_mgr_->get_zone_merge_mgr().get_snapshot(global_info, info_array))) {
      LOG_WARN("get zone info snapshot failed", KR(ret));
    }

    // set zone merging flag
    if (OB_SUCC(ret)) {
      HEAP_VAR(ObZoneMergeInfo, tmp_info) {
        FOREACH_CNT_X(zone, to_merge, (OB_SUCCESS == ret) && !stop_) {
          tmp_info.reset();
          tmp_info.tenant_id_ = tenant_id_;
          if (OB_FAIL(merge_info_mgr_->get_zone_merge_mgr().get_zone_merge_info(tmp_info))) {
            LOG_WARN("fail to get zone", KR(ret), K(*zone));
          } else if (0 == tmp_info.is_merging_.get_value()) {
            if (OB_FAIL(set_zone_merging(*zone))) {
              LOG_WARN("fail to set zone merging", KR(ret), K(*zone));
            }
          }
        }
      }
    }

    // start merge
    if (OB_SUCC(ret)) {
      if (OB_FAIL(start_zones_merge(to_merge))) {
        LOG_WARN("fail to start zone merge", KR(ret), K(to_merge));
      } else {
        LOG_INFO("start to schedule zone merge", K(to_merge));
      }
    }
  }

  return ret;
}

int ObMajorMergeScheduler::start_zones_merge(const ObZoneArray &to_merge)
{
  int ret = OB_SUCCESS;
  SCN global_broadcast_scn;

  if (IS_NOT_INIT) {
    ret = OB_NOT_INIT;
    LOG_WARN("not init", KR(ret));
  } else if (OB_FAIL(merge_info_mgr_->get_zone_merge_mgr().get_global_broadcast_scn(global_broadcast_scn))) {
    LOG_WARN("fail to get global broadcast scn", KR(ret));
  } else {
    for (int64_t i = 0; !stop_ && OB_SUCC(ret) && (i < to_merge.count()); ++i) {
      HEAP_VAR(ObZoneMergeInfo, tmp_info) {
        tmp_info.tenant_id_ = tenant_id_;
        if (OB_FAIL(merge_info_mgr_->get_zone_merge_mgr().get_zone_merge_info(tmp_info))) {
          LOG_WARN("fail to get zone", KR(ret));
        } else if (0 == tmp_info.is_merging_.get_value()) {
          ret = OB_ERR_UNEXPECTED;
          LOG_INFO("zone is not merging, can not start merge", KR(ret), K(tmp_info));
        } else if (OB_FAIL(merge_info_mgr_->get_zone_merge_mgr().start_zone_merge(to_merge.at(i)))) {
          LOG_WARN("fail to start zone merge", KR(ret), "zone", to_merge.at(i));
        } else {
          ROOTSERVICE_EVENT_ADD("daily_merge", "start_merge", K_(tenant_id), "zone", to_merge.at(i),
              "global_broadcast_scn", global_broadcast_scn.get_val_for_inner_table_field()) ;
        }
      }
    }
  }
  return ret;
}

int ObMajorMergeScheduler::update_merge_status(
  const SCN &global_broadcast_scn)
{
  int ret = OB_SUCCESS;
  int tmp_ret = OB_SUCCESS;

  DEBUG_SYNC(RS_VALIDATE_CHECKSUM);
  if (IS_NOT_INIT) {
    ret = OB_NOT_INIT;
    LOG_WARN("not inited", KR(ret));
  } else if (OB_FAIL(progress_checker_->check_progress())) {
    LOG_WARN("fail to check merge status", KR(ret), K_(tenant_id));
    if (OB_CHECKSUM_ERROR == ret) {
      if (OB_TMP_FAIL(merge_info_mgr_->get_zone_merge_mgr().set_merge_status(ObZoneMergeInfo::CHECKSUM_ERROR))) {
        LOG_WARN("fail to set merge error", KR(ret), KR(tmp_ret), K_(tenant_id));
      }
    }
  } else {
    const compaction::ObBasicMergeProgress &progress = progress_checker_->get_merge_progress();
    LOG_INFO("succcess to update merge status", K(ret), K(global_broadcast_scn), K(progress));
    if (OB_FAIL(handle_merge_progress(progress, global_broadcast_scn))) {
      LOG_WARN("fail to handle all zone merge", KR(ret), K(global_broadcast_scn));
    }
  }

  return ret;
}

int ObMajorMergeScheduler::handle_merge_progress(
    const compaction::ObBasicMergeProgress &progress,
    const share::SCN &global_broadcast_scn)
{
  int ret = OB_SUCCESS;
  if (progress.is_merge_finished() || progress.is_merge_abnomal()) {
    if (progress.is_merge_abnomal()) {
      LOG_WARN("merge progress is abnomal, finish progress anyway", K(global_broadcast_scn), K(progress));
    } else {
      LOG_INFO("merge completed", K(global_broadcast_scn), K(progress));
    }
    if (OB_FAIL(try_update_global_merged_scn())) { // MERGE_STATUS: change to IDLE
      LOG_WARN("fail to update global_merged_scn", KR(ret), K_(tenant_id));
    }
  } else {
    LOG_INFO("this round of traversal is completed, but there are still tablets/tables that have not been merged",
      K(ret), K(global_broadcast_scn), K(progress));
  }
  return ret;
}

int ObMajorMergeScheduler::try_update_global_merged_scn()
{
  int ret = OB_SUCCESS;
  HEAP_VAR(ObGlobalMergeInfo, global_info) {
    uint64_t global_broadcast_scn_val = UINT64_MAX;
    if (IS_NOT_INIT) {
      ret = OB_NOT_INIT;
      LOG_WARN("not inited", KR(ret));
    } else if (OB_FAIL(merge_info_mgr_->get_zone_merge_mgr().get_snapshot(global_info))) {
      LOG_WARN("fail to get global merge info", KR(ret));
    } else if (global_info.is_merge_error()) {
      LOG_WARN("should not update global merged scn, cuz is_merge_error is true", K(global_info));
    } else if (global_info.last_merged_scn() != global_info.global_broadcast_scn()) {
      if (FALSE_IT(global_broadcast_scn_val = global_info.global_broadcast_scn_.get_scn_val())) {
      } else if (OB_FAIL(update_all_tablets_report_scn(global_broadcast_scn_val))) {
        LOG_WARN("fail to update all tablets report_scn", KR(ret),
                  K(global_broadcast_scn_val));
      } else if (OB_FAIL(merge_info_mgr_->get_zone_merge_mgr().try_update_global_last_merged_scn())) {
        LOG_WARN("try update global last_merged_scn failed", KR(ret));
      } else if (OB_FAIL(merge_info_mgr_->get_zone_merge_mgr().finish_all_zone_merge(global_broadcast_scn_val))) {
        LOG_WARN("failed to finish all zone merge", KR(ret));
      } else if (OB_FAIL(ObGlobalMergeTableOperator::load_global_merge_info(
            *GCTX.sql_proxy_, tenant_id_, global_info, true/*print_sql*/))) {
        LOG_WARN("failed to load global merge info", KR(ret), K(global_info));
      } else if (global_info.is_last_merge_complete() && OB_FAIL(progress_checker_->clear_cached_info())) { // clear only when merge finished
        LOG_WARN("fail to do prepare handle of progress checker", KR(ret));
      } else {
        ROOTSERVICE_EVENT_ADD("daily_merge", "global_merged", K_(tenant_id),
                              "global_broadcast_scn", global_broadcast_scn_val);
      }
    }
  }
  return ret;
}

int ObMajorMergeScheduler::set_zone_merging(const ObZone &zone)
{
  int ret = OB_SUCCESS;
  if (OB_FAIL(merge_info_mgr_->get_zone_merge_mgr().set_zone_merging(zone))) {
    LOG_WARN("fail to set zone merging flag", KR(ret), K_(tenant_id), K(zone));
  } else {
    ROOTSERVICE_EVENT_ADD("daily_merge", "set_zone_merging", K_(tenant_id), K(zone));
    LOG_INFO("set zone merging success", K_(tenant_id), K(zone));
  }

  return ret;
}

int ObMajorMergeScheduler::update_all_tablets_report_scn(
    const uint64_t global_braodcast_scn_val)
{
  int ret = OB_SUCCESS;
  FREEZE_TIME_GUARD;
  if (OB_FAIL(ObTabletMetaTableCompactionOperator::batch_update_report_scn(
          tenant_id_,
          global_braodcast_scn_val,
          ObTabletReplica::ScnStatus::SCN_STATUS_ERROR,
          stop_))) {
    LOG_WARN("fail to batch update report_scn", KR(ret), K_(tenant_id), K(global_braodcast_scn_val));
  }
  return ret;
}

void ObMajorMergeScheduler::check_merge_interval_time(const bool is_merging)
{
  int ret = OB_SUCCESS;
  int64_t now = ObTimeUtility::current_time();
  int64_t global_last_merged_time = -1;
  int64_t global_merge_start_time = -1;
  if (OB_ISNULL(merge_info_mgr_)) {
    ret = OB_ERR_UNEXPECTED;
    LOG_WARN("merge info mgr is unexpected nullptr", KR(ret), K_(tenant_id));
  } else if (OB_FAIL(merge_info_mgr_->get_zone_merge_mgr().get_global_last_merged_time(global_last_merged_time))) {
    LOG_WARN("fail to get global last merged time", KR(ret), K_(tenant_id));
  } else if (OB_FAIL(merge_info_mgr_->get_zone_merge_mgr().get_global_merge_start_time(global_merge_start_time))) {
    LOG_WARN("fail to get global merge start time", KR(ret), K_(tenant_id));
  } else {
    const int64_t MAX_NO_MERGE_INTERVAL = 36 * 3600 * 1000 * 1000L;  // 36 hours
    const int64_t MAX_REFRESH_EPOCH_IN_MERGE_INTERVAL = 6 * 3600 * 1000 * 1000L; // 6 hours
    if ((global_last_merged_time < 0) || (global_merge_start_time < 0)) {
      ret = OB_ERR_UNEXPECTED;
      LOG_WARN("unexpected global_last_merged_time and global_merge_start_time", KR(ret),
               K(global_last_merged_time), K(global_merge_start_time), K_(tenant_id));
    } else if ((0 == global_last_merged_time) && (0 == global_merge_start_time)) {
      if (0 == first_check_merge_us_) {
        first_check_merge_us_ = now;
      }
    }
  }
}

} // end namespace rootserver
} // end namespace oceanbase
