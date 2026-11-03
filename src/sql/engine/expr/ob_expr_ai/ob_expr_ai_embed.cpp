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
      } else if (ob_is_null(types_stack[DIM_IDX].get_type())) {
        // A literal NULL dimension means "use the provider default dimension".
        // The type stays NULL and the dimension is skipped at evaluation time.
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
  } else if (arg_model_id->is_null()) {
    ret = OB_INVALID_ARGUMENT;
    LOG_USER_ERROR(OB_INVALID_ARGUMENT, "ai_embed, model id is null");
    res.set_null();
  } else if (arg_content->is_null()) {
    // SQL NULL in, SQL NULL out: a null document must not fail the statement.
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
    } else if (OB_NOT_NULL(arg_dim) && !arg_dim->is_null()) {
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

int ObExprAIEmbed::eval_ai_embed_batch(const ObExpr &expr,
                                       ObEvalCtx &ctx,
                                       const ObBitVector &skip,
                                       const int64_t batch_size)
{
  int ret = OB_SUCCESS;
  const int64_t max_request_rows = 20; // provider side input limit per request
  ObDatum *res_datums = expr.locate_batch_datums(ctx);
  ObBitVector &eval_flags = expr.get_evaluated_flags(ctx);
  ObEvalCtx::TempAllocGuard tmp_alloc_g(ctx);
  MultimodeAlloctor temp_allocator(tmp_alloc_g.get_allocator());
  lib::ObMallocHookAttrGuard malloc_guard(lib::ObMemAttr(N_AI_EMBED));
  const bool has_dim = expr.arg_cnt_ == 3;
  query::ObIAiEndpointResolver *endpoint_resolver =
      ::oceanbase::share::server_service<::oceanbase::query::ObIAiEndpointResolver>();

  for (int64_t i = 0; OB_SUCC(ret) && i < expr.arg_cnt_; ++i) {
    if (OB_FAIL(expr.args_[i]->eval_batch(ctx, skip, batch_size))) {
    }
  }

  if (OB_FAIL(ret)) {
  } else if (OB_ISNULL(res_datums) || OB_ISNULL(endpoint_resolver)) {
    ret = OB_ERR_UNEXPECTED;
  } else {
    ObDatumVector model_datums = expr.args_[MODEL_IDX]->locate_expr_datumvector(ctx);
    ObDatumVector content_datums = expr.args_[CONTENT_IDX]->locate_expr_datumvector(ctx);
    ObDatumVector dim_datums;
    if (has_dim) {
      dim_datums = expr.args_[DIM_IDX]->locate_expr_datumvector(ctx);
    }
    ObArray<int64_t> pending_rows;
    ObArray<ObString> pending_models;
    ObArray<ObString> pending_contents;
    ObArray<int64_t> pending_dims;

    for (int64_t j = 0; OB_SUCC(ret) && j < batch_size; ++j) {
      if (skip.at(j) || eval_flags.at(j)) {
        continue;
      }
      ObDatum *model_datum = model_datums.at(j);
      ObDatum *content_datum = content_datums.at(j);
      ObDatum *dim_datum = has_dim ? dim_datums.at(j) : nullptr;
      int64_t dim = 0;
      if (OB_ISNULL(model_datum) || OB_ISNULL(content_datum)) {
        ret = OB_ERR_UNEXPECTED;
      } else if (model_datum->is_null()) {
        ret = OB_INVALID_ARGUMENT;
        LOG_USER_ERROR(OB_INVALID_ARGUMENT, "ai_embed, model id is null");
      } else if (content_datum->is_null()) {
        // NULL row: result is NULL, and it never enters an HTTP request.
        res_datums[j].set_null();
        eval_flags.set(j);
      } else if (model_datum->get_string().empty() || content_datum->get_string().empty()) {
        ret = OB_INVALID_ARGUMENT;
        LOG_USER_ERROR(OB_INVALID_ARGUMENT, "ai_embed, model id or input is empty");
      } else if (has_dim && OB_NOT_NULL(dim_datum) && !dim_datum->is_null()) {
        dim = dim_datum->get_int();
        if (dim <= 0) {
          ret = OB_INVALID_ARGUMENT;
          LOG_USER_ERROR(OB_INVALID_ARGUMENT, "ai_embed, dimension parameter must be a positive integer");
        }
      }
      if (OB_SUCC(ret) && !content_datum->is_null()) {
        if (OB_FAIL(pending_rows.push_back(j)) ||
            OB_FAIL(pending_models.push_back(model_datum->get_string())) ||
            OB_FAIL(pending_contents.push_back(content_datum->get_string())) ||
            OB_FAIL(pending_dims.push_back(dim))) {
        }
      }
    }

    // Rows are grouped by (model, dimension) so that every request carries a
    // single model and a single dimension.  The groups are built over all the
    // pending rows instead of over the adjacent ones only, so alternating
    // dimensions still collapse into one request per group.  Inside a group
    // the original row order is kept.
    ObArray<ObString> group_models;
    ObArray<int64_t> group_dims;
    ObArray<int64_t> row_group;        // group index of every pending row
    ObArray<int64_t> group_start;      // first slot of a group in ordered_rows
    ObArray<int64_t> group_count;      // number of rows of a group
    ObArray<int64_t> ordered_rows;     // pending index of every ordered slot

    for (int64_t p = 0; OB_SUCC(ret) && p < pending_rows.count(); ++p) {
      const ObString &pending_model = pending_models.at(p);
      const int64_t pending_dim = pending_dims.at(p);
      int64_t group = -1;
      for (int64_t g = 0; g < group_models.count(); ++g) {
        if (group_dims.at(g) == pending_dim && group_models.at(g) == pending_model) {
          group = g;
          break;
        }
      }
      if (group < 0) {
        if (OB_FAIL(group_models.push_back(pending_model))
            || OB_FAIL(group_dims.push_back(pending_dim))) {
          break;
        }
        group = group_models.count() - 1;
      }
      if (OB_FAIL(row_group.push_back(group))) {
        break;
      }
    }

    // Order the pending rows by group, keeping the original row order inside
    // a group, so that the result of every request can be written back to the
    // row it belongs to.
    for (int64_t g = 0; OB_SUCC(ret) && g < group_models.count(); ++g) {
      int64_t count = 0;
      if (OB_FAIL(group_start.push_back(ordered_rows.count()))) {
        break;
      }
      for (int64_t p = 0; OB_SUCC(ret) && p < row_group.count(); ++p) {
        if (row_group.at(p) == g) {
          if (OB_FAIL(ordered_rows.push_back(p))) {
            break;
          }
          ++count;
        }
      }
      if (OB_SUCC(ret) && OB_FAIL(group_count.push_back(count))) {
        break;
      }
    }

    // One request per group, at most max_request_rows inputs per request.
    for (int64_t g = 0; OB_SUCC(ret) && g < group_models.count(); ++g) {
      const int64_t group_total = group_count.at(g);
      const int64_t group_begin = group_start.at(g);
      const int64_t group_dim = group_dims.at(g);
      ObAIFuncExprInfo *info = nullptr;
      share::ObAiModelEndpointInfo resolved_endpoint;
      ObJsonObject *config = nullptr;
      if (group_dim > 0) {
        ObJsonInt *dim_json = nullptr;
        if (OB_FAIL(ObAIFuncJsonUtils::get_json_object(temp_allocator, config))) {
        } else if (OB_FAIL(ObAIFuncJsonUtils::get_json_int(temp_allocator, group_dim, dim_json))) {
        } else if (OB_FAIL(config->add("dimensions", dim_json))) {
        }
      }
      for (int64_t offset = 0; OB_SUCC(ret) && offset < group_total; offset += max_request_rows) {
        int64_t chunk_len = group_total - offset;
        if (chunk_len > max_request_rows) {
          chunk_len = max_request_rows;
        }
        ObArray<ObString> chunk_contents;
        ObArray<ObString> chunk_results;
        for (int64_t k = 0; OB_SUCC(ret) && k < chunk_len; ++k) {
          if (OB_FAIL(chunk_contents.push_back(
                        pending_contents.at(ordered_rows.at(group_begin + offset + k))))) {
          }
        }
        if (OB_FAIL(ret)) {
        } else if (OB_FAIL(ObAIFuncUtils::get_ai_func_info(
                       temp_allocator, group_models.at(g), info))) {
        } else if (OB_FAIL(endpoint_resolver->resolve_by_model_name(
                       group_models.at(g), temp_allocator, resolved_endpoint))) {
        } else {
          ObAIFuncModel model(temp_allocator, *info, resolved_endpoint);
          if (OB_FAIL(model.call_dense_embedding_vector_v2(chunk_contents, config, chunk_results))) {
          } else if (chunk_results.count() != chunk_len) {
            ret = OB_ERR_UNEXPECTED;
          } else {
            for (int64_t k = 0; OB_SUCC(ret) && k < chunk_len; ++k) {
              const int64_t row = pending_rows.at(ordered_rows.at(group_begin + offset + k));
              const ObString &result = chunk_results.at(k);
              ObTextStringDatumResult output(expr.datum_meta_.type_, &expr, &ctx, &res_datums[row]);
              if (OB_FAIL(output.init_with_batch_idx(result.length(), row))) {
              } else if (OB_FAIL(output.append(result))) {
              } else {
                output.set_result();
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
