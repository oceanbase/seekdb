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

int ObServerStorageMetaPersister::prepare_create_tenant(const ObTenantMeta &meta, int64_t &epoch)
{
  int ret = OB_SUCCESS;
  if (IS_NOT_INIT) {
    ret = OB_NOT_INIT;
    LOG_WARN("not init", K(ret));
  } else  {
    epoch = 0;
    if (OB_FAIL(write_prepare_create_tenant_slog_(meta))) {
    }

  }
  return ret;
}

int ObServerStorageMetaPersister::commit_create_tenant(const int64_t epoch)
{
  int ret = OB_SUCCESS;
  if (IS_NOT_INIT) {
    ret = OB_NOT_INIT;
    LOG_WARN("not init", K(ret));
  } else if (!is_shared_storage_)  {
    if (OB_FAIL(write_commit_create_tenant_slog_())) {
    }
  } else {
  }
  return ret;
}

int ObServerStorageMetaPersister::abort_create_tenant(const int64_t epoch)
{
  int ret = OB_SUCCESS;
  if (IS_NOT_INIT) {
    ret = OB_NOT_INIT;
    LOG_WARN("not init", K(ret));
  } else if (!is_shared_storage_)  {
    if (OB_FAIL(write_abort_create_tenant_slog_())) {
    }
  } else {
  }
  return ret;
}

int ObServerStorageMetaPersister::commit_delete_tenant(const int64_t epoch)
{
  int ret = OB_SUCCESS;
  if (IS_NOT_INIT) {
    ret = OB_NOT_INIT;
    LOG_WARN("not init", K(ret));
  } else if (!is_shared_storage_)  {
    if (OB_FAIL(write_commit_delete_tenant_slog_())) {
    }
  } else {
  }
  return ret;
}

// Concurrency security is guaranteed by the ObMultiTenant,
// although ObTenantStorageMetaPerister also update the tenant super block,
// but it must they must occur after this, so it don't need a lock here.
int ObServerStorageMetaPersister::update_tenant_super_block(
    const int64_t tenant_epoch, const ObTenantSuperBlock &super_block)
{
  int ret = OB_SUCCESS;
  if (IS_NOT_INIT) {
    ret = OB_NOT_INIT;
    LOG_WARN("not init", K(ret));
  } else  {
    if (OB_FAIL(write_update_tenant_super_block_slog_(super_block))) {
    }

  }
  return ret;
}

int ObServerStorageMetaPersister::update_tenant_unit(
    const int64_t tenant_epoch, const ObUnitInfoGetter::ObTenantConfig &unit)
{
  int ret = OB_SUCCESS;
  if (IS_NOT_INIT) {
    ret = OB_NOT_INIT;
    LOG_WARN("not init", K(ret));
  } else  {
    if (OB_FAIL(write_update_tenant_unit_slog_(unit))) {
    }

  }
  return ret;
}

int ObServerStorageMetaPersister::clear_tenant_log_dir()
{
  int ret = OB_SUCCESS;
  char tenant_clog_dir[MAX_PATH_SIZE] = {0};
  char tenant_slog_dir[MAX_PATH_SIZE] = {0};
  bool exist = true;

  if (OB_FAIL(OB_FILE_SYSTEM_ROUTER.get_tenant_clog_dir(tenant_clog_dir))) {
  } else if (OB_FAIL(FileDirectoryUtils::is_exists(tenant_clog_dir, exist))) {
  } else if (exist) {
    // defense code begin
    int tmp_ret = OB_SUCCESS;
    bool directory_empty = true;
    if (OB_TMP_FAIL(FileDirectoryUtils::is_empty_directory(tenant_clog_dir, directory_empty))) {
    }
    if (!directory_empty) {
      LOG_DBA_ERROR(OB_ERR_UNEXPECTED, "msg", "clog directory must be empty when delete tenant", K(tenant_clog_dir));
    }
    // defense code end
    if (OB_FAIL(FileDirectoryUtils::delete_directory_rec(tenant_clog_dir))) {
    }
  }

  if (OB_SUCC(ret) && !is_shared_storage_) {
    if (OB_FAIL(SERVER_STORAGE_META_SERVICE.get_slogger_manager().get_tenant_slog_dir(tenant_slog_dir))) {
    } else if (OB_FAIL(FileDirectoryUtils::is_exists(tenant_slog_dir, exist))) {
    } else if (exist) {
      if (OB_FAIL(FileDirectoryUtils::delete_directory_rec(tenant_slog_dir))) {
      }
    }
  }
  return ret;
}

int ObServerStorageMetaPersister::write_prepare_create_tenant_slog_(const ObTenantMeta &meta)
{
  int ret = OB_SUCCESS;
  ObStorageLogParam log_param;
  int32_t cmd = ObIRedoModule::gen_cmd(ObRedoLogMainType::OB_REDO_LOG_SERVER_TENANT,
      ObRedoLogSubType::OB_REDO_LOG_CREATE_TENANT_PREPARE);
  ObCreateTenantPrepareLog log_entry(*const_cast<ObTenantMeta*>(&meta));
  log_param.data_ = &log_entry;
  log_param.cmd_ = cmd;
  if (OB_FAIL(server_slogger_->write_log(log_param))) {
  }

  return ret;
}

int ObServerStorageMetaPersister::write_commit_create_tenant_slog_()
{
  int ret = OB_SUCCESS;
  ObStorageLogParam log_param;
  int32_t cmd = ObIRedoModule::gen_cmd(ObRedoLogMainType::OB_REDO_LOG_SERVER_TENANT,
      ObRedoLogSubType::OB_REDO_LOG_CREATE_TENANT_COMMIT);
  ObCreateTenantCommitLog log_entry;
  log_param.data_ = &log_entry;
  log_param.cmd_ = cmd;
  if (OB_FAIL(server_slogger_->write_log(log_param))) {
  }

  return ret;
}
int ObServerStorageMetaPersister::write_abort_create_tenant_slog_()
{
  int ret = OB_SUCCESS;
  ObStorageLogParam log_param;
  int32_t cmd = ObIRedoModule::gen_cmd(ObRedoLogMainType::OB_REDO_LOG_SERVER_TENANT,
      ObRedoLogSubType::OB_REDO_LOG_CREATE_TENANT_ABORT);
  ObCreateTenantAbortLog log_entry;
  log_param.data_ = &log_entry;
  log_param.cmd_ = cmd;
  if (OB_FAIL(server_slogger_->write_log(log_param))) {
  }

  return ret;
}

int ObServerStorageMetaPersister::write_prepare_delete_tenant_slog_()
{
  int ret = OB_SUCCESS;
  ObStorageLogParam log_param;
  int32_t cmd = ObIRedoModule::gen_cmd(ObRedoLogMainType::OB_REDO_LOG_SERVER_TENANT,
      ObRedoLogSubType::OB_REDO_LOG_DELETE_TENANT_PREPARE);
  ObDeleteTenantPrepareLog log_entry;
  log_param.data_ = &log_entry;
  log_param.cmd_ = cmd;
  if (OB_FAIL(server_slogger_->write_log(log_param))) {
  }

  return ret;
}

int ObServerStorageMetaPersister::write_commit_delete_tenant_slog_()
{
  int ret = OB_SUCCESS;
  ObStorageLogParam log_param;
  int32_t cmd = ObIRedoModule::gen_cmd(ObRedoLogMainType::OB_REDO_LOG_SERVER_TENANT,
      ObRedoLogSubType::OB_REDO_LOG_DELETE_TENANT_COMMIT);
  ObDeleteTenantCommitLog log_entry;
  log_param.data_ = &log_entry;
  log_param.cmd_ = cmd;
  if (OB_FAIL(server_slogger_->write_log(log_param))) {
  }

  return ret;
}

int ObServerStorageMetaPersister::write_update_tenant_super_block_slog_(
    const ObTenantSuperBlock &super_block)
{
  int ret = OB_SUCCESS;
  if (IS_NOT_INIT) {
    ret = OB_NOT_INIT;
    LOG_WARN("not init", K(ret));
  } else if (OB_UNLIKELY(!super_block.is_valid())) {
    ret = OB_INVALID_ARGUMENT;
    LOG_WARN("invalid argument", K(ret), K(super_block));
  } else {
    ObUpdateTenantSuperBlockLog slog_entry(*const_cast<ObTenantSuperBlock*>(&super_block));
    ObStorageLogParam log_param;
    log_param.data_ = &slog_entry;
    log_param.cmd_ = ObIRedoModule::gen_cmd(ObRedoLogMainType::OB_REDO_LOG_SERVER_TENANT,
      ObRedoLogSubType::OB_REDO_LOG_UPDATE_TENANT_SUPER_BLOCK);
    if (OB_FAIL(server_slogger_->write_log(log_param))) {
    }
  }
  return ret;
}

int ObServerStorageMetaPersister::write_update_tenant_unit_slog_(const ObUnitInfoGetter::ObTenantConfig &unit)
{
  int ret = OB_SUCCESS;
  ObStorageLogParam log_param;
  int32_t cmd = ObIRedoModule::gen_cmd(ObRedoLogMainType::OB_REDO_LOG_SERVER_TENANT,
      ObRedoLogSubType::OB_REDO_LOG_UPDATE_TENANT_UNIT);
  ObUpdateTenantUnitLog log_entry(*const_cast<ObUnitInfoGetter::ObTenantConfig*>(&unit));
  log_param.data_ = &log_entry;
  log_param.cmd_ = cmd;
  if (OB_FAIL(server_slogger_->write_log(log_param))) {
  }

  return ret;
}


} // namespace storage
} // namespace oceanbase
