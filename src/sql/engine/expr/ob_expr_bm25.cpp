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

#define USING_LOG_PREFIX SQL_ENG
#include "sql/engine/expr/ob_expr_bm25.h"
#include "sql/resolver/expr/ob_raw_expr.h"

namespace oceanbase
{
namespace sql
{
ObExprBM25::ObExprBM25(ObIAllocator &alloc)
  : ObFuncExprOperator(alloc, T_FUN_SYS_BM25, N_BM25, MORE_THAN_TWO, VALID_FOR_GENERATED_COL, NOT_ROW_DIMENSION)
{
}

int ObExprBM25::calc_result_typeN(
    ObExprResType &result_type,
    ObExprResType *types,
    int64_t param_num,
    common::ObExprTypeCtx &type_ctx) const
{
  int ret = OB_SUCCESS;
  UNUSED(type_ctx);
  const int64_t expected_param_num = 6;

  if (OB_UNLIKELY(param_num != expected_param_num)) {
    ret = OB_INVALID_ARGUMENT;
    LOG_WARN("BM25 expr should have correct parameters", K(ret), K(param_num), K(expected_param_num));
  } else {
    types[TOKEN_DOC_CNT_PARAM_IDX].set_calc_type(ObIntType);
    types[TOTAL_DOC_CNT_PARAM_IDX].set_calc_type(ObIntType);
    types[DOC_LENGTH_PARAM_IDX].set_calc_type(ObUInt64Type);
    types[TOKEN_WEIGHT_PARAM_IDX].set_calc_type(ObDoubleType);
    types[AVG_DOC_CNT_PARAM_IDX].set_calc_type(ObDoubleType);
    types[RELATED_TOKEN_CNT_PARAM_IDX].set_calc_type(ObUInt64Type);
    result_type.set_double();
  }
  return ret;
}

int ObExprBM25::cg_expr(ObExprCGCtx &expr_cg_ctx, const ObRawExpr &raw_expr, ObExpr &rt_expr) const
{
  int ret = OB_SUCCESS;
  UNUSED(expr_cg_ctx);
  CK(6 == raw_expr.get_param_count());
  rt_expr.eval_func_ = eval_bm25_relevance_expr;
  rt_expr.eval_batch_func_ = eval_batch_bm25_relevance_expr;
  return ret;
}

int ObExprBM25::eval_bm25_relevance_expr(const ObExpr &expr, ObEvalCtx &ctx, ObDatum &res_datum)
{
  int ret = OB_SUCCESS;  
  if (!use_new_version(expr)) {
    ObDatum *token_doc_cnt_datum = nullptr;
    ObDatum *total_doc_cnt_datum = nullptr;
    ObDatum *doc_token_cnt_datum = nullptr;
    ObDatum *avg_doc_token_cnt_datum = nullptr;
    ObDatum *related_token_cnt_datum = nullptr;
    if (OB_FAIL(expr.eval_param_value(
        ctx,
        token_doc_cnt_datum,
        total_doc_cnt_datum,
        doc_token_cnt_datum,
        avg_doc_token_cnt_datum,
        related_token_cnt_datum))) {
      LOG_WARN("evaluate parameter value failed", K(ret));
    } else if (OB_UNLIKELY(token_doc_cnt_datum->is_null() || total_doc_cnt_datum->is_null()
        || doc_token_cnt_datum->is_null() || avg_doc_token_cnt_datum->is_null() || related_token_cnt_datum->is_null())) {
      ret = OB_ERR_UNEXPECTED;
      LOG_WARN("unexpected null datum", K(ret), KPC(token_doc_cnt_datum), KPC(total_doc_cnt_datum),
          KPC(doc_token_cnt_datum), KPC(avg_doc_token_cnt_datum), KPC(related_token_cnt_datum));
    } else {
      const int64_t token_doc_cnt = token_doc_cnt_datum->get_int();
      const int64_t total_doc_cnt = total_doc_cnt_datum->get_int();
      const int64_t related_token_cnt = related_token_cnt_datum->get_uint();
      const int64_t doc_token_cnt = doc_token_cnt_datum->get_uint();
      const double avg_doc_token_cnt = avg_doc_token_cnt_datum->get_double();
      const double norm_len = doc_token_cnt / avg_doc_token_cnt;
      const double token_weight = query_token_weight(token_doc_cnt, total_doc_cnt);
      const double doc_weight = doc_token_weight(related_token_cnt, norm_len);
      const double relevance = token_weight * doc_weight;
      res_datum.set_double(relevance);
    }
  } else {
    ObDatum *token_doc_cnt_datum = nullptr;
    ObDatum *total_doc_cnt_datum = nullptr;
    ObDatum *doc_length_datum = nullptr;
    ObDatum *token_weight_datum = nullptr;
    ObDatum *avg_doc_token_cnt_datum = nullptr;
    ObDatum *related_token_cnt_datum = nullptr;
    if (OB_FAIL(expr.eval_param_value(
        ctx,
        token_doc_cnt_datum,
        total_doc_cnt_datum,
        doc_length_datum,
        token_weight_datum,
        avg_doc_token_cnt_datum,
        related_token_cnt_datum))) {
      LOG_WARN("evaluate parameter value failed", K(ret));
    } else if (OB_UNLIKELY(token_doc_cnt_datum->is_null() || total_doc_cnt_datum->is_null()
        || doc_length_datum->is_null() || token_weight_datum->is_null()
        || avg_doc_token_cnt_datum->is_null() || related_token_cnt_datum->is_null())) {
      ret = OB_ERR_UNEXPECTED;
      LOG_WARN("unexpected null datum", K(ret),  KPC(token_doc_cnt_datum), KPC(total_doc_cnt_datum), KPC(doc_length_datum),
          KPC(token_weight_datum), KPC(avg_doc_token_cnt_datum), KPC(related_token_cnt_datum));
    } else {
      const int64_t related_token_cnt = related_token_cnt_datum->get_uint();
      const int64_t doc_token_cnt = doc_length_datum->get_uint();
      const double avg_doc_token_cnt = avg_doc_token_cnt_datum->get_double();
      const double norm_len = doc_token_cnt / avg_doc_token_cnt;
      const double token_weight = token_weight_datum->get_double();
      const double doc_weight = doc_token_weight(related_token_cnt, norm_len);
      const double relevance = token_weight * doc_weight;
      res_datum.set_double(relevance);
    }
  }
  return ret;
}

int ObExprBM25::eval_batch_bm25_relevance_expr(const ObExpr &expr, ObEvalCtx &ctx, const ObBitVector &skip, const int64_t size)
{
  int ret = OB_SUCCESS;  
  if (!use_new_version(expr)) {
    ObDatumVector token_doc_cnt_datum;
    ObDatumVector total_doc_cnt_datum;
    ObDatumVector doc_token_cnt_datum;
    ObDatumVector avg_doc_token_cnt_datum;
    ObDatumVector related_token_cnt_datum;
    if (OB_FAIL(expr.eval_batch_param_value(
      ctx,
      skip,
      size,
      token_doc_cnt_datum,
      total_doc_cnt_datum,
      doc_token_cnt_datum,
      avg_doc_token_cnt_datum,
      related_token_cnt_datum))) {
        LOG_WARN("evaluate parameter value failed", K(ret));
    } else if (OB_UNLIKELY(token_doc_cnt_datum.datums_[0].null_ || total_doc_cnt_datum.datums_[0].null_
        || avg_doc_token_cnt_datum.datums_[0].null_)) {
        ret = OB_ERR_UNEXPECTED;
        LOG_WARN("unexpected null datum", K(ret), K(token_doc_cnt_datum.datums_[0]),
            K(total_doc_cnt_datum.datums_[0]), K(avg_doc_token_cnt_datum.datums_[0]));
    } else {
      const int64_t token_doc_cnt = token_doc_cnt_datum.datums_[0].get_int();
      const int64_t total_doc_cnt = total_doc_cnt_datum.datums_[0].get_int();
      const double token_weight = query_token_weight(token_doc_cnt, total_doc_cnt);
      const double avg_doc_token_cnt = avg_doc_token_cnt_datum.datums_[0].get_double();
      ObDatum *res_datum = expr.locate_batch_datums(ctx);
      ObBitVector &eval_flags = expr.get_evaluated_flags(ctx);
      const uint64_t *skip_words = skip.reinterpret_data<uint64_t>();
      uint64_t *eval_words = eval_flags.reinterpret_data<uint64_t>();
      for(int64_t i = 0; OB_SUCC(ret) && i < size; ++i)
      {
        ObDatum &doc_token_cnt = doc_token_cnt_datum.datums_[doc_token_cnt_datum.mask_ & i];
        ObDatum &related_token_cnt_value = related_token_cnt_datum.datums_[related_token_cnt_datum.mask_ & i];
        if (OB_UNLIKELY(doc_token_cnt.null_ || related_token_cnt_value.null_)) {
          ret = OB_ERR_UNEXPECTED;
          LOG_WARN("unexpected null datum", K(ret), K(doc_token_cnt), K(related_token_cnt_value));
        } else {
          const int64_t word_idx = i / ObBitVector::WORD_BITS;
          const uint64_t bit_mask = 1ULL << (i % ObBitVector::WORD_BITS);
          if (0 == (skip_words[word_idx] & bit_mask) && 0 == (eval_words[word_idx] & bit_mask)) {
            const int64_t related_token_cnt = related_token_cnt_value.get_uint();
            const uint64_t doc_token_cnt_value = doc_token_cnt.get_uint();
            const double norm_len = doc_token_cnt_value / avg_doc_token_cnt;
            const double doc_weight = doc_token_weight(related_token_cnt, norm_len);
            const double relevance = token_weight * doc_weight;
            res_datum[i].set_double(relevance);
            eval_words[word_idx] |= bit_mask;
          }
        }
      }
    }
  } else {
    ObDatumVector token_doc_cnt_datum;
    ObDatumVector total_doc_cnt_datum;
    ObDatumVector doc_length_datum;
    ObDatumVector token_weight_datum;
    ObDatumVector avg_doc_token_cnt_datum;
    ObDatumVector related_token_cnt_datum;
    if (OB_FAIL(expr.eval_batch_param_value(
      ctx,
      skip,
      size,
      token_doc_cnt_datum,
      total_doc_cnt_datum,
      doc_length_datum,
      token_weight_datum,
      avg_doc_token_cnt_datum,
      related_token_cnt_datum))) {
        LOG_WARN("evaluate parameter value failed", K(ret));
    } else if (OB_UNLIKELY(token_doc_cnt_datum.datums_[0].null_ || total_doc_cnt_datum.datums_[0].null_
        || token_weight_datum.datums_[0].null_ || avg_doc_token_cnt_datum.datums_[0].null_)) {
        ret = OB_ERR_UNEXPECTED;
        LOG_WARN("unexpected null datum", K(ret), K(token_weight_datum.datums_[0]),
            K(avg_doc_token_cnt_datum.datums_[0]));
    } else {
        const double token_weight = token_weight_datum.datums_[0].get_double();
        const double avg_doc_token_cnt = avg_doc_token_cnt_datum.datums_[0].get_double();
        ObDatum *res_datum = expr.locate_batch_datums(ctx);
        ObBitVector &eval_flags = expr.get_evaluated_flags(ctx);
        const uint64_t *skip_words = skip.reinterpret_data<uint64_t>();
        uint64_t *eval_words = eval_flags.reinterpret_data<uint64_t>();
        for(int64_t i = 0; OB_SUCC(ret) && i < size; ++i)
        {
          ObDatum &doc_length = doc_length_datum.datums_[doc_length_datum.mask_ & i];
          ObDatum &related_token_cnt_value = related_token_cnt_datum.datums_[related_token_cnt_datum.mask_ & i];
          if (OB_UNLIKELY(doc_length.null_ || related_token_cnt_value.null_)) {
            ret = OB_ERR_UNEXPECTED;
            LOG_WARN("unexpected null datum", K(ret), K(doc_length), K(related_token_cnt_value));
          } else {
            const int64_t word_idx = i / ObBitVector::WORD_BITS;
            const uint64_t bit_mask = 1ULL << (i % ObBitVector::WORD_BITS);
            if (0 == (skip_words[word_idx] & bit_mask) && 0 == (eval_words[word_idx] & bit_mask)) {
              const int64_t related_token_cnt = related_token_cnt_value.get_uint();
              const uint64_t doc_token_cnt = doc_length.get_uint();
              const double norm_len = doc_token_cnt / avg_doc_token_cnt;
              const double doc_weight = doc_token_weight(related_token_cnt, norm_len);
              const double relevance = token_weight * doc_weight;
              res_datum[i].set_double(relevance);
              eval_words[word_idx] |= bit_mask;
            }
          }
        }
    }
  }
  return ret;
}

double ObExprBM25::query_token_weight(const int64_t doc_freq, const int64_t doc_cnt)
{
  const double df = static_cast<double>(doc_freq);
  const double len = static_cast<double>(doc_cnt);
  // Since we might use approximate count statistic for total doc cnt, possibilities there are
  //   document frequencies larger than total doc cnt
  const double diff = (len - df) > 0 ? (len - df) : 0;
  const double idf = std::log((diff + 0.5) / (df + 0.5));
  return MAX(p_epsilon, idf) * (1.0 + p_k1);
}


} // namespace sql
} // namespace oceanbase
