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

#ifndef OB_SPARSE_BMW_ITER_H_
#define OB_SPARSE_BMW_ITER_H_

#include "lib/container/ob_heap.h"
#include "ob_sparse_daat_iter.h"
#include "ob_sparse_retrieval_util.h"

namespace oceanbase
{
namespace storage
{
class ObSRBMWIterImpl : public ObSRDaaTIterImpl
{
public:
  ObSRBMWIterImpl();
  virtual ~ObSRBMWIterImpl() {}

  virtual void reuse(const bool switch_tablet = false) override;
  void reset();
  int init(
      ObSparseRetrievalMergeParam &iter_param,
      ObIArray<ObISRDaaTDimIter *> &dim_iters,
      ObIAllocator &iter_allocator,
      ObSRDaaTRelevanceCollector &relevance_collector);
  virtual int get_next_rows(const int64_t capacity, int64_t &count) override;
protected:
  virtual int process_collected_row(const ObDatum &id_datum, const double relevance) override;
  // BMW-specific collect: skip shallow items (block-level placeholder with
  // relevance=0 and id = block-min aggregated lower bound) and only collect
  // deep items whose domain id equals the first collected item's id, so that
  // (id, score) pairs never get mismatched by leftover shallow placeholders.
  virtual int collect_dims_by_id(const ObDatum *&id_datum, double &relevance, bool &got_valid_id) override;
  virtual int init_before_wand_process() { return OB_SUCCESS; }

  int top_k_search();
  int build_top_k_heap();
  int next_pivot(int64_t &pivot_iter_idx);
  int next_pivot_range(int64_t &skip_range_cnt);
  int evaluate_pivot(const int64_t pivot_iter_idx);
  int evaluate_pivot_range(const int64_t pivot_iter_idx, bool &is_candidate);
  double get_top_k_threshold()
  {
    return top_k_heap_.empty() ? 0.0 : top_k_heap_.top().relevance_;
  }

private:
  ObISRDimBlockMaxIter *get_iter(const int64_t iter_idx) const
  {
    return static_cast<ObISRDimBlockMaxIter *>(dim_iters_->at(iter_idx));
  }
  int fill_merge_heap_with_shallow_dims(const ObDatum *last_range_border_id, const bool inclusive);
  int try_generate_next_range_from_merge_heap(
      bool &is_candidate_range,
      const ObDatum *&min_domain_id_with_pivot,
      const ObDatum *&max_domain_id_without_pivot);
  int project_rows_from_top_k_heap(const int64_t capacity, int64_t &count);
  int unify_dim_iters_for_next_round();
  int advance_dim_iters_for_next_round(const ObDatum &target_id, const bool iter_end_available);
  void set_next_round_iter_end(const int64_t idx) { next_round_iter_idxes_[idx] = -1; }
  bool is_next_round_iter_end(const int64_t idx) const { return next_round_iter_idxes_[idx] == -1; }

protected:
  enum BMWStatus{
    FIND_NEXT_PIVOT,
    EVALUATE_PIVOT_RANGE,
    EVALUATE_PIVOT,
    FIND_NEXT_PIVOT_RANGE,
    FINISHED,
    MAX_STATUS,
  };
  struct TopKItem {
    TopKItem() : relevance_(0.0), cache_idx_(-1), docid_(0) {}
    TopKItem(const double &relevance, const int64_t &cache_idx, const int64_t &docid)
        : relevance_(relevance), cache_idx_(cache_idx), docid_(docid) {}
    ~TopKItem() = default;
    TO_STRING_KV(K_(relevance), K_(cache_idx), K_(docid));
    double relevance_;
    int64_t cache_idx_;
    int64_t docid_;
  };

  // max-heap semantics on the comparator: ObBinaryHeap keeps the element
  // that is "largest" according to the comparator at top(). We want top()
  // to be the worst candidate to evict, so a candidate is "larger" when it
  // is worse: worst = smallest relevance; among equal relevance, largest docid.
  struct TopKItemCmp {
    bool operator()(const TopKItem &a, const TopKItem &b) const {
      // ObBinaryHeap keeps the "greatest" element at top() w.r.t. this less-like
      // comparator. For top-k we want top() to be the worst candidate: lowest
      // score, and among equal scores the largest docid (so a better candidate
      // replaces it).
      if (a.relevance_ != b.relevance_) {
        return a.relevance_ > b.relevance_;
      }
      return a.docid_ < b.docid_;
    }
    int get_error_code() { return common::OB_SUCCESS; }
  };
  typedef common::ObBinaryHeap<TopKItem, TopKItemCmp> TopKHeap;

  DISALLOW_COPY_AND_ASSIGN(ObSRBMWIterImpl);
  ObArenaAllocator allocator_;
  TopKItemCmp less_score_cmp_;
  // Maybe domain id not need to be cached within the topk heap since there would be top-n sort with datum store above
  TopKHeap top_k_heap_;
  int64_t top_k_count_;
  ObDomainIdCmp domain_id_cmp_;
  ObFixedArray<ObDocIdExt, ObIAllocator> id_cache_;
  BMWStatus status_;
};


} // namespace storage
} // namespace oceanbase
#endif
