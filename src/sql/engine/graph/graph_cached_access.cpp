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

#define USING_LOG_PREFIX SQL_ENG
#include "sql/engine/graph/graph_cached_access.h"

namespace oceanbase
{
using namespace common;
namespace sql
{
namespace
{
// Reuse is optional and cannot consume the entire path execution work area.
constexpr int64_t GRAPH_CACHE_BUDGET_DIVISOR = 8;
}

void *GraphCacheAllocator::alloc(int64_t size)
{
  void *ptr = nullptr;
  const int64_t header = TrackedAllocator::header_size();
  if (size > 0 && size <= INT64_MAX - header
      && total() <= limit_ && size + header <= limit_ - total()) {
    if ((ptr = tracked_.alloc(size)) != nullptr) { ++allocations_; }
  } else {
    budget_exceeded_ = true;
  }
  return ptr;
}

void GraphCacheAllocator::free(void *ptr)
{
  if (ptr != nullptr) {
    tracked_.free(ptr);
    --allocations_;
  }
}

int64_t GraphCacheAllocator::total() const
{
  return tracker_.used() + allocations_ * TrackedAllocator::header_size();
}

GraphCachedAccess::GraphCachedAccess(ObIAllocator &allocator,
                                    IGraphExpandAccess &child,
                                    int64_t memory_limit)
    : child_(child),
      cache_allocator_(allocator, memory_limit / GRAPH_CACHE_BUDGET_DIVISOR),
      identities_(cache_allocator_), index_(cache_allocator_),
      entries_(OB_MALLOC_NORMAL_BLOCK_SIZE, ModulePageAllocator(cache_allocator_, "GraphAdjCache")),
      cached_edges_(OB_MALLOC_NORMAL_BLOCK_SIZE, ModulePageAllocator(cache_allocator_, "GraphAdjCache")),
      contexts_(OB_MALLOC_NORMAL_BLOCK_SIZE, ModulePageAllocator(cache_allocator_, "GraphAdjCache")),
      context_buckets_(OB_MALLOC_NORMAL_BLOCK_SIZE, ModulePageAllocator(cache_allocator_, "GraphAdjCache")),
      hits_(OB_MALLOC_NORMAL_BLOCK_SIZE, ModulePageAllocator(allocator, "GraphAdjCache")),
      misses_(OB_MALLOC_NORMAL_BLOCK_SIZE, ModulePageAllocator(allocator, "GraphAdjCache")),
      cursor_allocator_(allocator)
{}

void GraphCachedAccess::reset_cache()
{
  for (int64_t i = 0; i < contexts_.count(); ++i) {
    cache_allocator_.free(contexts_.at(i).key_.ptr());
  }
  contexts_.reset();
  context_buckets_.reset();
  context_id_ = parameterized_ ? -1 : 0;
  index_.reset();
  entries_.reset();
  cached_edges_.reset();
  identities_.reset();
  cache_allocator_.clear_budget_error();
}

int GraphCachedAccess::grow_context_buckets()
{
  int ret = OB_SUCCESS;
  if (contexts_.count() >= context_buckets_.count()) {
    if (context_buckets_.count() > INT64_MAX / 2) {
      ret = OB_SIZE_OVERFLOW;
    } else if (OB_FAIL(context_buckets_.prepare_allocate(
        std::max<int64_t>(1, context_buckets_.count() * 2)))) {
    } else {
      for (int64_t i = 0; i < context_buckets_.count(); ++i) { context_buckets_.at(i) = -1; }
      for (int64_t i = 0; i < contexts_.count(); ++i) {
        Context &context = contexts_.at(i);
        const int64_t bucket = context.hash_ % context_buckets_.count();
        context.next_ = context_buckets_.at(bucket);
        context_buckets_.at(bucket) = i;
      }
    }
  }
  return ret;
}

int GraphCachedAccess::select_context(const ObIArray<ObObj> &parameters)
{
  int ret = OB_SUCCESS;
  parameterized_ = !parameters.empty();
  context_id_ = parameterized_ ? -1 : 0;
  if (enabled_ && parameterized_) {
    int64_t size = 0;
    for (int64_t i = 0; i < parameters.count(); ++i) {
      const int64_t part = parameters.at(i).get_serialize_size();
      // ObString length is int32. Oversized optional keys bypass reuse rather
      // than narrowing values, truncating output, or changing SQL semantics.
      if (part < 0 || part > INT32_MAX - size) { return OB_SUCCESS; }
      size += part;
    }
    char *buffer = nullptr;
    if (OB_FAIL(check_status())) {
    } else if (OB_ISNULL(buffer = static_cast<char *>(cache_allocator_.alloc(size)))) {
      ret = OB_ALLOCATE_MEMORY_FAILED;
    } else {
      int64_t pos = 0;
      for (int64_t i = 0; OB_SUCC(ret) && i < parameters.count(); ++i) {
        ret = parameters.at(i).serialize(buffer, size, pos);
      }
      // ObObj serialization preserves NULL, type, collation and value lengths.
      // Conservative byte equality may miss equivalent values, never equate
      // distinct values just because their hashes or text representations agree.
      const ObString key(static_cast<int32_t>(size), buffer);
      const uint64_t hash = key.hash();
      int64_t index = context_buckets_.empty() ? -1
          : context_buckets_.at(hash % context_buckets_.count());
      int64_t visited = 0;
      while (OB_SUCC(ret) && index >= 0 && context_id_ < 0) {
        if (index >= contexts_.count() || visited++ >= contexts_.count()) {
          ret = OB_ERR_UNEXPECTED;
        } else if (contexts_.at(index).key_ == key) {
          context_id_ = index + 1;
        } else {
          index = contexts_.at(index).next_;
        }
      }
      if (OB_SUCC(ret) && context_id_ < 0) {
        if (OB_FAIL(grow_context_buckets())) {
        } else {
          const int64_t bucket = hash % context_buckets_.count();
          Context context;
          context.key_ = key;
          context.hash_ = hash;
          context.next_ = context_buckets_.at(bucket);
          if (OB_FAIL(contexts_.push_back(context))) {
          } else {
            context_buckets_.at(bucket) = contexts_.count() - 1;
            context_id_ = contexts_.count();
            buffer = nullptr; // Owned until reset_cache(), including rescans.
          }
        }
      }
    }
    if (buffer != nullptr) { cache_allocator_.free(buffer); }
    ret = recover_cache_budget(ret);
  }
  return ret;
}

void GraphCachedAccess::release()
{
  child_.release();
  // A rescan/close can abandon a partially consumed scan. Never publish its
  // partial entries, including empty-looking sources not visited yet by DAS.
  if (recording_) { reset_cache(); }
  recording_ = false;
  first_new_entry_ = -1;
  hits_.reset();
  misses_.reset();
  cursor_allocator_.reset();
  child_cursor_.reset();
  child_has_cursor_ = false;
  hit_position_ = 0;
  hit_edge_ = -1;
}

void GraphCachedAccess::clear()
{
  release();
  reset_cache();
}

int64_t GraphCachedAccess::used_memory() const
{
  const int64_t parts[] = {child_.used_memory(), cache_allocator_.total(),
      hits_.get_data_size(), misses_.get_data_size(), cursor_allocator_.total()};
  int64_t bytes = 0;
  for (int64_t part : parts) {
    if (part < 0 || part > INT64_MAX - bytes) { bytes = INT64_MAX; break; }
    bytes += part;
  }
  return bytes;
}

int GraphCachedAccess::copy_identity(const GraphElementIdentity &source,
                                     GraphElementIdentity &dest)
{
  dest = source;
  return source.rowkey_.deep_copy(dest.rowkey_, identities_);
}

int GraphCachedAccess::recover_cache_budget(int ret)
{
  if (ret == OB_ALLOCATE_MEMORY_FAILED && cache_allocator_.budget_exceeded()) {
    // All cached hits were returned before recording starts, so clearing here
    // cannot invalidate any edge views in the current child-produced page.
    reset_cache();
    recording_ = false;
    first_new_entry_ = -2;
    ret = OB_SUCCESS;
  }
  return ret;
}

int GraphCachedAccess::begin(const ObIArray<GraphElementIdentity> &sources)
{
  int ret = OB_SUCCESS;
  release();
  // Partitionability controls frontier batching, not cache correctness. A
  // full scan filters against the complete miss-source set in one pass; only
  // after that scan completes can each requested source become a cache entry.
  // Never expose can_partition_sources() as true merely because reuse is on:
  // doing so would turn a whole-frontier scan into one full scan per batch.
  bypass_ = !enabled_ || context_id_ < 0;
  for (int64_t i = 0; !bypass_ && i < sources.count(); ++i) {
    const ObRowkey &key = sources.at(i).rowkey_;
    for (int64_t k = 0; k < key.get_obj_cnt(); ++k) {
      // Tolerance equality is not an equivalence relation and cannot key reuse.
      if (key.get_obj_ptr()[k].get_type_class() == ObDoubleTC) { bypass_ = true; }
    }
  }
  for (int64_t i = 0; OB_SUCC(ret) && !bypass_ && i < sources.count(); ++i) {
    int64_t pos = -1;
    bool found = false;
    if (i % GRAPH_EXPAND_MAX_SOURCE_BATCH_SIZE == 0 && OB_FAIL(check_status())) {
    } else if (index_.count() > 0 && OB_FAIL(index_.find(sources.at(i), pos, found, context_id_))) {
    } else if (found && (pos < 0 || pos >= entries_.count() || !entries_.at(pos).complete_)) {
      ret = OB_ERR_UNEXPECTED;
    } else if (found) {
      ret = hits_.push_back(pos);
    } else {
      ret = misses_.push_back(sources.at(i));
    }
  }
  if (OB_SUCC(ret) && !hits_.empty()) {
    hit_edge_ = entries_.at(hits_.at(0)).first_edge_;
  }
  LOG_TRACE("graph adjacency reuse request", K(bypass_), K_(context_id), "hit_sources", hits_.count(),
            "miss_sources", misses_.count());
  return ret;
}

int GraphCachedAccess::begin_recording()
{
  int ret = OB_SUCCESS;
  first_new_entry_ = entries_.count();
  recording_ = true;
  if (index_.count() == 0 && OB_FAIL(index_.init(misses_.count()))) {
  }
  for (int64_t i = 0; OB_SUCC(ret) && i < misses_.count(); ++i) {
    GraphElementIdentity stable;
    int64_t pos = -1;
    bool inserted = false;
    if (OB_FAIL(copy_identity(misses_.at(i), stable))) {
    } else if (OB_FAIL(index_.get_or_insert(stable, pos, inserted, context_id_))) {
    } else if (!inserted || pos != entries_.count()) {
      ret = OB_ERR_UNEXPECTED;
    } else if (OB_FAIL(entries_.push_back(Entry()))) {
    }
  }
  return recover_cache_budget(ret);
}

int GraphCachedAccess::record(const ObIArray<GraphExpandEdge> &edges, bool end)
{
  int ret = OB_SUCCESS;
  if (first_new_entry_ == -1) { ret = begin_recording(); }
  for (int64_t i = 0; OB_SUCC(ret) && recording_ && i < edges.count(); ++i) {
    const GraphExpandEdge &edge = edges.at(i);
    Edge stable;
    int64_t pos = -1;
    bool found = false;
    if (OB_FAIL(index_.find(edge.source_identity_, pos, found, context_id_))) {
    } else if (!found || pos < first_new_entry_ || pos >= entries_.count()) {
      ret = OB_ERR_UNEXPECTED;
    } else if (OB_FAIL(copy_identity(edge.source_identity_, stable.value_.source_identity_))) {
    } else if (OB_FAIL(copy_identity(edge.edge_identity_, stable.value_.edge_identity_))) {
    } else if (OB_FAIL(copy_identity(edge.target_identity_, stable.value_.target_identity_))) {
    } else if (OB_FAIL(stable.value_.payload_.deep_copy(edge.payload_, identities_))) {
    } else if (OB_FAIL(cached_edges_.push_back(stable))) {
    } else {
      Entry &entry = entries_.at(pos);
      const int64_t edge_pos = cached_edges_.count() - 1;
      if (entry.last_edge_ < 0) { entry.first_edge_ = edge_pos; }
      else { cached_edges_.at(entry.last_edge_).next_ = edge_pos; }
      entry.last_edge_ = edge_pos;
    }
  }
  ret = recover_cache_budget(ret);
  if (OB_SUCC(ret) && recording_ && end) {
    for (int64_t i = first_new_entry_; i < entries_.count(); ++i) { entries_.at(i).complete_ = true; }
    recording_ = false;
  }
  return ret;
}

int GraphCachedAccess::scan_edges(const ObIArray<GraphElementIdentity> &sources,
                                  GraphPathDirection direction,
                                  const GraphElementIdentity *after_edge,
                                  int64_t limit, ObIArray<GraphExpandEdge> &edges,
                                  int64_t &scanned_edges, bool &end)
{
  int ret = OB_SUCCESS;
  edges.reuse();
  scanned_edges = 0;
  end = false;
  if (OB_FAIL(check_status())) {
  } else if (after_edge == nullptr && OB_FAIL(begin(sources))) {
  } else if (bypass_) {
    ret = child_.scan_edges(sources, direction, after_edge, limit, edges, scanned_edges, end);
  } else {
    while (OB_SUCC(ret) && hit_position_ < hits_.count() && edges.count() < limit) {
      if (hit_edge_ < 0) {
        if (++hit_position_ < hits_.count()) {
          hit_edge_ = entries_.at(hits_.at(hit_position_)).first_edge_;
        }
      } else if (OB_FAIL(edges.push_back(cached_edges_.at(hit_edge_).value_))) {
      } else {
        hit_edge_ = cached_edges_.at(hit_edge_).next_;
      }
    }
    if (OB_SUCC(ret) && !edges.empty()) {
      // Separate cached and physical pages so optional-cache eviction never
      // invalidates a returned mixed page. Empty hits are consumed above.
      end = hit_position_ == hits_.count() && misses_.empty();
    } else if (OB_SUCC(ret) && misses_.empty()) {
      end = true;
    } else if (OB_SUCC(ret)) {
      if (OB_FAIL(child_.scan_edges(misses_, direction,
                     child_has_cursor_ ? &child_cursor_ : nullptr,
                     limit, edges, scanned_edges, end))) {
      } else if (!edges.empty()) {
        cursor_allocator_.reuse();
        child_cursor_ = edges.at(edges.count() - 1).edge_identity_;
        if (OB_FAIL(edges.at(edges.count() - 1).edge_identity_.rowkey_.deep_copy(
                        child_cursor_.rowkey_, cursor_allocator_))) {
        } else { child_has_cursor_ = true; }
      }
      if (OB_SUCC(ret)) { ret = record(edges, end); }
    }
  }
  if (OB_FAIL(ret)) { clear(); edges.reuse(); end = false; }
  return ret;
}

} // namespace sql
} // namespace oceanbase
