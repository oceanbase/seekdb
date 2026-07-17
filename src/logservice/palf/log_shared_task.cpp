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

#include "log_shared_task.h"
#include "palf_env_impl.h"                    // PalfEnvImpl
#include "logservice/palf/palf_handle_impl_guard.h"

namespace oceanbase
{
namespace palf
{
LogSharedTask::LogSharedTask(const int64_t palf_epoch)
  : palf_epoch_(palf_epoch)
{}

LogSharedTask::~LogSharedTask()
{
  destroy();
}

void LogSharedTask::destroy()
{
  reset();
}

void LogSharedTask::reset()
{
  palf_epoch_ = -1;
}

LogHandleSubmitTask::LogHandleSubmitTask(const int64_t palf_epoch)
  : LogSharedTask(palf_epoch)
{}

LogHandleSubmitTask::~LogHandleSubmitTask()
{}

void LogHandleSubmitTask::free_this(IPalfEnvImpl *palf_env_impl)
{
  palf_env_impl->get_log_allocator()->free_log_handle_submit_task(this);
}

int LogHandleSubmitTask::do_task(IPalfEnvImpl *palf_env_impl)
{
  int ret = OB_SUCCESS;
  int64_t palf_epoch = -1;
  IPalfHandleImplGuard guard;
  common::ObTimeGuard time_guard("handle submit task", 100 * 1000);
  if (OB_FAIL(palf_env_impl->get_palf_handle_impl(guard))) {
    PALF_LOG(WARN, "IPalfEnvImpl get_palf_handle_impl failed", K(ret), KPC(this));
  } else if (OB_FAIL(guard.get_palf_handle_impl()->get_palf_epoch(palf_epoch))) {
    PALF_LOG(WARN, "IPalfEnvImpl get_palf_epoch failed", K(ret), KPC(this));
  } else if (palf_epoch != palf_epoch_) {
    PALF_LOG(WARN, "palf_epoch has changed, drop task", K(ret), K(palf_epoch), KPC(this));
  } else if (OB_FAIL(guard.get_palf_handle_impl()->try_handle_next_submit_log())) {
    PALF_LOG(WARN, "PalfHandleImpl try_handle_next_submit_log failed", K(ret), KPC(this));
  } else {
    PALF_LOG(TRACE, "LogHandleSubmitTask handle_task success", K(time_guard), KPC(this));
  }
  return ret;
}

} // end namespace palf
} // end namespace oceanbase
