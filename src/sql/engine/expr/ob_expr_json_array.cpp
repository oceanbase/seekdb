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
#include "ob_expr_json_array.h"
#include "common/json_type/json_builder.h"
#include "sql/engine/expr/ob_expr_json_func_helper.h"

using namespace oceanbase::common;
using namespace oceanbase::sql;

namespace oceanbase
{
namespace sql
{

ObExprJsonArray::ObExprJsonArray(ObIAllocator &alloc)
    : ObFuncExprOperator(alloc,
      T_FUN_SYS_JSON_ARRAY,
      N_JSON_ARRAY, 
      PARAM_NUM_UNKNOWN,
      VALID_FOR_GENERATED_COL, NOT_ROW_DIMENSION)
{
}

ObExprJsonArray::~ObExprJsonArray()
{
}

int ObExprJsonArray::calc_result_typeN(ObExprResType& type,
                                       ObExprResType* types_stack,
                                       int64_t param_num,
                                       ObExprTypeCtx& type_ctx) const
{
  UNUSED(type_ctx);
  INIT_SUCC(ret);
  {
    if (OB_UNLIKELY(param_num < 0)) {
      ret = OB_ERR_PARAM_SIZE;
      ObString name("json_array");
      LOG_USER_ERROR(OB_ERR_PARAM_SIZE, name.length(), name.ptr());
    } else {
      // param_num >= 0
      type.set_json();
      type.set_length((ObAccuracy::DDL_DEFAULT_ACCURACY[ObJsonType]).get_length());
      for (int64_t i = 0; i < param_num; i++) {
        if (ob_is_string_type(types_stack[i].get_type())) {
          if (types_stack[i].get_type() == ObVarcharType && types_stack[i].get_collation_type() == CS_TYPE_BINARY) {
            types_stack[i].set_calc_type(ObHexStringType);
            types_stack[i].set_calc_collation_type(CS_TYPE_UTF8MB4_BIN);
          } else if (types_stack[i].get_charset_type() != CHARSET_UTF8MB4) {
            types_stack[i].set_calc_collation_type(CS_TYPE_UTF8MB4_BIN);
          }
        } else if (types_stack[i].get_type() == ObJsonType) {
          types_stack[i].set_calc_collation_type(CS_TYPE_UTF8MB4_BIN);
        }
      }
    }
  }

  return ret;
}

// for new sql engine
int ObExprJsonArray::eval_json_array(const ObExpr &expr, ObEvalCtx &ctx, ObDatum &res)
{
  INIT_SUCC(ret);
  ObEvalCtx::TempAllocGuard tmp_alloc_g(ctx);
  
  MultimodeAlloctor temp_allocator(tmp_alloc_g.get_allocator());
  lib::ObMallocHookAttrGuard malloc_guard(lib::ObMemAttr("JSONModule"));
  JsonContainerBuilder builder(false);

  if (expr.datum_meta_.cs_type_ != CS_TYPE_UTF8MB4_BIN) {
    ret = OB_ERR_INVALID_JSON_CHARSET;
  }

  for (uint32_t i = 0; OB_SUCC(ret) && i < expr.arg_cnt_; i++) {
    bool input_error = false;
    if (OB_FAIL(ObJsonExprHelper::append_json_value(expr, ctx, temp_allocator, i,
        builder, ObString(), input_error))) {
      if (input_error) { ret = OB_ERR_INVALID_JSON_TEXT_IN_PARAM; }
    }
  }

  if (OB_SUCC(ret)) {
    ObString raw_bin;
    if (OB_FAIL(builder.finish(raw_bin, false, true, true,
        std::max<uint32_t>(JSON_DOCUMENT_MAX_DEPTH, ObJsonExprHelper::get_json_max_depth_config())))) {
    } else if (OB_FAIL(ObJsonExprHelper::pack_json_str_res(expr, ctx, res, raw_bin))) {
    }
  }

  return ret;
}

int ObExprJsonArray::cg_expr(ObExprCGCtx &expr_cg_ctx, const ObRawExpr &raw_expr,
                              ObExpr &rt_expr) const
{
  UNUSED(expr_cg_ctx);
  UNUSED(raw_expr);
  rt_expr.eval_func_ = eval_json_array;

  return OB_SUCCESS;
}

}
}
