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
#include "sql/engine/expr/ob_expr_pl_associative_index.h"
#include "sql/ob_spi.h"

namespace oceanbase
{
using namespace common;
namespace sql
{
OB_SERIALIZE_MEMBER((ObExprPLAssocIndex, ObExprOperator),
                    info_.for_write_,
                    info_.out_of_range_set_err_,
                    info_.parent_expr_type_,
                    info_.is_index_by_varchar_);

ObExprPLAssocIndex::ObExprPLAssocIndex(ObIAllocator &alloc)
  : ObExprOperator(alloc, T_FUN_PL_ASSOCIATIVE_INDEX, N_PL_ASSOCIATIVE_INDEX, 2, VALID_FOR_GENERATED_COL, NOT_ROW_DIMENSION,
                  false, INTERNAL_IN_ORACLE_MODE),
    info_()
{
}

ObExprPLAssocIndex::~ObExprPLAssocIndex()
{
}

void ObExprPLAssocIndex::reset()
{
  ObExprOperator::reset();
  info_ = Info();
}

int ObExprPLAssocIndex::assign(const ObExprOperator &other)
{
  int ret = OB_SUCCESS;
  if (other.get_type() != T_FUN_PL_ASSOCIATIVE_INDEX) {
    ret = OB_ERR_UNEXPECTED;
    LOG_WARN("expr operator is mismatch", K(other.get_type()));
  } else if (OB_FAIL(ObExprOperator::assign(other))) {
    LOG_WARN("assign parent expr failed", K(ret));
  } else {
    info_ = static_cast<const ObExprPLAssocIndex &>(other).info_;
  }
  return ret;
}

int ObExprPLAssocIndex::calc_result_type2(ObExprResType &type,
                                          ObExprResType &type1,
                                          ObExprResType &type2,
                                          common::ObExprTypeCtx &type_ctx) const
{
  int ret = OB_SUCCESS;
  UNUSED(type_ctx);
  UNUSED(type1);
  UNUSED(type2);
  type.set_int();
  type.set_precision(DEFAULT_SCALE_FOR_INTEGER);
  type.set_scale(ObAccuracy::DDL_DEFAULT_ACCURACY[ObIntType].scale_);
  return ret;
}

int ObExprPLAssocIndex::cg_expr(ObExprCGCtx &op_cg_ctx,
                                const ObRawExpr &raw_expr,
                                ObExpr &rt_expr) const
{
  int ret = OB_SUCCESS;
  UNUSED(op_cg_ctx);
  const ObPLAssocIndexRawExpr &assoc_idx_expr = static_cast<const ObPLAssocIndexRawExpr &>(raw_expr);
  if (rt_expr.arg_cnt_ != 2) {
    ret = OB_ERR_UNEXPECTED;
    LOG_WARN("unexpected arg cnt", K(ret), K(rt_expr.arg_cnt_));
  } else {
    Info info;
    info.for_write_ = assoc_idx_expr.get_write();
    info.out_of_range_set_err_ = assoc_idx_expr.get_out_of_range_set_err();
    info.parent_expr_type_ = assoc_idx_expr.get_parent_type();
    info.is_index_by_varchar_ = assoc_idx_expr.is_index_by_varchar();

    rt_expr.extra_ = info.v_;
    rt_expr.eval_func_ = &eval_assoc_idx;
  }
  return ret;
}

int ObExprPLAssocIndex::eval_assoc_idx(const ObExpr &expr,
                                       ObEvalCtx &ctx,
                                       ObDatum &expr_datum)
{
  int ret = OB_SUCCESS;
  UNUSEDx(expr, ctx, expr_datum);
  return ret;
}

}  // namespace sql
}  // namespace oceanbase


