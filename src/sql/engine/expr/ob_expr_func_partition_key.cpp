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
#include "sql/engine/expr/ob_expr_func_partition_key.h"

#include "share/datum/datum_hash_value_calculator.h"
#include "sql/engine/expr/ob_wide_integer_partition_hash.h"

namespace oceanbase
{
using namespace common;
namespace sql
{

ObExprFuncPartKey::ObExprFuncPartKey(ObIAllocator &alloc)
    : ObFuncExprOperator(alloc,T_FUN_SYS_PART_KEY, N_PART_KEY, MORE_THAN_ZERO, NOT_VALID_FOR_GENERATED_COL, NOT_ROW_DIMENSION)
{
}

ObExprFuncPartKey::~ObExprFuncPartKey()
{
}

int ObExprFuncPartKey::calc_result_typeN(ObExprResType &type,
                                            ObExprResType *types_stack,
                                            int64_t param_num,
                                            ObExprTypeCtx &type_ctx) const
{
  UNUSED(type_ctx);
  UNUSED(types_stack);
  UNUSED(param_num);
  type.set_int();
  return OB_SUCCESS;
}

int ObExprFuncPartKey::cg_expr(ObExprCGCtx &, const ObRawExpr &, ObExpr &rt_expr) const
{
  int ret = OB_SUCCESS;
  rt_expr.eval_func_ = calc_partition_key;
  rt_expr.eval_batch_func_ = calc_partition_key_batch;
  return ret;
}

int ObExprFuncPartKey::calc_partition_key(const ObExpr &expr,
                                          ObEvalCtx &ctx,
                                          Datum &expr_datum)
{
  int ret = OB_SUCCESS;
  const common::ObDatumAccessContext *datum_access_ctx = nullptr;
  if (OB_FAIL(ctx.get_datum_access_ctx(datum_access_ctx))) {
  } else if (OB_FAIL(expr.eval_param_value(ctx))) {
  } else {
    uint64_t hash_value = 0;
    for (int i = 0; i < expr.arg_cnt_ && OB_SUCC(ret); i++) {
      Datum &param_datum = expr.locate_param_datum(ctx, i);
      if (ob_is_decimal_int(expr.args_[i]->datum_meta_.type_)) {
        alignas(8) char coefficient_scratch[64];
        common::ObDatum coefficient;
        if (OB_FAIL(expr.args_[i]->get_datum_copy_plan().get_decimal(param_datum, coefficient,
            coefficient_scratch, sizeof(coefficient_scratch)))) {
        } else {
          ret = wide::PartitionHash<DatumMurmurHash, common::ObDatum>::calculate(coefficient, hash_value, hash_value);
        }
      } else {
        ret = expr.args_[i]->basic_funcs_->murmur_hash_(
            param_datum, hash_value, hash_value, datum_access_ctx);
      }
      if (OB_FAIL(ret)) {
      }
    }
    if (OB_SUCC(ret)) {
      int64_t result_num = static_cast<int64_t>(hash_value);
      result_num = result_num < 0 ? -result_num : result_num;
      expr_datum.set_int(result_num);
    }
  }
  return ret;
}

int ObExprFuncPartKey::calc_partition_key_batch(BATCH_EVAL_FUNC_ARG_DECL)
{
  int ret = OB_SUCCESS;
  Datum *results = expr.locate_batch_datums(ctx);
  ObBitVector &flags = expr.get_evaluated_flags(ctx);
  const common::ObDatumAccessContext *datum_access_ctx = nullptr;
  if (OB_FAIL(ctx.get_datum_access_ctx(datum_access_ctx))) {
  }
  for (int64_t i = 0; i < expr.arg_cnt_ && OB_SUCC(ret) ; i++) {
    ObExpr *e = expr.args_[i];
    if (OB_FAIL(e->eval_batch(ctx, skip, size))) {
    } else {
      const Datum *values = e->locate_batch_datums(ctx);
      const DatumCopyPlan plan = e->get_datum_copy_plan();
      for (int64_t row = 0; OB_SUCC(ret) && row < size; ++row) {
        if (skip.at(row) || flags.at(row)) { continue; }
        const uint64_t seed = i == 0 ? 0 : static_cast<uint64_t>(results[row].get_int());
        uint64_t value = 0;
        if (ob_is_decimal_int(e->datum_meta_.type_)) {
          alignas(8) char coefficient_scratch[64];
          common::ObDatum coefficient;
          if (OB_FAIL(plan.get_decimal(values[e->is_batch_result() ? row : 0], coefficient,
              coefficient_scratch, sizeof(coefficient_scratch)))) {
          } else {
            ret = wide::PartitionHash<DatumMurmurHash, common::ObDatum>::calculate(
                coefficient, seed, value);
          }
        } else {
          ret = e->basic_funcs_->murmur_hash_batch_(values[e->is_batch_result() ? row : 0],
                                                  seed, value, datum_access_ctx);
        }
        if (OB_SUCC(ret)) {
          // Keep the signed-I64 union member active even while holding hash
          // bits. No reserved payload exists for this inline result type.
          int64_t signed_value = 0;
          MEMCPY(&signed_value, &value, sizeof(signed_value));
          results[row].set_int(signed_value);
        }
      }
    }
  }
  if (OB_SUCC(ret)) {
    for (int64_t i = 0; i < size; i++) {
      if (!skip.at(i) && !flags.at(i)) {
        const int64_t v = results[i].get_int();
        results[i].set_int(v < 0 ? -v : v);
        flags.set(i);
      }
    }
  }
  return ret;
}

}  // namespace sql
}  // namespace oceanbase
