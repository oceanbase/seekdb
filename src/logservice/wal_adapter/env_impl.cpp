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

#define USING_LOG_PREFIX WAL
#include "logservice/wal_adapter/env_impl.h"
#include <cinttypes>
#include "lib/ob_running_mode.h"
#include "lib/time/ob_time_utility.h"
#include "lib/utility/ob_platform_io.h"
#include "share/rc/ob_server_runtime.h"
#include "log_meta.h"

namespace oceanbase
{
using namespace common;
using namespace share;
namespace wal
{
EnvImpl::EnvImpl()
    : wal_meta_lock_(common::ObLatchIds::WAL_ENV_LOCK), store_(nullptr), monitor_(NULL),
      disk_options_(), self_(), wal_handle_(nullptr), is_inited_(false), is_running_(false)
{
  log_dir_[0] = '\0';
  tmp_log_dir_[0] = '\0';
}

EnvImpl::~EnvImpl()
{
  destroy();
}

int EnvImpl::init(const Options &options, share::LocalStore &store,
    const char *base_dir, const ObAddr &self, MonitorCallback *monitor)
{
  int ret = OB_SUCCESS;
  int pret = 0;
  if (is_inited_) {
    ret = OB_INIT_TWICE;
  } else if (OB_ISNULL(base_dir) || !options.is_valid() || !self.is_valid() || OB_ISNULL(monitor)) {
    ret = OB_INVALID_ARGUMENT;
    WAL_LOG(ERROR, "invalid arguments", K(ret), K(base_dir), K(self), KP(monitor));
  } else if ((pret = snprintf(log_dir_, MAX_PATH_SIZE, "%s", base_dir)) < 0
             || pret >= MAX_PATH_SIZE) {
    ret = OB_BUF_NOT_ENOUGH;
  } else if ((pret = snprintf(tmp_log_dir_, MAX_PATH_SIZE, "%s/tmp_dir", log_dir_)) < 0
             || pret >= MAX_PATH_SIZE) {
    ret = OB_BUF_NOT_ENOUGH;
  } else {
    disk_options_ = options.disk_options_;
    store_ = &store;
    monitor_ = monitor;
    self_ = self;
    is_inited_ = true;
    is_running_ = true;
    WAL_LOG(INFO, "EnvImpl init success", K(ret), K(self_), KPC(this));
  }
  if (OB_FAIL(ret) && OB_INIT_TWICE != ret) {
    destroy();
  }
  return ret;
}

int EnvImpl::start()
{
  int ret = OB_SUCCESS;
  if (IS_NOT_INIT) {
    ret = OB_NOT_INIT;
  } else if (OB_FAIL(reload_wal_handle_impl_())) {
  } else if (OB_FAIL(remove_stale_incomplete_wal_())) {
  } else {
    is_running_ = true;
    WAL_LOG(INFO, "Env start success", K(ret));
  }
  return ret;
}

void EnvImpl::stop()
{
  WLockGuard guard(wal_meta_lock_);
  if (is_running_) {
    WAL_LOG(INFO, "EnvImpl begin stop", KPC(this));
    is_running_ = false;
    if (NULL != wal_handle_) {
      wal_handle_->destroy();
    }
    WAL_LOG(INFO, "EnvImpl stop success", KPC(this));
  }
}

void EnvImpl::wait()
{}

void EnvImpl::destroy()
{
  WLockGuard guard(wal_meta_lock_);
  is_running_ = false;
  is_inited_ = false;
  if (NULL != wal_handle_) {
    SERVER_DELETE(HandleImpl, "wal_env", wal_handle_);
    wal_handle_ = NULL;
  }
  store_ = nullptr;
  monitor_ = NULL;
  self_.reset();
  log_dir_[0] = '\0';
  tmp_log_dir_[0] = '\0';
  disk_options_.reset();
}

int EnvImpl::create_wal_handle_impl(const BaseInfo &wal_base_info, HandleImpl *&wal_handle_impl)
{
  WLockGuard guard(wal_meta_lock_);
  int ret = create_wal_handle_impl_(wal_base_info, wal_handle_impl);
  if (OB_FAIL(ret)) {
    wal_handle_impl = NULL;
  }
  return ret;
}

int EnvImpl::create_wal_handle_impl_(const BaseInfo &wal_base_info, HandleImpl *&wal_handle_impl)
{
  int ret = OB_SUCCESS;
  int pret = 0;
  char base_dir[MAX_PATH_SIZE] = {'\0'};
  HandleImpl *handle = NULL;
  if (IS_NOT_INIT) {
    ret = OB_NOT_INIT;
  } else if (!is_running_) {
    ret = OB_NOT_RUNNING;
  } else if (NULL != wal_handle_) {
    ret = OB_ENTRY_EXIST;
  } else if (0 > (pret = snprintf(base_dir, MAX_PATH_SIZE, "%s/log_stream", log_dir_))) {
    ret = OB_ERR_UNEXPECTED;
  } else if (NULL == (handle = SERVER_NEW(HandleImpl, "wal_env"))) {
    ret = OB_ALLOCATE_MEMORY_FAILED;
  } else if (OB_FAIL(create_directory(base_dir))) {
  } else if (OB_FAIL(handle->init(wal_base_info, base_dir, self_))) {
  } else {
    wal_handle_ = handle;
    (void)handle->set_monitor_cb(monitor_);
    wal_handle_impl = handle;
  }
  if (OB_FAIL(ret) && NULL != handle) {
    SERVER_DELETE(HandleImpl, "wal_env", handle);
    if (NULL == wal_handle_) {
      (void)remove_directory_while_exist_(base_dir);
    }
  }
  return ret;
}

int EnvImpl::remove_wal_handle_impl()
{
  WLockGuard guard(wal_meta_lock_);
  int ret = OB_SUCCESS;
  if (IS_NOT_INIT) {
    ret = OB_NOT_INIT;
  } else {
    ret = remove_wal_handle_impl_();
  }
  return ret;
}

int EnvImpl::get_wal_handle_impl(HandleImpl *&wal_handle_impl)
{
  RLockGuard guard(wal_meta_lock_);
  int ret = OB_SUCCESS;
  if (NULL == wal_handle_ || !wal_handle_->check_can_be_used()) {
    ret = OB_ENTRY_NOT_EXIST;
  } else {
    wal_handle_impl = wal_handle_;
  }
  return ret;
}

int EnvImpl::create_directory(const char *base_dir)
{
  int ret = OB_SUCCESS;
  int pret = 0;
  const mode_t mode = S_IRWXU | S_IRWXG | S_IROTH | S_IXOTH;
  char tmp_base_dir[MAX_PATH_SIZE] = {'\0'};
  if (0 > (pret = snprintf(tmp_base_dir, MAX_PATH_SIZE, "%s%s", base_dir, TMP_SUFFIX))) {
    ret = OB_ERR_UNEXPECTED;
  } else if (-1 == ob_mkdir_utf8(tmp_base_dir, mode)) {
    ret = convert_sys_errno();
  } else if (OB_FAIL(rename_directory(tmp_base_dir, base_dir))) {
  } else if (OB_FAIL(FileDirectoryUtils::fsync_dir(log_dir_))) {
  }
  if (OB_FAIL(ret)) {
    (void)remove_directory_while_exist_(tmp_base_dir);
    (void)remove_directory_while_exist_(base_dir);
  }
  return ret;
}

int EnvImpl::remove_directory(const char *log_dir)
{
  int ret = OB_SUCCESS;
  int pret = 0;
  char tmp_log_dir[MAX_PATH_SIZE] = {'\0'};
  if (0 > (pret = snprintf(tmp_log_dir, MAX_PATH_SIZE, "%s%s", log_dir, TMP_SUFFIX))) {
    ret = OB_ERR_UNEXPECTED;
  } else if (OB_FAIL(rename_directory(log_dir, tmp_log_dir))) {
  } else if (OB_FAIL(FileDirectoryUtils::fsync_dir(log_dir_))) {
  } else {
    char active_dir[MAX_PATH_SIZE] = {'\0'};
    (void)snprintf(active_dir, sizeof(active_dir), "%s/log_stream", log_dir_);
    if (0 == strcmp(log_dir, active_dir)) {
      ret = meta_store::remove(*store_);
    }
    if (OB_SUCC(ret)) {
      ret = FileDirectoryUtils::delete_directory_rec(tmp_log_dir);
    }
  }
  (void)FileDirectoryUtils::fsync_dir(log_dir_);
  return ret;
}

EnvImpl::RemoveStaleIncompleteWalFunctor::RemoveStaleIncompleteWalFunctor(
    EnvImpl *wal_env_impl) : wal_env_impl_(wal_env_impl)
{}

EnvImpl::RemoveStaleIncompleteWalFunctor::~RemoveStaleIncompleteWalFunctor()
{
  wal_env_impl_ = NULL;
}

int EnvImpl::RemoveStaleIncompleteWalFunctor::func(const char *entry_name)
{
  int ret = OB_SUCCESS;
  char *saveptr = NULL;
  char file_name[OB_MAX_FILE_NAME_LENGTH] = {'\0'};
  MEMCPY(file_name, entry_name, strlen(entry_name));
  char *tmp = strtok_r(file_name, "_", &saveptr);
  char *timestamp_str = NULL;
  if (NULL == tmp || NULL == (timestamp_str = strtok_r(NULL, "_", &saveptr))) {
    ret = OB_ERR_UNEXPECTED;
  } else {
    const int64_t timestamp = atol(timestamp_str);
    const int64_t current_timestamp = ObTimeUtility::current_time();
    constexpr int64_t week_us = 7 * 24 * 60 * 60 * 1000 * 1000ll;
    if (current_timestamp - timestamp > week_us) {
      char path[OB_MAX_FILE_NAME_LENGTH] = {'\0'};
      if (0 > snprintf(path, MAX_PATH_SIZE, "%s/%s", wal_env_impl_->tmp_log_dir_, entry_name)) {
        ret = OB_ERR_UNEXPECTED;
      } else {
        ret = FileDirectoryUtils::delete_directory_rec(path);
      }
    }
  }
  return ret;
}

int EnvImpl::get_disk_usage(int64_t &used_size_byte, int64_t &total_usable_size_byte)
{
  RLockGuard guard(wal_meta_lock_);
  int ret = get_disk_usage_(used_size_byte);
  if (OB_SUCC(ret)) {
    total_usable_size_byte = disk_options_.log_disk_usage_limit_size_;
  }
  return ret;
}

int EnvImpl::update_options(const Options &options)
{
  WLockGuard guard(wal_meta_lock_);
  int ret = OB_SUCCESS;
  if (IS_NOT_INIT) {
    ret = OB_NOT_INIT;
  } else if (!options.is_valid()) {
    ret = OB_INVALID_ARGUMENT;
  } else {
    disk_options_ = options.disk_options_;
  }
  return ret;
}

int EnvImpl::get_options(Options &options)
{
  RLockGuard guard(wal_meta_lock_);
  int ret = OB_SUCCESS;
  if (IS_NOT_INIT) {
    ret = OB_NOT_INIT;
  } else {
    options.disk_options_ = disk_options_;
  }
  return ret;
}

int EnvImpl::reload_wal_handle_impl_()
{
  int ret = OB_SUCCESS;
  int pret = 0;
  HandleImpl *handle = nullptr;
  char base_dir[OB_MAX_FILE_NAME_LENGTH] = {'\0'};
  bool is_integrity = true;
  bool dir_exist = false;
  if (0 > (pret = snprintf(base_dir, MAX_PATH_SIZE, "%s/log_stream", log_dir_))) {
    ret = OB_ERR_UNEXPECTED;
  } else if (OB_FAIL(FileDirectoryUtils::is_exists(base_dir, dir_exist))) {
  } else if (!dir_exist) {
    LogMeta meta;
    block_id_t expected_next_block_id = 0;
    const int load_ret = meta_store::load(*store_, meta, expected_next_block_id);
    if (OB_ENTRY_NOT_EXIST == load_ret) {
    } else if (OB_SUCCESS != load_ret) {
      ret = load_ret;
    } else {
      ret = OB_ERR_UNEXPECTED;
      WAL_LOG(ERROR, "WAL metadata exists but its log directory is missing",
          K(ret), K(base_dir), K(expected_next_block_id));
    }
  } else if (NULL == (handle = SERVER_NEW(HandleImpl, "wal_env"))) {
    ret = OB_ALLOCATE_MEMORY_FAILED;
  } else if (OB_FAIL(handle->load(base_dir, self_, is_integrity))) {
  } else {
    wal_handle_ = handle;
    (void)handle->set_monitor_cb(monitor_);
  }
  if (OB_FAIL(ret) && NULL != handle && NULL == wal_handle_) {
    SERVER_DELETE(HandleImpl, "wal_env", handle);
  } else if (OB_SUCC(ret) && !is_integrity) {
    ret = move_incomplete_wal_into_tmp_dir_();
  }
  return ret;
}

int EnvImpl::get_total_used_disk_space_(int64_t &total_used_disk_space,
    int64_t &total_unrecyclable_disk_space)
{
  int ret = OB_SUCCESS;
  if (NULL == wal_handle_) {
    total_used_disk_space = 0;
    total_unrecyclable_disk_space = 0;
  } else {
    ret = wal_handle_->get_total_used_disk_space(
        total_used_disk_space, total_unrecyclable_disk_space);
  }
  return ret;
}

int EnvImpl::get_disk_usage_(int64_t &used_size_byte, int64_t &unrecyclable_disk_space)
{
  return get_total_used_disk_space_(used_size_byte, unrecyclable_disk_space);
}

int EnvImpl::get_disk_usage_(int64_t &used_size_byte)
{
  int64_t unrecyclable_disk_space = 0;
  return get_disk_usage_(used_size_byte, unrecyclable_disk_space);
}

int EnvImpl::remove_wal_handle_impl_()
{
  int ret = OB_SUCCESS;
  if (NULL == wal_handle_) {
    ret = OB_ENTRY_NOT_EXIST;
  } else {
    SERVER_DELETE(HandleImpl, "wal_env", wal_handle_);
    wal_handle_ = nullptr;
    char active_dir[MAX_PATH_SIZE] = {'\0'};
    if (0 > snprintf(active_dir, sizeof(active_dir), "%s/log_stream", log_dir_)) {
      ret = OB_ERR_UNEXPECTED;
    } else {
      ret = remove_directory(active_dir);
    }
  }
  return ret;
}

int EnvImpl::move_incomplete_wal_into_tmp_dir_()
{
  int ret = OB_SUCCESS;
  int pret = 0;
  const mode_t mode = S_IRWXU | S_IRWXG | S_IROTH | S_IXOTH;
  char src_log_dir[OB_MAX_FILE_NAME_LENGTH] = {'\0'};
  char dest_log_dir[OB_MAX_FILE_NAME_LENGTH] = {'\0'};
  bool tmp_dir_exist = false;
  const int64_t timestamp = ObTimeUtility::current_time();
  HandleImpl *old_handle = wal_handle_;
  wal_handle_ = nullptr;
  if (NULL != old_handle) {
    SERVER_DELETE(HandleImpl, "wal_env", old_handle);
  }
  if (OB_FAIL(check_tmp_log_dir_exist_(tmp_dir_exist))) {
  } else if (!tmp_dir_exist && (-1 == ob_mkdir_utf8(tmp_log_dir_, mode))) {
    ret = convert_sys_errno();
  } else if (0 > (pret = snprintf(src_log_dir, MAX_PATH_SIZE, "%s/log_stream", log_dir_))) {
    ret = OB_ERR_UNEXPECTED;
  } else if (0 > (pret = snprintf(dest_log_dir, MAX_PATH_SIZE, "%s/log_stream_%" PRId64,
      tmp_log_dir_, timestamp))) {
    ret = OB_ERR_UNEXPECTED;
  } else if (OB_FAIL(rename_directory(src_log_dir, dest_log_dir))) {
  } else if (OB_FAIL(FileDirectoryUtils::fsync_dir(log_dir_))) {
  } else {
    ret = meta_store::remove(*store_);
  }
  return ret;
}

int EnvImpl::check_tmp_log_dir_exist_(bool &exist) const
{
  return FileDirectoryUtils::is_exists(tmp_log_dir_, exist);
}

int EnvImpl::remove_stale_incomplete_wal_()
{
  int ret = OB_SUCCESS;
  bool exist = false;
  RemoveStaleIncompleteWalFunctor functor(this);
  if (OB_FAIL(check_tmp_log_dir_exist_(exist))) {
  } else if (exist) {
    ret = scan_directory(tmp_log_dir_, functor);
  }
  return ret;
}

int EnvImpl::get_io_start_time(int64_t &last_working_time)
{
  RLockGuard guard(wal_meta_lock_);
  int ret = OB_SUCCESS;
  last_working_time = OB_INVALID_TIMESTAMP;
  if (IS_NOT_INIT) {
    ret = OB_NOT_INIT;
  } else if (NULL != wal_handle_) {
    ret = wal_handle_->get_io_start_time(last_working_time);
  }
  return ret;
}

int EnvImpl::remove_directory_while_exist_(const char *log_dir)
{
  int ret = OB_SUCCESS;
  bool result = true;
  if (OB_FAIL(FileDirectoryUtils::is_exists(log_dir, result))) {
  } else if (result) {
    ret = remove_directory(log_dir);
  }
  return ret;
}

} // namespace wal
} // namespace oceanbase
