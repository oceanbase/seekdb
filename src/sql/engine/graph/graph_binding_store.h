/*
 * Copyright (c) 2026 OceanBase.
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

#include "lib/allocator/page_arena.h"
#include "lib/container/ob_array.h"
#include "sql/engine/basic/ob_chunk_datum_store.h"

namespace oceanbase
{
namespace sql
{

// Query-local hard cap shared by binding rows, path states and expansion
// buffers. PageArena and ObArray allocate through this object, so a request
// that would cross the no-spill budget is rejected before tenant allocation.
class GraphWorkAreaAllocator final : public common::ObIAllocator
{
public:
  GraphWorkAreaAllocator() = default;
  ~GraphWorkAreaAllocator() = default;

  int init(int64_t memory_limit);
  void *alloc(int64_t size) override;
  void *alloc(int64_t size, const lib::ObMemAttr &attr) override;
  void free(void *ptr) override;
  int64_t total() const override;
  int64_t used() const override { return total(); }
  bool limit_exceeded() const { return limit_exceeded_; }
  void clear_limit_exceeded() { limit_exceeded_ = false; }

private:
  lib::ObMemAttr attr_{"GraphFeedback", common::ObCtxIds::WORK_AREA};
  common::ObMalloc base_allocator_{attr_};
  common::MemoryUsageTracker tracker_{};
  common::TrackedAllocator tracked_allocator_{
      base_allocator_, &tracker_, attr_};
  int64_t memory_limit_{0};
  int64_t allocation_count_{0};
  bool limit_exceeded_{false};

  DISALLOW_COPY_AND_ASSIGN(GraphWorkAreaAllocator);
};

// Query-local deep copies of anchor/outer rows. A binding ID is the row's
// monotonic array position, so duplicate seed rows deliberately receive
// different IDs and retain SQL bag multiplicity.
class GraphBindingStore final
{
public:
  GraphBindingStore() = default;
  ~GraphBindingStore() = default;

  int init(int64_t memory_limit);
  int add_binding(const common::ObIArray<ObExpr *> &exprs,
                  ObEvalCtx &eval_ctx,
                  int64_t &binding_id);
  int get_binding(
      int64_t binding_id,
      const ObChunkDatumStore::StoredRow *&row) const;
  int restore_binding(int64_t binding_id,
                      const common::ObIArray<ObExpr *> &exprs,
                      ObEvalCtx &eval_ctx) const;

  // Retains reusable pages across rescans. IDs and returned row pointers are
  // invalid after reset(), while allocator capacity remains budgeted.
  void reset();
  int64_t count() const { return rows_.count(); }
  int64_t used_memory() const;
  int64_t peak_memory() const { return peak_memory_bytes_; }
  common::ObIAllocator &get_work_area_allocator()
  { return memory_allocator_; }
  bool memory_limit_exceeded() const
  { return memory_allocator_.limit_exceeded(); }

private:
  bool is_valid_binding_id(int64_t binding_id) const;
  int check_memory_limit();
  int fail(int error);

private:
  GraphWorkAreaAllocator memory_allocator_{};
  common::ObArenaAllocator row_allocator_{memory_allocator_};
  int64_t memory_limit_{0};
  common::ObArray<ObChunkDatumStore::StoredRow *> rows_{
      OB_MALLOC_NORMAL_BLOCK_SIZE,
      common::ModulePageAllocator(memory_allocator_, "GraphBinding")};
  int64_t peak_memory_bytes_{0};

  DISALLOW_COPY_AND_ASSIGN(GraphBindingStore);
};

} // namespace sql
} // namespace oceanbase
