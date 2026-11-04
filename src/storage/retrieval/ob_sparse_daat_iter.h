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
#include "storage/access/ob_scan_merge_loser_tree.h"
#include "storage/access/ob_simple_rows_merger.h"

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

  int init(sql::ObDatumMeta id_meta, const ObFixedArray<const ObDatum *, ObIAllocator> *iter_ids);
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
  // Non-virtual fast paths around the merge heap. init_merge_heap() fixes the
  // concrete heap type (simple merger for <= 3 dims, loser tree otherwise) and
  // nothing derives from these two for ObSRMergeItem, so the qualified calls
  // below are behavior-identical to the virtual interface calls while skipping
  // the per-candidate virtual dispatch.
  inline int merge_heap_push(const ObSRMergeItem &item);
  inline int merge_heap_pop();
  inline int merge_heap_top(const ObSRMergeItem *&item);
  inline bool merge_heap_empty() const;
  inline bool merge_heap_unique_champion() const;
  inline int merge_heap_rebuild();
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
  // true iff the pushed-down filter is exactly a bool-cast of the relevance
  // projection column over a double datum, so the per-candidate filter decision
  // is bit-identical to the inline comparison (relevance != 0.0); see cpp.
  bool use_fast_filter_;
  // typed views of merge_heap_ fixed in init_merge_heap(), backing the
  // qualified heap calls above
  ObSRSimpleMerger *simple_merger_;
  ObSRLoserTree *loser_tree_;
  // snapshot of dim_iters_ pointers taken at init, so the merge loop indexes a
  // plain array instead of paying the per-access virtual ObIArray::at dispatch
  ObFixedArray<ObISRDaaTDimIter *, ObIAllocator> dim_iter_ptr_cache_;
  // true iff rows are projected into the expr datum vectors directly at cache
  // time (vectorized + value-copyable doc id), replacing the buffered copy plus
  // the second write in project_results(); see init()/cache_result().
  bool direct_project_;
private:
  DISALLOW_COPY_AND_ASSIGN(ObSRDaaTIterImpl);
};

inline int ObSRDaaTIterImpl::merge_heap_push(const ObSRMergeItem &item)
{
  if (nullptr != simple_merger_) {
    return simple_merger_->ObSRSimpleMerger::push(item);
  } else if (nullptr != loser_tree_) {
    return loser_tree_->ObSRLoserTree::push(item);
  }
  return common::OB_ERR_UNEXPECTED;
}

inline int ObSRDaaTIterImpl::merge_heap_pop()
{
  if (nullptr != simple_merger_) {
    return simple_merger_->ObSRSimpleMerger::pop();
  } else if (nullptr != loser_tree_) {
    return loser_tree_->ObSRLoserTree::pop();
  }
  return common::OB_ERR_UNEXPECTED;
}

inline int ObSRDaaTIterImpl::merge_heap_top(const ObSRMergeItem *&item)
{
  if (nullptr != simple_merger_) {
    return simple_merger_->ObSRSimpleMerger::top(item);
  } else if (nullptr != loser_tree_) {
    return loser_tree_->ObSRLoserTree::top(item);
  }
  return common::OB_ERR_UNEXPECTED;
}

inline bool ObSRDaaTIterImpl::merge_heap_empty() const
{
  if (nullptr != simple_merger_) {
    return simple_merger_->ObSRSimpleMerger::empty();
  } else if (nullptr != loser_tree_) {
    return loser_tree_->ObSRLoserTree::empty();
  }
  return true;
}

inline bool ObSRDaaTIterImpl::merge_heap_unique_champion() const
{
  if (nullptr != simple_merger_) {
    return simple_merger_->ObSRSimpleMerger::is_unique_champion();
  } else if (nullptr != loser_tree_) {
    return loser_tree_->ObSRLoserTree::is_unique_champion();
  }
  return true;
}

inline int ObSRDaaTIterImpl::merge_heap_rebuild()
{
  if (nullptr != simple_merger_) {
    return simple_merger_->ObSRSimpleMerger::rebuild();
  } else if (nullptr != loser_tree_) {
    return loser_tree_->ObSRLoserTree::rebuild();
  }
  return common::OB_ERR_UNEXPECTED;
}

} // namespace storage
} // namespace oceanbase

#endif
