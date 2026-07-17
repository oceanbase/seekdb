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

#define USING_LOG_PREFIX STORAGE

#include "ob_text_daat_iter.h"

namespace oceanbase
{
namespace storage
{
int ObTextDaaTIter::init(const ObTextDaaTParam &param)
{
  int ret = OB_SUCCESS;
  if (OB_ISNULL(param.base_param_) || OB_ISNULL(param.dim_iters_) || OB_ISNULL(param.allocator_)
      || OB_ISNULL(param.relevance_collector_)) {
    ret = OB_INVALID_ARGUMENT;
    LOG_WARN("unexpected null pointer in param", K(ret), KP_(param.base_param),
             KP_(param.dim_iters), KP_(param.allocator), KP_(param.relevance_collector));
  } else if (OB_FAIL(ObSRDaaTIterImpl::init(*param.base_param_, *param.dim_iters_,
                                            *param.allocator_, *param.relevance_collector_))) {
    LOG_WARN("failed to init sr daat iter", K(ret));
  } else if (param.base_param_->need_calc_relevance() && !param.dim_iters_->empty()
      && OB_FAIL(bm25_param_estimator_.init(param.bm25_param_est_ctx_))) {
    LOG_WARN("failed to init bm25 param estimator", K(ret));
  } else {
    mode_flag_ = param.mode_flag_;
    function_lookup_mode_ = param.function_lookup_mode_;
    use_batch_union_ = param.base_param_->eval_ctx_->is_vectorized()
        && !param.base_param_->need_collect_dims_
        && !param.base_param_->need_calc_relevance()
        && !param.function_lookup_mode_;
    for (int64_t i = 0; OB_SUCC(ret) && use_batch_union_ && i < param.dim_iters_->count(); ++i) {
      if (OB_FAIL(batch_union_finished_.push_back(false))) {
        LOG_WARN("failed to initialize batch union state", K(ret), K(i));
      }
    }
  }
  return ret;
}

int ObTextDaaTIter::get_next_rows(const int64_t capacity, int64_t &count)
{
  return use_batch_union_
      ? get_next_rows_batch_union(capacity, count)
      : ObSRDaaTIterImpl::get_next_rows(capacity, count);
}

int ObTextDaaTIter::get_next_rows_batch_union(const int64_t capacity, int64_t &count)
{
  int ret = OB_SUCCESS;
  count = 0;
  if (IS_NOT_INIT) {
    ret = OB_NOT_INIT;
    LOG_WARN("text DaaT iterator is not initialized", K(ret));
  } else if (OB_UNLIKELY(capacity < 0)) {
    ret = OB_INVALID_ARGUMENT;
    LOG_WARN("invalid batch capacity", K(ret), K(capacity));
  } else if (0 == capacity) {
  } else if (0 == dim_iters_->count()) {
    ret = OB_ITER_END;
  } else if (iter_param_->limit_param_->is_valid()
      && output_row_cnt_ >= iter_param_->limit_param_->limit_) {
    ret = OB_ITER_END;
  } else if (OB_FAIL(pre_process())) {
    if (OB_ITER_END != ret) {
      LOG_WARN("failed to prepare text batch union", K(ret));
    }
  } else {
    const int64_t real_capacity = MIN(capacity, iter_param_->max_batch_size_);
    while (OB_SUCC(ret) && count < real_capacity) {
      ObDocIdExt doc_id;
      if (OB_FAIL(get_next_union_doc_id(doc_id))) {
        if (OB_ITER_END != ret) {
          LOG_WARN("failed to get next union document id", K(ret), K(count));
        }
      } else {
        const common::ObLimitParam *limit_param = iter_param_->limit_param_;
        ++input_row_cnt_;
        if (!limit_param->is_valid() || input_row_cnt_ > limit_param->offset_) {
          buffered_domain_ids_[count] = doc_id;
          ++count;
          ++output_row_cnt_;
          if (limit_param->is_valid() && output_row_cnt_ >= limit_param->limit_) {
            ret = OB_ITER_END;
          }
        }
      }
    }
    if ((OB_SUCC(ret) || OB_ITER_END == ret)
        && count > 0
        && OB_FAIL(project_results(count))) {
      LOG_WARN("failed to project batch-union results", K(ret), K(count));
    }
  }
  return ret;
}

int ObTextDaaTIter::get_next_union_doc_id(ObDocIdExt &doc_id)
{
  int ret = OB_SUCCESS;
  int64_t min_idx = -1;
  for (int64_t i = 0; OB_SUCC(ret) && i < dim_iters_->count(); ++i) {
    if (batch_union_finished_[i]) {
      continue;
    }
    ObTextRetrievalDaaTTokenIter *iter =
        static_cast<ObTextRetrievalDaaTTokenIter *>(dim_iters_->at(i));
    if (OB_ISNULL(iter)) {
      ret = OB_ERR_UNEXPECTED;
      LOG_WARN("unexpected null text dimension iterator", K(ret), K(i));
    } else if (!iter->has_current_doc_id()) {
      ret = iter->load_next_doc_id_batch();
      if (OB_ITER_END == ret) {
        batch_union_finished_[i] = true;
        ret = OB_SUCCESS;
      } else if (OB_FAIL(ret)) {
        LOG_WARN("failed to load text dimension batch", K(ret), K(i));
      }
    }
    if (OB_SUCC(ret) && !batch_union_finished_[i]) {
      if (min_idx < 0) {
        min_idx = i;
      } else {
        int64_t cmp_ret = 0;
        const ObDocIdExt &candidate = iter->get_current_doc_id();
        const ObDocIdExt &minimum = static_cast<ObTextRetrievalDaaTTokenIter *>(
            dim_iters_->at(min_idx))->get_current_doc_id();
        if (OB_FAIL(compare_doc_id(candidate, minimum, cmp_ret))) {
          LOG_WARN("failed to compare text document ids", K(ret), K(i), K(min_idx));
        } else if (cmp_ret < 0) {
          min_idx = i;
        }
      }
    }
  }

  if (OB_FAIL(ret)) {
  } else if (min_idx < 0) {
    ret = OB_ITER_END;
  } else {
    ObTextRetrievalDaaTTokenIter *min_iter =
        static_cast<ObTextRetrievalDaaTTokenIter *>(dim_iters_->at(min_idx));
    const ObDocIdExt &minimum = min_iter->get_current_doc_id();
    doc_id = minimum;
    for (int64_t i = 0; OB_SUCC(ret) && i < dim_iters_->count(); ++i) {
      if (!batch_union_finished_[i]) {
        ObTextRetrievalDaaTTokenIter *iter =
            static_cast<ObTextRetrievalDaaTTokenIter *>(dim_iters_->at(i));
        int64_t cmp_ret = 0;
        if (OB_FAIL(compare_doc_id(iter->get_current_doc_id(), minimum, cmp_ret))) {
          LOG_WARN("failed to compare duplicate document id", K(ret), K(i), K(min_idx));
        } else if (0 == cmp_ret) {
          iter->advance_current_doc_id();
        }
      }
    }
  }
  return ret;
}

int ObTextDaaTIter::compare_doc_id(
    const ObDocIdExt &left,
    const ObDocIdExt &right,
    int64_t &cmp_ret) const
{
  int ret = OB_SUCCESS;
  const ObDatum &left_datum = left.get_datum();
  const ObDatum &right_datum = right.get_datum();
  if (common::ObUInt64Type == iter_param_->id_proj_expr_->datum_meta_.type_) {
    const uint64_t left_id = left_datum.get_uint64();
    const uint64_t right_id = right_datum.get_uint64();
    cmp_ret = left_id < right_id ? -1 : left_id > right_id ? 1 : 0;
  } else if (CS_TYPE_BINARY == iter_param_->id_proj_expr_->datum_meta_.cs_type_
      && ob_is_string_type(iter_param_->id_proj_expr_->datum_meta_.type_)) {
    cmp_ret = left_datum.get_string().compare(right_datum.get_string());
  } else {
    sql::ObExprBasicFuncs *basic_funcs = ObDatumFuncs::get_basic_func(
        iter_param_->id_proj_expr_->datum_meta_.type_,
        iter_param_->id_proj_expr_->datum_meta_.cs_type_);
    int datum_cmp_ret = 0;
    if (OB_ISNULL(basic_funcs) || OB_ISNULL(basic_funcs->null_first_cmp_)) {
      ret = OB_ERR_UNEXPECTED;
      LOG_WARN("failed to get document id comparator", K(ret));
    } else if (OB_FAIL(basic_funcs->null_first_cmp_(left_datum, right_datum, datum_cmp_ret))) {
      LOG_WARN("failed to compare document id datums", K(ret));
    } else {
      cmp_ret = datum_cmp_ret;
    }
  }
  return ret;
}

void ObTextDaaTIter::reset()
{
  batch_union_finished_.reset();
  use_batch_union_ = false;
  bm25_param_estimator_.reset();
  // this class can know the type of dim_iters_, but ObSRDaaTIterImpl maybe not
  if (OB_NOT_NULL(dim_iters_)) {
    for (int64_t i = 0; i < dim_iters_->count(); ++i) {
      static_cast<ObTextRetrievalDaaTTokenIter *>(dim_iters_->at(i))->reset();
    }
  }
  ObSRDaaTIterImpl::reset();
}

void ObTextDaaTIter::reuse(const bool switch_tablet)
{
  if ((OB_NOT_NULL(dim_iters_) && 0 == dim_iters_->count())) {
    // do nothing
  } else {
    bm25_param_estimator_.reuse(switch_tablet);
  }
  if (OB_NOT_NULL(dim_iters_)) {
    for (int64_t i = 0; i < dim_iters_->count(); ++i) {
      static_cast<ObTextRetrievalDaaTTokenIter *>(dim_iters_->at(i))->reuse();
      if (use_batch_union_) {
        batch_union_finished_[i] = false;
      }
    }
  }
  ObSRDaaTIterImpl::reuse(switch_tablet);
}

int ObTextDaaTIter::pre_process()
{
  int ret = OB_SUCCESS;
  if (dim_iters_->count() == 0) {
    ret = OB_ITER_END;
  } else if (iter_param_->need_calc_relevance()) {
    if (OB_FAIL(bm25_param_estimator_.do_estimation(*iter_param_->eval_ctx_))) {
      LOG_WARN("failed to do bm25 param estimation", K(ret));
    }
  }
  return ret;
}

ObTextBMWIter::ObTextBMWIter()
  : ObSRBMWIterImpl(),
    bm25_param_estimator_() {}

void ObTextBMWIter::reuse(const bool switch_tablet)
{
  bm25_param_estimator_.reuse(switch_tablet);
  if (OB_NOT_NULL(dim_iters_)) {
    for (int64_t i = 0; i < dim_iters_->count(); ++i) {
      static_cast<ObTextRetrievalBlockMaxIter *>(dim_iters_->at(i))->reuse();
    }
  }
  ObSRBMWIterImpl::reuse(switch_tablet);
}

void ObTextBMWIter::reset()
{
  bm25_param_estimator_.reset();
  if (OB_NOT_NULL(dim_iters_)) {
    for (int64_t i = 0; i < dim_iters_->count(); ++i) {
      static_cast<ObTextRetrievalBlockMaxIter *>(dim_iters_->at(i))->reset();
    }
  }
  ObSRBMWIterImpl::reset();
}

int ObTextBMWIter::init(const ObTextDaaTParam &param)
{
  int ret = OB_SUCCESS;
  if (OB_ISNULL(param.base_param_) || OB_ISNULL(param.dim_iters_) || OB_ISNULL(param.allocator_)
      || OB_ISNULL(param.relevance_collector_)) {
    ret = OB_INVALID_ARGUMENT;
    LOG_WARN("unexpected null pointer in param", K(ret), KP_(param.base_param),
             KP_(param.dim_iters), KP_(param.allocator), KP_(param.relevance_collector));
  } else if (OB_FAIL(ObSRBMWIterImpl::init(*param.base_param_, *param.dim_iters_,
                                           *param.allocator_, *param.relevance_collector_))) {
    LOG_WARN("failed to init sr bmw iter", K(ret));
  } else if (OB_FAIL(bm25_param_estimator_.init(param.bm25_param_est_ctx_))) {
    LOG_WARN("failed to init bm25 param estimator", K(ret));
  }
  return ret;
}

int ObTextBMWIter::get_next_rows(const int64_t capacity, int64_t &count)
{
  int ret = OB_SUCCESS;
  if (IS_NOT_INIT) {
    ret = OB_NOT_INIT;
    LOG_WARN("not inited", K(ret));
  } else if (dim_iters_->count() == 0) {
    ret = OB_ITER_END;
  } else if (OB_FAIL(bm25_param_estimator_.do_estimation(*iter_param_->eval_ctx_))) {
    LOG_WARN("failed to do bm25 param estimation", K(ret));
  }

  if (FAILEDx(ObSRBMWIterImpl::get_next_rows(capacity, count))) {
    if (OB_UNLIKELY(OB_ITER_END != ret)) {
      LOG_WARN("failed to get next rows", K(ret));
    }
  }
  return ret;
}

int ObTextBMWIter::init_before_wand_process()
{
  int ret = OB_SUCCESS;
  if (OB_UNLIKELY(!bm25_param_estimator_.is_estimated())) {
    ret = OB_ERR_UNEXPECTED;
    LOG_WARN("bm25 param not estimated", K(ret));
  }

  for (int64_t i = 0; OB_SUCC(ret) && i < dim_iters_->count(); ++i) {
    ObTextRetrievalBlockMaxIter *block_max_iter = static_cast<ObTextRetrievalBlockMaxIter *>(dim_iters_->at(i));
    if (OB_FAIL(block_max_iter->init_block_max_iter(
        bm25_param_estimator_.get_total_doc_cnt(), bm25_param_estimator_.get_avg_doc_token_cnt()))) {
      LOG_WARN("failed to init block max iter", K(ret));
    }
  }
  return ret;
}

} // namespace storage
} // namespace oceanbase
