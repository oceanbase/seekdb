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
#include "common/json_type/json_builder.h"
#include "query/ai/ob_ai_endpoint_resolver.h"
#include "share/rc/ob_server_runtime.h"
#include "lib/utility/utility.h"
#include "common/json_type/ob_json_common.h"
#include <cmath>
 
using namespace oceanbase::common;
using namespace oceanbase::sql;

namespace oceanbase 
{
namespace sql 
{

namespace
{
int get_rerank_score(const ObIJsonBase *score_node, double &score)
{
  INIT_SUCC(ret);
  if (OB_ISNULL(score_node)) {
    ret = OB_INVALID_DATA;
  } else if (score_node->json_type() == ObJsonNodeType::J_DOUBLE) {
    score = score_node->get_double();
  } else if (score_node->json_type() == ObJsonNodeType::J_INT) {
    score = static_cast<double>(score_node->get_int());
  } else if (score_node->json_type() == ObJsonNodeType::J_UINT) {
    score = static_cast<double>(score_node->get_uint());
  } else if (score_node->json_type() == ObJsonNodeType::J_DECIMAL) {
    char decimal_buf[512];
    int64_t pos = 0;
    number::ObNumber decimal = score_node->get_decimal_data();
    if (OB_FAIL(decimal.format(decimal_buf, sizeof(decimal_buf), pos, -1))) {
    } else if (pos >= static_cast<int64_t>(sizeof(decimal_buf))) {
      ret = OB_SIZE_OVERFLOW;
    } else {
      decimal_buf[pos] = '\0';
      score = atof(decimal_buf);
    }
  } else {
    ret = OB_INVALID_DATA;
  }
  return ret;
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
    const share::ObAiModelEndpointInfo *endpoint_info = &resolved_endpoint;
    query::ObIAiEndpointResolver *endpoint_resolver =
        ::oceanbase::share::server_service<::oceanbase::query::ObIAiEndpointResolver>();
    ObString model_id = arg_model_id->get_string();
    ObString query = arg_query->get_string();
    ObArray<ObString> header_array;
    ObIJsonBase *document_array = nullptr;
    ObIJsonBase *result_array = nullptr;
    ObIJsonBase *j_base = nullptr;
    bool has_doc_key = expr.arg_cnt_ == 4 && OB_NOT_NULL(arg_doc_key) && !arg_doc_key->is_null();
    bool is_null_result = false;
    if (OB_FAIL(ObJsonExprHelper::get_json_doc(expr, ctx, temp_allocator, DOCUMENTS_IDX,
          j_base, is_null_result))) {
    } else if (OB_ISNULL(j_base)) {
      ret = OB_ERR_UNEXPECTED;
    } else if (j_base->json_type() != ObJsonNodeType::J_ARRAY) {
      ret = OB_INVALID_ARGUMENT;
      LOG_USER_ERROR(OB_INVALID_ARGUMENT, "ai_rerank, document_array is not array");
    } else if (OB_ISNULL(document_array = j_base)) {
      ret = OB_ERR_UNEXPECTED;
    } else if (document_array->element_count() == 0) {
      ret = OB_INVALID_ARGUMENT;
      LOG_USER_ERROR(OB_INVALID_ARGUMENT, "ai_rerank, document_array is empty");
    } else {
      for (int64_t i = 0; OB_SUCC(ret) && i < document_array->element_count(); i++) {
        ObIJsonBase *doc_node = ObAIFuncJsonUtils::get_value(document_array, i);
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
    } else if (OB_ISNULL(endpoint_resolver)) {
      ret = OB_ERR_UNEXPECTED;
    } else if (OB_FAIL(endpoint_resolver->resolve_by_model_name(
                   model_id, temp_allocator, resolved_endpoint))) {
    } 

    if (OB_FAIL(ret)) {
    } else if (OB_ISNULL(info)) {
      ret = OB_ERR_UNEXPECTED;
    } else if (OB_FAIL(ObAIFuncUtils::check_info_type_rerank(info))) {
    } else if (OB_FAIL(ObAIFuncUtils::get_header(temp_allocator, *info, *endpoint_info, header_array))) {
    } else {
      ObIJsonBase *rerank_document_array = document_array;
      ObIJsonBase *sorted_document_array = nullptr;
      ObString doc_key;
      if (has_doc_key) {
        doc_key = arg_doc_key->get_string();
        if (OB_FAIL(get_doc_array_from_documents_array_with_key(
                      temp_allocator, document_array, doc_key, rerank_document_array))) {
        }
      }
      if (OB_FAIL(ret)) {
      } else if (OB_FAIL(rerank_in_batches(temp_allocator, *info, *endpoint_info,
                                           header_array, query, rerank_document_array,
                                           result_array))) {
      } else if (has_doc_key
                 && OB_FAIL(sort_document_array_by_model_result(
                              temp_allocator, document_array, result_array,
                              sorted_document_array))) {
      } else {
        ObIJsonBase *output_array = has_doc_key ? sorted_document_array : result_array;
        ObString raw_str;
        if (OB_FAIL(output_array->get_raw_binary(raw_str, &temp_allocator))) {
        } else if (OB_FAIL(ObJsonExprHelper::pack_json_str_res(expr, ctx, res, raw_str))) {
        }
      }
    }
  }
  return ret;
}

int ObExprAIRerank::rerank_in_batches(ObIAllocator &allocator,
                                      const ObAIFuncExprInfo &info,
                                      const share::ObAiModelEndpointInfo &endpoint_info,
                                      ObArray<ObString> &header_array,
                                      ObString &query,
                                      ObIJsonBase *document_array,
                                      ObIJsonBase *&result_array)
{
  INIT_SUCC(ret);
  static const int64_t MAX_BATCH_SIZE = 20;
  ObIJsonBase *all_results = nullptr;
  if (OB_ISNULL(document_array)) {
    ret = OB_INVALID_ARGUMENT;
  } else if (OB_FAIL(ObAIFuncJsonUtils::get_json_array(allocator, all_results))) {
  } else {
    ObString score_key(SCORE_KEY);
    for (int64_t start_idx = 0;
         OB_SUCC(ret) && start_idx < document_array->element_count();
         start_idx += MAX_BATCH_SIZE) {
      const int64_t end_idx = std::min(start_idx + MAX_BATCH_SIZE,
                                       static_cast<int64_t>(document_array->element_count()));
      ObIJsonBase *batch_documents = nullptr;
      ObIJsonBase *raw_batch_results = nullptr;
      ObIJsonBase *normalized_batch_results = nullptr;
      ObIJsonBase *merged_results = nullptr;
      if (OB_FAIL(construct_batch_document_array(
                    allocator, document_array, start_idx, end_idx, batch_documents))) {
      } else if (OB_FAIL(inner_eval_ai_rerank(allocator, info, endpoint_info,
                                               header_array, query, batch_documents,
                                               raw_batch_results))) {
      } else if (OB_FAIL(normalize_rerank_result(
                           allocator, raw_batch_results, end_idx - start_idx,
                           start_idx, normalized_batch_results))) {
      } else if (OB_FAIL(compact_json_array_by_key(
                           allocator, all_results, normalized_batch_results,
                           score_key, merged_results))) {
      } else {
        all_results = merged_results;
      }
    }
  }
  if (OB_SUCC(ret)) {
    result_array = all_results;
  }
  return ret;
}

int ObExprAIRerank::normalize_rerank_result(ObIAllocator &allocator,
                                            ObIJsonBase *model_result_array,
                                            int64_t expected_count,
                                            int64_t index_base,
                                            ObIJsonBase *&normalized_result_array)
{
  INIT_SUCC(ret);
  ObArray<ObIJsonBase *> entries;
  bool *seen = nullptr;
  if (OB_ISNULL(model_result_array) || expected_count <= 0) {
    ret = OB_INVALID_ARGUMENT;
  } else if (model_result_array->json_type() != ObJsonNodeType::J_ARRAY
             || model_result_array->element_count() != expected_count) {
    ret = OB_INVALID_DATA;
  } else if (OB_ISNULL(seen = static_cast<bool *>(allocator.alloc(sizeof(bool) * expected_count)))) {
    ret = OB_ALLOCATE_MEMORY_FAILED;
  } else {
    MEMSET(seen, 0, sizeof(bool) * expected_count);
    for (int64_t i = 0; OB_SUCC(ret) && i < expected_count; ++i) {
      ObIJsonBase *entry = ObAIFuncJsonUtils::get_value(model_result_array, i);
      ObIJsonBase *index_node = nullptr;
      ObIJsonBase *score_node = nullptr;
      int64_t local_index = -1;
      double score = 0.0;
      if (OB_ISNULL(entry) || entry->json_type() != ObJsonNodeType::J_OBJECT) {
        ret = OB_INVALID_DATA;
      } else if (OB_ISNULL(index_node = ObAIFuncJsonUtils::get_value(entry, INDEX_KEY))
                 || (index_node->json_type() != ObJsonNodeType::J_INT
                     && index_node->json_type() != ObJsonNodeType::J_UINT)) {
        ret = OB_INVALID_DATA;
      } else if (index_node->json_type() == ObJsonNodeType::J_UINT
                 && index_node->get_uint() > static_cast<uint64_t>(INT64_MAX)) {
        ret = OB_INVALID_DATA;
      } else {
        local_index = index_node->json_type() == ObJsonNodeType::J_INT
                        ? index_node->get_int()
                        : static_cast<int64_t>(index_node->get_uint());
        score_node = ObAIFuncJsonUtils::get_value(entry, SCORE_KEY);
        if (local_index < 0 || local_index >= expected_count || seen[local_index]) {
          ret = OB_INVALID_DATA;
        } else if (OB_ISNULL(score_node)
                   || !share::ObJsonHelper::is_number_type(score_node)
                   || OB_FAIL(get_rerank_score(score_node, score))
                   || !std::isfinite(score)) {
          ret = OB_INVALID_DATA;
        } else {
          seen[local_index] = true;
          ObIJsonBase *new_entry = ObAIFuncJsonUtils::clone_json(allocator, entry);
          ObIJsonBase *new_index = nullptr;
          ObIJsonBase *new_score = nullptr;
          if (OB_ISNULL(new_entry)) {
            ret = OB_ALLOCATE_MEMORY_FAILED;
          } else if (OB_FAIL(ObAIFuncJsonUtils::get_json_int(
                               allocator, local_index + index_base, new_index))) {
          } else if (OB_FAIL(ObAIFuncJsonUtils::get_json_double(allocator, score, new_score))) {
          } else if (OB_FAIL(ObAIFuncJsonUtils::object_set(allocator, new_entry, INDEX_KEY, new_index))) {
          } else if (OB_FAIL(ObAIFuncJsonUtils::object_set(allocator, new_entry, SCORE_KEY, new_score))) {
          } else if (OB_FAIL(entries.push_back(new_entry))) {
          }
        }
      }
    }
  }
  if (OB_SUCC(ret)) {
    lib::ob_sort(entries.begin(), entries.end(), [](ObIJsonBase *lhs, ObIJsonBase *rhs) {
      ObIJsonBase *lhs_score = ObAIFuncJsonUtils::get_value(lhs, SCORE_KEY);
      ObIJsonBase *rhs_score = ObAIFuncJsonUtils::get_value(rhs, SCORE_KEY);
      ObIJsonBase *lhs_index = ObAIFuncJsonUtils::get_value(lhs, INDEX_KEY);
      ObIJsonBase *rhs_index = ObAIFuncJsonUtils::get_value(rhs, INDEX_KEY);
      return lhs_score->get_double() > rhs_score->get_double()
             || (lhs_score->get_double() == rhs_score->get_double()
                 && lhs_index->get_int() < rhs_index->get_int());
    });
    JsonContainerBuilder builder(false);
    ObIJsonBase *normalized = nullptr;
    if (OB_FAIL(ObAIFuncJsonUtils::get_json_array(allocator, normalized))) {
    } else {
      for (int64_t i = 0; OB_SUCC(ret) && i < entries.count(); ++i) {
        if (OB_FAIL(ObAIFuncJsonUtils::append_value(allocator, builder, entries.at(i)))) {
        }
      }
      if (OB_FAIL(ret)) {
      } else if (OB_FAIL(ObAIFuncJsonUtils::finish_array(allocator, builder, normalized))) {
      } else {
        normalized_result_array = normalized;
      }
    }
  }
  return ret;
}

int ObExprAIRerank::get_doc_array_from_documents_array_with_key(ObIAllocator &allocator, ObIJsonBase *document_object_array, ObString &doc_key, ObIJsonBase *&doc_array)
{
  INIT_SUCC(ret);
  JsonContainerBuilder builder(false);
  if (OB_ISNULL(document_object_array)) {
    ret = OB_ERR_UNEXPECTED;
  } else if (doc_key.empty()) {
    ret = OB_INVALID_ARGUMENT;
  } else {
    ObIJsonBase *doc_array_obj = nullptr;
    if (OB_FAIL(ObAIFuncJsonUtils::get_json_array(allocator, doc_array_obj))) {
    } else {
      int64_t count = document_object_array->element_count();
      ObIJsonBase *doc_node = nullptr;
      ObIJsonBase *doc_value_node = nullptr;
      ObIJsonBase *doc_obj = nullptr;
      for (int64_t i = 0; OB_SUCC(ret) && i < count; i++) {
        doc_node = ObAIFuncJsonUtils::get_value(document_object_array, i);
        if (OB_ISNULL(doc_node)) {
          ret = OB_INVALID_ARGUMENT;
          LOG_USER_ERROR (OB_INVALID_ARGUMENT, "ai_rerank, document_array element is null");
        } else if (doc_node->json_type() != ObJsonNodeType::J_OBJECT) {
          ret = OB_INVALID_ARGUMENT;
          LOG_USER_ERROR (OB_INVALID_ARGUMENT, "ai_rerank, document_array element is not object");
        } else {
          doc_obj = doc_node;
          doc_value_node = ObAIFuncJsonUtils::get_value(doc_obj, doc_key);
          if (OB_ISNULL(doc_value_node)) {
            ret = OB_INVALID_ARGUMENT;
            LOG_USER_ERROR (OB_INVALID_ARGUMENT, "ai_rerank, the value for given doc key is null");
          } else if (doc_value_node->json_type() != ObJsonNodeType::J_STRING) {
            ret = OB_INVALID_ARGUMENT;
            LOG_USER_ERROR (OB_INVALID_ARGUMENT, "ai_rerank, the value for given doc key is not string");
          } else if (OB_FAIL(ObAIFuncJsonUtils::append_value(allocator, builder, doc_value_node))) {
          }
        }
      }
    }
    if (OB_FAIL(ret)) {
    } else if (OB_FAIL(ObAIFuncJsonUtils::finish_array(allocator, builder, doc_array_obj))) {
    } else {
      doc_array = doc_array_obj;
    }
  }
  return ret;
}

int ObExprAIRerank::sort_document_array_by_model_result(ObIAllocator &allocator, ObIJsonBase *document_array, ObIJsonBase *model_result_array, ObIJsonBase *&sorted_document_array)
{
  INIT_SUCC(ret);
  JsonContainerBuilder builder(false);
  ObIJsonBase *sorted_document_array_obj = nullptr;
  if (OB_FAIL(ObAIFuncJsonUtils::get_json_array(allocator, sorted_document_array_obj))) {
  } else {
    int64_t count = model_result_array->element_count();
    for (int64_t i = 0; OB_SUCC(ret) && i < count; i++) {
      ObIJsonBase *model_result_obj = ObAIFuncJsonUtils::get_value(model_result_array, i);
      if (OB_ISNULL(model_result_obj)) {
        ret = OB_ERR_UNEXPECTED;
      } else {
        ObIJsonBase *index_value = ObAIFuncJsonUtils::get_value(model_result_obj, INDEX_KEY);
        ObIJsonBase *score_value = ObAIFuncJsonUtils::get_value(model_result_obj, SCORE_KEY);
        if (OB_ISNULL(index_value) || OB_ISNULL(score_value)) {
          ret = OB_ERR_UNEXPECTED;
        } else {
          int64_t index = index_value->get_int();
          ObIJsonBase *origin_obj = index >= 0 && index < document_array->element_count()
                                      ? ObAIFuncJsonUtils::get_value(document_array, index)
                                      : nullptr;
          ObIJsonBase *new_obj = nullptr;
          if (OB_ISNULL(origin_obj)) {
            ret = OB_ERR_UNEXPECTED;
          } else if (OB_ISNULL(new_obj = ObAIFuncJsonUtils::clone_json(allocator, origin_obj))) {
            ret = OB_ERR_UNEXPECTED;
          } else if (OB_FAIL(ObAIFuncJsonUtils::object_set(allocator, new_obj, MODEL_SCORE_KEY, score_value))) {
          } else if (OB_FAIL(ObAIFuncJsonUtils::append_value(allocator, builder, new_obj))) {
          }
        }
      }
    }
  }
  if (OB_FAIL(ret)) {
  } else if (OB_FAIL(ObAIFuncJsonUtils::finish_array(allocator, builder, sorted_document_array_obj))) {
  } else {
    sorted_document_array = sorted_document_array_obj;
  }
  return ret;
}

int ObExprAIRerank::construct_batch_document_array(ObIAllocator &allocator, ObIJsonBase *document_array, int64_t start_idx, int64_t end_idx, ObIJsonBase *&batch_document_array)
{
  INIT_SUCC(ret);
  JsonContainerBuilder builder(false);
  ObIJsonBase *result_array = nullptr;
  if (OB_FAIL(ObAIFuncJsonUtils::get_json_array(allocator, result_array))) {
  } else {
    for (int64_t i = start_idx; OB_SUCC(ret) && i < end_idx; i++) {
      if (OB_FAIL(ObAIFuncJsonUtils::append_value(allocator, builder, ObAIFuncJsonUtils::get_value(document_array, i)))) {
      }
    }
  }
  if (OB_FAIL(ret)) {
  } else if (OB_FAIL(ObAIFuncJsonUtils::finish_array(allocator, builder, result_array))) {
  } else {
    batch_document_array = result_array;
  }
  return ret;
}

int ObExprAIRerank::inner_eval_ai_rerank(ObIAllocator &allocator, 
                                          const ObAIFuncExprInfo &info, 
                                          const share::ObAiModelEndpointInfo &endpoint_info,
                                          ObArray<ObString> &header_array, 
                                          ObString &query, 
                                          ObIJsonBase *document_array,
                                          ObIJsonBase *&result_array)
{
  INIT_SUCC(ret);
  ObIJsonBase *res = nullptr;
  ObIJsonBase *config_json = nullptr;
  ObIJsonBase *body = nullptr;
  ObIJsonBase *http_response = nullptr;
  ObIJsonBase *response = nullptr;
  ObAIFuncClient ai_client;
  if (OB_FAIL(ObAIFuncUtils::get_rerank_body(allocator, info, endpoint_info, query, document_array, config_json, body))) {
  } else if (OB_FAIL(ai_client.send_post(allocator, endpoint_info.get_url(), header_array, body, http_response))) {
  } else if (OB_FAIL(ObAIFuncUtils::parse_rerank_output(allocator, endpoint_info, http_response, response))) {
  } else if (response->json_type() != ObJsonNodeType::J_ARRAY) {
    ret = OB_ERR_UNEXPECTED;
  } else if (OB_ISNULL(res = response)) {
    ret = OB_ERR_UNEXPECTED;
  } else {
    result_array = res;
  }
  return ret;
}

int ObExprAIRerank::compact_json_array_by_key(ObIAllocator &allocator, ObIJsonBase *array1, ObIJsonBase *array2, ObString &score_key, ObIJsonBase *&compact_array)
{
  INIT_SUCC(ret);
  JsonContainerBuilder builder(false);
  ObIJsonBase *result_array = nullptr;
  if (OB_FAIL(ObAIFuncJsonUtils::get_json_array(allocator, result_array))) {
  } else {
    int64_t count1 = array1->element_count();
    int64_t count2 = array2->element_count();
    int64_t i = 0;
    int64_t j = 0;
    while (OB_SUCC(ret) && i < count1 && j < count2) {
      ObIJsonBase *obj1 = ObAIFuncJsonUtils::get_value(array1, i);
      ObIJsonBase *obj2 = ObAIFuncJsonUtils::get_value(array2, j);
      if (OB_ISNULL(obj1) || OB_ISNULL(obj2)) {
        ret = OB_ERR_UNEXPECTED;
      } else {
        ObIJsonBase *double1 = ObAIFuncJsonUtils::get_value(obj1, score_key);
        ObIJsonBase *double2 = ObAIFuncJsonUtils::get_value(obj2, score_key);
        if (OB_ISNULL(double1) || OB_ISNULL(double2)) {
          ret = OB_ERR_UNEXPECTED;
        } else {
          ObIJsonBase *index1 = ObAIFuncJsonUtils::get_value(obj1, INDEX_KEY);
          ObIJsonBase *index2 = ObAIFuncJsonUtils::get_value(obj2, INDEX_KEY);
          if (OB_ISNULL(index1) || OB_ISNULL(index2)) {
            ret = OB_ERR_UNEXPECTED;
          } else if (double1->get_double() > double2->get_double()
                     || (double1->get_double() == double2->get_double()
                         && index1->get_int() < index2->get_int())) {
            if (OB_FAIL(ObAIFuncJsonUtils::append_value(allocator, builder, obj1))) {
            }
            i++;
          } else {
            if (OB_FAIL(ObAIFuncJsonUtils::append_value(allocator, builder, obj2))) {
            }
            j++;
          }
        }
      }
    }
    if (OB_SUCC(ret)) {
      while (OB_SUCC(ret) && i < count1) {
        if (OB_FAIL(ObAIFuncJsonUtils::append_value(allocator, builder, ObAIFuncJsonUtils::get_value(array1, i)))) {
        }
        i++;
      }
    }
    if (OB_SUCC(ret)) {
      while (OB_SUCC(ret) && j < count2) {
        if (OB_FAIL(ObAIFuncJsonUtils::append_value(allocator, builder, ObAIFuncJsonUtils::get_value(array2, j)))) {
        }
        j++;
      }
    }
  }
  if (OB_FAIL(ret)) {
  } else if (OB_FAIL(ObAIFuncJsonUtils::finish_array(allocator, builder, result_array))) {
  } else {
    compact_array = result_array;
  }
  return ret;
}

int ObExprAIRerank::construct_config_json(ObIAllocator &allocator, int64_t top_n, int64_t return_doc, ObIJsonBase *&config_json)
{
  INIT_SUCC(ret);
  ObIJsonBase *config_obj = nullptr;
  ObIJsonBase *top_n_obj = nullptr;
  ObIJsonBase *return_doc_obj = nullptr;
  bool return_doc_value = true;
  if (return_doc <= 0) {
    return_doc_value = false;
  }
  if (OB_FAIL(ObAIFuncJsonUtils::get_json_object(allocator, config_obj))) {
  } else if (OB_FAIL(ObAIFuncJsonUtils::get_json_boolean(allocator, return_doc_value, return_doc_obj))) {
  } else if (OB_FAIL(ObAIFuncJsonUtils::object_set(allocator, config_obj, "return_documents", return_doc_obj))) {
  } else if (top_n > 0) {
    if (OB_FAIL(ObAIFuncJsonUtils::get_json_int(allocator, top_n, top_n_obj))) {
    } else if (OB_FAIL(ObAIFuncJsonUtils::object_set(allocator, config_obj, "top_n", top_n_obj))) {
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
