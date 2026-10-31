/*
 * Copyright (c) 2025 OceanBase.
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

#define USING_LOG_PREFIX SQL_DAS
#include "ob_das_text_retrieval_iter.h"
#include "ob_das_scan_iter.h"
#include "sql/das/ob_das_ir_define.h"
#include "sql/engine/expr/ob_expr_bm25.h"

namespace oceanbase
{
namespace sql
{

ObDASTextRetrievalIter::ObDASTextRetrievalIter()
  : ObDASIter(ObDASIterType::DAS_ITER_TEXT_RETRIEVAL),
    mem_context_(nullptr),
    allocator_(lib::ObMemAttr("TextIRIterSelf"), OB_MALLOC_NORMAL_BLOCK_SIZE),
    ir_ctdef_(nullptr),
    ir_rtdef_(nullptr),
    tx_desc_(nullptr),
    snapshot_(nullptr),
    inv_idx_tablet_id_(),
    inv_idx_scan_param_(),
    inv_idx_agg_param_(),
    calc_exprs_(),
    inverted_idx_scan_iter_(nullptr),
    inverted_idx_agg_iter_(nullptr),
    forward_idx_iter_(nullptr),
    fwd_range_objs_(nullptr),
    token_weight_expr_(nullptr),
    skip_(nullptr),
    skip_capacity_(0),
    token_doc_cnt_(0),
    max_batch_size_(0),
    topk_limit_(0),
    need_fwd_idx_agg_(false),
    need_inv_idx_agg_(false),
    inv_idx_agg_evaluated_(false),
    need_inv_idx_agg_reset_(false),
    is_inited_(false)
{
}

int ObDASTextRetrievalIter::set_query_token(const ObString &query_token)
{
  int ret = OB_SUCCESS;
  ObNewRange inv_idx_scan_range;
  if (OB_FAIL(check_inv_idx_scan_and_agg_param())) {
  } else {
    const ExprFixedArray *exprs = &(ir_ctdef_->get_inv_idx_scan_ctdef()->pd_expr_spec_.access_exprs_);
    int64 group_id = 0;
    for (int64_t i = 0; i < exprs->count(); ++i) {
      if (T_PSEUDO_GROUP_ID == exprs->at(i)->type_) {
        group_id = exprs->at(i)->locate_expr_datum(*eval_ctx_).get_int();
      }
    }
    int64_t group_idx = ObNewRange::get_group_idx(group_id);
    if (OB_FAIL(gen_default_inv_idx_scan_range(query_token, inv_idx_scan_range))) {
    } else if (need_inv_idx_agg_ && OB_FAIL(add_agg_rang_key(inv_idx_scan_range))) {
    } else if (FALSE_IT(inv_idx_scan_range.group_idx_ = group_idx)) {
    } else if (OB_FAIL(add_rowkey_range_key(inv_idx_scan_range))) {
    }
  }

  return ret;
}

int ObDASTextRetrievalIter::set_query_token_and_rangekey(const ObString &query_token, const common::ObIArray<ObDocIdExt> &doc_id, const int64_t &batch_size)
{
  int ret = OB_SUCCESS;
  ObNewRange inv_idx_scan_range;
  ObNewRange inv_idx_agg_scan_range;
  if (OB_FAIL(check_inv_idx_scan_and_agg_param())) {
  } else {
    const ExprFixedArray *exprs = &(ir_ctdef_->get_inv_idx_scan_ctdef()->pd_expr_spec_.access_exprs_);
    int64 group_id = 0;
    for (int64_t i = 0; OB_SUCC(ret) && i < exprs->count(); ++i) {
      if (T_PSEUDO_GROUP_ID == exprs->at(i)->type_) {
        group_id = exprs->at(i)->locate_expr_datum(*eval_ctx_).get_int();
      }
    }
    int64_t group_idx = ObNewRange::get_group_idx(group_id);
    for (int64_t i = 0; OB_SUCC(ret) && i < batch_size; ++i) {
      if (OB_FAIL(gen_inv_idx_scan_range(query_token, doc_id.at(i), inv_idx_scan_range))) {
      } else if (FALSE_IT(inv_idx_scan_range.group_idx_ = group_idx)) {
      } else if (OB_FAIL(add_rowkey_range_key(inv_idx_scan_range))) {
      }
    }
    if (OB_SUCC(ret) && need_inv_idx_agg_ && (!inv_idx_agg_evaluated_ || need_inv_idx_agg_reset_)) {
      if (OB_FAIL(gen_default_inv_idx_scan_range(query_token, inv_idx_agg_scan_range))) {
      } else if (OB_FAIL(add_agg_rang_key(inv_idx_agg_scan_range))) {
      }
    }
  }
  return ret;
}

int ObDASTextRetrievalIter::check_inv_idx_scan_and_agg_param()
{
  int ret = OB_SUCCESS;
  if (IS_NOT_INIT) {
    ret = OB_NOT_INIT;
  } else if (OB_UNLIKELY(!inv_idx_scan_param_.key_ranges_.empty())) {
    ret = OB_ERR_UNEXPECTED;
  } else if (need_inv_idx_agg_) {
    if (OB_UNLIKELY(!inv_idx_agg_param_.key_ranges_.empty())) {
      ret = OB_ERR_UNEXPECTED;
    }
  }
  return ret;
}

int ObDASTextRetrievalIter::add_agg_rang_key(const ObNewRange &range)
{
  int ret = OB_SUCCESS;
  if (OB_UNLIKELY(!need_inv_idx_agg_)) {
    ret = OB_ERR_UNEXPECTED;
  } else if (OB_FAIL(inv_idx_agg_param_.key_ranges_.push_back(range))) {
  }
  return ret;
}

int ObDASTextRetrievalIter::add_rowkey_range_key(const ObNewRange &range)
{
  int ret = OB_SUCCESS;
  if (OB_FAIL(inv_idx_scan_param_.key_ranges_.push_back(range))) {
  }
  return ret;
}

int ObDASTextRetrievalIter::inner_init(ObDASIterParam &param)
{
  int ret = OB_SUCCESS;
  if (IS_INIT) {
    ret = OB_INIT_TWICE;
  } else if (OB_UNLIKELY(ObDASIterType::DAS_ITER_TEXT_RETRIEVAL != param.type_)) {
    ret = OB_INVALID_ARGUMENT;
  } else {
    ObDASTextRetrievalIterParam &retrieval_param = static_cast<ObDASTextRetrievalIterParam &>(param);
    inverted_idx_scan_iter_ = static_cast<ObDASScanIter *>(retrieval_param.inv_idx_scan_iter_);
    ir_ctdef_ = retrieval_param.ir_ctdef_;
    ir_rtdef_ = retrieval_param.ir_rtdef_;
    tx_desc_ = retrieval_param.tx_desc_;
    snapshot_ = retrieval_param.snapshot_;
    need_inv_idx_agg_ = ir_ctdef_->need_inv_idx_agg();
    need_inv_idx_agg_reset_ = retrieval_param.need_inv_idx_agg_reset_;
    max_batch_size_ = ir_rtdef_->eval_ctx_->max_batch_size_;
    topk_limit_ = 0;
    if (ir_ctdef_->has_pushdown_topk() && OB_FAIL(init_topk_limit())) {
      // failing to resolve the topk limit is not fatal: it only disables the
      // single-batch promotion below, correctness stays unchanged.
      LOG_WARN("failed to init topk limit for text retrieval iter", K(ret));
      ret = OB_SUCCESS;
    }
  
    if (need_inv_idx_agg_) {
      inverted_idx_agg_iter_ = static_cast<ObDASScanIter *>(retrieval_param.inv_idx_agg_iter_);
    }

    if (OB_ISNULL(mem_context_)) {
      lib::ContextParam param;
      param.set_mem_attr("TextIRIter", ObCtxIds::DEFAULT_CTX_ID);
      if (OB_FAIL(CURRENT_CONTEXT->CREATE_CONTEXT(mem_context_, param))) {
      }
    }

    // The skip vector must cover the vectorized batch size *and* the pushed-down
    // topk limit (limit + offset), because ObDASTRCacheIter::inner_reuse() resets
    // the vector to OB_MAX(max_batch_size_, topk_limit_). topk_limit_ is user
    // controlled and may be far larger than max_batch_size_, so allocating only
    // max_batch_size_ bits made that reset() a heap buffer overflow.
    const int64_t default_size = OB_MAX(OB_MAX(max_batch_size_, topk_limit_), 1);
    if (OB_FAIL(ret)) {
    } else if (OB_FAIL(init_calc_exprs())) {
      LOG_WARN("failed to init row-wise calc exprs", K(ret));
    } else if (OB_FAIL(prepare_skip_vector(default_size))) {
      LOG_WARN("failed to init skip bit vector", K(ret), K(default_size));
    } else {
      is_inited_ = true;
    }
  }
  return ret;
}

int ObDASTextRetrievalIter::prepare_skip_vector(const int64_t size)
{
  int ret = OB_SUCCESS;
  if (OB_UNLIKELY(size <= 0)) {
    ret = OB_INVALID_ARGUMENT;
    LOG_WARN("invalid skip vector size", K(ret), K(size));
  } else if (OB_NOT_NULL(skip_) && skip_capacity_ >= size) {
    // reuse the existing allocation: declaring a smaller size is always safe
    skip_->reset(size);
  } else {
    sql::ObBitVector *new_skip = to_bit_vector(allocator_.alloc(ObBitVector::memory_size(size)));
    if (OB_ISNULL(new_skip)) {
      ret = OB_ALLOCATE_MEMORY_FAILED;
      LOG_WARN("failed to allocate skip bit vector", K(ret), K(size));
    } else {
      new_skip->init(size);
      skip_ = new_skip;
      skip_capacity_ = size;
    }
  }
  return ret;
}

int ObDASTextRetrievalIter::inner_reuse()
{
  int ret = OB_SUCCESS;
  if (nullptr != mem_context_) {
    mem_context_->reset_remain_one_page();
  }

  const int64_t old_max_batch_size = max_batch_size_;
  max_batch_size_ = ir_rtdef_->eval_ctx_->max_batch_size_;
  if (old_max_batch_size < max_batch_size_) {
    // The batch size grew, so the arena is rebuilt from scratch: drop the cached
    // skip vector metadata together with its memory and reallocate below.
    allocator_.reuse();
    skip_ = nullptr;
    skip_capacity_ = 0;
  }
  if (OB_FAIL(ret)) {
  } else if (OB_FAIL(prepare_skip_vector(OB_MAX(OB_MAX(max_batch_size_, topk_limit_), 1)))) {
    LOG_WARN("failed to reuse skip bit vector", K(ret), K(max_batch_size_), K(topk_limit_));
  }
  if (OB_SUCC(ret)) {
    const ObTabletID &old_inv_scan_id = inv_idx_scan_param_.tablet_id_;
    inverted_idx_scan_iter_->set_scan_param(inv_idx_scan_param_);
    inv_idx_scan_param_.need_switch_param_ = inv_idx_scan_param_.need_switch_param_ ||
      ((old_inv_scan_id.is_valid() && old_inv_scan_id != inv_idx_tablet_id_) ? true : false);
    if (!inv_idx_scan_param_.key_ranges_.empty()) {
      inv_idx_scan_param_.key_ranges_.reuse();
    } 
  }
  if (FAILEDx(inverted_idx_scan_iter_->reuse())) {
  } else {
    if (need_inv_idx_agg_) {
      const ObTabletID &old_inv_agg_id = inv_idx_agg_param_.tablet_id_;
      inverted_idx_agg_iter_->set_scan_param(inv_idx_agg_param_);
      inv_idx_agg_param_.need_switch_param_ = inv_idx_agg_param_.need_switch_param_ ||
        ((old_inv_agg_id.is_valid() && old_inv_agg_id != inv_idx_tablet_id_) ? true : false);
      if (!inv_idx_agg_param_.key_ranges_.empty()) {
        inv_idx_agg_param_.key_ranges_.reuse();
      }
      if (!inv_idx_agg_evaluated_ ||
          need_inv_idx_agg_reset_ ||
          inv_idx_agg_param_.need_switch_param_) {
        if (OB_FAIL(inverted_idx_agg_iter_->reuse())) {
        }
        inv_idx_agg_evaluated_ = false;
        token_doc_cnt_ = 0;
      }
    } else {
      inv_idx_agg_evaluated_ = false;
      token_doc_cnt_ = 0;
    }

  }
  return ret;
}

int ObDASTextRetrievalIter::inner_release()
{
  int ret = OB_SUCCESS;

  inv_idx_scan_param_.destroy_schema_guard();
  inv_idx_scan_param_.snapshot_.reset();
  inv_idx_scan_param_.destroy();
  inv_idx_agg_param_.destroy_schema_guard();
  inv_idx_agg_param_.snapshot_.reset();
  inv_idx_agg_param_.destroy();
  calc_exprs_.reset();
  if (nullptr != mem_context_)  {
    mem_context_->reset_remain_one_page();
    DESTROY_CONTEXT(mem_context_);
    mem_context_ = nullptr;
  }
  allocator_.reset();
  ir_ctdef_ = nullptr;
  ir_rtdef_ = nullptr;
  inverted_idx_scan_iter_ = nullptr;
  inverted_idx_agg_iter_ = nullptr;
  forward_idx_iter_ = nullptr;
  fwd_range_objs_ = nullptr;
  skip_ = nullptr;
  skip_capacity_ = 0;
  tx_desc_ = nullptr;
  snapshot_ = nullptr;
  token_doc_cnt_ = 0;
  max_batch_size_ = 0;
  need_inv_idx_agg_ = false;
  inv_idx_agg_evaluated_ = false;
  need_inv_idx_agg_reset_ = false;
  is_inited_ = false;
  return ret;
}

int ObDASTextRetrievalIter::do_table_scan()
{
  int ret = OB_SUCCESS;
  inverted_idx_scan_iter_->set_scan_param(inv_idx_scan_param_);
  if (need_inv_idx_agg_) {
    inverted_idx_agg_iter_->set_scan_param(inv_idx_agg_param_);
  }

  if (OB_FAIL(init_inv_idx_scan_param())) {
  } else if (OB_FAIL(inverted_idx_scan_iter_->do_table_scan())) {
  } else if (need_inv_idx_agg_ && OB_FAIL(inverted_idx_agg_iter_->do_table_scan())) {
  }
  return ret;
}

int ObDASTextRetrievalIter::rescan()
{
  int ret = OB_SUCCESS;

  inv_idx_scan_param_.tablet_id_ = inv_idx_tablet_id_;
  if (need_inv_idx_agg_) {
    inv_idx_agg_param_.tablet_id_ = inv_idx_tablet_id_;
  }
  if (OB_FAIL(inverted_idx_scan_iter_->rescan())) {
  } else if (need_inv_idx_agg_ && 
             !inv_idx_agg_evaluated_ &&
             OB_FAIL(inverted_idx_agg_iter_->rescan())) {
  } else {
    int64_t cnt = inv_idx_scan_param_.output_exprs_->count();
    // reset expr datums for table_scan
    for (int64_t i = 0; OB_SUCC(ret) && i < cnt; ++i) {
      inv_idx_scan_param_.output_exprs_->at(i)->locate_datums_for_update(*ir_rtdef_->get_inv_idx_scan_rtdef()->eval_ctx_, max_batch_size_ ? max_batch_size_ : 1);
    }
  }
  return ret;
}

int ObDASTextRetrievalIter::inner_get_next_row()
{
  int ret = OB_SUCCESS;
  if (IS_NOT_INIT) {
    ret = OB_NOT_INIT;
  } else if (!inv_idx_agg_evaluated_ && need_inv_idx_agg_) {
    if (OB_FAIL(do_doc_cnt_agg())) {
      if (OB_UNLIKELY(OB_ITER_END != ret)) {
      }
    } else {
      inv_idx_agg_evaluated_ = true;
    }
  }

  if (OB_FAIL(ret)) {
  } else if (OB_FAIL(inverted_idx_scan_iter_->get_next_row())) {
    if (OB_UNLIKELY(OB_ITER_END != ret)) {
    }
  } else {
    LOG_DEBUG("get one invert index scan row", "row",
        ROWEXPR2STR(*ir_rtdef_->get_inv_idx_scan_rtdef()->eval_ctx_,
        *inv_idx_scan_param_.output_exprs_));
    {
      sql::ObEvalCtx *ctx = ir_rtdef_->get_inv_idx_scan_rtdef()->eval_ctx_;
      sql::ObEvalCtx *outer_ctx = ir_rtdef_->eval_ctx_;
      sql::ObExpr *doc_id_expr = ir_ctdef_->inv_scan_domain_id_col_;
    }
    if (OB_FAIL(copy_doc_id_to_outer_ctx())) {
      LOG_WARN("failed to copy doc id to outer ctx", K(ret));
    } else if (ir_ctdef_->need_calc_relevance()) {
      clear_row_wise_evaluated_flag();
      if (OB_FAIL(fill_token_doc_cnt())) {
      } else if (OB_FAIL(fill_token_weight())) {
      } else if (OB_FAIL(project_relevance_expr())) {
      }
    }
  }
  return ret;
}

int ObDASTextRetrievalIter::init_topk_limit()
{
  int ret = OB_SUCCESS;
  topk_limit_ = 0;
  ObExpr *topk_limit_expr = ir_ctdef_->topk_limit_expr_;
  ObExpr *topk_offset_expr = ir_ctdef_->topk_offset_expr_;
  if (OB_ISNULL(topk_limit_expr)) {
    ret = OB_INVALID_ARGUMENT;
    LOG_WARN("null topk limit expr while pushdown topk", K(ret));
  } else {
    int64_t limit = 0;
    int64_t offset = 0;
    ObDatum *topk_limit_datum = nullptr;
    ObDatum *topk_offset_datum = nullptr;
    if (OB_FAIL(topk_limit_expr->eval(*ir_rtdef_->eval_ctx_, topk_limit_datum))) {
      LOG_WARN("failed to eval topk limit expr", K(ret));
    } else if (nullptr != topk_offset_expr && OB_FAIL(topk_offset_expr->eval(*ir_rtdef_->eval_ctx_, topk_offset_datum))) {
      LOG_WARN("failed to eval topk offset expr", K(ret));
    } else {
      limit = (topk_limit_datum->is_null() || topk_limit_datum->get_int() < 0) ? 0 : topk_limit_datum->get_int();
      if (nullptr != topk_offset_datum) {
        offset = (topk_offset_datum->is_null() || topk_offset_datum->get_int() < 0) ? 0 : topk_offset_datum->get_int();
      }
      topk_limit_ = limit + offset;
    }
  }
  return ret;
}

int ObDASTextRetrievalIter::inner_get_next_rows(int64_t &count, int64_t capacity)
{
  int ret = OB_SUCCESS;
  if (IS_NOT_INIT) {
    ret = OB_NOT_INIT;
  } else if (!inv_idx_agg_evaluated_ && need_inv_idx_agg_) {
    if (OB_FAIL(do_doc_cnt_agg())) {
      if (OB_UNLIKELY(OB_ITER_END != ret)) {
      } else {
        inv_idx_agg_evaluated_ = true;
      }
    } else {
      inv_idx_agg_evaluated_ = true;
    }
  }
  count = 0;
  int64_t real_capacity = OB_MIN(max_batch_size_, capacity);
  if (topk_limit_ > 0) {
    // topk is pushed down to the inverted index (BMW/DAAT): those iters
    // overwrite the same datum slots starting from 0 on every batch, so a
    // multi-batch output with capacity < total results would corrupt the
    // previously emitted rows (ORDER BY score() LIMIT k mismatch for k>=5).
    // Force a single batch covering the full topk (clamped by max_batch_size_).
    real_capacity = OB_MIN(max_batch_size_, OB_MAX(real_capacity, topk_limit_));
  }
  if (OB_FAIL(ret)) {
  } else if (max_batch_size_ == 0) {
    ret = OB_ERR_UNEXPECTED;
  } else if (OB_FAIL(inverted_idx_scan_iter_->get_next_rows(count, real_capacity))) {
    if (OB_UNLIKELY(OB_ITER_END != ret)) {
    } else if (count != 0) {
      ret = OB_SUCCESS;
    }
  }

  if (OB_SUCC(ret)) {
    if (OB_FAIL(copy_doc_id_to_outer_ctx_batch(count))) {
      LOG_WARN("failed to copy doc id to outer ctx batch", K(ret));
    } else if (ir_ctdef_->need_calc_relevance()) {
      sql::ObEvalCtx *ctx = ir_rtdef_->get_inv_idx_scan_rtdef()->eval_ctx_;
      const ObBitVector *skip = NULL;
      PRINT_VECTORIZED_ROWS(SQL, DEBUG, *ctx, *inv_idx_scan_param_.output_exprs_, count, skip);
      clear_batch_wise_evaluated_flag(count);
      if (OB_FAIL(fill_token_doc_cnt())) {
      } else if (OB_FAIL(fill_token_weight())) {
      } else if (OB_FAIL(batch_project_relevance_expr(count))) {
      }
    }
  }
  return ret;
}

int ObDASTextRetrievalIter::init_inv_idx_scan_param()
{
  int ret = OB_SUCCESS;
  if (OB_FAIL(init_base_idx_scan_param(
      inv_idx_tablet_id_,
      ir_ctdef_->get_inv_idx_scan_ctdef(),
      ir_rtdef_->get_inv_idx_scan_rtdef(),
      tx_desc_,
      snapshot_,
      inv_idx_scan_param_))) {
  } else if (need_inv_idx_agg_) {
    if (OB_FAIL(init_base_idx_scan_param(
        inv_idx_tablet_id_,
        ir_ctdef_->get_inv_idx_agg_ctdef(),
        ir_rtdef_->get_inv_idx_agg_rtdef(),
        tx_desc_,
        snapshot_,
        inv_idx_agg_param_))) {
    } else {
      // for some cases, the default scan_order_ may be the 'Reverse'.
      inv_idx_scan_param_.scan_flag_.scan_order_ = ObQueryFlag::Forward;

      if (OB_UNLIKELY(!static_cast<sql::ObStoragePushdownFlag>(
          ir_ctdef_->get_inv_idx_agg_ctdef()->pd_expr_spec_.pd_storage_flag_).is_aggregate_pushdown())) {
        ret = OB_NOT_IMPLEMENT;
        LOG_ERROR("not pushdown aggregate not supported", K(ret), KPC(ir_ctdef_->get_inv_idx_agg_ctdef()));
      }
    }
  }

  return ret;
}

int ObDASTextRetrievalIter::init_base_idx_scan_param(
    const common::ObTabletID &tablet_id,
    const sql::ObDASScanCtDef *ctdef,
    sql::ObDASScanRtDef *rtdef,
    transaction::ObTxDesc *tx_desc,
    transaction::ObTxReadSnapshot *snapshot,
    ObTableScanParam &scan_param)
{
  int ret = OB_SUCCESS;
  if (OB_ISNULL(ctdef) || OB_ISNULL(rtdef)) {
    ret = OB_INVALID_ARGUMENT;
  } else {
    
    
    scan_param.key_ranges_.set_attr(ObMemAttr("ScanParamKR"));
    scan_param.tx_lock_timeout_ = rtdef->tx_lock_timeout_;
    scan_param.index_id_ = ctdef->ref_table_id_;
    scan_param.is_get_ = false; // scan
    scan_param.is_for_foreign_check_ = false;
    scan_param.timeout_ = rtdef->timeout_ts_;
    scan_param.scan_flag_ = rtdef->scan_flag_;
    scan_param.reserved_cell_count_ = ctdef->access_column_ids_.count();
    scan_param.allocator_ = &rtdef->stmt_allocator_;
    scan_param.scan_allocator_ = &rtdef->scan_allocator_;
    scan_param.sql_mode_ = rtdef->sql_mode_;
    scan_param.frozen_version_ = rtdef->frozen_version_;
    scan_param.output_exprs_ = &(ctdef->pd_expr_spec_.access_exprs_);
    scan_param.calc_exprs_ = &(ctdef->pd_expr_spec_.calc_exprs_);
    scan_param.aggregate_exprs_ = &(ctdef->pd_expr_spec_.pd_storage_aggregate_output_);
    scan_param.table_param_ = &(ctdef->table_param_);
    scan_param.op_ = rtdef->p_pd_expr_op_;
    scan_param.row2exprs_projector_ = rtdef->p_row2exprs_projector_;
    scan_param.schema_version_ = ctdef->schema_version_;
    scan_param.runtime_schema_version_ = rtdef->runtime_schema_version_;
    scan_param.limit_param_ = rtdef->limit_param_;
    scan_param.need_scn_ = rtdef->need_scn_;
    scan_param.pd_storage_flag_ = ctdef->pd_expr_spec_.pd_storage_flag_.pd_flag_;
    scan_param.fb_snapshot_ = rtdef->fb_snapshot_;
    scan_param.tablet_id_ = tablet_id;
    if (!ctdef->pd_expr_spec_.pushdown_filters_.empty()) {
      scan_param.op_filters_ = &ctdef->pd_expr_spec_.pushdown_filters_;
    }
    scan_param.pd_storage_filters_ = rtdef->p_pd_expr_op_->pd_storage_filters_;
    if (OB_NOT_NULL(tx_desc)) {
      scan_param.tx_id_ = data_plane::tx_desc_id(tx_desc);
    } else {
      scan_param.tx_id_.reset();
    }

    if (OB_NOT_NULL(snapshot)) {
      if (OB_FAIL(scan_param.snapshot_.assign(*snapshot))) {
      }
    } else {
      ret = OB_ERR_UNEXPECTED;
      LOG_ERROR("null snapshot", K(ret), KP(snapshot));
    }

    if (FAILEDx(scan_param.column_ids_.assign(ctdef->access_column_ids_))) {
    }
  }
  return ret;
}

int ObDASTextRetrievalIter::do_doc_cnt_agg()
{
  int ret = OB_SUCCESS;
  if (OB_UNLIKELY(!static_cast<sql::ObStoragePushdownFlag>(inv_idx_agg_param_.pd_storage_flag_).is_aggregate_pushdown())) {
    ret = OB_NOT_SUPPORTED;
    LOG_ERROR("aggregate without pushdown not supported", K(ret));
  } else if (OB_FAIL(get_next_single_row(inv_idx_agg_param_.op_->is_vectorized(), inverted_idx_agg_iter_))) {
    if (OB_UNLIKELY(OB_ITER_END != ret)) {
    }
  } else {
    const sql::ObExpr *inv_idx_agg_expr = inv_idx_agg_param_.aggregate_exprs_->at(0);
    sql::ObEvalCtx *eval_ctx = ir_rtdef_->get_inv_idx_agg_rtdef()->eval_ctx_;
    ObDatum *doc_cnt_datum = nullptr;
    ObEvalCtx::BatchInfoScopeGuard guard(*eval_ctx);
    guard.set_batch_idx(0);
    if (OB_FAIL(inv_idx_agg_expr->eval(*eval_ctx, doc_cnt_datum))) {
    } else {
      token_doc_cnt_ = doc_cnt_datum->get_int();
    }
  }
  return ret;
}

int ObDASTextRetrievalIter::gen_default_inv_idx_scan_range(const ObString &query_token, ObNewRange &scan_range)
{
  int ret = OB_SUCCESS;
  void *buf = nullptr;
  ObObj *obj_ptr = nullptr;
  common::ObArenaAllocator &ctx_alloc = mem_context_->get_arena_allocator();
  constexpr int64_t obj_cnt = INV_IDX_ROWKEY_COL_CNT * 2;
  ObObj tmp_obj;
  tmp_obj.set_string(ObVarcharType, query_token);
  // We need to ensure collation type / level between query text and token column is compatible
  tmp_obj.set_meta_type(ir_ctdef_->search_text_->obj_meta_);

  if (OB_ISNULL(buf = ctx_alloc.alloc(sizeof(ObObj) * obj_cnt))) {
    ret = OB_ALLOCATE_MEMORY_FAILED;
  } else if (OB_ISNULL(obj_ptr = new (buf) ObObj[obj_cnt])) {
    ret = OB_ERR_UNEXPECTED;
  } else if (OB_FAIL(ob_write_obj(ctx_alloc, tmp_obj, obj_ptr[0]))) {
  } else if (OB_FAIL(ob_write_obj(ctx_alloc, tmp_obj, obj_ptr[2]))) {
  } else {
    obj_ptr[1].set_min_value();
    obj_ptr[3].set_max_value();
    ObRowkey start_key(obj_ptr, INV_IDX_ROWKEY_COL_CNT);
    ObRowkey end_key(&obj_ptr[2], INV_IDX_ROWKEY_COL_CNT);
    common::ObTableID inv_table_id = ir_ctdef_->get_inv_idx_scan_ctdef()->ref_table_id_;
    scan_range.table_id_ = inv_table_id;
    scan_range.start_key_.assign(obj_ptr, INV_IDX_ROWKEY_COL_CNT);
    scan_range.end_key_.assign(&obj_ptr[2], INV_IDX_ROWKEY_COL_CNT);
    scan_range.border_flag_.set_inclusive_start();
    scan_range.border_flag_.set_inclusive_end();
  }
  return ret;
}

int ObDASTextRetrievalIter::gen_inv_idx_scan_range(const ObString &query_token, const ObDocIdExt &doc_id, ObNewRange &scan_range)
{
  int ret = OB_SUCCESS;
  void *buf = nullptr;
  ObObj *obj_ptr = nullptr;
  common::ObArenaAllocator &ctx_alloc = mem_context_->get_arena_allocator();
  constexpr int64_t obj_cnt = INV_IDX_ROWKEY_COL_CNT;
  ObObj tmp_obj;
  tmp_obj.set_string(ObVarcharType, query_token);
  // We need to ensure collation type / level between query text and token column is compatible
  tmp_obj.set_meta_type(ir_ctdef_->search_text_->obj_meta_);

  if (OB_ISNULL(buf = ctx_alloc.alloc(sizeof(ObObj) * obj_cnt))) {
    ret = OB_ALLOCATE_MEMORY_FAILED;
  } else if (OB_ISNULL(obj_ptr = new (buf) ObObj[obj_cnt])) {
    ret = OB_ERR_UNEXPECTED;
  } else if (OB_FAIL(ob_write_obj(ctx_alloc, tmp_obj, obj_ptr[0]))) {
  } else if (OB_FAIL(doc_id.get_datum().to_obj(obj_ptr[1], ir_ctdef_->inv_scan_domain_id_col_->obj_meta_))) {
  } else {
    ObRowkey row_key(obj_ptr, obj_cnt);
    common::ObTableID inv_table_id = ir_ctdef_->get_inv_idx_scan_ctdef()->ref_table_id_;
    if (OB_FAIL(scan_range.build_range(inv_table_id, row_key))) {
    }
  }
  return ret;
}

int ObDASTextRetrievalIter::init_calc_exprs()
{
  int ret = OB_SUCCESS;
  if (ir_ctdef_->need_calc_relevance()) {
    sql::ObExpr *relevance_expr = ir_ctdef_->relevance_expr_;
    if (OB_ISNULL(relevance_expr)) {
      ret = OB_ERR_UNEXPECTED;
    } else if (OB_FAIL(calc_exprs_.push_back(relevance_expr))) {
    }
    for (int64_t i = 0; OB_SUCC(ret) && i < relevance_expr->arg_cnt_; ++i) {
      sql::ObExpr *arg_expr = relevance_expr->args_[i];
      if (OB_ISNULL(arg_expr)) {
        ret = OB_ERR_UNEXPECTED;
      } else if (T_FUN_SYS_CAST == arg_expr->type_) {
        // cast expr is evaluated with relevance expr
        if (OB_FAIL(calc_exprs_.push_back(arg_expr))) {
        }
      }
    }

    if (OB_SUCC(ret)) {
      sql::ObExpr *doc_length_param_expr = relevance_expr->args_[sql::ObExprBM25::DOC_LENGTH_PARAM_IDX];
      if (OB_UNLIKELY(nullptr == doc_length_param_expr || doc_length_param_expr->type_ != T_REF_COLUMN ||
                      doc_length_param_expr != ir_ctdef_->inv_scan_doc_length_col_ ||
                      doc_length_param_expr->datum_meta_.get_type() != ObUInt64Type)) {
        ret = OB_ERR_UNEXPECTED;
      }
    }
    if (OB_SUCC(ret)) {
      sql::ObExpr *token_weight_param_expr = relevance_expr->args_[sql::ObExprBM25::TOKEN_WEIGHT_PARAM_IDX];
      if (OB_UNLIKELY(nullptr == token_weight_param_expr || token_weight_param_expr->type_ != T_PSEUDO_COLUMN)) {
        ret = OB_ERR_UNEXPECTED;
      } else {
        token_weight_expr_ = token_weight_param_expr;
      }
    }
  }
  return ret;
}

void ObDASTextRetrievalIter::clear_row_wise_evaluated_flag()
{
  sql::ObEvalCtx *eval_ctx = ir_rtdef_->eval_ctx_;
  for (int64_t i = 0; i < calc_exprs_.count(); ++i) {
    sql::ObExpr *expr = calc_exprs_.at(i);
    if (expr->is_batch_result()) {
      expr->get_evaluated_flags(*eval_ctx).unset(eval_ctx->get_batch_idx());
    } else {
      expr->get_eval_info(*eval_ctx).clear_evaluated_flag();
    }
  }
}

void ObDASTextRetrievalIter::clear_batch_wise_evaluated_flag(const int64_t &count)
{
  sql::ObEvalCtx *eval_ctx = ir_rtdef_->eval_ctx_;
  int64_t max_size = OB_MAX(eval_ctx->max_batch_size_, count);
  for (int64_t i = 0; i < calc_exprs_.count(); ++i) {
    sql::ObExpr *expr = calc_exprs_.at(i);
    expr->get_evaluated_flags(*eval_ctx).reset(count);
  }
}

int ObDASTextRetrievalIter::fill_token_doc_cnt()
{
  int ret = OB_SUCCESS;
  const sql::ObExpr *inv_idx_agg_expr = inv_idx_agg_param_.aggregate_exprs_->at(0);
  sql::ObEvalCtx *eval_ctx = ir_rtdef_->get_inv_idx_agg_rtdef()->eval_ctx_;
  if (OB_ISNULL(inv_idx_agg_expr) || OB_ISNULL(eval_ctx)
      || OB_UNLIKELY(inv_idx_agg_expr->datum_meta_.get_type() != ObIntType)) {
    ret = OB_ERR_UNEXPECTED;
  } else {
    ObEvalCtx::BatchInfoScopeGuard guard(*eval_ctx);
    guard.set_batch_idx(0);
    ObDatum &doc_cnt_datum = inv_idx_agg_expr->locate_datum_for_write(*eval_ctx);
    doc_cnt_datum.set_int(token_doc_cnt_);
  }
  return ret;
}

int ObDASTextRetrievalIter::fill_token_weight()
{
  int ret = OB_SUCCESS;
  sql::ObExpr *total_doc_cnt_expr =
      ir_ctdef_->relevance_expr_->args_[sql::ObExprBM25::TOTAL_DOC_CNT_PARAM_IDX];
  if (OB_ISNULL(token_weight_expr_) || OB_ISNULL(total_doc_cnt_expr)) {
    ret = OB_ERR_UNEXPECTED;
  } else {
    const int64_t total_doc_cnt =
        total_doc_cnt_expr->locate_expr_datum(*ir_rtdef_->eval_ctx_, 0).get_int();
    ObEvalCtx::BatchInfoScopeGuard guard(*ir_rtdef_->eval_ctx_);
    guard.set_batch_idx(0);
    ObDatum &token_weight_datum =
        token_weight_expr_->locate_datum_for_write(*ir_rtdef_->eval_ctx_);
    token_weight_datum.set_double(
        sql::ObExprBM25::query_token_weight(token_doc_cnt_, total_doc_cnt));
  }
  return ret;
}

int ObDASTextRetrievalIter::project_relevance_expr()
{
  int ret = OB_SUCCESS;
  sql::ObExpr *relevance_expr = ir_ctdef_->relevance_expr_;
  ObDatum *relevance_datum = nullptr;
  if (OB_ISNULL(relevance_expr)) {
    ret = OB_INVALID_ARGUMENT;
  } else if (OB_FAIL(relevance_expr->eval(*ir_rtdef_->eval_ctx_, relevance_datum))) {
  }
  return ret;
}

int ObDASTextRetrievalIter::batch_project_relevance_expr(const int64_t &count)
{
  int ret = OB_SUCCESS;
  sql::ObExpr *relevance_expr = ir_ctdef_->relevance_expr_;
  ObDatum *relevance_datum = nullptr;
  if (OB_ISNULL(relevance_expr)) {
    ret = OB_INVALID_ARGUMENT;
  } else if (OB_FAIL(relevance_expr->eval_batch(*ir_rtdef_->eval_ctx_, *skip_, count))) {
  }
  return ret;
}

int ObDASTextRetrievalIter::copy_doc_id_to_outer_ctx()
{
  int ret = OB_SUCCESS;
  sql::ObExpr *doc_id_expr = ir_ctdef_->inv_scan_domain_id_col_;
  sql::ObEvalCtx *inner_ctx = ir_rtdef_->get_inv_idx_scan_rtdef()->eval_ctx_;
  sql::ObEvalCtx *outer_ctx = ir_rtdef_->eval_ctx_;
  if (OB_ISNULL(doc_id_expr) || OB_ISNULL(inner_ctx) || OB_ISNULL(outer_ctx)) {
    ret = OB_INVALID_ARGUMENT;
    LOG_WARN("invalid doc id copy arg", K(ret), KP(doc_id_expr), KP(inner_ctx), KP(outer_ctx));
  } else {
    // The inv idx scan writes the doc id column into the inner ctx; downstream
    // readers (storage token iters, loser-tree fillers) read it from the outer
    // ctx. Shallow copy here is safe: datum memory lives in the scan row buffer
    // of the current inverted index iteration, which stays valid until the next
    // get_next_row/get_next_rows, and consumers deep-copy immediately.
    ObDatum &outer_datum = doc_id_expr->locate_datum_for_write(*outer_ctx);
    const ObDatum &inner_datum = doc_id_expr->locate_expr_datum(*inner_ctx);
    outer_datum = inner_datum;
  }
  return ret;
}

int ObDASTextRetrievalIter::copy_doc_id_to_outer_ctx_batch(const int64_t &count)
{
  int ret = OB_SUCCESS;
  sql::ObExpr *doc_id_expr = ir_ctdef_->inv_scan_domain_id_col_;
  sql::ObEvalCtx *inner_ctx = ir_rtdef_->get_inv_idx_scan_rtdef()->eval_ctx_;
  sql::ObEvalCtx *outer_ctx = ir_rtdef_->eval_ctx_;
  if (OB_ISNULL(doc_id_expr) || OB_ISNULL(inner_ctx) || OB_ISNULL(outer_ctx)) {
    ret = OB_INVALID_ARGUMENT;
    LOG_WARN("invalid doc id copy arg", K(ret), KP(doc_id_expr), KP(inner_ctx), KP(outer_ctx));
  } else {
    ObDatum *outer_datums = doc_id_expr->locate_datums_for_update(*outer_ctx, count);
    const ObDatumVector &inner_datums = doc_id_expr->locate_expr_datumvector(*inner_ctx);
    for (int64_t i = 0; OB_SUCC(ret) && i < count; ++i) {
      outer_datums[i] = *inner_datums.at(i);
    }
  }
  return ret;
}

ObDASTRCacheIter::ObDASTRCacheIter()
  : ObDASTextRetrievalIter(),
    cur_idx_(-1),
    count_(0),
    total_cnt_(0),
    relevance_(),
    doc_id_(),
    cache_iter_end_(false)
{}

int ObDASTRCacheIter::inner_init(ObDASIterParam &param)
{
  int ret = OB_SUCCESS;
  if (OB_FAIL(ObDASTextRetrievalIter::inner_init(param))) {
  } else {
    is_inited_ = false;
    const int64_t default_size = OB_MAX(OB_MAX(max_batch_size_, 1), topk_limit_);
    relevance_.set_allocator(&allocator_);
    if (OB_FAIL(relevance_.init(default_size))) {
    } else if (OB_FAIL(relevance_.prepare_allocate(default_size))) {
    } else if (FALSE_IT(doc_id_.set_allocator(&allocator_))) {
    } else if (OB_FAIL(doc_id_.init(default_size))) {
    } else if (OB_FAIL(doc_id_.prepare_allocate(default_size))) {
    } else {
      is_inited_ = true;
    }
  }
  return ret;
}

int ObDASTRCacheIter::inner_reuse()
{
  int ret = OB_SUCCESS;
  int64_t old_default_size = OB_MAX(OB_MAX(max_batch_size_, 1), topk_limit_);
  max_batch_size_ = ir_rtdef_->eval_ctx_->max_batch_size_;
  const int64_t default_size = OB_MAX(OB_MAX(max_batch_size_, 1), topk_limit_);
  if (old_default_size < default_size) {
    relevance_.reuse();
    doc_id_.reuse();
    allocator_.reuse();
    // the whole arena is gone, so the cached skip vector is dangling now
    skip_ = nullptr;
    skip_capacity_ = 0;
    if (OB_FAIL(relevance_.init(default_size))) {
    } else if (OB_FAIL(relevance_.prepare_allocate(default_size))) {
    } else if (OB_FAIL(doc_id_.init(default_size))) {
    } else if (OB_FAIL(doc_id_.prepare_allocate(default_size))) {
      LOG_WARN("failed to prepare allocate doc_id_ array", K(ret));
    } else if (OB_FAIL(prepare_skip_vector(default_size))) {
      LOG_WARN("failed to init skip bit vector", K(ret), K(default_size));
    }
  } else {
    // topk can change between executions with the same max_batch_size_: only the
    // declared size has to move, and the allocation is grown when it does not fit.
    if (OB_FAIL(prepare_skip_vector(default_size))) {
      LOG_WARN("failed to reset skip bit vector", K(ret), K(default_size));
    }
  }
  cur_idx_ = -1;
  count_ = 0;
  total_cnt_ = 0;
  cache_iter_end_ = false;
  if (OB_FAIL(ret)) {
  } else if (OB_FAIL(ObDASTextRetrievalIter::inner_reuse())) {
  }
  return ret;
}

int ObDASTRCacheIter::inner_release()
{
  int ret = OB_SUCCESS;
  relevance_.reset();
  doc_id_.reset();
  cur_idx_ = -1;
  count_ = 0;
  total_cnt_ = 0;
  cache_iter_end_ = false;
  if (OB_FAIL(ObDASTextRetrievalIter::inner_release())) {
  }
  return ret;
}

int ObDASTRCacheIter::inner_get_next_rows(int64_t &count, int64_t capacity)
{
  int ret = OB_SUCCESS;
  if (IS_NOT_INIT) {
    ret = OB_NOT_INIT;
  } else if (!inv_idx_agg_evaluated_ && need_inv_idx_agg_) {
    if (OB_FAIL(do_doc_cnt_agg())) {
      if (OB_UNLIKELY(OB_ITER_END != ret)) {
      } else {
        inv_idx_agg_evaluated_ = true;
      }
    } else {
      inv_idx_agg_evaluated_ = true;
    }
  }
  count = 0;
  if (OB_FAIL(ret)) {
  } else {
    // Refill the cache until it holds at least `capacity` unread rows, or the
    // provider is exhausted. Serving a whole batch at once is what keeps the
    // doc id column and the relevance expr datum in the same slots of the
    // outer eval ctx: the consumer reads slot i for row i, so a row wise
    // hand-off would leave the relevance of the last cached batch visible.
    while (OB_SUCC(ret) && !cache_iter_end_ && (total_cnt_ - (cur_idx_ + 1)) < capacity) {
      if (OB_FAIL(get_next_batch_inner())) {
        if (OB_UNLIKELY(OB_ITER_END != ret)) {
          LOG_WARN("get next rows failed", K(ret));
        } else {
          // the underlying inverted index / BMW pipeline has been drained
          ret = OB_SUCCESS;
          cache_iter_end_ = true;
        }
      }
    }
  }
  if (OB_SUCC(ret)) {
    const int64_t start = cur_idx_ + 1;
    int64_t real_cnt = total_cnt_ - start;
    if (real_cnt > capacity) {
      real_cnt = capacity;
    }
    if (real_cnt > 0) {
      if (OB_FAIL(project_cached_batch(start, real_cnt))) {
        LOG_WARN("failed to project cached rows into outer ctx", K(ret), K(start), K(real_cnt));
      } else {
        cur_idx_ = start + real_cnt - 1;
        count = real_cnt;
      }
    } else {
      ret = OB_ITER_END;
    }
  }
  return ret;
}

// Project the cached rows [start, start + count) into the outer eval ctx so the
// consumer observes the (doc id, relevance) pair of the same cached row. The doc
// id is shallow copied from the cache (which outlives the outer read) and the
// cached relevance is written into the relevance expr datum, otherwise the
// pushed-down topk ordering would be projected together with a stale score.
int ObDASTRCacheIter::project_cached_batch(const int64_t start, const int64_t count)
{
  int ret = OB_SUCCESS;
  sql::ObEvalCtx *outer_ctx = ir_rtdef_->eval_ctx_;
  sql::ObExpr *doc_id_expr = ir_ctdef_->inv_scan_domain_id_col_;
  if (OB_ISNULL(outer_ctx) || OB_ISNULL(doc_id_expr)) {
    ret = OB_ERR_UNEXPECTED;
    LOG_WARN("unexpected null outer ctx or doc id expr", K(ret));
  } else if (OB_UNLIKELY(start < 0 || count <= 0 || start + count > total_cnt_)) {
    ret = OB_ERR_UNEXPECTED;
    LOG_WARN("unexpected cached row range", K(ret), K(start), K(count), K_(total_cnt));
  } else {
    ObDatum *doc_datums = doc_id_expr->locate_datums_for_update(*outer_ctx, count);
    // A cached doc id is a string datum whose payload lives in the iterator arena,
    // and inner_reuse() calls allocator_.reuse() when the buffer grows. Handing a
    // shallow copy to the upper layer therefore leaves a dangling pointer as soon
    // as the iterator is reused (the consumer may keep the projected batch across
    // executions). Deep copy string payloads into the projection expr's own result
    // buffer; numeric doc ids are stored inline in the datum and stay safe.
    const ObObjTypeClass id_type_class = ob_obj_type_class(doc_id_expr->datum_meta_.get_type());
    const bool need_deep_copy_id = (ObStringTC == id_type_class || ObTextTC == id_type_class);
    for (int64_t i = 0; OB_SUCC(ret) && i < count; ++i) {
      const ObDatum &src_id = doc_id_.at(start + i).get_datum();
      if (!need_deep_copy_id || OB_ISNULL(src_id.ptr_) || src_id.len_ <= 0) {
        doc_datums[i] = src_id;
      } else {
        char *id_dst = doc_id_expr->get_str_res_mem(*outer_ctx, src_id.len_, i);
        if (OB_ISNULL(id_dst)) {
          ret = OB_ALLOCATE_MEMORY_FAILED;
          LOG_WARN("failed to get result memory for cached doc id", K(ret), K(src_id.len_));
        } else {
          MEMCPY(id_dst, src_id.ptr_, src_id.len_);
          doc_datums[i].set_string(ObString(static_cast<int32_t>(src_id.len_), id_dst));
        }
      }
      if (OB_SUCC(ret)) {
        doc_id_expr->get_evaluated_flags(*outer_ctx).set(i);
      }
    }
    doc_id_expr->set_evaluated_projected(*outer_ctx);
    if (ir_ctdef_->need_calc_relevance()) {
      sql::ObExpr *relevance_expr = ir_ctdef_->relevance_expr_;
      if (OB_ISNULL(relevance_expr)) {
        ret = OB_ERR_UNEXPECTED;
        LOG_WARN("unexpected null relevance expr", K(ret));
      } else {
        ObDatum *rel_datums = relevance_expr->locate_datums_for_update(*outer_ctx, count);
        for (int64_t i = 0; i < count; ++i) {
          rel_datums[i].set_double(relevance_.at(start + i));
          relevance_expr->get_evaluated_flags(*outer_ctx).set(i);
        }
        relevance_expr->set_evaluated_projected(*outer_ctx);
      }
    }
    if (OB_SUCC(ret)) {
    }
  }
  return ret;
}

int ObDASTRCacheIter::get_next_batch_inner()
{
  int ret = OB_SUCCESS;
  bool need_load = false;
  if (OB_FAIL(inverted_idx_scan_iter_->get_next_rows(count_, max_batch_size_))) {
    if (OB_UNLIKELY(OB_ITER_END != ret)) {
    } else if (count_ != 0) {
      ret = OB_SUCCESS;
      need_load = true;
    }
  } else {
    need_load = true;
  }
  if (OB_SUCC(ret) && need_load) {
    // The (doc id, relevance) pair of a cached row is only meaningful while the
    // outer eval ctx geometry agrees with the number of rows actually supplied
    // by this provider batch. The consumer decides that geometry (e.g. the limit
    // operator shrinks the request to skip OFFSET rows), so pin it to count_
    // here; otherwise the relevance evaluation window and the doc id slots the
    // cache is fed from would be indexed with different origins.
    sql::ObEvalCtx *outer_ctx = ir_rtdef_->eval_ctx_;
    if (OB_ISNULL(outer_ctx)) {
      ret = OB_ERR_UNEXPECTED;
      LOG_WARN("unexpected null outer eval ctx", K(ret));
    } else {
      sql::ObEvalCtx::BatchInfoScopeGuard batch_info_scope_guard(*outer_ctx);
      batch_info_scope_guard.set_batch_idx(0);
      batch_info_scope_guard.set_batch_size(count_);
      if (OB_FAIL(copy_doc_id_to_outer_ctx_batch(count_))) {
        LOG_WARN("failed to copy doc id to outer ctx in cache iter", K(ret));
      } else if (ir_ctdef_->need_calc_relevance()) {
        sql::ObEvalCtx *ctx = ir_rtdef_->get_inv_idx_scan_rtdef()->eval_ctx_;
        const ObBitVector *skip = NULL;
        PRINT_VECTORIZED_ROWS(SQL, DEBUG, *ctx, *inv_idx_scan_param_.output_exprs_, count_, skip);
        clear_batch_wise_evaluated_flag(count_);
        if (OB_FAIL(fill_token_doc_cnt())) {
          LOG_WARN("failed to get token doc cnt", K(ret));
        } else if (OB_FAIL(save_relevances_and_docids())) {
          LOG_WARN("failed to evaluate simarity expr", K(ret));
        }
      } else if (OB_FAIL(save_docids())) {
          LOG_WARN("failed to save doc ids", K(ret));
      }
    }
  }
  return ret;
}

int ObDASTRCacheIter::save_relevances_and_docids()
{
  int ret = OB_SUCCESS;
  sql::ObExpr *relevance_expr = ir_ctdef_->relevance_expr_;
  ObExpr *doc_id_expr = ir_ctdef_->inv_scan_domain_id_col_;
  if (OB_ISNULL(relevance_expr) || OB_ISNULL(doc_id_expr)) {
    ret = OB_INVALID_ARGUMENT;
  } else if (OB_FAIL(relevance_expr->eval_batch(*ir_rtdef_->eval_ctx_, *skip_, count_))) {
  } else {
    const int64_t base = total_cnt_;
    const ObDatumVector &relevance_datum = relevance_expr->locate_expr_datumvector(*ir_rtdef_->eval_ctx_);
    // The BMW/WAND scan writes the docid into the *inner* ctx (inverted index
    // scan rtdef), not the outer one. Read it from the inner ctx.
    ObEvalCtx *inner_ctx = ir_rtdef_->get_inv_idx_scan_rtdef()->eval_ctx_;
    const ObDatumVector &doc_id_datum = doc_id_expr->locate_expr_datumvector(*inner_ctx);
    if (base + count_ > relevance_.count() && OB_FAIL(prepare_allocate_cache(base + count_))) {
      LOG_WARN("failed to expand cache arrays", K(ret), K(base), K_(count));
    } else {
      int64_t real_cnt = 0;
      for (int64_t i = 0; OB_SUCC(ret) && i < count_; ++i) {
        if (OB_LIKELY(!skip_->at(i))) {
          relevance_[base + real_cnt] = relevance_datum.at(i)->get_double();
          if (OB_FAIL(doc_id_[base + real_cnt].from_datum(*doc_id_datum.at(i)))) {
            LOG_WARN("failed to get doc id", K(ret), K(doc_id_datum.at(i)));
          } else {
            ++real_cnt;
          }
        }
      }
      if (OB_SUCC(ret)) {
        total_cnt_ = base + real_cnt;
      }
    }
  }
  return ret;
}

int ObDASTRCacheIter::save_docids()
{
  int ret = OB_SUCCESS;
  ObExpr *doc_id_expr = ir_ctdef_->inv_scan_domain_id_col_;
  if (OB_ISNULL(doc_id_expr)) {
    ret = OB_INVALID_ARGUMENT;
  } else {
    // The BMW/WAND scan writes the docid into the *inner* ctx.
    ObEvalCtx *inner_ctx = ir_rtdef_->get_inv_idx_scan_rtdef()->eval_ctx_;
    const ObDatumVector &doc_id_datum = doc_id_expr->locate_expr_datumvector(*inner_ctx);
    const int64_t base = total_cnt_;
    if (base + count_ > doc_id_.count() && OB_FAIL(prepare_allocate_cache(base + count_))) {
      LOG_WARN("failed to expand cache arrays", K(ret), K(base), K_(count));
    } else {
      int64_t real_cnt = 0;
      for (int64_t i = 0; OB_SUCC(ret) && i < count_; ++i) {
        if (OB_LIKELY(!skip_->at(i))) {
          if (OB_FAIL(doc_id_[base + real_cnt].from_datum(*doc_id_datum.at(i)))) {
            LOG_WARN("failed to get doc id", K(ret));
          } else {
            ++real_cnt;
          }
        }
      }
      if (OB_SUCC(ret)) {
        total_cnt_ = base + real_cnt;
      }
    }
  }
  return ret;
}

int ObDASTRCacheIter::prepare_allocate_cache(const int64_t size)
{
  int ret = OB_SUCCESS;
  if (size > relevance_.count() && OB_FAIL(relevance_.prepare_allocate(size))) {
    LOG_WARN("failed to expand relevance_ array", K(ret), K(size));
  }
  if (OB_SUCC(ret) && size > doc_id_.count() && OB_FAIL(doc_id_.prepare_allocate(size))) {
    LOG_WARN("failed to expand doc_id_ array", K(ret), K(size));
  }
  return ret;
}

int ObDASTRCacheIter::get_cur_row(double &relevance, ObDocIdExt &doc_id) const
{
  int ret = OB_SUCCESS;
  if (OB_UNLIKELY(cur_idx_ >= total_cnt_)) {
    ret = OB_ARRAY_OUT_OF_RANGE;
    LOG_WARN("array index out of bounds", K(ret), K_(cur_idx), K_(total_cnt));
  } else {
    // TODO: deep copy twice here and test once to see if there will be any optimization.
    // once: relevance_[cur_idx_],doc_id_[cur_idx_]=>item{relevance,doc_id}. tiwce: item=>losetree
    if (ir_ctdef_->need_calc_relevance()) {
      relevance = relevance_[cur_idx_];
    }
    doc_id = doc_id_[cur_idx_];
  }
  return ret;
}

} // namespace sql
} // namespace oceanbase
