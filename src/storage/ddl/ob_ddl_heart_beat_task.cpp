#include "rootserver/ob_local_management_service.h"
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

#include "ob_ddl_heart_beat_task.h"

#ifdef _WIN32
#include <windows.h>
#define sleep(sec) Sleep((sec) * 1000)
#endif

namespace oceanbase
{
namespace storage
{
ObRedefTableHeartBeatTask::ObRedefTableHeartBeatTask() : is_inited_(false) {}

int ObRedefTableHeartBeatTask::init(common::ObTimer &timer)
{
  int ret = OB_SUCCESS;
  if (OB_UNLIKELY(is_inited_)) {
    ret = OB_INIT_TWICE;
    LOG_WARN("ObReDefTableHeartBeatTask has a already been inited", K(ret));
  } else if (OB_FAIL(timer.schedule(*this, HEARTBEAT_INTERVAL, true))) {
    LOG_WARN("fail to schedule task ObReDefTableHeartBeatTask", K(ret));
  } else {
    is_inited_ = true;
  }
  return ret;
}

void ObRedefTableHeartBeatTask::runTimerTask()
{
  int ret = OB_SUCCESS;
  if (OB_UNLIKELY(!is_inited_)) {
    ret = OB_NOT_INIT;
    LOG_WARN("ObReDefTableHeartBeatTask has not been inited", K(ret));
  } else if (OB_FAIL(send_task_status_to_rs())) {
    LOG_WARN("send to rs all task status failed", KR(ret));
  } else {
    LOG_INFO("send to rs all task status succeed");
  }
}

int ObRedefTableHeartBeatTask::send_task_status_to_rs()
{
  int ret = OB_SUCCESS;
  if (OB_FAIL(OB_DDL_HEART_BEAT_TASK_CONTAINER.send_task_status_to_rs())) {
    LOG_WARN("failed to send task status to RS", K(ret));
  }
  return ret;
}

ObDDLHeartBeatTaskContainer::ObDDLHeartBeatTaskContainer() : register_tasks_(), is_inited_(false), lock_() {}
ObDDLHeartBeatTaskContainer::~ObDDLHeartBeatTaskContainer()
{
}
int ObDDLHeartBeatTaskContainer::init()
{
  int ret = OB_SUCCESS;
  if (OB_UNLIKELY(is_inited_)) {
    ret = OB_INIT_TWICE;
    LOG_WARN("ObDDLHeartBeatTaskContainer inited twice", K(ret));
  } else {
    is_inited_ = true;
  }
  return ret;
}

int ObDDLHeartBeatTaskContainer::set_register_task_id(const int64_t task_id)
{
  int ret = OB_SUCCESS;
  if (OB_UNLIKELY(!is_inited_)) {
    ret = OB_NOT_INIT;
    LOG_WARN("ObDDLHeartBeatTaskContainer not inited", K(ret));
  } else if (OB_UNLIKELY(!rootserver::ObDDLTaskID(task_id).is_valid())) {
    ret = OB_INVALID_ARGUMENT;
    LOG_WARN("invalid argument", K(ret), K(task_id));
  } else {
    const rootserver::ObDDLTaskID ddl_task_id(task_id);
    bool found = false;
    common::ObSpinLockGuard guard(lock_);
    for (int64_t i = 0; !found && i < register_tasks_.count(); ++i) {
      if (register_tasks_.at(i) == ddl_task_id) {
        found = true;
      }
    }
    if (!found && OB_FAIL(register_tasks_.push_back(ddl_task_id))) {
      LOG_ERROR("set register task id failed", KR(ret), K(task_id));
    }
  }
  return ret;
}

int ObDDLHeartBeatTaskContainer::remove_register_task_id(const int64_t task_id)
{
  int ret = OB_SUCCESS;
  if (OB_UNLIKELY(!is_inited_)) {
    ret = OB_NOT_INIT;
    LOG_WARN("ObDDLHeartBeatTaskContainer not inited", K(ret));
  } else if (OB_UNLIKELY(!rootserver::ObDDLTaskID(task_id).is_valid())) {
    ret = OB_INVALID_ARGUMENT;
    LOG_WARN("invalid argument", K(ret), K(task_id));
  } else {
    const rootserver::ObDDLTaskID ddl_task_id(task_id);
    common::ObSpinLockGuard guard(lock_);
    for (int64_t i = 0; OB_SUCC(ret) && i < register_tasks_.count(); ++i) {
      if (register_tasks_.at(i) == ddl_task_id) {
        if (OB_FAIL(register_tasks_.remove(i))) {
          LOG_WARN("remove register task id failed", KR(ret), K(task_id));
        }
        break;
      }
    }
  }
  return ret;
}

int ObDDLHeartBeatTaskContainer::send_task_status_to_rs()
{
  int ret = OB_SUCCESS;
  if (OB_UNLIKELY(!is_inited_)) {
    ret = OB_NOT_INIT;
    LOG_WARN("ObDDLHeartBeatTaskContainer not inited", K(ret));
  } else {
    ObAddr rs_leader_addr = GCTX.self_addr();
    ObArray<rootserver::ObDDLTaskID> task_ids;
    {
      common::ObSpinLockGuard guard(lock_);
      for (int64_t i = 0; OB_SUCC(ret) && i < register_tasks_.count(); ++i) {
        if (OB_FAIL(task_ids.push_back(register_tasks_.at(i)))) {
          LOG_WARN("task_ids push back failed", K(ret));
        }
      }
    }
    if (OB_FAIL(ret)) {
    } else {
      for (int64_t i = 0; OB_SUCC(ret) && i < task_ids.count(); i++) {
        obcall::ObUpdateDDLTaskActiveTimeArg arg;
        int64_t task_id = task_ids.at(i).task_id_;
        arg.task_id_ = task_id;
        if (OB_FAIL(GCTX.local_management_service_->update_ddl_task_active_time(arg))) {
          LOG_WARN("send to task status fail", K(ret), K(rs_leader_addr), K(task_id));
        }
      }
    }
  }
  return ret;
}

} // end of namespace storage
} // end of namespace oceanbase
