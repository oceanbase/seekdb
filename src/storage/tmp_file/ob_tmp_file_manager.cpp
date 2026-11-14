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
#include "data_plane/tmp_file/ob_tmp_file.h"
#include "lib/utility/ob_platform_utils.h"

#ifdef _WIN32
#include <windows.h>
#define sleep(sec) Sleep((sec) * 1000)
#endif

namespace oceanbase
{
namespace tmp_file
{
ObTmpFileManager::ObTmpFileManager()
  : is_inited_(false),
    is_running_(false),
    tmp_file_allocator_(),
    callback_allocator_(),
    wbp_index_cache_allocator_(),
    wbp_index_cache_bucket_allocator_(),
    files_(),
    tmp_file_block_manager_(),
    page_cache_controller_(tmp_file_block_manager_)
{
}

ObTmpFileManager::~ObTmpFileManager()
{
  destroy();
}

int ObTmpFileManager::init()
{
  int ret = OB_SUCCESS;
  if (OB_UNLIKELY(IS_INIT)) {
    ret = OB_INIT_TWICE;
  } else if (OB_FAIL(files_.init("TmpFileMap"))) {
  } else if (OB_FAIL(tmp_file_allocator_.init(common::OB_MALLOC_MIDDLE_BLOCK_SIZE,
                                              ObModIds::OB_TMP_FILE_MANAGER,
                                              INT64_MAX))) {
  } else if (OB_FAIL(callback_allocator_.init(lib::ObMallocAllocator::get_instance(),
                                              OB_MALLOC_MIDDLE_BLOCK_SIZE,
                                              ObMemAttr("TmpFileCallback", ObCtxIds::DEFAULT_CTX_ID)))) {
  } else if (OB_FAIL(wbp_index_cache_allocator_.init(lib::ObMallocAllocator::get_instance(),
                                                     OB_MALLOC_NORMAL_BLOCK_SIZE,
                                                     ObMemAttr("TmpFileIndCache",
                                                     ObCtxIds::DEFAULT_CTX_ID)))) {
  } else if (OB_FAIL(wbp_index_cache_bucket_allocator_.init(lib::ObMallocAllocator::get_instance(),
                                                            OB_MALLOC_MIDDLE_BLOCK_SIZE,
                                                            ObMemAttr("TmpFileIndCBkt",
                                                            ObCtxIds::DEFAULT_CTX_ID)))) {
  } else if (OB_FAIL(init_sub_module_())) {
  } else {
    is_inited_ = true;
  }

  return ret;
}

int ObTmpFileManager::start()
{
  int ret = OB_SUCCESS;
  if (IS_NOT_INIT) {
    ret = OB_NOT_INIT;
  } else if (OB_UNLIKELY(is_running())) {
    ret = OB_ERR_UNEXPECTED;
  } else if (OB_FAIL(start_sub_module_())) {
  } else {
    is_running_ = true;
  }

  return ret;
}

void ObTmpFileManager::stop()
{
  int ret = OB_SUCCESS;
  if (is_running_) {
    if (OB_FAIL(stop_sub_module_())) {
    } else {
      is_running_ = false;
    }
  }
}

void ObTmpFileManager::wait()
{
  int ret = OB_SUCCESS;
  if (OB_FAIL(wait_sub_module_())) {
  }
}

void ObTmpFileManager::destroy()
{
  int ret = OB_SUCCESS;
  if (IS_INIT) {
    if (OB_FAIL(destroy_sub_module_())) {
    } else {
      int64_t curr_file_cnt = files_.count();
      if (OB_UNLIKELY(curr_file_cnt > 0)) {
        int ret = OB_SUCCESS;
        TmpFileMap::BlurredIterator iter(files_);
        while (OB_SUCC(ret)) {
          ObTmpFileKey unused_key(ObTmpFileGlobal::INVALID_TMP_FILE_FD);
          ObSNTmpFileHandle handle;
          if (OB_FAIL(iter.next(unused_key, handle))) {
            if (OB_ITER_END == ret) {
              ret = OB_SUCCESS;
              break;
            } else {
            }
          } else {
            // resource leak
            LOG_ERROR("the tmp file has not been removed when tmp file mgr is destroying", KPC(handle.get()));
          }
        } // end while

        int64_t new_file_cnt = files_.count();
        if (OB_UNLIKELY(new_file_cnt != curr_file_cnt)) {
          LOG_ERROR("there are some operation for tmp files when tmp file mgr is destroying", K(curr_file_cnt));
        }
      }
      files_.destroy();
      tmp_file_allocator_.reset();
      callback_allocator_.reset();
      wbp_index_cache_allocator_.reset();
      wbp_index_cache_bucket_allocator_.reset();
      is_inited_ = false;
    }
  }

  LOG_INFO("ObTmpFileManager destroy", KP(this));
}

int ObTmpFileManager::remove(const int64_t fd)
{
  int ret = OB_SUCCESS;
  ObSNTmpFileHandle tmp_file_handle;
  int64_t start_remove_ts = ObTimeUtility::current_time();
  LOG_INFO("remove a tmp file start", KR(ret), K(start_remove_ts), K(fd), K(lbt()));

  if (IS_NOT_INIT) {
    ret = OB_NOT_INIT;
  // } else if (OB_UNLIKELY(!is_running())) {
  //   // some modules remove tmp file when they are destroying.
  //   // at this time, the tmp file mgr is not running because of stop().
  //   // thus, we need to support this case
  //   ret = OB_ERR_UNEXPECTED;
  //   LOG_WARN("ObTmpFileManager is not running", KR(ret), K(is_running_));
  } else if (OB_FAIL(files_.erase(ObTmpFileKey(fd), tmp_file_handle))) {
    if (OB_ENTRY_NOT_EXIST == ret) {
      ret = OB_SUCCESS;
      LOG_INFO("erase non-exist tmp file", K(fd), K(lbt()));
    } else {
    }
  } else if (OB_ISNULL(tmp_file_handle.get())) {
    ret = OB_ERR_UNEXPECTED;
    LOG_WARN("unexpected null", KR(ret), KP(tmp_file_handle.get()), K(fd));
  } else if (OB_FAIL(tmp_file_handle.get()->delete_file())) {
  } else {
    ObSharedNothingTmpFile *tmp_file = tmp_file_handle.get();

    int64_t LOG_WARN_TIMEOUT = 10 * 1000 * 1000;
    int64_t LOG_ERROR_TIMEOUT = 60 * 1000 * 1000;
    while(!tmp_file->can_remove())
    {
      if (start_remove_ts + LOG_ERROR_TIMEOUT < ObTimeUtility::current_time()) {
        LOG_ERROR("wait thread release reference too long",
            K(start_remove_ts), KP(tmp_file), KPC(tmp_file), K(lbt()));
        sleep(10); // 10s
      } else if (start_remove_ts + LOG_WARN_TIMEOUT < ObTimeUtility::current_time()) {
        LOG_WARN("wait thread release reference too long",
            K(start_remove_ts), KP(tmp_file), KPC(tmp_file), K(lbt()));
        sleep(2); // 2s
      } else {
        lib::ob_usleep(100 * 1000); // 100ms
      }
    }
    if (OB_FAIL(tmp_file->release_resource())) {
    } else {
      tmp_file_handle.reset();
      tmp_file_allocator_.free(tmp_file);
    }
  }

  LOG_INFO("remove a tmp file over", KR(ret), K(start_remove_ts), K(fd), K(lbt()));
  return ret;
}

int ObTmpFileManager::aio_read(const ObTmpFileIOInfo &io_info,
                                      ObTmpFileIOHandle &io_handle)
{
  int ret = OB_SUCCESS;
  ObSNTmpFileHandle tmp_file_handle;

  if (IS_NOT_INIT) {
    ret = OB_NOT_INIT;
  } else if (OB_UNLIKELY(!io_info.is_valid())) {
    ret = OB_INVALID_ARGUMENT;
  } else if (OB_UNLIKELY(io_handle.is_valid() && !io_handle.is_finished())) {
    ret = OB_INVALID_ARGUMENT;
  } else if (FALSE_IT(io_handle.reset())) {
  } else if (OB_FAIL(get_tmp_file(io_info.fd_, tmp_file_handle))) {
  } else if (OB_FAIL(io_handle.init_read(io_info))) {
  } else if (OB_FAIL(tmp_file_handle.get()->aio_pread(io_handle.get_io_ctx()))) {
  } else {
    tmp_file_handle.get()->set_read_stats_vars(io_handle.get_io_ctx(), io_info.size_);
  }

  LOG_DEBUG("aio_read a tmp file over", KR(ret), K(io_info), K(io_handle), KPC(tmp_file_handle.get()));
  return ret;
}

int ObTmpFileManager::aio_pread(const ObTmpFileIOInfo &io_info,
                                       const int64_t offset,
                                       ObTmpFileIOHandle &io_handle)
{
  int ret = OB_SUCCESS;
  ObSNTmpFileHandle tmp_file_handle;

  if (IS_NOT_INIT) {
    ret = OB_NOT_INIT;
  } else if (OB_UNLIKELY(!io_info.is_valid())) {
    ret = OB_INVALID_ARGUMENT;
  } else if (OB_UNLIKELY(io_handle.is_valid() && !io_handle.is_finished())) {
    ret = OB_INVALID_ARGUMENT;
  } else if (FALSE_IT(io_handle.reset())) {
  } else if (OB_FAIL(get_tmp_file(io_info.fd_, tmp_file_handle))) {
  } else if (OB_FAIL(io_handle.init_pread( io_info, offset))) {
  } else if (OB_FAIL(tmp_file_handle.get()->aio_pread(io_handle.get_io_ctx()))) {
  } else {
    tmp_file_handle.get()->set_read_stats_vars(io_handle.get_io_ctx(), io_info.size_);
  }

  LOG_DEBUG("aio_pread a tmp file over", KR(ret), K(io_info), K(offset), K(io_handle), KPC(tmp_file_handle.get()));
  return ret;
}

int ObTmpFileManager::read(const ObTmpFileIOInfo &io_info,
                                  ObTmpFileIOHandle &io_handle)
{
  int ret = OB_SUCCESS;
  ObSNTmpFileHandle tmp_file_handle;

  if (IS_NOT_INIT) {
    ret = OB_NOT_INIT;
  } else if (OB_UNLIKELY(!io_info.is_valid())) {
    ret = OB_INVALID_ARGUMENT;
  } else if (OB_UNLIKELY(io_handle.is_valid() && !io_handle.is_finished())) {
    ret = OB_INVALID_ARGUMENT;
  } else if (FALSE_IT(io_handle.reset())) {
  } else if (OB_FAIL(get_tmp_file(io_info.fd_, tmp_file_handle))) {
  } else if (OB_FAIL(io_handle.init_read(io_info))) {
  } else if (OB_FAIL(tmp_file_handle.get()->aio_pread(io_handle.get_io_ctx()))) {
  } else {
    tmp_file_handle.get()->set_read_stats_vars(io_handle.get_io_ctx(), io_info.size_);
  }

  if (OB_SUCC(ret) || OB_ITER_END == ret) {
    int tmp_ret = OB_SUCCESS;
    if (OB_TMP_FAIL(io_handle.wait())) {
    }
    ret = OB_SUCCESS == ret ? tmp_ret : ret;
  }

  LOG_DEBUG("read a tmp file over", KR(ret), K(io_info), K(io_handle), KPC(tmp_file_handle.get()));
  return ret;
}

int ObTmpFileManager::pread(const ObTmpFileIOInfo &io_info,
                                   const int64_t offset,
                                   ObTmpFileIOHandle &io_handle)
{
  int ret = OB_SUCCESS;
  ObSNTmpFileHandle tmp_file_handle;

  if (IS_NOT_INIT) {
    ret = OB_NOT_INIT;
  } else if (OB_UNLIKELY(!io_info.is_valid())) {
    ret = OB_INVALID_ARGUMENT;
  } else if (OB_UNLIKELY(io_handle.is_valid() && !io_handle.is_finished())) {
    ret = OB_INVALID_ARGUMENT;
  } else if (FALSE_IT(io_handle.reset())) {
  } else if (OB_FAIL(get_tmp_file(io_info.fd_, tmp_file_handle))) {
  } else if (OB_FAIL(io_handle.init_pread( io_info, offset))) {
  } else if (OB_FAIL(tmp_file_handle.get()->aio_pread(io_handle.get_io_ctx()))) {
  } else {
    tmp_file_handle.get()->set_read_stats_vars(io_handle.get_io_ctx(), io_info.size_);
  }

  if (OB_SUCC(ret) || OB_ITER_END == ret) {
    int tmp_ret = OB_SUCCESS;
    if (OB_TMP_FAIL(io_handle.wait())) {
    }
    ret = OB_SUCCESS == ret ? tmp_ret : ret;
  }

  LOG_DEBUG("pread a tmp file over", KR(ret), K(io_info), K(offset), K(io_handle), KPC(tmp_file_handle.get()));
  return ret;
}

int ObTmpFileManager::aio_write(const ObTmpFileIOInfo &io_info,
                                       ObTmpFileIOHandle &io_handle)
{
  int ret = OB_SUCCESS;
  ObSNTmpFileHandle tmp_file_handle;
  io_handle.reset();

  if (IS_NOT_INIT) {
    ret = OB_NOT_INIT;
  } else if (OB_UNLIKELY(!io_info.is_valid())) {
    ret = OB_INVALID_ARGUMENT;
  } else if (OB_FAIL(get_tmp_file(io_info.fd_, tmp_file_handle))) {
  } else if (OB_FAIL(io_handle.init_write(io_info))) {
  } else if (OB_FAIL(tmp_file_handle.get()->aio_write(io_handle.get_io_ctx()))) {
  } else {
    tmp_file_handle.get()->set_write_stats_vars(io_handle.get_io_ctx());
  }

  LOG_DEBUG("aio_write a tmp file over", KR(ret), K(io_info), K(io_handle), KPC(tmp_file_handle.get()));
  return ret;
}

// tmp file is always buffer writing, there are no io tasks need to be waited
int ObTmpFileManager::write(const ObTmpFileIOInfo &io_info)
{
  int ret = OB_SUCCESS;
  ObTmpFileIOHandle io_handle;

  if (OB_FAIL(aio_write(io_info, io_handle))) {
  }

  return ret;
}

int ObTmpFileManager::truncate(const int64_t fd, const int64_t offset)
{
  int ret = OB_SUCCESS;
  ObSNTmpFileHandle tmp_file_handle;

  if (IS_NOT_INIT) {
    ret = OB_NOT_INIT;
  } else if (OB_UNLIKELY(fd == ObTmpFileGlobal::INVALID_TMP_FILE_FD || offset < 0)) {
    ret = OB_INVALID_ARGUMENT;
  } else if (OB_FAIL(get_tmp_file(fd, tmp_file_handle))) {
  } else if (OB_FAIL(tmp_file_handle.get()->truncate(offset))) {
  } else {
  }
  return ret;
}

int ObTmpFileManager::seal(const int64_t fd)
{
  int ret = OB_SUCCESS;
  ObSNTmpFileHandle tmp_file_handle;

  if (IS_NOT_INIT) {
    ret = OB_NOT_INIT;
  } else if (OB_FAIL(get_tmp_file(fd, tmp_file_handle))) {
  } else if (OB_FAIL(tmp_file_handle.get()->seal())) {
  } else {
    LOG_INFO("seal a tmp file over", KR(ret), K(fd));
  }
  return ret;
}

// Get tmp file and increase its refcnt
int ObTmpFileManager::get_tmp_file(const int64_t fd, ObSNTmpFileHandle &file_handle)
{
  int ret = OB_SUCCESS;

  if (IS_NOT_INIT) {
    ret = OB_NOT_INIT;
  } else if (OB_UNLIKELY(fd == ObTmpFileGlobal::INVALID_TMP_FILE_FD)) {
    ret = OB_INVALID_ARGUMENT;
  } else if (OB_FAIL(files_.get(ObTmpFileKey(fd), file_handle))) {
    if (OB_ENTRY_NOT_EXIST == ret) {
    } else {
    }
  } else if (OB_ISNULL(file_handle.get())) {
    ret = OB_ERR_UNEXPECTED;
    LOG_WARN("get invalid tmp file pointer", KR(ret), K(fd), KP(file_handle.get()));
  }

  return ret;
}

int ObTmpFileManager::get_tmp_file_size(const int64_t fd, int64_t &size)
{
  int ret = OB_SUCCESS;
  ObSNTmpFileHandle tmp_file_handle;
  if (IS_NOT_INIT) {
    ret = OB_NOT_INIT;
  } else if (OB_FAIL(get_tmp_file(fd, tmp_file_handle))) {
  } else {
    size = tmp_file_handle.get()->get_file_size();
  }

  return ret;
}

bool ObTmpFileManager::CollectTmpFileKeyFunctor::operator()(
     const ObTmpFileKey &key, const ObSNTmpFileHandle &tmp_file_handle)
{
  int ret = OB_SUCCESS;
  ObSharedNothingTmpFile *tmp_file_ptr = NULL;

  if (OB_ISNULL(tmp_file_handle.get())) {
    ret = OB_ERR_UNEXPECTED;
    LOG_WARN("get invalid tmp file pointer", KR(ret), K(key), KP(tmp_file_handle.get()));
  } else if (OB_FAIL(fds_.push_back(key.fd_))) {
  }
  return OB_SUCCESS == ret;
}

int ObTmpFileManager::get_tmp_file_fds(ObIArray<int64_t> &fd_arr)
{
  int ret = OB_SUCCESS;
  CollectTmpFileKeyFunctor func(fd_arr);
  if (IS_NOT_INIT) {
    ret = OB_NOT_INIT;
  } else if (OB_FAIL(files_.for_each(func))) {
  }

  return ret;
}

int ObTmpFileManager::get_tmp_file_info(const int64_t fd, ObTmpFileInfo *tmp_file_info)
{
  int ret = OB_SUCCESS;
  ObSNTmpFileHandle file_handle;
  if (IS_NOT_INIT) {
    ret = OB_NOT_INIT;
  } else if (OB_ISNULL(tmp_file_info)) {
    ret = OB_INVALID_ARGUMENT;
  } else if (OB_FAIL(get_tmp_file(fd, file_handle))) {
    if (OB_ENTRY_NOT_EXIST == ret) {
      LOG_INFO("tmp file not exist", KR(ret), K(fd));
    } else {
    }
  } else if (OB_ISNULL(file_handle.get())) {
    ret = OB_ERR_UNEXPECTED;
    LOG_WARN("get invalid tmp file pointer", KR(ret), K(fd), KP(file_handle.get()));
  } else if (OB_FAIL(file_handle.get()->copy_info_for_virtual_table(*tmp_file_info))) {
  }

  return ret;
}

int64_t ObTmpFileManager::current_fd_ = ObTmpFileGlobal::INVALID_TMP_FILE_FD;
int64_t ObTmpFileManager::current_dir_id_ = ObTmpFileGlobal::INVALID_TMP_FILE_DIR_ID;

int ObTmpFileManager::init_sub_module_()
{
  int ret = OB_SUCCESS;

  if (OB_FAIL(tmp_file_block_manager_.init())) {
  } else if (OB_FAIL(page_cache_controller_.init())) {
  } else {
    LOG_INFO("ObTmpFileManager init successful", KP(this));
  }

  return ret;
}

int ObTmpFileManager::start_sub_module_()
{
  int ret = OB_SUCCESS;
  if (OB_FAIL(page_cache_controller_.start())) {
    ret = OB_ERR_UNEXPECTED;
  } else {
    is_running_ = true;
    LOG_INFO("ObTmpFileManager start successful", KP(this));
  }
  return ret;
}

int ObTmpFileManager::stop_sub_module_()
{
  int ret = OB_SUCCESS;
  page_cache_controller_.stop();
  LOG_INFO("ObTmpFileManager stop successful", KP(this));
  return ret;
}

int ObTmpFileManager::wait_sub_module_()
{
  int ret = OB_SUCCESS;
  page_cache_controller_.wait();
  LOG_INFO("ObTmpFileManager wait successful", KP(this));
  return ret;
}

int ObTmpFileManager::destroy_sub_module_()
{
  int ret = OB_SUCCESS;
  page_cache_controller_.destroy();
  tmp_file_block_manager_.destroy();

  LOG_INFO("ObTmpFileManager destroy", KP(this));
  return ret;
}

int ObTmpFileManager::alloc_dir(int64_t &dir_id)
{
  int ret = OB_SUCCESS;
  dir_id = ObTmpFileGlobal::INVALID_TMP_FILE_DIR_ID;

  if (IS_NOT_INIT) {
    ret = OB_NOT_INIT;
  } else if (OB_UNLIKELY(!is_running())) {
    ret = OB_ERR_UNEXPECTED;
  } else {
    dir_id = ATOMIC_AAF(&current_dir_id_, 1);
  }

  LOG_DEBUG("alloc dir over", KR(ret), K(dir_id), K(lbt()));
  return ret;
}

int ObTmpFileManager::open(int64_t &fd, const int64_t &dir_id, const char* const label)
{
  int ret = OB_SUCCESS;
  fd = ObTmpFileGlobal::INVALID_TMP_FILE_FD;
  void *buf = nullptr;
  ObSharedNothingTmpFile *tmp_file = nullptr;

  if (IS_NOT_INIT) {
    ret = OB_NOT_INIT;
  } else if (OB_UNLIKELY(!is_running())) {
    ret = OB_ERR_UNEXPECTED;
  } else if (OB_ISNULL(buf = tmp_file_allocator_.alloc(sizeof(ObSharedNothingTmpFile),
                                                       lib::ObMemAttr("SNTmpFile")))) {
    ret = OB_ALLOCATE_MEMORY_FAILED;
  } else if (FALSE_IT(tmp_file = new (buf) ObSharedNothingTmpFile())) {
  } else if (FALSE_IT(fd = ATOMIC_AAF(&current_fd_, 1))) {
  } else if (OB_FAIL(tmp_file->init(fd, dir_id,
                                    &tmp_file_block_manager_, &callback_allocator_,
                                    &wbp_index_cache_allocator_, &wbp_index_cache_bucket_allocator_,
                                    &page_cache_controller_, label))) {
  } else if (OB_FAIL(files_.insert(ObTmpFileKey(fd), tmp_file))) {
  }

  if (OB_FAIL(ret) && OB_NOT_NULL(tmp_file)) {
    tmp_file->~ObSharedNothingTmpFile();
    tmp_file_allocator_.free(tmp_file);
    tmp_file = nullptr;
  }

  LOG_INFO("open a tmp file over", KR(ret), K(fd), K(dir_id), KP(tmp_file), K(lbt()));
  return ret;
}

int ObTmpFileManager::get_macro_block_list(common::ObIArray<blocksstable::MacroBlockId> &macro_id_list)
{
  int ret = OB_SUCCESS;
  if (IS_NOT_INIT) {
    ret = OB_NOT_INIT;
// This function remains available after the service stops and before destruction.
//  } else if (OB_UNLIKELY(!is_running())) {
//    ret = OB_ERR_UNEXPECTED;
//    LOG_WARN("ObTmpFileManager is not running", KR(ret), K(is_running_));
  } else if (OB_FAIL(tmp_file_block_manager_.get_macro_block_list(macro_id_list))) {
  }

  LOG_INFO("get tmp file macro block list", KR(ret), K(macro_id_list.count()));
  return ret;
}

int ObTmpFileManager::get_tmp_file_disk_usage(int64_t &disk_data_size, int64_t &occupied_disk_size)
{
  int ret = OB_SUCCESS;
  int64_t used_page_num = 0;
  int64_t macro_block_count = 0;
  disk_data_size = 0;
  occupied_disk_size = 0;

  if (IS_NOT_INIT) {
    ret = OB_NOT_INIT;
// This function remains available after the service stops and before destruction.
//  } else if (OB_UNLIKELY(!is_running())) {
//    ret = OB_ERR_UNEXPECTED;
//    LOG_WARN("ObTmpFileManager is not running", KR(ret), K(is_running_));
  } else if (OB_FAIL(tmp_file_block_manager_.get_block_usage_stat(used_page_num, macro_block_count))) {
  } else {
    disk_data_size = used_page_num * ObTmpFileGlobal::ALLOC_PAGE_SIZE;
    occupied_disk_size = macro_block_count * ObTmpFileGlobal::SN_BLOCK_SIZE;
  }

  LOG_INFO("get tmp file macro block count", KR(ret), K(used_page_num), K(macro_block_count));
  return ret;
}

int ObTmpFileManager::server_module_init(ObTmpFileManager *&manager)
{
  int ret = OB_SUCCESS;
  if (OB_ISNULL(manager)) {
    ret = OB_INVALID_ARGUMENT;
  } else if (OB_FAIL(manager->init())) {
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
