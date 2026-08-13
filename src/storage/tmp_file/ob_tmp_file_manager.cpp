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
#include "data_plane/tmp_file/ob_tmp_file.h"
#include "share/rc/ob_server_runtime.h"
#include "storage/tmp_file/ob_tmp_file_manager.h"

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
    }
  }
  return ret;
}

}  // end namespace tmp_file
}  // end namespace oceanbase

namespace oceanbase
{
namespace data_plane
{

using ObTmpFileIOHandleImpl = tmp_file::ObTmpFileIOHandle;

class ObTmpFileAccess
{
public:
  static ObTmpFileIOHandleImpl *get(const ObTmpFileIOHandle &handle)
  {
    return static_cast<ObTmpFileIOHandleImpl *>(handle.impl_);
  }

  static int ensure(ObTmpFileIOHandle &handle, ObTmpFileIOHandleImpl *&impl)
  {
    int ret = OB_SUCCESS;
    impl = get(handle);
    if (OB_ISNULL(impl)) {
      impl = OB_NEW(ObTmpFileIOHandleImpl, common::ObMemAttr("QryTmpFileIO"));
      if (OB_ISNULL(impl)) {
        ret = OB_ALLOCATE_MEMORY_FAILED;
      } else {
        handle.impl_ = impl;
      }
    }
    return ret;
  }

  static void destroy(ObTmpFileIOHandle &handle)
  {
    ObTmpFileIOHandleImpl *impl = get(handle);
    if (OB_NOT_NULL(impl)) {
      OB_DELETE(ObTmpFileIOHandleImpl, common::ObMemAttr("QryTmpFileIO"), impl);
      handle.impl_ = nullptr;
    }
  }
};

namespace
{
tmp_file::ObTmpFileManager *get_tmp_file_manager()
{
  return ::oceanbase::share::server_service<::oceanbase::tmp_file::ObTmpFileManager>();
}

void translate_io_info(const ObTmpFileIOInfo &from, tmp_file::ObTmpFileIOInfo &to)
{
  to.fd_ = from.fd_;
  to.dir_id_ = from.dir_id_;
  to.buf_ = from.buf_;
  to.size_ = from.size_;
  to.disable_page_cache_ = from.disable_page_cache_;
  to.disable_block_cache_ = from.disable_block_cache_;
  to.prefetch_ = from.prefetch_;
  to.io_desc_ = from.io_desc_;
  to.io_timeout_ms_ = from.io_timeout_ms_;
}

typedef int (tmp_file::ObTmpFileManager::*TmpFileIOFn)(
    const tmp_file::ObTmpFileIOInfo &, tmp_file::ObTmpFileIOHandle &);

int submit_io(const ObTmpFileIOInfo &io_info,
              ObTmpFileIOHandle &io_handle,
              TmpFileIOFn operation)
{
  int ret = OB_SUCCESS;
  tmp_file::ObTmpFileIOHandle *impl = nullptr;
  if (OB_FAIL(ObTmpFileAccess::ensure(io_handle, impl))) {
  } else if (OB_ISNULL(get_tmp_file_manager())) {
    ret = OB_NOT_INIT;
    LOG_WARN("tmp file manager is not initialized", KR(ret));
  } else {
    tmp_file::ObTmpFileIOInfo impl_info;
    translate_io_info(io_info, impl_info);
    ret = (get_tmp_file_manager()->*operation)(impl_info, *impl);
  }
  return ret;
}
} // namespace

ObTmpFileIOInfo::ObTmpFileIOInfo()
  : fd_(tmp_file::ObTmpFileGlobal::INVALID_TMP_FILE_FD),
    dir_id_(0),
    buf_(nullptr),
    size_(0),
    disable_page_cache_(false),
    disable_block_cache_(false),
    prefetch_(false),
    io_desc_(),
    io_timeout_ms_(DEFAULT_IO_WAIT_TIME_MS)
{
}

void ObTmpFileIOInfo::reset()
{
  fd_ = tmp_file::ObTmpFileGlobal::INVALID_TMP_FILE_FD;
  dir_id_ = 0;
  buf_ = nullptr;
  size_ = 0;
  disable_page_cache_ = false;
  disable_block_cache_ = false;
  prefetch_ = false;
  io_desc_.reset();
  io_timeout_ms_ = DEFAULT_IO_WAIT_TIME_MS;
}

bool ObTmpFileIOInfo::is_valid() const
{
  return fd_ != tmp_file::ObTmpFileGlobal::INVALID_TMP_FILE_FD
      && dir_id_ != tmp_file::ObTmpFileGlobal::INVALID_TMP_FILE_DIR_ID
      && size_ > 0
      && OB_NOT_NULL(buf_)
      && io_desc_.is_valid()
      && io_timeout_ms_ >= 0;
}

ObTmpFileIOHandle::ObTmpFileIOHandle() : impl_(nullptr)
{
}

ObTmpFileIOHandle::~ObTmpFileIOHandle()
{
  ObTmpFileAccess::destroy(*this);
}

void ObTmpFileIOHandle::reset()
{
  tmp_file::ObTmpFileIOHandle *impl = ObTmpFileAccess::get(*this);
  if (OB_NOT_NULL(impl)) {
    impl->reset();
  }
}

int ObTmpFileIOHandle::wait()
{
  tmp_file::ObTmpFileIOHandle *impl = ObTmpFileAccess::get(*this);
  return OB_ISNULL(impl) ? OB_NOT_INIT : impl->wait();
}

bool ObTmpFileIOHandle::is_valid() const
{
  const tmp_file::ObTmpFileIOHandle *impl = ObTmpFileAccess::get(*this);
  return OB_NOT_NULL(impl) && impl->is_valid();
}

char *ObTmpFileIOHandle::get_buffer()
{
  tmp_file::ObTmpFileIOHandle *impl = ObTmpFileAccess::get(*this);
  return OB_ISNULL(impl) ? nullptr : impl->get_buffer();
}

int64_t ObTmpFileIOHandle::get_done_size() const
{
  const tmp_file::ObTmpFileIOHandle *impl = ObTmpFileAccess::get(*this);
  return OB_ISNULL(impl) ? -1 : impl->get_done_size();
}

int64_t ObTmpFileIOHandle::get_buf_size() const
{
  const tmp_file::ObTmpFileIOHandle *impl = ObTmpFileAccess::get(*this);
  return OB_ISNULL(impl) ? -1 : impl->get_buf_size();
}

bool ObTmpFileIOHandle::is_finished() const
{
  const tmp_file::ObTmpFileIOHandle *impl = ObTmpFileAccess::get(*this);
  return OB_NOT_NULL(impl) && impl->is_finished();
}

int tmp_file_alloc_dir(int64_t &dir_id)
{
  tmp_file::ObTmpFileManager *manager = get_tmp_file_manager();
  return OB_ISNULL(manager) ? OB_NOT_INIT : manager->alloc_dir(dir_id);
}

int tmp_file_open(int64_t &fd, int64_t dir_id, const char *label)
{
  tmp_file::ObTmpFileManager *manager = get_tmp_file_manager();
  return OB_ISNULL(manager) ? OB_NOT_INIT : manager->open(fd, dir_id, label);
}

int tmp_file_remove(int64_t fd)
{
  tmp_file::ObTmpFileManager *manager = get_tmp_file_manager();
  return OB_ISNULL(manager) ? OB_NOT_INIT : manager->remove(fd);
}

int tmp_file_aio_read(const ObTmpFileIOInfo &io_info, ObTmpFileIOHandle &io_handle)
{
  return submit_io(io_info, io_handle,
                   &tmp_file::ObTmpFileManager::aio_read);
}

int tmp_file_aio_pread(const ObTmpFileIOInfo &io_info,
                       int64_t offset,
                       ObTmpFileIOHandle &io_handle)
{
  int ret = OB_SUCCESS;
  tmp_file::ObTmpFileIOHandle *impl = nullptr;
  if (OB_FAIL(ObTmpFileAccess::ensure(io_handle, impl))) {
  } else if (OB_ISNULL(get_tmp_file_manager())) {
    ret = OB_NOT_INIT;
    LOG_WARN("tmp file manager is not initialized", KR(ret));
  } else {
    tmp_file::ObTmpFileIOInfo impl_info;
    translate_io_info(io_info, impl_info);
    ret = get_tmp_file_manager()->aio_pread(impl_info, offset, *impl);
  }
  return ret;
}

int tmp_file_pread(const ObTmpFileIOInfo &io_info,
                   int64_t offset,
                   ObTmpFileIOHandle &io_handle)
{
  int ret = OB_SUCCESS;
  tmp_file::ObTmpFileIOHandle *impl = nullptr;
  if (OB_FAIL(ObTmpFileAccess::ensure(io_handle, impl))) {
  } else if (OB_ISNULL(get_tmp_file_manager())) {
    ret = OB_NOT_INIT;
    LOG_WARN("tmp file manager is not initialized", KR(ret));
  } else {
    tmp_file::ObTmpFileIOInfo impl_info;
    translate_io_info(io_info, impl_info);
    ret = get_tmp_file_manager()->pread(impl_info, offset, *impl);
  }
  return ret;
}

int tmp_file_aio_write(const ObTmpFileIOInfo &io_info, ObTmpFileIOHandle &io_handle)
{
  return submit_io(io_info, io_handle,
                   &tmp_file::ObTmpFileManager::aio_write);
}

int tmp_file_write(const ObTmpFileIOInfo &io_info)
{
  tmp_file::ObTmpFileIOInfo impl_info;
  translate_io_info(io_info, impl_info);
  tmp_file::ObTmpFileManager *manager = get_tmp_file_manager();
  return OB_ISNULL(manager) ? OB_NOT_INIT : manager->write(impl_info);
}

int tmp_file_truncate(int64_t fd, int64_t offset)
{
  tmp_file::ObTmpFileManager *manager = get_tmp_file_manager();
  return OB_ISNULL(manager) ? OB_NOT_INIT : manager->truncate(fd, offset);
}

int tmp_file_seal(int64_t fd)
{
  tmp_file::ObTmpFileManager *manager = get_tmp_file_manager();
  return OB_ISNULL(manager) ? OB_NOT_INIT : manager->seal(fd);
}

int tmp_file_get_size(int64_t fd, int64_t &file_size)
{
  tmp_file::ObTmpFileManager *manager = get_tmp_file_manager();
  return OB_ISNULL(manager) ? OB_NOT_INIT : manager->get_tmp_file_size(fd, file_size);
}

} // namespace data_plane
} // namespace oceanbase
