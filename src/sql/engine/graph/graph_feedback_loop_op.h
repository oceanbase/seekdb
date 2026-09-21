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

#include "sql/engine/graph/graph_path_spec.h"
#include "sql/engine/recursive_cte/ob_recursive_union_all_op.h"

namespace oceanbase
{
namespace sql
{

// The bounded graph path has recursive-union execution semantics, but it uses
// an independent physical operator type so plan cache serialization, EXPLAIN
// and future graph-specific accounting do not depend on recognizing a generic
// recursive CTE after code generation.
// TODO(graph-path-v2): This inheritance is transitional. RecursiveUnionAll is
// coupled to FakeCTETable and UNION row semantics. When GraphFeedbackLoop
// directly drives GraphExpand and graph path states, extract a generic feedback
// loop shared with recursive CTE, or make the graph Spec and Op independent.
class GraphFeedbackLoopSpec final : public ObRecursiveUnionAllSpec
{
  OB_UNIS_VERSION_V(1);
public:
  explicit GraphFeedbackLoopSpec(common::ObIAllocator &allocator,
                                 const ObPhyOperatorType type);
  ~GraphFeedbackLoopSpec() = default;

  void set_graph_path(int64_t lower_bound,
                      int64_t upper_bound,
                      GraphPathDirection direction,
                      GraphPathMode path_mode)
  {
    lower_bound_ = lower_bound;
    upper_bound_ = upper_bound;
    direction_ = direction;
    path_mode_ = path_mode;
  }

  int64_t get_lower_bound() const { return lower_bound_; }
  int64_t get_upper_bound() const { return upper_bound_; }
  GraphPathDirection get_direction() const { return direction_; }
  GraphPathMode get_path_mode() const { return path_mode_; }

private:
  int64_t lower_bound_{0};
  int64_t upper_bound_{0};
  GraphPathDirection direction_{GraphPathDirection::OUT};
  GraphPathMode path_mode_{GraphPathMode::WALK};
};

class GraphFeedbackLoopOp final : public ObRecursiveUnionAllOp
{
public:
  explicit GraphFeedbackLoopOp(ObExecContext &exec_ctx,
                               const ObOpSpec &spec,
                               ObOpInput *input)
      : ObRecursiveUnionAllOp(exec_ctx, spec, input)
  {}
  ~GraphFeedbackLoopOp() = default;
};

} // namespace sql
} // namespace oceanbase
