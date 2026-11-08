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

namespace
{
// The number of documents that are sent to the model in one request.
constexpr int64_t RERANK_BATCH_SIZE = 20;

// The field of a result object that holds the position of the document the
// result belongs to.
const char *const RERANK_INDEX_KEY = "index";
// The field of a result object that holds the score of the document.
const char *const RERANK_SCORE_KEY = "relevance_score";
// Some providers call the score of a document "score".
const char *const RERANK_SCORE_KEY_ALT = "score";
// The field that is added to a document that carries its own score.
const char *const RERANK_MODEL_SCORE_KEY = "_model_score";

// One scored document.
struct ObRerankScoredDoc
{
  int64_t global_index_;     // the position of the document in the input array
  double score_;             // the score the model returned
  ObJsonNode *origin_node_;  // the document as the user wrote it
  ObJsonObject *result_obj_; // the result object the model returned
};

// Read a json number as a double. Every json number type is accepted, the
// value is converted instead of cast, a cast would read a wrong memory.
int get_json_number(ObJsonNode *node, double &value)
{
  int ret = OB_SUCCESS;
  if (OB_ISNULL(node)) {
    ret = OB_INVALID_DATA;
  } else {
    switch (node->json_type()) {
      case ObJsonNodeType::J_INT: {
        value = static_cast<double>(static_cast<ObJsonInt *>(node)->value());
        break;
      }
      case ObJsonNodeType::J_UINT: {
        value = static_cast<double>(static_cast<ObJsonUint *>(node)->value());
        break;
      }
      case ObJsonNodeType::J_DOUBLE: {
        value = static_cast<ObJsonDouble *>(node)->value();
        break;
      }
      default: {
        ret = OB_INVALID_DATA;
      }
    }
  }
  return ret;
}

// Read the index of a result object. The index addresses a document, so it
// has to be an integer.
int get_json_index(ObJsonNode *node, int64_t &value)
{
  int ret = OB_SUCCESS;
  if (OB_ISNULL(node)) {
    ret = OB_INVALID_DATA;
  } else if (node->json_type() == ObJsonNodeType::J_INT) {
    value = static_cast<ObJsonInt *>(node)->value();
  } else if (node->json_type() == ObJsonNodeType::J_UINT) {
    value = static_cast<int64_t>(static_cast<ObJsonUint *>(node)->value());
  } else {
    ret = OB_INVALID_DATA;
  }
  return ret;
}

// The score of a result object and the node that holds it.
int get_result_score(ObJsonObject *result_obj, double &score, ObJsonNode *&score_node)
{
  int ret = OB_SUCCESS;
  score_node = result_obj->get_value(RERANK_SCORE_KEY);
  if (OB_ISNULL(score_node)) {
    score_node = result_obj->get_value(RERANK_SCORE_KEY_ALT);
  }
  if (OB_FAIL(get_json_number(score_node, score))) {
    LOG_WARN("ai_rerank, the score of the result is not a number", K(ret));
  }
  return ret;
}

// The documents are ordered by their score, the document with the higher
// score comes first. Documents with the same score keep the order of the
// input array, that makes the result of the function reproducible.
bool compare_scored_docs(const ObRerankScoredDoc &left, const ObRerankScoredDoc &right)
{
  if (left.score_ != right.score_) {
    return left.score_ > right.score_;
  }
  return left.global_index_ < right.global_index_;
}

// Collect the documents to score. Every document keeps the position it has in
// the input array, that position is the global index of the document. Without
// a doc key the documents are plain strings, with a doc key they are objects
// and the field named by the doc key holds the text of the document.
int collect_rerank_documents(ObJsonArray *document_array,
                             ObString &doc_key,
                             bool has_doc_key,
                             ObArray<ObJsonNode *> &origin_nodes,
                             ObArray<ObString> &doc_texts)
{
  int ret = OB_SUCCESS;
  const int64_t doc_count = document_array->element_count();
  for (int64_t i = 0; OB_SUCC(ret) && i < doc_count; i++) {
    ObJsonNode *doc_node = document_array->get_value(i);
    ObString doc_text;
    if (OB_ISNULL(doc_node)) {
      ret = OB_ERR_UNEXPECTED;
    } else if (!has_doc_key) {
      if (doc_node->json_type() != ObJsonNodeType::J_STRING) {
        ret = OB_INVALID_ARGUMENT;
        LOG_USER_ERROR(OB_INVALID_ARGUMENT, "ai_rerank, document_array element is not string");
      } else {
        doc_text = static_cast<ObJsonString *>(doc_node)->value();
      }
    } else {
      ObJsonNode *text_node = nullptr;
      if (doc_node->json_type() != ObJsonNodeType::J_OBJECT) {
        ret = OB_INVALID_ARGUMENT;
        LOG_USER_ERROR(OB_INVALID_ARGUMENT, "ai_rerank, document_array element is not object");
      } else if (OB_ISNULL(text_node = static_cast<ObJsonObject *>(doc_node)->get_value(doc_key))) {
        ret = OB_INVALID_ARGUMENT;
        LOG_USER_ERROR(OB_INVALID_ARGUMENT, "ai_rerank, document_array element has no doc key");
      } else if (text_node->json_type() != ObJsonNodeType::J_STRING) {
        ret = OB_INVALID_ARGUMENT;
        LOG_USER_ERROR(OB_INVALID_ARGUMENT, "ai_rerank, the doc key of document_array element is not string");
      } else {
        doc_text = static_cast<ObJsonString *>(text_node)->value();
      }
    }
    if (OB_SUCC(ret)) {
      if (OB_FAIL(origin_nodes.push_back(doc_node))) {
      } else if (OB_FAIL(doc_texts.push_back(doc_text))) {
      }
    }
  }
  return ret;
}

// The documents of one request.
int construct_batch_document_array(ObIAllocator &allocator,
                                   ObArray<ObString> &doc_texts,
                                   int64_t start_idx,
                                   int64_t end_idx,
                                   ObJsonArray *&batch_document_array)
{
  int ret = OB_SUCCESS;
  ObJsonArray *result_array = nullptr;
  if (OB_FAIL(ObAIFuncJsonUtils::get_json_array(allocator, result_array))) {
  } else {
    for (int64_t i = start_idx; OB_SUCC(ret) && i < end_idx; i++) {
      ObJsonString *text_obj = nullptr;
      if (OB_FAIL(ObAIFuncJsonUtils::get_json_string(allocator, doc_texts[i], text_obj))) {
      } else if (OB_FAIL(result_array->append(text_obj))) {
      }
    }
  }
  if (OB_SUCC(ret)) {
    batch_document_array = result_array;
  }
  return ret;
}

// Send one batch of documents to the model and return its answer.
int eval_rerank_batch(ObIAllocator &allocator,
                      const ObAIFuncExprInfo &info,
                      const share::ObAiModelEndpointInfo &endpoint_info,
                      ObArray<ObString> &header_array,
                      ObString &query,
                      ObArray<ObString> &doc_texts,
                      int64_t start_idx,
                      int64_t end_idx,
                      ObJsonArray *&batch_result_array)
{
  int ret = OB_SUCCESS;
  ObJsonArray *batch_document_array = nullptr;
  ObJsonObject *body = nullptr;
  ObJsonObject *http_response = nullptr;
  ObIJsonBase *response = nullptr;
  ObAIFuncClient ai_client;
  if (OB_FAIL(construct_batch_document_array(allocator, doc_texts, start_idx, end_idx, batch_document_array))) {
  } else if (OB_FAIL(ObAIFuncUtils::get_rerank_body(allocator, info, endpoint_info, query, batch_document_array, nullptr, body))) {
  } else if (OB_FAIL(ai_client.send_post(allocator, endpoint_info.get_url(), header_array, body, http_response))) {
  } else if (OB_FAIL(ObAIFuncUtils::parse_rerank_output(allocator, endpoint_info, http_response, response))) {
  } else if (OB_ISNULL(response) || response->json_type() != ObJsonNodeType::J_ARRAY) {
    ret = OB_INVALID_DATA;
    LOG_USER_ERROR(OB_INVALID_DATA, "ai_rerank, the model did not answer with a result array");
  } else if (OB_ISNULL(batch_result_array = static_cast<ObJsonArray *>(response))) {
    ret = OB_ERR_UNEXPECTED;
  }
  return ret;
}

// Check the answer of the model and map the local index of every result to
// the global index of its document.
int collect_batch_scores(ObIAllocator &allocator,
                         ObJsonArray *batch_result_array,
                         int64_t start_idx,
                         int64_t batch_size,
                         ObArray<ObJsonNode *> &origin_nodes,
                         ObRerankScoredDoc *scored_docs,
                         int64_t &scored_count)
{
  int ret = OB_SUCCESS;
  const int64_t result_count = batch_result_array->element_count();
  if (result_count != batch_size) {
    // The model has to score every document of the batch.
    ret = OB_INVALID_DATA;
    LOG_USER_ERROR(OB_INVALID_DATA, "ai_rerank, the model returned %ld results for %ld documents",
                   result_count, batch_size);
  } else {
    // The model may answer in any order, but it must not repeat an index.
    char *seen = static_cast<char *>(allocator.alloc(batch_size));
    if (OB_ISNULL(seen)) {
      ret = OB_ALLOCATE_MEMORY_FAILED;
    } else {
      MEMSET(seen, 0, batch_size);
      for (int64_t i = 0; OB_SUCC(ret) && i < result_count; i++) {
        ObJsonNode *node = batch_result_array->get_value(i);
        if (OB_ISNULL(node) || node->json_type() != ObJsonNodeType::J_OBJECT) {
          ret = OB_INVALID_DATA;
          LOG_USER_ERROR(OB_INVALID_DATA, "ai_rerank, the result of the model is not an object");
        } else {
          ObJsonObject *result_obj = static_cast<ObJsonObject *>(node);
          int64_t local_index = 0;
          double score = 0.0;
          ObJsonNode *score_node = nullptr;
          if (OB_FAIL(get_json_index(result_obj->get_value(RERANK_INDEX_KEY), local_index))) {
            ret = OB_INVALID_DATA;
            LOG_USER_ERROR(OB_INVALID_DATA, "ai_rerank, the result of the model has no valid index");
          } else if (local_index < 0 || local_index >= batch_size) {
            ret = OB_INVALID_DATA;
            LOG_USER_ERROR(OB_INVALID_DATA, "ai_rerank, the index %ld of the result is out of range", local_index);
          } else if (seen[local_index]) {
            ret = OB_INVALID_DATA;
            LOG_USER_ERROR(OB_INVALID_DATA, "ai_rerank, the index %ld of the result is repeated", local_index);
          } else if (OB_FAIL(get_result_score(result_obj, score, score_node))) {
            ret = OB_INVALID_DATA;
            LOG_USER_ERROR(OB_INVALID_DATA, "ai_rerank, the result of the model has no valid score");
          } else {
            seen[local_index] = 1;
            ObRerankScoredDoc &scored_doc = scored_docs[scored_count++];
            scored_doc.global_index_ = start_idx + local_index;
            scored_doc.score_ = score;
            scored_doc.origin_node_ = origin_nodes[scored_doc.global_index_];
            scored_doc.result_obj_ = result_obj;
          }
        }
      }
    }
  }
  return ret;
}

// Score every document. The documents are sent to the model in batches, the
// answer of every batch is mapped back to the documents of the input array.
int score_documents(ObIAllocator &allocator,
                    const ObAIFuncExprInfo &info,
                    const share::ObAiModelEndpointInfo &endpoint_info,
                    ObArray<ObString> &header_array,
                    ObString &query,
                    ObArray<ObString> &doc_texts,
                    ObArray<ObJsonNode *> &origin_nodes,
                    ObRerankScoredDoc *&scored_docs,
                    int64_t &scored_count)
{
  int ret = OB_SUCCESS;
  const int64_t doc_count = doc_texts.count();
  scored_docs = static_cast<ObRerankScoredDoc *>(allocator.alloc(sizeof(ObRerankScoredDoc) * doc_count));
  scored_count = 0;
  if (OB_ISNULL(scored_docs)) {
    ret = OB_ALLOCATE_MEMORY_FAILED;
  } else {
    for (int64_t start_idx = 0; OB_SUCC(ret) && start_idx < doc_count; start_idx += RERANK_BATCH_SIZE) {
      const int64_t end_idx = std::min(start_idx + RERANK_BATCH_SIZE, doc_count);
      ObJsonArray *batch_result_array = nullptr;
      if (OB_FAIL(eval_rerank_batch(allocator, info, endpoint_info, header_array, query,
                                   doc_texts, start_idx, end_idx, batch_result_array))) {
      } else if (OB_FAIL(collect_batch_scores(allocator, batch_result_array, start_idx, end_idx - start_idx,
                                              origin_nodes, scored_docs, scored_count))) {
      }
    }
    if (OB_SUCC(ret) && scored_count != doc_count) {
      // Every document has to be scored exactly once.
      ret = OB_INVALID_DATA;
      LOG_USER_ERROR(OB_INVALID_DATA, "ai_rerank, the model did not score every document");
    }
    if (OB_SUCC(ret)) {
      // The model may answer in any order, the documents are sorted by their
      // score.
      std::sort(scored_docs, scored_docs + scored_count, compare_scored_docs);
    }
  }
  return ret;
}

// Build the result of the function. Without a doc key the result holds the
// result objects of the model, with a doc key it holds the documents of the
// input array, every document carries its score.
int build_result_array(ObIAllocator &allocator,
                        bool has_doc_key,
                        ObRerankScoredDoc *scored_docs,
                        int64_t scored_count,
                        ObJsonArray *&result_array)
{
  int ret = OB_SUCCESS;
  ObJsonArray *array = nullptr;
  if (OB_FAIL(ObAIFuncJsonUtils::get_json_array(allocator, array))) {
  } else {
    for (int64_t i = 0; OB_SUCC(ret) && i < scored_count; i++) {
      ObRerankScoredDoc &scored_doc = scored_docs[i];
      if (has_doc_key) {
        // The document is returned as the user wrote it, with its score.
        ObJsonObject *origin_obj = static_cast<ObJsonObject *>(scored_doc.origin_node_);
        ObJsonObject *new_obj = nullptr;
        if (OB_ISNULL(origin_obj)) {
          ret = OB_ERR_UNEXPECTED;
        } else if (OB_ISNULL(new_obj = static_cast<ObJsonObject *>(origin_obj->clone(&allocator, true)))) {
          ret = OB_ERR_UNEXPECTED;
        } else {
          ObJsonDouble *score_obj = OB_NEWx(ObJsonDouble, &allocator, scored_doc.score_);
          if (OB_ISNULL(score_obj)) {
            ret = OB_ALLOCATE_MEMORY_FAILED;
          } else if (OB_FAIL(new_obj->add(RERANK_MODEL_SCORE_KEY, score_obj))) {
          } else if (OB_FAIL(array->append(new_obj))) {
          }
        }
      } else {
        // The result of the model, the index tells which document it belongs
        // to.
        ObJsonInt *index_obj = OB_NEWx(ObJsonInt, &allocator, scored_doc.global_index_);
        if (OB_ISNULL(index_obj)) {
          ret = OB_ALLOCATE_MEMORY_FAILED;
        } else if (OB_FAIL(scored_doc.result_obj_->add(RERANK_INDEX_KEY, index_obj))) {
        } else if (OB_FAIL(array->append(scored_doc.result_obj_))) {
        }
      }
    }
  }
  if (OB_SUCC(ret)) {
    result_array = array;
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
    ObJsonArray *document_array = nullptr;
    ObJsonArray *result_array = nullptr;
    ObIJsonBase *j_base = nullptr;
    bool is_null_result = false;
    // A doc key that is passed as null selects the plain string documents.
    bool has_doc_key = OB_NOT_NULL(arg_doc_key) && !arg_doc_key->is_null();
    ObString doc_key;
    if (has_doc_key) {
      doc_key = arg_doc_key->get_string();
    }
    if (OB_FAIL(ObJsonExprHelper::get_json_doc(expr, ctx, temp_allocator, DOCUMENTS_IDX,
          j_base, is_null_result))) {
    } else if (OB_ISNULL(j_base)) {
      ret = OB_ERR_UNEXPECTED;
    } else if (j_base->json_type() != ObJsonNodeType::J_ARRAY) {
      ret = OB_INVALID_ARGUMENT;
      LOG_USER_ERROR(OB_INVALID_ARGUMENT, "ai_rerank, document_array is not array");
    } else if (OB_ISNULL(document_array = static_cast<ObJsonArray *>(j_base))) {
      ret = OB_ERR_UNEXPECTED;
    } else if (document_array->element_count() == 0) {
      ret = OB_INVALID_ARGUMENT;
      LOG_USER_ERROR(OB_INVALID_ARGUMENT, "ai_rerank, document_array is empty");
    } else if (has_doc_key && doc_key.empty()) {
      ret = OB_INVALID_ARGUMENT;
      LOG_USER_ERROR(OB_INVALID_ARGUMENT, "ai_rerank, doc key is empty");
    } else if (OB_FAIL(ObAIFuncUtils::get_ai_func_info(temp_allocator, model_id, info))) {
    } else if (OB_ISNULL(endpoint_resolver)) {
      ret = OB_ERR_UNEXPECTED;
    } else if (OB_FAIL(endpoint_resolver->resolve_by_model_name(
                   model_id, temp_allocator, resolved_endpoint))) {
    } else if (OB_ISNULL(info)) {
      ret = OB_ERR_UNEXPECTED;
    } else if (OB_FAIL(ObAIFuncUtils::check_info_type_rerank(info))) {
    } else if (OB_FAIL(ObAIFuncUtils::get_header(temp_allocator, *info, *endpoint_info, header_array))) {
    } else {
      ObArray<ObJsonNode *> origin_nodes;
      ObArray<ObString> doc_texts;
      ObRerankScoredDoc *scored_docs = nullptr;
      int64_t scored_count = 0;
      if (OB_FAIL(collect_rerank_documents(document_array, doc_key, has_doc_key,
                                           origin_nodes, doc_texts))) {
      } else if (OB_FAIL(score_documents(temp_allocator, *info, *endpoint_info, header_array, query,
                                         doc_texts, origin_nodes, scored_docs, scored_count))) {
      } else if (OB_FAIL(build_result_array(temp_allocator, has_doc_key, scored_docs, scored_count, result_array))) {
      }
    }
    if (OB_SUCC(ret)) {
      ObString raw_str;
      if (OB_FAIL(result_array->get_raw_binary(raw_str, &temp_allocator))) {
      } else if (OB_FAIL(ObJsonExprHelper::pack_json_str_res(expr, ctx, res, raw_str))) {
      }
    }
  }
  return ret;
}

int ObExprAIRerank::cg_expr(ObExprCGCtx &expr_cg_ctx, 
                        const ObRawExpr &raw_expr,
                        ObExpr &rt_expr) const 
{
  int ret = OB_SUCCESS;
  // TODO: support schema version match in plan cache for ai func
  if (OB_SUCC(ret)) {
    rt_expr.eval_func_ = ObExprAIRerank::eval_ai_rerank;
  }
  return ret;
}

} // namespace sql
} // namespace oceanbase
