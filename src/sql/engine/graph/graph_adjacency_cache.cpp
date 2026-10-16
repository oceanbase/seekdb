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
#include "sql/engine/graph/graph_adjacency_cache.h"

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

GraphAdjacencyCache::GraphAdjacencyCache(ObIAllocator &allocator, int64_t memory_limit)
    : cache_allocator_(allocator, memory_limit / GRAPH_CACHE_BUDGET_DIVISOR),
      identities_(cache_allocator_), index_(cache_allocator_),
      entries_(OB_MALLOC_NORMAL_BLOCK_SIZE, ModulePageAllocator(cache_allocator_, "GraphAdjCache")),
      cached_edges_(OB_MALLOC_NORMAL_BLOCK_SIZE, ModulePageAllocator(cache_allocator_, "GraphAdjCache")),
      contexts_(OB_MALLOC_NORMAL_BLOCK_SIZE, ModulePageAllocator(cache_allocator_, "GraphAdjCache")),
      context_buckets_(OB_MALLOC_NORMAL_BLOCK_SIZE, ModulePageAllocator(cache_allocator_, "GraphAdjCache"))
{}

void GraphAdjacencyCache::reset_cache()
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

int GraphAdjacencyCache::grow_context_buckets()
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

int GraphAdjacencyCache::select_context(const ObIArray<ObObj> &parameters)
{
  int ret = OB_SUCCESS;
  parameterized_ = !parameters.empty();
  context_id_ = parameterized_ ? -1 : 0;
  if (parameterized_) {
    int64_t size = 0;
    for (int64_t i = 0; i < parameters.count(); ++i) {
      const int64_t part = parameters.at(i).get_serialize_size();
      // ObString length is int32. Oversized optional keys bypass reuse rather
      // than narrowing values, truncating output, or changing SQL semantics.
      if (part < 0 || part > INT32_MAX - size) { return OB_SUCCESS; }
      size += part;
    }
    char *buffer = nullptr;
    if (OB_ISNULL(buffer = static_cast<char *>(cache_allocator_.alloc(size)))) {
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

int GraphAdjacencyCache::copy_identity(const GraphElementIdentity &source,
                                     GraphElementIdentity &dest)
{
  dest = source;
  return source.rowkey_.deep_copy(dest.rowkey_, identities_);
}

int GraphAdjacencyCache::recover_cache_budget(int ret)
{
  if (ret == OB_ALLOCATE_MEMORY_FAILED && cache_allocator_.budget_exceeded()) {
    // All cached hits were returned before recording starts, so clearing here
    // cannot invalidate any edge views in the current child-produced page.
    reset_cache();
    recording_state_ = RecordingState::DISABLED;
    first_new_entry_ = -1;
    ret = OB_SUCCESS;
  }
  return ret;
}

int GraphAdjacencyCache::begin_recording(const ObIArray<GraphElementIdentity> &sources)
{
  int ret = OB_SUCCESS;
  first_new_entry_ = entries_.count();
  recording_state_ = RecordingState::RECORDING;
  if (index_.count() == 0 && OB_FAIL(index_.init(sources.count()))) {
  }
  for (int64_t i = 0; OB_SUCC(ret) && i < sources.count(); ++i) {
    GraphElementIdentity stable;
    int64_t pos = -1;
    bool inserted = false;
    if (OB_FAIL(copy_identity(sources.at(i), stable))) {
    } else if (OB_FAIL(index_.get_or_insert(stable, pos, inserted, context_id_))) {
    } else if (!inserted || pos != entries_.count()) {
      ret = OB_ERR_UNEXPECTED;
    } else if (OB_FAIL(entries_.push_back(Entry()))) {
    }
  }
  return recover_cache_budget(ret);
}

int GraphAdjacencyCache::record(const ObIArray<GraphElementIdentity> &sources,
                                const ObIArray<GraphExpandEdge> &edges, bool end)
{
  int ret = OB_SUCCESS;
  if (recording_state_ == RecordingState::NOT_STARTED) { ret = begin_recording(sources); }
  for (int64_t i = 0; OB_SUCC(ret) && recording_state_ == RecordingState::RECORDING && i < edges.count(); ++i) {
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
  if (OB_SUCC(ret) && recording_state_ == RecordingState::RECORDING && end) {
    for (int64_t i = first_new_entry_; i < entries_.count(); ++i) { entries_.at(i).complete_ = true; }
    recording_state_ = RecordingState::COMPLETE;
    first_new_entry_ = -1;
  }
  return ret;
}

void GraphAdjacencyCache::release_recording()
{
  if (recording_state_ == RecordingState::RECORDING) { reset_cache(); }
  recording_state_ = RecordingState::NOT_STARTED;
  first_new_entry_ = -1;
}

void GraphAdjacencyCache::clear()
{
  reset_cache();
  recording_state_ = RecordingState::NOT_STARTED;
  first_new_entry_ = -1;
}

int GraphAdjacencyCache::find(const GraphElementIdentity &source,
                             int64_t &entry, bool &found) const
{
  int ret = OB_SUCCESS;
  entry = -1;
  found = false;
  if (index_.count() > 0 && OB_FAIL(index_.find(source, entry, found, context_id_))) {
  } else if (found && (entry < 0 || entry >= entries_.count() || !entries_.at(entry).complete_)) {
    ret = OB_ERR_UNEXPECTED;
  }
  return ret;
}

int GraphAdjacencyCache::read_edge(int64_t &position, GraphExpandEdge &edge) const
{
  int ret = OB_SUCCESS;
  if (position < 0 || position >= cached_edges_.count()) {
    ret = OB_ERR_UNEXPECTED;
  } else {
    edge = cached_edges_.at(position).value_;
    position = cached_edges_.at(position).next_;
  }
  return ret;
}

} // namespace sql
} // namespace oceanbase
