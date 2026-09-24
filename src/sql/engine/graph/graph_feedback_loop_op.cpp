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
#include "lib/utility/ob_macro_utils.h"
#include "share/ob_errno.h"
#include "sql/engine/table/ob_table_scan_op.h"

#include <algorithm>
#include <utility>

namespace oceanbase
{
using namespace common;
namespace sql
{

namespace
{

const ObTableScanSpec *find_table_scan_spec(const ObOpSpec *root,
                                            uint64_t op_id)
{
  const ObTableScanSpec *result = nullptr;
  if (root != nullptr && root->get_id() == op_id && root->is_table_scan()) {
    result = static_cast<const ObTableScanSpec *>(root);
  }
  for (uint32_t i = 0; result == nullptr && root != nullptr
                         && i < root->get_child_cnt(); ++i) {
    result = find_table_scan_spec(root->get_child(i), op_id);
  }
  return result;
}

} // namespace

GraphFeedbackFrontier::GraphFeedbackFrontier(ObIAllocator &allocator,
                                             GraphExpand &expand,
                                             int64_t input_batch_size,
                                             int64_t memory_limit)
    : expand_(expand),
      state_store_(allocator, memory_limit),
      input_batch_size_(input_batch_size),
      memory_limit_(memory_limit),
      frontier_buffer_a_(OB_MALLOC_NORMAL_BLOCK_SIZE,
                         ModulePageAllocator(allocator, "GraphFrontier")),
      frontier_buffer_b_(OB_MALLOC_NORMAL_BLOCK_SIZE,
                         ModulePageAllocator(allocator, "GraphFrontier")),
      current_state_ids_(&frontier_buffer_a_),
      next_state_ids_(&frontier_buffer_b_),
      input_batch_(OB_MALLOC_NORMAL_BLOCK_SIZE,
                   ModulePageAllocator(allocator, "GraphFrontier"))
{
}

int GraphFeedbackFrontier::check_memory_limit()
{
  int ret = OB_SUCCESS;
  int64_t used = state_store_.used_memory();
  const int64_t buffer_a_bytes = frontier_buffer_a_.get_data_size();
  const int64_t buffer_b_bytes = frontier_buffer_b_.get_data_size();
  const int64_t input_bytes = input_batch_.get_data_size();
  const int64_t memory_parts[] = {
      buffer_a_bytes, buffer_b_bytes, input_bytes, expand_.used_memory()};

  if (OB_UNLIKELY(memory_limit_ <= 0)) {
    ret = OB_INVALID_ARGUMENT;
    LOG_WARN("invalid graph frontier memory limit", K(ret), K_(memory_limit));
  }
  for (int64_t i = 0; OB_SUCC(ret)
                          && i < ARRAYSIZEOF(memory_parts); ++i) {
    if (OB_UNLIKELY(memory_parts[i] < 0
                    || used > INT64_MAX - memory_parts[i])) {
      ret = OB_SIZE_OVERFLOW;
      LOG_WARN("graph frontier memory accounting overflow", K(ret), K(used),
               K(i), "part", memory_parts[i]);
    } else {
      used += memory_parts[i];
    }
  }
  if (OB_SUCC(ret) && OB_UNLIKELY(used > memory_limit_)) {
    ret = OB_EXCEED_QUERY_MEM_LIMIT;
    LOG_WARN("graph frontier exceeded query memory limit", K(ret), K(used),
             K_(memory_limit));
  } else if (OB_SUCC(ret)) {
    peak_memory_bytes_ = std::max(peak_memory_bytes_, used);
  }
  return ret;
}

int GraphFeedbackFrontier::add_seed(
    int64_t binding_id,
    const GraphElementIdentity &source_identity)
{
  int ret = OB_SUCCESS;
  int64_t path_state_id = GRAPH_INVALID_PATH_STATE_ID;
  if (OB_UNLIKELY(hop_ != 0 || input_batch_size_ <= 0
                  || input_batch_size_ > GRAPH_EXPAND_MAX_INPUT_STATE_COUNT
                  || memory_limit_ <= 0)) {
    ret = OB_INVALID_ARGUMENT;
    LOG_WARN("invalid graph frontier seed state", K(ret), K_(hop),
             K_(input_batch_size), K_(memory_limit));
  } else if (OB_FAIL(state_store_.add_root(binding_id, source_identity,
                                            path_state_id))) {
    LOG_WARN("failed to add graph frontier seed", K(ret), K(binding_id));
  } else if (OB_FAIL(current_state_ids_->push_back(path_state_id))) {
    LOG_WARN("failed to append graph frontier state id", K(ret),
             K(path_state_id));
  } else if (OB_FAIL(check_memory_limit())) {
  }
  if (OB_SUCCESS != ret) {
    ret = fail(ret);
  }
  return ret;
}

int GraphFeedbackFrontier::build_input_batch(int64_t start,
                                             int64_t &next_start)
{
  int ret = OB_SUCCESS;
  next_start = start;
  input_batch_.reuse();
  if (OB_UNLIKELY(start < 0 || start >= current_state_ids_->count()
                  || input_batch_size_ <= 0
                  || input_batch_size_ > GRAPH_EXPAND_MAX_INPUT_STATE_COUNT)) {
    ret = OB_INVALID_ARGUMENT;
    LOG_WARN("invalid graph frontier input batch", K(ret), K(start),
             "frontier_count", current_state_ids_->count(),
             K_(input_batch_size));
  } else {
    const int64_t count = std::min(input_batch_size_,
                                   current_state_ids_->count() - start);
    next_start = start + count;
    for (int64_t i = start; OB_SUCC(ret) && i < next_start; ++i) {
      GraphPathState state;
      GraphExpandInput input;
      const int64_t path_state_id = current_state_ids_->at(i);
      if (OB_FAIL(state_store_.get_state(path_state_id, state))) {
        LOG_WARN("failed to read graph frontier state", K(ret),
                 K(path_state_id));
      } else if (OB_UNLIKELY(state.hop_ != hop_)) {
        ret = OB_ERR_UNEXPECTED;
        LOG_WARN("graph frontier contains a state from another hop", K(ret),
                 K_(hop), K(state));
      } else if (OB_FAIL(state_store_.make_expand_input(path_state_id, input))) {
        LOG_WARN("failed to make graph expand input", K(ret),
                 K(path_state_id));
      } else if (OB_FAIL(input_batch_.push_back(input))) {
        LOG_WARN("failed to append graph expand input", K(ret),
                 K(path_state_id));
      }
    }
    if (OB_SUCC(ret) && OB_FAIL(check_memory_limit())) {
    }
  }
  return ret;
}

int GraphFeedbackFrontier::consume_input_batch(
    GraphPathDirection direction,
    GraphPathMode path_mode)
{
  int ret = OB_SUCCESS;
  if (OB_FAIL(expand_.rescan(input_batch_, direction))) {
    LOG_WARN("failed to open graph expand for frontier batch", K(ret),
             "input_count", input_batch_.count());
  }
  while (OB_SUCC(ret)) {
    GraphExpandOutput extension;
    bool repeated_edge = false;
    int next_ret = expand_.get_next_row(extension);
    if (next_ret == OB_ITER_END) {
      break;
    } else if (OB_SUCCESS != next_ret) {
      ret = next_ret;
      LOG_WARN("failed to read graph frontier extension", K(ret));
    } else if (path_mode == GraphPathMode::TRAIL
               && OB_FAIL(state_store_.contains_edge(
                   extension.path_state_id_, extension.edge_identity_,
                   repeated_edge))) {
      LOG_WARN("failed to inspect graph path edge history", K(ret),
               K(extension));
    } else if (!repeated_edge) {
      int64_t child_state_id = GRAPH_INVALID_PATH_STATE_ID;
      if (OB_FAIL(state_store_.add_child(extension, child_state_id))) {
        LOG_WARN("failed to add graph frontier child", K(ret), K(extension));
      } else if (OB_FAIL(next_state_ids_->push_back(child_state_id))) {
        LOG_WARN("failed to append next graph frontier state", K(ret),
                 K(child_state_id));
      } else if (OB_FAIL(check_memory_limit())) {
      }
    }
  }
  // Include the access component's resident buffers before releasing its
  // current scanner state. Its reusable identity arena remains accounted by
  // GraphExpand::used_memory() after release().
  if (OB_SUCC(ret) && OB_FAIL(check_memory_limit())) {
  }
  expand_.release();
  return ret;
}

int GraphFeedbackFrontier::expand(GraphPathDirection direction,
                                  GraphPathMode path_mode)
{
  int ret = OB_SUCCESS;
  int64_t start = 0;
  int64_t next_start = 0;
  if (current_state_ids_->empty()) {
    ret = OB_ITER_END;
  } else if (OB_UNLIKELY(hop_ >= GRAPH_PATH_MAX_HOPS
                         || (direction != GraphPathDirection::OUT
                             && direction != GraphPathDirection::IN)
                         || (path_mode != GraphPathMode::WALK
                             && path_mode != GraphPathMode::TRAIL))) {
    ret = OB_INVALID_ARGUMENT;
    LOG_WARN("invalid graph frontier expansion", K(ret), K_(hop),
             "direction", static_cast<int64_t>(direction),
             "path_mode", static_cast<int64_t>(path_mode));
  } else {
    next_state_ids_->reuse();
    while (OB_SUCC(ret) && start < current_state_ids_->count()) {
      if (OB_FAIL(build_input_batch(start, next_start))) {
      } else if (OB_FAIL(consume_input_batch(direction, path_mode))) {
      } else {
        start = next_start;
      }
    }
    if (OB_SUCC(ret)) {
      std::swap(current_state_ids_, next_state_ids_);
      next_state_ids_->reuse();
      ++hop_;
      if (OB_FAIL(check_memory_limit())) {
      }
    }
  }
  if (OB_SUCCESS != ret && OB_ITER_END != ret) {
    ret = fail(ret);
  }
  return ret;
}

int GraphFeedbackFrontier::get_frontier_state(
    int64_t index,
    GraphPathState &state) const
{
  int ret = OB_SUCCESS;
  if (OB_UNLIKELY(index < 0 || index >= current_state_ids_->count())) {
    ret = OB_INVALID_ARGUMENT;
    LOG_WARN("invalid graph frontier index", K(ret), K(index),
             "frontier_count", current_state_ids_->count());
  } else if (OB_FAIL(state_store_.get_state(current_state_ids_->at(index),
                                             state))) {
  } else if (OB_UNLIKELY(state.hop_ != hop_)) {
    ret = OB_ERR_UNEXPECTED;
    LOG_WARN("graph frontier state hop is inconsistent", K(ret), K_(hop),
             K(state));
  }
  return ret;
}

int GraphFeedbackFrontier::fail(int error)
{
  reset();
  return error;
}

void GraphFeedbackFrontier::reset()
{
  expand_.release();
  state_store_.reset();
  frontier_buffer_a_.reset();
  frontier_buffer_b_.reset();
  input_batch_.reset();
  current_state_ids_ = &frontier_buffer_a_;
  next_state_ids_ = &frontier_buffer_b_;
  hop_ = 0;
  peak_memory_bytes_ = 0;
}

GraphFeedbackLoopSpec::GraphFeedbackLoopSpec(common::ObIAllocator &allocator,
                                             const ObPhyOperatorType type)
    : RecursivePumpSpec(allocator, type),
      source_scan_desc_(allocator),
      edge_scan_desc_(allocator),
      target_scan_desc_(allocator)
{
}

int GraphFeedbackLoopSpec::bind_expand_scans(
    uint64_t source_scan_op_id,
    uint64_t edge_scan_op_id,
    uint64_t target_scan_op_id)
{
  int ret = OB_SUCCESS;
  const ObTableScanSpec *source_scan = find_table_scan_spec(
      get_left(), source_scan_op_id);
  const ObTableScanSpec *edge_scan = find_table_scan_spec(
      get_right(), edge_scan_op_id);
  const ObTableScanSpec *target_scan = find_table_scan_spec(
      get_right(), target_scan_op_id);
  if (OB_UNLIKELY(!expand_access_desc_.is_valid())) {
    ret = OB_INVALID_ARGUMENT;
    LOG_WARN("invalid graph expand access descriptor", K(ret),
             K_(expand_access_desc));
  } else if (OB_UNLIKELY(source_scan_op_id == OB_INVALID_ID
                         || edge_scan_op_id == OB_INVALID_ID
                         || target_scan_op_id == OB_INVALID_ID
                         || source_scan_op_id == edge_scan_op_id
                         || source_scan_op_id == target_scan_op_id
                         || edge_scan_op_id == target_scan_op_id)) {
    ret = OB_ERR_UNEXPECTED;
    LOG_WARN("graph expand table scan binding is invalid", K(ret),
             K(source_scan_op_id), K(edge_scan_op_id),
             K(target_scan_op_id));
  } else if (OB_ISNULL(source_scan) || OB_ISNULL(edge_scan)
             || OB_ISNULL(target_scan)) {
    ret = OB_ERR_UNEXPECTED;
    LOG_WARN("graph expand table scan spec is missing", K(ret),
             K_(expand_access_desc), K(source_scan), K(edge_scan),
             K(target_scan));
  } else if (OB_UNLIKELY(source_scan->ref_table_id_
                             != expand_access_desc_.source_table_id_
                         || edge_scan->ref_table_id_
                             != expand_access_desc_.edge_table_id_
                         || edge_scan->tsc_ctdef_.scan_ctdef_.ref_table_id_
                             != expand_access_desc_.edge_access_table_id_
                         || target_scan->ref_table_id_
                             != expand_access_desc_.target_table_id_)) {
    ret = OB_ERR_UNEXPECTED;
    LOG_WARN("graph expand table scan spec does not match its descriptor",
             K(ret), K_(expand_access_desc), KPC(source_scan), KPC(edge_scan),
             KPC(target_scan));
  } else if (OB_FAIL(source_scan_desc_.init(*source_scan))) {
    LOG_WARN("failed to bind graph source scan descriptor", K(ret),
             K(source_scan_op_id));
  } else if (OB_FAIL(edge_scan_desc_.init(*edge_scan))) {
    LOG_WARN("failed to bind graph edge scan descriptor", K(ret),
             K(edge_scan_op_id));
  } else if (OB_FAIL(target_scan_desc_.init(*target_scan))) {
    LOG_WARN("failed to bind graph target scan descriptor", K(ret),
             K(target_scan_op_id));
  }
  return ret;
}

int GraphFeedbackLoopOp::inner_open()
{
  int ret = OB_SUCCESS;
  if (OB_UNLIKELY(!get_graph_spec().get_path_desc().is_valid()
                  || !get_graph_spec().get_expand_access_desc().is_valid()
                  || !get_graph_spec().get_source_scan_desc().is_valid()
                  || !get_graph_spec().get_edge_scan_desc().is_valid()
                  || !get_graph_spec().get_target_scan_desc().is_valid())) {
    ret = OB_ERR_UNEXPECTED;
    LOG_WARN("invalid graph feedback physical descriptor", K(ret),
             K(get_graph_spec().get_path_desc()),
             K(get_graph_spec().get_expand_access_desc()));
  } else if (OB_FAIL(RecursivePumpOp::inner_open())) {
  }
  return ret;
}

// Cached plans need the graph and element mapping IDs as well as traversal
// controls. A native access adapter can therefore consume this descriptor
// directly instead of reverse-engineering the recursive child plan.
OB_SERIALIZE_MEMBER((GraphFeedbackLoopSpec, RecursivePumpSpec),
                    path_desc_.graph_id_,
                    path_desc_.graph_version_,
                    path_desc_.source_element_id_,
                    path_desc_.edge_element_id_,
                    path_desc_.target_element_id_,
                    path_desc_.lower_bound_,
                    path_desc_.upper_bound_,
                    path_desc_.direction_,
                    path_desc_.path_mode_,
                    path_desc_.row_shape_,
                    path_desc_.need_path_,
                    expand_access_desc_,
                    source_scan_desc_,
                    edge_scan_desc_,
                    target_scan_desc_);

} // namespace sql
} // namespace oceanbase
