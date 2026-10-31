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
#include "lib/container/ob_se_array.h"

using namespace oceanbase::common;
using namespace oceanbase::sql;

namespace oceanbase 
{
namespace sql
{
namespace
{
struct EmbeddingInputRow
{
  EmbeddingInputRow() : row_(0), model_(), text_(), dimension_(0) {}
  EmbeddingInputRow(const int64_t row, const ObString &model,
                    const ObString &text, const int64_t dimension)
      : row_(row), model_(model), text_(text), dimension_(dimension)
  {
  }
  int64_t row_;
  ObString model_;
  ObString text_;
  int64_t dimension_;
};
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


// Use a single provider request per (model, dimension) group in an SQL batch.
// The mapping back to the original row is explicit: NULL and skipped rows do
// not consume an input slot and cannot shift later results.
int ObExprAIEmbed::eval_ai_embed_batch(const ObExpr &expr, ObEvalCtx &ctx,
                                       const ObBitVector &skip, const int64_t size)
{
  int ret = OB_SUCCESS;
  ObEvalCtx::BatchInfoScopeGuard batch_guard(ctx);
  batch_guard.set_batch_size(size);
  if (OB_UNLIKELY(size < 0 || expr.arg_cnt_ < 2 || expr.arg_cnt_ > 3)) {
    ret = OB_INVALID_ARGUMENT;
  } else if (size == 0) {
    // Nothing to evaluate.
  } else if (OB_FAIL(expr.args_[MODEL_IDX]->eval_batch(ctx, skip, size))) {
  } else if (OB_FAIL(expr.args_[CONTENT_IDX]->eval_batch(ctx, skip, size))) {
  } else if (expr.arg_cnt_ == 3 &&
             OB_FAIL(expr.args_[DIM_IDX]->eval_batch(ctx, skip, size))) {
  } else {
    ObEvalCtx::TempAllocGuard tmp_guard(ctx);
    MultimodeAlloctor allocator(tmp_guard.get_allocator());
    lib::ObMallocHookAttrGuard malloc_guard(lib::ObMemAttr(N_AI_EMBED));
    ObDatum *results = expr.locate_batch_datums(ctx);
    ObBitVector &evaluated = expr.get_evaluated_flags(ctx);
    ObSEArray<EmbeddingInputRow, 16> rows;
    ObSEArray<int64_t, 16> assigned;

    for (int64_t i = 0; OB_SUCC(ret) && i < size; ++i) {
      if (skip.contain(i) || evaluated.contain(i)) {
        continue;
      }
      const ObDatum &model = expr.args_[MODEL_IDX]->locate_expr_datum(ctx, i);
      const ObDatum &text = expr.args_[CONTENT_IDX]->locate_expr_datum(ctx, i);
      const ObDatum *dimension = expr.arg_cnt_ == 3
          ? &expr.args_[DIM_IDX]->locate_expr_datum(ctx, i) : nullptr;
      if (model.is_null() || text.is_null() ||
          (OB_NOT_NULL(dimension) && dimension->is_null())) {
        results[i].set_null();
        evaluated.set(i);
        expr.get_eval_info(ctx).notnull_ = false;
      } else if (model.get_string().empty() || text.get_string().empty()) {
        ret = OB_INVALID_ARGUMENT;
        LOG_USER_ERROR(OB_INVALID_ARGUMENT, "ai_embed, model id or input is empty");
      } else if (OB_NOT_NULL(dimension) && dimension->get_int() <= 0) {
        ret = OB_INVALID_ARGUMENT;
        LOG_USER_ERROR(OB_INVALID_ARGUMENT, "ai_embed, dimension must be positive");
      } else if (OB_FAIL(rows.push_back(EmbeddingInputRow(
                     i, model.get_string(), text.get_string(),
                     OB_NOT_NULL(dimension) ? dimension->get_int() : 0)))) {
      } else if (OB_FAIL(assigned.push_back(0))) {
      }
    }

    query::ObIAiEndpointResolver *resolver =
        ::oceanbase::share::server_service<::oceanbase::query::ObIAiEndpointResolver>();
    if (OB_SUCC(ret) && OB_ISNULL(resolver)) {
      ret = OB_ERR_UNEXPECTED;
    }

    for (int64_t first = 0; OB_SUCC(ret) && first < rows.count(); ++first) {
      if (assigned.at(first)) {
        continue;
      }
      ObArray<ObString> contents;
      ObArray<int64_t> output_rows;
      ObArray<ObString> embeddings;
      const ObString model_name = rows.at(first).model_;
      const int64_t dim = rows.at(first).dimension_;
      for (int64_t j = first; OB_SUCC(ret) && j < rows.count(); ++j) {
        if (!assigned.at(j) && rows.at(j).dimension_ == dim &&
            rows.at(j).model_.compare(model_name) == 0) {
          if (OB_FAIL(contents.push_back(rows.at(j).text_))) {
          } else if (OB_FAIL(output_rows.push_back(rows.at(j).row_))) {
          } else {
            assigned.at(j) = 1;
          }
        }
      }
      ObAIFuncExprInfo *info = nullptr;
      share::ObAiModelEndpointInfo endpoint;
      ObJsonObject *config = nullptr;
      if (OB_FAIL(ret)) {
      } else if (dim > 0) {
        ObJsonInt *dim_node = nullptr;
        if (OB_FAIL(ObAIFuncJsonUtils::get_json_object(allocator, config))) {
        } else if (OB_FAIL(ObAIFuncJsonUtils::get_json_int(allocator, dim, dim_node))) {
        } else if (OB_FAIL(config->add("dimensions", dim_node))) {
        }
      }
      if (OB_FAIL(ret)) {
      } else if (OB_FAIL(ObAIFuncUtils::get_ai_func_info(allocator, model_name, info))) {
      } else if (OB_ISNULL(info)) {
        ret = OB_ERR_UNEXPECTED;
      } else if (OB_FAIL(resolver->resolve_by_model_name(model_name, allocator, endpoint))) {
      } else {
        ObAIFuncModel client(allocator, *info, endpoint);
        if (OB_FAIL(client.call_dense_embedding_vector_v2(contents, config, embeddings))) {
        } else if (embeddings.count() != output_rows.count()) {
          ret = OB_INVALID_DATA;
        } else {
          for (int64_t k = 0; OB_SUCC(ret) && k < embeddings.count(); ++k) {
            const int64_t row_id = output_rows.at(k);
            batch_guard.set_batch_idx(row_id);
            ObString value = embeddings.at(k);
            if (OB_FAIL(ObAIFuncUtils::set_string_result(expr, ctx, results[row_id], value))) {
            } else {
              evaluated.set(row_id);
            }
          }
        }
      }
    }
  }
  return ret;
}

int ObExprAIEmbed::eval_ai_embed(const ObExpr &expr, ObEvalCtx &ctx, ObDatum &res)
{
  // When nested in a row-wise parent, the parent's skip vector is the only
  // safe set of rows to batch. No skip information -> keep scalar semantics.
  if (expr.is_batch_result() && ctx.is_vectorized() &&
      ctx.get_batch_size() > 1 && OB_NOT_NULL(ctx.get_batch_skip())) {
    return eval_ai_embed_batch(expr, ctx, *ctx.get_batch_skip(), ctx.get_batch_size());
  }
  INIT_SUCC(ret);
  ObDatum *arg_model_id = nullptr;
  ObDatum *arg_content = nullptr;
  ObDatum *arg_dim = nullptr;
  if (expr.arg_cnt_ == 3 ? OB_FAIL(expr.eval_param_value(ctx, arg_model_id, arg_content, arg_dim))
                         : OB_FAIL(expr.eval_param_value(ctx, arg_model_id, arg_content))) {
  } else if (arg_model_id->is_null() || arg_content->is_null() ||
             (OB_NOT_NULL(arg_dim) && arg_dim->is_null())) {
    // SQL NULL is propagated without sending an unnecessary model request.
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
    ObString content = arg_content->get_string();
    if (model_id.empty() || content.empty()) {
      ret = OB_INVALID_ARGUMENT;
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
