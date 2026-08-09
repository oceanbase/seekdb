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
#include "ob_das_spiv_merge_iter.h"
#include "data_plane/blocksstable/ob_datum_row.h"
#include "sql/das/ob_das_scan_op.h"
#include "data_plane/access/ob_table_scan_access.h"
#include "query/das/ob_das_iter_access.h"
#include "query/das/ob_block_max_spec_access.h"
#include "sql/das/iter/ob_das_vec_scan_utils.h"
#include "sql/engine/expr/ob_expr_lob_utils.h"
#include "sql/engine/expr/ob_array_expr_utils.h"
#include <time.h>

namespace oceanbase
{
namespace sql
{
namespace
{

// SQL-private adapter for one SPIV posting-list scan.  The retrieval core only
// sees the query-neutral source port; DAS scan state and expression projection
// remain on this side of the seam.
class ObDASSPIVDaaTSourceAdapter final : public data_plane::ObISparseRetrievalSource
{
public:
  ObDASSPIVDaaTSourceAdapter()
    : allocator_(nullptr),
      scan_param_(nullptr),
      scan_iter_(nullptr),
      id_expr_(nullptr),
      score_expr_(nullptr),
      eval_ctx_(nullptr),
      cmp_func_(nullptr),
      datum_access_ctx_(nullptr),
      max_batch_size_(1),
      query_value_(0.0),
      scores_(),
      ids_(),
      current_idx_(-1),
      count_(0),
      last_id_(),
      last_score_(0.0),
      has_last_(false),
      exhausted_(false),
      saved_error_(OB_SUCCESS),
      is_reset_(true)
  {}
  virtual ~ObDASSPIVDaaTSourceAdapter() = default;

  int init(
      common::ObIAllocator &allocator,
      storage::ObTableScanParam &scan_param,
      ObDASScanIter &scan_iter,
      ObExpr &id_expr,
      ObExpr &score_expr,
      ObEvalCtx &eval_ctx,
      const double query_value)
  {
    int ret = OB_SUCCESS;
    allocator_ = &allocator;
    if (OB_FAIL(eval_ctx.get_datum_access_ctx(datum_access_ctx_))) {
    }
    common::ObDatumBasicFuncs *basic_funcs =
        ObDatumFuncs::get_basic_func(id_expr.datum_meta_.type_, CS_TYPE_BINARY);
    if (OB_FAIL(ret)) {
    } else if (OB_ISNULL(basic_funcs) || OB_ISNULL(basic_funcs->null_first_cmp_)) {
      ret = OB_ERR_UNEXPECTED;
      LOG_WARN("failed to resolve SPIV id comparator", K(ret), K(id_expr.datum_meta_));
    } else {
      scan_param_ = &scan_param;
      scan_iter_ = &scan_iter;
      id_expr_ = &id_expr;
      score_expr_ = &score_expr;
      eval_ctx_ = &eval_ctx;
      cmp_func_ = basic_funcs->null_first_cmp_;
      max_batch_size_ = OB_MAX(eval_ctx.max_batch_size_, 1);
      query_value_ = query_value;
      scores_.set_allocator(&allocator);
      ids_.set_allocator(&allocator);
      if (OB_FAIL(scores_.init(max_batch_size_))) {
      } else if (OB_FAIL(scores_.prepare_allocate(max_batch_size_))) {
      } else if (OB_FAIL(ids_.init(max_batch_size_))) {
      } else if (OB_FAIL(ids_.prepare_allocate(max_batch_size_))) {
      } else {
        query::das_scan_set_param(scan_iter_, *scan_param_);
        is_reset_ = false;
      }
    }
    return ret;
  }

  virtual int next(data_plane::ObSparseRetrievalEntryView &entry) override
  {
    int ret = OB_SUCCESS;
    entry = data_plane::ObSparseRetrievalEntryView();
    if (OB_SUCCESS != saved_error_) {
      ret = saved_error_;
    } else if (exhausted_) {
      ret = OB_ITER_END;
    } else if (++current_idx_ >= count_) {
      if (OB_FAIL(load_batch())) {
      } else {
        current_idx_ = 0;
      }
    }
    if (OB_SUCC(ret)) {
      ret = publish_current(entry);
    }
    if (OB_FAIL(ret) && OB_ITER_END != ret) {
      saved_error_ = ret;
    }
    return ret;
  }

  virtual int advance_to(
      const data_plane::ObSparseRetrievalIdView &target,
      data_plane::ObSparseRetrievalEntryView &entry) override
  {
    int ret = OB_SUCCESS;
    bool found = false;
    data_plane::ObSparseRetrievalId target_copy;
    entry = data_plane::ObSparseRetrievalEntryView();
    if (OB_SUCCESS != saved_error_) {
      ret = saved_error_;
    } else if (!target.is_valid()) {
      ret = OB_INVALID_ARGUMENT;
    } else if (OB_FAIL(target_copy.assign(target))) {
    } else if (has_last_) {
      int cmp_result = 0;
      if (OB_FAIL(cmp_func_(
              last_id_.datum(), target_copy.datum(), cmp_result,
              datum_access_ctx_))) {
      } else if (cmp_result >= 0) {
        entry.id_ = last_id_.view();
        entry.score_ = last_score_;
        found = true;
      }
    }
    while (OB_SUCC(ret) && !found) {
      if (OB_FAIL(next(entry))) {
      } else {
        int cmp_result = 0;
        if (OB_FAIL(cmp_func_(
                *entry.id_.datum_, target_copy.datum(), cmp_result,
                datum_access_ctx_))) {
        } else {
          found = cmp_result >= 0;
        }
      }
    }
    if (OB_FAIL(ret) && OB_ITER_END != ret) {
      saved_error_ = ret;
    }
    return ret;
  }

  virtual int max_score(double &score) const override
  {
    UNUSED(score);
    return OB_NOT_SUPPORTED;
  }

  virtual int reuse(const bool switch_source) override
  {
    UNUSED(switch_source);
    current_idx_ = -1;
    count_ = 0;
    last_id_.reset();
    last_score_ = 0.0;
    has_last_ = false;
    exhausted_ = false;
    saved_error_ = OB_SUCCESS;
    is_reset_ = false;
    return OB_SUCCESS;
  }

  virtual void reset() override
  {
    if (!is_reset_ && OB_NOT_NULL(scan_iter_)) {
      query::das_scan_reset(scan_iter_);
    }
    current_idx_ = -1;
    count_ = 0;
    scores_.reset();
    ids_.reset();
    last_id_.reset();
    last_score_ = 0.0;
    has_last_ = false;
    exhausted_ = true;
    saved_error_ = OB_SUCCESS;
    is_reset_ = true;
  }

  virtual void destroy() override
  {
    common::ObIAllocator *allocator = allocator_;
    reset();
    this->~ObDASSPIVDaaTSourceAdapter();
    if (OB_NOT_NULL(allocator)) {
      allocator->free(this);
    }
  }

private:
  int load_batch()
  {
    int ret = OB_SUCCESS;
    count_ = 0;
    if (OB_ISNULL(scan_iter_) || OB_ISNULL(id_expr_) || OB_ISNULL(score_expr_)
        || OB_ISNULL(eval_ctx_)) {
      ret = OB_NOT_INIT;
    } else if (max_batch_size_ > 1) {
      if (OB_FAIL(query::das_scan_next_rows(scan_iter_, count_, max_batch_size_))) {
        if (OB_ITER_END == ret && count_ > 0) {
          ret = OB_SUCCESS;
        } else if (OB_ITER_END == ret) {
          exhausted_ = true;
        } else {
          LOG_WARN("failed to read SPIV posting-list batch", K(ret));
        }
      }
    } else if (OB_FAIL(query::das_scan_next_row(scan_iter_))) {
      if (OB_ITER_END == ret) {
        exhausted_ = true;
      } else {
        LOG_WARN("failed to read SPIV posting-list row", K(ret));
      }
    } else {
      count_ = 1;
    }
    if (OB_SUCC(ret) && count_ <= 0) {
      ret = OB_ITER_END;
      exhausted_ = true;
    } else if (OB_SUCC(ret)) {
      const ObDatumVector &score_datums = score_expr_->locate_expr_datumvector(*eval_ctx_);
      const ObDatumVector &id_datums = id_expr_->locate_expr_datumvector(*eval_ctx_);
      for (int64_t i = 0; OB_SUCC(ret) && i < count_; ++i) {
        scores_[i] = score_datums.at(i)->get_float();
        if (OB_FAIL(ids_[i].assign(data_plane::ObSparseRetrievalIdView(*id_datums.at(i))))) {
        }
      }
    }
    return ret;
  }

  int publish_current(data_plane::ObSparseRetrievalEntryView &entry)
  {
    int ret = OB_SUCCESS;
    if (current_idx_ < 0 || current_idx_ >= count_) {
      ret = OB_ERR_UNEXPECTED;
      LOG_WARN("SPIV source cursor is outside its buffered batch", K(ret), K_(current_idx), K_(count));
    } else if (has_last_) {
      int cmp_result = 0;
      if (OB_FAIL(cmp_func_(
              last_id_.datum(), ids_[current_idx_].datum(), cmp_result,
              datum_access_ctx_))) {
      } else if (cmp_result >= 0) {
        ret = OB_ERR_UNEXPECTED;
        LOG_WARN("SPIV source ids are not strictly increasing", K(ret), K_(current_idx));
      }
    }
    if (OB_SUCC(ret)) {
      entry.id_ = ids_[current_idx_].view();
      entry.score_ = static_cast<double>(scores_[current_idx_]) * query_value_;
      if (OB_FAIL(last_id_.assign(entry.id_))) {
      } else {
        last_score_ = entry.score_;
        has_last_ = true;
      }
    }
    return ret;
  }

private:
  common::ObIAllocator *allocator_;
  storage::ObTableScanParam *scan_param_;
  ObDASScanIter *scan_iter_;
  ObExpr *id_expr_;
  ObExpr *score_expr_;
  ObEvalCtx *eval_ctx_;
  common::ObDatumCmpFuncType cmp_func_;
  const common::ObDatumAccessContext *datum_access_ctx_;
  int64_t max_batch_size_;
  double query_value_;
  common::ObFixedArray<float, common::ObIAllocator> scores_;
  common::ObFixedArray<data_plane::ObSparseRetrievalId, common::ObIAllocator> ids_;
  int64_t current_idx_;
  int64_t count_;
  data_plane::ObSparseRetrievalId last_id_;
  double last_score_;
  bool has_last_;
  bool exhausted_;
  int saved_error_;
  bool is_reset_;
  DISALLOW_COPY_AND_ASSIGN(ObDASSPIVDaaTSourceAdapter);
};

class ObDASSPIVIdOpsAdapter final : public data_plane::ObISparseRetrievalIdOps
{
public:
  ObDASSPIVIdOpsAdapter()
    : allocator_(nullptr), cmp_func_(nullptr), datum_access_ctx_(nullptr)
  {}
  virtual ~ObDASSPIVIdOpsAdapter() = default;

  int init(
      common::ObIAllocator &allocator,
      const ObExpr &id_expr,
      const common::ObDatumAccessContext *datum_access_ctx)
  {
    int ret = OB_SUCCESS;
    allocator_ = &allocator;
    datum_access_ctx_ = datum_access_ctx;
    common::ObDatumBasicFuncs *basic_funcs =
        ObDatumFuncs::get_basic_func(id_expr.datum_meta_.type_, CS_TYPE_BINARY);
    if (OB_ISNULL(datum_access_ctx)) {
      ret = OB_INVALID_ARGUMENT;
      LOG_WARN("missing datum access context", K(ret));
    } else if (OB_ISNULL(basic_funcs) || OB_ISNULL(basic_funcs->null_first_cmp_)) {
      ret = OB_ERR_UNEXPECTED;
      LOG_WARN("failed to resolve SPIV retrieval comparator", K(ret), K(id_expr.datum_meta_));
    } else {
      cmp_func_ = basic_funcs->null_first_cmp_;
    }
    return ret;
  }

  virtual int compare(
      const data_plane::ObSparseRetrievalIdView &left,
      const data_plane::ObSparseRetrievalIdView &right,
      int &cmp_result) const override
  {
    int ret = OB_SUCCESS;
    cmp_result = 0;
    if (OB_ISNULL(cmp_func_) || !left.is_valid() || !right.is_valid()) {
      ret = OB_INVALID_ARGUMENT;
    } else if (OB_FAIL(cmp_func_(
                   *left.datum_, *right.datum_, cmp_result,
                   datum_access_ctx_))) {
    }
    return ret;
  }

  virtual void destroy() override
  {
    common::ObIAllocator *allocator = allocator_;
    this->~ObDASSPIVIdOpsAdapter();
    if (OB_NOT_NULL(allocator)) {
      allocator->free(this);
    }
  }

private:
  common::ObIAllocator *allocator_;
  common::ObDatumCmpFuncType cmp_func_;
  const common::ObDatumAccessContext *datum_access_ctx_;
  DISALLOW_COPY_AND_ASSIGN(ObDASSPIVIdOpsAdapter);
};

class ObDASSPIVFilterAdapter final : public data_plane::ObISparseRetrievalFilter
{
public:
  ObDASSPIVFilterAdapter() : allocator_(nullptr), valid_ids_(nullptr) {}
  virtual ~ObDASSPIVFilterAdapter() = default;

  int init(
      common::ObIAllocator &allocator,
      const common::hash::ObHashSet<ObDocIdExt> &valid_ids)
  {
    allocator_ = &allocator;
    valid_ids_ = &valid_ids;
    return OB_SUCCESS;
  }

  virtual int accept(
      const data_plane::ObSparseRetrievalIdView &id,
      bool &accepted) const override
  {
    int ret = OB_SUCCESS;
    accepted = false;
    ObDocIdExt doc_id;
    if (OB_ISNULL(valid_ids_) || !id.is_valid()) {
      ret = OB_INVALID_ARGUMENT;
    } else if (OB_FAIL(doc_id.from_datum(*id.datum_))) {
    } else {
      const int hash_ret = valid_ids_->exist_refactored(doc_id);
      if (OB_HASH_EXIST == hash_ret) {
        accepted = true;
      } else if (OB_HASH_NOT_EXIST == hash_ret) {
        accepted = false;
      } else {
        ret = hash_ret;
        LOG_WARN("failed to probe SPIV pre-filter set", K(ret));
      }
    }
    return ret;
  }

  virtual void destroy() override
  {
    common::ObIAllocator *allocator = allocator_;
    this->~ObDASSPIVFilterAdapter();
    if (OB_NOT_NULL(allocator)) {
      allocator->free(this);
    }
  }

private:
  common::ObIAllocator *allocator_;
  const common::hash::ObHashSet<ObDocIdExt> *valid_ids_;
  DISALLOW_COPY_AND_ASSIGN(ObDASSPIVFilterAdapter);
};

} // namespace

int ObDASSPIVMergeIter::get_ob_sparse_drop_ratio_search(uint64_t &drop_ratio)
{
  int ret = OB_SUCCESS; 
  const uint64_t OB_SPARSE_DROP_RATIO_SEARCH_DEFAULT = 0;

  ObSQLSessionInfo *session = nullptr;
  if (OB_ISNULL(session = exec_ctx_->get_my_session())) {
    drop_ratio = OB_SPARSE_DROP_RATIO_SEARCH_DEFAULT;
    LOG_WARN("session is null", K(ret), KP(exec_ctx_));
  } else if (OB_FAIL(session->get_ob_sparse_drop_ratio_search(drop_ratio))) {
  }

  return ret;
}

int ObDASSPIVMergeIter::init_query_vector(const ObDASVecAuxScanCtDef *ir_ctdef,
                                          ObDASVecAuxScanRtDef *ir_rtdef,
                                          const ObDASSortCtDef *sort_ctdef,
                                          ObDASSortRtDef *sort_rtdef,
                                          const common::ObLimitParam &limit_param,
                                          ObExpr *&search_vec,
                                          ObExpr *&distance_calc)
{
  int ret = OB_SUCCESS;

  ObDatum *qvec_datum;
  ObString qvec_data;
  uint64_t drop_ratio;
  if (OB_FAIL(get_ob_sparse_drop_ratio_search(drop_ratio))) {
  } else if (drop_ratio == 100) {
  } else if (OB_FAIL(ObDasVecScanUtils::init_sort(vec_aux_ctdef_, vec_aux_rtdef_, sort_ctdef_, 
                                          sort_rtdef_, limit_param_, qvec_expr_, distance_calc_))) {
  } else if (OB_ISNULL(qvec_expr_)) {
    ret = OB_ERR_UNEXPECTED;
    LOG_WARN("qvec expr is null", K(ret));
  } else if (OB_FAIL(qvec_expr_->eval(*(sort_rtdef_->eval_ctx_), qvec_datum))) {
  } else if (qvec_datum->is_null()){
    ret = OB_ERR_NULL_VALUE;
    LOG_WARN("qvec datum null", K(ret));
  } else {
    const uint16_t subschema_id = qvec_expr_->obj_meta_.get_subschema_id();

    ObIArrayType *qvec_ptr = nullptr;
    if (OB_FAIL(ObArrayExprUtils::get_array_obj(allocator_, *(sort_rtdef_->eval_ctx_), subschema_id, qvec_datum->get_string(), qvec_ptr))) {
    } else if (OB_ISNULL(qvec_ptr)) {
      ret = OB_ERR_UNEXPECTED;
      LOG_WARN("qvec is null", K(ret));    
    } else if (OB_FALSE_IT(qvec_ = static_cast<ObMapType *>(qvec_ptr))) {
    } else {
      int size = qvec_->cardinality();
      int drop_count = size * drop_ratio / 100;
      if (drop_count != 0) {
        ObArrayFixedSize<uint32_t> *keys_arr = dynamic_cast<ObArrayFixedSize<uint32_t> *>(qvec_->get_key_array());
        ObArrayFixedSize<float> *values_arr = dynamic_cast<ObArrayFixedSize<float> *>(qvec_->get_value_array());
        if (OB_ISNULL(keys_arr) || OB_ISNULL(values_arr)) {
          ret = OB_ERR_UNEXPECTED;
          LOG_WARN("failed to cast key", K(ret));
        } else {
          uint32_t *keys = reinterpret_cast<uint32_t *>(keys_arr->get_data());
          float *values = reinterpret_cast<float *>(values_arr->get_data());
          sort_by_value(keys, values, 0, size - 1);
          float threshold = values[drop_count];
          int real_drop_count = drop_count;
          for(; real_drop_count >= 1; real_drop_count--) {
            if (values[real_drop_count - 1] != threshold) {
              break;
            }
          }
          uint32_t *new_keys = keys + real_drop_count;
          float *new_values = values + real_drop_count;
          sort_by_key(new_keys, new_values, 0, size - 1 - real_drop_count);
          keys_arr->set_data(new_keys, size - real_drop_count);
          values_arr->set_data(new_values, size - real_drop_count);
        }
      }
    }
  }
  return ret;
}

void ObDASSPIVMergeIter::sort_by_key(uint32_t *keys, float *values, int l, int r) 
{
  if (l < r) {
    int pos = random_partition_by_key(keys, values, l, r);
    sort_by_key(keys, values, l, pos - 1);
    sort_by_key(keys, values, pos + 1, r);
  }
}

int ObDASSPIVMergeIter::random_partition_by_value(uint32_t *keys, float *values, int l, int r) 
{
  int i = l, j = r;
  srand(time(0));
  int pos = rand() % (r - l + 1) + l;
  uint32_t pivot_key = keys[pos];
  float pivot_value = values[pos];
  std::swap(keys[l], keys[pos]);
  std::swap(values[l], values[pos]);
  while (i < j) {
    while (i < j && values[j] >= pivot_value) {
      j--;
    }
    keys[i] = keys[j];
    values[i] = values[j];
    while (i < j && values[i] <= pivot_value) {
      i++;
    }
    keys[j] = keys[i];
    values[j] = values[i];
  }
  keys[i] = pivot_key;
  values[i] = pivot_value;

  return i;
}

int ObDASSPIVMergeIter::random_partition_by_key(uint32_t *keys, float *values, int l, int r) 
{
  int i = l, j = r;
  srand(time(0));
  int pos = rand() % (r - l + 1) + l;
  uint32_t pivot_key = keys[pos];
  float pivot_value = values[pos];
  std::swap(keys[l], keys[pos]);
  std::swap(values[l], values[pos]);
  while (i < j) {
    while (i < j && keys[j] >= pivot_key) j--;
    keys[i] = keys[j];
    values[i] = values[j];
    while (i < j && keys[i] <= pivot_key) i++;
    keys[j] = keys[i];
    values[j] = values[i];
  }
  keys[i] = pivot_key;
  values[i] = pivot_value;

  return i;
}

void ObDASSPIVMergeIter::sort_by_value(uint32_t *keys, float *values, int l, int r)
{
  if (l < r) {
    int pos = random_partition_by_value(keys, values, l, r);
    sort_by_value(keys, values, l, pos - 1);
    sort_by_value(keys, values, pos + 1, r);
  }
}

// TODO: set algo dynamic
void ObDASSPIVMergeIter::set_algo()
{
  algo_ = SPIVAlgo::BLOCK_MAX_WAND;
}

void ObDASSPIVMergeIter::clear_evaluated_flag()
{
  if (vec_aux_ctdef_->is_pre_filter()) {
    if (OB_NOT_NULL(inv_idx_scan_iter_)) {
      inv_idx_scan_iter_->clear_evaluated_flag();
    } else if (OB_NOT_NULL(rowkey_docid_iter_)) {
      rowkey_docid_iter_->clear_evaluated_flag();
    } else if (OB_NOT_NULL(aux_data_iter_)) {
      aux_data_iter_->clear_evaluated_flag();
    }
  } 
}

int ObDASSPIVMergeIter::rescan()
{
  int ret = OB_SUCCESS;
  if (vec_aux_ctdef_->is_pre_filter()) {
    if (OB_NOT_NULL(inv_idx_scan_iter_) && OB_FAIL(inv_idx_scan_iter_->rescan())) {
      LOG_WARN("failed to do inv idx table rescan", K(ret));
    } 
  }
  if (OB_FAIL(ret)) {
  } else if (OB_FAIL(set_inv_scan_range_key())) {
  }
  for (int i = 0; i < inv_dim_scan_iters_.count() && OB_SUCC(ret); ++i) {
    if (OB_NOT_NULL(inv_dim_scan_iters_[i]) && OB_FAIL(inv_dim_scan_iters_[i]->rescan())) {
      LOG_WARN("failed to rescan inv dim scan iter", K(ret));
    }
  }
  return ret;
}

int ObDASSPIVMergeIter::do_table_scan()
{
  int ret = OB_SUCCESS;
  if (vec_aux_ctdef_->is_pre_filter()) {
    if (OB_NOT_NULL(inv_idx_scan_iter_) && OB_FAIL(inv_idx_scan_iter_->do_table_scan())) {
      LOG_WARN("failed to do inv idx table scan", K(ret));
    }
  }
  if(OB_SUCC(ret)) {
    if (OB_FAIL(create_dim_iters())) {
    } else if (OB_FAIL(create_spiv_merge_iter())) {
    } else if (OB_FAIL(set_inv_scan_range_key())) {
    } else {
      for (int i = 0; i < inv_dim_scan_iters_.count() && OB_SUCC(ret); ++i) {
        if (OB_NOT_NULL(inv_dim_scan_iters_[i]) && OB_FAIL(inv_dim_scan_iters_[i]->do_table_scan())) {
          LOG_WARN("failed to do table scan", K(ret));
        }
      }
    }
  }
  return ret;
}

int ObDASSPIVMergeIter::inner_init(ObDASIterParam &param)
{
  int ret = OB_SUCCESS;
  if (IS_INIT) {
    ret = OB_INIT_TWICE;
    LOG_WARN("double initialization", K(ret));
  } else if (OB_UNLIKELY(ObDASIterType::DAS_ITER_SPIV_MERGE != param.type_)) {
    ret = OB_INVALID_ARGUMENT;
    LOG_WARN("invalid das iter param type for spiv merge iter", K(ret), K(param));
  } else {
    ObDASSPIVMergeIterParam spiv_merge_param = static_cast<ObDASSPIVMergeIterParam &>(param);

    tx_desc_ = spiv_merge_param.tx_desc_;
    snapshot_ = spiv_merge_param.snapshot_;

    rowkey_docid_iter_ = spiv_merge_param.rowkey_docid_iter_;
    aux_data_iter_ = spiv_merge_param.aux_data_iter_;
    inv_idx_scan_iter_ = spiv_merge_param.inv_idx_scan_iter_;

    vec_aux_ctdef_ = spiv_merge_param.vec_aux_ctdef_;
    vec_aux_rtdef_ = spiv_merge_param.vec_aux_rtdef_;
    sort_ctdef_ = spiv_merge_param.sort_ctdef_;
    sort_rtdef_ = spiv_merge_param.sort_rtdef_;
    spiv_scan_ctdef_ = spiv_merge_param.spiv_scan_ctdef_;
    spiv_scan_rtdef_ = spiv_merge_param.spiv_scan_rtdef_;
    block_max_scan_ctdef_ = spiv_merge_param.block_max_scan_ctdef_;
    block_max_scan_rtdef_ = spiv_merge_param.block_max_scan_rtdef_;
    selectivity_ = vec_aux_ctdef_->selectivity_;

    if (is_use_docid()) {
      set_datum_func_ = set_datum_shallow;
      docid_lt_func_ = docid_lt_string;
      docid_gt_func_ = docid_gt_string;
    } else {
      set_datum_func_ = set_datum_int;
      docid_lt_func_ = docid_lt_int;
      docid_gt_func_ = docid_gt_int;
    }

    if (OB_ISNULL(mem_context_)) {
      lib::ContextParam param;
      param.set_mem_attr("SPIV_MERGE", ObCtxIds::DEFAULT_CTX_ID);
      if (OB_FAIL(CURRENT_CONTEXT->CREATE_CONTEXT(mem_context_, param))) {
      }
    }
    
    if (OB_FAIL(ret)) {
    } else if (OB_FAIL(ObDasVecScanUtils::init_limit(vec_aux_ctdef_, vec_aux_rtdef_, sort_ctdef_, sort_rtdef_, limit_param_))) {
    } else if (OB_FAIL(init_query_vector(vec_aux_ctdef_, vec_aux_rtdef_, sort_ctdef_, sort_rtdef_, limit_param_, qvec_expr_, distance_calc_))) {
    } else if (OB_ISNULL(qvec_)) {
    } else if (OB_FAIL(ObDasVecScanUtils::get_distance_expr_type(*sort_ctdef_->sort_exprs_[0], *sort_rtdef_->eval_ctx_, dis_type_))) {
    } else if (dis_type_ != ObExprVectorDistance::ObVecDisType::DOT) {
      ret = OB_ERR_UNEXPECTED;
      LOG_WARN("distance type not support yet", K(ret), K(dis_type_));
    } else if (OB_FALSE_IT(set_algo())) {
    } else if (vec_aux_ctdef_->is_pre_filter()){
      if (OB_FAIL(valid_docid_set_.create(16, ObMemAttr("ValidDocidSet")))) {
      }
    }

    is_inited_ = true;
    is_pre_processed_ = false;
  }
  return ret;
}

int ObDASSPIVMergeIter::build_inv_scan_range(ObNewRange &range, uint64_t table_id, uint32_t dim)
{
  int ret = OB_SUCCESS;

  ObArenaAllocator &allocator = mem_context_->get_arena_allocator();
  ObObj *start_key_ptr = nullptr;
  ObObj *end_key_ptr = nullptr;
  ObRowkey start_key;
  ObRowkey end_key;
  if (OB_ISNULL(start_key_ptr = static_cast<ObObj *>(allocator.alloc(sizeof(ObObj) * INV_IDX_ROWKEY_COL_CNT)))) {
    ret = OB_ALLOCATE_MEMORY_FAILED;
    LOG_WARN("fail to alloc memory for ObObj", K(ret));
  } else if (OB_ISNULL(end_key_ptr = static_cast<ObObj *>(allocator.alloc(sizeof(ObObj) * INV_IDX_ROWKEY_COL_CNT)))) {
    ret = OB_ALLOCATE_MEMORY_FAILED;
    LOG_WARN("fail to alloc memory for ObObj", K(ret));
  } else {
    start_key_ptr[0].set_uint32(dim);
    start_key_ptr[1].set_min_value();
    start_key.assign(start_key_ptr, INV_IDX_ROWKEY_COL_CNT);

    end_key_ptr[0].set_uint32(dim);
    end_key_ptr[1].set_max_value();
    end_key.assign(end_key_ptr, INV_IDX_ROWKEY_COL_CNT);

    range.table_id_ = table_id;
    range.start_key_ = start_key;
    range.end_key_ = end_key;
  }
  return ret;
}

int ObDASSPIVMergeIter::set_inv_scan_range_key()
{
  int ret = OB_SUCCESS;
  uint32_t *dims = nullptr;
  int size = 0;
  if (OB_NOT_NULL(qvec_)) {
    dims = reinterpret_cast<uint32_t *>(qvec_->get_key_array()->get_data());
    size = qvec_->cardinality();
  }
  for (int i = 0; i < size && OB_SUCC(ret); ++i) {
    ObNewRange range;
    if (OB_FAIL(build_inv_scan_range(range, spiv_scan_ctdef_->ref_table_id_, dims[i]))) {
    } else if (OB_FAIL(inv_scan_params_[i]->key_ranges_.push_back(range))) {
    } else if (algo_ == BLOCK_MAX_WAND && OB_FAIL(block_max_scan_params_[i]->key_ranges_.push_back(range))) {
      LOG_WARN("failed to push block max scan range", K(ret), K(dims[i]));
    }
  }
  return ret;
}

int ObDASSPIVMergeIter::create_dim_iters()
{
  int ret = OB_SUCCESS;
  if (OB_NOT_NULL(qvec_)) {
    const int64_t size = qvec_->cardinality();
    float *values = reinterpret_cast<float *>(qvec_->get_value_array()->get_data());
    query::ObVectorBlockMaxSpecView block_spec_view;
    common::ObSEArray<data_plane::ObSparseVectorBlockColumnSpec, 8> block_columns;
    if (FALSE_IT(inv_scan_params_.set_allocator(&allocator_))) {
    } else if (OB_FAIL(inv_scan_params_.init(size))) {
    } else if (OB_FAIL(inv_scan_params_.prepare_allocate(size))) {
    } else if (algo_ != SPIVAlgo::BLOCK_MAX_WAND) {
    } else if (FALSE_IT(block_max_scan_params_.set_allocator(&allocator_))) {
    } else if (OB_FAIL(block_max_scan_params_.init(size))) {
    } else if (OB_FAIL(block_max_scan_params_.prepare_allocate(size))) {
    } else if (OB_FAIL(query::get_vector_block_max_spec(*vec_aux_ctdef_, block_spec_view))) {
    }
    for (int64_t i = 0;
         OB_SUCC(ret) && algo_ == SPIVAlgo::BLOCK_MAX_WAND
             && i < block_spec_view.column_count_;
         ++i) {
      query::ObBlockMaxColumnView column_view;
      data_plane::ObSparseVectorBlockColumnSpec column;
      if (OB_FAIL(query::get_vector_block_max_column(*vec_aux_ctdef_, i, column_view))) {
      } else {
        column.store_index_ = column_view.store_index_;
        column.statistic_type_ = column_view.statistic_type_;
        column.projector_ = column_view.projector_;
        if (OB_FAIL(block_columns.push_back(column))) {
        }
      }
    }
    for (int64_t i = 0; i < size && OB_SUCC(ret); ++i) {
      ObDASSPIVDaaTSourceAdapter *source = nullptr;
      data_plane::ObISparseRetrievalBlockSource *block_source = nullptr;
      if (i >= inv_dim_scan_iters_.count()
          || OB_ISNULL(inv_dim_scan_iters_[i])
          || OB_ISNULL(spiv_scan_ctdef_)
          || spiv_scan_ctdef_->result_output_.empty()
          || OB_ISNULL(spiv_scan_ctdef_->result_output_.at(0))
          || OB_ISNULL(vec_aux_ctdef_->spiv_scan_value_col_)
          || OB_ISNULL(vec_aux_rtdef_->eval_ctx_)) {
        ret = OB_ERR_UNEXPECTED;
        LOG_WARN("missing SPIV source metadata", K(ret), K(i), K(size));
      } else if (OB_ISNULL(inv_scan_params_[i] = OB_NEWx(ObTableScanParam, &allocator_))) {
        ret = OB_ALLOCATE_MEMORY_FAILED;
        LOG_WARN("failed to allocate SPIV exact scan param", K(ret), K(i));
      } else if (OB_FAIL(ObDasVecScanUtils::init_scan_param(
          dim_docid_value_tablet_id_,
          spiv_scan_ctdef_,
          spiv_scan_rtdef_,
          tx_desc_,
          snapshot_,
          *inv_scan_params_[i],
          false))) {
      } else if (OB_ISNULL(source = OB_NEWx(ObDASSPIVDaaTSourceAdapter, &allocator_))) {
        ret = OB_ALLOCATE_MEMORY_FAILED;
        LOG_WARN("failed to allocate SPIV exact source", K(ret), K(i));
      } else if (OB_FAIL(source->init(
          allocator_,
          *inv_scan_params_[i],
          *inv_dim_scan_iters_[i],
          *spiv_scan_ctdef_->result_output_.at(0),
          *vec_aux_ctdef_->spiv_scan_value_col_,
          *vec_aux_rtdef_->eval_ctx_,
          values[i]))) {
      } else if (OB_FAIL(retrieval_sources_.push_back(source))) {
      } else {
        source = nullptr;
      }

      if (OB_SUCC(ret) && algo_ == SPIVAlgo::BLOCK_MAX_WAND) {
        if (OB_ISNULL(block_max_scan_params_[i] = OB_NEWx(ObTableScanParam, &allocator_))) {
          ret = OB_ALLOCATE_MEMORY_FAILED;
          LOG_WARN("failed to allocate memory for block max scan param", K(ret));
        } else if (OB_FAIL(ObDasVecScanUtils::init_scan_param(dim_docid_value_tablet_id_,
                       block_max_scan_ctdef_,
                       block_max_scan_rtdef_,
                       tx_desc_,
                       snapshot_,
                       *block_max_scan_params_[i],
                       false))) {
        } else {
          data_plane::ObSparseVectorBlockSourceSpec spec;
          spec.columns_ = block_columns.get_data();
          spec.column_count_ = block_columns.count();
          spec.min_domain_id_index_ = block_spec_view.min_domain_id_index_;
          spec.max_domain_id_index_ = block_spec_view.max_domain_id_index_;
          spec.score_index_ = block_spec_view.score_index_;
          spec.domain_id_meta_ = block_spec_view.domain_id_meta_;
          spec.dimension_meta_ = block_spec_view.dimension_meta_;
          spec.query_value_ = values[i];
          if (OB_FAIL(data_plane::create_sparse_vector_block_source(
              allocator_, *block_max_scan_params_[i], spec, block_source))) {
          } else if (OB_FAIL(block_sources_.push_back(block_source))) {
          } else {
            block_source = nullptr;
          }
        }
      }
      if (OB_NOT_NULL(source)) {
        source->destroy();
      }
      if (OB_NOT_NULL(block_source)) {
        block_source->destroy();
      }
    }
    if (OB_FAIL(ret)) {
      destroy_unowned_retrieval_ports();
    }
  }

  return ret;
}

int ObDASSPIVMergeIter::create_spiv_merge_iter()
{
  int ret = OB_SUCCESS;
  ObDASSPIVIdOpsAdapter *id_ops = nullptr;
  ObDASSPIVFilterAdapter *filter = nullptr;
  const common::ObDatumAccessContext *datum_access_ctx = nullptr;
  if (SPIVAlgo::DAAT_NAIVE != algo_ && SPIVAlgo::BLOCK_MAX_WAND != algo_) {
    ret = OB_NOT_SUPPORTED;
    LOG_WARN("not supported sparse vector query algorithm", K(ret), K_(algo));
  } else if (OB_ISNULL(vec_aux_ctdef_) || OB_ISNULL(vec_aux_rtdef_)
      || OB_ISNULL(vec_aux_rtdef_->eval_ctx_)
      || OB_ISNULL(spiv_scan_ctdef_)
      || spiv_scan_ctdef_->result_output_.empty()
      || OB_ISNULL(spiv_scan_ctdef_->result_output_.at(0))) {
    ret = OB_ERR_UNEXPECTED;
    LOG_WARN("missing SPIV retrieval adapter metadata", K(ret));
  } else if (OB_ISNULL(id_ops = OB_NEWx(ObDASSPIVIdOpsAdapter, &allocator_))) {
    ret = OB_ALLOCATE_MEMORY_FAILED;
    LOG_WARN("failed to allocate SPIV id operations adapter", K(ret));
  } else if (OB_FAIL(
                 vec_aux_rtdef_->eval_ctx_->get_datum_access_ctx(
                     datum_access_ctx))) {
  } else if (OB_FAIL(id_ops->init(
                 allocator_, *spiv_scan_ctdef_->result_output_.at(0),
                 datum_access_ctx))) {
  } else if (vec_aux_ctdef_->is_pre_filter()
      && OB_ISNULL(filter = OB_NEWx(ObDASSPIVFilterAdapter, &allocator_))) {
    ret = OB_ALLOCATE_MEMORY_FAILED;
    LOG_WARN("failed to allocate SPIV filter adapter", K(ret));
  } else if (OB_NOT_NULL(filter)
      && OB_FAIL(filter->init(allocator_, valid_docid_set_))) {
    LOG_WARN("failed to initialize SPIV filter adapter", K(ret));
  } else {
    int64_t candidate_limit = limit_param_.limit_ + limit_param_.offset_;
    if (!vec_aux_ctdef_->is_pre_filter() && selectivity_ < 1.0) {
      candidate_limit *= 2;
    }
    const int64_t max_batch_size = OB_MAX(
        vec_aux_rtdef_->eval_ctx_->max_batch_size_, 1);
    if (SPIVAlgo::DAAT_NAIVE == algo_) {
      data_plane::ObSparseRetrievalDaaTRequest request;
      request.allocator_ = &allocator_;
      request.sources_ = &retrieval_sources_;
      request.id_ops_ = id_ops;
      request.filter_ = filter;
      request.dimension_weights_ = nullptr;
      request.candidate_limit_ = candidate_limit;
      request.max_batch_size_ = max_batch_size;
      if (OB_FAIL(data_plane::ObSparseRetrievalFactory::create_daat(request, retrieval_))) {
      }
    } else {
      data_plane::ObSparseRetrievalBlockMaxWandRequest request;
      request.allocator_ = &allocator_;
      request.sources_ = &retrieval_sources_;
      request.block_sources_ = &block_sources_;
      request.id_ops_ = id_ops;
      request.filter_ = filter;
      request.dimension_weights_ = nullptr;
      request.candidate_limit_ = candidate_limit;
      request.max_batch_size_ = max_batch_size;
      if (OB_FAIL(data_plane::ObSparseRetrievalFactory::create_block_max_wand(
          request, retrieval_))) {
      }
    }
    if (OB_SUCC(ret)) {
      // Factory success commits ownership of every port atomically.
      retrieval_sources_.reset();
      block_sources_.reset();
      id_ops = nullptr;
      filter = nullptr;
    }
  }
  if (OB_FAIL(ret)) {
    destroy_unowned_retrieval_ports();
    if (OB_NOT_NULL(id_ops)) {
      id_ops->destroy();
    }
    if (OB_NOT_NULL(filter)) {
      filter->destroy();
    }
  }
  return ret;
}

int ObDASSPIVMergeIter::inner_reuse()
{
  int ret = OB_SUCCESS;

  if (nullptr != mem_context_) {
    mem_context_->reset_remain_one_page();
  }
  
  if (OB_NOT_NULL(inv_idx_scan_iter_) && OB_FAIL(inv_idx_scan_iter_->reuse())) {
    LOG_WARN("failed to reuse inv idx scan iter", K(ret));
  } else if (!aux_data_table_first_scan_ && OB_FAIL(reuse_aux_data_iter())) {
    LOG_WARN("failed to reuse com aux vec iter", K(ret));
  } else if (!rowkey_docid_table_first_scan_ && OB_FAIL(reuse_rowkey_docid_iter())) {
    LOG_WARN("failed to reuse rowkey vid iter", K(ret));
  } else {
    for (int i = 0; i < inv_dim_scan_iters_.count() && OB_SUCC(ret); ++i) {
      if (OB_FAIL(ObDasVecScanUtils::reuse_iter(
              inv_dim_scan_iters_[i], *inv_scan_params_[i], dim_docid_value_tablet_id_))) {
      }
    }
    if (!retrieval_.is_valid()) {
      ret = OB_ERR_UNEXPECTED;
      LOG_WARN("SPIV retrieval is not initialized", K(ret));
    } else if (OB_FAIL(retrieval_.reuse())) {
    }
  }
  saved_rowkeys_.reset();
  valid_docid_set_.clear();
  result_docids_.reset();
  result_docids_curr_iter_ = OB_INVALID_INDEX_INT64;
  is_pre_processed_ = false;

  return ret;
}

int ObDASSPIVMergeIter::inner_release()
{
  int ret = OB_SUCCESS;
  int tmp_ret = OB_SUCCESS;

  // Retrieval sources still reference DAS scan iterators and scan params, so
  // sever the query-neutral cursor before releasing either dependency.
  retrieval_.reset();
  destroy_unowned_retrieval_ports();
  
  if (OB_NOT_NULL(inv_idx_scan_iter_) && OB_FAIL(inv_idx_scan_iter_->release())) {
    LOG_WARN("failed to release inv idx scan iter", K(ret));
    tmp_ret = ret;
    ret = OB_SUCCESS;
  }
  if (OB_NOT_NULL(aux_data_iter_) && OB_FAIL(aux_data_iter_->release())) {
    LOG_WARN("failed to release aux data iter", K(ret));
    tmp_ret = tmp_ret == OB_SUCCESS ? ret : tmp_ret;
    ret = OB_SUCCESS;
  }
  if (OB_NOT_NULL(rowkey_docid_iter_) && OB_FAIL(rowkey_docid_iter_->release())) {
    LOG_WARN("failed to release rowkey docid iter", K(ret));
    tmp_ret = tmp_ret == OB_SUCCESS ? ret : tmp_ret;
    ret = OB_SUCCESS;
  }
  for (int i = 0; i < inv_dim_scan_iters_.count() && OB_SUCC(ret); ++i) {
    if (OB_NOT_NULL(inv_dim_scan_iters_[i]) && OB_FAIL(inv_dim_scan_iters_[i]->release())) {
      LOG_WARN("failed to release dim scan iter", K(ret));
      tmp_ret = tmp_ret == OB_SUCCESS ? ret : tmp_ret;
      ret = OB_SUCCESS;
    }
  }
  inv_dim_scan_iters_.reset();
  // return first error code
  if (tmp_ret != OB_SUCCESS) {
    ret = tmp_ret;
  }
  
  inv_idx_scan_iter_ = nullptr;
  aux_data_iter_ = nullptr;
  rowkey_docid_iter_ = nullptr;

  tx_desc_ = nullptr;
  snapshot_ = nullptr;
  vec_aux_ctdef_ = nullptr;
  vec_aux_rtdef_ = nullptr;
  sort_ctdef_ = nullptr;
  sort_rtdef_ = nullptr;
  qvec_ = nullptr;
  qvec_expr_ = nullptr;
  distance_calc_ = nullptr;

  saved_rowkeys_.reset();
  valid_docid_set_.destroy();
  result_docids_.reset();
  result_docids_curr_iter_ = OB_INVALID_INDEX_INT64;
  is_pre_processed_ = false;

  ObDasVecScanUtils::release_scan_param(aux_data_scan_param_);
  ObDasVecScanUtils::release_scan_param(rowkey_docid_scan_param_);
  for(int64_t i = 0; i < inv_scan_params_.count(); i++) {
    if (OB_NOT_NULL(inv_scan_params_[i])) {
      ObDasVecScanUtils::release_scan_param(*inv_scan_params_[i]);
    }
  }
  inv_scan_params_.reset();
  for(int64_t i = 0; i < block_max_scan_params_.count(); i++) {
    if (OB_NOT_NULL(block_max_scan_params_[i])) {
      ObDasVecScanUtils::release_scan_param(*block_max_scan_params_[i]);
    }
  }
  block_max_scan_params_.reset();

  if (nullptr != mem_context_)  {
    mem_context_->reset_remain_one_page();
    DESTROY_CONTEXT(mem_context_);
    mem_context_ = nullptr;
  }
  allocator_.reset();
  
  return ret;
}

int ObDASSPIVMergeIter::inner_get_next_row()
{
  int64_t count = 0;
  int ret = inner_get_next_rows(count, 1);
  return ret;
}

int ObDASSPIVMergeIter::project_brute_result(int64_t &count, int64_t capacity)
{
  int ret = OB_SUCCESS;

  if (OB_ISNULL(qvec_) || limit_param_.limit_ + limit_param_.offset_ == 0) {
    ret = OB_ITER_END;
  } else if (OB_INVALID_INDEX_INT64 == result_docids_curr_iter_) {
    ret = OB_ERR_UNEXPECTED;
    LOG_WARN("failed to get heap iter", K(ret));
  } else if (result_docids_curr_iter_ == result_docids_.count()) {
    ret = OB_ITER_END;
  } else {
    count = OB_MIN(result_docids_.count() - result_docids_curr_iter_, capacity);
    ObExpr *docid_expr = vec_aux_ctdef_->spiv_scan_docid_col_;
    ObDatum *docid_datum = nullptr;
    ObEvalCtx::BatchInfoScopeGuard guard(*vec_aux_rtdef_->eval_ctx_);
    guard.set_batch_size(count);
    if (OB_ISNULL(docid_datum = docid_expr->locate_datums_for_update(*vec_aux_rtdef_->eval_ctx_, count))) {
      ret = OB_ERR_UNEXPECTED;
      LOG_WARN("unexpected error, datums is nullptr", K(ret), KPC(docid_expr));
    } else {
      for (int64_t i = 0; i < count; ++i) {  
        guard.set_batch_idx(i);
        set_datum_func_(docid_datum[i], result_docids_.at(result_docids_curr_iter_++));
      }
      docid_expr->set_evaluated_projected(*vec_aux_rtdef_->eval_ctx_);
    }
  }

  return ret;
}

int ObDASSPIVMergeIter::project_retrieval_matches(int64_t &count, int64_t capacity)
{
  int ret = OB_SUCCESS;
  const data_plane::ObSparseRetrievalMatch *matches = nullptr;
  count = 0;
  if (!retrieval_.is_valid()) {
    ret = OB_NOT_INIT;
    LOG_WARN("SPIV retrieval is not initialized", K(ret));
  } else if (OB_FAIL(retrieval_.next_batch(capacity, matches, count))) {
    if (OB_ITER_END != ret) {
      LOG_WARN("failed to read SPIV retrieval matches", K(ret), K(capacity));
    }
  } else if (count > 0 && OB_ISNULL(matches)) {
    ret = OB_ERR_UNEXPECTED;
    LOG_WARN("SPIV retrieval returned a null batch", K(ret), K(count));
  } else if (count > 0) {
    ObExpr *docid_expr = vec_aux_ctdef_->spiv_scan_docid_col_;
    ObDatum *docid_datums = nullptr;
    ObEvalCtx::BatchInfoScopeGuard guard(*vec_aux_rtdef_->eval_ctx_);
    guard.set_batch_size(count);
    if (OB_ISNULL(docid_expr)
        || OB_ISNULL(docid_datums =
            docid_expr->locate_datums_for_update(*vec_aux_rtdef_->eval_ctx_, count))) {
      ret = OB_ERR_UNEXPECTED;
      LOG_WARN("failed to locate SPIV retrieval projection datums", K(ret), KP(docid_expr));
    } else {
      for (int64_t i = 0; i < count; ++i) {
        guard.set_batch_idx(i);
        if (is_use_docid()) {
          docid_datums[i].set_datum(matches[i].id_.datum());
        } else {
          docid_datums[i].set_int(matches[i].id_.datum().get_int());
        }
      }
      docid_expr->set_evaluated_projected(*vec_aux_rtdef_->eval_ctx_);
    }
  }
  return ret;
}

void ObDASSPIVMergeIter::destroy_unowned_retrieval_ports()
{
  for (int64_t i = 0; i < retrieval_sources_.count(); ++i) {
    if (OB_NOT_NULL(retrieval_sources_.at(i))) {
      retrieval_sources_.at(i)->destroy();
    }
  }
  retrieval_sources_.reset();
  for (int64_t i = 0; i < block_sources_.count(); ++i) {
    if (OB_NOT_NULL(block_sources_.at(i))) {
      block_sources_.at(i)->destroy();
    }
  }
  block_sources_.reset();
}

int ObDASSPIVMergeIter::inner_get_next_rows(int64_t &count, int64_t capacity)
{
  int ret = OB_SUCCESS;
  bool is_vectorized = capacity > 1 ? true : false;
  if (OB_ISNULL(qvec_) || limit_param_.limit_ + limit_param_.offset_ == 0) {
    ret = OB_ITER_END;
  } else if (vec_aux_ctdef_->is_pre_filter() && !is_pre_processed_) {
    if(OB_FAIL(pre_process(is_vectorized))) {
    }
  }
  if(OB_FAIL(ret)) {
  } else if (result_docids_.count() != 0) {
    if(OB_FAIL(project_brute_result(count, capacity))) {
      if (ret != OB_ITER_END) {
        LOG_WARN("failed to project brute result", K(ret));
      }
    }
  } else {
    if (IS_NOT_INIT) {
      ret = OB_NOT_INIT;
      LOG_WARN("not inited", K(ret));
    } else if (OB_UNLIKELY(0 == capacity)) {
      count = 0;
    } else if (SPIVAlgo::DAAT_NAIVE == algo_ || SPIVAlgo::BLOCK_MAX_WAND == algo_) {
      if (OB_FAIL(project_retrieval_matches(count, capacity))) {
        if (OB_ITER_END != ret) {
          LOG_WARN("failed to project SPIV retrieval matches", K(ret));
        }
      }
    } else {
      ret = OB_NOT_SUPPORTED;
      LOG_WARN("not supported sparse vector query algorithm", K(ret), K_(algo));
    }
  }
  return ret;
}

int ObDASSPIVMergeIter::get_ctdef_with_rowkey_exprs(const ObDASScanCtDef *&ctdef, ObDASScanRtDef *&rtdef)
{
  int ret = OB_SUCCESS;
  ctdef = nullptr;
  rtdef = nullptr;

  if (!is_use_docid()) {
    int idx = get_aux_data_tbl_idx();
    ctdef = vec_aux_ctdef_->get_vec_aux_tbl_ctdef(idx, ObTSCIRScanType::OB_VEC_COM_AUX_SCAN);
    rtdef = vec_aux_rtdef_->get_vec_aux_tbl_rtdef(idx);
  } else {
    ctdef = vec_aux_ctdef_->get_vec_aux_tbl_ctdef(vec_aux_ctdef_->get_spiv_rowkey_docid_tbl_idx(), ObTSCIRScanType::OB_VEC_ROWKEY_VID_SCAN);
    rtdef = vec_aux_rtdef_->get_vec_aux_tbl_rtdef(vec_aux_ctdef_->get_spiv_rowkey_docid_tbl_idx());
  }

  if (OB_ISNULL(ctdef) || OB_ISNULL(rtdef)) {
    ret = OB_ERR_UNEXPECTED;
    LOG_WARN("ctdef or rtdef is null", K(ret), KP(ctdef), KP(rtdef));
  } 

  return ret;
}

int ObDASSPIVMergeIter::get_rowkey_pre_filter(ObIAllocator &allocator, bool is_vectorized, int64_t batch_count){
  int ret = OB_SUCCESS;

  const ObDASScanCtDef *ctdef = nullptr;
  ObDASScanRtDef *rtdef = nullptr;
  if (OB_FAIL(get_ctdef_with_rowkey_exprs(ctdef, rtdef))) {
  }

  bool is_iter_end = false;
  while (OB_SUCC(ret) && saved_rowkeys_.count() < MAX_SPIV_BRUTE_FORCE_SIZE && !is_iter_end) {
    if (!is_vectorized) {
      ObRowkey *rowkey;
      inv_idx_scan_iter_->clear_evaluated_flag();
      if (OB_FAIL(inv_idx_scan_iter_->get_next_row())) {
        if (ret == OB_ITER_END) {
          ret = OB_SUCCESS;
          is_iter_end = true;
        } else {
          LOG_WARN("failed to get next row", K(ret));
        }
      } else if (OB_FAIL(ObDasVecScanUtils::get_rowkey(allocator, ctdef, rtdef, rowkey))) {
      } else if (OB_FAIL(saved_rowkeys_.push_back(rowkey))) {
      }
    } else {
      int64_t scan_row_cnt = 0;
      int64_t curr_batch_count = OB_MIN(batch_count, MAX_SPIV_BRUTE_FORCE_SIZE - saved_rowkeys_.count());
      if (OB_FAIL(inv_idx_scan_iter_->get_next_rows(scan_row_cnt, curr_batch_count))) {
        if (OB_ITER_END != ret) {
          LOG_WARN("fail to get next row from inv_idx_scan_iter_", K(ret));
        } else { 
          ret = OB_SUCCESS;        
          if (scan_row_cnt == 0) {
            is_iter_end = true;
          }
        }
      }

      if (OB_SUCC(ret) && !is_iter_end) {
        ObEvalCtx::BatchInfoScopeGuard guard(*rtdef->eval_ctx_);
        guard.set_batch_size(scan_row_cnt);
        for (int i = 0; OB_SUCC(ret) && i < scan_row_cnt; i++) {
          guard.set_batch_idx(i);
          ObRowkey *rowkey;
          if (OB_FAIL(ObDasVecScanUtils::get_rowkey(allocator, ctdef, rtdef, rowkey))) {
          } else if (OB_FAIL(saved_rowkeys_.push_back(rowkey))) {
          }
        }
      }
    }
  }

  return ret;
}

int ObDASSPIVMergeIter::get_rowkey_and_set_docids(ObIAllocator &allocator, bool is_vectorized, int64_t batch_count){
  int ret = OB_SUCCESS;

  bool is_iter_end = false;
  const ObDASScanCtDef *ctdef = nullptr;
  ObDASScanRtDef *rtdef = nullptr;
  if (OB_FAIL(get_ctdef_with_rowkey_exprs(ctdef, rtdef))) {
  }

  while (OB_SUCC(ret) && !is_iter_end) {
    if (!is_vectorized) {
      for (int i = 0; OB_SUCC(ret) && i < batch_count && !is_iter_end; i++) {
        ObRowkey *rowkey;
        inv_idx_scan_iter_->clear_evaluated_flag();
        if (OB_FAIL(inv_idx_scan_iter_->get_next_row())) {
          if (ret == OB_ITER_END) {
            ret = OB_SUCCESS;
            is_iter_end = true;
          } else {
            LOG_WARN("failed to get next row", K(ret));
          }
        } else if (OB_FAIL(ObDasVecScanUtils::get_rowkey(allocator, ctdef, rtdef, rowkey))) {
        } else if (is_use_docid() && OB_FAIL(ObDasVecScanUtils::set_lookup_key(*rowkey, rowkey_docid_scan_param_, ctdef->ref_table_id_))) {
          LOG_WARN("failed to set rowkey.", K(ret));
        } else if (!is_use_docid()) {
          ObDocIdExt docid;
          if (OB_FAIL(rowkey2docid(*rowkey, docid))) {
          } else if (OB_FAIL(valid_docid_set_.set_refactored(docid))){
          }
        }
      }
    } else {
      int64_t scan_row_cnt = 0;
      if (OB_FAIL(inv_idx_scan_iter_->get_next_rows(scan_row_cnt, batch_count))) {
        if (OB_ITER_END != ret) {
          LOG_WARN("fail to get next row from inv_idx_scan_iter_", K(ret));
        } else {  
          ret = OB_SUCCESS;       
          if (scan_row_cnt == 0) {
            is_iter_end = true;
          }
        }
      }
      if (OB_SUCC(ret) && !is_iter_end) {
        ObEvalCtx::BatchInfoScopeGuard guard(*rtdef->eval_ctx_);
        guard.set_batch_size(scan_row_cnt);
        for (int i = 0; OB_SUCC(ret) && i < scan_row_cnt; i++) {
          guard.set_batch_idx(i);
          ObRowkey *rowkey;
          if (OB_FAIL(ObDasVecScanUtils::get_rowkey(allocator, ctdef, rtdef, rowkey))) {
          } else if (is_use_docid() && OB_FAIL(ObDasVecScanUtils::set_lookup_key(*rowkey, rowkey_docid_scan_param_, ctdef->ref_table_id_))) {
            LOG_WARN("failed to set rowkey.", K(ret));
          } else if (!is_use_docid()) {
            ObDocIdExt docid;
            if (OB_FAIL(rowkey2docid(*rowkey, docid))) {
            } else if (OB_FAIL(valid_docid_set_.set_refactored(docid))){
            }
          }
        }
      }
    }
    
    if (OB_FAIL(ret)) {
    } else if (!is_use_docid()) {
      // do nothing
    } else if (rowkey_docid_scan_param_.key_ranges_.count() != 0) {
      if (OB_FAIL(do_rowkey_docid_table_scan())) {
      }

      for (int64_t i = 0; OB_SUCC(ret) && i < batch_count; ++i) {
        ObDocIdExt docid;
        if (OB_FAIL(get_docid_from_rowkey_docid_table(docid))) {
          if (OB_UNLIKELY(OB_ITER_END != ret)) {
            LOG_WARN("failed to get docid from rowkey docid table.", K(ret));
          } else {
            ret = OB_SUCCESS;
            break;
          }
        } else if (OB_FAIL(valid_docid_set_.set_refactored(docid))){
        }
      }
      if (OB_SUCC(ret) && OB_FAIL(reuse_rowkey_docid_iter())) {
        LOG_WARN("failed to reuse rowkey docid iter", K(ret));
      }
    }
  }

  return ret;
}

int ObDASSPIVMergeIter::rowkey2docid(ObRowkey &rowkey, ObDocIdExt &docid) 
{
  int ret = OB_SUCCESS;

  if (OB_UNLIKELY(is_use_docid())) {
    ret = OB_ERR_UNEXPECTED;
    LOG_WARN("can not use rowkey as docid when use docid", K(ret));
  } else if (OB_UNLIKELY(rowkey.length() != 1)) {
    ret = OB_ERR_UNEXPECTED;
    LOG_WARN("invalid rowkey length", K(ret), K(rowkey.length()));
  } else if (OB_UNLIKELY(!rowkey.is_valid())) {
    ret = OB_ERR_UNEXPECTED;
    LOG_WARN("invalid rowkey", K(ret), K(rowkey.length()));  
  } else if (OB_FAIL(docid.from_obj(*rowkey.ptr()))){
  }

  return ret;
}

int ObDASSPIVMergeIter::do_brute_force(ObIAllocator &allocator, bool is_vectorized, int64_t batch_count)
{
  int ret = OB_SUCCESS;

  uint64_t limit = limit_param_.limit_ + limit_param_.offset_;
  uint64_t saved_rowkey_count = saved_rowkeys_.count();
  uint64_t capacity = limit > saved_rowkey_count ? saved_rowkey_count : limit;
  
  ObSPIVFixedSizeHeap<ObRowkeyScoreItem, ObRowkeyScoreItemCmp> max_heap(capacity, allocator, rowkey_score_cmp_);
  
  int idx = get_aux_data_tbl_idx();
  const ObDASScanCtDef *aux_data_ctdef = vec_aux_ctdef_->get_vec_aux_tbl_ctdef(idx, ObTSCIRScanType::OB_VEC_COM_AUX_SCAN);
  const ObDASScanRtDef *aux_data_rtdef = vec_aux_rtdef_->get_vec_aux_tbl_rtdef(idx);

  // get vector by rowkey, calc distance and push to heap
  int64_t cur_idx = 0;
  while (OB_SUCC(ret) && cur_idx < saved_rowkey_count) {
    int64_t start_idx = cur_idx;
    for (int64_t i = 0; OB_SUCC(ret) && i < batch_count && cur_idx < saved_rowkey_count; ++i) {
      if (OB_FAIL(ObDasVecScanUtils::set_lookup_key(*saved_rowkeys_[cur_idx++], aux_data_scan_param_, aux_data_ctdef->ref_table_id_))) {
      } 
    }

    if (OB_FAIL(ret)) {
    } else if (OB_FAIL(do_aux_data_table_scan())) {
    } else {
      for (; OB_SUCC(ret) && start_idx < cur_idx; ++start_idx) {
        ObString vector;
        if (OB_FAIL(get_vector_from_aux_data_table(vector))) {
          if (OB_UNLIKELY(OB_ITER_END != ret)) {
            LOG_WARN("failed to get vector from aux data table.", K(ret), K(start_idx));
          }
        } else if (OB_ISNULL(vector.ptr())) {
        } else {
          double score;
          ObIArrayType *arr = nullptr;
          ObMapType *vec = nullptr;
          if (OB_FAIL(ObArrayTypeObjFactory::construct(allocator, *qvec_->get_array_type(), arr, true))) {
          } else if (OB_FAIL(arr->init(vector))){
          } else if (OB_FALSE_IT(vec = dynamic_cast<ObMapType *>(arr))) {
          } else if (OB_ISNULL(vec)) {
            ret = OB_ERR_UNEXPECTED;
            LOG_WARN("arr cast failed", K(ret));
          } else if (OB_FAIL(ObExprVectorDistance::SparseVectorDisFunc::spiv_distance_funcs[static_cast<int64_t>(dis_type_)](qvec_, vec, score))) {
          } else if (score == 0) {
          } else {
            ObRowkey *rowkey = saved_rowkeys_[start_idx];
            ObRowkeyScoreItem item{rowkey, -score};
            max_heap.push(item);
          } 
        }
      }
      int tmp_ret = ret;
      ret = OB_SUCCESS;
      if (OB_FAIL(reuse_aux_data_iter())) {
      } else {
        ret = tmp_ret;
      }
      ret = OB_ITER_END == ret ? OB_SUCCESS : ret;
    }
  }

  uint64_t heap_size = max_heap.count();

  if (OB_FAIL(ret)) {
  } else if (is_use_docid()) {
    const ObDASScanCtDef *rowkey_docid_ctdef = vec_aux_ctdef_->get_vec_aux_tbl_ctdef(vec_aux_ctdef_->get_spiv_rowkey_docid_tbl_idx(), ObTSCIRScanType::OB_VEC_ROWKEY_VID_SCAN);
    while (OB_SUCC(ret) && !max_heap.empty()) {
      for (int64_t i = 0; OB_SUCC(ret) && i < batch_count && !max_heap.empty(); ++i) {
        ObRowkey *rowkey = max_heap.top().rowkey_;
        if (OB_FAIL(max_heap.pop())) {
        } else if (OB_ISNULL(rowkey)) {
          ret = OB_ERR_UNEXPECTED;
          LOG_WARN("failed to get rowkey from max heap.", K(ret));  
        } else if (OB_FAIL(ObDasVecScanUtils::set_lookup_key(*rowkey, rowkey_docid_scan_param_, rowkey_docid_ctdef->ref_table_id_))) {
        }      
      }

      if (OB_SUCC(ret) && OB_FAIL(do_rowkey_docid_table_scan())) { 
        LOG_WARN("failed to do rowkey docid table scan", K(ret));
      }

      ObDocIdExt cur_docid;
      for (int64_t i = 0; OB_SUCC(ret) && i < batch_count; ++i) {
        if (OB_FAIL(get_docid_from_rowkey_docid_table(cur_docid))) {
          if (OB_UNLIKELY(OB_ITER_END != ret)) {
            LOG_WARN("failed to get docid from rowkey docid table.", K(ret), K(i));
          } else {
            ret = OB_SUCCESS;
            break;
          }
        } else if (OB_FAIL(result_docids_.push_back(cur_docid))) {
        }
      }
      if (OB_SUCC(ret) && OB_FAIL(reuse_rowkey_docid_iter())) {
        LOG_WARN("failed to reuse rowkey docid iter", K(ret));
      }
    }
    for (int i = 0; OB_SUCC(ret) && i < heap_size / 2; i++) {
      std::swap(result_docids_.at(i), result_docids_.at(heap_size - 1 - i));
    }
  } else {
    while (OB_SUCC(ret) && !max_heap.empty()) {
      for (int64_t i = 0; OB_SUCC(ret) && i < batch_count && !max_heap.empty(); ++i) {
        ObRowkey *rowkey = max_heap.top().rowkey_;
        if (OB_FAIL(max_heap.pop())) {
        } else if (OB_ISNULL(rowkey)) {
          ret = OB_ERR_UNEXPECTED;
          LOG_WARN("failed to get rowkey from max heap.", K(ret));  
        } else {
          ObDocIdExt cur_docid;
          if (OB_FAIL(rowkey2docid(*rowkey, cur_docid))) {
          } else if (OB_FAIL(result_docids_.push_back(cur_docid))) {
          }
        }   
      }
    }
    for (int i = 0; OB_SUCC(ret) && i < heap_size / 2; i++) {
      std::swap(result_docids_.at(i), result_docids_.at(heap_size - 1 - i));
    } 
  }

  return ret;
}

int ObDASSPIVMergeIter::set_valid_docids_with_rowkeys(ObIAllocator &allocator, int64_t batch_count)
{
  int ret = OB_SUCCESS;

  if (is_use_docid()) {
    const ObDASScanCtDef *rowkey_docid_ctdef = vec_aux_ctdef_->get_vec_aux_tbl_ctdef(vec_aux_ctdef_->get_spiv_rowkey_docid_tbl_idx(), ObTSCIRScanType::OB_VEC_ROWKEY_VID_SCAN);

    int64_t rowkey_idx = 0;
    int rowkeys_size = saved_rowkeys_.count();
    
    while (OB_SUCC(ret) && rowkey_idx < rowkeys_size) {
      int batch_size = 0;
      for (int64_t i = 0; OB_SUCC(ret) && i < batch_count && rowkey_idx < rowkeys_size; ++i) {
        ObRowkey *rowkey;
        if (OB_ISNULL(rowkey = saved_rowkeys_.at(rowkey_idx++))) {
          ret = OB_ERR_UNEXPECTED;
          LOG_WARN("failed to get rowkey from saved rowkeys.", K(ret));  
        } else if (OB_FAIL(ObDasVecScanUtils::set_lookup_key(*rowkey, rowkey_docid_scan_param_, rowkey_docid_ctdef->ref_table_id_))) {
        }
        batch_size++;
      }
      if (OB_SUCC(ret) && OB_FAIL(do_rowkey_docid_table_scan())) { 
        LOG_WARN("failed to do rowkey docid table scan", K(ret));
      }

      for (int64_t i = 0; OB_SUCC(ret) && i < batch_size; ++i) {
        ObDocIdExt docid;
        if (OB_FAIL(get_docid_from_rowkey_docid_table(docid))) {
          if (OB_UNLIKELY(OB_ITER_END != ret)) {
            LOG_WARN("failed to get docid from rowkey docid table.", K(ret));
          }
        } else if (OB_FAIL(valid_docid_set_.set_refactored(docid))){
        }
      }
      if (OB_SUCC(ret) && OB_FAIL(reuse_rowkey_docid_iter())) {
        LOG_WARN("failed to reuse rowkey docid iter", K(ret));
      }
    }
  } else {
    int64_t rowkey_idx = 0;
    int rowkeys_size = saved_rowkeys_.count();
    
    while (OB_SUCC(ret) && rowkey_idx < rowkeys_size) {
      for (int64_t i = 0; OB_SUCC(ret) && i < batch_count && rowkey_idx < rowkeys_size; ++i) {
        ObRowkey *rowkey;
        ObDocIdExt docid;
        if (OB_ISNULL(rowkey = saved_rowkeys_.at(rowkey_idx++))) {
          ret = OB_ERR_UNEXPECTED;
          LOG_WARN("failed to get rowkey from saved rowkeys.", K(ret));  
        } else if (OB_FAIL(rowkey2docid(*rowkey, docid))) {
        } else if (OB_FAIL(valid_docid_set_.set_refactored(docid))){
        }
      }
    }
  }

  return ret;
}

int ObDASSPIVMergeIter::pre_process(bool is_vectorized)
{
  int ret = OB_SUCCESS;
  ObArenaAllocator &allocator = mem_context_->get_arena_allocator();
  int64_t batch_count = ObVectorParamData::VI_PARAM_DATA_BATCH_SIZE;
  if (OB_FAIL(get_rowkey_pre_filter(allocator, is_vectorized, batch_count))) {
  } else if (saved_rowkeys_.count() < MAX_SPIV_BRUTE_FORCE_SIZE) {
    if (OB_FAIL(do_brute_force(allocator, is_vectorized, batch_count))) {
    } else if (result_docids_.count() != 0) {
      result_docids_curr_iter_ = 0;
    }
  } else if (OB_FAIL(set_valid_docids_with_rowkeys(allocator, batch_count))) {
  } else if (OB_FAIL(get_rowkey_and_set_docids(allocator, is_vectorized, batch_count))) {
  } else {
    // The SQL-private filter port references valid_docid_set_ directly, so no
    // data-plane downcast or state injection is needed after populating it.
  }
  if(OB_SUCC(ret)) {
    is_pre_processed_ = true;
  }
  return ret;
}

int ObDASSPIVMergeIter::do_aux_data_table_scan()
{
  int ret = OB_SUCCESS;

  if (aux_data_table_first_scan_) {
    int idx = get_aux_data_tbl_idx();
    const ObDASScanCtDef *aux_data_ctdef = vec_aux_ctdef_->get_vec_aux_tbl_ctdef(idx, ObTSCIRScanType::OB_VEC_COM_AUX_SCAN);
    ObDASScanRtDef *aux_data_rtdef = vec_aux_rtdef_->get_vec_aux_tbl_rtdef(idx);
    if (OB_FAIL(ObDasVecScanUtils::init_vec_aux_scan_param(aux_data_tablet_id_,
                                                           aux_data_ctdef,
                                                           aux_data_rtdef,
                                                           tx_desc_,
                                                           snapshot_,
                                                           aux_data_scan_param_,
                                                           true/*is_get*/))) {
    } else if (OB_FALSE_IT(aux_data_iter_->set_scan_param(aux_data_scan_param_))) {
    } else if (OB_FAIL(aux_data_iter_->do_table_scan())) {
    } else {
      aux_data_table_first_scan_ = false;
    }
  } else {
    if (OB_FAIL(aux_data_iter_->rescan())) {
    }
  }
  return ret;
}

int ObDASSPIVMergeIter::do_rowkey_docid_table_scan()
{
  int ret = OB_SUCCESS;

  if (rowkey_docid_table_first_scan_) {
    rowkey_docid_scan_param_.need_switch_param_ = false;
    const ObDASScanCtDef *rowkey_docid_ctdef = vec_aux_ctdef_->get_vec_aux_tbl_ctdef(
        vec_aux_ctdef_->get_spiv_rowkey_docid_tbl_idx(), ObTSCIRScanType::OB_VEC_ROWKEY_VID_SCAN); 
    ObDASScanRtDef *rowkey_docid_rtdef = vec_aux_rtdef_->get_vec_aux_tbl_rtdef(vec_aux_ctdef_->get_spiv_rowkey_docid_tbl_idx());
    if (OB_FAIL(ObDasVecScanUtils::init_scan_param(rowkey_docid_tablet_id_,
                                                   rowkey_docid_ctdef,
                                                   rowkey_docid_rtdef,
                                                   tx_desc_,
                                                   snapshot_,
                                                   rowkey_docid_scan_param_))) {
    } else if (OB_FALSE_IT(rowkey_docid_iter_->set_scan_param(rowkey_docid_scan_param_))) {
    } else if (OB_FAIL(rowkey_docid_iter_->do_table_scan())) {
    } else {
      rowkey_docid_table_first_scan_ = false;
    }
  } else {
    if (OB_FAIL(rowkey_docid_iter_->rescan())) {
    }
  }
  return ret;
}

int ObDASSPIVMergeIter::get_vector_from_aux_data_table(ObString &vector)
{
  int ret = OB_SUCCESS;

  ObArenaAllocator &allocator = mem_context_->get_arena_allocator();
  const ObDatumAccessContext *access_ctx = nullptr;
  int idx =get_aux_data_tbl_idx();
  const ObDASScanCtDef *aux_data_ctdef = vec_aux_ctdef_->get_vec_aux_tbl_ctdef(idx, ObTSCIRScanType::OB_VEC_COM_AUX_SCAN);
  common::ObNewRowIterator *table_scan_iter = aux_data_iter_->get_output_result_iter();
  blocksstable::ObDatumRow *datum_row = nullptr;
  
  aux_data_iter_->clear_evaluated_flag();

  const int64_t INVALID_COLUMN_ID = -1;
  int64_t vec_col_idx = INVALID_COLUMN_ID;
  int output_row_cnt = aux_data_ctdef->pd_expr_spec_.access_exprs_.count();
  for (int64_t i = 0; OB_SUCC(ret) && i < output_row_cnt; i++) {
    ObExpr *expr = aux_data_ctdef->pd_expr_spec_.access_exprs_.at(i);
    if (T_REF_COLUMN == expr->type_) {
      if (vec_col_idx == INVALID_COLUMN_ID) {
        vec_col_idx = i;
      } else {
        ret = OB_ERR_UNEXPECTED;
        LOG_WARN("already get vec col idx.", K(ret), K(vec_col_idx), K(i));
      }
    }
  }

  if (vec_col_idx == INVALID_COLUMN_ID) {
    ret = OB_ERR_UNEXPECTED;
    LOG_WARN("failed to get vec col idx.", K(ret));
  } else if (OB_ISNULL(sort_rtdef_) || OB_ISNULL(sort_rtdef_->eval_ctx_)) {
    ret = OB_ERR_UNEXPECTED;
    LOG_WARN("sort runtime definition has no evaluation context", K(ret));
  } else if (OB_FAIL(sort_rtdef_->eval_ctx_->get_datum_access_ctx(access_ctx))) {
  }

  if (OB_FAIL(ret)) {
  } else if (OB_FAIL(data_plane::table_scan_next_datum_row(table_scan_iter, datum_row))) {
    if (OB_ITER_END != ret) {
      LOG_WARN("failed to scan aux data iter", K(ret));
    }
  } else if (datum_row->get_column_count() != output_row_cnt) {
    ret = OB_ERR_UNEXPECTED;
    LOG_WARN("get row column cnt invalid.", K(ret), K(datum_row->get_column_count()));
  } else if (OB_FALSE_IT(vector = datum_row->storage_datums_[vec_col_idx].get_string())) {
    LOG_WARN("failed to get vid.", K(ret));
  } else if (OB_FAIL(ObTextStringHelper::read_real_string_data(
                                                                *access_ctx->lob_read_options_,
                                                                &allocator,
                                                                ObLongTextType, 
                                                                CS_TYPE_BINARY, 
                                                                aux_data_ctdef->result_output_.at(0)->obj_meta_.has_lob_header(), 
                                                                vector))) {
  }

  return ret;
}

int ObDASSPIVMergeIter::get_docid_from_rowkey_docid_table(ObDocIdExt &docid)
{
  int ret = OB_SUCCESS;

  const ObDASScanCtDef *rowkey_docid_ctdef = vec_aux_ctdef_->get_vec_aux_tbl_ctdef(vec_aux_ctdef_->get_spiv_rowkey_docid_tbl_idx(),ObTSCIRScanType::OB_VEC_ROWKEY_VID_SCAN);
  ObDASScanRtDef *rowkey_docid_rtdef = vec_aux_rtdef_->get_vec_aux_tbl_rtdef(vec_aux_ctdef_->get_spiv_rowkey_docid_tbl_idx());

  rowkey_docid_iter_->clear_evaluated_flag();
  if (OB_FAIL(rowkey_docid_iter_->get_next_row())) {
    if (OB_ITER_END != ret) {
      LOG_WARN("failed to scan rowkey docid iter", K(ret));
    }
  } else {
    ObExpr *docid_expr = vec_aux_ctdef_->spiv_scan_docid_col_;
    ObDatum &docid_datum = docid_expr->locate_expr_datum(*rowkey_docid_rtdef->eval_ctx_);
    if (OB_FAIL(docid.from_datum(docid_datum))) {
    } 
  }

  return ret;
}


} // namespace sql
} // namespace oceanbase
