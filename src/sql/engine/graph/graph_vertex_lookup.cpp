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
#include "sql/engine/graph/graph_vertex_lookup.h"

#include "common/ob_range.h"
#include "share/ob_debug_sync.h"
#include "sql/das/ob_das_scan_op.h"
#include "sql/engine/ob_exec_context.h"
#include "sql/engine/ob_operator.h"
#include "sql/optimizer/ob_table_location.h"
#include "sql/session/ob_sql_session_info.h"
#include "sql/engine/graph/graph_das_access_utils.h"
namespace oceanbase
{
using namespace common;
namespace sql
{
namespace
{

bool has_tolerance_compared_key(
    const ObIArray<GraphElementIdentity> &identities)
{
  bool found = false;
  for (int64_t i = 0; !found && i < identities.count(); ++i) {
    const ObRowkey &rowkey = identities.at(i).rowkey_;
    for (int64_t key = 0; !found && key < rowkey.get_obj_cnt(); ++key) {
      found = rowkey.get_obj_ptr()[key].get_type_class() == ObDoubleTC;
    }
  }
  return found;
}

int64_t find_identity(const ObIArray<GraphElementIdentity> &identities,
                      const GraphElementIdentity &identity)
{
  int64_t index = -1;
  for (int64_t i = 0; index < 0 && i < identities.count(); ++i) {
    if (identities.at(i) == identity) {
      index = i;
    }
  }
  return index;
}

bool contains_identity(const ObIArray<GraphElementIdentity> &identities,
                       const GraphElementIdentity &identity)
{
  return find_identity(identities, identity) >= 0;
}
} // namespace

GraphVertexLookup::GraphVertexLookup(ObExecContext &exec_ctx,
                                     ObEvalCtx &eval_ctx,
                                     ObIAllocator &work_area_allocator)
    : exec_ctx_(exec_ctx),
      eval_ctx_(eval_ctx),
      work_area_allocator_(work_area_allocator),
      identity_page_allocator_(exec_ctx.get_allocator())
{
}

GraphVertexLookup::~GraphVertexLookup()
{
  release();
  destroy_vertex_routers();
}

int GraphVertexLookup::init(const GraphPathDesc &path_desc,
                            const GraphExpandAccessDesc &access_desc,
                            const GraphExpandScanDesc &source_scan,
                            const GraphExpandScanDesc &target_scan)
{
  int ret = OB_SUCCESS;
  release();
  destroy_vertex_routers();
  initialized_ = false;
  path_desc_ = GraphPathDesc();
  source_binding_ = VertexLookupBinding();
  target_binding_ = VertexLookupBinding();
  if (OB_UNLIKELY(!path_desc.is_valid() || !access_desc.is_valid()
                  || !source_scan.is_valid() || !target_scan.is_valid())) {
    ret = OB_INVALID_ARGUMENT;
  } else if (OB_UNLIKELY(source_scan.ref_table_id_ != access_desc.source_table_id_
                         || target_scan.ref_table_id_ != access_desc.target_table_id_)) {
    ret = OB_ERR_UNEXPECTED;
  } else if (OB_FAIL(init_vertex_binding(
                 path_desc.source_element_id_, access_desc.source_table_id_,
                 access_desc.source_key_count_, access_desc.source_key_columns_,
                 source_scan, source_binding_))) {
    LOG_WARN("failed to bind graph source vertex lookup", K(ret));
  } else if (OB_FAIL(init_vertex_binding(
                 path_desc.target_element_id_, access_desc.target_table_id_,
                 access_desc.target_key_count_, access_desc.target_key_columns_,
                 target_scan, target_binding_))) {
    LOG_WARN("failed to bind graph target vertex lookup", K(ret));
  } else {
    path_desc_ = path_desc;
    initialized_ = true;
  }
  return ret;
}

void GraphVertexLookup::release()
{
  identity_page_allocator_.reset();
  clear_lookup_memory();
}

int64_t GraphVertexLookup::used_memory() const
{
  const int64_t page_bytes = identity_page_allocator_.total();
  const int64_t lookup_bytes = MAX(vertex_lookup_memory_bytes_, 0);
  return page_bytes > INT64_MAX - lookup_bytes ? INT64_MAX : page_bytes + lookup_bytes;
}

bool GraphVertexLookup::VertexLookupBinding::is_valid() const
{
  bool valid = element_id_ != OB_INVALID_ID
      && table_id_ != OB_INVALID_ID
      && key_count_ > 0
      && key_count_ <= OB_USER_MAX_ROWKEY_COLUMN_NUMBER
      && scan_desc_ != nullptr
      && scan_ctdef_ != nullptr
      && loc_meta_ != nullptr
      && scan_ctdef_->ref_table_id_ == table_id_
      && loc_meta_->table_loc_id_ == scan_desc_->get_table_loc_id()
      && loc_meta_->ref_table_id_ == table_id_;
  for (int64_t i = 0; valid && i < key_count_; ++i) {
    valid = key_columns_[i] != OB_INVALID_ID && key_exprs_[i] != nullptr;
  }
  return valid;
}

void GraphVertexLookup::destroy_vertex_routers()
{
  if (source_vertex_router_ != nullptr) {
    source_vertex_router_->~ObTableLocation();
    work_area_allocator_.free(source_vertex_router_);
    source_vertex_router_ = nullptr;
  }
  if (target_vertex_router_ != nullptr) {
    target_vertex_router_->~ObTableLocation();
    work_area_allocator_.free(target_vertex_router_);
    target_vertex_router_ = nullptr;
  }
  vertex_router_allocator_.reset();
}

int GraphVertexLookup::get_vertex_router(
    const VertexLookupBinding &binding,
    ObTableLocation *&router)
{
  int ret = OB_SUCCESS;
  router = nullptr;
  ObTableLocation **slot = nullptr;
  if (&binding == &source_binding_) {
    slot = &source_vertex_router_;
  } else if (&binding == &target_binding_) {
    slot = &target_vertex_router_;
  } else {
    ret = OB_INVALID_ARGUMENT;
  }
  if (OB_SUCC(ret) && *slot == nullptr) {
    void *buf = work_area_allocator_.alloc(sizeof(ObTableLocation));
    ObTableLocation *candidate = nullptr;
    ObSqlCtx *sql_ctx = exec_ctx_.get_sql_ctx();
    ObSQLSessionInfo *session = exec_ctx_.get_my_session();
    ObArray<uint64_t> key_columns(
        OB_MALLOC_NORMAL_BLOCK_SIZE,
        ModulePageAllocator(work_area_allocator_, "GraphVtxRoute"));
    if (OB_ISNULL(buf)) {
      ret = OB_ALLOCATE_MEMORY_FAILED;
    } else if (OB_ISNULL(sql_ctx) || OB_ISNULL(sql_ctx->schema_guard_)
               || OB_ISNULL(session)) {
      ret = OB_ERR_UNEXPECTED;
      LOG_WARN("graph vertex router context is incomplete", K(ret),
               K(sql_ctx), K(session));
    } else {
      candidate = new(buf) ObTableLocation(vertex_router_allocator_);
      for (int64_t i = 0; OB_SUCC(ret) && i < binding.key_count_; ++i) {
        if (OB_FAIL(key_columns.push_back(binding.key_columns_[i]))) {
          LOG_WARN("failed to append graph vertex routing column", K(ret),
                   K(i), K(binding));
        }
      }
      if (OB_SUCC(ret)) {
        ObSqlSchemaGuard schema_guard;
        schema_guard.set_schema_guard(sql_ctx->schema_guard_);
        if (OB_FAIL(candidate->init_table_location_with_column_ids(
                schema_guard, binding.table_id_, key_columns, exec_ctx_,
                false))) {
          LOG_WARN("failed to initialize graph vertex router", K(ret),
                   K(binding));
        }
      }
    }
    if (OB_SUCC(ret)) {
      *slot = candidate;
    } else if (candidate != nullptr) {
      candidate->~ObTableLocation();
      work_area_allocator_.free(candidate);
    } else if (buf != nullptr) {
      work_area_allocator_.free(buf);
    }
  }
  if (OB_SUCC(ret)) {
    router = *slot;
  }
  return ret;
}

int GraphVertexLookup::init_vertex_binding(
    uint64_t element_id,
    uint64_t table_id,
    int64_t key_count,
    const uint64_t *key_columns,
    const GraphExpandScanDesc &scan_desc,
    VertexLookupBinding &binding)
{
  int ret = OB_SUCCESS;
  const ObTableScanCtDef &tsc_ctdef = scan_desc.get_tsc_ctdef();
  const ObDASScanCtDef *scan_ctdef
      = tsc_ctdef.graph_lookup_ctdef_;
  const ObDASTableLocMeta *loc_meta
      = tsc_ctdef.graph_lookup_loc_meta_;
  VertexLookupBinding candidate;
  if (OB_UNLIKELY(element_id == OB_INVALID_ID || table_id == OB_INVALID_ID
                  || key_count <= 0
                  || key_count > OB_USER_MAX_ROWKEY_COLUMN_NUMBER
                  || key_columns == nullptr)) {
    ret = OB_INVALID_ARGUMENT;
  } else if (OB_ISNULL(scan_ctdef) || OB_ISNULL(loc_meta)) {
    ret = OB_ERR_UNEXPECTED;
    LOG_WARN("graph vertex scan has no dedicated base-table DAS ctdef", K(ret),
             K(table_id), K(scan_desc.ref_table_id_),
             "access_table_id",
             tsc_ctdef.scan_ctdef_.ref_table_id_);
  } else if (OB_UNLIKELY(scan_ctdef->ref_table_id_ != table_id
                         || loc_meta->ref_table_id_ != table_id
                         || loc_meta->table_loc_id_
                                != scan_desc.get_table_loc_id())) {
    ret = OB_ERR_UNEXPECTED;
    LOG_WARN("graph vertex lookup definition does not match table scan", K(ret),
             K(table_id), K(scan_ctdef->ref_table_id_), KPC(loc_meta),
             K(scan_desc.get_table_loc_id()));
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
    candidate.scan_desc_ = &scan_desc;
    candidate.scan_ctdef_ = scan_ctdef;
    candidate.loc_meta_ = loc_meta;
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

int GraphVertexLookup::check_status()
{
  return exec_ctx_.check_status();
}

const GraphVertexLookup::VertexLookupBinding *
GraphVertexLookup::find_vertex_binding(uint64_t element_id) const
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

int GraphVertexLookup::validate_requested(
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
        || identity.rowkey_.get_obj_cnt() != binding->key_count_) {
      ret = OB_INVALID_ARGUMENT;
    }
  }
  return ret;
}

int GraphVertexLookup::lookup_vertices(
    const ObIArray<GraphElementIdentity> &requested,
    ObIArray<GraphElementIdentity> &existing,
    GraphVertexLookupPhase phase)
{
  int ret = OB_SUCCESS;
  const VertexLookupBinding *binding = nullptr;
  vertex_lookup_memory_bytes_ = 0;
  existing.reset();
  if (OB_UNLIKELY(!initialized_)) {
    ret = OB_NOT_INIT;
  } else if (OB_UNLIKELY(phase != GraphVertexLookupPhase::SOURCE
                         && phase != GraphVertexLookupPhase::TARGET)) {
    ret = OB_INVALID_ARGUMENT;
  } else if (OB_FAIL(check_status())) {
  } else if (OB_FAIL(validate_requested(requested, binding))) {
    LOG_WARN("invalid graph vertex lookup request", K(ret), K(requested));
  } else if (requested.empty()) {
  } else {
    identity_page_allocator_.reuse();
    if (OB_FAIL(lookup_vertices(
            *binding, requested, existing, phase))) {
      LOG_WARN("failed to look up graph vertices", K(ret), K(requested),
               KPC(binding));
    }
  }
  if (OB_FAIL(ret)) {
    existing.reset();
  }
  return ret;
}

int GraphVertexLookup::lookup_vertices(
    const VertexLookupBinding &binding,
    const ObIArray<GraphElementIdentity> &requested,
    ObIArray<GraphElementIdentity> &existing,
    GraphVertexLookupPhase phase)
{
  int ret = OB_SUCCESS;
  ObDASRef das_ref(eval_ctx_, exec_ctx_);
  ObDASScanRtDef scan_rtdef;
  GraphIdentityIndex requested_index(das_ref.get_das_alloc());
  GraphIdentityIndex existing_index(das_ref.get_das_alloc());
  ObArray<int64_t> requested_positions(
      OB_MALLOC_NORMAL_BLOCK_SIZE,
      ModulePageAllocator(das_ref.get_das_alloc(), "GraphVtxLookup"));
  ObArray<uint64_t> key_columns(
      OB_MALLOC_NORMAL_BLOCK_SIZE,
      ModulePageAllocator(das_ref.get_das_alloc(), "GraphVtxRoute"));
  ObArray<ObTabletID> routed_tablet_ids(
      OB_MALLOC_NORMAL_BLOCK_SIZE,
      ModulePageAllocator(das_ref.get_das_alloc(), "GraphVtxRoute"));
  ObArray<ObObjectID> routed_partition_ids(
      OB_MALLOC_NORMAL_BLOCK_SIZE,
      ModulePageAllocator(das_ref.get_das_alloc(), "GraphVtxRoute"));
  const bool use_identity_index = !has_tolerance_compared_key(requested);
  bool has_task = false;
  ObTableLocation *router = nullptr;
  das_ref.set_mem_attr(ObMemAttr("GraphVtxLookup"));
  if (OB_FAIL(graph_das_detail::init_scan_rtdef(exec_ctx_, eval_ctx_, *binding.scan_desc_, *binding.scan_ctdef_,
                              binding.loc_meta_, false,
                              das_ref.get_das_alloc(), scan_rtdef))) {
  } else if (use_identity_index
             && OB_FAIL(requested_index.init(requested.count()))) {
    LOG_WARN("failed to initialize graph vertex request index", K(ret),
             "request_count", requested.count());
  } else if (use_identity_index
             && OB_FAIL(existing_index.init(requested.count()))) {
    LOG_WARN("failed to initialize graph vertex result index", K(ret),
             "request_count", requested.count());
  }
  ObDASTableLoc *table_loc = scan_rtdef.table_loc_;
  if (OB_SUCC(ret) && OB_ISNULL(table_loc)) {
    ret = OB_ERR_UNEXPECTED;
    LOG_WARN("graph vertex lookup has no tablet location", K(ret),
             KPC(table_loc));
  } else if (OB_SUCC(ret)
             && OB_FAIL(get_vertex_router(binding, router))) {
    LOG_WARN("failed to get graph vertex router", K(ret), K(binding));
  }
  for (int64_t key = 0;
       OB_SUCC(ret) && router != nullptr && key < binding.key_count_; ++key) {
    if (OB_FAIL(key_columns.push_back(binding.key_columns_[key]))) {
      LOG_WARN("failed to append graph vertex routing column", K(ret),
               K(key), K(binding));
    }
  }
  for (int64_t i = 0; OB_SUCC(ret) && i < requested.count(); ++i) {
    int64_t entry_index = -1;
    bool inserted = false;
    bool append_range = !use_identity_index;
    if (use_identity_index
        && OB_FAIL(requested_index.get_or_insert(
               requested.at(i), entry_index, inserted))) {
      LOG_WARN("failed to index graph vertex request", K(ret), K(i));
    } else if (use_identity_index && inserted
               && OB_UNLIKELY(entry_index != requested_positions.count())) {
      ret = OB_ERR_UNEXPECTED;
      LOG_WARN("graph vertex request index is inconsistent", K(ret), K(i),
               K(entry_index), "position_count", requested_positions.count());
    } else if (use_identity_index && inserted
               && OB_FAIL(requested_positions.push_back(i))) {
      LOG_WARN("failed to append graph vertex request position", K(ret), K(i));
    } else if (use_identity_index) {
      append_range = inserted;
    } else {
      for (int64_t previous = 0;
           append_range && previous < i; ++previous) {
        append_range = !(requested.at(i) == requested.at(previous));
      }
    }
    if (OB_SUCC(ret) && append_range) {
      ObDASTabletLoc *tablet_loc = nullptr;
      if (router == nullptr) {
        tablet_loc = table_loc->get_first_tablet_loc();
      } else {
        routed_tablet_ids.reuse();
        routed_partition_ids.reuse();
        ObNewRow key_row;
        key_row.cells_ = const_cast<ObObj *>(
            requested.at(i).rowkey_.get_obj_ptr());
        key_row.count_ = requested.at(i).rowkey_.get_obj_cnt();
        if (OB_FAIL(router->calculate_tablet_id_by_row(
                exec_ctx_, binding.table_id_, key_columns, key_row,
                routed_tablet_ids, routed_partition_ids))) {
          LOG_WARN("failed to route graph vertex identity", K(ret), K(i),
                   K(requested.at(i)), K(binding));
        } else if (OB_UNLIKELY(routed_tablet_ids.count()
                                   != routed_partition_ids.count()
                               || routed_tablet_ids.count() > 1)) {
          ret = OB_ERR_UNEXPECTED;
          LOG_WARN("graph vertex identity has ambiguous routing", K(ret),
                   K(i), K(requested.at(i)), K(routed_tablet_ids),
                   K(routed_partition_ids));
        } else if (!routed_tablet_ids.empty()
                   && OB_FAIL(DAS_CTX(exec_ctx_).extended_tablet_loc(
                       *table_loc, routed_tablet_ids.at(0), tablet_loc,
                       routed_partition_ids.at(0)))) {
          LOG_WARN("failed to extend routed graph vertex tablet", K(ret),
                   K(i), K(routed_tablet_ids.at(0)),
                   K(routed_partition_ids.at(0)));
        }
      }
      if (OB_SUCC(ret) && tablet_loc != nullptr) {
        ObObj *keys = static_cast<ObObj *>(
            das_ref.get_das_alloc().alloc(
                sizeof(ObObj) * binding.key_count_));
        ObNewRange range;
        ObDASScanOp *scan_op = nullptr;
        if (OB_ISNULL(keys)) {
          ret = OB_ALLOCATE_MEMORY_FAILED;
        } else {
          new(keys) ObObj[binding.key_count_];
        }
        for (int64_t key = 0;
             OB_SUCC(ret) && key < binding.key_count_; ++key) {
          ret = ob_write_obj(das_ref.get_das_alloc(),
                             requested.at(i).rowkey_.get_obj_ptr()[key],
                             keys[key]);
        }
        if (OB_SUCC(ret)) {
          ObRowkey rowkey(keys, binding.key_count_);
          if (OB_FAIL(range.build_range(binding.table_id_, rowkey))) {
          } else if (!das_ref.has_das_op(tablet_loc, scan_op)) {
            if (OB_FAIL(das_ref.prepare_das_task(tablet_loc, scan_op))) {
            } else if (OB_ISNULL(scan_op)) {
              ret = OB_ERR_UNEXPECTED;
            } else {
              scan_op->set_scan_ctdef(binding.scan_ctdef_);
              scan_op->set_scan_rtdef(&scan_rtdef);
              scan_op->set_can_part_retry(false);
            }
          }
          if (OB_SUCC(ret)) {
            if (OB_FAIL(scan_op->get_scan_param()
                            .key_ranges_.push_back(range))) {
            } else {
              has_task = true;
            }
          }
        }
      }
    }
  }
  if (OB_SUCC(ret) && has_task) {
    table_loc->is_reading_ = true;
  }
  if (OB_SUCC(ret) && has_task && OB_FAIL(das_ref.execute_all_task())) {
    LOG_WARN("failed to execute graph vertex lookup DAS tasks", K(ret));
  }
  if (OB_SUCC(ret) && has_task) {
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
          int64_t request_entry = -1;
          int64_t result_entry = -1;
          bool found = false;
          bool inserted = false;
          int64_t requested_position = -1;
          if (use_identity_index
              && OB_FAIL(requested_index.find(
                     identity, request_entry, found))) {
            LOG_WARN("failed to find graph vertex request", K(ret), K(identity));
          } else if (use_identity_index
                     && (!found || request_entry < 0
                         || request_entry >= requested_positions.count())) {
            ret = OB_ERR_UNEXPECTED;
            LOG_WARN("graph vertex lookup returned an unexpected row", K(ret),
                     K(identity), K(requested));
          } else {
            requested_position = use_identity_index
                ? requested_positions.at(request_entry)
                : find_identity(requested, identity);
          }
          if (OB_SUCC(ret) && requested_position < 0) {
            ret = OB_ERR_UNEXPECTED;
            LOG_WARN("graph vertex lookup returned an unexpected row", K(ret),
                     K(identity), K(requested));
          } else if (OB_SUCC(ret)) {
            const GraphElementIdentity &stable_identity =
                requested.at(requested_position);
            if (use_identity_index
                && OB_FAIL(existing_index.get_or_insert(
                       stable_identity, result_entry, inserted))) {
              LOG_WARN("failed to index graph vertex result", K(ret),
                       K(stable_identity));
            } else if ((use_identity_index && !inserted)
                       || (!use_identity_index
                           && contains_identity(existing, identity))) {
              ret = OB_ERR_UNEXPECTED;
              LOG_WARN("graph vertex lookup returned a duplicate row", K(ret),
                       K(identity), K(result_entry));
            } else if (OB_FAIL(existing.push_back(stable_identity))) {
            }
          }
        }
        // The source-existence lookup happens before edge access. Inject only
        // after a target result has been appended, while this multi-get's DAS
        // tasks and a partial output array are both live.
        if (OB_SUCC(ret) && phase == GraphVertexLookupPhase::TARGET) {
          const int simulate_error = EVENT_CALL(
              EventTable::EN_DAS_GRAPH_TARGET_LOOKUP_AFTER_VERTEX_ROW);
          if (OB_UNLIKELY(OB_SUCCESS != simulate_error)) {
            ret = simulate_error;
            LOG_WARN("injected graph target vertex lookup error", K(ret),
                     "existing_count", existing.count());
          }
        }
      }
    }
  }
  vertex_lookup_memory_bytes_ = das_ref.get_das_mem_used();
  ret = graph_das_detail::close_das_tasks(das_ref, ret);
  return ret;
}

int GraphVertexLookup::materialize_identity(
    const VertexLookupBinding &binding,
    GraphElementIdentity &identity)
{
  return graph_das_detail::materialize_identity(eval_ctx_, identity_page_allocator_,
      path_desc_.graph_id_, binding.element_id_, binding.key_count_,
                              binding.key_exprs_, identity);
}
} // namespace sql
} // namespace oceanbase
