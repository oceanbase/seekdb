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
      if (ob_is_integer_type(types_stack[DIM_IDX].get_type()) 
          || types_stack[DIM_IDX].get_type() == ObNullType) {
        types_stack[DIM_IDX].set_calc_type(ObIntType);
        types_stack[DIM_IDX].set_precision(10);
        types_stack[DIM_IDX].set_scale(0);
      } else {
        ret = OB_INVALID_ARGUMENT;
        LOG_USER_ERROR(OB_INVALID_ARGUMENT, "ai_embed, dimension parameter must be an integer, not a decimal or float");
      }
    }
    if (OB_SUCC(ret)) {
      type.set_json();
      type.set_length((ObAccuracy::DDL_DEFAULT_ACCURACY[ObJsonType]).get_length());
    }
  }
  return ret;
}

int ObExprAIEmbed::build_dim_config(ObIAllocator &allocator, const int64_t dim, ObJsonObject *&config)
{
  INIT_SUCC(ret);
  ObJsonInt *dim_json = nullptr;
  config = nullptr;
  if (dim <= 0) {
    ret = OB_INVALID_ARGUMENT;
    LOG_USER_ERROR(OB_INVALID_ARGUMENT, "ai_embed, dimension parameter must be a positive integer");
  } else if (OB_FAIL(ObAIFuncJsonUtils::get_json_object(allocator, config))) {
  } else if (OB_FAIL(ObAIFuncJsonUtils::get_json_int(allocator, dim, dim_json))) {
  } else if (OB_FAIL(config->add("dimensions", dim_json))) {
  }
  return ret;
}

int ObExprAIEmbed::embed_one_row(ObIAllocator &allocator,
                                 const ObString &model_id,
                                 const ObString &content,
                                 ObJsonObject *config,
                                 ObString &result)
{
  INIT_SUCC(ret);
  ObAIFuncExprInfo *info = nullptr;
  share::ObAiModelEndpointInfo resolved_endpoint;
  query::ObIAiEndpointResolver *endpoint_resolver =
      ::oceanbase::share::server_service<::oceanbase::query::ObIAiEndpointResolver>();
  if (OB_FAIL(ObAIFuncUtils::get_ai_func_info(allocator, model_id, info))) {
  } else if (OB_ISNULL(info)) {
    ret = OB_ERR_UNEXPECTED;
  } else if (OB_ISNULL(endpoint_resolver)) {
    ret = OB_ERR_UNEXPECTED;
  } else if (OB_FAIL(ObAIFuncUtils::check_info_type_dense_embedding(info))) {
  } else if (OB_FAIL(endpoint_resolver->resolve_by_model_name(model_id, allocator, resolved_endpoint))) {
  } else {
    ObAIFuncModel model(allocator, *info, resolved_endpoint);
    if (OB_FAIL(model.call_dense_embedding(content, config, result))) {
    }
  }
  return ret;
}

int ObExprAIEmbed::embed_multi_rows(ObIAllocator &allocator,
                                    const ObString &model_id,
                                    ObArray<ObString> &contents,
                                    ObJsonObject *config,
                                    ObArray<ObString> &results)
{
  INIT_SUCC(ret);
  ObAIFuncExprInfo *info = nullptr;
  share::ObAiModelEndpointInfo resolved_endpoint;
  query::ObIAiEndpointResolver *endpoint_resolver =
      ::oceanbase::share::server_service<::oceanbase::query::ObIAiEndpointResolver>();
  if (OB_FAIL(ObAIFuncUtils::get_ai_func_info(allocator, model_id, info))) {
  } else if (OB_ISNULL(info)) {
    ret = OB_ERR_UNEXPECTED;
  } else if (OB_ISNULL(endpoint_resolver)) {
    ret = OB_ERR_UNEXPECTED;
  } else if (OB_FAIL(ObAIFuncUtils::check_info_type_dense_embedding(info))) {
  } else if (OB_FAIL(endpoint_resolver->resolve_by_model_name(model_id, allocator, resolved_endpoint))) {
  } else {
    ObAIFuncModel model(allocator, *info, resolved_endpoint);
    if (OB_FAIL(model.call_dense_embedding_vector_v2(contents, config, results))) {
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
  } else if (arg_model_id->is_null() || arg_content->is_null()) {
    // null input is propagated as a null result instead of an error, so that a
    // table scan containing null rows keeps working.
    res.set_null();
  } else {
    ObEvalCtx::TempAllocGuard tmp_alloc_g(ctx);
    
    MultimodeAlloctor temp_allocator(tmp_alloc_g.get_allocator());
    lib::ObMallocHookAttrGuard malloc_guard(lib::ObMemAttr(N_AI_EMBED));
    ObString model_id = arg_model_id->get_string();
    ObString content;
    ObExpr *content_expr = expr.args_[CONTENT_IDX];
    ObJsonObject *config = nullptr;
    if (model_id.empty()) {
      ret = OB_INVALID_ARGUMENT;
      LOG_USER_ERROR(OB_INVALID_ARGUMENT, "ai_embed, model id is empty");
      res.set_null();
    } else if (ob_is_text_tc(content_expr->datum_meta_.type_)) {
      if (OB_FAIL(ObTextStringHelper::read_real_string_data(ctx.exec_ctx_,
                                                            temp_allocator,
                                                            *arg_content,
                                                            content_expr->datum_meta_,
                                                            content_expr->obj_meta_.has_lob_header(),
                                                            content))) {
      }
    } else {
      content = arg_content->get_string();
    }
    // dimension is optional: when it is omitted (or null) the vector is
    // returned as is, otherwise the returned length is validated against it.
    if (OB_FAIL(ret)) {
    } else if (content.empty()) {
      ret = OB_INVALID_ARGUMENT;
      LOG_USER_ERROR(OB_INVALID_ARGUMENT, "ai_embed, input is empty");
      res.set_null();
    } else if (OB_NOT_NULL(arg_dim) && !arg_dim->is_null()) {
      if (OB_FAIL(build_dim_config(temp_allocator, arg_dim->get_int(), config))) {
        res.set_null();
      }
    }
    if (OB_SUCC(ret)) {
      ObString result;
      if (OB_FAIL(embed_one_row(temp_allocator, model_id, content, config, result))) {
      } else if (OB_FAIL(ObAIFuncUtils::set_string_result(expr, ctx, res, result))) {
      }
    }
  }
  return ret;
}

int ObExprAIEmbed::eval_ai_embed_batch(const ObExpr &expr, ObEvalCtx &ctx,
                                       const ObBitVector &skip, const int64_t size)
{
  INIT_SUCC(ret);
  ObDatum *res_datums = nullptr;
  if (OB_ISNULL(res_datums = expr.locate_batch_datums(ctx))) {
    ret = OB_ERR_UNEXPECTED;
  } else {
    ObBitVector &eval_flags = expr.get_evaluated_flags(ctx);
    for (int64_t i = 0; OB_SUCC(ret) && i < expr.arg_cnt_; i++) {
      if (OB_FAIL(expr.args_[i]->eval_batch(ctx, skip, size))) {
      }
    }
    if (OB_SUCC(ret)) {
      ObEvalCtx::TempAllocGuard tmp_alloc_g(ctx);
      MultimodeAlloctor allocator(tmp_alloc_g.get_allocator());
      lib::ObMallocHookAttrGuard malloc_guard(lib::ObMemAttr(N_AI_EMBED));
      ObExpr *model_expr = expr.args_[MODEL_IDX];
      ObExpr *content_expr = expr.args_[CONTENT_IDX];
      ObExpr *dim_expr = (expr.arg_cnt_ > DIM_IDX) ? expr.args_[DIM_IDX] : nullptr;
      ObArray<int64_t> row_indexes;
      ObArray<ObString> contents;
      ObString batch_model_id;
      int64_t batch_dim = 0;
      bool homogeneous = true;

      // Collect the inputs of the whole batch. Rows with a null input simply
      // produce a null result here, they do not take part in the request.
      for (int64_t i = 0; OB_SUCC(ret) && homogeneous && i < size; i++) {
        if (skip.at(i) || eval_flags.at(i)) {
          continue;
        }
        ObDatum &model_datum = model_expr->locate_expr_datum(ctx, i);
        ObDatum &content_datum = content_expr->locate_expr_datum(ctx, i);
        if (model_datum.is_null() || content_datum.is_null()) {
          res_datums[i].set_null();
          eval_flags.set(i);
          continue;
        }
        ObString content;
        if (ob_is_text_tc(content_expr->datum_meta_.type_)) {
          if (OB_FAIL(ObTextStringHelper::read_real_string_data(ctx.exec_ctx_,
                                                                allocator,
                                                                content_datum,
                                                                content_expr->datum_meta_,
                                                                content_expr->obj_meta_.has_lob_header(),
                                                                content))) {
          }
        } else {
          content = content_datum.get_string();
        }
        if (OB_FAIL(ret)) {
        } else if (model_datum.get_string().empty() || content.empty()) {
          ret = OB_INVALID_ARGUMENT;
          LOG_USER_ERROR(OB_INVALID_ARGUMENT, "ai_embed, model id or input is empty");
        } else if (row_indexes.empty()) {
          batch_model_id = model_datum.get_string();
          if (OB_NOT_NULL(dim_expr)) {
            ObDatum &dim_datum = dim_expr->locate_expr_datum(ctx, i);
            batch_dim = dim_datum.is_null() ? 0 : dim_datum.get_int();
          }
        } else if (batch_model_id != model_datum.get_string()) {
          // different endpoints inside one batch, fall back to row by row
          homogeneous = false;
        } else if (OB_NOT_NULL(dim_expr)) {
          ObDatum &dim_datum = dim_expr->locate_expr_datum(ctx, i);
          const int64_t dim = dim_datum.is_null() ? 0 : dim_datum.get_int();
          if (dim != batch_dim) {
            homogeneous = false;
          }
        }
        if (OB_SUCC(ret) && homogeneous) {
          if (OB_FAIL(row_indexes.push_back(i))) {
          } else if (OB_FAIL(contents.push_back(content))) {
          }
        }
      }

      if (OB_FAIL(ret)) {
      } else if (row_indexes.empty()) {
        // nothing to do, every row is skipped, evaluated or null
      } else if (!homogeneous) {
        for (int64_t i = 0; OB_SUCC(ret) && i < size; i++) {
          if (skip.at(i) || eval_flags.at(i)) {
            continue;
          }
          ObDatum &model_datum = model_expr->locate_expr_datum(ctx, i);
          ObDatum &content_datum = content_expr->locate_expr_datum(ctx, i);
          ObString content;
          ObJsonObject *row_config = nullptr;
          if (model_datum.is_null() || content_datum.is_null()) {
            res_datums[i].set_null();
            eval_flags.set(i);
            continue;
          } else if (ob_is_text_tc(content_expr->datum_meta_.type_)) {
            if (OB_FAIL(ObTextStringHelper::read_real_string_data(ctx.exec_ctx_,
                                                                  allocator,
                                                                  content_datum,
                                                                  content_expr->datum_meta_,
                                                                  content_expr->obj_meta_.has_lob_header(),
                                                                  content))) {
            }
          } else {
            content = content_datum.get_string();
          }
          if (OB_FAIL(ret)) {
          } else if (OB_NOT_NULL(dim_expr)) {
            ObDatum &dim_datum = dim_expr->locate_expr_datum(ctx, i);
            if (!dim_datum.is_null()
                && OB_FAIL(build_dim_config(allocator, dim_datum.get_int(), row_config))) {
            }
          }
          ObString result;
          if (OB_FAIL(ret)) {
          } else if (OB_FAIL(embed_one_row(allocator, model_datum.get_string(), content,
                                           row_config, result))) {
          } else if (OB_FAIL(ObAIFuncUtils::set_string_result(expr, ctx, res_datums[i], result))) {
          }
          if (OB_SUCC(ret)) {
            eval_flags.set(i);
          }
        }
      } else {
        ObJsonObject *config = nullptr;
        if (batch_dim > 0 && OB_FAIL(build_dim_config(allocator, batch_dim, config))) {
        }
        for (int64_t start = 0; OB_SUCC(ret) && start < contents.count(); start += EMBED_BATCH_SIZE) {
          int64_t end = start + EMBED_BATCH_SIZE;
          if (end > contents.count()) {
            end = contents.count();
          }
          ObArray<ObString> chunk_inputs;
          ObArray<ObString> chunk_results;
          for (int64_t i = start; OB_SUCC(ret) && i < end; i++) {
            if (OB_FAIL(chunk_inputs.push_back(contents.at(i)))) {
            }
          }
          if (OB_FAIL(ret)) {
          } else if (OB_FAIL(embed_multi_rows(allocator, batch_model_id, chunk_inputs,
                                              config, chunk_results))) {
          } else if (chunk_results.count() != chunk_inputs.count()) {
            ret = OB_ERR_UNEXPECTED;
          } else {
            for (int64_t i = 0; OB_SUCC(ret) && i < chunk_results.count(); i++) {
              const int64_t row = row_indexes.at(start + i);
              if (OB_FAIL(ObAIFuncUtils::set_string_result(expr, ctx, res_datums[row],
                                                           chunk_results.at(i)))) {
              } else {
                eval_flags.set(row);
              }
            }
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
