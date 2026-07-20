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
#include "ob_schedule_status_cache.h"
#include "share/rc/ob_module_provider.h"
#include "src/storage/compaction/ob_basic_schedule_tablet_func.h"
#include "storage/compaction/ob_medium_compaction_func.h"
#include "observer/report/ob_tablet_table_updater.h" // for ObTabletTableUpdater
namespace oceanbase
{
using namespace storage;
namespace compaction
{
/********************************************ObLSStatusCache impl******************************************/
const static char * ObLSStateStr[] = {
    "CAN_MERGE",
    "WEAK_READ_TS_NOT_READY",
    "OFFLINE",
    "STATE_MAX"
};

const char * ObLSStatusCache::ls_state_to_str(const ObLSStatusCache::LSState &state)
{
  STATIC_ASSERT(static_cast<int64_t>(STATE_MAX + 1) == ARRAYSIZEOF(ObLSStateStr), "ls state str len is mismatch");
  const char *str = "";
  if (is_valid_ls_state(state)) {
    str = ObLSStateStr[state];
  } else {
    str = "invalid_state";
  }
  return str;
}
/*
* check_list
* ls is offline
* weak_read_ts
*/
int ObLSStatusCache::init_for_major(
  const int64_t merge_version,
  ObLS *ls)
{
  int ret = OB_SUCCESS;
  reset(); // reset before init
  if (OB_UNLIKELY(merge_version < 0 || OB_ISNULL(ls))) {
    ret = OB_INVALID_ARGUMENT;
    LOG_WARN("invalid argument", KR(ret), K(merge_version), KP(ls));
  } else {
    check_ls_state(*ls, state_);
    if (can_merge()) {
      weak_read_ts_ = ls->get_ls_wrs_handler()->get_ls_weak_read_ts();
      if (OB_UNLIKELY(!weak_read_ts_.is_valid())) {
        ret = OB_ERR_UNEXPECTED;
        LOG_WARN("invalid weak read ts", KR(ret), K_(weak_read_ts));
      } else if (merge_version > 0 && weak_read_ts_.get_val_for_tx() < merge_version) {
        state_ = WEAK_READ_TS_NOT_READY;
      }
    }
  }
  if (OB_SUCC(ret)) {
    ls_ = ls;
    if (!can_merge()) {
      ADD_COMMON_SUSPECT_INFO(MAJOR_MERGE,
                              ObDiagnoseTabletType::TYPE_MEDIUM_MERGE,
                              ObSuspectInfoType::SUSPECT_LS_CANT_MERGE,
                              weak_read_ts_.is_valid() ? weak_read_ts_.get_val_for_tx() : -1,
                              "ls_status", *this);
      if (REACH_THREAD_TIME_INTERVAL(PRINT_LOG_INVERVAL)) {
        LOG_INFO("ls is not ready for compaction", KPC(this));
      }
    }
  }
  return ret;
}

void ObLSStatusCache::reset()
{
  weak_read_ts_.reset();
  state_ = STATE_MAX;
  ls_ = nullptr;
}

void ObLSStatusCache::check_ls_state(ObLS &ls, LSState &state)
{
  if (ls.is_offline()) {
    state = OFFLINE;
    if (REACH_THREAD_TIME_INTERVAL(PRINT_LOG_INVERVAL)) {
      LOG_INFO("ls is offline", K(ls), K(ls.is_offline()));
    }
  } else {
    state = CAN_MERGE;
  }
}

bool ObLSStatusCache::check_weak_read_ts_ready(
    const int64_t &merge_version,
    ObLS &ls)
{
  bool is_ready_for_compaction = false;
  const SCN &weak_read_scn = ls.get_ls_wrs_handler()->get_ls_weak_read_ts();

  if (weak_read_scn.get_val_for_tx() < merge_version) {
    FLOG_INFO("current slave_read_ts is smaller than freeze_ts, try later",
              K(merge_version), K(weak_read_scn));
  } else {
    is_ready_for_compaction = true;
  }
  return is_ready_for_compaction;
}

/********************************************ObTabletStatusCache impl******************************************/
const static char * ObTabletExecuteStateStr[] = {
    "CAN_MERGE",
    "DATA_NOT_COMPLETE",
    "NO_MAJOR_SSTABLE",
    "INVALID_LS_STATE",
    "TENANT_SKIP_MERGE",
    "STATE_MAX"
};

const char * ObTabletStatusCache::tablet_execute_state_to_str(const ObTabletStatusCache::TabletExecuteState &state)
{
  STATIC_ASSERT(static_cast<int64_t>(EXECUTE_STATE_MAX + 1) == ARRAYSIZEOF(ObTabletExecuteStateStr), "tablet execute state str len is mismatch");
  const char *str = "";
  if (is_valid_tablet_execute_state(state)) {
    str = ObTabletExecuteStateStr[state];
  } else {
    str = "invalid_state";
  }
  return str;
}

const static char * ObTabletScheduleNewRoundStateStr[] = {
    "CAN_SCHEDULE_NEW_ROUND",
    "NEED_CHECK_LAST_MEDIUM_CKM",
    "EXIST_UNFINISH_MEDIUM",
    "NONE",
    "STATE_MAX"
};

const char * ObTabletStatusCache::new_round_state_to_str(const ObTabletStatusCache::TabletScheduleNewRoundState &state)
{
  STATIC_ASSERT(static_cast<int64_t>(NEW_ROUND_STATE_MAX + 1) == ARRAYSIZEOF(ObTabletScheduleNewRoundStateStr), "tablet schedule new round state str len is mismatch");
  const char *str = "";
  if (is_valid_schedule_new_round_state(state)) {
    str = ObTabletScheduleNewRoundStateStr[state];
  } else {
    str = "invalid_state";
  }
  return str;
}

int ObTabletStatusCache::init_for_major(
  ObLS &ls,
  const int64_t merge_version,
  const ObTablet &tablet,
  const bool is_skip_merge_tenant,
  const bool ls_could_schedule_new_round)
{
  int ret = OB_SUCCESS;
  if (IS_INIT) {
    ret = OB_INIT_TWICE;
    LOG_WARN("init twice", KR(ret), KPC(this));
  } else {
    const ObTabletID &tablet_id = tablet.get_tablet_id();
    if (OB_FAIL(inner_init_state(merge_version, tablet, is_skip_merge_tenant))) {
      LOG_WARN("failed to init state", KR(ret), K(merge_version), K(tablet_id));
    } else if (OB_FAIL(update_tablet_report_status(ls, tablet))) {
      LOG_WARN("failed to update tablet report status", KR(ret), K(tablet_id));
    } else {
      inner_init_could_schedule_new_round(tablet,
                                          ls_could_schedule_new_round,
                                          true /*normal_schedule*/);
    }
    if (OB_SUCC(ret)) {
      tablet_id_ = tablet_id;
      is_inited_ = true;
      if (!can_merge() || !could_schedule_new_round()) {
        LOG_DEBUG("success to init tablet status", KR(ret), KPC(this), K(ls_could_schedule_new_round));
      }
    } else {
      inner_destroy();
    }
  }
  return ret;
}

int ObTabletStatusCache::init_for_diagnose(
  ObLS &ls,
  const int64_t merge_version,
  const ObTablet &tablet)
{
  int ret = OB_SUCCESS;
  if (IS_INIT) {
    ret = OB_INIT_TWICE;
    LOG_WARN("init twice", KR(ret), KPC(this));
  } else {
    const ObTabletID &tablet_id = tablet.get_tablet_id();
    if (OB_FAIL(inner_init_state(merge_version, tablet, false/*is_skip_merge_tenant*/))) {
      LOG_WARN("failed to init state", KR(ret), K(merge_version), K(tablet_id));
    } else {
      inner_init_could_schedule_new_round(tablet,
                                          true /*ls_could_schedule_new_round*/,
                                          false /*normal_schedule*/);
    }
    if (OB_SUCC(ret)) {
      tablet_id_ = tablet_id;
      is_inited_ = true;
    } else {
      inner_destroy();
    }
  }
  return ret;
}

/*
* check_list
* tablet is_data_complete
* exist major sstable
*/
int ObTabletStatusCache::inner_init_state(
    const int64_t merge_version,
    const ObTablet &tablet,
    const bool is_skip_merge_tenant)
{
  int ret = OB_SUCCESS;
  const ObTabletID &tablet_id = tablet.get_tablet_id();
  const int64_t last_major_snapshot = tablet.get_last_major_snapshot_version();

  if (OB_UNLIKELY(!tablet.is_data_complete())) {
    execute_state_ = DATA_NOT_COMPLETE;
    if (REACH_THREAD_TIME_INTERVAL(PRINT_LOG_INVERVAL)) {
      LOG_INFO("tablet is not data complete, could not to merge now", K(ret), K(tablet_id));
    }
  } else if (last_major_snapshot <= 0) {
    execute_state_ = NO_MAJOR_SSTABLE;
    LOG_TRACE("no major", KR(ret), K(tablet_id), K(last_major_snapshot));
  } else if (FALSE_IT(tablet_merge_finish_ = (last_major_snapshot >= merge_version))){
  } else if (is_skip_merge_tenant) {
    execute_state_ = TENANT_SKIP_MERGE;
  } else {
    execute_state_ = CAN_MERGE;
  }
  if (FAILEDx(tablet.read_medium_info_list(allocator_, medium_list_))) {
    LOG_WARN("failed to load medium info list", K(ret), K(tablet_id));
  }
  return ret;
}

void ObTabletStatusCache::inner_init_could_schedule_new_round(
  const ObTablet &tablet,
  const bool ls_could_schedule_new_round,
  const bool normal_schedule)
{
  int ret = OB_SUCCESS;
  const ObTabletID &tablet_id = tablet.get_tablet_id();
  ObTabletCreateDeleteMdsUserData user_data;
  mds::MdsWriter writer;// will be removed later
  mds::TwoPhaseCommitState trans_stat;// will be removed later
  share::SCN trans_version;// will be removed later
  new_round_state_ = NEW_ROUND_STATE_MAX;
  if (OB_FAIL(tablet.ObITabletMdsInterface::get_latest_tablet_status(user_data, writer, trans_stat, trans_version))) {
    LOG_WARN("failed to get tablet status", K(ret), K(tablet), K(user_data));
  } else if (OB_FAIL(check_medium_list(tablet, normal_schedule))) {
    // call medium_list_->need_check_finish even if ls_could_schedule_new_round=false
    LOG_WARN("failed to check medium list", K(ret), K(tablet_id));
  } else if (!ls_could_schedule_new_round || NEW_ROUND_STATE_MAX != new_round_state_) {
    // do nothing
  } else if (normal_schedule) {
    if (OB_FAIL(register_map(tablet))) {
      // register_map must be the last step
      LOG_WARN("failed to add tablet", K(ret), K(tablet_id));
    }
  } else { // for diagnose
    new_round_state_ = DIAGNOSE_NORMAL;
  }
}

void ObTabletStatusCache::inner_destroy()
{
  is_inited_ = false;
  if (OB_NOT_NULL(medium_list_)) {
    medium_list_->~ObMediumCompactionInfoList();
    allocator_.free((void *)medium_list_);
    medium_list_ = NULL;
  }
  allocator_.reset(); // use this allocator to read mds, medium list may be null after read mds
  tablet_id_.reset();
  tablet_merge_finish_ = false;
  execute_state_ = EXECUTE_STATE_MAX;
  new_round_state_ = NEW_ROUND_STATE_MAX;
}

int ObTabletStatusCache::check_medium_list(
  const ObTablet &tablet,
  const bool normal_schedule)
{
  int ret = OB_SUCCESS;

  const ObTabletID &tablet_id = tablet.get_tablet_id();
  if (OB_UNLIKELY(nullptr == medium_list_)) {
    ret = OB_ERR_UNEXPECTED;
    LOG_WARN("medium info list is unexpected null", K(ret), K(tablet_id));
  } else if (medium_list_->need_check_finish()) { // need check finished
    new_round_state_ = NEED_CHECK_LAST_MEDIUM_CKM;
    if (normal_schedule) {
      int tmp_ret = OB_SUCCESS;
      if (OB_TMP_FAIL(share::g_mp->tenant_medium_checker()->add_tablet(
          tablet_id, medium_list_->get_wait_check_medium_scn()))) {
        LOG_WARN("failed to add tablet", K(tmp_ret), K(tablet_id));
      } else {
        LOG_TRACE("success to add tablet into checker", KR(ret), K(tablet_id));
      }
    }
  } else if (!medium_list_->could_schedule_next_round(tablet.get_last_major_snapshot_version())) {
    new_round_state_ = EXIST_UNFINISH_MEDIUM;
  }
  return ret;
}

int ObTabletStatusCache::register_map(
  const ObTablet &tablet)
{
  int ret = OB_SUCCESS;
  ObTabletCreateDeleteMdsUserData user_data;
  mds::MdsWriter writer;
  mds::TwoPhaseCommitState trans_stat;
  share::SCN trans_version;
  if (OB_FAIL(tablet.ObITabletMdsInterface::get_latest_tablet_status(user_data, writer, trans_stat, trans_version))) {
    LOG_WARN("failed to get tablet status", K(ret), K(tablet), K(user_data));
  } else {
    new_round_state_ = CAN_SCHEDULE_NEW_ROUND;
  }
  return ret;
}

int ObTabletStatusCache::update_tablet_report_status(
    ObLS &ls,
    const ObTablet &tablet)
{
  int ret = OB_SUCCESS;
  if (tablet_merge_finish_) {
    int tmp_ret = OB_SUCCESS;
    const ObTabletID &tablet_id = tablet.get_tablet_id();
    if (tablet.get_tablet_meta().report_status_.need_report()) {
      if (OB_TMP_FAIL(share::g_mp->tablet_table_updater()->submit_tablet_update_task(tablet_id, true/*need_diagnose*/))) {
        LOG_WARN("failed to submit tablet update task to report", K(tmp_ret), K(tablet_id));
      } else if (OB_TMP_FAIL(ls.get_tablet_svr()->update_tablet_report_status(tablet_id))) {
        LOG_WARN("failed to update tablet report status", K(tmp_ret), K(tablet_id));
      }
    }
  }
  return ret;
}

int ObTabletStatusCache::check_could_execute(const ObMergeType merge_type, const ObTablet &tablet)
{
  int ret = OB_SUCCESS;
  bool need_merge = true;

  if (OB_UNLIKELY(merge_type <= ObMergeType::INVALID_MERGE_TYPE
      || merge_type >= ObMergeType::MERGE_TYPE_MAX)) {
    ret = OB_ERR_UNEXPECTED;
    LOG_WARN("merge type is invalid", K(ret), "merge_type", merge_type_to_str(merge_type));
  } else if (!is_minor_merge(merge_type)
      && !is_mini_merge(merge_type)
      && !is_major_or_meta_merge_type(merge_type)
      && !is_medium_merge(merge_type)) {
    need_merge = true;
  } else {
    const common::ObTabletID &tablet_id = tablet.get_tablet_meta().tablet_id_;
    bool is_empty_shell = tablet.is_empty_shell();
    if (is_minor_merge(merge_type) || is_mini_merge(merge_type)) {
      need_merge = !is_empty_shell;
    } else if (is_major_or_meta_merge_type(merge_type)) {
      need_merge = tablet.is_data_complete();
    }

    if (OB_FAIL(ret)) {
    } else if (!need_merge) {
      ret = OB_NO_NEED_MERGE;
      LOG_INFO("tablet has no need to merge", K(ret), K(tablet_id),
          "merge_type", merge_type_to_str(merge_type), K(is_empty_shell));
    }
  }

  return ret;
}

bool ObTabletStatusCache::need_diagnose() const
{
  bool bret = false;
  if (!can_merge()) {
    bret = (INVALID_LS_STATE != execute_state_ && TENANT_SKIP_MERGE != execute_state_);
  }
  return bret;
}

} // namespace compaction
} // namespace oceanbase
