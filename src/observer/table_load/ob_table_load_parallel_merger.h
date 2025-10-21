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

#pragma once

#include "storage/direct_load/ob_direct_load_i_merge_task.h"
#include "storage/direct_load/ob_direct_load_merge_ctx.h"

namespace oceanbase
{
namespace observer
{
class ObTableLoadStoreCtx;
class ObTableLoadMergeTableBaseOp;

class ObTableLoadParallelMerger
{
  class MergeTaskProcessor;
  class MergeTaskCallback;

public:
  ObTableLoadParallelMerger();
  ~ObTableLoadParallelMerger();
  int init_merge_task(ObTableLoadMergeTableBaseOp *op);
  int init_rescan_task(ObTableLoadMergeTableBaseOp *op);
  int start();
  void stop();
  ObDirectLoadMergeCtx &get_merge_ctx() { return merge_ctx_; }

private:
  int init_merge_ctx(ObTableLoadMergeTableBaseOp *op);

  int get_next_merge_task(ObDirectLoadIMergeTask *&merge_task);
  int handle_merge_task_finish(ObDirectLoadIMergeTask *merge_task, int ret_code);
  int handle_merge_thread_finish(int ret_code);

private:
  ObTableLoadStoreCtx *store_ctx_;
  ObTableLoadMergeTableBaseOp *op_;
  ObDirectLoadMergeCtx merge_ctx_;
  mutable lib::ObMutex mutex_;
  ObDirectLoadMergeTaskIterator task_iter_;
  common::ObDList<ObDirectLoadIMergeTask> running_task_list_;
  int64_t running_thread_cnt_ CACHE_ALIGNED;
  bool has_error_;
  bool is_stop_;
  bool is_inited_;
};

} // namespace observer
} // namespace oceanbase
