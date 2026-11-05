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

#ifndef OB_TEXT_TAAT_ITER_H_
#define OB_TEXT_TAAT_ITER_H_

#include "ob_inv_idx_param_estimator.h"
#include "ob_sparse_taat_iter.h"
#include "ob_text_retrieval_token_iter.h"

namespace oceanbase
{
namespace sql
{
  class ObDASScanIter;
}
namespace storage
{

struct ObTextTaaTParam
{
  ObTextTaaTParam()
    : dim_iter_(nullptr), query_tokens_(nullptr), base_param_(nullptr), allocator_(nullptr),
      bm25_param_est_ctx_(), mode_flag_(ObMatchAgainstMode::NATURAL_LANGUAGE_MODE),
      function_lookup_mode_(false) {}
  ~ObTextTaaTParam() {}
  TO_STRING_KV(K_(base_param), KP_(dim_iter), KP_(query_tokens), K_(bm25_param_est_ctx),
      K_(mode_flag), K_(function_lookup_mode));
  ObISparseRetrievalDimIter *dim_iter_;
  ObIArray<ObString> *query_tokens_;
  ObSparseRetrievalMergeParam *base_param_;
  common::ObArenaAllocator *allocator_;
  ObBM25ParamEstCtx bm25_param_est_ctx_;
  ObMatchAgainstMode mode_flag_;
  bool function_lookup_mode_;
};

class ObTextTaaTIter final : public ObSRTaaTIterImpl
{
public:
  ObTextTaaTIter() : ObSRTaaTIterImpl(),
      mem_context_(nullptr),
      query_tokens_(nullptr),
      bm25_param_estimator_(),
      mode_flag_(ObMatchAgainstMode::NATURAL_LANGUAGE_MODE),
      function_lookup_mode_(false) {}
  virtual ~ObTextTaaTIter() {}

  int init(const ObTextTaaTParam &param);
  virtual void reuse(const bool switch_tablet = false) override;
  virtual void reset() override;
protected:
  virtual int pre_process() override;
  virtual int update_dim_iter(const int64_t dim_idx) override;
protected:
  lib::MemoryContext mem_context_;
  ObIArray<ObString> *query_tokens_;
  ObBM25ParamEstimator bm25_param_estimator_;
  ObMatchAgainstMode mode_flag_;
  bool function_lookup_mode_;
private:
  DISALLOW_COPY_AND_ASSIGN(ObTextTaaTIter);
};

} // namespace storage
} // namespace oceanbase

#endif
