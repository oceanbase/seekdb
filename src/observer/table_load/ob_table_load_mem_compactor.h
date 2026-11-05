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

#include "storage/direct_load/ob_direct_load_i_table.h"
#include "storage/direct_load/ob_direct_load_mem_context.h"

namespace oceanbase
{
namespace observer
{
class ObTableLoadStoreCtx;
class ObTableLoadStoreTableCtx;
class ObTableLoadMergeMemSortOp;
class ObITableLoadTaskScheduler;

class ObTableLoadMemCompactor
{
  class LoadTaskProcessor;
  class DumpTaskProcessor;
  class SampleTaskProcessor;
  class CompactTaskCallback;
  class FinishTaskProcessor;
  class FinishTaskCallback;

public:
  ObTableLoadMemCompactor();
  virtual ~ObTableLoadMemCompactor();
  void reset();
  int init(ObTableLoadMergeMemSortOp *op);
  int start();
  void stop();

  void set_has_error() { mem_ctx_.has_error_ = true; }

private:
  int init_scheduler();
  int construct_compactors();
  int start_compact();
  int start_load();
  int start_dump();
  int start_sample();
  int start_finish();
  int handle_compact_thread_finish();
  int finish();

private:
  int add_tablet_table(const storage::ObDirectLoadTableHandle &table_handle);

private:
  ObTableLoadStoreCtx *store_ctx_;
  ObTableLoadStoreTableCtx *store_table_ctx_;
  ObTableLoadMergeMemSortOp *op_;
  common::ObArenaAllocator allocator_; // needs to be destructed last
  ObDirectLoadMemContext mem_ctx_;
  ObITableLoadTaskScheduler *task_scheduler_;
  int64_t finish_thread_cnt_ CACHE_ALIGNED;
  bool is_inited_;
};

} // namespace observer
} // namespace oceanbase
