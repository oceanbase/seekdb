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

#ifndef OB_SPARSE_DAAT_ITER_H_
#define OB_SPARSE_DAAT_ITER_H_

#include "ob_i_sparse_retrieval_iter.h"
#include "ob_sparse_utils.h"
#include "lib/container/ob_loser_tree.h"
#include "sql/das/ob_das_ir_define.h"

namespace oceanbase
{
namespace storage
{

struct ObSRMergeItem
{
  ObSRMergeItem() : relevance_(0.0), iter_idx_(-1), equal_with_next_(false) {}
  ~ObSRMergeItem() = default;
  TO_STRING_KV(K_(iter_idx), K_(relevance), K_(equal_with_next));

  double relevance_;
  int64_t iter_idx_;
  bool equal_with_next_;
};

struct ObSRMergeCmp
{
  ObSRMergeCmp();
  virtual ~ObSRMergeCmp() {}

  int init(ObDatumMeta id_meta, const ObFixedArray<const ObDatum *, ObIAllocator> *iter_ids);
  int cmp(const ObSRMergeItem &l, const ObSRMergeItem &r, int64_t &cmp_ret);
  OB_INLINE int cmp_fast(const ObSRMergeItem &l, const ObSRMergeItem &r, int64_t &cmp_ret)
  {
    int ret = OB_SUCCESS;
    const ObDatum &l_id = get_id_datum(l.iter_idx_);
    const ObDatum &r_id = get_id_datum(r.iter_idx_);
    if (OB_LIKELY(use_binary_string_cmp_ && 0 == l_id.null_ && 0 == r_id.null_)) {
      const int64_t min_len = MIN(l_id.len_, r_id.len_);
      const int byte_cmp = min_len > 0 ? MEMCMP(l_id.ptr_, r_id.ptr_, min_len) : 0;
      cmp_ret = byte_cmp > 0 ? 1 : (byte_cmp < 0 ? -1
          : (l_id.len_ > r_id.len_ ? 1 : (l_id.len_ < r_id.len_ ? -1 : 0)));
    } else {
      int tmp_ret = 0;
      if (OB_FAIL(cmp_func_(l_id, r_id, tmp_ret))) {
      } else {
        cmp_ret = tmp_ret;
      }
    }
    return ret;
  }
private:
  OB_INLINE const ObDatum &get_id_datum(const int64_t iter_idx)
  {
    const ObDatum *datum = iter_id_data_[iter_idx];
    OB_ASSERT(nullptr != datum);
    return *datum;
  }
private:
  common::ObDatumCmpFuncType cmp_func_;
  // TODO: if memory lifetime of docid datum is guaranteed by dim_iters, we can use pointer to datum directly
  //       and avoid deep copy into merge heap here
  const ObFixedArray<const ObDatum *, ObIAllocator> *iter_ids_;
  const ObDatum *const *iter_id_data_;
  bool use_binary_string_cmp_;
  bool is_inited_;
};

class ObSRFastMerger
{
public:
  static const int64_t MAX_ITEM_COUNT = 3;
  ObSRFastMerger() : cmp_(nullptr), table_cnt_(0), item_cnt_(0), items_() {}
  ~ObSRFastMerger() = default;

  void init(ObSRMergeCmp &cmp, const int64_t table_cnt)
  {
    cmp_ = &cmp;
    table_cnt_ = table_cnt;
    item_cnt_ = 0;
  }
  void reset()
  {
    cmp_ = nullptr;
    table_cnt_ = 0;
    item_cnt_ = 0;
  }
  void reuse(const int64_t table_cnt)
  {
    table_cnt_ = table_cnt;
    item_cnt_ = 0;
  }
  OB_INLINE bool empty() const { return 0 == item_cnt_; }
  OB_INLINE bool is_unique_champion() const
  {
    return empty() || !items_[0].equal_with_next_;
  }
  OB_INLINE int top(const ObSRMergeItem *&item) const
  {
    const int ret = empty() ? OB_EMPTY_RESULT : OB_SUCCESS;
    if (OB_SUCCESS == ret) {
      item = &items_[0];
    }
    return ret;
  }
  OB_INLINE int pop()
  {
    int ret = OB_SUCCESS;
    if (OB_UNLIKELY(empty())) {
      ret = OB_EMPTY_RESULT;
    } else {
      for (int64_t i = 1; i < item_cnt_; ++i) {
        items_[i - 1] = items_[i];
      }
      --item_cnt_;
    }
    return ret;
  }
  OB_INLINE int push(const ObSRMergeItem &item)
  {
    int ret = OB_SUCCESS;
    if (OB_UNLIKELY(nullptr == cmp_)) {
      ret = OB_NOT_INIT;
    } else if (OB_UNLIKELY(item_cnt_ >= table_cnt_ || item_cnt_ >= MAX_ITEM_COUNT)) {
      ret = OB_SIZE_OVERFLOW;
    } else if (0 == item_cnt_) {
      items_[0] = item;
      items_[0].equal_with_next_ = false;
      ++item_cnt_;
    } else {
      int64_t cmp_ret = 0;
      int64_t pos = item_cnt_;
      bool equal_with_next = false;
      while (OB_SUCC(ret) && pos > 0) {
        if (OB_FAIL(cmp_->cmp_fast(item, items_[pos - 1], cmp_ret))) {
        } else if (cmp_ret < 0) {
          items_[pos] = items_[pos - 1];
          --pos;
        } else if (0 == cmp_ret && item.iter_idx_ < items_[pos - 1].iter_idx_) {
          items_[pos] = items_[pos - 1];
          equal_with_next = true;
          --pos;
        } else {
          if (0 == cmp_ret) {
            items_[pos - 1].equal_with_next_ = true;
          }
          break;
        }
      }
      if (OB_SUCC(ret)) {
        items_[pos] = item;
        items_[pos].equal_with_next_ = equal_with_next;
        ++item_cnt_;
      }
    }
    return ret;
  }
private:
  ObSRMergeCmp *cmp_;
  int64_t table_cnt_;
  int64_t item_cnt_;
  ObSRMergeItem items_[MAX_ITEM_COUNT];
};

typedef ObSimpleRowsMerger<ObSRMergeItem, ObSRMergeCmp> ObSRSimpleMerger;
typedef ObMergeLoserTree<ObSRMergeItem, ObSRMergeCmp> ObSRLoserTree;
typedef common::ObRowsMerger<ObSRMergeItem, ObSRMergeCmp> ObSRMergeHeap;

// implementation of basic DaaT query processing algorithm primitives
class ObSRDaaTIterImpl : public ObISparseRetrievalMergeIter
{
public:
  ObSRDaaTIterImpl();
  virtual ~ObSRDaaTIterImpl() {}
  virtual int get_next_row() override;
  virtual int get_next_rows(const int64_t capacity, int64_t &count) override;
  int init(
      ObSparseRetrievalMergeParam &iter_param,
      ObIArray<ObISRDaaTDimIter *> &dim_iters,
      ObIAllocator &iter_allocator,
      ObSRDaaTRelevanceCollector &relevance_collector);
  virtual void reuse(const bool switch_tablet = false) override;
  virtual void reset() override;
  virtual int get_query_max_score(double &score) override;
  
  INHERIT_TO_STRING_KV("ObISparseRetrievalMergeIter", ObISparseRetrievalMergeIter,
      K_(next_round_iter_idxes), K_(next_round_cnt));
protected:
  virtual int pre_process();
  virtual int do_one_merge_round(int64_t &count);
  virtual int fill_merge_heap();
  virtual int collect_dims_by_id(const ObDatum *&id_datum, double &relevance, bool &got_valid_id);
  virtual int process_collected_row(const ObDatum &id_datum, const double relevance);
  virtual int filter_on_demand(const int64_t count, const double relevance, bool &need_project);
  virtual int cache_result(int64_t &count, const ObDatum &id_datum, const double relevance);
  virtual int project_results(const int64_t count);
  int init_merge_heap(const int64_t count);
protected:
  OB_INLINE bool merge_empty() const
  {
    return use_fast_merge_ ? fast_merge_.empty() : merge_heap_->empty();
  }
  OB_INLINE bool merge_is_unique_champion() const
  {
    return use_fast_merge_ ? fast_merge_.is_unique_champion() : merge_heap_->is_unique_champion();
  }
  OB_INLINE int merge_top(const ObSRMergeItem *&item) const
  {
    return use_fast_merge_ ? fast_merge_.top(item) : merge_heap_->top(item);
  }
  OB_INLINE int merge_pop()
  {
    return use_fast_merge_ ? fast_merge_.pop() : merge_heap_->pop();
  }
  OB_INLINE int merge_push(const ObSRMergeItem &item)
  {
    return use_fast_merge_ ? fast_merge_.push(item) : merge_heap_->push(item);
  }
  OB_INLINE int merge_rebuild()
  {
    return use_fast_merge_ ? OB_SUCCESS : merge_heap_->rebuild();
  }
  OB_INLINE ObISRDaaTDimIter *get_dim_iter(const int64_t iter_idx) const
  {
    return dim_iter_data_[iter_idx];
  }
  ObIAllocator *iter_allocator_;
  ObSparseRetrievalMergeParam *iter_param_;
  ObIArray<ObISRDaaTDimIter *> *dim_iters_;
  ObFixedArray<ObISRDaaTDimIter *, ObIAllocator> dim_iter_cache_;
  ObISRDaaTDimIter **dim_iter_data_;
  int64_t dim_iter_cnt_;
  ObSRMergeCmp merge_cmp_;
  ObSRFastMerger fast_merge_;
  ObSRMergeHeap *merge_heap_;
  bool use_fast_merge_;
  ObSRDaaTRelevanceCollector *relevance_collector_;
  ObFixedArray<const ObDatum *, ObIAllocator> iter_domain_ids_; // record every dim iter's output domain id, one (ObDatum *) for one dim iter
  ObFixedArray<ObDocIdExt, ObIAllocator> buffered_domain_ids_; // cache for output
  ObFixedArray<double, ObIAllocator> buffered_relevances_;
  ObFixedArray<int64_t, ObIAllocator> next_round_iter_idxes_;
  const ObDatum **iter_domain_id_data_;
  ObDocIdExt *buffered_domain_id_data_;
  double *buffered_relevance_data_;
  int64_t *next_round_iter_idx_data_;
  int64_t next_round_cnt_;
  void (*set_datum_func_)(ObDatum &, const ObDocIdExt &);
  bool use_fast_bool_filter_;
private:
  DISALLOW_COPY_AND_ASSIGN(ObSRDaaTIterImpl);
};

} // namespace storage
} // namespace oceanbase

#endif
