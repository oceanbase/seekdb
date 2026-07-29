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

#include "ob_server_storage_meta_persister.h"
#include "storage/meta_store/ob_server_storage_meta_service.h"
#include "storage/meta_store/ob_storage_meta_io_util.h"
#include "storage/slog/ob_storage_log.h"
#include "storage/slog/ob_storage_log_replayer.h"
#include "storage/ob_file_system_router.h"

namespace oceanbase
{
using namespace omt;
using namespace blocksstable;
namespace storage
{

int ObServerStorageMetaPersister::init(ObStorageLogger *server_slogger)
{
  int ret = OB_SUCCESS;
  const int64_t MEM_LIMIT = 512UL << 20;
  lib::ObMemAttr attr("SvrMetaPersist");

  if (OB_UNLIKELY(is_inited_)) {
    ret = OB_INIT_TWICE;
    LOG_WARN("has inited", K(ret));
  } else if (OB_FAIL(allocator_.init(common::OB_MALLOC_NORMAL_BLOCK_SIZE, attr, MEM_LIMIT))) {
    LOG_WARN("fail to init fifo allocator", K(ret));
  } else {
    server_slogger_ = server_slogger;
    is_inited_ = true;
  }
  return ret;
}

void ObServerStorageMetaPersister::destroy()
{
  server_slogger_ = nullptr;
  allocator_.reset();
  is_inited_ = false;
}

int ObServerStorageMetaPersister::prepare_create_runtime(const ObServerRuntimeMeta &meta)
{
  int ret = OB_SUCCESS;
  if (IS_NOT_INIT) {
    ret = OB_NOT_INIT;
    LOG_WARN("not init", K(ret));
  } else if (OB_FAIL(write_prepare_create_runtime_slog_(meta))) {
    LOG_WARN("fail to write prepare create runtime slog", K(ret), K(meta));
  }
  return ret;
}

int ObServerStorageMetaPersister::commit_create_runtime()
{
  int ret = OB_SUCCESS;
  if (IS_NOT_INIT) {
    ret = OB_NOT_INIT;
    LOG_WARN("not init", K(ret));
  } else if (OB_FAIL(write_commit_create_runtime_slog_())) {
    LOG_WARN("fail to write commit create runtime slog", K(ret));
  }
  return ret;
}

int ObServerStorageMetaPersister::abort_create_runtime()
{
  int ret = OB_SUCCESS;
  if (IS_NOT_INIT) {
    ret = OB_NOT_INIT;
    LOG_WARN("not init", K(ret));
  } else if (OB_FAIL(write_abort_create_runtime_slog_())) {
    LOG_WARN("fail to write abort create runtime slog", K(ret));
  }
  return ret;
}

// ObServerRuntimeController serializes updates, so this path needs no extra lock.
int ObServerStorageMetaPersister::update_runtime_super_block(
    const ObServerRuntimeSuperBlock &super_block)
{
  int ret = OB_SUCCESS;
  if (IS_NOT_INIT) {
    ret = OB_NOT_INIT;
    LOG_WARN("not init", K(ret));
  } else if (OB_FAIL(write_update_runtime_super_block_slog_(super_block))) {
    LOG_WARN("fail to write runtime super block slog", K(ret), K(super_block));
  }
  return ret;
}

int ObServerStorageMetaPersister::update_server_resources(
    const ObServerRuntimeConfig &runtime_config)
{
  int ret = OB_SUCCESS;
  if (IS_NOT_INIT) {
    ret = OB_NOT_INIT;
    LOG_WARN("not init", K(ret));
  } else if (OB_FAIL(write_update_server_resources_slog_(runtime_config))) {
    LOG_WARN("fail to write update server resources slog", K(ret), K(runtime_config));
  }
  return ret;
}

int ObServerStorageMetaPersister::clear_runtime_log_dirs()
{
  int ret = OB_SUCCESS;
  char clog_dir[MAX_PATH_SIZE] = {0};
  char slog_dir[MAX_PATH_SIZE] = {0};
  bool exist = true;

  if (OB_FAIL(OB_FILE_SYSTEM_ROUTER.get_server_clog_dir(clog_dir))) {
    LOG_WARN("failed to get server clog dir", K(ret));
  } else if (OB_FAIL(FileDirectoryUtils::is_exists(clog_dir, exist))) {
    LOG_WARN("fail to check exist", K(ret));
  } else if (exist) {
    int tmp_ret = OB_SUCCESS;
    bool directory_empty = true;
    if (OB_TMP_FAIL(FileDirectoryUtils::is_empty_directory(clog_dir, directory_empty))) {
      LOG_WARN("fail to check directory whether is empty", KR(tmp_ret), K(clog_dir));
    }
    if (!directory_empty) {
      LOG_DBA_ERROR(OB_ERR_UNEXPECTED, "msg",
          "clog directory must be empty before rollback cleanup", K(clog_dir));
    }
    if (OB_FAIL(FileDirectoryUtils::delete_directory_rec(clog_dir))) {
      LOG_WARN("fail to delete clog dir", K(ret), K(clog_dir));
    }
  }

  if (OB_SUCC(ret)) {
    const int pret = snprintf(slog_dir, MAX_PATH_SIZE, "%s/sys",
        OB_FILE_SYSTEM_ROUTER.get_slog_dir());
    if (pret < 0 || pret >= MAX_PATH_SIZE) {
      ret = OB_BUF_NOT_ENOUGH;
      LOG_WARN("failed to construct server slog path", K(ret));
    } else if (OB_FAIL(FileDirectoryUtils::is_exists(slog_dir, exist))) {
      LOG_WARN("fail to check exist", K(ret));
    } else if (exist && OB_FAIL(FileDirectoryUtils::delete_directory_rec(slog_dir))) {
      LOG_WARN("fail to delete slog dir", K(ret), K(slog_dir));
    }
  }
  return ret;
}

int ObServerStorageMetaPersister::write_prepare_create_runtime_slog_(
    const ObServerRuntimeMeta &meta)
{
  int ret = OB_SUCCESS;
  ObStorageLogParam log_param;
  int32_t cmd = ObIRedoModule::gen_cmd(ObRedoLogMainType::OB_REDO_LOG_SERVER_RUNTIME,
      ObRedoLogSubType::OB_REDO_LOG_CREATE_RUNTIME_PREPARE);
  ObCreateRuntimePrepareLog log_entry(*const_cast<ObServerRuntimeMeta*>(&meta));
  log_param.data_ = &log_entry;
  log_param.cmd_ = cmd;
  if (OB_FAIL(server_slogger_->write_log(log_param))) {
    LOG_WARN("failed to write create runtime prepare slog", K(ret), K(log_param));
  }
  return ret;
}

int ObServerStorageMetaPersister::write_commit_create_runtime_slog_()
{
  int ret = OB_SUCCESS;
  ObStorageLogParam log_param;
  int32_t cmd = ObIRedoModule::gen_cmd(ObRedoLogMainType::OB_REDO_LOG_SERVER_RUNTIME,
      ObRedoLogSubType::OB_REDO_LOG_CREATE_RUNTIME_COMMIT);
  ObCreateRuntimeCommitLog log_entry;
  log_param.data_ = &log_entry;
  log_param.cmd_ = cmd;
  if (OB_FAIL(server_slogger_->write_log(log_param))) {
    LOG_WARN("failed to write slog", K(ret), K(log_param));
  }
  return ret;
}

int ObServerStorageMetaPersister::write_abort_create_runtime_slog_()
{
  int ret = OB_SUCCESS;
  ObStorageLogParam log_param;
  int32_t cmd = ObIRedoModule::gen_cmd(ObRedoLogMainType::OB_REDO_LOG_SERVER_RUNTIME,
      ObRedoLogSubType::OB_REDO_LOG_CREATE_RUNTIME_ABORT);
  ObCreateRuntimeAbortLog log_entry;
  log_param.data_ = &log_entry;
  log_param.cmd_ = cmd;
  if (OB_FAIL(server_slogger_->write_log(log_param))) {
    LOG_WARN("failed to write slog", K(ret), K(log_param));
  }
  return ret;
}

int ObServerStorageMetaPersister::write_update_runtime_super_block_slog_(
    const ObServerRuntimeSuperBlock &super_block)
{
  int ret = OB_SUCCESS;
  if (IS_NOT_INIT) {
    ret = OB_NOT_INIT;
    LOG_WARN("not init", K(ret));
  } else if (OB_UNLIKELY(!super_block.is_valid())) {
    ret = OB_INVALID_ARGUMENT;
    LOG_WARN("invalid argument", K(ret), K(super_block));
  } else {
    ObUpdateRuntimeSuperBlockLog slog_entry(
        *const_cast<ObServerRuntimeSuperBlock*>(&super_block));
    ObStorageLogParam log_param;
    log_param.data_ = &slog_entry;
    log_param.cmd_ = ObIRedoModule::gen_cmd(
        ObRedoLogMainType::OB_REDO_LOG_SERVER_RUNTIME,
        ObRedoLogSubType::OB_REDO_LOG_UPDATE_RUNTIME_SUPER_BLOCK);
    if (OB_FAIL(server_slogger_->write_log(log_param))) {
      LOG_WARN("fail to write runtime super block slog", K(ret), K(log_param));
    }
  }
  return ret;
}

int ObServerStorageMetaPersister::write_update_server_resources_slog_(
    const ObServerRuntimeConfig &runtime_config)
{
  int ret = OB_SUCCESS;
  ObStorageLogParam log_param;
  int32_t cmd = ObIRedoModule::gen_cmd(ObRedoLogMainType::OB_REDO_LOG_SERVER_RUNTIME,
      ObRedoLogSubType::OB_REDO_LOG_UPDATE_SERVER_RESOURCES);
  ObUpdateServerResourcesLog log_entry(
      *const_cast<ObServerRuntimeConfig*>(&runtime_config));
  log_param.data_ = &log_entry;
  log_param.cmd_ = cmd;
  if (OB_FAIL(server_slogger_->write_log(log_param))) {
    LOG_WARN("failed to write server resources slog", K(ret), K(log_param));
  }
  return ret;
}

} // namespace storage
} // namespace oceanbase
