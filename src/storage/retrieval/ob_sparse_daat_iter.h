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
private:
  inline const ObDatum &get_id_datum(const int64_t iter_idx)
  {
    const ObDatum *datum = iter_ids_->at(iter_idx);
    OB_ASSERT(nullptr != datum);
    return *datum;
  }
private:
  common::ObDatumCmpFuncType cmp_func_;
  // TODO: if memory lifetime of docid datum is guaranteed by dim_iters, we can use pointer to datum directly
  //       and avoid deep copy into merge heap here
  const ObFixedArray<const ObDatum *, ObIAllocator> *iter_ids_;
  common::ObObjType id_type_;
  bool is_inited_;
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
  // Fast path for queries with exactly 2 dimensions and no relevance/filter.
  // Bypasses the generic merge heap and relevance collector with a direct two-pointer
  // merge over the two sorted posting lists (intersection for AND, union for OR).
  int do_two_way_merge_round(int64_t &count);
  // Fast path for queries with 3..MAX_FAST_DIMS dimensions and no relevance/filter.
  // Replaces the merge heap with a linear min scan over the sorted posting lists.
  static constexpr int64_t MAX_FAST_DIMS = 4;
  // Advance one dimension iterator and cache its current doc id, so the merge
  // round only re-reads the dims that actually moved instead of all of them.
  int advance_k_way_dim(const int64_t dim_idx);
  int do_k_way_merge_round(int64_t &count);
  // Integer doc-id comparison for the k-way fast path, avoiding the generic
  // datum compare function pointer in the merge hot loop.
  inline int cmp_k_way_int_id(const int64_t l_idx, const int64_t r_idx) const
  {
    int cmp_ret = 0;
    if (OB_UNLIKELY(l_idx < 0 || r_idx < 0)) {
      cmp_ret = 0;
    } else if (k_way_id_unsigned_) {
      const uint64_t l_val = static_cast<uint64_t>(k_way_int_vals_[l_idx]);
      const uint64_t r_val = static_cast<uint64_t>(k_way_int_vals_[r_idx]);
      cmp_ret = (l_val < r_val) ? -1 : ((l_val > r_val) ? 1 : 0);
    } else {
      const int64_t l_val = k_way_int_vals_[l_idx];
      const int64_t r_val = k_way_int_vals_[r_idx];
      cmp_ret = (l_val < r_val) ? -1 : ((l_val > r_val) ? 1 : 0);
    }
    return cmp_ret;
  }
  int fill_merge_heap();
  virtual int collect_dims_by_id(const ObDatum *&id_datum, double &relevance, bool &got_valid_id);
  virtual int process_collected_row(const ObDatum &id_datum, const double relevance);
  virtual int filter_on_demand(const int64_t count, const double relevance, bool &need_project);
  virtual int cache_result(int64_t &count, const ObDatum &id_datum, const double relevance);
  virtual int project_results(const int64_t count);
  int init_merge_heap(const int64_t count);
protected:
  ObIAllocator *iter_allocator_;
  ObSparseRetrievalMergeParam *iter_param_;
  ObIArray<ObISRDaaTDimIter *> *dim_iters_;
  ObSRMergeCmp merge_cmp_;
  ObSRMergeHeap *merge_heap_;
  ObSRDaaTRelevanceCollector *relevance_collector_;
  ObFixedArray<const ObDatum *, ObIAllocator> iter_domain_ids_; // record every dim iter's output domain id, one (ObDatum *) for one dim iter
  ObFixedArray<ObDocIdExt, ObIAllocator> buffered_domain_ids_; // cache for output
  ObFixedArray<double, ObIAllocator> buffered_relevances_;
  ObFixedArray<int64_t, ObIAllocator> next_round_iter_idxes_;
  int64_t next_round_cnt_;
  void (*set_datum_func_)(ObDatum &, const ObDocIdExt &);
  // Two-way fast-path state (2 dims, no relevance, no filter)
  bool two_way_fast_;       // true iff dim_cnt==2, no relevance/filter
  bool two_way_intersect_;  // true for AND (should_match>=dim_cnt), false for OR (union)
  bool two_way_primed_;     // whether both dim iters have been advanced to first row
  bool two_way_iter0_valid_;// whether dim iter 0 has a current row
  bool two_way_iter1_valid_;// whether dim iter 1 has a current row
  // K-way fast-path state (3..MAX_FAST_DIMS dims, no relevance, no filter)
  bool k_way_fast_;
  bool k_way_intersect_;
  bool k_way_primed_;
  bool k_way_valid_[MAX_FAST_DIMS];
  const ObDatum *k_way_ids_[MAX_FAST_DIMS];
  // Raw integer doc-id values, used only when k_way_int_ids_ is true (doc ids
  // are never null in practice; any unexpected null disables this fast path).
  bool k_way_int_ids_;
  bool k_way_id_unsigned_;
  int64_t k_way_int_vals_[MAX_FAST_DIMS];
  int64_t k_way_dim_cnt_;
private:
  DISALLOW_COPY_AND_ASSIGN(ObSRDaaTIterImpl);
};

} // namespace storage
} // namespace oceanbase

#endif
