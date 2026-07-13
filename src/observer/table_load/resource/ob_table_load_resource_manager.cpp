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

#define USING_LOG_PREFIX SERVER

#include "observer/table_load/resource/ob_table_load_resource_manager.h"
#include "observer/omt/ob_multi_tenant.h"
#include "observer/table_load/ob_table_load_service.h"
#include "share/rc/ob_module_provider.h"

namespace oceanbase
{
namespace observer
{
using namespace common;
using namespace lib;

void ObTableLoadResourceManager::ObRefreshMemoryTask::runTimerTask()
{
  int ret = OB_SUCCESS;
  if (OB_FAIL(manager_.refresh_memory_limit_())) {
    LOG_WARN("fail to refresh table load memory limit", KR(ret));
  }
}

ObTableLoadResourceManager::ObTableLoadResourceManager()
  : refresh_memory_task_(*this),
    memory_total_(0),
    memory_remain_(0),
    is_stop_(false),
    is_inited_(false)
{
}

int ObTableLoadResourceManager::init()
{
  int ret = OB_SUCCESS;
  const int64_t bucket_num = 1024;
  if (IS_INIT) {
    ret = OB_INIT_TWICE;
    LOG_WARN("resource manager init twice", KR(ret));
  } else if (OB_FAIL(assigned_tasks_.create(bucket_num, "TLD_AssignedMgr", "TLD_AssignedMgr"))) {
    LOG_WARN("fail to create assigned task map", KR(ret));
  } else {
    is_inited_ = true;
  }
  return ret;
}

int ObTableLoadResourceManager::start()
{
  int ret = OB_SUCCESS;
  if (IS_NOT_INIT) {
    ret = OB_NOT_INIT;
    LOG_WARN("resource manager not init", KR(ret));
  } else if (OB_FAIL(share::g_mp->shared_timer()->schedule(
      refresh_memory_task_, REFRESH_MEMORY_INTERVAL, true))) {
    LOG_WARN("fail to schedule table load memory refresh task", KR(ret));
  }
  return ret;
}

void ObTableLoadResourceManager::stop()
{
  share::g_mp->shared_timer()->cancel_task(refresh_memory_task_);
  {
    ObMutexGuard guard(mutex_);
    is_stop_ = true;
  }
}

int ObTableLoadResourceManager::wait()
{
  share::g_mp->shared_timer()->wait_task(refresh_memory_task_);
  return release_all_resource();
}

void ObTableLoadResourceManager::destroy()
{
  share::g_mp->shared_timer()->cancel_task(refresh_memory_task_);
  share::g_mp->shared_timer()->wait_task(refresh_memory_task_);
  ObMutexGuard guard(mutex_);
  assigned_tasks_.destroy();
  memory_total_ = 0;
  memory_remain_ = 0;
  is_inited_ = false;
}

int64_t ObTableLoadResourceManager::get_required_memory_(const ObDirectLoadResourceApplyArg &arg)
{
  int64_t required_memory = 0;
  for (int64_t i = 0; i < arg.apply_array_.count(); ++i) {
    required_memory += arg.apply_array_[i].memory_size_;
  }
  return required_memory;
}

int ObTableLoadResourceManager::refresh_memory_limit_()
{
  int ret = OB_SUCCESS;
  int64_t new_memory_total = 0;
  ObMutexGuard refresh_guard(refresh_mutex_);
  if (OB_FAIL(ObTableLoadService::get_memory_limit(new_memory_total))) {
    LOG_WARN("fail to get table load memory limit", KR(ret));
  } else if (OB_FAIL(ObTableLoadService::refresh_avail_memory(new_memory_total))) {
    LOG_WARN("fail to refresh table load available memory", KR(ret), K(new_memory_total));
  } else {
    ObMutexGuard guard(mutex_);
    const int64_t memory_used = memory_total_ - memory_remain_;
    if (new_memory_total != memory_total_) {
      memory_total_ = new_memory_total;
      memory_remain_ = new_memory_total - memory_used;
      LOG_INFO("refresh table load memory limit", K_(memory_total), K_(memory_remain), K(memory_used));
    }
  }
  return ret;
}

int ObTableLoadResourceManager::apply_resource(ObDirectLoadResourceApplyArg &arg)
{
  int ret = OB_SUCCESS;
  if (IS_NOT_INIT) {
    ret = OB_NOT_INIT;
    LOG_WARN("resource manager not init", KR(ret));
  } else if (OB_UNLIKELY(!arg.is_valid())) {
    ret = OB_INVALID_ARGUMENT;
    LOG_WARN("invalid resource request", KR(ret), K(arg));
  } else {
    {
      ObMutexGuard guard(mutex_);
      ObResourceAssigned assigned;
      if (is_stop_) {
        ret = OB_IN_STOP_STATE;
      } else if (OB_SUCCESS == assigned_tasks_.get_refactored(arg.task_key_, assigned)) {
        // Repeated apply is idempotent.
        return OB_SUCCESS;
      }
    }
    int tmp_ret = OB_SUCCESS;
    if (OB_SUCC(ret) && OB_TMP_FAIL(refresh_memory_limit_())) {
      // Keep serving with the last valid limit; the periodic task will retry.
      LOG_WARN("fail to refresh table load memory limit", KR(tmp_ret));
    }
    if (OB_SUCC(ret)) {
      ObMutexGuard guard(mutex_);
      ObResourceAssigned assigned;
      const int64_t required_memory = get_required_memory_(arg);
      if (is_stop_) {
        ret = OB_IN_STOP_STATE;
      } else if (OB_SUCCESS == assigned_tasks_.get_refactored(arg.task_key_, assigned)) {
        // Repeated apply is idempotent.
      } else if (required_memory > memory_remain_) {
        ret = OB_EAGAIN;
      } else if (OB_FAIL(assigned_tasks_.set_refactored(arg.task_key_, ObResourceAssigned(arg)))) {
        LOG_WARN("fail to record assigned resource", KR(ret), K(arg));
      } else {
        memory_remain_ -= required_memory;
        LOG_INFO("assign table load resource", K(arg), K_(memory_remain), K_(memory_total));
      }
    }
  }
  return ret;
}

int ObTableLoadResourceManager::release_resource(ObDirectLoadResourceReleaseArg &arg)
{
  int ret = OB_SUCCESS;
  if (IS_NOT_INIT) {
    ret = OB_NOT_INIT;
    LOG_WARN("resource manager not init", KR(ret));
  } else if (OB_UNLIKELY(!arg.is_valid())) {
    ret = OB_INVALID_ARGUMENT;
    LOG_WARN("invalid resource release", KR(ret), K(arg));
  } else {
    ObMutexGuard guard(mutex_);
    ObResourceAssigned assigned;
    if (OB_HASH_NOT_EXIST == (ret = assigned_tasks_.get_refactored(arg.task_key_, assigned))) {
      ret = OB_SUCCESS;
    } else if (OB_FAIL(ret)) {
      LOG_WARN("fail to get assigned resource", KR(ret), K(arg));
    } else if (OB_FAIL(assigned_tasks_.erase_refactored(arg.task_key_))) {
      LOG_WARN("fail to erase assigned resource", KR(ret), K(arg));
    } else {
      memory_remain_ += get_required_memory_(assigned.apply_arg_);
      LOG_INFO("release table load resource", K(arg), K_(memory_remain), K_(memory_total));
    }
  }
  return ret;
}

int ObTableLoadResourceManager::release_all_resource()
{
  ObMutexGuard guard(mutex_);
  int ret = assigned_tasks_.clear();
  if (OB_SUCC(ret)) {
    memory_remain_ = memory_total_;
  }
  return ret;
}

} // namespace observer
} // namespace oceanbase
