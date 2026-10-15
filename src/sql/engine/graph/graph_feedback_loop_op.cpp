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
#include "sql/engine/graph/graph_feedback_frontier.h"
#include "sql/engine/graph/graph_binding_store.h"
#include "sql/engine/graph/graph_expand_das_access.h"
#include "sql/engine/graph/graph_cached_access.h"
#include "sql/engine/ob_physical_plan.h"
#include "sql/engine/ob_sql_mem_mgr_processor.h"
#include "lib/utility/ob_macro_utils.h"
#include "share/ob_errno.h"
#include "sql/engine/table/ob_table_scan_op.h"
#include "sql/engine/expr/ob_expr_json_func_helper.h"
#include "share/ob_json_access_utils.h"

#include <algorithm>
#include <utility>

namespace oceanbase
{
using namespace common;
namespace sql
{


namespace
{

int64_t saturated_stat_add(int64_t left, int64_t right)
{
  return left < 0 || right < 0 || left > INT64_MAX - right
      ? INT64_MAX : left + right;
}

} // namespace

// Owns the native graph components as one allocation so their reference
// dependencies are constructed and destroyed in a fixed order. It remains
// query-local. Only proven parameter-independent adjacency entries survive
// rescans; no bindings, paths or state are shared with another execution.
class GraphFeedbackRuntime final
{
public:
  GraphFeedbackRuntime(ObExecContext &exec_ctx,
                       ObEvalCtx &eval_ctx,
                       int64_t memory_limit)
      : eval_ctx_(eval_ctx),
        access_(exec_ctx, eval_ctx,
                binding_store_.get_work_area_allocator()),
        cached_access_(binding_store_.get_work_area_allocator(), access_, memory_limit),
        expand_(binding_store_.get_work_area_allocator(), cached_access_,
                GRAPH_EXPAND_MAX_EDGE_PAGE_SIZE,
                memory_limit),
        frontier_(binding_store_.get_work_area_allocator(), expand_,
                  GRAPH_EXPAND_MAX_SOURCE_BATCH_SIZE, memory_limit),
        payload_allocator_(binding_store_.get_work_area_allocator()),
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
    if (OB_SUCC(ret)) {
      const ObPhysicalPlanCtx *plan_ctx = eval_ctx_.exec_ctx_.get_physical_plan_ctx();
      // Also check the complete physical statement: a SELECT subplan may
      // belong to DML or a statement with locking/side-effecting PL functions.
      cached_access_.set_enabled(spec.reuse_adjacency()
          && plan_ctx != nullptr && plan_ctx->get_phy_plan() != nullptr
          && plan_ctx->get_phy_plan()->is_plain_select()
          && spec.get_edge_scan_desc().get_tsc_ctdef().snapshot_item_.snapshot_query_type_
                 == TableItem::NOT_USING);
    }
    return ret;
  }

  // Called lazily on the first native output request after this runtime takes
  // exclusive ownership of the anchor child. Fallback execution never creates
  // this runtime, so one seed row cannot be stored by both implementations.
  int load_seeds(ObOperator &anchor, const GraphFeedbackLoopSpec &spec)
  {
    int ret = OB_SUCCESS;
    if (OB_UNLIKELY(seeds_loaded_)) {
      ret = OB_INIT_TWICE;
      LOG_WARN("graph feedback seeds are already loaded", K(ret),
               "binding_count", binding_store_.count());
    }
    // The enclosing nested loop has populated outer execution parameters by
    // this point. Do not evaluate their referring expressions or recursive
    // child parameters: only read the already-bound scalar values.
    ObArray<ObObj> parameters(OB_MALLOC_NORMAL_BLOCK_SIZE,
        ModulePageAllocator(binding_store_.get_work_area_allocator(), "GraphAdjCache"));
    for (int64_t i = 0; OB_SUCC(ret) && i < spec.get_adjacency_parameters().count(); ++i) {
      ObExpr *expr = spec.get_adjacency_parameters().at(i);
      ObDatum *datum = nullptr;
      ObObj value;
      if (OB_ISNULL(expr)) {
        ret = OB_ERR_UNEXPECTED;
      } else if (OB_FAIL(expr->eval(eval_ctx_, datum))) {
      } else if (OB_ISNULL(datum)) {
        ret = OB_ERR_UNEXPECTED;
      } else if (OB_FAIL(datum->to_obj(value, expr->obj_meta_, expr->obj_datum_map_))) {
      } else if (OB_FAIL(parameters.push_back(value))) {
      }
    }
    if (OB_SUCC(ret) && OB_FAIL(cached_access_.select_context(parameters))) {
      LOG_WARN("failed to select graph adjacency context", K(ret));
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
      ret = normalize_work_area_error(ret);
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

  GraphExpandStats get_cumulative_stats() const
  {
    GraphExpandStats stats = cumulative_stats_;
    if (frontier_.is_expanding()) {
      add_hop_stats(frontier_.get_pending_hop_stats(), stats);
    }
    return stats;
  }
  int64_t get_expanded_hop_count() const
  { return expanded_hop_count_ + (frontier_.is_expanding() ? 1 : 0); }

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
      } else if (!frontier_.is_expanding()
                 && (frontier_.empty()
                     || frontier_.get_hop() >= spec.get_upper_bound())) {
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

  // Produces a recursive WALK row with hop count, endpoint keys and optional
  // ordered edge arrays / typed history for per-step projection. TRAIL keeps
  // the recursive SQL fallback until its native eligibility is enabled.
  int get_next_basic_output_row(
      ObOperator &anchor,
      const GraphFeedbackLoopSpec &spec)
  {
    int ret = OB_SUCCESS;
    const ObIArray<GraphPathState> *path = nullptr;
    if (!seeds_loaded_ && OB_FAIL(load_seeds(anchor, spec))) {
      LOG_WARN("failed to load native graph feedback seeds", K(ret));
    } else if (OB_FAIL(get_next_output_path(
                   spec, anchor.get_spec().output_, path))) {
      if (ret != OB_ITER_END) {
        LOG_WARN("failed to get native graph feedback path", K(ret));
      }
    } else if (OB_FAIL(materialize_basic_output_row(spec, *path))) {
      LOG_WARN("failed to materialize basic graph feedback output", K(ret));
    }
    if (OB_SUCCESS != ret && OB_ITER_END != ret) {
      reset();
    }
    return ret;
  }

  // Starts/resumes one level and never crosses the SQL upper bound. A visible
  // batch can be consumed before the level completes; lower-bound levels are
  // still fully drained before any qualifying path is returned.
  int advance_frontier(const GraphFeedbackLoopSpec &spec)
  {
    int ret = OB_SUCCESS;
    // DAS batch iterators can change the shared evaluation batch position.
    // Restore the caller's scalar output slot even when the cursor stays open
    // at a yield; otherwise the first output can be written past slot zero.
    ObEvalCtx::BatchInfoScopeGuard batch_guard(eval_ctx_);
    const int64_t current_hop = frontier_.get_hop();
    if (OB_UNLIKELY(!seeds_loaded_)) {
      ret = OB_NOT_INIT;
      LOG_WARN("graph feedback seeds are not loaded", K(ret));
    } else if (!frontier_.is_expanding()
               && (frontier_.empty() || current_hop >= spec.get_upper_bound())) {
      ret = OB_ITER_END;
    } else if (OB_FAIL(frontier_.expand(spec.get_direction(),
                                        spec.get_path_mode(),
                                        spec.prune_duplicate_states()))) {
      ret = normalize_work_area_error(ret);
      if (ret != OB_ITER_END) {
        LOG_WARN("failed to advance graph feedback frontier", K(ret),
                 K(current_hop));
      }
    } else {
      if (!frontier_.is_expanding()) { accumulate_last_hop_stats(); }
      if (frontier_.empty() && !frontier_.is_expanding()) {
        ret = OB_ITER_END;
      } else if (frontier_.get_hop() != current_hop) {
        next_output_index_ = 0;
      }
    }
    if (OB_SUCCESS != ret && OB_ITER_END != ret) {
      reset();
    }
    return ret;
  }

  void reset(bool keep_adjacency = false)
  {
    if (frontier_.is_expanding()) {
      add_hop_stats(frontier_.get_pending_hop_stats(), cumulative_stats_);
      expanded_hop_count_ = saturated_stat_add(expanded_hop_count_, 1);
    }
    payload_allocator_.reset();
    frontier_.reset();
    if (!keep_adjacency) { cached_access_.clear(); }
    binding_store_.reset();
    next_output_index_ = 0;
    seeds_loaded_ = false;
    // Operator monitor counters are cumulative across rescans, just like its
    // output-row and rescan counters, so retain cumulative_stats_ here.
  }

private:
  int write_output_value(ObExpr *expr, const ObObj &value)
  {
    int ret = OB_SUCCESS;
    if (OB_ISNULL(expr)) {
      ret = OB_ERR_UNEXPECTED;
      LOG_WARN("graph feedback output expression is null", K(ret));
    } else {
      ObDatum &datum = expr->locate_datum_for_write(eval_ctx_);
      if (OB_FAIL(datum.from_obj(value, expr->obj_datum_map_))) {
        LOG_WARN("failed to write graph feedback output value", K(ret),
                 K(value), KPC(expr));
      } else {
        expr->set_evaluated_projected(eval_ctx_);
        ObEvalInfo &info = expr->get_eval_info(eval_ctx_);
        info.notnull_ = !datum.is_null();
        info.point_to_frame_ = false;
      }
    }
    return ret;
  }

  int write_output_key(const ExprFixedArray &output_exprs,
                       int64_t begin,
                       int64_t count,
                       const GraphElementIdentity &identity)
  {
    int ret = OB_SUCCESS;
    if (OB_UNLIKELY(begin < 0 || count <= 0
                    || begin > output_exprs.count() - count
                    || identity.rowkey_.get_obj_cnt() != count)) {
      ret = OB_ERR_UNEXPECTED;
      LOG_WARN("graph feedback output key shape is inconsistent", K(ret),
               K(begin), K(count), "output_count", output_exprs.count(),
               K(identity));
    }
    for (int64_t i = 0; OB_SUCC(ret) && i < count; ++i) {
      if (OB_FAIL(write_output_value(
              output_exprs.at(begin + i), identity.rowkey_.get_obj_ptr()[i]))) {
        LOG_WARN("failed to write graph feedback output key", K(ret), K(i),
                 K(begin), K(identity));
      }
    }
    return ret;
  }

  // Materializes fixed fields and ordered payload arrays from path-local state.
  // Shared adjacency reads never merge the histories of different paths.
  int materialize_basic_output_row(
      const GraphFeedbackLoopSpec &spec,
      const ObIArray<GraphPathState> &path)
  {
    int ret = OB_SUCCESS;
    ObObj hop_value;
    const GraphFeedbackRowDesc &desc = spec.get_output_row_desc();
    const ExprFixedArray &output_exprs = spec.output_union_exprs_;
    if (OB_UNLIKELY(path.empty()
                    || desc.output_expr_count_ != output_exprs.count())) {
      ret = OB_ERR_UNEXPECTED;
      LOG_WARN("invalid graph feedback output row", K(ret),
               "path_count", path.count(), K(desc),
               "output_count", output_exprs.count());
    } else if (OB_UNLIKELY(!spec.can_use_basic_native_runtime())) {
      ret = OB_NOT_SUPPORTED;
      LOG_WARN("graph feedback output row is outside basic native capability",
               K(ret), K(spec.get_path_desc()),
               K(spec.get_expand_access_desc()), K(desc));
    } else {
      const GraphPathState &root = path.at(0);
      const GraphPathState &leaf = path.at(path.count() - 1);
      hop_value.set_int(leaf.hop_);
      if (OB_FAIL(write_output_value(
              output_exprs.at(desc.depth_expr_index_), hop_value))) {
      } else if (OB_FAIL(write_output_key(
                     output_exprs, desc.source_key_expr_begin_,
                     desc.source_key_expr_count_, root.current_identity_))) {
      } else if (OB_FAIL(write_output_key(
                     output_exprs, desc.current_key_expr_begin_,
                     desc.current_key_expr_count_, leaf.current_identity_))) {
      } else {
        const int64_t key_count = spec.get_expand_access_desc().edge_key_count_;
        for (int64_t slot = 0; OB_SUCC(ret)
             && slot * key_count < desc.trail_key_expr_count_; ++slot) {
          const int64_t begin = desc.trail_key_expr_begin_ + slot * key_count;
          if (slot + 1 < path.count()) {
            ret = write_output_key(output_exprs, begin, key_count,
                                   path.at(slot + 1).incoming_edge_identity_);
          } else {
            ObObj unused;
            unused.set_null();
            for (int64_t key = 0; OB_SUCC(ret) && key < key_count; ++key) {
              ret = write_output_value(output_exprs.at(begin + key), unused);
            }
          }
        }
        if (OB_SUCC(ret)) {
          ret = materialize_path_payloads(spec, path);
        }
      }
    }
    return ret;
  }

  int materialize_path_payloads(const GraphFeedbackLoopSpec &spec,
                                const ObIArray<GraphPathState> &path)
  {
    int ret = OB_SUCCESS;
    payload_allocator_.reuse();
    const GraphFeedbackRowDesc &desc = spec.get_output_row_desc();
    for (int64_t column = 0; OB_SUCC(ret) && column < desc.payload_expr_count_; ++column) {
      ObJsonArray array(&payload_allocator_);
      // The root has no incoming edge. Walking root-to-leaf preserves order
      // and repeated edges; SQL's existing zero-hop wrapper handles [] output.
      for (int64_t hop = 1; OB_SUCC(ret) && hop < path.count(); ++hop) {
        const ObNewRow &row = path.at(hop).incoming_edge_payload_.values_;
        ObIJsonBase *value = nullptr;
        if (OB_UNLIKELY(row.count_ != desc.payload_expr_count_ || row.cells_ == nullptr)) {
          ret = OB_ERR_UNEXPECTED;
        } else if (OB_FAIL(ObJsonBaseFactory::get_json_base(
                       &payload_allocator_, row.cells_[column].get_string(),
                       ObJsonInType::JSON_BIN, ObJsonInType::JSON_TREE, value))) {
        } else if (OB_FAIL(array.array_append(value))) {
        }
      }
      ObString binary;
      ObExpr *output = spec.output_union_exprs_.at(desc.payload_expr_begin_ + column);
      if (OB_SUCC(ret)) {
        if (OB_ISNULL(output)) {
          ret = OB_ERR_UNEXPECTED;
        } else if (ObJsonExprHelper::is_json_depth_exceed_limit(array.depth())) {
          ret = OB_ERR_JSON_OUT_OF_DEPTH;
        } else if (OB_FAIL(share::ObJsonWrapper::get_raw_binary(&array, binary, &payload_allocator_))) {
        } else if (OB_FAIL(ObJsonExprHelper::pack_json_str_res(
                       *output, eval_ctx_, output->locate_datum_for_write(eval_ctx_), binary))) {
        } else {
          output->set_evaluated_projected(eval_ctx_);
        }
      }
    }
    cumulative_stats_.peak_path_memory_ = std::max(
        cumulative_stats_.peak_path_memory_, binding_store_.used_memory());
    return normalize_work_area_error(ret);
  }

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

  void accumulate_last_hop_stats()
  {
    add_hop_stats(frontier_.get_last_hop_stats(), cumulative_stats_);
    expanded_hop_count_ = saturated_stat_add(expanded_hop_count_, 1);
  }

  static void add_hop_stats(const GraphExpandStats &hop_stats, GraphExpandStats &total)
  {
    total.input_states_ = saturated_stat_add(total.input_states_, hop_stats.input_states_);
    total.distinct_sources_ = saturated_stat_add(total.distinct_sources_, hop_stats.distinct_sources_);
    total.distinct_sources_exact_ = total.distinct_sources_exact_
        && hop_stats.distinct_sources_exact_;
    total.scanned_edges_ = saturated_stat_add(total.scanned_edges_, hop_stats.scanned_edges_);
    total.looked_up_vertices_ = saturated_stat_add(total.looked_up_vertices_, hop_stats.looked_up_vertices_);
    total.orphan_edges_ = saturated_stat_add(total.orphan_edges_, hop_stats.orphan_edges_);
    total.output_states_ = saturated_stat_add(total.output_states_, hop_stats.output_states_);
    total.peak_path_memory_ = std::max(total.peak_path_memory_, hop_stats.peak_path_memory_);
  }

private:
  // Declared first because GraphExpand and GraphFeedbackFrontier allocate
  // through the same hard-capped work-area allocator owned by this store.
  GraphBindingStore binding_store_{};
  ObEvalCtx &eval_ctx_;
  GraphExpandDasAccess access_;
  GraphCachedAccess cached_access_;
  GraphExpand expand_;
  GraphFeedbackFrontier frontier_;
  common::ObArenaAllocator payload_allocator_;
  int64_t memory_limit_{0};
  int64_t next_output_index_{0};
  GraphExpandStats cumulative_stats_{};
  int64_t expanded_hop_count_{0};
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

bool identity_has_double_key(const GraphElementIdentity &identity)
{
  bool has_double = false;
  for (int64_t i = 0;
       !has_double && i < identity.rowkey_.get_obj_cnt(); ++i) {
    has_double = identity.rowkey_.get_obj_ptr()[i].get_type_class()
        == ObDoubleTC;
  }
  return has_double;
}

} // namespace

GraphFeedbackLoopSpec::GraphFeedbackLoopSpec(common::ObIAllocator &allocator,
                                             const ObPhyOperatorType type)
    : RecursivePumpSpec(allocator, type),
      seed_key_exprs_(allocator),
      adjacency_parameters_(allocator),
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

int GraphFeedbackLoopSpec::remove_native_edge_identity_filters(const ObIArray<ObExpr *> &filters)
{
  int ret = OB_SUCCESS;
  ObArray<ObExpr *> retained;
  for (int64_t i = 0; i < filters.count(); ++i) {
    bool above_das = false;
    for (int64_t j = 0; !above_das && j < edge_scan_desc_.filters_.count(); ++j) {
      above_das = filters.at(i) == edge_scan_desc_.filters_.at(j);
    }
    if (!above_das) {
      // Only batch-scan dynamic predicates are guaranteed to stay above DAS.
      // Do not reuse a storage ctdef that may still evaluate a recursive param.
      set_native_step_supported(false);
      set_reuse_adjacency(false);
    }
  }
  for (int64_t i = 0; OB_SUCC(ret) && i < edge_scan_desc_.filters_.count(); ++i) {
    ObExpr *expr = edge_scan_desc_.filters_.at(i);
    bool identity_filter = false;
    for (int64_t j = 0; !identity_filter && j < filters.count(); ++j) {
      identity_filter = expr == filters.at(j);
    }
    if (!identity_filter) {
      ret = retained.push_back(expr);
    }
  }
  // Do not modify the original table-scan spec/ctdef: TRAIL and other fallback
  // shapes still execute that recursive child and require its join predicates.
  if (OB_SUCC(ret)) {
    ret = edge_scan_desc_.filters_.assign(retained);
  }
  return ret;
}

int GraphFeedbackLoopOp::inner_open()
{
  int ret = OB_SUCCESS;
  use_native_runtime_ = false;
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
  } else if (FALSE_IT(use_native_runtime_
                          = get_graph_spec().can_use_basic_native_runtime())) {
  } else if (use_native_runtime_ && OB_FAIL(init_native_runtime())) {
    LOG_WARN("failed to initialize native graph feedback runtime", K(ret));
  } else if (!use_native_runtime_
             && OB_FAIL(RecursivePumpOp::inner_open())) {
    LOG_WARN("failed to initialize recursive graph feedback runtime", K(ret));
  }
  if (OB_SUCC(ret) && use_native_runtime_) {
    init_native_monitor_info();
  }
  return ret;
}

int GraphFeedbackLoopOp::inner_close()
{
  update_native_monitor_info();
  reset_native_runtime();
  return use_native_runtime_ ? OB_SUCCESS : RecursivePumpOp::inner_close();
}

int GraphFeedbackLoopOp::get_next_native_row()
{
  int ret = OB_SUCCESS;
  clear_evaluated_flag();
  if (OB_UNLIKELY(!use_native_runtime_)) {
    ret = OB_ERR_UNEXPECTED;
    LOG_WARN("native graph feedback runtime is not selected", K(ret));
  } else if (OB_ISNULL(native_runtime_) || OB_ISNULL(left_)) {
    ret = OB_ERR_UNEXPECTED;
    LOG_WARN("native graph feedback runtime is incomplete", K(ret),
             K_(native_runtime), K(left_));
  } else if (OB_FAIL(try_check_status())) {
  } else {
    ret = native_runtime_->get_next_basic_output_row(*left_,
                                                     get_graph_spec());
    update_native_monitor_info();
    if (OB_SUCCESS != ret && ret != OB_ITER_END) {
      LOG_WARN("failed to produce native graph feedback row", K(ret));
    }
  }
  if (OB_SUCCESS != ret && ret != OB_ITER_END) {
    // try_check_status() can fail before control reaches GraphFeedbackRuntime.
    // Release any open DAS scan and frontier state on every terminal error,
    // rather than relying solely on the executor's later close/destroy pass.
    reset_native_runtime();
  }
  return ret;
}

int GraphFeedbackLoopOp::inner_get_next_row()
{
  return use_native_runtime_
      ? get_next_native_row()
      : RecursivePumpOp::inner_get_next_row();
}

int GraphFeedbackLoopOp::inner_get_next_batch(int64_t max_row_cnt)
{
  int ret = OB_SUCCESS;
  if (!use_native_runtime_) {
    ret = RecursivePumpOp::inner_get_next_batch(max_row_cnt);
  } else {
    ObEvalCtx::BatchInfoScopeGuard guard(eval_ctx_);
    guard.set_batch_size(1);
    guard.set_batch_idx(0);
    if (OB_FAIL(get_next_native_row())) {
      if (ret == OB_ITER_END) {
        ret = OB_SUCCESS;
        brs_.size_ = 0;
        brs_.end_ = true;
      }
    } else {
      brs_.skip_->reset(1);
      brs_.size_ = 1;
      brs_.end_ = false;
      brs_.all_rows_active_ = true;
    }
  }
  return ret;
}

int GraphFeedbackLoopOp::inner_rescan()
{
  update_native_monitor_info();
  reset_native_runtime(true);
  return use_native_runtime_
      ? ObOperator::inner_rescan()
      : RecursivePumpOp::inner_rescan();
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

// TODO: Revisit these monitor slots after settling the SeekDB diagnostics interface.
// These slots currently have no virtual-table output (frontier statistics have
// separate TRACE logs); remove this plumbing if no supported consumer needs it.
void GraphFeedbackLoopOp::init_native_monitor_info()
{
  op_monitor_info_.otherstat_1_id_ =
      ObSqlMonitorStatIds::GRAPH_EXPANDED_HOP_COUNT;
  op_monitor_info_.otherstat_2_id_ =
      ObSqlMonitorStatIds::GRAPH_INPUT_STATE_COUNT;
  op_monitor_info_.otherstat_3_id_ =
      ObSqlMonitorStatIds::GRAPH_DISTINCT_SOURCE_COUNT;
  op_monitor_info_.otherstat_4_id_ =
      ObSqlMonitorStatIds::GRAPH_DISTINCT_SOURCE_EXACT;
  op_monitor_info_.otherstat_5_id_ =
      ObSqlMonitorStatIds::GRAPH_SCANNED_EDGE_COUNT;
  op_monitor_info_.otherstat_6_id_ =
      ObSqlMonitorStatIds::GRAPH_LOOKED_UP_VERTEX_COUNT;
  op_monitor_info_.otherstat_7_id_ =
      ObSqlMonitorStatIds::GRAPH_ORPHAN_EDGE_COUNT;
  op_monitor_info_.otherstat_8_id_ =
      ObSqlMonitorStatIds::GRAPH_OUTPUT_PATH_COUNT;
  op_monitor_info_.otherstat_9_id_ =
      ObSqlMonitorStatIds::GRAPH_PEAK_PATH_MEMORY;
  op_monitor_info_.otherstat_1_value_ = 0;
  op_monitor_info_.otherstat_2_value_ = 0;
  op_monitor_info_.otherstat_3_value_ = 0;
  op_monitor_info_.otherstat_4_value_ = 1;
  op_monitor_info_.otherstat_5_value_ = 0;
  op_monitor_info_.otherstat_6_value_ = 0;
  op_monitor_info_.otherstat_7_value_ = 0;
  op_monitor_info_.otherstat_8_value_ = 0;
  op_monitor_info_.otherstat_9_value_ = 0;
}

void GraphFeedbackLoopOp::update_native_monitor_info()
{
  if (use_native_runtime_ && native_runtime_ != nullptr) {
    const GraphExpandStats &stats = native_runtime_->get_cumulative_stats();
    const int64_t expanded_hops = native_runtime_->get_expanded_hop_count();
    // A consumer may stop during a hop. Include the live cursor's counters,
    // not only completed levels, without accumulating a batch twice.
    {
      op_monitor_info_.otherstat_1_value_ = expanded_hops;
      op_monitor_info_.otherstat_2_value_ = stats.input_states_;
      op_monitor_info_.otherstat_3_value_ = stats.distinct_sources_;
      op_monitor_info_.otherstat_4_value_ =
          stats.distinct_sources_exact_ ? 1 : 0;
      op_monitor_info_.otherstat_5_value_ = stats.scanned_edges_;
      op_monitor_info_.otherstat_6_value_ = stats.looked_up_vertices_;
      op_monitor_info_.otherstat_7_value_ = stats.orphan_edges_;
      op_monitor_info_.otherstat_8_value_ = stats.output_states_;
      op_monitor_info_.otherstat_9_value_ = stats.peak_path_memory_;
    }
  }
}

void GraphFeedbackLoopOp::reset_native_runtime(bool keep_adjacency)
{
  if (native_runtime_ != nullptr) {
    native_runtime_->reset(keep_adjacency);
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
                    path_desc_.any_shortest_,
                    path_desc_.shortest_single_segment_,
                    expand_access_desc_,
                    output_row_desc_,
                    native_step_supported_,
                    prune_duplicate_states_,
                    reuse_adjacency_,
                    seed_key_exprs_,
                    adjacency_parameters_,
                    source_scan_desc_,
                    edge_scan_desc_,
                    target_scan_desc_);


} // namespace sql
} // namespace oceanbase
