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
  int ret = OB_SUCCESS;
  if (OB_UNLIKELY(param_num > 3 || param_num < 2)) {
    ObString func_name_(get_name());
    ret = OB_ERR_PARAM_SIZE;
    LOG_USER_ERROR(OB_ERR_PARAM_SIZE, func_name_.length(), func_name_.ptr());
  } else {
    types_stack[MODEL_IDX].set_calc_type(ObVarcharType);
    types_stack[MODEL_IDX].set_calc_collation_type(CS_TYPE_UTF8MB4_BIN);
    if (!ob_is_text_tc(types_stack[CONTENT_IDX].get_type())) {
      types_stack[CONTENT_IDX].set_calc_type(ObVarcharType);
    }
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
    type.set_type(ObLongTextType);
    type.set_accuracy(ObAccuracy::DDL_DEFAULT_ACCURACY[ObLongTextType]);
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
  } else if (arg_content->is_null()) {
    res.set_null();
  } else if (arg_model_id->is_null()) {
    ret = OB_INVALID_ARGUMENT;
    LOG_USER_ERROR(OB_INVALID_ARGUMENT, "ai_embed, model id or content is null");
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
            ctx.exec_ctx_, temp_allocator, *arg_content, expr.args_[CONTENT_IDX]->datum_meta_,
            expr.args_[CONTENT_IDX]->obj_meta_.has_lob_header(), content))) {
    } else if (model_id.empty() || content.empty()) {
      ret = OB_INVALID_ARGUMENT;
      LOG_USER_ERROR(OB_INVALID_ARGUMENT, "ai_embed, model id or input is empty");
      res.set_null();
    }
    int64_t dim = 0;
    ObJsonInt *dim_json = nullptr;
    ObJsonObject *config = nullptr;
    if (OB_FAIL(ret)) {
    } else if (OB_NOT_NULL(arg_dim)) {
      if (arg_dim->is_null()) {
        ret = OB_INVALID_ARGUMENT;
        LOG_USER_ERROR(OB_INVALID_ARGUMENT, "ai_embed, dimension must not be null");
      } else if ((dim = arg_dim->get_int()) <= 0) {
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

int ObExprAIEmbed::eval_ai_embed_batch(const ObExpr &expr, ObEvalCtx &ctx,
                                     const ObBitVector &skip, const int64_t batch_size)
{
  int ret = OB_SUCCESS;
  if (batch_size <= 0) {
    return ret;
  }
  ObEvalCtx::TempAllocGuard alloc_guard(ctx);
  MultimodeAlloctor allocator(alloc_guard.get_allocator());
  ObEvalCtx::BatchInfoScopeGuard batch_guard(ctx);
  batch_guard.set_batch_size(batch_size);
  ObBitVector &evaluated = expr.get_evaluated_flags(ctx);
  void *skip_mem = allocator.alloc(ObBitVector::memory_size(batch_size));
  ObBitVector *pending_skip = nullptr;
  if (OB_ISNULL(skip_mem)) {
    ret = OB_ALLOCATE_MEMORY_FAILED;
  } else {
    pending_skip = to_bit_vector(skip_mem);
    pending_skip->bit_calculate(skip, evaluated, batch_size,
        [](const uint64_t left, const uint64_t right) { return left | right; });
  }
  for (int64_t arg = 0; OB_SUCC(ret) && arg < expr.arg_cnt_; ++arg) {
    if (OB_FAIL(expr.args_[arg]->eval_batch(ctx, *pending_skip, batch_size))) {
    }
  }
  struct EmbedRow
  {
    ObString model;
    ObString content;
    ObString result;
    int64_t dimension = 0;
    int64_t group = -1;
    bool is_null = false;
  };
  ObArray<EmbedRow> rows;
  ObArray<int64_t> group_heads;
  // Collect and validate every active row before the first HTTP request.
  for (int64_t i = 0; OB_SUCC(ret) && i < batch_size; ++i) {
    EmbedRow row;
    if (!pending_skip->at(i)) {
      batch_guard.set_batch_idx(i);
      ObDatum &content = expr.args_[CONTENT_IDX]->locate_expr_datum(ctx, i);
      ObDatum &model = expr.args_[MODEL_IDX]->locate_expr_datum(ctx, i);
      if (content.is_null()) {
        row.is_null = true;
      } else if (model.is_null() || model.get_string().empty()) {
        ret = OB_INVALID_ARGUMENT;
        LOG_USER_ERROR(OB_INVALID_ARGUMENT, "ai_embed, model id or content is null");
      } else if (OB_FAIL(ObTextStringHelper::read_real_string_data(
                     ctx.exec_ctx_, allocator, content, expr.args_[CONTENT_IDX]->datum_meta_,
                     expr.args_[CONTENT_IDX]->obj_meta_.has_lob_header(), row.content))) {
      } else if (row.content.empty()) {
        ret = OB_INVALID_ARGUMENT;
        LOG_USER_ERROR(OB_INVALID_ARGUMENT, "ai_embed, model id or input is empty");
      } else {
        row.model = model.get_string();
        if (expr.arg_cnt_ == 3) {
          ObDatum &dimension = expr.args_[DIM_IDX]->locate_expr_datum(ctx, i);
          if (dimension.is_null() || (row.dimension = dimension.get_int()) <= 0) {
            ret = OB_INVALID_ARGUMENT;
            LOG_USER_ERROR(OB_INVALID_ARGUMENT, "ai_embed, dimension parameter must be a positive integer");
          }
        }
        for (int64_t g = 0; OB_SUCC(ret) && g < group_heads.count(); ++g) {
          const EmbedRow &head = rows.at(group_heads.at(g));
          if (row.model.case_compare(head.model) == 0 && row.dimension == head.dimension) {
            row.group = g;
            break;
          }
        }
        if (OB_SUCC(ret) && row.group == -1) {
          row.group = group_heads.count();
          ret = group_heads.push_back(i);
        }
      }
    }
    if (OB_SUCC(ret) && OB_FAIL(rows.push_back(row))) {
    }
  }
  query::ObIAiEndpointResolver *endpoint_resolver =
      ::oceanbase::share::server_service<::oceanbase::query::ObIAiEndpointResolver>();
  for (int64_t g = 0; OB_SUCC(ret) && g < group_heads.count(); ++g) {
    EmbedRow &head = rows.at(group_heads.at(g));
    ObAIFuncExprInfo *info = nullptr;
    share::ObAiModelEndpointInfo endpoint;
    if (OB_FAIL(ObAIFuncUtils::get_ai_func_info(allocator, head.model, info))) {
    } else if (OB_ISNULL(endpoint_resolver)) {
      ret = OB_ERR_UNEXPECTED;
    } else if (OB_FAIL(endpoint_resolver->resolve_by_model_name(head.model, allocator, endpoint))) {
    } else {
      ObAIFuncModel model(allocator, *info, endpoint);
      ObArray<int64_t> row_indices;
      for (int64_t i = 0; OB_SUCC(ret) && i < batch_size; ++i) {
        if (!pending_skip->at(i) && rows.at(i).group == g
            && OB_FAIL(row_indices.push_back(i))) {
        }
      }
      // Bound each remote request independently of the SQL engine's rowset size.
      static const int64_t MAX_EMBED_INPUTS = 20;
      for (int64_t begin = 0; OB_SUCC(ret) && begin < row_indices.count(); begin += MAX_EMBED_INPUTS) {
        ObArray<ObString> inputs;
        ObArray<ObString> outputs;
        ObJsonObject *config = nullptr;
        ObJsonInt *dimension = nullptr;
        if (head.dimension > 0) {
          if (OB_FAIL(ObAIFuncJsonUtils::get_json_object(allocator, config))) {
          } else if (OB_FAIL(ObAIFuncJsonUtils::get_json_int(allocator, head.dimension, dimension))) {
          } else if (OB_FAIL(config->add("dimensions", dimension))) {
          }
        }
        const int64_t end = std::min(begin + MAX_EMBED_INPUTS, row_indices.count());
        for (int64_t j = begin; OB_SUCC(ret) && j < end; ++j) {
          if (OB_FAIL(inputs.push_back(rows.at(row_indices.at(j)).content))) {
          }
        }
        if (OB_FAIL(ret)) {
        } else if (OB_FAIL(model.call_dense_embedding_vector_v2(inputs, config, outputs))) {
        } else if (outputs.count() != end - begin) {
          ret = OB_INVALID_DATA;
        } else {
          for (int64_t j = begin; j < end; ++j) {
            rows.at(row_indices.at(j)).result = outputs.at(j - begin);
          }
        }
      }
    }
  }
  // Allocate/copy durable Datum results only after every group and request succeeds.
  ObDatum *results = expr.locate_batch_datums(ctx);
  for (int64_t i = 0; OB_SUCC(ret) && i < batch_size; ++i) {
    if (!pending_skip->at(i)) {
      batch_guard.set_batch_idx(i);
      if (rows.at(i).is_null) {
        results[i].set_null();
      } else if (OB_FAIL(ObAIFuncUtils::set_string_result(expr, ctx, results[i], rows.at(i).result))) {
      }
    }
  }
  if (OB_SUCC(ret)) {
    for (int64_t i = 0; i < batch_size; ++i) {
      if (!pending_skip->at(i)) {
        evaluated.set(i);
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
