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
#include "query/engine/expr/ob_expr_lob_utils.h"
#include "share/rc/ob_server_runtime.h"

using namespace oceanbase::common;
using namespace oceanbase::sql;

namespace oceanbase
{
namespace sql
{

namespace
{
// compare two strings by content, the model id and the dimension must be the
// same for all the rows of a batch, otherwise the rows can not be merged into
// one request
bool str_equal(const ObString &lhs, const ObString &rhs)
{
  return lhs.length() == rhs.length()
         && (lhs.length() == 0 || 0 == memcmp(lhs.ptr(), rhs.ptr(), lhs.length()));
}

// The maximum number of the inputs carried by one request. The model may
// refuse a request carrying too many inputs, so the inputs of a batch are
// split into several requests when the batch is larger than this.
constexpr int64_t MAX_EMBED_REQUEST_SIZE = 64;
// The maximum depth of the split and the retry. A failing request is split
// into two half requests and both of them are sent again, until this depth is
// reached.
constexpr int64_t MAX_EMBED_SPLIT_DEPTH = 3;

// Send the inputs of one request to the model and collect the results of
// them. When the request fails and the inputs can be split, the inputs are
// split into two halves and the halves are sent separately, so that one
// oversized request does not fail the whole batch.
int call_embedding(ObAIFuncModel &model,
                   ObArray<ObString> &contents,
                   ObJsonObject *config,
                   const int64_t start,
                   const int64_t count,
                   const int64_t depth,
                   ObArray<ObString> &results)
{
  int ret = OB_SUCCESS;
  int request_ret = OB_SUCCESS;
  ObArray<ObString> request_contents;
  ObArray<ObString> request_results;
  for (int64_t i = 0; OB_SUCC(ret) && i < count; i++) {
    if (OB_FAIL(request_contents.push_back(contents.at(start + i)))) {
    }
  }
  if (OB_SUCC(ret)) {
    request_ret = model.call_dense_embedding_vector_v2(request_contents, config, request_results);
    if (OB_SUCC(request_ret) && request_results.count() != count) {
      request_ret = OB_ERR_UNEXPECTED;
    }
  }
  if (OB_SUCC(request_ret)) {
    for (int64_t i = 0; OB_SUCC(ret) && i < request_results.count(); i++) {
      if (OB_FAIL(results.push_back(request_results.at(i)))) {
      }
    }
  } else if (OB_SUCC(ret) && count > 1 && depth < MAX_EMBED_SPLIT_DEPTH) {
    // the request fails, the inputs are split into two halves and sent again
    LOG_WARN("failed to send the embedding request, retry it with the split inputs",
             K(request_ret), K(count), K(depth));
    const int64_t half = count / 2;
    if (OB_FAIL(call_embedding(model, contents, config, start, half, depth + 1, results))) {
    } else if (OB_FAIL(call_embedding(model, contents, config,
                                      start + half, count - half, depth + 1, results))) {
    }
  } else if (OB_SUCC(ret)) {
    ret = request_ret;
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
    if (ob_is_text_tc(types_stack[CONTENT_IDX].get_type())) {
      // the content may be a lob, keep its own type so that the real data can
      // be read at runtime
    } else {
      types_stack[CONTENT_IDX].set_calc_type(ObVarcharType);
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
  } else if (arg_model_id->is_null()) {
    ret = OB_INVALID_ARGUMENT;
    LOG_USER_ERROR(OB_INVALID_ARGUMENT, "ai_embed, model id is null");
    res.set_null();
  } else if (arg_content->is_null()) {
    // a null content gives a null result, such a row is not sent to the model
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
    if (OB_FAIL(ObTextStringHelper::get_string(ctx.exec_ctx_, expr, temp_allocator,
                                                CONTENT_IDX, arg_content, content))) {
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

// the rows of a batch are merged into one request, the null rows are neither
// sent to the model nor do they shift the results of the other rows
int ObExprAIEmbed::eval_ai_embed_batch(const ObExpr &expr,
                                        ObEvalCtx &ctx,
                                        const common::ObBitVector &skip,
                                        const int64_t batch_size)
{
  INIT_SUCC(ret);
  ObDatumVector model_datum;
  ObDatumVector content_datum;
  ObDatumVector dim_datum;
  ObEvalCtx::TempAllocGuard tmp_alloc_g(ctx);
  MultimodeAlloctor temp_allocator(tmp_alloc_g.get_allocator());
  lib::ObMallocHookAttrGuard malloc_guard(lib::ObMemAttr(N_AI_EMBED));
  ObDatum *res_datum = expr.locate_batch_datums(ctx);
  ObBitVector &eval_flags = expr.get_evaluated_flags(ctx);
  ObArray<int64_t> row_idxs;
  ObArray<ObString> contents;
  ObString model_id;
  int64_t dim = 0;
  bool same_config = true;

  if (OB_FAIL(expr.eval_batch_param_value(ctx, skip, batch_size,
                                          model_datum, content_datum, dim_datum))) {
  } else if (OB_ISNULL(model_datum.datums_) || model_datum.at(0)->is_null()) {
    ret = OB_INVALID_ARGUMENT;
    LOG_USER_ERROR(OB_INVALID_ARGUMENT, "ai_embed, model id is null");
  } else {
    model_id = model_datum.at(0)->get_string();
    if (model_id.empty()) {
      ret = OB_INVALID_ARGUMENT;
      LOG_USER_ERROR(OB_INVALID_ARGUMENT, "ai_embed, model id or input is empty");
    } else if (expr.arg_cnt_ == 3 && !dim_datum.at(0)->is_null()) {
      dim = dim_datum.at(0)->get_int();
      if (dim <= 0) {
        ret = OB_INVALID_ARGUMENT;
        LOG_USER_ERROR(OB_INVALID_ARGUMENT, "ai_embed, dimension parameter must be a positive integer");
      }
    }
    for (int64_t i = 0; OB_SUCC(ret) && i < batch_size; i++) {
      if (skip.at(i) || eval_flags.at(i)) {
        continue;
      }
      ObDatum *content_ptr = content_datum.at(i);
      if (content_ptr->is_null()) {
        // a null row is not sent to the model and its result stays null
        res_datum[i].set_null();
        eval_flags.set(i);
        continue;
      }
      ObString content;
      if (OB_FAIL(ObTextStringHelper::get_string(ctx.exec_ctx_, expr, temp_allocator,
                                                  CONTENT_IDX, content_ptr, content))) {
      } else if (content.empty()) {
        ret = OB_INVALID_ARGUMENT;
        LOG_USER_ERROR(OB_INVALID_ARGUMENT, "ai_embed, model id or input is empty");
      } else if (!str_equal(model_id, model_datum.at(i)->get_string())) {
        // the model id differs from row to row, the rows can not be merged
        same_config = false;
        break;
      } else if (expr.arg_cnt_ == 3) {
        int64_t row_dim = dim_datum.at(i)->is_null() ? 0 : dim_datum.at(i)->get_int();
        if (row_dim != dim) {
          same_config = false;
          break;
        }
      }
      if (OB_SUCC(ret) && OB_FAIL(row_idxs.push_back(i))) {
      } else if (OB_SUCC(ret) && OB_FAIL(contents.push_back(content))) {
      }
    }
  }

  if (OB_FAIL(ret)) {
  } else if (!same_config) {
    // the model id or the dimension is not the same for all the rows, the rows
    // are evaluated one by one
    ObEvalCtx::BatchInfoScopeGuard batch_info_guard(ctx);
    batch_info_guard.set_batch_size(batch_size);
    for (int64_t i = 0; OB_SUCC(ret) && i < batch_size; i++) {
      if (skip.at(i) || eval_flags.at(i)) {
        continue;
      }
      batch_info_guard.set_batch_idx(i);
      if (OB_FAIL(eval_ai_embed(expr, ctx, res_datum[i]))) {
      } else {
        eval_flags.set(i);
      }
    }
  } else if (contents.count() > 0) {
    ObAIFuncExprInfo *info = nullptr;
    share::ObAiModelEndpointInfo resolved_endpoint;
    const share::ObAiModelEndpointInfo *endpoint_info = &resolved_endpoint;
    query::ObIAiEndpointResolver *endpoint_resolver =
        ::oceanbase::share::server_service<::oceanbase::query::ObIAiEndpointResolver>();
    ObJsonInt *dim_json = nullptr;
    ObJsonObject *config = nullptr;
    ObArray<ObString> results;
    if (dim > 0) {
      if (OB_FAIL(ObAIFuncJsonUtils::get_json_object(temp_allocator, config))) {
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
      // the inputs of the batch are sent in several requests when the batch is
      // larger than the maximum number of the inputs of one request
      for (int64_t start = 0; OB_SUCC(ret) && start < contents.count();
           start += MAX_EMBED_REQUEST_SIZE) {
        int64_t count = contents.count() - start;
        if (count > MAX_EMBED_REQUEST_SIZE) {
          count = MAX_EMBED_REQUEST_SIZE;
        }
        if (OB_FAIL(call_embedding(model, contents, config, start, count, 0, results))) {
          break;
        }
      }
      if (OB_FAIL(ret)) {
      } else if (results.count() != row_idxs.count()) {
        ret = OB_ERR_UNEXPECTED;
        LOG_USER_ERROR(OB_ERR_UNEXPECTED, "ai_embed, the number of the results does not match the number of the inputs");
      } else {
        for (int64_t i = 0; OB_SUCC(ret) && i < results.count(); i++) {
          const int64_t row_idx = row_idxs.at(i);
          ObTextStringDatumResult text_result(expr.datum_meta_.type_, &expr, &ctx,
                                              res_datum + row_idx);
          if (OB_FAIL(text_result.init_with_batch_idx(results.at(i).length(), row_idx))) {
          } else if (OB_FAIL(text_result.append(results.at(i)))) {
          } else {
            text_result.set_result();
            eval_flags.set(row_idx);
          }
        }
      }
    }
  }
  if (OB_SUCC(ret) && NULL != res_datum) {
    // the result of some rows of the batch is null, the expression is not
    // always not null
    for (int64_t i = 0; i < batch_size; i++) {
      if (eval_flags.at(i) && res_datum[i].is_null()) {
        expr.get_eval_info(ctx).notnull_ = false;
        break;
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
  //   } else {
  //     ObAIFuncExprInfo *info = OB_NEWx(ObAIFuncExprInfo, allocator, *allocator, T_FUN_SYS_AI_COMPLETE);
  //     if (OB_ISNULL(info)) {
  //       ret = OB_ALLOCATE_MEMORY_FAILED;
  //       LOG_WARN("failed to allocate memory for ai func info", K(ret));
  //     } else if (OB_FAIL(ObAIFuncUtils::get_ai_func_info(*allocator, const_data.get_string(), *expr_cg_ctx.schema_guard_, info))) {
  //       LOG_WARN("failed to get ai func info", K(ret), K(const_data.get_string()));
  //     } else {
  //       rt_expr.extra_info_ = info;
  //     }
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
