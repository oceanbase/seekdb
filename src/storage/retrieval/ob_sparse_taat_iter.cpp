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

#include "ob_sparse_taat_iter.h"

namespace oceanbase
{
namespace storage
{

ObSRTaaTIterImpl::ObSRTaaTIterImpl()
  : ObISparseRetrievalMergeIter(),
    iter_allocator_(nullptr),
    iter_param_(nullptr),
    dim_iter_(nullptr),
    partition_cnt_(0),
    datum_stores_(nullptr),
    datum_store_iters_(nullptr),
    skips_(nullptr),
    hash_maps_(nullptr),
    cur_map_iter_(nullptr),
    cur_map_idx_(-1),
    next_clear_map_idx_(0),
    cache_first_id_(),
    are_chunk_stores_inited_(false),
    are_chunk_stores_filled_(false),
    set_datum_func_(nullptr)
{
}

int ObSRTaaTIterImpl::init(
    ObSparseRetrievalMergeParam &iter_param,
    ObISparseRetrievalDimIter &dim_iter,
    ObIAllocator &iter_allocator)
{
  int ret = OB_SUCCESS;
  if (IS_INIT) {
    ret = OB_INIT_TWICE;
    LOG_WARN("double initialization", K(ret));
  } else if (OB_UNLIKELY(iter_param.max_batch_size_ <= 0)) {
    ret = OB_ERR_UNEXPECTED;
    LOG_WARN("unexpected max batch size", K(ret), K(iter_param.max_batch_size_));
  } else {
    iter_allocator_ = &iter_allocator;
    iter_param_ = &iter_param;
    dim_iter_ = &dim_iter;
    input_row_cnt_ = 0;
    output_row_cnt_ = 0;
    if (iter_param_->id_proj_expr_->datum_meta_.type_ == common::ObUInt64Type) {
      set_datum_func_ = ObISparseRetrievalMergeIter::set_datum_int;
    } else {
      set_datum_func_ = ObISparseRetrievalMergeIter::set_datum_shallow;
    }
    is_inited_ = true;
  }
  return ret;
}

void ObSRTaaTIterImpl::reset()
{
  ObSRTaaTIterImpl::reuse();
  partition_cnt_ = 0;
  is_inited_ = false;
}

void ObSRTaaTIterImpl::reuse(const bool switch_tablet)
{
  if (nullptr != hash_maps_) {
    for (int64_t i = 0; i < partition_cnt_; ++i) {
      hash_maps_[i]->destroy();
    }
    hash_maps_ = nullptr;
  }
  if (nullptr != datum_stores_) {
    for (int64_t i = 0; i < partition_cnt_; ++i) {
      query::reset_spill_row_store(datum_stores_[i]);
    }
    datum_stores_ = nullptr;
  }
  if (nullptr != datum_store_iters_) {
    for (int64_t i = 0; i < partition_cnt_; ++i) {
      query::reset_spill_row_store_iterator(datum_store_iters_[i]);
    }
    datum_store_iters_ = nullptr;
  }
  skips_ = nullptr;
  cur_map_iter_ = nullptr;
  cur_map_idx_ = -1;
  next_clear_map_idx_ = 0;
  cache_first_id_.reset();
  are_chunk_stores_inited_ = false;
  are_chunk_stores_filled_ = false;
  input_row_cnt_ = 0;
  output_row_cnt_ = 0;
}

int ObSRTaaTIterImpl::get_next_row()
{
  int ret = OB_SUCCESS;
  int64_t count = 0;
  if (OB_FAIL(get_next_rows(1, count))) {
    if (OB_UNLIKELY(OB_ITER_END != ret)) {
      LOG_WARN("failed to get next row", K(ret));
    } else if (OB_UNLIKELY(count != 0)) {
      ret = OB_ERR_UNEXPECTED;
      LOG_WARN("unexpected row count", K(ret), K(count));
    }
  } else if (OB_UNLIKELY(count != 1)) {
    ret = OB_ERR_UNEXPECTED;
    LOG_WARN("unexpected row count", K(ret), K(count));
  }
  return ret;
}

int ObSRTaaTIterImpl::get_next_rows(const int64_t capacity, int64_t &count)
{
  int ret = OB_SUCCESS;
  if (IS_NOT_INIT) {
    ret = OB_NOT_INIT;
    LOG_WARN("not inited", K(ret));
  } else if (OB_UNLIKELY(0 == capacity)) {
    count = 0;
  } else if (iter_param_->limit_param_->is_valid() && output_row_cnt_ >= iter_param_->limit_param_->limit_) {
    ret = OB_ITER_END;
  } else if (OB_FAIL(pre_process())) {
    if (OB_UNLIKELY(OB_ITER_END != ret)) {
      LOG_WARN("failed to pre process", K(ret));
    } else {
      count = 0;
    }
  } else if (OB_UNLIKELY(0 == partition_cnt_)) {
    ret = OB_ITER_END;
  } else if (!are_chunk_stores_inited_ && OB_FAIL(init_chunk_stores())) {
    LOG_WARN("failed to init chunk stores", K(ret));
  } else if (!are_chunk_stores_filled_ && OB_FAIL(fill_chunk_stores())) {
    LOG_WARN("failed to fill chunk stores", K(ret));
  } else if (cur_map_idx_ > partition_cnt_) {
    ret = OB_ERR_UNEXPECTED;
    LOG_WARN("unexpected cur map idx", K(ret), K_(cur_map_idx), K_(partition_cnt));
  } else {
    if (cur_map_idx_ == partition_cnt_) {
      ret = OB_ITER_END;
    }
    while (next_clear_map_idx_ < cur_map_idx_) {
      hash_maps_[next_clear_map_idx_++]->clear();
    }
    count = 0;
    const int64_t real_capacity = MIN(capacity, iter_param_->max_batch_size_);
    while (OB_SUCC(ret) && count < real_capacity) {
      if (-1 != cur_map_idx_ && nullptr != cur_map_iter_
          && *cur_map_iter_ != hash_maps_[cur_map_idx_]->end()) {
        if (OB_FAIL(project_results(real_capacity, count))) {
          LOG_WARN("failed to fill output exprs", K(ret));
        }
      } else if (OB_FAIL(load_next_hash_map())) {
        if (OB_ITER_END != ret) {
          LOG_WARN("failed to load next hash map", K(ret), K(count), K_(cur_map_idx));
        }
      }
    }

    if ((OB_SUCC(ret) || OB_ITER_END == ret) && count > 0) {
      ObEvalCtx *eval_ctx = iter_param_->eval_ctx_;
      ObDatum &id_datum = iter_param_->id_proj_expr_->locate_expr_datum(*eval_ctx, 0);
      set_datum_func_(id_datum, cache_first_id_);
    }
  }
  return ret;
}

int ObSRTaaTIterImpl::pre_process()
{
  return OB_NOT_IMPLEMENT;
}

int ObSRTaaTIterImpl::init_chunk_stores()
{
  int ret = OB_SUCCESS;
  if (OB_UNLIKELY(are_chunk_stores_inited_)) {
    ret = OB_ERR_UNEXPECTED;
    LOG_WARN("chunk stores are already inited", K(ret));
  } else if (OB_UNLIKELY(partition_cnt_ <= 0)) {
    ret = OB_ERR_UNEXPECTED;
    LOG_WARN("partition cnt is not set", K(ret), K_(partition_cnt));
  } else {
    void *buf = nullptr;
    int64_t capacity = iter_param_->max_batch_size_;
    OB_ASSERT(nullptr == hash_maps_ && nullptr == datum_stores_ && nullptr == datum_store_iters_
      && nullptr == skips_); // TODO: to be deleted
    if (OB_FAIL(ret)) {
    } else if (OB_ISNULL(buf = iter_allocator_->alloc(sizeof(ObSRTaaTHashMap *) * partition_cnt_))) {
      ret = OB_ALLOCATE_MEMORY_FAILED;
      LOG_WARN("failed to allocate memory for hash maps", K(ret));
    } else {
      hash_maps_ = static_cast<ObSRTaaTHashMap **>(buf);
    }
    if (OB_FAIL(ret)) {
    } else if (OB_ISNULL(buf = iter_allocator_->alloc(sizeof(query::ObSpillRowStore *) * partition_cnt_))) {
      ret = OB_ALLOCATE_MEMORY_FAILED;
      LOG_WARN("failed to allocate memory for chunk stores", K(ret));
    } else {
      datum_stores_ = static_cast<query::ObSpillRowStore **>(buf);
    }
    if (OB_FAIL(ret)) {
    } else if (OB_ISNULL(buf = iter_allocator_->alloc(sizeof(query::ObSpillRowStoreIterator *) * partition_cnt_))) {
      ret = OB_ALLOCATE_MEMORY_FAILED;
      LOG_WARN("failed to allocate memory for chunk store iterators", K(ret));
    } else {
      datum_store_iters_ = static_cast<query::ObSpillRowStoreIterator **>(buf);
    }
    if (OB_FAIL(ret) || !iter_param_->id_proj_expr_->is_batch_result()) {
    } else if (OB_ISNULL(buf = iter_allocator_->alloc(sizeof(sql::ObBitVector *) * partition_cnt_))) {
      ret = OB_ALLOCATE_MEMORY_FAILED;
      LOG_WARN("failed to allocate memory for skips", K(ret));
    } else {
      skips_ = static_cast<sql::ObBitVector **>(buf);
    }
    for (int64_t i = 0; OB_SUCC(ret) && i < partition_cnt_; ++i) {
      if (OB_FAIL(ret)) {
      } else if (OB_ISNULL(buf = iter_allocator_->alloc(sizeof(ObSRTaaTHashMap)))) {
        ret = OB_ALLOCATE_MEMORY_FAILED;
        LOG_WARN("failed to allocate memory for hash map", K(ret));
      } else {
        ObSRTaaTHashMap *hash_map = new (buf) ObSRTaaTHashMap();
        if (OB_FAIL(hash_map->create(10, common::ObMemAttr("FTTaatMap")))) {
          LOG_WARN("failed to create token map", K(ret));
        } else {
          hash_maps_[i] = hash_map;
        }
      }
      if (OB_FAIL(ret)) {
      } else if (OB_FAIL(query::create_spill_row_store(
          *iter_allocator_, datum_stores_[i]))) {
        LOG_WARN("failed to create spill row store", K(ret));
      }
      if (OB_FAIL(ret)) {
      } else if (OB_FAIL(query::create_spill_row_store_iterator(
          *iter_allocator_, datum_store_iters_[i]))) {
        LOG_WARN("failed to create spill row store iterator", K(ret));
      }
      if (OB_FAIL(ret) || !iter_param_->id_proj_expr_->is_batch_result()) {
      } else if (OB_ISNULL(buf = iter_allocator_->alloc(sql::ObBitVector::memory_size(capacity)))) {
        ret = OB_ALLOCATE_MEMORY_FAILED;
        LOG_WARN("failed to allocate memory for skip", K(ret));
      } else {
        sql::ObBitVector *skip = sql::to_bit_vector(buf);
        skip->init(capacity);
        skips_[i] = skip;
      }
    }
    if (OB_SUCC(ret)) {
      are_chunk_stores_inited_ = true;
    }
  }
  return ret;
}

int ObSRTaaTIterImpl::fill_chunk_stores()
{
  int ret = OB_SUCCESS;
  if (OB_UNLIKELY(!are_chunk_stores_inited_)) {
    ret = OB_ERR_UNEXPECTED;
    LOG_WARN("chunk stores are not inited", K(ret));
  } else if (OB_UNLIKELY(are_chunk_stores_filled_)) {
    ret = OB_ERR_UNEXPECTED;
    LOG_WARN("chunk stores are already filled", K(ret));
  } else {
    ObSEArray<ObExpr *, 2> exprs;
    if (OB_FAIL(exprs.push_back(iter_param_->id_proj_expr_))) {
      LOG_WARN("failed to push back id expr", K(ret));
    } else if (iter_param_->need_project_relevance()
        && OB_FAIL(exprs.push_back(iter_param_->relevance_expr_))) {
      LOG_WARN("failed to push back relevance expr", K(ret));
    }
    ObEvalCtx *eval_ctx = iter_param_->eval_ctx_;
    int64_t capacity = iter_param_->max_batch_size_;
    int64_t total_count = 0;

    for (int64_t dim_idx = 0; OB_SUCC(ret); ) {
      int64_t subtotal_count = 0;
      if (iter_param_->id_proj_expr_->is_batch_result()) {
        // batched
        while (OB_SUCC(ret)) {
          int64_t count = 0;
          if (OB_FAIL(dim_iter_->get_next_batch(capacity, count))) {
            if (OB_UNLIKELY(OB_ITER_END != ret)) {
              LOG_WARN("failed to get next rows from dimension iter", K(ret));
            } else if (OB_LIKELY(count > 0)) {
              ret = OB_SUCCESS;
            }
          }
          if (OB_SUCC(ret)) {
            for (int64_t i = 0; i < partition_cnt_; ++i) {
              skips_[i]->set_all(capacity);
            }
            for (int64_t i = 0; OB_SUCC(ret) && i < count; ++i) {
              const ObDatum &id_datum = iter_param_->id_proj_expr_->locate_expr_datum(*eval_ctx, i);
              if (OB_UNLIKELY(id_datum.is_null()) || OB_ISNULL(id_datum.ptr_)) {
                ret = OB_ERR_UNEXPECTED;
                LOG_WARN("unexpected id datum", K(ret), K(id_datum));
              } else {
                uint64_t partition = murmurhash(id_datum.ptr_, id_datum.len_, 0) % partition_cnt_;
                skips_[partition]->unset(i);
              }
            }
            int64_t check_count = 0;
            for (int64_t i = 0; OB_SUCC(ret) && i < partition_cnt_; ++i) {
              int64_t stored_count = 0;
              if (OB_FAIL(query::spill_row_store_add_batch(
                  datum_stores_[i], exprs, *eval_ctx, *skips_[i], count,
                  stored_count))) {
                LOG_WARN("failed to add rows", K(ret));
              } else {
                check_count += stored_count;
                subtotal_count += stored_count;
              }
            }
            if (OB_SUCC(ret) && count != check_count) {
              ret = OB_ERR_UNEXPECTED;
              LOG_WARN("unexpected row count", K(ret), K(count), K(check_count));
            }
          }
        }
      } else {
        // non-batched
        while (OB_SUCC(ret)) {
          if (OB_FAIL(dim_iter_->get_next_row())) {
            if (OB_UNLIKELY(OB_ITER_END != ret)) {
              LOG_WARN("failed to get next row from dimension iter", K(ret));
            }
          } else {
            const ObDatum &id_datum = iter_param_->id_proj_expr_->locate_expr_datum(*eval_ctx);
            if (OB_UNLIKELY(id_datum.is_null()) || OB_ISNULL(id_datum.ptr_)) {
              ret = OB_ERR_UNEXPECTED;
              LOG_WARN("unexpected id datum", K(ret), K(id_datum));
            } else {
              uint64_t partition = murmurhash(id_datum.ptr_, id_datum.len_, 0) % partition_cnt_;
              if (OB_FAIL(query::spill_row_store_add_row(
                  datum_stores_[partition], exprs, *eval_ctx))) {
                LOG_WARN("failed to add row", K(ret));
              } else {
                ++subtotal_count;
              }
            }
          }
        }
      }

      if (OB_ITER_END == ret) {
        ret = OB_SUCCESS;
      }
      if (OB_SUCC(ret)) {
        total_count += subtotal_count;
        eval_ctx->reuse(eval_ctx->get_batch_size());
        if (OB_FAIL(update_dim_iter(++dim_idx))) {
          if (OB_ITER_END != ret) {
            LOG_WARN("failed to update dimension iter", K(ret));
          }
        }
      }
    }

    if (OB_LIKELY(OB_ITER_END == ret)) {
      ret = OB_SUCCESS;
      int64_t check_count = 0;
      for (int64_t i = 0; OB_SUCC(ret) && i < partition_cnt_; ++i) {
        if (OB_FAIL(query::init_spill_row_store_iterator(
            datum_store_iters_[i], datum_stores_[i]))) {
          LOG_WARN("failed to init datum store iter", K(ret));
        } else {
          check_count += query::spill_row_store_row_count(datum_stores_[i]);
        }
      }
      if (OB_SUCC(ret) && OB_UNLIKELY(total_count != check_count)) {
        ret = OB_ERR_UNEXPECTED;
        LOG_WARN("unexpected total row count", K(ret), K(total_count), K(check_count));
      }
    }
  }
  if (OB_SUCC(ret)) {
    are_chunk_stores_filled_ = true;
  }
  return ret;
}

int ObSRTaaTIterImpl::load_next_hash_map()
{
  int ret = OB_SUCCESS;
  ++cur_map_idx_;
  if (cur_map_idx_ >= partition_cnt_) {
    ret = OB_ITER_END;
  } else if (OB_UNLIKELY(hash_maps_[cur_map_idx_]->size() != 0)) {
    ret = OB_ERR_UNEXPECTED;
    LOG_WARN("unexpected non-empty hash map", K(ret), K_(cur_map_idx), K(hash_maps_[cur_map_idx_]->size()));
  } else if (OB_ISNULL(iter_param_->id_proj_expr_)) {
    ret = OB_ERR_UNEXPECTED;
    LOG_WARN("unexpected null id expr", K(ret));
  } else {
    ObSRTaaTHashMap *map = hash_maps_[cur_map_idx_];
    query::ObSpillRowStoreIterator *store_iter = datum_store_iters_[cur_map_idx_];

    ObSEArray<ObExpr *, 2> exprs;
    if (OB_FAIL(exprs.push_back(iter_param_->id_proj_expr_))) {
      LOG_WARN("failed to push back id expr", K(ret));
    } else if (iter_param_->need_project_relevance()
        && OB_FAIL(exprs.push_back(iter_param_->relevance_expr_))) {
      LOG_WARN("failed to push back relevance expr", K(ret));
    }
    ObEvalCtx *eval_ctx = iter_param_->eval_ctx_;
    ObEvalCtx::BatchInfoScopeGuard guard(*eval_ctx);
    guard.set_batch_idx(0);

    ObDocIdExt id;
    double cur_relevance = 0.0;
    double last_relevance = 0.0;
    while (OB_SUCC(ret)) {
      if (OB_FAIL(query::spill_row_store_next_row(
          store_iter, *eval_ctx, exprs))) {
        if (OB_UNLIKELY(OB_ITER_END != ret)) {
          LOG_WARN("failed to get next row from datum store", K(ret));
        }
      } else {
        ObDatum &id_datum = iter_param_->id_proj_expr_->locate_expr_datum(*eval_ctx);
        if (OB_FAIL(id.from_datum(id_datum))) {
          LOG_WARN("failed to get id from datum", K(ret));
        } else if (iter_param_->need_project_relevance()) {
          ObDatum &relevance_datum = iter_param_->relevance_expr_->locate_expr_datum(*eval_ctx);
          cur_relevance = relevance_datum.get_double();
          if (OB_FAIL(map->get_refactored(id, last_relevance))) {
            if (OB_HASH_NOT_EXIST != ret) {
              LOG_WARN("failed to get relevance from hash map", K(ret));
            } else if (OB_FAIL(map->set_refactored(id, cur_relevance, 1 /* overwrite */))) {
              LOG_WARN("failed to set relevance in hash map", K(ret));
            }
          } else if (OB_FAIL(map->set_refactored(id, cur_relevance + last_relevance, 1 /* overwrite */))) {
            LOG_WARN("failed to set relevance in hash map", K(ret));
          }
        } else {
          if (OB_FAIL(map->set_refactored(id, 1.0, 0 /* overwrite */))) {
            LOG_WARN("failed to set relevance in hash map", K(ret));
          }
        }
      }
    }
    if (OB_ITER_END == ret) {
      ret = OB_SUCCESS;
    }
  }
  if (OB_SUCC(ret)) {
    ObSRTaaTHashMap::iterator map_iter = hash_maps_[cur_map_idx_]->begin();
    void *buf = nullptr;
    if (nullptr != cur_map_iter_) {
      cur_map_iter_ = new (cur_map_iter_) ObSRTaaTHashMap::iterator(map_iter);
    } else if (OB_ISNULL(buf = iter_allocator_->alloc(sizeof(ObSRTaaTHashMap::iterator)))) {
      ret = OB_ALLOCATE_MEMORY_FAILED;
      LOG_WARN("failed to allocate memory for cur map iterator", K(ret));
    } else {
      cur_map_iter_ = new (buf) ObSRTaaTHashMap::iterator(map_iter);
    }
  }
  return ret;
}

int ObSRTaaTIterImpl::project_results(const int64_t safe_capacity, int64_t &count)
{
  int ret = OB_SUCCESS;
  ObEvalCtx *eval_ctx = iter_param_->eval_ctx_;
  ObDatum *id_datum = iter_param_->id_proj_expr_->locate_batch_datums(*eval_ctx);
  ObExpr *relevance_expr = iter_param_->relevance_proj_expr_;
  ObDatum *relevance_datum = nullptr;
  ObExpr *filter_expr = iter_param_->filter_expr_;
  const ObLimitParam *limit_param = iter_param_->limit_param_;
  if (iter_param_->need_project_relevance()) {
    if (count == 0) {
      relevance_datum = relevance_expr->locate_datums_for_update(*eval_ctx, safe_capacity);
    } else {
      relevance_datum = relevance_expr->locate_batch_datums(*eval_ctx);
    }
  }
  for (; OB_SUCC(ret) && count < safe_capacity && *cur_map_iter_ != hash_maps_[cur_map_idx_]->end(); ++(*cur_map_iter_)) {
    ObEvalCtx::BatchInfoScopeGuard guard(*eval_ctx);
    guard.set_batch_idx(count);
    set_datum_func_(id_datum[count], (*cur_map_iter_)->first);
    if (iter_param_->need_project_relevance()) {
      relevance_datum[count].set_double((*cur_map_iter_)->second);
      relevance_expr->set_evaluated_flag(*eval_ctx);
    }
    if (0 == count) {
      cache_first_id_ = (*cur_map_iter_)->first;
    }

    bool need_project = false;
    ObDatum *filter_res = nullptr;
    if (!iter_param_->need_filter()) {
      need_project = true;
    } else {
      filter_expr->clear_evaluated_flag(*eval_ctx);
      if (OB_FAIL(filter_expr->eval(*eval_ctx, filter_res))) {
        LOG_WARN("failed to evalaute filter", K(ret));
      } else {
        need_project = !(filter_res->is_null() || 0 == filter_res->get_int());
      }
    }

    if (OB_SUCC(ret) && need_project) {
      ++input_row_cnt_;
      if (limit_param->is_valid() && input_row_cnt_ <= limit_param->offset_) {
        // don't need to project, will be overwritten
      } else {
        ++output_row_cnt_;
        ++count;
        if (limit_param->is_valid() && output_row_cnt_ >= limit_param->limit_) {
          ret = OB_ITER_END;
        }
      }
    }
  }
  if (OB_SUCC(ret) && iter_param_->need_project_relevance()) {
    relevance_expr->set_evaluated_projected(*eval_ctx);
  }
  return ret;
}

int ObSRTaaTIterImpl::update_dim_iter(const int64_t dim_idx)
{
  return OB_NOT_IMPLEMENT;
}

} // namespace storage
} // namespace oceanbase
