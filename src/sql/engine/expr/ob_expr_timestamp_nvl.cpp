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

#include "sql/engine/expr/ob_expr_timestamp_nvl.h"
using namespace oceanbase::common;

namespace oceanbase
{
namespace sql
{

ObExprTimestampNvl::ObExprTimestampNvl(ObIAllocator &alloc)
    : ObStringExprOperator(alloc, T_FUN_SYS_TIMESTAMP_NVL, N_TIMESTAMP_NVL, 2, VALID_FOR_GENERATED_COL)
{
}

ObExprTimestampNvl::~ObExprTimestampNvl()
{
}

int ObExprTimestampNvl::calc_result_type2(ObExprResType &type,
                                          ObExprResType &type1,
                                          ObExprResType &type2,
                                          common::ObExprTypeCtx &type_ctx) const
{
  int ret = OB_SUCCESS;
  UNUSED(type_ctx);
  UNUSED(type2);
  type.set_type(ObTimestampType);
  type.set_accuracy(type1.get_accuracy());
  type.set_collation_level(type1.get_collation_level());
  type.set_collation_type(type1.get_collation_type());

  type1.set_calc_meta(type.get_obj_meta());
  type2.set_calc_meta(type.get_obj_meta());
  return ret;
}

int ObExprTimestampNvl::cg_expr(ObExprCGCtx &op_cg_ctx,
                              const ObRawExpr &raw_expr,
                              ObExpr &rt_expr) const
{
  UNUSED(op_cg_ctx);
  UNUSED(raw_expr);
  int ret = OB_SUCCESS;
  if (rt_expr.arg_cnt_ != 2) {
    ret = OB_INVALID_ARGUMENT;
    LOG_WARN("timestampnvl expr should have two params", K(ret), K(rt_expr.arg_cnt_));
  } else if (OB_ISNULL(rt_expr.args_) || OB_ISNULL(rt_expr.args_[0])
            || OB_ISNULL(rt_expr.args_[1])) {
    ret = OB_ERR_UNEXPECTED;
    LOG_WARN("children of timestampnvl expr is null", K(ret), K(rt_expr.args_));
  } else {
    rt_expr.eval_func_ = ObExprTimestampNvl::calc_timestampnvl;
  }
  return ret;
}

int ObExprTimestampNvl::calc_timestampnvl(const ObExpr &expr, ObEvalCtx &ctx,
                                          ObDatum &expr_datum)
{
  int ret = OB_SUCCESS;
  ObDatum *param_datum1 = NULL;
  ObDatum *param_datum2 = NULL;
  if (OB_FAIL(expr.eval_param_value(ctx, param_datum1, param_datum2))) {
    LOG_WARN("eval param value failed", K(ret));
  } else if (param_datum1->is_null()) {
    if (param_datum2->is_null()) {
      expr_datum.set_null();
    } else {
      expr_datum.set_timestamp(param_datum2->get_timestamp());
    }
  } else {
    expr_datum.set_timestamp(param_datum1->get_timestamp());
  }
  return ret;
}

}
}
