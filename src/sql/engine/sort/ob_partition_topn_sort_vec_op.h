/**
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

#ifndef OCEANBASE_SQL_ENGINE_SORT_OB_PARTITION_TOPN_SORT_VEC_OP_H_
#define OCEANBASE_SQL_ENGINE_SORT_OB_PARTITION_TOPN_SORT_VEC_OP_H_

#include "share/rc/ob_tenant_base.h"
#include "lib/container/ob_array.h"
#include "sql/engine/sort/ob_sort_row_store_mgr.h"
#include "sql/engine/sort/ob_sort_vec_op_context.h"
#include "sql/engine/sort/ob_sort_key_fetcher_vec_op.h"
#include "sql/engine/ob_sql_mem_mgr_processor.h"

namespace oceanbase
{
namespace sql
{

template <typename Compare, typename Store_Row, bool has_addon>
class ObPartitionTopNSort
{
public:
  class CopyableComparer
  {
  public:
    explicit CopyableComparer(Compare &compare) : compare_(compare) {}
    bool operator()(const Store_Row *l, const Store_Row *r)
    {
      return compare_(l, r);
    }
  private:
    Compare &compare_;
  };

  ObPartitionTopNSort(
      ObIAllocator &allocator,
      lib::MemoryContext &mem_context,
      ObSqlWorkAreaProfile &profile,
      Compare &comp,
      ObSortKeyFetcher &sort_exprs_getter,
      ObSqlMemMgrProcessor &sql_mem_processor,
      int64_t &inmem_row_size,
      int64_t &outputted_rows_cnt)
    : allocator_(allocator),
      store_mgr_(allocator),
      comp_(comp),
      sk_exprs_(nullptr),
      addon_exprs_(nullptr),
      eval_ctx_(nullptr),
      sk_row_meta_(nullptr),
      addon_row_meta_(nullptr),
      topn_cnt_(INT64_MAX),
      part_cnt_(0),
      max_batch_size_(0),
      rows_(nullptr),
      iter_idx_(0),
      current_part_count_(0),
      last_row_(nullptr),
      sorted_(false),
      inmem_row_size_(inmem_row_size),
      outputted_rows_cnt_(outputted_rows_cnt),
      sk_rows_(allocator),
      addon_rows_(allocator),
      sk_vec_ptrs_(allocator),
      addon_vec_ptrs_(allocator),
      is_inited_(false)
  {
    UNUSEDx(mem_context, profile, sort_exprs_getter, sql_mem_processor);
  }

  int init(
      ObSortVecOpContext &ctx,
      ObIAllocator *page_allocator,
      ObIArray<ObExpr *> *all_exprs,
      const RowMeta *sk_row_meta,
      const RowMeta *addon_row_meta)
  {
    int ret = OB_SUCCESS;
    UNUSEDx(page_allocator, all_exprs);
    if (OB_UNLIKELY(is_inited_)) {
      ret = OB_INIT_TWICE;
      SQL_ENG_LOG(WARN, "partition topn sort init twice", K(ret));
    } else if (OB_ISNULL(ctx.sk_exprs_) || OB_ISNULL(ctx.eval_ctx_) || OB_ISNULL(sk_row_meta)) {
      ret = OB_INVALID_ARGUMENT;
      SQL_ENG_LOG(WARN, "invalid init argument", K(ret), KP(ctx.sk_exprs_), KP(ctx.eval_ctx_), KP(sk_row_meta));
    } else {
      sk_exprs_ = ctx.sk_exprs_;
      addon_exprs_ = has_addon ? ctx.addon_exprs_ : nullptr;
      eval_ctx_ = ctx.eval_ctx_;
      sk_row_meta_ = sk_row_meta;
      addon_row_meta_ = has_addon ? addon_row_meta : nullptr;
      topn_cnt_ = ctx.topn_cnt_;
      part_cnt_ = ctx.part_cnt_;
      max_batch_size_ = eval_ctx_->max_batch_size_;
      const uint64_t tenant_id = MTL_ID();
      if (OB_FAIL(sk_rows_.prepare_allocate(max_batch_size_))) {
        SQL_ENG_LOG(WARN, "prepare sk row buffer failed", K(ret), K(max_batch_size_));
      } else if (has_addon && OB_FAIL(addon_rows_.prepare_allocate(max_batch_size_))) {
        SQL_ENG_LOG(WARN, "prepare addon row buffer failed", K(ret), K(max_batch_size_));
      } else if (OB_FAIL(init_vec_ptrs(*sk_exprs_, sk_vec_ptrs_))) {
        SQL_ENG_LOG(WARN, "init sort key vectors failed", K(ret));
      } else if (has_addon && OB_NOT_NULL(addon_exprs_) && OB_FAIL(init_vec_ptrs(*addon_exprs_, addon_vec_ptrs_))) {
        SQL_ENG_LOG(WARN, "init addon vectors failed", K(ret));
      } else if (OB_FAIL(store_mgr_.init(
                     *sk_exprs_,
                     has_addon ? addon_exprs_ : nullptr,
                     max_batch_size_,
                     false /*need_callback*/,
                     Store_Row::get_extra_size(true),
                     Store_Row::get_extra_size(false),
                     tenant_id,
                     INT64_MAX,
                     false /*enable_dump*/,
                     ctx.compress_type_))) {
        SQL_ENG_LOG(WARN, "init store manager failed", K(ret), K(tenant_id), K(max_batch_size_));
      } else {
        is_inited_ = true;
      }
    }
    return ret;
  }

  int add_batch(
      const ObBatchRows &input_brs,
      const int64_t start_pos,
      int64_t *append_row_count,
      bool need_load_data,
      common::ObIArray<Store_Row *> *&rows)
  {
    int ret = OB_SUCCESS;
    int64_t stored_row_cnt = 0;
    int64_t inmem_row_size = 0;
    UNUSED(need_load_data);
    if (OB_UNLIKELY(!is_inited_)) {
      ret = OB_NOT_INIT;
    } else if (OB_ISNULL(rows)) {
      ret = OB_INVALID_ARGUMENT;
    } else if (OB_FAIL(store_mgr_.add_batch(
                   *sk_exprs_,
                   has_addon ? addon_exprs_ : nullptr,
                   *eval_ctx_,
                   input_brs,
                   stored_row_cnt,
                   sk_rows_.get_data(),
                   has_addon ? addon_rows_.get_data() : nullptr,
                   inmem_row_size,
                   start_pos))) {
      SQL_ENG_LOG(WARN, "add partition topn batch failed", K(ret), K(start_pos));
    } else {
      rows_ = rows;
      sorted_ = false;
      inmem_row_size_ += inmem_row_size;
      for (int64_t i = 0; OB_SUCC(ret) && i < stored_row_cnt; ++i) {
        if (OB_FAIL(rows->push_back(sk_rows_.at(i)))) {
          SQL_ENG_LOG(WARN, "push partition topn row failed", K(ret), K(i), K(stored_row_cnt));
        }
      }
      if (OB_NOT_NULL(append_row_count)) {
        *append_row_count = stored_row_cnt;
      }
    }
    return ret;
  }

  int add_batch(
      const ObBatchRows &input_brs,
      const uint16_t selector[],
      const int64_t size,
      common::ObIArray<Store_Row *> *&rows,
      Store_Row **sk_rows)
  {
    int ret = OB_SUCCESS;
    int64_t inmem_row_size = 0;
    UNUSED(input_brs);
    if (OB_UNLIKELY(!is_inited_)) {
      ret = OB_NOT_INIT;
    } else if (OB_ISNULL(rows) || OB_ISNULL(sk_rows)) {
      ret = OB_INVALID_ARGUMENT;
    } else if (OB_FAIL(store_mgr_.add_batch(
                   sk_vec_ptrs_,
                   has_addon ? &addon_vec_ptrs_ : nullptr,
                   selector,
                   size,
                   sk_rows,
                   has_addon ? addon_rows_.get_data() : nullptr,
                   inmem_row_size))) {
      SQL_ENG_LOG(WARN, "add selector batch failed", K(ret), K(size));
    } else {
      rows_ = rows;
      sorted_ = false;
      inmem_row_size_ += inmem_row_size;
      for (int64_t i = 0; OB_SUCC(ret) && i < size; ++i) {
        if (OB_FAIL(rows->push_back(sk_rows[i]))) {
          SQL_ENG_LOG(WARN, "push selector row failed", K(ret), K(i), K(size));
        }
      }
    }
    return ret;
  }

  int do_sort()
  {
    int ret = OB_SUCCESS;
    if (OB_UNLIKELY(!is_inited_)) {
      ret = OB_NOT_INIT;
    } else if (OB_ISNULL(rows_)) {
      ret = OB_ERR_UNEXPECTED;
    } else if (rows_->count() > 1) {
      lib::ob_sort(&rows_->at(0), &rows_->at(0) + rows_->count(), CopyableComparer(comp_));
      if (OB_SUCCESS != comp_.ret_) {
        ret = comp_.ret_;
      }
    }
    if (OB_SUCC(ret)) {
      reset_row_idx();
      sorted_ = true;
    }
    return ret;
  }

  int next_stored_row(const Store_Row *&sk_row)
  {
    return part_topn_next_stored_row(sk_row);
  }

  int part_topn_next_stored_row(const Store_Row *&sk_row)
  {
    int ret = OB_SUCCESS;
    if (OB_UNLIKELY(!is_inited_)) {
      ret = OB_NOT_INIT;
    } else if (!sorted_ && OB_FAIL(do_sort())) {
      SQL_ENG_LOG(WARN, "sort partition topn rows failed", K(ret));
    } else {
      ret = get_next_partition_topn_row(sk_row);
      if (OB_SUCC(ret)) {
        ++outputted_rows_cnt_;
      }
    }
    return ret;
  }

  int part_topn_node_next(
      int64_t &cur_topn_node_array_idx,
      int64_t &cur_topn_node_idx,
      const Store_Row *&store_row,
      const Store_Row *&addon_row)
  {
    int ret = part_topn_next_stored_row(store_row);
    if (OB_SUCC(ret)) {
      cur_topn_node_array_idx = 0;
      cur_topn_node_idx = iter_idx_;
      if (has_addon) {
        addon_row = store_row->get_addon_ptr(*sk_row_meta_);
      }
    }
    return ret;
  }

  void reset_row_idx()
  {
    iter_idx_ = 0;
    current_part_count_ = 0;
    last_row_ = nullptr;
  }

  int64_t get_ht_bucket_size() const
  {
    return 0;
  }

  int64_t get_need_extra_mem_size() const
  {
    return 0;
  }

  void reset()
  {
    reuse();
  }

  void reuse()
  {
    rows_ = nullptr;
    sorted_ = false;
    reset_row_idx();
    store_mgr_.reuse();
  }

private:
  int init_vec_ptrs(
      const common::ObIArray<ObExpr *> &exprs,
      common::ObFixedArray<ObIVector *, common::ObIAllocator> &vec_ptrs)
  {
    int ret = OB_SUCCESS;
    vec_ptrs.reset();
    if (OB_FAIL(vec_ptrs.init(exprs.count()))) {
      SQL_ENG_LOG(WARN, "init vector array failed", K(ret), K(exprs.count()));
    } else {
      for (int64_t i = 0; OB_SUCC(ret) && i < exprs.count(); ++i) {
        if (OB_FAIL(vec_ptrs.push_back(exprs.at(i)->get_vector(*eval_ctx_)))) {
          SQL_ENG_LOG(WARN, "push vector failed", K(ret), K(i));
        }
      }
    }
    return ret;
  }

  int is_same_partition(const Store_Row *lhs, const Store_Row *rhs, bool &same)
  {
    int ret = OB_SUCCESS;
    same = false;
    if (0 >= part_cnt_) {
      same = false;
    } else if (OB_ISNULL(lhs) || OB_ISNULL(rhs)) {
      ret = OB_INVALID_ARGUMENT;
    } else {
      const int64_t old_cmp_start = comp_.cmp_start_;
      const int64_t old_cmp_end = comp_.cmp_end_;
      comp_.set_cmp_range(0, part_cnt_);
      const bool less_lr = comp_(lhs, rhs);
      if (OB_SUCCESS != comp_.ret_) {
        ret = comp_.ret_;
      } else {
        const bool less_rl = comp_(rhs, lhs);
        if (OB_SUCCESS != comp_.ret_) {
          ret = comp_.ret_;
        } else {
          same = !less_lr && !less_rl;
        }
      }
      comp_.set_cmp_range(old_cmp_start, old_cmp_end);
    }
    return ret;
  }

  int get_next_partition_topn_row(const Store_Row *&sk_row)
  {
    int ret = OB_ITER_END;
    sk_row = nullptr;
    while (iter_idx_ < rows_->count()) {
      const Store_Row *candidate = rows_->at(iter_idx_++);
      bool same_partition = false;
      if (OB_ISNULL(last_row_)) {
        current_part_count_ = 0;
      } else if (OB_FAIL(is_same_partition(last_row_, candidate, same_partition))) {
        SQL_ENG_LOG(WARN, "check partition equality failed", K(ret));
        break;
      } else if (!same_partition) {
        current_part_count_ = 0;
      }
      last_row_ = candidate;
      if (current_part_count_ < topn_cnt_) {
        ++current_part_count_;
        sk_row = candidate;
        ret = OB_SUCCESS;
        break;
      }
    }
    return ret;
  }

private:
  ObIAllocator &allocator_;
  ObSortRowStoreMgr<Store_Row, has_addon> store_mgr_;
  Compare &comp_;
  const common::ObIArray<ObExpr *> *sk_exprs_;
  const common::ObIArray<ObExpr *> *addon_exprs_;
  ObEvalCtx *eval_ctx_;
  const RowMeta *sk_row_meta_;
  const RowMeta *addon_row_meta_;
  int64_t topn_cnt_;
  int64_t part_cnt_;
  int64_t max_batch_size_;
  common::ObIArray<Store_Row *> *rows_;
  int64_t iter_idx_;
  int64_t current_part_count_;
  const Store_Row *last_row_;
  bool sorted_;
  int64_t &inmem_row_size_;
  int64_t &outputted_rows_cnt_;
  common::ObArray<Store_Row *> sk_rows_;
  common::ObArray<Store_Row *> addon_rows_;
  common::ObFixedArray<ObIVector *, common::ObIAllocator> sk_vec_ptrs_;
  common::ObFixedArray<ObIVector *, common::ObIAllocator> addon_vec_ptrs_;
  bool is_inited_;
};

} // namespace sql
} // namespace oceanbase

#endif /* OCEANBASE_SQL_ENGINE_SORT_OB_PARTITION_TOPN_SORT_VEC_OP_H_ */
