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
 *
 * Change Stream Worker: ObCSWorker owns multiple ObCSExecutor (each is an
 * ObSimpleThreadPool LinkQueue). No pre-allocated queue array; when queue
 * is empty, adaptive threads shrink to save resources.
 */

#ifndef OB_CS_WORKER_H_
#define OB_CS_WORKER_H_

#include "lib/ob_define.h"
#include "lib/thread/ob_simple_thread_pool.h"
#include "lib/thread/thread_mgr_interface.h"
#include "share/change_stream/ob_change_stream_dispatcher.h"
#include "share/change_stream/ob_change_stream_plugin.h"

namespace oceanbase
{
namespace share
{

static const int64_t CS_EXECUTOR_QUEUE_LIMIT = 1000;

/// Single executor: ObSimpleThreadPool in LinkQueue mode (ObLinkQueueThreadPool).
/// ObCSExecSubTask inherits LinkTask directly, so no wrapper node is needed.
/// Adaptive threads; no pre-allocated queue array.
class ObCSExecutor : public common::ObLinkQueueThreadPool
{
public:
  ObCSExecutor();
  virtual ~ObCSExecutor();

  int init(int64_t executor_id, int64_t thread_num, int64_t task_queue_limit,
           const char *name, uint64_t tenant_id);
  int start();
  void stop();
  void wait();
  void destroy();

  /// Push a subtask directly (ObCSExecSubTask IS a LinkTask; owned by exec_ctx).
  int push_subtask(ObCSExecSubTask *sub_task);

  int64_t get_executor_id() const { return executor_id_; }
  char *get_name() { return name_; }

protected:
  void handle(common::LinkTask *task) override;
  void handle_drop(common::LinkTask *task) override;

private:
  int process_sub_task(ObCSExecSubTask *sub_task);
  /// Last-worker cleanup: serial commit spin → plugin commit → advance scn →
  /// txn commit/rollback → release_batch → dec active_batch_count → free ctx.
  void do_finish_batch_(ObCSExecCtx *ctx, ObCSDispatcher &dispatcher);

private:
  bool is_inited_;
  int64_t executor_id_;
  char name_[64];
};

// ---------------------------------------------------------------------------
// ObCSWorker: does not inherit ObThreadPool; owns multiple ObCSExecutor.
// Mgr keeps a single ObCSWorker; dispatcher pushes by slice_id.
// ---------------------------------------------------------------------------

class ObCSWorker
{
public:
  ObCSWorker();
  virtual ~ObCSWorker();

  static const int64_t CS_DEFAULT_EXECUTOR_COUNT = 4;

  int init(int64_t executor_count = CS_DEFAULT_EXECUTOR_COUNT);
  int start();
  void stop();
  void wait();
  void destroy();

  /// Push subtask to executor selected by slice_id (slice_id % executor_count).
  int push_subtask(int64_t slice_id, ObCSExecSubTask *sub_task);

  int64_t get_executor_count() const { return executor_count_; }

private:
  bool is_inited_;
  ObCSExecutor **executors_;  // allocated in init(), each element OB_NEW(ObCSExecutor)
  int64_t executor_count_;
};

}  // namespace share
}  // namespace oceanbase

#endif  // OB_CS_WORKER_H_
