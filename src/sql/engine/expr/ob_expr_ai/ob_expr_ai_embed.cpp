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
#include "lib/utility/utility.h"

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
    if (!ob_is_text_tc(types_stack[CONTENT_IDX].get_type())) {
      types_stack[CONTENT_IDX].set_calc_type(ObVarcharType);
    }
    // A text/longtext content keeps its own type: its content is read from the
    // lob when the function is evaluated.
    types_stack[CONTENT_IDX].set_calc_collation_type(CS_TYPE_UTF8MB4_BIN);
    if (param_num == 3) {
      if (ob_is_integer_type(types_stack[DIM_IDX].get_type())
          || ObNullType == types_stack[DIM_IDX].get_type()) {
        types_stack[DIM_IDX].set_calc_type(ObIntType);
        types_stack[DIM_IDX].set_precision(10);
        types_stack[DIM_IDX].set_scale(0);
      } else {
        ret = OB_INVALID_ARGUMENT;
        LOG_USER_ERROR(OB_INVALID_ARGUMENT, "ai_embed, dimension parameter must be an integer, not a decimal or float");
      }
    }
    // The result is the json text of the embedding vector, it may be longer
    // than a varchar, so the long text type is used.
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
  } else if (arg_model_id->is_null()) {
    ret = OB_INVALID_ARGUMENT;
    LOG_USER_ERROR(OB_INVALID_ARGUMENT, "ai_embed, model id is null");
    res.set_null();
  } else if (arg_content->is_null()) {
    // The null of the input is propagated to the result, no request is issued.
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
    if (OB_FAIL(ObTextStringHelper::read_real_string_data(ctx.exec_ctx_, temp_allocator,
                *arg_content, expr.args_[CONTENT_IDX]->datum_meta_,
                expr.args_[CONTENT_IDX]->obj_meta_.has_lob_header(), content))) {
      LOG_WARN("failed to read the ai_embed content", K(ret));
    } else if (model_id.empty() || content.empty()) {
      ret = OB_INVALID_ARGUMENT;
      LOG_USER_ERROR(OB_INVALID_ARGUMENT, "ai_embed, model id or input is empty");
      res.set_null();
    }
    int64_t dim = 0;
    ObJsonInt *dim_json = nullptr;
    ObJsonObject *config = nullptr;
    if (OB_FAIL(ret)) {
    } else if (OB_NOT_NULL(arg_dim) && !arg_dim->is_null()) {
      // An omitted or SQL null dimension keeps the model default dimension.
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

/**
 * @brief Send one group of the contents to the model in one request and write
 *        the answers back to the result datum of the rows. The answers keep
 *        the order of the contents, so the answer of every row is written to
 *        the datum of that row.
 */
static int eval_ai_embed_group(const ObExpr &expr,
                               ObEvalCtx &ctx,
                               ObEvalCtx::BatchInfoScopeGuard &batch_guard,
                               common::ObIAllocator &allocator,
                               common::ObBitVector &eval_flags,
                               common::ObDatumVector &res_datums,
                               const common::ObString &model_id,
                               const int64_t dimension,
                               common::ObArray<common::ObString> &contents,
                               common::ObArray<int64_t> &rows)
{
  INIT_SUCC(ret);
  ObAIFuncExprInfo *info = nullptr;
  share::ObAiModelEndpointInfo resolved_endpoint;
  const share::ObAiModelEndpointInfo *endpoint_info = &resolved_endpoint;
  query::ObIAiEndpointResolver *endpoint_resolver =
      ::oceanbase::share::server_service<::oceanbase::query::ObIAiEndpointResolver>();
  ObJsonInt *dim_json = nullptr;
  ObJsonObject *config = nullptr;
  if (OB_UNLIKELY(contents.count() != rows.count() || contents.empty())) {
    ret = OB_ERR_UNEXPECTED;
    LOG_WARN("invalid embed group", K(ret), K(contents.count()), K(rows.count()));
  } else if (dimension > 0) {
    // The dimension is sent to the model, the dimension of the answer is
    // checked when the answer is parsed.
    if (OB_FAIL(ObAIFuncJsonUtils::get_json_object(allocator, config))) {
    } else if (OB_FAIL(ObAIFuncJsonUtils::get_json_int(allocator, dimension, dim_json))) {
    } else if (OB_FAIL(config->add("dimensions", dim_json))) {
    }
  }
  if (OB_FAIL(ret)) {
  } else if (OB_FAIL(ObAIFuncUtils::get_ai_func_info(allocator, model_id, info))) {
  } else if (OB_ISNULL(endpoint_resolver)) {
    ret = OB_ERR_UNEXPECTED;
  } else if (OB_FAIL(endpoint_resolver->resolve_by_model_name(
                 model_id, allocator, resolved_endpoint))) {
  } else {
    ObAIFuncModel model(allocator, *info, *endpoint_info);
    ObArray<ObString> results;
    if (OB_FAIL(model.call_dense_embedding_vector_v2(contents, config, results))) {
    } else if (OB_UNLIKELY(results.count() != contents.count())) {
      // the model must answer with one embedding for every input
      ret = OB_INVALID_DATA;
      LOG_WARN("the number of the embedding answers is not equal to the number of the inputs",
               K(ret), K(results.count()), K(contents.count()));
    } else {
      for (int64_t i = 0; OB_SUCC(ret) && i < results.count(); ++i) {
        const int64_t row_idx = rows.at(i);
        // The result of a text type is a temporary lob: set_string_result
        // builds the datum of the current row, the batch index selects it.
        batch_guard.set_batch_idx(row_idx);
        if (OB_FAIL(ObAIFuncUtils::set_string_result(
                      expr, ctx, *res_datums.at(row_idx), results.at(i)))) {
          LOG_WARN("failed to set the embedding result of the row", K(ret), K(row_idx));
        } else {
          eval_flags.set(row_idx);
        }
      }
    }
  }
  return ret;
}

int ObExprAIEmbed::eval_ai_embed_batch(const ObExpr &expr,
                                       ObEvalCtx &ctx,
                                       const common::ObBitVector &skip,
                                       const int64_t size)
{
  INIT_SUCC(ret);
  if (size <= 0) {
    return ret;
  }
  ObEvalCtx::BatchInfoScopeGuard batch_guard(ctx);
  batch_guard.set_batch_size(size);
  ObBitVector &eval_flags = expr.get_evaluated_flags(ctx);
  ObDatumVector res_datums = expr.locate_expr_datumvector(ctx);
  ObDatumVector arg_datums[3];
  for (int64_t i = 0; OB_SUCC(ret) && i < expr.arg_cnt_; ++i) {
    if (OB_FAIL(expr.args_[i]->eval_batch(ctx, skip, size))) {
      LOG_WARN("failed to evaluate the ai_embed argument", K(ret), K(i));
    } else {
      arg_datums[i] = expr.args_[i]->locate_expr_datumvector(ctx);
    }
  }

  if (OB_FAIL(ret)) {
  } else {
    // The content strings of the rows that wait for the request are kept in
    // this temporary allocator until the answers are written back.
    ObEvalCtx::TempAllocGuard tmp_alloc_g(ctx);
    MultimodeAlloctor temp_allocator(tmp_alloc_g.get_allocator());
    lib::ObMallocHookAttrGuard malloc_guard(lib::ObMemAttr(N_AI_EMBED));

    // 1. collect the rows that must be sent to the model. The rows that are
    //    filtered out, already evaluated or null are written back directly and
    //    they do not enter a request nor shift the answers of the other rows.
    ObArray<int64_t> row_indexes;
    ObArray<int64_t> row_dimensions;
    ObArray<ObString> row_models;
    ObArray<ObString> row_contents;
    for (int64_t i = 0; OB_SUCC(ret) && i < size; ++i) {
      if (skip.at(i) || eval_flags.at(i)) {
        continue;
      }
      const ObDatum *model_datum = arg_datums[MODEL_IDX].at(i);
      const ObDatum *content_datum = arg_datums[CONTENT_IDX].at(i);
      if (OB_ISNULL(model_datum) || OB_ISNULL(content_datum)) {
        ret = OB_ERR_UNEXPECTED;
        LOG_WARN("the ai_embed argument datum is null", K(ret), K(i));
      } else if (model_datum->is_null()) {
        ret = OB_INVALID_ARGUMENT;
        LOG_USER_ERROR(OB_INVALID_ARGUMENT, "ai_embed, model id is null");
      } else if (content_datum->is_null()) {
        res_datums.at(i)->set_null();
        expr.get_eval_info(ctx).notnull_ = false;
        eval_flags.set(i);
      } else {
        ObString model_id;
        ObString content;
        int64_t dim = 0;
        if (OB_FAIL(ObTextStringHelper::read_real_string_data(ctx.exec_ctx_, temp_allocator,
                    *model_datum, expr.args_[MODEL_IDX]->datum_meta_,
                    expr.args_[MODEL_IDX]->obj_meta_.has_lob_header(), model_id))) {
          LOG_WARN("failed to read the ai_embed model id", K(ret), K(i));
        } else if (OB_FAIL(ObTextStringHelper::read_real_string_data(ctx.exec_ctx_, temp_allocator,
                    *content_datum, expr.args_[CONTENT_IDX]->datum_meta_,
                    expr.args_[CONTENT_IDX]->obj_meta_.has_lob_header(), content))) {
          LOG_WARN("failed to read the ai_embed content", K(ret), K(i));
        } else if (model_id.empty() || content.empty()) {
          ret = OB_INVALID_ARGUMENT;
          LOG_USER_ERROR(OB_INVALID_ARGUMENT, "ai_embed, model id or input is empty");
        } else if (expr.arg_cnt_ > DIM_IDX) {
          // A null dimension is an invalid argument, like a non positive one.
          const ObDatum *dim_datum = arg_datums[DIM_IDX].at(i);
          if (OB_ISNULL(dim_datum)) {
            ret = OB_ERR_UNEXPECTED;
            LOG_WARN("the ai_embed dimension datum is null", K(ret), K(i));
          } else if (dim_datum->is_null()) {
            // The model default dimension is used.
            dim = 0;
          } else if (dim_datum->get_int() <= 0) {
            ret = OB_INVALID_ARGUMENT;
            LOG_USER_ERROR(OB_INVALID_ARGUMENT,
                           "ai_embed, dimension parameter must be a positive integer");
          } else {
            dim = dim_datum->get_int();
          }
        }
        if (OB_FAIL(ret)) {
        } else if (OB_FAIL(row_indexes.push_back(i))) {
        } else if (OB_FAIL(row_dimensions.push_back(dim))) {
        } else if (OB_FAIL(row_models.push_back(model_id))) {
        } else if (OB_FAIL(row_contents.push_back(content))) {
        }
      }
    }

    // 2. send the rows to the model. One request carries at most
    //    MAX_BATCH_CONTENT_CNT inputs that use the same model and the same
    //    dimension. The rows of one request keep the scan order, so the
    //    answers are written back to the rows they belong to.
    ObArray<int8_t> consumed;
    for (int64_t i = 0; OB_SUCC(ret) && i < row_indexes.count(); ++i) {
      if (OB_FAIL(consumed.push_back(0))) {
      }
    }
    for (int64_t first = 0; OB_SUCC(ret) && first < row_indexes.count(); ++first) {
      if (0 != consumed.at(first)) {
        continue;
      }
      ObArray<int64_t> group_rows;
      ObArray<ObString> group_contents;
      for (int64_t i = first;
           OB_SUCC(ret) && i < row_indexes.count()
           && group_contents.count() < MAX_BATCH_CONTENT_CNT; ++i) {
        if (0 != consumed.at(i)
            || row_dimensions.at(i) != row_dimensions.at(first)
            || 0 != row_models.at(i).compare(row_models.at(first))) {
          continue;
        } else if (OB_FAIL(group_rows.push_back(row_indexes.at(i)))) {
        } else if (OB_FAIL(group_contents.push_back(row_contents.at(i)))) {
        } else {
          consumed.at(i) = 1;
        }
      }
      if (OB_FAIL(ret)) {
      } else if (OB_FAIL(eval_ai_embed_group(expr, ctx, batch_guard, temp_allocator, eval_flags,
                                             res_datums, row_models.at(first),
                                             row_dimensions.at(first), group_contents,
                                             group_rows))) {
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
