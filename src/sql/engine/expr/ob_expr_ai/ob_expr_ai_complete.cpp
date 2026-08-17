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
#include "ob_expr_ai_complete.h"
#include "query/ai/ob_ai_endpoint_resolver.h"
#include "share/rc/ob_server_runtime.h"

using namespace oceanbase::common;
using namespace oceanbase::sql;

namespace oceanbase 
{
namespace sql 
{
ObExprAIComplete::ObExprAIComplete(common::ObIAllocator &alloc)
    : ObFuncExprOperator(alloc, 
                        T_FUN_SYS_AI_COMPLETE, 
                        N_AI_COMPLETE,
                        MORE_THAN_ZERO, 
                        NOT_VALID_FOR_GENERATED_COL,
                        NOT_ROW_DIMENSION) 
{
}

ObExprAIComplete::~ObExprAIComplete() 
{
}

int ObExprAIComplete::calc_result_typeN(ObExprResType &type,
                                        ObExprResType *types_stack,
                                        int64_t param_num,
                                        common::ObExprTypeCtx &type_ctx) const 
{

  UNUSED(type_ctx);
  int ret = OB_SUCCESS;
  if (OB_UNLIKELY(param_num < 2 || param_num > 3)) {
    ObString func_name_(get_name());
    ret = OB_ERR_PARAM_SIZE;
    LOG_USER_ERROR(OB_ERR_PARAM_SIZE, func_name_.length(), func_name_.ptr());
  } else {
    types_stack[MODEL_IDX].set_calc_type(ObVarcharType);
    types_stack[MODEL_IDX].set_calc_collation_type(CS_TYPE_UTF8MB4_BIN);
    if (ob_is_string_type(types_stack[PROMPT_IDX].get_type())) {
      types_stack[PROMPT_IDX].set_calc_collation_type(CS_TYPE_UTF8MB4_BIN);
    } else if (ob_is_json(types_stack[PROMPT_IDX].get_type())) {
    } else {
      ret = OB_ERR_INVALID_TYPE_FOR_OP;
      LOG_WARN("invalid param type", K(ret), K(types_stack[PROMPT_IDX]));
    }

    if (OB_FAIL(ret)) {
    } else if (param_num == 3) {
      ObObjType in_type = types_stack[CONFIG_IDX].get_type();
      if (OB_FAIL(ObJsonExprHelper::is_valid_for_json(types_stack, CONFIG_IDX, N_AI_COMPLETE))) {
      } else if (ob_is_string_type(in_type) && types_stack[CONFIG_IDX].get_collation_type() != CS_TYPE_BINARY) {
        if (types_stack[CONFIG_IDX].get_charset_type() != CHARSET_UTF8MB4) {
          types_stack[CONFIG_IDX].set_calc_collation_type(CS_TYPE_UTF8MB4_BIN);
        }
      }
    }
    if (OB_SUCC(ret)) {
      type.set_type(ObLongTextType);
      type.set_collation_type(CS_TYPE_UTF8MB4_BIN);
      type.set_collation_level(CS_LEVEL_IMPLICIT);
      type.set_accuracy(ObAccuracy::DDL_DEFAULT_ACCURACY[ObLongTextType]);
    }
  }
  return ret;
}

int ObExprAIComplete::eval_ai_complete(const ObExpr &expr, 
                                       ObEvalCtx &ctx,
                                       ObDatum &res) 
{
  INIT_SUCC(ret);
  ObDatum *arg_model_id = nullptr;
  ObDatum *arg_prompt = nullptr;
  ObDatum *arg_config = nullptr;
  if (OB_FAIL(expr.eval_param_value(ctx, arg_model_id, arg_prompt, arg_config))) {
  } else if (arg_model_id->is_null() || arg_prompt->is_null()) {
    ret = OB_INVALID_ARGUMENT;
    LOG_WARN("parameters is null", K(ret));
    LOG_USER_ERROR(OB_INVALID_ARGUMENT, "ai_complete, parameters is null");
    res.set_null();
  } else {
    ObEvalCtx::TempAllocGuard tmp_alloc_g(ctx);
    
    MultimodeAlloctor temp_allocator(tmp_alloc_g.get_allocator());
    lib::ObMallocHookAttrGuard malloc_guard(lib::ObMemAttr(N_AI_COMPLETE));
    ObAIFuncExprInfo *info = nullptr;
    ObString model_id = arg_model_id->get_string();
    ObString prompt;
    ObJsonObject *config = nullptr;
    ObString config_str;
    share::ObAiModelEndpointInfo resolved_endpoint;
    const share::ObAiModelEndpointInfo *endpoint_info = &resolved_endpoint;
    query::ObIAiEndpointResolver *endpoint_resolver =
        ::oceanbase::share::server_service<::oceanbase::query::ObIAiEndpointResolver>();
    ObExpr *arg_expr_prompt = expr.args_[1];
    if ( OB_ISNULL(arg_expr_prompt) ) {
      ret = OB_ERR_UNEXPECTED;
      LOG_WARN("arg_expr_prompt is null", K(ret));
    } else if (arg_expr_prompt->datum_meta_.type_ == ObJsonType) {
      ObIJsonBase *j_base = nullptr;
      ObJsonObject *prompt_object = nullptr;
      bool is_null = false;
      if (OB_FAIL(ObJsonExprHelper::get_json_doc(expr, ctx, temp_allocator, PROMPT_IDX, j_base, is_null))) {
      } else if (j_base->json_type() != ObJsonNodeType::J_OBJECT) {
        ret = OB_INVALID_ARGUMENT;
        LOG_WARN("j_base is not json object", K(ret));
      } else if (OB_FALSE_IT(prompt_object = static_cast<ObJsonObject *>(j_base))) {
      } else if (!ObAIFuncPromptObjectUtils::is_valid_prompt_object(prompt_object)) {
        ret = OB_INVALID_ARGUMENT;
        LOG_WARN("prompt is not valid", K(ret));
        LOG_USER_ERROR(OB_INVALID_ARGUMENT, "prompt is not valid");
        res.set_null();
      } else if (OB_FAIL(ObAIFuncPromptObjectUtils::replace_all_str_args_in_template(temp_allocator, prompt_object, prompt))) {
      }
    } else if (OB_FAIL(ObTextStringHelper::read_real_string_data(ctx.exec_ctx_, temp_allocator, *arg_prompt, expr.args_[1]->datum_meta_, expr.args_[1]->obj_meta_.has_lob_header(), prompt))) {
    }

    if (OB_FAIL(ret)) {
    } else if (OB_NOT_NULL(arg_config) && !arg_config->is_null()) {
      if (OB_FAIL(ObTextStringHelper::read_real_string_data(ctx.exec_ctx_, temp_allocator, *arg_config, expr.args_[2]->datum_meta_, expr.args_[2]->obj_meta_.has_lob_header(), config_str))) {
      } else if (OB_FAIL(ObAIFuncJsonUtils::get_json_object_form_str(temp_allocator, config_str, config))) {
      }
    }

    if (OB_FAIL(ret)) {
    } else if (model_id.empty() || prompt.empty()) {
      ret = OB_INVALID_ARGUMENT;
      LOG_WARN("model id or input is empty", K(ret));
      LOG_USER_ERROR(OB_INVALID_ARGUMENT, "ai_complete, model id or input is empty");
      res.set_null();
    }

    if (OB_FAIL(ret)){
    } else if (OB_FAIL(ObAIFuncUtils::get_ai_func_info(temp_allocator, model_id, info))) {
    } else if (OB_ISNULL(endpoint_resolver)) {
      ret = OB_ERR_UNEXPECTED;
      LOG_WARN("AI endpoint resolver is unavailable", K(ret));
    } else if (OB_FAIL(endpoint_resolver->resolve_by_model_name(
                   model_id, temp_allocator, resolved_endpoint))) {
    } else {
      ObAIFuncModel model(temp_allocator, *info, *endpoint_info);
      ObString result;
      if (OB_FAIL(model.call_completion(prompt, config, result))) {
      } else if (OB_FAIL(ObAIFuncUtils::set_string_result(expr, ctx, res, result))) {
      }
    }
  }
  return ret;
}

int ObExprAIComplete::cg_expr(ObExprCGCtx &expr_cg_ctx,
                              const ObRawExpr &raw_expr,
                              ObExpr &rt_expr) const 
{
  INIT_SUCC(ret);
  // TODO: support schema version match in plan cache for ai func
  // const ObRawExpr *model_key = raw_expr.get_param_expr(0);
  // if (OB_NOT_NULL(model_key)
  //     && (model_key->is_static_scalar_const_expr() || model_key->is_const_expr())
  //     && model_key->get_expr_type() != T_OP_GET_USER_VAR &&
  //     OB_NOT_NULL(expr_cg_ctx.schema_guard_)) {
  //   ObIAllocator *allocator = expr_cg_ctx.allocator_;
  //   ObExecContext *exec_ctx = expr_cg_ctx.session_->get_cur_exec_ctx();
  //   bool got_data = false;
  //   ObObj const_data;
  //   ObAIFuncExprInfo *info = nullptr;
  //   if (OB_ISNULL(allocator)) {
  //     ret = OB_ERR_UNEXPECTED;
  //     LOG_WARN("allocator is null", K(ret));
  //   } else if (OB_FAIL(ObSQLUtils::calc_const_or_calculable_expr(exec_ctx,
  //                                                         model_key,
  //                                                         const_data,
  //                                                         got_data,
  //                                                         *allocator))) {
  //     LOG_WARN("failed to calc offset expr", K(ret));
  //   } else if (!got_data || const_data.is_null()) {
  //   } else if (OB_FAIL(ObAIFuncUtils::get_ai_func_info(*allocator, const_data.get_string(), *expr_cg_ctx.schema_guard_, info))) {
  //     LOG_WARN("failed to get ai func info", K(ret), K(const_data.get_string()));
  //   } else {
  //     rt_expr.extra_info_ = info;
  //   }
  // }

  if (OB_SUCC(ret)) {
    rt_expr.eval_func_ = ObExprAIComplete::eval_ai_complete;
  }
  return ret;
}

} // namespace sql
} // namespace oceanbase
