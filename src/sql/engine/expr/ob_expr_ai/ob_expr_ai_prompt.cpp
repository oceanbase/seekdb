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
#include "common/json_type/json_builder.h"


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
  int ret = OB_SUCCESS;
  UNUSED(type_ctx);
  for (int64_t i = 0; i < param_num && OB_SUCC(ret); ++i) {
    ObExprResType &arg_type = types_stack[i];
    const ObObjType obj_type = arg_type.get_type();
    if (ob_is_string_tc(obj_type)) {
      arg_type.set_calc_type(ObVarcharType);
      arg_type.set_calc_collation_type(CS_TYPE_UTF8MB4_BIN);
    } else if (ob_is_text_tc(obj_type) && arg_type.get_collation_type() != CS_TYPE_BINARY) {
      arg_type.set_calc_collation_type(CS_TYPE_UTF8MB4_BIN);
    } else if (i == 0 || !ob_is_json(obj_type)) {
      ret = OB_ERR_INVALID_TYPE_FOR_OP;
    }
  }

  if (OB_SUCC(ret)) {
    type.set_json();
    type.set_length((ObAccuracy::DDL_DEFAULT_ACCURACY[ObJsonType]).get_length());
  }
  return ret;
}

int ObExprAIPrompt::eval_ai_prompt(const ObExpr &expr, ObEvalCtx &ctx, ObDatum &res) 
{
  INIT_SUCC(ret);
  ObDatum *template_datum = nullptr;
  const ObExpr &template_expr = *expr.args_[0];
  ObString template_str;
  ObString args_array;
  ObIJsonBase *prompt_object = nullptr;
  ObEvalCtx::TempAllocGuard tmp_alloc_g(ctx);
  MultimodeAlloctor tmp_allocator(tmp_alloc_g.get_allocator());
  lib::ObMallocHookAttrGuard malloc_guard(lib::ObMemAttr(N_AI_PROMPT));
  JsonContainerBuilder args_builder(false);

  if (OB_FAIL(tmp_allocator.eval_arg(&template_expr, ctx, template_datum))) {
  } else if (template_datum->is_null()) {
    ret = OB_INVALID_ARGUMENT;
  } else if (OB_FAIL(ObTextStringHelper::read_real_string_data(ctx.exec_ctx_, tmp_allocator,
                         *template_datum, template_expr.datum_meta_,
                         template_expr.obj_meta_.has_lob_header(), template_str))) {
  }

  for (int64_t i = 1; i < expr.arg_cnt_ && OB_SUCC(ret); ++i) {
    const ObExpr &arg = *expr.args_[i];
    if (ob_is_json(arg.datum_meta_.type_)) {
      ObIJsonBase *value = nullptr;
      bool is_null = false;
      if (OB_FAIL(ObJsonExprHelper::get_json_doc(expr, ctx, tmp_allocator, i, value, is_null))) {
      } else if (is_null) {
        ret = OB_ERR_INVALID_TYPE_FOR_OP;
      } else if (value->json_type() != ObJsonNodeType::J_ARRAY
                 && value->json_type() != ObJsonNodeType::J_OBJECT) {
        ret = OB_INVALID_ARGUMENT;
        LOG_USER_ERROR(OB_INVALID_ARGUMENT, "ai_prompt: expected a JSON array or object");
      } else if (OB_FAIL(ObAIFuncJsonUtils::append_value(tmp_allocator, args_builder, value))) {
      }
    } else {
      ObDatum *datum = nullptr;
      ObString value;
      if (OB_FAIL(tmp_allocator.eval_arg(&arg, ctx, datum))) {
      } else if (datum->is_null()) {
        ret = OB_ERR_INVALID_TYPE_FOR_OP;
      } else if (OB_FAIL(ObTextStringHelper::read_real_string_data(ctx.exec_ctx_, tmp_allocator,
                             *datum, arg.datum_meta_, arg.obj_meta_.has_lob_header(), value))) {
      } else if (OB_FAIL(args_builder.append_string(ObString(), value))) {
      }
    }
  }

  if (OB_FAIL(ret)) {
  } else if (OB_FAIL(args_builder.finish(args_array))) {
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
