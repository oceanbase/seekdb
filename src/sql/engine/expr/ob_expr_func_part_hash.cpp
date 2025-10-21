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
#include "sql/resolver/ob_resolver_utils.h"

namespace oceanbase
{
using namespace common;
namespace sql
{

ObExprFuncPartHashBase::ObExprFuncPartHashBase(common::ObIAllocator &alloc, ObExprOperatorType type,
          const char *name, int32_t param_num, int32_t dimension,
          bool is_internal_for_mysql,
          bool is_internal_for_oracle)
    : ObFuncExprOperator(alloc, type, name, param_num, NOT_VALID_FOR_GENERATED_COL, dimension,
                         is_internal_for_mysql, is_internal_for_oracle)
{
}

template<typename T>
int ObExprFuncPartHashBase::calc_value_for_mysql(const T &input, T &output,
                                                 const ObObjType input_type)
{
  int ret = OB_SUCCESS;
  if (OB_UNLIKELY(input.is_null())) {
    output.set_int(0);
  } else {
    int64_t num = 0;
    switch (ob_obj_type_class(input_type)) {
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
      LOG_WARN("Failed to get value", K(ret));
    }
  }
  LOG_TRACE("calc hash value with mysql mode", K(ret));
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
    LOG_WARN("objs_stack is null", K(ret));
  } else {
    for (int64_t i = 0; OB_SUCC(ret) && i < param_num; ++i) {
      ObObjTypeClass tc = types_stack[i].get_type_class();
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

int ObExprFuncPartHash::calc_hash_value_with_seed(const ObObj &obj, int64_t seed, uint64_t &res)
{
  int ret = OB_SUCCESS;
  ObObjType type = obj.get_type();
  // Fixed-length types need to remove trailing spaces, see
  if (ObCharType == type) {
    ObObj obj_trimmed;
    int32_t val_len = obj.get_val_len();
    const char* obj1_str = obj.get_string_ptr();
    char* real_end = NULL;
    // oracle hash test
    if (OB_FAIL(common::ObCharset::trim_end_of_str(obj1_str, val_len, real_end,
                                                   ObCharset::charset_type_by_coll(obj.get_collation_type())))){
      LOG_WARN("fail to trim end of str", K(ret));
    } else if (OB_ISNULL(real_end)) {
      ret = OB_ERR_UNEXPECTED;
      LOG_WARN("unexpected null ptr", K(ret));
    } else {
      val_len = real_end - obj1_str;
      if (val_len < 0) {
        ret = OB_ERR_UNEXPECTED;
        LOG_WARN("unexpected length", K(val_len));
      } else {
        obj_trimmed.set_collation_type(obj.get_collation_type());
        obj_trimmed.set_string(ObCharType, obj.get_string_ptr(), val_len);
        if (OB_FAIL(obj_trimmed.hash_murmur(res, seed))) {
          LOG_WARN("fail to do hash", K(ret));
        }
      }
    }
  } else if (obj.is_decimal_int()) {
    ret = wide::PartitionHash<ObMurmurHash, ObObj>::calculate(obj, seed, res);
  } else {
    if (OB_FAIL(obj.hash_murmur(res, seed))) {
      LOG_WARN("fail to do hash", K(ret));
    }
  }
  return ret;
}
bool ObExprFuncPartHash::is_oracle_supported_type(const common::ObObjType type)
{
  bool supported = false;
  switch (type) {
    case ObIntType:
    case ObFloatType:
    case ObDoubleType:
    case ObNumberType:
    case ObDateTimeType:
    case ObCharType:
    case ObVarcharType:
    case ObDecimalIntType: {
      supported = true;
      break;
    }
    default: {
      supported = false;
    }
  }
  return supported;
}


int ObExprFuncPartHash::calc_value(
    ObExprCtx &expr_ctx,
    const ObObj *objs_stack,
    int64_t param_num,
    ObObj &result)
{
  int ret = OB_SUCCESS;
  //DO not change this function's result.
  //This will influence data.
  //If you need to do, remember ObTableLocation has the same code!!!
  CHECK_COMPATIBILITY_MODE(expr_ctx.my_session_);
  // mysql mode only allows one parameter, syntax already restricts
  if (OB_ISNULL(objs_stack) || 1 != param_num) {
    ret = OB_ERR_UNEXPECTED;
    LOG_WARN("objs_stack is null or number incorrect", K(objs_stack), K(param_num), K(ret));
  } else {
    ret = calc_value_for_mysql(objs_stack[0], result, objs_stack[0].get_type());
  }
  return ret;
}

int ObExprFuncPartHash::cg_expr(ObExprCGCtx &, const ObRawExpr &, ObExpr &rt_expr) const
{
  int ret = OB_SUCCESS;
  if (lib::is_mysql_mode()) {
    if (1 != rt_expr.arg_cnt_) {
      ret = OB_INVALID_ARGUMENT;
      LOG_WARN("expect one parameter in mysql", K(ret));
      LOG_USER_ERROR(OB_INVALID_ARGUMENT, "part hash");
    }
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
    LOG_WARN("evaluate parameter failed", K(ret));
  } else if (arg0->is_null()) {
    expr_datum.set_int(0);
  } else if (OB_FAIL(calc_value_for_mysql(*arg0, expr_datum, expr.args_[0]->datum_meta_.type_))) {
    LOG_WARN("calc value for mysql failed", K(ret));
  }
  return ret;
}

}  // namespace sql
}  // namespace oceanbase
