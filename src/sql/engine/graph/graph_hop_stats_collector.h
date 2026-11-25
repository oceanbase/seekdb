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

#include "sql/engine/graph/graph_expand.h"

namespace oceanbase
{
namespace sql
{

// Passive per-level counters: never selects an access strategy or owns path
// identities. Removing this collector must not change traversal semantics.
class GraphHopStatsCollector final
{
public:
  void begin(int64_t input_states, bool exact_sources);
  void add_batch(const GraphExpandStats &stats);
  void record_memory(int64_t bytes);
  GraphExpandStats snapshot(int64_t sources, bool indexed_sources,
                          int64_t outputs, const GraphExpandStats *pending) const;
  void finish(int64_t sources, bool indexed_sources, int64_t outputs);
  const GraphExpandStats &last() const { return last_; }
  void reset();
private:
  GraphExpandStats current_{};
  GraphExpandStats last_{};
  bool active_{false};
};

} // namespace sql
} // namespace oceanbase
