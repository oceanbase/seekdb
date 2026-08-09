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

#define USING_LOG_PREFIX STORAGE

#include "standby/restore/ob_standby_sstable_copier.h"
#include "share/ob_server_struct.h"
#include "share/rc/ob_server_runtime.h"
#include "standby/restore/ob_physical_copy_task.h"
#include "standby/restore/ob_restore_helper.h"
#include "standby/standby_host.h"
#include "standby/restore/ob_standby_restore_tablet_builder.h"
#include "standby/restore/ob_tablet_copy_finish_task.h"
#include "standby/restore/ob_sstable_copy_finish_task.h"
#include "standby/ob_standby_grpc.h"
#include "standby/ob_standby_palf_base_info.h"
#include "storage/ls/ob_ls.h"
#include "storage/ls/ob_ls_meta.h"
#include "storage/tablet/ob_tablet_create_delete_helper.h"
#include "storage/tx_storage/ob_ls_service.h"

using namespace oceanbase::common;
using namespace oceanbase::share;
using namespace oceanbase::blocksstable;
using namespace oceanbase::restore;

namespace oceanbase
{
namespace storage
{
ObStandbySSTableCopier::CopyTabletCtx::CopyTabletCtx()
    : status_(ObCopyTabletStatus::MAX_STATUS),
      extra_info_()
{
}

int ObStandbySSTableCopier::CopyTabletCtx::set_copy_tablet_status(
    const ObCopyTabletStatus::STATUS &status)
{
  int ret = OB_SUCCESS;
  if (!ObCopyTabletStatus::is_valid(status)) {
    ret = OB_INVALID_ARGUMENT;
    LOG_WARN("invalid copy tablet status", K(ret), K(status));
  } else {
    status_ = status;
  }
  return ret;
}

int ObStandbySSTableCopier::CopyTabletCtx::get_copy_tablet_status(
    ObCopyTabletStatus::STATUS &status) const
{
  status = status_;
  return OB_SUCCESS;
}

int ObStandbySSTableCopier::CopyTabletCtx::get_copy_tablet_record_extra_info(
    ObCopyTabletRecordExtraInfo *&extra_info)
{
  extra_info = &extra_info_;
  return OB_SUCCESS;
}

ObStandbySSTableCopier::ObStandbySSTableCopier()
    : is_inited_(false),
      replay_base_prepared_(false),
      src_(),
      bandwidth_throttle_(nullptr),
      config_(nullptr),
      source_ls_meta_(),
      physical_checkpoint_scn_(),
      ls_view_helper_()
{
}

int ObStandbySSTableCopier::init(
    const common::ObAddr &src,
    common::ObInOutBandwidthThrottle *bandwidth_throttle,
    const standby::StandbyConfig &config)
{
  int ret = OB_SUCCESS;
  if (is_inited_) {
    ret = OB_INIT_TWICE;
    LOG_WARN("standby sstable copier init twice", K(ret));
  } else if (!src.is_valid() || OB_ISNULL(bandwidth_throttle) || !config.is_valid()) {
    ret = OB_INVALID_ARGUMENT;
    LOG_WARN("invalid standby sstable copier argument", K(ret), K(src), KP(bandwidth_throttle));
  } else {
    src_ = src;
    bandwidth_throttle_ = bandwidth_throttle;
    config_ = &config;
    is_inited_ = true;
  }
  return ret;
}

int ObStandbySSTableCopier::prepare_replay_base(
    share::SCN &restore_checkpoint_scn,
    palf::PalfBaseInfo &palf_base_info,
    share::SCN &source_end_scn)
{
  int ret = OB_SUCCESS;
  static const int64_t RPC_TIMEOUT_US = 60L * 1000L * 1000L;
  standby::ObFetchStandbyPalfBaseInfoArg arg;
  standby::ObFetchStandbyPalfBaseInfoResult result;
  standby::ObStandbyGrpcClient client;
  restore_checkpoint_scn.reset();
  palf_base_info.reset();
  source_end_scn.reset();

  if (!is_inited_) {
    ret = OB_NOT_INIT;
  } else if (replay_base_prepared_) {
    ret = OB_INIT_TWICE;
  } else if (OB_FAIL(init_helper_(ls_view_helper_))) {
    LOG_WARN("failed to init ls view helper", K(ret), K_(src));
  } else if (OB_FAIL(ls_view_helper_.check_restore_precondition())) {
    LOG_WARN("standby restore precondition check failed", K(ret), K_(src));
  } else if (OB_FAIL(ls_view_helper_.init_for_ls_view())) {
    LOG_WARN("failed to initialize source ls view", K(ret), K_(src));
  } else if (OB_FAIL(ls_view_helper_.fetch_ls_meta(
                 source_ls_meta_, physical_checkpoint_scn_))) {
    LOG_WARN("failed to fetch source ls meta", K(ret), K_(src));
  } else if (FALSE_IT(restore_checkpoint_scn = physical_checkpoint_scn_)) {
  } else if (!restore_checkpoint_scn.is_valid()) {
    ret = OB_ERR_UNEXPECTED;
    LOG_WARN("source returned invalid restore checkpoint", K(ret), K(source_ls_meta_));
  } else if (FALSE_IT(arg.replay_start_scn_ = restore_checkpoint_scn)) {
  } else if (OB_FAIL(client.init(src_, RPC_TIMEOUT_US, config_->rpc_tls_enabled_))) {
    LOG_WARN("failed to init grpc client for replay base", K(ret), K_(src));
  } else if (OB_FAIL(client.fetch_standby_palf_base_info(arg, result))) {
    LOG_WARN("failed to fetch source palf replay base", K(ret), K_(src), K(arg));
  } else if (!result.is_valid()) {
    ret = OB_ERR_UNEXPECTED;
    LOG_WARN("source returned invalid palf replay base", K(ret), K_(src), K(arg), K(result));
  } else {
    palf_base_info = result.palf_base_info_;
    source_end_scn = result.source_end_scn_;
    replay_base_prepared_ = true;
    LOG_INFO("prepared standby physical replay base",
        K_(src), K(restore_checkpoint_scn), K(palf_base_info), K(result.source_end_scn_));
  }
  return ret;
}

int ObStandbySSTableCopier::copy(share::SCN &restore_checkpoint_scn)
{
  int ret = OB_SUCCESS;
  ObLS *ls = nullptr;
  ObArray<ObTabletID> tablet_id_array;
  ObStandbyRestoreTableInfoMgr table_info_mgr;
  restore_checkpoint_scn.set_min();

  if (!is_inited_ || !replay_base_prepared_) {
    ret = OB_NOT_INIT;
    LOG_WARN("standby sstable copier replay base is not prepared", K(ret), K_(is_inited), K_(replay_base_prepared));
  } else if (OB_FAIL(table_info_mgr.init())) {
    LOG_WARN("failed to init ha table info mgr", K(ret));
  } else if (OB_FAIL(get_sys_ls_(ls))) {
    LOG_WARN("failed to get sys ls", K(ret));
  } else if (OB_FAIL(fetch_ls_view_and_create_tablets_(ls, tablet_id_array, restore_checkpoint_scn))) {
    LOG_ERROR("failed to fetch ls view and create tablets", K(ret), K_(src));
  } else if (tablet_id_array.empty()) {
    ret = OB_ERR_UNEXPECTED;
    LOG_ERROR("standby source has no tablet in ls view", K(ret), K_(src));
  } else if (OB_FAIL(build_table_info_(ls, tablet_id_array, table_info_mgr))) {
    LOG_WARN("failed to build tablet sstable info", K(ret), K_(src), K(tablet_id_array));
  } else if (OB_FAIL(copy_all_tablets_(ls, tablet_id_array, table_info_mgr))) {
    LOG_WARN("failed to copy tablets", K(ret), K_(src), K(tablet_id_array));
  } else if (OB_FAIL(finish_all_tablets_restore_(ls, tablet_id_array))) {
    LOG_WARN("failed to finish standby tablet restore", K(ret), K_(src), K(tablet_id_array));
  } else if (OB_FAIL(finish_ls_restore_(ls, restore_checkpoint_scn))) {
    LOG_WARN("failed to finish standby ls restore", K(ret), K_(src), K(restore_checkpoint_scn));
  } else {
    LOG_INFO("standby sstable baseline copy finished", K_(src), K(restore_checkpoint_scn),
        "tablet_count", tablet_id_array.count());
  }
  return ret;
}

int ObStandbySSTableCopier::init_helper_(ObStandbyRestoreHelper &helper) const
{
  int ret = OB_SUCCESS;
  ObTaskId task_id;
  task_id.init(config_->self_addr_);
  if (!is_inited_) {
    ret = OB_NOT_INIT;
    LOG_WARN("standby sstable copier not init", K(ret));
  } else if (OB_FAIL(helper.init(src_, task_id, bandwidth_throttle_, *config_))) {
    LOG_WARN("failed to init standby restore helper", K(ret), K_(src), K(task_id));
  }
  return ret;
}

int ObStandbySSTableCopier::get_sys_ls_(ObLS *&ls)
{
  int ret = OB_SUCCESS;
  ls = nullptr;
  ObLSService *ls_service = share::server_service<ObLSService>();
  const ObLSID ls_id(ObLSID::SYS_LS_ID);
  if (OB_ISNULL(ls_service)) {
    ret = OB_ERR_UNEXPECTED;
    LOG_WARN("ls service is null", K(ret));
  } else if (OB_FAIL(ls_service->get_ls(ls))) {
    LOG_WARN("failed to get sys ls", K(ret), K(ls_id));
  } else if (OB_ISNULL(ls)) {
    ret = OB_ERR_UNEXPECTED;
    LOG_WARN("sys ls is null", K(ret), K(ls_id));
  }
  return ret;
}

int ObStandbySSTableCopier::fetch_ls_view_and_create_tablets_(
    ObLS *&ls,
    common::ObIArray<common::ObTabletID> &tablet_id_array,
    share::SCN &restore_checkpoint_scn)
{
  int ret = OB_SUCCESS;
  ObLSService *ls_service = share::server_service<ObLSService>();
  int64_t tablet_count = 0;
  restore_checkpoint_scn.set_min();

  if (OB_ISNULL(ls) || OB_ISNULL(ls_service)) {
    ret = OB_INVALID_ARGUMENT;
    LOG_WARN("invalid argument", K(ret), KP(ls), KP(ls_service));
  } else if (!replay_base_prepared_ || !source_ls_meta_.is_valid()) {
    ret = OB_NOT_INIT;
    LOG_WARN("source ls metadata is not prepared", K(ret), K_(replay_base_prepared), K_(source_ls_meta));
  } else if (OB_FAIL(ls_service->update_ls_meta_for_physical_restore(source_ls_meta_))) {
    LOG_ERROR("failed to update local ls restore meta", K(ret), K_(source_ls_meta));
  } else if (FALSE_IT(restore_checkpoint_scn = physical_checkpoint_scn_)) {
  } else if (OB_FAIL(get_sys_ls_(ls))) {
    LOG_ERROR("failed to reload sys ls after restore update", K(ret), K_(source_ls_meta));
  } else {
    while (OB_SUCC(ret)) {
      obcall::ObCopyTabletInfo tablet_info;
      if (OB_FAIL(ls_view_helper_.fetch_next_tablet_info(tablet_info))) {
        if (OB_ITER_END == ret) {
          ret = OB_SUCCESS;
          break;
        } else {
          LOG_ERROR("failed to fetch next tablet info", K(ret), K_(src), K(tablet_count));
        }
      } else if (OB_FAIL(create_or_update_tablet_(ls, tablet_info))) {
        LOG_ERROR("failed to create or update tablet", K(ret), K(tablet_count), K(tablet_info));
      } else if (OB_FAIL(tablet_id_array.push_back(tablet_info.tablet_id_))) {
        LOG_ERROR("failed to push tablet id", K(ret), K(tablet_count), K(tablet_info));
      } else {
        ++tablet_count;
        if (0 == tablet_count % 100) {
          FLOG_INFO("standby baseline ls view consumed", K(tablet_count), K(tablet_info.tablet_id_));
        }
      }
    }
    if (OB_SUCC(ret)) {
      FLOG_INFO("standby baseline ls view finished", K(tablet_count));
    }
  }
  return ret;
}

int ObStandbySSTableCopier::create_or_update_tablet_(
    ObLS *ls,
    obcall::ObCopyTabletInfo &tablet_info)
{
  int ret = OB_SUCCESS;
  if (OB_ISNULL(ls) || !tablet_info.is_valid()) {
    ret = OB_INVALID_ARGUMENT;
    LOG_ERROR("invalid tablet create argument", K(ret), KP(ls), K(tablet_info));
  } else if (ObCopyTabletStatus::TABLET_NOT_EXIST == tablet_info.status_) {
    common::ObArray<common::ObTabletID> tablet_ids;
    if (tablet_info.tablet_id_.is_ls_inner_tablet()) {
      ret = OB_TABLET_NOT_EXIST;
      LOG_ERROR("src ls inner tablet is not exist", K(ret), K(tablet_info));
    } else if (OB_FAIL(tablet_ids.push_back(tablet_info.tablet_id_))) {
      LOG_WARN("failed to collect deleted source tablet", K(ret), K(tablet_info));
    } else if (OB_FAIL(ls->remove_tablets(tablet_ids))) {
      LOG_WARN("failed to remove tablet deleted on source", K(ret), K(tablet_info));
    }
  } else if (!tablet_info.param_.is_empty_shell()
             && (!tablet_info.param_.tablet_meta_.local_status_.is_restore_status_full()
                 || !tablet_info.param_.tablet_meta_.local_status_.is_data_status_complete())) {
    ret = OB_ERR_UNEXPECTED;
    LOG_ERROR("source tablet is not fully restored", K(ret), K(tablet_info));
  } else if (!tablet_info.param_.is_empty_shell()
             && OB_FAIL(tablet_info.param_.tablet_meta_.local_status_.set_restore_status(
                 ObTabletRestoreStatus::EMPTY))) {
    LOG_ERROR("failed to mark local tablet restore empty", K(ret), K(tablet_info));
  } else if (OB_FAIL(ObTabletCreateMdsHelper::check_create_new_tablets(
                 1LL, ObTabletCreateThrottlingLevel::SOFT))) {
    LOG_ERROR("failed to check create tablet throttling", K(ret), K(tablet_info));
  } else if (OB_FAIL(ls->get_tablet_svr()->replace_tablet_for_physical_restore(
                 tablet_info.param_.tablet_meta_, tablet_info.param_.storage_schema_))) {
    LOG_ERROR("failed to create tablet for physical restore", K(ret), K(tablet_info));
  } else {
    LOG_INFO("created standby baseline tablet", K(tablet_info.tablet_id_));
  }
  return ret;
}

int ObStandbySSTableCopier::build_table_info_(
    ObLS *ls,
    const common::ObIArray<common::ObTabletID> &tablet_id_array,
    ObStandbyRestoreTableInfoMgr &table_info_mgr)
{
  int ret = OB_SUCCESS;
  ObArray<ObTabletHandle> tablet_handle_array;
  ObStandbyRestoreHelper helper;
  if (OB_ISNULL(ls) || tablet_id_array.empty()) {
    ret = OB_INVALID_ARGUMENT;
    LOG_WARN("invalid argument", K(ret), KP(ls), K(tablet_id_array));
  } else {
    for (int64_t i = 0; OB_SUCC(ret) && i < tablet_id_array.count(); ++i) {
      ObTabletHandle tablet_handle;
      if (OB_FAIL(ls->get_tablet(tablet_id_array.at(i), tablet_handle))) {
        if (OB_TABLET_NOT_EXIST == ret) {
          LOG_INFO("local tablet not exist, skip sstable info fetch", K(ret), K(tablet_id_array.at(i)));
          ret = OB_SUCCESS;
        } else {
          LOG_WARN("failed to get local tablet", K(ret), K(tablet_id_array.at(i)));
        }
      } else if (OB_FAIL(tablet_handle_array.push_back(tablet_handle))) {
        LOG_WARN("failed to push tablet handle", K(ret), K(tablet_id_array.at(i)));
      }
    }
  }

  if (OB_SUCC(ret) && tablet_handle_array.empty()) {
    ret = OB_ERR_UNEXPECTED;
    LOG_WARN("no local tablet can be used to fetch sstable info", K(ret), K(tablet_id_array));
  } else if (OB_SUCC(ret) && OB_FAIL(init_helper_(helper))) {
    LOG_WARN("failed to init helper", K(ret));
  } else if (OB_SUCC(ret) && OB_FAIL(helper.init_for_build_tablets_sstable_info(tablet_handle_array))) {
    LOG_WARN("failed to init helper for tablet sstable info", K(ret));
  } else {
    while (OB_SUCC(ret)) {
      obcall::ObCopyTabletSSTableHeader header;
      if (OB_FAIL(helper.fetch_next_tablet_sstable_header(header))) {
        if (OB_ITER_END == ret) {
          ret = OB_SUCCESS;
          break;
        } else {
          LOG_WARN("failed to fetch tablet sstable header", K(ret));
        }
      } else if (ObCopyTabletStatus::TABLET_NOT_EXIST == header.status_
                 && header.tablet_id_.is_ls_inner_tablet()) {
        ret = OB_ERR_UNEXPECTED;
        LOG_WARN("ls inner tablet should exist on source", K(ret), K(header));
      } else if (OB_FAIL(table_info_mgr.init_tablet_info(header))) {
        LOG_WARN("failed to init tablet table info", K(ret), K(header));
      } else {
        for (int64_t i = 0; OB_SUCC(ret) && i < header.sstable_count_; ++i) {
          obcall::ObCopyTabletSSTableInfo sstable_info;
          if (OB_FAIL(helper.fetch_next_sstable_meta(sstable_info))) {
            LOG_WARN("failed to fetch sstable meta", K(ret), K(header));
          } else if (!sstable_info.is_valid()) {
            ret = OB_INVALID_ARGUMENT;
            LOG_WARN("invalid sstable info", K(ret), K(sstable_info));
          } else if (sstable_info.table_key_.is_memtable()) {
            ret = OB_ERR_UNEXPECTED;
            LOG_WARN("table should not be memtable", K(ret), K(sstable_info));
          } else if (OB_FAIL(table_info_mgr.add_table_info(sstable_info.tablet_id_, sstable_info))) {
            LOG_WARN("failed to add sstable info", K(ret), K(sstable_info));
          }
        }
      }
    }
  }
  return ret;
}

int ObStandbySSTableCopier::copy_all_tablets_(
    ObLS *ls,
    const common::ObIArray<common::ObTabletID> &tablet_id_array,
    ObStandbyRestoreTableInfoMgr &table_info_mgr)
{
  int ret = OB_SUCCESS;
  if (OB_ISNULL(ls)) {
    ret = OB_INVALID_ARGUMENT;
    LOG_WARN("ls is null", K(ret));
  } else {
    for (int64_t i = 0; OB_SUCC(ret) && i < tablet_id_array.count(); ++i) {
      if (OB_FAIL(copy_tablet_(ls, tablet_id_array.at(i), table_info_mgr))) {
        LOG_WARN("failed to copy standby tablet baseline", K(ret), K(tablet_id_array.at(i)));
      }
    }
  }
  return ret;
}

int ObStandbySSTableCopier::finish_all_tablets_restore_(
    ObLS *ls,
    const common::ObIArray<common::ObTabletID> &tablet_id_array)
{
  int ret = OB_SUCCESS;
  if (OB_ISNULL(ls) || tablet_id_array.empty()) {
    ret = OB_INVALID_ARGUMENT;
    LOG_WARN("invalid finish all tablets restore argument", K(ret), KP(ls), K(tablet_id_array));
  } else {
    for (int64_t i = 0; OB_SUCC(ret) && i < tablet_id_array.count(); ++i) {
      if (OB_FAIL(finish_tablet_restore_(ls, tablet_id_array.at(i)))) {
        LOG_WARN("failed to finish standby tablet restore", K(ret), K(tablet_id_array.at(i)));
      }
    }
  }
  return ret;
}

int ObStandbySSTableCopier::finish_tablet_restore_(
    ObLS *ls,
    const common::ObTabletID &tablet_id)
{
  int ret = OB_SUCCESS;
  ObTabletHandle tablet_handle;
  ObTablet *tablet = nullptr;
  const ObTabletRestoreStatus::STATUS restore_status = ObTabletRestoreStatus::FULL;
  if (OB_ISNULL(ls) || !tablet_id.is_valid()) {
    ret = OB_INVALID_ARGUMENT;
    LOG_WARN("invalid finish tablet restore argument", K(ret), KP(ls), K(tablet_id));
  } else if (OB_FAIL(ls->get_tablet(tablet_id, tablet_handle))) {
    if (OB_TABLET_NOT_EXIST == ret) {
      LOG_INFO("standby tablet not exist or is empty shell, skip finish restore", K(ret), K(tablet_id));
      ret = OB_SUCCESS;
    } else {
      LOG_WARN("failed to get standby tablet", K(ret), K(tablet_id));
    }
  } else if (OB_ISNULL(tablet = tablet_handle.get_obj())) {
    ret = OB_ERR_UNEXPECTED;
    LOG_WARN("standby tablet is null", K(ret), K(tablet_id));
  } else if (tablet->get_tablet_meta().local_status_.is_restore_status_full()) {
    LOG_INFO("standby tablet restore already complete", K(tablet_id));
  } else if (OB_FAIL(ls->update_tablet_restore_status(tablet_id, restore_status))) {
    LOG_WARN("failed to update standby tablet restore status", K(ret), K(tablet_id), K(restore_status));
  } else {
    LOG_INFO("standby tablet restore marked complete", K(tablet_id), K(restore_status));
  }
  return ret;
}

int ObStandbySSTableCopier::finish_ls_restore_(
    ObLS *ls,
    const share::SCN &restore_checkpoint_scn)
{
  int ret = OB_SUCCESS;
  if (OB_ISNULL(ls) || !restore_checkpoint_scn.is_valid()) {
    ret = OB_INVALID_ARGUMENT;
    LOG_WARN("invalid finish ls restore argument", K(ret), KP(ls), K(restore_checkpoint_scn));
  } else {
    ObLSLockGuard lock_ls(ls);
    if (OB_FAIL(ls->set_restore_status(
                   ObRestoreStatus(ObRestoreStatus::Status::NONE)))) {
      LOG_WARN("failed to finish standby ls restore status", K(ret), K(restore_checkpoint_scn));
    } else if (OB_FAIL(ls->online_in_replay_mode_without_lock())) {
      LOG_WARN("failed to online standby restored ls", K(ret), K(restore_checkpoint_scn));
    } else {
      LOG_INFO("standby ls restore finished", K(restore_checkpoint_scn));
    }
  }
  return ret;
}

int ObStandbySSTableCopier::copy_tablet_(
    ObLS *ls,
    const common::ObTabletID &tablet_id,
    ObStandbyRestoreTableInfoMgr &table_info_mgr)
{
  int ret = OB_SUCCESS;
  bool tablet_info_exist = false;
  ObArray<ObITable::TableKey> table_keys;
  const ObMigrationTabletParam *src_tablet_meta = nullptr;
  CopyTabletCtx copy_tablet_ctx;
  ObTaskId copy_id;
  ObTabletCopyFinishTask tablet_finish_task;
  ObTabletCopyFinishTaskParam tablet_param;
  ObStandbyRestoreHelper macro_helper;
  ObStandbyRestoreCopySSTableParam copy_sstable_param;
  ObStandbyRestoreCopySSTableInfoMgr copy_sstable_info_mgr;
  copy_id.init(config_->self_addr_);
  copy_sstable_param.helper_ = &macro_helper;

  if (OB_ISNULL(ls) || !tablet_id.is_valid()) {
    ret = OB_INVALID_ARGUMENT;
    LOG_WARN("invalid argument", K(ret), KP(ls), K(tablet_id));
  } else if (OB_FAIL(table_info_mgr.check_tablet_table_info_exist(tablet_id, tablet_info_exist))) {
    LOG_WARN("failed to check tablet info exist", K(ret), K(tablet_id));
  } else if (!tablet_info_exist) {
    LOG_INFO("tablet has no copied sstable info, skip", K(tablet_id));
  } else if (OB_FAIL(table_info_mgr.get_tablet_meta(tablet_id, src_tablet_meta))) {
    LOG_WARN("failed to get source tablet meta", K(ret), K(tablet_id));
  } else if (OB_FAIL(table_info_mgr.get_table_keys(tablet_id, table_keys))) {
    LOG_WARN("failed to get copy table keys", K(ret), K(tablet_id));
  } else if (!copy_id.is_valid()) {
    ret = OB_ERR_UNEXPECTED;
    LOG_WARN("failed to initialize standby copy id", K(ret), K(copy_id));
  } else if (OB_FAIL(init_helper_(macro_helper))) {
    LOG_WARN("failed to init macro helper", K(ret));
  } else if (OB_FAIL(copy_sstable_param.copy_table_key_array_.assign(table_keys))) {
    LOG_WARN("failed to assign copy table keys", K(ret), K(tablet_id), K(table_keys));
  } else if (OB_FAIL(copy_sstable_info_mgr.init(copy_sstable_param))) {
    LOG_WARN("failed to init copy sstable info mgr", K(ret), K(tablet_id), K(table_keys));
  } else if (OB_FAIL(copy_sstable_info_mgr.check_src_tablet_exist(tablet_info_exist))) {
    LOG_WARN("failed to check source tablet exist", K(ret), K(tablet_id));
  } else if (!tablet_info_exist) {
    LOG_INFO("source tablet does not exist, skip copy tablet", K(tablet_id));
  } else {
    tablet_param.ls_ = ls;
    tablet_param.tablet_id_ = tablet_id;
    tablet_param.restore_action_ = ObTabletRestoreAction::RESTORE_NONE;
    tablet_param.is_leader_restore_ = false;
    tablet_param.src_tablet_meta_ = src_tablet_meta;
    tablet_param.copy_tablet_ctx_ = &copy_tablet_ctx;
    tablet_param.is_only_replace_major_ = false;
    tablet_param.config_ = config_;
    if (OB_FAIL(tablet_finish_task.init(tablet_param))) {
      LOG_WARN("failed to init tablet finish task", K(ret), K(tablet_param));
    }
  }

  for (int64_t i = 0; OB_SUCC(ret) && i < table_keys.count(); ++i) {
    const ObMigrationSSTableParam *sstable_param = nullptr;
    ObCopySSTableMacroRangeInfo macro_range_info;
    if (OB_FAIL(table_info_mgr.get_table_info(tablet_id, table_keys.at(i), sstable_param))) {
      LOG_WARN("failed to get sstable param", K(ret), K(tablet_id), K(table_keys.at(i)));
    } else if (OB_FAIL(copy_sstable_info_mgr.get_copy_sstable_maro_range_info(
                   table_keys.at(i), macro_range_info))) {
      LOG_WARN("failed to get macro range info", K(ret), K(tablet_id), K(table_keys.at(i)));
    } else if (OB_FAIL(copy_sstable_(ls, tablet_id, sstable_param, macro_range_info,
                   macro_helper, copy_tablet_ctx, tablet_finish_task,
                   copy_id))) {
      LOG_WARN("failed to copy sstable", K(ret), K(tablet_id), K(table_keys.at(i)));
    }
  }

  if (OB_SUCC(ret) && tablet_info_exist) {
    if (OB_FAIL(tablet_finish_task.process())) {
      LOG_WARN("failed to finish tablet copy", K(ret), K(tablet_id));
    } else {
      LOG_INFO("standby tablet baseline copy finished", K(tablet_id), "sstable_count", table_keys.count());
    }
  }
  return ret;
}

int ObStandbySSTableCopier::copy_sstable_(
    ObLS *ls,
    const common::ObTabletID &tablet_id,
    const blocksstable::ObMigrationSSTableParam *sstable_param,
    const ObCopySSTableMacroRangeInfo &macro_range_info,
    ObStandbyRestoreHelper &helper,
    CopyTabletCtx &copy_tablet_ctx,
    ObTabletCopyFinishTask &tablet_finish_task,
    const ObTaskId &copy_id)
{
  int ret = OB_SUCCESS;
  ObSSTableCopyFinishTask sstable_finish_task;
  ObPhysicalCopyTaskInitParam init_param;

  if (OB_ISNULL(ls) || OB_ISNULL(sstable_param) || !macro_range_info.is_valid()) {
    ret = OB_INVALID_ARGUMENT;
    LOG_WARN("invalid argument", K(ret), KP(ls), K(tablet_id), KP(sstable_param), K(macro_range_info));
  } else {
    init_param.tenant_id_ = OB_SERVER_RUNTIME_ID;
    init_param.ls_id_ = ObLSID(ObLSID::SYS_LS_ID);
    init_param.tablet_id_ = tablet_id;
    init_param.sstable_param_ = sstable_param;
    init_param.tablet_copy_finish_task_ = &tablet_finish_task;
    init_param.ls_ = ls;
    init_param.helper_ = &helper;
    init_param.copy_id_ = copy_id;
    init_param.extra_info_ = &copy_tablet_ctx.extra_info_;
    if (OB_FAIL(init_param.sstable_macro_range_info_.assign(macro_range_info))) {
      LOG_WARN("failed to assign macro range info", K(ret), K(macro_range_info));
    } else {
      if (OB_FAIL(sstable_finish_task.init(init_param))) {
        LOG_WARN("failed to init sstable finish task", K(ret), K(macro_range_info), KPC(sstable_param));
      }
    }
  }

  while (OB_SUCC(ret)) {
    ObPhysicalCopyTask physical_task;
    if (OB_FAIL(physical_task.init(sstable_finish_task.get_copy_ctx(), &sstable_finish_task))) {
      if (OB_ITER_END == ret) {
        ret = OB_SUCCESS;
        break;
      } else {
        LOG_WARN("failed to init physical copy task", K(ret), K(macro_range_info), KPC(sstable_param));
      }
    } else if (OB_FAIL(physical_task.process())) {
      LOG_WARN("failed to process physical copy task", K(ret), K(macro_range_info), KPC(sstable_param));
    }
  }

  if (OB_SUCC(ret) && OB_FAIL(sstable_finish_task.process())) {
    LOG_WARN("failed to finish sstable copy", K(ret), K(macro_range_info), KPC(sstable_param));
  }
  return ret;
}

} // namespace storage
} // namespace oceanbase
