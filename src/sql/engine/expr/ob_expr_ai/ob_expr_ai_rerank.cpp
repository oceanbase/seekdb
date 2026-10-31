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
#include "ob_expr_ai_rerank.h"
#include "query/ai/ob_ai_endpoint_resolver.h"
#include "share/rc/ob_server_runtime.h"
#include "lib/utility/utility.h"
#include "common/json_type/ob_json_common.h"
#include "lib/container/ob_array_iterator.h"
#include <algorithm>
#include <cmath>
 
using namespace oceanbase::common;
using namespace oceanbase::sql;

namespace oceanbase 
{
namespace sql
{
namespace
{
struct RankedRerankItem
{
  RankedRerankItem() : index_(0), score_(0), score_node_(nullptr), response_(nullptr) {}
  RankedRerankItem(int64_t index, double score, ObJsonNode *score_node, ObJsonObject *response)
    : index_(index), score_(score), score_node_(score_node), response_(response)
  {}
  int64_t index_;
  double score_;
  ObJsonNode *score_node_;
  ObJsonObject *response_;
};

// The provider returns indices local to the requested batch, not global document ids.
// Validate the full permutation before rewriting any indices or publishing results.
int append_validated_rerank_batch(ObJsonArray *batch, int64_t base_index,
                                  int64_t requested_count,
                                  ObArray<RankedRerankItem> &ranked)
{
  int ret = OB_SUCCESS;
  const int64_t MAX_BATCH_SIZE = 20;
  bool seen[MAX_BATCH_SIZE] = {false};
  if (OB_ISNULL(batch) || requested_count <= 0 || requested_count > MAX_BATCH_SIZE ||
      batch->element_count() != requested_count) {
    ret = OB_INVALID_DATA;
  } else {
    for (int64_t i = 0; OB_SUCC(ret) && i < requested_count; ++i) {
      ObJsonNode *item_node = batch->get_value(i);
      if (OB_ISNULL(item_node) || item_node->json_type() != ObJsonNodeType::J_OBJECT) {
        ret = OB_INVALID_DATA;
      } else {
        ObJsonObject *item = static_cast<ObJsonObject *>(item_node);
        ObJsonNode *index_node = item->get_value("index");
        ObJsonNode *score_node = item->get_value("relevance_score");
        if (OB_ISNULL(index_node) || index_node->json_type() != ObJsonNodeType::J_INT ||
            OB_ISNULL(score_node) ||
            (score_node->json_type() != ObJsonNodeType::J_DOUBLE &&
             score_node->json_type() != ObJsonNodeType::J_INT)) {
          ret = OB_INVALID_DATA;
        } else {
          int64_t local_index = static_cast<ObJsonInt *>(index_node)->value();
          if (local_index < 0 || local_index >= requested_count || seen[local_index]) {
            ret = OB_INVALID_DATA;
          } else {
            double score = score_node->json_type() == ObJsonNodeType::J_DOUBLE
                ? static_cast<ObJsonDouble *>(score_node)->value()
                : static_cast<double>(static_cast<ObJsonInt *>(score_node)->value());
            if (!std::isfinite(score)) {
              ret = OB_INVALID_DATA;
            } else {
              seen[local_index] = true;
              // Keep the original response node (including extra provider fields)
              // but expose the stable global index in the SQL result.
              static_cast<ObJsonInt *>(index_node)->set_value(base_index + local_index);
              RankedRerankItem entry(base_index + local_index, score, score_node, item);
              if (OB_FAIL(ranked.push_back(entry))) {
              }
            }
          }
        }
      }
    }
  }
  return ret;
}

// Stable with respect to the original input order, regardless of HTTP response order.
void sort_rerank_results(ObArray<RankedRerankItem> &ranked)
{
  std::sort(ranked.begin(), ranked.end(),
            [](const RankedRerankItem &a, const RankedRerankItem &b) {
              return a.score_ > b.score_ ||
                     (a.score_ == b.score_ && a.index_ < b.index_);
            });
}
} // namespace

ObExprAIRerank::ObExprAIRerank(common::ObIAllocator &alloc)
: ObFuncExprOperator(alloc, 
                    T_FUN_SYS_AI_RERANK, 
                    N_AI_RERANK, 
                    MORE_THAN_ZERO,
                    NOT_VALID_FOR_GENERATED_COL, 
                    NOT_ROW_DIMENSION) 
{
}

ObExprAIRerank::~ObExprAIRerank() 
{
}

int ObExprAIRerank::calc_result_typeN(ObExprResType &type,
                                    ObExprResType *types_stack,
                                    int64_t param_num,
                                    common::ObExprTypeCtx &type_ctx) const {
  UNUSED(type_ctx);
  UNUSED(types_stack);
  int ret = OB_SUCCESS;
  if (OB_UNLIKELY(param_num < 3 || param_num > 4)) {
    ObString func_name_(get_name());
    ret = OB_ERR_PARAM_SIZE;
    LOG_USER_ERROR(OB_ERR_PARAM_SIZE, func_name_.length(), func_name_.ptr());
  } else {
    if (!ob_is_string_tc(types_stack[MODEL_IDX].get_type())) {
      ret = OB_INVALID_ARGUMENT;
      LOG_USER_ERROR(OB_INVALID_ARGUMENT, "ai_rerank, model key must be string type");
    } else if (!ob_is_string_tc(types_stack[QUERY_IDX].get_type())) {
      ret = OB_INVALID_ARGUMENT;
      LOG_USER_ERROR(OB_INVALID_ARGUMENT, "ai_rerank, query must be string type");
    } else {
      types_stack[MODEL_IDX].set_calc_collation_type(CS_TYPE_UTF8MB4_BIN);
      types_stack[QUERY_IDX].set_calc_collation_type(CS_TYPE_UTF8MB4_BIN);
    }
    ObObjType in_type = types_stack[DOCUMENTS_IDX].get_type();
    if (OB_FAIL(ret)) {
    } else if (OB_FAIL(ObJsonExprHelper::is_valid_for_json(types_stack, DOCUMENTS_IDX, N_AI_RERANK))) {
    } else if (ob_is_string_type(in_type) && types_stack[DOCUMENTS_IDX].get_collation_type() != CS_TYPE_BINARY) {
      if (types_stack[DOCUMENTS_IDX].get_charset_type() != CHARSET_UTF8MB4) {
        types_stack[DOCUMENTS_IDX].set_calc_collation_type(CS_TYPE_UTF8MB4_BIN);
      }
    }
    if (OB_FAIL(ret)) {
    } else if (param_num == 4) {
      ObObjType doc_key_type = types_stack[DOC_KEY_IDX].get_type();
      if (doc_key_type == ObNullType) {
      } else if (doc_key_type == ObJsonType) {
      } else if (ob_is_string_type(doc_key_type)) {
        types_stack[DOC_KEY_IDX].set_calc_collation_type(CS_TYPE_UTF8MB4_BIN);
      } else {  
        ret = OB_INVALID_ARGUMENT;
        LOG_USER_ERROR(OB_INVALID_ARGUMENT, "ai_rerank, doc key must be string type");
      }
    }

    if (OB_SUCC(ret)) {
      type.set_json();
      type.set_length((ObAccuracy::DDL_DEFAULT_ACCURACY[ObJsonType]).get_length());
    }
  }
  return ret;
}

int ObExprAIRerank::eval_ai_rerank(const ObExpr &expr, ObEvalCtx &ctx, ObDatum &res)
{
  INIT_SUCC(ret);
  ObDatum *arg_model_id = nullptr;
  ObDatum *arg_query = nullptr;
  ObDatum *arg_documents = nullptr;
  ObDatum *arg_doc_key = nullptr;
  if (OB_FAIL(expr.eval_param_value(ctx, arg_model_id, arg_query, arg_documents, arg_doc_key))) {
  } else if (arg_model_id->is_null() || arg_query->is_null() || arg_documents->is_null()) {
    ret = OB_INVALID_ARGUMENT;
    LOG_USER_ERROR(OB_INVALID_ARGUMENT, "ai_rerank, model id or query or documents is null");
    res.set_null();
  } else {
    ObEvalCtx::TempAllocGuard tmp_alloc_g(ctx);
    MultimodeAlloctor temp_allocator(tmp_alloc_g.get_allocator());
    lib::ObMallocHookAttrGuard malloc_guard(lib::ObMemAttr(N_AI_RERANK));
    ObAIFuncExprInfo *info = nullptr;
    share::ObAiModelEndpointInfo resolved_endpoint;
    query::ObIAiEndpointResolver *endpoint_resolver =
        ::oceanbase::share::server_service<::oceanbase::query::ObIAiEndpointResolver>();
    ObString model_id = arg_model_id->get_string();
    ObString query = arg_query->get_string();
    ObArray<ObString> headers;
    ObIJsonBase *json_document = nullptr;
    ObJsonArray *documents = nullptr;
    ObJsonArray *input_texts = nullptr;
    ObJsonArray *ranked_json = nullptr;
    bool is_null_result = false;
    const bool use_doc_key = OB_NOT_NULL(arg_doc_key) && !arg_doc_key->is_null();
    const int64_t batch_size = 20;
    ObArray<RankedRerankItem> ranked;

    if (OB_FAIL(ObJsonExprHelper::get_json_doc(expr, ctx, temp_allocator, DOCUMENTS_IDX,
                                              json_document, is_null_result))) {
    } else if (OB_ISNULL(json_document) || json_document->json_type() != ObJsonNodeType::J_ARRAY) {
      ret = OB_INVALID_ARGUMENT;
      LOG_USER_ERROR(OB_INVALID_ARGUMENT, "ai_rerank, documents must be an array");
    } else {
      documents = static_cast<ObJsonArray *>(json_document);
      if (documents->element_count() == 0) {
        ret = OB_INVALID_ARGUMENT;
        LOG_USER_ERROR(OB_INVALID_ARGUMENT, "ai_rerank, document_array is empty");
      }
    }

    if (OB_SUCC(ret) && use_doc_key) {
      ObString doc_key = arg_doc_key->get_string();
      if (OB_FAIL(get_doc_array_from_documents_array_with_key(temp_allocator, documents,
                                                               doc_key, input_texts))) {
      }
    } else if (OB_SUCC(ret)) {
      for (int64_t i = 0; OB_SUCC(ret) && i < documents->element_count(); ++i) {
        ObJsonNode *node = documents->get_value(i);
        if (OB_ISNULL(node) || node->json_type() != ObJsonNodeType::J_STRING) {
          ret = OB_INVALID_ARGUMENT;
          LOG_USER_ERROR(OB_INVALID_ARGUMENT, "ai_rerank, document must be string");
        }
      }
      input_texts = documents;
    }

    if (OB_FAIL(ret)) {
    } else if (OB_FAIL(ObAIFuncUtils::get_ai_func_info(temp_allocator, model_id, info))) {
    } else if (OB_ISNULL(info)) {
      ret = OB_ERR_UNEXPECTED;
    } else if (OB_FAIL(ObAIFuncUtils::check_info_type_rerank(info))) {
    } else if (OB_ISNULL(endpoint_resolver)) {
      ret = OB_ERR_UNEXPECTED;
    } else if (OB_FAIL(endpoint_resolver->resolve_by_model_name(
                   model_id, temp_allocator, resolved_endpoint))) {
    } else if (OB_FAIL(ObAIFuncUtils::get_header(temp_allocator, *info, resolved_endpoint, headers))) {
    }

    // The same batched request/validation path is used for JSON string arrays
    // and for object arrays; this avoids silently losing or misplacing IDs.
    for (int64_t start = 0; OB_SUCC(ret) && start < documents->element_count();
         start += batch_size) {
      int64_t end = start + batch_size;
      if (end > documents->element_count()) {
        end = documents->element_count();
      }
      ObJsonArray *batch_inputs = nullptr;
      ObJsonArray *batch_results = nullptr;
      if (OB_FAIL(construct_batch_document_array(temp_allocator, input_texts,
                                                  start, end, batch_inputs))) {
      } else if (OB_FAIL(inner_eval_ai_rerank(temp_allocator, *info, resolved_endpoint,
                                               headers, query, batch_inputs, batch_results))) {
      } else if (OB_FAIL(append_validated_rerank_batch(batch_results, start, end - start, ranked))) {
        LOG_USER_ERROR(OB_INVALID_DATA, "ai_rerank, invalid result indices or scores");
      }
    }

    if (OB_SUCC(ret)) {
      sort_rerank_results(ranked);
      if (OB_FAIL(ObAIFuncJsonUtils::get_json_array(temp_allocator, ranked_json))) {
      }
      for (int64_t i = 0; OB_SUCC(ret) && i < ranked.count(); ++i) {
        const RankedRerankItem &entry = ranked.at(i);
        if (use_doc_key) {
          ObJsonNode *origin = documents->get_value(entry.index_);
          ObJsonObject *copy = nullptr;
          if (OB_ISNULL(origin) || origin->json_type() != ObJsonNodeType::J_OBJECT) {
            ret = OB_INVALID_DATA;
          } else if (OB_ISNULL(copy = static_cast<ObJsonObject *>(origin->clone(&temp_allocator, true)))) {
            ret = OB_ALLOCATE_MEMORY_FAILED;
          } else if (OB_FAIL(copy->add(MODEL_SCORE_KEY, entry.score_node_))) {
          } else if (OB_FAIL(ranked_json->append(copy))) {
          }
        } else if (OB_FAIL(ranked_json->append(entry.response_))) {
        }
      }
    }
    if (OB_SUCC(ret)) {
      ObString raw_str;
      if (OB_FAIL(ranked_json->get_raw_binary(raw_str, &temp_allocator))) {
      } else if (OB_FAIL(ObJsonExprHelper::pack_json_str_res(expr, ctx, res, raw_str))) {
      }
    }
  }
  return ret;
}

int ObExprAIRerank::get_doc_array_from_documents_array_with_key(ObIAllocator &allocator, ObJsonArray *document_object_array, ObString &doc_key, ObJsonArray *&doc_array)
{
  INIT_SUCC(ret);
  if (OB_ISNULL(document_object_array)) {
    ret = OB_ERR_UNEXPECTED;
  } else if (doc_key.empty()) {
    ret = OB_INVALID_ARGUMENT;
  } else {
    ObJsonArray *doc_array_obj = nullptr;
    if (OB_FAIL(ObAIFuncJsonUtils::get_json_array(allocator, doc_array_obj))) {
    } else {
      int64_t count = document_object_array->element_count();
      ObJsonNode *doc_node = nullptr;
      ObJsonNode *doc_value_node = nullptr;
      ObJsonObject *doc_obj = nullptr;
      for (int64_t i = 0; OB_SUCC(ret) && i < count; i++) {
        doc_node = document_object_array->get_value(i);
        if (OB_ISNULL(doc_node)) {
          ret = OB_INVALID_ARGUMENT;
          LOG_USER_ERROR (OB_INVALID_ARGUMENT, "ai_rerank, document_array element is null");
        } else if (doc_node->json_type() != ObJsonNodeType::J_OBJECT) {
          ret = OB_INVALID_ARGUMENT;
          LOG_USER_ERROR (OB_INVALID_ARGUMENT, "ai_rerank, document_array element is not object");
        } else {
          doc_obj = static_cast<ObJsonObject *>(doc_node);
          doc_value_node = doc_obj->get_value(doc_key);
          if (OB_ISNULL(doc_value_node)) {
            ret = OB_INVALID_ARGUMENT;
            LOG_USER_ERROR (OB_INVALID_ARGUMENT, "ai_rerank, the value for given doc key is null");
          } else if (doc_value_node->json_type() != ObJsonNodeType::J_STRING) {
            ret = OB_INVALID_ARGUMENT;
            LOG_USER_ERROR (OB_INVALID_ARGUMENT, "ai_rerank, the value for given doc key is not string");
          } else if (OB_FAIL(doc_array_obj->append(doc_value_node))) {
          }
        }
      }
    }
    if (OB_SUCC(ret)) {
      doc_array = doc_array_obj;
    }
  }
  return ret;
}

int ObExprAIRerank::construct_batch_document_array(ObIAllocator &allocator, ObJsonArray *document_array, int64_t start_idx, int64_t end_idx, ObJsonArray *&batch_document_array)
{
  INIT_SUCC(ret);
  ObJsonArray *result_array = nullptr;
  if (OB_FAIL(ObAIFuncJsonUtils::get_json_array(allocator, result_array))) {
  } else {
    for (int64_t i = start_idx; i < end_idx; i++) {
      if (OB_FAIL(result_array->append(document_array->get_value(i)))) {
      }
    }
  }
  if (OB_SUCC(ret)) {
    batch_document_array = result_array;
  }
  return ret;
}

int ObExprAIRerank::inner_eval_ai_rerank(ObIAllocator &allocator, 
                                          const ObAIFuncExprInfo &info, 
                                          const share::ObAiModelEndpointInfo &endpoint_info,
                                          ObArray<ObString> &header_array, 
                                          ObString &query, 
                                          ObJsonArray *document_array, 
                                          ObJsonArray *&result_array)
{
  INIT_SUCC(ret);
  ObJsonArray *res = nullptr;
  ObJsonObject *config_json = nullptr;
  ObJsonObject *body = nullptr;
  ObJsonObject *http_response = nullptr;
  ObIJsonBase *response = nullptr;
  ObAIFuncClient ai_client;
  if (OB_FAIL(ObAIFuncUtils::get_rerank_body(allocator, info, endpoint_info, query, document_array, config_json, body))) {
  } else if (OB_FAIL(ai_client.send_post(allocator, endpoint_info.get_url(), header_array, body, http_response))) {
  } else if (OB_FAIL(ObAIFuncUtils::parse_rerank_output(allocator, endpoint_info, http_response, response))) {
  } else if (response->json_type() != ObJsonNodeType::J_ARRAY) {
    ret = OB_ERR_UNEXPECTED;
  } else if (OB_ISNULL(res = static_cast<ObJsonArray *>(response))) {
    ret = OB_ERR_UNEXPECTED;
  } else {
    result_array = res;
  }
  return ret;
}

int ObExprAIRerank::cg_expr(ObExprCGCtx &expr_cg_ctx, 
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
  //     ObAIFuncExprInfo *info = OB_NEWx(ObAIFuncExprInfo, allocator, *allocator, T_FUN_SYS_AI_RERANK);
  //     if (OB_ISNULL(info)) {
  //       ret = OB_ALLOCATE_MEMORY_FAILED;
  //       LOG_WARN("failed to allocate memory for ai rerank info", K(ret));
  //     } else if (OB_FAIL(ObAIFuncUtils::get_ai_func_info(*allocator, const_data.get_string(), *expr_cg_ctx.schema_guard_, info))) {
  //       LOG_WARN("failed to get ai func info", K(ret), K(const_data.get_string()));
  //     } else {
  //       rt_expr.extra_info_ = info;
  //     }
  //   }
  // }
  if (OB_SUCC(ret)) {
    rt_expr.eval_func_ = ObExprAIRerank::eval_ai_rerank;
  }
  return ret;
}

} // namespace sql
} // namespace oceanbase
