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

#include "storage/direct_load/ob_direct_load_mem_context.h"

namespace oceanbase
{
namespace observer
{
class ObTableLoadStoreCtx;

struct ObTableLoadChunkNode
{
public:
  using ChunkType = storage::ObDirectLoadExternalMultiPartitionRowChunk;
  ObTableLoadChunkNode();
  ~ObTableLoadChunkNode();
  bool is_used() const { return ATOMIC_LOAD(&is_used_); }
  bool set_used() { return (false == ATOMIC_VCAS(&is_used_, false, true)); }
  bool set_unused() { return (true == ATOMIC_VCAS(&is_used_, true, false)); }
  TO_STRING_KV(KP_(chunk), K_(is_used));
public:
  ChunkType *chunk_;
  bool is_used_;
};

class ObTableLoadMemChunkManager
{
  using ChunkType = storage::ObDirectLoadExternalMultiPartitionRowChunk;
  using CompareType = storage::ObDirectLoadExternalMultiPartitionRowCompare;
public:
  ObTableLoadMemChunkManager();
  ~ObTableLoadMemChunkManager();
  int init(ObTableLoadStoreCtx *store_ctx, storage::ObDirectLoadMemContext *mem_ctx);
  int get_chunk(int64_t &chunk_node_id, ChunkType *&chunk);
  // for close all
  int get_unclosed_chunks(ObIArray<int64_t> &chunk_node_ids);
  int push_chunk(int64_t chunk_node_id);
  int close_chunk(int64_t chunk_node_id);
  int close_and_acquire_chunk(int64_t chunk_node_id, ChunkType *&chunk);
private:
  ObTableLoadStoreCtx *store_ctx_;
  storage::ObDirectLoadMemContext *mem_ctx_;
  ObArray<ObTableLoadChunkNode> chunk_nodes_;
  bool is_inited_;

  DISALLOW_COPY_AND_ASSIGN(ObTableLoadMemChunkManager);
};

} // namespace observer
} // namespace oceanbase
