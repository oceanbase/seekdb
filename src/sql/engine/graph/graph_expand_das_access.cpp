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
#include "sql/engine/graph/graph_expand_das_access.h"

#include "common/ob_range.h"
#include "sql/das/ob_das_ref.h"
#include "sql/das/ob_das_scan_op.h"
#include "sql/engine/ob_exec_context.h"
#include "sql/engine/ob_physical_plan_ctx.h"
#include "sql/engine/table/ob_table_scan_op.h"
#include "sql/session/ob_sql_session_info.h"

namespace oceanbase
{
using namespace common;
namespace sql
{

namespace
{

const ObDASScanCtDef *find_base_table_scan_ctdef(
    const ObTableScanSpec &scan_spec, uint64_t table_id)
{
  const ObDASScanCtDef *scan_ctdef = nullptr;
  if (scan_spec.tsc_ctdef_.scan_ctdef_.ref_table_id_ == table_id) {
    scan_ctdef = &scan_spec.tsc_ctdef_.scan_ctdef_;
  } else if (scan_spec.tsc_ctdef_.lookup_ctdef_ != nullptr
             && scan_spec.tsc_ctdef_.lookup_ctdef_->ref_table_id_ == table_id) {
    scan_ctdef = scan_spec.tsc_ctdef_.lookup_ctdef_;
  }
  return scan_ctdef;
}

int close_das_tasks(ObDASRef &das_ref, int ret)
{
  const int close_ret = das_ref.close_all_task();
  if (OB_SUCCESS != close_ret) {
    LOG_WARN("failed to close graph vertex lookup DAS tasks", K(close_ret),
             K(ret));
    if (OB_SUCCESS == ret) {
      ret = close_ret;
    }
  }
  return ret;
}

} // namespace

GraphExpandDasAccess::GraphExpandDasAccess(ObExecContext &exec_ctx,
                                           ObEvalCtx &eval_ctx)
    : exec_ctx_(exec_ctx),
      eval_ctx_(eval_ctx)
{
}

bool GraphExpandDasAccess::VertexLookupBinding::is_valid() const
{
  bool valid = element_id_ != OB_INVALID_ID
      && table_id_ != OB_INVALID_ID
      && key_count_ > 0
      && key_count_ <= GRAPH_IDENTITY_MAX_KEYS
      && scan_spec_ != nullptr
      && scan_ctdef_ != nullptr
      && scan_ctdef_->ref_table_id_ == table_id_;
  for (int64_t i = 0; valid && i < key_count_; ++i) {
    valid = key_columns_[i] != OB_INVALID_ID && key_exprs_[i] != nullptr;
  }
  return valid;
}

int GraphExpandDasAccess::init(const GraphPathDesc &path_desc,
                               const GraphExpandAccessDesc &access_desc,
                               const ObTableScanSpec &source_scan,
                               const ObTableScanSpec &edge_scan,
                               const ObTableScanSpec &target_scan)
{
  int ret = OB_SUCCESS;
  initialized_ = false;
  path_desc_ = GraphPathDesc();
  access_desc_ = GraphExpandAccessDesc();
  source_binding_ = VertexLookupBinding();
  target_binding_ = VertexLookupBinding();
  edge_scan_spec_ = nullptr;
  if (OB_UNLIKELY(!path_desc.is_valid() || !access_desc.is_valid())) {
    ret = OB_INVALID_ARGUMENT;
    LOG_WARN("invalid graph DAS access descriptor", K(ret), K(path_desc),
             K(access_desc));
  } else if (OB_UNLIKELY(source_scan.ref_table_id_
                             != access_desc.source_table_id_
                         || edge_scan.ref_table_id_
                             != access_desc.edge_table_id_
                         || edge_scan.tsc_ctdef_.scan_ctdef_.ref_table_id_
                             != access_desc.edge_access_table_id_
                         || target_scan.ref_table_id_
                             != access_desc.target_table_id_)) {
    ret = OB_ERR_UNEXPECTED;
    LOG_WARN("graph DAS scans do not match their access descriptor", K(ret),
             K(access_desc), K(source_scan.ref_table_id_),
             K(edge_scan.ref_table_id_),
             K(edge_scan.tsc_ctdef_.scan_ctdef_.ref_table_id_),
             K(target_scan.ref_table_id_));
  } else if (OB_FAIL(init_vertex_binding(
                 path_desc.source_element_id_, access_desc.source_table_id_,
                 access_desc.source_key_count_, access_desc.source_key_columns_,
                 source_scan, source_binding_))) {
    LOG_WARN("failed to bind graph source vertex lookup", K(ret),
             K(path_desc), K(access_desc));
  } else if (OB_FAIL(init_vertex_binding(
                 path_desc.target_element_id_, access_desc.target_table_id_,
                 access_desc.target_key_count_, access_desc.target_key_columns_,
                 target_scan, target_binding_))) {
    LOG_WARN("failed to bind graph target vertex lookup", K(ret),
             K(path_desc), K(access_desc));
  } else {
    path_desc_ = path_desc;
    access_desc_ = access_desc;
    edge_scan_spec_ = &edge_scan;
    initialized_ = true;
  }
  return ret;
}

int GraphExpandDasAccess::init_vertex_binding(
    uint64_t element_id,
    uint64_t table_id,
    int64_t key_count,
    const uint64_t *key_columns,
    const ObTableScanSpec &scan_spec,
    VertexLookupBinding &binding)
{
  int ret = OB_SUCCESS;
  const ObDASScanCtDef *scan_ctdef = find_base_table_scan_ctdef(scan_spec,
                                                               table_id);
  VertexLookupBinding candidate;
  if (OB_UNLIKELY(element_id == OB_INVALID_ID || table_id == OB_INVALID_ID
                  || key_count <= 0 || key_count > GRAPH_IDENTITY_MAX_KEYS
                  || key_columns == nullptr)) {
    ret = OB_INVALID_ARGUMENT;
  } else if (OB_ISNULL(scan_ctdef)) {
    // A covering secondary-index scan cannot accept a base-table primary-key
    // range. Keep the legacy recursive plan available instead of silently
    // probing the wrong key layout.
    ret = OB_NOT_SUPPORTED;
    LOG_WARN("graph vertex scan has no base-table DAS ctdef", K(ret),
             K(table_id), K(scan_spec.ref_table_id_),
             "access_table_id",
             scan_spec.tsc_ctdef_.scan_ctdef_.ref_table_id_);
  } else if (OB_UNLIKELY(scan_ctdef->access_column_ids_.count()
                         != scan_ctdef->pd_expr_spec_.access_exprs_.count())) {
    ret = OB_ERR_UNEXPECTED;
    LOG_WARN("graph vertex DAS access columns and expressions do not match",
             K(ret), K(scan_ctdef->access_column_ids_.count()),
             K(scan_ctdef->pd_expr_spec_.access_exprs_.count()));
  } else {
    candidate.element_id_ = element_id;
    candidate.table_id_ = table_id;
    candidate.key_count_ = key_count;
    candidate.scan_spec_ = &scan_spec;
    candidate.scan_ctdef_ = scan_ctdef;
    for (int64_t key = 0; OB_SUCC(ret) && key < key_count; ++key) {
      candidate.key_columns_[key] = key_columns[key];
      for (int64_t column = 0;
           candidate.key_exprs_[key] == nullptr
               && column < scan_ctdef->access_column_ids_.count();
           ++column) {
        if (scan_ctdef->access_column_ids_.at(column) == key_columns[key]) {
          candidate.key_exprs_[key]
              = scan_ctdef->pd_expr_spec_.access_exprs_.at(column);
        }
      }
      if (OB_ISNULL(candidate.key_exprs_[key])) {
        ret = OB_ERR_UNEXPECTED;
        LOG_WARN("graph vertex key is absent from the DAS scan output", K(ret),
                 K(element_id), K(table_id), K(key), K(key_columns[key]));
      }
    }
  }
  if (OB_SUCC(ret)) {
    if (OB_UNLIKELY(!candidate.is_valid())) {
      ret = OB_ERR_UNEXPECTED;
      LOG_WARN("generated graph vertex lookup binding is invalid", K(ret),
               K(element_id), K(table_id), K(key_count));
    } else {
      binding = candidate;
    }
  }
  return ret;
}

int GraphExpandDasAccess::check_status()
{
  return exec_ctx_.check_status();
}

const GraphExpandDasAccess::VertexLookupBinding *
GraphExpandDasAccess::find_vertex_binding(uint64_t element_id) const
{
  // Prefer the target binding when a homogeneous path maps both endpoints to
  // the same element. The target scan intentionally excludes the anchor-only
  // source predicate, which must not be reapplied at later hops.
  const VertexLookupBinding *binding = nullptr;
  if (target_binding_.element_id_ == element_id) {
    binding = &target_binding_;
  } else if (source_binding_.element_id_ == element_id) {
    binding = &source_binding_;
  }
  return binding;
}

int GraphExpandDasAccess::validate_requested(
    const ObIArray<GraphElementIdentity> &requested,
    const VertexLookupBinding *&binding) const
{
  int ret = OB_SUCCESS;
  binding = nullptr;
  if (!requested.empty()) {
    binding = find_vertex_binding(requested.at(0).element_id_);
    if (OB_ISNULL(binding) || !binding->is_valid()) {
      ret = OB_INVALID_ARGUMENT;
    }
  }
  for (int64_t i = 0; OB_SUCC(ret) && i < requested.count(); ++i) {
    const GraphElementIdentity &identity = requested.at(i);
    if (!identity.is_valid() || identity.graph_id_ != path_desc_.graph_id_
        || binding == nullptr || identity.element_id_ != binding->element_id_
        || identity.key_count_ != binding->key_count_) {
      ret = OB_INVALID_ARGUMENT;
    }
    for (int64_t key = 0; OB_SUCC(ret) && key < identity.key_count_; ++key) {
      if (identity.keys_[key].get_type() != ObIntType) {
        ret = OB_INVALID_ARGUMENT;
      }
    }
  }
  return ret;
}

int GraphExpandDasAccess::lookup_vertices(
    const ObIArray<GraphElementIdentity> &requested,
    ObIArray<GraphElementIdentity> &existing)
{
  int ret = OB_SUCCESS;
  const VertexLookupBinding *binding = nullptr;
  existing.reset();
  if (OB_UNLIKELY(!initialized_)) {
    ret = OB_NOT_INIT;
  } else if (OB_FAIL(check_status())) {
  } else if (OB_FAIL(validate_requested(requested, binding))) {
    LOG_WARN("invalid graph vertex lookup request", K(ret), K(requested));
  } else if (requested.empty()) {
  } else if (OB_FAIL(lookup_vertices(*binding, requested, existing))) {
    LOG_WARN("failed to look up graph vertices", K(ret), K(requested),
             KPC(binding));
  }
  if (OB_FAIL(ret)) {
    existing.reset();
  }
  return ret;
}

int GraphExpandDasAccess::init_scan_rtdef(
    const VertexLookupBinding &binding,
    ObIAllocator &scan_allocator,
    ObDASScanRtDef &scan_rtdef) const
{
  int ret = OB_SUCCESS;
  ObPhysicalPlanCtx *plan_ctx = exec_ctx_.get_physical_plan_ctx();
  ObSQLSessionInfo *session = exec_ctx_.get_my_session();
  const ObTableScanSpec *scan_spec = binding.scan_spec_;
  if (OB_ISNULL(plan_ctx) || OB_ISNULL(session) || OB_ISNULL(scan_spec)
      || OB_ISNULL(binding.scan_ctdef_)) {
    ret = OB_ERR_UNEXPECTED;
    LOG_WARN("graph vertex lookup runtime context is incomplete", K(ret),
             K(plan_ctx), K(session), K(scan_spec), K(binding.scan_ctdef_));
  } else {
    scan_rtdef.ctdef_ = binding.scan_ctdef_;
    scan_rtdef.timeout_ts_ = plan_ctx->get_ps_timeout_timestamp();
    scan_rtdef.tx_lock_timeout_ = session->get_trx_lock_timeout();
    scan_rtdef.scan_flag_ = scan_spec->tsc_ctdef_.scan_flags_;
    scan_rtdef.scan_flag_.scan_order_ = ObQueryFlag::NoOrder;
    scan_rtdef.scan_flag_.index_back_ = false;
    scan_rtdef.scan_flag_.is_need_feedback_ = false;
    scan_rtdef.need_check_output_datum_ = scan_spec->need_check_output_datum_;
    scan_rtdef.sql_mode_ = session->get_sql_mode();
    scan_rtdef.stmt_allocator_.set_alloc(&scan_allocator);
    scan_rtdef.scan_allocator_.set_alloc(&scan_allocator);
    scan_rtdef.eval_ctx_ = &eval_ctx_;
    scan_rtdef.frozen_version_ = scan_spec->frozen_version_;
    scan_rtdef.scan_op_id_ = scan_spec->get_id();
    scan_rtdef.scan_rows_size_ = scan_spec->rows_ * scan_spec->width_;
    scan_rtdef.runtime_schema_version_
        = exec_ctx_.get_sql_exec_ctx().get_query_begin_schema_version();
    if (OB_FAIL(scan_rtdef.init_pd_op(exec_ctx_, *binding.scan_ctdef_))) {
      LOG_WARN("failed to initialize graph vertex lookup pushdown", K(ret),
               KPC(binding.scan_ctdef_));
    } else if (OB_FAIL(scan_spec->tsc_ctdef_.snapshot_item_
                           .set_snapshot_query_info(eval_ctx_, scan_rtdef))) {
      LOG_WARN("failed to initialize graph vertex lookup snapshot", K(ret),
               KPC(scan_spec));
    } else {
      scan_rtdef.table_loc_ = DAS_CTX(exec_ctx_).get_table_loc_by_id(
          scan_spec->get_table_loc_id(), binding.scan_ctdef_->ref_table_id_);
      if (scan_rtdef.table_loc_ == nullptr
          && binding.scan_ctdef_ == scan_spec->tsc_ctdef_.lookup_ctdef_
          && scan_spec->tsc_ctdef_.lookup_loc_meta_ != nullptr) {
        ret = DAS_CTX(exec_ctx_).extended_table_loc(
            *scan_spec->tsc_ctdef_.lookup_loc_meta_, scan_rtdef.table_loc_);
      }
      if (OB_SUCC(ret)
          && (OB_ISNULL(scan_rtdef.table_loc_)
              || OB_ISNULL(scan_rtdef.table_loc_->loc_meta_))) {
        ret = OB_ERR_UNEXPECTED;
        LOG_WARN("graph vertex lookup table location metadata is missing", K(ret),
                 K(scan_spec->get_table_loc_id()),
                 K(binding.scan_ctdef_->ref_table_id_));
      } else if (OB_SUCC(ret)
                 && scan_rtdef.table_loc_->loc_meta_->is_weak_read_) {
        scan_rtdef.scan_flag_.set_is_select_follower();
      }
    }
  }
  return ret;
}

int GraphExpandDasAccess::lookup_vertices(
    const VertexLookupBinding &binding,
    const ObIArray<GraphElementIdentity> &requested,
    ObIArray<GraphElementIdentity> &existing)
{
  int ret = OB_SUCCESS;
  ObDASRef das_ref(eval_ctx_, exec_ctx_);
  ObDASScanRtDef scan_rtdef;
  ObArray<ObNewRange> ranges(
      OB_MALLOC_NORMAL_BLOCK_SIZE,
      ModulePageAllocator(das_ref.get_das_alloc(), "GraphVtxRange"));
  das_ref.set_mem_attr(ObMemAttr("GraphVtxLookup"));
  if (OB_FAIL(init_scan_rtdef(binding, das_ref.get_das_alloc(), scan_rtdef))) {
  }
  for (int64_t i = 0; OB_SUCC(ret) && i < requested.count(); ++i) {
    bool duplicate = false;
    for (int64_t j = 0; !duplicate && j < i; ++j) {
      duplicate = requested.at(i) == requested.at(j);
    }
    if (!duplicate) {
      ObObj *keys = static_cast<ObObj *>(
          das_ref.get_das_alloc().alloc(sizeof(ObObj) * binding.key_count_));
      ObNewRange range;
      if (OB_ISNULL(keys)) {
        ret = OB_ALLOCATE_MEMORY_FAILED;
      } else {
        new(keys) ObObj[binding.key_count_];
        for (int64_t key = 0; OB_SUCC(ret) && key < binding.key_count_; ++key) {
          ret = ob_write_obj(das_ref.get_das_alloc(),
                             requested.at(i).keys_[key], keys[key]);
        }
        if (OB_SUCC(ret)) {
          ObRowkey rowkey(keys, binding.key_count_);
          if (OB_FAIL(range.build_range(binding.table_id_, rowkey))) {
          } else if (OB_FAIL(ranges.push_back(range))) {
          }
        }
      }
    }
  }
  ObDASTableLoc *table_loc = scan_rtdef.table_loc_;
  if (OB_SUCC(ret) && (OB_ISNULL(table_loc)
                       || table_loc->get_tablet_locs().empty())) {
    ret = OB_ERR_UNEXPECTED;
    LOG_WARN("graph vertex lookup has no tablet location", K(ret),
             KPC(table_loc));
  }
  if (OB_SUCC(ret)) {
    // A full primary key contains every partition key. This first adapter
    // deliberately fans the exact multi-get ranges to the local tablet list;
    // storage can return a row from only its owning tablet. A later routing
    // optimization may prune ranges per tablet without changing this contract.
    for (DASTabletLocListIter node = table_loc->tablet_locs_begin();
         OB_SUCC(ret) && node != table_loc->tablet_locs_end(); ++node) {
      ObDASScanOp *scan_op = nullptr;
      if (OB_FAIL(das_ref.prepare_das_task(*node, scan_op))) {
      } else if (OB_ISNULL(scan_op)) {
        ret = OB_ERR_UNEXPECTED;
      } else {
        scan_op->set_scan_ctdef(binding.scan_ctdef_);
        scan_op->set_scan_rtdef(&scan_rtdef);
        scan_op->set_can_part_retry(false);
        for (int64_t i = 0; OB_SUCC(ret) && i < ranges.count(); ++i) {
          ret = scan_op->get_scan_param().key_ranges_.push_back(ranges.at(i));
        }
      }
    }
    table_loc->is_reading_ = true;
  }
  if (OB_SUCC(ret) && OB_FAIL(das_ref.execute_all_task())) {
    LOG_WARN("failed to execute graph vertex lookup DAS tasks", K(ret));
  }
  if (OB_SUCC(ret)) {
    DASOpResultIter result_iter = das_ref.begin_result_iter();
    bool finished = false;
    while (OB_SUCC(ret) && !finished) {
      if (OB_FAIL(check_status())) {
      } else if (OB_ISNULL(scan_rtdef.p_pd_expr_op_)) {
        ret = OB_ERR_UNEXPECTED;
        LOG_WARN("graph vertex lookup pushdown operator is missing", K(ret));
      } else {
        scan_rtdef.p_pd_expr_op_->clear_datum_eval_flag();
      }
      if (OB_SUCC(ret) && OB_FAIL(result_iter.get_next_row())) {
        if (ret == OB_ITER_END) {
          ret = result_iter.next_result();
          if (ret == OB_ITER_END) {
            ret = OB_SUCCESS;
            finished = true;
          }
        }
      } else if (OB_SUCC(ret)) {
        GraphElementIdentity identity;
        if (OB_FAIL(materialize_identity(binding, identity))) {
        } else {
          const int64_t requested_index = find_requested(requested, identity);
          if (requested_index < 0 || contains(existing, identity)) {
            ret = OB_ERR_UNEXPECTED;
            LOG_WARN("graph vertex lookup returned an unexpected row", K(ret),
                     K(identity), K(requested));
          } else if (OB_FAIL(existing.push_back(requested.at(requested_index)))) {
          }
        }
      }
    }
  }
  ret = close_das_tasks(das_ref, ret);
  return ret;
}

int GraphExpandDasAccess::materialize_identity(
    const VertexLookupBinding &binding,
    GraphElementIdentity &identity) const
{
  int ret = OB_SUCCESS;
  identity.graph_id_ = path_desc_.graph_id_;
  identity.element_id_ = binding.element_id_;
  identity.key_count_ = binding.key_count_;
  for (int64_t key = 0; OB_SUCC(ret) && key < binding.key_count_; ++key) {
    ObDatum *datum = nullptr;
    ObExpr *expr = binding.key_exprs_[key];
    if (OB_ISNULL(expr)) {
      ret = OB_ERR_UNEXPECTED;
    } else if (OB_FAIL(expr->eval(eval_ctx_, datum))) {
    } else if (OB_ISNULL(datum) || datum->is_null()) {
      ret = OB_ERR_UNEXPECTED;
    } else if (OB_FAIL(datum->to_obj(identity.keys_[key], expr->obj_meta_,
                                     expr->obj_datum_map_))) {
    }
  }
  return ret;
}

int64_t GraphExpandDasAccess::find_requested(
    const ObIArray<GraphElementIdentity> &requested,
    const GraphElementIdentity &identity) const
{
  int64_t index = -1;
  for (int64_t i = 0; index < 0 && i < requested.count(); ++i) {
    if (requested.at(i) == identity) {
      index = i;
    }
  }
  return index;
}

bool GraphExpandDasAccess::contains(
    const ObIArray<GraphElementIdentity> &identities,
    const GraphElementIdentity &identity) const
{
  bool found = false;
  for (int64_t i = 0; !found && i < identities.count(); ++i) {
    found = identities.at(i) == identity;
  }
  return found;
}

int GraphExpandDasAccess::scan_edges(
    const ObIArray<GraphElementIdentity> &sources,
    GraphPathDirection direction,
    const GraphElementIdentity *after_edge,
    int64_t limit,
    ObIArray<GraphExpandEdge> &edges,
    bool &end)
{
  UNUSEDx(sources, direction, after_edge, limit);
  edges.reset();
  end = false;
  return OB_NOT_SUPPORTED;
}

} // namespace sql
} // namespace oceanbase
