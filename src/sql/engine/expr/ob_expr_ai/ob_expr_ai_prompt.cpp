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
#include "ob_expr_ai_prompt.h"
#include "sql/engine/expr/ob_expr_lob_utils.h"
#include "ob_ai_func.h"
#include "lib/utility/utility.h"
#include "common/json_type/ob_json_common.h"


using namespace oceanbase::common;
using namespace oceanbase::sql;

namespace oceanbase 
{
namespace sql 
{

ObExprAIPrompt::ObExprAIPrompt(common::ObIAllocator &alloc)
    : ObFuncExprOperator(alloc, 
                    T_FUN_SYS_AI_PROMPT, 
                    N_AI_PROMPT, 
                    MORE_THAN_ZERO,
                    VALID_FOR_GENERATED_COL, 
                    NOT_ROW_DIMENSION) 
{
}

ObExprAIPrompt::~ObExprAIPrompt() 
{
}

int ObExprAIPrompt::calc_result_typeN(ObExprResType &type,
                                    ObExprResType *types_stack,
                                    int64_t param_num,
                                    common::ObExprTypeCtx &type_ctx) const
{
  UNUSED(type_ctx);
  int ret = OB_SUCCESS;
  if (OB_UNLIKELY(param_num < 1)) {
    ret = OB_ERR_PARAM_SIZE;
  } else if (!ob_is_string_type(types_stack[0].get_type())) {
    ret = OB_ERR_INVALID_TYPE_FOR_OP;
  } else {
    // Keep TEXT/LONGTEXT in its original type: casting it to VARCHAR can
    // truncate a prompt or make an in-row datum look like a normal string.
    if (types_stack[0].get_charset_type() != CHARSET_UTF8MB4 &&
        types_stack[0].get_collation_type() != CS_TYPE_BINARY) {
      types_stack[0].set_calc_collation_type(CS_TYPE_UTF8MB4_BIN);
    }
    for (int64_t i = 1; i < param_num && OB_SUCC(ret); i++) {
      if (ob_is_string_type(types_stack[i].get_type())) {
        if (types_stack[i].get_charset_type() != CHARSET_UTF8MB4 &&
            types_stack[i].get_collation_type() != CS_TYPE_BINARY) {
          types_stack[i].set_calc_collation_type(CS_TYPE_UTF8MB4_BIN);
        }
      } else if (ob_is_json(types_stack[i].get_type())) {
        // Preserve JSON arrays / objects as JSON, not double-encoded strings.
      } else {
        ret = OB_ERR_INVALID_TYPE_FOR_OP;
      }
    }
    if (OB_SUCC(ret)) {
      type.set_json();
      type.set_length((ObAccuracy::DDL_DEFAULT_ACCURACY[ObJsonType]).get_length());
    }
  }
  return ret;
}

int ObExprAIPrompt::eval_ai_prompt(const ObExpr &expr, ObEvalCtx &ctx, ObDatum &res)
{
  INIT_SUCC(ret);
  ObDatum *template_datum = nullptr;
  ObString template_str;
  ObJsonArray *args_array = nullptr;
  ObJsonObject *prompt_object = nullptr;
  ObEvalCtx::TempAllocGuard tmp_alloc_g(ctx);
  MultimodeAlloctor tmp_allocator(tmp_alloc_g.get_allocator());
  lib::ObMallocHookAttrGuard malloc_guard(lib::ObMemAttr(N_AI_PROMPT));

  if (expr.arg_cnt_ < 1 || OB_ISNULL(expr.args_[0])) {
    ret = OB_INVALID_ARGUMENT;
  } else if (OB_FAIL(tmp_allocator.eval_arg(expr.args_[0], ctx, template_datum))) {
  } else if (OB_ISNULL(template_datum) || template_datum->is_null()) {
    ret = OB_INVALID_ARGUMENT;
  } else if (OB_FAIL(ObTextStringHelper::read_real_string_data(
                 ctx.exec_ctx_, tmp_allocator, *template_datum,
                 expr.args_[0]->datum_meta_, expr.args_[0]->obj_meta_.has_lob_header(), template_str))) {
  } else if (template_str.empty()) {
    ret = OB_INVALID_ARGUMENT;
  } else if (OB_FAIL(ObAIFuncJsonUtils::get_json_array(tmp_allocator, args_array))) {
  } else {
    for (int64_t i = 1; i < expr.arg_cnt_ && OB_SUCC(ret); i++) {
      ObDatum *datum = nullptr;
      ObExpr *arg = expr.args_[i];
      if (OB_ISNULL(arg)) {
        ret = OB_ERR_UNEXPECTED;
      } else if (OB_FAIL(tmp_allocator.eval_arg(arg, ctx, datum))) {
      } else if (OB_ISNULL(datum) || datum->is_null()) {
        ret = OB_INVALID_ARGUMENT;
      } else if (ob_is_json(arg->datum_meta_.type_)) {
        ObIJsonBase *json_value = nullptr;
        bool is_null = false;
        if (OB_FAIL(ObJsonExprHelper::get_json_doc(expr, ctx, tmp_allocator,
                                                   static_cast<uint16_t>(i), json_value, is_null))) {
        } else if (is_null || OB_ISNULL(json_value)) {
          ret = OB_INVALID_ARGUMENT;
        } else if (OB_FAIL(args_array->array_append(json_value))) {
        }
      } else if (ob_is_string_type(arg->datum_meta_.type_)) {
        ObString arg_str;
        ObJsonString *json_string = nullptr;
        if (OB_FAIL(ObTextStringHelper::read_real_string_data(
                       ctx.exec_ctx_, tmp_allocator, *datum,
                       arg->datum_meta_, arg->obj_meta_.has_lob_header(), arg_str))) {
        } else if (OB_FAIL(ObAIFuncJsonUtils::get_json_string(tmp_allocator, arg_str, json_string))) {
        } else if (OB_FAIL(args_array->append(json_string))) {
        }
      } else {
        ret = OB_ERR_INVALID_TYPE_FOR_OP;
      }
    }
  }

  if (OB_FAIL(ret)) {
  } else if (OB_FAIL(ObAIFuncPromptObjectUtils::construct_prompt_object(
                        tmp_allocator, template_str, args_array, prompt_object))) {
  } else {
    ObString raw_str;
    if (OB_FAIL(prompt_object->get_raw_binary(raw_str, &tmp_allocator))) {
    } else if (OB_FAIL(ObJsonExprHelper::pack_json_str_res(expr, ctx, res, raw_str))) {
    }
  }
  return ret;
}


int ObExprAIPrompt::cg_expr(ObExprCGCtx &expr_cg_ctx, 
                        const ObRawExpr &raw_expr,
                        ObExpr &rt_expr) const 
{
  UNUSED(expr_cg_ctx);
  UNUSED(raw_expr);
  int ret = OB_SUCCESS;
  rt_expr.eval_func_ = ObExprAIPrompt::eval_ai_prompt;
  return ret;
}

} // namespace sql
} // namespace oceanbase
