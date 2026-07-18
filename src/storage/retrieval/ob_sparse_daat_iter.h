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
  int cmp(const int64_t l_iter_idx, const int64_t r_iter_idx, int64_t &cmp_ret);
private:
  OB_INLINE const ObDatum &get_id_datum(const int64_t iter_idx)
  {
    // FTS query merge optimization (reference 8a33f37): the fixed array is
    // contiguous after prepare_allocate(), so avoid a checked at() call in
    // every loser-tree comparison.
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
  int do_small_any_match_merge_round(int64_t &count);
  int do_small_any_match_batch_merge(const int64_t capacity, int64_t &count);
  int do_three_way_any_match_batch_merge(const int64_t capacity, int64_t &count);
  int refill_small_any_match_batch(const int64_t iter_idx);
  int refill_small_any_match_iters();
protected:
  OB_INLINE ObISRDaaTDimIter *get_dim_iter(const int64_t iter_idx) const
  {
    // FTS match hot-path optimization (reference f25202c): dimensions do not
    // change after init, so bypass the generic ObIArray virtual access.
    return dim_iter_data_[iter_idx];
  }
  ObIAllocator *iter_allocator_;
  ObSparseRetrievalMergeParam *iter_param_;
  ObIArray<ObISRDaaTDimIter *> *dim_iters_;
  ObFixedArray<ObISRDaaTDimIter *, ObIAllocator> dim_iter_cache_;
  ObISRDaaTDimIter **dim_iter_data_;
  int64_t dim_iter_cnt_;
  ObSRMergeCmp merge_cmp_;
  ObSRMergeHeap *merge_heap_;
  ObSRDaaTRelevanceCollector *relevance_collector_;
  ObFixedArray<const ObDatum *, ObIAllocator> iter_domain_ids_; // record every dim iter's output domain id, one (ObDatum *) for one dim iter
  const ObDatum **iter_domain_id_data_;
  ObFixedArray<ObDocIdExt, ObIAllocator> buffered_domain_ids_; // cache for output
  ObDocIdExt *buffered_domain_id_data_;
  ObFixedArray<uint64_t, ObIAllocator> buffered_fixed_doc_ids_;
  uint64_t *buffered_fixed_doc_id_data_;
  ObFixedArray<double, ObIAllocator> buffered_relevances_;
  ObFixedArray<int64_t, ObIAllocator> next_round_iter_idxes_;
  int64_t next_round_cnt_;
  // FTS PERF OPT 7: natural-language predicate queries normally contain only
  // a few tokens.  Keep their active posting iterators inline so the hot OR
  // merge does not pay the generic heap's virtual-call and shifting overhead.
  static const int64_t SMALL_ANY_MATCH_MAX_ITER_CNT = 3;
  int64_t small_active_iter_idxes_[SMALL_ANY_MATCH_MAX_ITER_CNT];
  int64_t small_active_iter_cnt_;
  bool use_small_any_match_merge_;
  // FTS PERF OPT 9/15: keep one sorted posting batch per small-OR dimension
  // and merge it with direct cursors. This removes scalar virtual dispatch for
  // the common two- and three-token truth-only queries while retaining the
  // existing scalar path as a compatibility fallback.
  const ObDocIdExt *small_batch_doc_ids_[SMALL_ANY_MATCH_MAX_ITER_CNT];
  int64_t small_batch_counts_[SMALL_ANY_MATCH_MAX_ITER_CNT];
  int64_t small_batch_positions_[SMALL_ANY_MATCH_MAX_ITER_CNT];
  bool small_batch_ended_[SMALL_ANY_MATCH_MAX_ITER_CNT];
  bool use_small_any_match_batch_merge_;
  bool use_fixed_doc_id_batch_output_;
  void (*set_datum_func_)(ObDatum &, const ObDocIdExt &);
private:
  DISALLOW_COPY_AND_ASSIGN(ObSRDaaTIterImpl);
};

} // namespace storage
} // namespace oceanbase

#endif
