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
#include "lib/oblog/ob_log_module.h"
#include "lib/allocator/ob_malloc.h"
#include "lib/atomic/ob_atomic.h"
#include "share/change_stream/ob_change_stream_worker.h"
#include "share/change_stream/ob_change_stream_fetcher.h"
#include "share/change_stream/ob_change_stream_plugin.h"
#include "share/change_stream/ob_change_stream_mgr.h"
#include "share/rc/ob_tenant_base.h"
#include "share/ob_global_stat_proxy.h"

namespace oceanbase
{
namespace share
{

// ---------------------------------------------------------------------------
// ObCSExecutor
// ---------------------------------------------------------------------------

ObCSExecutor::ObCSExecutor()
  : common::ObLinkQueueThreadPool(),
    is_inited_(false),
    executor_id_(-1)
{
  MEMSET(name_, 0 , sizeof(name_));
}

ObCSExecutor::~ObCSExecutor()
{
  destroy();
}

int ObCSExecutor::init(int64_t executor_id, int64_t thread_num, int64_t task_queue_limit,
                       const char *name, uint64_t tenant_id)
{
  int ret = common::OB_SUCCESS;
  if (is_inited_) {
    ret = common::OB_INIT_TWICE;
    LOG_WARN("ObCSExecutor already inited", K(ret), K(executor_id));
  } else if (executor_id < 0 || thread_num <= 0 || task_queue_limit <= 0 || OB_ISNULL(name)) {
    ret = common::OB_INVALID_ARGUMENT;
    LOG_WARN("ObCSExecutor invalid argument", K(ret), K(executor_id), K(thread_num), K(task_queue_limit), KP(name));
  } else if (FALSE_IT(ObThreadPool::set_run_wrapper(MTL_CTX()))) {
  } else if (OB_FAIL(set_adaptive_thread(1, thread_num))) {
    LOG_WARN("ObCSExecutor set_adaptive_thread failed", K(ret), K(executor_id));
    // Base pool was already initialized — must clean up to avoid resource leak.
    ObLinkQueueThreadPool::destroy();
  } else if (OB_FAIL(ObLinkQueueThreadPool::init(thread_num, task_queue_limit, name, tenant_id))) {
    LOG_WARN("ObCSExecutor base init failed", K(ret), K(executor_id));
  } else {
    executor_id_ = executor_id;
    is_inited_ = true;
    LOG_INFO("ObCSExecutor inited (LinkQueue, adaptive 1~N)", K(executor_id), K(thread_num), K(task_queue_limit), K(name));
  }
  return ret;
}

int ObCSExecutor::start()
{
  // Thread pool is already started in base init() (ObLinkQueueThreadPool::init calls ThreadPool::start).
  // No-op here for API compatibility with mgr init/start sequence.
  return common::OB_SUCCESS;
}

void ObCSExecutor::stop()
{
  if (is_inited_) {
    ObLinkQueueThreadPool::stop();
  }
}

void ObCSExecutor::wait()
{
  if (is_inited_) {
    ObLinkQueueThreadPool::wait();
  }
}

void ObCSExecutor::destroy()
{
  if (is_inited_) {
    stop();
    wait();
    ObLinkQueueThreadPool::destroy();
    is_inited_ = false;
    executor_id_ = -1;
  }
}

int ObCSExecutor::push_subtask(ObCSExecSubTask *sub_task)
{
  int ret = common::OB_SUCCESS;
  if (!is_inited_ || OB_ISNULL(sub_task)) {
    ret = common::OB_INVALID_ARGUMENT;
    LOG_WARN("ObCSExecutor push_subtask invalid", K(ret), K(executor_id_));
  } else if (OB_FAIL(push(sub_task))) {
    LOG_WARN("ObCSExecutor push failed", K(ret), K(executor_id_));
  }
  return ret;
}

void ObCSExecutor::handle(common::LinkTask *task)
{
  if (OB_NOT_NULL(task)) {
    ObCSExecSubTask *sub_task = static_cast<ObCSExecSubTask *>(task);
    (void)process_sub_task(sub_task);
    // SubTask is owned by exec_ctx->sub_tasks_; do NOT delete here.
  }
}

void ObCSExecutor::handle_drop(common::LinkTask *task)
{
  int ret = common::OB_SUCCESS;
  UNUSED(ret);
  if (OB_NOT_NULL(task)) {
    ObCSExecSubTask *sub_task = static_cast<ObCSExecSubTask *>(task);
    ObCSExecCtx *ctx = sub_task->get_exec_ctx();
    ATOMIC_AAF(&ctx->task_fail_, 1);
    const int64_t finished = ATOMIC_AAF(&ctx->task_finish_, 1);
    if (finished == ctx->task_count_) {
      ObCSDispatcher &dispatcher = MTL(ObChangeStreamMgr *)->get_dispatcher();
      do_finish_batch_(ctx, dispatcher);
    }
  }
}

int ObCSExecutor::process_sub_task(ObCSExecSubTask *sub_task)
{
  int ret = common::OB_SUCCESS;
  ObCSExecCtx *ctx = sub_task->get_exec_ctx();
  ATOMIC_CAS(&ctx->process_time_, 0, ObTimeUtil::current_time());
  ObCSDispatcher &dispatcher = MTL(ObChangeStreamMgr *)->get_dispatcher();
  const int64_t row_count = sub_task->get_rows().count();

  // ── Phase 1: process ──
  // Epoch check: if a previous batch already triggered global abort, skip.
  if (ctx->epoch_ != dispatcher.get_epoch()) {
    ATOMIC_AAF(&ctx->task_fail_, 1);
    LOG_INFO("CSWorker: epoch mismatch, marking fail",
             K(executor_id_), K(ctx->batch_sn_), K(ctx->epoch_), K(dispatcher.get_epoch()));
  } else if (ATOMIC_LOAD(&ctx->task_fail_) == 0) {
    // Normal processing: invoke each plugin on this subtask's rows.
    common::ObSEArray<ObCSRow, 4> &rows = sub_task->get_rows();
    for (int64_t i = 0; OB_SUCC(ret) && i < ctx->plugin_cnt_; ++i) {
      ObCSPlugin *plugin = ctx->plugins_[i];
      if (OB_NOT_NULL(plugin) && OB_FAIL(plugin->process(rows, *ctx))) {
        LOG_WARN("plugin process failed", KR(ret), K(executor_id_), K(i));
      }
    }
    if (OB_FAIL(ret)) {
      ATOMIC_AAF(&ctx->task_fail_, 1);
    }
  }
  // ── Last-worker gate ──
  const int64_t finished = ATOMIC_AAF(&ctx->task_finish_, 1);
  if (finished == ctx->task_count_) {
    do_finish_batch_(ctx, dispatcher);
  }

  return ret;
}

// ---------------------------------------------------------------------------
// do_finish_batch_: called by the last Worker to finish a batch.
//
// Flow:
//   1. If epoch mismatch (global abort already signaled) → rollback → cleanup.
//   2. Serial commit spin (with epoch check each iteration).
//   3. If task_fail_ > 0 → rollback + inc_epoch → cleanup.
//   4. Plugin commit → advance refresh_scn → trans.end(true).
//      On failure: rollback + inc_epoch → cleanup.
//      On success: release_batch (pop ring + advance next_commit_sn) → cleanup.
//   5. Cleanup: dec_active_batch_count, OB_DELETE(ctx) (dtor calls destroy_plugins).
// ---------------------------------------------------------------------------
void ObCSExecutor::do_finish_batch_(ObCSExecCtx *ctx, ObCSDispatcher &dispatcher)
{
  int ret = common::OB_SUCCESS;
  const bool aborted = (ctx->epoch_ != dispatcher.get_epoch());
  const bool any_fail = (ATOMIC_LOAD(&ctx->task_fail_) > 0);
  int64_t start_time = ObTimeUtil::current_time();

  if (aborted) {
    // ── Abort path: global abort already triggered by an earlier batch. ──
    LOG_INFO("CSWorker: batch aborted (epoch mismatch)", K(ctx->batch_sn_), K(executor_id_));
    (void)ctx->trans_.end(false);
  } else {
    // ── Serial commit spin with epoch check ──
    bool spin_aborted = false;
    while (dispatcher.get_next_commit_sn() != ctx->batch_sn_) {
      if (OB_UNLIKELY(dispatcher.get_next_commit_sn() > ctx->batch_sn_)) {
        // Programming error: next_commit_sn_ somehow skipped past this batch.
        // Spinning forever is unrecoverable — abort the process.
        LOG_ERROR("FATAL: next_commit_sn skipped past batch_sn, aborting to avoid infinite spin",
                  K(ctx->batch_sn_), K(dispatcher.get_next_commit_sn()), K(executor_id_));
        ob_abort();
      }
      if (ctx->epoch_ != dispatcher.get_epoch()) {
        spin_aborted = true;
        break;
      }
      usleep(1000);
      if (REACH_TIME_INTERVAL(1 * 1000 * 1000)) {
        LOG_INFO("wait serial commit", K(ctx->batch_sn_),
                 K(dispatcher.get_next_commit_sn()), K(executor_id_));
      }
    }
    if (!spin_aborted && ctx->epoch_ != dispatcher.get_epoch()) {
      spin_aborted = true;
    }

    if (spin_aborted) {
      // Abort detected during spin — just rollback.
      (void)ctx->trans_.end(false);
    } else if (any_fail) {
      // ── This batch failed at processing — trigger global abort. ──
      (void)ctx->trans_.end(false);
      dispatcher.inc_epoch();
      LOG_WARN("batch processing failed, triggered global abort",
               K(ctx->batch_sn_), K(executor_id_), K(ctx->schema_version_));
    } else {
      // ── Success path: plugin commit + advance scn + trans commit. ──
      for (int64_t i = 0; OB_SUCC(ret) && i < ctx->plugin_cnt_; ++i) {
        ObCSPlugin *plugin = ctx->plugins_[i];
        if (OB_NOT_NULL(plugin) && OB_FAIL(plugin->commit())) {
          LOG_WARN("plugin commit failed", KR(ret), K(i));
        }
      }

      if (OB_SUCC(ret)) {
        SCN curr_refresh_scn;
        SCN ctx_refresh_scn;
        int64_t affected_rows = 0;
        if (OB_FAIL(ObGlobalStatProxy::get_change_stream_refresh_scn(
                ctx->trans_, MTL_ID(), true, curr_refresh_scn))) {
          LOG_WARN("get_change_stream_refresh_scn fail", KR(ret));
        } else if (curr_refresh_scn.get_val_for_gts() > static_cast<uint64_t>(ctx->refresh_scn_)) {
          ret = OB_ERR_UNEXPECTED;
          LOG_WARN("refresh scn unexpected", KR(ret), K(ctx->refresh_scn_), K(curr_refresh_scn));
        } else if (OB_FAIL(ctx_refresh_scn.convert_for_tx(ctx->refresh_scn_))) {
          LOG_WARN("convert scn failed", KR(ret));
        } else if (OB_FAIL(ObGlobalStatProxy::advance_change_stream_refresh_scn(
                ctx->trans_, MTL_ID(), ctx_refresh_scn, affected_rows))) {
          LOG_WARN("advance refresh_scn failed", KR(ret));
        }
      }

      if (OB_FAIL(ret)) {
        (void)ctx->trans_.end(false);
        dispatcher.inc_epoch();
        LOG_WARN("batch commit path failed, triggered global abort",
                 KR(ret), K(ctx->batch_sn_), K(executor_id_));
      } else if (OB_FAIL(ctx->trans_.end(true))) {
        LOG_WARN("trans commit failed, triggered global abort",
                 KR(ret), K(ctx->batch_sn_));
        dispatcher.inc_epoch();
      } else {
        // ── Commit succeeded — release ring buffer and advance cursor. ──
        int64_t end_time = ObTimeUtil::current_time();
        int64_t first_tx_commit = ctx->create_time_;
        int64_t last_tx_in_dispatch = ctx->create_time_;
        if (ctx->tx_list_.count() > 0) {
          first_tx_commit = ctx->tx_list_.at(0)->commit_version_/1000;
          last_tx_in_dispatch = ctx->tx_list_.at(ctx->tx_list_.count() -1)->in_dispatch_time_;
        }
        dispatcher.release_batch(ctx);
        FLOG_INFO("CSWorker: batch finish",
                 K(ctx->batch_sn_), K(ctx->row_count_), K(ctx->process_cnt_), K(ctx->tx_list_.count()),
                 K(ctx->refresh_scn_), K(executor_id_), K(ctx->create_time_),
                 "batch_cost", end_time - ctx->create_time_,
                 "fetcher_delay",last_tx_in_dispatch - first_tx_commit,
                 "dispatch_wait", ctx->process_time_ - ctx->create_time_,
                 "process", start_time - ctx->process_time_,
                 "batch_end", end_time - start_time,
                 "result_delay", end_time - first_tx_commit,
                 K(dispatcher.get_next_commit_sn()),
                 K(dispatcher.get_refresh_scn()));
      }
    }
  }

  // ── Cleanup (always) ──
  dispatcher.dec_active_batch_count();
  OB_DELETE(ObCSExecCtx, "CSExecCtx", ctx);
}

// ---------------------------------------------------------------------------
// ObCSWorker
// ---------------------------------------------------------------------------

ObCSWorker::ObCSWorker()
  : is_inited_(false),
    executors_(nullptr),
    executor_count_(0)
{
}

ObCSWorker::~ObCSWorker()
{
  destroy();
}

int ObCSWorker::init(int64_t executor_count)
{
  int ret = common::OB_SUCCESS;
  if (is_inited_) {
    ret = common::OB_INIT_TWICE;
    LOG_WARN("ObCSWorker already inited", K(ret));
  } else if (executor_count <= 0) {
    ret = common::OB_INVALID_ARGUMENT;
    LOG_WARN("ObCSWorker invalid executor_count", K(ret), K(executor_count));
  } else {
    executor_count_ = executor_count;
    const uint64_t tenant_id = MTL_ID();
    const common::ObMemAttr attr(tenant_id, "CSExecutors");
    void *buf = ob_malloc(executor_count_ * sizeof(ObCSExecutor *), attr);
    if (OB_ISNULL(buf)) {
      ret = common::OB_ALLOCATE_MEMORY_FAILED;
      LOG_WARN("ObCSWorker alloc executors array failed", K(ret), K(executor_count_));
    } else {
      executors_ = static_cast<ObCSExecutor **>(buf);
      for (int64_t i = 0; i < executor_count_; ++i) {
        executors_[i] = nullptr;
      }
      for (int64_t i = 0; OB_SUCC(ret) && i < executor_count_; ++i) {
        if (OB_ISNULL(executors_[i] = OB_NEW(ObCSExecutor, attr))) {
          ret = common::OB_ALLOCATE_MEMORY_FAILED;
          LOG_WARN("ObCSWorker alloc executor failed", K(ret), K(i));
          break;
        }
        (void)snprintf(executors_[i]->get_name(), 64, "CSWorker%ld", i);
        if (OB_FAIL(executors_[i]->init(i, 1, CS_EXECUTOR_QUEUE_LIMIT, executors_[i]->get_name(), tenant_id))) {
          LOG_WARN("ObCSWorker: executor init failed", K(ret), K(i));
          break;
        }
      }
      if (OB_SUCC(ret)) {
        is_inited_ = true;
        LOG_INFO("ObCSWorker init success", K(executor_count_));
      } else {
        for (int64_t j = 0; j < executor_count_; ++j) {
          if (OB_NOT_NULL(executors_[j])) {
            executors_[j]->destroy();
            OB_DELETE(ObCSExecutor, "CSExecutor", executors_[j]);
            executors_[j] = nullptr;
          }
        }
        ob_free(executors_);
        executors_ = nullptr;
        executor_count_ = 0;
      }
    }
  }
  return ret;
}

int ObCSWorker::start()
{
  int ret = common::OB_SUCCESS;
  if (!is_inited_) {
    ret = common::OB_NOT_INIT;
    LOG_WARN("ObCSWorker not inited", K(ret));
  } else {
    for (int64_t i = 0; OB_SUCC(ret) && i < executor_count_; ++i) {
      if (OB_NOT_NULL(executors_[i]) && OB_FAIL(executors_[i]->start())) {
        LOG_WARN("ObCSWorker: executor start failed", K(ret), K(i));
        break;
      }
    }
  }
  return ret;
}

void ObCSWorker::stop()
{
  if (is_inited_ && OB_NOT_NULL(executors_)) {
    for (int64_t i = 0; i < executor_count_; ++i) {
      if (OB_NOT_NULL(executors_[i])) {
        executors_[i]->stop();
      }
    }
  }
}

void ObCSWorker::wait()
{
  if (is_inited_ && OB_NOT_NULL(executors_)) {
    for (int64_t i = 0; i < executor_count_; ++i) {
      if (OB_NOT_NULL(executors_[i])) {
        executors_[i]->wait();
      }
    }
  }
}

void ObCSWorker::destroy()
{
  if (is_inited_) {
    stop();
    wait();
    if (OB_NOT_NULL(executors_)) {
      for (int64_t i = 0; i < executor_count_; ++i) {
        if (OB_NOT_NULL(executors_[i])) {
          executors_[i]->destroy();
          OB_DELETE(ObCSExecutor, "CSExecutor", executors_[i]);
          executors_[i] = nullptr;
        }
      }
      ob_free(executors_);
      executors_ = nullptr;
    }
    executor_count_ = 0;
    is_inited_ = false;
  }
}

int ObCSWorker::push_subtask(int64_t slice_id, ObCSExecSubTask *sub_task)
{
  int ret = common::OB_SUCCESS;
  if (!is_inited_ || executor_count_ <= 0 || OB_ISNULL(executors_) || OB_ISNULL(sub_task)
      || slice_id < 0 || slice_id >= executor_count_) {
    ret = common::OB_INVALID_ARGUMENT;
    LOG_WARN("ObCSWorker push_subtask invalid", K(ret), K(executor_count_), K(slice_id), KP(executors_),
        KP(sub_task), K(is_inited_));
  } else if (OB_ISNULL(executors_[slice_id])) {
    ret = common::OB_ERR_UNEXPECTED;
    LOG_WARN("ObCSWorker executor is null", K(ret), K(slice_id));
  } else if (OB_FAIL(executors_[slice_id]->push_subtask(sub_task))) {
    LOG_WARN("push subtask failed", K(ret));
  }
  return ret;
}

}  // namespace share
}  // namespace oceanbase
