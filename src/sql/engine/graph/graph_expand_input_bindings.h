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
#include "sql/engine/graph/graph_identity_index.h"

namespace oceanbase
{
namespace sql
{

class IGraphExpandAccess;

// Owns one input batch and its source-to-binding associations. Sharing a source
// read never removes an input occurrence. DOUBLE keys retain linear matching.
class GraphExpandInputBindings final
{
public:
  GraphExpandInputBindings(common::ObIAllocator &allocator, IGraphExpandAccess &access,
                           int64_t memory_limit);
  int init(const common::ObIArray<GraphExpandInput> &inputs);
  int begin_edge(const GraphElementIdentity &source);
  int next_input(const GraphElementIdentity &source, const GraphExpandInput *&input);
  void reset();
  int64_t used_memory() const;
  bool grouped() const { return source_input_groups_enabled_; }
  const common::ObIArray<GraphElementIdentity> &sources() const { return source_identities_; }
  int find_source(const GraphElementIdentity &identity, int64_t &index, bool &found) const
  { return source_input_index_.find(identity, index, found); }
private:
  struct SourceInputGroup
  {
    int64_t first_input_{-1};
    int64_t last_input_{-1};
    TO_STRING_KV(K_(first_input), K_(last_input));
  };
  int deep_copy_identity(const GraphElementIdentity &source, GraphElementIdentity &destination);
  int deep_copy_inputs(const common::ObIArray<GraphExpandInput> &inputs);
  int validate_input_keys(const common::ObIArray<GraphExpandInput> &inputs);
  int build_source_input_groups();
  int start_edge_input_group(const GraphElementIdentity &source);
  bool contains(const common::ObIArray<GraphElementIdentity> &identities,
                const GraphElementIdentity &identity) const;
  int push_unique(common::ObIArray<GraphElementIdentity> &identities,
                  const GraphElementIdentity &identity);
  int check_memory_limit() const;
  IGraphExpandAccess &access_;
  int64_t memory_limit_;
  common::ObArenaAllocator identity_allocator_;
  GraphIdentityIndex source_input_index_;
  common::ObArray<GraphExpandInput> inputs_;
  common::ObArray<int64_t> input_key_slots_;
  common::ObArray<GraphElementIdentity> source_identities_;
  common::ObArray<SourceInputGroup> source_input_groups_;
  common::ObArray<int64_t> next_input_indices_;
  int64_t input_index_{-1};
  bool source_input_groups_enabled_{true};
  bool input_group_started_{false};
  DISALLOW_COPY_AND_ASSIGN(GraphExpandInputBindings);
};

} // namespace sql
} // namespace oceanbase
