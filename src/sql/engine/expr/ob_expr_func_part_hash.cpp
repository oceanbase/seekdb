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
#include "ob_expr_func_part_hash.h"

namespace oceanbase
{
using namespace common;
namespace sql
{

ObExprFuncPartHashBase::ObExprFuncPartHashBase(common::ObIAllocator &alloc, ObExprOperatorType type,
          const char *name, int32_t param_num, int32_t dimension,
          bool is_internal_for_mysql)
    : ObFuncExprOperator(alloc, type, name, param_num, NOT_VALID_FOR_GENERATED_COL, dimension,
                         is_internal_for_mysql)
{
}

int ObExprFuncPartHashBase::calc_value_for_mysql(const ObDatum &input, ObDatum &output,
                                                 const SqlValueType input_type)
{
  int ret = OB_SUCCESS;
  if (OB_UNLIKELY(input.is_null())) {
    output.set_int(0);
  } else {
    int64_t num = 0;
    switch (sql_type_class(input_type)) {
      case ObIntTC: {
        num = input.get_int();
        break;
      }
      case ObUIntTC: {
        num = static_cast<int64_t>(input.get_uint64());
        break;
      }
      case ObBitTC: {
        num = static_cast<int64_t>(input.get_bit());
        break;
      }
      case ObYearTC: {
        num = static_cast<int64_t>(input.get_year());
        break;
      }
      default: {
        ret = OB_INVALID_ARGUMENT;
        LOG_WARN("type is wrong", K(ret), K(input_type));
        break;
      }
    }
    if (OB_SUCC(ret)) {
      if (OB_UNLIKELY(INT64_MIN == num)) {
        num = INT64_MAX;
      } else {
        num = num < 0 ? -num : num;
      }
      output.set_int(num);
    } else {
    }
  }
  return ret;
}

ObExprFuncPartHash::ObExprFuncPartHash(ObIAllocator &alloc)
    : ObExprFuncPartHashBase(alloc, T_FUN_SYS_PART_HASH, N_PART_HASH, MORE_THAN_ZERO, NOT_ROW_DIMENSION)
{
}

ObExprFuncPartHash::~ObExprFuncPartHash()
{
}

int ObExprFuncPartHash::calc_result_typeN(ObExprResType &type,
                                          ObExprResType *types_stack,
                                          int64_t param_num,
                                          ObExprTypeCtx &type_ctx) const
{
  UNUSED(type_ctx);
  int ret = OB_SUCCESS;
  if (OB_ISNULL(types_stack)) {
    ret = OB_ERR_UNEXPECTED;
  } else {
    for (int64_t i = 0; OB_SUCC(ret) && i < param_num; ++i) {
      SqlTypeClass tc = types_stack[i].get_type_class();
      if (OB_UNLIKELY(ObIntTC != tc && ObUIntTC != tc && ObBitTC != tc && ObYearTC != tc)) {
        ret = OB_ERR_PARTITION_FUNC_NOT_ALLOWED_ERROR;
        LOG_WARN("expr type class is not correct", "type", types_stack[i].get_type_class());
        LOG_USER_ERROR(OB_ERR_PARTITION_FUNC_NOT_ALLOWED_ERROR);
      }
    }
  }
  if (OB_SUCC(ret)) {
    type.set_int();
    type.set_precision(ObAccuracy::DDL_DEFAULT_ACCURACY[ObIntType].precision_);
    type.set_scale(DEFAULT_SCALE_FOR_INTEGER);
  }
  return ret;
}


int ObExprFuncPartHash::cg_expr(ObExprCGCtx &, const ObRawExpr &, ObExpr &rt_expr) const
{
  int ret = OB_SUCCESS;
  if (1 != rt_expr.arg_cnt_) {
    ret = OB_INVALID_ARGUMENT;
    LOG_USER_ERROR(OB_INVALID_ARGUMENT, "part hash");
  }

  if (OB_SUCC(ret)) {
    rt_expr.eval_func_ = eval_part_hash;
  }
  return ret;
}

int ObExprFuncPartHash::eval_part_hash(
    const ObExpr &expr, ObEvalCtx &ctx, ObDatum &expr_datum)
{
  int ret = OB_SUCCESS;
  // for mysql, see calc_value_for_mysql
  ObDatum *arg0 = NULL;
  if (OB_FAIL(expr.eval_param_value(ctx, arg0))) {
  } else if (arg0->is_null()) {
    expr_datum.set_int(0);
  } else if (OB_FAIL(calc_value_for_mysql(*arg0, expr_datum, expr.args_[0]->datum_meta_.type_))) {
  }
  return ret;
}

}  // namespace sql
}  // namespace oceanbase
