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
    if (!ob_is_string_type(types_stack[CONTENT_IDX].get_type())) {
      ret = OB_ERR_INVALID_TYPE_FOR_OP;
      LOG_WARN("content parameter must be a string", K(ret), K(types_stack[CONTENT_IDX]));
    } else {
      types_stack[CONTENT_IDX].set_calc_collation_type(CS_TYPE_UTF8MB4_BIN);
    }
    if (OB_SUCC(ret) && param_num == 3) {
      if (ob_is_integer_type(types_stack[DIM_IDX].get_type())) {
        types_stack[DIM_IDX].set_calc_type(ObIntType);
        types_stack[DIM_IDX].set_precision(10);
        types_stack[DIM_IDX].set_scale(0);
      } else {
        ret = OB_INVALID_ARGUMENT;
        LOG_WARN("dimension parameter must be an integer, not a decimal or float", K(ret), K(types_stack[DIM_IDX].get_type()));
        LOG_USER_ERROR(OB_INVALID_ARGUMENT, "ai_embed, dimension parameter must be an integer, not a decimal or float");
      }
    }
    type.set_varchar();
    type.set_collation_type(CS_TYPE_UTF8MB4_BIN);
    type.set_collation_level(CS_LEVEL_COERCIBLE);
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
    LOG_WARN("evaluate parameters failed", K(ret));
  } else if (arg_model_id->is_null() || arg_content->is_null()
             || (OB_NOT_NULL(arg_dim) && arg_dim->is_null())) {
    res.set_null();
  } else {
    ObEvalCtx::TempAllocGuard tmp_alloc_g(ctx);
    
    MultimodeAlloctor temp_allocator(tmp_alloc_g.get_allocator());
    lib::ObMallocHookAttrGuard malloc_guard(lib::ObMemAttr(N_AI_EMBED));
    ObAIFuncExprInfo *info = nullptr;
    share::ObAiModelEndpointInfo resolved_endpoint;
    const share::ObAiModelEndpointInfo *endpoint_info = &resolved_endpoint;
    query::ObIAiEndpointResolver *endpoint_resolver =
        ::oceanbase::share::server_service<::oceanbase::query::ObIAiEndpointResolver>();
    ObString model_id = arg_model_id->get_string();
    ObString content;
    if (OB_FAIL(ObTextStringHelper::read_real_string_data(
          ctx.exec_ctx_, temp_allocator, *arg_content,
          expr.args_[CONTENT_IDX]->datum_meta_,
          expr.args_[CONTENT_IDX]->obj_meta_.has_lob_header(), content))) {
    }
    if (OB_FAIL(ret)) {
    } else if (model_id.empty() || content.empty()) {
      ret = OB_INVALID_ARGUMENT;
      LOG_WARN("model id or input is empty", K(ret));
      LOG_USER_ERROR(OB_INVALID_ARGUMENT, "ai_embed, model id or input is empty");
      res.set_null();
    }
    int64_t dim = 0;
    ObJsonInt *dim_json = nullptr;
    ObJsonObject *config = nullptr;
    if (OB_FAIL(ret)) {
    } else if (OB_NOT_NULL(arg_dim)) {
      dim = arg_dim->get_int();
      if (dim <= 0) {
        ret = OB_INVALID_ARGUMENT;
        LOG_WARN("dimension parameter must be a positive integer", K(ret), K(dim));
        LOG_USER_ERROR(OB_INVALID_ARGUMENT, "ai_embed, dimension parameter must be a positive integer");
        res.set_null();
      } else if (OB_FAIL(ObAIFuncJsonUtils::get_json_object(temp_allocator, config))) {
      } else if (OB_FAIL(ObAIFuncJsonUtils::get_json_int(temp_allocator, dim, dim_json))) {
      } else if (OB_FAIL(config->add("dimensions", dim_json))) {
      }
    }
    if (OB_FAIL(ret)) {
    } else if (OB_FAIL(ObAIFuncUtils::get_ai_func_info(temp_allocator, model_id, info))) {
    } else if (OB_ISNULL(endpoint_resolver)) {
      ret = OB_ERR_UNEXPECTED;
      LOG_WARN("AI endpoint resolver is unavailable", K(ret));
    } else if (OB_FAIL(endpoint_resolver->resolve_by_model_name(
                   model_id, temp_allocator, resolved_endpoint))) {
    } else {
      ObAIFuncModel model(temp_allocator, *info, *endpoint_info);
      ObString result;
      if (OB_FAIL(model.call_dense_embedding(content, config, result))) {
      } else if (OB_FAIL(ObAIFuncUtils::set_string_result(expr, ctx, res, result))) {
      }
    }
  }
  return ret;
}

int ObExprAIEmbed::eval_ai_embed_batch(const ObExpr &expr,
                                       ObEvalCtx &ctx,
                                       const ObBitVector &skip,
                                       int64_t batch_size)
{
  INIT_SUCC(ret);
  ObDatumVector result_datums = expr.locate_expr_datumvector(ctx);
  ObBitVector &eval_flags = expr.get_evaluated_flags(ctx);
  ObEvalCtx::TempAllocGuard tmp_alloc_g(ctx);
  MultimodeAlloctor temp_allocator(tmp_alloc_g.get_allocator());
  lib::ObMallocHookAttrGuard malloc_guard(lib::ObMemAttr(N_AI_EMBED));
  bool *processed = nullptr;
  query::ObIAiEndpointResolver *endpoint_resolver =
      ::oceanbase::share::server_service<::oceanbase::query::ObIAiEndpointResolver>();

  for (int64_t i = 0; OB_SUCC(ret) && i < expr.arg_cnt_; ++i) {
    if (OB_FAIL(expr.args_[i]->eval_batch(ctx, skip, batch_size))) {
      LOG_WARN("failed to evaluate ai_embed argument batch", K(ret), K(i), K(batch_size));
    }
  }
  ObDatumVector model_datums;
  ObDatumVector content_datums;
  ObDatumVector dim_datums;
  if (OB_FAIL(ret)) {
  } else if (OB_ISNULL(processed = static_cast<bool *>(
                         temp_allocator.alloc(sizeof(bool) * batch_size)))) {
    ret = OB_ALLOCATE_MEMORY_FAILED;
    LOG_WARN("failed to allocate ai_embed batch state", K(ret), K(batch_size));
  } else {
    MEMSET(processed, 0, sizeof(bool) * batch_size);
    model_datums = expr.args_[MODEL_IDX]->locate_expr_datumvector(ctx);
    content_datums = expr.args_[CONTENT_IDX]->locate_expr_datumvector(ctx);
    if (expr.arg_cnt_ == 3) {
      dim_datums = expr.args_[DIM_IDX]->locate_expr_datumvector(ctx);
    }
  }

  for (int64_t seed = 0; OB_SUCC(ret) && seed < batch_size; ++seed) {
    if (skip.at(seed) || eval_flags.at(seed) || processed[seed]) {
      continue;
    }
    ObDatum *model_datum = model_datums.at(seed);
    ObDatum *content_datum = content_datums.at(seed);
    ObDatum *dim_datum = expr.arg_cnt_ == 3 ? dim_datums.at(seed) : nullptr;
    if (model_datum->is_null() || content_datum->is_null()
        || (OB_NOT_NULL(dim_datum) && dim_datum->is_null())) {
      processed[seed] = true;
      eval_flags.set(seed);
      result_datums.at(seed)->set_null();
      continue;
    }

    const ObString model_id = model_datum->get_string();
    const int64_t dimension = OB_NOT_NULL(dim_datum) ? dim_datum->get_int() : 0;
    ObArray<int64_t> row_indices;
    ObArray<ObString> contents;
    if (model_id.empty()) {
      ret = OB_INVALID_ARGUMENT;
      LOG_WARN("model id is empty", K(ret));
      LOG_USER_ERROR(OB_INVALID_ARGUMENT, "ai_embed, model id is empty");
    } else if (OB_NOT_NULL(dim_datum) && dimension <= 0) {
      ret = OB_INVALID_ARGUMENT;
      LOG_WARN("dimension parameter must be a positive integer", K(ret), K(dimension));
      LOG_USER_ERROR(OB_INVALID_ARGUMENT, "ai_embed, dimension parameter must be a positive integer");
    } else {
      for (int64_t row = seed; OB_SUCC(ret) && row < batch_size; ++row) {
        if (skip.at(row) || eval_flags.at(row) || processed[row]
            || model_datums.at(row)->is_null() || content_datums.at(row)->is_null()
            || (expr.arg_cnt_ == 3 && dim_datums.at(row)->is_null())) {
          continue;
        }
        const int64_t row_dimension = expr.arg_cnt_ == 3 ? dim_datums.at(row)->get_int() : 0;
        if (model_datums.at(row)->get_string() == model_id && row_dimension == dimension) {
          ObString content;
          if (OB_FAIL(ObTextStringHelper::read_real_string_data(
                ctx.exec_ctx_, temp_allocator, *content_datums.at(row),
                expr.args_[CONTENT_IDX]->datum_meta_,
                expr.args_[CONTENT_IDX]->obj_meta_.has_lob_header(), content))) {
          } else if (content.empty()) {
            ret = OB_INVALID_ARGUMENT;
            LOG_WARN("input is empty", K(ret), K(row));
            LOG_USER_ERROR(OB_INVALID_ARGUMENT, "ai_embed, input is empty");
          } else if (OB_FAIL(row_indices.push_back(row))) {
          } else if (OB_FAIL(contents.push_back(content))) {
          } else {
            processed[row] = true;
          }
        }
      }
    }

    ObAIFuncExprInfo *info = nullptr;
    share::ObAiModelEndpointInfo endpoint_info;
    ObJsonObject *config = nullptr;
    ObJsonInt *dimension_node = nullptr;
    if (OB_FAIL(ret)) {
    } else if (dimension > 0
               && OB_FAIL(ObAIFuncJsonUtils::get_json_object(temp_allocator, config))) {
    } else if (dimension > 0
               && OB_FAIL(ObAIFuncJsonUtils::get_json_int(
                            temp_allocator, dimension, dimension_node))) {
    } else if (dimension > 0 && OB_FAIL(config->add("dimensions", dimension_node))) {
    } else if (OB_FAIL(ObAIFuncUtils::get_ai_func_info(temp_allocator, model_id, info))) {
    } else if (OB_ISNULL(endpoint_resolver)) {
      ret = OB_ERR_UNEXPECTED;
      LOG_WARN("AI endpoint resolver is unavailable", K(ret));
    } else if (OB_FAIL(endpoint_resolver->resolve_by_model_name(
                         model_id, temp_allocator, endpoint_info))) {
    } else {
      ObAIFuncModel model(temp_allocator, *info, endpoint_info);
      ObArray<ObString> results;
      if (OB_FAIL(model.call_dense_embedding_vector_v2(contents, config, results))) {
      } else if (results.count() != row_indices.count()) {
        ret = OB_INVALID_DATA;
        LOG_WARN("embedding result count mismatch", K(ret), K(results.count()),
                 K(row_indices.count()));
      } else {
        for (int64_t i = 0; OB_SUCC(ret) && i < results.count(); ++i) {
          const int64_t row = row_indices.at(i);
          const ObString &result = results.at(i);
          char *result_buf = expr.get_str_res_mem(ctx, result.length(), row);
          if (OB_ISNULL(result_buf)) {
            ret = OB_ALLOCATE_MEMORY_FAILED;
            LOG_WARN("failed to allocate ai_embed batch result", K(ret), K(row),
                     K(result.length()));
          } else {
            MEMCPY(result_buf, result.ptr(), result.length());
            result_datums.at(row)->set_string(result_buf, result.length());
            eval_flags.set(row);
          }
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
