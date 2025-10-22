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

#ifndef OB_INV_IDX_PARAM_ESTIMATOR_H_
#define OB_INV_IDX_PARAM_ESTIMATOR_H_

#include "share/ob_define.h"
#include "lib/number/ob_number_v2.h"

namespace oceanbase
{
namespace sql
{
class ObExpr;
class ObEvalCtx;
class ObDASScanIter;
}

namespace storage
{
class ObTableScanParam;
class ObBlockStatScanParam;

struct ObTextAvgDocLenEstimator
{
  ObTextAvgDocLenEstimator(const int64_t total_doc_cnt, ObBlockStatScanParam &doc_length_est_param)
    : total_doc_cnt_(total_doc_cnt),
      doc_length_est_param_(doc_length_est_param) {}
  int estimate_avg_doc_len(sql::ObExpr &avg_doc_token_cnt_expr, sql::ObEvalCtx &eval_ctx, double &result);
  int cast_number_to_double(const number::ObNumber &num, double &result);
  int64_t total_doc_cnt_;
  ObBlockStatScanParam &doc_length_est_param_;
};


struct ObBM25ParamEstCtx
{
  ObBM25ParamEstCtx();
  virtual ~ObBM25ParamEstCtx() {}
  bool is_valid() const;
  int assign(const ObBM25ParamEstCtx &other);
  void reset();
  TO_STRING_KV(K_(estimated_total_doc_cnt), KP_(total_doc_cnt_iter),
      KP_(total_doc_cnt_expr), KP_(avg_doc_token_cnt_expr), KP_(doc_length_est_param),
      K_(can_est_by_sum_skip_index), K_(need_est_avg_doc_token_cnt));
  int64_t estimated_total_doc_cnt_;
  sql::ObDASScanIter *total_doc_cnt_iter_;
  sql::ObExpr *total_doc_cnt_expr_;
  sql::ObExpr *avg_doc_token_cnt_expr_;
  ObBlockStatScanParam *doc_length_est_param_;
  bool can_est_by_sum_skip_index_;
  bool need_est_avg_doc_token_cnt_;
};

class ObBM25ParamEstimator
{
public:
  ObBM25ParamEstimator();
  virtual ~ObBM25ParamEstimator() {}

  int init(const ObBM25ParamEstCtx &est_ctx);
  void reset();
  void reuse(const bool switch_tablet);
  int do_estimation(sql::ObEvalCtx &eval_ctx);
  int64_t get_total_doc_cnt() const { return total_doc_cnt_; }
  double get_avg_doc_token_cnt() const { return avg_doc_token_cnt_; }
  bool is_estimated() const { return estimated_; }
private:
  ObBM25ParamEstCtx est_ctx_;
  int64_t total_doc_cnt_;
  double avg_doc_token_cnt_;
  bool estimated_;
  bool is_inited_;
};

} // namespace storage
} // namespace oceanbase

#endif // OB_INV_IDX_PARAM_ESTIMATOR_H_