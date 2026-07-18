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
  } else if (OB_FAIL(bm25_param_estimator_.init(param.bm25_param_est_ctx_))) {
    LOG_WARN("failed to init bm25 param estimator", K(ret));
  } else {
    mode_flag_ = param.mode_flag_;
    minimum_should_match_ = param.minimum_should_match_;
    function_lookup_mode_ = param.function_lookup_mode_;
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
  } else if (iter_param_->need_project_relevance()) {
    if (OB_FAIL(bm25_param_estimator_.do_estimation(*iter_param_->eval_ctx_))) {
      LOG_WARN("failed to do bm25 param estimation", K(ret));
    }
  }
  return ret;
}

int ObTextDaaTIter::get_total_count(int64_t &count)
{
  int ret = OB_SUCCESS;
  static const int64_t MAX_COUNT_SPAN_DIM = 2;
  count = 0;
  if (IS_NOT_INIT) {
    ret = OB_NOT_INIT;
    LOG_WARN("text retrieval count iterator is not initialized", K(ret));
  } else if (OB_ISNULL(dim_iters_) || OB_ISNULL(iter_param_)) {
    ret = OB_ERR_UNEXPECTED;
    LOG_WARN("unexpected null text retrieval count state", K(ret), KP_(dim_iters), KP_(iter_param));
  } else if (function_lookup_mode_
      || iter_param_->need_project_relevance()
      || iter_param_->need_filter()
      || iter_param_->need_pushdown_topk()) {
    ret = OB_NOT_SUPPORTED;
  } else if (ObMatchAgainstMode::NATURAL_LANGUAGE_MODE != mode_flag_
      || minimum_should_match_ > 1
      || dim_iters_->count() > MAX_COUNT_SPAN_DIM) {
    ret = ObSRDaaTIterImpl::get_total_count(count);
  } else {
    bool active[MAX_COUNT_SPAN_DIM] = {false, false};
    for (int64_t i = 0; OB_SUCC(ret) && i < dim_iters_->count(); ++i) {
      ObTextRetrievalDaaTTokenIter *iter = static_cast<ObTextRetrievalDaaTTokenIter *>(dim_iters_->at(i));
      if (OB_ISNULL(iter)) {
        ret = OB_ERR_UNEXPECTED;
        LOG_WARN("unexpected null text retrieval count dimension", K(ret), K(i));
      } else if (OB_FAIL(iter->ensure_count_row(active[i]))) {
        LOG_WARN("failed to initialize text retrieval count dimension", K(ret), K(i));
      }
    }

    while (OB_SUCC(ret)) {
      int64_t min_idx = OB_INVALID_INDEX;
      const ObDocIdExt *min_doc_id = nullptr;
      for (int64_t i = 0; OB_SUCC(ret) && i < dim_iters_->count(); ++i) {
        if (!active[i]) {
        } else {
          ObTextRetrievalDaaTTokenIter *iter = static_cast<ObTextRetrievalDaaTTokenIter *>(dim_iters_->at(i));
          const ObDocIdExt *doc_id = nullptr;
          if (OB_FAIL(iter->get_count_doc_id(doc_id))) {
            LOG_WARN("failed to get text retrieval count document id", K(ret), K(i));
          } else if (OB_ISNULL(doc_id)) {
            ret = OB_ERR_UNEXPECTED;
            LOG_WARN("unexpected null text retrieval count document id", K(ret), K(i));
          } else if (OB_INVALID_INDEX == min_idx) {
            min_idx = i;
            min_doc_id = doc_id;
          } else {
            int64_t cmp_ret = 0;
            ObTextRetrievalDaaTTokenIter *min_iter =
                static_cast<ObTextRetrievalDaaTTokenIter *>(dim_iters_->at(min_idx));
            if (OB_FAIL(min_iter->compare_count_doc_ids(*doc_id, *min_doc_id, cmp_ret))) {
              LOG_WARN("failed to compare text retrieval count heads", K(ret), K(i), K(min_idx));
            } else if (cmp_ret < 0) {
              min_idx = i;
              min_doc_id = doc_id;
            }
          }
        }
      }
      if (OB_FAIL(ret) || OB_INVALID_INDEX == min_idx) {
        break;
      }

      int64_t equal_count = 0;
      int64_t boundary_idx = OB_INVALID_INDEX;
      const ObDocIdExt *boundary_doc_id = nullptr;
      bool equal_heads[MAX_COUNT_SPAN_DIM] = {false, false};
      for (int64_t i = 0; OB_SUCC(ret) && i < dim_iters_->count(); ++i) {
        if (!active[i]) {
        } else {
          ObTextRetrievalDaaTTokenIter *iter = static_cast<ObTextRetrievalDaaTTokenIter *>(dim_iters_->at(i));
          const ObDocIdExt *doc_id = nullptr;
          int64_t cmp_ret = 0;
          if (OB_FAIL(iter->get_count_doc_id(doc_id))) {
            LOG_WARN("failed to get text retrieval count boundary", K(ret), K(i));
          } else if (OB_FAIL(iter->compare_count_doc_ids(*doc_id, *min_doc_id, cmp_ret))) {
            LOG_WARN("failed to compare text retrieval count boundary", K(ret), K(i), K(min_idx));
          } else if (0 == cmp_ret) {
            ++equal_count;
            equal_heads[i] = true;
          } else if (OB_INVALID_INDEX == boundary_idx) {
            boundary_idx = i;
            boundary_doc_id = doc_id;
          } else {
            int64_t boundary_cmp = 0;
            if (OB_FAIL(iter->compare_count_doc_ids(*doc_id, *boundary_doc_id, boundary_cmp))) {
              LOG_WARN("failed to compare text retrieval count upper bounds", K(ret), K(i), K(boundary_idx));
            } else if (boundary_cmp < 0) {
              boundary_idx = i;
              boundary_doc_id = doc_id;
            }
          }
        }
      }

      if (OB_FAIL(ret)) {
      } else if (equal_count > 1) {
        ++count;
        for (int64_t i = 0; OB_SUCC(ret) && i < dim_iters_->count(); ++i) {
          if (active[i] && equal_heads[i]) {
            ObTextRetrievalDaaTTokenIter *iter = static_cast<ObTextRetrievalDaaTTokenIter *>(dim_iters_->at(i));
            if (OB_FAIL(iter->consume_cached_docs(1))) {
              LOG_WARN("failed to consume duplicate count document id", K(ret), K(i));
            } else if (OB_FAIL(iter->ensure_count_row(active[i]))) {
              LOG_WARN("failed to advance duplicate count dimension", K(ret), K(i));
            }
          }
        }
      } else {
        ObTextRetrievalDaaTTokenIter *min_iter =
            static_cast<ObTextRetrievalDaaTTokenIter *>(dim_iters_->at(min_idx));
        int64_t run_count = min_iter->get_cached_doc_count();
        if (OB_NOT_NULL(boundary_doc_id)
            && OB_FAIL(min_iter->count_cached_docs_before(*boundary_doc_id, run_count))) {
          LOG_WARN("failed to count non-overlapping posting span", K(ret), K(min_idx), K(boundary_idx));
        } else if (OB_UNLIKELY(run_count <= 0)) {
          ret = OB_ERR_UNEXPECTED;
          LOG_WARN("unexpected empty count posting span", K(ret), K(min_idx), K(run_count));
        } else {
          count += run_count;
          if (OB_FAIL(min_iter->consume_cached_docs(run_count))) {
            LOG_WARN("failed to consume count posting span", K(ret), K(min_idx), K(run_count));
          } else if (OB_FAIL(min_iter->ensure_count_row(active[min_idx]))) {
            LOG_WARN("failed to advance count posting span", K(ret), K(min_idx));
          }
        }
      }
    }
  }
  return ret;
}

int ObTextDaaTIter::fill_merge_heap()
{
  int ret = OB_SUCCESS;
  ObSRMergeItem item;
  for (int64_t i = 0; OB_SUCC(ret) && i < next_round_cnt_; ++i) {
    const int64_t iter_idx = next_round_iter_idxes_[i];
    ObTextRetrievalDaaTTokenIter *dim_iter = nullptr;
    bool has_row = true;
    if (OB_ISNULL(dim_iter = static_cast<ObTextRetrievalDaaTTokenIter *>(dim_iters_->at(iter_idx)))) {
      ret = OB_ERR_UNEXPECTED;
      LOG_WARN("unexpected null text retrieval dimension iterator", K(ret), K(iter_idx));
    } else if (OB_FAIL(dim_iter->get_next_row())) {
      if (OB_ITER_END == ret) {
        ret = OB_SUCCESS;
        has_row = false;
      } else {
        LOG_WARN("failed to load next text retrieval dimension row", K(ret), K(iter_idx));
      }
    } else if (!iter_param_->need_project_relevance()) {
      item.relevance_ = 1.0;
    } else if (OB_FAIL(dim_iter->get_curr_score(item.relevance_))) {
      LOG_WARN("failed to get current text retrieval score", K(ret), K(iter_idx));
    } else if (OB_NOT_NULL(iter_param_->dim_weights_)) {
      item.relevance_ *= iter_param_->field_boost_ * iter_param_->dim_weights_->at(iter_idx);
    }
    if (OB_FAIL(ret) || !has_row) {
    } else if (OB_FAIL(dim_iter->get_curr_id(iter_domain_ids_[iter_idx]))) {
      LOG_WARN("failed to get current text retrieval document id", K(ret), K(iter_idx));
    } else {
      item.iter_idx_ = iter_idx;
      if (OB_FAIL(merge_heap_->push(item))) {
        LOG_WARN("failed to push text retrieval item into merge heap", K(ret), K(item));
      }
    }
  }

  if (OB_FAIL(ret)) {
  } else if (merge_heap_->empty()) {
    ret = OB_ITER_END;
  } else if (0 != next_round_cnt_ && OB_FAIL(merge_heap_->rebuild())) {
    LOG_WARN("failed to rebuild text retrieval merge heap", K(ret));
  } else {
    next_round_cnt_ = 0;
  }
  return ret;
}

int ObTextDaaTIter::collect_dims_by_id(
    const ObDatum *&id_datum,
    double &relevance,
    bool &got_valid_id)
{
  int ret = OB_SUCCESS;
  const ObSRMergeItem *top_item = nullptr;
  bool current_doc_end = false;
  int64_t iter_idx = OB_INVALID_INDEX;
  relevance = 0.0;
  got_valid_id = false;

  while (OB_SUCC(ret) && !merge_heap_->empty() && !current_doc_end) {
    current_doc_end = merge_heap_->is_unique_champion();
    if (OB_FAIL(merge_heap_->top(top_item))) {
      LOG_WARN("failed to get top text retrieval merge item", K(ret));
    } else if (OB_FAIL(relevance_collector_->collect_one_dim(top_item->iter_idx_, top_item->relevance_))) {
      LOG_WARN("failed to collect text retrieval dimension", K(ret));
    } else {
      iter_idx = top_item->iter_idx_;
      if (OB_FAIL(merge_heap_->pop())) {
        LOG_WARN("failed to pop text retrieval merge item", K(ret));
      } else {
        next_round_iter_idxes_[next_round_cnt_++] = iter_idx;
      }
    }
  }

  if (OB_SUCC(ret)) {
    if (OB_UNLIKELY(OB_INVALID_INDEX == iter_idx)) {
      ret = OB_ERR_UNEXPECTED;
      LOG_WARN("unexpected invalid text retrieval dimension index", K(ret));
    } else if (OB_ISNULL(id_datum = iter_domain_ids_[iter_idx])) {
      ret = OB_ERR_UNEXPECTED;
      LOG_WARN("unexpected null text retrieval document id", K(ret), K(iter_idx));
    } else if (OB_FAIL(relevance_collector_->get_result(relevance, got_valid_id))) {
      LOG_WARN("failed to get text retrieval relevance result", K(ret));
    } else if (got_valid_id && OB_FAIL(process_collected_row(*id_datum, relevance))) {
      LOG_WARN("failed to process collected text retrieval row", K(ret));
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
