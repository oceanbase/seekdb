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

#define USING_LOG_PREFIX RS
#include "ob_ddl_trans_controller.h"

#include "lib/ob_running_mode.h"
#include "share/rc/ob_module_provider.h"

namespace oceanbase
{
namespace share
{
namespace schema
{

int ObDDLTransController::init(share::schema::ObMultiVersionSchemaService *schema_service)
{
  int ret = OB_SUCCESS;
  if (!inited_) {
    for (int i=0; OB_SUCC(ret) && i < DDL_TASK_COND_SLOT; i++) {
      if (OB_FAIL(cond_slot_[i].init(ObWaitEventIds::DEFAULT_COND_WAIT))) {
        LOG_WARN("init cond fail", KR(ret));
      }
    }
    if (OB_FAIL(ret)) {
    } else if (OB_ISNULL(schema_service)) {
      ret = OB_INVALID_ARGUMENT;
      LOG_WARN("schema_service is null", KR(ret));
    } else if (FALSE_IT(use_shared_executor_ = lib::is_mini_mode())) {
    } else if (!use_shared_executor_
        && OB_FAIL(lib::ThreadPool::start())) {
      LOG_WARN("thread start fail", KR(ret));
    } else {
      schema_service_ = schema_service;
      inited_ = true;
    }
  }
  return ret;
}

int ObDDLTransController::start()
{
  int ret = OB_SUCCESS;
  if (!inited_) {
    ret = OB_NOT_INIT;
  } else if (!use_shared_executor_) {
    // The dedicated worker was started by init(), preserving normal mode.
  } else if (source_handle_.is_valid()) {
  } else if (OB_ISNULL(share::g_mp)
      || OB_ISNULL(background_executor_ =
          share::g_mp->background_task_executor())) {
    ret = OB_ERR_UNEXPECTED;
    LOG_WARN("background task executor is null", K(ret),
        KP(share::g_mp), KP(background_executor_));
  } else {
    share::ObBackgroundTaskSourceConfig config;
    config.name_ = "DDLTransCtr";
    config.max_concurrency_ = 1;
    if (OB_FAIL(background_executor_->register_source(
        *this, config, source_handle_))) {
      LOG_WARN("register ddl transaction controller source failed", K(ret));
    } else if (claim_refresh_request_()) {
      // Registration happens after schema-service initialization. Preserve a
      // refresh request that may have arrived during the startup interval.
      {
        common::SpinWLockGuard guard(lock_);
        need_refresh_ = true;
      }
      if (OB_FAIL(notify_background_source_())) {
        LOG_WARN("notify initial ddl transaction refresh failed", K(ret));
      }
    }
  }
  return ret;
}

void ObDDLTransController::stop()
{
  if (use_shared_executor_) {
    const int tmp_ret = unregister_background_source_(false);
    if (OB_SUCCESS != tmp_ret && OB_EAGAIN != tmp_ret) {
      LOG_WARN_RET(tmp_ret, "stop ddl transaction controller source failed",
          K(tmp_ret));
    }
  } else {
    lib::ThreadPool::stop();
    wait_cond_.signal();
  }
}

void ObDDLTransController::wait()
{
  if (use_shared_executor_) {
    const int tmp_ret = unregister_background_source_(true);
    if (OB_SUCCESS != tmp_ret) {
      LOG_WARN_RET(tmp_ret, "wait ddl transaction controller source failed",
          K(tmp_ret));
    }
  } else {
    wait_cond_.signal();
    lib::ThreadPool::wait();
  }
}

void ObDDLTransController::destroy()
{
  if (inited_) {
    stop();
    wait();
    if (!use_shared_executor_) {
      lib::ThreadPool::destroy();
    }
    inited_ = false;
    tasks_.destroy();
    schema_service_ = NULL;
    background_executor_ = NULL;
    source_handle_.reset();
  }
}

ObDDLTransController::~ObDDLTransController()
{
  destroy();
}

int ObDDLTransController::reserve_schema_version(const uint64_t schema_version_count)
{
  int ret = OB_SUCCESS;
  SpinWLockGuard guard(lock_);
  int64_t end_schema_version = OB_INVALID_VERSION;
  if (!inited_) {
    ret = OB_NOT_INIT;
    LOG_WARN("ObDDLTransController", KR(ret));
  } else if (OB_ISNULL(schema_service_)) {
    ret = OB_ERR_UNEXPECTED;
    LOG_WARN("ObDDLTransController", KR(ret));
  } else if (schema_version_count == 0) {
    ret = OB_INVALID_ARGUMENT;
    LOG_WARN("register_task_and_assign_schema_version", KR(ret), K(schema_version_count));
  } else if (OB_FAIL(schema_service_->gen_batch_new_schema_versions(schema_version_count, end_schema_version))) {
    LOG_WARN("fail to gen batch new schema versions", KR(ret), K(schema_version_count));
  }
  return ret;
}

int ObDDLTransController::create_task_and_assign_schema_version(const uint64_t schema_version_count,
    int64_t &task_id,
    ObIArray<int64_t> &schema_version_res)
{
  int ret = OB_SUCCESS;
  if (!inited_) {
    ret = OB_NOT_INIT;
    LOG_WARN("ObDDLTransController", KR(ret));
  } else if (OB_ISNULL(schema_service_)) {
    ret = OB_ERR_UNEXPECTED;
    LOG_WARN("ObDDLTransController", KR(ret));
  } else if (schema_version_count == 0 || schema_version_res.count() != 0) {
    ret = OB_INVALID_ARGUMENT;
    LOG_WARN("register_task_and_assign_schema_version", KR(ret), K(schema_version_count), K(schema_version_res));
  } else {
    int64_t end_schema_version = OB_INVALID_VERSION;
    SpinWLockGuard guard(lock_);
    if (OB_FAIL(schema_service_->gen_batch_new_schema_versions(schema_version_count, end_schema_version))) {
      LOG_WARN("fail to gen batch new schema versions", KR(ret), K(schema_version_count));
    } else if (OB_FAIL(schema_version_res.reserve(schema_version_count))) {
      LOG_WARN("fail to reserve memory", KR(ret), K(schema_version_count));
    } else {
      int64_t new_schema_version = end_schema_version -
      (schema_version_count - 1) * ObSchemaVersionGenerator::SCHEMA_VERSION_INC_STEP;
      for (int i = 0; OB_SUCC(ret) && i < schema_version_count; i++) {
        if (OB_FAIL(schema_version_res.push_back(new_schema_version))) {
          LOG_WARN("register_task_and_assign_schema_version", KR(ret));
        } else {
          new_schema_version += ObSchemaVersionGenerator::SCHEMA_VERSION_INC_STEP;
        }
      }
    }
    if (OB_SUCC(ret)) {
      int64_t first_schema_version = schema_version_res.at(0);
      int64_t last_schema_version = schema_version_res.at(schema_version_res.count() - 1);
      // Check the runtime schema version.
      for (int64_t i = tasks_.count() - 1; i >= 0; i--) {
        {
          if (first_schema_version <= tasks_.at(i).task_id_) {
            ret = OB_ERR_UNEXPECTED;
            LOG_WARN("assign schema_version", KR(ret), K(tasks_), K(schema_version_res));
          }
          break;
        }
      }
      if (OB_FAIL(ret)) {
      } else if (OB_FAIL(tasks_.push_back(TaskDesc{last_schema_version, false}))) {
        LOG_WARN("register_task_and_assign_schema_version", KR(ret));
      } else {
        task_id = last_schema_version;
      }
    }
  }
  LOG_INFO("create_task_and_assign_schema_version", KR(ret), K(task_id));
  return ret;
}

int ObDDLTransController::check_task_ready_(const int64_t task_id,
    bool &ready)
{
  int ret = OB_SUCCESS;
  int idx = OB_INVALID_INDEX;
  int pre_task_count = 0;
  SpinWLockGuard guard(lock_);
  for (int i = 0; i < tasks_.count(); i++) {
    {
      pre_task_count++;
      if (tasks_.at(i).task_id_ == task_id) {
        idx = i;
        break;
      }
    }
  }
  ready = false;
  if (OB_FAIL(ret)) {
  } else if (OB_INVALID_INDEX == idx) {
    ret = OB_ENTRY_NOT_EXIST;
    LOG_WARN("task_id not found", KR(ret), K(task_id), K(tasks_));
  } else if (pre_task_count == 0) {
    ret = OB_ERR_UNEXPECTED;
    LOG_WARN("pre_task is null", KR(ret), K(task_id), K(tasks_));
  } else if (pre_task_count == 1) {
    ready = true;
  } else {
    // gc end task
    for (int i = 0; i < 10; i++) {
      if (!tasks_.empty()
          && tasks_.at(0).task_end_
          && !(tasks_.at(0).task_id_ == task_id)) {
        LOG_INFO("gc parallel ddl task", K(tasks_.at(0)));
        int tmp_ret = OB_SUCCESS;
        if (OB_TMP_FAIL(tasks_.remove(0))) {
          LOG_WARN("check_task_ready", KR(tmp_ret));
        }
      } else {
        break;
      }
    }
  }
  return ret;
}

int ObDDLTransController::wait_task_ready(
    const int64_t task_id,
    const int64_t wait_us)
{
  int ret = OB_SUCCESS;
  bool ready = false;
  uint64_t cond_idx = task_id % DDL_TASK_COND_SLOT;
  int64_t start_time = ObTimeUtility::current_time();
  while (OB_SUCC(ret) && ObTimeUtility::current_time() - start_time < wait_us) {
    if (OB_FAIL(check_task_ready_(task_id, ready))) {
      LOG_WARN("wait_task_ready", KR(ret), K(task_id), K(ready));
    } else if (ready) {
      break;
    } else {
      ObThreadCondGuard guard(cond_slot_[cond_idx]);
      cond_slot_[cond_idx].wait(100);
    }
  }
  if (OB_FAIL(ret)) {
  } else if (!ready) {
    if (OB_FAIL(remove_task(task_id))) {
      LOG_WARN("fail to remove task", KR(ret), K(task_id));
    } else {
      ret = OB_TIMEOUT;
    }
    LOG_WARN("wait_task_ready", KR(ret), K(task_id), K(tasks_), K(ready));
  }
  return ret;
}

int ObDDLTransController::remove_task(const int64_t task_id)
{
  int ret = OB_SUCCESS;
  int idx = OB_INVALID_INDEX;
  bool need_wakeup = false;
  {
    SpinWLockGuard guard(lock_);
    for (int i = 0; i < tasks_.count(); i++) {
      if (tasks_.at(i).task_id_ == task_id) {
        tasks_.at(i).task_end_ = true;
        idx = i;
        LOG_INFO("remove parallel ddl task", K(tasks_.at(i)));
        if (OB_FAIL(tasks_.remove(i))) {
          LOG_WARN("remove_task fail", KR(ret), K(task_id));
        } else {
          need_refresh_ = true;
          need_wakeup = true;
        }
        break;
      }
    }
    if (OB_FAIL(ret)) {
    } else if (OB_INVALID_INDEX == idx) {
      ret = OB_ENTRY_NOT_EXIST;
      LOG_WARN("task_id not found", KR(ret), K(task_id), K(tasks_));
    } else {
      // wake up next
      for (int next = idx; next < tasks_.count(); next++) {
        int64_t next_task_id = tasks_.at(next).task_id_;
        uint64_t cond_idx = next_task_id % DDL_TASK_COND_SLOT;
        cond_slot_[cond_idx].broadcast();
        break;
      }
    }
  }
  if (need_wakeup) {
    if (use_shared_executor_) {
      const int tmp_ret = notify_background_source_();
      if (OB_SUCCESS != tmp_ret) {
        // The schema task is already committed and removed. Preserve the old
        // remove_task() result while retaining need_refresh_ for a later wake.
        LOG_WARN_RET(tmp_ret,
            "notify ddl transaction controller source failed", K(tmp_ret));
      }
    } else {
      wait_cond_.signal();
    }
  }
  return ret;
}

bool ObDDLTransController::claim_refresh_request_()
{
  bool need_refresh = false;
  SpinWLockGuard guard(lock_);
  need_refresh = need_refresh_;
  need_refresh_ = false;
  return need_refresh;
}

int ObDDLTransController::notify_background_source_()
{
  int ret = OB_SUCCESS;
  if (!use_shared_executor_) {
    wait_cond_.signal();
  } else if (OB_ISNULL(background_executor_)
      || !source_handle_.is_valid()) {
    ret = OB_NOT_RUNNING;
  } else if (OB_FAIL(background_executor_->notify(
      source_handle_, share::BG_TASK_HIGH))) {
    LOG_WARN("notify ddl transaction controller source failed", K(ret));
  }
  return ret;
}

int ObDDLTransController::unregister_background_source_(
    const bool wait_running)
{
  int ret = OB_SUCCESS;
  if (use_shared_executor_
      && OB_NOT_NULL(background_executor_)
      && source_handle_.is_valid()) {
    do {
      ret = background_executor_->unregister_source(source_handle_);
      if (wait_running && OB_EAGAIN == ret) {
        ob_usleep(1000);
      }
    } while (wait_running && OB_EAGAIN == ret);
    if (OB_ENTRY_NOT_EXIST == ret || OB_NOT_INIT == ret) {
      source_handle_.reset();
      ret = OB_SUCCESS;
    }
  }
  if (!source_handle_.is_valid()) {
    background_executor_ = NULL;
  }
  return ret;
}


} // end schema
} // end share
} // end oceanbase
