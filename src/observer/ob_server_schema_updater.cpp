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

#include "ob_server_schema_updater.h"
using namespace oceanbase::common;
using namespace oceanbase::share;
using namespace oceanbase::share::schema;

namespace oceanbase
{
namespace observer
{
ObServerSchemaTask::ObServerSchemaTask()
  : type_(INVALID), schema_info_()
{
}

ObServerSchemaTask::ObServerSchemaTask(TYPE type)
  : type_(type), schema_info_()
{
}

ObServerSchemaTask::ObServerSchemaTask(
  TYPE type,
  const int64_t schema_version)
  : type_(type), schema_info_()
{

  schema_info_.set_schema_version(schema_version);
}

bool ObServerSchemaTask::need_process_alone() const
{
  return RELEASE == type_;
}

bool ObServerSchemaTask::is_valid() const
{
  return INVALID != type_;
}

void ObServerSchemaTask::reset()
{
  type_ = INVALID;
  schema_info_.reset();
}

int64_t ObServerSchemaTask::hash() const
{
  uint64_t hash_val = 0;
  hash_val = murmurhash(&type_, sizeof(type_), hash_val);
  if (ASYNC_REFRESH == type_) {
    const uint64_t const_id = 1UL;
    const int64_t schema_version = get_schema_version();
    hash_val = murmurhash(&const_id, sizeof(const_id), hash_val);
    hash_val = murmurhash(&schema_version, sizeof(schema_version), hash_val);
  }
  return static_cast<int64_t>(hash_val);
}

bool ObServerSchemaTask::operator ==(const ObServerSchemaTask &other) const
{
  bool bret = (type_ == other.type_);
  if (bret && ASYNC_REFRESH == type_) {
    bret = (get_schema_version() == other.get_schema_version());
  }
  return bret;
}


bool ObServerSchemaTask::greator_than(
     const ObServerSchemaTask &lt,
     const ObServerSchemaTask &rt)
{
  bool bret = (lt.type_ > rt.type_);
  if (!bret && ASYNC_REFRESH == lt.type_ && ASYNC_REFRESH == rt.type_) {
    if (lt.get_schema_version() > rt.get_schema_version()) {
      bret = true;
    } else {
      bret = false;
    }
  }
  return bret;
}

bool ObServerSchemaTask::compare_without_version(const ObServerSchemaTask &other) const
{
  return (*this == other);
}

uint64_t ObServerSchemaTask::get_group_id() const
{
  return static_cast<uint64_t>(type_);
}

bool ObServerSchemaTask::is_barrier() const
{
  return false;
}

int ObServerSchemaUpdater::init(const common::ObAddr &host, ObMultiVersionSchemaService *schema_mgr)
{
  int ret = OB_SUCCESS;
  if (NULL == schema_mgr) {
    ret = OB_INVALID_ARGUMENT;
    LOG_WARN("schema_mgr must not null");
  } else if (OB_FAIL(task_queue_.init(this,
                                      SSU_MAX_THREAD_NUM,
                                      SSU_TASK_QUEUE_SIZE,
                                      "SerScheQueue"))) {
    LOG_WARN("init task queue failed", KR(ret), LITERAL_K(SSU_MAX_THREAD_NUM),
             LITERAL_K(SSU_TASK_QUEUE_SIZE));
  } else {
    host_ = host;
    schema_mgr_ = schema_mgr;
    inited_ = true;
  }
  return ret;
}

void ObServerSchemaUpdater::stop()
{
  if (!inited_) {
    LOG_WARN_RET(OB_NOT_INIT, "not init");
  } else {
    task_queue_.stop();
  }
}

void ObServerSchemaUpdater::wait()
{
  if (!inited_) {
    LOG_WARN_RET(OB_NOT_INIT, "not init");
  } else {
    task_queue_.wait();
  }
}

void ObServerSchemaUpdater::destroy()
{
 if (inited_) {
   stop();
   wait();
   host_.reset();
   schema_mgr_ = NULL;
   inited_ = false;
 }
}

int ObServerSchemaUpdater::process_barrier(const ObServerSchemaTask &task, bool &stopped)
{
  UNUSEDx(task, stopped);
  return OB_NOT_SUPPORTED;
}

int ObServerSchemaUpdater::batch_process_tasks(
    const ObIArray<ObServerSchemaTask> &batch_tasks, bool &stopped)
{
  int ret = OB_SUCCESS;
  ObCurTraceId::init(host_);
  ObArray<ObServerSchemaTask> tasks;
  if (!inited_) {
    ret = OB_NOT_INIT;
    LOG_WARN("ob_server_schema_updeter is not inited.", KR(ret));
  } else if (stopped) {
    ret = OB_CANCELED;
    LOG_WARN("ob_server_schema_updeter is stopped.", KR(ret));
  } else if (batch_tasks.count() <= 0) {
    ret = OB_INVALID_ARGUMENT;
    LOG_WARN("batch_tasks cnt is 0", KR(ret));
  } else if (OB_FAIL(tasks.assign(batch_tasks))) {
    LOG_WARN("fail to assign task", KR(ret), "task_cnt", batch_tasks.count());
  } else {
    DEBUG_SYNC(BEFORE_SET_NEW_SCHEMA_VERSION);
    lib::ob_sort(tasks.begin(), tasks.end(), ObServerSchemaTask::greator_than);
    ObServerSchemaTask::TYPE type = tasks.at(0).type_;
    if (ObServerSchemaTask::RELEASE == type && 1 != tasks.count()) {
      ret = OB_ERR_UNEXPECTED;
      LOG_WARN("release schema task should process alone",
               KR(ret), "task_cnt", tasks.count());
    } else if (ObServerSchemaTask::RELEASE == type) {
      if (OB_FAIL(process_release_task())) {
        LOG_WARN("fail to process release task", KR(ret), K(tasks.at(0)));
      }
    } else if (ObServerSchemaTask::ASYNC_REFRESH == type) {
      if (OB_FAIL(process_async_refresh_tasks(tasks))) {
        LOG_WARN("fail to process async refresh tasks", KR(ret));
      }
    } else {
      ret = OB_ERR_UNEXPECTED;
      LOG_WARN("invalid type", KR(ret), K(type));
    }
  }
  ObCurTraceId::reset();
  return ret;
}

int ObServerSchemaUpdater::process_release_task()
{
  int ret = OB_SUCCESS;
  ObTaskController::get().switch_task(share::ObTaskType::SCHEMA);
  THIS_WORKER.set_timeout_ts(INT64_MAX);
  if (OB_ISNULL(schema_mgr_)) {
    ret = OB_ERR_UNEXPECTED;
    LOG_WARN("schema_mgr_ is NULL", KR(ret));
  } else if (OB_FAIL(schema_mgr_->try_eliminate_schema_mgr())) {
    LOG_WARN("fail to eliminate schema mgr", KR(ret));
  }
  LOG_INFO("try to release schema", KR(ret));
  return ret;
}

int ObServerSchemaUpdater::process_async_refresh_tasks(
    const ObIArray<ObServerSchemaTask> &tasks)
{
  int ret = OB_SUCCESS;
  ObTaskController::get().switch_task(share::ObTaskType::SCHEMA);
  THIS_WORKER.set_timeout_ts(INT64_MAX);
  if (OB_ISNULL(schema_mgr_)) {
    ret = OB_ERR_UNEXPECTED;
    LOG_WARN("schema_mgr_ is NULL", KR(ret));
  } else {
    // Only the async refresh task with the maximum schema version needs execution.
    bool need_refresh = false;
    for (int64_t i = 0; OB_SUCC(ret) && i < tasks.count(); i++) {
      const ObServerSchemaTask &cur_task = tasks.at(i);
      if (ObServerSchemaTask::ASYNC_REFRESH != cur_task.type_) {
        ret = OB_ERR_UNEXPECTED;
        LOG_WARN("cur task type should be ASYNC_REFRESH", KR(ret), K(cur_task));
      } else if (i > 0) {
        const ObServerSchemaTask &last_task = tasks.at(i - 1);
        if (true
                && last_task.get_schema_version() < cur_task.get_schema_version()) {
          ret = OB_ERR_UNEXPECTED;
          LOG_WARN("cur task should be less than last task",
                   KR(ret), K(last_task), K(cur_task));
        }
      }
      if (OB_SUCC(ret)) {
        if (!need_refresh) {
          int64_t local_version = OB_INVALID_VERSION;
          int tmp_ret = OB_SUCCESS;
          if (i > 0) {
            // Tasks have been sorted by schema_version in desc order, so we just get the first task.
          } else if (OB_SUCCESS != (tmp_ret = schema_mgr_->get_runtime_refreshed_schema_version(
                     local_version))) { // ignore ret
            if (OB_ENTRY_NOT_EXIST != tmp_ret) {
              LOG_WARN("failed to get refreshed schema version", KR(tmp_ret));
            }
          } else if (cur_task.get_schema_version() > local_version) {
            need_refresh = true;
          }
        }
      }
    }
    if (OB_SUCC(ret) && need_refresh) {
      if (OB_FAIL(schema_mgr_->refresh_and_add_schema())) {
        LOG_WARN("fail to refresh schema", KR(ret));
      }
    }
  }
  LOG_INFO("try to async refresh schema", KR(ret));
  return ret;
}

int ObServerSchemaUpdater::try_release_schema()
{
  int ret = OB_SUCCESS;
  ObServerSchemaTask release_task(ObServerSchemaTask::RELEASE);
  if (!inited_) {
    ret = OB_NOT_INIT;
    LOG_WARN("ob_server_schema_updeter is not inited.", KR(ret));
  } else if (OB_FAIL(task_queue_.add(release_task))) {
    if (OB_EAGAIN != ret) {
      LOG_WARN("schedule release schema task failed", KR(ret));
    }
  } else {
    LOG_INFO("schedule release schema task", KR(ret));
  }
  return ret;
}

int ObServerSchemaUpdater::async_refresh_schema(const int64_t schema_version)
{
  int ret = OB_SUCCESS;
  DEBUG_SYNC(BEFORE_ADD_ASYNC_REFRESH_SCHEMA_TASK);
  ObServerSchemaTask refresh_task(ObServerSchemaTask::ASYNC_REFRESH, schema_version);
  if (!inited_) {
    ret = OB_NOT_INIT;
    LOG_WARN("ob_server_schema_updeter is not inited.", KR(ret));
  } else if (OB_FAIL(task_queue_.add(refresh_task))) {
    if (OB_EAGAIN != ret) {
      LOG_WARN("schedule async refresh schema task failed",
               KR(ret), K(schema_version));
    }
  } else {
    LOG_INFO("schedule async refresh schema task",
             KR(ret), K(schema_version));
  }
  return ret;
}

}
}
