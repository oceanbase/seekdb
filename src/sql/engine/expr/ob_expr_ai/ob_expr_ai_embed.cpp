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
#include "ob_expr_ai_embed.h"
#include "query/ai/ob_ai_endpoint_resolver.h"
#include "share/rc/ob_server_runtime.h"

using namespace oceanbase::common;
using namespace oceanbase::sql;

namespace oceanbase 
{
namespace sql 
{
ObExprAIEmbed::ObExprAIEmbed(common::ObIAllocator &alloc)
    : ObFuncExprOperator(alloc, 
                        T_FUN_SYS_AI_EMBED, 
                        N_AI_EMBED, 
                        MORE_THAN_ZERO,
                        NOT_VALID_FOR_GENERATED_COL, 
                        NOT_ROW_DIMENSION) 
{
}

ObExprAIEmbed::~ObExprAIEmbed() 
{
}

int ObExprAIEmbed::calc_result_typeN(ObExprResType &type,
                                     ObExprResType *types_stack,
                                     int64_t param_num,
                                     common::ObExprTypeCtx &type_ctx) const 
{
  UNUSED(type_ctx);
  UNUSED(types_stack);
  int ret = OB_SUCCESS;
  if (OB_UNLIKELY(param_num > 3 || param_num < 2)) {
    ObString func_name_(get_name());
    ret = OB_ERR_PARAM_SIZE;
    LOG_USER_ERROR(OB_ERR_PARAM_SIZE, func_name_.length(), func_name_.ptr());
  } else {
    types_stack[MODEL_IDX].set_calc_type(ObVarcharType);
    types_stack[MODEL_IDX].set_calc_collation_type(CS_TYPE_UTF8MB4_BIN);
    types_stack[CONTENT_IDX].set_calc_type(ObVarcharType);
    types_stack[CONTENT_IDX].set_calc_collation_type(CS_TYPE_UTF8MB4_BIN);
    if (param_num == 3) {
      if (ob_is_integer_type(types_stack[DIM_IDX].get_type())) {
        types_stack[DIM_IDX].set_calc_type(ObIntType);
        types_stack[DIM_IDX].set_precision(10);
        types_stack[DIM_IDX].set_scale(0);
      } else {
        ret = OB_INVALID_ARGUMENT;
        LOG_USER_ERROR(OB_INVALID_ARGUMENT, "ai_embed, dimension parameter must be an integer, not a decimal or float");
      }
    }
    type.set_varchar();
    type.set_collation_type(CS_TYPE_UTF8MB4_BIN);
    type.set_collation_level(CS_LEVEL_COERCIBLE);
  }
  return ret;
}

int ObExprAIEmbed::inner_eval_ai_embed(common::ObIAllocator &allocator,
                                       const common::ObString &model_id,
                                       common::ObArray<common::ObString> &contents,
                                       const bool has_dim,
                                       const int64_t dim,
                                       common::ObArray<common::ObString> &results)
{
  int ret = OB_SUCCESS;
  ObAIFuncExprInfo *info = nullptr;
  ObJsonInt *dim_json = nullptr;
  ObJsonObject *config = nullptr;
  share::ObAiModelEndpointInfo resolved_endpoint;
  const share::ObAiModelEndpointInfo *endpoint_info = &resolved_endpoint;
  query::ObIAiEndpointResolver *endpoint_resolver =
      ::oceanbase::share::server_service<::oceanbase::query::ObIAiEndpointResolver>();
  if (model_id.empty()) {
    ret = OB_INVALID_ARGUMENT;
    LOG_USER_ERROR(OB_INVALID_ARGUMENT, "ai_embed, model id is empty");
  } else if (contents.empty()) {
    ret = OB_INVALID_ARGUMENT;
    LOG_USER_ERROR(OB_INVALID_ARGUMENT, "ai_embed, input is empty");
  } else if (has_dim && dim <= 0) {
    ret = OB_INVALID_ARGUMENT;
    LOG_USER_ERROR(OB_INVALID_ARGUMENT, "ai_embed, dimension parameter must be a positive integer");
  } else if (has_dim) {
    if (OB_FAIL(ObAIFuncJsonUtils::get_json_object(allocator, config))) {
    } else if (OB_FAIL(ObAIFuncJsonUtils::get_json_int(allocator, dim, dim_json))) {
    } else if (OB_FAIL(config->add("dimensions", dim_json))) {
    }
  }
  if (OB_FAIL(ret)) {
  } else if (OB_FAIL(ObAIFuncUtils::get_ai_func_info(allocator, model_id, info))) {
  } else if (OB_ISNULL(endpoint_resolver)) {
    ret = OB_ERR_UNEXPECTED;
    LOG_WARN("endpoint resolver is null", K(ret));
  } else if (OB_FAIL(endpoint_resolver->resolve_by_model_name(
                 model_id, allocator, resolved_endpoint))) {
    LOG_WARN("failed to resolve endpoint by model name", K(ret), K(model_id));
  } else {
    ObAIFuncModel model(allocator, *info, *endpoint_info);
    if (OB_FAIL(model.call_dense_embedding_vector_v2(contents, config, results))) {
      LOG_WARN("failed to call dense embedding", K(ret), K(model_id));
    }
  }
  return ret;
}

int ObExprAIEmbed::eval_ai_embed(const ObExpr &expr, ObEvalCtx &ctx, ObDatum &res) 
{
  INIT_SUCC(ret);
  ObDatum *arg_model_id = nullptr;
  ObDatum *arg_content = nullptr;
  ObDatum *arg_dim = nullptr;
  if (expr.arg_cnt_ == 3 ? OB_FAIL(expr.eval_param_value(ctx, arg_model_id, arg_content, arg_dim))
                         : OB_FAIL(expr.eval_param_value(ctx, arg_model_id, arg_content))) {
  } else if (OB_ISNULL(arg_model_id) || OB_ISNULL(arg_content)
             || arg_model_id->is_null() || arg_content->is_null()
             || arg_model_id->get_string().empty() || arg_content->get_string().empty()) {
    // A row without model id or content has nothing to embed, the result is null
    // instead of failing the whole statement.
    res.set_null();
  } else {
    ObEvalCtx::TempAllocGuard tmp_alloc_g(ctx);
    MultimodeAlloctor temp_allocator(tmp_alloc_g.get_allocator());
    lib::ObMallocHookAttrGuard malloc_guard(lib::ObMemAttr(N_AI_EMBED));
    ObArray<ObString> contents;
    ObArray<ObString> results;
    bool has_dim = OB_NOT_NULL(arg_dim) && !arg_dim->is_null();
    int64_t dim = has_dim ? arg_dim->get_int() : 0;
    if (OB_FAIL(contents.push_back(arg_content->get_string()))) {
    } else if (OB_FAIL(inner_eval_ai_embed(temp_allocator, arg_model_id->get_string(),
                                            contents, has_dim, dim, results))) {
    } else if (results.count() != 1) {
      ret = OB_ERR_UNEXPECTED;
      LOG_WARN("unexpected embedding result count", K(ret), K(results.count()));
    } else if (OB_FAIL(ObAIFuncUtils::set_string_result(expr, ctx, res, results.at(0)))) {
    }
  }
  return ret;
}

int ObExprAIEmbed::eval_ai_embed_batch(const ObExpr &expr,
                                       ObEvalCtx &ctx,
                                       const ObBitVector &skip,
                                       const int64_t batch_size)
{
  int ret = OB_SUCCESS;
  ObEvalCtx::TempAllocGuard tmp_alloc_g(ctx);
  MultimodeAlloctor temp_allocator(tmp_alloc_g.get_allocator());
  lib::ObMallocHookAttrGuard malloc_guard(lib::ObMemAttr(N_AI_EMBED));
  ObDatumVector res_datum = expr.locate_expr_datumvector(ctx);
  ObBitVector &eval_flags = expr.get_evaluated_flags(ctx);
  ObDatumVector model_vec;
  ObDatumVector content_vec;
  ObDatumVector dim_vec;
  if (expr.arg_cnt_ == 3) {
    if (OB_FAIL(expr.eval_batch_param_value(ctx, skip, batch_size, model_vec, content_vec, dim_vec))) {
      LOG_WARN("failed to evaluate batch parameters", K(ret));
    }
  } else if (OB_FAIL(expr.eval_batch_param_value(ctx, skip, batch_size, model_vec, content_vec))) {
    LOG_WARN("failed to evaluate batch parameters", K(ret));
  }

  // Rows are collected first so that all of them can share a single request to
  // the endpoint. A row without content is answered with null and is never
  // sent to the endpoint, which also keeps the remaining rows aligned.
  ObArray<ObString> contents;
  ObArray<int64_t> row_indexes;
  ObArray<ObString> models;
  ObArray<int64_t> dims;
  ObArray<int64_t> has_dims;
  for (int64_t i = 0; OB_SUCC(ret) && i < batch_size; i++) {
    ObDatum *model_arg = model_vec.at(i);
    ObDatum *content_arg = content_vec.at(i);
    ObDatum *dim_arg = expr.arg_cnt_ == 3 ? dim_vec.at(i) : nullptr;
    if (skip.at(i) || eval_flags.at(i)) {
      continue;
    } else if (OB_ISNULL(model_arg) || OB_ISNULL(content_arg)
               || model_arg->is_null() || content_arg->is_null()
               || model_arg->get_string().empty() || content_arg->get_string().empty()) {
      res_datum.at(i)->set_null();
      eval_flags.set(i);
    } else if (OB_FAIL(contents.push_back(content_arg->get_string()))) {
    } else if (OB_FAIL(row_indexes.push_back(i))) {
    } else if (OB_FAIL(models.push_back(model_arg->get_string()))) {
    } else if (OB_FAIL(dims.push_back(OB_NOT_NULL(dim_arg) && !dim_arg->is_null()
                                      ? dim_arg->get_int() : 0))) {
    } else if (OB_FAIL(has_dims.push_back(OB_NOT_NULL(dim_arg) && !dim_arg->is_null() ? 1 : 0))) {
    }
  }

  if (OB_SUCC(ret) && contents.count() > 0) {
    bool same_request = true;
    for (int64_t i = 1; same_request && i < contents.count(); i++) {
      same_request = 0 == models.at(0).compare(models.at(i))
                     && has_dims.at(0) == has_dims.at(i)
                     && (0 == has_dims.at(0) || dims.at(0) == dims.at(i));
    }
    if (same_request) {
      ObArray<ObString> results;
      if (OB_FAIL(inner_eval_ai_embed(temp_allocator, models.at(0), contents,
                                      0 != has_dims.at(0), dims.at(0), results))) {
      } else if (results.count() != contents.count()) {
        ret = OB_ERR_UNEXPECTED;
        LOG_WARN("unexpected embedding result count", K(ret), K(results.count()),
                 K(contents.count()));
      } else {
        for (int64_t i = 0; OB_SUCC(ret) && i < results.count(); i++) {
          if (OB_FAIL(ObAIFuncUtils::set_string_result(expr, ctx, res_datum.at(row_indexes.at(i)),
                                                       row_indexes.at(i), results.at(i)))) {
          } else {
            eval_flags.set(row_indexes.at(i));
          }
        }
      }
    } else {
      // Rows use different models or dimensions, so each of them gets its own
      // request.
      for (int64_t i = 0; OB_SUCC(ret) && i < contents.count(); i++) {
        ObArray<ObString> one_content;
        ObArray<ObString> one_result;
        if (OB_FAIL(one_content.push_back(contents.at(i)))) {
        } else if (OB_FAIL(inner_eval_ai_embed(temp_allocator, models.at(i), one_content,
                                               0 != has_dims.at(i), dims.at(i), one_result))) {
        } else if (one_result.count() != 1) {
          ret = OB_ERR_UNEXPECTED;
          LOG_WARN("unexpected embedding result count", K(ret), K(one_result.count()));
        } else if (OB_FAIL(ObAIFuncUtils::set_string_result(expr, ctx,
                     res_datum.at(row_indexes.at(i)), row_indexes.at(i), one_result.at(0)))) {
        } else {
          eval_flags.set(row_indexes.at(i));
        }
      }
    }
  }
  return ret;
}

int ObExprAIEmbed::cg_expr(ObExprCGCtx &expr_cg_ctx, 
                           const ObRawExpr &raw_expr,
                           ObExpr &rt_expr) const 
{
  int ret = OB_SUCCESS;
  // TODO: support schema version match in plan cache for ai func
  // const ObRawExpr *model_key = raw_expr.get_param_expr(0);
  // if (OB_NOT_NULL(model_key)
  //     && (model_key->is_static_scalar_const_expr() || model_key->is_const_expr())
  //     && model_key->get_expr_type() != T_OP_GET_USER_VAR &&
  //     OB_NOT_NULL(expr_cg_ctx.schema_guard_)) {
  //   ObIAllocator *allocator = expr_cg_ctx.allocator_;
  //   ObExecContext *exec_ctx = expr_cg_ctx.session_->get_cur_exec_ctx();
  //   ObObj const_data;
  //   bool got_data = false;
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
    rt_expr.eval_func_ = ObExprAIEmbed::eval_ai_embed;
    rt_expr.eval_batch_func_ = ObExprAIEmbed::eval_ai_embed_batch;
  }
  return ret;
}

} // namespace sql
} // namespace oceanbase
