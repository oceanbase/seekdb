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

#include "storage/tablet/ob_tablet_replay_executor.h"
#include "share/rc/ob_server_runtime.h"
#include "storage/ls/ob_ls.h"
#include "storage/tx_storage/ob_ls_service.h"

namespace oceanbase
{
namespace storage
{

ERRSIM_POINT_DEF(EN_REPLAY_FATAL_ERROR);

#ifdef CLOG_LOG_LIMIT
#undef CLOG_LOG_LIMIT
#endif

#define CLOG_LOG_LIMIT(level, args...)             \
  do                                               \
  {                                                \
    if (REACH_TIME_INTERVAL(1000 * 1000)) {        \
      CLOG_LOG(level, ##args);                     \
    }                                              \
  } while(0)


int ObTabletReplayExecutor::replay_check_restore_status(storage::ObTabletHandle &tablet_handle, const bool update_tx_data)
{
  int ret = OB_SUCCESS;
  ObTablet *tablet = tablet_handle.get_obj();
  ObTabletRestoreStatus::STATUS restore_status = ObTabletRestoreStatus::STATUS::RESTORE_STATUS_MAX;
  if (OB_ISNULL(tablet)) {
    ret = OB_ERR_UNEXPECTED;
    CLOG_LOG(WARN, "tablet is null", K(ret));
  } else if (OB_FAIL(tablet->get_restore_status(restore_status))) {
  } else if (ObTabletRestoreStatus::is_undefined(restore_status)) {
    // UNDEFINED tablet need replay.
    ret = OB_SUCCESS;
    CLOG_LOG_LIMIT(INFO, "tablet is UNDEFINED, but need replay", K(restore_status), K(update_tx_data));
  } else if (ObTabletRestoreStatus::is_pending(restore_status)) {
    ret = OB_EAGAIN;
    CLOG_LOG_LIMIT(WARN, "tablet is PENDING, need retry", K(ret), K(restore_status), K(update_tx_data));
  }

  return ret;
}


int ObTabletReplayExecutor::execute(const share::SCN &scn, const common::ObTabletID &tablet_id)
{
  MDS_TG(5_ms);
  int ret = OB_SUCCESS;
  storage::ObTabletHandle tablet_handle;
  bool can_skip_replay = false;
  ObTablet *tablet = nullptr;
  ObLS *ls = nullptr;
  ObLSService *const ls_service = ::oceanbase::share::server_service<::oceanbase::storage::ObLSService>();
  if (OB_UNLIKELY(!is_inited_)) {
    ret = OB_NOT_INIT;
    CLOG_LOG(WARN, "replay executor not init", KR(ret), K_(is_inited));
  } else if (!scn.is_valid()) {
    ret = OB_INVALID_ARGUMENT;
    CLOG_LOG(WARN, "replay executor get invalid argument", KR(ret), K(scn));
  } else if (CLICK_FAIL(ls_service->get_ls(ls))) {
    CLOG_LOG(WARN, "fail to get log stream", KR(ret));
  } else if (CLICK_FAIL(check_can_skip_replay_(ls, scn, can_skip_replay))) {
    CLOG_LOG(WARN, "failed to check can skip reply", K(ret), K(scn));
  } else if (can_skip_replay) {
    // do nothing
  } else if (CLICK_FAIL(replay_get_tablet_(ls, tablet_id, scn, tablet_handle))) {
    if (OB_OBSOLETE_CLOG_NEED_SKIP == ret) {
      CLOG_LOG(INFO, "clog is already obsolete, should skip replay", K(ret), K(scn));
      ret = OB_SUCCESS;
    } else if (OB_EAGAIN == ret) {
      CLOG_LOG_LIMIT(WARN, "need retry to get tablet", K(ret), K(scn));
    } else {
      CLOG_LOG(WARN, "failed to get tablet", K(ret), K(scn));
    }
  } else if (CLICK_FAIL(replay_check_restore_status_(tablet_handle))) {
    if (OB_NO_NEED_UPDATE == ret) {
      CLOG_LOG(WARN, "no need replay after check restore status, skip this log", K(ret), K(scn));
    } else if (OB_EAGAIN == ret) {
      CLOG_LOG_LIMIT(WARN, "need retry after check restore status", K(ret), K(scn));
    } else {
      CLOG_LOG(ERROR, "failed to check restore status", K(ret), K(scn));
    }
  } else if (OB_ISNULL(tablet = tablet_handle.get_obj())) {
    ret = OB_ERR_UNEXPECTED;
    CLOG_LOG(WARN, "tablet should not be NULL", K(ret), KP(tablet));
  } else if (OB_ISNULL(tablet->get_tablet_pointer_())) {
    ret = OB_ERR_UNEXPECTED;
    CLOG_LOG(WARN, "tablet pointer should not be NULL", K(ret), KP(tablet));
  } else {
    ObTabletMdsSharedLockGuard mds_truncate_lock_guard(tablet->get_tablet_pointer_()->get_mds_truncate_lock());
    if (OB_FAIL(mds_truncate_lock_guard.get_ret())) {
    } else if (CLICK_FAIL(check_can_skip_replay_to_mds_(scn, tablet_handle, can_skip_replay))) {
      CLOG_LOG(WARN, "failed to check can skip reply to mds", K(ret), K(scn), K(tablet_handle));
    } else if (can_skip_replay) {
      //do nothing
    } else if (CLICK_FAIL(do_replay_(tablet_handle))) {
      if (OB_NO_NEED_UPDATE == ret) {
        CLOG_LOG(WARN, "no need replay, skip this log", K(ret), K(scn));
      } else if (OB_EAGAIN == ret) {
        CLOG_LOG_LIMIT(WARN, "failed to replay, need retry", K(ret), K(scn));
      } else {
        CLOG_LOG(ERROR, "failed to replay", K(ret), K(scn));
      }
    } 

  }
  return ret;
}

int ObTabletReplayExecutor::replay_get_tablet_(
    storage::ObLS *ls,
    const common::ObTabletID &tablet_id,
    const share::SCN &scn,
    storage::ObTabletHandle &tablet_handle)
{
  int ret = OB_SUCCESS;
  const bool is_update_mds_table = is_replay_update_mds_table_();
  if (!scn.is_valid() || !tablet_id.is_valid()) {
    ret = OB_INVALID_ARGUMENT;
    CLOG_LOG(WARN, "check can skip replay to mds get invalid argument", K(ret), K(scn), K(tablet_id));
  } else {
    if (is_replay_update_tablet_status_() || is_replay_ddl_control_log_()) {
      const bool allow_tablet_not_exist = replay_allow_tablet_not_exist_();
      if (!is_replay_update_tablet_status_() && is_replay_ddl_control_log_()) {
        share::ObTaskController::get().allow_next_syslog();
        CLOG_LOG(INFO, "force replay ddl control log", K(tablet_id), K(scn), K(allow_tablet_not_exist));
      }
      if (OB_FAIL(ls->replay_get_tablet_no_check(tablet_id, scn, allow_tablet_not_exist, tablet_handle))) {
      }
    } else if (OB_FAIL(ls->replay_get_tablet(tablet_id, scn, is_update_mds_table, tablet_handle))) {
    }

    if (OB_FAIL(ret)) {
      if (OB_TIMEOUT == ret) {
        ret = OB_EAGAIN;
        CLOG_LOG(WARN, "retry get tablet for timeout error", KR(ret), K(tablet_id));
      }
    }
  }

  return ret;
}

int ObTabletReplayExecutor::replay_check_restore_status_(storage::ObTabletHandle &tablet_handle)
{
  const bool update_user_data = is_replay_update_tablet_status_();
  return ObTabletReplayExecutor::replay_check_restore_status(tablet_handle, update_user_data);
}

int ObTabletReplayExecutor::check_can_skip_replay_to_mds_(
    const share::SCN &scn,
    storage::ObTabletHandle &tablet_handle,
    bool &can_skip)
{
  int ret = OB_SUCCESS;
  ObTablet *tablet = tablet_handle.get_obj();
  can_skip = false;

  if (!scn.is_valid()) {
    ret = OB_INVALID_ARGUMENT;
    CLOG_LOG(WARN, "check can skip replay to mds get invalid argument", K(ret), K(scn));
  } else if (!is_replay_update_mds_table_()) {
    can_skip = false;
  } else if (tablet->get_tablet_meta().mds_checkpoint_scn_ >= scn) {
    can_skip = true;
    CLOG_LOG(INFO, "skip replay to mds", KPC(tablet), K(scn));
  } else {
    can_skip = false;
  }
  return ret;
}

int ObTabletReplayExecutor::check_can_skip_replay_(
    storage::ObLS *ls,
    const share::SCN &scn,
    bool &can_skip)
{
  int ret = OB_SUCCESS;
  can_skip = false;
  if (!is_replay_update_tablet_status_()) {
    can_skip = false;
  } else if (!scn.is_valid()) {
    ret = OB_INVALID_ARGUMENT;
    CLOG_LOG(WARN, "check can skip replay to mds get invalid argument", K(ret), K(scn));
  } else {
    const share::SCN tablet_change_scn = ls->get_tablet_change_checkpoint_scn();
    if (scn <= tablet_change_scn) {
      can_skip = true;
      CLOG_LOG(INFO, "can skip replay", K(tablet_change_scn), K(scn));
    }
  }

  return ret;
}

int ObTabletReplayExecutor::replay_to_mds_table_(
    storage::ObTabletHandle &tablet_handle,
    const ObTabletCreateDeleteMdsUserData &mds,
    storage::mds::MdsCtx &ctx,
    const share::SCN &scn)
{
  int ret = OB_SUCCESS;
  storage::ObTablet *tablet = tablet_handle.get_obj();
  if (!is_replay_update_mds_table_()) {
    ret = OB_ERR_UNEXPECTED;
    CLOG_LOG(ERROR, "replay log do not update mds table, cannot replay to mds table", K(ret), K(tablet_handle));
  } else if (OB_ISNULL(tablet)) {
    ret = OB_ERR_UNEXPECTED;
    CLOG_LOG(WARN, "tablet should not be NULL", KR(ret));
  } else if (tablet->is_ls_inner_tablet()) {
    ret = OB_NOT_SUPPORTED;
    CLOG_LOG(WARN, "inner tablets have no mds table", KR(ret));
  } else {
    ObLSService *ls_svr = ::oceanbase::share::server_service<::oceanbase::storage::ObLSService>();
    ObLS *ls = nullptr;
    const common::ObTabletID &tablet_id = tablet->get_tablet_meta().tablet_id_;
    if (OB_FAIL(ls_svr->get_ls(ls))) {
    } else {
      if (OB_FAIL(ls->get_tablet_svr()->replay_set_tablet_status(tablet_id, scn, mds, ctx))) {
      }
    }
  }
  return ret;
}

int ObTabletReplayExecutor::replay_to_mds_table_(
    storage::ObTabletHandle &tablet_handle,
    const ObTabletBindingMdsUserData &mds,
    storage::mds::MdsCtx &ctx,
    const share::SCN &scn)
{
  int ret = OB_SUCCESS;
  storage::ObTablet *tablet = tablet_handle.get_obj();
  if (!is_replay_update_mds_table_()) {
    ret = OB_ERR_UNEXPECTED;
    CLOG_LOG(ERROR, "replay log do not update mds table, cannot replay to mds table", K(ret), K(tablet_handle));
  } else if (OB_ISNULL(tablet)) {
    ret = OB_ERR_UNEXPECTED;
    CLOG_LOG(WARN, "tablet should not be NULL", KR(ret));
  } else if (tablet->is_ls_inner_tablet()) {
    ret = OB_NOT_SUPPORTED;
    CLOG_LOG(WARN, "inner tablets have no mds table", KR(ret));
  } else {
    ObLSService *ls_svr = ::oceanbase::share::server_service<::oceanbase::storage::ObLSService>();
    ObLS *ls = nullptr;
    const common::ObTabletID &tablet_id = tablet->get_tablet_meta().tablet_id_;
    if (OB_FAIL(ls_svr->get_ls(ls))) {
    } else {
      if (OB_FAIL(ls->get_tablet_svr()->replay_set_ddl_info(tablet_id, scn, mds, ctx))) {
      }
    }
  }
  return ret;
}

int ObTabletReplayExecutor::replay_to_mds_table_(
    storage::ObTabletHandle &tablet_handle,
    const ObTabletDDLCompleteMdsUserData &mds,
    storage::mds::MdsCtx &ctx,
    const share::SCN &scn)
{
  int ret = OB_SUCCESS;
  storage::ObTablet *tablet = tablet_handle.get_obj();
  if (!is_replay_update_mds_table_()) {
    ret = OB_ERR_UNEXPECTED;
    CLOG_LOG(ERROR, "replay log do not update mds table, cannot replay to mds table", K(ret), K(tablet_handle));
  } else if (OB_ISNULL(tablet)) {
    ret = OB_ERR_UNEXPECTED;
    CLOG_LOG(WARN, "tablet should not be NULL", KR(ret));
  } else if (tablet->is_ls_inner_tablet()) {
    ret = OB_NOT_SUPPORTED;
    CLOG_LOG(WARN, "inner tablets have no mds table", KR(ret));
  } else {
    ObLSService *ls_svr = ::oceanbase::share::server_service<::oceanbase::storage::ObLSService>();
    ObLS *ls = nullptr;
    const common::ObTabletID &tablet_id = tablet->get_tablet_meta().tablet_id_;
    if (OB_FAIL(ls_svr->get_ls(ls))) {
    } else {
      if (OB_FAIL(ls->get_tablet_svr()->replay_set_ddl_complete(
          tablet_id, scn, mds::DummyKey(), mds, ctx))) {
      }
    }
  }
  return ret;
}

}
}
