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
#include "sql/engine/graph/graph_path_spec.h"

namespace oceanbase
{
namespace sql
{
// Owns stable rowkeys/payloads for one edge page. Lookup/scanner buffers may be
// overwritten independently. The continuation identity outlives page turnover.
class GraphExpandEdgePage final
{
public:
  explicit GraphExpandEdgePage(common::ObIAllocator &allocator);
  void reuse();
  void reset();
  int stabilize_edge_page();
  int stabilize_targets() { return stabilize_identities(existing_targets_); }
  int save_continuation();
  int64_t used_memory() const;
  common::ObIArray<GraphExpandEdge> &edges() { return edge_page_; }
  common::ObIArray<GraphElementIdentity> &targets() { return target_identities_; }
  common::ObIArray<GraphElementIdentity> &existing_targets() { return existing_targets_; }
  const GraphElementIdentity *continuation() const
  { return has_after_edge_ ? &after_edge_ : nullptr; }
private:
  int deep_copy_identity(const GraphElementIdentity &source, GraphElementIdentity &destination);
  int stabilize_identities(common::ObIArray<GraphElementIdentity> &identities);
  common::ObArenaAllocator identity_allocator_;
  common::ObArenaAllocator cursor_allocator_;
  common::ObArray<GraphExpandEdge> edge_page_;
  common::ObArray<GraphElementIdentity> target_identities_;
  common::ObArray<GraphElementIdentity> existing_targets_;
  GraphElementIdentity after_edge_{};
  bool has_after_edge_{false};
  DISALLOW_COPY_AND_ASSIGN(GraphExpandEdgePage);
};

} // namespace sql
} // namespace oceanbase
