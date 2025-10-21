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

#define USING_LOG_PREFIX STORAGE

#include "ob_sparse_utils.h"

namespace oceanbase
{
namespace storage
{

int ObSRDaaTInnerProductRelevanceCollector::init(int64_t should_match)
{
  int ret = OB_SUCCESS;
  total_relevance_ = 0.0;
  should_match_ = should_match;
  return ret;
}

void ObSRDaaTInnerProductRelevanceCollector::reset()
{
  total_relevance_ = 0.0;
  matched_cnt_ = 0;
}

void ObSRDaaTInnerProductRelevanceCollector::reuse()
{
  total_relevance_ = 0.0;
  matched_cnt_ = 0;
}

int ObSRDaaTInnerProductRelevanceCollector::collect_one_dim(const int64_t dim_idx, const double relevance)
{
  int ret = OB_SUCCESS;
  total_relevance_ += relevance;
  matched_cnt_ ++;
  return ret;
}

int ObSRDaaTInnerProductRelevanceCollector::get_result(double &relevance, bool &is_valid)
{
  int ret = OB_SUCCESS;
  relevance = total_relevance_;
  is_valid = matched_cnt_ >= should_match_;
  total_relevance_ = 0.0;
  matched_cnt_ = 0;
  return ret;
}

int ObSRDaaTBooleanRelevanceCollector::init(ObIAllocator *allocator, const int64_t dim_cnt, ObFtsEvalNode *node)
{
  int ret = OB_SUCCESS;
  allocator_ = allocator;
  dim_cnt_ = dim_cnt;
  boolean_compute_node_ = node;
  if (FALSE_IT(boolean_relevances_.set_allocator(allocator))) {
  } else if (OB_FAIL(boolean_relevances_.init(dim_cnt_))) {
    LOG_WARN("failed to init boolean relevances array", K(ret));
  } else if (OB_FAIL(boolean_relevances_.prepare_allocate(dim_cnt_))) {
    LOG_WARN("failed to prepare allocate boolean relevacnes array", K(ret));
  } else {
    for (int64_t i = 0; i < dim_cnt_; ++i) {
      boolean_relevances_[i] = 0.0;
    }
  }
  return ret;
}

void ObSRDaaTBooleanRelevanceCollector::reset()
{
  boolean_relevances_.reset();
  if (OB_NOT_NULL(boolean_compute_node_)) {
    boolean_compute_node_->release();
    boolean_compute_node_ = nullptr;
  }
}

void ObSRDaaTBooleanRelevanceCollector::reuse()
{
  for (int64_t i = 0; i < dim_cnt_; ++i) {
    boolean_relevances_[i] = 0.0;
  }
}

int ObSRDaaTBooleanRelevanceCollector::collect_one_dim(const int64_t dim_idx, const double relevance)
{
  int ret = OB_SUCCESS;
  boolean_relevances_[dim_idx] = relevance;
  return ret;
}

int ObSRDaaTBooleanRelevanceCollector::get_result(double &relevance, bool &is_valid)
{
  int ret = OB_SUCCESS;
  if (OB_ISNULL(boolean_compute_node_)) {
    ret = OB_ERR_UNEXPECTED;
    LOG_WARN("unexpected null boolean compute node", K(ret));
  } else if (OB_FAIL(ObFtsEvalNode::fts_boolean_eval(boolean_compute_node_, boolean_relevances_, relevance))) {
    LOG_WARN("failed to evaluate boolean relevance");
  } else {
    is_valid = relevance > 0;
    for (int64_t i = 0; i < dim_cnt_; ++i) {
      boolean_relevances_[i] = 0.0;
    }
  }
  return ret;
}

} // namespace storage
} // namespace oceanbase
