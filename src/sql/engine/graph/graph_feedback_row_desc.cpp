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
#include "sql/engine/graph/graph_feedback_row_desc.h"

namespace oceanbase
{
using namespace common;
namespace sql
{

namespace
{
// One source of truth for the generated recursive row layout. Callers validate
// the path/access descriptors before computing offsets. A short output row
// produces a negative payload count and is rejected by init()/is_valid().
GraphFeedbackRowDesc make_feedback_row_layout(
    const GraphPathDesc &path_desc, const GraphExpandAccessDesc &access_desc,
    int64_t output_count)
{
  GraphFeedbackRowDesc desc;
  desc.source_key_expr_begin_ = 1;
  desc.source_key_expr_count_ = access_desc.source_key_count_;
  desc.current_key_expr_begin_ = desc.source_key_expr_begin_ + desc.source_key_expr_count_;
  desc.current_key_expr_count_ = access_desc.target_key_count_;
  desc.trail_key_expr_begin_ = desc.current_key_expr_begin_ + desc.current_key_expr_count_;
  desc.trail_key_expr_count_ = path_desc.needs_edge_key_history()
      ? path_desc.upper_bound_ * access_desc.edge_key_count_ : 0;
  desc.payload_expr_begin_ = desc.trail_key_expr_begin_ + desc.trail_key_expr_count_;
  desc.payload_expr_count_ = output_count - desc.payload_expr_begin_;
  desc.output_expr_count_ = output_count;
  return desc;
}
} // namespace

int GraphFeedbackRowDesc::init(const GraphPathDesc &path_desc,
                               const GraphExpandAccessDesc &access_desc,
                               int64_t output_count)
{
  int ret = OB_SUCCESS;
  if (OB_UNLIKELY(!path_desc.is_valid() || !access_desc.is_valid()
                  || output_count <= 0)) {
    ret = OB_INVALID_ARGUMENT;
    LOG_WARN("invalid graph feedback row description", K(ret), K(path_desc),
             K(access_desc), K(output_count));
  } else {
    const GraphFeedbackRowDesc desc = make_feedback_row_layout(path_desc, access_desc, output_count);
    if (OB_UNLIKELY(desc.payload_expr_count_ < 0)) {
      ret = OB_ERR_UNEXPECTED;
      LOG_WARN("graph feedback output row is too short", K(ret), K(output_count), K(desc));
    } else {
      *this = desc;
    }
  }
  return ret;
}

bool GraphFeedbackRowDesc::is_valid(
    const GraphPathDesc &path_desc, const GraphExpandAccessDesc &access_desc) const
{
  bool valid = path_desc.is_valid() && access_desc.is_valid() && output_expr_count_ > 0;
  if (valid) {
    const GraphFeedbackRowDesc expected =
        make_feedback_row_layout(path_desc, access_desc, output_expr_count_);
    valid = depth_expr_index_ == expected.depth_expr_index_
        && source_key_expr_begin_ == expected.source_key_expr_begin_
        && source_key_expr_count_ == expected.source_key_expr_count_
        && current_key_expr_begin_ == expected.current_key_expr_begin_
        && current_key_expr_count_ == expected.current_key_expr_count_
        && trail_key_expr_begin_ == expected.trail_key_expr_begin_
        && trail_key_expr_count_ == expected.trail_key_expr_count_
        && payload_expr_begin_ == expected.payload_expr_begin_
        && payload_expr_count_ >= 0
        && payload_expr_count_ == expected.payload_expr_count_;
  }
  return valid;
}

// Execution eligibility is separate from the positional row description.
bool supports_basic_native_graph_feedback(
    const GraphPathDesc &path_desc,
    const GraphExpandAccessDesc &access_desc,
    int64_t output_expr_count,
    int64_t payload_expr_count)
{
  bool supported = path_desc.is_valid() && access_desc.is_valid() && output_expr_count > 0;
  if (supported) {
    const GraphFeedbackRowDesc layout =
        make_feedback_row_layout(path_desc, access_desc, output_expr_count);
    supported = path_desc.direction_ != GraphPathDirection::BOTH
        && path_desc.source_element_id_ == path_desc.target_element_id_
        && path_desc.path_mode_ == GraphPathMode::WALK
        && (!path_desc.need_path_ || payload_expr_count > 0
            || path_desc.row_shape_ == GraphPathRowShape::PER_STEP)
        && access_desc.source_table_id_ == access_desc.target_table_id_
        && payload_expr_count >= 0
        && layout.payload_expr_count_ == payload_expr_count;
  }
  return supported;
}

OB_SERIALIZE_MEMBER(GraphFeedbackRowDesc,
                    depth_expr_index_,
                    source_key_expr_begin_,
                    source_key_expr_count_,
                    current_key_expr_begin_,
                    current_key_expr_count_,
                    trail_key_expr_begin_,
                    trail_key_expr_count_,
                    payload_expr_begin_,
                    payload_expr_count_,
                    output_expr_count_);

} // namespace sql
} // namespace oceanbase
