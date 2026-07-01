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

#include "storage/tmp_file/ob_sn_tmp_file_manager.h"

namespace oceanbase
{
namespace tmp_file
{
int64_t ObSNTenantTmpFileManager::current_fd_ = ObTmpFileGlobal::INVALID_TMP_FILE_FD;
int64_t ObSNTenantTmpFileManager::current_dir_id_ = ObTmpFileGlobal::INVALID_TMP_FILE_DIR_ID;

ObSNTenantTmpFileManager::ObSNTenantTmpFileManager()
  : ObITenantTmpFileManager(),
    tmp_file_block_manager_(),
    page_cache_controller_(tmp_file_block_manager_)
{
}

ObSNTenantTmpFileManager::~ObSNTenantTmpFileManager()
{
  destroy();
}

int ObSNTenantTmpFileManager::init_sub_module_()
{
  int ret = OB_SUCCESS;

  if (OB_FAIL(tmp_file_block_manager_.init())) {
  } else if (OB_FAIL(page_cache_controller_.init())) {
  } else {
    LOG_INFO("ObSNTenantTmpFileManager init successful", KP(this));
  }

  return ret;
}

int ObSNTenantTmpFileManager::start_sub_module_()
{
  int ret = OB_SUCCESS;
  if (OB_FAIL(page_cache_controller_.start())) {
    ret = OB_ERR_UNEXPECTED;
    LOG_WARN("fail to start page cache controller background threads", KR(ret));
  } else {
    is_running_ = true;
    LOG_INFO("ObSNTenantTmpFileManager start successful", KP(this));
  }
  return ret;
}

int ObSNTenantTmpFileManager::stop_sub_module_()
{
  int ret = OB_SUCCESS;
  page_cache_controller_.stop();
  LOG_INFO("ObSNTenantTmpFileManager stop successful", KP(this));
  return ret;
}

int ObSNTenantTmpFileManager::wait_sub_module_()
{
  int ret = OB_SUCCESS;
  page_cache_controller_.wait();
  LOG_INFO("ObSNTenantTmpFileManager wait successful", KP(this));
  return ret;
}

int ObSNTenantTmpFileManager::destroy_sub_module_()
{
  int ret = OB_SUCCESS;
  page_cache_controller_.destroy();
  tmp_file_block_manager_.destroy();

  LOG_INFO("ObSNTenantTmpFileManager destroy", KP(this));
  return ret;
}

int ObSNTenantTmpFileManager::alloc_dir(int64_t &dir_id)
{
  int ret = OB_SUCCESS;
  dir_id = ObTmpFileGlobal::INVALID_TMP_FILE_DIR_ID;

  if (IS_NOT_INIT) {
    ret = OB_NOT_INIT;
    LOG_WARN("ObSNTenantTmpFileManager has not been inited", KR(ret));
  } else if (OB_UNLIKELY(!is_running())) {
    ret = OB_ERR_UNEXPECTED;
    LOG_WARN("ObSNTenantTmpFileManager is not running", KR(ret), K(is_running_));
  } else {
    dir_id = ATOMIC_AAF(&current_dir_id_, 1);
  }

  LOG_DEBUG("alloc dir over", KR(ret), K(dir_id), K(lbt()));
  return ret;
}

int ObSNTenantTmpFileManager::open(int64_t &fd, const int64_t &dir_id, const char* const label)
{
  int ret = OB_SUCCESS;
  fd = ObTmpFileGlobal::INVALID_TMP_FILE_FD;
  void *buf = nullptr;
  ObSharedNothingTmpFile *tmp_file = nullptr;

  if (IS_NOT_INIT) {
    ret = OB_NOT_INIT;
    LOG_WARN("ObSNTenantTmpFileManager has not been inited", KR(ret));
  } else if (OB_UNLIKELY(!is_running())) {
    ret = OB_ERR_UNEXPECTED;
    LOG_WARN("ObSNTenantTmpFileManager is not running", KR(ret), K(is_running_));
  } else if (OB_ISNULL(buf = tmp_file_allocator_.alloc(sizeof(ObSharedNothingTmpFile),
                                                       lib::ObMemAttr("SNTmpFile")))) {
    ret = OB_ALLOCATE_MEMORY_FAILED;
    LOG_WARN("fail to allocate memory for tmp file",
             KR(ret), K(sizeof(ObSharedNothingTmpFile)));
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

// Get tmp file and increase its refcnt
int ObSNTenantTmpFileManager::get_tmp_file(const int64_t fd, ObSNTmpFileHandle &file_handle) const
{
  int ret = OB_SUCCESS;

  if (IS_NOT_INIT) {
    ret = OB_NOT_INIT;
    LOG_WARN("ObSNTenantTmpFileManager has not been inited", KR(ret));
  } else if (OB_UNLIKELY(!is_running())) {
    ret = OB_ERR_UNEXPECTED;
    LOG_WARN("ObSNTenantTmpFileManager is not running", KR(ret), K(is_running_));
  } else if (OB_FAIL(files_.get(ObTmpFileKey(fd), file_handle))) {
    if (OB_ENTRY_NOT_EXIST == ret) {
      LOG_WARN("tmp file does not exist", KR(ret), K(fd));
    } else {
      LOG_WARN("fail to get tmp file", KR(ret), K(fd));
    }
  } else if (OB_ISNULL(file_handle.get())) {
    ret = OB_ERR_UNEXPECTED;
    LOG_WARN("get invalid tmp file pointer", KR(ret), K(fd), KP(file_handle.get()));
  }

  return ret;
}

int ObSNTenantTmpFileManager::get_macro_block_list(common::ObIArray<blocksstable::MacroBlockId> &macro_id_list)
{
  int ret = OB_SUCCESS;
  if (IS_NOT_INIT) {
    ret = OB_NOT_INIT;
    LOG_WARN("ObSNTenantTmpFileManager has not been inited", KR(ret));
// XXX This function must still be available after the tenant is stopped and before it is destroyed.
//  } else if (OB_UNLIKELY(!is_running())) {
//    ret = OB_ERR_UNEXPECTED;
//    LOG_WARN("ObSNTenantTmpFileManager is not running", KR(ret), K(is_running_));
  } else if (OB_FAIL(tmp_file_block_manager_.get_macro_block_list(macro_id_list))) {
  }

  LOG_INFO("get tmp file macro block list", KR(ret), K(macro_id_list.count()));
  return ret;
}

int ObSNTenantTmpFileManager::get_tmp_file_disk_usage(int64_t &disk_data_size, int64_t &occupied_disk_size)
{
  int ret = OB_SUCCESS;
  int64_t used_page_num = 0;
  int64_t macro_block_count = 0;
  disk_data_size = 0;
  occupied_disk_size = 0;

  if (IS_NOT_INIT) {
    ret = OB_NOT_INIT;
    LOG_WARN("ObSNTenantTmpFileManager has not been inited", KR(ret));
// XXX This function must still be available after the tenant is stopped and before it is destroyed.
//  } else if (OB_UNLIKELY(!is_running())) {
//    ret = OB_ERR_UNEXPECTED;
//    LOG_WARN("ObSNTenantTmpFileManager is not running", KR(ret), K(is_running_));
  } else if (OB_FAIL(tmp_file_block_manager_.get_block_usage_stat(used_page_num, macro_block_count))) {
  } else {
    disk_data_size = used_page_num * ObTmpFileGlobal::ALLOC_PAGE_SIZE;
    occupied_disk_size = macro_block_count * ObTmpFileGlobal::SN_BLOCK_SIZE;
  }

  LOG_INFO("get tmp file macro block count", KR(ret), K(used_page_num), K(macro_block_count));
  return ret;
}

}  // end namespace tmp_file
}  // end namespace oceanbase
