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

int ObServerStorageMetaPersister::prepare_create_tenant(const ObTenantMeta &meta, int64_t &epoch)
{
  int ret = OB_SUCCESS;
  if (IS_NOT_INIT) {
    ret = OB_NOT_INIT;
    LOG_WARN("not init", K(ret));
  } else  {
    epoch = 0;
    if (OB_FAIL(write_prepare_create_tenant_slog_(meta))) {
      LOG_WARN("fail to write prepare create tenant slog", K(ret), K(meta));
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
      LOG_WARN("fail to write commit create tenant slog", K(ret));
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
      LOG_WARN("fail to write abort create tenant slog", K(ret));
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
      LOG_WARN("fail to write commit delete tenant slog", K(ret));
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
      LOG_WARN("fail to write update tenant super block slog", K(ret), K(super_block));
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
      LOG_WARN("fail to write update tenant unit slog", K(ret), K(unit));
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
    LOG_WARN("fail to get tenant clog dir", K(ret));
  } else if (OB_FAIL(FileDirectoryUtils::is_exists(tenant_clog_dir, exist))) {
    LOG_WARN("fail to check exist", K(ret));
  } else if (exist) {
    // defense code begin
    int tmp_ret = OB_SUCCESS;
    bool directory_empty = true;
    if (OB_TMP_FAIL(FileDirectoryUtils::is_empty_directory(tenant_clog_dir, directory_empty))) {
      LOG_WARN("fail to check directory whether is empty", KR(tmp_ret), K(tenant_clog_dir));
    }
    if (!directory_empty) {
      LOG_DBA_ERROR(OB_ERR_UNEXPECTED, "msg", "clog directory must be empty when delete tenant", K(tenant_clog_dir));
    }
    // defense code end
    if (OB_FAIL(FileDirectoryUtils::delete_directory_rec(tenant_clog_dir))) {
      LOG_WARN("fail to delete clog dir", K(ret), K(tenant_clog_dir));
    }
  }

  if (OB_SUCC(ret) && !is_shared_storage_) {
    const int pret = snprintf(tenant_slog_dir, MAX_PATH_SIZE, "%s/sys", OB_FILE_SYSTEM_ROUTER.get_slog_dir());
    if (pret < 0 || pret >= MAX_PATH_SIZE) {
      ret = OB_BUF_NOT_ENOUGH;
      LOG_WARN("construct tenant slog path fail", K(ret));
    } else if (OB_FAIL(FileDirectoryUtils::is_exists(tenant_slog_dir, exist))) {
      LOG_WARN("fail to check exist", K(ret));
    } else if (exist) {
      if (OB_FAIL(FileDirectoryUtils::delete_directory_rec(tenant_slog_dir))) {
        LOG_WARN("fail to delete slog dir", K(ret), K(tenant_slog_dir));
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
    LOG_WARN("failed to write put tenant slog", K(ret), K(log_param));
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
    LOG_WARN("failed to write slog", K(ret), K(log_param));
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
    LOG_WARN("failed to write slog", K(ret), K(log_param));
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
    LOG_WARN("failed to write slog", K(ret), K(log_param));
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
    LOG_WARN("failed to write slog", K(ret), K(log_param));
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
      LOG_WARN("fail to write tenant super block slog", K(ret), K(log_param));
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
    LOG_WARN("failed to write tenant unit slog", K(ret), K(log_param));
  }

  return ret;
}


} // namespace storage
} // namespace oceanbase
