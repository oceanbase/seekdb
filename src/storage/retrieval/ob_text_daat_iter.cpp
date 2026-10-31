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
  } else if (OB_FAIL(ObSRDaaTIterImpl::init(*param.base_param_, *param.dim_iters_,
                                            *param.allocator_, *param.relevance_collector_))) {
  } else if (nullptr != param.base_param_->relevance_expr_
             && OB_FAIL(bm25_param_estimator_.init(param.bm25_param_est_ctx_))) {
  } else {
    mode_flag_ = param.mode_flag_;
    function_lookup_mode_ = param.function_lookup_mode_;
  }
  return ret;
}

int ObTextDaaTIter::get_next_rows(const int64_t capacity, int64_t &count)
{
  // The natural-language existence path is a union of posting lists. Keep
  // Boolean collectors, function lookups and scored searches on the generic path.
  if (!is_inited_ || iter_param_->relevance_expr_ != nullptr
      || mode_flag_ != sql::NATURAL_LANGUAGE_MODE || function_lookup_mode_
      || iter_param_->need_filter()) {
    return ObSRDaaTIterImpl::get_next_rows(capacity, count);
  }
  int ret = OB_SUCCESS;
  count = 0;
  const auto *limit = iter_param_->limit_param_;
  const int64_t real_capacity = MIN(capacity, iter_param_->max_batch_size_);
  // For a short binary-ID union, compare posting heads directly instead of
  // pushing, rebuilding and popping a polymorphic merger for every document.
  const bool binary_short_union = dim_iters_->count() <= 8
      && iter_param_->id_proj_expr_->datum_meta_.type_ == common::ObVarcharType
      && iter_param_->id_proj_expr_->datum_meta_.cs_type_ == common::CS_TYPE_BINARY;
  if (capacity <= 0) {
  } else if (dim_iters_->empty() || (limit->is_valid() && output_row_cnt_ >= limit->limit_)) {
    ret = OB_ITER_END;
  } else {
    while (OB_SUCC(ret) && count < real_capacity) {
      for (int64_t i = 0; OB_SUCC(ret) && i < next_round_cnt_; ++i) {
        const int64_t idx = next_round_iter_idxes_[i];
        ObISRDaaTDimIter *dim = dim_iters_->at(idx);
        ObSRMergeItem item;
        item.iter_idx_ = idx;
        item.relevance_ = 1.0;
        if (OB_FAIL(dim->get_next_row())) {
          if (ret == OB_ITER_END) {
            iter_domain_ids_[idx] = nullptr;
            ret = OB_SUCCESS;
          }
        } else if (OB_FAIL(dim->get_curr_id(iter_domain_ids_[idx]))) {
        } else if (!binary_short_union && OB_FAIL(merge_heap_->push(item))) {
        }
      }
      if (OB_FAIL(ret)) {
      } else if (binary_short_union) {
        int64_t first = -1;
        next_round_cnt_ = 0;
        for (int64_t i = 0; i < dim_iters_->count(); ++i) {
          if (nullptr != iter_domain_ids_[i]) {
            int cmp = -1;
            if (first >= 0) {
              const ObDatum &left = *iter_domain_ids_[i];
              const ObDatum &right = *iter_domain_ids_[first];
              if (left.is_null() || right.is_null()) {
                cmp = left.is_null() ? (right.is_null() ? 0 : -1) : 1;
              } else {
                const int64_t length = MIN(left.len_, right.len_);
                cmp = length > 0 ? MEMCMP(left.ptr_, right.ptr_, length) : 0;
                if (cmp == 0) {
                  cmp = (left.len_ > right.len_) - (left.len_ < right.len_);
                }
              }
            }
            if (cmp <= 0) {
              if (cmp < 0) {
                first = i;
                next_round_cnt_ = 0;
              }
              next_round_iter_idxes_[next_round_cnt_++] = i;
            }
          }
        }
        if (first < 0) {
          ret = OB_ITER_END;
        } else {
          ret = ObSRDaaTIterImpl::cache_result(count, *iter_domain_ids_[first], 1.0);
        }
      } else if (merge_heap_->empty()) {
        ret = OB_ITER_END;
      } else if (next_round_cnt_ != 0 && OB_FAIL(merge_heap_->rebuild())) {
      } else {
        next_round_cnt_ = 0;
        int64_t idx = -1;
        bool doc_end = false;
        while (OB_SUCC(ret) && !merge_heap_->empty() && !doc_end) {
          const ObSRMergeItem *item = nullptr;
          doc_end = merge_heap_->is_unique_champion();
          if (OB_FAIL(merge_heap_->top(item))) {
          } else {
            idx = item->iter_idx_;
            if (OB_FAIL(merge_heap_->pop())) {
            } else {
              next_round_iter_idxes_[next_round_cnt_++] = idx;
            }
          }
        }
        if (OB_SUCC(ret)) {
          ret = ObSRDaaTIterImpl::cache_result(count, *iter_domain_ids_[idx], 1.0);
        }
      }
    }
    if ((OB_SUCC(ret) || ret == OB_ITER_END) && count > 0) {
      const int project_ret = ObSRDaaTIterImpl::project_results(count);
      if (project_ret != OB_SUCCESS) { ret = project_ret; }
    }
  }
  return ret;
}

void ObTextDaaTIter::reset()
{
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
    }
  }
  ObSRDaaTIterImpl::reuse(switch_tablet);
}

int ObTextDaaTIter::pre_process()
{
  int ret = OB_SUCCESS;
  if (dim_iters_->count() == 0) {
    ret = OB_ITER_END;
  } else if (nullptr != iter_param_->relevance_expr_) {
    if (OB_FAIL(bm25_param_estimator_.do_estimation(*iter_param_->eval_ctx_))) {
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
  } else if (OB_FAIL(ObSRBMWIterImpl::init(*param.base_param_, *param.dim_iters_,
                                           *param.allocator_, *param.relevance_collector_))) {
  } else if (OB_FAIL(bm25_param_estimator_.init(param.bm25_param_est_ctx_))) {
  }
  return ret;
}

int ObTextBMWIter::get_next_rows(const int64_t capacity, int64_t &count)
{
  int ret = OB_SUCCESS;
  if (IS_NOT_INIT) {
    ret = OB_NOT_INIT;
  } else if (dim_iters_->count() == 0) {
    ret = OB_ITER_END;
  } else if (OB_FAIL(bm25_param_estimator_.do_estimation(*iter_param_->eval_ctx_))) {
  }

  if (FAILEDx(ObSRBMWIterImpl::get_next_rows(capacity, count))) {
    if (OB_UNLIKELY(OB_ITER_END != ret)) {
    }
  }
  return ret;
}

int ObTextBMWIter::init_before_wand_process()
{
  int ret = OB_SUCCESS;
  if (OB_UNLIKELY(!bm25_param_estimator_.is_estimated())) {
    ret = OB_ERR_UNEXPECTED;
  }

  for (int64_t i = 0; OB_SUCC(ret) && i < dim_iters_->count(); ++i) {
    ObTextRetrievalBlockMaxIter *block_max_iter = static_cast<ObTextRetrievalBlockMaxIter *>(dim_iters_->at(i));
    if (OB_FAIL(block_max_iter->init_block_max_iter(
        bm25_param_estimator_.get_total_doc_cnt(), bm25_param_estimator_.get_avg_doc_token_cnt()))) {
    }
  }
  return ret;
}

} // namespace storage
} // namespace oceanbase
