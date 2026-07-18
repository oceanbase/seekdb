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

#include "storage/direct_load/ob_direct_load_sstable_builder.h"
#include "share/table/ob_table_load_define.h"
#include "storage/direct_load/ob_direct_load_compare.h"
#include <memory>
#include "storage/direct_load/ob_direct_load_mem_dump.h"
#include "storage/direct_load/ob_direct_load_mem_context.h"
#include "observer/table_load/ob_table_load_table_ctx.h"
#include "observer/table_load/ob_table_load_store_ctx.h"

namespace oceanbase
{
namespace storage
{

class ObDirectLoadMemSample
{
  static const constexpr int64_t DEFAULT_SAMPLE_TIMES = 50000;
  typedef ObDirectLoadConstExternalMultiPartitionRow RowType;
  typedef ObDirectLoadExternalMultiPartitionRowChunk ChunkType;
  typedef ObDirectLoadExternalMultiPartitionRowRange RangeType;
  typedef ObDirectLoadExternalMultiPartitionRowCompare CompareType;
public:
  ObDirectLoadMemSample(observer::ObTableLoadTableCtx *ctx, ObDirectLoadMemContext *mem_ctx);
  virtual ~ObDirectLoadMemSample() {}

  int do_sample();

private:
  int do_work();
  int add_dump(int64_t idx,
               common::ObArray<ChunkType *> &mem_chunk_array,
               const RangeType &range,
               table::ObTableLoadHandle<ObDirectLoadMemDump::Context> sample_ptr);
  int gen_ranges(common::ObIArray<ChunkType *> &chunks,
                 common::ObIArray<RangeType> &ranges);

private:
  // data members
  observer::ObTableLoadTableCtx *ctx_;
  ObDirectLoadMemContext *mem_ctx_;
  int64_t range_count_;
};



} // namespace storage
} // namespace oceanbase
