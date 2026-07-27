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

#include "ob_local_storage_meta_service.h"
#include "share/rc/ob_module_provider.h"
#include "storage/meta_store/ob_storage_meta_io_util.h"
#include "storage/meta_store/ob_server_storage_meta_service.h"
#include "storage/ob_file_system_router.h"
#include "storage/tablet/ob_tablet_macro_info_iterator.h"
#include "observer/omt/ob_server_runtime.h"
#include "storage/ls/ob_ls.h"

namespace oceanbase
{
using namespace compaction;
namespace storage
{

ObLocalStorageMetaService::ObLocalStorageMetaService()
    : is_inited_(false),
      is_started_(false),
      ckpt_slog_handler_(),
    slogger_(),
    persister_(),
    replayer_(),
    object_rwriter_(),
    object_raw_rwriter_()
{}

int ObLocalStorageMetaService::server_module_init(ObLocalStorageMetaService *&meta_service)
{
  int ret = OB_SUCCESS;
  if (OB_FAIL(meta_service->init())) {
    LOG_WARN("fail to init ObLocalStorageMetaService", K(ret));
  }
  return ret;
}

int ObLocalStorageMetaService::init()
{
  int ret = OB_SUCCESS;

  if (OB_UNLIKELY(is_inited_)) {
    ret = OB_INIT_TWICE;
    LOG_WARN("has inited", K(ret));
  } else if (OB_FAIL(slogger_.init(
        OB_FILE_SYSTEM_ROUTER.get_slog_dir(),
        ObLogConstants::MAX_LOG_FILE_SIZE,
        OB_FILE_SYSTEM_ROUTER.get_slog_file_spec()))) {
    LOG_WARN("failed to init slogger", K(ret));
  } else if (OB_FAIL(ckpt_slog_handler_.init(slogger_))) {
    LOG_WARN("fail to init runtime checkpoint slog hander", K(ret));
  } else if (OB_FAIL(persister_.init(slogger_, ckpt_slog_handler_))) {
    LOG_WARN("fail to init persister", K(ret));
  } else if (OB_FAIL(replayer_.init(persister_, ckpt_slog_handler_))) {
    LOG_WARN("fail to init replayer", K(ret));
  } else if (OB_FAIL(object_rwriter_.init())) {
    LOG_WARN("fail to init object reader writer", K(ret));
  } else if (OB_FAIL(object_raw_rwriter_.init())) {
    LOG_WARN("fail to init raw object reader writer", K(ret));
  } else {
    is_inited_ = true;
  }
  return ret;
}

int ObLocalStorageMetaService::start()
{
  int ret = OB_SUCCESS;
  omt::ObServerRuntime *runtime = static_cast<omt::ObServerRuntime*>(share::server_runtime());
  const ObServerRuntimeSuperBlock super_block = runtime->get_super_block();

  if (IS_NOT_INIT) {
    ret = OB_NOT_INIT;
    LOG_WARN("not init", K(ret));
  } else if (OB_FAIL(slogger_.start())) {
    LOG_WARN("fail to start slogger", K(ret));
  } else if (OB_FAIL(ckpt_slog_handler_.start())) {
    LOG_WARN("fail to start runtime checkpoint slog handler", K(ret));
  } else if (OB_FAIL(replayer_.start_replay(super_block))) {
    LOG_WARN("fail to start replayer", K(ret));
  }
  if (OB_SUCC(ret)) {
    is_started_ = true;
  }
  FLOG_INFO("finish start ObLocalStorageMetaService", K(ret));
  return ret;
}

void ObLocalStorageMetaService::stop()
{
  if (IS_INIT) {
     {
      slogger_.stop();
      ckpt_slog_handler_.stop();
    }
  }
}

void ObLocalStorageMetaService::wait()
{
  if (IS_INIT) {
     {
      slogger_.wait();
      ckpt_slog_handler_.wait();
    }
  }
}

void ObLocalStorageMetaService::destroy()
{
  slogger_.destroy();
  ckpt_slog_handler_.destroy();
  persister_.destroy();
  replayer_.destroy();
  object_rwriter_.reset();
  object_raw_rwriter_.reset();

  is_started_ = false;
  is_inited_ = false;
}

int ObLocalStorageMetaService::get_active_cursor(common::ObLogCursor &log_cursor)
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

int ObLocalStorageMetaService::get_meta_block_list(
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

int ObLocalStorageMetaService::write_checkpoint(bool is_force)
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

int ObLocalStorageMetaService::add_snapshot(const ObServerSnapshotMeta &snapshot)
{
  int ret = OB_SUCCESS;
  if (IS_NOT_INIT) {
    ret = OB_NOT_INIT;
    LOG_WARN("not init", K(ret));
  } else if (OB_FAIL(ckpt_slog_handler_.add_snapshot(snapshot))) {
    LOG_WARN("fail to get meta block list", K(ret));
  }
  return ret;
}

int ObLocalStorageMetaService::delete_snapshot(const share::ObServerSnapshotID &snapshot_id)
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

int ObLocalStorageMetaService::swap_snapshot(const ObServerSnapshotMeta &snapshot)
{
  int ret = OB_SUCCESS;
  if (IS_NOT_INIT) {
    ret = OB_NOT_INIT;
    LOG_WARN("not init", K(ret));
  } else if (OB_FAIL(ckpt_slog_handler_.swap_snapshot(snapshot))) {
    LOG_WARN("fail to get meta block list", K(ret));
  }
  return ret;
}

int ObLocalStorageMetaService::clone_ls(
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

int ObLocalStorageMetaService::read_from_disk(
    const ObMetaDiskAddr &addr,
    common::ObArenaAllocator &allocator,
    char *&buf,
    int64_t &buf_len)
{
  int ret = OB_SUCCESS;
  if (ObMetaDiskAddr::DiskType::FILE == addr.type()) {
    if (OB_FAIL(ckpt_slog_handler_.read_empty_shell_file(addr, allocator, buf, buf_len))) {
      LOG_WARN("fail to read empty shell", K(ret), K(addr), K(buf), K(buf_len));
    }
  } else {
    if (OB_FAIL(read_from_block(addr, allocator, buf, buf_len))) {
      LOG_WARN("fail to read from block", K(ret), K(addr), K(buf), K(buf_len));
    }
  }
  return ret;
}

int ObLocalStorageMetaService::read_from_block(
    const ObMetaDiskAddr &addr,
    common::ObArenaAllocator &allocator,
    char *&buf,
    int64_t &buf_len)
{
  int ret = OB_SUCCESS;
  ObObjectReadHandle read_handle(allocator);
  ObObjectReadInfo read_info;
  read_info.io_desc_.set_wait_event(ObWaitEventIds::DB_FILE_DATA_READ);
  read_info.io_timeout_ms_ = GCONF._data_storage_io_timeout / 1000;
  read_info.addr_ = addr;
  if (OB_FAIL(ObObjectReaderWriter::async_read(read_info, read_handle))) {
    LOG_WARN("fail to read tablet from macro block", K(ret), K(read_info));
  } else if (OB_FAIL(read_handle.wait())) {
    LOG_WARN("fail to wait for read handle", K(ret));
  } else if (OB_FAIL(read_handle.get_data(allocator, buf, buf_len))) {
    LOG_WARN("fail to get data from read handle", K(ret), KP(buf), K(buf_len));
  }
  return ret;
}

} // namespace storage
} // namespace oceanbase
