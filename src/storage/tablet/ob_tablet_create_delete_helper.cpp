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

#include "storage/tablet/ob_tablet_create_delete_helper.h"
#include "share/rc/ob_module_provider.h"
#include "storage/tx/ob_trans_part_ctx.h"
#include "storage/tx/ob_trans_service.h"
#include "storage/tx_storage/ob_ls_service.h"
#include "storage/meta_store/ob_storage_meta_io_util.h"

#define USING_LOG_PREFIX STORAGE

using namespace oceanbase::common;
using namespace oceanbase::share;
using namespace oceanbase::share::schema;
using namespace oceanbase::blocksstable;
using namespace oceanbase::transaction;
using namespace oceanbase::palf;
using namespace oceanbase::memtable;

namespace oceanbase
{
namespace storage
{
ObTabletCreateDeleteHelper::ReadMdsFunctor::ReadMdsFunctor(ObTabletCreateDeleteMdsUserData &user_data)
  : user_data_(user_data)
{
}

int ObTabletCreateDeleteHelper::replay_mds_get_tablet(
    const ObTabletMapKey &key, ObLS *ls, ObTabletHandle &handle)
{
  int ret = OB_SUCCESS;
  if (OB_ISNULL(ls)) {
    ret = OB_INVALID_ARGUMENT;
    LOG_WARN("ls is null", K(ret));
  } else if (OB_FAIL(ObTabletCreateDeleteHelper::get_tablet(key, handle))) {
    if (OB_TABLET_NOT_EXIST == ret) {
    } else {
      LOG_WARN("fail to get tablet", K(ret), K(key));
    }
  }
  return ret;
}


int ObTabletCreateDeleteHelper::get_tablet(
    const ObTabletMapKey &key,
    ObTabletHandle &handle,
    const int64_t timeout_us)
{
#ifdef ENABLE_DEBUG_LOG
  ObTimeGuard tg("ObTabletCreateDeleteHelper::get_tablet", 10000);
#endif
  int ret = OB_SUCCESS;
  static const int64_t SLEEP_TIME_US = 10;
  ObTenantMetaMemMgr *t3m = share::g_mp->tenant_meta_mem_mgr();
  const int64_t begin_time = ObClockGenerator::getClock();
  int64_t current_time = 0;

  while (OB_SUCC(ret)) {
    ret = t3m->get_tablet(WashTabletPriority::WTP_HIGH, key, handle);
    if (OB_SUCC(ret)) {
      break;
    } else if (OB_ENTRY_NOT_EXIST == ret) {
      ret = OB_TABLET_NOT_EXIST;
      LOG_DEBUG("tablet does not exist", K(ret), K(key));
    } else if (OB_ITEM_NOT_SETTED == ret) {
      current_time = ObClockGenerator::getClock();
      if (current_time - begin_time > timeout_us) {
        ret = OB_TABLET_NOT_EXIST;
        LOG_WARN("continuously meet item not set error", K(ret), K(key),
            K(begin_time), K(current_time), K(timeout_us));
      } else {
        ret = OB_SUCCESS;
        ob_usleep(SLEEP_TIME_US);
      }
    } else {
      LOG_WARN("failed to get tablet", K(ret), K(key));
    }
  }
  return ret;
}

int ObTabletCreateDeleteHelper::check_and_get_tablet(
    const ObTabletMapKey &key,
    ObTabletHandle &handle,
    const int64_t timeout_us,
    const ObMDSGetTabletMode mode,
    const int64_t snapshot_version)
{
  int ret = OB_SUCCESS;
  ObTablet *tablet = nullptr;

  if (OB_FAIL(get_tablet(key, handle, timeout_us))) {
    if (OB_TABLET_NOT_EXIST == ret) {
      LOG_DEBUG("tablet does not exist", K(ret), K(key), K(mode));
    } else {
      LOG_WARN("failed to get tablet", K(ret), K(key), K(mode));
    }
  } else if (OB_ISNULL(tablet = handle.get_obj())) {
    ret = OB_ERR_UNEXPECTED;
    LOG_WARN("tablet is null", K(ret), K(handle));
  } else if (tablet->is_ls_inner_tablet()) {
    // no need to check ls inner tablet, do nothing
  } else if (ObMDSGetTabletMode::READ_WITHOUT_CHECK == mode) {
    // no checking
  } else if (ObMDSGetTabletMode::READ_ALL_COMMITED == mode) {
    if (OB_UNLIKELY(snapshot_version != ObTransVersion::MAX_TRANS_VERSION)) {
      ret = OB_NOT_SUPPORTED;
      LOG_WARN("read all committed mode should only pass max scn", K(ret), K(key), K(mode), K(snapshot_version));
    } else if (OB_FAIL(tablet->check_tablet_status_for_read_all_committed())) {
      LOG_WARN("failed to check tablet status", K(ret), K(key));
    }
  } else if (ObMDSGetTabletMode::READ_READABLE_COMMITED == mode) {
    if (OB_FAIL(tablet->check_new_mds_with_cache(snapshot_version))) {
      LOG_WARN("failed to check status for new mds", K(ret), K(mode), K(snapshot_version));
    }
  } else {
    ret = OB_ERR_UNEXPECTED;
    LOG_WARN("unexpected mode", K(ret), K(key), K(mode));
  }
  return ret;
}

int ObTabletCreateDeleteHelper::check_status_for_new_mds(
    const ObTablet &tablet,
    const int64_t snapshot_version,
    ObTabletStatusCache &tablet_status_cache)
{
  int ret = OB_SUCCESS;
  const ObLSID &ls_id = tablet.get_tablet_meta().ls_id_;
  const ObTabletID &tablet_id = tablet.get_tablet_meta().tablet_id_;
  ObTabletCreateDeleteMdsUserData user_data;
  mds::MdsWriter writer;// will be removed later
  mds::TwoPhaseCommitState trans_state;// will be removed later
  share::SCN trans_version;// will be removed later

  if (OB_UNLIKELY(tablet.is_empty_shell())) {
    ret = OB_TABLET_NOT_EXIST;
    LOG_WARN("tablet is empty shell", K(ret), K(ls_id), K(tablet_id), K(user_data));
  } else if (OB_FAIL(tablet.get_latest(user_data, writer, trans_state, trans_version))) {
    if (OB_EMPTY_RESULT == ret) {
      ret = OB_TABLET_NOT_EXIST;
      LOG_WARN("tablet creation has not been committed, or has been roll backed", K(ret), K(ls_id), K(tablet_id));
    } else {
      LOG_WARN("failed to get snapshot", KR(ret), K(ls_id), K(tablet_id));
    }
  } else {
    const ObTabletStatus::Status &status = user_data.tablet_status_.get_status();
    switch (status) {
      case ObTabletStatus::NORMAL:
      case ObTabletStatus::SPLIT_DST:
        ret = check_read_snapshot_for_normal_or_split_dst(tablet, snapshot_version, user_data, writer, trans_state, trans_version);
        break;
      case ObTabletStatus::RESERVED_5:
        ret = check_read_snapshot_for_reserved_5(tablet, snapshot_version, user_data, writer, trans_state, trans_version);
        break;
      case ObTabletStatus::DELETED:
      case ObTabletStatus::RESERVED_4:
        ret = check_read_snapshot_for_deleted_or_reserved_4(tablet, snapshot_version, user_data, writer, trans_state, trans_version);
        break;
      case ObTabletStatus::RESERVED_6:
        ret = check_read_snapshot_for_reserved_6(tablet, snapshot_version, user_data);
        break;
      case ObTabletStatus::SPLIT_SRC:
        ret = check_read_snapshot_for_split_src(tablet, snapshot_version, user_data, trans_state);
        break;
      case ObTabletStatus::SPLIT_SRC_DELETED:
        ret = check_read_snapshot_for_split_src_deleted(tablet, user_data, trans_state);
        break;
      default:
        ret = OB_ERR_UNEXPECTED;
        LOG_WARN("unexpected tablet status", K(ret), K(ls_id), K(tablet_id), K(user_data));
    }

    if (OB_FAIL(ret)) {
    } else if (mds::TwoPhaseCommitState::ON_COMMIT == trans_state &&
        (ObTabletStatus::NORMAL == user_data.tablet_status_ || ObTabletStatus::SPLIT_DST == user_data.tablet_status_)) {
      tablet_status_cache.set_value(user_data);
      LOG_INFO("refresh tablet status cache", K(ret), K(ls_id), K(tablet_id), K(tablet_status_cache), K(snapshot_version));
    }
  }

  return ret;
}

int ObTabletCreateDeleteHelper::check_read_snapshot_by_commit_version(
    const ObTablet &tablet,
    const int64_t create_commit_version,
    const int64_t delete_commit_version,
    const int64_t snapshot_version,
    const ObTabletStatus &tablet_status)
{
  int ret = OB_SUCCESS;
  const ObLSID &ls_id = tablet.get_tablet_meta().ls_id_;
  const ObTabletID &tablet_id = tablet.get_tablet_meta().tablet_id_;

  if (snapshot_version == ObTransVersion::MAX_TRANS_VERSION) {
    // do nothing
  } else if (OB_UNLIKELY(create_commit_version == ObTransVersion::INVALID_TRANS_VERSION)) {
    ret = OB_ERR_UNEXPECTED;
    LOG_WARN("create tablet trans version is invalid",
        K(ret), K(ls_id), K(tablet_id), K(snapshot_version), K(create_commit_version));
  } else if (snapshot_version < create_commit_version) {
    // read snapshot is smaller than create tablet trans version,
    // no previous committed transaction
    ret= OB_SNAPSHOT_DISCARDED;
    LOG_INFO("tablet status is set to MAX because read snapshot is smaller than create trans version",
        K(ret), K(ls_id), K(tablet_id), K(snapshot_version), K(create_commit_version));
  } else if (delete_commit_version == ObTransVersion::INVALID_TRANS_VERSION) {
    // delete commit version is not valid, no delete transaction committed
  } else if (snapshot_version < delete_commit_version) {
    // read snapshot is smaller than delete tablet trans version,
    // previous transaction is create tablet, so tablet status is NORMAL
    LOG_INFO("tablet status is set to NORMAL because read snapshot is smaller than delete trans version",
        K(ret), K(ls_id), K(tablet_id), K(snapshot_version), K(delete_commit_version));
  } else {
    // snapshot_version >= user_data.delete_commit_version_
    ret = ObTabletStatus::RESERVED_6 == tablet_status ? OB_TABLET_NOT_EXIST : OB_TABLE_NOT_EXIST;
    LOG_INFO("tablet is deleted or in reserved deleted state",
        K(ret), K(ls_id), K(tablet_id), K(tablet_status), K(snapshot_version), K(delete_commit_version));
  }

  if (OB_FAIL(ret)) {
  } else if (ObTabletStatus::NORMAL == tablet_status || ObTabletStatus::RESERVED_5 == tablet_status || ObTabletStatus::SPLIT_DST == tablet_status) {
    if (OB_UNLIKELY(tablet.is_empty_shell())) {
      ret = OB_TABLET_NOT_EXIST;
      LOG_WARN("tablet is empty shell", K(ret), K(ls_id), K(tablet_id), K(snapshot_version), K(create_commit_version));
    }
  } else {
    ret = OB_TABLET_NOT_EXIST;
  }

  return ret;
}

int ObTabletCreateDeleteHelper::check_read_snapshot_for_normal_or_split_dst(
    const ObTablet &tablet,
    const int64_t snapshot_version,
    const ObTabletCreateDeleteMdsUserData &user_data,
    const mds::MdsWriter &writer,
    const mds::TwoPhaseCommitState &trans_state,
    const share::SCN &trans_version)
{
  int ret = OB_SUCCESS;
  const share::ObLSID &ls_id = tablet.get_tablet_meta().ls_id_;
  const common::ObTabletID &tablet_id = tablet.get_tablet_meta().tablet_id_;
  const ObTabletStatus &tablet_status = user_data.tablet_status_;
  share::SCN read_snapshot;

  if (OB_UNLIKELY(ObTabletStatus::NORMAL != tablet_status && ObTabletStatus::SPLIT_DST != tablet_status)) {
    ret = OB_INVALID_ARGUMENT;
    LOG_WARN("invalid args", K(ret), K(ls_id), K(tablet_id), K(user_data));
  } else if (user_data.create_commit_version_ == ObTransVersion::MAX_TRANS_VERSION) {
    ret = OB_ERR_UNEXPECTED;
    LOG_WARN("create commit version is max trans version",
        K(ret), K(ls_id), K(tablet_id), K(snapshot_version), K(trans_state), K(user_data));
  } else if (user_data.create_commit_version_ != ObTransVersion::INVALID_TRANS_VERSION) {
    LOG_INFO("tablet create transaction is committed, currently in reserved status transaction",
        K(ret), K(ls_id), K(tablet_id), K(snapshot_version), K(trans_state), K(user_data));
    if (OB_FAIL(check_read_snapshot_for_reserved_status_tx(tablet, snapshot_version, user_data))) {
      LOG_WARN("fail to check readsnapshot for reserved status tx",
          K(ret), K(ls_id), K(tablet_id), K(snapshot_version), K(trans_state), K(user_data));
    }
  } else if (OB_FAIL(check_read_snapshot_for_create_tx(
      tablet, snapshot_version, user_data, writer, trans_state, trans_version))) {
    LOG_WARN("fail to check read snapshot for create tx",
        K(ret), K(ls_id), K(tablet_id), K(snapshot_version), K(trans_state), K(user_data));
  }

  return ret;
}

int ObTabletCreateDeleteHelper::check_read_snapshot_for_reserved_status_tx(
    const ObTablet &tablet,
    const int64_t snapshot_version,
    const ObTabletCreateDeleteMdsUserData &user_data)
{
  int ret = OB_SUCCESS;
  const share::ObLSID &ls_id = tablet.get_tablet_meta().ls_id_;
  const common::ObTabletID &tablet_id = tablet.get_tablet_meta().tablet_id_;

  if (snapshot_version < user_data.create_commit_version_) {
    ret = OB_SNAPSHOT_DISCARDED;
    LOG_WARN("read snapshot smaller than create commit version",
        K(ret), K(ls_id), K(tablet_id), K(snapshot_version), K(user_data));
  }

  return ret;
}

int ObTabletCreateDeleteHelper::check_read_snapshot_for_create_tx(
    const ObTablet &tablet,
    const int64_t snapshot_version,
    const ObTabletCreateDeleteMdsUserData &user_data,
    const mds::MdsWriter &writer,
    const mds::TwoPhaseCommitState &trans_state,
    const share::SCN &trans_version)
{
  int ret = OB_SUCCESS;
  const share::ObLSID &ls_id = tablet.get_tablet_meta().ls_id_;
  const common::ObTabletID &tablet_id = tablet.get_tablet_meta().tablet_id_;
  const ObTabletStatus &tablet_status = user_data.tablet_status_;
  share::SCN read_snapshot;

  if (trans_state < mds::TwoPhaseCommitState::ON_PREPARE) {
    ret = OB_SNAPSHOT_DISCARDED;
    LOG_WARN("tablet creation transaction has not entered 2pc procedure",
        K(ret), K(ls_id), K(tablet_id), K(snapshot_version), K(trans_state), K(user_data));
  } else if (OB_FAIL(read_snapshot.convert_for_tx(snapshot_version))) {
    LOG_WARN("failed to convert from int64_t to SCN", K(ret), K(snapshot_version));
  } else if (trans_state >= mds::TwoPhaseCommitState::ON_PREPARE && trans_state < mds::TwoPhaseCommitState::ON_COMMIT) {
    if (read_snapshot < trans_version) {
      ret = OB_SNAPSHOT_DISCARDED;
      LOG_WARN("read snapshot is smaller than prepare version",
          K(ret), K(ls_id), K(tablet_id), K(trans_state), K(read_snapshot), K(trans_version));
    } else {
      // primary tenant
      ret = OB_SNAPSHOT_DISCARDED;
      LOG_WARN("tablet creation transaction has not committed",
          K(ret), K(ls_id), K(tablet_id), K(trans_state), K(read_snapshot), K(trans_version));
    }
  } else if (mds::TwoPhaseCommitState::ON_COMMIT == trans_state) {
    if (snapshot_version < user_data.create_commit_version_) {
      ret = OB_SNAPSHOT_DISCARDED;
      LOG_WARN("read snapshot smaller than create commit version",
          K(ret), K(ls_id), K(tablet_id), K(snapshot_version), K(user_data));
    }
  }

  return ret;
}

int ObTabletCreateDeleteHelper::check_read_snapshot_for_reserved_5(
    const ObTablet &tablet,
    const int64_t snapshot_version,
    const ObTabletCreateDeleteMdsUserData &user_data,
    const mds::MdsWriter &writer,
    const mds::TwoPhaseCommitState &trans_state,
    const share::SCN &trans_version)
{
  int ret = OB_SUCCESS;
  const share::ObLSID &ls_id = tablet.get_tablet_meta().ls_id_;
  const common::ObTabletID &tablet_id = tablet.get_tablet_meta().tablet_id_;
  const ObTabletStatus &tablet_status = user_data.tablet_status_;
  share::SCN read_snapshot;

  if (OB_UNLIKELY(ObTabletStatus::RESERVED_5 != tablet_status)) {
    ret = OB_INVALID_ARGUMENT;
    LOG_WARN("invalid args", K(ret), K(ls_id), K(tablet_id), K(user_data));
  } else if (snapshot_version < user_data.create_commit_version_) {
    ret = OB_SNAPSHOT_DISCARDED;
    LOG_WARN("read snapshot smaller than create commit version",
        K(ret), K(ls_id), K(tablet_id), K(snapshot_version), K(user_data));
  } else if (trans_state < mds::TwoPhaseCommitState::ON_PREPARE) {
    ret = OB_TABLET_NOT_EXIST;
    LOG_WARN("reserved tablet status transaction has not entered 2pc procedure, should retry",
        K(ret), K(ls_id), K(tablet_id), K(snapshot_version), K(trans_state));
  } else if (OB_FAIL(read_snapshot.convert_for_tx(snapshot_version))) {
    LOG_WARN("failed to convert from int64_t to SCN", K(ret), K(snapshot_version));
  } else if (trans_state >= mds::TwoPhaseCommitState::ON_PREPARE && trans_state < mds::TwoPhaseCommitState::ON_COMMIT) {
    if (read_snapshot < trans_version) {
      ret = OB_TABLET_NOT_EXIST;
      LOG_WARN("read snapshot is smaller than prepare version, should retry",
          K(ret), K(ls_id), K(tablet_id), K(trans_state), K(read_snapshot), K(trans_version));
    } else {
      // primary tenant: not allowed to read, retry
      ret = OB_TABLET_NOT_EXIST;
      LOG_WARN("read snapshot is no smaller than prepare version, primary tenant should retry",
          K(ret), K(ls_id), K(tablet_id), K(trans_state), K(read_snapshot), K(trans_version));
    }
  } else if (mds::TwoPhaseCommitState::ON_COMMIT == trans_state) {
    if (read_snapshot < trans_version) {
      // not allow to read
      ret = OB_TABLET_NOT_EXIST;
      LOG_WARN("read snapshot is smaller than reserved status commit version, should retry",
          K(ret), K(ls_id), K(tablet_id), K(trans_state), K(read_snapshot), K(trans_version));
    }
  }

  return ret;
}

int ObTabletCreateDeleteHelper::check_read_snapshot_for_deleted_or_reserved_4(
    const ObTablet &tablet,
    const int64_t snapshot_version,
    const ObTabletCreateDeleteMdsUserData &user_data,
    const mds::MdsWriter &writer,
    const mds::TwoPhaseCommitState &trans_state,
    const share::SCN &trans_version)
{
  int ret = OB_SUCCESS;
  const share::ObLSID &ls_id = tablet.get_tablet_meta().ls_id_;
  const common::ObTabletID &tablet_id = tablet.get_tablet_meta().tablet_id_;
  const ObTabletStatus &tablet_status = user_data.tablet_status_;
  share::SCN read_snapshot;

  if (OB_UNLIKELY(ObTabletStatus::RESERVED_4 != tablet_status && ObTabletStatus::DELETED != tablet_status)) {
    ret = OB_INVALID_ARGUMENT;
    LOG_WARN("invalid args", K(ret), K(ls_id), K(tablet_id), K(user_data));
  } else if (snapshot_version < user_data.create_commit_version_) {
    ret = OB_SNAPSHOT_DISCARDED;
    LOG_WARN("read snapshot smaller than create commit version",
        K(ret), K(ls_id), K(tablet_id), K(snapshot_version), K(user_data));
  } else if (OB_FAIL(read_snapshot.convert_for_tx(snapshot_version))) {
    LOG_WARN("failed to convert from int64_t to SCN", K(ret), K(snapshot_version));
  } else if (trans_state < mds::TwoPhaseCommitState::ON_PREPARE) {
    if (read_snapshot.is_max()) {
      ret = OB_TABLET_NOT_EXIST;
      LOG_WARN("read snapshot is MAX, maybe this is a write request, should retry",
          K(ret), K(ls_id), K(tablet_id), K(read_snapshot), K(user_data));
    }
  } else if (trans_state >= mds::TwoPhaseCommitState::ON_PREPARE && trans_state < mds::TwoPhaseCommitState::ON_COMMIT) {
    if (read_snapshot < trans_version) {
      // allow to read
    } else {
      // primary tenant: retry
      ret = OB_TABLET_NOT_EXIST;
      LOG_INFO("read snapshot is no smaller than prepare version on primary tenant, should retry",
          K(ret), K(ls_id), K(tablet_id), K(trans_state), K(read_snapshot), K(trans_version));
    }
  } else if (mds::TwoPhaseCommitState::ON_COMMIT == trans_state) {
    if (read_snapshot < trans_version) {
      // allow to read
    } else {
      ret = OB_TABLET_NOT_EXIST;
      LOG_WARN("read snapshot is no smaller than trans version, should retry",
          K(ret), K(ls_id), K(tablet_id), K(read_snapshot), K(trans_version));
    }
  }

  return ret;
}

int ObTabletCreateDeleteHelper::check_read_snapshot_for_reserved_6(
    const ObTablet &tablet,
    const int64_t snapshot_version,
    const ObTabletCreateDeleteMdsUserData &user_data)
{
  int ret = OB_SUCCESS;
  const share::ObLSID &ls_id = tablet.get_tablet_meta().ls_id_;
  const common::ObTabletID &tablet_id = tablet.get_tablet_meta().tablet_id_;
  const ObTabletStatus &tablet_status = user_data.tablet_status_;

  if (OB_UNLIKELY(ObTabletStatus::RESERVED_6 != tablet_status)) {
    ret = OB_INVALID_ARGUMENT;
    LOG_WARN("invalid args", K(ret), K(ls_id), K(tablet_id), K(user_data));
  } else if (snapshot_version < user_data.create_commit_version_) {
    ret = OB_SNAPSHOT_DISCARDED;
    LOG_WARN("read snapshot smaller than create commit version",
        K(ret), K(ls_id), K(tablet_id), K(snapshot_version), K(user_data));
  } else if (snapshot_version >= user_data.reserved_commit_version_) {
    ret = OB_TABLET_NOT_EXIST;
    LOG_WARN("read snapshot is no smaller than reserved status commit version, should retry",
        K(ret), K(ls_id), K(tablet_id), K(snapshot_version), K(user_data));
  }

  return ret;
}

int ObTabletCreateDeleteHelper::check_read_snapshot_for_split_src(
    const ObTablet &tablet,
    const int64_t snapshot_version,
    const ObTabletCreateDeleteMdsUserData &user_data,
    const mds::TwoPhaseCommitState &trans_state)
{
  int ret = OB_SUCCESS;
  const share::ObLSID &ls_id = tablet.get_tablet_meta().ls_id_;
  const common::ObTabletID &tablet_id = tablet.get_tablet_meta().tablet_id_;
  const ObTabletStatus &tablet_status = user_data.tablet_status_;

  if (OB_UNLIKELY(ObTabletStatus::SPLIT_SRC != tablet_status)) {
    ret = OB_INVALID_ARGUMENT;
    LOG_WARN("invalid args", K(ret), K(ls_id), K(tablet_id), K(user_data));
  } else if (mds::TwoPhaseCommitState::ON_COMMIT == trans_state) {
    if (snapshot_version < user_data.create_commit_version_) {
      ret = OB_SNAPSHOT_DISCARDED;
      LOG_WARN("read snapshot smaller than create commit version",
          K(ret), K(ls_id), K(tablet_id), K(snapshot_version), K(user_data));
    } else {
      ret = OB_TABLET_IS_SPLIT_SRC;
      LOG_WARN("tablet is split src", K(ret), K(ls_id), K(tablet_id), K(common::lbt()));
    }
  }

  return ret;
}

int ObTabletCreateDeleteHelper::check_read_snapshot_for_split_src_deleted(
    const ObTablet &tablet,
    const ObTabletCreateDeleteMdsUserData &user_data,
    const mds::TwoPhaseCommitState &trans_state)
{
  int ret = OB_SUCCESS;
  const share::ObLSID &ls_id = tablet.get_tablet_meta().ls_id_;
  const common::ObTabletID &tablet_id = tablet.get_tablet_meta().tablet_id_;
  const ObTabletStatus &tablet_status = user_data.tablet_status_;

  if (OB_UNLIKELY(ObTabletStatus::SPLIT_SRC_DELETED != tablet_status)) {
    ret = OB_INVALID_ARGUMENT;
    LOG_WARN("invalid args", K(ret), K(ls_id), K(tablet_id), K(user_data));
  } else if (mds::TwoPhaseCommitState::ON_COMMIT == trans_state) {
    ret = OB_TABLET_NOT_EXIST;
    LOG_WARN("split src deleted", K(ret), K(ls_id), K(tablet_id), K(common::lbt()));
  }

  return ret;
}

int ObTabletCreateDeleteHelper::create_tmp_tablet(
    const ObTabletMapKey &key,
    common::ObArenaAllocator &allocator,
    ObTabletHandle &handle)
{
  int ret = OB_SUCCESS;
  ObLSHandle ls_handle;
  ObLSService *ls_service = share::g_mp->ls_service();
  ObTenantMetaMemMgr *t3m = share::g_mp->tenant_meta_mem_mgr();
  if (OB_UNLIKELY(!key.is_valid())) {
    ret = OB_INVALID_ARGUMENT;
    LOG_WARN("invalid arguments", K(ret), K(key));
  } else if (OB_FAIL(ls_service->get_ls(key.ls_id_, ls_handle, ObLSGetMod::TABLET_MOD))) {
    LOG_WARN("fail to get ls", K(ret), "ls_id", key.ls_id_);
  } else if (OB_FAIL(t3m->create_tmp_tablet(WashTabletPriority::WTP_HIGH, key, allocator, ls_handle, handle))) {
    LOG_WARN("fail to create temporary tablet", K(ret), K(key));
  } else if (OB_ISNULL(handle.get_obj())) {
    ret = OB_ERR_UNEXPECTED;
    LOG_ERROR("new tablet is null", K(ret), K(handle));
  }
  return ret;
}

int ObTabletCreateDeleteHelper::prepare_create_msd_tablet()
{
  int ret = OB_SUCCESS;
  ObTenantMetaMemMgr *t3m = share::g_mp->tenant_meta_mem_mgr();
  if (OB_FAIL(t3m->get_mstx_tablet_creator().throttle_tablet_creation())) {
    LOG_WARN("fail to prepare full tablet", K(ret));
  }
  return ret;
}

int ObTabletCreateDeleteHelper::create_msd_tablet(
    const ObTabletMapKey &key,
    ObTabletHandle &handle)
{
  int ret = OB_SUCCESS;
  ObLSHandle ls_handle;
  ObLSService *ls_service = share::g_mp->ls_service();
  ObTenantMetaMemMgr *t3m = share::g_mp->tenant_meta_mem_mgr();
  if (OB_UNLIKELY(!key.is_valid())) {
    ret = OB_INVALID_ARGUMENT;
    LOG_WARN("invalid arguments", K(ret), K(key));
  } else if (OB_FAIL(ls_service->get_ls(key.ls_id_, ls_handle, ObLSGetMod::TABLET_MOD))) {
    LOG_WARN("fail to get ls", K(ret), "ls_id", key.ls_id_);
  } else if (OB_FAIL(t3m->create_msd_tablet(WashTabletPriority::WTP_HIGH, key, ls_handle, handle))) {
    LOG_WARN("fail to create multi source data tablet", K(ret), K(key));
  } else if (OB_ISNULL(handle.get_obj())) {
    ret = OB_ERR_UNEXPECTED;
    LOG_ERROR("new tablet is null", K(ret), K(handle));
  }
  return ret;
}

int ObTabletCreateDeleteHelper::acquire_tmp_tablet(
    const ObTabletMapKey &key,
    common::ObArenaAllocator &allocator,
    ObTabletHandle &handle)
{
  TIMEGUARD_INIT(STORAGE, 10_ms);
  int ret = OB_SUCCESS;
  ObTenantMetaMemMgr *t3m = share::g_mp->tenant_meta_mem_mgr();
  if (OB_UNLIKELY(!key.is_valid())) {
    ret = OB_INVALID_ARGUMENT;
    LOG_WARN("invalid arguments", K(ret), K(key));
  } else if (CLICK_FAIL(t3m->acquire_tmp_tablet(WashTabletPriority::WTP_HIGH, key, allocator, handle))) {
    LOG_WARN("fail to acquire temporary tablet", K(ret), K(key));
  } else if (OB_ISNULL(handle.get_obj())) {
    ret = OB_ERR_UNEXPECTED;
    LOG_ERROR("new tablet is null", K(ret), K(handle));
  }
  return ret;
}

int ObTabletCreateDeleteHelper::acquire_tablet_from_pool(
    const ObTabletPoolType &type,
    const ObTabletMapKey &key,
    ObTabletHandle &handle)
{
  int ret = OB_SUCCESS;
  ObTenantMetaMemMgr *t3m = share::g_mp->tenant_meta_mem_mgr();
  if (OB_UNLIKELY(!key.is_valid())) {
    ret = OB_INVALID_ARGUMENT;
    LOG_WARN("invalid arguments", K(ret), K(key));
  } else if (OB_FAIL(t3m->acquire_tablet_from_pool(type, WashTabletPriority::WTP_HIGH, key, handle))) {
    LOG_WARN("fail to acquire tablet from pool", K(ret), K(key), K(type));
  } else if (OB_ISNULL(handle.get_obj())) {
    ret = OB_ERR_UNEXPECTED;
    LOG_ERROR("new tablet is null", K(ret), K(handle));
  }
  return ret;
}

int ObTabletCreateDeleteHelper::create_empty_sstable(
    common::ObArenaAllocator &allocator,
    const ObStorageSchema &storage_schema,
    const common::ObTabletID &tablet_id,
    const int64_t snapshot_version,
    ObTableHandleV2 &table_handle)
{
  int ret = OB_SUCCESS;
  table_handle.reset();
  ObTabletCreateSSTableParam param;

  if (OB_UNLIKELY(!storage_schema.is_valid() || snapshot_version < 0)) {
    ret = OB_INVALID_ARGUMENT;
    LOG_WARN("get invalid arguments", K(ret), K(snapshot_version), K(storage_schema));
  } else if (OB_FAIL(param.init_for_empty_major_sstable(tablet_id, storage_schema, snapshot_version))) {
    LOG_WARN("failed to build sstable param", K(ret), K(tablet_id), K(storage_schema), K(snapshot_version));
  } else if (OB_FAIL(create_sstable(param, allocator, table_handle))) {
    LOG_WARN("failed to create sstable", K(ret), K(param));
  }

  if (OB_FAIL(ret)) {
    table_handle.reset();
  }
  return ret;
}

bool ObTabletCreateDeleteHelper::is_pure_hidden_tablets(const ObCreateTabletInfo &info)
{
  const ObTabletID &data_tablet_id = info.data_tablet_id_;
  const ObSArray<ObTabletID> &tablet_ids = info.tablet_ids_;
  return tablet_ids.count() >= 1 && !is_contain(tablet_ids, data_tablet_id) && info.is_create_bind_hidden_tablets_;
}
} // namespace storage
} // namespace oceanbase
