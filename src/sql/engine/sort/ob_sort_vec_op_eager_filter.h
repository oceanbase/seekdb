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

#ifndef OCEANBASE_SQL_ENGINE_SORT_SORT_VEC_OP_EAGER_FILTER_H_
#define OCEANBASE_SQL_ENGINE_SORT_SORT_VEC_OP_EAGER_FILTER_H_
#include "sql/engine/sort/ob_sort_compare_vec_op.h"
#include "sql/engine/sort/ob_sort_vec_op_store_row_factory.h"
namespace oceanbase {
namespace sql {
// ObSortVecOpEagerFilter implementation class
template <typename Compare, typename Store_Row, bool has_addon>
class ObSortVecOpEagerFilter {
public:
  static const int64_t MAX_BUCKET_NUM = 100;
  static const int64_t MIN_BUCKET_NUM = 10;
  static constexpr double FILTER_RATIO = 0.05; // limit the ratio of memory used

  typedef common::ObBinaryHeap<Store_Row *, Compare, MAX_BUCKET_NUM> BucketHeap;

  explicit ObSortVecOpEagerFilter(
      common::ObIAllocator &allocator,
      ObSortVecOpStoreRowFactory<Store_Row, has_addon> &store_row_factory)
      : allocator_(allocator), bucket_num_(0), bucket_size_(0),
        store_row_factory_(store_row_factory), bucket_heap_(nullptr),
        comp_(nullptr), is_inited_(false), is_by_pass_(false), output_brs_() {}

  int filter(common::ObFixedArray<ObExpr *, common::ObIAllocator> &exprs,
             ObEvalCtx &eval_ctx, const int64_t start_pos,
             const ObBatchRows &input_brs);
  int update_filter(const Store_Row *dumped_bucket, bool &updated);

private:
  int init_output_brs(const int64_t max_batch_size);

public:
  int init(Compare &comp, const int64_t dumped_rows_cnt, const int64_t topn_cnt,
           const int64_t max_batch_size) {
    int ret = OB_SUCCESS;
    if (is_inited()) {
      ret = OB_INIT_TWICE;
      SQL_ENG_LOG(WARN, "ObSortVecOpEagerFilter has already been initialized", K(ret));
    } else if (!comp.is_inited() ||
               OB_ISNULL(bucket_heap_ =
                             OB_NEWx(BucketHeap, &allocator_, comp)) ||
               topn_cnt < 0) {
      ret = OB_ERR_UNEXPECTED;
      SQL_ENG_LOG(WARN, "failed to init ObSortVecOpEagerFilter", K(ret));
    } else {
      comp_ = &comp;
      bucket_num_ =
          min(static_cast<int64_t>(floor(dumped_rows_cnt * FILTER_RATIO)),
              MAX_BUCKET_NUM);
      // if the number of buckets is very small (e.g. 1), then this optimization
      // is unnecessary
      bucket_num_ = bucket_num_ < MIN_BUCKET_NUM ? 0 : bucket_num_;
      if (bucket_num_ != 0) {
        bucket_size_ = (topn_cnt + bucket_num_ - 1) / bucket_num_;
        if (OB_FAIL(init_output_brs(max_batch_size))) {
          SQL_ENG_LOG(WARN, "init ouput batch rows failed", K(ret));
        }
      } else {
        is_by_pass_ = true;
        SQL_ENG_LOG(INFO, "no need to use filter ", K(dumped_rows_cnt),
                    K(topn_cnt));
      }
      is_inited_ = true;
    }
    return ret;
  }
  inline bool is_inited() const { return is_inited_; }
  inline bool is_by_pass() const { return is_by_pass_; }
  inline const ObBatchRows &get_output_brs() const { return output_brs_; }
  int64_t bucket_size() const { return bucket_size_; }
  void reset();

protected:
  common::ObIAllocator &allocator_;
  int64_t bucket_num_;
  int64_t bucket_size_;
  ObSortVecOpStoreRowFactory<Store_Row, has_addon> &store_row_factory_;
  BucketHeap *bucket_heap_;
  Compare *comp_;
  bool is_inited_;
  bool is_by_pass_;
  ObBatchRows output_brs_;
};
// This function was originally defined in CPP, [because of the effect of UNITY merging compilation units, it compiled successfully, but the implementation of template code needs to be defined in the header file], therefore, closing UNITY led to observer failing to compile
// To solve the compilation problem after closing UNITY, move it to the header file
// But this function uses OZ, CK macros, these two macros internal log print used LOG_WARN, requirement must define USING_LOG_PREFIX
// Since this is a header file, this will lead to very tricky problems:
// 1. If USING_LOG_PREFIX is not defined before this header file, it must be redefined (but defining the macro in the header file will cause pollution)
// 2. If USING_LOG_PREFIX is newly defined in this file, it needs to be cleaned up to prevent pollution from spreading to other .h and cpp files
// Therefore here we check if USING_LOG_PREFIX is already defined, if it is defined then we abandon redefining it (this means logs are not always printed with the "SQL_RESV" identifier), and also define a special identifier
// If a special identifier is found, execute macro cleanup actions during preprocessing
// The entire logic is quite tricky, is to minimize code logic modification, code owner needs to refactor this logic later
#ifndef USING_LOG_PREFIX
#define MARK_MACRO_DEFINED_BY_OB_SORT_VEC_OP_EAGER_FILTER_H
#define USING_LOG_PREFIX SQL_RESV
#endif
template <typename Compare, typename Store_Row, bool has_addon>
int ObSortVecOpEagerFilter<Compare, Store_Row, has_addon>::init_output_brs(
    const int64_t max_batch_size) {
  int ret = OB_SUCCESS;
  if (OB_ISNULL(output_brs_.skip_)) {
    void *buf = allocator_.alloc(ObBitVector::memory_size(max_batch_size));
    if (OB_ISNULL(buf)) {
      ret = OB_ALLOCATE_MEMORY_FAILED;
      SQL_ENG_LOG(WARN, "allocate memory failed", K(ret));
    } else {
      output_brs_.skip_ = to_bit_vector(buf);
    }
  } else {
    ret = OB_ERR_UNEXPECTED;
    SQL_ENG_LOG(WARN, "output_brs_ init twice", K(ret));
  }
  return ret;
}

template <typename Compare, typename Store_Row, bool has_addon>
int ObSortVecOpEagerFilter<Compare, Store_Row, has_addon>::filter(
    common::ObFixedArray<ObExpr *, common::ObIAllocator> &exprs,
    ObEvalCtx &eval_ctx, const int64_t start_pos,
    const ObBatchRows &input_brs) {
  int ret = OB_SUCCESS;
  if (OB_UNLIKELY(!is_inited())) {
    ret = OB_ERR_UNEXPECTED;
    LOG_WARN("ObSortVecOpEagerFilter is not initialized", K(ret));
  } else if (is_by_pass()) {
    // do nothing
  } else {
    output_brs_.copy(&input_brs);
    if (bucket_heap_->count() < bucket_num_) {
    } else {
      ObEvalCtx::BatchInfoScopeGuard batch_info_guard(eval_ctx);
      batch_info_guard.set_batch_size(output_brs_.size_);
      for (int64_t i = start_pos; OB_SUCC(ret) && i < output_brs_.size_; ++i) {
        if (output_brs_.skip_->exist(i)) {
          continue;
        }
        batch_info_guard.set_batch_idx(i);
        const bool less = (*comp_)(bucket_heap_->top(), eval_ctx);
        if (!less) {
          output_brs_.set_skip(i);
        }
        ret = comp_->ret_;
      }
      if (ret != OB_SUCCESS) {
        LOG_WARN("failed to eager filter rows in topn operator", K(ret));
      }
    }
  }
  return ret;
}

template <typename Compare, typename Store_Row, bool has_addon>
int ObSortVecOpEagerFilter<Compare, Store_Row, has_addon>::update_filter(
    const Store_Row *bucket_head_row, bool &updated) {
  int ret = OB_SUCCESS;
  if (!is_inited()) {
    ret = OB_ERR_UNEXPECTED;
    LOG_WARN("ObSortVecOpEagerFilter is not initialized", K(ret));
  } else if (is_by_pass()) {
    updated = false;
  } else {
    Store_Row *reuse_row = nullptr;
    if (bucket_heap_->count() < bucket_num_) {
      if (OB_FAIL(store_row_factory_.copy_to_row(bucket_head_row, reuse_row))) {
        bucket_heap_->top() = reuse_row;
        LOG_WARN("failed to generate new row", K(ret));
      } else {
        int64_t topn_heap_size = bucket_heap_->count();
        if (OB_FAIL(bucket_heap_->push(reuse_row))) {
          ret = OB_ERR_UNEXPECTED;
          LOG_WARN("failed to push back row", K(ret));
          if (bucket_heap_->count() == topn_heap_size) {
            store_row_factory_.free_row_store(reuse_row);
          }
        }
      }
      updated = true;
    } else {
      reuse_row = bucket_heap_->top();
      const bool less = (*comp_)(bucket_head_row, reuse_row);
      if (OB_SUCCESS != comp_->ret_) {
        ret = comp_->ret_;
        LOG_WARN("failed to compare", K(ret));
      } else if (less) {
        if (OB_FAIL(
                store_row_factory_.copy_to_row(bucket_head_row, reuse_row))) {
          LOG_WARN("failed to generate new row", K(ret));
        } else if (OB_FAIL(bucket_heap_->replace_top(reuse_row))) {
          LOG_WARN("failed to replace heap top element", K(ret));
        }
        updated = true;
      } else {
        updated = false;
      }
    }
  }
  return ret;
}

template <typename Compare, typename Store_Row, bool has_addon>
void ObSortVecOpEagerFilter<Compare, Store_Row, has_addon>::reset() {
  if (nullptr != bucket_heap_) {
    for (int64_t i = 0; i < bucket_heap_->count(); i++) {
      if (OB_NOT_NULL(bucket_heap_->at(i))) {
        store_row_factory_.free_row_store(bucket_heap_->at(i));
        bucket_heap_->at(i) = nullptr;
      }
    }
    bucket_heap_->reset();
    bucket_heap_->~BucketHeap();
    allocator_.free(bucket_heap_);
    bucket_heap_ = nullptr;
  }
  if (nullptr != output_brs_.skip_) {
    allocator_.free(output_brs_.skip_);
    output_brs_.skip_ = nullptr;
  }
  bucket_size_ = 0;
  bucket_num_ = 0;
  comp_ = nullptr;
  is_inited_ = false;
}
#ifdef MARK_MACRO_DEFINED_BY_OB_SORT_VEC_OP_EAGER_FILTER_H
#undef USING_LOG_PREFIX
#endif

} // end namespace sql
} // end namespace oceanbase
#endif /* OCEANBASE_SQL_ENGINE_SORT_SORT_VEC_OP_EAGER_FILTER_H_ */
