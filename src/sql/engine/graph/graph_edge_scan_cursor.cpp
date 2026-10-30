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
#include "sql/engine/graph/graph_edge_scan_cursor.h"

#include "common/ob_range.h"
#include "share/ob_debug_sync.h"
#include "sql/das/ob_das_scan_op.h"
#include "sql/das/ob_das_utils.h"
#include "sql/engine/ob_exec_context.h"
#include "sql/engine/ob_operator.h"
#include "sql/engine/graph/graph_das_access_utils.h"
namespace oceanbase
{
using namespace common;
namespace sql
{
namespace
{

bool identity_matches(const GraphElementIdentity &identity,
                      uint64_t graph_id,
                      uint64_t element_id,
                      int64_t key_count)
{
  bool matches = identity.is_valid()
      && identity.graph_id_ == graph_id
      && identity.element_id_ == element_id
      && identity.rowkey_.get_obj_cnt() == key_count;
  return matches;
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

} // namespace

GraphEdgeScanCursor::GraphEdgeScanCursor(ObExecContext &exec_ctx,
                                        ObEvalCtx &eval_ctx,
                                        ObIAllocator &work_area_allocator)
    : exec_ctx_(exec_ctx),
      eval_ctx_(eval_ctx),
      edge_das_ref_(eval_ctx, exec_ctx),
      identity_page_allocator_(exec_ctx.get_allocator()),
      payload_scratch_allocator_(work_area_allocator),
      edge_cursor_allocator_(exec_ctx.get_allocator()),
      edge_source_index_(work_area_allocator)
{
  edge_das_ref_.set_mem_attr(ObMemAttr("GraphEdgeScan", ObCtxIds::WORK_AREA));
}

GraphEdgeScanCursor::~GraphEdgeScanCursor()
{
  const int ret = release();
  if (OB_SUCCESS != ret) {
    LOG_WARN("failed to close graph edge cursor", K(ret));
  }
}

int GraphEdgeScanCursor::init(const GraphPathDesc &path_desc,
                              const GraphExpandAccessDesc &access_desc,
                              const GraphExpandScanDesc &edge_scan)
{
  int ret = release();
  initialized_ = false;
  path_desc_ = GraphPathDesc();
  access_desc_ = GraphExpandAccessDesc();
  edge_binding_ = EdgeScanBinding();
  if (OB_SUCCESS != ret) {
    LOG_WARN("failed to release previous graph edge cursor", K(ret));
  } else if (OB_UNLIKELY(!path_desc.is_valid() || !access_desc.is_valid()
                         || !edge_scan.is_valid())) {
    ret = OB_INVALID_ARGUMENT;
  } else if (OB_FAIL(init_edge_binding(path_desc, access_desc, edge_scan, edge_binding_))) {
    LOG_WARN("failed to bind graph edge scan", K(ret));
  } else {
    path_desc_ = path_desc;
    access_desc_ = access_desc;
    initialized_ = true;
  }
  return ret;
}

int GraphEdgeScanCursor::release()
{
  const int ret = reset_edge_scan(OB_SUCCESS, false);
  identity_page_allocator_.reset();
  payload_scratch_allocator_.reset();
  edge_cursor_allocator_.reset();
  return ret;
}

bool GraphEdgeScanCursor::EdgeScanBinding::is_valid() const
{
  return scan_desc_ != nullptr && access_ctdef_ != nullptr && output_ctdef_ != nullptr
      && scan_desc_->ref_table_id_ != OB_INVALID_ID
      && access_ctdef_->ref_table_id_ != OB_INVALID_ID
      && (output_ctdef_ == access_ctdef_
          || output_ctdef_->ref_table_id_ == scan_desc_->ref_table_id_)
      && source_.is_valid() && edge_.is_valid() && target_.is_valid();
}

int64_t GraphEdgeScanCursor::used_memory() const
{
  const int64_t page_bytes = identity_page_allocator_.total();
  const int64_t cursor_bytes = edge_cursor_allocator_.total();
  const int64_t edge_das_used = edge_das_ref_.get_das_mem_used();
  const int64_t edge_das_bytes = edge_das_used > 0 ? edge_das_used : 0;
  const int64_t source_index_bytes = edge_source_index_.used_memory();
  int64_t used = page_bytes;
  const int64_t memory_parts[] = {
      cursor_bytes, edge_das_bytes, source_index_bytes, payload_scratch_allocator_.total()};
  for (int64_t i = 0; i < ARRAYSIZEOF(memory_parts); ++i) {
    if (used > INT64_MAX - memory_parts[i]) {
      used = INT64_MAX;
    } else {
      used += memory_parts[i];
    }
  }
  return used;
}

bool GraphEdgeScanCursor::has_live_edge_scan_resources() const
{
  return edge_scan_active_
      && edge_scan_rtdef_ != nullptr
      && edge_das_ref_.has_task()
      && edge_das_ref_.get_reuse_alloc_total() > 0;
}

bool GraphEdgeScanCursor::edge_scan_resources_released() const
{
  return !edge_scan_active_
      && edge_scan_rtdef_ == nullptr
      && edge_lookup_rtdef_ == nullptr
      && !edge_das_ref_.has_task()
      && edge_das_ref_.get_reuse_alloc_total() == 0;
}

int GraphEdgeScanCursor::reset_edge_scan(int ret, bool retain_allocator_page)
{
  if (edge_das_ref_.has_task()) {
    ret = graph_das_detail::close_das_tasks(edge_das_ref_, ret);
  }
  if (edge_lookup_rtdef_ != nullptr) {
    edge_lookup_rtdef_->~ObDASScanRtDef();
    edge_lookup_rtdef_ = nullptr;
  }
  if (edge_scan_rtdef_ != nullptr) {
    edge_scan_rtdef_->~ObDASScanRtDef();
    edge_scan_rtdef_ = nullptr;
  }
  edge_das_ref_.reuse(retain_allocator_page);
  edge_result_iter_ = DASOpResultIter();
  edge_cursor_allocator_.reuse();
  edge_source_index_.reset();
  edge_cursor_.reset();
  edge_sources_data_ = nullptr;
  edge_source_count_ = 0;
  edge_batch_count_ = 0;
  edge_batch_index_ = 0;
  has_edge_cursor_ = false;
  edge_scan_active_ = false;
  return ret;
}

int GraphEdgeScanCursor::build_edge_source_index(
    const ObIArray<GraphElementIdentity> &sources)
{
  int ret = OB_SUCCESS;
  if (OB_FAIL(edge_source_index_.init(sources.count()))) {
    LOG_WARN("failed to initialize graph edge source index", K(ret),
             "source_count", sources.count());
  }
  for (int64_t i = 0; OB_SUCC(ret) && i < sources.count(); ++i) {
    int64_t entry_index = -1;
    bool inserted = false;
    if (OB_FAIL(edge_source_index_.get_or_insert(
            sources.at(i), entry_index, inserted))) {
      LOG_WARN("failed to index graph edge source", K(ret), K(i));
    } else if (OB_UNLIKELY(!inserted || entry_index != i)) {
      ret = OB_INVALID_ARGUMENT;
      LOG_WARN("duplicate graph edge source", K(ret), K(i), K(entry_index),
               K(inserted));
    }
  }
  return ret;
}

int GraphEdgeScanCursor::init_edge_binding(
    const GraphPathDesc &path_desc,
    const GraphExpandAccessDesc &access_desc,
    const GraphExpandScanDesc &scan_desc,
    EdgeScanBinding &binding)
{
  int ret = OB_SUCCESS;
  EdgeScanBinding candidate;
  const ObTableScanCtDef &tsc_ctdef = scan_desc.get_tsc_ctdef();
  const ObDASScanCtDef *access_ctdef = &tsc_ctdef.scan_ctdef_;
  const ObDASScanCtDef *output_ctdef = tsc_ctdef.lookup_ctdef_ == nullptr
      ? access_ctdef : tsc_ctdef.lookup_ctdef_;
  if (OB_UNLIKELY(!path_desc.is_valid() || !access_desc.is_valid()
                  || scan_desc.ref_table_id_ != access_desc.edge_table_id_
                  || access_ctdef->ref_table_id_
                         != access_desc.edge_access_table_id_)) {
    ret = OB_INVALID_ARGUMENT;
  } else if (OB_ISNULL(output_ctdef)) {
    ret = OB_ERR_UNEXPECTED;
  } else {
    candidate.scan_desc_ = &scan_desc;
    candidate.access_ctdef_ = access_ctdef;
    candidate.output_ctdef_ = output_ctdef;
    if (OB_FAIL(candidate.source_.init(
            *output_ctdef, access_desc.edge_current_columns_, access_desc.source_key_count_))) {
    } else if (OB_FAIL(candidate.edge_.init(
            *output_ctdef, access_desc.edge_key_columns_, access_desc.edge_key_count_))) {
    } else if (OB_FAIL(candidate.target_.init(
            *output_ctdef, access_desc.edge_next_columns_, access_desc.target_key_count_))) {
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

int GraphEdgeScanCursor::check_status()
{
  return exec_ctx_.check_status();
}

int GraphEdgeScanCursor::save_edge_cursor(
    const GraphElementIdentity &identity)
{
  int ret = OB_SUCCESS;
  edge_cursor_allocator_.reuse();
  edge_cursor_.reset();
  has_edge_cursor_ = false;
  if (OB_UNLIKELY(!identity.is_valid())) {
    ret = OB_INVALID_ARGUMENT;
  } else {
    edge_cursor_.graph_id_ = identity.graph_id_;
    edge_cursor_.element_id_ = identity.element_id_;
    if (OB_FAIL(identity.rowkey_.deep_copy(edge_cursor_.rowkey_,
                                           edge_cursor_allocator_))) {
      edge_cursor_.reset();
    } else {
      has_edge_cursor_ = true;
    }
  }
  return ret;
}

int GraphEdgeScanCursor::validate_edge_scan_request(
    const ObIArray<GraphElementIdentity> &sources,
    GraphPathDirection direction,
    const GraphElementIdentity *after_edge,
    int64_t limit) const
{
  int ret = OB_SUCCESS;
  if (OB_UNLIKELY(direction != path_desc_.direction_ || limit <= 0
                  || (can_partition_sources()
                      && sources.count()
                         > GRAPH_EXPAND_MAX_SOURCE_BATCH_SIZE))) {
    ret = OB_INVALID_ARGUMENT;
  }
  // Validate every source when a scan starts. Continuations use the immutable
  // array token below, so pagination remains O(1) in frontier width.
  for (int64_t i = 0;
       OB_SUCC(ret) && after_edge == nullptr && i < sources.count(); ++i) {
    if (!identity_matches(sources.at(i), path_desc_.graph_id_,
                          path_desc_.source_element_id_,
                          edge_binding_.source_.key_count_)) {
      ret = OB_INVALID_ARGUMENT;
    }
  }
  if (OB_SUCC(ret) && after_edge != nullptr
      && !identity_matches(*after_edge, path_desc_.graph_id_,
                           path_desc_.edge_element_id_,
                           edge_binding_.edge_.key_count_)) {
    ret = OB_INVALID_ARGUMENT;
  } else if (OB_SUCC(ret) && after_edge != nullptr
             && (!edge_scan_active_ || !has_edge_cursor_
                 || edge_sources_data_ != sources.get_data()
                 || edge_source_count_ != sources.count()
                 || !(*after_edge == edge_cursor_))) {
    ret = OB_INVALID_ARGUMENT;
  }
  return ret;
}

int GraphEdgeScanCursor::init_edge_scan_rtdefs(bool uses_index_back)
{
  int ret = OB_SUCCESS;
  const GraphExpandScanDesc *scan_desc = edge_binding_.scan_desc_;
  const ObDASScanCtDef *scan_ctdef = edge_binding_.access_ctdef_;
  const ObDASScanCtDef *lookup_ctdef = edge_binding_.output_ctdef_;
  void *scan_buffer = nullptr;
  void *lookup_buffer = nullptr;
  if (OB_ISNULL(scan_desc) || OB_ISNULL(scan_ctdef)
      || OB_ISNULL(lookup_ctdef)) {
    ret = OB_NOT_INIT;
  } else if (OB_ISNULL(scan_buffer = edge_das_ref_.get_das_alloc().alloc(
                           sizeof(ObDASScanRtDef)))) {
    ret = OB_ALLOCATE_MEMORY_FAILED;
  } else {
    edge_scan_rtdef_ = new(scan_buffer) ObDASScanRtDef();
    if (OB_FAIL(graph_das_detail::init_scan_rtdef(exec_ctx_, eval_ctx_, *scan_desc, *scan_ctdef, nullptr,
                                uses_index_back,
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
      if (OB_FAIL(graph_das_detail::init_scan_rtdef(exec_ctx_, eval_ctx_, *scan_desc, *lookup_ctdef, nullptr, true,
                                  edge_das_ref_.get_das_alloc(),
                                  *edge_lookup_rtdef_))) {
      }
    }
  }
  return ret;
}

int GraphEdgeScanCursor::build_adjacency_ranges(
    const ObIArray<GraphElementIdentity> &sources,
    ObIArray<ObNewRange> &ranges)
{
  int ret = OB_SUCCESS;
  const ObDASScanCtDef *scan_ctdef = edge_binding_.access_ctdef_;
  const int64_t prefix_count = edge_binding_.source_.key_count_;
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
                                   sources.at(i).rowkey_.get_obj_ptr()[key],
                                   start_keys[key]))) {
          } else if (OB_FAIL(ob_write_obj(edge_das_ref_.get_das_alloc(),
                                          sources.at(i).rowkey_.get_obj_ptr()[key],
                                          end_keys[key]))) {
          }
        }
        for (int64_t key = prefix_count; key < rowkey_count; ++key) {
          start_keys[key].set_min_value();
          end_keys[key].set_max_value();
        }
        if (OB_SUCC(ret)) {
          ObNewRange range;
          range.table_id_ = access_desc_.edge_access_table_id_;
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

int GraphEdgeScanCursor::attach_edge_lookup(
    ObDASScanOp &scan_op, const DASRuntimeTabletAccess &index_tablet_access)
{
  int ret = OB_SUCCESS;
  DASRuntimeTableAccess *lookup_table_access = edge_lookup_rtdef_ == nullptr
      ? nullptr : edge_lookup_rtdef_->table_access_;
  DASRuntimeTabletAccess *lookup_tablet_access = nullptr;
  if (OB_ISNULL(lookup_table_access) || OB_ISNULL(lookup_table_access->access_meta_)
      || OB_ISNULL(edge_binding_.output_ctdef_)) {
    ret = OB_ERR_UNEXPECTED;
  } else if (OB_ISNULL(lookup_tablet_access = ObDASUtils::get_related_tablet_access(
                           index_tablet_access,
                           lookup_table_access->access_meta_->ref_table_id_))) {
    ret = OB_ERR_UNEXPECTED;
    LOG_WARN("graph edge lookup tablet is missing", K(ret),
             K(index_tablet_access), KPC(lookup_table_access));
  } else if (OB_FAIL(scan_op.reserve_related_buffer(1))) {
  } else if (OB_FAIL(scan_op.set_related_task_info(
                 edge_binding_.output_ctdef_, edge_lookup_rtdef_,
                 lookup_tablet_access->tablet_id_))) {
  } else {
    lookup_table_access->is_reading_ = true;
  }
  return ret;
}

int GraphEdgeScanCursor::start_full_edge_scan(bool &empty)
{
  int ret = OB_SUCCESS;
  bool startup_filtered = false;
  empty = false;
  const GraphExpandScanDesc *scan_desc = edge_binding_.scan_desc_;
  const ObDASScanCtDef *scan_ctdef = edge_binding_.access_ctdef_;
  if (OB_ISNULL(scan_desc) || OB_ISNULL(scan_ctdef)) {
    ret = OB_NOT_INIT;
  } else if (access_desc_.uses_adjacency_index()
             || edge_binding_.output_ctdef_ != scan_ctdef) {
    ret = OB_NOT_SUPPORTED;
  } else if (OB_FAIL(evaluate_filters(eval_ctx_, scan_desc->startup_filters_,
                                      startup_filtered))) {
  } else if (startup_filtered) {
    empty = true;
  } else if (OB_FAIL(init_edge_scan_rtdefs(false))) {
  }
  DASRuntimeTableAccess *table_access = edge_scan_rtdef_ == nullptr
      ? nullptr : edge_scan_rtdef_->table_access_;
  if (OB_SUCC(ret) && !empty
      && (OB_ISNULL(table_access) || table_access->empty())) {
    ret = OB_ERR_UNEXPECTED;
    LOG_WARN("graph edge full scan has no tablet location", K(ret),
             KPC(table_access));
  }
  if (OB_SUCC(ret) && !empty) {
    ObNewRange whole_range;
    whole_range.set_whole_range();
    whole_range.table_id_ = access_desc_.edge_access_table_id_;
    // DASOpResultIter keeps its current task and row iterator between calls,
    // so a page can end in one tablet and resume there before advancing to the
    // next tablet. SQL does not promise a global row order without ORDER BY.
    for (DASRuntimeTabletListIter node = table_access->tablet_accesses_begin();
         OB_SUCC(ret) && node != table_access->tablet_accesses_end(); ++node) {
      ObDASScanOp *scan_op = nullptr;
      if (OB_ISNULL(*node)) {
        ret = OB_ERR_UNEXPECTED;
      } else if (OB_FAIL(edge_das_ref_.prepare_das_task(*node, scan_op))) {
      } else if (OB_ISNULL(scan_op)) {
        ret = OB_ERR_UNEXPECTED;
      } else {
        scan_op->set_scan_ctdef(scan_ctdef);
        scan_op->set_scan_rtdef(edge_scan_rtdef_);
        scan_op->set_can_part_retry(false);
        if (OB_FAIL(scan_op->get_scan_param().key_ranges_.push_back(
                whole_range))) {
        }
      }
    }
    if (OB_SUCC(ret)) {
      table_access->is_reading_ = true;
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

int GraphEdgeScanCursor::start_adjacency_edge_scan(
    const ObIArray<GraphElementIdentity> &sources,
    bool &empty)
{
  int ret = OB_SUCCESS;
  bool startup_filtered = false;
  const GraphExpandScanDesc *scan_desc = edge_binding_.scan_desc_;
  const ObDASScanCtDef *scan_ctdef = edge_binding_.access_ctdef_;
  const bool uses_index_back = edge_binding_.output_ctdef_ != scan_ctdef;
  ObArray<ObNewRange> ranges(
      OB_MALLOC_NORMAL_BLOCK_SIZE,
      ModulePageAllocator(edge_das_ref_.get_das_alloc(), "GraphEdgeRange"));
  empty = false;
  if (OB_ISNULL(scan_desc) || OB_ISNULL(scan_ctdef)) {
    ret = OB_NOT_INIT;
  } else if (OB_UNLIKELY(!access_desc_.uses_adjacency_index())) {
    ret = OB_INVALID_ARGUMENT;
  } else if (OB_FAIL(evaluate_filters(eval_ctx_, scan_desc->startup_filters_,
                                      startup_filtered))) {
  } else if (startup_filtered) {
    empty = true;
  } else if (OB_FAIL(init_edge_scan_rtdefs(uses_index_back))) {
  } else if (OB_FAIL(build_adjacency_ranges(sources, ranges))) {
    LOG_WARN("failed to build graph adjacency ranges", K(ret), K(sources));
  } else if (ranges.empty()) {
    empty = true;
  }
  DASRuntimeTableAccess *table_access = edge_scan_rtdef_ == nullptr
      ? nullptr : edge_scan_rtdef_->table_access_;
  if (OB_SUCC(ret) && !empty
      && (OB_ISNULL(table_access) || table_access->empty())) {
    ret = OB_ERR_UNEXPECTED;
    LOG_WARN("graph adjacency scan has no tablet location", K(ret),
             KPC(table_access));
  }
  if (OB_SUCC(ret) && !empty) {
    // Local secondary indexes have one related index tablet per base-table
    // tablet. Fan the prefix ranges to each local tablet; only the owning
    // tablet can return a row, so this preserves correctness before range
    // routing is optimized.
    for (DASRuntimeTabletListIter node = table_access->tablet_accesses_begin();
         OB_SUCC(ret) && node != table_access->tablet_accesses_end(); ++node) {
      ObDASScanOp *scan_op = nullptr;
      if (OB_ISNULL(*node)) {
        ret = OB_ERR_UNEXPECTED;
      } else if (OB_FAIL(edge_das_ref_.prepare_das_task(*node, scan_op))) {
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
    if (OB_SUCC(ret)) {
      table_access->is_reading_ = true;
    }
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

int GraphEdgeScanCursor::clear_edge_eval_flags(bool vectorized)
{
  int ret = OB_SUCCESS;
  if (OB_ISNULL(edge_scan_rtdef_)
      || OB_ISNULL(edge_scan_rtdef_->p_pd_expr_op_)) {
    ret = OB_ERR_UNEXPECTED;
    LOG_WARN("graph edge scan pushdown operator is missing", K(ret));
  } else {
    if (vectorized) {
      edge_scan_rtdef_->p_pd_expr_op_->clear_evaluated_flag();
    } else {
      edge_scan_rtdef_->p_pd_expr_op_->clear_datum_eval_flag();
    }
  }
  if (OB_SUCC(ret) && edge_lookup_rtdef_ != nullptr) {
    if (OB_ISNULL(edge_lookup_rtdef_->p_pd_expr_op_)) {
      ret = OB_ERR_UNEXPECTED;
      LOG_WARN("graph edge lookup pushdown operator is missing", K(ret));
    } else {
      if (vectorized) {
        edge_lookup_rtdef_->p_pd_expr_op_->clear_evaluated_flag();
      } else {
        edge_lookup_rtdef_->p_pd_expr_op_->clear_datum_eval_flag();
      }
    }
  }
  return ret;
}

int GraphEdgeScanCursor::get_edge_page(
    const ObIArray<GraphElementIdentity> &sources,
    int64_t limit,
    ObIArray<GraphExpandEdge> &edges,
    int64_t &scanned_edges,
    bool &end)
{
  int ret = OB_SUCCESS;
  bool finished = false;
  GraphEdgeRowMaterializer materializer(eval_ctx_, identity_page_allocator_,
                                        payload_scratch_allocator_, path_desc_);
  const GraphExpandScanDesc *scan_desc = edge_binding_.scan_desc_;
  if (OB_UNLIKELY(!edge_scan_active_ || edge_scan_rtdef_ == nullptr
                  || scan_desc == nullptr)) {
    ret = OB_NOT_INIT;
  } else if (OB_ISNULL(edge_scan_rtdef_->p_pd_expr_op_)) {
    ret = OB_ERR_UNEXPECTED;
    LOG_WARN("graph edge scan pushdown operator is missing", K(ret));
  }
  const bool vectorized = OB_SUCC(ret)
      && edge_scan_rtdef_->p_pd_expr_op_->is_vectorized();
  const int64_t batch_capacity = vectorized
      ? edge_scan_rtdef_->p_pd_expr_op_->get_batch_size() : 1;
  while (OB_SUCC(ret) && !finished && edges.count() < limit) {
    if (OB_FAIL(check_status())) {
    } else if (vectorized && edge_batch_index_ >= edge_batch_count_) {
      edge_batch_count_ = 0;
      edge_batch_index_ = 0;
      if (OB_FAIL(clear_edge_eval_flags(true))) {
      } else if (OB_FAIL(edge_result_iter_.get_next_rows(
                     edge_batch_count_, batch_capacity))) {
        if (ret == OB_ITER_END && edge_batch_count_ > 0) {
          ret = OB_SUCCESS;
        } else if (ret == OB_ITER_END) {
          ret = edge_result_iter_.next_result();
          if (ret == OB_ITER_END) {
            ret = OB_SUCCESS;
            finished = true;
          }
        }
      }
    } else if (!vectorized) {
      if (OB_FAIL(clear_edge_eval_flags(false))) {
      } else if (OB_FAIL(edge_result_iter_.get_next_row())) {
        if (ret == OB_ITER_END) {
          ret = edge_result_iter_.next_result();
          if (ret == OB_ITER_END) {
            ret = OB_SUCCESS;
            finished = true;
          }
        }
      } else {
        edge_batch_count_ = 1;
        edge_batch_index_ = 0;
      }
    }
    if (OB_SUCC(ret) && !finished && edge_batch_index_ < edge_batch_count_) {
      ObEvalCtx::BatchInfoScopeGuard batch_guard(eval_ctx_);
      if (vectorized) {
        batch_guard.set_batch_size(edge_batch_count_);
        batch_guard.set_batch_idx(edge_batch_index_);
      }
      // Count every row delivered by DAS before graph SQL filters and the
      // full-scan frontier hash can discard it. This makes the statistic show
      // the actual edge-access work rather than only returned extensions.
      if (scanned_edges < INT64_MAX) {
        ++scanned_edges;
      }
      bool filtered = false;
      bool matches = false;
      bool source_found = false;
      int64_t source_index = -1;
      GraphExpandEdge edge;
      // Pause on the first row of a page, after DAS has returned it while the
      // stateful edge scan is still live. Avoid a debug-sync lookup for every
      // row; the following status check is needed only because this point can
      // wait while another session cancels the query.
      if (scanned_edges == 1) {
        if (OB_UNLIKELY(!has_live_edge_scan_resources())) {
          ret = OB_ERR_UNEXPECTED;
          LOG_WARN("graph edge scan lost its live DAS resources", K(ret),
                   K(scanned_edges), K(edge_scan_active_),
                   KP(edge_scan_rtdef_), "has_task", edge_das_ref_.has_task(),
                   "das_total", edge_das_ref_.get_reuse_alloc_total());
        }
        // Signal even when the invariant above fails, so the mysqltest
        // handshake completes and the query's distinct error exposes it.
        const int sync_ret = DEBUG_SYNC(DAS_GRAPH_EXPAND_AFTER_EDGE_ROW);
        if (OB_SUCCESS != sync_ret) {
          LOG_WARN("graph edge scan debug sync failed", K(sync_ret),
                   K(scanned_edges));
          if (OB_SUCC(ret)) {
            ret = sync_ret;
          }
        }
        if (OB_SUCC(ret) && OB_FAIL(check_status())) {
        }
      }
      if (OB_SUCC(ret)) {
        // This point lets mysqltest verify that a terminal scan error closes
        // live DAS tasks rather than only testing a timeout before access.
        const int simulate_error = EVENT_CALL(
            EventTable::EN_DAS_GRAPH_EXPAND_AFTER_EDGE_ROW);
        if (OB_UNLIKELY(OB_SUCCESS != simulate_error)) {
          ret = simulate_error;
          LOG_WARN("injected graph edge scan error", K(ret), K(scanned_edges));
        } else if (OB_FAIL(evaluate_filters(eval_ctx_, scan_desc->filters_,
                                            filtered))) {
        } else if (!filtered && OB_FAIL(materializer.materialize_edge(
                       edge_binding_.source_, edge_binding_.edge_, edge_binding_.target_, edge, matches))) {
        } else if (!filtered && matches
                   && OB_FAIL(edge_source_index_.find(
                       edge.source_identity_, source_index, source_found))) {
          LOG_WARN("failed to find graph edge frontier source", K(ret), K(edge));
        } else if (!filtered && matches && access_desc_.uses_adjacency_index()
                   && !source_found) {
          ret = OB_ERR_UNEXPECTED;
          LOG_WARN("graph adjacency scan returned a non-frontier edge", K(ret),
                   K(edge), K(sources));
        } else if (!filtered && matches && source_found) {
          // edge_cursor_ is a continuity token for this stateful result
          // iterator, not a global seek key. Tablet task order need not match
          // edge rowkey order, and no output ordering is exposed without an
          // outer ORDER BY.
          if (OB_FAIL(materializer.materialize_payload(*scan_desc, edge.payload_))) {
          } else if (OB_FAIL(edges.push_back(edge))) {
          } else if (OB_FAIL(save_edge_cursor(edge.edge_identity_))) {
          }
        }
      }
      ++edge_batch_index_;
    }
  }
  if (OB_SUCC(ret) && finished) {
    ret = reset_edge_scan(ret, true);
    end = OB_SUCC(ret);
  }
  return ret;
}

int GraphEdgeScanCursor::scan_edges(
    const ObIArray<GraphElementIdentity> &sources,
    GraphPathDirection direction,
    const GraphElementIdentity *after_edge,
    int64_t limit,
    ObIArray<GraphExpandEdge> &edges,
    int64_t &scanned_edges,
    bool &end)
{
  int ret = OB_SUCCESS;
  bool empty = false;
  edges.reset();
  scanned_edges = 0;
  end = false;
  if (OB_UNLIKELY(!initialized_)) {
    ret = OB_NOT_INIT;
  } else if (OB_FAIL(check_status())) {
  } else if (OB_FAIL(validate_edge_scan_request(sources, direction,
                                                after_edge, limit))) {
    LOG_WARN("invalid graph edge scan request", K(ret), K(sources),
             K(direction), KPC(after_edge), K(limit));
  } else {
    // Validation above still sees the previous page's cursor. The caller owns
    // a stable copy in after_edge, so the page arena can now be reused.
    identity_page_allocator_.reuse();
  }
  if (OB_SUCC(ret) && after_edge == nullptr
      && OB_FAIL(reset_edge_scan(OB_SUCCESS, true))) {
    LOG_WARN("failed to reset graph edge scan", K(ret));
  } else if (OB_SUCC(ret) && sources.empty()) {
    end = true;
  } else if (OB_SUCC(ret) && after_edge == nullptr) {
    if (OB_FAIL(build_edge_source_index(sources))) {
      LOG_WARN("failed to build graph edge source index", K(ret));
    } else {
      // GraphExpand keeps this array immutable until the stateful scan ends.
      // Pointer plus count is therefore a constant-time continuation token;
      // avoid rehashing a potentially whole-level frontier for every page.
      edge_sources_data_ = sources.get_data();
      edge_source_count_ = sources.count();
    }
    if (OB_SUCC(ret) && access_desc_.uses_adjacency_index()
        && OB_FAIL(start_adjacency_edge_scan(sources, empty))) {
      LOG_WARN("failed to start graph adjacency scan", K(ret));
    } else if (OB_SUCC(ret) && !access_desc_.uses_adjacency_index()
               && OB_FAIL(start_full_edge_scan(empty))) {
      LOG_WARN("failed to start graph edge full scan", K(ret));
    } else if (empty) {
      end = true;
    }
  }
  if (OB_SUCC(ret) && !end
      && OB_FAIL(get_edge_page(sources, limit, edges, scanned_edges, end))) {
    LOG_WARN("failed to read graph edge page", K(ret));
  }
  if (OB_SUCCESS != ret) {
    ret = reset_edge_scan(ret, false);
    edges.reset();
    end = false;
    if (OB_UNLIKELY(!edge_scan_resources_released())) {
      ret = OB_ERR_UNEXPECTED;
      LOG_WARN("graph edge scan resources remain after terminal cleanup",
               K(ret), K(edge_scan_active_), KP(edge_scan_rtdef_),
               KP(edge_lookup_rtdef_), "has_task", edge_das_ref_.has_task(),
               "das_total", edge_das_ref_.get_reuse_alloc_total());
    }
    // Keep the original scan error. This point only lets mysqltest observe
    // the checked cleanup invariant before the error reaches the outer
    // operator's generic close path.
    const int sync_ret = DEBUG_SYNC(DAS_GRAPH_EXPAND_AFTER_ERROR_CLEANUP);
    if (OB_SUCCESS != sync_ret) {
      LOG_WARN("graph edge scan cleanup debug sync failed", K(sync_ret), K(ret));
    }
  }
  return ret;
}
} // namespace sql
} // namespace oceanbase
