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
GraphCachedAccess::GraphCachedAccess(ObIAllocator &allocator,
                                    IGraphExpandAccess &child, int64_t memory_limit)
    : child_(child), cache_(allocator, memory_limit),
      hits_(OB_MALLOC_NORMAL_BLOCK_SIZE, ModulePageAllocator(allocator, "GraphAdjCache")),
      misses_(OB_MALLOC_NORMAL_BLOCK_SIZE, ModulePageAllocator(allocator, "GraphAdjCache")),
      cursor_allocator_(allocator)
{}

int GraphCachedAccess::select_context(const ObIArray<ObObj> &parameters)
{
  int ret = OB_SUCCESS;
  // Release borrowed cache pages and abort incomplete recording before changing
  // the namespace. A disabled adapter must not retain a previously enabled cache.
  release();
  if (!enabled_) {
    cache_.clear();
  } else if (OB_FAIL(check_status())) {
  } else {
    ret = cache_.select_context(parameters);
  }
  if (OB_FAIL(ret)) { clear(); }
  return ret;
}

void GraphCachedAccess::release()
{
  child_.release();
  // A rescan/close can abandon a partially consumed scan. Never publish its
  // partial entries, including empty-looking sources not visited yet by DAS.
  cache_.release_recording();
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
  cache_.clear();
}

int64_t GraphCachedAccess::used_memory() const
{
  const int64_t parts[] = {child_.used_memory(), cache_.used_memory(),
      hits_.get_data_size(), misses_.get_data_size(), cursor_allocator_.total()};
  int64_t bytes = 0;
  for (int64_t part : parts) {
    if (part < 0 || part > INT64_MAX - bytes) { bytes = INT64_MAX; break; }
    bytes += part;
  }
  return bytes;
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
  bypass_ = !enabled_ || !cache_.can_reuse();
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
    } else if (OB_FAIL(cache_.find(sources.at(i), pos, found))) {
    } else if (found) {
      ret = hits_.push_back(pos);
    } else {
      ret = misses_.push_back(sources.at(i));
    }
  }
  if (OB_SUCC(ret) && !hits_.empty()) {
    hit_edge_ = cache_.first_edge(hits_.at(0));
  }
  LOG_TRACE("graph adjacency reuse request", K(bypass_), "context_id", cache_.context_id(), "hit_sources", hits_.count(),
            "miss_sources", misses_.count());
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
          hit_edge_ = cache_.first_edge(hits_.at(hit_position_));
        }
      } else {
        GraphExpandEdge edge;
        if (OB_FAIL(cache_.read_edge(hit_edge_, edge))) {
        } else if (OB_FAIL(edges.push_back(edge))) {
        }
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
      if (OB_SUCC(ret)) { ret = cache_.record(misses_, edges, end); }
    }
  }
  if (OB_FAIL(ret)) { clear(); edges.reuse(); end = false; }
  return ret;
}

} // namespace sql
} // namespace oceanbase
