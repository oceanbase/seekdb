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

#ifndef _OB_EXPR_BM25_H_
#define _OB_EXPR_BM25_H_

#include "sql/engine/expr/ob_expr_operator.h"

namespace oceanbase
{
namespace sql
{

/**
 * An implementation of Okapi BM25 relevance estimation / ranking algorithm
 *
 * Params:
 *    token document count: count of documents contains query token
 *    total document count: count of all documents in retrieval domain
 *    document token count: count of tokens in specific document
 *    average document token count: average count of tokens in document in retrieval domain
 *    related token count: count of query token in specific document
 *  
 *    p_k1, p_b, p_epsilon: parameters to tune bm25 score, hard coded for now.
 */
class ObExprBM25 : public ObFuncExprOperator
{
public:
  explicit ObExprBM25(common::ObIAllocator &alloc);
  virtual ~ObExprBM25() {}

  virtual int calc_result_typeN(
      ObExprResType &result_type,
      ObExprResType *types,
      int64_t param_num,
      common::ObExprTypeCtx &type_ctx) const override;
  virtual int cg_expr(
      ObExprCGCtx &expr_cg_ctx,
      const ObRawExpr &raw_expr,
      ObExpr &rt_expr) const override;

  static int eval_bm25_relevance_expr(const ObExpr &expr, ObEvalCtx &ctx, ObDatum &res_datum);
  static int eval_batch_bm25_relevance_expr(const ObExpr &expr, ObEvalCtx &ctx, const ObBitVector &skip, const int64_t size);
  static double eval(
      const int64_t token_freq,
      const int64_t doc_length,
      const int64_t doc_freq,
      const int64_t doc_cnt,
      const double avg_doc_token_cnt)
  {
    OB_ASSERT(avg_doc_token_cnt != 0);
    const double norm_len = doc_length / avg_doc_token_cnt;
    const double score = query_token_weight(doc_freq, doc_cnt) * doc_token_weight(token_freq, norm_len);
    return score;
  }
  static double doc_token_weight(const int64_t token_freq, const double norm_len);
  static double query_token_weight(const int64_t doc_freq, const int64_t doc_cnt);
public:
  static constexpr int TOKEN_DOC_CNT_PARAM_IDX = 0;
  static constexpr int TOTAL_DOC_CNT_PARAM_IDX = 1;
  static constexpr int DOC_TOKEN_CNT_PARAM_IDX = 2;
  static constexpr int AVG_DOC_CNT_PARAM_IDX = 3;
  static constexpr int RELATED_TOKEN_CNT_PARAM_IDX = 4;
  static constexpr double DEFAULT_AVG_DOC_TOKEN_CNT = 10.0;
private:
  static constexpr double p_k1 = 1.2;
  static constexpr double p_b = 0.75;
  static constexpr double p_epsilon = 0.25;
  DISALLOW_COPY_AND_ASSIGN(ObExprBM25);
};

} // namespace sql
} // namespace oceanbase

#endif
