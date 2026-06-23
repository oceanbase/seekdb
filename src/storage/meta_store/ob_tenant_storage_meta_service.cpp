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

#include "ob_tenant_storage_meta_service.h"
#include "storage/meta_store/ob_storage_meta_io_util.h"
#include "storage/meta_store/ob_server_storage_meta_service.h"
#include "storage/tablet/ob_tablet_macro_info_iterator.h"
#include "observer/omt/ob_tenant.h"
#include "storage/ls/ob_ls.h"

namespace oceanbase
{
using namespace compaction;
namespace storage
{

ObTenantStorageMetaService::ObTenantStorageMetaService()
  : is_inited_(false),
    is_started_(false),
    ckpt_slog_handler_(),
    slogger_(),
    persister_(),
    replayer_(),
    shared_object_rwriter_(),
    shared_object_raw_rwriter_()
{}

int ObTenantStorageMetaService::mtl_init(ObTenantStorageMetaService *&meta_service)
{
  int ret = OB_SUCCESS;
  if (OB_FAIL(meta_service->init())) {
    LOG_WARN("fail to init ObTenantStorageMetaService", K(ret));
  }
  return ret;
}

int ObTenantStorageMetaService::init()
{
  int ret = OB_SUCCESS;

  if (OB_UNLIKELY(is_inited_)) {
    ret = OB_INIT_TWICE;
    LOG_WARN("has inited", K(ret));
  } else if (OB_FAIL(slogger_.init(SERVER_STORAGE_META_SERVICE.get_slogger_manager(), MTL_ID()))) {
    LOG_WARN("failed to init slogger", K(ret));
  } else if (OB_FAIL(ckpt_slog_handler_.init(slogger_))) {
    LOG_WARN("fail to init tenant checkpoint slog hander", K(ret));
  } else if (OB_FAIL(persister_.init(slogger_, ckpt_slog_handler_))) {
    LOG_WARN("fail to init persister", K(ret));
  } else if (OB_FAIL(replayer_.init(persister_, ckpt_slog_handler_))) {
    LOG_WARN("fail to init replayer", K(ret));
  } else if (OB_FAIL(shared_object_rwriter_.init())) {
    LOG_WARN("fail to init shared block rwriter", K(ret));
  } else if (OB_FAIL(shared_object_raw_rwriter_.init(
      true /*need_align*/, false /*need_cross*/, true /*auto_release_data_buffer*/))) {
    LOG_WARN("fail to init shared block raw rwriter", K(ret));
  } else {
    
    is_inited_ = true;
  }
  return ret;
}

int ObTenantStorageMetaService::start()
{
  int ret = OB_SUCCESS;
  omt::ObTenant *tenant = static_cast<omt::ObTenant*>(share::ObTenantEnv::get_tenant());
  const ObTenantSuperBlock super_block = tenant->get_super_block();
  uint64_t macro_block_id = 0;

  if (IS_NOT_INIT) {
    ret = OB_NOT_INIT;
    LOG_WARN("not init", K(ret));
  } else if (OB_FAIL(slogger_.start())) {
    LOG_WARN("fail to start slogger", K(ret));
  } else if (OB_FAIL(ckpt_slog_handler_.start())) {
    LOG_WARN("fail to start tenant checkpoint slog handler", K(ret));
  } else if (OB_FAIL(replayer_.start_replay(super_block))) {
    LOG_WARN("fail to start replayer", K(ret));
  } else if (OB_FAIL(seq_generator_.init(persister_))) {
    LOG_WARN("fail to seq generator", K(ret));
  } else if (OB_FAIL(seq_generator_.start())) {
    LOG_WARN("fail to seq generator", K(ret));
  }
  if (OB_SUCC(ret)) {
    is_started_ = true;
  }
  FLOG_INFO("finish start ObTenantStorageMetaService", K(ret));
  return ret;
}

void ObTenantStorageMetaService::stop()
{
  if (IS_INIT) {
     {
      slogger_.stop();
      ckpt_slog_handler_.stop();
    }
    seq_generator_.stop();
  }
}

void ObTenantStorageMetaService::wait()
{
  if (IS_INIT) {
     {
      slogger_.wait();
      ckpt_slog_handler_.wait();
    }
    seq_generator_.stop();
  }
}

void ObTenantStorageMetaService::destroy()
{
  slogger_.destroy();
  ckpt_slog_handler_.destroy();
  persister_.destroy();
  replayer_.destroy();
  seq_generator_.destroy();
  shared_object_rwriter_.reset();
  shared_object_raw_rwriter_.reset();
  
  is_started_ = false;
  is_inited_ = false;
}

int ObTenantStorageMetaService::get_active_cursor(common::ObLogCursor &log_cursor)
{
  int ret = OB_SUCCESS;
  if (IS_NOT_INIT) {
    ret = OB_NOT_INIT;
    LOG_WARN("not init", K(ret));
  } else if (OB_FAIL(slogger_.get_active_cursor(log_cursor))) {
    LOG_WARN("fail to get active cursor", K(ret));
  }
  return ret;
}

int ObTenantStorageMetaService::get_meta_block_list(
    ObIArray<blocksstable::MacroBlockId> &meta_block_list)
{
  int ret = OB_SUCCESS;
  if (IS_NOT_INIT) {
    ret = OB_NOT_INIT;
    LOG_WARN("not init", K(ret));
  } else {
    if (OB_FAIL(ckpt_slog_handler_.get_meta_block_list(meta_block_list))) {
      LOG_WARN("fail to get meta block list", K(ret));
    }
  }
  return ret;
}

int ObTenantStorageMetaService::write_checkpoint(bool is_force)
{
  int ret = OB_SUCCESS;
  if (IS_NOT_INIT) {
    ret = OB_NOT_INIT;
    LOG_WARN("not init", K(ret));
  } else if (OB_FAIL(ckpt_slog_handler_.write_checkpoint(is_force))) {
    LOG_WARN("fail to write checkpoint", K(ret));
  }
  return ret;
}

int ObTenantStorageMetaService::add_snapshot(const ObTenantSnapshotMeta &tenant_snapshot)
{
  int ret = OB_SUCCESS;
  if (IS_NOT_INIT) {
    ret = OB_NOT_INIT;
    LOG_WARN("not init", K(ret));
  } else if (OB_FAIL(ckpt_slog_handler_.add_snapshot(tenant_snapshot))) {
    LOG_WARN("fail to get meta block list", K(ret));
  }
  return ret;
}

int ObTenantStorageMetaService::delete_snapshot(const share::ObTenantSnapshotID &snapshot_id)
{
  int ret = OB_SUCCESS;
  if (IS_NOT_INIT) {
    ret = OB_NOT_INIT;
    LOG_WARN("not init", K(ret));
  } else if (OB_FAIL(ckpt_slog_handler_.delete_snapshot(snapshot_id))) {
    LOG_WARN("fail to get meta block list", K(ret));
  }
  return ret;
}

int ObTenantStorageMetaService::swap_snapshot(const ObTenantSnapshotMeta &tenant_snapshot)
{
  int ret = OB_SUCCESS;
  if (IS_NOT_INIT) {
    ret = OB_NOT_INIT;
    LOG_WARN("not init", K(ret));
  } else if (OB_FAIL(ckpt_slog_handler_.swap_snapshot(tenant_snapshot))) {
    LOG_WARN("fail to get meta block list", K(ret));
  }
  return ret;
}

int ObTenantStorageMetaService::clone_ls(
    observer::ObStartupAccelTaskHandler* startup_accel_handler,
    const blocksstable::MacroBlockId &tablet_meta_entry)
{
  int ret = OB_SUCCESS;
  if (IS_NOT_INIT) {
    ret = OB_NOT_INIT;
    LOG_WARN("not init", K(ret));
  } else if (OB_FAIL(ckpt_slog_handler_.clone_ls(startup_accel_handler, tablet_meta_entry))) {
    LOG_WARN("fail to get meta block list", K(ret));
  }
  return ret;
}

int ObTenantStorageMetaService::read_from_disk(
    const ObMetaDiskAddr &addr,
    const int64_t ls_epoch,
    common::ObArenaAllocator &allocator,
    char *&buf,
    int64_t &buf_len)
{
  int ret = OB_SUCCESS;
  char *read_buf = nullptr;
  const int64_t read_buf_len = addr.size();
  if (ObMetaDiskAddr::DiskType::FILE == addr.type()) {
    if (OB_FAIL(ckpt_slog_handler_.read_empty_shell_file(addr, allocator, buf, buf_len))) {
      LOG_WARN("fail to read empty shell", K(ret), K(addr), K(buf), K(buf_len));
    }
  } else {
    if (OB_FAIL(read_from_share_blk(addr, ls_epoch, allocator, buf, buf_len))) {
      LOG_WARN("fail to read from share block", K(ret), K(addr), K(ls_epoch), K(buf), K(buf_len));
    }
  }
  return ret;
}

int ObTenantStorageMetaService::read_from_share_blk(
    const ObMetaDiskAddr &addr,
    const int64_t ls_epoch,
    common::ObArenaAllocator &allocator,
    char *&buf,
    int64_t &buf_len)
{
  int ret = OB_SUCCESS;
  ObSharedObjectReadHandle read_handle(allocator);
  ObSharedObjectReadInfo read_info;
  read_info.io_desc_.set_wait_event(ObWaitEventIds::DB_FILE_DATA_READ);
  read_info.io_timeout_ms_ = GCONF._data_storage_io_timeout / 1000;
  read_info.addr_ = addr;
  read_info.ls_epoch_ = ls_epoch; /* ls_epoch for share storage */
  if (OB_FAIL(ObSharedObjectReaderWriter::async_read(read_info, read_handle))) {
    LOG_WARN("fail to read tablet from macro block", K(ret), K(read_info));
  } else if (OB_FAIL(read_handle.wait())) {
    LOG_WARN("fail to wait for read handle", K(ret));
  } else if (OB_FAIL(read_handle.get_data(allocator, buf, buf_len))) {
    LOG_WARN("fail to get data from read handle", K(ret), KP(buf), K(buf_len));
  }
  return ret;
}

int ObTenantStorageMetaService::ObLSItemIterator::get_next_ls_item(
      storage::ObLSItem &item)
{
  int ret = OB_SUCCESS;
  if (idx_ == tenant_super_block_.ls_cnt_) {
    ret = OB_ITER_END;
  } else {
    item = tenant_super_block_.ls_item_arr_[idx_++];
  }
  return ret;
}

int ObTenantStorageMetaService::get_ls_items_by_status(
    const storage::ObLSItemStatus status,
    ObIArray<storage::ObLSItem> &ls_items)
{
  int ret = OB_SUCCESS;
  if (IS_NOT_INIT) {
    ret = OB_NOT_INIT;
    LOG_WARN("not init", K(ret));
  } else {
    ls_items.reuse();
    omt::ObTenant *tenant = static_cast<omt::ObTenant*>(MTL_CTX());
    HEAP_VAR(ObLSItemIterator, ls_item_iter, tenant->get_super_block()) {
      ObLSItem ls_item;
      while (OB_SUCC(ls_item_iter.get_next_ls_item(ls_item))) {
        if (status == ls_item.status_ &&
            OB_FAIL(ls_items.push_back(ls_item))) {
          LOG_WARN("failed to push back tenant_item", K(ret), K(ls_item), K(ls_items), K(ls_item_iter));
        }
      }
      if (OB_ITER_END == ret) {
        ret = OB_SUCCESS;
      } else {
        LOG_WARN("failed to get tenant items by status", K(ret), K(ls_item), K(ls_items), K(ls_item_iter));
      }
    }
  }
  return ret;
}

} // namespace storage
} // namespace oceanbase
