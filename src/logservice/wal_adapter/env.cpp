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
#include "logservice/wal_adapter/env.h"
#include "share/rc/ob_server_runtime.h"
#include "handle.h"

namespace oceanbase
{
namespace wal
{
Env::Env()
    : lock_(common::ObLatchIds::WAL_ENV_LOCK),
      rust_env_(),
      wal_handle_(nullptr),
      is_inited_(false)
{}

Env::~Env()
{
  destroy_();
}

int Env::create_wal_env(
    const int64_t disk_soft_target,
    const char *base_dir,
    Env *&wal_env)
{
  int ret = OB_SUCCESS;
  wal_env = SERVER_NEW(Env, "Env");
  if (OB_ISNULL(wal_env)) {
    ret = OB_ALLOCATE_MEMORY_FAILED;
  } else if (OB_ISNULL(base_dir) || disk_soft_target < 0) {
    ret = OB_INVALID_ARGUMENT;
  } else {
    wal_env->rust_env_.emplace(wal_rs::create_env(
        ::rust::Str(base_dir), static_cast<uint64_t>(disk_soft_target)));
    wal_env->is_inited_ = true;
  }
  if (OB_NOT_NULL(wal_env) && OB_FAIL(ret)) {
    SERVER_DELETE(Env, "Env", wal_env);
    wal_env = NULL;
  }
  return ret;
}

void Env::destroy_wal_env(Env *&wal_env)
{
  SERVER_DELETE(Env, "wal_env", wal_env);
}

int Env::start()
{
  WLockGuard guard(lock_);
  int ret = OB_SUCCESS;
  if (!is_inited_) {
    ret = OB_NOT_INIT;
  } else if (OB_FAIL(from_rust_code(wal_rs::start_env_ffi(**rust_env_)))) {
  } else {
    if (OB_NOT_NULL(wal_handle_)) {
      wal_handle_->destroy();
      wal_handle_.reset();
    }
    WAL_LOG(INFO, "Env start success");
  }
  return ret;
}

void Env::stop_()
{
  WLockGuard guard(lock_);
  if (OB_NOT_NULL(wal_handle_)) {
    wal_handle_->destroy();
    wal_handle_.reset();
  }
  if (rust_env_.has_value()) {
    wal_rs::stop_env_ffi(**rust_env_);
  }
}

void Env::destroy_()
{
  stop_();
  WLockGuard guard(lock_);
  rust_env_.reset();
  is_inited_ = false;
}

int Env::wrap_stream_(::rust::Box<wal_rs::Stream> stream, HandleImplPtr &handle)
{
  int ret = OB_SUCCESS;
  HandleImplPtr new_handle(SERVER_NEW(HandleImpl, "wal_env"), [](HandleImpl *ptr) {
    SERVER_DELETE(HandleImpl, "wal_env", ptr);
  });
  if (OB_ISNULL(new_handle)) {
    ret = OB_ALLOCATE_MEMORY_FAILED;
  } else if (OB_FAIL(new_handle->init(std::move(stream)))) {
  } else {
    handle = new_handle;
  }
  return ret;
}

int Env::create(const BaseInfo &wal_base_info, Handle &handle)
{
  WLockGuard guard(lock_);
  int ret = OB_SUCCESS;
  HandleImplPtr new_handle;
  if (!is_inited_) {
    ret = OB_NOT_INIT;
  } else if (!wal_base_info.is_valid()) {
    ret = OB_INVALID_ARGUMENT;
  } else {
    wal_rs::BaseInfo base {
        static_cast<uint64_t>(wal_base_info.curr_lsn_.val_),
        static_cast<uint64_t>(wal_base_info.prev_log_info_.lsn_.val_),
        wal_base_info.prev_log_info_.scn_.get_val_for_logservice(),
        wal_base_info.prev_log_info_.log_id_,
        static_cast<uint64_t>(wal_base_info.prev_log_info_.accum_checksum_)};
    auto stream = wal_rs::create_env_stream_ffi(**rust_env_, base, 0);
    if (OB_FAIL(from_rust_code(wal_rs::status_ffi(*stream)))) {
    } else if (OB_FAIL(wrap_stream_(std::move(stream), new_handle))) {
      (void)wal_rs::remove_env_ffi(**rust_env_);
    } else {
      wal_handle_ = new_handle;
      handle.wal_handle_impl_ = new_handle;
    }
  }
  if (OB_FAIL(ret)) {
    handle.wal_handle_impl_.reset();
  }
  return ret;
}

int Env::open(Handle &handle)
{
  WLockGuard guard(lock_);
  int ret = OB_SUCCESS;
  if (!is_inited_) {
    ret = OB_NOT_INIT;
  } else if (OB_NOT_NULL(wal_handle_) && wal_handle_->check_can_be_used()) {
    handle.wal_handle_impl_ = wal_handle_;
  } else {
    HandleImplPtr new_handle;
    auto stream = wal_rs::open_env_stream_ffi(**rust_env_);
    if (OB_FAIL(wrap_stream_(std::move(stream), new_handle))) {
    } else {
      wal_handle_ = new_handle;
      handle.wal_handle_impl_ = new_handle;
    }
  }
  if (OB_FAIL(ret)) {
    handle.wal_handle_impl_.reset();
  }
  return ret;
}

void Env::close(Handle &handle)
{
  (void)handle.unregister_file_size_cb();
  handle.wal_handle_impl_.reset();
}

int Env::remove()
{
  WLockGuard guard(lock_);
  int ret = OB_SUCCESS;
  if (!is_inited_) {
    ret = OB_NOT_INIT;
  } else {
    if (OB_NOT_NULL(wal_handle_)) {
      wal_handle_->destroy();
      wal_handle_.reset();
    }
    ret = from_rust_code(wal_rs::remove_env_ffi(**rust_env_));
  }
  return ret;
}

int Env::get_disk_usage(int64_t &used_size_byte, int64_t &total_size_byte)
{
  RLockGuard guard(lock_);
  int ret = OB_SUCCESS;
  if (!is_inited_) {
    ret = OB_NOT_INIT;
  } else {
    const auto usage = wal_rs::disk_usage_env_ffi(**rust_env_);
    ret = from_rust_code(usage.code);
    if (OB_SUCC(ret)) {
      used_size_byte = static_cast<int64_t>(usage.used);
      total_size_byte = static_cast<int64_t>(usage.total);
    }
  }
  return ret;
}

int Env::get_disk_soft_target(int64_t &disk_soft_target)
{
  RLockGuard guard(lock_);
  int ret = OB_SUCCESS;
  if (!is_inited_) {
    ret = OB_NOT_INIT;
  } else {
    disk_soft_target = static_cast<int64_t>(wal_rs::disk_soft_target_env_ffi(**rust_env_));
  }
  return ret;
}

int Env::update_disk_soft_target(const int64_t disk_soft_target)
{
  WLockGuard guard(lock_);
  int ret = OB_SUCCESS;
  if (!is_inited_) {
    ret = OB_NOT_INIT;
  } else if (disk_soft_target < 0) {
    ret = OB_INVALID_ARGUMENT;
  } else {
    wal_rs::update_disk_soft_target_env_ffi(
        **rust_env_, static_cast<uint64_t>(disk_soft_target));
  }
  return ret;
}

} // namespace wal
} // namespace oceanbase
