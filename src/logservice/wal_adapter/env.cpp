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
Env::Env() : wal_env_impl_()
{

}

Env::~Env()
{
  stop_();
  wait_();
  destroy_();
}

int Env::create_wal_env(
    const Options &options,
    share::LocalStore &store,
    const char *base_dir,
    const common::ObAddr &self,
    MonitorCallback *monitor,
    Env *&wal_env)
{
  int ret = OB_SUCCESS;
  wal_env = SERVER_NEW(Env, "Env");
  if (NULL == wal_env) {
    ret = OB_ALLOCATE_MEMORY_FAILED;
  } else if (OB_FAIL(FileDirectoryUtils::delete_tmp_file_or_directory_at(base_dir))) {
  } else if (OB_FAIL(wal_env->wal_env_impl_.init(options, store, base_dir, self, monitor))) {
  } else {
    WAL_LOG(INFO, "create_wal_handle_impl success", K(base_dir));
  }
  if (NULL != wal_env && OB_FAIL(ret)) {
    SERVER_DELETE(Env, "Env", wal_env);
    wal_env = NULL;
  }
  return ret;
}

void Env::destroy_wal_env(Env *&wal_env)
{
  SERVER_DELETE(Env, "wal_env", wal_env);
  WAL_LOG_RET(WARN, OB_SUCCESS, "destroy_wal_env success", K(wal_env));
}

int Env::start()
{
  int ret = OB_SUCCESS;
  ret = wal_env_impl_.start();
  return ret;
}

void Env::stop_()
{
  wal_env_impl_.stop();
}

void Env::wait_()
{
  wal_env_impl_.wait();
}

void Env::destroy_()
{
  wal_env_impl_.destroy();
}

int Env::create(const BaseInfo &wal_base_info, Handle &handle)
{
  int ret = OB_SUCCESS;
  wal::HandleImpl *wal_handle_impl = NULL;
  if (OB_FAIL(wal_env_impl_.create_wal_handle_impl(wal_base_info, wal_handle_impl))) {
  } else if (FALSE_IT(handle.wal_handle_impl_ = wal_handle_impl)) {
  } else {
    WAL_LOG(INFO, "create wal handle success");
  }
  if (OB_FAIL(ret)) {
    handle.wal_handle_impl_ = NULL;
  }
  return ret;
}

int Env::open(Handle &handle)
{
  int ret = OB_SUCCESS;
  wal::HandleImpl *wal_handle_impl = NULL;
  if (OB_FAIL(wal_env_impl_.get_wal_handle_impl(wal_handle_impl))) {
  } else if (FALSE_IT(handle.wal_handle_impl_ = wal_handle_impl)) {
  } else {
  }
  if (OB_FAIL(ret)) {
    handle.wal_handle_impl_ = NULL;
  }
  return ret;
}

void Env::close(Handle &handle)
{
  (void)handle.unregister_file_size_cb();
  handle.wal_handle_impl_ = NULL;
}

int Env::remove()
{
  return wal_env_impl_.remove_wal_handle_impl();
}

int Env::get_disk_usage(int64_t &used_size_byte, int64_t &total_size_byte)
{
  return wal_env_impl_.get_disk_usage(used_size_byte, total_size_byte);
}

int Env::get_options(Options &options)
{
  return wal_env_impl_.get_options(options);
}

int Env::update_options(const Options &options)
{
  return wal_env_impl_.update_options(options);
}

int Env::get_io_start_time(int64_t &last_working_time)
{
  return wal_env_impl_.get_io_start_time(last_working_time);
}

} // end namespace wal
} // end namespace oceanbase
