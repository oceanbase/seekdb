#include "rootserver/ob_root_service.h"
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

ObDDLHeartBeatTaskContainer::ObDDLHeartBeatTaskContainer() : is_inited_(false), bucket_lock_() {}
ObDDLHeartBeatTaskContainer::~ObDDLHeartBeatTaskContainer()
{
  bucket_lock_.destroy();
}
int ObDDLHeartBeatTaskContainer::init()
{
  int ret = OB_SUCCESS;
  ObMemAttr attr("register_tasks");
  SET_USE_500(attr);
  if (OB_UNLIKELY(is_inited_)) {
    ret = OB_INIT_TWICE;
    LOG_WARN("ObDDLHeartBeatTaskContainer inited twice", K(ret));
  } else if (OB_FAIL(register_tasks_.create(BUCKET_LOCK_BUCKET_CNT, attr, attr))) {
    LOG_WARN("failed to create register_tasks map", K(ret));
  } else if (OB_FAIL(bucket_lock_.init(BUCKET_LOCK_BUCKET_CNT))) {
    LOG_WARN("failed to init bucket lock", K(ret));
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
  } else {
    ObBucketHashWLockGuard lock_guard(bucket_lock_, task_id);
    if (OB_FAIL(register_tasks_.set_refactored(rootserver::ObDDLTaskID(task_id), 0))) {
      LOG_ERROR("set register task id failed", KR(ret));
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
  } else {
    ObBucketHashWLockGuard lock_guard(bucket_lock_, task_id);
    if (OB_FAIL(register_tasks_.erase_refactored(rootserver::ObDDLTaskID(task_id)))) {
      if (OB_HASH_NOT_EXIST == ret) {
        ret = OB_SUCCESS;
      } else {
        LOG_WARN("remove register task id failed", KR(ret), K(task_id));
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
    int64_t cnt = 0;
    ObAddr rs_leader_addr = GCTX.self_addr();
    ObArray<ObDDLHeartBeatTaskInfo> heart_beart_task_infos;
    while (OB_SUCC(ret) && cnt < RETRY_COUNT) {
      ObBucketTryRLockAllGuard all_reg_task_guard(bucket_lock_);
      if (OB_FAIL(all_reg_task_guard.get_ret())) {
        if (OB_EAGAIN == ret) {
          cnt++;
          LOG_INFO("all reg task guard failed, please try again, retry count: ", K(cnt));
          ret = OB_SUCCESS;
          sleep(RETRY_TIME_INTERVAL);
        }
      } else {
        for (common::hash::ObHashMap<rootserver::ObDDLTaskID, uint64_t>::iterator it = register_tasks_.begin(); OB_SUCC(ret) && it != register_tasks_.end(); it++) {
          int64_t task_id = it->first.task_id_;
          
          if (OB_FAIL(heart_beart_task_infos.push_back(ObDDLHeartBeatTaskInfo(task_id)))) {
            LOG_WARN("task_ids push_back failed", K(ret));
          }
        }
        break;
      }
    }
    if (OB_FAIL(ret)) {
    } else {
      for (int64_t i = 0; OB_SUCC(ret) && i < heart_beart_task_infos.count(); i++) {
        ObDDLHeartBeatTaskInfo heart_beart_task_info;
        obcall::ObUpdateDDLTaskActiveTimeArg arg;
        if (OB_FAIL(heart_beart_task_infos.at(i, heart_beart_task_info))) {
          LOG_WARN("get task id failed", K(ret));
        } else {
          int64_t task_id = heart_beart_task_info.get_task_id();
          
          arg.task_id_ = task_id;
          
          
          if (OB_FAIL(GCTX.root_service_->update_ddl_task_active_time(arg))) {
            LOG_WARN("send to task status fail", K(ret), K(rs_leader_addr), K(task_id));
          }
        }
      }
    }
  }
  return ret;
}

} // end of namespace storage
} // end of namespace oceanbase
