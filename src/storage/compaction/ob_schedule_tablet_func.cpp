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
#include "storage/compaction/ob_schedule_tablet_func.h"
#include "share/rc/ob_module_provider.h"
#include "storage/compaction/ob_medium_compaction_func.h"
namespace oceanbase
{
using namespace storage;
namespace compaction
{
/********************************************ObScheduleTabletFunc impl******************************************/

ObScheduleTabletFunc::ObScheduleTabletFunc(
  const int64_t merge_version,
  const int64_t loop_cnt,
  const ObAdaptiveMergePolicy::AdaptiveMergeReason merge_reason)
  : ObBasicScheduleTabletFunc(merge_version, loop_cnt),
    tablet_status_(),
    time_guard_(),
    clear_stat_tablets_(),
    merge_reason_(merge_reason)
{
  clear_stat_tablets_.set_attr(ObMemAttr("BatchClearTblts"));
}

// when call schedule_tablet, ls status have been checked
int ObScheduleTabletFunc::schedule_tablet(
  ObTabletHandle &tablet_handle,
  bool &tablet_merge_finish)
{
  tablet_merge_finish = false;
  int ret = OB_SUCCESS;
  int tmp_ret = OB_SUCCESS;
  ObTablet *tablet = nullptr;
  ObTabletID tablet_id;
  bool need_diagnose = false;
  time_guard_.click(ObCompactionScheduleTimeGuard::GET_TABLET);
  tablet_cnt_.loop_tablet_cnt_++;
  if (OB_UNLIKELY(!ls_status_.is_valid())) {
    ret = OB_ERR_UNEXPECTED;
    LOG_WARN("invalid ls status", KR(ret), K_(ls_status));
  } else if (OB_UNLIKELY(!tablet_handle.is_valid())) {
    ret = OB_INVALID_ARGUMENT;
    LOG_WARN("invalid tablet handle", KR(ret), K(tablet_handle));
  } else if (FALSE_IT(tablet = tablet_handle.get_obj())) {
  } else if (FALSE_IT(tablet_id = tablet->get_tablet_id())) {
  } else if (OB_FAIL(tablet_status_.init_for_major(
                 ls_status_.get_ls(), merge_version_, *tablet,
                 should_skip_merge_, ls_could_schedule_new_round_))) {
    need_diagnose = true;
    LOG_WARN("failed to init tablet status", KR(ret), K_(ls_status), K(tablet_id));
  } else {
    time_guard_.click(ObCompactionScheduleTimeGuard::INIT_TABLET_STATUS);
    if (tablet_status_.tablet_merge_finish()) {
      tablet_merge_finish = true;
      tablet_cnt_.finish_cnt_++;
    }
    if (tablet_status_.could_schedule_new_round() && OB_TMP_FAIL(schedule_tablet_new_round(tablet_handle, false/*user_request*/))) {
      need_diagnose = true;
      LOG_ERROR("failed to schedule tablet new round", KR(tmp_ret), K_(ls_status), K(tablet_id));
    }
    if (!tablet_status_.can_merge()) {
      need_diagnose = tablet_status_.need_diagnose();
    } else if (ls_could_schedule_merge_
        && OB_TMP_FAIL(schedule_tablet_execute(*tablet))) {
      need_diagnose = true;
      if (OB_SIZE_OVERFLOW != tmp_ret && OB_EAGAIN != tmp_ret) {
        LOG_ERROR("failed to schedule tablet execute", KR(tmp_ret), K_(ls_status), K(tablet_id));
      }
    } else {
      LOG_DEBUG("success to schedule tablet execute", KR(tmp_ret), K_(ls_status), K(tablet_status_), K_(ls_could_schedule_merge));
    }
  }
  if (need_diagnose
      && OB_TMP_FAIL(share::g_mp->diagnose_tablet_mgr()->add_diagnose_tablet(tablet_id,
                          share::ObDiagnoseTabletType::TYPE_MEDIUM_MERGE))) {
    LOG_WARN("failed to add diagnose tablet", K(tmp_ret), K_(ls_status), K(tablet_id));
  }
  schedule_freeze_dag(false/*force*/);
  tablet_status_.destroy();
  return ret;
}

int ObScheduleTabletFunc::schedule_tablet_new_round(
  ObTabletHandle &tablet_handle,
  const bool user_request)
{
  int ret = OB_SUCCESS;
  int tmp_ret = OB_SUCCESS;
  const ObTabletID &tablet_id = tablet_handle.get_obj()->get_tablet_id();
  bool medium_clog_submitted = false;

  if (OB_ISNULL(tablet_status_.medium_list())) {
    ret = OB_ERR_UNEXPECTED;
    LOG_WARN("medium list in tablet status is null", KR(ret), K_(tablet_status));
  } else if (!tablet_status_.tablet_merge_finish()
      || user_request
      || ObBasicMergeScheduler::get_merge_scheduler()->enable_adaptive_compaction()) {
    ObMediumCompactionScheduleFunc func(
        ls_status_.get_ls(), tablet_handle, ls_status_.weak_read_ts_,
        *tablet_status_.medium_list(), &tablet_cnt_,
        merge_reason_);
    if (OB_FAIL(func.schedule_next_medium_for_leader(
        tablet_status_.tablet_merge_finish() ? 0 : merge_version_,
        medium_clog_submitted))) {
      LOG_WARN("failed to schedule next medium", K(ret), K_(ls_status), K(tablet_id));
    } else if (medium_clog_submitted) {
      if (OB_TMP_FAIL(clear_stat_tablets_.push_back(tablet_id))) {
        LOG_WARN("failed to push back tablet_id for batch_freeze", KR(tmp_ret), K_(ls_status), K(tablet_id));
      }
    }
    time_guard_.click(ObCompactionScheduleTimeGuard::SCHEDULE_NEXT_MEDIUM);
  }
  return ret;
}

int ObScheduleTabletFunc::request_schedule_new_round(
  ObTabletHandle &tablet_handle,
  const bool user_request)
{
  int ret = OB_SUCCESS;
  int tmp_ret = OB_SUCCESS;
  ObTablet *tablet = nullptr;
  ObTabletID tablet_id;
  bool schedule_flag = false;
  if (OB_UNLIKELY(!ls_status_.is_valid())) {
    ret = OB_ERR_UNEXPECTED;
    LOG_WARN("invalid ls status", KR(ret), K_(ls_status));
  } else if (OB_UNLIKELY(!tablet_handle.is_valid())) {
    ret = OB_INVALID_ARGUMENT;
    LOG_WARN("invalid tablet handle", KR(ret), K(tablet_handle));
  } else if (FALSE_IT(tablet = tablet_handle.get_obj())) {
  } else if (FALSE_IT(tablet_id = tablet->get_tablet_id())) {
  } else if (OB_FAIL(tablet_status_.init_for_major(
                 ls_status_.get_ls(), merge_version_, *tablet,
                 should_skip_merge_, ls_could_schedule_new_round_))) {
    LOG_WARN("failed to init tablet status", KR(ret), K_(ls_status), K(tablet_id));
  } else if (user_request) { // should print error log for user request
    if (!tablet_status_.tablet_merge_finish()) {
      ret = OB_MAJOR_FREEZE_NOT_FINISHED;
      LOG_WARN("no major sstable or database major compaction is unfinished; cannot schedule another medium compaction",
        K(ret), K_(ls_status), K(tablet_id), K_(merge_version), K_(tablet_status));
    } else if (!tablet_status_.can_merge()) {
      ret = OB_STATE_NOT_MATCH;
      LOG_WARN("tablet status can't merge now",
        K(ret), K_(ls_status), K(tablet_id), K_(merge_version), K_(tablet_status));
    } else if (!tablet_status_.could_schedule_new_round()) {
      ret = OB_MAJOR_FREEZE_NOT_FINISHED;
      LOG_WARN("tablet need check finish, can't schedule another medium", K(ret), K_(ls_status), K(tablet_id),
        K_(tablet_status));
    } else {
      schedule_flag = true;
    }
  } else {
    schedule_flag = tablet_status_.tablet_merge_finish() && tablet_status_.could_schedule_new_round();
  }
  if (OB_SUCC(ret) && schedule_flag && OB_TMP_FAIL(schedule_tablet_new_round(tablet_handle, user_request))) {
    LOG_ERROR("failed to schedule tablet new round", KR(tmp_ret), K_(ls_status), K(tablet_id));
  }
  tablet_status_.destroy();
  return ret;
}

int ObScheduleTabletFunc::schedule_tablet_execute(
  ObTablet &tablet)
{
  int ret = OB_SUCCESS;
#ifdef ERRSIM
  ret = OB_E(EventTable::EN_MEDIUM_CREATE_DAG) ret;
  if (OB_FAIL(ret)) {
    FLOG_INFO("ERRSIM EN_MEDIUM_CREATE_DAG", K(ret));
    return ret;
  }
#endif
  const ObTabletID &tablet_id = tablet.get_tablet_id();
  bool can_merge = false;
  int64_t schedule_scn = 0;
  if (OB_FAIL(ObTabletScheduler::check_ready_for_major_merge(tablet, MEDIUM_MERGE))) {
    LOG_WARN("failed to check ready for major merge", K(ret), K(tablet_id));
  } else if (OB_FAIL(get_schedule_execute_info(tablet, schedule_scn))) {
    if (OB_NO_NEED_MERGE == ret) {
      ret = OB_SUCCESS;
    } else {
      LOG_WARN("failed to get schedule execute info", KR(ret), K_(ls_status), K(tablet_id));
    }
  } else if (OB_FAIL(check_with_schedule_scn(tablet, schedule_scn, tablet_status_, can_merge))) {
    LOG_WARN("failed to check with schedule scn", KR(ret), K(schedule_scn));
  } else if (can_merge) {
    if (OB_FAIL(ObTabletScheduler::schedule_merge_dag(tablet, MEDIUM_MERGE, schedule_scn))) {
      if (OB_SIZE_OVERFLOW != ret && OB_EAGAIN != ret) {
        LOG_ERROR("failed to schedule medium merge dag", K(ret), K_(ls_status), K(tablet_id));
      }
    } else {
      LOG_DEBUG("success to schedule medium merge dag", K(ret), K(schedule_scn), K_(ls_status), K(tablet_id));
      ++tablet_cnt_.schedule_dag_cnt_;
    }
  }
  time_guard_.click(ObCompactionScheduleTimeGuard::SCHEDULE_TABLET_EXECUTE);
  return ret;
}

int ObScheduleTabletFunc::get_schedule_execute_info(
    ObTablet &tablet,
    int64_t &schedule_scn)
{
  int ret = OB_SUCCESS;
  bool schedule_flag = false;
  const int64_t last_major_snapshot = tablet.get_last_major_snapshot_version();
  ObMediumCompactionInfo::ObCompactionType compaction_type = ObMediumCompactionInfo::COMPACTION_TYPE_MAX;
  schedule_scn = 0; // medium_snapshot in medium info
  if (OB_ISNULL(tablet_status_.medium_list())) {
    ret = OB_ERR_UNEXPECTED;
    LOG_WARN("medium list in tablet status is null", KR(ret), K_(tablet_status));
  } else if (OB_FAIL(tablet_status_.medium_list()->get_next_schedule_info(
    last_major_snapshot, merge_version_, compaction_type, schedule_scn))) {
    if (OB_NO_NEED_MERGE != ret) {
      LOG_WARN("failed to get next schedule info", KR(ret), K(last_major_snapshot), K_(merge_version));
    }
  } else {
    schedule_flag = true;
  }
  if (OB_SUCC(ret) && !schedule_flag) {
    LOG_DEBUG("tablet no need to schedule", KR(ret), K_(ls_status), K(tablet_status_), K(schedule_flag));
    ret = OB_NO_NEED_MERGE;
  }
  return ret;
}

void ObScheduleTabletFunc::schedule_freeze_dag(const bool force)
{
  int tmp_ret = OB_SUCCESS;
  IGNORE_RETURN ObBasicScheduleTabletFunc::schedule_freeze_dag(force);
  if (force || clear_stat_tablets_.count() > SCHEDULE_DAG_THREHOLD) {
    if (OB_TMP_FAIL(share::g_mp->tablet_stat_mgr()->batch_clear_tablet_stat(clear_stat_tablets_))) {
      LOG_WARN_RET(tmp_ret, "failed to batch clear tablet stats");
    }
    clear_stat_tablets_.reset();
  }
}

int ObScheduleTabletFunc::diagnose_switch_tablet(
  ObLS &ls,
  const ObTablet &tablet)
{
  int ret = OB_SUCCESS;
  const ObTabletID &tablet_id = tablet.get_tablet_id();
  tablet_status_.destroy();
  if (OB_FAIL(tablet_status_.init_for_diagnose(ls, merge_version_, tablet))) {
    LOG_WARN("failed to init tablet status", KR(ret), K_(merge_version), K(ls_status_), K(tablet_id));
  }
  return ret;
}

} // namespace compaction
} // namespace oceanbase
