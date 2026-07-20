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

#include "lib/stat/ob_diagnostic_info_guard.h"
#include "ob_ddl_replay_executor.h"
#include "share/rc/ob_module_provider.h"
#include "storage/compaction/ob_schedule_dag_func.h"
#include "storage/tx_storage/ob_ls_service.h"
#include "storage/tx_storage/ob_tenant_freezer.h"
#include "storage/ddl/ob_direct_insert_sstable_ctx_new.h"
#include "storage/ddl/ob_ddl_merge_task_utils.h"
#include "storage/ddl/ob_ddl_merge_schedule.h"
#include "storage/ddl/ob_tablet_fork_task.h"
using namespace oceanbase::common;
using namespace oceanbase::lib;
using namespace oceanbase::blocksstable;
using namespace oceanbase::storage;
using namespace oceanbase::share;
using namespace oceanbase::transaction;

ERRSIM_POINT_DEF(EN_REPLAY_REDO_DDL_LOG_WAIT);

ObDDLReplayExecutor::ObDDLReplayExecutor()
  : logservice::ObTabletReplayExecutor(), ls_(nullptr), scn_()
{}

int ObDDLReplayExecutor::check_need_replay_ddl_log_(
    const ObLS *ls,
    const ObTabletHandle &tablet_handle,
    const share::SCN &ddl_start_scn,
    const share::SCN &scn,
    bool &need_replay)
{
  int ret = OB_SUCCESS;
  need_replay = true;
  ObTablet *tablet = nullptr;
  if (OB_UNLIKELY(nullptr == ls || !tablet_handle.is_valid() || (!ObDDLUtil::use_idempotent_mode() && !ddl_start_scn.is_valid_and_not_min()) || !scn.is_valid_and_not_min())) {
    ret = OB_INVALID_ARGUMENT;
    LOG_WARN("not init", K(ret), KP(ls), K(tablet_handle), K(ddl_start_scn), K(scn));
  } else if (OB_FAIL(check_need_replay_(ls, tablet_handle, need_replay))) {
    LOG_WARN("fail to check need replay", K(ret), KP(ls), K(tablet_handle));
  } else if (!need_replay) {
    // do nothing
  } else if (OB_ISNULL(tablet = tablet_handle.get_obj())) {
    ret = OB_ERR_UNEXPECTED;
    LOG_WARN("tablet is null", K(ret), K(tablet_handle));
  } else if (scn <= tablet->get_tablet_meta().ddl_checkpoint_scn_) {
    need_replay = false;
    if (REACH_COUNT_INTERVAL(1000L)) {
      LOG_INFO("no need to replay ddl log, because the log ts is less than the ddl checkpoint ts",
          K(tablet_handle), K(scn), "ddl_checkpoint_ts", tablet->get_tablet_meta().ddl_checkpoint_scn_);
    }
  } else if (!ObDDLUtil::use_idempotent_mode() && (ddl_start_scn < tablet->get_tablet_meta().ddl_start_scn_)) {
    need_replay = false;
    if (REACH_COUNT_INTERVAL(1000L)) {
      LOG_INFO("no need to replay ddl log, because the ddl start log ts is less than the value in ddl kv manager",
          K(tablet_handle), K(ddl_start_scn), "ddl_start_scn_in_tablet", tablet->get_tablet_meta().ddl_start_scn_);
    }
  }
  return ret;
}

int ObDDLReplayExecutor::check_need_replay_(
    const ObLS *ls,
    const ObTabletHandle &tablet_handle,
    bool &need_replay)
{
  int ret = OB_SUCCESS;
  need_replay = true;
  ObTablet *tablet = nullptr;
  if (OB_ISNULL(tablet = tablet_handle.get_obj())) {
    ret = OB_ERR_UNEXPECTED;
    LOG_WARN("tablet is null", K(ret), K(tablet_handle));
  } else if (tablet->is_empty_shell()) {
    need_replay = false;
    if (REACH_COUNT_INTERVAL(1000L)) {
      LOG_INFO("no need to replay ddl log, because this tablet is empty shell",
          K(tablet_handle), "tablet_meta", tablet->get_tablet_meta());
    }
  }

  return ret;
}

int ObDDLReplayExecutor::get_lob_meta_tablet_id(
    const ObTabletHandle &tablet_handle,
    const common::ObTabletID &possible_lob_meta_tablet_id,
    common::ObTabletID &lob_meta_tablet_id)
{
  int ret = OB_SUCCESS;
  if (OB_UNLIKELY(!tablet_handle.is_valid())) {
    ret = OB_INVALID_ARGUMENT;
    LOG_WARN("invalid argument", K(ret), K(tablet_handle));
  } else if (ObDDLClog::COMPATIBLE_LOB_META_TABLET_ID == possible_lob_meta_tablet_id.id()) { // compatible code
    ObTabletBindingMdsUserData ddl_data;
    if (OB_FAIL(tablet_handle.get_obj()->ObITabletMdsInterface::get_ddl_data(share::SCN::max_scn(), ddl_data))) {
      LOG_WARN("failed to get ddl data from tablet", K(ret), K(tablet_handle));
    } else {
      lob_meta_tablet_id = ddl_data.lob_meta_tablet_id_;
    }
  } else {
    lob_meta_tablet_id = possible_lob_meta_tablet_id;
  }
  return ret;
}

// ObDDLStartReplayExecutor
ObDDLStartReplayExecutor::ObDDLStartReplayExecutor()
  : ObDDLReplayExecutor(), log_(nullptr)
{

}

int ObDDLStartReplayExecutor::init(
    ObLS *ls,
    const ObDDLStartLog &log,
    const SCN &scn)
{
  int ret = OB_SUCCESS;
  if (OB_UNLIKELY(is_inited_)) {
    ret = OB_INIT_TWICE;
    LOG_WARN("init twice", K(ret), K_(is_inited));
  } else if (OB_ISNULL(ls)
          || OB_UNLIKELY(!log.is_valid())
          || OB_UNLIKELY(!scn.is_valid())) {
    ret = OB_INVALID_ARGUMENT;
    LOG_WARN("invalid arguments", K(ret), KPC(ls), K(log), K(scn));
  } else {
    ls_ = ls;
    log_ = &log;
    scn_ = scn;
    is_inited_ = true;
  }

  return ret;
}

int ObDDLStartReplayExecutor::do_replay_(ObTabletHandle &tablet_handle)
{
  int ret = OB_SUCCESS;
  ObTabletID lob_meta_tablet_id;
  if (OB_UNLIKELY(!is_inited_)) {
    ret = OB_NOT_INIT;
    LOG_WARN("ObDDLRedoLogReplayer has not been inited", K(ret));
  } else if (OB_UNLIKELY(!log_->is_valid() || !tablet_handle.is_valid())) {
    ret = OB_INVALID_ARGUMENT;
    LOG_WARN("invalid arguments", K(ret), K_(log), K(tablet_handle));
  } else if (OB_FAIL(ObDDLReplayExecutor::get_lob_meta_tablet_id(tablet_handle, log_->get_lob_meta_tablet_id(), lob_meta_tablet_id))) {
    LOG_WARN("get lob meta tablet id failed", K(ret));
  } else if (lob_meta_tablet_id.is_valid()) {
    ObTabletHandle lob_meta_tablet_handle;
    const bool replay_allow_tablet_not_exist = true;
    if (OB_FAIL(ls_->replay_get_tablet_no_check(lob_meta_tablet_id, scn_,
        replay_allow_tablet_not_exist, lob_meta_tablet_handle))) {
      if (OB_OBSOLETE_CLOG_NEED_SKIP == ret) {
        LOG_INFO("clog is already obsolete, should skip replay", K(ret), K(lob_meta_tablet_id), K(scn_));
        ret = OB_SUCCESS;
      } else {
        LOG_WARN("get tablet handle failed", K(ret), K(lob_meta_tablet_id), K(scn_));
      }
    } else if (OB_FAIL(replay_ddl_start(lob_meta_tablet_handle, true/*is_lob_meta_tablet*/))) {
      LOG_WARN("replay ddl start for lob meta tablet failed", K(ret), K(lob_meta_tablet_id), K(scn_));
    }
  }
  if (OB_SUCC(ret)) {
    if (OB_FAIL(replay_ddl_start(tablet_handle, false/*is_lob_meta_tablet*/))) {
      LOG_WARN("replay ddl start for data tablet failed", K(ret));
    }
  }
  return ret;
}

int ObDDLStartReplayExecutor::replay_ddl_start(ObTabletHandle &tablet_handle, const bool is_lob_meta_tablet)
{
  int ret = OB_SUCCESS;
  ObTabletDirectLoadMgrHandle direct_load_mgr_handle;
  ObTenantDirectLoadMgr *tenant_direct_load_mgr = share::g_mp->tenant_direct_load_mgr();
  const int64_t unused_context_id = -1;
  bool need_replay = true;
  ObTabletID tablet_id;
  if (OB_UNLIKELY(!is_inited_)) {
    ret = OB_NOT_INIT;
    LOG_WARN("ObDDLRedoLogReplayer has not been inited", K(ret));
  } else if (OB_UNLIKELY(!log_->is_valid())) {
    ret = OB_INVALID_ARGUMENT;
    LOG_WARN("invalid arguments", K(ret), K_(log));
  } else if (OB_FAIL(check_need_replay_ddl_log_(ls_, tablet_handle, scn_, scn_, need_replay))) {
    if (OB_EAGAIN != ret) {
      LOG_ERROR("fail to check need replay ddl log", K(ret), K(tablet_id), K_(scn));
    }
  } else if (!need_replay) {
    // do nothing
  } else if (OB_UNLIKELY(!tablet_handle.is_valid())) {
    ret = OB_ERR_UNEXPECTED;
    LOG_WARN("need replay but tablet handle is invalid", K(ret), K(need_replay), K(tablet_handle));
  } else if (FALSE_IT(tablet_id = tablet_handle.get_obj()->get_tablet_id())) {
  } else if (OB_ISNULL(tenant_direct_load_mgr)) {
    ret = OB_ERR_UNEXPECTED;
    LOG_WARN("unexpected error", K(ret));
  } else {
    ObTabletDirectLoadInsertParam direct_load_param;
    direct_load_param.is_replay_ = true;
    bool is_major_sstable_exist = false;
    const int64_t snapshot_version = log_->get_table_key().get_snapshot_version();
    direct_load_param.common_param_.tablet_id_ = tablet_id;
    direct_load_param.common_param_.data_format_version_ = log_->get_data_format_version();
    direct_load_param.common_param_.direct_load_type_ = log_->get_direct_load_type();
    direct_load_param.common_param_.read_snapshot_ = snapshot_version;
    ObITable::TableKey table_key;
    if (is_lob_meta_tablet) {
      table_key.table_type_ = ObITable::MAJOR_SSTABLE;
      table_key.tablet_id_ = tablet_id;
      table_key.version_range_.base_version_ = 0;
      table_key.version_range_.snapshot_version_ = snapshot_version;
    } else {
      table_key = log_->get_table_key();
    }

    if (OB_FAIL(tenant_direct_load_mgr->replay_create_tablet_direct_load(tablet_handle.get_obj(), log_->get_execution_id(), direct_load_param))) {
      LOG_WARN("create tablet manager failed", K(ret));
    } else if (OB_FAIL(tenant_direct_load_mgr->get_tablet_mgr_and_check_major(
            tablet_id,
            true/* is_full_direct_load */,
            direct_load_mgr_handle,
            is_major_sstable_exist))) {
      if (OB_ENTRY_NOT_EXIST == ret && is_major_sstable_exist) {
        ret = OB_SUCCESS;
        LOG_INFO("ddl start log is expired, skip", K(ret), KPC(log_), K(scn_));
      } else {
        LOG_WARN("get tablet mgr failed", K(ret), K(tablet_id));
      }
    } else if (OB_FAIL(direct_load_mgr_handle.get_full_obj()->update(
            nullptr/*lob_direct_load_mgr*/, // replay is independent for data and lob meta tablet, force null here
            direct_load_param))) {
      LOG_WARN("update direct load mgr failed", K(ret));
    } else if (OB_FAIL(direct_load_mgr_handle.get_full_obj()->start(*tablet_handle.get_obj(), 
            table_key, scn_, log_->get_data_format_version(), log_->get_execution_id(), SCN::min_scn()/*checkpoint_scn*/))) {
      LOG_WARN("direct load start failed", K(ret));
      if (OB_TASK_EXPIRED != ret) {
        LOG_WARN("start ddl log failed", K(ret), K_(log), K_(scn));
      } else {
        ret = OB_SUCCESS; // ignored expired ddl start log
      }
    } else {
      LOG_INFO("succeed to replay ddl start log", K(ret), KPC_(log), K_(scn));
    }
  }
  FLOG_INFO("[DDL_REPLAY] finish replay ddl start log", K(ret), K(need_replay), K(tablet_id), KPC_(log), K_(scn), "ddl_event_info", ObDDLEventInfo());
  return ret;
}

// ObDDLRedoReplayExecutor
ObDDLRedoReplayExecutor::ObDDLRedoReplayExecutor()
  : ObDDLReplayExecutor(), log_(nullptr)
{

}

int ObDDLRedoReplayExecutor::init(
    ObLS *ls,
    const ObDDLRedoLog &log,
    const SCN &scn)
{
  int ret = OB_SUCCESS;
  if (OB_UNLIKELY(is_inited_)) {
    ret = OB_INIT_TWICE;
    LOG_WARN("init twice", K(ret), K_(is_inited));
  } else if (OB_ISNULL(ls)
          || OB_UNLIKELY(!log.is_valid())
          || OB_UNLIKELY(!scn.is_valid())) {
    ret = OB_INVALID_ARGUMENT;
    LOG_WARN("invalid arguments", K(ret), KPC(ls), K(log), K(scn));
  } else {
    ls_ = ls;
    log_ = &log;
    scn_ = scn;
    is_inited_ = true;
  }

  return ret;
}

int ObDDLRedoReplayExecutor::do_replay_(ObTabletHandle &tablet_handle)
{
  int ret = OB_SUCCESS;
  if (OB_UNLIKELY(!is_inited_)) {
    ret = OB_NOT_INIT;
    LOG_WARN("ObDDLRedoLogExecutor has not been inited", K(ret));
  } else if (OB_UNLIKELY(!tablet_handle.is_valid())) {
    ret = OB_INVALID_ARGUMENT;
    LOG_WARN("invalid arguments", K(ret), K(tablet_handle));
  } else {
    const ObDDLMacroBlockRedoInfo &redo_info = log_->get_redo_info();
    ObMacroBlockWriteInfo write_info;
    ObDDLMacroBlock macro_block;
    bool can_skip = false;
    write_info.buffer_ = redo_info.data_buffer_.ptr();
    write_info.size_= redo_info.data_buffer_.length();
    write_info.io_desc_.set_wait_event(ObWaitEventIds::DB_FILE_COMPACT_WRITE);
    write_info.io_timeout_ms_ = max(DDL_FLUSH_MACRO_BLOCK_TIMEOUT / 1000L, GCONF._data_storage_io_timeout / 1000L);
    macro_block.block_type_ = redo_info.block_type_;
    macro_block.logic_id_ = redo_info.logic_id_;
    macro_block.scn_ = scn_;
    macro_block.ddl_start_scn_ = redo_info.start_scn_;
    macro_block.table_key_ = redo_info.table_key_;
    macro_block.merge_slice_idx_ = redo_info.merge_slice_idx_;

    if (OB_FAIL(filter_redo_log_(redo_info, tablet_handle, can_skip))) {
      LOG_WARN("fail to filter redo log", K(ret), K(redo_info), K_(ls));
    } else if (can_skip) {
    } else {
      if (OB_FAIL(do_full_replay_(tablet_handle, write_info, macro_block))) {
        LOG_WARN("fail to do full replay", K(ret));
      }
    }
    if (OB_SERVER_OUTOF_DISK_SPACE == ret) {
      // force retry
      ret = OB_EAGAIN;
    }
  }

  return ret;
}

int check_idem_block_exist(const ObDDLMacroBlockRedoInfo &redo_info, ObTabletHandle &tablet_handle, bool &need_replay, int64_t &checksum)
{
  int ret = OB_SUCCESS;
  bool is_macro_block_exist = false;
  ObDDLKvMgrHandle ddl_kv_mgr_handle;
  need_replay = true;
  if (!redo_info.is_valid() || !tablet_handle.is_valid()) {
    ret = OB_INVALID_ARGUMENT;
    LOG_WARN("invalid arguments", K(ret), K(redo_info), K(tablet_handle));
  } else if (OB_FAIL(tablet_handle.get_obj()->get_ddl_kv_mgr(ddl_kv_mgr_handle, true /* allow create ddl kv mgr*/))) {
    LOG_WARN("failed to get ddl_kv mgr handle", K(ret), K(tablet_handle));
  } else if (!ddl_kv_mgr_handle.is_valid()) {
    ret = OB_ERR_UNEXPECTED;
    LOG_WARN("ddl kv mgr handle not valid", K(ret));
  } else if (OB_FAIL(ddl_kv_mgr_handle.get_obj()->calc_idem_block_checksum(redo_info.block_type_,
                                                                           redo_info.type_,
                                                                           redo_info.data_buffer_.ptr(),
                                                                           redo_info.data_buffer_.length(),
                                                                           checksum))) {
    LOG_WARN("failed to calc block checksum", K(ret), K(redo_info));
  } else if (OB_FAIL(ddl_kv_mgr_handle.get_obj()->check_idem_block_exist(redo_info.block_type_,
                                                                         redo_info.type_,
                                                                         redo_info.macro_block_id_,
                                                                         redo_info.logic_id_,
                                                                         checksum,
                                                                         redo_info.table_key_.table_type_,
                                                                         is_macro_block_exist))) {
    LOG_WARN("failed to check block exist", K(ret), K(redo_info));
  } else if (is_macro_block_exist ) {
    need_replay = false;
    LOG_INFO("macro block already exist, skip replay the redo", K(redo_info), K(need_replay), K(checksum));
  }
  return ret;
}

int set_idem_block_checksum(ObTabletHandle &tablet_handle, const ObDDLMacroBlockRedoInfo &redo_info, int64_t &checksum)
{
  int ret = OB_SUCCESS;
  ObDDLKvMgrHandle ddl_kv_mgr_handle;
  if (!tablet_handle.is_valid()) {
    ret = OB_INVALID_ARGUMENT;
    LOG_WARN("invalid arguments", K(ret), K(tablet_handle));
  } else if (OB_FAIL(tablet_handle.get_obj()->get_ddl_kv_mgr(ddl_kv_mgr_handle))) {
    LOG_WARN("failed to get ddl_kv mgr handle", K(ret), K(tablet_handle));
  } else if (!ddl_kv_mgr_handle.is_valid()) {
    ret = OB_ERR_UNEXPECTED;
    LOG_WARN("ddl kv mgr handle not valid", K(ret));
  } else if (OB_FAIL(ddl_kv_mgr_handle.get_obj()->set_idem_block_checksum(redo_info.block_type_,
                                                                          redo_info.type_,
                                                                          redo_info.macro_block_id_,
                                                                          redo_info.logic_id_,
                                                                          checksum,
                                                                          redo_info.table_key_.table_type_))) {
    LOG_WARN("failed to set block checksum", K(ret), K(redo_info));
  }
  return ret;
}

int ObDDLRedoReplayExecutor::do_full_replay_(
    ObTabletHandle &tablet_handle,
    blocksstable::ObMacroBlockWriteInfo &write_info, 
    storage::ObDDLMacroBlock &macro_block)
{
  int ret = OB_SUCCESS;
  ObMacroBlockHandle macro_handle;
  bool need_replay = true;
  ObTabletMemberWrapper<ObTabletTableStore> table_store_wrapper;
  int64_t checksum = 0;
  int tmp_ret = OB_SUCCESS;
  ObDDLKvMgrHandle ddl_kv_mgr_handle;
  if (OB_FAIL(check_need_replay_ddl_log_(ls_, tablet_handle, log_->get_redo_info().start_scn_, scn_, need_replay))) {
    if (OB_EAGAIN != ret) {
      LOG_ERROR("fail to check need replay ddl log", K(ret), K_(tablet_id), K_(scn));
    }
  } else if (!need_replay) {
    // do nothing
  } else if (OB_FAIL(tablet_handle.get_obj()->fetch_table_store(table_store_wrapper))) {
    LOG_WARN("fail to fetch table store", K(ret));
  } else if (!table_store_wrapper.get_member()->get_major_sstables().empty()) {
    // major sstable already exist, means ddl commit success
    need_replay = false;
    if (REACH_TIME_INTERVAL(1000L * 1000L)) {
      LOG_INFO("no need to replay ddl log, because the major sstable already exist", K_(tablet_id));
    }
  } else if (OB_TMP_FAIL(check_idem_block_exist(log_->get_redo_info(), tablet_handle, need_replay, checksum))) {
    /* using tmp fail to avoid replay redo log */
    LOG_WARN("faield to check idempotence for full redo", K(ret));
  }

  if (OB_FAIL(ret)) {
  } else if (!need_replay) {
  } else {
    const ObDDLMacroBlockRedoInfo &redo_info = log_->get_redo_info();
    ObStorageObjectOpt opt;
    opt.set_private_object_opt(tablet_handle.get_obj()->get_tablet_id().id());
    ObStorageObjectHandle macro_handle;
    ObStorageObjectWriteInfo write_info;
    write_info.buffer_ = redo_info.data_buffer_.ptr();
    write_info.size_= redo_info.data_buffer_.length();
    write_info.offset_ = 0;
    write_info.io_desc_.set_wait_event(ObWaitEventIds::DB_FILE_COMPACT_WRITE);
    write_info.io_desc_.set_sealed();
    write_info.io_timeout_ms_ = max(DDL_FLUSH_MACRO_BLOCK_TIMEOUT / 1000L, GCONF._data_storage_io_timeout / 1000L);
    

    if (OB_FAIL(ObObjectManager::async_write_object(opt, write_info, macro_handle))) {
      LOG_WARN("fail to async write block", K(ret), K(write_info), K(macro_handle));
    } else if (OB_FAIL(macro_handle.wait())) {
      LOG_WARN("fail to wait macro block io finish", K(ret), K(write_info));
    } else if (OB_FAIL(macro_block.block_handle_.set_block_id(macro_handle.get_macro_id()))) {
      LOG_WARN("set macro block id failed", K(ret), K(macro_handle.get_macro_id()));
    } 

    if (OB_FAIL(ret)) {
    } else if (OB_FAIL(macro_block.set_data_macro_meta(macro_block.block_handle_.get_block_id(),
                                                       redo_info.data_buffer_.ptr(), 
                                                       redo_info.data_buffer_.length(),
                                                       redo_info.block_type_))) {
      LOG_WARN("fail to set data macro meta", K(ret), K(macro_handle.get_macro_id()), 
                                                      KP(redo_info.data_buffer_.ptr()), 
                                                      K(redo_info.data_buffer_.length()),
                                                      K(redo_info.block_type_));
    } else {
      macro_block.block_type_ = redo_info.block_type_;
      macro_block.logic_id_ = redo_info.logic_id_;
      macro_block.scn_ = scn_;
      macro_block.ddl_start_scn_ = redo_info.start_scn_;
      macro_block.table_key_ = redo_info.table_key_;
      const int64_t snapshot_version = redo_info.table_key_.get_snapshot_version();
      const ObITable::TableKey &table_key = redo_info.table_key_;
      bool is_major_sstable_exist = false;
      uint64_t data_format_version = redo_info.data_format_version_;
      ObTabletDirectLoadMgrHandle direct_load_mgr_handle;
      ObTenantDirectLoadMgr *tenant_direct_load_mgr = share::g_mp->tenant_direct_load_mgr();
      if (OB_ISNULL(tenant_direct_load_mgr)) {
        ret = OB_ERR_UNEXPECTED;
        LOG_WARN("unexpected err", K(ret));
      }
      if (OB_SUCC(ret) && need_replay) {
        if (OB_FAIL(ObDDLKVPendingGuard::set_macro_block(tablet_handle.get_obj(), macro_block,
            snapshot_version, data_format_version, direct_load_mgr_handle, ObDirectLoadType::DIRECT_LOAD_DDL))) {
           if (OB_ENTRY_EXIST == ret && is_idem_type(redo_info.type_))  {
            ret = OB_SUCCESS;
            need_replay = false;
            LOG_INFO("macro block already exist, skip replay the redo", K(ret), K(macro_block), K(snapshot_version), K(data_format_version));
          } else if (OB_TASK_EXPIRED == ret) {
            need_replay = false;
            LOG_INFO("task expired, skip replay the redo", K(ret), K(macro_block), K(snapshot_version), K(data_format_version));
            ret = OB_SUCCESS;
          } else {
            LOG_WARN("set macro block into ddl kv failed", K(ret), K(tablet_handle), K(macro_block),
                K(snapshot_version), K(data_format_version));
          }
        }

        if (OB_FAIL(ret)) {
        } else if (OB_TMP_FAIL(set_idem_block_checksum(tablet_handle, redo_info, checksum))) {
          LOG_WARN("failed to set block checksum", K(ret), K(redo_info));
        }
      }
    }
  }
  FLOG_INFO("[DDL_REPLAY] finish replay ddl full redo log", K(ret), K(need_replay), K(checksum), KPC_(log), K(macro_block), "ddl_event_info", ObDDLEventInfo());
  return ret;
}

int ObDDLRedoReplayExecutor::filter_redo_log_(
    const ObDDLMacroBlockRedoInfo &redo_info,
    const ObTabletHandle &tablet_handle,
    bool &can_skip)
{
  int ret = OB_SUCCESS;
  UNUSED(redo_info);
  UNUSED(tablet_handle);
  can_skip = false;
#ifdef ERRSIM
  if (OB_SUCC(ret)) {
    ret = EN_REPLAY_REDO_DDL_LOG_WAIT;
    if (OB_FAIL(ret)) {
      LOG_INFO("EN_REPLAY_REDO_DDL_LOG_WAIT replay ddl redo failed", K(ret), K(redo_info), K(can_skip));
    }
  }
#endif
  return ret;
}

// ObDDLCommitReplayExecutor
ObDDLCommitReplayExecutor::ObDDLCommitReplayExecutor()
  : ObDDLReplayExecutor(), log_(nullptr)
{
}

int ObDDLCommitReplayExecutor::init(
    ObLS *ls,
    const ObDDLCommitLog &log,
    const SCN &scn)
{
  int ret = OB_SUCCESS;
  if (OB_UNLIKELY(is_inited_)) {
    ret = OB_INIT_TWICE;
    LOG_WARN("init twice", K(ret), K_(is_inited));
  } else if (OB_ISNULL(ls)
          || OB_UNLIKELY(!log.is_valid())
          || OB_UNLIKELY(!scn.is_valid())) {
    ret = OB_INVALID_ARGUMENT;
    LOG_WARN("invalid arguments", K(ret), KPC(ls), K(log), K(scn));
  } else {
    ls_ = ls;
    log_ = &log;
    scn_ = scn;
    is_inited_ = true;
  }

  return ret;
}

int ObDDLCommitReplayExecutor::do_replay_(ObTabletHandle &tablet_handle)
{
  int ret = OB_SUCCESS;
  ObTabletID lob_meta_tablet_id;
  if (OB_UNLIKELY(!is_inited_)) {
    ret = OB_NOT_INIT;
    LOG_WARN("ObDDLRedoLogReplayer has not been inited", K(ret));
  } else if (OB_UNLIKELY(!log_->is_valid() || !tablet_handle.is_valid())) {
    ret = OB_INVALID_ARGUMENT;
    LOG_WARN("invalid arguments", K(ret), K_(log), K(tablet_handle));
  } else if (OB_FAIL(ObDDLReplayExecutor::get_lob_meta_tablet_id(tablet_handle, log_->get_lob_meta_tablet_id(), lob_meta_tablet_id))) {
    LOG_WARN("get lob meta tablet id failed", K(ret));
  } else if (lob_meta_tablet_id.is_valid()) {
    ObTabletHandle lob_meta_tablet_handle;
    const bool replay_allow_tablet_not_exist = true;
    if (OB_FAIL(ls_->replay_get_tablet_no_check(lob_meta_tablet_id, scn_,
        replay_allow_tablet_not_exist, lob_meta_tablet_handle))) {
      if (OB_OBSOLETE_CLOG_NEED_SKIP == ret) {
        LOG_INFO("clog is already obsolete, should skip replay", K(ret), K(lob_meta_tablet_id), K(scn_));
        ret = OB_SUCCESS;
      } else {
        LOG_WARN("get tablet handle failed", K(ret), K(lob_meta_tablet_id), K(scn_));
      }
    } else if (OB_FAIL(replay_ddl_commit(lob_meta_tablet_handle))) {
      LOG_WARN("replay ddl start for lob meta tablet failed", K(ret), K(lob_meta_tablet_id), K(scn_));
    }
  }
  if (OB_SUCC(ret)) {
    if (OB_FAIL(replay_ddl_commit(tablet_handle))) {
      LOG_WARN("replay ddl commit for data tablet failed", K(ret));
    }
  }
  return ret;
}

int ObDDLCommitReplayExecutor::replay_ddl_commit(ObTabletHandle &tablet_handle)
{
  int ret = OB_SUCCESS;
  ObTabletID tablet_id;
  ObTabletFullDirectLoadMgr *data_direct_load_mgr = nullptr;
  ObTabletDirectLoadMgrHandle direct_load_mgr_handle;
  bool need_replay = true;
  bool is_major_sstable_exist = false;

  DEBUG_SYNC(BEFORE_REPLAY_DDL_PREPRARE);
  if (OB_UNLIKELY(!is_inited_)) {
    ret = OB_NOT_INIT;
    LOG_WARN("ObDDLRedoLogReplayer has not been inited", K(ret));
  } else if (OB_UNLIKELY(!log_->is_valid())) {
    ret = OB_INVALID_ARGUMENT;
    LOG_WARN("invalid arguments", K(ret), K_(log));
  } else if (OB_FAIL(check_need_replay_ddl_log_(ls_, tablet_handle, log_->get_start_scn(), scn_, need_replay))) {
    if (OB_EAGAIN != ret) {
      LOG_ERROR("fail to check need replay ddl log", K(ret), K_(scn), K_(log), "tablet", PC(tablet_handle.get_obj()));
    }
  } else if (!need_replay) {
    // do nothing
  } else if (OB_UNLIKELY(!tablet_handle.is_valid())) {
    ret = OB_ERR_UNEXPECTED;
    LOG_WARN("need replay but tablet handle is invalid", K(ret), K(need_replay), K(tablet_handle), K_(log), K_(scn));
  } else if (OB_FALSE_IT(tablet_id = tablet_handle.get_obj()->get_tablet_id())) {
  } else if (OB_ISNULL(share::g_mp->tenant_direct_load_mgr())) {
    ret = OB_ERR_UNEXPECTED;
    LOG_WARN("unexpected error", K(ret));
  } else if (OB_FAIL(share::g_mp->tenant_direct_load_mgr()->get_tablet_mgr_and_check_major(
          tablet_id,
          true/* is_full_direct_load */,
          direct_load_mgr_handle,
          is_major_sstable_exist))) {
    if (OB_ENTRY_NOT_EXIST == ret && is_major_sstable_exist) {
      ret = OB_SUCCESS;
      LOG_INFO("ddl commit log is expired, skip", K(ret), KPC(log_), K(scn_));
    } else {
      LOG_WARN("get tablet mgr failed", K(ret), K(tablet_id));
    }
  } else if (OB_ISNULL(data_direct_load_mgr = direct_load_mgr_handle.get_full_obj())) {
    ret = OB_ERR_UNEXPECTED;
    LOG_WARN("unexpected err", K(ret), K(tablet_id));
  } else if (OB_FAIL(data_direct_load_mgr->commit(*tablet_handle.get_obj(), log_->get_start_scn(), scn_, 0/*unused table_id*/, 0/*unused ddl_task_id*/, true/*is replay*/))) {
    if (OB_TABLET_NOT_EXIST == ret || OB_TASK_EXPIRED == ret) {
      ret = OB_SUCCESS; // exit when tablet not exist or task expired
    } else {
      LOG_WARN("replay ddl commit log failed", K(ret), K_(log), K_(scn));
    }
  } else {
    LOG_INFO("replay ddl commit log success", K(ret), K_(log), K_(scn));
  }
  FLOG_INFO("[DDL_REPLAY] finish replay ddl commit log", K(ret), K(need_replay), K(tablet_id), KPC_(log), K_(scn), "ddl_event_info", ObDDLEventInfo());
  return ret;
}

// ObDDLStartReplayExecutor
ObTabletForkFreezeReplayExecutor::ObTabletForkFreezeReplayExecutor()
  : ObDDLReplayExecutor(), log_(nullptr)
{
}

int ObTabletForkFreezeReplayExecutor::init(
    ObLS *ls,
    const ObTableForkFreezeLog &log,
    const SCN &scn)
{
  int ret = OB_SUCCESS;
  if (OB_UNLIKELY(is_inited_)) {
    ret = OB_INIT_TWICE;
    LOG_WARN("init twice", K(ret), K_(is_inited));
  } else if (OB_ISNULL(ls) || OB_UNLIKELY(!log.is_valid()) || OB_UNLIKELY(!scn.is_valid())) {
    ret = OB_INVALID_ARGUMENT;
    LOG_WARN("invalid arguments", K(ret), KPC(ls), K(log), K(scn));
  } else {
    ls_ = ls;
    log_ = &log;
    scn_ = scn;
    is_inited_ = true;
  }
  return ret;
}

int ObTabletForkFreezeReplayExecutor::do_replay_(ObTabletHandle &handle)
{
  int ret = OB_SUCCESS;
  if (OB_UNLIKELY(!is_inited_)) {
    ret = OB_NOT_INIT;
    LOG_WARN("ObTabletForkFreezeReplayExecutor has not been inited", K(ret));
  } else if (OB_ISNULL(log_)
      || OB_UNLIKELY(!log_->is_valid())
      || OB_UNLIKELY(!handle.is_valid())
      || OB_ISNULL(handle.get_obj())) {
    ret = OB_INVALID_ARGUMENT;
    LOG_WARN("invalid arguments", K(ret), KPC(log_), K(handle));
  } else {
    const ObTabletID &tablet_id = handle.get_obj()->get_tablet_id();
    if (OB_FAIL(ObTabletForkUtil::freeze_tablet(tablet_id))) {
      LOG_WARN("failed to freeze tablet from fork freeze log", K(ret), K(tablet_id), KPC(log_));
    } else {
      LOG_INFO("succeeded to replay table fork freeze log: tablet frozen", K(tablet_id));
    }
  }
  return ret;
}

ObTabletForkStartReplayExecutor::ObTabletForkStartReplayExecutor()
  : ObDDLReplayExecutor(), log_(nullptr)
{
}

int ObTabletForkStartReplayExecutor::init(
    ObLS *ls,
    const ObTableForkStartLog &log,
    const SCN &scn)
{
  int ret = OB_SUCCESS;
  if (OB_UNLIKELY(is_inited_)) {
    ret = OB_INIT_TWICE;
    LOG_WARN("init twice", K(ret), K_(is_inited));
  } else if (OB_ISNULL(ls) || OB_UNLIKELY(!log.is_valid()) || OB_UNLIKELY(!scn.is_valid())) {
    ret = OB_INVALID_ARGUMENT;
    LOG_WARN("invalid arguments", K(ret), KPC(ls), K(log), K(scn));
  } else {
    ls_ = ls;
    log_ = &log;
    scn_ = scn;
    is_inited_ = true;
  }
  return ret;
}

int ObTabletForkStartReplayExecutor::do_replay_(ObTabletHandle &handle)
{
  int ret = OB_SUCCESS;
  if (OB_UNLIKELY(!is_inited_)) {
    ret = OB_NOT_INIT;
    LOG_WARN("ObTabletForkStartReplayExecutor has not been inited", K(ret));
  } else if (OB_ISNULL(log_)
      || OB_UNLIKELY(!log_->is_valid())
      || OB_UNLIKELY(!handle.is_valid())
      || OB_ISNULL(handle.get_obj())) {
    ret = OB_INVALID_ARGUMENT;
    LOG_WARN("invalid arguments", K(ret), KPC(log_), K(handle));
  } else {
    const ObTableForkInfo &fork_info = log_->fork_info_;
    const ObTabletID &src_tablet_id = handle.get_obj()->get_tablet_id();
    ObTabletForkParam fork_param;
    if (OB_FAIL(fork_info.get_tablet_fork_param(src_tablet_id, fork_param))) {
      if (OB_ENTRY_NOT_EXIST == ret) {
        ret = OB_NO_NEED_UPDATE;
        LOG_INFO("fork start replay skip: src tablet not in fork info", K(src_tablet_id), K(fork_info));
      } else {
        LOG_WARN("failed to get tablet fork param", K(ret), K(src_tablet_id), K(fork_info));
      }
    } else if (OB_FAIL(compaction::ObScheduleDagFunc::schedule_tablet_fork_dag(fork_param, false /* is_emergency */))) {
      if (OB_SIZE_OVERFLOW != ret && OB_EAGAIN != ret) {
        LOG_ERROR("failed to schedule tablet fork dag from start log", K(ret), K(fork_param), K(fork_info));
      } else if (OB_EAGAIN == ret) {
        LOG_DEBUG("exists same fork dag, wait the dag to finish", K(ret), K(fork_param));
        ret = OB_SUCCESS;
      }
    } else {
      LOG_INFO("succeeded to replay table fork start log: scheduled one fork dag", K(fork_param));
    }
  }
  return ret;
}

ObTabletForkFinishReplayExecutor::ObTabletForkFinishReplayExecutor()
  : ObDDLReplayExecutor(), log_(nullptr)
{
}

int ObTabletForkFinishReplayExecutor::init(
    ObLS *ls,
    const ObTableForkFinishLog &log,
    const SCN &scn)
{
  int ret = OB_SUCCESS;
  if (OB_UNLIKELY(is_inited_)) {
    ret = OB_INIT_TWICE;
    LOG_WARN("init twice", K(ret), K_(is_inited));
  } else if (OB_ISNULL(ls) || OB_UNLIKELY(!log.is_valid()) || OB_UNLIKELY(!scn.is_valid())) {
    ret = OB_INVALID_ARGUMENT;
    LOG_WARN("invalid arguments", K(ret), KPC(ls), K(log), K(scn));
  } else {
    ls_ = ls;
    log_ = &log;
    scn_ = scn;
    is_inited_ = true;
  }
  return ret;
}

int ObTabletForkFinishReplayExecutor::do_replay_(ObTabletHandle &handle)
{
  int ret = OB_SUCCESS;
  if (OB_UNLIKELY(!is_inited_)) {
    ret = OB_NOT_INIT;
    LOG_WARN("ObTabletForkFinishReplayExecutor has not been inited", K(ret));
  } else if (OB_ISNULL(log_)
      || OB_UNLIKELY(!log_->is_valid())
      || OB_UNLIKELY(!handle.is_valid())
      || OB_ISNULL(handle.get_obj())) {
    ret = OB_INVALID_ARGUMENT;
    LOG_WARN("invalid arguments", K(ret), KPC(log_), K(handle));
  } else {
    const ObTableForkInfo &fork_info = log_->fork_info_;
    const ObTabletID &src_tablet_id = handle.get_obj()->get_tablet_id();
    ObTabletForkParam fork_param;
    if (OB_FAIL(fork_info.get_tablet_fork_param(src_tablet_id, fork_param))) {
      if (OB_ENTRY_NOT_EXIST == ret) {
        ret = OB_NO_NEED_UPDATE;
        LOG_INFO("fork finish replay skip: src tablet not in fork info", K(src_tablet_id), K(fork_info));
      } else {
        LOG_WARN("failed to get tablet fork param", K(ret), K(src_tablet_id), K(fork_info));
      }
    } else if (OB_FAIL(compaction::ObScheduleDagFunc::schedule_tablet_fork_dag(fork_param, false /* is_emergency */))) {
      if (OB_SIZE_OVERFLOW != ret && OB_EAGAIN != ret) {
        LOG_ERROR("failed to try schedule fork dag from finish log", K(ret), K(fork_param), K(fork_info));
      } else if (OB_EAGAIN == ret) {
        ret = OB_SUCCESS;
      }
    } else {
      const ObTabletID &dst_tablet_id = fork_param.dest_tablet_id_;
      bool is_complete = false;
      if (OB_FAIL(storage::ObTabletForkUtil::check_fork_data_complete(dst_tablet_id, is_complete))) {
        LOG_WARN("failed to check fork data complete", K(ret), K(dst_tablet_id), K(fork_param));
      } else if (!is_complete) {
        ret = OB_EAGAIN; // retry replay until fork data complete
        if (REACH_COUNT_INTERVAL(1000L)) {
          LOG_INFO("fork data not complete yet, need retry", K(ret), K(dst_tablet_id), K(fork_param));
        }
      } else {
        LOG_INFO("replay table fork finish log: data complete for one tablet", K(fork_param));
      }
    }
  }
  return ret;
}


// ObSchemaChangeReplayExecutor
ObSchemaChangeReplayExecutor::ObSchemaChangeReplayExecutor()
  : logservice::ObTabletReplayExecutor(), log_(nullptr), scn_()
{

}

int ObSchemaChangeReplayExecutor::init(
    const ObTabletSchemaVersionChangeLog &log,
    const SCN &scn)
{
  int ret = OB_SUCCESS;
  if (OB_UNLIKELY(is_inited_)) {
    ret = OB_INIT_TWICE;
    LOG_WARN("init twice", K(ret), K_(is_inited));
  } else if (OB_UNLIKELY(!log.is_valid())
          || OB_UNLIKELY(!scn.is_valid())) {
    ret = OB_INVALID_ARGUMENT;
    LOG_WARN("invalid arguments", K(log), K(scn), K(ret));
  } else {
    log_ = &log;
    scn_ = scn;
    is_inited_ = true;
  }

  return ret;
}

int ObSchemaChangeReplayExecutor::do_replay_(ObTabletHandle &handle)
{
  int ret = OB_SUCCESS;
  if (OB_FAIL(handle.get_obj()->replay_schema_version_change_log(log_->get_schema_version()))) {
    LOG_WARN("fail to replay schema version change log", K(ret), KPC_(log));
  } else {
    LOG_INFO("replay tablet schema version change log success", KPC_(log), K_(scn));
  }
  return ret;
}
