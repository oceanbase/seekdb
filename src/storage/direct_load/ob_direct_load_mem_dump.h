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

#include "lib/lock/ob_mutex.h"
#include "share/table/ob_table_load_handle.h"
#include "storage/direct_load/ob_direct_load_mem_context.h"
#include "storage/direct_load/ob_direct_load_mem_define.h"
#include "storage/direct_load/ob_direct_load_multi_map.h"
#include "storage/direct_load/ob_direct_load_sstable_builder.h"
#include "observer/table_load/ob_table_load_table_ctx.h"
#include "storage/direct_load/ob_direct_load_mem_worker.h"

namespace oceanbase
{
namespace storage
{
class ObDirectLoadITable;

class ObDirectLoadMemDump : public ObDirectLoadMemWorker
{
  typedef ObDirectLoadConstExternalMultiPartitionRow RowType;
  typedef ObDirectLoadExternalMultiPartitionRowChunk ChunkType;
  typedef ObDirectLoadExternalMultiPartitionRowRange RangeType;
  typedef ObDirectLoadExternalMultiPartitionRowCompare CompareType;
public:
  class Context
  {
  public:
    Context(ObDirectLoadMemContext *mem_ctx);
    ~Context();
    int add_table(const common::ObTabletID &tablet_id, int64_t range_idx,
                  const ObDirectLoadTableHandle &table);

    int init()
    {
      return tables_.init();
    }

  private:
    ObArenaAllocator allocator_; // just for safe_allocator_
  public:
    ObSafeArenaAllocator safe_allocator_;
    // Note, if this tables_ is to be operated by multiple threads, it must be locked
    ObDirectLoadMultiMap<common::ObTabletID, std::pair<int64_t, ObDirectLoadTableHandle>>
      tables_;
    common::ObArray<ChunkType *> mem_chunk_array_;
    int64_t finished_sub_dump_count_;
    int64_t sub_dump_count_;

  private:
    lib::ObMutex mutex_;
    ObDirectLoadTableHandleArray all_tables_;
    ObDirectLoadMemContext *mem_ctx_;
  };

public:
  ObDirectLoadMemDump(observer::ObTableLoadTableCtx *ctx, 
                      ObDirectLoadMemContext *mem_ctx,
                      const RangeType &range,
                      table::ObTableLoadHandle<Context> context_ptr, int64_t range_idx);
  virtual ~ObDirectLoadMemDump();
  virtual int add_table(const ObDirectLoadTableHandle &table_handle) override { return OB_SUCCESS; }
  virtual int work();
  int do_dump();
  VIRTUAL_TO_STRING_KV(KP(mem_ctx_), K_(range));
private:
  // dump tables
  int new_table_builder(const ObTabletID &tablet_id,
                        ObIDirectLoadPartitionTableBuilder *&table_builder);
  int new_sstable_builder(const ObTabletID &tablet_id,
                          ObIDirectLoadPartitionTableBuilder *&table_builder);
  int new_external_table_builder(const ObTabletID &tablet_id,
                                 ObIDirectLoadPartitionTableBuilder *&table_builder);
  int close_table_builder(ObIDirectLoadPartitionTableBuilder *table_builder,
                          common::ObTabletID tablet_id, bool is_final);
  int dump_tables();
  // compact tables
  int new_table_compactor(const common::ObTabletID &tablet_id,
                          ObIDirectLoadTabletTableCompactor *&compactor);
  int new_sstable_compactor(const common::ObTabletID &tablet_id,
                            ObIDirectLoadTabletTableCompactor *&compactor);
  int new_external_table_compactor(const common::ObTabletID &tablet_id,
                                   ObIDirectLoadTabletTableCompactor *&compactor);
  int compact_tables();
  int compact_tablet_tables(const common::ObTabletID &tablet_id);

private:
  // data members
  ObArenaAllocator allocator_;
  observer::ObTableLoadTableCtx *ctx_;
  ObDirectLoadMemContext *mem_ctx_;
  RangeType range_;
  table::ObTableLoadHandle<Context> context_ptr_;
  int64_t range_idx_;
  char *extra_buf_;
  int64_t extra_buf_size_;
};

} // namespace storage
} // namespace oceanbase
