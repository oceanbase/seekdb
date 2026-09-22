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
#include "sql/das/ob_das_utils.h"
#include "sql/engine/ob_exec_context.h"
#include "sql/engine/ob_operator.h"
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
    LOG_WARN("failed to close graph DAS tasks", K(close_ret), K(ret));
    if (OB_SUCCESS == ret) {
      ret = close_ret;
    }
  }
  return ret;
}

bool identity_matches(const GraphElementIdentity &identity,
                      uint64_t graph_id,
                      uint64_t element_id,
                      int64_t key_count)
{
  bool matches = identity.is_valid()
      && identity.graph_id_ == graph_id
      && identity.element_id_ == element_id
      && identity.key_count_ == key_count;
  for (int64_t key = 0; matches && key < key_count; ++key) {
    matches = identity.keys_[key].get_type() == ObIntType;
  }
  return matches;
}

int compare_identities(const GraphElementIdentity &left,
                       const GraphElementIdentity &right,
                       int &comparison)
{
  int ret = OB_SUCCESS;
  comparison = 0;
  if (OB_UNLIKELY(!left.is_valid() || !right.is_valid()
                  || left.graph_id_ != right.graph_id_
                  || left.element_id_ != right.element_id_
                  || left.key_count_ != right.key_count_)) {
    ret = OB_INVALID_ARGUMENT;
  }
  for (int64_t key = 0; OB_SUCC(ret) && comparison == 0
       && key < left.key_count_; ++key) {
    if (OB_UNLIKELY(left.keys_[key].get_type() != ObIntType
                    || right.keys_[key].get_type() != ObIntType)) {
      ret = OB_INVALID_ARGUMENT;
    } else if (left.keys_[key].get_int() < right.keys_[key].get_int()) {
      comparison = -1;
    } else if (left.keys_[key].get_int() > right.keys_[key].get_int()) {
      comparison = 1;
    }
  }
  return ret;
}

uint64_t identity_array_hash(const ObIArray<GraphElementIdentity> &identities)
{
  uint64_t hash = do_hash(identities.count(), 0);
  for (int64_t i = 0; i < identities.count(); ++i) {
    hash = identities.at(i).hash(hash);
  }
  return hash;
}

int evaluate_filters(ObEvalCtx &eval_ctx,
                     const ObIArray<ObExpr *> &filters,
                     bool &filtered)
{
  int ret = OB_SUCCESS;
  for (int64_t i = 0; OB_SUCC(ret) && i < filters.count(); ++i) {
    if (OB_ISNULL(filters.at(i))) {
      ret = OB_ERR_UNEXPECTED;
    } else {
      filters.at(i)->clear_evaluated_flag(eval_ctx);
    }
  }
  if (OB_SUCC(ret)) {
    ret = ObOperator::filter_row(eval_ctx, filters, filtered);
  }
  return ret;
}

int bind_column_exprs(const ObDASScanCtDef &ctdef,
                      const uint64_t *column_ids,
                      int64_t column_count,
                      ObExpr **exprs)
{
  int ret = OB_SUCCESS;
  if (OB_UNLIKELY(column_ids == nullptr || exprs == nullptr
                  || column_count <= 0
                  || column_count > GRAPH_IDENTITY_MAX_KEYS)) {
    ret = OB_INVALID_ARGUMENT;
  } else if (OB_UNLIKELY(ctdef.access_column_ids_.count()
                         != ctdef.pd_expr_spec_.access_exprs_.count())) {
    ret = OB_ERR_UNEXPECTED;
    LOG_WARN("graph edge DAS access columns and expressions do not match",
             K(ret), K(ctdef.access_column_ids_.count()),
             K(ctdef.pd_expr_spec_.access_exprs_.count()));
  }
  for (int64_t key = 0; OB_SUCC(ret) && key < column_count; ++key) {
    for (int64_t column = 0;
         exprs[key] == nullptr && column < ctdef.access_column_ids_.count();
         ++column) {
      if (ctdef.access_column_ids_.at(column) == column_ids[key]) {
        exprs[key] = ctdef.pd_expr_spec_.access_exprs_.at(column);
      }
    }
    if (OB_ISNULL(exprs[key])) {
      ret = OB_ERR_UNEXPECTED;
      LOG_WARN("graph edge identity column is absent from DAS output", K(ret),
               K(key), K(column_ids[key]), K(ctdef.ref_table_id_));
    }
  }
  return ret;
}

} // namespace

GraphExpandDasAccess::GraphExpandDasAccess(ObExecContext &exec_ctx,
                                           ObEvalCtx &eval_ctx)
    : exec_ctx_(exec_ctx),
      eval_ctx_(eval_ctx),
      edge_das_ref_(eval_ctx, exec_ctx)
{
}

GraphExpandDasAccess::~GraphExpandDasAccess()
{
  release();
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

bool GraphExpandDasAccess::EdgeScanBinding::is_valid() const
{
  bool valid = edge_element_id_ != OB_INVALID_ID
      && edge_table_id_ != OB_INVALID_ID
      && access_table_id_ != OB_INVALID_ID
      && source_key_count_ > 0
      && source_key_count_ <= GRAPH_IDENTITY_MAX_KEYS
      && edge_key_count_ > 0
      && edge_key_count_ <= GRAPH_IDENTITY_MAX_KEYS
      && target_key_count_ > 0
      && target_key_count_ <= GRAPH_IDENTITY_MAX_KEYS
      && scan_spec_ != nullptr
      && access_ctdef_ != nullptr
      && output_ctdef_ != nullptr
      && scan_spec_->ref_table_id_ == edge_table_id_
      && access_ctdef_->ref_table_id_ == access_table_id_
      && (output_ctdef_ == access_ctdef_
          || output_ctdef_->ref_table_id_ == edge_table_id_);
  for (int64_t i = 0; valid && i < source_key_count_; ++i) {
    valid = source_columns_[i] != OB_INVALID_ID && source_exprs_[i] != nullptr;
  }
  for (int64_t i = 0; valid && i < edge_key_count_; ++i) {
    valid = edge_columns_[i] != OB_INVALID_ID && edge_exprs_[i] != nullptr;
  }
  for (int64_t i = 0; valid && i < target_key_count_; ++i) {
    valid = target_columns_[i] != OB_INVALID_ID && target_exprs_[i] != nullptr;
  }
  return valid;
}

int GraphExpandDasAccess::init(const GraphPathDesc &path_desc,
                               const GraphExpandAccessDesc &access_desc,
                               const ObTableScanSpec &source_scan,
                               const ObTableScanSpec &edge_scan,
                               const ObTableScanSpec &target_scan)
{
  int ret = reset_edge_scan(OB_SUCCESS);
  initialized_ = false;
  path_desc_ = GraphPathDesc();
  access_desc_ = GraphExpandAccessDesc();
  source_binding_ = VertexLookupBinding();
  target_binding_ = VertexLookupBinding();
  edge_binding_ = EdgeScanBinding();
  if (OB_SUCCESS != ret) {
    LOG_WARN("failed to release the previous graph edge scan", K(ret));
  } else if (OB_UNLIKELY(!path_desc.is_valid() || !access_desc.is_valid())) {
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
  } else if (OB_FAIL(init_edge_binding(path_desc, access_desc, edge_scan,
                                       edge_binding_))) {
    LOG_WARN("failed to bind graph edge scan", K(ret), K(path_desc),
             K(access_desc));
  } else {
    path_desc_ = path_desc;
    access_desc_ = access_desc;
    initialized_ = true;
  }
  return ret;
}

void GraphExpandDasAccess::release()
{
  const int ret = reset_edge_scan(OB_SUCCESS);
  if (OB_SUCCESS != ret) {
    LOG_WARN("failed to release graph edge scan", K(ret));
  }
}

int GraphExpandDasAccess::reset_edge_scan(int ret)
{
  if (edge_das_ref_.has_task()) {
    ret = close_das_tasks(edge_das_ref_, ret);
  }
  if (edge_lookup_rtdef_ != nullptr) {
    edge_lookup_rtdef_->~ObDASScanRtDef();
    edge_lookup_rtdef_ = nullptr;
  }
  if (edge_scan_rtdef_ != nullptr) {
    edge_scan_rtdef_->~ObDASScanRtDef();
    edge_scan_rtdef_ = nullptr;
  }
  edge_das_ref_.reuse();
  edge_result_iter_ = DASOpResultIter();
  edge_cursor_.reset();
  edge_sources_hash_ = 0;
  has_edge_cursor_ = false;
  edge_scan_active_ = false;
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

int GraphExpandDasAccess::init_edge_binding(
    const GraphPathDesc &path_desc,
    const GraphExpandAccessDesc &access_desc,
    const ObTableScanSpec &scan_spec,
    EdgeScanBinding &binding)
{
  int ret = OB_SUCCESS;
  EdgeScanBinding candidate;
  const ObDASScanCtDef *access_ctdef = &scan_spec.tsc_ctdef_.scan_ctdef_;
  const ObDASScanCtDef *output_ctdef = scan_spec.tsc_ctdef_.lookup_ctdef_ == nullptr
      ? access_ctdef : scan_spec.tsc_ctdef_.lookup_ctdef_;
  if (OB_UNLIKELY(!path_desc.is_valid() || !access_desc.is_valid()
                  || scan_spec.ref_table_id_ != access_desc.edge_table_id_
                  || access_ctdef->ref_table_id_
                         != access_desc.edge_access_table_id_)) {
    ret = OB_INVALID_ARGUMENT;
  } else if (OB_ISNULL(output_ctdef)) {
    ret = OB_ERR_UNEXPECTED;
  } else {
    candidate.edge_element_id_ = path_desc.edge_element_id_;
    candidate.edge_table_id_ = access_desc.edge_table_id_;
    candidate.access_table_id_ = access_desc.edge_access_table_id_;
    candidate.source_key_count_ = access_desc.source_key_count_;
    candidate.edge_key_count_ = access_desc.edge_key_count_;
    candidate.target_key_count_ = access_desc.target_key_count_;
    candidate.scan_spec_ = &scan_spec;
    candidate.access_ctdef_ = access_ctdef;
    candidate.output_ctdef_ = output_ctdef;
    for (int64_t i = 0; i < candidate.source_key_count_; ++i) {
      candidate.source_columns_[i] = access_desc.edge_current_columns_[i];
    }
    for (int64_t i = 0; i < candidate.edge_key_count_; ++i) {
      candidate.edge_columns_[i] = access_desc.edge_key_columns_[i];
    }
    for (int64_t i = 0; i < candidate.target_key_count_; ++i) {
      candidate.target_columns_[i] = access_desc.edge_next_columns_[i];
    }
    if (OB_FAIL(bind_column_exprs(*output_ctdef, candidate.source_columns_,
                                  candidate.source_key_count_,
                                  candidate.source_exprs_))) {
    } else if (OB_FAIL(bind_column_exprs(*output_ctdef,
                                         candidate.edge_columns_,
                                         candidate.edge_key_count_,
                                         candidate.edge_exprs_))) {
    } else if (OB_FAIL(bind_column_exprs(*output_ctdef,
                                         candidate.target_columns_,
                                         candidate.target_key_count_,
                                         candidate.target_exprs_))) {
    }
  }
  if (OB_SUCC(ret)) {
    if (OB_UNLIKELY(!candidate.is_valid())) {
      ret = OB_ERR_UNEXPECTED;
      LOG_WARN("generated graph edge scan binding is invalid", K(ret),
               K(candidate));
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
    const ObTableScanSpec &scan_spec,
    const ObDASScanCtDef &scan_ctdef,
    bool uses_index_back,
    ObIAllocator &scan_allocator,
    ObDASScanRtDef &scan_rtdef) const
{
  int ret = OB_SUCCESS;
  ObPhysicalPlanCtx *plan_ctx = exec_ctx_.get_physical_plan_ctx();
  ObSQLSessionInfo *session = exec_ctx_.get_my_session();
  if (OB_ISNULL(plan_ctx) || OB_ISNULL(session)) {
    ret = OB_ERR_UNEXPECTED;
    LOG_WARN("graph DAS scan runtime context is incomplete", K(ret),
             K(plan_ctx), K(session));
  } else {
    const bool is_lookup = &scan_ctdef == scan_spec.tsc_ctdef_.lookup_ctdef_;
    scan_rtdef.ctdef_ = &scan_ctdef;
    scan_rtdef.timeout_ts_ = plan_ctx->get_ps_timeout_timestamp();
    scan_rtdef.tx_lock_timeout_ = session->get_trx_lock_timeout();
    scan_rtdef.scan_flag_ = scan_spec.tsc_ctdef_.scan_flags_;
    scan_rtdef.scan_flag_.scan_order_ = uses_index_back && is_lookup
        ? ObQueryFlag::KeepOrder : ObQueryFlag::NoOrder;
    scan_rtdef.scan_flag_.index_back_ = uses_index_back;
    scan_rtdef.scan_flag_.is_need_feedback_ = false;
    scan_rtdef.need_check_output_datum_ = scan_spec.need_check_output_datum_;
    scan_rtdef.sql_mode_ = session->get_sql_mode();
    scan_rtdef.stmt_allocator_.set_alloc(&scan_allocator);
    scan_rtdef.scan_allocator_.set_alloc(&scan_allocator);
    scan_rtdef.eval_ctx_ = &eval_ctx_;
    scan_rtdef.frozen_version_ = scan_spec.frozen_version_;
    scan_rtdef.scan_op_id_ = scan_spec.get_id();
    scan_rtdef.scan_rows_size_ = scan_spec.rows_ * scan_spec.width_;
    scan_rtdef.runtime_schema_version_
        = exec_ctx_.get_sql_exec_ctx().get_query_begin_schema_version();
    if (OB_FAIL(scan_rtdef.init_pd_op(exec_ctx_, scan_ctdef))) {
      LOG_WARN("failed to initialize graph DAS scan pushdown", K(ret),
               K(scan_ctdef));
    } else if (OB_FAIL(scan_spec.tsc_ctdef_.snapshot_item_
                           .set_snapshot_query_info(eval_ctx_, scan_rtdef))) {
      LOG_WARN("failed to initialize graph DAS scan snapshot", K(ret),
               K(scan_spec));
    } else {
      scan_rtdef.table_loc_ = DAS_CTX(exec_ctx_).get_table_loc_by_id(
          scan_spec.get_table_loc_id(), scan_ctdef.ref_table_id_);
      if (scan_rtdef.table_loc_ == nullptr
          && &scan_ctdef == scan_spec.tsc_ctdef_.lookup_ctdef_
          && scan_spec.tsc_ctdef_.lookup_loc_meta_ != nullptr) {
        ret = DAS_CTX(exec_ctx_).extended_table_loc(
            *scan_spec.tsc_ctdef_.lookup_loc_meta_, scan_rtdef.table_loc_);
      }
      if (OB_SUCC(ret)
          && (OB_ISNULL(scan_rtdef.table_loc_)
              || OB_ISNULL(scan_rtdef.table_loc_->loc_meta_))) {
        ret = OB_ERR_UNEXPECTED;
        LOG_WARN("graph DAS scan table location metadata is missing", K(ret),
                 K(scan_spec.get_table_loc_id()), K(scan_ctdef.ref_table_id_));
      } else if (OB_SUCC(ret)
                 && scan_rtdef.table_loc_->loc_meta_->is_weak_read_) {
        scan_rtdef.scan_flag_.set_is_select_follower();
      }
      if (OB_SUCC(ret) && scan_spec.ref_table_id_ != scan_ctdef.ref_table_id_) {
        scan_rtdef.need_scn_ = false;
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
  if (OB_FAIL(init_scan_rtdef(*binding.scan_spec_, *binding.scan_ctdef_, false,
                              das_ref.get_das_alloc(), scan_rtdef))) {
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
    uint64_t element_id,
    int64_t key_count,
    ObExpr *const *key_exprs,
    GraphElementIdentity &identity) const
{
  int ret = OB_SUCCESS;
  identity.reset();
  if (OB_UNLIKELY(element_id == OB_INVALID_ID || key_count <= 0
                  || key_count > GRAPH_IDENTITY_MAX_KEYS
                  || key_exprs == nullptr)) {
    ret = OB_INVALID_ARGUMENT;
  } else {
    identity.graph_id_ = path_desc_.graph_id_;
    identity.element_id_ = element_id;
    identity.key_count_ = key_count;
  }
  for (int64_t key = 0; OB_SUCC(ret) && key < key_count; ++key) {
    ObDatum *datum = nullptr;
    ObExpr *expr = key_exprs[key];
    if (OB_ISNULL(expr)) {
      ret = OB_ERR_UNEXPECTED;
    } else if (OB_FAIL(expr->eval(eval_ctx_, datum))) {
    } else if (OB_ISNULL(datum) || datum->is_null()) {
      ret = OB_ERR_UNEXPECTED;
    } else if (OB_FAIL(datum->to_obj(identity.keys_[key], expr->obj_meta_,
                                     expr->obj_datum_map_))) {
    } else if (OB_UNLIKELY(identity.keys_[key].get_type() != ObIntType)) {
      ret = OB_ERR_UNEXPECTED;
    }
  }
  return ret;
}

int GraphExpandDasAccess::materialize_identity(
    const VertexLookupBinding &binding,
    GraphElementIdentity &identity) const
{
  return materialize_identity(binding.element_id_, binding.key_count_,
                              binding.key_exprs_, identity);
}

int GraphExpandDasAccess::edge_row_has_null_endpoint(bool &has_null) const
{
  int ret = OB_SUCCESS;
  has_null = false;
  for (int endpoint = 0; OB_SUCC(ret) && !has_null && endpoint < 2;
       ++endpoint) {
    const int64_t key_count = endpoint == 0 ? edge_binding_.source_key_count_
                                            : edge_binding_.target_key_count_;
    ObExpr *const *key_exprs = endpoint == 0 ? edge_binding_.source_exprs_
                                              : edge_binding_.target_exprs_;
    for (int64_t key = 0; OB_SUCC(ret) && !has_null && key < key_count; ++key) {
      ObDatum *datum = nullptr;
      if (OB_ISNULL(key_exprs[key])) {
        ret = OB_ERR_UNEXPECTED;
      } else if (OB_FAIL(key_exprs[key]->eval(eval_ctx_, datum))) {
      } else if (OB_ISNULL(datum)) {
        ret = OB_ERR_UNEXPECTED;
      } else {
        has_null = datum->is_null();
      }
    }
  }
  return ret;
}

int GraphExpandDasAccess::materialize_edge(GraphExpandEdge &edge,
                                           bool &matches) const
{
  int ret = OB_SUCCESS;
  bool has_null_endpoint = false;
  edge = GraphExpandEdge();
  matches = false;
  if (OB_UNLIKELY(!edge_binding_.is_valid())) {
    ret = OB_NOT_INIT;
  } else if (OB_FAIL(edge_row_has_null_endpoint(has_null_endpoint))) {
  } else if (has_null_endpoint) {
    // SQL equality does not join a NULL endpoint to a frontier or vertex key,
    // so this physical row is not a graph-edge match.
  } else if (OB_FAIL(materialize_identity(
                 path_desc_.source_element_id_, edge_binding_.source_key_count_,
                 edge_binding_.source_exprs_, edge.source_identity_))) {
  } else if (OB_FAIL(materialize_identity(
                 path_desc_.edge_element_id_, edge_binding_.edge_key_count_,
                 edge_binding_.edge_exprs_, edge.edge_identity_))) {
  } else if (OB_FAIL(materialize_identity(
                 path_desc_.target_element_id_, edge_binding_.target_key_count_,
                 edge_binding_.target_exprs_, edge.target_identity_))) {
  } else {
    matches = true;
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

int GraphExpandDasAccess::validate_edge_scan_request(
    const ObIArray<GraphElementIdentity> &sources,
    GraphPathDirection direction,
    const GraphElementIdentity *after_edge,
    int64_t limit) const
{
  int ret = OB_SUCCESS;
  if (OB_UNLIKELY(direction != path_desc_.direction_ || limit <= 0
                  || sources.count() > GRAPH_EXPAND_MAX_INPUT_STATE_COUNT)) {
    ret = OB_INVALID_ARGUMENT;
  }
  for (int64_t i = 0; OB_SUCC(ret) && i < sources.count(); ++i) {
    if (!identity_matches(sources.at(i), path_desc_.graph_id_,
                          path_desc_.source_element_id_,
                          edge_binding_.source_key_count_)) {
      ret = OB_INVALID_ARGUMENT;
    }
  }
  if (OB_SUCC(ret) && after_edge != nullptr
      && !identity_matches(*after_edge, path_desc_.graph_id_,
                           path_desc_.edge_element_id_,
                           edge_binding_.edge_key_count_)) {
    ret = OB_INVALID_ARGUMENT;
  } else if (OB_SUCC(ret) && after_edge != nullptr
             && (!edge_scan_active_ || !has_edge_cursor_
                 || edge_sources_hash_ != identity_array_hash(sources)
                 || !(*after_edge == edge_cursor_))) {
    ret = OB_INVALID_ARGUMENT;
  }
  return ret;
}

int GraphExpandDasAccess::init_edge_scan_rtdefs(bool uses_index_back)
{
  int ret = OB_SUCCESS;
  const ObTableScanSpec *scan_spec = edge_binding_.scan_spec_;
  const ObDASScanCtDef *scan_ctdef = edge_binding_.access_ctdef_;
  const ObDASScanCtDef *lookup_ctdef = edge_binding_.output_ctdef_;
  edge_das_ref_.set_mem_attr(ObMemAttr("GraphEdgeScan"));
  void *scan_buffer = nullptr;
  void *lookup_buffer = nullptr;
  if (OB_ISNULL(scan_spec) || OB_ISNULL(scan_ctdef)
      || OB_ISNULL(lookup_ctdef)) {
    ret = OB_NOT_INIT;
  } else if (OB_ISNULL(scan_buffer = edge_das_ref_.get_das_alloc().alloc(
                           sizeof(ObDASScanRtDef)))) {
    ret = OB_ALLOCATE_MEMORY_FAILED;
  } else {
    edge_scan_rtdef_ = new(scan_buffer) ObDASScanRtDef();
    if (OB_FAIL(init_scan_rtdef(*scan_spec, *scan_ctdef, uses_index_back,
                                edge_das_ref_.get_das_alloc(),
                                *edge_scan_rtdef_))) {
    } else {
      edge_scan_rtdef_->scan_flag_.scan_order_ = ObQueryFlag::Forward;
    }
  }
  if (OB_SUCC(ret) && uses_index_back) {
    if (OB_UNLIKELY(lookup_ctdef == scan_ctdef)) {
      ret = OB_ERR_UNEXPECTED;
    } else if (OB_ISNULL(lookup_buffer = edge_das_ref_.get_das_alloc().alloc(
                              sizeof(ObDASScanRtDef)))) {
      ret = OB_ALLOCATE_MEMORY_FAILED;
    } else {
      edge_lookup_rtdef_ = new(lookup_buffer) ObDASScanRtDef();
      if (OB_FAIL(init_scan_rtdef(*scan_spec, *lookup_ctdef, true,
                                  edge_das_ref_.get_das_alloc(),
                                  *edge_lookup_rtdef_))) {
      }
    }
  }
  return ret;
}

int GraphExpandDasAccess::build_adjacency_ranges(
    const ObIArray<GraphElementIdentity> &sources,
    ObIArray<ObNewRange> &ranges)
{
  int ret = OB_SUCCESS;
  const ObDASScanCtDef *scan_ctdef = edge_binding_.access_ctdef_;
  const int64_t prefix_count = edge_binding_.source_key_count_;
  const int64_t rowkey_count = scan_ctdef == nullptr
      ? 0 : scan_ctdef->table_param_.get_read_info().get_schema_rowkey_count();
  if (OB_UNLIKELY(!access_desc_.uses_adjacency_index()
                  || scan_ctdef == nullptr || prefix_count <= 0
                  || rowkey_count < prefix_count)) {
    ret = OB_ERR_UNEXPECTED;
    LOG_WARN("invalid graph adjacency rowkey layout", K(ret), K(prefix_count),
             K(rowkey_count), K_(edge_binding));
  }
  for (int64_t i = 0; OB_SUCC(ret) && i < sources.count(); ++i) {
    bool duplicate = false;
    for (int64_t j = 0; !duplicate && j < i; ++j) {
      duplicate = sources.at(i) == sources.at(j);
    }
    if (!duplicate) {
      ObObj *keys = static_cast<ObObj *>(edge_das_ref_.get_das_alloc().alloc(
          sizeof(ObObj) * rowkey_count * 2));
      if (OB_ISNULL(keys)) {
        ret = OB_ALLOCATE_MEMORY_FAILED;
      } else {
        new(keys) ObObj[rowkey_count * 2];
        ObObj *start_keys = keys;
        ObObj *end_keys = keys + rowkey_count;
        for (int64_t key = 0; OB_SUCC(ret) && key < prefix_count; ++key) {
          if (OB_FAIL(ob_write_obj(edge_das_ref_.get_das_alloc(),
                                   sources.at(i).keys_[key],
                                   start_keys[key]))) {
          } else if (OB_FAIL(ob_write_obj(edge_das_ref_.get_das_alloc(),
                                          sources.at(i).keys_[key],
                                          end_keys[key]))) {
          }
        }
        for (int64_t key = prefix_count; key < rowkey_count; ++key) {
          start_keys[key].set_min_value();
          end_keys[key].set_max_value();
        }
        if (OB_SUCC(ret)) {
          ObNewRange range;
          range.table_id_ = edge_binding_.access_table_id_;
          range.start_key_.assign(start_keys, rowkey_count);
          range.end_key_.assign(end_keys, rowkey_count);
          range.border_flag_.set_inclusive_start();
          range.border_flag_.set_inclusive_end();
          if (OB_FAIL(ranges.push_back(range))) {
          }
        }
      }
    }
  }
  return ret;
}

int GraphExpandDasAccess::attach_edge_lookup(
    ObDASScanOp &scan_op, const ObDASTabletLoc &index_tablet_loc)
{
  int ret = OB_SUCCESS;
  ObDASTableLoc *lookup_table_loc = edge_lookup_rtdef_ == nullptr
      ? nullptr : edge_lookup_rtdef_->table_loc_;
  ObDASTabletLoc *lookup_tablet_loc = nullptr;
  if (OB_ISNULL(lookup_table_loc) || OB_ISNULL(lookup_table_loc->loc_meta_)
      || OB_ISNULL(edge_binding_.output_ctdef_)) {
    ret = OB_ERR_UNEXPECTED;
  } else if (OB_ISNULL(lookup_tablet_loc = ObDASUtils::get_related_tablet_loc(
                           index_tablet_loc,
                           lookup_table_loc->loc_meta_->ref_table_id_))) {
    ret = OB_ERR_UNEXPECTED;
    LOG_WARN("graph edge lookup tablet is missing", K(ret),
             K(index_tablet_loc), KPC(lookup_table_loc));
  } else if (OB_FAIL(scan_op.reserve_related_buffer(1))) {
  } else if (OB_FAIL(scan_op.set_related_task_info(
                 edge_binding_.output_ctdef_, edge_lookup_rtdef_,
                 lookup_tablet_loc->tablet_id_))) {
  } else {
    lookup_table_loc->is_reading_ = true;
  }
  return ret;
}

int GraphExpandDasAccess::start_full_edge_scan(bool &empty)
{
  int ret = OB_SUCCESS;
  bool startup_filtered = false;
  empty = false;
  const ObTableScanSpec *scan_spec = edge_binding_.scan_spec_;
  const ObDASScanCtDef *scan_ctdef = edge_binding_.access_ctdef_;
  if (OB_ISNULL(scan_spec) || OB_ISNULL(scan_ctdef)) {
    ret = OB_NOT_INIT;
  } else if (access_desc_.uses_adjacency_index()
             || edge_binding_.output_ctdef_ != scan_ctdef) {
    ret = OB_NOT_SUPPORTED;
  } else if (OB_FAIL(evaluate_filters(eval_ctx_, scan_spec->startup_filters_,
                                      startup_filtered))) {
  } else if (startup_filtered) {
    empty = true;
  } else if (OB_FAIL(init_edge_scan_rtdefs(false))) {
  }
  ObDASTableLoc *table_loc = edge_scan_rtdef_ == nullptr
      ? nullptr : edge_scan_rtdef_->table_loc_;
  if (OB_SUCC(ret) && !empty
      && (OB_ISNULL(table_loc) || table_loc->get_tablet_locs().empty())) {
    ret = OB_ERR_UNEXPECTED;
    LOG_WARN("graph edge full scan has no tablet location", K(ret),
             KPC(table_loc));
  } else if (OB_SUCC(ret) && !empty
             && table_loc->get_tablet_locs().size() != 1) {
    // Global identity ordering across tablet streams needs an explicit merge
    // cursor. Keep that separate from the single-tablet full-scan increment.
    ret = OB_NOT_SUPPORTED;
    LOG_WARN("partitioned graph edge full scan is not implemented", K(ret),
             "tablet_count", table_loc->get_tablet_locs().size());
  }
  if (OB_SUCC(ret) && !empty) {
    ObDASScanOp *scan_op = nullptr;
    ObNewRange whole_range;
    whole_range.set_whole_range();
    whole_range.table_id_ = edge_binding_.access_table_id_;
    if (OB_FAIL(edge_das_ref_.prepare_das_task(
            table_loc->get_first_tablet_loc(), scan_op))) {
    } else if (OB_ISNULL(scan_op)) {
      ret = OB_ERR_UNEXPECTED;
    } else {
      scan_op->set_scan_ctdef(scan_ctdef);
      scan_op->set_scan_rtdef(edge_scan_rtdef_);
      scan_op->set_can_part_retry(false);
      if (OB_FAIL(scan_op->get_scan_param().key_ranges_.push_back(
              whole_range))) {
      } else {
        table_loc->is_reading_ = true;
      }
    }
  }
  if (OB_SUCC(ret) && !empty
      && OB_FAIL(edge_das_ref_.execute_all_task())) {
    LOG_WARN("failed to execute graph edge full scan DAS task", K(ret));
  }
  if (OB_SUCC(ret) && !empty) {
    edge_result_iter_ = edge_das_ref_.begin_result_iter();
    edge_scan_active_ = true;
  }
  return ret;
}

int GraphExpandDasAccess::start_adjacency_edge_scan(
    const ObIArray<GraphElementIdentity> &sources,
    bool &empty)
{
  int ret = OB_SUCCESS;
  bool startup_filtered = false;
  const ObTableScanSpec *scan_spec = edge_binding_.scan_spec_;
  const ObDASScanCtDef *scan_ctdef = edge_binding_.access_ctdef_;
  const bool uses_index_back = edge_binding_.output_ctdef_ != scan_ctdef;
  ObArray<ObNewRange> ranges(
      OB_MALLOC_NORMAL_BLOCK_SIZE,
      ModulePageAllocator(edge_das_ref_.get_das_alloc(), "GraphEdgeRange"));
  empty = false;
  if (OB_ISNULL(scan_spec) || OB_ISNULL(scan_ctdef)) {
    ret = OB_NOT_INIT;
  } else if (OB_UNLIKELY(!access_desc_.uses_adjacency_index())) {
    ret = OB_INVALID_ARGUMENT;
  } else if (OB_FAIL(evaluate_filters(eval_ctx_, scan_spec->startup_filters_,
                                      startup_filtered))) {
  } else if (startup_filtered) {
    empty = true;
  } else if (OB_FAIL(init_edge_scan_rtdefs(uses_index_back))) {
  } else if (OB_FAIL(build_adjacency_ranges(sources, ranges))) {
    LOG_WARN("failed to build graph adjacency ranges", K(ret), K(sources));
  } else if (ranges.empty()) {
    empty = true;
  }
  ObDASTableLoc *table_loc = edge_scan_rtdef_ == nullptr
      ? nullptr : edge_scan_rtdef_->table_loc_;
  if (OB_SUCC(ret) && !empty
      && (OB_ISNULL(table_loc) || table_loc->get_tablet_locs().empty())) {
    ret = OB_ERR_UNEXPECTED;
    LOG_WARN("graph adjacency scan has no tablet location", K(ret),
             KPC(table_loc));
  }
  if (OB_SUCC(ret) && !empty) {
    // Local secondary indexes have one related index tablet per base-table
    // tablet. Fan the prefix ranges to each local tablet; only the owning
    // tablet can return a row, so this preserves correctness before range
    // routing is optimized.
    for (DASTabletLocListIter node = table_loc->tablet_locs_begin();
         OB_SUCC(ret) && node != table_loc->tablet_locs_end(); ++node) {
      ObDASScanOp *scan_op = nullptr;
      if (OB_FAIL(edge_das_ref_.prepare_das_task(*node, scan_op))) {
      } else if (OB_ISNULL(scan_op)) {
        ret = OB_ERR_UNEXPECTED;
      } else {
        scan_op->set_scan_ctdef(scan_ctdef);
        scan_op->set_scan_rtdef(edge_scan_rtdef_);
        scan_op->set_can_part_retry(false);
        for (int64_t i = 0; OB_SUCC(ret) && i < ranges.count(); ++i) {
          ret = scan_op->get_scan_param().key_ranges_.push_back(ranges.at(i));
        }
        if (OB_SUCC(ret) && uses_index_back
            && OB_FAIL(attach_edge_lookup(*scan_op, **node))) {
          LOG_WARN("failed to attach graph edge index lookup", K(ret));
        }
      }
    }
    table_loc->is_reading_ = true;
  }
  if (OB_SUCC(ret) && !empty
      && OB_FAIL(edge_das_ref_.execute_all_task())) {
    LOG_WARN("failed to execute graph adjacency scan DAS tasks", K(ret));
  }
  if (OB_SUCC(ret) && !empty) {
    edge_result_iter_ = edge_das_ref_.begin_result_iter();
    edge_scan_active_ = true;
  }
  return ret;
}

int GraphExpandDasAccess::clear_edge_eval_flags()
{
  int ret = OB_SUCCESS;
  if (OB_ISNULL(edge_scan_rtdef_)
      || OB_ISNULL(edge_scan_rtdef_->p_pd_expr_op_)) {
    ret = OB_ERR_UNEXPECTED;
    LOG_WARN("graph edge scan pushdown operator is missing", K(ret));
  } else {
    edge_scan_rtdef_->p_pd_expr_op_->clear_datum_eval_flag();
  }
  if (OB_SUCC(ret) && edge_lookup_rtdef_ != nullptr) {
    if (OB_ISNULL(edge_lookup_rtdef_->p_pd_expr_op_)) {
      ret = OB_ERR_UNEXPECTED;
      LOG_WARN("graph edge lookup pushdown operator is missing", K(ret));
    } else {
      edge_lookup_rtdef_->p_pd_expr_op_->clear_datum_eval_flag();
    }
  }
  return ret;
}

int GraphExpandDasAccess::get_edge_page(
    const ObIArray<GraphElementIdentity> &sources,
    int64_t limit,
    ObIArray<GraphExpandEdge> &edges,
    bool &end)
{
  int ret = OB_SUCCESS;
  bool finished = false;
  const ObTableScanSpec *scan_spec = edge_binding_.scan_spec_;
  if (OB_UNLIKELY(!edge_scan_active_ || edge_scan_rtdef_ == nullptr
                  || scan_spec == nullptr)) {
    ret = OB_NOT_INIT;
  }
  while (OB_SUCC(ret) && !finished && edges.count() < limit) {
    if (OB_FAIL(check_status())) {
    } else if (OB_FAIL(clear_edge_eval_flags())) {
    }
    if (OB_SUCC(ret) && OB_FAIL(edge_result_iter_.get_next_row())) {
      if (ret == OB_ITER_END) {
        ret = edge_result_iter_.next_result();
        if (ret == OB_ITER_END) {
          ret = OB_SUCCESS;
          finished = true;
        }
      }
    } else if (OB_SUCC(ret)) {
      bool filtered = false;
      bool matches = false;
      GraphExpandEdge edge;
      if (OB_FAIL(evaluate_filters(eval_ctx_, scan_spec->filters_, filtered))) {
      } else if (!filtered && OB_FAIL(materialize_edge(edge, matches))) {
      } else if (!filtered && matches
                 && access_desc_.uses_adjacency_index()
                 && !contains(sources, edge.source_identity_)) {
        ret = OB_ERR_UNEXPECTED;
        LOG_WARN("graph adjacency scan returned a non-frontier edge", K(ret),
                 K(edge), K(sources));
      } else if (!filtered && matches
                 && contains(sources, edge.source_identity_)) {
        int comparison = 1;
        if (!access_desc_.uses_adjacency_index() && has_edge_cursor_
            && OB_FAIL(compare_identities(edge.edge_identity_, edge_cursor_,
                                          comparison))) {
        } else if (OB_UNLIKELY(!access_desc_.uses_adjacency_index()
                               && has_edge_cursor_ && comparison <= 0)) {
          ret = OB_ERR_UNEXPECTED;
          LOG_WARN("graph edge full scan order is not stable", K(ret),
                   K(edge), K_(edge_cursor));
        } else if (OB_FAIL(edges.push_back(edge))) {
        } else {
          edge_cursor_ = edge.edge_identity_;
          has_edge_cursor_ = true;
        }
      }
    }
  }
  if (OB_SUCC(ret) && finished) {
    ret = reset_edge_scan(ret);
    end = OB_SUCC(ret);
  }
  return ret;
}

int GraphExpandDasAccess::scan_edges(
    const ObIArray<GraphElementIdentity> &sources,
    GraphPathDirection direction,
    const GraphElementIdentity *after_edge,
    int64_t limit,
    ObIArray<GraphExpandEdge> &edges,
    bool &end)
{
  int ret = OB_SUCCESS;
  bool empty = false;
  edges.reset();
  end = false;
  if (OB_UNLIKELY(!initialized_)) {
    ret = OB_NOT_INIT;
  } else if (OB_FAIL(check_status())) {
  } else if (OB_FAIL(validate_edge_scan_request(sources, direction,
                                                after_edge, limit))) {
    LOG_WARN("invalid graph edge scan request", K(ret), K(sources),
             K(direction), KPC(after_edge), K(limit));
  } else if (after_edge == nullptr
             && OB_FAIL(reset_edge_scan(OB_SUCCESS))) {
    LOG_WARN("failed to reset graph edge scan", K(ret));
  } else if (sources.empty()) {
    end = true;
  } else if (after_edge == nullptr) {
    edge_sources_hash_ = identity_array_hash(sources);
    if (access_desc_.uses_adjacency_index()
        && OB_FAIL(start_adjacency_edge_scan(sources, empty))) {
      LOG_WARN("failed to start graph adjacency scan", K(ret));
    } else if (!access_desc_.uses_adjacency_index()
               && OB_FAIL(start_full_edge_scan(empty))) {
      LOG_WARN("failed to start graph edge full scan", K(ret));
    } else if (empty) {
      end = true;
    }
  }
  if (OB_SUCC(ret) && !end
      && OB_FAIL(get_edge_page(sources, limit, edges, end))) {
    LOG_WARN("failed to read graph edge page", K(ret));
  }
  if (OB_SUCCESS != ret) {
    ret = reset_edge_scan(ret);
    edges.reset();
    end = false;
  }
  return ret;
}

} // namespace sql
} // namespace oceanbase
