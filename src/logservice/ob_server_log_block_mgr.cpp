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
#include "lib/utility/ob_platform_io.h"
#include "logservice/ob_log_service.h"

namespace oceanbase
{
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
      is_inited_(false)
{}

ObServerLogBlockMgr::~ObServerLogBlockMgr()
{
  destroy();
}

int ObServerLogBlockMgr::init(GetLogDiskInfoInConfig get_log_disk_info_in_config)
{
  int ret = OB_SUCCESS;
  if (IS_INIT) {
    ret = OB_INIT_TWICE;
    CLOG_LOG(ERROR, "ObServerLogBlockMgr inited twice", K(ret), KPC(this));
  } else if (OB_ISNULL(get_log_disk_info_in_config)) {
    ret = OB_INVALID_ARGUMENT;
    CLOG_LOG(ERROR, "Invalid argument", K(ret), KPC(this));
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
  int64_t total_size_byte = 0;
  if (IS_NOT_INIT) {
    ret = OB_NOT_INIT;
    CLOG_LOG(ERROR, "ObServerLogBlockMgr has not inited", K(ret), KPC(this));
  } else if (OB_ISNULL(log_service_)) {
    ret = OB_NOT_INIT;
    CLOG_LOG(ERROR, "log service is not bound", K(ret), KPC(this));
  } else if (OB_FAIL(log_service_->get_wal_disk_usage(in_use_size_byte, total_size_byte))) {
    CLOG_LOG(WARN, "failed to get WAL disk usage", K(ret), KPC(this));
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
  if (NULL == log_service_) {
  } else if (OB_FAIL(log_service_->get_wal_disk_soft_target(runtime_log_disk_size))) {
  }
  return ret;
}

} // namespace logservice
} // namespace oceanbase
