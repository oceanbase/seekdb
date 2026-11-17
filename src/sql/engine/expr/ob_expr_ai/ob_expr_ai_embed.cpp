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
#include "sql/engine/expr/ob_expr_lob_utils.h"

using namespace oceanbase::common;
using namespace oceanbase::sql;

namespace oceanbase 
{
namespace sql 
{

namespace
{
// The max count of the inputs which are carried by one embedding request.
constexpr int64_t AI_EMBED_MAX_BATCH_SIZE = 64;

// Resolve the ai model and its endpoint.
int resolve_ai_embed_target(ObIAllocator &allocator,
                             const ObString &model_id,
                             ObAIFuncExprInfo *&info,
                             share::ObAiModelEndpointInfo &resolved_endpoint)
{
  int ret = OB_SUCCESS;
  query::ObIAiEndpointResolver *endpoint_resolver =
      ::oceanbase::share::server_service<::oceanbase::query::ObIAiEndpointResolver>();
  if (OB_FAIL(ObAIFuncUtils::get_ai_func_info(allocator, model_id, info))) {
  } else if (OB_ISNULL(endpoint_resolver)) {
    ret = OB_ERR_UNEXPECTED;
  } else if (OB_FAIL(endpoint_resolver->resolve_by_model_name(
                   model_id, allocator, resolved_endpoint))) {
  }
  return ret;
}

// Build the request config of one embedding call. The dimension is only
// added when the caller gives a valid positive integer.
int build_ai_embed_config(ObIAllocator &allocator, int64_t dim, ObJsonObject *&config)
{
  int ret = OB_SUCCESS;
  ObJsonInt *dim_json = nullptr;
  if (dim <= 0) {
    // no dimension is given, the default dimension of the model is used
  } else if (OB_FAIL(ObAIFuncJsonUtils::get_json_object(allocator, config))) {
  } else if (OB_FAIL(ObAIFuncJsonUtils::get_json_int(allocator, dim, dim_json))) {
  } else if (OB_FAIL(config->add("dimensions", dim_json))) {
  }
  return ret;
}

// Set the string result of one row of the batch.
int set_ai_embed_result(const ObExpr &expr,
                         ObEvalCtx &ctx,
                         ObDatum *res,
                         ObString &res_str,
                         const int64_t batch_idx)
{
  int ret = OB_SUCCESS;
  ObTextStringDatumResult text_result(expr.datum_meta_.type_, &expr, &ctx, res);
  int64_t res_len = res_str.length();
  if (OB_FAIL(text_result.init_with_batch_idx(res_len, batch_idx))) {
  } else if (OB_FAIL(text_result.append(res_str))) {
  } else {
    text_result.set_result();
  }
  return ret;
}
} // namespace

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

int ObExprAIEmbed::eval_ai_embed(const ObExpr &expr, ObEvalCtx &ctx, ObDatum &res) 
{
  INIT_SUCC(ret);
  ObDatum *arg_model_id = nullptr;
  ObDatum *arg_content = nullptr;
  ObDatum *arg_dim = nullptr;
  if (expr.arg_cnt_ == 3 ? OB_FAIL(expr.eval_param_value(ctx, arg_model_id, arg_content, arg_dim))
                         : OB_FAIL(expr.eval_param_value(ctx, arg_model_id, arg_content))) {
  } else if (arg_model_id->is_null() || arg_content->is_null()) {
    // a null model id or a null content gives a null result, the row does not
    // enter the request of the model
    res.set_null();
  } else {
    ObEvalCtx::TempAllocGuard tmp_alloc_g(ctx);
    
    MultimodeAlloctor temp_allocator(tmp_alloc_g.get_allocator());
    lib::ObMallocHookAttrGuard malloc_guard(lib::ObMemAttr(N_AI_EMBED));
    ObAIFuncExprInfo *info = nullptr;
    share::ObAiModelEndpointInfo resolved_endpoint;
    const share::ObAiModelEndpointInfo *endpoint_info = &resolved_endpoint;
    ObString model_id = arg_model_id->get_string();
    ObString content = arg_content->get_string();
    if (model_id.empty() || content.empty()) {
      ret = OB_INVALID_ARGUMENT;
      LOG_USER_ERROR(OB_INVALID_ARGUMENT, "ai_embed, model id or input is empty");
      res.set_null();
    }
    int64_t dim = 0;
    ObJsonObject *config = nullptr;
    if (OB_FAIL(ret)) {
    } else if (OB_NOT_NULL(arg_dim)) {
      dim = arg_dim->get_int();
      if (dim <= 0) {
        ret = OB_INVALID_ARGUMENT;
        LOG_USER_ERROR(OB_INVALID_ARGUMENT, "ai_embed, dimension parameter must be a positive integer");
        res.set_null();
      } else if (OB_FAIL(build_ai_embed_config(temp_allocator, dim, config))) {
      }
    }
    if (OB_FAIL(ret)) {
    } else if (OB_FAIL(resolve_ai_embed_target(temp_allocator, model_id, info, resolved_endpoint))) {
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
                                        const int64_t batch_size)
{
  INIT_SUCC(ret);
  ObDatumVector res_datum = expr.locate_expr_datumvector(ctx);
  ObBitVector &eval_flags = expr.get_evaluated_flags(ctx);
  ObEvalCtx::TempAllocGuard tmp_alloc_g(ctx);
  MultimodeAlloctor temp_allocator(tmp_alloc_g.get_allocator());
  lib::ObMallocHookAttrGuard malloc_guard(lib::ObMemAttr(N_AI_EMBED));

  // the arguments of the whole batch are evaluated at once
  for (int64_t i = 0; OB_SUCC(ret) && i < expr.arg_cnt_; i++) {
    if (OB_FAIL(expr.args_[i]->eval_batch(ctx, skip, batch_size))) {
    }
  }

  if (OB_SUCC(ret)) {
    ObDatumVector model_id_dv = expr.args_[MODEL_IDX]->locate_expr_datumvector(ctx);
    ObDatumVector content_dv = expr.args_[CONTENT_IDX]->locate_expr_datumvector(ctx);
    ObDatumVector dim_dv;
    if (expr.arg_cnt_ == 3) {
      dim_dv = expr.args_[DIM_IDX]->locate_expr_datumvector(ctx);
    }
    // the model and the dimension may be given by a column, in that case every
    // row is calculated on its own
    const bool is_row_wise = expr.args_[MODEL_IDX]->is_batch_result()
                             || (expr.arg_cnt_ == 3 && expr.args_[DIM_IDX]->is_batch_result());
    ObAIFuncExprInfo *info = nullptr;
    share::ObAiModelEndpointInfo resolved_endpoint;
    const share::ObAiModelEndpointInfo *endpoint_info = &resolved_endpoint;
    int64_t dim = 0;
    ObJsonObject *config = nullptr;
    if (!is_row_wise) {
      ObString model_id = model_id_dv.at(0)->get_string();
      if (model_id.empty()) {
        ret = OB_INVALID_ARGUMENT;
        LOG_USER_ERROR(OB_INVALID_ARGUMENT, "ai_embed, model id or input is empty");
      } else if (expr.arg_cnt_ == 3 && !dim_dv.at(0)->is_null()) {
        dim = dim_dv.at(0)->get_int();
        if (dim <= 0) {
          ret = OB_INVALID_ARGUMENT;
          LOG_USER_ERROR(OB_INVALID_ARGUMENT, "ai_embed, dimension parameter must be a positive integer");
        }
      }
      if (OB_SUCC(ret) && OB_FAIL(build_ai_embed_config(temp_allocator, dim, config))) {
      } else if (OB_SUCC(ret) && OB_FAIL(resolve_ai_embed_target(
                                        temp_allocator, model_id, info, resolved_endpoint))) {
      }
    }

    if (OB_SUCC(ret)) {
      // collect the inputs of the rows which need the model, the null rows do
      // not enter the request and their result is null
      ObArray<ObString> contents;
      ObArray<int64_t> row_idxs;
      for (int64_t j = 0; OB_SUCC(ret) && j < batch_size; j++) {
        if (skip.at(j) || eval_flags.at(j)) {
          continue;
        }
        ObDatum *content_datum = content_dv.at(j);
        if (OB_ISNULL(content_datum) || content_datum->is_null()) {
          res_datum.at(j)->set_null();
          eval_flags.set(j);
          continue;
        }
        if (is_row_wise) {
          // every row is calculated on its own
          ObString row_model_id = model_id_dv.at(j)->get_string();
          ObString row_content = content_datum->get_string();
          int64_t row_dim = 0;
          ObJsonObject *row_config = nullptr;
          ObAIFuncExprInfo *row_info = nullptr;
          share::ObAiModelEndpointInfo row_endpoint;
          ObString result;
          if (OB_FAIL(resolve_ai_embed_target(temp_allocator, row_model_id, row_info, row_endpoint))) {
          } else if (expr.arg_cnt_ == 3 && !dim_dv.at(j)->is_null()) {
            row_dim = dim_dv.at(j)->get_int();
            if (row_dim <= 0) {
              ret = OB_INVALID_ARGUMENT;
              LOG_USER_ERROR(OB_INVALID_ARGUMENT, "ai_embed, dimension parameter must be a positive integer");
            } else if (OB_FAIL(build_ai_embed_config(temp_allocator, row_dim, row_config))) {
            }
          }
          if (OB_SUCC(ret)) {
            ObAIFuncModel model(temp_allocator, *row_info, row_endpoint);
            if (OB_FAIL(model.call_dense_embedding(row_content, row_config, result))) {
            } else if (OB_FAIL(set_ai_embed_result(expr, ctx, res_datum.at(j), result, j))) {
            } else {
              eval_flags.set(j);
            }
          }
        } else {
          ObString content = content_datum->get_string();
          if (content.empty()) {
            ret = OB_INVALID_ARGUMENT;
            LOG_USER_ERROR(OB_INVALID_ARGUMENT, "ai_embed, input is empty");
          } else if (OB_FAIL(contents.push_back(content))) {
          } else if (OB_FAIL(row_idxs.push_back(j))) {
          }
        }
      }

      if (OB_SUCC(ret) && !is_row_wise && contents.count() > 0) {
        // all the inputs of one batch are carried by one request, the request
        // is split when the count of the inputs is too large
        ObArray<ObString> results;
        const int64_t total = contents.count();
        for (int64_t begin = 0; OB_SUCC(ret) && begin < total; begin += AI_EMBED_MAX_BATCH_SIZE) {
          const int64_t end = OB_MIN(begin + AI_EMBED_MAX_BATCH_SIZE, total);
          ObArray<ObString> batch_contents;
          for (int64_t i = begin; OB_SUCC(ret) && i < end; i++) {
            if (OB_FAIL(batch_contents.push_back(contents[i]))) {
            }
          }
          ObArray<ObString> batch_results;
          if (OB_SUCC(ret)) {
            ObAIFuncModel model(temp_allocator, *info, *endpoint_info);
            if (OB_FAIL(model.call_dense_embedding_vector_v2(batch_contents, config, batch_results))) {
            } else if (batch_results.count() != batch_contents.count()) {
              ret = OB_ERR_UNEXPECTED;
              LOG_WARN("the count of the embedding results is not equal to the count of the inputs",
                       K(ret), K(batch_results.count()), K(batch_contents.count()));
            } else {
              for (int64_t i = 0; OB_SUCC(ret) && i < batch_results.count(); i++) {
                if (OB_FAIL(results.push_back(batch_results[i]))) {
                }
              }
            }
          }
        }
        // map the results back to the rows they belong to
        for (int64_t i = 0; OB_SUCC(ret) && i < results.count() && i < row_idxs.count(); i++) {
          if (OB_FAIL(set_ai_embed_result(expr, ctx, res_datum.at(row_idxs[i]), results[i], row_idxs[i]))) {
          } else {
            eval_flags.set(row_idxs[i]);
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
  //     && model_key->get_expr_type() != T_OP_GET_USER_VAR
  //     && OB_NOT_NULL(expr_cg_ctx.schema_guard_)) {
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
    rt_expr.eval_func_ = ObExprAIEmbed::eval_ai_embed;
    rt_expr.eval_batch_func_ = ObExprAIEmbed::eval_ai_embed_batch;
  }
  return ret;
}

} // namespace sql
} // namespace oceanbase
