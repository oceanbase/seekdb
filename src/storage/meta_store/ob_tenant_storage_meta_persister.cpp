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

#include "ob_tenant_storage_meta_persister.h"
#include "storage/meta_store/ob_storage_meta_io_util.h"
#include "storage/ls/ob_ls.h"
#include "storage/tablet/ob_tablet_iterator.h"
#include "observer/omt/ob_tenant.h"
#include "storage/slog/ob_storage_logger.h"
#include "storage/slog/ob_storage_log_replayer.h"
#include "storage/slog_ckpt/ob_tenant_checkpoint_slog_handler.h"

namespace oceanbase
{
using namespace omt;
using namespace blocksstable;
namespace storage
{

int ObTenantStorageMetaPersister::init(
    ObStorageLogger &slogger,
    ObTenantCheckpointSlogHandler &ckpt_slog_handler)
{
  int ret = OB_SUCCESS;

  const int64_t MEM_LIMIT = 512UL << 20;
  lib::ObMemAttr attr("TntMetaPersist");
  const int64_t MAP_BUCKET_CNT = 256;
  lib::ObMemAttr map_attr("PendingFreeMap");

  if (OB_UNLIKELY(is_inited_)) {
    ret = OB_INIT_TWICE;
    LOG_WARN("ObTenantStorageMetaPersister has inited", K(ret));
  } else if (OB_FAIL(allocator_.init(common::OB_MALLOC_NORMAL_BLOCK_SIZE, attr, MEM_LIMIT))) {
    LOG_WARN("fail to init fifo allocator", K(ret));
  } else if (OB_FAIL(pending_free_tablet_arr_map_.create(MAP_BUCKET_CNT, map_attr))) {
    LOG_WARN("fail to create pending_free_tablet_arr_map", K(ret));
  } else {
    
    slogger_ = &slogger;
    ckpt_slog_handler_ = &ckpt_slog_handler;
    is_inited_ = true;
  }
  return ret;
}

void ObTenantStorageMetaPersister::destroy()
{
  int ret = OB_SUCCESS;
  if (IS_INIT) {
    for (PendingFreeTabletArrayMap::iterator iter = pending_free_tablet_arr_map_.begin();
         iter !=  pending_free_tablet_arr_map_.end(); iter++) {
      if (OB_ISNULL(iter->second)) {
        ret = OB_ERR_UNEXPECTED;
        LOG_ERROR("PendingFreeTabletArrayInfo is null", K(ret), K(iter->first));
      } else {
        ob_delete(iter->second);
      }
    }
    pending_free_tablet_arr_map_.destroy();
    slogger_ = nullptr;
    ckpt_slog_handler_ = nullptr;
    allocator_.reset();
    is_inited_ = false;
  }
}

int ObTenantStorageMetaPersister::prepare_create_ls(const ObLSMeta &meta, int64_t &ls_epoch)
{
  int ret = OB_SUCCESS;
  if (IS_NOT_INIT) {
    ret = OB_NOT_INIT;
    LOG_WARN("not init", K(ret));
  } else  {
    ls_epoch = 0;
    if (OB_FAIL(write_prepare_create_ls_slog_(meta))) {
      LOG_WARN("fail to write prepare create ls slog", K(ret), K(meta));
    }
  }
  return ret;
}

int ObTenantStorageMetaPersister::commit_create_ls(
    const share::ObLSID &ls_id, const int64_t ls_epoch)
{
  int ret = OB_SUCCESS;
  if (IS_NOT_INIT) {
    ret = OB_NOT_INIT;
    LOG_WARN("not init", K(ret));
  } else  {
    if (OB_FAIL(write_commit_create_ls_slog_(ls_id))) {
      LOG_WARN("fail to write commit create ls slog", K(ret), K(ls_id));
    }
  }
  return ret;

}
int ObTenantStorageMetaPersister::abort_create_ls(const share::ObLSID &ls_id, const int64_t ls_epoch)
{
  int ret = OB_SUCCESS;
  if (IS_NOT_INIT) {
    ret = OB_NOT_INIT;
    LOG_WARN("not init", K(ret));
  } else  {
    if (OB_FAIL(write_abort_create_ls_slog_(ls_id))) {
      LOG_WARN("fail to write abort create ls slog", K(ret), K(ls_id));
    }
  }
  return ret;
}

int ObTenantStorageMetaPersister::delete_ls(const share::ObLSID &ls_id, const int64_t ls_epoch)
{
  int ret = OB_SUCCESS;
  if (IS_NOT_INIT) {
    ret = OB_NOT_INIT;
    LOG_WARN("not init", K(ret));
  } else  {
    if (OB_FAIL(write_delete_ls_slog_(ls_id))) {
      LOG_WARN("fail to write delete ls slog", K(ret), K(ls_id));
    }
  }
  return ret;
}

int ObTenantStorageMetaPersister::update_ls_meta(const int64_t ls_epoch, const ObLSMeta &ls_meta)
{
  int ret = OB_SUCCESS;
  if (IS_NOT_INIT) {
    ret = OB_NOT_INIT;
    LOG_WARN("not init", K(ret));
  } else  {
    if (OB_FAIL(write_update_ls_meta_slog_(ls_meta))) {
      LOG_WARN("fail to write update ls meta slog", K(ret), K(ls_meta));
    }
  }
  return ret;
}

int ObTenantStorageMetaPersister::update_tenant_preallocated_seqs(
    const ObTenantMonotonicIncSeqs &preallocated_seqs)
{
  int ret = OB_SUCCESS;

  if (IS_NOT_INIT) {
    ret = OB_NOT_INIT;
    LOG_WARN("not init", K(ret));
  } else {
    ret = OB_NOT_SUPPORTED;
    LOG_ERROR("not support for shared-nothing", K(ret));
  }
  return ret;
}

int ObTenantStorageMetaPersister::batch_update_tablet(const ObIArray<ObUpdateTabletLog> &slog_arr)
{
  int ret = OB_SUCCESS;
  if (IS_NOT_INIT) {
    ret = OB_NOT_INIT;
    LOG_WARN("not init", K(ret));
  } else {
    ObSArray<ObStorageLogParam> param_arr;
    param_arr.set_attr(ObMemAttr("BatchUpdateTab"));
    ObStorageLogParam log_param;
    log_param.cmd_ = ObIRedoModule::gen_cmd(
        ObRedoLogMainType::OB_REDO_LOG_TENANT_STORAGE,
        ObRedoLogSubType::OB_REDO_LOG_UPDATE_TABLET);
    if (OB_FAIL(param_arr.reserve(slog_arr.count()))) {
      LOG_WARN("fail to reserve memory for slog param arr", K(ret), K(slog_arr.count()));
    }
    for (int64_t i = 0; OB_SUCC(ret) && i < slog_arr.count(); i++) {
      log_param.data_ =(ObIBaseStorageLogEntry*)(&(slog_arr.at(i)));
      if (OB_FAIL(param_arr.push_back(log_param))) {
        LOG_WARN("fail to push back slog param", K(ret), K(log_param));
      }
    }
    if (OB_FAIL(ret)) {
      // do nothing
    } else if (OB_FAIL(slogger_->write_log(param_arr))) {
      LOG_WARN("fail to batch write slog", K(ret), K(param_arr.count()));
    } else {
      for (int64_t i = 0; OB_SUCC(ret) && i < param_arr.count(); i++) {
        const ObStorageLogParam &log_param = param_arr.at(i);
        const ObUpdateTabletLog *slog = reinterpret_cast<const ObUpdateTabletLog*>(log_param.data_);
        const ObTabletMapKey tablet_key(slog->ls_id_, slog->tablet_id_);
        do {
          if (OB_FAIL(ckpt_slog_handler_->report_slog(tablet_key, log_param.disk_addr_))) {
            if (OB_ALLOCATE_MEMORY_FAILED != ret) {
              LOG_WARN("fail to report slog", K(ret), K(tablet_key), K(log_param));
            } else if (REACH_TIME_INTERVAL(1000 * 1000L)) { // 1s
              LOG_WARN("fail to report slog due to memory limit", K(ret), K(tablet_key), K(log_param));
            }
          }
        } while (OB_ALLOCATE_MEMORY_FAILED == ret);
      }
    }
  }

  return ret;
}

int ObTenantStorageMetaPersister::update_tablet(
    const share::ObLSID &ls_id,
    const int64_t ls_epoch,
    const common::ObTabletID &tablet_id,
    const ObMetaDiskAddr &tablet_addr)
{
  int ret = OB_SUCCESS;
  if (IS_NOT_INIT) {
    ret = OB_NOT_INIT;
    LOG_WARN("not init", K(ret));
  } else if (OB_UNLIKELY(!ls_id.is_valid() || !tablet_id.is_valid() || !tablet_addr.is_valid())) {
    ret = OB_INVALID_ARGUMENT;
    LOG_WARN("invalid arguments", K(ret), K(ls_id), K(tablet_id), K(tablet_addr));
  } else {
    if (OB_FAIL(write_update_tablet_slog_(ls_id, tablet_id, tablet_addr))) {
      LOG_WARN("fail to write update tablet slog", K(ret), K(ls_id), K(tablet_id), K(tablet_addr));
    }
  }
  return ret;
}

int ObTenantStorageMetaPersister::write_active_tablet_array(ObLS *ls)
{
  int ret = OB_SUCCESS;
  if (IS_NOT_INIT) {
    ret = OB_NOT_INIT;
    LOG_WARN("not init", K(ret));
  } else if (OB_ISNULL(ls)) {
    ret = OB_INVALID_ARGUMENT;
    LOG_WARN("invalid arguments", K(ret));
  } else {
    // do nothing for shared-nothing
  }
  return ret;
}

int ObTenantStorageMetaPersister::write_empty_shell_tablet(ObTablet *tablet, ObMetaDiskAddr &tablet_addr)
{
  int ret = OB_SUCCESS;
  if (IS_NOT_INIT) {
    ret = OB_NOT_INIT;
    LOG_WARN("not init", K(ret));
  } else if (OB_UNLIKELY(!tablet->is_empty_shell())) {
    ret = OB_STATE_NOT_MATCH;
    LOG_WARN("the tablet is not empty shell", K(ret), K(tablet));
  } else {
    const ObTabletMapKey tablet_key(tablet->get_tablet_meta().ls_id_, tablet->get_tablet_meta().tablet_id_);
    ObEmptyShellTabletLog slog_entry(tablet->get_tablet_meta().ls_id_,
                                     tablet->get_tablet_meta().tablet_id_,
                                     tablet);
    ObStorageLogParam log_param;
    log_param.cmd_ = ObIRedoModule::gen_cmd(ObRedoLogMainType::OB_REDO_LOG_TENANT_STORAGE,
        ObRedoLogSubType::OB_REDO_LOG_EMPTY_SHELL_TABLET);
    log_param.data_ = &slog_entry;
    if (OB_FAIL(slogger_->write_log(log_param))) {
      LOG_WARN("fail to write slog for empty shell tablet", K(ret), K(log_param));
    } else if (OB_FAIL(ckpt_slog_handler_->report_slog(tablet_key, log_param.disk_addr_))) {
      LOG_WARN("fail to report slog", K(ret), K(tablet_key));
    } else {
      tablet_addr = log_param.disk_addr_;
    }
  }
  return ret;
}

int ObTenantStorageMetaPersister::remove_tablet(
    const share::ObLSID &ls_id, const int64_t ls_epoch,
    const ObTabletHandle &tablet_handle)
{
  int ret = OB_SUCCESS;
  if (IS_NOT_INIT) {
    ret = OB_NOT_INIT;
    LOG_WARN("not init", K(ret));
  } else if (OB_UNLIKELY(!tablet_handle.is_valid())) {
    ret = OB_INVALID_ARGUMENT;
    LOG_WARN("invalid tablet", K(ret), K(tablet_handle));
  } else {
    const common::ObTabletID &tablet_id = tablet_handle.get_obj()->get_tablet_meta().tablet_id_;
    const ObMetaDiskAddr &tablet_addr = tablet_handle.get_obj()->get_tablet_addr();

    if (OB_UNLIKELY(!ls_id.is_valid() || !tablet_id.is_valid() || !tablet_addr.is_valid())) {
      ret = OB_INVALID_ARGUMENT;
      LOG_WARN("invalid arguments", K(ret), K(ls_id), K(tablet_id), K(tablet_addr));
    } else {
      if (OB_FAIL(write_remove_tablet_slog_(ls_id, tablet_id))) {
        LOG_WARN("fail to write remove tablet slog", K(ret), K(ls_id), K(tablet_id));
      }
    }
  }
  return ret;
}

int ObTenantStorageMetaPersister::remove_tablets(
    const share::ObLSID &ls_id, const int64_t ls_epoch,
    const ObIArray<common::ObTabletID> &tablet_ids, const ObIArray<ObMetaDiskAddr> &tablet_addrs)
{
  int ret = OB_SUCCESS;
  if (IS_NOT_INIT) {
    ret = OB_NOT_INIT;
    LOG_WARN("not init", K(ret));
  } else if (OB_UNLIKELY(!ls_id.is_valid())) {
    ret = OB_INVALID_ARGUMENT;
    LOG_WARN("invalid arguments", K(ret), K(ls_id));
  } else {
    if (OB_FAIL(write_remove_tablets_slog_(ls_id, tablet_ids))) {
      LOG_WARN("fail to write remove tablets slog", K(ret), K(ls_id));
    }
  }
  return ret;

}

//=================================== SLOG ==============================================//
int ObTenantStorageMetaPersister::write_prepare_create_ls_slog_(const ObLSMeta &ls_meta)
{
  int ret = OB_SUCCESS;
  ObCreateLSPrepareSlog slog_entry(ls_meta);
  ObStorageLogParam log_param;
  log_param.data_ = &slog_entry;
  log_param.cmd_ = ObIRedoModule::gen_cmd(ObRedoLogMainType::OB_REDO_LOG_TENANT_STORAGE,
                                          ObRedoLogSubType::OB_REDO_LOG_CREATE_LS);
  if (OB_FAIL(slogger_->write_log(log_param))) {
    LOG_WARN("fail to write remove ls slog", K(log_param));
  }
  return ret;
}

int ObTenantStorageMetaPersister::write_commit_create_ls_slog_(const share::ObLSID &ls_id)
{
  int ret = OB_SUCCESS;
  share::ObLSID tmp_ls_id = ls_id;
  ObCreateLSCommitSLog slog_entry(tmp_ls_id);
  ObStorageLogParam log_param;
  log_param.data_ = &slog_entry;
  log_param.cmd_ = ObIRedoModule::gen_cmd(ObRedoLogMainType::OB_REDO_LOG_TENANT_STORAGE,
                                          ObRedoLogSubType::OB_REDO_LOG_CREATE_LS_COMMIT);
  if (OB_FAIL(slogger_->write_log(log_param))) {
    LOG_WARN("fail to write create ls commit slog", K(log_param));
  }
  return ret;
}

int ObTenantStorageMetaPersister::write_abort_create_ls_slog_(const share::ObLSID &ls_id)
{
  int ret = OB_SUCCESS;
  share::ObLSID tmp_ls_id = ls_id;
  ObCreateLSAbortSLog slog_entry(tmp_ls_id);
  ObStorageLogParam log_param;
  log_param.data_ = &slog_entry;
  log_param.cmd_ = ObIRedoModule::gen_cmd(ObRedoLogMainType::OB_REDO_LOG_TENANT_STORAGE,
                                            ObRedoLogSubType::OB_REDO_LOG_CREATE_LS_ABORT);
  if (OB_FAIL(slogger_->write_log(log_param))) {
    LOG_WARN("fail to write create ls abort slog", K(log_param));
  }
  return ret;
}

int ObTenantStorageMetaPersister::write_delete_ls_slog_(const share::ObLSID &ls_id)
{
  int ret = OB_SUCCESS;
  share::ObLSID tmp_ls_id = ls_id;
  ObDeleteLSLog slog_entry(tmp_ls_id);
  ObStorageLogParam log_param;
  log_param.data_ = &slog_entry;
  log_param.cmd_ = ObIRedoModule::gen_cmd(ObRedoLogMainType::OB_REDO_LOG_TENANT_STORAGE,
                                            ObRedoLogSubType::OB_REDO_LOG_DELETE_LS);
  if (OB_FAIL(slogger_->write_log(log_param))) {
    LOG_WARN("fail to write remove ls slog", K(log_param));
  }
  return ret;
}

int ObTenantStorageMetaPersister::write_update_ls_meta_slog_(const ObLSMeta &ls_meta)
{
  int ret = OB_SUCCESS;
  ObLSMetaLog slog_entry(ls_meta);
  ObStorageLogParam log_param;
  log_param.data_ = &slog_entry;
  log_param.cmd_ = ObIRedoModule::gen_cmd(ObRedoLogMainType::OB_REDO_LOG_TENANT_STORAGE,
                                          ObRedoLogSubType::OB_REDO_LOG_UPDATE_LS);
  if (OB_FAIL(slogger_->write_log(log_param))) {
    LOG_WARN("fail to write update ls slog", K(log_param), K(ret));
  }
  return ret;
}

int ObTenantStorageMetaPersister::write_update_tablet_slog_(
    const share::ObLSID &ls_id,
    const common::ObTabletID &tablet_id,
    const ObMetaDiskAddr &disk_addr)
{
  int ret = OB_SUCCESS;
  const ObTabletMapKey tablet_key(ls_id, tablet_id);
  if (OB_FAIL(LOCAL_DEVICE_INSTANCE.fsync_block())) { // make sure that all data or meta written on the macro block is flushed
    LOG_WARN("fail to fsync_block", K(ret));
  } else {
    ObUpdateTabletLog slog_entry(ls_id, tablet_id, disk_addr);
    ObStorageLogParam log_param;
    log_param.cmd_ = ObIRedoModule::gen_cmd(ObRedoLogMainType::OB_REDO_LOG_TENANT_STORAGE,
        ObRedoLogSubType::OB_REDO_LOG_UPDATE_TABLET);
    log_param.data_ = &slog_entry;
    if (OB_FAIL(slogger_->write_log(log_param))) {
      LOG_WARN("fail to write slog for creating tablet", K(ret), K(log_param));
    } else {
      do {
        if (OB_FAIL(ckpt_slog_handler_->report_slog(tablet_key, log_param.disk_addr_))) {
          if (OB_ALLOCATE_MEMORY_FAILED != ret) {
            LOG_WARN("fail to report slog", K(ret), K(tablet_key));
          } else if (REACH_TIME_INTERVAL(1000 * 1000L)) { // 1s
            LOG_WARN("fail to report slog due to memory limit", K(ret), K(tablet_key));
          }
        }
      } while (OB_ALLOCATE_MEMORY_FAILED == ret);
    }
  }
  return ret;
}

int ObTenantStorageMetaPersister::write_remove_tablet_slog_(
    const share::ObLSID &ls_id,
    const common::ObTabletID &tablet_id)
{
  int ret = OB_SUCCESS;
  ObDeleteTabletLog slog_entry(ls_id, tablet_id);
  ObStorageLogParam log_param;
  log_param.cmd_ = ObIRedoModule::gen_cmd(ObRedoLogMainType::OB_REDO_LOG_TENANT_STORAGE,
      ObRedoLogSubType::OB_REDO_LOG_DELETE_TABLET);
  log_param.data_ = &slog_entry;
  if (OB_FAIL(slogger_->write_log(log_param))) {
    LOG_WARN("fail to write remove tablet slog", K(ret), K(log_param));
  }
  return ret;
}

int ObTenantStorageMetaPersister::write_remove_tablets_slog_(
    const ObLSID &ls_id, const common::ObIArray<ObTabletID> &tablet_ids)
{
  int ret = OB_SUCCESS;
  // We can split the tablet_ids array due to following reasons:
  // 1. batch remove tablets doesn't need atomic semantic, they can be written in different log items
  // 2. log item batch header count is int16_t type, we can't over the limit
  const int64_t MAX_ARRAY_SIZE = 32000;
  const int64_t total_cnt = tablet_ids.count();
  ObSEArray<ObTabletID, 16> current_tablet_arr;
  int64_t finish_cnt = 0;
  int64_t cur_cnt = 0;
  while (OB_SUCC(ret) && finish_cnt < total_cnt) {
    current_tablet_arr.reset();
    cur_cnt = MIN(MAX_ARRAY_SIZE, total_cnt - finish_cnt);

    if (OB_FAIL(current_tablet_arr.reserve(cur_cnt))) {
      STORAGE_REDO_LOG(WARN, "reserve array fail", K(ret), K(cur_cnt), K(total_cnt), K(finish_cnt));
    }
    for (int64_t i = finish_cnt; OB_SUCC(ret) && i < finish_cnt + cur_cnt; ++i) {
      if (OB_FAIL(current_tablet_arr.push_back(tablet_ids.at(i)))) {
        STORAGE_REDO_LOG(WARN, "push back tablet id fail", K(ret), K(cur_cnt), K(total_cnt),
            K(finish_cnt), K(i));
      }
    }
    if (OB_FAIL(ret)){
    } else if (OB_FAIL(safe_batch_write_remove_tablets_slog_(ls_id, current_tablet_arr))){
      STORAGE_REDO_LOG(WARN, "inner write log fail", K(ret), K(cur_cnt), K(total_cnt), K(finish_cnt));
    } else {
      finish_cnt += cur_cnt;
    }
  }

  return ret;
}

int ObTenantStorageMetaPersister::safe_batch_write_remove_tablets_slog_(
    const ObLSID &ls_id, const common::ObIArray<ObTabletID> &tablet_ids)
{
  int ret = OB_SUCCESS;
  const int64_t tablet_count = tablet_ids.count();
  const int32_t cmd = ObIRedoModule::gen_cmd(ObRedoLogMainType::OB_REDO_LOG_TENANT_STORAGE,
      ObRedoLogSubType::OB_REDO_LOG_DELETE_TABLET);
  ObSArray<ObDeleteTabletLog> slog_array;
  ObSArray<ObStorageLogParam> param_array;
  const bool need_write = (tablet_count > 0);

  if (!need_write) {
  } else if (OB_FAIL(slog_array.reserve(tablet_count))) {
    LOG_WARN("failed to reserve for slog array", K(ret), K(tablet_count));
  } else if (OB_FAIL(param_array.reserve(tablet_count))) {
    LOG_WARN("failed to reserve for param array", K(ret), K(tablet_count));
  } else {
    for (int64_t i = 0; OB_SUCC(ret) && i < tablet_count; ++i) {
      const ObTabletID &tablet_id = tablet_ids.at(i);
      ObDeleteTabletLog slog_entry(ls_id, tablet_id);
      if (OB_UNLIKELY(!tablet_id.is_valid())) {
        ret = OB_ERR_UNEXPECTED;
        LOG_WARN("tablet id is invalid", K(ret), K(ls_id), K(tablet_id));
      } else if (OB_FAIL(slog_array.push_back(slog_entry))) {
        LOG_WARN("fail to push slog entry into slog array", K(ret), K(slog_entry), K(i));
      }
    }

    for (int64_t i = 0; OB_SUCC(ret) && i < tablet_count; i++) {
      ObStorageLogParam log_param(cmd, &slog_array[i]);
      if (OB_FAIL(param_array.push_back(log_param))) {
        LOG_WARN("fail to push log param into param array", K(ret), K(log_param), K(i));
      }
    }
  }

  if (OB_FAIL(ret)) {
  } else if (!need_write) {
  } else if (OB_FAIL(slogger_->write_log(param_array))) {
    LOG_WARN("fail to write slog for batch deleting tablet", K(ret), K(param_array));
  }

  return ret;
}

int ObTenantStorageMetaPersister::get_items_from_pending_free_tablet_array(
    const ObLSID &ls_id, 
    const int64_t ls_epoch,
    ObIArray<ObPendingFreeTabletItem> &items) 
{
  int ret = OB_SUCCESS;
  items.reuse();
  PendingFreeTabletArrayKey key(ls_id, ls_epoch);
  PendingFreeTabletArrayInfo *array_info = nullptr;
  {
    lib::ObMutexGuard guard(peding_free_map_lock_);
    if (OB_FAIL(pending_free_tablet_arr_map_.get_refactored(key, array_info))) {
      LOG_WARN("fail to get pending free tablet array info", K(ret), K(key));
    } 
  }
  if (OB_FAIL(ret)) {
    if (OB_HASH_NOT_EXIST == ret) {
      array_info = nullptr;
      ret = OB_SUCCESS;
    } else {
      LOG_WARN("fail to get pending free tablet array info from map", K(ret), K(key));
    }
  } else if (OB_ISNULL(array_info)) {
    ret = OB_ERR_UNEXPECTED; // get_refactored successfully, but array_info = nullptr
    LOG_WARN("array info is nullptr", K(ret), K(key));
  } else {
    lib::ObMutexGuard guard(array_info->lock_);
    if (OB_FAIL(items.assign(array_info->pending_free_tablet_arr_.items_))) {
      LOG_WARN("fail to assign array", K(ret), K(array_info->pending_free_tablet_arr_.items_));
    }
  }
  return ret;
}
int ObTenantStorageMetaPersister::delete_items_from_pending_free_tablet_array(
    const ObLSID &ls_id, 
    const int64_t ls_epoch, 
    const ObIArray<ObPendingFreeTabletItem> &items)
{
  int ret = OB_SUCCESS;

  PendingFreeTabletArrayKey key(ls_id, ls_epoch);
  PendingFreeTabletArrayInfo *array_info = nullptr;
  {
    lib::ObMutexGuard guard(peding_free_map_lock_);
    if (OB_FAIL(pending_free_tablet_arr_map_.get_refactored(key, array_info))) {
      LOG_WARN("fail to get pending free tablet array info", K(ret), K(key));
    }
  } // guard peding_free_map_lock_

  if (OB_FAIL(ret)) {
    if (OB_HASH_NOT_EXIST == ret) {
      array_info = nullptr;
      ret = OB_SUCCESS;
    } else {
      LOG_WARN("fail to get pending free tablet array info", K(ret), K(key));
    }  
  } else if (OB_ISNULL(array_info)) {
    ret = OB_ERR_UNEXPECTED; // get_refactored successfully, but array_info = nullptr
    LOG_WARN("array info is nullptr", K(ret), K(key));
  } else {
    lib::ObMutexGuard guard(array_info->lock_);
    const ObIArray<ObPendingFreeTabletItem> &arr = array_info->pending_free_tablet_arr_.items_;
    common::ObSArray<ObPendingFreeTabletItem> tmp;
    tmp.reserve(arr.count());
    int64_t delete_cnt = 0;
    for (int64_t i = 0; OB_SUCC(ret) && i < arr.count(); i++) {
      if (has_exist_in_array(items, arr.at(i))) {
        delete_cnt++;
      } else if (OB_FAIL(tmp.push_back(arr.at(i)))) {
        LOG_WARN("failed to push_back", K(ret), K(tmp), K(arr), K(items));
      }
    }
    if (OB_FAIL(ret)) {
      // error occurred
    } else if (items.count() == delete_cnt) {
      // all deleted
      if (OB_FAIL(array_info->pending_free_tablet_arr_.items_.assign(tmp))) {
        LOG_WARN("failed to sync delete op to pending_free_items_arr", K(ret), K(tmp), K(arr), K(items));
      }
    } else if (items.count() != delete_cnt) {
      ret = OB_ENTRY_NOT_EXIST;
      LOG_WARN("deleting item(s) do not all exist in pending_free_arr", K(ret), K(items), K(arr), K(tmp));
    }
  }
  return ret;
}

//=================================== Shared-Storage =============================================//


} // namespace storage
} // namespace oceanbase
