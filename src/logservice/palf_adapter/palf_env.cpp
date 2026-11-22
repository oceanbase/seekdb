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

#define USING_LOG_PREFIX PALF
#include "logservice/palf_adapter/palf_env.h"
#include "share/rc/ob_server_runtime.h"
#include "palf_handle.h"

namespace oceanbase
{
namespace palf
{
PalfEnv::PalfEnv() : palf_env_impl_()
{

}

PalfEnv::~PalfEnv()
{
  stop_();
  wait_();
  destroy_();
}

int PalfEnv::create_palf_env(
    const PalfOptions &options,
    share::LocalStore &store,
    const char *base_dir,
    const common::ObAddr &self,
    PalfMonitorCb *monitor,
    PalfEnv *&palf_env)
{
  int ret = OB_SUCCESS;
  palf_env = SERVER_NEW(PalfEnv, "PalfEnv");
  if (NULL == palf_env) {
    ret = OB_ALLOCATE_MEMORY_FAILED;
  } else if (OB_FAIL(FileDirectoryUtils::delete_tmp_file_or_directory_at(base_dir))) {
  } else if (OB_FAIL(palf_env->palf_env_impl_.init(options, store, base_dir, self, monitor))) {
  } else {
    PALF_LOG(INFO, "create_palf_handle_impl success", K(base_dir));
  }
  if (NULL != palf_env && OB_FAIL(ret)) {
    SERVER_DELETE(PalfEnv, "PalfEnv", palf_env);
    palf_env = NULL;
  }
  return ret;
}

void PalfEnv::destroy_palf_env(PalfEnv *&palf_env)
{
  SERVER_DELETE(PalfEnv, "palf_env", palf_env);
  PALF_LOG_RET(WARN, OB_SUCCESS, "destroy_palf_env success", K(palf_env));
}

int PalfEnv::start()
{
  int ret = OB_SUCCESS;
  ret = palf_env_impl_.start();
  return ret;
}

void PalfEnv::stop_()
{
  palf_env_impl_.stop();
}

void PalfEnv::wait_()
{
  palf_env_impl_.wait();
}

void PalfEnv::destroy_()
{
  palf_env_impl_.destroy();
}

int PalfEnv::create(const AccessMode &access_mode,
                    const PalfBaseInfo &palf_base_info,
                    PalfHandle &handle)
{
  int ret = OB_SUCCESS;
  palf::PalfHandleImpl *palf_handle_impl = NULL;
  if (OB_FAIL(palf_env_impl_.create_palf_handle_impl(access_mode, palf_base_info, palf_handle_impl))) {
  } else if (FALSE_IT(handle.palf_handle_impl_ = palf_handle_impl)) {
  } else {
    PALF_LOG(INFO, "create palf handle success");
  }
  if (OB_FAIL(ret)) {
    handle.palf_handle_impl_ = NULL;
  }
  return ret;
}

int PalfEnv::open(PalfHandle &handle)
{
  int ret = OB_SUCCESS;
  palf::PalfHandleImpl *palf_handle_impl = NULL;
  if (OB_FAIL(palf_env_impl_.get_palf_handle_impl(palf_handle_impl))) {
  } else if (FALSE_IT(handle.palf_handle_impl_ = palf_handle_impl)) {
  } else {
  }
  if (OB_FAIL(ret)) {
    handle.palf_handle_impl_ = NULL;
  }
  return ret;
}

void PalfEnv::close(PalfHandle &handle)
{
  (void)handle.unregister_file_size_cb();
  handle.palf_handle_impl_ = NULL;
}

int PalfEnv::remove()
{
  return palf_env_impl_.remove_palf_handle_impl();
}

int PalfEnv::get_disk_usage(int64_t &used_size_byte, int64_t &total_size_byte)
{
  return palf_env_impl_.get_disk_usage(used_size_byte, total_size_byte);
}

int PalfEnv::get_options(PalfOptions &options)
{
  return palf_env_impl_.get_options(options);
}

int PalfEnv::update_options(const PalfOptions &options)
{
  return palf_env_impl_.update_options(options);
}

int PalfEnv::get_io_start_time(int64_t &last_working_time)
{
  return palf_env_impl_.get_io_start_time(last_working_time);
}

} // end namespace palf
} // end namespace oceanbase
