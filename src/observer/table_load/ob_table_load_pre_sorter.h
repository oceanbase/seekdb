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
namespace storage
{
class ObDirectLoadTableStore;
} // namespace storage
namespace observer
{
class ObTableLoadTableCtx;
class ObTableLoadStoreCtx;
class ObTableLoadMemChunkManager;
class ObITableLoadTaskScheduler;

class ObTableLoadPreSorter
{
  class SampleTaskProcessor;
  class DumpTaskProcessor;
  class PreSortTaskCallback;
  class FinishTaskProcessor;
  class FinishTaskCallback;
  class ChunkSorter;

  friend class ObTableLoadPreSortWriter;
public:
  ObTableLoadPreSorter(ObTableLoadStoreCtx *store_ctx);
  ~ObTableLoadPreSorter();
  void reset();
  int init();
  int start();
  int close();
  void stop();
  void wait();
  bool is_stopped() const;
  void set_has_error() { mem_ctx_.has_error_ = true; }
public:
  OB_INLINE int64_t inc_sort_chunk_task_cnt() { return ATOMIC_AAF(&sort_chunk_task_cnt_, 1); }
  OB_INLINE int64_t dec_sort_chunk_task_cnt() { return ATOMIC_AAF(&sort_chunk_task_cnt_, -1); }
  OB_INLINE int64_t get_sort_chunk_task_cnt() { return ATOMIC_LOAD(&sort_chunk_task_cnt_); }
private:
  int init_mem_ctx();
  int init_chunks_manager();
  int init_sample_task_scheduler();
  int start_sample();
  int start_dump();
  int start_finish();
  int close_chunk(int64_t chunk_node_id);
  int handle_pre_sort_thread_finish();
  int finish();
private:
  ObTableLoadTableCtx *ctx_;
  ObTableLoadStoreCtx *store_ctx_;
  ObArenaAllocator allocator_;
  ObDirectLoadMemContext mem_ctx_;
  ObTableLoadMemChunkManager *chunks_manager_;
  ObITableLoadTaskScheduler *sample_task_scheduler_;
  int64_t unclosed_chunk_id_pos_;
  int64_t finish_thread_cnt_;
  int64_t sort_chunk_task_cnt_;
  bool all_trans_finished_;
  bool is_inited_;
};

} // namespace observer
} // namespace oceanbase
