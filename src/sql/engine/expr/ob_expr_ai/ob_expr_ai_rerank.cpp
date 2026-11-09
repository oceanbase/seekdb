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
#include <algorithm>
 
using namespace oceanbase::common;
using namespace oceanbase::sql;

namespace oceanbase 
{
namespace sql 
{

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
    // null arguments are propagated as a null result instead of an error
    res.set_null();
  } else {
    ObEvalCtx::TempAllocGuard tmp_alloc_g(ctx);
    
    MultimodeAlloctor temp_allocator(tmp_alloc_g.get_allocator());
    lib::ObMallocHookAttrGuard malloc_guard(lib::ObMemAttr(N_AI_RERANK));
    ObAIFuncExprInfo *info = nullptr;
    share::ObAiModelEndpointInfo resolved_endpoint;
    const share::ObAiModelEndpointInfo *endpoint_info = &resolved_endpoint;
    query::ObIAiEndpointResolver *endpoint_resolver =
        ::oceanbase::share::server_service<::oceanbase::query::ObIAiEndpointResolver>();
    ObString model_id = arg_model_id->get_string();
    ObString query;
    ObExpr *query_expr = expr.args_[QUERY_IDX];
    ObJsonArray *document_array = nullptr;
    ObIJsonBase *j_base = nullptr;
    bool is_null_result = false;
    bool skip_request = false;
    const bool has_doc_key = (OB_NOT_NULL(arg_doc_key) && !arg_doc_key->is_null());
    if (model_id.empty()) {
      ret = OB_INVALID_ARGUMENT;
      LOG_USER_ERROR(OB_INVALID_ARGUMENT, "ai_rerank, model id is empty");
      res.set_null();
    } else if (ob_is_text_tc(query_expr->datum_meta_.type_)) {
      if (OB_FAIL(ObTextStringHelper::read_real_string_data(ctx.exec_ctx_,
                                                            temp_allocator,
                                                            *arg_query,
                                                            query_expr->datum_meta_,
                                                            query_expr->obj_meta_.has_lob_header(),
                                                            query))) {
      }
    } else {
      query = arg_query->get_string();
    }

    if (OB_FAIL(ret)) {
    } else if (query.empty()) {
      ret = OB_INVALID_ARGUMENT;
      LOG_USER_ERROR(OB_INVALID_ARGUMENT, "ai_rerank, query is empty");
      res.set_null();
    } else if (OB_FAIL(ObJsonExprHelper::get_json_doc(expr, ctx, temp_allocator, DOCUMENTS_IDX,
                                                      j_base, is_null_result))) {
    } else if (is_null_result || OB_ISNULL(j_base)) {
      // null documents propagate as a null result, no request is sent
      res.set_null();
      skip_request = true;
    } else if (j_base->json_type() != ObJsonNodeType::J_ARRAY) {
      ret = OB_INVALID_ARGUMENT;
      LOG_USER_ERROR(OB_INVALID_ARGUMENT, "ai_rerank, document_array is not array");
    } else if (OB_ISNULL(document_array = static_cast<ObJsonArray *>(j_base))) {
      ret = OB_ERR_UNEXPECTED;
    } else if (document_array->element_count() == 0) {
      ret = OB_INVALID_ARGUMENT;
      LOG_USER_ERROR(OB_INVALID_ARGUMENT, "ai_rerank, document_array is empty");
    } else {
      for (int64_t i = 0; OB_SUCC(ret) && i < document_array->element_count(); i++) {
        ObJsonNode *doc_node = document_array->get_value(i);
        if (OB_ISNULL(doc_node)) {
          ret = OB_ERR_UNEXPECTED;
        } else if (has_doc_key && doc_node->json_type() != ObJsonNodeType::J_OBJECT) {
          ret = OB_INVALID_ARGUMENT;
          LOG_USER_ERROR(OB_INVALID_ARGUMENT, "ai_rerank, document_array element is not object");
        } else if (!has_doc_key && doc_node->json_type() != ObJsonNodeType::J_STRING) {
          ret = OB_INVALID_ARGUMENT;
          LOG_USER_ERROR(OB_INVALID_ARGUMENT, "ai_rerank, document_array element is not string");
        }
      }
    }
    
    if (OB_FAIL(ret)) {
    } else if (skip_request) {
    } else if (OB_FAIL(ObAIFuncUtils::get_ai_func_info(temp_allocator, model_id, info))) {
    } else if (OB_ISNULL(info)) {
      ret = OB_ERR_UNEXPECTED;
    } else if (OB_ISNULL(endpoint_resolver)) {
      ret = OB_ERR_UNEXPECTED;
    } else if (OB_FAIL(ObAIFuncUtils::check_info_type_rerank(info))) {
    } else if (OB_FAIL(endpoint_resolver->resolve_by_model_name(
                   model_id, temp_allocator, resolved_endpoint))) {
    } else if (has_doc_key) {
      ObString doc_key = arg_doc_key->get_string();
      if (OB_FAIL(eval_ai_rerank_with_doc_key(expr, ctx, temp_allocator, model_id,
                  query, document_array, doc_key, *info, *endpoint_info, res))) {
      }
    } else {
      ObArray<ObString> header_array;
      ObJsonArray *all_result = nullptr;
      ObArray<ObRerankSortItem> sorted_items;
      ObJsonArray *result_array = nullptr;
      if (OB_FAIL(ObAIFuncUtils::get_header(temp_allocator, *info, *endpoint_info, header_array))) {
      } else if (OB_FAIL(collect_rerank_result(temp_allocator, *info, *endpoint_info, header_array,
                                               query, document_array, all_result))) {
      } else if (OB_FAIL(sort_model_result_by_score(temp_allocator, all_result,
                                                    document_array->element_count(), sorted_items))) {
      } else if (OB_FAIL(ObAIFuncJsonUtils::get_json_array(temp_allocator, result_array))) {
      } else {
        for (int64_t i = 0; OB_SUCC(ret) && i < sorted_items.count(); i++) {
          ObJsonNode *node = all_result->get_value(sorted_items.at(i).pos_);
          if (OB_ISNULL(node)) {
            ret = OB_ERR_UNEXPECTED;
          } else if (OB_FAIL(result_array->append(node))) {
          }
        }
      }
      if (OB_SUCC(ret)) {
        ObString raw_str;
        if (OB_FAIL(result_array->get_raw_binary(raw_str, &temp_allocator))) {
        } else if (OB_FAIL(ObJsonExprHelper::pack_json_str_res(expr, ctx, res, raw_str))) {
        }
      }
    }
  }
  return ret;
}

int ObExprAIRerank::eval_ai_rerank_with_doc_key(const ObExpr &expr, ObEvalCtx &ctx, ObIAllocator &allocator, 
                                                ObString& model_id, ObString& query, ObJsonArray *document_array,
                                                ObString& doc_key, const ObAIFuncExprInfo &info,
                                                const share::ObAiModelEndpointInfo &endpoint_info, ObDatum &res)
{
  INIT_SUCC(ret);
  ObArray<ObString> header_array;
  ObJsonArray *doc_array = nullptr;
  ObJsonArray *all_result = nullptr;
  ObJsonArray *sorted_document_array = nullptr;
  ObArray<ObRerankSortItem> sorted_items;
  UNUSED(model_id);
  if (doc_key.empty()) {
    ret = OB_INVALID_ARGUMENT;
    LOG_USER_ERROR(OB_INVALID_ARGUMENT, "ai_rerank, doc key is empty");
  } else if (OB_FAIL(ObAIFuncUtils::get_header(allocator, info, endpoint_info, header_array))) {
  } else if (OB_FAIL(get_doc_array_from_documents_array_with_key(allocator, document_array, doc_key, doc_array))) {
  } else if (OB_FAIL(collect_rerank_result(allocator, info, endpoint_info, header_array,
                                           query, doc_array, all_result))) {
  } else if (OB_FAIL(sort_model_result_by_score(allocator, all_result,
                                                document_array->element_count(), sorted_items))) {
  } else if (OB_FAIL(build_sorted_document_array(allocator, document_array, all_result,
                                                 sorted_items, sorted_document_array))) {
  } else {
    ObString raw_str;
    if (OB_FAIL(sorted_document_array->get_raw_binary(raw_str, &allocator))) {
    } else if (OB_FAIL(ObJsonExprHelper::pack_json_str_res(expr, ctx, res, raw_str))) {
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

int ObExprAIRerank::collect_rerank_result(ObIAllocator &allocator,
                                           const ObAIFuncExprInfo &info,
                                           const share::ObAiModelEndpointInfo &endpoint_info,
                                           ObArray<ObString> &header_array,
                                           ObString &query,
                                           ObJsonArray *document_array,
                                           ObJsonArray *&result_array)
{
  INIT_SUCC(ret);
  ObJsonArray *all_result = nullptr;
  if (OB_ISNULL(document_array)) {
    ret = OB_INVALID_ARGUMENT;
  } else if (OB_FAIL(ObAIFuncJsonUtils::get_json_array(allocator, all_result))) {
  } else {
    const int64_t count = document_array->element_count();
    for (int64_t start = 0; OB_SUCC(ret) && start < count; start += RERANK_BATCH_SIZE) {
      int64_t end = start + RERANK_BATCH_SIZE;
      if (end > count) {
        end = count;
      }
      ObJsonArray *batch_document_array = nullptr;
      ObJsonArray *batch_result_array = nullptr;
      if (OB_FAIL(construct_batch_document_array(allocator, document_array, start, end, batch_document_array))) {
      } else if (OB_FAIL(inner_eval_ai_rerank(allocator, info, endpoint_info, header_array, query,
                                              batch_document_array, batch_result_array))) {
      } else if (OB_FAIL(batch_result_add_base(allocator, batch_result_array, start))) {
      } else {
        for (int64_t i = 0; OB_SUCC(ret) && i < batch_result_array->element_count(); i++) {
          ObJsonNode *node = batch_result_array->get_value(i);
          if (OB_ISNULL(node)) {
            ret = OB_ERR_UNEXPECTED;
          } else if (OB_FAIL(all_result->append(node))) {
          }
        }
      }
    }
  }
  if (OB_SUCC(ret)) {
    result_array = all_result;
  }
  return ret;
}

// Validate the model response and produce a global, deterministic order:
// higher relevance score first, and for equal scores the original document
// order is kept (stable sort), so the result never depends on the batch layout.
int ObExprAIRerank::sort_model_result_by_score(ObIAllocator &allocator,
                                               ObJsonArray *model_result_array,
                                               const int64_t document_count,
                                               ObArray<ObRerankSortItem> &sorted_items)
{
  INIT_SUCC(ret);
  UNUSED(allocator);
  if (OB_ISNULL(model_result_array)) {
    ret = OB_INVALID_ARGUMENT;
  } else if (model_result_array->element_count() <= 0) {
    ret = OB_INVALID_DATA;
    LOG_USER_ERROR(OB_INVALID_DATA);
  } else if (model_result_array->element_count() > document_count) {
    // the provider must not return more documents than we sent
    ret = OB_INVALID_DATA;
    LOG_USER_ERROR(OB_INVALID_DATA);
  } else {
    const int64_t count = model_result_array->element_count();
    for (int64_t i = 0; OB_SUCC(ret) && i < count; i++) {
      ObJsonNode *node = model_result_array->get_value(i);
      if (OB_ISNULL(node) || node->json_type() != ObJsonNodeType::J_OBJECT) {
        ret = OB_INVALID_DATA;
        LOG_USER_ERROR(OB_INVALID_DATA);
      } else {
        ObJsonObject *result_obj = static_cast<ObJsonObject *>(node);
        ObJsonNode *index_node = result_obj->get_value(INDEX_KEY);
        ObJsonNode *score_node = result_obj->get_value(SCORE_KEY);
        if (OB_ISNULL(index_node) || index_node->json_type() != ObJsonNodeType::J_INT) {
          ret = OB_INVALID_DATA;
          LOG_USER_ERROR(OB_INVALID_DATA);
        } else if (OB_ISNULL(score_node) 
                   || (score_node->json_type() != ObJsonNodeType::J_INT
                       && score_node->json_type() != ObJsonNodeType::J_UINT
                       && score_node->json_type() != ObJsonNodeType::J_DOUBLE
                       && score_node->json_type() != ObJsonNodeType::J_DECIMAL)) {
          ret = OB_INVALID_DATA;
          LOG_USER_ERROR(OB_INVALID_DATA);
        } else {
          const int64_t index = index_node->get_int();
          const double score = score_node->get_double();
          if (index < 0 || index >= document_count) {
            ret = OB_INVALID_DATA;
            LOG_USER_ERROR(OB_INVALID_DATA);
          } else if (score != score) {
            // reject NaN, it breaks any ordering
            ret = OB_INVALID_DATA;
            LOG_USER_ERROR(OB_INVALID_DATA);
          } else {
            ObRerankSortItem item;
            item.pos_ = i;
            item.index_ = index;
            item.score_ = score;
            if (OB_FAIL(sorted_items.push_back(item))) {
            }
          }
        }
      }
    }
  }
  if (OB_SUCC(ret)) {
    std::stable_sort(sorted_items.begin(), sorted_items.end());
  }
  return ret;
}

int ObExprAIRerank::build_sorted_document_array(ObIAllocator &allocator,
                                                ObJsonArray *document_array,
                                                ObJsonArray *model_result_array,
                                                ObArray<ObRerankSortItem> &sorted_items,
                                                ObJsonArray *&sorted_document_array)
{
  INIT_SUCC(ret);
  ObJsonArray *result_array = nullptr;
  if (OB_ISNULL(document_array) || OB_ISNULL(model_result_array)) {
    ret = OB_INVALID_ARGUMENT;
  } else if (OB_FAIL(ObAIFuncJsonUtils::get_json_array(allocator, result_array))) {
  } else {
    for (int64_t i = 0; OB_SUCC(ret) && i < sorted_items.count(); i++) {
      const ObRerankSortItem &item = sorted_items.at(i);
      ObJsonNode *origin_node = document_array->get_value(item.index_);
      ObJsonNode *result_node = model_result_array->get_value(item.pos_);
      if (OB_ISNULL(origin_node) || OB_ISNULL(result_node)) {
        ret = OB_ERR_UNEXPECTED;
      } else if (origin_node->json_type() != ObJsonNodeType::J_OBJECT
                 || result_node->json_type() != ObJsonNodeType::J_OBJECT) {
        ret = OB_INVALID_DATA;
        LOG_USER_ERROR(OB_INVALID_DATA);
      } else {
        ObJsonObject *origin_obj = static_cast<ObJsonObject *>(origin_node);
        ObJsonNode *score_node = static_cast<ObJsonObject *>(result_node)->get_value(SCORE_KEY);
        ObJsonObject *new_obj = nullptr;
        if (OB_ISNULL(score_node)) {
          ret = OB_INVALID_DATA;
          LOG_USER_ERROR(OB_INVALID_DATA);
        } else if (OB_ISNULL(new_obj = static_cast<ObJsonObject *>(origin_obj->clone(&allocator, true)))) {
          ret = OB_ERR_UNEXPECTED;
        } else if (OB_FAIL(new_obj->add(MODEL_SCORE_KEY, score_node))) {
        } else if (OB_FAIL(result_array->append(new_obj))) {
        }
      }
    }
  }
  if (OB_SUCC(ret)) {
    sorted_document_array = result_array;
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

int ObExprAIRerank::batch_result_add_base(ObIAllocator &allocator, ObJsonArray *array, int64_t start_idx)
{
  INIT_SUCC(ret);
  int64_t count = array->element_count();
  ObString index_key(INDEX_KEY);
  for (int64_t i = 0; OB_SUCC(ret) && i < count; i++) {
    ObJsonObject *obj = static_cast<ObJsonObject *>(array->get_value(i));
    if (OB_ISNULL(obj)) {
      ret = OB_ERR_UNEXPECTED;
    } else {
      ObJsonInt *int_obj = static_cast<ObJsonInt *>(obj->get_value(index_key));
      if (OB_ISNULL(int_obj)) {
        ret = OB_ERR_UNEXPECTED;
      } else {
        int64_t id = int_obj->value() + start_idx;
        int_obj->set_value(id);
      }
    }
  }
  return ret;
}

int ObExprAIRerank::construct_config_json(ObIAllocator &allocator, int64_t top_n, int64_t return_doc, ObJsonObject *&config_json)
{
  INIT_SUCC(ret);
  ObJsonObject *config_obj = nullptr;
  ObJsonInt *top_n_obj = nullptr;
  ObJsonBoolean *return_doc_obj = nullptr;
  bool return_doc_value = true;
  if (return_doc <= 0) {
    return_doc_value = false;
  }
  if (OB_FAIL(ObAIFuncJsonUtils::get_json_object(allocator, config_obj))) {
  } else if (OB_FAIL(ObAIFuncJsonUtils::get_json_boolean(allocator, return_doc_value, return_doc_obj))) {
  } else if (OB_FAIL(config_obj->add("return_documents", return_doc_obj))) {
  } else if (top_n > 0) {
    if (OB_FAIL(ObAIFuncJsonUtils::get_json_int(allocator, top_n, top_n_obj))) {
    } else if (OB_FAIL(config_obj->add("top_n", top_n_obj))) {
    }
  }
  if (OB_SUCC(ret)) {
    config_json = config_obj;
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
