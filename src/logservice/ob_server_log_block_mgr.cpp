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
#include <regex>
#include "lib/file/file_directory_utils.h"
#include "lib/time/ob_time_utility.h"
#include "lib/utility/ob_platform_io.h"
#include "logservice/ob_log_service.h"

namespace oceanbase
{
using namespace wal;
namespace logservice
{

int ObServerLogBlockMgr::check_clog_directory_is_empty(const char *clog_dir, bool &result)
{
  int ret = OB_SUCCESS;
  common::ObPlatformDir *dir = NULL;
  result = false;
  if (NULL == clog_dir) {
    ret = OB_INVALID_ARGUMENT;
    CLOG_LOG(WARN, "directory path is NULL", K(ret));
  } else if (NULL == (dir = common::ob_opendir_utf8(clog_dir))) {
    ret = OB_ERR_SYS;
    CLOG_LOG(WARN, "Fail to open dir", K(ret), K(errno), K(clog_dir));
  } else {
    char entry_name[OB_MAX_FILE_NAME_LENGTH] = {'\0'};
    int read_ret = 0;
    result = true;
    while (1 == (read_ret = common::ob_readdir_utf8(dir, entry_name, sizeof(entry_name)))) {
      if (0 != strcmp(entry_name, ".") && 0 != strcmp(entry_name, "..")) {
        result = false;
      }
    }
    if (0 > read_ret) {
      result = false;
      ret = OB_ERR_SYS;
      CLOG_LOG(WARN, "read dir failed", K(ret), K(errno), K(clog_dir));
    }
  }
  if (NULL != dir) {
    common::ob_closedir_utf8(dir);
  }
  return ret;
}

ObServerLogBlockMgr::ObServerLogBlockMgr()
    : get_log_disk_info_in_config_func_(NULL),
      log_service_(NULL),
      block_cnt_in_use_(0),
      is_inited_(false)
{}

ObServerLogBlockMgr::~ObServerLogBlockMgr()
{
  destroy();
}

int ObServerLogBlockMgr::init(
    const char *log_disk_base_path,
    GetLogDiskInfoInConfig get_log_disk_info_in_config)
{
  int ret = OB_SUCCESS;
  if (IS_INIT) {
    ret = OB_INIT_TWICE;
    CLOG_LOG(ERROR, "ObServerLogBlockMgr inited twice", K(ret), KPC(this));
  } else if (OB_ISNULL(log_disk_base_path) || OB_ISNULL(get_log_disk_info_in_config)) {
    ret = OB_INVALID_ARGUMENT;
    CLOG_LOG(ERROR, "Invalid argument", K(ret), KPC(this), KP(log_disk_base_path));
  } else if (OB_FAIL(do_load_(log_disk_base_path))) {
  } else {
    get_log_disk_info_in_config_func_ = get_log_disk_info_in_config;
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
  is_inited_ = false;
  block_cnt_in_use_ = 0;
  log_service_ = NULL;
  get_log_disk_info_in_config_func_ = NULL;
}

int ObServerLogBlockMgr::start(const int64_t new_size_byte)
{
  int ret = OB_SUCCESS;
  if (IS_NOT_INIT) {
    ret = OB_NOT_INIT;
    CLOG_LOG(WARN, "ObServerLogBlockMgr is not inited", K(ret), KPC(this));
  } else if (!check_space_is_enough_(new_size_byte)) {
    ret = OB_MACHINE_RESOURCE_NOT_ENOUGH;
    CLOG_LOG(WARN, "server log disk is too small for the local runtime",
             K(ret), KPC(this), K(new_size_byte));
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
  if (OB_FAIL(get_runtime_log_disk_size_(log_disk_size))) {
  } else if (OB_ISNULL(get_log_disk_info_in_config_func_)) {
    ret = OB_NOT_INIT;
    CLOG_LOG(ERROR, "get_log_disk_info_in_config_func_ is null", K(ret), KPC(this));
  } else if (OB_FAIL(get_log_disk_info_in_config_func_(expected_log_disk_size,
             unused_log_disk_percentage,
             total_log_disk_size))) {
  } else if (expected_log_disk_size > total_log_disk_size) {
    ret = OB_MACHINE_RESOURCE_NOT_ENOUGH;
    CLOG_LOG(ERROR, "try_resize failed, log disk space is not enough",
        K(expected_log_disk_size), KPC(this));
  } else {
    log_disk_size = expected_log_disk_size;
  }
  return log_disk_size;
}

int ObServerLogBlockMgr::update_log_disk_size(const int64_t old_log_disk_size,
                                              const int64_t new_log_disk_size,
                                              int64_t &allowed_new_log_disk_size,
                                              logservice::ObLogService *log_service)
{
  int ret = OB_SUCCESS;
  if (IS_NOT_INIT) {
    ret = OB_NOT_INIT;
    CLOG_LOG(WARN, "ObServerLogBlockMgr is not inited",
        K(old_log_disk_size), K(new_log_disk_size), KPC(this));
  } else if (old_log_disk_size < 0 || new_log_disk_size < 0 || OB_ISNULL(log_service)) {
    ret = OB_INVALID_ARGUMENT;
    CLOG_LOG(WARN, "invalid argument", K(old_log_disk_size), K(new_log_disk_size),
        KP(log_service), KPC(this));
  } else {
    allowed_new_log_disk_size = new_log_disk_size;
    ret = log_service->update_log_disk_usage_limit_size(new_log_disk_size);
  }
  return ret;
}

int ObServerLogBlockMgr::do_load_(const char *log_disk_path)
{
  int ret = OB_SUCCESS;
  int64_t segment_count = 0;
  ObTimeGuard time_guard("RestartServerBlockMgr", 1 * 1000 * 1000);
  if (OB_FAIL(remove_tmp_file_or_directory_for_runtime_(log_disk_path))) {
  } else if (OB_FAIL(scan_log_disk_dir_(log_disk_path, segment_count))) {
  } else {
    time_guard.click("scan_log_disk_");
    block_cnt_in_use_ = segment_count;
    CLOG_LOG(INFO, "do_load_ success", K(ret), KPC(this), K(time_guard));
  }
  return ret;
}

int ObServerLogBlockMgr::scan_log_disk_dir_(const char *log_disk_path, int64_t &segment_count)
{
  return get_has_allocated_blocks_cnt_in_(log_disk_path, segment_count);
}

bool ObServerLogBlockMgr::check_space_is_enough_(const int64_t log_disk_size) const
{
  bool bool_ret = false;
  int64_t runtime_log_disk_size = 0;
  int ret = OB_SUCCESS;
  if (OB_FAIL(get_runtime_log_disk_size_(runtime_log_disk_size))) {
  } else {
    bool_ret = runtime_log_disk_size <= log_disk_size;
    CLOG_LOG(INFO, "check_space_is_enough_ finished", K(runtime_log_disk_size), K(log_disk_size));
  }
  return bool_ret;
}

int ObServerLogBlockMgr::get_runtime_log_disk_size_(int64_t &runtime_log_disk_size) const
{
  int ret = OB_SUCCESS;
  runtime_log_disk_size = 0;
  Options opts;
  if (NULL == log_service_) {
  } else if (OB_FAIL(log_service_->get_wal_options(opts))) {
  } else {
    runtime_log_disk_size = opts.disk_options_.log_disk_usage_limit_size_;
  }
  return ret;
}

int64_t ObServerLogBlockMgr::get_in_use_size_() const
{
  return block_cnt_in_use_ * BLOCK_SIZE;
}

int ObServerLogBlockMgr::get_has_allocated_blocks_cnt_in_(
    const char *log_disk_path, int64_t &segment_count)
{
  int ret = OB_SUCCESS;
  common::ObPlatformDir *dir = NULL;
  const std::regex runtime_pattern(".*/sys");
  if (NULL == (dir = common::ob_opendir_utf8(log_disk_path))) {
    ret = OB_ERR_SYS;
    CLOG_LOG(WARN, "opendir failed", K(ret), K(errno), K(log_disk_path));
  } else {
    char entry_name[OB_MAX_FILE_NAME_LENGTH] = {'\0'};
    char current_file_path[OB_MAX_FILE_NAME_LENGTH] = {'\0'};
    int read_ret = 0;
    while (OB_SUCC(ret)
           && 1 == (read_ret = common::ob_readdir_utf8(dir, entry_name, sizeof(entry_name)))) {
      bool is_dir = false;
      MEMSET(current_file_path, '\0', OB_MAX_FILE_NAME_LENGTH);
      if (0 == strcmp(entry_name, ".") || 0 == strcmp(entry_name, "..")) {
      } else if (0 >= snprintf(current_file_path, OB_MAX_FILE_NAME_LENGTH, "%s/%s",
                               log_disk_path, entry_name)) {
        ret = OB_ERR_UNEXPECTED;
      } else if (OB_FAIL(FileDirectoryUtils::is_directory(current_file_path, is_dir))) {
      } else if (!is_dir || !std::regex_match(current_file_path, runtime_pattern)) {
        ret = OB_ERR_UNEXPECTED;
        LOG_DBA_ERROR_V2(OB_LOG_EXTERNAL_FILE_EXIST, ret, "Attention!!!",
            "There are unexpected files in the log directory.",
            "[suggestion] Please confirm whether manual deletion is required",
            ", unexpected path is ", current_file_path);
      } else {
        ret = scan_runtime_dir_(current_file_path, segment_count);
      }
    }
    if (OB_SUCC(ret) && 0 > read_ret) {
      ret = convert_sys_errno();
      CLOG_LOG(WARN, "read dir failed", K(ret), K(errno), K(log_disk_path));
    }
  }
  if (NULL != dir) {
    common::ob_closedir_utf8(dir);
  }
  return ret;
}

int ObServerLogBlockMgr::remove_tmp_file_or_directory_for_runtime_(const char *log_disk_path)
{
  int ret = OB_SUCCESS;
  char tmp_dir[OB_MAX_FILE_NAME_LENGTH] = {'\0'};
  bool exists = false;
  if (0 >= snprintf(tmp_dir, sizeof(tmp_dir), "%s/sys/tmp_dir", log_disk_path)) {
    ret = OB_ERR_UNEXPECTED;
  } else if (OB_FAIL(FileDirectoryUtils::is_exists(tmp_dir, exists))) {
  } else if (exists) {
    ret = FileDirectoryUtils::delete_directory_rec(tmp_dir);
  }
  return ret;
}

int ObServerLogBlockMgr::scan_runtime_dir_(const char *runtime_dir, int64_t &segment_count)
{
  int ret = OB_SUCCESS;
  common::ObPlatformDir *dir = NULL;
  if (NULL == (dir = common::ob_opendir_utf8(runtime_dir))) {
    ret = OB_ERR_SYS;
    CLOG_LOG(WARN, "opendir failed", K(ret), K(errno), K(runtime_dir));
  } else {
    char entry_name[OB_MAX_FILE_NAME_LENGTH] = {'\0'};
    char current_file_path[OB_MAX_FILE_NAME_LENGTH] = {'\0'};
    int read_ret = 0;
    while (OB_SUCC(ret)
           && 1 == (read_ret = common::ob_readdir_utf8(dir, entry_name, sizeof(entry_name)))) {
      bool is_dir = false;
      MEMSET(current_file_path, '\0', OB_MAX_FILE_NAME_LENGTH);
      if (0 == strcmp(entry_name, ".") || 0 == strcmp(entry_name, "..")) {
      } else if (0 >= snprintf(current_file_path, OB_MAX_FILE_NAME_LENGTH, "%s/%s",
                               runtime_dir, entry_name)) {
        ret = OB_ERR_UNEXPECTED;
      } else if (OB_FAIL(FileDirectoryUtils::is_directory(current_file_path, is_dir))) {
      } else if (!is_dir) {
        ret = OB_ERR_UNEXPECTED;
        LOG_DBA_ERROR_V2(OB_LOG_EXTERNAL_FILE_EXIST, ret, "Attention!!!",
            "There are unexpected files in the runtime log directory.",
            "[suggestion] Please confirm whether manual deletion is required",
            ", unexpected file is ", current_file_path);
      } else if (0 == strcmp(entry_name, "log_stream")) {
        ret = scan_ls_dir_(current_file_path, segment_count);
      } else if (0 != strcmp(entry_name, "tmp_dir")) {
        ret = OB_ERR_UNEXPECTED;
        LOG_DBA_ERROR_V2(OB_LOG_EXTERNAL_FILE_EXIST, ret, "Attention!!!",
            "There are unexpected directories in the runtime log directory.",
            "[suggestion] Please confirm whether manual deletion is required",
            ", unexpected directory is ", current_file_path);
      }
    }
    if (OB_SUCC(ret) && 0 > read_ret) {
      ret = convert_sys_errno();
      CLOG_LOG(WARN, "read dir failed", K(ret), K(errno), K(runtime_dir));
    }
  }
  if (NULL != dir) {
    common::ob_closedir_utf8(dir);
  }
  return ret;
}

int ObServerLogBlockMgr::scan_ls_dir_(const char *ls_dir, int64_t &segment_count)
{
  int ret = OB_SUCCESS;
  common::ObPlatformDir *dir = NULL;
  const std::regex segment_pattern("^[0-9a-f]{16}\\.log$");
  if (NULL == (dir = common::ob_opendir_utf8(ls_dir))) {
    ret = OB_ERR_SYS;
    CLOG_LOG(WARN, "opendir failed", K(ret), K(errno), K(ls_dir));
  } else {
    char entry_name[OB_MAX_FILE_NAME_LENGTH] = {'\0'};
    char current_file_path[OB_MAX_FILE_NAME_LENGTH] = {'\0'};
    int read_ret = 0;
    while (OB_SUCC(ret)
           && 1 == (read_ret = common::ob_readdir_utf8(dir, entry_name, sizeof(entry_name)))) {
      bool is_dir = false;
      MEMSET(current_file_path, '\0', OB_MAX_FILE_NAME_LENGTH);
      if (0 == strcmp(entry_name, ".") || 0 == strcmp(entry_name, "..")) {
      } else if (0 >= snprintf(current_file_path, OB_MAX_FILE_NAME_LENGTH, "%s/%s",
                               ls_dir, entry_name)) {
        ret = OB_ERR_UNEXPECTED;
      } else if (OB_FAIL(FileDirectoryUtils::is_directory(current_file_path, is_dir))) {
      } else if (!is_dir && (0 == strcmp(entry_name, "manifest")
                             || 0 == strcmp(entry_name, "manifest.tmp"))) {
      } else if (!is_dir && std::regex_match(entry_name, segment_pattern)) {
        ++segment_count;
      } else {
        ret = OB_ERR_UNEXPECTED;
        LOG_DBA_ERROR_V2(OB_LOG_EXTERNAL_FILE_EXIST, ret, "Attention!!!",
            "There are unexpected files in the log stream directory.",
            "[suggestion] Please confirm whether manual deletion is required",
            ", unexpected path is ", current_file_path);
      }
    }
    if (OB_SUCC(ret) && 0 > read_ret) {
      ret = convert_sys_errno();
      CLOG_LOG(WARN, "read dir failed", K(ret), K(errno), K(ls_dir));
    }
  }
  if (NULL != dir) {
    common::ob_closedir_utf8(dir);
  }
  return ret;
}

} // namespace logservice
} // namespace oceanbase
