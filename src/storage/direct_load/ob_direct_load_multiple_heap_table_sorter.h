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

#include "storage/direct_load/ob_direct_load_external_fragment.h"
#include "storage/direct_load/ob_direct_load_i_table.h"
#include "storage/direct_load/ob_direct_load_mem_context.h"
#include "storage/direct_load/ob_direct_load_mem_worker.h"

namespace oceanbase
{
namespace observer
{
class ObTableLoadTableCtx;
} // namespace observer
namespace storage
{
class ObDirectLoadMultipleHeapTableMap;

class ObDirectLoadMultipleHeapTableSorter : public ObDirectLoadMemWorker
{
public:
  ObDirectLoadMultipleHeapTableSorter(ObDirectLoadMemContext *mem_ctx);
  virtual ~ObDirectLoadMultipleHeapTableSorter();
  int add_table(const ObDirectLoadTableHandle &table) override;
  void set_work_param(observer::ObTableLoadTableCtx *ctx,
                      int64_t index_dir_id,
                      int64_t data_dir_id,
                      ObDirectLoadTableHandleArray *heap_table_array)
  {
    ctx_ = ctx;
    index_dir_id_ = index_dir_id;
    data_dir_id_ = data_dir_id;
    heap_table_array_ = heap_table_array;
  }
  int work() override;
  VIRTUAL_TO_STRING_KV(KP(mem_ctx_), K_(fragments));

private:
  int acquire_chunk(ObDirectLoadMultipleHeapTableMap *&chunk);
  int close_chunk(ObDirectLoadMultipleHeapTableMap *&chunk);
  int get_tables(ObIDirectLoadPartitionTableBuilder &table_builder);

private:
  // data members
  observer::ObTableLoadTableCtx *ctx_;
  ObDirectLoadMemContext *mem_ctx_;
  ObDirectLoadExternalFragmentArray fragments_;
  ObArenaAllocator allocator_;
  int64_t index_dir_id_;
  int64_t data_dir_id_;
  ObDirectLoadTableHandleArray *heap_table_array_;

  DISALLOW_COPY_AND_ASSIGN(ObDirectLoadMultipleHeapTableSorter);
};

} // namespace storage
} // namespace oceanbase
