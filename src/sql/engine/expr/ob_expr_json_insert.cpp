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

#include "ob_expr_json_insert.h"
#include "ob_expr_json_func_helper.h"
#include "share/ob_json_access_utils.h"

using namespace oceanbase::common;
using namespace oceanbase::sql;

namespace oceanbase
{
namespace sql
{
ObExprJsonInsert::ObExprJsonInsert(ObIAllocator &alloc)
    : ObFuncExprOperator(alloc,
      T_FUN_SYS_JSON_INSERT,
      N_JSON_INSERT, 
      MORE_THAN_TWO,
      VALID_FOR_GENERATED_COL,
      NOT_ROW_DIMENSION)
{
}

ObExprJsonInsert::~ObExprJsonInsert()
{
}

int ObExprJsonInsert::calc_result_typeN(ObExprResType& type,
                                        ObExprResType* types_stack,
                                        int64_t param_num,
                                        ObExprTypeCtx& type_ctx) const
{
  UNUSED(type_ctx); // type_ctx is currently unused.
  INIT_SUCC(ret);
  const ObString name("json_insert");

  // param_num is odd, and param > 1
  if (OB_UNLIKELY((param_num & 1 ) == 0)) {
    ret = OB_ERR_PARAM_SIZE;
    LOG_USER_ERROR(OB_ERR_PARAM_SIZE, name.length(), name.ptr());
  } else {
    type.set_json();
    type.set_length((ObAccuracy::DDL_DEFAULT_ACCURACY[ObJsonType]).get_length());
    if (OB_FAIL(ObJsonExprHelper::is_valid_for_json(types_stack, 0, N_JSON_INSERT))) {
    } else {
      for (int64_t i = 1; OB_SUCC(ret) && i < param_num; i += 2) {
        //path type
        if (OB_FAIL(ObJsonExprHelper::is_valid_for_path(types_stack, i))) {
        }

        if (OB_SUCC(ret)) {
          if (types_stack[i+1].get_type() == ObNullType) {
            // do nothing
          } else if (ob_is_string_type(types_stack[i+1].get_type())) {
            if (types_stack[i+1].get_charset_type() != CHARSET_UTF8MB4) {
              types_stack[i+1].set_calc_collation_type(CS_TYPE_UTF8MB4_BIN);
            }
          } else if  (types_stack[i+1].get_type() == ObJsonType) {
            types_stack[i+1].set_calc_collation_type(CS_TYPE_UTF8MB4_BIN);
          }
        }
      }
    }
  }
  return ret;
}                                     

int ObExprJsonInsert::eval_json_insert(const ObExpr &expr, ObEvalCtx &ctx, ObDatum &res)
{
  return ObJsonExprHelper::eval_json_mutation(expr, ctx, res, JsonMutation::Insert);
}

int ObExprJsonInsert::cg_expr(ObExprCGCtx &expr_cg_ctx, const ObRawExpr &raw_expr,
                              ObExpr &rt_expr) const
{
  UNUSED(expr_cg_ctx);
  UNUSED(raw_expr);
  rt_expr.eval_func_ = eval_json_insert;
  return OB_SUCCESS;
}

}
}
