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
int ObTmpFileManager::server_module_init(ObTmpFileManager *&manager)
{
  int ret = OB_SUCCESS;
  if (OB_ISNULL(manager)) {
    ret = OB_INVALID_ARGUMENT;
    LOG_WARN("fail to initialize runtime tmp file manager, null pointer argument", KR(ret), KP(manager));
  } else if (OB_FAIL(manager->init())) {
    LOG_WARN("fail to init ObTmpFileManager", KR(ret));
  }
  return ret;
}

int ObTmpFileManager::init()
{
  int ret = OB_SUCCESS;
  if (OB_UNLIKELY(is_inited_)) {
    ret = OB_INIT_TWICE;
    LOG_WARN("ObTmpFileManager init twice", K(ret), K(is_inited_));
  } else {
    if (OB_FAIL(get_sn_file_manager().init())) {
      LOG_WARN("fail to init sn tmp file manager", KR(ret));
    }
  }

  if (OB_SUCC(ret)) {
    is_inited_ = true;
  }
  LOG_INFO("ObTmpFileManager init success", KR(ret));
  return ret;
}

int ObTmpFileManager::start()
{
  int ret = OB_SUCCESS;
  if (IS_NOT_INIT) {
    ret = OB_NOT_INIT;
    LOG_WARN("ObTmpFileManager has not been inited", KR(ret));
  } else {
    if (OB_FAIL(get_sn_file_manager().start())) {
      LOG_WARN("fail to start sn tmp file manager", KR(ret));
    }
  }
  LOG_INFO("ObTmpFileManager start success", KR(ret));
  return ret;
}

void ObTmpFileManager::stop()
{
  get_sn_file_manager().stop();
  LOG_INFO("ObTmpFileManager stop success");
}

void ObTmpFileManager::wait()
{
  get_sn_file_manager().wait();
  LOG_INFO("ObTmpFileManager wait success");
}

void ObTmpFileManager::destroy()
{
  get_sn_file_manager().destroy();
  is_inited_ = false;
  LOG_INFO("ObTmpFileManager destroy success");
}

int ObTmpFileManager::alloc_dir(int64_t &dir_id)
{
  int ret = OB_SUCCESS;
  if (IS_NOT_INIT) {
    ret = OB_NOT_INIT;
    LOG_WARN("ObTmpFileManager has not been inited", KR(ret));
  } else {
    if (OB_FAIL(get_sn_file_manager().alloc_dir(dir_id))) {
      LOG_WARN("fail to alloc dir in sn tmp file manager", KR(ret), K(dir_id));
    }
  }
  return ret;
}

int ObTmpFileManager::open(int64_t &fd, const int64_t &dir_id, const char* const label)
{
  int ret = OB_SUCCESS;
  if (IS_NOT_INIT) {
    ret = OB_NOT_INIT;
    LOG_WARN("ObTmpFileManager has not been inited", KR(ret));
  } else {
    if (OB_FAIL(get_sn_file_manager().open(fd, dir_id, label))) {
      LOG_WARN("fail to open file in sn tmp file manager", KR(ret), K(fd), K(dir_id), KP(label));
    }
  }
  return ret;
}

int ObTmpFileManager::remove(const int64_t fd)
{
  int ret = OB_SUCCESS;
  if (IS_NOT_INIT) {
    ret = OB_NOT_INIT;
    LOG_WARN("ObTmpFileManager has not been inited", KR(ret));
  } else {
    if (OB_FAIL(get_sn_file_manager().remove(fd))) {
      LOG_WARN("fail to remove file in sn tmp file manager", KR(ret), K(fd));
    }
  }
  return ret;
}

int ObTmpFileManager::aio_read(const ObTmpFileIOInfo &io_info,
                                     ObTmpFileIOHandle &io_handle)
{
  int ret = OB_SUCCESS;
  if (IS_NOT_INIT) {
    ret = OB_NOT_INIT;
    LOG_WARN("ObTmpFileManager has not been inited", KR(ret));
  } else {
    if (OB_FAIL(get_sn_file_manager().aio_read(io_info, io_handle))) {
      LOG_WARN("fail to read file in sn tmp file manager", KR(ret), K(io_info));
    }
  }
  return ret;
}

int ObTmpFileManager::aio_pread(const ObTmpFileIOInfo &io_info,
                                      const int64_t offset, ObTmpFileIOHandle &io_handle)
{
  int ret = OB_SUCCESS;
  if (IS_NOT_INIT) {
    ret = OB_NOT_INIT;
    LOG_WARN("ObTmpFileManager has not been inited", KR(ret));
  } else {
    if (OB_FAIL(get_sn_file_manager().aio_pread(io_info, offset, io_handle))) {
      LOG_WARN("fail to read file in sn tmp file manager", KR(ret), K(io_info), K(offset));
    }
  }
  return ret;
}

int ObTmpFileManager::read(const ObTmpFileIOInfo &io_info,
                                 ObTmpFileIOHandle &io_handle)
{
  int ret = OB_SUCCESS;
  if (IS_NOT_INIT) {
    ret = OB_NOT_INIT;
    LOG_WARN("ObTmpFileManager has not been inited", KR(ret));
  } else {
    if (OB_FAIL(get_sn_file_manager().read(io_info, io_handle))) {
      LOG_WARN("fail to read file in sn tmp file manager", KR(ret), K(io_info));
    }
  }
  return ret;
}

int ObTmpFileManager::pread(const ObTmpFileIOInfo &io_info,
                                  const int64_t offset, ObTmpFileIOHandle &io_handle)
{
  int ret = OB_SUCCESS;
  if (IS_NOT_INIT) {
    ret = OB_NOT_INIT;
    LOG_WARN("ObTmpFileManager has not been inited", KR(ret));
  } else {
    if (OB_FAIL(get_sn_file_manager().pread(io_info, offset, io_handle))) {
      LOG_WARN("fail to read file in sn tmp file manager", KR(ret), K(io_info), K(offset));
    }
  }
  return ret;
}

int ObTmpFileManager::aio_write(const ObTmpFileIOInfo &io_info,
                                      ObTmpFileIOHandle &io_handle)
{
  int ret = OB_SUCCESS;
  if (IS_NOT_INIT) {
    ret = OB_NOT_INIT;
    LOG_WARN("ObTmpFileManager has not been inited", KR(ret));
  } else {
    if (OB_FAIL(get_sn_file_manager().aio_write(io_info, io_handle))) {
      LOG_WARN("fail to write file in sn tmp file manager", KR(ret), K(io_info));
    }
  }
  return ret;
}

int ObTmpFileManager::write(const ObTmpFileIOInfo &io_info)
{
  int ret = OB_SUCCESS;
  if (IS_NOT_INIT) {
    ret = OB_NOT_INIT;
    LOG_WARN("ObTmpFileManager has not been inited", KR(ret));
  } else {
    if (OB_FAIL(get_sn_file_manager().write(io_info))) {
      LOG_WARN("fail to write file in sn tmp file manager", KR(ret), K(io_info));
    }
  }
  return ret;
}

int ObTmpFileManager::truncate(const int64_t fd, const int64_t offset)
{
  int ret = OB_SUCCESS;
  if (IS_NOT_INIT) {
    ret = OB_NOT_INIT;
    LOG_WARN("ObTmpFileManager has not been inited", KR(ret));
  } else {
    if (OB_FAIL(get_sn_file_manager().truncate(fd, offset))) {
      LOG_WARN("fail to truncate file in sn tmp file manager", KR(ret), K(fd), K(offset));
    }
  }
  return ret;
}

int ObTmpFileManager::seal(const int64_t fd)
{
  int ret = OB_SUCCESS;
  if (IS_NOT_INIT) {
    ret = OB_NOT_INIT;
    LOG_WARN("ObTmpFileManager has not been inited", KR(ret));
  } else {
    if (OB_FAIL(sn_file_manager_.seal(fd))) {
      LOG_WARN("fail to seal file in sn tmp file manager", KR(ret), K(fd));
    }
  }
  return ret;
}

int ObTmpFileManager::get_tmp_file_size(const int64_t fd, int64_t &file_size)
{
  int ret = OB_SUCCESS;
  if (IS_NOT_INIT) {
    ret = OB_NOT_INIT;
    LOG_WARN("ObTmpFileManager has not been inited", KR(ret));
  } else {
    if (OB_FAIL(get_sn_file_manager().get_tmp_file_size(fd, file_size))) {
      LOG_WARN("fail to get tmp file size in sn tmp file manager", KR(ret), K(fd));
    }
  }
  return ret;
}

int ObTmpFileManager::get_tmp_file(const int64_t fd, ObITmpFileHandle &handle)
{
  int ret = OB_SUCCESS;
  if (IS_NOT_INIT) {
    ret = OB_NOT_INIT;
    LOG_WARN("ObTmpFileManager has not been inited", KR(ret));
  } else {
    if (OB_FAIL(get_sn_file_manager().ObITmpFileManager::get_tmp_file(fd, handle))) {
      LOG_WARN("fail to get tmp file in sn tmp file manager", KR(ret), K(fd));
    }
  }
  return ret;
}

int ObTmpFileManager::get_tmp_file_disk_usage(int64_t &disk_data_size, int64_t &occupied_disk_size)
{
  int ret = OB_SUCCESS;
  if (IS_NOT_INIT) {
    ret = OB_NOT_INIT;
    LOG_WARN("ObTmpFileManager has not been inited", KR(ret));
  } else {
    if (OB_FAIL(get_sn_file_manager().get_tmp_file_disk_usage(disk_data_size, occupied_disk_size))) {
      LOG_WARN("fail to get tmp file disk usage in sn tmp file manager",
               KR(ret), K(disk_data_size), K(occupied_disk_size));
    }
  }

  return ret;
}

int ObTmpFileManager::get_tmp_file_fds(ObIArray<int64_t> &fd_arr)
{
  int ret = OB_SUCCESS;
  if (IS_NOT_INIT) {
    ret = OB_NOT_INIT;
    LOG_WARN("ObTmpFileManager has not been inited", KR(ret));
  } else {
    if (OB_FAIL(get_sn_file_manager().get_tmp_file_fds(fd_arr))) {
      LOG_WARN("fail to get tmp file fds in sn tmp file manager", KR(ret), K(fd_arr));
    }
  }

  return ret;
}

int ObTmpFileManager::get_tmp_file_info(const int64_t fd, ObTmpFileInfo *tmp_file_info)
{
  int ret = OB_SUCCESS;

  if (IS_NOT_INIT) {
    ret = OB_NOT_INIT;
    LOG_WARN("ObTmpFileManager has not been inited", KR(ret));
  } else if (OB_ISNULL(tmp_file_info)) {
    ret = OB_INVALID_ARGUMENT;
    LOG_WARN("invalid argument", KR(ret), K(fd), KP(tmp_file_info));
  } else {
    if (OB_FAIL(get_sn_file_manager().get_tmp_file_info(fd, *tmp_file_info))) {
      LOG_WARN("fail to get tmp file info in sn tmp file manager", KR(ret), K(fd));
    }
  }
  return ret;
}

ObServerTmpFileManagerProxy &ObServerTmpFileManagerProxy::get_instance()
{
  static ObServerTmpFileManagerProxy mgr;

  return mgr;
}

int ObServerTmpFileManagerProxy::alloc_dir(int64_t &dir_id)
{
  int ret = OB_SUCCESS;
  ObTmpFileManager *tmp_file_mgr = share::g_mp->tmp_file_manager();
  if (OB_ISNULL(tmp_file_mgr)) {
    ret = OB_ERR_UNEXPECTED;
    LOG_ERROR("tmp file manager is null", KR(ret));
  } else if (OB_FAIL(tmp_file_mgr->alloc_dir(dir_id))) {
    LOG_WARN("fail to alloc dir", KR(ret));
  }
  return ret;
}

int ObServerTmpFileManagerProxy::open(int64_t &fd,
                                       const int64_t &dir_id,
                                       const char* const label)
{
  int ret = OB_SUCCESS;
  ObTmpFileManager *tmp_file_mgr = share::g_mp->tmp_file_manager();
  if (OB_ISNULL(tmp_file_mgr)) {
    ret = OB_ERR_UNEXPECTED;
    LOG_ERROR("tmp file manager is null", KR(ret));
  } else if (OB_FAIL(tmp_file_mgr->open(fd, dir_id, label))) {
    LOG_WARN("fail to open", KR(ret), K(fd), K(dir_id), KP(label));
  }
  return ret;
}

int ObServerTmpFileManagerProxy::remove(const int64_t fd)
{
  int ret = OB_SUCCESS;
  ObTmpFileManager *tmp_file_mgr = share::g_mp->tmp_file_manager();
  if (OB_ISNULL(tmp_file_mgr)) {
    ret = OB_ERR_UNEXPECTED;
    LOG_ERROR("tmp file manager is null", KR(ret));
  } else if (OB_FAIL(tmp_file_mgr->remove(fd))) {
    LOG_WARN("fail to remove", KR(ret));
  }
  return ret;
}

int ObServerTmpFileManagerProxy::aio_read(const ObTmpFileIOInfo &io_info, ObTmpFileIOHandle &io_handle)
{
  int ret = OB_SUCCESS;
  ObTmpFileManager *tmp_file_mgr = share::g_mp->tmp_file_manager();
  if (OB_ISNULL(tmp_file_mgr)) {
    ret = OB_ERR_UNEXPECTED;
    LOG_ERROR("tmp file manager is null", KR(ret));
  } else if (OB_FAIL(tmp_file_mgr->aio_read(io_info, io_handle))) {
    LOG_WARN("fail to aio read", KR(ret), K(io_info));
  }
  return ret;
}

int ObServerTmpFileManagerProxy::aio_pread(const ObTmpFileIOInfo &io_info,
                                            const int64_t offset, ObTmpFileIOHandle &io_handle)
{
  int ret = OB_SUCCESS;
  ObTmpFileManager *tmp_file_mgr = share::g_mp->tmp_file_manager();
  if (OB_ISNULL(tmp_file_mgr)) {
    ret = OB_ERR_UNEXPECTED;
    LOG_ERROR("tmp file manager is null", KR(ret));
  } else if (OB_FAIL(tmp_file_mgr->aio_pread(io_info, offset, io_handle))) {
    LOG_WARN("fail to aio pread", KR(ret), K(io_info), K(offset));
  }
  return ret;
}

int ObServerTmpFileManagerProxy::pread(const ObTmpFileIOInfo &io_info, const int64_t offset, ObTmpFileIOHandle &io_handle)
{
  int ret = OB_SUCCESS;
  ObTmpFileManager *tmp_file_mgr = share::g_mp->tmp_file_manager();
  if (OB_ISNULL(tmp_file_mgr)) {
    ret = OB_ERR_UNEXPECTED;
    LOG_ERROR("tmp file manager is null", KR(ret));
  } else if (OB_FAIL(tmp_file_mgr->pread(io_info, offset, io_handle))) {
    LOG_WARN("fail to pread", KR(ret), K(io_info), K(offset));
  }
  return ret;
}

int ObServerTmpFileManagerProxy::aio_write(const ObTmpFileIOInfo &io_info, ObTmpFileIOHandle &io_handle)
{
  int ret = OB_SUCCESS;
  ObTmpFileManager *tmp_file_mgr = share::g_mp->tmp_file_manager();
  if (OB_ISNULL(tmp_file_mgr)) {
    ret = OB_ERR_UNEXPECTED;
    LOG_ERROR("tmp file manager is null", KR(ret));
  } else if (OB_FAIL(tmp_file_mgr->aio_write(io_info, io_handle))) {
    LOG_WARN("fail to aio write", KR(ret), K(io_info));
  }
  return ret;
}

int ObServerTmpFileManagerProxy::write(const ObTmpFileIOInfo &io_info)
{
  int ret = OB_SUCCESS;
  ObTmpFileManager *tmp_file_mgr = share::g_mp->tmp_file_manager();
  if (OB_ISNULL(tmp_file_mgr)) {
    ret = OB_ERR_UNEXPECTED;
    LOG_ERROR("tmp file manager is null", KR(ret));
  } else if (OB_FAIL(tmp_file_mgr->write(io_info))) {
    LOG_WARN("fail to write", KR(ret), K(io_info));
  }
  return ret;
}

int ObServerTmpFileManagerProxy::truncate(const int64_t fd, const int64_t offset)
{
  int ret = OB_SUCCESS;
  ObTmpFileManager *tmp_file_mgr = share::g_mp->tmp_file_manager();
  if (OB_ISNULL(tmp_file_mgr)) {
    ret = OB_ERR_UNEXPECTED;
    LOG_ERROR("tmp file manager is null", KR(ret));
  } else if (OB_FAIL(tmp_file_mgr->truncate(fd, offset))) {
    LOG_WARN("fail to truncate", KR(ret), K(fd), K(offset));
  }
  return ret;
}

int ObServerTmpFileManagerProxy::seal(const int64_t fd)
{
  int ret = OB_SUCCESS;
  ObTmpFileManager *tmp_file_mgr = share::g_mp->tmp_file_manager();
  if (OB_ISNULL(tmp_file_mgr)) {
    ret = OB_ERR_UNEXPECTED;
    LOG_ERROR("tmp file manager is null", KR(ret));
  } else if (OB_FAIL(tmp_file_mgr->seal(fd))) {
    LOG_WARN("fail to seal", KR(ret), K(fd));
  }
  return ret;
}

int ObServerTmpFileManagerProxy::get_tmp_file_size(const int64_t fd, int64_t &file_size)
{
  int ret = OB_SUCCESS;
  ObTmpFileManager *tmp_file_mgr = share::g_mp->tmp_file_manager();
  if (OB_ISNULL(tmp_file_mgr)) {
    ret = OB_ERR_UNEXPECTED;
    LOG_ERROR("tmp file manager is null", KR(ret));
  } else if (OB_FAIL(tmp_file_mgr->get_tmp_file_size(fd, file_size))) {
    LOG_WARN("fail to get tmp file size", KR(ret), K(fd));
  }
  return ret;
}

int ObServerTmpFileManagerProxy::get_tmp_file_fds(ObIArray<int64_t> &fd_arr)
{
  int ret = OB_SUCCESS;
  ObTmpFileManager *tmp_file_mgr = share::g_mp->tmp_file_manager();
  if (OB_ISNULL(tmp_file_mgr)) {
    ret = OB_ERR_UNEXPECTED;
    LOG_ERROR("tmp file manager is null", KR(ret));
  } else if (OB_FAIL(tmp_file_mgr->get_tmp_file_fds(fd_arr))) {
    LOG_WARN("fail to get tmp file fds", KR(ret));
  }
  return ret;
}

int ObServerTmpFileManagerProxy::get_tmp_file_info(const int64_t fd, ObTmpFileInfo *tmp_file_info)
{
  int ret = OB_SUCCESS;
  ObTmpFileManager *tmp_file_mgr = share::g_mp->tmp_file_manager();
  if (OB_ISNULL(tmp_file_info)) {
    ret = OB_INVALID_ARGUMENT;
    LOG_WARN("invalid argument", KR(ret), K(fd), KP(tmp_file_info));
  } else if (OB_ISNULL(tmp_file_mgr)) {
    ret = OB_ERR_UNEXPECTED;
    LOG_ERROR("tmp file manager is null", KR(ret));
  } else if (OB_FAIL(tmp_file_mgr->get_tmp_file_info(fd, tmp_file_info))) {
    LOG_WARN("fail to get tmp file info", KR(ret), K(fd));
  }
  return ret;
}

}  // end namespace tmp_file
}  // end namespace oceanbase
