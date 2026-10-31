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

#include "ob_expr_hash.h"

using namespace oceanbase::common;
using namespace oceanbase::sql;

namespace oceanbase
{
namespace sql
{

ObExprHash::ObExprHash(ObIAllocator &alloc)
  : ObExprOperator(alloc, T_FUN_SYS_HASH, N_HASH, MORE_THAN_ONE, VALID_FOR_GENERATED_COL, NOT_ROW_DIMENSION,
                   INTERNAL_IN_MYSQL_MODE)
{
}

ObExprHash::~ObExprHash()
{
}

int ObExprHash::calc_result_typeN(ObExprResType &type,
                                  ObExprResType *types,
                                  int64_t param_num,
                                  ObExprTypeCtx &type_ctx) const
{
  int ret = OB_SUCCESS;
  UNUSED(type_ctx);
  type.set_uint64();
  type.set_precision(ObAccuracy::DDL_DEFAULT_ACCURACY[ObUInt64Type].precision_);
  type.set_scale(ObAccuracy::DDL_DEFAULT_ACCURACY[ObUInt64Type].scale_);
  return ret;
}

int ObExprHash::calc_hash_value_expr(const ObExpr &expr, ObEvalCtx &ctx, Datum &res_datum)
{
  int ret = OB_SUCCESS;
  uint64_t hash_value = HASH_SEED;
  const common::ObDatumAccessContext *datum_access_ctx = nullptr;
  if (OB_FAIL(ctx.get_datum_access_ctx(datum_access_ctx))) {
  }
  for (int64_t i = 0; OB_SUCC(ret) && i < expr.arg_cnt_; ++i) {
    Datum *datum = NULL;
    if (OB_FAIL(expr.args_[i]->eval(ctx, datum))) {
    } else {
      ObExprHashFuncType hash_func = expr.args_[i]->basic_funcs_->murmur_hash_v2_;
      if (OB_FAIL(hash_func(
              *datum, hash_value, hash_value, datum_access_ctx))) {
      }
    }
  }
  res_datum.set_uint(hash_value);
  return ret;
}

int ObExprHash::calc_hash_value_expr_batch(
    const ObExpr &expr, ObEvalCtx &ctx, const ObBitVector &skip, const int64_t batch_size)
{
  int ret = OB_SUCCESS;
  Datum *results = expr.locate_batch_datums(ctx);
  ObBitVector &eval_flags = expr.get_evaluated_flags(ctx);
  const common::ObDatumAccessContext *datum_access_ctx = nullptr;
  if (OB_FAIL(ctx.get_datum_access_ctx(datum_access_ctx))) {
  }
  for (int64_t i = 0; OB_SUCC(ret) && i < expr.arg_cnt_; ++i) {
    if (OB_FAIL(expr.args_[i]->eval_batch(ctx, skip, batch_size))) {
    } else {
      Datum *datums = expr.args_[i]->locate_batch_datums(ctx);
      if (OB_ISNULL(datums)) {
        ret = OB_ERR_UNEXPECTED;
      } else {
        const DatumHashOp &hash_func = expr.args_[i]->basic_funcs_->murmur_hash_v2_batch_;
        for (int64_t row = 0; OB_SUCC(ret) && row < batch_size; ++row) {
          if (skip.at(row) || eval_flags.at(row)) { continue; }
          uint64_t value = 0;
          const uint64_t seed = i == 0 ? HASH_SEED : results[row].get_uint();
          if (OB_FAIL(hash_func(datums[expr.args_[i]->is_batch_result() ? row : 0],
                               seed, value, datum_access_ctx))) {
          } else {
            // Inline results have no reserved payload. Keep the running hash
            // in the native slot; publish evaluated flags only after all args.
            // ObExpr::do_eval_batch clears these slots if a later arg fails.
            results[row].set_uint(value);
          }
        }
      }
    }
  }
  if (OB_FAIL(ret)) {
    // do nothing
  } else {
    for (int64_t i = 0; OB_SUCC(ret) && i < batch_size; ++i) {
      if (skip.at(i) || eval_flags.at(i)) {
        continue;
      } else {
        eval_flags.set(i);
      }
    }
  }
  return ret;
}

int ObExprHash::cg_expr(ObExprCGCtx &expr_cg_ctx,
                        const ObRawExpr &raw_expr,
                        ObExpr &rt_expr) const
{
  int ret = OB_SUCCESS;
  UNUSED(raw_expr);
  UNUSED(expr_cg_ctx);
  if (0 == rt_expr.arg_cnt_) {
    ret = OB_INVALID_ARGUMENT;
  } else {
    rt_expr.eval_func_ = &calc_hash_value_expr;
    rt_expr.eval_batch_func_ = &calc_hash_value_expr_batch;
  }
  return ret;
}

} //namespace sql
} //namespace oceanbase
