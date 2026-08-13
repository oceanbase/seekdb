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
#include "share/rc/ob_server_runtime.h"
#include "storage/meta_store/ob_storage_meta_io_util.h"
#include "storage/meta_store/ob_server_storage_meta_service.h"
#include "storage/api/storage/runtime/ob_i_server_runtime.h"
#include "storage/ob_file_system_router.h"
#include "storage/tablet/ob_tablet_macro_info_iterator.h"
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
  } else if (OB_FAIL(ckpt_slog_handler_.init(slogger_))) {
  } else if (OB_FAIL(persister_.init(slogger_, ckpt_slog_handler_))) {
  } else if (OB_FAIL(replayer_.init(persister_, ckpt_slog_handler_))) {
  } else if (OB_FAIL(object_rwriter_.init())) {
  } else if (OB_FAIL(object_raw_rwriter_.init())) {
  } else {
    is_inited_ = true;
  }
  return ret;
}

int ObLocalStorageMetaService::start()
{
  int ret = OB_SUCCESS;
  ObIServerRuntime *runtime = ::oceanbase::share::server_service<::oceanbase::storage::ObIServerRuntime>();
  const ObServerRuntimeSuperBlock super_block = runtime->get_super_block();

  if (IS_NOT_INIT) {
    ret = OB_NOT_INIT;
    LOG_WARN("not init", K(ret));
  } else if (OB_FAIL(slogger_.start())) {
  } else if (OB_FAIL(ckpt_slog_handler_.start())) {
  } else if (OB_FAIL(replayer_.start_replay(super_block))) {
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
  }
  return ret;
}

int ObLocalStorageMetaService::clone_ls(
    ObStartupAccelTaskHandler* startup_accel_handler,
    const blocksstable::MacroBlockId &tablet_meta_entry)
{
  int ret = OB_SUCCESS;
  if (IS_NOT_INIT) {
    ret = OB_NOT_INIT;
    LOG_WARN("not init", K(ret));
  } else if (OB_FAIL(ckpt_slog_handler_.clone_ls(startup_accel_handler, tablet_meta_entry))) {
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
    }
  } else {
    if (OB_FAIL(read_from_block(addr, allocator, buf, buf_len))) {
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
  } else if (OB_FAIL(read_handle.wait())) {
  } else if (OB_FAIL(read_handle.get_data(allocator, buf, buf_len))) {
  }
  return ret;
}

} // namespace storage
} // namespace oceanbase
