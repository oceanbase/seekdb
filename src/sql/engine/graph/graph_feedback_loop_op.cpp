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
#include "sql/engine/graph/graph_feedback_loop_op.h"

namespace oceanbase
{
namespace sql
{

GraphFeedbackLoopSpec::GraphFeedbackLoopSpec(common::ObIAllocator &allocator,
                                             const ObPhyOperatorType type)
    : ObRecursiveUnionAllSpec(allocator, type)
{
}

OB_SERIALIZE_MEMBER((GraphFeedbackLoopSpec, ObRecursiveUnionAllSpec),
                    lower_bound_,
                    upper_bound_,
                    direction_,
                    path_mode_);

} // namespace sql
} // namespace oceanbase
