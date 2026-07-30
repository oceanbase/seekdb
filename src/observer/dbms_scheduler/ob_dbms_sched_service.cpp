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

#include "observer/dbms_scheduler/ob_dbms_sched_service.h"
#include "lib/ob_running_mode.h"
#include "share/rc/ob_module_provider.h"
#include "share/rc/ob_server_runtime.h"
#define USING_LOG_PREFIX SERVER

namespace oceanbase
{
using namespace common;
using namespace oceanbase::share;
namespace rootserver
{

int ObDBMSSchedService::server_module_init(ObDBMSSchedService *&dbms_sched_service)
{
  return dbms_sched_service->init();
}

int ObDBMSSchedService::init()
{
  int ret = OB_SUCCESS;
  if (job_master_.is_inited()) {
    ret = OB_INIT_TWICE;
    LOG_WARN("has inited", KR(ret));
  } else if (FALSE_IT(use_shared_executor_ = lib::is_mini_mode())) {
  } else if (OB_FAIL(job_master_.init(GCTX.sql_proxy_, GCTX.schema_service_))) {
    LOG_WARN("[DBMS_SCHED_SERVICE] job master init failed");
  } else if (!use_shared_executor_
      && OB_FAIL(ObServerThreadHelper::create(
      "DBMSSched",
      1))) {
    LOG_WARN("[DBMS_SCHED_SERVICE] fail to create thread", KR(ret));
  } else {
    LOG_INFO("[DBMS_SCHED_SERVICE] ObDBMSSchedService init success");
  }
  return ret;
}

int ObDBMSSchedService::start()
{
  int ret = OB_SUCCESS;
  if (!job_master_.is_inited()) {
    ret = OB_NOT_INIT;
    LOG_WARN("not init", K(ret), K(job_master_.is_inited()));
  } else if (OB_FAIL(job_master_.start())) {
    LOG_WARN("[DBMS_SCHED_SERVICE] job master start failed", K(ret));
  } else if (!use_shared_executor_
      && OB_FAIL(ObServerThreadHelper::start())) {
    LOG_WARN("[DBMS_SCHED_SERVICE] failed to start thread", KR(ret));
  } else {
    LOG_INFO("[DBMS_SCHED_SERVICE] ObDBMSSchedService start success");
  }
  return ret;
}

void ObDBMSSchedService::do_work()
{
  int ret = OB_SUCCESS;
  if (!job_master_.is_inited()) {
    ret = OB_NOT_INIT;
    LOG_WARN("not init", K(ret), K(job_master_.is_inited()));
  } else if (OB_FAIL(job_master_.scheduler())) {
    LOG_WARN("[DBMS_SCHED_SERVICE] job master sched failed", K(ret));
  }
}

int ObDBMSSchedService::process_one_quantum(
    const share::ObBackgroundTaskPriority priority,
    share::ObBackgroundTaskRunResult &result)
{
  int ret = OB_SUCCESS;
  const int64_t saved_worker_timeout_ts = THIS_WORKER.get_timeout_ts();
  if (!use_shared_executor_) {
    ret = OB_STATE_NOT_MATCH;
  } else if (share::BG_TASK_NORMAL != priority) {
    ret = OB_INVALID_ARGUMENT;
  } else if (OB_FAIL(job_master_.process_one_quantum(
      result.processed_count_,
      result.has_more_ready_,
      result.next_ready_ts_))) {
    LOG_WARN("failed to process dbms scheduler quantum", K(ret));
  }
  THIS_WORKER.set_timeout_ts(saved_worker_timeout_ts);
  return ret;
}

void ObDBMSSchedService::stop()
{
  int ret = OB_SUCCESS;
  if (!job_master_.is_inited()) {
    ret = OB_NOT_INIT;
    LOG_WARN("not init", K(ret), K(job_master_.is_inited()));
  } else if (use_shared_executor_) {
    job_master_.switch_to_follower();
    if (OB_FAIL(unregister_background_source_(true))) {
      LOG_WARN("[DBMS_SCHED_SERVICE] unregister source failed", K(ret));
    }
    const int tmp_ret = job_master_.stop();
    if (OB_SUCCESS != tmp_ret) {
      LOG_WARN("[DBMS_SCHED_SERVICE] job master stop failed", K(tmp_ret));
    }
    job_master_.reset_scheduler_state();
  } else if (OB_FAIL(job_master_.stop())) {
    LOG_INFO("[DBMS_SCHED_SERVICE] ObDBMSSchedService stop failure", K(ret));
  } else {
    ObServerThreadHelper::stop();
    LOG_INFO("[DBMS_SCHED_SERVICE] ObDBMSSchedService stop success");
  }
}

void ObDBMSSchedService::wait()
{
  int ret = OB_SUCCESS;
  if (!job_master_.is_inited()) {
    ret = OB_NOT_INIT;
    LOG_WARN("not init", K(ret), K(job_master_.is_inited()));
  } else if (use_shared_executor_) {
    if (OB_FAIL(unregister_background_source_(true))) {
      LOG_WARN("[DBMS_SCHED_SERVICE] wait source failed", K(ret));
    }
  } else {
    ObServerThreadHelper::wait();
    LOG_INFO("[DBMS_SCHED_SERVICE] ObDBMSSchedService wait success");
  }
}

void ObDBMSSchedService::destroy()
{
  int ret = OB_SUCCESS;
  if (job_master_.is_inited()) {
    if (use_shared_executor_) {
      (void)unregister_background_source_(true);
      (void)job_master_.stop();
      job_master_.reset_scheduler_state();
    }
    if (OB_FAIL(job_master_.destroy())) {
      LOG_WARN("[DBMS_SCHED_SERVICE] job master destroy failed", K(ret));
    } else {
      LOG_INFO("[DBMS_SCHED_SERVICE] job master destroy success");
    }
    if (!use_shared_executor_) {
      ObServerThreadHelper::destroy();
    }
  }
  background_executor_ = NULL;
  source_handle_.reset();
  LOG_INFO("[DBMS_SCHED_SERVICE] ObDBMSSchedService destroy success");
}

void ObDBMSSchedService::deactivate()
{
  int ret = OB_SUCCESS;
  if (job_master_.is_inited()) {
    job_master_.switch_to_follower();
    if (use_shared_executor_) {
      const int tmp_ret = unregister_background_source_(true);
      if (OB_SUCCESS != tmp_ret) {
        LOG_WARN("[DBMS_SCHED_SERVICE] deactivate source failed", K(tmp_ret));
      }
      job_master_.reset_scheduler_state();
    } else {
      ObServerThreadHelper::switch_to_follower_gracefully();
    }
    LOG_INFO("[DBMS_SCHED_SERVICE] ObDBMSSchedService switch follower");
  }
}
int ObDBMSSchedService::activate()
{
  int ret = OB_SUCCESS;
  if (job_master_.is_inited()) {
    job_master_.switch_to_leader();
    if (use_shared_executor_) {
      if (OB_FAIL(register_background_source_())) {
        job_master_.switch_to_follower();
        LOG_WARN("[DBMS_SCHED_SERVICE] failed to register leader source", KR(ret));
      } else if (OB_FAIL(notify_background_source_())) {
        LOG_WARN("[DBMS_SCHED_SERVICE] failed to notify leader source", KR(ret));
      }
    } else if (OB_FAIL(ObServerThreadHelper::switch_to_leader())) {
      LOG_WARN("[DBMS_SCHED_SERVICE] failed to switch helper thread to leader", KR(ret));
    }
    LOG_INFO("[DBMS_SCHED_SERVICE] ObDBMSSchedService switch leader");
  }
  return ret;
}
void ObDBMSSchedService::wakeup_scheduler()
{
  int ret = OB_SUCCESS;
  SERVER_MODULE_SCOPE {
    rootserver::ObDBMSSchedService *svc = share::g_mp->dbms_sched_service();
    if (OB_NOT_NULL(svc)) {
      if (svc->use_shared_executor_) {
        const int tmp_ret = svc->notify_background_source_();
        if (OB_SUCCESS != tmp_ret && OB_NOT_RUNNING != tmp_ret) {
          LOG_WARN("[DBMS_SCHED_SERVICE] wake shared scheduler failed", K(tmp_ret));
        }
      } else {
        svc->job_master_.wakeup();
      }
    }
  }
}

int ObDBMSSchedService::register_background_source_()
{
  int ret = OB_SUCCESS;
  if (!use_shared_executor_ || source_handle_.is_valid()) {
  } else if (OB_ISNULL(share::g_mp)
      || OB_ISNULL(background_executor_ =
          share::g_mp->background_task_executor())) {
    ret = OB_ERR_UNEXPECTED;
    LOG_WARN("background task executor is null",
        K(ret), KP(share::g_mp), KP(background_executor_));
  } else {
    share::ObBackgroundTaskSourceConfig config;
    config.name_ = "DBMSSched";
    config.max_concurrency_ = 1;
    if (OB_FAIL(background_executor_->register_source(
        *this, config, source_handle_))) {
      LOG_WARN("failed to register dbms scheduler source", K(ret));
    }
  }
  return ret;
}

int ObDBMSSchedService::unregister_background_source_(
    const bool wait_running)
{
  int ret = OB_SUCCESS;
  if (use_shared_executor_ && OB_NOT_NULL(background_executor_)
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

int ObDBMSSchedService::notify_background_source_()
{
  int ret = OB_SUCCESS;
  if (!use_shared_executor_) {
  } else if (OB_ISNULL(background_executor_)
      || !source_handle_.is_valid()) {
    ret = OB_NOT_RUNNING;
  } else if (OB_FAIL(background_executor_->notify(
      source_handle_, share::BG_TASK_NORMAL))) {
    LOG_WARN("failed to notify dbms scheduler source", K(ret));
  }
  return ret;
}

}  // namespace rootserver
}  // namespace oceanbase
