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

#include "lib/ob_running_mode.h"
#include "lib/stat/ob_diagnostic_info_guard.h"
#include "ob_server_schema_updater.h"
#include "observer/ob_server.h"
#include "share/rc/ob_module_provider.h"

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

ObServerSchemaTask::ObServerSchemaTask(
  TYPE type,
  const ObRefreshSchemaInfo &schema_info)
  : type_(type), schema_info_(schema_info)
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
  return REFRESH == type_ || RELEASE == type_;
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
  } else if (FALSE_IT(use_shared_executor_ = lib::is_mini_mode())) {
  } else if (use_shared_executor_
      && OB_FAIL(task_queue_.init_without_thread(
          this,
          SSU_MAX_THREAD_NUM,
          SSU_TASK_QUEUE_SIZE,
          "SerScheQueue",
          &stopping_))) {
    LOG_WARN("init externally driven schema task queue failed", KR(ret),
             LITERAL_K(SSU_MAX_THREAD_NUM), LITERAL_K(SSU_TASK_QUEUE_SIZE));
  } else if (!use_shared_executor_
      && OB_FAIL(task_queue_.init(this,
          SSU_MAX_THREAD_NUM,
          SSU_TASK_QUEUE_SIZE,
          "SerScheQueue"))) {
    LOG_WARN("init task queue failed", KR(ret), LITERAL_K(SSU_MAX_THREAD_NUM),
             LITERAL_K(SSU_TASK_QUEUE_SIZE));
  } else {
    host_ = host;
    schema_mgr_ = schema_mgr;
    ATOMIC_STORE(&stopping_, false);
    inited_ = true;
  }
  if (OB_FAIL(ret)) {
    task_queue_.destroy();
  }
  return ret;
}

int ObServerSchemaUpdater::start_background_task_source()
{
  int ret = OB_SUCCESS;
  if (!inited_) {
    ret = OB_NOT_INIT;
  } else if (!use_shared_executor_) {
  } else if (source_handle_.is_valid()) {
  } else if (OB_ISNULL(share::g_mp)
      || OB_ISNULL(background_executor_ =
          share::g_mp->background_task_executor())) {
    ret = OB_ERR_UNEXPECTED;
    LOG_WARN("background task executor is null", K(ret),
        KP(share::g_mp), KP(background_executor_));
  } else {
    share::ObBackgroundTaskSourceConfig config;
    config.name_ = "SerScheQueue";
    config.max_concurrency_ = 1;
    ATOMIC_STORE(&stopping_, false);
    if (OB_FAIL(background_executor_->register_source(
        *this, config, source_handle_))) {
      LOG_WARN("register server schema source failed", K(ret));
    } else if (task_queue_.task_count() > 0
        && OB_FAIL(notify_background_source_())) {
      LOG_WARN("notify pending server schema task failed", K(ret));
    }
  }
  return ret;
}

void ObServerSchemaUpdater::stop()
{
  if (!inited_) {
    LOG_WARN_RET(OB_NOT_INIT, "not init");
  } else if (use_shared_executor_) {
    ATOMIC_STORE(&stopping_, true);
    const int tmp_ret = unregister_background_source_(false);
    if (OB_SUCCESS != tmp_ret && OB_EAGAIN != tmp_ret) {
      LOG_WARN_RET(tmp_ret, "stop server schema source failed", K(tmp_ret));
    }
  } else {
    task_queue_.stop();
  }
}

void ObServerSchemaUpdater::wait()
{
  if (!inited_) {
    LOG_WARN_RET(OB_NOT_INIT, "not init");
  } else if (use_shared_executor_) {
    const int tmp_ret = unregister_background_source_(true);
    if (OB_SUCCESS != tmp_ret) {
      LOG_WARN_RET(tmp_ret, "wait server schema source failed", K(tmp_ret));
    }
  } else {
    task_queue_.wait();
  }
}

void ObServerSchemaUpdater::destroy()
{
 if (inited_) {
   stop();
   wait();
   task_queue_.destroy();
   host_.reset();
   schema_mgr_ = NULL;
   background_executor_ = NULL;
   source_handle_.reset();
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
    if ((ObServerSchemaTask::REFRESH == type || ObServerSchemaTask::RELEASE == type)
        && (1 != tasks.count())) {
      ret = OB_ERR_UNEXPECTED;
      LOG_WARN("refresh/release schema task should process alone",
               KR(ret), "task_cnt", tasks.count());
    } else if (ObServerSchemaTask::REFRESH == type) {
      if (OB_FAIL(process_refresh_task(tasks.at(0)))) {
        LOG_WARN("fail to process refresh task", KR(ret), K(tasks.at(0)));
      }
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

int ObServerSchemaUpdater::process_one_quantum(
    const share::ObBackgroundTaskPriority priority,
    share::ObBackgroundTaskRunResult &result)
{
  int ret = OB_SUCCESS;
  int64_t processed_count = 0;
  bool has_more_ready = false;
  const int64_t saved_worker_timeout_ts = THIS_WORKER.get_timeout_ts();
  if (!use_shared_executor_) {
    ret = OB_STATE_NOT_MATCH;
  } else if (share::BG_TASK_HIGH != priority) {
    ret = OB_INVALID_ARGUMENT;
  } else if (!ATOMIC_LOAD(&stopping_)
      && OB_FAIL(task_queue_.process_one_quantum(
          processed_count, has_more_ready))) {
    LOG_WARN("process server schema task quantum failed", K(ret));
  } else if (!ATOMIC_LOAD(&stopping_)) {
    result.processed_count_ = processed_count;
    result.has_more_ready_ = has_more_ready;
  }
  THIS_WORKER.set_timeout_ts(saved_worker_timeout_ts);
  return ret;
}

int ObServerSchemaUpdater::notify_background_source_()
{
  int ret = OB_SUCCESS;
  if (!use_shared_executor_) {
  } else if (OB_ISNULL(background_executor_)
      || !source_handle_.is_valid()) {
    ret = OB_NOT_RUNNING;
  } else if (OB_FAIL(background_executor_->notify(
      source_handle_, share::BG_TASK_HIGH))) {
    LOG_WARN("notify server schema source failed", K(ret));
  }
  return ret;
}

int ObServerSchemaUpdater::unregister_background_source_(
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

int ObServerSchemaUpdater::decide_schema_refresh_(
    const ObRefreshSchemaInfo &local_schema_info,
    const ObRefreshSchemaInfo &new_schema_info,
    bool &skip_refresh)
{
  int ret = OB_SUCCESS;
  skip_refresh = false;
  
  const ObDDLSequenceID local_sequence_id = local_schema_info.get_sequence_id();
  const ObDDLSequenceID new_sequence_id = new_schema_info.get_sequence_id();
  switch (new_sequence_id.compare_to_other_id(local_sequence_id)) {
    case ObDDLSequenceID::NOT_COMPARABLE:
    case ObDDLSequenceID::MORE_OVER: {
      // refresh all schema
      skip_refresh = false;
      LOG_INFO("[REFRESH_SCHEMA] sequence_id is not comparable or local schema is far behind,"
               " refresh all schema", K(new_sequence_id), K(local_sequence_id));
      break;
    }
    case ObDDLSequenceID::LESS_THAN:
    case ObDDLSequenceID::EQUAL_TO: {
      // do not refresh any schema
      skip_refresh = true;
      LOG_INFO("[REFRESH_SCHEMA] local schema is newer, do not refresh any schema",
               K(new_sequence_id), K(local_sequence_id));
      break;
    }
    case ObDDLSequenceID::ONE_OVER: {
      skip_refresh = false;
      LOG_INFO("[REFRESH_SCHEMA] refresh schema",
               K(new_sequence_id), K(local_sequence_id));
      break;
    }
    default: {
      ret = OB_INVALID_ARGUMENT;
      LOG_WARN("unexpect compare result", KR(ret));
      break;
    }
  }
  return ret;
}

int ObServerSchemaUpdater::process_refresh_task(const ObServerSchemaTask &task)
{
  ObASHSetInnerSqlWaitGuard ash_inner_sql_guard(ObInnerSqlWaitTypeId::REFRESH_SCHEMA);
  int ret = OB_SUCCESS;
  const ObRefreshSchemaInfo &schema_info = task.schema_info_;
  ObRefreshSchemaInfo local_schema_info;
  bool skip_refresh = false;
  ObTaskController::get().switch_task(share::ObTaskType::SCHEMA);
  THIS_WORKER.set_timeout_ts(INT64_MAX);
  LOG_INFO("[REFRESH_SCHEMA] start to process schema refresh task", KR(ret), K(schema_info));
  if (OB_ISNULL(schema_mgr_)) {
    ret = OB_ERR_UNEXPECTED;
    LOG_WARN("schema_mgr_ is NULL", KR(ret));
  } else if (OB_FAIL(schema_mgr_->get_last_refreshed_schema_info(local_schema_info))) {
    LOG_WARN("fail to get local schema info", KR(ret));
  } else if (OB_FAIL(decide_schema_refresh_(local_schema_info, schema_info, skip_refresh))) {
    LOG_WARN("fail to decide schema refresh", KR(ret), K(schema_info), K(local_schema_info), K(skip_refresh));
  } else if (skip_refresh) {
    // skip
    LOG_INFO("[REFRESH_SCHEMA] local schema info is newer, no need to refresh schema",
              KR(ret), K(local_schema_info), K(schema_info));
  } else {
    int64_t begin_time = ::oceanbase::common::ObTimeUtility::current_time();
    LOG_INFO("[REFRESH_SCHEMA] begin refresh schema, ", K(begin_time), K(schema_info));
    bool check_bootstrap = GCTX.in_bootstrap_;
    // GCTX.in_bootstrap_ = false only when sys full schema version is refreshed
    // check bootstrap to avoid refreshing schema too early
    if (FAILEDx(schema_mgr_->refresh_and_add_schema(check_bootstrap))) {
      LOG_WARN("fail to refresh and add schema", KR(ret), K(check_bootstrap));
    } else if (OB_FAIL(schema_mgr_->set_last_refreshed_schema_info(schema_info))) {
      LOG_WARN("fail to set last_refreshed_schema_info", KR(ret), K(schema_info));
    }
    LOG_INFO("[REFRESH_SCHEMA] end refresh schema with new mode, ",
             KR(ret), "used time", ObTimeUtility::current_time() - begin_time,
             K(check_bootstrap), K(schema_info));
  }

  int tmp_ret = OB_SUCCESS;
  if (OB_TMP_FAIL(try_load_baseline_schema_version_())) { // ignore ret
    LOG_WARN("fail to load baseline schema version", KR(tmp_ret));
  }

  // For performance, schema_guard will be cached in one session instead of each SQL statement constructs its own new schema_guard,
  // which may lead to lack of schema slots since schema guard will hold reference of schema mgr in long time.
  // To avoid -4023 error caused by lack of schema slots while refresh schema, observer should try to release cached schema_guard in different sessions in such situation.
  if (OB_EAGAIN == ret) {
    OBSERVER.get_sql_session_mgr().try_check_session();
  }

  // dump schema statistics info
  if (REACH_TIME_INTERVAL(10 * 60 * 1000 * 1000)) { // 10min
    if (OB_NOT_NULL(schema_mgr_)) {
      schema_mgr_->dump_schema_statistics();
    }
  }
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
  ObASHSetInnerSqlWaitGuard ash_inner_sql_guard(ObInnerSqlWaitTypeId::ASYNC_REFRESH_SCHEMA);
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

int ObServerSchemaUpdater::try_reload_schema(
    const ObRefreshSchemaInfo &schema_info,
    const bool set_received_schema_version)
{

  int ret = OB_SUCCESS;
  if (!inited_) {
    ret = OB_NOT_INIT;
    LOG_WARN("ob_server_schema_updeter is not inited.", KR(ret));
  } else if (OB_ISNULL(schema_mgr_)) {
    ret = OB_ERR_UNEXPECTED;
    LOG_WARN("schema_service is null", KR(ret));
  } else {
    // Try to update received_broadcast_version which used to check if local schema is new enough for SQL execution.
    // Ignore errors before the runtime completes its first schema refresh.
    int tmp_ret = OB_SUCCESS;
    if (true
        && schema_info.get_schema_version() > 0
        && set_received_schema_version
        && OB_TMP_FAIL(schema_mgr_->set_runtime_received_broadcast_version(
           schema_info.get_schema_version()))) {
      LOG_WARN("fail to set runtime received broadcast version", K(tmp_ret), K(schema_info));
    }

    DEBUG_SYNC(BEFORE_ADD_REFRESH_SCHEMA_TASK);
    ObServerSchemaTask refresh_task(ObServerSchemaTask::REFRESH, schema_info);
    if (OB_FAIL(task_queue_.add(refresh_task))) {
      if (OB_EAGAIN != ret) {
        LOG_WARN("schedule fetch new schema task failed", KR(ret), K(schema_info));
      }
    } else {
      LOG_INFO("schedule fetch new schema task", KR(ret), K(schema_info));
      if (use_shared_executor_) {
        const int tmp_ret = notify_background_source_();
        if (OB_SUCCESS != tmp_ret) {
          LOG_WARN_RET(tmp_ret,
              "notify schema refresh task failed", K(tmp_ret));
        }
      }
    }
  }
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
    if (use_shared_executor_) {
      const int tmp_ret = notify_background_source_();
      if (OB_SUCCESS != tmp_ret) {
        LOG_WARN_RET(tmp_ret,
            "notify schema release task failed", K(tmp_ret));
      }
    }
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
    if (use_shared_executor_) {
      const int tmp_ret = notify_background_source_();
      if (OB_SUCCESS != tmp_ret) {
        LOG_WARN_RET(tmp_ret,
            "notify async schema refresh task failed", K(tmp_ret));
      }
    }
  }
  return ret;
}

int ObServerSchemaUpdater::try_load_baseline_schema_version_()
{
  int ret = OB_SUCCESS;
  if (OB_ISNULL(schema_mgr_)) {
    ret = OB_ERR_UNEXPECTED;
    LOG_WARN("schema_mgr_ is NULL", KR(ret));
  }

  {
    ObSchemaGetterGuard guard;
    if (FAILEDx(schema_mgr_->get_runtime_schema_guard(guard))) {
      LOG_WARN("fail to get schema guard", KR(ret));
    }
  }

  int64_t timeout = GCONF.rpc_timeout;
  int64_t baseline_schema_version = OB_INVALID_VERSION; // not used
  if (OB_SUCC(ret)) { // ignore ret
    int tmp_ret = OB_SUCCESS;
    ObTimeoutCtx ctx;
    if (OB_TMP_FAIL(ctx.set_timeout(timeout))) {
      LOG_WARN("fail to set timeout", KR(tmp_ret), K(timeout));
    } else if (OB_TMP_FAIL(schema_mgr_->get_baseline_schema_version(
      true/*auto_update*/, baseline_schema_version))) {
      LOG_WARN("fail to update baseline schema version", KR(tmp_ret));
    }
  }
  return ret;
}
}
}
