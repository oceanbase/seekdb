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
#include "common/json_type/ob_json_common.h"
#include "sql/engine/expr/ob_expr_json_func_helper.h"
#include "sql/engine/expr/ob_expr_lob_utils.h"

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
                        VALID_FOR_GENERATED_COL, 
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
    } else if (ob_is_text_tc(types_stack[CONTENT_IDX].get_type())) {
      // TEXT/LONGTEXT input keeps its type so the whole lob can be read at eval.
    } else {
      ret = OB_ERR_INVALID_TYPE_FOR_OP;
    }
    if (param_num == 3 && OB_SUCC(ret)) {
      if (ob_is_integer_type(types_stack[DIM_IDX].get_type())) {
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

static bool ai_embed_same_string(const ObString &lhs, const ObString &rhs)
{
  return lhs.length() == rhs.length()
         && (0 == lhs.length() || 0 == MEMCMP(lhs.ptr(), rhs.ptr(), lhs.length()));
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
    if (OB_FAIL(ObTextStringHelper::read_real_string_data(ctx.exec_ctx_,
                                                          temp_allocator,
                                                          *arg_content,
                                                          expr.args_[CONTENT_IDX]->datum_meta_,
                                                          expr.args_[CONTENT_IDX]->obj_meta_.has_lob_header(),
                                                          content))) {
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
      } else if (OB_FAIL(ObJsonExprHelper::pack_json_str_res(expr, ctx, res, result))) {
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
  query::ObIAiEndpointResolver *endpoint_resolver =
      ::oceanbase::share::server_service<::oceanbase::query::ObIAiEndpointResolver>();

  if (OB_UNLIKELY(expr.arg_cnt_ != 2 && expr.arg_cnt_ != 3)) {
    ret = OB_ERR_UNEXPECTED;
    LOG_WARN("unexpected argument count", K(ret), K(expr.arg_cnt_));
  } else if (OB_ISNULL(res_datum.datums_) || OB_ISNULL(endpoint_resolver)) {
    ret = OB_ERR_UNEXPECTED;
    LOG_WARN("unexpected null", K(ret), KP(res_datum.datums_), KP(endpoint_resolver));
  }

  for (int64_t i = 0; OB_SUCC(ret) && i < expr.arg_cnt_; ++i) {
    if (OB_FAIL(expr.args_[i]->eval_batch(ctx, skip, batch_size))) {
      LOG_WARN("failed to eval batch argument", K(ret), K(i));
    }
  }
  if (OB_FAIL(ret)) {
  } else {
    ObDatumVector model_vec = expr.args_[MODEL_IDX]->locate_expr_datumvector(ctx);
    ObDatumVector content_vec = expr.args_[CONTENT_IDX]->locate_expr_datumvector(ctx);
    ObDatumVector dim_vec;
    if (expr.arg_cnt_ == 3) {
      dim_vec = expr.args_[DIM_IDX]->locate_expr_datumvector(ctx);
    }
    // Rows evaluated in this batch, keyed by their position inside the batch. All
    // of them are sent to the model in a single request per (model, dimension)
    // group so that a table scan does not issue one request per row.
    ObSEArray<int64_t, 32> row_idx;
    ObSEArray<ObString, 32> row_model;
    ObSEArray<ObString, 32> row_content;
    ObSEArray<int64_t, 32> row_dim;
    ObSEArray<char, 32> row_done;

    for (int64_t i = 0; OB_SUCC(ret) && i < batch_size; ++i) {
      if (skip.at(i) || eval_flags.at(i)) {
        continue;
      }
      eval_flags.set(i);
      ObDatum *model_datum = model_vec.at(i);
      ObDatum *content_datum = content_vec.at(i);
      ObDatum *dim_datum = expr.arg_cnt_ == 3 ? dim_vec.at(i) : nullptr;
      if (OB_ISNULL(model_datum) || OB_ISNULL(content_datum)) {
        ret = OB_ERR_UNEXPECTED;
        LOG_WARN("argument datum is null", K(ret), K(i));
      } else if (model_datum->is_null() || content_datum->is_null()) {
        // NULL input propagates to a NULL result without poisoning the batch.
        res_datum.at(i)->set_null();
      } else {
        ObString model_id = model_datum->get_string();
        ObString content;
        int64_t dim = 0;
        if (OB_FAIL(ObTextStringHelper::read_real_string_data(
                ctx.exec_ctx_,
                temp_allocator,
                *content_datum,
                expr.args_[CONTENT_IDX]->datum_meta_,
                expr.args_[CONTENT_IDX]->obj_meta_.has_lob_header(),
                content))) {
        } else if (OB_NOT_NULL(dim_datum) && !dim_datum->is_null()) {
          dim = dim_datum->get_int();
          if (dim <= 0) {
            ret = OB_INVALID_ARGUMENT;
            LOG_USER_ERROR(OB_INVALID_ARGUMENT,
                           "ai_embed, dimension parameter must be a positive integer");
            res_datum.at(i)->set_null();
          }
        }
        if (OB_FAIL(ret)) {
        } else if (model_id.empty() || content.empty()) {
          ret = OB_INVALID_ARGUMENT;
          LOG_USER_ERROR(OB_INVALID_ARGUMENT, "ai_embed, model id or input is empty");
          res_datum.at(i)->set_null();
        } else if (OB_FAIL(row_idx.push_back(i))
                   || OB_FAIL(row_model.push_back(model_id))
                   || OB_FAIL(row_content.push_back(content))
                   || OB_FAIL(row_dim.push_back(dim))
                   || OB_FAIL(row_done.push_back(0))) {
          LOG_WARN("failed to record embedding input", K(ret));
        }
      }
    }

    int64_t processed = 0;
    while (OB_SUCC(ret) && processed < row_idx.count()) {
      int64_t first = -1;
      for (int64_t i = 0; i < row_idx.count() && first < 0; ++i) {
        if (0 == row_done[i]) {
          first = i;
        }
      }
      if (OB_UNLIKELY(first < 0)) {
        ret = OB_ERR_UNEXPECTED;
        LOG_WARN("failed to find pending embedding input", K(ret));
        break;
      }
      ObSEArray<int64_t, 16> group_rows;
      ObArray<ObString> group_contents;
      for (int64_t i = first; OB_SUCC(ret) && i < row_idx.count(); ++i) {
        if (0 == row_done[i] && row_dim[i] == row_dim[first]
            && ai_embed_same_string(row_model[i], row_model[first])) {
          row_done[i] = 1;
          ++processed;
          if (OB_FAIL(group_rows.push_back(row_idx[i]))
              || OB_FAIL(group_contents.push_back(row_content[i]))) {
            LOG_WARN("failed to build embedding group", K(ret));
          }
        }
      }
      if (OB_FAIL(ret)) {
        break;
      }
      ObJsonObject *config = nullptr;
      ObJsonInt *dim_json = nullptr;
      ObAIFuncExprInfo *info = nullptr;
      share::ObAiModelEndpointInfo resolved_endpoint;
      if (row_dim[first] > 0) {
        if (OB_FAIL(ObAIFuncJsonUtils::get_json_object(temp_allocator, config))
            || OB_FAIL(ObAIFuncJsonUtils::get_json_int(temp_allocator, row_dim[first], dim_json))
            || OB_FAIL(config->add("dimensions", dim_json))) {
          LOG_WARN("failed to build embedding config", K(ret), K(row_dim[first]));
        }
      }
      if (OB_FAIL(ret)) {
      } else if (OB_FAIL(ObAIFuncUtils::get_ai_func_info(
                     temp_allocator, row_model[first], info))) {
      } else if (OB_FAIL(endpoint_resolver->resolve_by_model_name(
                     row_model[first], temp_allocator, resolved_endpoint))) {
      } else {
        ObAIFuncModel model(temp_allocator, *info, resolved_endpoint);
        ObArray<ObString> group_results;
        int tmp_ret = model.call_dense_embedding_vector_v2(group_contents, config, group_results);
        if (OB_SUCCESS == tmp_ret && group_results.count() == group_contents.count()) {
          for (int64_t k = 0; OB_SUCC(ret) && k < group_results.count(); ++k) {
            if (OB_FAIL(ObJsonExprHelper::pack_json_str_res(
                    expr, ctx, *res_datum.at(group_rows[k]), group_results[k]))) {
              LOG_WARN("failed to pack embedding result", K(ret), K(k));
            }
          }
        } else {
          // Keep the failure local to the offending input: retry the group one
          // input at a time so that a single bad row does not fail every row in
          // the batch, and each row keeps its own result mapping.
          LOG_WARN("batch embedding request failed, fallback to single input",
                   K(tmp_ret), K(group_contents.count()));
          for (int64_t k = 0; OB_SUCC(ret) && k < group_contents.count(); ++k) {
            ObString single_result;
            if (OB_FAIL(model.call_dense_embedding(group_contents[k], config, single_result))) {
              LOG_WARN("failed to embed single input", K(ret), K(k));
            } else if (OB_FAIL(ObJsonExprHelper::pack_json_str_res(
                           expr, ctx, *res_datum.at(group_rows[k]), single_result))) {
              LOG_WARN("failed to pack embedding result", K(ret), K(k));
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
