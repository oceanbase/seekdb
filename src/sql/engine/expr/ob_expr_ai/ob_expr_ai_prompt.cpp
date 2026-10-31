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
#include <regex>
#include "ob_ai_func.h"
#include "sql/engine/expr/ob_expr_json_func_helper.h"
#include "sql/engine/expr/ob_expr_lob_utils.h"
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

namespace {

// char/varchar/binary belong to ObStringTC, text/blob columns belong to ObTextTC
// (MySQL blob is a text type with CS_TYPE_BINARY in OceanBase).
bool is_prompt_content_type(const ObObjType type)
{
  return ob_is_string_tc(type) || ob_is_text_tc(type);
}

// BINARY(n)/VARBINARY(n)/BLOB keep their own type: casting them to VARCHAR drops
// the padding bytes and re-encodes the content, which is user visible in the
// generated prompt object.
bool is_binary_like_type(const ObExprResType &type)
{
  return CS_TYPE_BINARY == type.get_collation_type();
}

// char/varchar are normalized to utf8mb4 for a stable comparison, while binary
// and text values are used byte by byte.
int normalize_prompt_content_type(ObExprResType &type)
{
  int ret = OB_SUCCESS;
  if (ob_is_text_tc(type.get_type()) || is_binary_like_type(type)) {
    // keep the original type, see is_binary_like_type() for the reason
  } else {
    type.set_calc_type(ObVarcharType);
    type.set_calc_collation_type(CS_TYPE_UTF8MB4_BIN);
  }
  return ret;
}

} // namespace

int ObExprAIPrompt::calc_result_typeN(ObExprResType &type,
                                    ObExprResType *types_stack,
                                    int64_t param_num,
                                    common::ObExprTypeCtx &type_ctx) const 
{
  int ret = OB_SUCCESS;
  UNUSED(type_ctx);
  if (OB_UNLIKELY(param_num < 1)) {
    ret = OB_ERR_PARAM_SIZE;
    ObString func_name_(get_name());
    LOG_USER_ERROR(OB_ERR_PARAM_SIZE, func_name_.length(), func_name_.ptr());
  } else if (!is_prompt_content_type(types_stack[0].get_type())) {
    // the template must be a string, a text/blob column, not a json or a number
    ret = OB_ERR_INVALID_TYPE_FOR_OP;
  } else if (OB_FAIL(normalize_prompt_content_type(types_stack[0]))) {
  } else {
    for (int64_t i = 1; i < param_num && OB_SUCC(ret); i++) {
      if (ob_is_json(types_stack[i].get_type())) {
        // json arguments are embedded into the args array with their structure
        // preserved, so the calc type is kept as json
      } else if (is_prompt_content_type(types_stack[i].get_type())) {
        if (OB_FAIL(normalize_prompt_content_type(types_stack[i]))) {
        }
      } else {
        ret = OB_ERR_INVALID_TYPE_FOR_OP;
      }
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
  ObDatum *template_datum = NULL;
  ObExpr *arg0 = expr.args_[0];
  ObString template_str;
  ObJsonArray *args_array = NULL;
  ObJsonObject *prompt_object = NULL;
  ObEvalCtx::TempAllocGuard tmp_alloc_g(ctx);
  
  MultimodeAlloctor tmp_allocator(tmp_alloc_g.get_allocator());
  lib::ObMallocHookAttrGuard malloc_guard(lib::ObMemAttr(N_AI_PROMPT));

  if (OB_FAIL(tmp_allocator.eval_arg(arg0, ctx, template_datum))) {
  } else if (template_datum->is_null()) {
    ret = OB_INVALID_ARGUMENT;
  } else if (OB_FAIL(ObTextStringHelper::read_real_string_data(ctx.exec_ctx_,
                                                               tmp_allocator,
                                                               *template_datum,
                                                               arg0->datum_meta_,
                                                               arg0->obj_meta_.has_lob_header(),
                                                               template_str))) {
  } else if (OB_FAIL(ObAIFuncJsonUtils::get_json_array(tmp_allocator, args_array))) {
  } else {
    for (int64_t i = 1; i < expr.arg_cnt_ && OB_SUCC(ret); i++) {
      ObDatum *datum = NULL;
      ObExpr *arg = expr.args_[i];
      if (OB_ISNULL(arg)) {
        ret = OB_ERR_UNEXPECTED;
      } else if (OB_FAIL(tmp_allocator.eval_arg(arg, ctx, datum))) {
      } else if (datum->is_null()) {
        // null arguments are not supported, they would silently shift placeholders
        ret = OB_ERR_INVALID_TYPE_FOR_OP;
      } else if (ob_is_json(arg->datum_meta_.type_)) {
        ObIJsonBase *j_base = NULL;
        bool is_null = false;
        if (OB_FAIL(ObJsonExprHelper::get_json_doc(expr, ctx, tmp_allocator,
                                                   static_cast<uint16_t>(i), j_base, is_null))) {
        } else if (is_null || OB_ISNULL(j_base)) {
          ret = OB_ERR_INVALID_TYPE_FOR_OP;
        } else if (OB_FAIL(args_array->append(static_cast<ObJsonNode *>(j_base)))) {
        }
      } else {
        ObString arg_str;
        ObJsonString *arg_json_str = NULL;
        if (OB_FAIL(ObTextStringHelper::read_real_string_data(ctx.exec_ctx_,
                                                              tmp_allocator,
                                                              *datum,
                                                              arg->datum_meta_,
                                                              arg->obj_meta_.has_lob_header(),
                                                              arg_str))) {
        } else if (OB_FAIL(ObAIFuncJsonUtils::get_json_string(tmp_allocator, arg_str, arg_json_str))) {
        } else if (OB_FAIL(args_array->append(arg_json_str))) {
        }
      }
    }
  }

  if (OB_FAIL(ret)) {
  } else if (OB_FAIL(ObAIFuncPromptObjectUtils::construct_prompt_object(tmp_allocator, template_str, args_array, prompt_object))) {
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
