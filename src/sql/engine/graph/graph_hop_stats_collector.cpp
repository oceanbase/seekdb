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
#include "sql/engine/graph/graph_hop_stats_collector.h"
#include <algorithm>

namespace oceanbase
{
using namespace common;
namespace sql
{

namespace
{
int64_t graph_stats_saturated_add(int64_t left, int64_t right)
{
  return left < 0 || right < 0 || left > INT64_MAX - right ? INT64_MAX : left + right;
}
} // namespace

void GraphHopStatsCollector::begin(int64_t input_states, bool exact_sources)
{
  current_ = GraphExpandStats();
  current_.input_states_ = input_states;
  current_.distinct_sources_exact_ = exact_sources;
  active_ = true;
}

void GraphHopStatsCollector::record_memory(int64_t bytes)
{
  if (active_) {
    current_.peak_path_memory_ = std::max(current_.peak_path_memory_, bytes);
  }
}

void GraphHopStatsCollector::add_batch(const GraphExpandStats &batch)
{
  current_.distinct_sources_ = graph_stats_saturated_add(current_.distinct_sources_, batch.distinct_sources_);
  current_.scanned_edges_ = graph_stats_saturated_add(current_.scanned_edges_, batch.scanned_edges_);
  current_.looked_up_vertices_ = graph_stats_saturated_add(current_.looked_up_vertices_, batch.looked_up_vertices_);
  current_.orphan_edges_ = graph_stats_saturated_add(current_.orphan_edges_, batch.orphan_edges_);
}

void GraphHopStatsCollector::finish(int64_t sources, bool indexed_sources, int64_t outputs)
{
  last_ = snapshot(sources, indexed_sources, outputs, nullptr);
  active_ = false;
}

GraphExpandStats GraphHopStatsCollector::snapshot(
    int64_t sources, bool indexed_sources, int64_t outputs,
    const GraphExpandStats *pending) const
{
  GraphExpandStats stats = current_;
  if (pending != nullptr) {
    const GraphExpandStats &batch = *pending;
    stats.distinct_sources_ = graph_stats_saturated_add(stats.distinct_sources_, batch.distinct_sources_);
    stats.scanned_edges_ = graph_stats_saturated_add(stats.scanned_edges_, batch.scanned_edges_);
    stats.looked_up_vertices_ = graph_stats_saturated_add(stats.looked_up_vertices_, batch.looked_up_vertices_);
    stats.orphan_edges_ = graph_stats_saturated_add(stats.orphan_edges_, batch.orphan_edges_);
  }
  if (indexed_sources) { stats.distinct_sources_ = sources; }
  stats.output_states_ = outputs;
  return stats;
}

void GraphHopStatsCollector::reset()
{
  current_ = GraphExpandStats();
  last_ = GraphExpandStats();
  active_ = false;
}

} // namespace sql
} // namespace oceanbase
