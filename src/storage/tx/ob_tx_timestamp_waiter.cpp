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

#include "ob_tx_timestamp_waiter.h"
#include "lib/ob_running_mode.h"
#include "share/rc/ob_server_runtime.h"
#include "ob_ts_mgr.h"
#include "ob_tx_ctx.h"

namespace oceanbase
{
using namespace common;
using namespace share;

namespace transaction
{

int ObTxTimestampCallbackWorker::init(ObTxTimestampWaiter *waiter)
{
  int ret = OB_SUCCESS;
  const int64_t thread_count = lib::is_mini_mode() ? 1 : MAX(common::get_cpu_count() / 12, 1L);

  waiter_ = waiter;
  set_run_wrapper(share::server_runtime());
  if (OB_FAIL(common::ObSimpleThreadPool::init(thread_count, MAX_TASK_NUM, "TxTsCb"))) {
    TRANS_LOG(WARN, "timestamp callback worker init failed", KR(ret), K(thread_count));
    waiter_ = NULL;
  }
  return ret;
}

void ObTxTimestampCallbackWorker::handle(void *task)
{
  waiter_->handle_callback_(static_cast<ObTxCtx *>(task));
}

ObTxTimestampWaiter::ObTxTimestampWaiter()
    : share::ObThreadPool(1),
      is_inited_(false),
      running_(false),
      use_shared_executor_(false),
      latest_gts_(0),
      ts_mgr_(NULL),
      wait_queue_(),
      cond_(),
      callback_worker_(),
      background_executor_(NULL),
      source_handle_()
{
}

int ObTxTimestampWaiter::init(
    ObTsMgr *ts_mgr,
    share::ObBackgroundTaskExecutor *background_executor)
{
  int ret = OB_SUCCESS;
  bool thread_pool_inited = false;

  if (is_inited_) {
    ret = OB_INIT_TWICE;
    TRANS_LOG(WARN, "timestamp waiter init twice", KR(ret));
  } else if (OB_ISNULL(ts_mgr)) {
    ret = OB_INVALID_ARGUMENT;
    TRANS_LOG(WARN, "invalid timestamp manager", KR(ret));
  } else if (OB_FAIL(cond_.init(ObWaitEventIds::DEFAULT_COND_WAIT))) {
    TRANS_LOG(WARN, "timestamp waiter condition init failed", KR(ret));
  } else if (FALSE_IT(use_shared_executor_ = lib::is_mini_mode())) {
  } else if (use_shared_executor_ && OB_ISNULL(background_executor)) {
    ret = OB_INVALID_ARGUMENT;
    TRANS_LOG(WARN, "invalid background task executor", KR(ret));
  } else if (!use_shared_executor_
      && OB_FAIL(share::ObThreadPool::init())) {
    TRANS_LOG(WARN, "timestamp waiter thread init failed", KR(ret));
  } else if (FALSE_IT(thread_pool_inited = !use_shared_executor_)) {
  } else if (!use_shared_executor_
      && FALSE_IT(share::ObThreadPool::set_run_wrapper(
          share::server_runtime()))) {
  } else if (OB_FAIL(callback_worker_.init(this))) {
    TRANS_LOG(WARN, "timestamp callback worker init failed", KR(ret));
  } else {
    ts_mgr_ = ts_mgr;
    background_executor_ =
        use_shared_executor_ ? background_executor : NULL;
    is_inited_ = true;
    TRANS_LOG(INFO, "timestamp waiter init success");
  }

  if (OB_FAIL(ret)) {
    callback_worker_.destroy();
    if (thread_pool_inited) {
      share::ObThreadPool::destroy();
    }
    cond_.destroy();
    background_executor_ = NULL;
    use_shared_executor_ = false;
  }
  return ret;
}

int ObTxTimestampWaiter::start()
{
  int ret = OB_SUCCESS;

  if (!is_inited_) {
    ret = OB_NOT_INIT;
    TRANS_LOG(WARN, "timestamp waiter not init", KR(ret));
  } else if (is_running_()) {
    ret = OB_ERR_UNEXPECTED;
    TRANS_LOG(WARN, "timestamp waiter already running", KR(ret));
  } else if (use_shared_executor_) {
    share::ObBackgroundTaskSourceConfig config;
    config.name_ = "TxTsWaiter";
    config.max_concurrency_ = 1;
    if (OB_FAIL(background_executor_->register_source(
        *this, config, source_handle_))) {
      TRANS_LOG(WARN,
          "register timestamp waiter background source failed", KR(ret));
    } else {
      ATOMIC_STORE(&running_, true);
      if (wait_queue_.size() > 0
          && OB_FAIL(notify_background_source_())) {
        TRANS_LOG(WARN,
            "notify pending timestamp wait tasks failed", KR(ret));
      }
    }
  } else {
    ATOMIC_STORE(&running_, true);
    if (OB_FAIL(share::ObThreadPool::start())) {
      ATOMIC_STORE(&running_, false);
      TRANS_LOG(WARN, "timestamp waiter start failed", KR(ret));
    }
  }
  if (OB_FAIL(ret) && use_shared_executor_) {
    ATOMIC_STORE(&running_, false);
    (void)unregister_background_source_(true);
  }
  return ret;
}

void ObTxTimestampWaiter::stop()
{
  if (is_inited_ && is_running_()) {
    {
      ObThreadCondGuard guard(cond_);
      ATOMIC_STORE(&running_, false);
      cond_.broadcast();
    }
    if (use_shared_executor_) {
      const int tmp_ret = unregister_background_source_(true);
      if (OB_SUCCESS != tmp_ret) {
        TRANS_LOG_RET(WARN, tmp_ret,
            "unregister timestamp waiter background source failed",
            K(tmp_ret));
      }
    } else {
      share::ObThreadPool::stop();
      share::ObThreadPool::wait();
    }
    callback_worker_.stop();
    callback_worker_.wait();
    drain_wait_queue_();
    TRANS_LOG(INFO, "timestamp waiter stopped");
  }
}

void ObTxTimestampWaiter::destroy()
{
  if (is_inited_) {
    stop();
    if (use_shared_executor_) {
      const int tmp_ret = unregister_background_source_(true);
      if (OB_SUCCESS != tmp_ret) {
        TRANS_LOG_RET(WARN, tmp_ret,
            "destroy timestamp waiter background source failed",
            K(tmp_ret));
      }
    }
    drain_wait_queue_();
    callback_worker_.destroy();
    if (!use_shared_executor_) {
      share::ObThreadPool::destroy();
    }
    cond_.destroy();
    ts_mgr_ = NULL;
    background_executor_ = NULL;
    source_handle_.reset();
    latest_gts_ = 0;
    use_shared_executor_ = false;
    is_inited_ = false;
  }
}

int ObTxTimestampWaiter::wait_gts_elapse(const SCN &target_scn,
                                         ObTxCtx *ctx,
                                         bool &need_wait)
{
  int ret = OB_SUCCESS;
  SCN current_gts;
  bool gts_ready = true;
  need_wait = false;

  if (!is_inited_) {
    ret = OB_NOT_INIT;
  } else if (OB_ISNULL(ctx)) {
    ret = OB_INVALID_ARGUMENT;
  } else if (OB_FAIL(ts_mgr_->get_gts(current_gts))) {
    if (OB_EAGAIN == ret) {
      ret = OB_SUCCESS;
      gts_ready = false;
    } else {
      TRANS_LOG(WARN, "get gts for timestamp wait failed", KR(ret), K(target_scn));
    }
  }

  if (OB_SUCC(ret) && (!gts_ready || current_gts < target_scn)) {
    ObThreadCondGuard guard(cond_);
    if (!is_running_()) {
      ret = OB_NOT_RUNNING;
      TRANS_LOG(WARN, "timestamp waiter is not running", KR(ret));
    } else if (OB_FAIL(wait_queue_.push(static_cast<ObLink *>(ctx)))) {
      TRANS_LOG(ERROR, "push timestamp wait task failed", KR(ret), KP(ctx));
    } else {
      need_wait = true;
      if (use_shared_executor_) {
        const int tmp_ret = notify_background_source_();
        if (OB_SUCCESS != tmp_ret) {
          TRANS_LOG_RET(WARN, tmp_ret,
              "notify timestamp waiter background source failed",
              K(tmp_ret), KP(ctx));
        }
      } else {
        cond_.signal();
      }
    }
  }
  return ret;
}

void ObTxTimestampWaiter::run1()
{
  lib::set_thread_name("TxTsWaiter");

  while (is_running_()) {
    if (wait_queue_.size() == 0) {
      ObThreadCondGuard guard(cond_);
      if (is_running_() && wait_queue_.size() == 0) {
        cond_.wait();
      }
    } else {
      int ret = OB_SUCCESS;
      SCN gts;
      if (OB_FAIL(ts_mgr_->get_gts(gts))) {
        if (OB_EAGAIN != ret) {
          TRANS_LOG(WARN, "get gts in timestamp waiter failed", KR(ret));
        }
      } else {
        ATOMIC_STORE(&latest_gts_, gts.get_val_for_gts());
        int64_t dispatched_count = 0;
        bool has_more_ready = false;
        bool need_retry = false;
        dispatch_ready_tasks_(
            gts,
            wait_queue_.size(),
            dispatched_count,
            has_more_ready,
            need_retry);
      }

      ObThreadCondGuard guard(cond_);
      if (is_running_() && wait_queue_.size() > 0) {
        cond_.wait_us(POLL_INTERVAL_US);
      }
    }
  }
}

int ObTxTimestampWaiter::process_one_quantum(
    const share::ObBackgroundTaskPriority priority,
    share::ObBackgroundTaskRunResult &result)
{
  int ret = OB_SUCCESS;
  bool need_retry = false;
  if (!use_shared_executor_) {
    ret = OB_STATE_NOT_MATCH;
  } else if (share::BG_TASK_HIGH != priority) {
    ret = OB_INVALID_ARGUMENT;
  } else if (!is_running_()) {
  } else if (wait_queue_.size() > 0) {
    SCN gts;
    const int gts_ret = ts_mgr_->get_gts(gts);
    if (OB_SUCCESS == gts_ret) {
      ATOMIC_STORE(&latest_gts_, gts.get_val_for_gts());
      dispatch_ready_tasks_(
          gts,
          MAX_DISPATCH_TASK_COUNT,
          result.processed_count_,
          result.has_more_ready_,
          need_retry);
    } else {
      need_retry = true;
      if (OB_EAGAIN != gts_ret) {
        TRANS_LOG_RET(WARN, gts_ret,
            "get gts in shared timestamp waiter failed", K(gts_ret));
      }
    }
    if (!result.has_more_ready_
        && need_retry
        && wait_queue_.size() > 0) {
      result.next_ready_ts_ =
          ObTimeUtility::current_time() + POLL_INTERVAL_US;
    }
  }
  return ret;
}

void ObTxTimestampWaiter::dispatch_ready_tasks_(
    const SCN &gts,
    const int64_t max_dispatch_count,
    int64_t &dispatched_count,
    bool &has_more_ready,
    bool &need_retry)
{
  int ret = OB_SUCCESS;
  int64_t task_count =
      MIN(wait_queue_.size(), MAX(max_dispatch_count, 0L));
  dispatched_count = 0;
  has_more_ready = false;
  need_retry = false;

  while (task_count-- > 0 && is_running_()) {
    ObLink *link = NULL;
    if (OB_FAIL(wait_queue_.pop(link))) {
      break;
    }
    ObTxCtx *ctx = static_cast<ObTxCtx *>(link);
    if (ctx->get_commit_version() > gts) {
      wait_queue_.push(link);
      need_retry = true;
      break;
    } else if (OB_FAIL(callback_worker_.push(ctx))) {
      if (is_running_()) {
        wait_queue_.push(link);
        need_retry = true;
      } else {
        ctx->gts_callback_interrupted();
      }
      if (OB_EAGAIN != ret && OB_IN_STOP_STATE != ret) {
        TRANS_LOG(WARN, "push timestamp callback failed", KR(ret), KP(ctx));
      }
      break;
    } else {
      ++dispatched_count;
    }
  }
  has_more_ready = !need_retry && wait_queue_.size() > 0;
}

void ObTxTimestampWaiter::handle_callback_(ObTxCtx *ctx)
{
  if (!is_running_()) {
    ctx->gts_callback_interrupted();
  } else {
    int ret = OB_SUCCESS;
    SCN gts;
    (void)gts.convert_for_gts(ATOMIC_LOAD(&latest_gts_));
    if (OB_FAIL(ctx->gts_elapse_callback(gts))) {
      if (OB_EAGAIN == ret) {
        requeue_or_interrupt_(ctx);
      } else {
        TRANS_LOG(WARN, "timestamp elapse callback failed", KR(ret), KP(ctx), K(gts));
      }
    }
  }
}

void ObTxTimestampWaiter::requeue_or_interrupt_(ObTxCtx *ctx)
{
  bool requeued = false;
  {
    ObThreadCondGuard guard(cond_);
    if (is_running_()) {
      wait_queue_.push(static_cast<ObLink *>(ctx));
      if (use_shared_executor_) {
        const int tmp_ret = notify_background_source_();
        if (OB_SUCCESS != tmp_ret) {
          TRANS_LOG_RET(WARN, tmp_ret,
              "notify requeued timestamp task failed",
              K(tmp_ret), KP(ctx));
        }
      } else {
        cond_.signal();
      }
      requeued = true;
    }
  }
  if (!requeued) {
    ctx->gts_callback_interrupted();
  }
}

int ObTxTimestampWaiter::notify_background_source_()
{
  int ret = OB_SUCCESS;
  if (!use_shared_executor_) {
  } else if (OB_ISNULL(background_executor_)
      || !source_handle_.is_valid()
      || !is_running_()) {
    ret = OB_NOT_RUNNING;
  } else if (OB_FAIL(background_executor_->notify(
      source_handle_, share::BG_TASK_HIGH))) {
    TRANS_LOG(WARN,
        "notify timestamp waiter background source failed", KR(ret));
  }
  return ret;
}

int ObTxTimestampWaiter::unregister_background_source_(
    const bool wait_running)
{
  int ret = OB_SUCCESS;
  bool need_retry = false;
  do {
    need_retry = false;
    if (use_shared_executor_
        && OB_NOT_NULL(background_executor_)
        && source_handle_.is_valid()) {
      ret = background_executor_->unregister_source(source_handle_);
      if (OB_EAGAIN == ret && wait_running) {
        need_retry = true;
      } else if (OB_ENTRY_NOT_EXIST == ret || OB_NOT_INIT == ret) {
        source_handle_.reset();
        ret = OB_SUCCESS;
      }
    }
    if (need_retry) {
      ob_usleep(1000);
    }
  } while (need_retry);
  return ret;
}

void ObTxTimestampWaiter::drain_wait_queue_()
{
  ObLink *link = NULL;
  while (OB_SUCCESS == wait_queue_.pop(link)) {
    ObTxCtx *ctx = static_cast<ObTxCtx *>(link);
    ctx->gts_callback_interrupted();
  }
}

} // namespace transaction
} // namespace oceanbase
