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

#define USING_LOG_PREFIX CLOG
#include "ob_server_log_block_mgr.h"
#include <regex>                                // std::regex
#include "observer/ob_server.h"                 // OBSERVER
#include "observer/ob_server_utils.h"           // get_log_disk_info_in_config
#include "logservice/ob_log_service.h"          // ObLogService

#define BYTE_TO_MB(byte) (byte+1024*1024-1)/1024/1024

namespace oceanbase
{
using namespace palf;
using namespace share;
namespace logservice
{

int ObServerLogBlockMgr::check_clog_directory_is_empty(const char *clog_dir, bool &result)
{
  int ret = OB_SUCCESS;
  DIR *dir = NULL;
  struct dirent *entry = NULL;
  int64_t num = 0;
  result = false;
  if (NULL == clog_dir) {
    ret = OB_INVALID_ARGUMENT;
    CLOG_LOG(WARN, "directory path is NULL, ", K(ret));
  } else if (NULL == (dir = opendir(clog_dir))) {
    ret = OB_ERR_SYS;
    CLOG_LOG(WARN, "Fail to open dir, ", K(ret), K(errno), K(clog_dir));
  } else {
    result = true;
    while (NULL != (entry = readdir(dir))) {
      if (0 != strcmp(entry->d_name, ".") && 0 != strcmp(entry->d_name, "..")) {
        result = false;
      }
    }
  }

  if (NULL != dir) {
    closedir(dir);
  }
  return ret;
}

ObServerLogBlockMgr::ObServerLogBlockMgr()
    : block_cnt_in_use_(0),
      is_started_(false),
      is_inited_(false)
{
}

ObServerLogBlockMgr::~ObServerLogBlockMgr()
{
  destroy();
}

int ObServerLogBlockMgr::init(const char *log_disk_base_path)
{
  int ret = OB_SUCCESS;
  if (IS_INIT) {
    ret = OB_INIT_TWICE;
    CLOG_LOG(ERROR, "ObServerLogBlockMgr inited twice", K(ret), KPC(this));
  } else if (OB_ISNULL(log_disk_base_path)) {
    ret = OB_INVALID_ARGUMENT;
    CLOG_LOG(ERROR, "Invalid argument", K(ret), KPC(this), KP(log_disk_base_path));
  } else if (OB_FAIL(do_load_(log_disk_base_path))) {
    CLOG_LOG(ERROR, "do_load_ failed", K(ret), KPC(this), K(log_disk_base_path));
  } else {
    get_tenants_log_disk_size_func_ = [this](int64_t &log_disk_size) -> int
    { 
      log_disk_size = 0;
      return get_all_tenants_log_disk_size_(log_disk_size);
    };
    is_inited_ = true;
    CLOG_LOG(INFO, "ObServerLogBlockMgr init success", KPC(this));
  }
  if (OB_FAIL(ret)) {
    destroy();
  }
  return ret;
}

void ObServerLogBlockMgr::destroy()
{
  CLOG_LOG_RET(WARN, OB_SUCCESS, "ObServerLogBlockMgr  destroy", KPC(this));
  is_inited_ = false;
  is_started_ = false;
  block_cnt_in_use_ = 0;
}

int ObServerLogBlockMgr::start(const int64_t new_size_byte)
{
  int ret = OB_SUCCESS;
  if (IS_NOT_INIT) {
    ret = OB_NOT_INIT;
    CLOG_LOG(WARN, "ObServerLogBlockMGR is not inited", K(ret), KPC(this));
  } else if (!check_space_is_enough_(new_size_byte)) {
    ret = OB_MACHINE_RESOURCE_NOT_ENOUGH;
    CLOG_LOG(WARN, "server log disk is too small to hold all tenants or the count of tenants"
             ", log disk space is not enough!!!",
             K(ret), KPC(this), K(new_size_byte));
  } else {
    ATOMIC_STORE(&is_started_, true);
    CLOG_LOG(INFO, "ObServerLogBlockMGR start success", K(ret), KPC(this), K(new_size_byte));
  }
  return ret;
}

int ObServerLogBlockMgr::get_disk_usage(int64_t &in_use_size_byte)
{
  int ret = OB_SUCCESS;
  if (IS_NOT_INIT) {
    ret = OB_NOT_INIT;
    CLOG_LOG(ERROR, "ObServerLogBlockMgr has not inited", K(ret), KPC(this));
  } else {
    in_use_size_byte = get_in_use_size_();
  }
  return ret;
}

int64_t ObServerLogBlockMgr::get_log_disk_size()
{
  int ret = OB_SUCCESS;
  int64_t log_disk_size = 0;
  int64_t expected_log_disk_size = 0;
  int64_t unused_log_disk_percentage = 0;
  int64_t total_log_disk_size = 0;
  if (OB_FAIL(get_tenants_log_disk_size_func_(log_disk_size))) {
    CLOG_LOG(WARN, "get_tenants_log_disk_size_func_ failed", K(ret), K(log_disk_size));
  } else if (OB_FAIL(observer::ObServerUtils::get_log_disk_info_in_config(expected_log_disk_size,
             unused_log_disk_percentage,
             total_log_disk_size))) {
    CLOG_LOG(ERROR, "get_log_disk_info_in_config failed", K(expected_log_disk_size), KPC(this));
  } else if (expected_log_disk_size > total_log_disk_size) {
    ret = OB_MACHINE_RESOURCE_NOT_ENOUGH;
    CLOG_LOG(ERROR, "try_resize failed, log disk space is not enough", K(expected_log_disk_size), KPC(this));
  } else {
    log_disk_size = expected_log_disk_size;
  }
  return log_disk_size;
}

int ObServerLogBlockMgr::create_block_at(const FileDesc &dest_dir_fd,
                                         const char *dest_block_path,
                                         const int64_t block_size)
{
  int ret = OB_SUCCESS;
  if (IS_NOT_INIT) {
    ret = OB_NOT_INIT;
    CLOG_LOG(ERROR, "ObServerLogBlockMgr has not inited", K(ret), KPC(this));
  } else if (false == is_valid_file_desc(dest_dir_fd)
             || NULL == dest_block_path || BLOCK_SIZE != block_size) {
    ret = OB_INVALID_ARGUMENT;
    CLOG_LOG(ERROR, "Invalid argument", K(ret), KPC(this), K(dest_dir_fd),
             K(dest_block_path), K(block_size));
  }
  if (OB_SUCC(ret)) {
    while (OB_FAIL(allocate_block_at_(dest_dir_fd, dest_block_path, block_size))) {
      CLOG_LOG(WARN, "allocate_block_at_ failed", K(ret), KPC(this),
               K(dest_dir_fd), K(dest_block_path));
      ob_usleep(10 * 1000); // 10ms
    }
  }
  // make sure the meta info of both directory has been flushed.
  if (OB_FAIL(ret)) {
  } else if (OB_FAIL(fsync_until_success_(dest_dir_fd))) {
    CLOG_LOG(ERROR, "fsync_until_success_ failed", K(ret), KPC(this), K(dest_block_path),
             K(dest_dir_fd));
  } else {
    ATOMIC_INC(&block_cnt_in_use_);
    CLOG_LOG(INFO, "create_new_block_at success", K(ret), KPC(this), K(dest_dir_fd),
             K(dest_block_path));
  }
  return ret;
}

int ObServerLogBlockMgr::remove_block_at(const FileDesc &src_dir_fd,
                                         const char *src_block_path)
{
  int ret = OB_SUCCESS;
  block_id_t dest_block_id = LOG_INVALID_BLOCK_ID;
  char dest_block_path[OB_MAX_FILE_NAME_LENGTH] = {'\0'};
  bool result = true;
  if (OB_FAIL(is_block_used_for_palf(src_dir_fd, src_block_path, result))) {
    CLOG_LOG(ERROR, "block_is_used_for_palf failed", K(ret));
  } else if (false == result) {
    CLOG_LOG(ERROR, "this block is not used for palf", K(ret), K(src_block_path));
    ::unlinkat(src_dir_fd, src_block_path, 0);
  } else {
    if (IS_NOT_INIT) {
      ret = OB_NOT_INIT;
      CLOG_LOG(ERROR, "ObServerLogBlockMGR has not inited", K(ret), KPC(this));
    } else if (OB_FAIL(free_block_at_(src_dir_fd, src_block_path))) {
      CLOG_LOG(ERROR, "free_block_at_ failed", K(ret), KPC(this), K(src_dir_fd),
               K(src_block_path));
      // make sure the meta info of both directory has been flushed.
    } else if (OB_FAIL(fsync_until_success_(src_dir_fd))) {
      CLOG_LOG(ERROR, "fsync_until_success_ failed", K(ret), KPC(this), K(dest_block_id),
               K(src_dir_fd), K(src_block_path));
    } else {
      ATOMIC_DEC(&block_cnt_in_use_);
      CLOG_LOG(INFO, "delete_block_at success", K(ret), KPC(this), K(src_dir_fd),
               K(src_block_path));
    }
  }
  return ret;
}

int ObServerLogBlockMgr::update_tenant(const int64_t old_log_disk_size,
                                       const int64_t new_log_disk_size,
                                       int64_t &allowed_new_log_disk_size,
                                       logservice::ObLogService *log_service)
{
  int ret = OB_SUCCESS;
  if (IS_NOT_INIT) {
    ret = OB_NOT_INIT;
    CLOG_LOG(WARN, "ObServerLogBlockMGR is not inited", K(old_log_disk_size), K(new_log_disk_size), KPC(this));
  } else if (old_log_disk_size < 0 || new_log_disk_size < 0 || OB_ISNULL(log_service)) {
    ret = OB_INVALID_ARGUMENT;
    CLOG_LOG(WARN, "invalid argument", K(old_log_disk_size), K(new_log_disk_size), KP(log_service), KPC(this));
  } else if (FALSE_IT(allowed_new_log_disk_size = new_log_disk_size)) {
  } else if (OB_FAIL(log_service->update_log_disk_usage_limit_size(new_log_disk_size))) {
    CLOG_LOG(WARN, "failed to update_log_disk_usage_limit_size", K(new_log_disk_size), K(old_log_disk_size),
             K(allowed_new_log_disk_size));
  }
  return ret;
}

// step1. scan the directory of 'log_disk_path'(ie: like **/store/clog), get the total
// block count.
//
// step2. scan the directory of 'log_pool_path_'(ie: like **/log_pool), get
// the total block  count, and then trim the directory.
//
// step3. load the meta.
//
// step4. if the 'status_' of meta is EXPANDING_STATUS or SHRINKING_STATUS, continous to
// finish it.
//
// step5. check the total block count of 'log_disk_path' and 'log_pool_path_' whether is
// same as the 'curr_total_size_' of 'log_pool_meta_'.
int ObServerLogBlockMgr::do_load_(const char *log_disk_path)
{
  int ret = OB_SUCCESS;
  int64_t has_allocated_block_cnt = 0;
  ObTimeGuard time_guard("RestartServerBlockMgr", 1 * 1000 * 1000);
  if (OB_FAIL(remove_tmp_file_or_directory_for_tenant_(log_disk_path))) {
    CLOG_LOG(WARN, "remove_tmp_file_or_directory_at failed", K(ret), K(log_disk_path));
  } else if (OB_FAIL(scan_log_disk_dir_(log_disk_path, has_allocated_block_cnt))) {
    CLOG_LOG(WARN, "scan_log_disk_dir_ failed", K(ret), KPC(this), K(log_disk_path),
             K(has_allocated_block_cnt));
  } else if (FALSE_IT(time_guard.click("scan_log_disk_"))) {
  } else {
    ATOMIC_STORE(&block_cnt_in_use_, has_allocated_block_cnt);
    CLOG_LOG(INFO, "do_load_ success", K(ret), KPC(this), K(time_guard));
  }
  return ret;
}

int ObServerLogBlockMgr::scan_log_disk_dir_(const char *log_disk_path,
                                            int64_t &has_allocated_block_cnt)
{
  return get_has_allocated_blocks_cnt_in_(log_disk_path, has_allocated_block_cnt);
}

bool ObServerLogBlockMgr::check_space_is_enough_(const int64_t log_disk_size) const
{
  bool bool_ret = false;
  int64_t all_tenants_log_disk_size = 0;
  int ret = OB_SUCCESS;
  if (OB_FAIL(get_tenants_log_disk_size_func_(all_tenants_log_disk_size))) {
    CLOG_LOG(WARN, "get_tenants_log_disk_size_func_ failed", K(ret), K(all_tenants_log_disk_size));
  } else {
    bool_ret = (all_tenants_log_disk_size <= log_disk_size ? true : false);
    CLOG_LOG(INFO, "check_space_is_enough_ finished", K(all_tenants_log_disk_size), K(log_disk_size));
  }
  return bool_ret;
}

int ObServerLogBlockMgr::get_all_tenants_log_disk_size_(int64_t &all_tenants_log_disk_size) const
{
  int ret = OB_SUCCESS;
  omt::ObMultiTenant *omt = GCTX.omt_;
  int64_t tenant_count = 0;
  auto func = [&all_tenants_log_disk_size] () -> int{
    int ret = OB_SUCCESS;
    ObLogService *log_service = MTL(ObLogService*);
    PalfOptions opts;
    if (OB_FAIL(log_service->get_palf_options(opts))) {
      CLOG_LOG(WARN, "get_palf_options failed", K(ret), K(all_tenants_log_disk_size));
    } else {
      all_tenants_log_disk_size += opts.disk_options_.log_disk_usage_limit_size_;
    }
    return ret;
  };
  if (OB_FAIL(omt->operate_in_each_tenant(func))) {
    CLOG_LOG(WARN, "operate_in_each_tenant failed", K(ret), K(all_tenants_log_disk_size));
  }
  return ret;
}

int64_t ObServerLogBlockMgr::get_in_use_size_()
{
  return ATOMIC_LOAD(&block_cnt_in_use_) * BLOCK_SIZE;
}

int ObServerLogBlockMgr::allocate_block_at_(const FileDesc &dir_fd,
                                            const char *block_path,
                                            const int64_t block_size)
{
  int ret = OB_SUCCESS;
  FileDesc fd = -1;
  if (-1 == (fd = ::openat(dir_fd, block_path, CREATE_FILE_FLAG, CREATE_FILE_MODE))) {
    ret = convert_sys_errno();
    CLOG_LOG(ERROR, "::openat failed", K(ret), KPC(this), K(dir_fd), K(block_path));
  } else if (-1 == ::fallocate(fd, 0, 0, block_size)) {
    ret = convert_sys_errno();
    CLOG_LOG(ERROR, "::fallocate failed", K(ret), KPC(this), K(dir_fd), K(block_path),
             K(errno));
  } else {
    if (REACH_TIME_INTERVAL(PRINT_INTERVAL)) {
      CLOG_LOG(INFO, "allocate_block_at_ success", K(ret), KPC(this), K(dir_fd),
               K(block_path));
    }
  }
  if (-1 != fd && -1 == ::close(fd)) {
    int tmp_ret = convert_sys_errno();
    CLOG_LOG(ERROR, "::close failed", K(ret), K(tmp_ret), KPC(this), K(dir_fd), K(block_path));
    ret = (OB_SUCCESS == ret ? tmp_ret : ret);
  }
  return ret;
}

int ObServerLogBlockMgr::free_block_at_(const FileDesc &src_dir_fd,
                                        const char *block_path)
{
  int ret = OB_SUCCESS;
  if (OB_FAIL(unlinkat_until_success_(src_dir_fd, block_path, 0))) {
    CLOG_LOG(ERROR, "unlinkat_until_success_ failed", K(ret), KPC(this), K(src_dir_fd),
             K(block_path));
  } else {
    if (REACH_TIME_INTERVAL(PRINT_INTERVAL)) {
      CLOG_LOG(INFO, "free_block_at_ success", K(ret), KPC(this), K(src_dir_fd),
               K(block_path));
    }
  }
  return ret;
}

int ObServerLogBlockMgr::get_has_allocated_blocks_cnt_in_(
    const char *log_disk_path, int64_t &has_allocated_block_cnt)
{
  int ret = OB_SUCCESS;
  DIR *dir = NULL;
  struct dirent *entry = NULL;
  std::regex pattern_tenant(".*/sys");
  std::regex pattern_log_pool(".*/log_pool/*");
  if (NULL == (dir = opendir(log_disk_path))) {
    ret = OB_ERR_SYS;
    CLOG_LOG(WARN, "opendir failed", K(log_disk_path));
  } else {
    char current_file_path[OB_MAX_FILE_NAME_LENGTH] = {'\0'};
    while ((entry = readdir(dir)) != NULL && OB_SUCC(ret)) {
      bool is_dir = false;
      MEMSET(current_file_path, '\0', OB_MAX_FILE_NAME_LENGTH);
      if (0 == strcmp(entry->d_name, ".") || 0 == strcmp(entry->d_name, "..")) {
        // do nothing
      } else if (0 >= snprintf(current_file_path, OB_MAX_FILE_NAME_LENGTH, "%s/%s",
                               log_disk_path, entry->d_name)) {
        ret = OB_ERR_UNEXPECTED;
        CLOG_LOG(WARN, "snprintf failed", K(ret), K(current_file_path), K(log_disk_path),
                K(entry->d_name));
      } else if (OB_FAIL(FileDirectoryUtils::is_directory(current_file_path, is_dir))) {
        CLOG_LOG(WARN, "is_directory failed", K(ret), K(entry->d_name));
      } else if (false == is_dir) {
        ret = OB_ERR_UNEXPECTED;
        LOG_DBA_ERROR_V2(OB_LOG_EXTERNAL_FILE_EXIST, ret, "Attention!!!", "There are several files in the log directory that are not generated by "
                         "OceanBase.", "[suggestion] Please confirm whether manual deletion is required",
                         ", unexpected file path is ", current_file_path);
      } else if (true == std::regex_match(current_file_path, pattern_tenant)) {
        ret = scan_tenant_dir_(current_file_path, has_allocated_block_cnt);
      } else if (true == std::regex_match(current_file_path, pattern_log_pool)) {
        CLOG_LOG(INFO, "ignore log_pool path", K(current_file_path), KPC(this));
      } else {
        ret = OB_ERR_UNEXPECTED;
        LOG_DBA_ERROR_V2(OB_LOG_EXTERNAL_FILE_EXIST, ret, "Attention!!!", "There are several files in the log directory that are not generated by "
                         "OceanBase.", "[suggestion] Please confirm whether manual deletion is required",
                         ", unexpected directory is ", current_file_path);
      }
    }
  }
  if (NULL != dir) {
    closedir(dir);
  }
  return ret;
}

int ObServerLogBlockMgr::remove_tmp_file_or_directory_for_tenant_(const char *log_disk_path)
{
  int ret = OB_SUCCESS;
  DIR *dir = NULL;
  std::regex pattern_tenant(".*/sys");
  struct dirent *entry = NULL;
  if (NULL == (dir = opendir(log_disk_path))) {
    ret = OB_ERR_SYS;
    CLOG_LOG(WARN, "opendir failed", K(log_disk_path));
  } else {
    char current_file_path[OB_MAX_FILE_NAME_LENGTH] = {'\0'};
    while ((entry = readdir(dir)) != NULL && OB_SUCC(ret)) {
      bool is_dir = false;
      MEMSET(current_file_path, '\0', OB_MAX_FILE_NAME_LENGTH);
      if (0 == strcmp(entry->d_name, ".") || 0 == strcmp(entry->d_name, "..")) {
        // do nothing
      } else if (0 >= snprintf(current_file_path, OB_MAX_FILE_NAME_LENGTH, "%s/%s",
                               log_disk_path, entry->d_name)) {
        ret = OB_ERR_UNEXPECTED;
        CLOG_LOG(WARN, "snprintf failed", K(ret), K(current_file_path), K(log_disk_path),
                K(entry->d_name));
      } else if (OB_FAIL(FileDirectoryUtils::is_directory(current_file_path, is_dir))) {
        CLOG_LOG(WARN, "is_directory failed", K(ret), K(entry->d_name));
      } else if (false == is_dir) {
        CLOG_LOG(ERROR, "is not diectory, unexpected", K(ret), K(log_disk_path), K(current_file_path));
      } else if (true == std::regex_match(current_file_path, pattern_tenant)) {
        if (OB_FAIL(remove_tmp_file_or_directory_at(current_file_path, this))) {
          CLOG_LOG(ERROR, "this dir is tenant, remove_tmp_file_or_directory_at failed", K(ret), K(current_file_path));
        } else {
          CLOG_LOG(INFO, "this dir is tenant, remove_tmp_file_or_directory_at success", K(ret), K(current_file_path));
        }
      } else {
      }
    }
  }
  if (NULL != dir) {
    closedir(dir);
  }
  return ret;
}

int ObServerLogBlockMgr::unlinkat_until_success_(const palf::FileDesc &src_dir_fd,
                                                 const char *block_path, const int flag)
{
  int ret = OB_SUCCESS;
  do {
    if (-1 == ::unlinkat(src_dir_fd, block_path, flag)) {
      ret = convert_sys_errno();
      CLOG_LOG(ERROR, "::unlink failed", K(ret), KPC(this), K(src_dir_fd), K(block_path),
               K(flag));
      ob_usleep(SLEEP_TS_US);
    } else {
      ret = OB_SUCCESS;
      break;
    }
  } while (OB_FAIL(ret));
  return ret;
}

int ObServerLogBlockMgr::fsync_until_success_(const FileDesc &dest_dir_fd)
{
  int ret = OB_SUCCESS;
  if (OB_FAIL(fsync_with_retry(dest_dir_fd))) {
    CLOG_LOG(ERROR, "fsync_with_retry failed", KR(ret), KPC(this), K(dest_dir_fd));
  }
  return ret;
}

int ObServerLogBlockMgr::force_update_tenant_log_disk(const uint64_t tenant_id,
                                                      const int64_t new_log_disk_size)
{
  int ret = OB_SUCCESS;
  MTL_SWITCH(tenant_id) {
    int64_t unused_size = 0;
    int64_t old_log_disk_size = 0;
    int64_t allowed_new_log_disk_size = 0;
    ObLogService *log_service = MTL(ObLogService*);
    if (NULL == log_service) {
      ret = OB_ERR_UNEXPECTED;
      CLOG_LOG(ERROR, "unexpected error, ObLogService is nullptr", KR(ret), KP(log_service));
    } else if (OB_FAIL(log_service->get_palf_stable_disk_usage(unused_size, old_log_disk_size))) {
      CLOG_LOG(ERROR, "get_palf_stable_disk_usage failed", KR(ret), KP(log_service));
    } else if (OB_FAIL(update_tenant(old_log_disk_size, new_log_disk_size, allowed_new_log_disk_size, log_service))) {
      CLOG_LOG(WARN, "update_tenant failed", KR(ret), KP(log_service));
    } else if (allowed_new_log_disk_size != new_log_disk_size) {
      ret = OB_STATE_NOT_MATCH;
      CLOG_LOG(WARN, "can not force update tenant log disk, force_update_tenant_log_disk failed", KR(ret), KP(log_service), K(new_log_disk_size),
               K(allowed_new_log_disk_size), K(old_log_disk_size));
    } else {
    }
    CLOG_LOG(INFO, "force_update_tenant_log_disk finished", KR(ret), KP(log_service), K(new_log_disk_size),
             K(allowed_new_log_disk_size), K(old_log_disk_size));
  } else {
    CLOG_LOG(WARN, "force_update_tenant_log_disk failed, no such tenant", KR(ret),  K(tenant_id), K(new_log_disk_size));
  }
  return ret;
}

// the prefix is tenant_xxx
int ObServerLogBlockMgr::scan_tenant_dir_(const char *tenant_dir,
                                          int64_t &has_allocated_block_cnt)
{
  int ret = OB_SUCCESS;
  DIR *dir = NULL;
  std::regex pattern_log_stream(".*/sys/[1-9]\\d*");
  std::regex pattern_tmp_dir(".*/sys/tmp_dir");
  struct dirent *entry = NULL;
  if (NULL == (dir = opendir(tenant_dir))) {
    ret = OB_ERR_SYS;
    CLOG_LOG(WARN, "opendir failed", K(tenant_dir));
  } else {
    char current_file_path[OB_MAX_FILE_NAME_LENGTH] = {'\0'};
    while ((entry = readdir(dir)) != NULL && OB_SUCC(ret)) {
      bool is_dir = false;
      MEMSET(current_file_path, '\0', OB_MAX_FILE_NAME_LENGTH);
      if (0 == strcmp(entry->d_name, ".") || 0 == strcmp(entry->d_name, "..")) {
        // do nothing
      } else if (0 >= snprintf(current_file_path, OB_MAX_FILE_NAME_LENGTH, "%s/%s",
                               tenant_dir, entry->d_name)) {
        ret = OB_ERR_UNEXPECTED;
        CLOG_LOG(WARN, "snprintf failed", K(ret), K(current_file_path), K(tenant_dir),
                K(entry->d_name));
      } else if (OB_FAIL(FileDirectoryUtils::is_directory(current_file_path, is_dir))) {
        CLOG_LOG(WARN, "is_directory failed", K(ret), K(entry->d_name));
      } else if (false == is_dir) {
        ret = OB_ERR_UNEXPECTED;
        LOG_DBA_ERROR_V2(OB_LOG_EXTERNAL_FILE_EXIST, ret, "Attention!!!", "There are several files in the log directory that are not generated by "
                         "OceanBase.", "[suggestion] Please confirm whether manual deletion is required",
                         ", unexpected file is ", current_file_path);
      } else if (true == std::regex_match(current_file_path, pattern_log_stream)) {
        ret = scan_ls_dir_(current_file_path, has_allocated_block_cnt);
      } else if (true == std::regex_match(current_file_path, pattern_tmp_dir)) {
        CLOG_LOG(INFO, "ignore tmp_dir", K(current_file_path), K(has_allocated_block_cnt), KPC(this));
      } else {
        ret = OB_ERR_UNEXPECTED;
        LOG_DBA_ERROR_V2(OB_LOG_EXTERNAL_FILE_EXIST, ret, "Attention!!!", "There are several files in the log directory that are not generated by "
                         "OceanBase.", "[suggestion] Please confirm whether manual deletion is required",
                         ", unexpected directory is ", current_file_path);
      }
    }
  }
  if (NULL != dir) {
    closedir(dir);
  }
  return ret;
}

// the prefix of ls_dir tenant_xxx/xxxx
int ObServerLogBlockMgr::scan_ls_dir_(const char *ls_dir,
                                      int64_t &has_allocated_block_cnt)
{
  int ret = OB_SUCCESS;
  DIR *dir = NULL;
  std::regex pattern_log(".*/sys/[1-9]\\d*/log");
  std::regex pattern_meta(".*/sys/[1-9]\\d*/meta");
  struct dirent *entry = NULL;
  if (NULL == (dir = opendir(ls_dir))) {
    ret = OB_ERR_SYS;
    CLOG_LOG(WARN, "opendir failed", K(ls_dir));
  } else {
    char current_file_path[OB_MAX_FILE_NAME_LENGTH] = {'\0'};
    while ((entry = readdir(dir)) != NULL && OB_SUCC(ret)) {
      bool is_dir = false;
      MEMSET(current_file_path, '\0', OB_MAX_FILE_NAME_LENGTH);
      if (0 == strcmp(entry->d_name, ".") || 0 == strcmp(entry->d_name, "..")) {
        // do nothing
      } else if (0 >= snprintf(current_file_path, OB_MAX_FILE_NAME_LENGTH, "%s/%s",
                               ls_dir, entry->d_name)) {
        ret = OB_ERR_UNEXPECTED;
        CLOG_LOG(WARN, "snprintf failed", K(ret), K(current_file_path), K(ls_dir),
                K(entry->d_name));
      } else if (OB_FAIL(FileDirectoryUtils::is_directory(current_file_path, is_dir))) {
        CLOG_LOG(WARN, "is_directory failed", K(ret), K(entry->d_name));
      } else if (false == is_dir) {
        ret = OB_ERR_UNEXPECTED;
        LOG_DBA_ERROR_V2(OB_LOG_EXTERNAL_FILE_EXIST, ret, "Attention!!!", "There are several files in the log directory that are not generated by "
                         "OceanBase.", "[suggestion] Please confirm whether manual deletion is required",
                         ", unexpected file is ", current_file_path);
      } else if (true == std::regex_match(current_file_path, pattern_log)
                 || true == std::regex_match(current_file_path, pattern_meta)) {
        GetBlockCountFunctor functor(current_file_path);
        if (OB_FAIL(palf::scan_dir(current_file_path, functor))) {
          LOG_DBA_ERROR_V2(OB_LOG_EXTERNAL_FILE_EXIST, ret, "Attention!!!", "There are several files in the log directory that are not generated by "
                           "OceanBase.", "[suggestion] Please confirm whether manual deletion is required",
                           ", unexpected directory is ", current_file_path);
        } else {
          has_allocated_block_cnt += functor.get_block_count();
          CLOG_LOG(INFO, "get_has_allocated_blocks_cnt_in_ success", K(ret),
                   K(current_file_path), "block_cnt", functor.get_block_count());
        }
      } else {
        ret = OB_ERR_UNEXPECTED;
        LOG_DBA_ERROR_V2(OB_LOG_EXTERNAL_FILE_EXIST, ret, "Attention!!!", "There are several files in the log directory that are not generated by "
                         "OceanBase.", "[suggestion] Please confirm whether manual deletion is required",
                         ", unexpected directory is ", current_file_path);
      }
    }
  }
  if (NULL != dir) {
    closedir(dir);
  }
  return ret;
}
} // namespace logservice
} // namespace oceanbase
