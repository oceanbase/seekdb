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
#include "ob_medium_loop.h"
#include "share/rc/ob_module_provider.h"
#include "storage/compaction/ob_tablet_scheduler.h"
#include "storage/compaction/ob_schedule_tablet_func.h"
#include "storage/compaction/ob_server_compaction_event_history.h"
#include "storage/compaction/ob_compaction_progress.h"
#include "share/ob_tablet_meta_table_compaction_operator.h"
#include "storage/tx_storage/ob_ls_service.h"

namespace oceanbase
{
namespace compaction
{
/********************************************ObMediumLoop impl******************************************/
int ObMediumLoop::start_merge(const int64_t merge_version)
{
  int ret = OB_SUCCESS;
  if (OB_UNLIKELY(merge_version <= 0)) {
    ret = OB_INVALID_ARGUMENT;
    LOG_WARN("invalid argument", KR(ret), K(merge_version));
  } else {
    merge_version_ = merge_version;
    schedule_stats_.start_merge();

    const int64_t last_merged_version = ObBasicMergeScheduler::get_merge_scheduler()->get_merged_version();
    ADD_COMPACTION_EVENT(
        merge_version,
        ObServerCompactionEvent::RECEIVE_BROADCAST_SCN,
        schedule_stats_.start_timestamp_,
        K(last_merged_version));
  }
  return ret;
}

int ObMediumLoop::init(const int64_t batch_size)
{
  int ret = OB_SUCCESS;
  if (OB_UNLIKELY(merge_version_ <= 0)) {
    ret = OB_ERR_UNEXPECTED;
    LOG_WARN("invalid merge_version", KR(ret), K_(merge_version));
  } else if (OB_FAIL(tablet_iter_.build_iter(batch_size))) {
    LOG_WARN("failed to init tablet iterator", K(ret));
  }
  return ret;
}

int ObMediumLoop::loop()
{
  int ret = OB_SUCCESS;
  int tmp_ret = OB_SUCCESS;

  ObScheduleTabletFunc func(merge_version_, loop_cnt_);
  schedule_stats_.weak_read_ts_ready_ = true;
  if (!tablet_iter_.is_scan_finish()) {
    ObLS *ls = tablet_iter_.get_ls();
    if (OB_TMP_FAIL(loop_tablets(ls, func))) {
      LOG_TRACE("failed to scan tablets", KR(ret), K(func));
      tablet_iter_.finish_scan();
      tablet_iter_.update_merge_finish(false);
      if (OB_SIZE_OVERFLOW != tmp_ret && !schedule_ignore_error(tmp_ret)) {
        LOG_ERROR("failed to schedule merge", K(tmp_ret));
      }
    }
    if (OB_SUCC(ret) && tablet_iter_.need_report_scn()) {
      // Scan tablet metadata to publish a conservative report_scn when required.
      tmp_ret = update_report_scn_as_ls_leader(*ls, func);
#ifndef ERRSIM
      LOG_INFO("try to update report scn", K(tmp_ret)); // low printing frequency
#endif
    }
  }
  add_event_and_diagnose(func);
  LOG_TRACE("finish scheduling medium merge", K(tmp_ret), K(ret), K_(tablet_iter));
  return ret;
}

int ObMediumLoop::loop_tablets(
  ObLS *ls,
  ObScheduleTabletFunc &func)
{
  int ret = OB_SUCCESS;
  int tmp_ret = OB_SUCCESS;
  if (OB_FAIL(func.init(ls))) {
    if (OB_STATE_NOT_MATCH != ret) {
      LOG_ERROR("failed to initialize compaction status", KR(ret), K(func));
    } else {
      tablet_iter_.update_merge_finish(false);
      schedule_stats_.weak_read_ts_ready_ = false;
    }
  } else {
    ObTabletHandle tablet_handle;
    ObTablet *tablet = nullptr;
    ObTabletID tablet_id;
    bool tablet_merge_finish = false;
    while (OB_SUCC(ret)) { // process the remaining tablets
      if (OB_FAIL(tablet_iter_.get_next_tablet(tablet_handle))) {
        if (OB_ITER_END == ret) {
          ret = OB_SUCCESS;
          break;
        } else {
          LOG_WARN("failed to get tablet", K(ret), K(tablet_handle));
        }
      } else if (OB_UNLIKELY(!tablet_handle.is_valid()
        || nullptr == (tablet = tablet_handle.get_obj()))) {
        ret = OB_ERR_UNEXPECTED;
        LOG_WARN("tablet handle is invalid", KR(ret), K(tablet_handle));
      } else if (FALSE_IT(tablet_id = tablet->get_tablet_id())) {
      } else if (tablet_id.is_ls_inner_tablet()) {
        // do nothing
      } else if (OB_TMP_FAIL(func.schedule_tablet(tablet_handle, tablet_merge_finish))) {
        if (OB_STATE_NOT_MATCH != tmp_ret) {
          LOG_ERROR("failed to schedule tablet", KR(tmp_ret), K(tablet_id));
        }
        tablet_iter_.update_merge_finish(false);
      } else {
        tablet_iter_.update_merge_finish(tablet_merge_finish);
      }
    } // while
  }
  return ret;
}

void ObMediumLoop::add_event_and_diagnose(const ObScheduleTabletFunc &func)
{
  int ret = OB_SUCCESS;
  int tmp_ret = OB_SUCCESS;
  if (!tablet_iter_.database_merge_finish() && merge_version_ > ObBasicMergeScheduler::INIT_COMPACTION_SCN) {
    // not finish cur merge_version
    if (schedule_stats_.weak_read_ts_ready_) { // check schedule Timer Task
      if (schedule_stats_.add_weak_read_ts_event_flag_ && tablet_iter_.is_scan_finish()) {
        schedule_stats_.add_weak_read_ts_event_flag_ = false;
        ADD_COMPACTION_EVENT(
            merge_version_,
            ObServerCompactionEvent::WEAK_READ_TS_READY,
            ObTimeUtility::fast_current_time(),
            "check_weak_read_ts_cnt", schedule_stats_.check_weak_read_ts_cnt_ + 1);
      }
    } else {
      schedule_stats_.check_weak_read_ts_cnt_++;
    }

    if (tablet_iter_.is_scan_finish()) {
      loop_cnt_++;
      if (REACH_THREAD_TIME_INTERVAL(ADD_LOOP_EVENT_INTERVAL)) {
        ADD_COMPACTION_EVENT(
          merge_version_,
          ObServerCompactionEvent::SCHEDULER_LOOP,
          ObTimeUtility::fast_current_time(),
          "schedule_stats", schedule_stats_,
          "schedule_tablet_cnt", func.get_schedule_tablet_cnt());
      }
    }
  }

  const int64_t merged_version = ObBasicMergeScheduler::get_merge_scheduler()->get_merged_version();
  if (tablet_iter_.database_merge_finish() && merge_version_ > merged_version) {
    ObBasicMergeScheduler::get_merge_scheduler()->update_merged_version(merge_version_);
    LOG_INFO("all tablet major merge finish", K(merged_version), K_(loop_cnt));

    DEL_SUSPECT_INFO(MEDIUM_MERGE, UNKNOW_TABLET_ID, share::ObDiagnoseTabletType::TYPE_MEDIUM_MERGE);
    if (OB_TMP_FAIL(share::g_mp->compaction_progress_mgr()->finish_progress(merge_version_))) {
      LOG_WARN("failed to finish progress", K(tmp_ret), K_(merge_version));
    }

    const int64_t current_time = ObTimeUtility::fast_current_time();
    ADD_COMPACTION_EVENT(
          merge_version_,
          ObServerCompactionEvent::TABLET_COMPACTION_FINISHED,
          current_time,
          "cost_time",
          current_time - schedule_stats_.start_timestamp_);
  }

  LOG_INFO("finish schedule all tablet merge", K(merge_version_), K(schedule_stats_), K_(loop_cnt),
      "database_merge_finish", tablet_iter_.database_merge_finish(),
      "is_scan_all_tablet_finish", tablet_iter_.is_scan_finish(),
      "schedule_tablet_cnt", func.get_schedule_tablet_cnt(),
      "time_guard", func.get_time_guard());
}


int ObMediumLoop::update_report_scn_as_ls_leader(ObLS &ls, const ObScheduleTabletFunc &func)
{
  int ret = OB_SUCCESS;
  const int64_t inner_table_merged_scn = ObBasicMergeScheduler::get_merge_scheduler()->get_inner_table_merged_scn();
  const ObLSStatusCache &ls_status = func.get_ls_status();
  if (ls_status.can_merge()) {
    ObSEArray<ObTabletID, 200> tablet_id_array;
    if (OB_FAIL(ls.get_tablet_svr()->get_all_tablet_ids(true/*except_ls_inner_tablet*/, tablet_id_array))) {
      LOG_WARN("failed to get tablet id", K(ret));
    } else if (inner_table_merged_scn > ObBasicMergeScheduler::INIT_COMPACTION_SCN
        && OB_FAIL(ObTabletMetaTableCompactionOperator::batch_update_unequal_report_scn_tablet(inner_table_merged_scn, tablet_id_array))) {
      LOG_WARN("failed to get unequal report scn", K(ret), K(inner_table_merged_scn));
    }
  } else {
    ret = OB_LS_LOCATION_LEADER_NOT_EXIST;
  }
  return ret;
}

/********************************************ObScheduleNewMediumLoop impl******************************************/
int ObScheduleNewMediumLoop::loop()
{
  int ret = OB_SUCCESS;
  ObLS *ls = nullptr;
  const int64_t frozen_version = ObBasicMergeScheduler::get_merge_scheduler()->get_frozen_version();
  ObScheduleTabletFunc func(frozen_version);
  // sort tablet check info
  if (OB_FAIL(sort_tablet_check_info())) {
    LOG_WARN("failed to sort", KR(ret));
  } else if (OB_FAIL(share::g_mp->ls_service()->get_ls(ls))) {
    LOG_WARN("failed to get ls", K(ret));
  } else if (OB_FAIL(func.init(ls))) {
    if (OB_STATE_NOT_MATCH != ret) {
      LOG_ERROR("failed to initialize compaction status", KR(ret));
    } else {
      LOG_WARN("not support schedule medium", K(ret), K(func));
    }
  }
  for (int64_t i = 0; OB_SUCC(ret) && i < tablet_check_infos_.count(); ++i) { // ignore OB_FAIL
    const ObTabletID &tablet_id = tablet_check_infos_.at(i).get_tablet_id();
    ObTabletHandle tablet_handle;
    if (OB_FAIL(ls->get_tablet_svr()->get_tablet(
                 tablet_id, tablet_handle, 0 /*timeout_us*/))) {
      LOG_WARN("get tablet failed", K(ret), K(tablet_id));
    } else if (OB_FAIL(func.request_schedule_new_round(tablet_handle, false/*user_request*/))) {
      LOG_WARN("get tablet failed", K(ret), K(tablet_id));
    }
  } // end of for
  ret = OB_SUCCESS;
  LOG_INFO("end of ObScheduleNewMediumLoop", KR(ret), K(func));
  return ret;
}

struct ObTabletCheckInfoComparator final {
public:
  ObTabletCheckInfoComparator(int &sort_ret)
    : result_code_(sort_ret)
  {}
  bool operator()(const ObTabletCheckInfo &lhs, const ObTabletCheckInfo &rhs)
  {
    return lhs.get_tablet_id().id() < rhs.get_tablet_id().id();
  }
  int &result_code_;
};

int ObScheduleNewMediumLoop::sort_tablet_check_info()
{
  int ret = OB_SUCCESS;
  if (tablet_check_infos_.count() > 2) {
    ObTabletCheckInfoComparator cmp(ret);
    ob_sort(tablet_check_infos_.begin(), tablet_check_infos_.end(), cmp);
  }
  return ret;
}

} // namespace compaction
} // namespace oceanbase
