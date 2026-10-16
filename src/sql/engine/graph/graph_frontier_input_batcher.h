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
#include "sql/engine/graph/graph_path_state_store.h"

namespace oceanbase
{
namespace sql
{

// Organizes one immutable BFS level into access batches. The batch remains
// valid until next_batch() or reset(); callers must release GraphExpand first.
// A batch limit counts source groups when grouping is supported, not paths.
class GraphFrontierInputBatcher final
{
public:
  GraphFrontierInputBatcher(common::ObIAllocator &allocator,
                           const GraphPathStateStore &states, GraphExpand &expand,
                           int64_t batch_size);
  // memory_budget excludes all other frontier/access allocations. Those stay
  // unchanged during each call; the owner recomputes the budget between calls.
  // The state IDs and their identities must outlive this level's access cursor.
  int begin_level(const common::ObIArray<int64_t> &states, int64_t hop,
                  int64_t memory_budget);
  int next_batch(int64_t memory_budget);
  bool exhausted() const { return state_ids_ == nullptr || next_position_ == input_units(); }
  const common::ObIArray<GraphExpandInput> &batch() const { return input_batch_; }
  int64_t used_memory() const;
  bool has_source_index() const { return source_index_enabled_; }
  int64_t source_count() const { return source_index_.count(); }
  // Called after the access cursor is released and final statistics are read.
  void finish_level();
  void reset();

private:
  struct SourceGroup
  {
    int64_t first_position_{-1};
    int64_t last_position_{-1};
  };
  int prepare_source_groups(int64_t memory_budget);
  int append_input(int64_t position, int64_t memory_budget);
  int check_memory_limit(int64_t memory_budget) const;
  int64_t input_units() const
  { return batch_by_source_groups_ ? source_groups_.count() : state_ids_->count(); }

  const GraphPathStateStore &states_;
  GraphExpand &expand_;
  // Shared source identity directory for grouping and source counts. Full-scan
  // inputs remain a single batch even when this directory is available.
  GraphIdentityIndex source_index_;
  common::ObArray<SourceGroup> source_groups_;
  common::ObArray<int64_t> next_source_positions_;
  common::ObArray<GraphExpandInput> input_batch_;
  const common::ObIArray<int64_t> *state_ids_{nullptr};
  int64_t batch_size_{0};
  int64_t hop_{0};
  int64_t next_position_{0};
  bool source_index_enabled_{false};
  bool batch_by_source_groups_{false};
  DISALLOW_COPY_AND_ASSIGN(GraphFrontierInputBatcher);
};

} // namespace sql
} // namespace oceanbase
