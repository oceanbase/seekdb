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
#include "share/ob_json_access_utils.h"
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
  if (lib::is_mysql_mode()) {
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
  } else {
    if (OB_UNLIKELY(param_num < 4)) {
      ret = OB_ERR_PARAM_SIZE;
      ObString name("json_array");
      LOG_USER_ERROR(OB_ERR_PARAM_SIZE, name.length(), name.ptr());
    } else {
      int64_t ele_idx = param_num - 3;
      for (int64_t i = 0; i < ele_idx && OB_SUCC(ret); i += 2) {
        if (types_stack[i].get_type() == ObNullType) {
        } else if (ob_is_string_type(types_stack[i].get_type())) {
          if (types_stack[i].get_collation_type() == CS_TYPE_BINARY) {
            types_stack[i].set_calc_collation_type(CS_TYPE_BINARY);
          } else if (types_stack[i].get_charset_type() != CHARSET_UTF8MB4) {
            types_stack[i].set_calc_collation_type(CS_TYPE_UTF8MB4_BIN);
          }
          //types_stack[i].set_calc_collation_type(types_stack[i].get_collation_type());
        } else if (types_stack[i].get_type() == ObJsonType) {
          types_stack[i].set_calc_collation_type(CS_TYPE_UTF8MB4_BIN);
        } else {
          types_stack[i].set_calc_type(types_stack[i].get_type());
          types_stack[i].set_calc_collation_type(types_stack[i].get_collation_type());
        }

        if (OB_FAIL(ret)) {
        } else if (!ob_is_integer_type(types_stack[i + 1].get_type())) {
          ret = OB_ERR_UNEXPECTED;
          LOG_WARN("failed to calc type param type should be int type", K(types_stack[i + 1].get_type()));
        } else {
          types_stack[i + 1].set_calc_collation_type(types_stack[i + 1].get_collation_type());
          types_stack[i + 1].set_calc_type(types_stack[i + 1].get_type());
        }
      }

      // returning type : 2 
      if (OB_SUCC(ret)) {
        ObExprResType dst_type;
        dst_type.set_type(ObJsonType);
        dst_type.set_collation_type(CS_TYPE_UTF8MB4_BIN);
        int16_t length_semantics = (OB_NOT_NULL(type_ctx.get_session())
                ? type_ctx.get_session()->get_actual_nls_length_semantics() : LS_BYTE);
        dst_type.set_length((ObAccuracy::DDL_DEFAULT_ACCURACY[ObJsonType]).get_length());

        dst_type.set_collation_level(CS_LEVEL_IMPLICIT);
        if (ele_idx > 0 && OB_FAIL(ObJsonExprHelper::parse_res_type(types_stack[0], types_stack[param_num - 2], dst_type, type_ctx))) {
          LOG_WARN("get cast dest type failed", K(ret));
        } else if (OB_FAIL(ObJsonExprHelper::set_dest_type(types_stack[0], type, dst_type, type_ctx))) {
          LOG_WARN("set dest type failed", K(ret));
        }
      }

      if (OB_SUCC(ret)) {
        if (!ob_is_integer_type(types_stack[param_num - 3].get_type()) 
            || !ob_is_integer_type(types_stack[param_num - 1].get_type())) {
          ret = OB_ERR_UNEXPECTED;
          LOG_WARN("failed to calc type param type should be int type",
                   K(types_stack[param_num - 1].get_type()), K(types_stack[param_num - 3].get_type()));
        } else {
          types_stack[param_num - 3].set_calc_collation_type(types_stack[param_num - 3].get_collation_type());
          types_stack[param_num - 3].set_calc_type(types_stack[param_num - 3].get_type());

          types_stack[param_num - 1].set_calc_collation_type(types_stack[param_num - 1].get_collation_type());
          types_stack[param_num - 1].set_calc_type(types_stack[param_num - 1].get_type());
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
  ObDatum *json_datum = NULL;
  ObEvalCtx::TempAllocGuard tmp_alloc_g(ctx);
  uint64_t tenant_id = ObMultiModeExprHelper::get_tenant_id(ctx.exec_ctx_.get_my_session());
  MultimodeAlloctor temp_allocator(tmp_alloc_g.get_allocator(), expr.type_, tenant_id, ret);
  lib::ObMallocHookAttrGuard malloc_guard(lib::ObMemAttr(tenant_id, "JSONModule"));
  ObJsonArray j_arr(&temp_allocator);
  ObIJsonBase *j_base = &j_arr;

  if (expr.datum_meta_.cs_type_ != CS_TYPE_UTF8MB4_BIN) {
    ret = OB_ERR_INVALID_JSON_CHARSET;
    LOG_WARN("invalid out put charset", K(ret), K(expr.datum_meta_.cs_type_));
  }

  for (uint32_t i = 0; OB_SUCC(ret) && i < expr.arg_cnt_; i++) {
    ObIJsonBase *j_val;
    if (OB_FAIL(temp_allocator.add_baseline_size(expr.args_[i], ctx))) {
      LOG_WARN("failed to add baselien size", K(ret), K(i));
    } else if (OB_FAIL(ObJsonExprHelper::get_json_val(expr, ctx, &temp_allocator, i, j_val))) {
      ret = OB_ERR_INVALID_JSON_TEXT_IN_PARAM;
      LOG_WARN("failed: get_json_val failed", K(ret));
    } else if (OB_FAIL(j_base->array_append(j_val))) {
      LOG_WARN("failed: json array append json value", K(ret));
    }
  }

  if (OB_SUCC(ret)) {
    ObString raw_bin;
    if (ObJsonExprHelper::is_json_depth_exceed_limit(j_arr.depth())) {
      ret = OB_ERR_JSON_OUT_OF_DEPTH;
      LOG_WARN("current json over depth", K(ret), K(j_arr.depth()));
    } else if (OB_FAIL(ObJsonWrapper::get_raw_binary(j_base, raw_bin, &temp_allocator))) {
      LOG_WARN("failed: json get binary", K(ret));
    } else if (OB_FAIL(ObJsonExprHelper::pack_json_str_res(expr, ctx, res, raw_bin))) {
      LOG_WARN("fail to pack json result", K(ret));
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
