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

#ifndef OB_SPARSE_UTILS_H_
#define OB_SPARSE_UTILS_H_

#include "ob_i_sparse_retrieval_iter.h"
#include "sql/das/ob_das_ir_define.h"
#include "sql/das/iter/ob_das_text_retrieval_eval_node.h"

namespace oceanbase
{
namespace storage
{

struct ObSRDaaTRelevanceCollector
{
  ObSRDaaTRelevanceCollector() {}
  virtual ~ObSRDaaTRelevanceCollector() {}

  virtual void reset() = 0;
  virtual void reuse() = 0;
  virtual int collect_one_dim(const int64_t dim_idx, const double relevance) = 0;
  virtual int get_result(double &relevance, bool &is_valid) = 0;
  // Returns the minimum number of dimensions a document must match to be valid.
  // For pure-AND inner-product collectors this equals the dimension count, so the
  // merge layer can short-circuit validity from the popped-item count alone.
  virtual int64_t get_should_match() const { return 0; }
};

struct ObSRDaaTInnerProductRelevanceCollector : ObSRDaaTRelevanceCollector
{
  ObSRDaaTInnerProductRelevanceCollector() : ObSRDaaTRelevanceCollector(),
      total_relevance_(0),
      matched_cnt_(0),
      should_match_(0) {}
  virtual ~ObSRDaaTInnerProductRelevanceCollector() {};

  int init(int64_t should_match = 0);
  virtual void reset() override;
  virtual void reuse() override;
  virtual int collect_one_dim(const int64_t dim_idx, const double) override;
  virtual int get_result(double &relevance, bool &is_valid) override;
  virtual int64_t get_should_match() const override { return should_match_; }
private:
  double total_relevance_;
  int64_t matched_cnt_;
  int64_t should_match_;
};

struct ObSRDaaTBooleanRelevanceCollector : ObSRDaaTRelevanceCollector
{
  ObSRDaaTBooleanRelevanceCollector() : ObSRDaaTRelevanceCollector(),
      allocator_(nullptr), dim_cnt_(0), boolean_compute_node_(nullptr), boolean_relevances_() {}
  virtual ~ObSRDaaTBooleanRelevanceCollector() {};

  int init(ObIAllocator *allocator, const int64_t dim_cnt, ObFtsEvalNode *node);
  virtual void reset() override;
  virtual void reuse() override;
  virtual int collect_one_dim(const int64_t dim_idx, const double) override;
  virtual int get_result(double &relevance, bool &is_valid) override;
private:
  ObIAllocator *allocator_;
  int64_t dim_cnt_;
  ObFtsEvalNode *boolean_compute_node_;
  ObFixedArray<double, ObIAllocator> boolean_relevances_;
};

} // namespace storage
} // namespace oceanbase

#endif
