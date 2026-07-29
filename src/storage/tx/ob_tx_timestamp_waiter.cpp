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
  const int64_t thread_count = MAX(common::get_cpu_count() / 12, 1L);

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
      latest_gts_(0),
      ts_mgr_(NULL),
      wait_queue_(),
      cond_(),
      callback_worker_()
{
}

int ObTxTimestampWaiter::init(ObTsMgr *ts_mgr)
{
  int ret = OB_SUCCESS;

  if (is_inited_) {
    ret = OB_INIT_TWICE;
    TRANS_LOG(WARN, "timestamp waiter init twice", KR(ret));
  } else if (OB_ISNULL(ts_mgr)) {
    ret = OB_INVALID_ARGUMENT;
    TRANS_LOG(WARN, "invalid timestamp manager", KR(ret));
  } else if (OB_FAIL(cond_.init(ObWaitEventIds::DEFAULT_COND_WAIT))) {
    TRANS_LOG(WARN, "timestamp waiter condition init failed", KR(ret));
  } else if (OB_FAIL(share::ObThreadPool::init())) {
    TRANS_LOG(WARN, "timestamp waiter thread init failed", KR(ret));
  } else if (FALSE_IT(share::ObThreadPool::set_run_wrapper(share::server_runtime()))) {
  } else if (OB_FAIL(callback_worker_.init(this))) {
    TRANS_LOG(WARN, "timestamp callback worker init failed", KR(ret));
  } else {
    ts_mgr_ = ts_mgr;
    is_inited_ = true;
    TRANS_LOG(INFO, "timestamp waiter init success");
  }

  if (OB_FAIL(ret)) {
    callback_worker_.destroy();
    share::ObThreadPool::destroy();
    cond_.destroy();
  }
  return ret;
}

int ObTxTimestampWaiter::start()
{
  int ret = OB_SUCCESS;

  if (!is_inited_) {
    ret = OB_NOT_INIT;
    TRANS_LOG(WARN, "timestamp waiter not init", KR(ret));
  } else {
    ATOMIC_STORE(&running_, true);
    if (OB_FAIL(share::ObThreadPool::start())) {
      ATOMIC_STORE(&running_, false);
      TRANS_LOG(WARN, "timestamp waiter start failed", KR(ret));
    }
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
    share::ObThreadPool::stop();
    callback_worker_.stop();
    share::ObThreadPool::wait();
    callback_worker_.wait();
    drain_wait_queue_();
    TRANS_LOG(INFO, "timestamp waiter stopped");
  }
}

void ObTxTimestampWaiter::destroy()
{
  if (is_inited_) {
    stop();
    drain_wait_queue_();
    callback_worker_.destroy();
    share::ObThreadPool::destroy();
    cond_.destroy();
    ts_mgr_ = NULL;
    latest_gts_ = 0;
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

  if (OB_FAIL(ts_mgr_->get_gts(current_gts))) {
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
      cond_.signal();
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
        dispatch_ready_tasks_(gts);
      }

      ObThreadCondGuard guard(cond_);
      if (is_running_() && wait_queue_.size() > 0) {
        cond_.wait_us(POLL_INTERVAL_US);
      }
    }
  }
}

void ObTxTimestampWaiter::dispatch_ready_tasks_(const SCN &gts)
{
  int ret = OB_SUCCESS;
  int64_t task_count = wait_queue_.size();

  while (task_count-- > 0 && is_running_()) {
    ObLink *link = NULL;
    if (OB_FAIL(wait_queue_.pop(link))) {
      break;
    }
    ObTxCtx *ctx = static_cast<ObTxCtx *>(link);
    if (ctx->get_commit_version() > gts) {
      wait_queue_.push(link);
      break;
    } else if (OB_FAIL(callback_worker_.push(ctx))) {
      if (is_running_()) {
        wait_queue_.push(link);
      } else {
        ctx->gts_callback_interrupted();
      }
      if (OB_EAGAIN != ret && OB_IN_STOP_STATE != ret) {
        TRANS_LOG(WARN, "push timestamp callback failed", KR(ret), KP(ctx));
      }
      break;
    }
  }
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
      cond_.signal();
      requeued = true;
    }
  }
  if (!requeued) {
    ctx->gts_callback_interrupted();
  }
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
