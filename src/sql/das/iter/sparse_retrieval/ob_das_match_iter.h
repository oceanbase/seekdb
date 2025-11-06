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

#ifndef OB_DAS_MATCH_ITER_H_
#define OB_DAS_MATCH_ITER_H_

#include "ob_das_tr_merge_iter.h"

namespace oceanbase
{
namespace sql
{
struct ObDASIRScanCtDef;
struct ObDASIRScanRtDef;
class ObDocIdExt;

struct ObDASMatchIterParam : public ObDASIterParam
{
  ObDASMatchIterParam()
    : ObDASIterParam(DAS_ITER_ES_MATCH),
      ir_match_part_score_ctdef_(nullptr),
      ir_match_part_score_rtdef_(nullptr),
      domain_id_expr_(nullptr),
      children_relevance_exprs_(),
      children_domain_id_exprs_()
  {}

  virtual bool is_valid() const override
  {
    return true;
  }

  const ObDASIREsMatchCtDef *ir_match_part_score_ctdef_;
  ObDASIREsMatchRtDef *ir_match_part_score_rtdef_;
  ObExpr *domain_id_expr_;
  ObSEArray<ObExpr *, 4> children_relevance_exprs_;
  ObSEArray<ObExpr *, 4> children_domain_id_exprs_;
};

struct ObDASMatchMergeCmp
{
  ObDASMatchMergeCmp();
  virtual ~ObDASMatchMergeCmp() {}

  int init(ObDatumMeta id_meta, const ObFixedArray<ObDocIdExt, ObIAllocator> *iter_ids);
  int cmp(const ObSRMergeItem &l, const ObSRMergeItem &r, int64_t &cmp_ret);
private:
  inline const ObDatum &get_id_datum(const int64_t iter_idx)
  {
    return iter_ids_->at(iter_idx).get_datum();
  }
private:
  common::ObDatumCmpFuncType cmp_func_;
  const ObFixedArray<ObDocIdExt, ObIAllocator> *iter_ids_;
  bool is_inited_;
};

struct ObDasBestfieldCollector : ObSRDaaTRelevanceCollector
{
  ObDasBestfieldCollector() : ObSRDaaTRelevanceCollector(), max_relevance_() {}
  virtual ~ObDasBestfieldCollector() {};

  int init();
  virtual void reset() override;
  virtual void reuse() override;
  virtual int collect_one_dim(const int64_t dim_idx, const double) override;
  virtual int get_result(double &relevance, bool &is_valid) override;
private:
  double max_relevance_;
};

typedef common::ObLoserTree<ObSRMergeItem, ObDASMatchMergeCmp> ObDASMatchMergeLoserTree;

class ObDASMatchIter : public ObDASIter
{
public:
  ObDASMatchIter();
  virtual ~ObDASMatchIter();
  virtual int do_table_scan() override;
  virtual int rescan() override;
  static int get_match_param(const ObDASIREsMatchCtDef *match_ctdef,
                             ObDASIREsMatchRtDef *match_rtdef,
                             common::ObIAllocator &alloc,
                             int &minimum_should_match);

  INHERIT_TO_STRING_KV("ObDASIter", ObDASIter, K_(ir_match_part_score_ctdef), K_(ir_match_part_score_rtdef), K_(eval_ctx), K_(domain_id_expr),
    K_(children_relevance_exprs), K_(is_inited));
protected:
  virtual int inner_init(ObDASIterParam &param) override;
  virtual int inner_reuse() override;
  virtual int inner_release() override;
  virtual int inner_get_next_row() override;
  virtual int inner_get_next_rows(int64_t &count, int64_t capacity) override;
private:
  int do_one_merge_round(int64_t &count);
  int fill_merge_heap();
  int collect_dims_by_id(const ObDatum *&id_datum, double &relevance, bool &got_valid_id);
  int process_collected_row(const ObDatum &id_datum, const double relevance) { return OB_SUCCESS; }
  int filter_on_demand(const int64_t count, const double relevance, bool &need_project);
  int cache_result(int64_t &count, const ObDatum &id_datum, const double relevance);
  int project_results(const int64_t count);

public:
  bool is_match_part_score_iter() { return nullptr != ir_match_part_score_ctdef_ && nullptr != ir_match_part_score_rtdef_; }
  bool is_match_score_iter() { return !is_match_part_score_iter(); }
private:
  lib::MemoryContext mem_context_;  // clean after release or reuse
  common::ObArenaAllocator myself_allocator_; // clean after release
  const ObDASIREsMatchCtDef *ir_match_part_score_ctdef_;
  ObDASIREsMatchRtDef *ir_match_part_score_rtdef_;
  ObEvalCtx *eval_ctx_;
  ObExpr *domain_id_expr_;
  void (*set_datum_func_)(ObDatum &, const ObDocIdExt &);
  ObSRDaaTRelevanceCollector *relevance_collector_;
  ObFixedArray<ObExpr *, ObIAllocator> children_relevance_exprs_;
  ObFixedArray<ObExpr *, ObIAllocator> children_domain_id_exprs_;
  ObFixedArray<ObDocIdExt, ObIAllocator> iter_domain_ids_; //cache from lose tree
  ObFixedArray<int64_t, ObIAllocator> next_round_iter_idxes_;
  ObFixedArray<ObDocIdExt, ObIAllocator> buffered_domain_ids_; // cache for output
  ObFixedArray<double, ObIAllocator> buffered_relevances_;

  ObDASMatchMergeCmp merge_cmp_;
  ObDASMatchMergeLoserTree *merge_heap_;
  int64_t next_round_cnt_;
  double max_query_score_;
  bool is_inited_;
  DISALLOW_COPY_AND_ASSIGN(ObDASMatchIter);
};

} // namespace sql
} // namespace oceanbase

#endif // OB_DAS_MATCH_ITER_H_
