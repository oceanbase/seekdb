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

#define USING_LOG_PREFIX SQL

#include "ob_batch_eval_util.h"

namespace oceanbase
{
namespace sql
{

int binary_operand_batch_eval(const ObExpr &expr,
                              ObEvalCtx &ctx,
                              const ObBitVector &skip,
                              const int64_t size,
                              const bool null_short_circuit)
{
  int ret = 0;
  const ObExpr &left = *expr.args_[0];
  const ObExpr &right = *expr.args_[1];
  if (null_short_circuit) {
    if (OB_FAIL(left.eval_batch(ctx, skip, size))) {
    } else if (left.is_batch_result()) {
      const ObBitVector *rskip = &skip;
      if (!left.get_eval_info(ctx).notnull_) {
        ObBitVector &my_skip = expr.get_pvt_skip(ctx);
        rskip = &my_skip;
        my_skip.deep_copy(skip, size);
        const ObDatum *ldatums = left.locate_batch_datums(ctx);
        for (int64_t i = 0; i < size; i++) {
          if (!my_skip.at(i) && ldatums[i].is_null()) {
            my_skip.set(i);
          }
        }
      }
      if (OB_FAIL(ret)) {
      } else if (rskip->is_all_true(size)) {
        // If rskip is all true, the right expr does not need to be evaluated.
      } else if (OB_FAIL(right.eval_batch(ctx, *rskip, size))) {
      }
    } else {
      if (!left.locate_expr_datum(ctx).is_null()) {
        if (OB_FAIL(right.eval_batch(ctx, skip, size))) {
        }
      }
    }
  } else {
    if (OB_FAIL(left.eval_batch(ctx, skip, size))
        || OB_FAIL(right.eval_batch(ctx, skip, size))) {
      LOG_WARN("batch evaluate failed", K(ret), K(expr));
    }
  }
  return ret;
}

int ObNestedArithOpBaseFunc::construct_param(ObIAllocator &alloc, ObEvalCtx &ctx, const uint16_t meta_id,
                             ObString &str_data, ObIArrayType *&param_obj)
{
  return ObNestedVectorFunc::construct_param(alloc, ctx, meta_id, str_data, param_obj);
}

int ObNestedArithOpBaseFunc::construct_params(ObIAllocator &alloc, ObEvalCtx &ctx, const uint16_t left_meta_id,
                              const uint16_t right_meta_id, const uint16_t res_meta_id, ObString &left, ObString right,
                              ObIArrayType *&left_obj, ObIArrayType *&right_obj, ObIArrayType *&res_obj)
{
  int ret = OB_SUCCESS;
  if (OB_FAIL(ObArrayExprUtils::get_array_obj(alloc, ctx, left_meta_id, left, left_obj))) {
  } else if (OB_FAIL(ObArrayExprUtils::get_array_obj(alloc, ctx, right_meta_id, right, right_obj))) {
  } else if (OB_FAIL(ObArrayExprUtils::construct_array_obj(alloc, ctx, res_meta_id, res_obj, false))) {
  }
  return ret;
}

int ObNestedArithOpBaseFunc::get_res(ObEvalCtx &ctx, ObIArrayType *res_obj, const ObExpr &expr, ObString &res_str)
{
  int ret = OB_SUCCESS;
  if (OB_FAIL(res_obj->init())) {
  } else if (OB_FAIL(ObArrayExprUtils::set_array_res(res_obj, res_obj->get_raw_binary_len(), expr, ctx, res_str))) {
  }
  return ret;
}


} // end namespace sql
} // end namespace oceanbase
