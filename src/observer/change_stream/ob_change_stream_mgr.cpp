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

#define USING_LOG_PREFIX SHARE
#include "lib/ob_running_mode.h"
#include "lib/oblog/ob_log_module.h"
#include "share/rc/ob_module_provider.h"
#include "observer/change_stream/ob_change_stream_mgr.h"
#include "observer/ob_server_struct.h"
#include "share/rc/ob_server_runtime.h"
#include "storage/tx/ob_ts_mgr.h"
#include <unistd.h>

#ifndef GET_THREAD_NUM_BY_NPROCESSORS
#define GET_THREAD_NUM_BY_NPROCESSORS(factor) \
  (sysconf(_SC_NPROCESSORS_ONLN) / (factor) > 0 ? sysconf(_SC_NPROCESSORS_ONLN) / (factor) : 1)
#endif

namespace oceanbase
{
namespace share
{

namespace
{

const int64_t CS_IDLE_MAINTENANCE_RETRY_US = 1000 * 1000L;

} // end anonymous namespace

ObChangeStreamMgr::ObChangeStreamMgr()
  : is_inited_(false),
    use_lazy_start_(false),
    is_running_(false),
    components_started_(false),
    fetcher_started_(false),
    dispatcher_started_(false),
    worker_started_(false),
    lifecycle_lock_(),
    background_executor_(NULL),
    source_handle_(),
    fetcher_(),
    dispatcher_(),
    worker_()
{
}

ObChangeStreamMgr::~ObChangeStreamMgr()
{
  destroy();
}

int ObChangeStreamMgr::server_module_init(ObChangeStreamMgr *&mgr)
{
  int ret = common::OB_SUCCESS;
  if (OB_ISNULL(mgr)) {
    ret = common::OB_INVALID_ARGUMENT;
    LOG_WARN("ObChangeStreamMgr: mgr is null", K(ret));
  } else if (OB_FAIL(mgr->init())) {
    LOG_WARN("ObChangeStreamMgr init failed", KR(ret));
  } else {
    LOG_INFO("ObChangeStreamMgr server_module_init success",  KP(share::server_runtime()));
  }
  return ret;
}

int ObChangeStreamMgr::init()
{
  int ret = common::OB_SUCCESS;
  if (is_inited_) {
    ret = OB_INIT_TWICE;
  } else if (OB_FAIL(fetcher_.init(&dispatcher_))) {
    LOG_WARN("ObChangeStreamMgr: fetcher init failed", K(ret));
  } else if (OB_FAIL(dispatcher_.init())) {
    LOG_WARN("ObChangeStreamMgr: dispatcher init failed", K(ret));
  } else if (OB_FAIL(worker_.init(GET_THREAD_NUM_BY_NPROCESSORS(1)))) {
    LOG_WARN("ObChangeStreamMgr: worker init failed", K(ret));
  } else {
    use_lazy_start_ = lib::is_mini_mode();
    is_inited_ = true;
    FLOG_INFO("ObChangeStreamMgr init success (Fetcher/Dispatcher/Worker)",
        K(GET_THREAD_NUM_BY_NPROCESSORS(1)), K(use_lazy_start_));
  }
  return ret;
}

int ObChangeStreamMgr::start()
{
  int ret = common::OB_SUCCESS;
  if (!is_inited_) {
    ret = common::OB_NOT_INIT;
    LOG_WARN("ObChangeStreamMgr is not inited", K(ret));
  } else if (ATOMIC_LOAD(&is_running_)) {
    ret = OB_INIT_TWICE;
    LOG_WARN("ObChangeStreamMgr is already running", K(ret));
  } else if (use_lazy_start_) {
    if (OB_FAIL(register_background_source_())) {
      LOG_WARN("ObChangeStreamMgr: register idle maintenance source failed", K(ret));
    } else {
      ATOMIC_STORE(&is_running_, true);
      if (OB_FAIL(notify_background_source_())) {
        ATOMIC_STORE(&is_running_, false);
        (void)unregister_background_source_(true);
        LOG_WARN("ObChangeStreamMgr: notify idle maintenance source failed", K(ret));
      } else {
        LOG_INFO("ObChangeStreamMgr start success (Change Stream threads start on demand)");
      }
    }
  } else {
    lib::ObMutexGuard guard(lifecycle_lock_);
    if (OB_FAIL(start_components_())) {
      LOG_WARN("ObChangeStreamMgr: start components failed", K(ret));
    } else {
      ATOMIC_STORE(&is_running_, true);
    }
  }
  return ret;
}

void ObChangeStreamMgr::stop()
{
  if (is_inited_) {
    ATOMIC_STORE(&is_running_, false);
    if (use_lazy_start_) {
      const int tmp_ret = unregister_background_source_(false);
      if (OB_SUCCESS != tmp_ret && OB_EAGAIN != tmp_ret) {
        LOG_WARN_RET(tmp_ret,
            "ObChangeStreamMgr: unregister idle maintenance source failed");
      }
    }
    lib::ObMutexGuard guard(lifecycle_lock_);
    stop_components_();
  }
}

void ObChangeStreamMgr::wait()
{
  if (is_inited_) {
    if (use_lazy_start_) {
      const int tmp_ret = unregister_background_source_(true);
      if (OB_SUCCESS != tmp_ret) {
        LOG_WARN_RET(tmp_ret,
            "ObChangeStreamMgr: wait idle maintenance source failed");
      }
    }
    lib::ObMutexGuard guard(lifecycle_lock_);
    wait_components_();
  }
}

void ObChangeStreamMgr::destroy()
{
  if (is_inited_) {
    stop();
    wait();

    fetcher_.destroy();
    dispatcher_.destroy();
    worker_.destroy();
  }
  use_lazy_start_ = false;
  ATOMIC_STORE(&is_running_, false);
  ATOMIC_STORE(&components_started_, false);
  fetcher_started_ = false;
  dispatcher_started_ = false;
  worker_started_ = false;
  background_executor_ = NULL;
  source_handle_.reset();
  is_inited_ = false;
}

int ObChangeStreamMgr::process_one_quantum(
    const ObBackgroundTaskPriority priority,
    ObBackgroundTaskRunResult &result)
{
  int ret = OB_SUCCESS;
  result = ObBackgroundTaskRunResult();
  if (BG_TASK_NORMAL != priority) {
    ret = OB_INVALID_ARGUMENT;
  } else if (!ATOMIC_LOAD(&is_running_)) {
  } else {
    lib::ObMutexGuard guard(lifecycle_lock_);
    if (!ATOMIC_LOAD(&is_running_) || ATOMIC_LOAD(&components_started_)) {
    } else if (OB_ISNULL(GCTX.sql_proxy_)
        || OB_ISNULL(GCTX.schema_service_)
        || GCTX.in_bootstrap_
        || GCTX.start_service_time_ <= 0) {
      result.next_ready_ts_ =
          ObTimeUtility::current_time() + CS_IDLE_MAINTENANCE_RETRY_US;
    } else {
      bool has_async_index = false;
      ++result.processed_count_;
      if (OB_FAIL(fetcher_.get_has_async_index_tables(has_async_index))) {
        LOG_WARN("ObChangeStreamMgr: check async vector indexes failed, retry",
            K(ret));
        result.next_ready_ts_ =
            ObTimeUtility::current_time() + CS_IDLE_MAINTENANCE_RETRY_US;
        ret = OB_SUCCESS;
      } else if (has_async_index) {
        if (OB_FAIL(start_components_())) {
          LOG_WARN("ObChangeStreamMgr: lazy start components failed, retry",
              K(ret));
          result.next_ready_ts_ =
              ObTimeUtility::current_time() + CS_IDLE_MAINTENANCE_RETRY_US;
          ret = OB_SUCCESS;
        }
      } else {
        fetcher_.run_idle_maintenance();
        result.next_ready_ts_ =
            ObTimeUtility::current_time()
            + CS_FETCHER_REFRESH_SCN_ADVANCE_INTERVAL_US;
      }
    }
  }
  return ret;
}

void ObChangeStreamMgr::notify_schema_changed()
{
  lib::ObMutexGuard guard(lifecycle_lock_);
  if (ATOMIC_LOAD(&is_running_)) {
    if (ATOMIC_LOAD(&components_started_)) {
      fetcher_.notify_schema_changed();
    } else if (use_lazy_start_) {
      const int ret = notify_background_source_();
      if (OB_SUCCESS != ret && OB_NOT_RUNNING != ret
          && OB_IN_STOP_STATE != ret && OB_ENTRY_NOT_EXIST != ret) {
        LOG_WARN("ObChangeStreamMgr: notify schema change failed", K(ret));
      }
    }
  }
}

int ObChangeStreamMgr::start_components_()
{
  int ret = OB_SUCCESS;
  if (ATOMIC_LOAD(&components_started_)) {
  } else if (!worker_started_ && OB_FAIL(worker_.start())) {
    LOG_WARN("ObChangeStreamMgr: worker start failed", K(ret));
  } else if (FALSE_IT(worker_started_ = true)) {
  } else if (!dispatcher_started_ && OB_FAIL(dispatcher_.start())) {
    LOG_WARN("ObChangeStreamMgr: dispatcher start failed", K(ret));
  } else if (FALSE_IT(dispatcher_started_ = true)) {
  } else if (!fetcher_started_ && OB_FAIL(fetcher_.start())) {
    LOG_WARN("ObChangeStreamMgr: fetcher start failed", K(ret));
  } else {
    fetcher_started_ = true;
    ATOMIC_STORE(&components_started_, true);
    LOG_INFO("ObChangeStreamMgr start success (Fetcher/Dispatcher/Worker threads started)",
        K(use_lazy_start_));
  }
  return ret;
}

void ObChangeStreamMgr::stop_components_()
{
  if (fetcher_started_) {
    fetcher_.stop();
  }
  if (dispatcher_started_) {
    dispatcher_.stop();
  }
  if (worker_started_) {
    worker_.stop();
  }
}

void ObChangeStreamMgr::wait_components_()
{
  if (fetcher_started_) {
    fetcher_.wait();
  }
  if (dispatcher_started_) {
    dispatcher_.wait();
  }
  if (worker_started_) {
    worker_.wait();
  }
}

int ObChangeStreamMgr::register_background_source_()
{
  int ret = OB_SUCCESS;
  if (!use_lazy_start_ || source_handle_.is_valid()) {
  } else if (OB_ISNULL(share::g_mp)
      || OB_ISNULL(background_executor_ =
          share::g_mp->background_task_executor())) {
    ret = OB_ERR_UNEXPECTED;
    LOG_WARN("ObChangeStreamMgr: background task executor is null",
        K(ret), KP(share::g_mp), KP(background_executor_));
  } else {
    ObBackgroundTaskSourceConfig config;
    config.name_ = "CSIdleMaint";
    config.max_concurrency_ = 1;
    if (OB_FAIL(background_executor_->register_source(
        *this, config, source_handle_))) {
      LOG_WARN("ObChangeStreamMgr: register background source failed", K(ret));
    }
  }
  return ret;
}

int ObChangeStreamMgr::unregister_background_source_(
    const bool wait_running)
{
  int ret = OB_SUCCESS;
  if (use_lazy_start_ && OB_NOT_NULL(background_executor_)
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

int ObChangeStreamMgr::notify_background_source_()
{
  int ret = OB_SUCCESS;
  if (!use_lazy_start_) {
  } else if (OB_ISNULL(background_executor_)
      || !source_handle_.is_valid()) {
    ret = OB_NOT_RUNNING;
  } else if (OB_FAIL(background_executor_->notify(
      source_handle_, BG_TASK_NORMAL))) {
    LOG_WARN("ObChangeStreamMgr: notify background source failed", K(ret));
  }
  return ret;
}

int ObChangeStreamMgr::wait_refresh_scn(
    common::ObISQLClient &sql_client,
    const int64_t timeout_us)
{
  UNUSED(sql_client);
  int ret = common::OB_SUCCESS;
  SCN safe_visible_scn;
  const int64_t SLEEP_INTERVAL_US = 100 * 1000; // 100ms
  const int64_t abs_timeout_us = ObTimeUtility::current_time() + timeout_us;

  if (OB_FAIL(OB_TS_MGR.get_gts_sync(abs_timeout_us - ObTimeUtility::current_time(),
                                    safe_visible_scn))) {
    LOG_WARN("get gts for safe visible scn failed", KR(ret));
  } else {
    ObChangeStreamMgr *mgr = share::g_mp->change_stream_mgr();
    bool is_satisfied = false;
    while (OB_SUCC(ret) && !is_satisfied) {
      SCN current_refresh_scn;
      const int64_t now = ObTimeUtility::current_time();
      ObCSDispatcher *dispatcher = (OB_NOT_NULL(mgr) ? &mgr->dispatcher_ : nullptr);
      if (now >= abs_timeout_us) {
        ret = OB_TIMEOUT;
        LOG_WARN("wait change stream refresh scn timeout", KR(ret),
                 K(safe_visible_scn), K(current_refresh_scn));
      } else if (OB_ISNULL(mgr) || !mgr->is_inited()) {
        ret = OB_NOT_INIT;
        LOG_WARN("change stream mgr is not inited", KR(ret), KP(mgr));
      } else if (OB_FAIL(current_refresh_scn.convert_for_tx(
                     dispatcher->get_refresh_scn()))) {
        LOG_WARN("failed to convert mgr refresh_scn", KR(ret),
                 "mgr_refresh_scn", dispatcher->get_refresh_scn());
      } else if (current_refresh_scn >= safe_visible_scn) {
        is_satisfied = true;
        LOG_INFO("change stream refresh scn caught up",
                 K(safe_visible_scn), K(current_refresh_scn));
      } else {
        LOG_INFO("waiting for change stream refresh scn",
                 K(safe_visible_scn), K(current_refresh_scn));
        ob_usleep(SLEEP_INTERVAL_US);
      }
    }
  }
  return ret;
}

}  // namespace share
}  // namespace oceanbase
