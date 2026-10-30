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
#include <climits>
#include <cmath>
 
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
    if (!ob_is_string_type(types_stack[MODEL_IDX].get_type())) {
      ret = OB_INVALID_ARGUMENT;
      LOG_USER_ERROR(OB_INVALID_ARGUMENT, "ai_rerank, model key must be string type");
    } else if (!ob_is_string_type(types_stack[QUERY_IDX].get_type())) {
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
    const share::ObAiModelEndpointInfo *endpoint_info = &resolved_endpoint;
    query::ObIAiEndpointResolver *endpoint_resolver =
        ::oceanbase::share::server_service<::oceanbase::query::ObIAiEndpointResolver>();
    ObString model_id;
    ObString query;
    ObArray<ObString> header_array;
    ObJsonArray *document_array = nullptr;
    ObJsonArray *result_array = nullptr;
    ObIJsonBase *j_base = nullptr;
    bool is_null_result = false;
    // the document key is optional, an absent or null document key means that
    // the documents are an array of strings
    const bool has_doc_key = OB_NOT_NULL(arg_doc_key) && !arg_doc_key->is_null();
    if (OB_FAIL(ObTextStringHelper::read_real_string_data(ctx.exec_ctx_, temp_allocator, *arg_model_id,
          expr.args_[MODEL_IDX]->datum_meta_, expr.args_[MODEL_IDX]->obj_meta_.has_lob_header(), model_id))) {
    } else if (OB_FAIL(ObTextStringHelper::read_real_string_data(ctx.exec_ctx_, temp_allocator, *arg_query,
          expr.args_[QUERY_IDX]->datum_meta_, expr.args_[QUERY_IDX]->obj_meta_.has_lob_header(), query))) {
    } else if (model_id.empty() || query.empty()) {
      ret = OB_INVALID_ARGUMENT;
    } else if (OB_FAIL(ObJsonExprHelper::get_json_doc(expr, ctx, temp_allocator, DOCUMENTS_IDX,
          j_base, is_null_result))) {
    } else if (OB_ISNULL(j_base) || is_null_result) {
      ret = OB_ERR_UNEXPECTED;
    } else if (j_base->json_type() != ObJsonNodeType::J_ARRAY) {
      ret = OB_INVALID_ARGUMENT;
      LOG_USER_ERROR(OB_INVALID_ARGUMENT, "ai_rerank, document_array is not array");
    } else if (OB_ISNULL(document_array = static_cast<ObJsonArray *>(j_base))) {
      ret = OB_ERR_UNEXPECTED;
    } else if (document_array->element_count() == 0) {
      ret = OB_INVALID_ARGUMENT;
      LOG_USER_ERROR(OB_INVALID_ARGUMENT, "ai_rerank, document_array is empty");
    } else {
      const int64_t doc_count = static_cast<int64_t>(document_array->element_count());
      for (int64_t i = 0; OB_SUCC(ret) && i < doc_count; i++) {
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
    } else if (OB_FAIL(ObAIFuncUtils::get_ai_func_info(temp_allocator, model_id, info))) {
    } else if (OB_ISNULL(info)) {
      ret = OB_ERR_UNEXPECTED;
    } else if (OB_ISNULL(endpoint_resolver)) {
      ret = OB_ERR_UNEXPECTED;
    } else if (OB_FAIL(endpoint_resolver->resolve_by_model_name(
                   model_id, temp_allocator, resolved_endpoint))) {
    } else if (OB_FAIL(ObAIFuncUtils::check_info_type_rerank(info))) {
    } else if (OB_FAIL(ObAIFuncUtils::get_header(temp_allocator, *info, *endpoint_info, header_array))) {
    } else if (has_doc_key) {
      ObJsonArray *sorted_document_array = nullptr;
      ObString doc_key;
      if (OB_FAIL(ObTextStringHelper::read_real_string_data(ctx.exec_ctx_, temp_allocator, *arg_doc_key,
            expr.args_[DOC_KEY_IDX]->datum_meta_, expr.args_[DOC_KEY_IDX]->obj_meta_.has_lob_header(), doc_key))) {
      } else if (OB_FAIL(eval_ai_rerank_with_doc_key(temp_allocator, *info, *endpoint_info, header_array,
                                              query, document_array, doc_key, sorted_document_array))) {
      } else if (OB_ISNULL(sorted_document_array)) {
        ret = OB_ERR_UNEXPECTED;
      } else {
        ObString raw_str;
        if (OB_FAIL(sorted_document_array->get_raw_binary(raw_str, &temp_allocator))) {
        } else if (OB_FAIL(ObJsonExprHelper::pack_json_str_res(expr, ctx, res, raw_str))) {
        }
      }
    } else {
      if (OB_FAIL(call_rerank_in_batches(temp_allocator, *info, *endpoint_info, header_array,
                                         query, document_array, result_array))) {
      } else if (OB_ISNULL(result_array)) {
        ret = OB_ERR_UNEXPECTED;
      } else {
        ObString raw_str;
        if (OB_FAIL(result_array->get_raw_binary(raw_str, &temp_allocator))) {
        } else if (OB_FAIL(ObJsonExprHelper::pack_json_str_res(expr, ctx, res, raw_str))) {
        }
      }
    }
  }
  return ret;
}

int ObExprAIRerank::eval_ai_rerank_with_doc_key(ObIAllocator &allocator,
                                                const ObAIFuncExprInfo &info,
                                                const share::ObAiModelEndpointInfo &endpoint_info,
                                                ObArray<ObString> &header_array,
                                                ObString &query,
                                                ObJsonArray *document_array,
                                                ObString &doc_key,
                                                ObJsonArray *&sorted_document_array)
{
  INIT_SUCC(ret);
  sorted_document_array = nullptr;
  ObJsonArray *doc_array = nullptr;
  ObJsonArray *result_array = nullptr;
  if (OB_FAIL(get_doc_array_from_documents_array_with_key(allocator, document_array, doc_key, doc_array))) {
  } else if (OB_FAIL(call_rerank_in_batches(allocator, info, endpoint_info, header_array,
                                            query, doc_array, result_array))) {
  } else if (OB_FAIL(sort_document_array_by_model_result(allocator, document_array, result_array,
                                                         sorted_document_array))) {
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

int ObExprAIRerank::sort_document_array_by_model_result(ObIAllocator &allocator, ObJsonArray *document_array, ObJsonArray *model_result_array, ObJsonArray *&sorted_document_array)
{
  INIT_SUCC(ret);
  sorted_document_array = nullptr;
  ObJsonArray *sorted_document_array_obj = nullptr;
  const int64_t doc_count = OB_ISNULL(document_array) ? 0 : static_cast<int64_t>(document_array->element_count());
  const int64_t result_count = OB_ISNULL(model_result_array) ? 0 : static_cast<int64_t>(model_result_array->element_count());
  if (OB_ISNULL(document_array) || OB_ISNULL(model_result_array)) {
    ret = OB_ERR_UNEXPECTED;
  } else if (result_count != doc_count) {
    // every document must be ranked exactly once, otherwise the order of the
    // documents cannot be restored
    ret = OB_INVALID_DATA;
    FORWARD_USER_ERROR(OB_INVALID_DATA, "ai_rerank, the number of the results of the model is not equal to the number of the documents");
  } else if (OB_FAIL(ObAIFuncJsonUtils::get_json_array(allocator, sorted_document_array_obj))) {
  } else {
    ObArray<int8_t> index_used;
    for (int64_t i = 0; OB_SUCC(ret) && i < doc_count; i++) {
      if (OB_FAIL(index_used.push_back(static_cast<int8_t>(0)))) {
        ret = OB_ALLOCATE_MEMORY_FAILED;
      }
    }
    for (int64_t i = 0; OB_SUCC(ret) && i < result_count; i++) {
      ObJsonNode *model_result_node = model_result_array->get_value(i);
      int64_t index = 0;
      double score = 0.0;
      if (OB_ISNULL(model_result_node) || model_result_node->json_type() != ObJsonNodeType::J_OBJECT) {
        ret = OB_INVALID_DATA;
        FORWARD_USER_ERROR(OB_INVALID_DATA, "ai_rerank, the result of the model is not an object");
      } else if (OB_FAIL(get_rerank_score_and_index(static_cast<ObJsonObject *>(model_result_node), index, score))) {
      } else if (index < 0 || index >= doc_count) {
        ret = OB_INVALID_DATA;
        FORWARD_USER_ERROR(OB_INVALID_DATA, "ai_rerank, the index of the result of the model is out of range");
      } else if (index_used[index] != 0) {
        ret = OB_INVALID_DATA;
        FORWARD_USER_ERROR(OB_INVALID_DATA, "ai_rerank, the index of the result of the model is duplicated");
      } else {
        index_used[index] = 1;
        ObJsonNode *document_node = document_array->get_value(index);
        ObJsonNode *score_node = static_cast<ObJsonObject *>(model_result_node)->get_value(SCORE_KEY);
        ObJsonObject *new_obj = nullptr;
        ObJsonNode *score_clone = nullptr;
        if (OB_ISNULL(document_node) || document_node->json_type() != ObJsonNodeType::J_OBJECT) {
          ret = OB_INVALID_DATA;
          FORWARD_USER_ERROR(OB_INVALID_DATA, "ai_rerank, document_array element is not object");
        } else if (OB_ISNULL(new_obj = static_cast<ObJsonObject *>(document_node->clone(&allocator, true)))) {
          ret = OB_ALLOCATE_MEMORY_FAILED;
        } else if (OB_ISNULL(score_node)) {
          ret = OB_INVALID_DATA;
          FORWARD_USER_ERROR(OB_INVALID_DATA, "ai_rerank, the score of the result of the model is missing");
        } else if (OB_ISNULL(score_clone = score_node->clone(&allocator, false))) {
          ret = OB_ALLOCATE_MEMORY_FAILED;
        } else if (OB_FAIL(new_obj->add(MODEL_SCORE_KEY, score_clone))) {
        } else if (OB_FAIL(sorted_document_array_obj->append(new_obj))) {
        }
      }
    }
  }
  if (OB_SUCC(ret)) {
    sorted_document_array = sorted_document_array_obj;
  }
  return ret;
}

int ObExprAIRerank::construct_batch_document_array(ObIAllocator &allocator, ObJsonArray *document_array, int64_t start_idx, int64_t end_idx, ObJsonArray *&batch_document_array)
{
  INIT_SUCC(ret);
  batch_document_array = nullptr;
  ObJsonArray *result_array = nullptr;
  const int64_t doc_count = OB_ISNULL(document_array) ? 0 : static_cast<int64_t>(document_array->element_count());
  if (OB_ISNULL(document_array) || start_idx < 0 || end_idx < start_idx || end_idx > doc_count) {
    ret = OB_ERR_UNEXPECTED;
  } else if (OB_FAIL(ObAIFuncJsonUtils::get_json_array(allocator, result_array))) {
  } else {
    for (int64_t i = start_idx; OB_SUCC(ret) && i < end_idx; i++) {
      ObJsonNode *doc_node = document_array->get_value(i);
      if (OB_ISNULL(doc_node)) {
        ret = OB_ERR_UNEXPECTED;
      } else if (OB_FAIL(result_array->append(doc_node))) {
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
  result_array = nullptr;
  ObJsonArray *res = nullptr;
  ObJsonObject *config_json = nullptr;
  ObJsonObject *body = nullptr;
  ObJsonObject *http_response = nullptr;
  ObIJsonBase *response = nullptr;
  ObAIFuncClient ai_client;
  if (OB_FAIL(ObAIFuncUtils::get_rerank_body(allocator, info, endpoint_info, query, document_array, config_json, body))) {
  } else if (OB_FAIL(ai_client.send_post(allocator, endpoint_info.get_url(), header_array, body, http_response))) {
  } else if (OB_FAIL(ObAIFuncUtils::parse_rerank_output(allocator, endpoint_info, http_response, response))) {
  } else if (OB_ISNULL(response) || response->json_type() != ObJsonNodeType::J_ARRAY) {
    ret = OB_INVALID_DATA;
    FORWARD_USER_ERROR(OB_INVALID_DATA, "ai_rerank, the answer of the model is not an array");
  } else if (OB_ISNULL(res = static_cast<ObJsonArray *>(response))) {
    ret = OB_ERR_UNEXPECTED;
  } else {
    result_array = res;
  }
  return ret;
}

int ObExprAIRerank::get_rerank_score_and_index(ObJsonObject *result_obj, int64_t &index, double &score)
{
  INIT_SUCC(ret);
  if (OB_ISNULL(result_obj)) {
    ret = OB_ERR_UNEXPECTED;
  } else {
    ObJsonNode *index_node = result_obj->get_value(INDEX_KEY);
    ObJsonNode *score_node = result_obj->get_value(SCORE_KEY);
    if (OB_ISNULL(index_node)
        || (index_node->json_type() != ObJsonNodeType::J_INT
            && index_node->json_type() != ObJsonNodeType::J_UINT)) {
      ret = OB_INVALID_DATA;
      FORWARD_USER_ERROR(OB_INVALID_DATA, "ai_rerank, the index of the result of the model is missing or invalid");
    } else if (OB_ISNULL(score_node)) {
      ret = OB_INVALID_DATA;
      FORWARD_USER_ERROR(OB_INVALID_DATA, "ai_rerank, the score of the result of the model is missing");
    } else {
      if (ObJsonNodeType::J_INT == index_node->json_type()) {
        index = static_cast<ObJsonInt *>(index_node)->value();
      } else {
        const uint64_t uint_index = static_cast<ObJsonUint *>(index_node)->value();
        if (uint_index > static_cast<uint64_t>(INT64_MAX)) {
          ret = OB_INVALID_DATA;
          FORWARD_USER_ERROR(OB_INVALID_DATA, "ai_rerank, the index of the result of the model is out of range");
        } else {
          index = static_cast<int64_t>(uint_index);
        }
      }
      if (OB_FAIL(ret)) {
      } else if (ObJsonNodeType::J_DOUBLE == score_node->json_type()) {
        score = static_cast<ObJsonDouble *>(score_node)->value();
      } else if (ObJsonNodeType::J_INT == score_node->json_type()) {
        score = static_cast<double>(static_cast<ObJsonInt *>(score_node)->value());
      } else if (ObJsonNodeType::J_UINT == score_node->json_type()) {
        score = static_cast<double>(static_cast<ObJsonUint *>(score_node)->value());
      } else {
        ret = OB_INVALID_DATA;
        FORWARD_USER_ERROR(OB_INVALID_DATA, "ai_rerank, the score of the result of the model is invalid");
      }
      if (OB_SUCC(ret) && !std::isfinite(score)) {
        ret = OB_INVALID_DATA;
        FORWARD_USER_ERROR(OB_INVALID_DATA, "ai_rerank, the score of the result of the model is not finite");
      }
    }
  }
  return ret;
}

int ObExprAIRerank::build_rerank_batch_result(ObIAllocator &allocator,
                                              ObJsonArray *batch_result,
                                              int64_t start_idx,
                                              int64_t batch_count,
                                              ObJsonArray *&sorted_batch)
{
  INIT_SUCC(ret);
  sorted_batch = nullptr;
  ObJsonArray *sorted_batch_obj = nullptr;
  const int64_t result_count = OB_ISNULL(batch_result) ? 0 : static_cast<int64_t>(batch_result->element_count());
  if (OB_ISNULL(batch_result) || start_idx < 0 || batch_count <= 0) {
    ret = OB_ERR_UNEXPECTED;
  } else if (result_count != batch_count) {
    // every document of the batch must be ranked exactly once
    ret = OB_INVALID_DATA;
    FORWARD_USER_ERROR(OB_INVALID_DATA, "ai_rerank, the number of the results of the model is not equal to the number of the documents");
  } else if (OB_FAIL(ObAIFuncJsonUtils::get_json_array(allocator, sorted_batch_obj))) {
  } else {
    ObArray<ObJsonNode *> nodes;
    ObArray<int64_t> indexes;
    ObArray<double> scores;
    ObArray<int8_t> index_used;
    for (int64_t i = 0; OB_SUCC(ret) && i < batch_count; i++) {
      if (OB_FAIL(index_used.push_back(static_cast<int8_t>(0)))) {
        ret = OB_ALLOCATE_MEMORY_FAILED;
      }
    }
    for (int64_t i = 0; OB_SUCC(ret) && i < result_count; i++) {
      ObJsonNode *node = batch_result->get_value(i);
      ObJsonObject *result_obj = nullptr;
      ObJsonObject *normalized_obj = nullptr;
      ObJsonInt *global_index_node = nullptr;
      ObJsonNode *score_node = nullptr;
      ObJsonNode *score_clone = nullptr;
      int64_t index = 0;
      double score = 0.0;
      if (OB_ISNULL(node) || node->json_type() != ObJsonNodeType::J_OBJECT) {
        ret = OB_INVALID_DATA;
        FORWARD_USER_ERROR(OB_INVALID_DATA, "ai_rerank, the result of the model is not an object");
      } else if (OB_FAIL(get_rerank_score_and_index(static_cast<ObJsonObject *>(node), index, score))) {
      } else if (index < 0 || index >= batch_count) {
        ret = OB_INVALID_DATA;
        FORWARD_USER_ERROR(OB_INVALID_DATA, "ai_rerank, the index of the result of the model is out of range");
      } else if (index_used[index] != 0) {
        ret = OB_INVALID_DATA;
        FORWARD_USER_ERROR(OB_INVALID_DATA, "ai_rerank, the index of the result of the model is duplicated");
      } else {
        index_used[index] = 1;
        result_obj = static_cast<ObJsonObject *>(node);
        score_node = result_obj->get_value(SCORE_KEY);
        if (OB_FAIL(ObAIFuncJsonUtils::get_json_object(allocator, normalized_obj))) {
        } else if (OB_FAIL(ObAIFuncJsonUtils::get_json_int(allocator, index + start_idx, global_index_node))) {
        } else if (OB_FAIL(normalized_obj->add(INDEX_KEY, global_index_node))) {
        } else if (OB_ISNULL(score_node)) {
          ret = OB_INVALID_DATA;
          FORWARD_USER_ERROR(OB_INVALID_DATA, "ai_rerank, the score of the result of the model is missing");
        } else if (OB_ISNULL(score_clone = score_node->clone(&allocator, false))) {
          ret = OB_ALLOCATE_MEMORY_FAILED;
        } else if (OB_FAIL(normalized_obj->add(SCORE_KEY, score_clone))) {
        } else if (OB_FAIL(nodes.push_back(normalized_obj))
                   || OB_FAIL(indexes.push_back(index + start_idx))
                   || OB_FAIL(scores.push_back(score))) {
          ret = OB_ALLOCATE_MEMORY_FAILED;
        }
      }
    }
    // the elements are sorted by the score, the bigger score comes first and
    // the smaller index comes first when the scores are equal
    for (int64_t i = 1; OB_SUCC(ret) && i < nodes.count(); i++) {
      ObJsonNode *cur_node = nodes[i];
      int64_t cur_index = indexes[i];
      double cur_score = scores[i];
      int64_t j = i - 1;
      while (j >= 0 && (scores[j] < cur_score
                        || (scores[j] == cur_score && indexes[j] > cur_index))) {
        nodes[j + 1] = nodes[j];
        indexes[j + 1] = indexes[j];
        scores[j + 1] = scores[j];
        j--;
      }
      nodes[j + 1] = cur_node;
      indexes[j + 1] = cur_index;
      scores[j + 1] = cur_score;
    }
    for (int64_t i = 0; OB_SUCC(ret) && i < nodes.count(); i++) {
      if (OB_FAIL(sorted_batch_obj->append(nodes[i]))) {
      }
    }
  }
  if (OB_SUCC(ret)) {
    sorted_batch = sorted_batch_obj;
  }
  return ret;
}

int ObExprAIRerank::merge_rerank_result_by_score(ObIAllocator &allocator,
                                                 ObJsonArray *array1,
                                                 ObJsonArray *array2,
                                                 ObJsonArray *&merged_array)
{
  INIT_SUCC(ret);
  merged_array = nullptr;
  ObJsonArray *merged_array_obj = nullptr;
  if (OB_ISNULL(array1) || OB_ISNULL(array2)) {
    ret = OB_ERR_UNEXPECTED;
  } else if (OB_FAIL(ObAIFuncJsonUtils::get_json_array(allocator, merged_array_obj))) {
  } else {
    const int64_t count1 = static_cast<int64_t>(array1->element_count());
    const int64_t count2 = static_cast<int64_t>(array2->element_count());
    int64_t i = 0;
    int64_t j = 0;
    while (OB_SUCC(ret) && i < count1 && j < count2) {
      ObJsonNode *node1 = array1->get_value(i);
      ObJsonNode *node2 = array2->get_value(j);
      int64_t index1 = 0;
      int64_t index2 = 0;
      double score1 = 0.0;
      double score2 = 0.0;
      if (OB_ISNULL(node1) || node1->json_type() != ObJsonNodeType::J_OBJECT
          || OB_ISNULL(node2) || node2->json_type() != ObJsonNodeType::J_OBJECT) {
        ret = OB_INVALID_DATA;
        FORWARD_USER_ERROR(OB_INVALID_DATA, "ai_rerank, the result of the model is not an object");
      } else if (OB_FAIL(get_rerank_score_and_index(static_cast<ObJsonObject *>(node1), index1, score1))
                 || OB_FAIL(get_rerank_score_and_index(static_cast<ObJsonObject *>(node2), index2, score2))) {
      } else if (score1 > score2 || (score1 == score2 && index1 <= index2)) {
        // the result with the bigger score comes first, the result with the
        // smaller index comes first when the scores are equal
        if (OB_FAIL(merged_array_obj->append(node1))) {
          break;
        }
        i++;
      } else {
        if (OB_FAIL(merged_array_obj->append(node2))) {
          break;
        }
        j++;
      }
    }
    while (OB_SUCC(ret) && i < count1) {
      if (OB_FAIL(merged_array_obj->append(array1->get_value(i)))) {
        break;
      }
      i++;
    }
    while (OB_SUCC(ret) && j < count2) {
      if (OB_FAIL(merged_array_obj->append(array2->get_value(j)))) {
        break;
      }
      j++;
    }
  }
  if (OB_SUCC(ret)) {
    merged_array = merged_array_obj;
  }
  return ret;
}

int ObExprAIRerank::validate_rerank_result(ObJsonArray *result_array, int64_t doc_count)
{
  INIT_SUCC(ret);
  const int64_t result_count = OB_ISNULL(result_array) ? 0 : static_cast<int64_t>(result_array->element_count());
  ObArray<int8_t> index_used;
  if (OB_ISNULL(result_array) || doc_count <= 0) {
    ret = OB_ERR_UNEXPECTED;
  } else if (result_count != doc_count) {
    ret = OB_INVALID_DATA;
    FORWARD_USER_ERROR(OB_INVALID_DATA, "ai_rerank, the number of the results of the model is not equal to the number of the documents");
  }
  for (int64_t i = 0; OB_SUCC(ret) && i < doc_count; i++) {
    if (OB_FAIL(index_used.push_back(static_cast<int8_t>(0)))) {
      ret = OB_ALLOCATE_MEMORY_FAILED;
    }
  }
  for (int64_t i = 0; OB_SUCC(ret) && i < result_count; i++) {
    ObJsonNode *node = result_array->get_value(i);
    int64_t index = 0;
    double score = 0.0;
    if (OB_ISNULL(node) || node->json_type() != ObJsonNodeType::J_OBJECT) {
      ret = OB_INVALID_DATA;
      FORWARD_USER_ERROR(OB_INVALID_DATA, "ai_rerank, the result of the model is not an object");
    } else if (OB_FAIL(get_rerank_score_and_index(static_cast<ObJsonObject *>(node), index, score))) {
    } else if (index < 0 || index >= doc_count) {
      ret = OB_INVALID_DATA;
      FORWARD_USER_ERROR(OB_INVALID_DATA, "ai_rerank, the index of the result of the model is out of range");
    } else if (index_used[index] != 0) {
      ret = OB_INVALID_DATA;
      FORWARD_USER_ERROR(OB_INVALID_DATA, "ai_rerank, the index of the result of the model is duplicated");
    } else {
      index_used[index] = 1;
    }
  }
  return ret;
}

int ObExprAIRerank::call_rerank_in_batches(ObIAllocator &allocator,
                                           const ObAIFuncExprInfo &info,
                                           const share::ObAiModelEndpointInfo &endpoint_info,
                                           ObArray<ObString> &header_array,
                                           ObString &query,
                                           ObJsonArray *document_array,
                                           ObJsonArray *&result_array)
{
  INIT_SUCC(ret);
  result_array = nullptr;
  ObJsonArray *merged_array = nullptr;
  const int64_t doc_count = OB_ISNULL(document_array) ? 0 : static_cast<int64_t>(document_array->element_count());
  if (OB_ISNULL(document_array)) {
    ret = OB_ERR_UNEXPECTED;
  } else if (doc_count <= 0) {
    ret = OB_INVALID_ARGUMENT;
    LOG_USER_ERROR(OB_INVALID_ARGUMENT, "ai_rerank, document_array is empty");
  } else if (OB_FAIL(ObAIFuncJsonUtils::get_json_array(allocator, merged_array))) {
  } else {
    for (int64_t start_idx = 0; OB_SUCC(ret) && start_idx < doc_count; start_idx += RERANK_BATCH_SIZE) {
      int64_t end_idx = start_idx + RERANK_BATCH_SIZE;
      if (end_idx > doc_count) {
        end_idx = doc_count;
      }
      ObJsonArray *batch_document_array = nullptr;
      ObJsonArray *batch_result_array = nullptr;
      ObJsonArray *sorted_batch_array = nullptr;
      ObJsonArray *new_merged_array = nullptr;
      if (OB_FAIL(construct_batch_document_array(allocator, document_array, start_idx, end_idx, batch_document_array))) {
      } else if (OB_FAIL(inner_eval_ai_rerank(allocator, info, endpoint_info, header_array, query, batch_document_array, batch_result_array))) {
      } else if (OB_FAIL(build_rerank_batch_result(allocator, batch_result_array, start_idx, end_idx - start_idx, sorted_batch_array))) {
      } else if (OB_FAIL(merge_rerank_result_by_score(allocator, merged_array, sorted_batch_array, new_merged_array))) {
      } else {
        merged_array = new_merged_array;
      }
    }
    if (OB_SUCC(ret)) {
      ObJsonArray *normalized_array = nullptr;
      if (OB_FAIL(validate_rerank_result(merged_array, doc_count))) {
      } else if (OB_FAIL(ObAIFuncUtils::normalize_rerank_result(allocator, merged_array,
                     doc_count, normalized_array))) {
      } else if (OB_ISNULL(normalized_array)) {
        ret = OB_ERR_UNEXPECTED;
      } else {
        // the normalized answer keeps the index of every document and its
        // relevance score, the bigger score comes first and the smaller index
        // comes first when the scores are equal
        result_array = normalized_array;
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
