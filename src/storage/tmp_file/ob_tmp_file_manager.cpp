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
#include "storage/tmp_file/ob_tmp_file_manager.h"
#include "share/rc/ob_module_provider.h"
#include "observer/ob_server_struct.h"

namespace oceanbase
{
namespace tmp_file
{
int ObTenantTmpFileManager::mtl_init(ObTenantTmpFileManager *&manager)
{
  int ret = OB_SUCCESS;
  if (OB_ISNULL(manager)) {
    ret = OB_INVALID_ARGUMENT;
    LOG_WARN("fail to mtl init tmp file manager, null pointer argument", KR(ret), KP(manager));
  } else if (OB_FAIL(manager->init())) {
  }
  return ret;
}

int ObTenantTmpFileManager::init()
{
  int ret = OB_SUCCESS;
  if (OB_UNLIKELY(is_inited_)) {
    ret = OB_INIT_TWICE;
    LOG_WARN("ObTenantTmpFileManager init twice", K(ret), K(is_inited_));
  } else {
    if (OB_FAIL(get_sn_file_manager().init())) {
    }
  }

  if (OB_SUCC(ret)) {
    is_inited_ = true;
  }
  LOG_INFO("ObTenantTmpFileManager init success", KR(ret), K(GCTX.is_shared_storage_mode()));
  return ret;
}

int ObTenantTmpFileManager::start()
{
  int ret = OB_SUCCESS;
  if (IS_NOT_INIT) {
    ret = OB_NOT_INIT;
    LOG_WARN("ObTenantTmpFileManager has not been inited", KR(ret));
  } else {
    if (OB_FAIL(get_sn_file_manager().start())) {
    }
  }
  LOG_INFO("ObTenantTmpFileManager start success", KR(ret), K(GCTX.is_shared_storage_mode()));
  return ret;
}

void ObTenantTmpFileManager::stop()
{
  if (!GCTX.is_shared_storage_mode()) {
    get_sn_file_manager().stop();
  }
  LOG_INFO("ObTenantTmpFileManager stop success", K(GCTX.is_shared_storage_mode()));
}

void ObTenantTmpFileManager::wait()
{
  if (!GCTX.is_shared_storage_mode()) {
    get_sn_file_manager().wait();
  }
  LOG_INFO("ObTenantTmpFileManager wait success", K(GCTX.is_shared_storage_mode()));
}

void ObTenantTmpFileManager::destroy()
{
  get_sn_file_manager().destroy();
  is_inited_ = false;
  LOG_INFO("ObTenantTmpFileManager destroy success", K(GCTX.is_shared_storage_mode()));
}

int ObTenantTmpFileManager::alloc_dir(int64_t &dir_id)
{
  int ret = OB_SUCCESS;
  if (IS_NOT_INIT) {
    ret = OB_NOT_INIT;
    LOG_WARN("ObTenantTmpFileManager has not been inited", KR(ret));
  } else {
    if (OB_FAIL(get_sn_file_manager().alloc_dir(dir_id))) {
    }
  }
  return ret;
}

int ObTenantTmpFileManager::open(int64_t &fd, const int64_t &dir_id, const char* const label)
{
  int ret = OB_SUCCESS;
  if (IS_NOT_INIT) {
    ret = OB_NOT_INIT;
    LOG_WARN("ObTenantTmpFileManager has not been inited", KR(ret));
  } else {
    if (OB_FAIL(get_sn_file_manager().open(fd, dir_id, label))) {
    }
  }
  return ret;
}

int ObTenantTmpFileManager::remove(const int64_t fd)
{
  int ret = OB_SUCCESS;
  if (IS_NOT_INIT) {
    ret = OB_NOT_INIT;
    LOG_WARN("ObTenantTmpFileManager has not been inited", KR(ret));
  } else {
    if (OB_FAIL(get_sn_file_manager().remove(fd))) {
    }
  }
  return ret;
}

int ObTenantTmpFileManager::aio_read(const ObTmpFileIOInfo &io_info,
                                     ObTmpFileIOHandle &io_handle)
{
  int ret = OB_SUCCESS;
  if (IS_NOT_INIT) {
    ret = OB_NOT_INIT;
    LOG_WARN("ObTenantTmpFileManager has not been inited", KR(ret));
  } else {
    if (OB_FAIL(get_sn_file_manager().aio_read(io_info, io_handle))) {
    }
  }
  return ret;
}

int ObTenantTmpFileManager::aio_pread(const ObTmpFileIOInfo &io_info,
                                      const int64_t offset, ObTmpFileIOHandle &io_handle)
{
  int ret = OB_SUCCESS;
  if (IS_NOT_INIT) {
    ret = OB_NOT_INIT;
    LOG_WARN("ObTenantTmpFileManager has not been inited", KR(ret));
  } else {
    if (OB_FAIL(get_sn_file_manager().aio_pread(io_info, offset, io_handle))) {
    }
  }
  return ret;
}

int ObTenantTmpFileManager::read(const ObTmpFileIOInfo &io_info,
                                 ObTmpFileIOHandle &io_handle)
{
  int ret = OB_SUCCESS;
  if (IS_NOT_INIT) {
    ret = OB_NOT_INIT;
    LOG_WARN("ObTenantTmpFileManager has not been inited", KR(ret));
  } else {
    if (OB_FAIL(get_sn_file_manager().read(io_info, io_handle))) {
    }
  }
  return ret;
}

int ObTenantTmpFileManager::pread(const ObTmpFileIOInfo &io_info,
                                  const int64_t offset, ObTmpFileIOHandle &io_handle)
{
  int ret = OB_SUCCESS;
  if (IS_NOT_INIT) {
    ret = OB_NOT_INIT;
    LOG_WARN("ObTenantTmpFileManager has not been inited", KR(ret));
  } else {
    if (OB_FAIL(get_sn_file_manager().pread(io_info, offset, io_handle))) {
    }
  }
  return ret;
}

int ObTenantTmpFileManager::aio_write(const ObTmpFileIOInfo &io_info,
                                      ObTmpFileIOHandle &io_handle)
{
  int ret = OB_SUCCESS;
  if (IS_NOT_INIT) {
    ret = OB_NOT_INIT;
    LOG_WARN("ObTenantTmpFileManager has not been inited", KR(ret));
  } else {
    if (OB_FAIL(get_sn_file_manager().aio_write(io_info, io_handle))) {
    }
  }
  return ret;
}

int ObTenantTmpFileManager::write(const ObTmpFileIOInfo &io_info)
{
  int ret = OB_SUCCESS;
  if (IS_NOT_INIT) {
    ret = OB_NOT_INIT;
    LOG_WARN("ObTenantTmpFileManager has not been inited", KR(ret));
  } else {
    if (OB_FAIL(get_sn_file_manager().write(io_info))) {
    }
  }
  return ret;
}

int ObTenantTmpFileManager::truncate(const int64_t fd, const int64_t offset)
{
  int ret = OB_SUCCESS;
  if (IS_NOT_INIT) {
    ret = OB_NOT_INIT;
    LOG_WARN("ObTenantTmpFileManager has not been inited", KR(ret));
  } else {
    if (OB_FAIL(get_sn_file_manager().truncate(fd, offset))) {
    }
  }
  return ret;
}

int ObTenantTmpFileManager::seal(const int64_t fd)
{
  int ret = OB_SUCCESS;
  if (IS_NOT_INIT) {
    ret = OB_NOT_INIT;
    LOG_WARN("ObTenantTmpFileManager has not been inited", KR(ret));
  } else {
    if (OB_FAIL(sn_file_manager_.seal(fd))) {
    }
  }
  return ret;
}

int ObTenantTmpFileManager::get_tmp_file_size(const int64_t fd, int64_t &file_size)
{
  int ret = OB_SUCCESS;
  if (IS_NOT_INIT) {
    ret = OB_NOT_INIT;
    LOG_WARN("ObTenantTmpFileManager has not been inited", KR(ret));
  } else {
    if (OB_FAIL(get_sn_file_manager().get_tmp_file_size(fd, file_size))) {
    }
  }
  return ret;
}

int ObTenantTmpFileManager::get_tmp_file(const int64_t fd, ObITmpFileHandle &handle)
{
  int ret = OB_SUCCESS;
  if (IS_NOT_INIT) {
    ret = OB_NOT_INIT;
    LOG_WARN("ObTenantTmpFileManager has not been inited", KR(ret));
  } else {
    if (OB_FAIL(get_sn_file_manager().ObITenantTmpFileManager::get_tmp_file(fd, handle))) {
    }
  }
  return ret;
}

int ObTenantTmpFileManager::get_tmp_file_disk_usage(int64_t &disk_data_size, int64_t &occupied_disk_size)
{
  int ret = OB_SUCCESS;
  if (IS_NOT_INIT) {
    ret = OB_NOT_INIT;
    LOG_WARN("ObTenantTmpFileManager has not been inited", KR(ret));
  } else {
    if (OB_FAIL(get_sn_file_manager().get_tmp_file_disk_usage(disk_data_size, occupied_disk_size))) {
    }
  }

  return ret;
}

int ObTenantTmpFileManager::get_tmp_file_fds(ObIArray<int64_t> &fd_arr)
{
  int ret = OB_SUCCESS;
  if (IS_NOT_INIT) {
    ret = OB_NOT_INIT;
    LOG_WARN("ObTenantTmpFileManager has not been inited", KR(ret));
  } else {
    if (OB_FAIL(get_sn_file_manager().get_tmp_file_fds(fd_arr))) {
    }
  }

  return ret;
}

int ObTenantTmpFileManager::get_tmp_file_info(const int64_t fd, ObTmpFileInfo *tmp_file_info)
{
  int ret = OB_SUCCESS;

  if (IS_NOT_INIT) {
    ret = OB_NOT_INIT;
    LOG_WARN("ObTenantTmpFileManager has not been inited", KR(ret));
  } else if (OB_ISNULL(tmp_file_info)) {
    ret = OB_INVALID_ARGUMENT;
    LOG_WARN("invalid argument", KR(ret), K(fd), KP(tmp_file_info));
  } else {
    if (OB_FAIL(get_sn_file_manager().get_tmp_file_info(fd, *tmp_file_info))) {
    }
  }
  return ret;
}

ObTenantTmpFileManagerWithMTLSwitch &ObTenantTmpFileManagerWithMTLSwitch::get_instance()
{
  static ObTenantTmpFileManagerWithMTLSwitch mgr;

  return mgr;
}

int ObTenantTmpFileManagerWithMTLSwitch::alloc_dir(int64_t &dir_id)
{
  int ret = OB_SUCCESS;
  if (OB_UNLIKELY(!true || false)) {
    ret = OB_INVALID_ARGUMENT;
    LOG_WARN("invalid argument", KR(ret));
  } else {
    MAKE_TENANT_SWITCH_SCOPE_GUARD(guard);
    
    ObTenantTmpFileManager* tmp_file_mgr = share::g_mp->tenant_tmp_file_manager();
    if (OB_FAIL(ret)) {
    } else if (OB_ISNULL(tmp_file_mgr)) {
      ret = OB_ERR_UNEXPECTED;
      LOG_ERROR("tmp file manager is null", KR(ret));
    } else if (OB_FAIL(tmp_file_mgr->alloc_dir(dir_id))) {
    }
  }
  return ret;
}

int ObTenantTmpFileManagerWithMTLSwitch::open(int64_t &fd,
                                              const int64_t &dir_id,
                                              const char* const label)
{
  int ret = OB_SUCCESS;
  if (OB_UNLIKELY(!true || false)) {
    ret = OB_INVALID_ARGUMENT;
    LOG_WARN("invalid argument", KR(ret));
  } else {
    MAKE_TENANT_SWITCH_SCOPE_GUARD(guard);
    
    ObTenantTmpFileManager* tmp_file_mgr = share::g_mp->tenant_tmp_file_manager();
    if (OB_FAIL(ret)) {
    } else if (OB_ISNULL(tmp_file_mgr)) {
      ret = OB_ERR_UNEXPECTED;
      LOG_ERROR("tmp file manager is null", KR(ret));
    } else if (OB_FAIL(tmp_file_mgr->open(fd, dir_id, label))) {
    }
  }
  return ret;
}

int ObTenantTmpFileManagerWithMTLSwitch::remove(const int64_t fd)
{
  int ret = OB_SUCCESS;
  if (OB_UNLIKELY(!true || false)) {
    ret = OB_INVALID_ARGUMENT;
    LOG_WARN("invalid argument", KR(ret));
  } else {
    MAKE_TENANT_SWITCH_SCOPE_GUARD(guard);
    
    ObTenantTmpFileManager* tmp_file_mgr = share::g_mp->tenant_tmp_file_manager();
    if (OB_FAIL(ret)) {
    } else if (OB_ISNULL(tmp_file_mgr)) {
      ret = OB_ERR_UNEXPECTED;
      LOG_ERROR("tmp file manager is null", KR(ret));
    } else if (OB_FAIL(tmp_file_mgr->remove(fd))) {
    }
  }
  return ret;
}

int ObTenantTmpFileManagerWithMTLSwitch::aio_read(const ObTmpFileIOInfo &io_info, ObTmpFileIOHandle &io_handle)
{
  int ret = OB_SUCCESS;
  if (OB_UNLIKELY(!true || false)) {
    ret = OB_INVALID_ARGUMENT;
    LOG_WARN("invalid argument", KR(ret));
  } else {
    MAKE_TENANT_SWITCH_SCOPE_GUARD(guard);
    
    ObTenantTmpFileManager* tmp_file_mgr = share::g_mp->tenant_tmp_file_manager();
    if (OB_FAIL(ret)) {
    } else if (OB_ISNULL(tmp_file_mgr)) {
      ret = OB_ERR_UNEXPECTED;
      LOG_ERROR("tmp file manager is null", KR(ret));
    } else if (OB_FAIL(tmp_file_mgr->aio_read(io_info, io_handle))) {
    }
  }
  return ret;
}

int ObTenantTmpFileManagerWithMTLSwitch::aio_pread(const ObTmpFileIOInfo &io_info,
                                                   const int64_t offset, ObTmpFileIOHandle &io_handle)
{
  int ret = OB_SUCCESS;
  if (OB_UNLIKELY(!true || false)) {
    ret = OB_INVALID_ARGUMENT;
    LOG_WARN("invalid argument", KR(ret));
  } else {
    MAKE_TENANT_SWITCH_SCOPE_GUARD(guard);
    
    ObTenantTmpFileManager* tmp_file_mgr = share::g_mp->tenant_tmp_file_manager();
    if (OB_FAIL(ret)) {
    } else if (OB_ISNULL(tmp_file_mgr)) {
      ret = OB_ERR_UNEXPECTED;
      LOG_ERROR("tmp file manager is null", KR(ret));
    } else if (OB_FAIL(tmp_file_mgr->aio_pread(io_info, offset, io_handle))) {
    }
  }
  return ret;
}

int ObTenantTmpFileManagerWithMTLSwitch::pread(const ObTmpFileIOInfo &io_info, const int64_t offset, ObTmpFileIOHandle &io_handle)
{
  int ret = OB_SUCCESS;
  if (OB_UNLIKELY(!true || false)) {
    ret = OB_INVALID_ARGUMENT;
    LOG_WARN("invalid argument", KR(ret));
  } else {
    MAKE_TENANT_SWITCH_SCOPE_GUARD(guard);
    
    ObTenantTmpFileManager* tmp_file_mgr = share::g_mp->tenant_tmp_file_manager();
    if (OB_FAIL(ret)) {
    } else if (OB_ISNULL(tmp_file_mgr)) {
      ret = OB_ERR_UNEXPECTED;
      LOG_ERROR("tmp file manager is null", KR(ret));
    } else if (OB_FAIL(tmp_file_mgr->pread(io_info, offset, io_handle))) {
    }
  }
  return ret;
}

int ObTenantTmpFileManagerWithMTLSwitch::aio_write(const ObTmpFileIOInfo &io_info, ObTmpFileIOHandle &io_handle)
{
  int ret = OB_SUCCESS;
  if (OB_UNLIKELY(!true || false)) {
    ret = OB_INVALID_ARGUMENT;
    LOG_WARN("invalid argument", KR(ret));
  } else {
    MAKE_TENANT_SWITCH_SCOPE_GUARD(guard);
    
    ObTenantTmpFileManager* tmp_file_mgr = share::g_mp->tenant_tmp_file_manager();
    if (OB_FAIL(ret)) {
    } else if (OB_ISNULL(tmp_file_mgr)) {
      ret = OB_ERR_UNEXPECTED;
      LOG_ERROR("tmp file manager is null", KR(ret));
    } else if (OB_FAIL(tmp_file_mgr->aio_write(io_info, io_handle))) {
    }
  }
  return ret;
}

int ObTenantTmpFileManagerWithMTLSwitch::write(const ObTmpFileIOInfo &io_info)
{
  int ret = OB_SUCCESS;
  if (OB_UNLIKELY(!true || false)) {
    ret = OB_INVALID_ARGUMENT;
    LOG_WARN("invalid argument", KR(ret));
  } else {
    MAKE_TENANT_SWITCH_SCOPE_GUARD(guard);
    
    ObTenantTmpFileManager* tmp_file_mgr = share::g_mp->tenant_tmp_file_manager();
    if (OB_FAIL(ret)) {
    } else if (OB_ISNULL(tmp_file_mgr)) {
      ret = OB_ERR_UNEXPECTED;
      LOG_ERROR("tmp file manager is null", KR(ret));
    } else if (OB_FAIL(tmp_file_mgr->write(io_info))) {
    }
  }
  return ret;
}

int ObTenantTmpFileManagerWithMTLSwitch::truncate(const int64_t fd, const int64_t offset)
{
  int ret = OB_SUCCESS;
  if (OB_UNLIKELY(!true || false)) {
    ret = OB_INVALID_ARGUMENT;
    LOG_WARN("invalid argument", KR(ret));
  } else {
    MAKE_TENANT_SWITCH_SCOPE_GUARD(guard);
    
    ObTenantTmpFileManager* tmp_file_mgr = share::g_mp->tenant_tmp_file_manager();
    if (OB_FAIL(ret)) {
    } else if (OB_ISNULL(tmp_file_mgr)) {
      ret = OB_ERR_UNEXPECTED;
      LOG_ERROR("tmp file manager is null", KR(ret));
    } else if (OB_FAIL(tmp_file_mgr->truncate(fd, offset))) {
    }
  }
  return ret;
}

int ObTenantTmpFileManagerWithMTLSwitch::seal(const int64_t fd)
{
  int ret = OB_SUCCESS;
  if (OB_UNLIKELY(!true || false)) {
    ret = OB_INVALID_ARGUMENT;
    LOG_WARN("invalid argument", KR(ret));
  } else {
    MAKE_TENANT_SWITCH_SCOPE_GUARD(guard);
    
    ObTenantTmpFileManager* tmp_file_mgr = share::g_mp->tenant_tmp_file_manager();
    if (OB_FAIL(ret)) {
    } else if (OB_ISNULL(tmp_file_mgr)) {
      ret = OB_ERR_UNEXPECTED;
      LOG_ERROR("tmp file manager is null", KR(ret));
    } else if (OB_FAIL(tmp_file_mgr->seal(fd))) {
    }
  }
  return ret;
}

int ObTenantTmpFileManagerWithMTLSwitch::get_tmp_file_size(const int64_t fd, int64_t &file_size)
{
  int ret = OB_SUCCESS;
  if (OB_UNLIKELY(!true || false)) {
    ret = OB_INVALID_ARGUMENT;
    LOG_WARN("invalid argument", KR(ret));
  } else {
    MAKE_TENANT_SWITCH_SCOPE_GUARD(guard);
    
    ObTenantTmpFileManager* tmp_file_mgr = share::g_mp->tenant_tmp_file_manager();
    if (OB_FAIL(ret)) {
    } else if (OB_ISNULL(tmp_file_mgr)) {
      ret = OB_ERR_UNEXPECTED;
      LOG_ERROR("tmp file manager is null", KR(ret));
    } else if (OB_FAIL(tmp_file_mgr->get_tmp_file_size(fd, file_size))) {
    }
  }
  return ret;
}

int ObTenantTmpFileManagerWithMTLSwitch::get_tmp_file_fds(ObIArray<int64_t> &fd_arr)
{
  int ret = OB_SUCCESS;
  if (OB_UNLIKELY(!true || false)) {
    ret = OB_INVALID_ARGUMENT;
    LOG_WARN("invalid argument", KR(ret));
  } else {
    MAKE_TENANT_SWITCH_SCOPE_GUARD(guard);
    
    ObTenantTmpFileManager* tmp_file_mgr = share::g_mp->tenant_tmp_file_manager();
    if (OB_FAIL(ret)) {
    } else if (OB_ISNULL(tmp_file_mgr)) {
      ret = OB_ERR_UNEXPECTED;
      LOG_ERROR("tmp file manager is null", KR(ret));
    } else if (OB_FAIL(tmp_file_mgr->get_tmp_file_fds(fd_arr))) {
    }
  }
  return ret;
}

int ObTenantTmpFileManagerWithMTLSwitch::get_tmp_file_info(const int64_t fd, ObTmpFileInfo *tmp_file_info)
{
  int ret = OB_SUCCESS;
  if (OB_UNLIKELY(!true || false)) {
    ret = OB_INVALID_ARGUMENT;
    LOG_WARN("invalid argument", KR(ret));
  } else if (OB_ISNULL(tmp_file_info)) {
    ret = OB_INVALID_ARGUMENT;
    LOG_WARN("invalid argument", KR(ret), K(fd), KP(tmp_file_info));
  } else {
    MAKE_TENANT_SWITCH_SCOPE_GUARD(guard);
    
    ObTenantTmpFileManager* tmp_file_mgr = share::g_mp->tenant_tmp_file_manager();
    if (OB_FAIL(ret)) {
    } else if (OB_ISNULL(tmp_file_mgr)) {
      ret = OB_ERR_UNEXPECTED;
      LOG_ERROR("tmp file manager is null", KR(ret));
    } else if (OB_FAIL(tmp_file_mgr->get_tmp_file_info(fd, tmp_file_info))) {
    }
  }
  return ret;
}

}  // end namespace tmp_file
}  // end namespace oceanbase
