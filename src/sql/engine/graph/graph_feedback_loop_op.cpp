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
#include "sql/engine/graph/graph_binding_store.h"
#include "sql/engine/graph/graph_expand_das_access.h"
#include "sql/engine/ob_sql_mem_mgr_processor.h"
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

int GraphFeedbackRowDesc::init(const GraphPathDesc &path_desc,
                               const GraphExpandAccessDesc &access_desc,
                               int64_t output_count)
{
  int ret = OB_SUCCESS;
  GraphFeedbackRowDesc desc;
  if (OB_UNLIKELY(!path_desc.is_valid() || !access_desc.is_valid()
                  || output_count <= 0)) {
    ret = OB_INVALID_ARGUMENT;
    LOG_WARN("invalid graph feedback row description", K(ret), K(path_desc),
             K(access_desc), K(output_count));
  } else {
    const int64_t trail_key_count = path_desc.path_mode_ == GraphPathMode::TRAIL
        ? path_desc.upper_bound_ * access_desc.edge_key_count_ : 0;
    const int64_t fixed_count = 1 + access_desc.source_key_count_
        + access_desc.target_key_count_ + trail_key_count;
    if (OB_UNLIKELY(output_count < fixed_count)) {
      ret = OB_ERR_UNEXPECTED;
      LOG_WARN("graph feedback output row is too short", K(ret),
               K(output_count), K(fixed_count), K(path_desc), K(access_desc));
    } else {
      desc.source_key_expr_begin_ = 1;
      desc.source_key_expr_count_ = access_desc.source_key_count_;
      desc.current_key_expr_begin_ = desc.source_key_expr_begin_
          + desc.source_key_expr_count_;
      desc.current_key_expr_count_ = access_desc.target_key_count_;
      desc.trail_key_expr_begin_ = desc.current_key_expr_begin_
          + desc.current_key_expr_count_;
      desc.trail_key_expr_count_ = trail_key_count;
      desc.payload_expr_begin_ = fixed_count;
      desc.payload_expr_count_ = output_count - fixed_count;
      desc.output_expr_count_ = output_count;
      *this = desc;
    }
  }
  return ret;
}

bool GraphFeedbackRowDesc::is_valid(
    const GraphPathDesc &path_desc,
    const GraphExpandAccessDesc &access_desc) const
{
  bool valid = path_desc.is_valid() && access_desc.is_valid();
  if (valid) {
    const int64_t trail_key_count = path_desc.path_mode_ == GraphPathMode::TRAIL
        ? path_desc.upper_bound_ * access_desc.edge_key_count_ : 0;
    const int64_t fixed_count = 1 + access_desc.source_key_count_
        + access_desc.target_key_count_ + trail_key_count;
    valid = depth_expr_index_ == 0
        && source_key_expr_begin_ == 1
        && source_key_expr_count_ == access_desc.source_key_count_
        && current_key_expr_begin_
               == source_key_expr_begin_ + source_key_expr_count_
        && current_key_expr_count_ == access_desc.target_key_count_
        && trail_key_expr_begin_
               == current_key_expr_begin_ + current_key_expr_count_
        && trail_key_expr_count_ == trail_key_count
        && payload_expr_begin_ == fixed_count
        && payload_expr_count_ >= 0
        && output_expr_count_ == payload_expr_begin_ + payload_expr_count_;
  }
  return valid;
}

// Owns the native graph components as one allocation so their reference
// dependencies are constructed and destroyed in a fixed order. It remains
// query-local; no state is shared by rescans or concurrent executions.
class GraphFeedbackRuntime final
{
public:
  GraphFeedbackRuntime(ObExecContext &exec_ctx,
                       ObEvalCtx &eval_ctx,
                       int64_t memory_limit)
      : eval_ctx_(eval_ctx),
        access_(exec_ctx, eval_ctx),
        expand_(binding_store_.get_work_area_allocator(), access_,
                GRAPH_EXPAND_MAX_EDGE_PAGE_SIZE,
                memory_limit),
        frontier_(binding_store_.get_work_area_allocator(), expand_,
                  GRAPH_EXPAND_MAX_INPUT_STATE_COUNT, memory_limit),
        memory_limit_(memory_limit)
  {}
  ~GraphFeedbackRuntime() = default;

  int init(const GraphFeedbackLoopSpec &spec)
  {
    int ret = OB_SUCCESS;
    if (OB_FAIL(binding_store_.init(memory_limit_))) {
      LOG_WARN("failed to initialize graph feedback work area", K(ret),
               K_(memory_limit));
    } else if (OB_FAIL(access_.init(
                   spec.get_path_desc(), spec.get_expand_access_desc(),
                   spec.get_source_scan_desc(), spec.get_edge_scan_desc(),
                   spec.get_target_scan_desc()))) {
      LOG_WARN("failed to initialize graph expand access", K(ret));
    }
    return ret;
  }

  // This loader stays dormant while RecursivePumpOp owns row production. The
  // native executor will call it only after taking exclusive ownership of the
  // anchor child, avoiding duplicate seed storage in the fallback work area.
  int load_seeds(ObOperator &anchor, const GraphFeedbackLoopSpec &spec)
  {
    int ret = OB_SUCCESS;
    if (OB_UNLIKELY(seeds_loaded_)) {
      ret = OB_INIT_TWICE;
      LOG_WARN("graph feedback seeds are already loaded", K(ret),
               "binding_count", binding_store_.count());
    }
    while (OB_SUCC(ret) && !seeds_loaded_) {
      const int next_ret = anchor.get_next_row();
      if (next_ret == OB_ITER_END) {
        seeds_loaded_ = true;
      } else if (next_ret != OB_SUCCESS) {
        ret = next_ret;
        LOG_WARN("failed to read graph feedback seed row", K(ret));
      } else if (OB_FAIL(add_seed(anchor.get_spec().output_, spec))) {
        LOG_WARN("failed to save graph feedback seed row", K(ret));
      }
    }
    if (OB_SUCCESS != ret) {
      reset();
    }
    return ret;
  }

  // The current frontier represents one complete BFS level. A level is
  // externally visible only inside the inclusive SQL hop bounds; expansion
  // may still be required for levels below the lower bound.
  bool has_output_frontier(const GraphFeedbackLoopSpec &spec) const
  {
    const int64_t hop = frontier_.get_hop();
    return seeds_loaded_
        && !frontier_.empty()
        && hop >= spec.get_lower_bound()
        && hop <= spec.get_upper_bound();
  }

  int64_t get_output_count(const GraphFeedbackLoopSpec &spec) const
  {
    return has_output_frontier(spec) ? frontier_.get_frontier_count() : 0;
  }

  int get_output_state(const GraphFeedbackLoopSpec &spec,
                       int64_t index,
                       GraphPathState &state) const
  {
    int ret = OB_SUCCESS;
    if (!has_output_frontier(spec)) {
      ret = OB_ITER_END;
    } else if (OB_FAIL(frontier_.get_frontier_state(index, state))) {
      LOG_WARN("failed to read graph feedback output state", K(ret), K(index));
    }
    return ret;
  }

  int get_output_path(
      const GraphFeedbackLoopSpec &spec,
      int64_t index,
      const ObIArray<GraphPathState> *&path)
  {
    int ret = OB_SUCCESS;
    path = nullptr;
    if (!has_output_frontier(spec)) {
      ret = OB_ITER_END;
    } else if (OB_FAIL(frontier_.get_frontier_path(index, path))) {
      ret = normalize_work_area_error(ret);
      LOG_WARN("failed to reconstruct graph feedback output path", K(ret),
               K(index));
    }
    return ret;
  }

  // Emits every path in one visible BFS level before advancing to the next
  // level. Before returning, restores the anchor/outer row associated with
  // that path so later result materialization can evaluate correlated values.
  // Levels below the SQL lower bound are expanded but not returned.
  int get_next_output_path(
      const GraphFeedbackLoopSpec &spec,
      const ObIArray<ObExpr *> &binding_exprs,
      const ObIArray<GraphPathState> *&path)
  {
    int ret = OB_SUCCESS;
    path = nullptr;
    if (OB_UNLIKELY(!seeds_loaded_)) {
      ret = OB_NOT_INIT;
      LOG_WARN("graph feedback seeds are not loaded", K(ret));
    }
    while (OB_SUCC(ret) && path == nullptr) {
      if (has_output_frontier(spec)
          && next_output_index_ < get_output_count(spec)) {
        if (OB_FAIL(get_output_path(spec, next_output_index_, path))) {
          LOG_WARN("failed to read next graph feedback output path", K(ret),
                   K_(next_output_index));
        } else if (OB_FAIL(restore_output_binding(*path, binding_exprs))) {
          path = nullptr;
          LOG_WARN("failed to restore graph feedback output binding", K(ret),
                   K_(next_output_index));
        } else {
          ++next_output_index_;
        }
      } else if (frontier_.empty()
                 || frontier_.get_hop() >= spec.get_upper_bound()) {
        ret = OB_ITER_END;
      } else if (OB_FAIL(advance_frontier(spec))) {
        if (ret != OB_ITER_END) {
          LOG_WARN("failed to advance to next graph feedback output level",
                   K(ret));
        }
      }
    }
    if (OB_SUCCESS != ret && OB_ITER_END != ret) {
      reset();
    }
    return ret;
  }

  // Advances exactly one level and never crosses the SQL upper bound. WALK
  // and TRAIL decisions remain path-local inside GraphFeedbackFrontier.
  int advance_frontier(const GraphFeedbackLoopSpec &spec)
  {
    int ret = OB_SUCCESS;
    const int64_t current_hop = frontier_.get_hop();
    if (OB_UNLIKELY(!seeds_loaded_)) {
      ret = OB_NOT_INIT;
      LOG_WARN("graph feedback seeds are not loaded", K(ret));
    } else if (frontier_.empty() || current_hop >= spec.get_upper_bound()) {
      ret = OB_ITER_END;
    } else if (OB_FAIL(frontier_.expand(spec.get_direction(),
                                        spec.get_path_mode()))) {
      ret = normalize_work_area_error(ret);
      if (ret != OB_ITER_END) {
        LOG_WARN("failed to advance graph feedback frontier", K(ret),
                 K(current_hop));
      }
    } else if (frontier_.empty()) {
      ret = OB_ITER_END;
    } else {
      next_output_index_ = 0;
    }
    if (OB_SUCCESS != ret && OB_ITER_END != ret) {
      reset();
    }
    return ret;
  }

  void reset()
  {
    frontier_.reset();
    binding_store_.reset();
    next_output_index_ = 0;
    seeds_loaded_ = false;
  }

private:
  int restore_output_binding(
      const ObIArray<GraphPathState> &path,
      const ObIArray<ObExpr *> &binding_exprs)
  {
    int ret = OB_SUCCESS;
    if (OB_UNLIKELY(path.empty())) {
      ret = OB_ERR_UNEXPECTED;
      LOG_WARN("graph feedback output path is empty", K(ret));
    } else if (OB_FAIL(binding_store_.restore_binding(
                   path.at(path.count() - 1).binding_id_,
                   binding_exprs, eval_ctx_))) {
      LOG_WARN("failed to restore graph feedback binding", K(ret),
               "binding_id", path.at(path.count() - 1).binding_id_);
    }
    return ret;
  }

  int add_seed(const ObIArray<ObExpr *> &binding_exprs,
               const GraphFeedbackLoopSpec &spec)
  {
    int ret = OB_SUCCESS;
    int64_t binding_id = -1;
    GraphElementIdentity identity;
    ObObj key_values[OB_USER_MAX_ROWKEY_COLUMN_NUMBER]{};
    const ExprFixedArray &key_exprs = spec.get_seed_key_exprs();
    const int64_t key_count = key_exprs.count();
    if (OB_UNLIKELY(!spec.has_valid_seed_key_exprs())) {
      ret = OB_ERR_UNEXPECTED;
      LOG_WARN("invalid graph feedback seed state", K(ret), K(key_count),
               "binding_count", binding_store_.count());
    } else {
      identity.graph_id_ = spec.get_path_desc().graph_id_;
      identity.element_id_ = spec.get_path_desc().source_element_id_;
      identity.rowkey_.assign(key_values, key_count);
    }
    for (int64_t i = 0; OB_SUCC(ret) && i < key_count; ++i) {
      ObExpr *expr = key_exprs.at(i);
      ObDatum *datum = nullptr;
      if (OB_ISNULL(expr)) {
        ret = OB_ERR_UNEXPECTED;
        LOG_WARN("graph feedback seed key expression is null", K(ret), K(i));
      } else if (OB_FAIL(expr->eval(eval_ctx_, datum))) {
        LOG_WARN("failed to evaluate graph feedback seed key", K(ret), K(i));
      } else if (OB_ISNULL(datum) || datum->is_null()) {
        ret = OB_ERR_UNEXPECTED;
        LOG_WARN("graph feedback seed key is null", K(ret), K(i));
      } else if (OB_FAIL(datum->to_obj(key_values[i], expr->obj_meta_,
                                       expr->obj_datum_map_))) {
        LOG_WARN("failed to materialize graph feedback seed key", K(ret), K(i));
      }
    }
    if (OB_SUCC(ret)
        && OB_FAIL(binding_store_.add_binding(
               binding_exprs, eval_ctx_, binding_id))) {
      LOG_WARN("failed to store graph feedback seed binding", K(ret),
               "binding_count", binding_store_.count());
    } else if (OB_SUCC(ret)
               && OB_FAIL(frontier_.add_seed(binding_id, identity))) {
      ret = normalize_work_area_error(ret);
      LOG_WARN("failed to add graph feedback seed", K(ret),
               K(binding_id), K(identity));
    }
    if (OB_SUCCESS != ret) {
      reset();
    }
    return ret;
  }

  int normalize_work_area_error(int error) const
  {
    return error == OB_ALLOCATE_MEMORY_FAILED
        && binding_store_.memory_limit_exceeded()
        ? OB_EXCEED_QUERY_MEM_LIMIT : error;
  }

private:
  // Declared first because GraphExpand and GraphFeedbackFrontier allocate
  // through the same hard-capped work-area allocator owned by this store.
  GraphBindingStore binding_store_{};
  ObEvalCtx &eval_ctx_;
  GraphExpandDasAccess access_;
  GraphExpand expand_;
  GraphFeedbackFrontier frontier_;
  int64_t memory_limit_{0};
  int64_t next_output_index_{0};
  bool seeds_loaded_{false};

  DISALLOW_COPY_AND_ASSIGN(GraphFeedbackRuntime);
};

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
                   ModulePageAllocator(allocator, "GraphFrontier")),
      path_buffer_(OB_MALLOC_NORMAL_BLOCK_SIZE,
                   ModulePageAllocator(allocator, "GraphPathResult"))
{
}

int GraphFeedbackFrontier::check_memory_limit()
{
  int ret = OB_SUCCESS;
  int64_t used = state_store_.used_memory();
  const int64_t buffer_a_bytes = frontier_buffer_a_.get_data_size();
  const int64_t buffer_b_bytes = frontier_buffer_b_.get_data_size();
  const int64_t input_bytes = input_batch_.get_data_size();
  const int64_t path_bytes = path_buffer_.get_data_size();
  const int64_t memory_parts[] = {
      buffer_a_bytes, buffer_b_bytes, input_bytes, path_bytes,
      expand_.used_memory()};

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

int GraphFeedbackFrontier::get_frontier_path(
    int64_t index,
    const ObIArray<GraphPathState> *&path)
{
  int ret = OB_SUCCESS;
  GraphPathState leaf;
  path = nullptr;
  if (OB_FAIL(get_frontier_state(index, leaf))) {
  } else if (OB_FAIL(state_store_.build_path(leaf.path_state_id_,
                                             path_buffer_))) {
    LOG_WARN("failed to reconstruct graph frontier path", K(ret), K(index),
             K(leaf));
  } else if (OB_FAIL(check_memory_limit())) {
    LOG_WARN("failed to account graph frontier path", K(ret), K(index),
             K(leaf));
  } else {
    path = &path_buffer_;
  }
  if (OB_SUCCESS != ret) {
    path_buffer_.reuse();
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
  // ModulePageAllocator may wrap the query arena, whose individual free() is
  // a no-op. Keep this reusable result block across rescans so reset does not
  // lose the only pointer to memory that remains charged to the query.
  path_buffer_.reuse();
  current_state_ids_ = &frontier_buffer_a_;
  next_state_ids_ = &frontier_buffer_b_;
  hop_ = 0;
  peak_memory_bytes_ = 0;
}

GraphFeedbackLoopSpec::GraphFeedbackLoopSpec(common::ObIAllocator &allocator,
                                             const ObPhyOperatorType type)
    : RecursivePumpSpec(allocator, type),
      seed_key_exprs_(allocator),
      source_scan_desc_(allocator),
      edge_scan_desc_(allocator),
      target_scan_desc_(allocator)
{
}

int GraphFeedbackLoopSpec::init_output_row_desc()
{
  return output_row_desc_.init(path_desc_, expand_access_desc_,
                               output_union_exprs_.count());
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
                  || !get_graph_spec().get_output_row_desc().is_valid(
                         get_graph_spec().get_path_desc(),
                         get_graph_spec().get_expand_access_desc())
                  || !get_graph_spec().has_valid_seed_key_exprs()
                  || !get_graph_spec().get_source_scan_desc().is_valid()
                  || !get_graph_spec().get_edge_scan_desc().is_valid()
                  || !get_graph_spec().get_target_scan_desc().is_valid())) {
    ret = OB_ERR_UNEXPECTED;
    LOG_WARN("invalid graph feedback physical descriptor", K(ret),
             K(get_graph_spec().get_path_desc()),
             K(get_graph_spec().get_expand_access_desc()),
             K(get_graph_spec().get_output_row_desc()));
  } else if (OB_FAIL(init_native_runtime())) {
    LOG_WARN("failed to initialize native graph feedback runtime", K(ret));
  } else if (OB_FAIL(RecursivePumpOp::inner_open())) {
    reset_native_runtime();
  }
  return ret;
}

int GraphFeedbackLoopOp::inner_close()
{
  reset_native_runtime();
  return RecursivePumpOp::inner_close();
}

int GraphFeedbackLoopOp::inner_rescan()
{
  reset_native_runtime();
  return RecursivePumpOp::inner_rescan();
}

int GraphFeedbackLoopOp::init_native_runtime()
{
  int ret = OB_SUCCESS;
  int64_t memory_limit = 0;
  if (OB_FAIL(ObSqlWorkareaUtil::get_workarea_size(
          ObSqlWorkAreaType::HASH_WORK_AREA, &ctx_, memory_limit))) {
    LOG_WARN("failed to get graph feedback work-area limit", K(ret));
  } else if (OB_UNLIKELY(memory_limit <= 0)) {
    ret = OB_ERR_UNEXPECTED;
    LOG_WARN("invalid graph feedback work-area limit", K(ret),
             K(memory_limit));
  } else if (native_runtime_ == nullptr
             && OB_ISNULL(native_runtime_ = OB_NEWx(
                    GraphFeedbackRuntime, &ctx_.get_allocator(),
                    ctx_, eval_ctx_, memory_limit))) {
    ret = OB_ALLOCATE_MEMORY_FAILED;
    LOG_WARN("failed to allocate native graph feedback runtime", K(ret),
             K(memory_limit));
  } else if (OB_FAIL(native_runtime_->init(get_graph_spec()))) {
    LOG_WARN("failed to bind native graph feedback runtime", K(ret),
             K(memory_limit));
  }
  if (OB_SUCCESS != ret) {
    destroy_native_runtime();
  }
  return ret;
}

void GraphFeedbackLoopOp::reset_native_runtime()
{
  if (native_runtime_ != nullptr) {
    native_runtime_->reset();
  }
}

void GraphFeedbackLoopOp::destroy_native_runtime()
{
  if (native_runtime_ != nullptr) {
    native_runtime_->reset();
    native_runtime_->~GraphFeedbackRuntime();
    native_runtime_ = nullptr;
  }
}

void GraphFeedbackLoopOp::destroy()
{
  destroy_native_runtime();
  RecursivePumpOp::destroy();
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
                    output_row_desc_,
                    seed_key_exprs_,
                    source_scan_desc_,
                    edge_scan_desc_,
                    target_scan_desc_);

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
