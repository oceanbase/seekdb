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
namespace
{
int eval_ai_embed_batch(BATCH_EVAL_FUNC_ARG_DECL);
}

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
    if (ob_is_string_tc(types_stack[CONTENT_IDX].get_type())) {
      types_stack[CONTENT_IDX].set_calc_type(ObVarcharType);
      types_stack[CONTENT_IDX].set_calc_collation_type(CS_TYPE_UTF8MB4_BIN);
    } else if (ob_is_text_tc(types_stack[CONTENT_IDX].get_type()) &&
               types_stack[CONTENT_IDX].get_collation_type() != CS_TYPE_BINARY) {
      // Keep TEXT/LONGTEXT as LOB so that out-of-row LOB values are read in full.
      types_stack[CONTENT_IDX].set_calc_collation_type(CS_TYPE_UTF8MB4_BIN);
    }
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
  } else if (expr.is_batch_result() && ctx.get_batch_size() > 1) {
    // The expression may be evaluated row-by-row by a non-vectorized parent
    // (for example json_length(ai_embed(...))). In that case run the whole
    // vector batch once and let later rows reuse the prepared results.
    const int64_t batch_size = ctx.get_batch_size();
    ObEvalCtx::TempAllocGuard batch_alloc_g(ctx);
    char *skip_mem = static_cast<char *>(
        batch_alloc_g.get_allocator().alloc(ObBitVector::memory_size(batch_size)));
    if (OB_ISNULL(skip_mem)) {
      ret = OB_ALLOCATE_MEMORY_FAILED;
    } else {
      ObBitVector *skip = to_bit_vector(skip_mem);
      skip->init(batch_size);
      ret = eval_ai_embed_batch(expr, ctx, *skip, batch_size);
    }
  } else if (arg_model_id->is_null() || arg_content->is_null()) {
    // NULL input propagates to the result instead of failing the whole query.
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
    ObString model_id;
    ObString content;
    if (OB_FAIL(ObTextStringHelper::read_real_string_data(
            ctx.exec_ctx_, temp_allocator, *arg_model_id,
            expr.args_[MODEL_IDX]->datum_meta_,
            expr.args_[MODEL_IDX]->obj_meta_.has_lob_header(), model_id))) {
    } else if (OB_FAIL(ObTextStringHelper::read_real_string_data(
                   ctx.exec_ctx_, temp_allocator, *arg_content,
                   expr.args_[CONTENT_IDX]->datum_meta_,
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
        LOG_USER_ERROR(OB_INVALID_ARGUMENT, "ai_embed, dimension parameter must be a positive integer");
        res.set_null();
      } else {
        dim = arg_dim->get_int();
      }
      if (OB_FAIL(ret)) {
      } else if (dim <= 0) {
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

namespace
{

constexpr int64_t AI_EMBED_MODEL_IDX = 0;
constexpr int64_t AI_EMBED_CONTENT_IDX = 1;
constexpr int64_t AI_EMBED_DIM_IDX = 2;
// Keep each embedding request reasonably small so that large scans are split
// into multiple batches instead of sending one unbounded request.
constexpr int64_t AI_EMBED_MAX_BATCH_SIZE = 20;

int build_embed_dim_config(ObIAllocator &allocator, const int64_t dim, ObJsonObject *&config)
{
  int ret = OB_SUCCESS;
  config = nullptr;
  if (dim > 0) {
    ObJsonInt *dim_json = nullptr;
    if (OB_FAIL(ObAIFuncJsonUtils::get_json_object(allocator, config))) {
    } else if (OB_FAIL(ObAIFuncJsonUtils::get_json_int(allocator, dim, dim_json))) {
    } else if (OB_FAIL(config->add("dimensions", dim_json))) {
    }
  }
  return ret;
}

int flush_embed_group(const ObExpr &expr,
                      ObEvalCtx &ctx,
                      ObIAllocator &allocator,
                      ObDatum *res_datums,
                      ObBitVector &eval_flags,
                      const ObString &model_id,
                      const int64_t dim,
                      ObArray<ObString> &contents,
                      ObArray<int64_t> &row_idxs)
{
  int ret = OB_SUCCESS;
  if (contents.empty()) {
    // nothing to do
  } else if (contents.count() != row_idxs.count()) {
    ret = OB_ERR_UNEXPECTED;
  } else {
    ObAIFuncExprInfo *info = nullptr;
    share::ObAiModelEndpointInfo resolved_endpoint;
    query::ObIAiEndpointResolver *endpoint_resolver =
        ::oceanbase::share::server_service<::oceanbase::query::ObIAiEndpointResolver>();
    ObJsonObject *config = nullptr;
    if (OB_FAIL(build_embed_dim_config(allocator, dim, config))) {
    } else if (OB_FAIL(ObAIFuncUtils::get_ai_func_info(allocator, model_id, info))) {
    } else if (OB_ISNULL(info)) {
      ret = OB_ERR_UNEXPECTED;
    } else if (OB_FAIL(ObAIFuncUtils::check_info_type_dense_embedding(info))) {
    } else if (OB_ISNULL(endpoint_resolver)) {
      ret = OB_ERR_UNEXPECTED;
    } else if (OB_FAIL(endpoint_resolver->resolve_by_model_name(model_id, allocator,
                                                               resolved_endpoint))) {
    } else {
      ObAIFuncModel model(allocator, *info, resolved_endpoint);
      ObArray<ObString> results;
      if (OB_FAIL(model.call_dense_embedding_vector_v2(contents, config, results))) {
      } else if (results.count() != contents.count()) {
        ret = OB_ERR_UNEXPECTED;
        LOG_WARN("ai_embed, batch result count mismatch", K(ret),
                 K(results.count()), K(contents.count()));
      } else {
        for (int64_t i = 0; OB_SUCC(ret) && i < results.count(); ++i) {
          const int64_t row_idx = row_idxs.at(i);
          const ObString &result = results.at(i);
          char *result_buf = nullptr;
          if (row_idx < 0 || OB_ISNULL(result_buf = expr.get_str_res_mem(ctx, result.length(), row_idx))) {
            ret = OB_ALLOCATE_MEMORY_FAILED;
          } else {
            if (!result.empty()) {
              MEMCPY(result_buf, result.ptr(), result.length());
            }
            res_datums[row_idx].set_string(result_buf, result.length());
            eval_flags.set(row_idx);
          }
        }
      }
    }
  }
  return ret;
}

int eval_ai_embed_batch(BATCH_EVAL_FUNC_ARG_DECL)
{
  int ret = OB_SUCCESS;
  if (OB_UNLIKELY(expr.arg_cnt_ < 2 || expr.arg_cnt_ > 3)) {
    ret = OB_ERR_UNEXPECTED;
  } else if (OB_FAIL(expr.args_[AI_EMBED_MODEL_IDX]->eval_batch(ctx, skip, size))) {
  } else if (OB_FAIL(expr.args_[AI_EMBED_CONTENT_IDX]->eval_batch(ctx, skip, size))) {
  } else if (expr.arg_cnt_ == 3 &&
             OB_FAIL(expr.args_[AI_EMBED_DIM_IDX]->eval_batch(ctx, skip, size))) {
  } else {
    const bool has_dim = expr.arg_cnt_ == 3;
    bool has_null_result = false;
    ObDatum *res_datums = expr.locate_batch_datums(ctx);
    ObBitVector &eval_flags = expr.get_evaluated_flags(ctx);
    ObDatumVector model_datums = expr.args_[AI_EMBED_MODEL_IDX]->locate_expr_datumvector(ctx);
    ObDatumVector content_datums = expr.args_[AI_EMBED_CONTENT_IDX]->locate_expr_datumvector(ctx);
    ObDatumVector dim_datums;
    if (has_dim) {
      dim_datums = expr.args_[AI_EMBED_DIM_IDX]->locate_expr_datumvector(ctx);
    }

    ObEvalCtx::TempAllocGuard tmp_alloc_g(ctx);
    MultimodeAlloctor temp_allocator(tmp_alloc_g.get_allocator());
    lib::ObMallocHookAttrGuard malloc_guard(lib::ObMemAttr(N_AI_EMBED));

    ObString cur_model_id;
    int64_t cur_dim = 0;
    ObArray<ObString> cur_contents;
    ObArray<int64_t> cur_row_idxs;
    bool cur_initialized = false;

    for (int64_t i = 0; OB_SUCC(ret) && i < size; ++i) {
      if (skip.at(i) || eval_flags.at(i)) {
        continue;
      }
      ObDatum *model_datum = model_datums.at(i);
      ObDatum *content_datum = content_datums.at(i);
      if (OB_ISNULL(model_datum) || OB_ISNULL(content_datum)) {
        ret = OB_ERR_UNEXPECTED;
      } else if (model_datum->is_null() || content_datum->is_null()) {
        // NULL input is propagated to the output row and never enters an HTTP
        // request. This keeps result positions aligned with the scanned rows.
        has_null_result = true;
        res_datums[i].set_null();
        eval_flags.set(i);
      } else {
        ObString model_id;
        ObString content;
        if (OB_FAIL(ObTextStringHelper::read_real_string_data(
                ctx.exec_ctx_, temp_allocator, *model_datum,
                expr.args_[AI_EMBED_MODEL_IDX]->datum_meta_,
                expr.args_[AI_EMBED_MODEL_IDX]->obj_meta_.has_lob_header(), model_id))) {
        } else if (model_id.empty()) {
          ret = OB_INVALID_ARGUMENT;
          LOG_USER_ERROR(OB_INVALID_ARGUMENT, "ai_embed, model id is empty");
        } else if (OB_FAIL(ObTextStringHelper::read_real_string_data(
                       ctx.exec_ctx_, temp_allocator, *content_datum,
                       expr.args_[AI_EMBED_CONTENT_IDX]->datum_meta_,
                       expr.args_[AI_EMBED_CONTENT_IDX]->obj_meta_.has_lob_header(), content))) {
        } else if (content.empty()) {
          ret = OB_INVALID_ARGUMENT;
          LOG_USER_ERROR(OB_INVALID_ARGUMENT, "ai_embed, input is empty");
        }

        int64_t dim = 0;
        if (OB_SUCC(ret) && has_dim) {
          ObDatum *dim_datum = dim_datums.at(i);
          if (OB_ISNULL(dim_datum)) {
            ret = OB_ERR_UNEXPECTED;
          } else if (dim_datum->is_null()) {
            ret = OB_INVALID_ARGUMENT;
            LOG_USER_ERROR(OB_INVALID_ARGUMENT,
                           "ai_embed, dimension parameter must be a positive integer");
          } else {
            dim = dim_datum->get_int();
            if (dim <= 0) {
              ret = OB_INVALID_ARGUMENT;
              LOG_USER_ERROR(OB_INVALID_ARGUMENT,
                             "ai_embed, dimension parameter must be a positive integer");
            }
          }
        }

        if (OB_SUCC(ret)) {
          if (cur_initialized &&
              (model_id != cur_model_id || dim != cur_dim ||
               cur_contents.count() >= AI_EMBED_MAX_BATCH_SIZE)) {
            if (OB_FAIL(flush_embed_group(expr, ctx, temp_allocator, res_datums, eval_flags,
                                          cur_model_id, cur_dim, cur_contents, cur_row_idxs))) {
            } else {
              cur_contents.reuse();
              cur_row_idxs.reuse();
              cur_model_id = ObString();
              cur_dim = 0;
              cur_initialized = false;
            }
          }
          if (OB_SUCC(ret)) {
            if (!cur_initialized) {
              cur_model_id = model_id;
              cur_dim = dim;
              cur_initialized = true;
            }
            if (OB_FAIL(cur_contents.push_back(content))) {
            } else if (OB_FAIL(cur_row_idxs.push_back(i))) {
            }
          }
        }
      }
    }

    if (OB_FAIL(ret)) {
    } else if (cur_initialized &&
               OB_FAIL(flush_embed_group(expr, ctx, temp_allocator, res_datums, eval_flags,
                                         cur_model_id, cur_dim, cur_contents, cur_row_idxs))) {
    }
    if (OB_SUCC(ret) && has_null_result) {
      expr.get_eval_info(ctx).notnull_ = false;
    }
  }
  return ret;
}

} // namespace

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
    rt_expr.eval_batch_func_ = eval_ai_embed_batch;
  }
  return ret;
}

} // namespace sql
} // namespace oceanbase
