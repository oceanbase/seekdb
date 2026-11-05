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

#include "sql/engine/join/hash_join/ob_hash_join_struct.h"
#include "sql/engine/expr/ob_array_expr_utils.h"

namespace oceanbase
{
namespace sql
{
int ObHJStoredRow::convert_one_row_to_exprs(const ExprFixedArray &exprs,
                                            ObEvalCtx &eval_ctx,
                                            const RowMeta &row_meta,
                                            const ObHJStoredRow *row,
                                            const int64_t batch_idx)
{
  int ret = OB_SUCCESS;
  for (int64_t i = 0; OB_SUCC(ret) && i < exprs.count(); i++) {
    ObExpr *expr = exprs.at(i);
    if (OB_UNLIKELY(expr->is_const_expr())) {
      continue;
    } else {
      ObIVector *vec = expr->get_vector(eval_ctx);
      if (OB_FAIL(vec->from_row(row_meta, row, batch_idx, i))) {
        LOG_WARN("fail to set row to vector", K(ret), K(batch_idx), K(i), K(*expr));
      }
      exprs.at(i)->set_evaluated_projected(eval_ctx);
    }
  }
  return ret;
}

int ObHJStoredRow::convert_rows_to_exprs(const ExprFixedArray &exprs, ObEvalCtx &eval_ctx,
    const RowMeta &row_meta, const ObHJStoredRow **rows, const uint16_t *sel,
    const uint16_t sel_cnt)
{
  int ret = OB_SUCCESS;
  for (int64_t i = 0; OB_SUCC(ret) && i < exprs.count(); i++) {
    ObExpr *expr = exprs.at(i);
    if (OB_UNLIKELY(expr->is_const_expr())) {
      continue;
    } else {
      ObIVector *vec = expr->get_vector(eval_ctx);
      if (OB_FAIL(vec->from_rows(
              row_meta, reinterpret_cast<const ObCompactRow **>(rows), sel, sel_cnt, i))) {
        LOG_WARN("fail to set rows to vector", K(ret), K(i), K(*expr));
      }
      exprs.at(i)->set_evaluated_projected(eval_ctx);
    }
  }
  return ret;
}

int ObHJStoredRow::attach_rows(const ObExprPtrIArray &exprs,
                               ObEvalCtx &ctx,
                               const RowMeta &row_meta,
                               const ObHJStoredRow **srows,
                               const uint16_t selector[],
                               const int64_t size) {
  int ret = OB_SUCCESS;
  if (size <= 0) {
    // do nothing
  } else {
    for (int64_t col_idx = 0; OB_SUCC(ret) && col_idx < exprs.count(); col_idx++) {
      ObExpr *expr = exprs.at(col_idx);
      if (OB_FAIL(expr->init_vector_default(ctx, selector[size - 1] + 1))) {
        LOG_WARN("fail to init vector", K(ret));
      } else {
        ObIVector *vec = expr->get_vector(ctx);
        if (VEC_UNIFORM_CONST != vec->get_format()) {
          ret = vec->from_rows(row_meta,
                               reinterpret_cast<const ObCompactRow **>(srows),
                               selector, size, col_idx);
          expr->set_evaluated_projected(ctx);
        }
      }
    }
  }

  return ret;
}

int ObHJStoredRow::attach_rows(const ObExprPtrIArray &exprs,
                               ObEvalCtx &ctx,
                               const RowMeta &row_meta,
                               const ObHJStoredRow **srows,
                               const int64_t size) {
  int ret = OB_SUCCESS;
  if (size <= 0) {
    // do nothing
  } else {
    for (int64_t col_idx = 0; OB_SUCC(ret) && col_idx < exprs.count(); col_idx++) {
      ObExpr *expr = exprs.at(col_idx);
      if (OB_FAIL(expr->init_vector_default(ctx, size))) {
        LOG_WARN("fail to init vector", K(ret));
      } else {
        ObIVector *vec = expr->get_vector(ctx);
        if (VEC_UNIFORM_CONST != vec->get_format()) {
          ret =
            vec->from_rows(row_meta, reinterpret_cast<const ObCompactRow **>(srows), size, col_idx);
          expr->set_evaluated_projected(ctx);
        }
      }
    }
  }

  return ret;
}


} // end namespace sql
} // end namespace oceanbase
