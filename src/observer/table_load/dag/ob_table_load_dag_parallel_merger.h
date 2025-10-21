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

#include "storage/direct_load/ob_direct_load_merge_ctx.h"

namespace oceanbase
{
namespace observer
{
class ObTableLoadStoreCtx;
class ObTableLoadTableOpCtx;

class ObTableLoadDagParallelMerger
{
public:
  ObTableLoadDagParallelMerger();
  ~ObTableLoadDagParallelMerger();
  int init_merge_task(ObTableLoadStoreCtx *store_ctx, ObTableLoadTableOpCtx *op_ctx);
  int get_next_merge_task(ObDirectLoadIMergeTask *&merge_task);
  ObDirectLoadMergeCtx &get_merge_ctx() { return merge_ctx_; }
  int prepare_clear_table();
  int clear_table(const int64_t thread_cnt, const int64_t thread_idx);
  TO_STRING_KV(K(task_iter_), K(is_inited_));

private:
  int init_merge_ctx();

private:
  ObTableLoadStoreCtx *store_ctx_;
  ObTableLoadTableOpCtx *op_ctx_;
  storage::ObDirectLoadMergeCtx merge_ctx_;
  mutable lib::ObMutex mutex_;
  ObDirectLoadMergeTaskIterator task_iter_;
  ObDirectLoadTableHandleArray all_table_handles_;
  bool clear_table_prepared_;
  bool is_inited_;
};

} // namespace observer
} // namespace oceanbase
