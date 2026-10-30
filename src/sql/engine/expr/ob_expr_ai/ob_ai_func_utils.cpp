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
#include "ob_ai_func_utils.h"
#include "ob_ai_func_client.h"
#include "query/engine/expr/ob_ai_model_resolver.h"
#include "lib/string/ob_string_buffer.h"

#include <algorithm>
#include <climits>
#include <cmath>
#include <cstdlib>

namespace oceanbase 
{
namespace common 
{

int ObOpenAIUtils::get_header(common::ObIAllocator &allocator,
                              ObString &api_key,
                              common::ObArray<ObString> &headers) 
{
  int ret = OB_SUCCESS;
  if (api_key.empty()) {
    ret = OB_INVALID_ARGUMENT;
  } else {
    // ["Authorization: Bearer <api key>", "Content-Type: application/json"]
    // The buffer of the authorization header is sized by the length of the
    // api key: a fixed buffer truncates a long key.
    const char *auth_prefix = "Authorization: Bearer ";
    const int64_t auth_prefix_len = strlen(auth_prefix);
    const int64_t auth_header_len = auth_prefix_len + api_key.length();
    char *auth_header_str = static_cast<char *>(allocator.alloc(auth_header_len + 1));
    ObString content_type_str("Content-Type: application/json");
    ObString content_type_c_str;
    if (OB_FAIL(ob_write_string(allocator, content_type_str, content_type_c_str, true))) {
      LOG_WARN("failed to write the content type header", K(ret));
    } else if (OB_ISNULL(auth_header_str)) {
      ret = OB_ALLOCATE_MEMORY_FAILED;
      LOG_WARN("failed to allocate memory for the auth header", K(ret));
    } else {
      MEMCPY(auth_header_str, auth_prefix, auth_prefix_len);
      MEMCPY(auth_header_str + auth_prefix_len, api_key.ptr(), api_key.length());
      auth_header_str[auth_header_len] = '\0';
      if (OB_FAIL(headers.push_back(ObString(static_cast<int32_t>(auth_header_len), auth_header_str)))) {
        LOG_WARN("failed to push the auth header", K(ret));
      } else if (OB_FAIL(headers.push_back(content_type_c_str))) {
        LOG_WARN("failed to push the content type header", K(ret));
      }
    }
  }
  return ret;
}
int ObOpenAIUtils::ObOpenAIComplete::get_header(common::ObIAllocator &allocator,
                                                ObString &api_key,
                                                common::ObArray<ObString> &headers) 
{
  return ObOpenAIUtils::get_header(allocator, api_key, headers);
}

int ObOpenAIUtils::ObOpenAIComplete::get_body(common::ObIAllocator &allocator,
                                              common::ObString &model,
                                              common::ObString &prompt,
                                              common::ObString &content,
                                              common::ObJsonObject *config,
                                              common::ObJsonObject *&body) 
{
  int ret = OB_SUCCESS;
  if (model.empty() || content.empty()) {
    ret = OB_INVALID_ARGUMENT;
  } else {
    // {"model": "*", "messages": [{"role": "system", "content": "*"}, {"role": "user", "content": "*"}]}
    ObJsonObject *body_obj = nullptr;
    ObJsonString *model_str = nullptr;
    ObJsonArray *messages_array = nullptr;
    if (OB_FAIL(ObAIFuncJsonUtils::get_json_object(allocator, body_obj))) {
    } else if (OB_FAIL(ObAIFuncJsonUtils::get_json_string(allocator, model, model_str))) {
    } else if (OB_FAIL(body_obj->add("model", model_str))) {
    } else if (OB_FAIL(construct_messages_array(allocator, prompt, content, messages_array))) {
    } else if (OB_FAIL(body_obj->add("messages", messages_array))) {
    } else if (OB_FAIL(ObAIFuncJsonUtils::compact_json_object(allocator, config, body_obj))) {
    } else {
      body = body_obj;
    }
  }
  return ret;
}

int ObOpenAIUtils::ObOpenAIComplete::construct_messages_array(ObIAllocator &allocator, ObString &prompt, ObString &content, ObJsonArray *&messages)
{
  int ret = OB_SUCCESS;
  if (content.empty()) {
    ret = OB_INVALID_ARGUMENT;
  } else {
    //messages: [{"role": "system", "content": "You are a helpful assistant."}, {"role": "user", "content": "What is the capital of France?"}]
    ObJsonArray *messages_array = nullptr;
    ObJsonObject *sys_message_obj = nullptr;
    ObJsonObject *user_message_obj = nullptr;
    ObString system_str("system");
    ObString user_str("user");
    if (OB_FAIL(ObAIFuncJsonUtils::get_json_array(allocator, messages_array))) {
    } else if(!prompt.empty()) {
      if (OB_FAIL(construct_message_obj(allocator, system_str, prompt, sys_message_obj))) {
      } else if (OB_FAIL(messages_array->append(sys_message_obj))) {
      }
    } 
    if (OB_SUCC(ret)) {
      if (OB_FAIL(construct_message_obj(allocator, user_str, content, user_message_obj))) {
      } else if (OB_FAIL(messages_array->append(user_message_obj))) {
      } else {
        messages = messages_array;
      }
    }
  }
  return ret;
}

int ObOpenAIUtils::ObOpenAIComplete::construct_message_obj(ObIAllocator &allocator, ObString &role, ObString &content, ObJsonObject *&message)
{
  int ret = OB_SUCCESS;
  if (role.empty() || content.empty()) {
    ret = OB_INVALID_ARGUMENT;
  } else {
    ObJsonObject *message_obj = nullptr;
    ObJsonString *role_json_str = nullptr;
    ObJsonString *content_json_str = nullptr;
    if (OB_FAIL(ObAIFuncJsonUtils::get_json_object(allocator, message_obj))) {
    } else if (OB_FAIL(ObAIFuncJsonUtils::get_json_string(allocator, role, role_json_str))) {
    } else if (OB_FAIL(message_obj->add("role", role_json_str))) {
    } else if (OB_FAIL(ObAIFuncJsonUtils::get_json_string(allocator, content, content_json_str))) {
    } else if (OB_FAIL(message_obj->add("content", content_json_str))) {
    } else {
      message = message_obj;
    }
  }
  return ret;
}

int ObOpenAIUtils::ObOpenAIComplete::set_config_json_format(common::ObIAllocator &allocator, common::ObJsonObject *config)
{
  int ret = OB_SUCCESS;
  if (OB_ISNULL(config)) {
    ret = OB_INVALID_ARGUMENT;
  } else {
    // {"response_format":{"type":"json_object"}}
    ObString json_str("json_object");
    ObJsonString *json_str_obj = nullptr;
    ObJsonObject *json_obj = nullptr;
    if (OB_FAIL(ObAIFuncJsonUtils::get_json_object(allocator, json_obj))) {
    } else if (OB_FAIL(ObAIFuncJsonUtils::get_json_string(allocator, json_str, json_str_obj))) {
    } else if (OB_FAIL(json_obj->add("type", json_str_obj))) {
    } else if (OB_FAIL(config->add("response_format", json_obj))) {
    }
  }
  return ret;
}

int ObOpenAIUtils::ObOpenAIComplete::parse_output(common::ObIAllocator &allocator, 
                                                  common::ObJsonObject *http_response,
                                                  common::ObIJsonBase *&result)
{
  int ret = OB_SUCCESS;
  if (OB_ISNULL(http_response)) {
    ret = OB_INVALID_ARGUMENT;
    LOG_WARN("http_response is null", K(ret));
  } else {
    ObIJsonBase *j_tree = http_response;
    common::ObString path_text("$.choices[0].message.content");
    ObJsonPath j_path(path_text, &allocator);
    ObJsonSeekResult hit;
    if (OB_FAIL(j_path.parse_path())) {
      LOG_WARN("failed to parse the path of the content", K(ret));
    } else if (OB_FAIL(j_tree->seek(j_path, j_path.path_node_cnt(), false, false, hit))) {
      LOG_WARN("failed to seek the content of the answer", K(ret));
    } else if (0 == hit.size()) {
      ret = OB_INVALID_DATA;
      LOG_WARN("the answer of the model holds no choice", K(ret));
    } else if (OB_ISNULL(hit[0]) || ObJsonNodeType::J_STRING != hit[0]->json_type()
               || 0 == hit[0]->get_data_length()) {
      // the content of the answer must be a not empty string
      ret = OB_INVALID_DATA;
      LOG_WARN("the content of the answer is not a not empty string", K(ret));
    } else {
      result = hit[0];
    }
  }
  return ret;
}

int ObOpenAIUtils::ObOpenAIEmbed::get_header(common::ObIAllocator &allocator,
                                             ObString &api_key,
                                             common::ObArray<ObString> &headers) 
{
  int ret = OB_SUCCESS;
  if (OB_FAIL(ObOpenAIUtils::get_header(allocator, api_key, headers))) {
  }
  return ret;
}

int ObOpenAIUtils::ObOpenAIEmbed::get_body(common::ObIAllocator &allocator,
                                           common::ObString &model,
                                           common::ObArray<ObString> &contents,
                                           common::ObJsonObject *config,
                                           common::ObJsonObject *&body) 
{
  int ret = OB_SUCCESS;
  if (model.empty() || contents.empty()) {
    ret = OB_INVALID_ARGUMENT;
  } else {
    ObJsonObject *body_obj = nullptr;
    ObJsonString *model_str = nullptr;
    ObJsonArray *input_array = nullptr;
    if (OB_FAIL(ObAIFuncJsonUtils::get_json_object(allocator, body_obj))) {
    } else if (OB_FAIL(ObAIFuncJsonUtils::get_json_string(allocator, model, model_str))) {
    } else if (OB_FAIL(body_obj->add("model", model_str))) {
    } else if (OB_FAIL(ObAIFuncJsonUtils::transform_array_to_json_array(allocator, contents, input_array))) {
    } else if (OB_FAIL(body_obj->add("input", input_array))) {
    } else if (OB_FAIL(ObAIFuncJsonUtils::compact_json_object(allocator, config, body_obj))) {
    } else {
      body = body_obj;
    }
  }
  return ret;
}

// Build the embeddings of the answer of a model in the order of the inputs.
// Every item of the answer must carry the index of the input that it belongs
// to: the model may answer with the results in another order than the order
// of the inputs.
static int build_ordered_embeddings(ObIAllocator &allocator,
                                    ObJsonArray *items,
                                    ObJsonArray *&ordered_embeddings)
{
  int ret = OB_SUCCESS;
  ordered_embeddings = nullptr;
  ObJsonArray *result_array = nullptr;
  if (OB_ISNULL(items) || 0 >= items->element_count()) {
    // the model must answer with one embedding for every input
    ret = OB_INVALID_DATA;
    LOG_WARN("the answer holds no embedding", K(ret));
  } else {
    const int64_t item_count = items->element_count();
    // the embedding of the input with the index i is kept in the i-th slot
    ObArray<ObJsonNode *> embeddings;
    for (int64_t i = 0; OB_SUCC(ret) && i < item_count; i++) {
      ret = embeddings.push_back(nullptr);
    }
    for (int64_t i = 0; OB_SUCC(ret) && i < item_count; i++) {
      ObJsonNode *item_node = items->get_value(i);
      if (OB_ISNULL(item_node) || ObJsonNodeType::J_OBJECT != item_node->json_type()) {
        ret = OB_INVALID_DATA;
        LOG_WARN("the item of the answer is not an object", K(ret), K(i));
      } else {
        ObJsonObject *item_obj = static_cast<ObJsonObject *>(item_node);
        ObJsonNode *index_node = item_obj->get_value("index");
        ObJsonNode *embedding_node = item_obj->get_value("embedding");
        int64_t index = -1;
        if (OB_NOT_NULL(index_node) && ObJsonNodeType::J_INT == index_node->json_type()) {
          index = static_cast<ObJsonInt *>(index_node)->get_int();
        } else if (OB_NOT_NULL(index_node) && ObJsonNodeType::J_UINT == index_node->json_type()
                   && static_cast<ObJsonUint *>(index_node)->get_uint() < static_cast<uint64_t>(item_count)) {
          index = static_cast<int64_t>(static_cast<ObJsonUint *>(index_node)->get_uint());
        }
        if (index < 0 || index >= item_count || OB_ISNULL(embedding_node)
            || OB_NOT_NULL(embeddings.at(index))) {
          // the index is missing, duplicated or out of range
          ret = OB_INVALID_DATA;
          LOG_WARN("the index or the embedding of the answer is invalid", K(ret), K(i), K(index));
        } else {
          embeddings.at(index) = embedding_node;
        }
      }
    }
    if (OB_SUCC(ret) && OB_FAIL(ObAIFuncJsonUtils::get_json_array(allocator, result_array))) {
      LOG_WARN("failed to allocate the embeddings of the answer", K(ret));
    }
    for (int64_t i = 0; OB_SUCC(ret) && i < item_count; i++) {
      if (OB_ISNULL(embeddings.at(i))) {
        ret = OB_INVALID_DATA;
        LOG_WARN("the embedding of an input is missing", K(ret), K(i));
      } else if (OB_FAIL(result_array->append(embeddings.at(i)))) {
        LOG_WARN("failed to append the embedding of the answer", K(ret), K(i));
      }
    }
    if (OB_SUCC(ret) && OB_FAIL(ObAIFuncUtils::validate_embedding_result(allocator, result_array))) {
      LOG_WARN("the embeddings of the answer is invalid", K(ret));
    } else if (OB_SUCC(ret)) {
      ordered_embeddings = result_array;
    }
  }
  return ret;
}

int ObOpenAIUtils::ObOpenAIEmbed::parse_output(common::ObIAllocator &allocator,
                                               common::ObJsonObject *http_response,
                                               common::ObIJsonBase *&result)
{
  int ret = OB_SUCCESS;
  result = nullptr;
  ObJsonNode *data_node = nullptr;
  ObJsonArray *embeddings = nullptr;
  if (OB_ISNULL(http_response) || OB_ISNULL(data_node = http_response->get_value("data"))
      || ObJsonNodeType::J_ARRAY != data_node->json_type()
      || 0 >= data_node->element_count()) {
    ret = OB_INVALID_DATA;
    LOG_WARN("the data of the answer is not a not empty array", K(ret));
  } else if (OB_FAIL(build_ordered_embeddings(allocator,
                                              static_cast<ObJsonArray *>(data_node),
                                              embeddings))) {
  } else {
    result = embeddings;
  }
  return ret;
}

int ObOllamaUtils::get_header(common::ObIAllocator &allocator,
                              common::ObArray<ObString> &headers) 
{
  int ret = OB_SUCCESS;
  // ollama does not authenticate the request, but the http client refuses an
  // empty header list, so only the content type is sent.
  ObString content_type_str("Content-Type: application/json");
  ObString content_type_c_str;
  if (OB_FAIL(ob_write_string(allocator, content_type_str, content_type_c_str, true))) {
    LOG_WARN("failed to write the content type header", K(ret));
  } else if (OB_FAIL(headers.push_back(content_type_c_str))) {
    LOG_WARN("failed to push the content type header", K(ret));
  }
  return ret;
}

int ObOllamaUtils::ObOllamaComplete::get_header(common::ObIAllocator &allocator,
                                                common::ObString &api_key,
                                                common::ObArray<ObString> &headers) 
{
  return ObOllamaUtils::get_header(allocator, headers);
}

int ObOllamaUtils::ObOllamaComplete::get_body(ObIAllocator &allocator,
    ObString &model, ObString &prompt, ObString &content, ObJsonObject *config, ObJsonObject *&body)
{
  int ret = OB_SUCCESS;
  ObJsonObject *object = nullptr;
  ObJsonObject *options = nullptr;
  ObJsonString *model_node = nullptr;
  ObJsonString *content_node = nullptr;
  ObJsonBoolean *stream = nullptr;
  if (model.empty() || content.empty()) {
    ret = OB_INVALID_ARGUMENT;
    LOG_WARN("the model or the content of the completion is empty", K(ret));
  } else if (OB_FAIL(ObAIFuncJsonUtils::get_json_object(allocator, object))) {
  } else if (OB_FAIL(ObAIFuncJsonUtils::get_json_string(allocator, model, model_node))) {
  } else if (OB_FAIL(object->add("model", model_node))) {
  } else if (OB_FAIL(ObAIFuncJsonUtils::get_json_string(allocator, content, content_node))) {
  } else if (OB_FAIL(object->add("prompt", content_node))) {
  } else if (OB_FAIL(ObAIFuncJsonUtils::get_json_object(allocator, options))) {
  } else if (OB_NOT_NULL(config)) {
    // The native protocol of ollama takes the parameters of the generation in
    // an options object instead of the top level of the request. The
    // parameters of the openai protocol are translated, the other parameters
    // are copied as they are.
    for (int64_t i = 0; OB_SUCC(ret) && i < config->element_count(); i++) {
      ObString key;
      ObJsonNode *value = config->get_value(i);
      if (OB_FAIL(config->get_key(i, key))) {
      } else if (key.case_compare("options") == 0) {
        // the options of the native protocol may be given directly
        if (OB_ISNULL(value) || ObJsonNodeType::J_OBJECT != value->json_type()) {
          ret = OB_INVALID_ARGUMENT;
          LOG_WARN("the options of the config is not an object", K(ret));
        } else {
          ret = ObAIFuncJsonUtils::compact_json_object(allocator, static_cast<ObJsonObject *>(value), options);
        }
      } else if (key.case_compare("format") == 0 || key.case_compare("keep_alive") == 0) {
        // the format and the keep alive are parameters of the top level
        ret = object->add(key, value);
      } else if (key.case_compare("stream") != 0 && key.case_compare("model") != 0
                 && key.case_compare("prompt") != 0 && key.case_compare("system") != 0) {
        // ollama calls the maximum number of the generated tokens num_predict
        ret = options->add(key.case_compare("max_tokens") == 0 ? ObString("num_predict") : key, value);
      }
    }
  }
  if (OB_SUCC(ret) && !prompt.empty()) {
    ObJsonString *system = nullptr;
    if (OB_FAIL(ObAIFuncJsonUtils::get_json_string(allocator, prompt, system))) {
    } else if (OB_FAIL(object->add("system", system))) {
    }
  }
  if (OB_SUCC(ret)) {
    if (OB_FAIL(object->add("options", options))) {
    } else if (OB_FAIL(ObAIFuncJsonUtils::get_json_boolean(allocator, false, stream))) {
      // the answer of ollama is streamed by default, the whole answer must be
      // returned to parse it as one json tree
    } else if (OB_FAIL(object->add("stream", stream))) {
      // the stream is forced to false, the config cannot override it
    } else {
      body = object;
    }
  }
  return ret;
}

int ObOllamaUtils::ObOllamaComplete::set_config_json_format(common::ObIAllocator &allocator, common::ObJsonObject *config)
{
  int ret = OB_SUCCESS;
  if (OB_ISNULL(config)) {
    ret = OB_INVALID_ARGUMENT;
  } else {
    // {"format": "json"}
    ObString json_str("json");
    ObJsonString *json_str_obj = nullptr;
    if (OB_FAIL(ObAIFuncJsonUtils::get_json_string(allocator, json_str, json_str_obj))) {
    } else if (OB_FAIL(config->add("format", json_str_obj))) {
    } 
  }
  return ret;
}

int ObOllamaUtils::ObOllamaComplete::parse_output(common::ObIAllocator &allocator,
                                                  common::ObJsonObject *http_response,
                                                  common::ObIJsonBase *&result)
{
  int ret = OB_SUCCESS;
  if (OB_ISNULL(http_response)) {
    ret = OB_INVALID_ARGUMENT;
    LOG_WARN("http_response is null", K(ret));
  } else {
    ObIJsonBase *j_tree = http_response;
    common::ObString path_text("$.response");
    ObJsonPath j_path(path_text, &allocator);
    ObJsonSeekResult hit;
    if (OB_FAIL(j_path.parse_path())) {
    } else if (OB_FAIL(j_tree->seek(j_path, j_path.path_node_cnt(), false, false, hit))) {
    } else if (0 == hit.size()) {
      ret = OB_INVALID_DATA;
      LOG_WARN("the answer of the model holds no response", K(ret));
    } else if (OB_ISNULL(hit[0]) || ObJsonNodeType::J_STRING != hit[0]->json_type()
               || 0 == hit[0]->get_data_length()) {
      // the response of the model must be a not empty string
      ret = OB_INVALID_DATA;
      LOG_WARN("the response of the model is not a not empty string", K(ret));
    } else {
      result = hit[0];
    }
  }
  return ret;
}

int ObOllamaUtils::ObOllamaEmbed::get_header(common::ObIAllocator &allocator,
                                             ObString &api_key,
                                             common::ObArray<ObString> &headers) 
{
  return ObOllamaUtils::get_header(allocator, headers);
}

int ObOllamaUtils::ObOllamaEmbed::get_body(common::ObIAllocator &allocator,
                                           common::ObString &model,
                                           common::ObArray<ObString> &contents,
                                           common::ObJsonObject *config,
                                           common::ObJsonObject *&body) 
{
  int ret = OB_SUCCESS;
  if (model.empty() || contents.empty()) {
    ret = OB_INVALID_ARGUMENT;
  } else {
    ObJsonObject *body_obj = nullptr;
    ObJsonString *model_str = nullptr;
    ObJsonArray *input_array = nullptr;
    if (OB_FAIL(ObAIFuncJsonUtils::get_json_object(allocator, body_obj))) {
    } else if (OB_FAIL(ObAIFuncJsonUtils::get_json_string(allocator, model, model_str))) {
    } else if (OB_FAIL(body_obj->add("model", model_str))) {
    } else if (OB_FAIL(ObAIFuncJsonUtils::transform_array_to_json_array(allocator, contents, input_array))) {
    } else if (OB_FAIL(body_obj->add("input", input_array))) {
    } else {
      body = body_obj;
    }
    if (OB_SUCC(ret)) {
      if (OB_FAIL(ObAIFuncJsonUtils::compact_json_object(allocator, config, body))) {
      }
    }
  }
  return ret;
}

int ObOllamaUtils::ObOllamaEmbed::parse_output(common::ObIAllocator &allocator,
                                               common::ObJsonObject *http_response,
                                               common::ObIJsonBase *&result)
{
  int ret = OB_SUCCESS;
  result = nullptr;
  ObJsonNode *embeddings = nullptr;
  if (OB_ISNULL(http_response) || OB_ISNULL(embeddings = http_response->get_value("embeddings"))) {
    ret = OB_INVALID_DATA;
    LOG_WARN("the answer of the model holds no embeddings", K(ret));
  } else if (OB_FAIL(ObAIFuncUtils::validate_embedding_result(allocator, embeddings))) {
    LOG_WARN("the embeddings of the answer is invalid", K(ret));
  } else {
    result = embeddings;
  }
  return ret;
}

int ObDashscopeUtils::get_header(common::ObIAllocator &allocator,
                                 common::ObString &api_key,
                                 common::ObArray<ObString> &headers) 
{
  int ret = OB_SUCCESS;
  if (OB_FAIL(ObOpenAIUtils::get_header(allocator, api_key, headers))) {
  }
  return ret;
}

int ObDashscopeUtils::ObDashscopeComplete::get_header(common::ObIAllocator &allocator, 
                                                      common::ObString &api_key,
                                                      common::ObArray<ObString> &headers) 
{
  return ObOpenAIUtils::get_header(allocator, api_key, headers);
}

int ObDashscopeUtils::ObDashscopeComplete::get_body(common::ObIAllocator &allocator,
                                                    common::ObString &model,
                                                    common::ObString &prompt,
                                                    common::ObString &content,
                                                    common::ObJsonObject *config,
                                                    common::ObJsonObject *&body) 
{
  int ret = OB_SUCCESS;
  if (OB_ISNULL(model) || OB_ISNULL(content)) {
    ret = OB_INVALID_ARGUMENT;
  } else {
    // {"model": "*", "input": {"messages": [{"role": "system", "content": "*"}, {"role": "user", "content": "*"}]}, "parameters": {}}
    ObJsonObject *body_obj = nullptr;
    ObJsonString *model_str = nullptr;
    ObJsonObject *input_obj = nullptr;

    if (OB_ISNULL(config)) {
      if (OB_FAIL(ObAIFuncJsonUtils::get_json_object(allocator, config))) {
      }
    }
    if (OB_SUCC(ret)) {
      if (OB_FAIL(ObAIFuncJsonUtils::get_json_object(allocator, body_obj))) {
      } else if (OB_FAIL(ObAIFuncJsonUtils::get_json_string(allocator, model, model_str))) {
      } else if (OB_FAIL(body_obj->add("model", model_str))) {
      } else if (OB_FAIL(ObDashscopeUtils::ObDashscopeComplete::construct_input_obj(allocator, prompt, content, input_obj))) {
      } else if (OB_FAIL(body_obj->add("input", input_obj))) {
      } else if (OB_FAIL(ObDashscopeUtils::ObDashscopeComplete::set_config_result_format(allocator, config))) {
      } else if (OB_FAIL(body_obj->add("parameters", config))) {
      } else {
        body = body_obj;
      }
    }
  }
  return ret;
}

int ObDashscopeUtils::ObDashscopeComplete::set_config_result_format(ObIAllocator &allocator, ObJsonObject *config)
{
  int ret = OB_SUCCESS;
  if (OB_ISNULL(config)) {
    ret = OB_INVALID_ARGUMENT;
  } else if (OB_NOT_NULL(config->get_value("result_format"))) {
    if (OB_FAIL(config->remove("result_format"))) {
    }
  } 
  if (OB_SUCC(ret)) {
    // {"result_format": "message"}
    ObJsonString *result_format_str = nullptr;
    ObString result_format_str_val("message");
    if (OB_FAIL(ObAIFuncJsonUtils::get_json_string(allocator, result_format_str_val, result_format_str))) {
    } else if (OB_FAIL(config->add("result_format", result_format_str))) {
    }
  } 
  return ret;
}

int ObDashscopeUtils::ObDashscopeComplete::construct_input_obj(ObIAllocator &allocator, ObString &prompt, ObString &content, ObJsonObject *&input_obj)
{
  int ret = OB_SUCCESS;
  if (OB_ISNULL(content)) {
    ret = OB_INVALID_ARGUMENT;
  } else {
    ObJsonObject *obj = nullptr;
    ObJsonArray *messages_array = nullptr;
    if (OB_FAIL(ObAIFuncJsonUtils::get_json_object(allocator, obj))) {
    } else if (OB_FAIL(ObOpenAIUtils::ObOpenAIComplete::construct_messages_array(allocator, prompt, content, messages_array))) {
    } else if (OB_FAIL(obj->add("messages", messages_array))) {
    } else {
      input_obj = obj;
    }
  }
  return ret;
}

int ObDashscopeUtils::ObDashscopeComplete::set_config_json_format(ObIAllocator &allocator, ObJsonObject *config)
{
  int ret = OB_SUCCESS;
  if (OB_ISNULL(config)) {
    ret = OB_INVALID_ARGUMENT;
  } else {
    //{"type": "json_object"}
    ObJsonString *type_str = nullptr;
    ObString type_str_val("json_object");
    if (OB_FAIL(ObAIFuncJsonUtils::get_json_string(allocator, type_str_val, type_str))) {
    } else if (OB_FAIL(config->add("type", type_str))) {
    }
  }
  return ret;
}

int ObDashscopeUtils::ObDashscopeComplete::parse_output(ObIAllocator &allocator,
    ObJsonObject *http_response, ObIJsonBase *&result)
{
  int ret = OB_SUCCESS;
  result = nullptr;
  if (OB_ISNULL(http_response)) {
    ret = OB_INVALID_ARGUMENT;
    LOG_WARN("http_response is null", K(ret));
  } else {
    ObString path_text("$.output.choices[0].message.content");
    ObJsonPath j_path(path_text, &allocator);
    ObJsonSeekResult hit;
    if (OB_FAIL(j_path.parse_path())) {
    } else if (OB_FAIL(http_response->seek(j_path, j_path.path_node_cnt(), false, false, hit))) {
    } else if (1 != hit.size() || OB_ISNULL(hit[0])
               || ObJsonNodeType::J_STRING != hit[0]->json_type()
               || 0 == hit[0]->get_data_length()) {
      // the answer of the model must hold exactly one not empty content
      ret = OB_INVALID_DATA;
      LOG_WARN("the content of the answer is not a not empty string", K(ret));
    } else {
      result = hit[0];
    }
  }
  return ret;
}

int ObDashscopeUtils::ObDashscopeEmbed::get_header(common::ObIAllocator &allocator,
                                                   common::ObString &api_key,
                                                   common::ObArray<ObString> &headers) 
{
  return ObDashscopeUtils::get_header(allocator, api_key, headers);
}

int ObDashscopeUtils::ObDashscopeEmbed::get_body(common::ObIAllocator &allocator,
                                                 common::ObString &model,
                                                 common::ObArray<ObString> &contents,
                                                 common::ObJsonObject *config,
                                                 common::ObJsonObject *&body) 
{
  int ret = OB_SUCCESS;
  if (OB_ISNULL(model) || contents.empty()) {
    ret = OB_INVALID_ARGUMENT;
  } else {
    // {"model": "*", "input": {"texts": ["*"]}, "parameters": {}}
    ObJsonObject *body_obj = nullptr;
    ObJsonString *model_str = nullptr;
    ObJsonObject *input_obj = nullptr;
    ObJsonArray *texts_array = nullptr;
    if (OB_FAIL(ObAIFuncJsonUtils::get_json_object(allocator, body_obj))) {
    } else if (OB_FAIL(ObAIFuncJsonUtils::get_json_string(allocator, model, model_str))) {
    } else if (OB_FAIL(body_obj->add("model", model_str))) {
    } else if (OB_FAIL(ObAIFuncJsonUtils::get_json_object(allocator, input_obj))) {
    } else if (OB_FAIL(ObAIFuncJsonUtils::transform_array_to_json_array(allocator, contents, texts_array))) {
    } else if (OB_FAIL(input_obj->add("texts", texts_array))) {
    } else if (OB_FAIL(body_obj->add("input", input_obj))) {
    } else if (config != nullptr && config->element_count() > 0) {
      ObJsonNode *dimensions_node = config->get_value("dimensions");
      if (OB_ISNULL(dimensions_node)) {
        // do nothing
      } else if (OB_FAIL(config->rename_key("dimensions", "dimension"))) {
      } 
      if (OB_SUCC(ret)) {
        if (OB_FAIL(body_obj->add("parameters", config))) {
        }
      }
    } 
    if (OB_SUCC(ret)) {
      body = body_obj;
    }
  }
  return ret;
}

int ObDashscopeUtils::ObDashscopeEmbed::parse_output(common::ObIAllocator &allocator,
                                                    common::ObJsonObject *http_response,
                                                    common::ObIJsonBase *&result)
{
  int ret = OB_SUCCESS;
  result = nullptr;
  ObJsonNode *output = nullptr;
  ObJsonNode *embeddings = nullptr;
  ObJsonArray *data = nullptr;
  ObJsonObject *normalized = nullptr;
  if (OB_ISNULL(http_response) || OB_ISNULL(output = http_response->get_value("output"))
      || ObJsonNodeType::J_OBJECT != output->json_type()
      || OB_ISNULL(embeddings = static_cast<ObJsonObject *>(output)->get_value("embeddings"))
      || ObJsonNodeType::J_ARRAY != embeddings->json_type()
      || 0 >= embeddings->element_count()) {
    ret = OB_INVALID_DATA;
    LOG_WARN("the output of the answer holds no embeddings", K(ret));
  } else if (OB_FAIL(ObAIFuncJsonUtils::get_json_array(allocator, data))) {
  } else if (OB_FAIL(ObAIFuncJsonUtils::get_json_object(allocator, normalized))) {
  } else {
    // The dashscope protocol carries the index of the input in the text_index
    // field, the answer is converted to the protocol of the openai embed and
    // the same indexed validation restores the order of the inputs.
    for (int64_t i = 0; OB_SUCC(ret) && i < embeddings->element_count(); i++) {
      ObJsonNode *entry = static_cast<ObJsonArray *>(embeddings)->get_value(i);
      ObJsonObject *item = nullptr;
      ObJsonInt *position = nullptr;
      if (OB_ISNULL(entry) || ObJsonNodeType::J_OBJECT != entry->json_type()) {
        ret = OB_INVALID_DATA;
        LOG_WARN("the embedding of the answer is not an object", K(ret), K(i));
      } else if (OB_ISNULL(item = static_cast<ObJsonObject *>(entry->clone(&allocator, true)))) {
        ret = OB_ALLOCATE_MEMORY_FAILED;
        LOG_WARN("failed to clone the embedding of the answer", K(ret), K(i));
      } else {
        ObJsonNode *index = item->get_value("text_index");
        if (OB_NOT_NULL(index)) {
          ret = item->add("index", index);
        } else if (OB_FAIL(ObAIFuncJsonUtils::get_json_int(allocator, i, position))) {
        } else {
          // the protocol of the openai embed is used by the validation
          ret = item->add("index", position);
        }
        if (OB_SUCC(ret)) {
          ret = data->append(item);
        }
      }
    }
    if (OB_SUCC(ret)) {
      if (OB_FAIL(normalized->add("data", data))) {
      } else {
        ObOpenAIUtils::ObOpenAIEmbed provider;
        ret = provider.parse_output(allocator, normalized, result);
      }
    }
  }
  return ret;
}

int ObDashscopeUtils::ObDashscopeRerank::get_header(common::ObIAllocator &allocator,
                                                    common::ObString &api_key,
                                                    common::ObArray<ObString> &headers) 
{
  int ret = OB_SUCCESS;
  if (OB_FAIL(ObOpenAIUtils::get_header(allocator, api_key, headers))) {
  }
  return ret;
}

int ObDashscopeUtils::ObDashscopeRerank::get_body(common::ObIAllocator &allocator,
                                                  common::ObString &model,
                                                  common::ObString &query,
                                                  common::ObJsonArray *document_array,
                                                  common::ObJsonObject *config,
                                                  common::ObJsonObject *&body) 
{
  int ret = OB_SUCCESS;
  if (OB_ISNULL(model) || OB_ISNULL(query) || OB_ISNULL(document_array)) {
    ret = OB_INVALID_ARGUMENT;
  } else {
    ObJsonObject *body_obj = nullptr;
    ObJsonString *model_str = nullptr;
    ObJsonObject *input_obj = nullptr;
    if (OB_FAIL(ObAIFuncJsonUtils::get_json_object(allocator, body_obj))) {
    } else if (OB_FAIL(ObAIFuncJsonUtils::get_json_string(allocator, model, model_str))) {
    } else if (OB_FAIL(body_obj->add("model", model_str))) {
    } else if (OB_FAIL(ObDashscopeUtils::ObDashscopeRerank::get_input_obj(allocator, query, document_array, input_obj))) {
    } else if (OB_FAIL(body_obj->add("input", input_obj))) {
    } else if (config != nullptr && config->element_count() > 0) {
      if (OB_FAIL(body_obj->add("parameters", config))) {
      }
    } 
    if (OB_SUCC(ret)) {
      body = body_obj;
    }
  }
  return ret;
}

int ObDashscopeUtils::ObDashscopeRerank::get_input_obj(common::ObIAllocator &allocator,
                                                      common::ObString &query,
                                                      common::ObJsonArray *document_array,
                                                      common::ObJsonObject *&input_obj) 
{
  int ret = OB_SUCCESS;
  if (OB_ISNULL(query) || OB_ISNULL(document_array)) {
    ret = OB_INVALID_ARGUMENT;
  } else {
    ObJsonObject *obj = nullptr;
    ObJsonString *query_str = nullptr;
    if (OB_FAIL(ObAIFuncJsonUtils::get_json_object(allocator, obj))) {
    } else if (OB_FAIL(ObAIFuncJsonUtils::get_json_string(allocator, query, query_str))) {
    } else if (OB_FAIL(obj->add("query", query_str))) {
    } else if (OB_FAIL(obj->add("documents", document_array))) {
    } else {
      input_obj = obj;
    }
  }
  return ret;
}

int ObDashscopeUtils::ObDashscopeRerank::parse_output(ObIAllocator &allocator,
                                                      ObJsonObject *http_response,
                                                      ObIJsonBase *&result)
{
  int ret = OB_SUCCESS;
  UNUSED(allocator);
  if (OB_ISNULL(http_response)) {
    ret = OB_INVALID_ARGUMENT;
    LOG_WARN("http_response is null", K(ret));
  } else {
    ObJsonNode *output_node = http_response->get_value("output");
    ObJsonNode *results_node = nullptr;
    if (OB_ISNULL(output_node) || ObJsonNodeType::J_OBJECT != output_node->json_type()) {
      ret = OB_INVALID_DATA;
      LOG_WARN("the output of the rerank answer is not an object", K(ret));
    } else if (OB_ISNULL(results_node = static_cast<ObJsonObject *>(output_node)->get_value("results"))
               || ObJsonNodeType::J_ARRAY != results_node->json_type()) {
      ret = OB_INVALID_DATA;
      LOG_WARN("the results of the rerank answer is not an array", K(ret));
    } else {
      result = results_node;
    }
  }
  return ret;
}

int ObSiliconflowUtils::get_header(common::ObIAllocator &allocator,
                                 common::ObString &api_key,
                                 common::ObArray<ObString> &headers) 
{
  int ret = OB_SUCCESS;
  if (OB_FAIL(ObOpenAIUtils::get_header(allocator, api_key, headers))) {
  }
  return ret;
}

int ObSiliconflowUtils::ObSiliconflowRerank::get_header(common::ObIAllocator &allocator,
                                                    common::ObString &api_key,
                                                    common::ObArray<ObString> &headers) 
{
  int ret = OB_SUCCESS;
  if (OB_FAIL(ObOpenAIUtils::get_header(allocator, api_key, headers))) {
  }
  return ret;
}

int ObSiliconflowUtils::ObSiliconflowRerank::get_body(common::ObIAllocator &allocator,
                                                  common::ObString &model,
                                                  common::ObString &query,
                                                  common::ObJsonArray *document_array,
                                                  common::ObJsonObject *config,
                                                  common::ObJsonObject *&body) 
{
  int ret = OB_SUCCESS;
  if (OB_ISNULL(model) || OB_ISNULL(query) || OB_ISNULL(document_array)) {
    ret = OB_INVALID_ARGUMENT;
  } else {
    ObJsonObject *body_obj = nullptr;
    ObJsonString *model_str = nullptr;
    ObJsonString *query_str = nullptr;
    if (OB_FAIL(ObAIFuncJsonUtils::get_json_object(allocator, body_obj))) {
    } else if (OB_FAIL(ObAIFuncJsonUtils::get_json_string(allocator, model, model_str))) {
    } else if (OB_FAIL(body_obj->add("model", model_str))) {
    } else if (OB_FAIL(ObAIFuncJsonUtils::get_json_string(allocator, query, query_str))) {
    } else if (OB_FAIL(body_obj->add("query", query_str))) {
    } else if (OB_FAIL(body_obj->add("documents", document_array))) {
    } else if (OB_FAIL(ObAIFuncJsonUtils::compact_json_object(allocator, config, body_obj))) {
    } else {
      body = body_obj;
    }
  }
  return ret;
}

int ObSiliconflowUtils::ObSiliconflowRerank::parse_output(ObIAllocator &allocator,
                                                      ObJsonObject *http_response,
                                                      ObIJsonBase *&result)
{
  int ret = OB_SUCCESS;
  UNUSED(allocator);
  if (OB_ISNULL(http_response)) {
    ret = OB_INVALID_ARGUMENT;
    LOG_WARN("http_response is null", K(ret));
  } else {
    ObJsonNode *results_node = http_response->get_value("results");
    if (OB_ISNULL(results_node) || ObJsonNodeType::J_ARRAY != results_node->json_type()) {
      ret = OB_INVALID_DATA;
      LOG_WARN("the results of the rerank answer is not an array", K(ret));
    } else {
      result = results_node;
    }
  }
  return ret;
}


int ObAIFuncUtils::get_header(ObIAllocator &allocator, 
                              const ObAIFuncExprInfo &info,
                              const share::ObAiModelEndpointInfo &endpoint_info,
                              ObArray<ObString> &headers) 
{
  int ret = OB_SUCCESS;
  ObString unencrypted_access_key;
  if (OB_FAIL(endpoint_info.get_unencrypted_access_key(allocator, unencrypted_access_key))) {
  } else if (ObAIFuncUtils::is_completion_type(&info)) {
    ObAIFuncIComplete *complete_provider = nullptr;
    if (OB_FAIL(get_complete_provider(allocator, endpoint_info.get_provider(), complete_provider))) {
    } else if (OB_FAIL(complete_provider->get_header(allocator, unencrypted_access_key, headers))) {
    }
  } else if (ObAIFuncUtils::is_dense_embedding_type(&info)) {
    ObAIFuncIEmbed *embed_provider = nullptr;
    if (OB_FAIL(get_embed_provider(allocator, endpoint_info.get_provider(), embed_provider))) {
    } else if (OB_FAIL(embed_provider->get_header(allocator, unencrypted_access_key, headers))) {
    }
  } else if (ObAIFuncUtils::is_rerank_type(&info)) {
    ObAIFuncIRerank *rerank_provider = nullptr;
    if (OB_FAIL(get_rerank_provider(allocator, endpoint_info.get_provider(), rerank_provider))) {
    } else if (OB_FAIL(rerank_provider->get_header(allocator, unencrypted_access_key, headers))) {
    }
  }
  return ret;
}

int ObAIFuncUtils::get_complete_body(ObIAllocator &allocator, 
                                    const ObAIFuncExprInfo &info, 
                                    const share::ObAiModelEndpointInfo &endpoint_info,
                                    ObString &prompt, 
                                    ObString &content, 
                                    ObJsonObject *config,
                                    ObJsonObject *&body) 
{
  int ret = OB_SUCCESS;
  ObString request_model_name = info.model_;
  if (!endpoint_info.get_request_model_name().empty()) {
    request_model_name = endpoint_info.get_request_model_name();
  }

  ObAIFuncIComplete *complete_provider = nullptr;
  if (OB_FAIL(get_complete_provider(allocator, endpoint_info.get_provider(), complete_provider))) {
  } else if (OB_FAIL(complete_provider->get_body(allocator, request_model_name, prompt, content, config, body))) {
  }
  return ret;
}

int ObAIFuncUtils::set_json_format_config(ObIAllocator &allocator, const ObString &provider, ObJsonObject *config)
{
  int ret = OB_SUCCESS;
  ObAIFuncIComplete *complete_provider = nullptr;
  if (OB_FAIL(get_complete_provider(allocator, provider, complete_provider))) {
  } else if (OB_FAIL(complete_provider->set_config_json_format(allocator, config))) {
  } 
  return ret;
}

int ObAIFuncUtils::get_embed_body(ObIAllocator &allocator, 
                                  const ObAIFuncExprInfo &info, 
                                  const share::ObAiModelEndpointInfo &endpoint_info,
                                  ObArray<ObString> &contents, 
                                  ObJsonObject *config,
                                  ObJsonObject *&body) 
{
  int ret = OB_SUCCESS;
  ObString request_model_name = info.model_;
  if (!endpoint_info.get_request_model_name().empty()) {
    request_model_name = endpoint_info.get_request_model_name();
  }

  ObAIFuncIEmbed *embed_provider = nullptr;
  if (OB_FAIL(get_embed_provider(allocator, endpoint_info.get_provider(), embed_provider))) {
  } else if (OB_FAIL(embed_provider->get_body(allocator, request_model_name, contents, config, body))) {
  }
  return ret;
}

int ObAIFuncUtils::get_rerank_body(ObIAllocator &allocator, 
                                   const ObAIFuncExprInfo &info,
                                   const share::ObAiModelEndpointInfo &endpoint_info,
                                   ObString &query,
                                   ObJsonArray *document_array,
                                   ObJsonObject *config,
                                   ObJsonObject *&body) 
{
  int ret = OB_SUCCESS;
  ObString request_model_name = info.model_;
  if (!endpoint_info.get_request_model_name().empty()) {
    request_model_name = endpoint_info.get_request_model_name();
  }

  ObAIFuncIRerank *rerank_provider = nullptr;
  if (OB_FAIL(get_rerank_provider(allocator, endpoint_info.get_provider(), rerank_provider))) {
  } else if (OB_FAIL(rerank_provider->get_body(allocator, request_model_name, query, document_array, config, body))) {
  }
  return ret;
}

int ObAIFuncUtils::parse_complete_output(ObIAllocator &allocator, 
                                        const share::ObAiModelEndpointInfo &endpoint_info,
                                        ObJsonObject *http_response,
                                        ObIJsonBase *&result)
{
  int ret = OB_SUCCESS;
  ObAIFuncIComplete *complete_provider = nullptr;
  if (OB_FAIL(get_complete_provider(allocator, endpoint_info.get_provider(), complete_provider))) {
  } else if (OB_FAIL(complete_provider->parse_output(allocator, http_response, result))) {
  }
  return ret;
}

int ObAIFuncUtils::parse_embed_output(ObIAllocator &allocator, 
                                      const share::ObAiModelEndpointInfo &endpoint_info,
                                      ObJsonObject *http_response,
                                      ObIJsonBase *&result)
{
  int ret = OB_SUCCESS;
  ObAIFuncIEmbed *embed_provider = nullptr;
  if (OB_FAIL(get_embed_provider(allocator, endpoint_info.get_provider(), embed_provider))) {
  } else if (OB_FAIL(embed_provider->parse_output(allocator, http_response, result))) {
  }
  return ret;
}

int ObAIFuncUtils::parse_rerank_output(ObIAllocator &allocator, 
                                       const share::ObAiModelEndpointInfo &endpoint_info,
                                       ObJsonObject *http_response,
                                       ObIJsonBase *&result)
{
  int ret = OB_SUCCESS;
  ObAIFuncIRerank *rerank_provider = nullptr;
  if (OB_FAIL(get_rerank_provider(allocator, endpoint_info.get_provider(), rerank_provider))) {
  } else if (OB_FAIL(rerank_provider->parse_output(allocator, http_response, result))) {
  }
  return ret;
}

int ObAIFuncJsonUtils::get_json_object(ObIAllocator &allocator, ObJsonObject *&obj_node)
{
  int ret = OB_SUCCESS;
  ObJsonObject *j_obj = OB_NEWx(ObJsonObject, &allocator, &allocator);
  if (OB_ISNULL(j_obj)) {
    ret = OB_ALLOCATE_MEMORY_FAILED;
  } else {
    obj_node = j_obj;
  }
  return ret;
}

int ObAIFuncJsonUtils::get_json_array(ObIAllocator &allocator, ObJsonArray *&array_node)
{
  int ret = OB_SUCCESS;
  ObJsonArray *j_array = OB_NEWx(ObJsonArray, &allocator, &allocator);
  if (OB_ISNULL(j_array)) {
    ret = OB_ALLOCATE_MEMORY_FAILED;
  } else {
    array_node = j_array; 
  }
  return ret;
}

int ObAIFuncJsonUtils::get_json_string(ObIAllocator &allocator, ObString &str, ObJsonString *&str_node)
{
  int ret = OB_SUCCESS;
  ObJsonString *j_str = OB_NEWx(ObJsonString, &allocator,str);
  if (OB_ISNULL(j_str)) {
    ret = OB_ALLOCATE_MEMORY_FAILED;
  } else {
    str_node = j_str;
  }
  return ret;
}

int ObAIFuncJsonUtils::get_json_int(ObIAllocator &allocator, int64_t num, ObJsonInt *&int_node)
{
  int ret = OB_SUCCESS;
  ObJsonInt *j_int = OB_NEWx(ObJsonInt, &allocator, num);
  if (OB_ISNULL(j_int)) {
    ret = OB_ALLOCATE_MEMORY_FAILED;
  } else {
    int_node = j_int;
  }
  return ret;
}

int ObAIFuncJsonUtils::get_json_boolean(ObIAllocator &allocator, bool value, ObJsonBoolean *&boolean_node)
{
  int ret = OB_SUCCESS;
  ObJsonBoolean *j_bool = OB_NEWx(ObJsonBoolean, &allocator, value);
  if (OB_ISNULL(j_bool)) {
    ret = OB_ALLOCATE_MEMORY_FAILED;
  } else {
    boolean_node = j_bool;
  }
  return ret;
}

int ObAIFuncJsonUtils::print_json_to_str(ObIAllocator &allocator, ObIJsonBase *base_node, ObString &str)
{
  int ret = OB_SUCCESS;
  ObJsonBuffer j_buf(&allocator);
  if (OB_ISNULL(base_node)) {
    ret = OB_INVALID_DATA;
    LOG_WARN("the json node to print is null", K(ret));
  } else if (OB_FAIL(base_node->print(j_buf, 0))) {
    LOG_WARN("failed to print the json node", K(ret));
  } else if (OB_FAIL(j_buf.get_result_string(str))) {
    LOG_WARN("failed to get the text of the json node", K(ret));
  }
  return ret;
}

int ObAIFuncJsonUtils::get_json_object_form_str(ObIAllocator &allocator, ObString &str, ObJsonObject *&obj_node)
{
  int ret = OB_SUCCESS;
  ObIJsonBase *j_base = NULL;
  if (str.empty()) {
    ret = OB_INVALID_ARGUMENT;
    LOG_WARN("the json string is empty", K(ret));
  } else if (OB_FAIL(ObJsonBaseFactory::get_json_base(&allocator, str, ObJsonInType::JSON_TREE, ObJsonInType::JSON_TREE, j_base))) {
    LOG_WARN("failed to parse the json string", K(ret));
  } else if (OB_ISNULL(j_base) || ObJsonNodeType::J_OBJECT != j_base->json_type()) {
    // the config of the ai functions must be a json object, the static cast
    // below is not valid for the other json types
    ret = OB_INVALID_ARGUMENT;
    LOG_WARN("the json text is not an object", K(ret),
             K(OB_ISNULL(j_base) ? ObJsonNodeType::J_NULL : j_base->json_type()));
  } else {
    obj_node = static_cast<ObJsonObject *>(j_base);
  }
  return ret;
}

int ObAIFuncJsonUtils::compact_json_object(ObIAllocator &allocator, ObJsonObject *obj_node, ObJsonObject *compact_obj)
{
  int ret = OB_SUCCESS;
  // add all members of obj_node to compact_obj
  if (OB_ISNULL(compact_obj)) {
    ret = OB_INVALID_ARGUMENT;
  } else if (OB_ISNULL(obj_node)) {
    // do nothing
  } else {
    ObString key;
    int64_t count = obj_node->element_count();
    for (int64_t i = 0; OB_SUCC(ret) && i < count; i++) {
      ObJsonNode *j_node = obj_node->get_value(i);
      if (OB_ISNULL(j_node)) {
        ret = OB_ERR_UNEXPECTED;
      } else if (OB_FAIL(obj_node->get_key(i, key))) {
      } else if (OB_FAIL(compact_obj->add(key, j_node))) {
      }
    }
  }
  return ret;
}

int ObAIFuncPromptUtils::replace_meta_prompt(ObIAllocator &allocator, ObString &meta_prompt, ObString &key, ObString &content, ObString &result)
{
  int ret = OB_SUCCESS;
  if (meta_prompt.empty() || key.empty()) {
    ret = OB_INVALID_ARGUMENT;
  } else {
    const char *key_pos = nullptr;
    if (OB_ISNULL(key_pos = STRSTR(meta_prompt.ptr(), key.ptr()))) {
      ret = OB_ERR_UNEXPECTED;
    } else {
      int64_t before_key_len = key_pos - meta_prompt.ptr();
      int64_t after_key_len = meta_prompt.length() - before_key_len - key.length();
      int64_t new_len = before_key_len + content.length() + after_key_len;
      char *new_str = static_cast<char *>(allocator.alloc(new_len));
      if (OB_ISNULL(new_str)) {
        ret = OB_ALLOCATE_MEMORY_FAILED;
      } else {
        MEMCPY(new_str, meta_prompt.ptr(), before_key_len);
        MEMCPY(new_str + before_key_len, content.ptr(), content.length());
        MEMCPY(new_str + before_key_len + content.length(), key_pos + key.length(), after_key_len);
        result.assign_ptr(new_str, static_cast<ObString::obstr_size_t>(new_len));
      }
    }
  }
  return ret;
}

int ObAIFuncJsonUtils::transform_array_to_json_array(ObIAllocator &allocator, ObArray<ObString> &contents, ObJsonArray *&array)
{
  int ret = OB_SUCCESS;
  if (OB_ISNULL(array = OB_NEWx(ObJsonArray, &allocator, &allocator))) {
    ret = OB_ALLOCATE_MEMORY_FAILED;
  } else {
    for (int i = 0; OB_SUCC(ret) && i < contents.count(); i++) {
      ObString content = contents[i];
      ObJsonString *j_str = OB_NEWx(ObJsonString, &allocator, content);
      if (OB_ISNULL(j_str)) {
        ret = OB_ALLOCATE_MEMORY_FAILED;
      } else if (OB_FAIL(array->append(j_str))) {
        LOG_WARN("failed to append the content of the input", K(ret), K(i));
      }
    }
  }
  return ret;
}

int ObAIFuncUtils::get_complete_provider(ObIAllocator &allocator, const ObString &provider, ObAIFuncIComplete *&complete_provider)
{
  int ret = OB_SUCCESS;
  if (provider.empty()) {
    ret = OB_INVALID_ARGUMENT;
    LOG_USER_ERROR(OB_INVALID_ARGUMENT, "ai_function, provider is empty");
  } else if (ob_provider_check(provider, ObAIFuncProviderUtils::OPENAI) 
      || ob_provider_check(provider, ObAIFuncProviderUtils::ALIYUN) 
      || ob_provider_check(provider, ObAIFuncProviderUtils::DEEPSEEK)
      || ob_provider_check(provider, ObAIFuncProviderUtils::SILICONFLOW)
      || ob_provider_check(provider, ObAIFuncProviderUtils::HUNYUAN)) {
    complete_provider = OB_NEWx(ObOpenAIUtils::ObOpenAIComplete, &allocator);
  } else if (ob_provider_check(provider, ObAIFuncProviderUtils::DASHSCOPE)) {
    complete_provider = OB_NEWx(ObDashscopeUtils::ObDashscopeComplete, &allocator);
  } else if (ob_provider_check(provider, ObAIFuncProviderUtils::OLLAMA)) {
    complete_provider = OB_NEWx(ObOllamaUtils::ObOllamaComplete, &allocator);
  } else {
    ret = OB_NOT_SUPPORTED;
    LOG_USER_ERROR(OB_NOT_SUPPORTED, "this provider current is");
  } 
  if (OB_SUCC(ret) && OB_ISNULL(complete_provider)) {
    ret = OB_ALLOCATE_MEMORY_FAILED;
  }
  return ret;
}

int ObAIFuncUtils::get_embed_provider(ObIAllocator &allocator, const ObString &provider, ObAIFuncIEmbed *&embed_provider)
{
  int ret = OB_SUCCESS;
  if (provider.empty()) {
    ret = OB_INVALID_ARGUMENT;
    LOG_USER_ERROR(OB_INVALID_ARGUMENT, "ai_function, provider is empty");
  } else if (ob_provider_check(provider, ObAIFuncProviderUtils::OPENAI) 
      || ob_provider_check(provider, ObAIFuncProviderUtils::ALIYUN)
      || ob_provider_check(provider, ObAIFuncProviderUtils::HUNYUAN)
      || ob_provider_check(provider, ObAIFuncProviderUtils::SILICONFLOW)) {
    embed_provider = OB_NEWx(ObOpenAIUtils::ObOpenAIEmbed, &allocator);
  } else if (ob_provider_check(provider, ObAIFuncProviderUtils::DASHSCOPE)) {
    embed_provider = OB_NEWx(ObDashscopeUtils::ObDashscopeEmbed, &allocator);
  } else if (ob_provider_check(provider, ObAIFuncProviderUtils::OLLAMA)) {
    embed_provider = OB_NEWx(ObOllamaUtils::ObOllamaEmbed, &allocator);
  } else {
    ret = OB_NOT_SUPPORTED;
    LOG_USER_ERROR(OB_NOT_SUPPORTED, "this provider current is");
  } 
  if (OB_SUCC(ret) && OB_ISNULL(embed_provider)) {
    ret = OB_ALLOCATE_MEMORY_FAILED;
  }
  return ret;
}

int ObAIFuncUtils::get_rerank_provider(ObIAllocator &allocator, const ObString &provider, ObAIFuncIRerank *&rerank_provider)
{
  int ret = OB_SUCCESS;
  if (provider.empty()) {
    ret = OB_INVALID_ARGUMENT;
    LOG_USER_ERROR(OB_INVALID_ARGUMENT, "ai_function, provider is empty");
  } else if (ob_provider_check(provider, ObAIFuncProviderUtils::SILICONFLOW)
          || ob_provider_check(provider, ObAIFuncProviderUtils::COHERE)) {
    rerank_provider = OB_NEWx(ObSiliconflowUtils::ObSiliconflowRerank, &allocator);
  } else if (ob_provider_check(provider, ObAIFuncProviderUtils::DASHSCOPE)) {
    rerank_provider = OB_NEWx(ObDashscopeUtils::ObDashscopeRerank, &allocator);
  } else {
    ret = OB_NOT_SUPPORTED;
    LOG_USER_ERROR(OB_NOT_SUPPORTED, "rerank support siliconflow, cohere and ailiyun-dashscope, this provider current is");
  } 
  if (OB_SUCC(ret) && OB_ISNULL(rerank_provider)) {
    ret = OB_ALLOCATE_MEMORY_FAILED;
  }
  return ret;
}

int ObAIFuncUtils::check_info_type_completion(const ObAIFuncExprInfo *info)
{
  int ret = OB_SUCCESS;
  if (OB_ISNULL(info)) {
    ret = OB_INVALID_ARGUMENT;
    LOG_USER_ERROR(OB_INVALID_ARGUMENT, "ai_complete, info is null");
  } else if (!is_completion_type(info)) {
    ret = OB_INVALID_ARGUMENT;
    LOG_USER_ERROR(OB_INVALID_ARGUMENT, "ai_complete, info type is not completion");
  }
  return ret;
}

int ObAIFuncUtils::check_info_type_dense_embedding(const ObAIFuncExprInfo *info)
{
  int ret = OB_SUCCESS;
  if (OB_ISNULL(info)) {
    ret = OB_INVALID_ARGUMENT;
    LOG_USER_ERROR(OB_INVALID_ARGUMENT, "ai_embed, info is null");
  } else if (!is_dense_embedding_type(info)) {
    ret = OB_INVALID_ARGUMENT;
    LOG_USER_ERROR(OB_INVALID_ARGUMENT, "ai_embed, info type is not dense embedding");
  }
  return ret;
}

int ObAIFuncUtils::check_info_type_rerank(const ObAIFuncExprInfo *info)
{
  int ret = OB_SUCCESS;
  if (OB_ISNULL(info)) {
    ret = OB_INVALID_ARGUMENT;
    LOG_USER_ERROR(OB_INVALID_ARGUMENT, "ai_rerank, info is null");
  } else if (!is_rerank_type(info)) {
    ret = OB_INVALID_ARGUMENT;
    LOG_USER_ERROR(OB_INVALID_ARGUMENT, "ai_rerank, info type is not rerank");
  }
  return ret;
}

int ObAIFuncUtils::set_string_result(const ObExpr &expr, ObEvalCtx &ctx, ObDatum &res, ObString &res_str)
{
  int ret = OB_SUCCESS;
  ObTextStringDatumResult text_result(expr.datum_meta_.type_, &expr, &ctx, &res);
  int64_t res_len = res_str.length();
  if (OB_FAIL(text_result.init(res_len))) {
  } else if (OB_FAIL(text_result.append(res_str))) {
  } else {
    text_result.set_result();
  }
  return ret;
}

int ObAIFuncUtils::get_ai_func_info(ObIAllocator &allocator, const ObString &model_id,
                                    share::schema::ObSchemaGetterGuard &guard,
                                    ObAIFuncExprInfo *&info)
{
  int ret = OB_SUCCESS;
  if (model_id.empty()) {
    ret = OB_INVALID_ARGUMENT;
    LOG_USER_ERROR(OB_INVALID_ARGUMENT, "ai_function, model_id is empty");
  } else {
    ObAIFuncExprInfo *info_obj = OB_NEWx(ObAIFuncExprInfo, (&allocator), allocator, T_FUN_SYS_AI_COMPLETE);
    if (OB_ISNULL(info_obj)) {
      ret = OB_ALLOCATE_MEMORY_FAILED;
    } else if (OB_FAIL(info_obj->init(allocator, model_id, guard))) {
    } else {
      info = info_obj;
    }
  }
  return ret;
}

bool ObAIFuncUtils::is_provider_support_base64(const ObString &provider)
{
  return ob_provider_check(provider, ObAIFuncProviderUtils::OPENAI)
      || ob_provider_check(provider, ObAIFuncProviderUtils::SILICONFLOW);
}

int ObAIFuncUtils::decode_base64_embedding_array(const ObIJsonBase &embedding_jbase,
                                                 ObIAllocator &allocator,
                                                 const int64_t dimension,
                                                 float *&vector)
{
  int ret = OB_SUCCESS;
  if (embedding_jbase.json_type() != ObJsonNodeType::J_STRING) {
    ret = OB_INVALID_ARGUMENT;
  } else {
    const char *encoded_embedding = embedding_jbase.get_data();
    uint64_t encoded_embedding_len = embedding_jbase.get_data_length();
    uint64_t decoded_buf_len = ObBase64Encoder::needed_decoded_length(encoded_embedding_len);
    uint8_t *decoded_buf = nullptr;
    int64_t pos = 0;
    if (decoded_buf_len <= 0) {
      ret = OB_INVALID_ARGUMENT;
    } else if (OB_ISNULL(decoded_buf = static_cast<uint8_t *>(allocator.alloc(decoded_buf_len)))) {
      ret = OB_ALLOCATE_MEMORY_FAILED;
    } else if (OB_FAIL(ObBase64Encoder::decode(encoded_embedding, encoded_embedding_len,
                                               decoded_buf, decoded_buf_len, pos))) {
    } else if (pos != dimension * sizeof(float)) {
      ret = OB_ERR_UNEXPECTED;
    } else {
      vector = reinterpret_cast<float *>(decoded_buf);
    }
  }
  return ret;
}

int ObAIFuncUtils::decode_float_embedding_array(const ObIJsonBase &embedding_jbase,
                                                ObIAllocator &allocator,
                                                share::ObJsonReaderHelper &json_reader,
                                                const int64_t dimension,
                                                float *&vector)
{
  int ret = OB_SUCCESS;
  float *tmp_vector = nullptr;
  if (!share::ObJsonHelper::is_array_type(&embedding_jbase)) {
    ret = OB_INVALID_ARGUMENT;
  } else {
    uint64_t embedding_size = json_reader.get_array_size(&embedding_jbase);
    if (embedding_size != dimension) {
      ret = OB_ERR_UNEXPECTED;
    } else {
      tmp_vector = static_cast<float*>(allocator.alloc(dimension * sizeof(float)));
      if (OB_ISNULL(tmp_vector)) {
        ret = OB_ALLOCATE_MEMORY_FAILED;
      } else {
        for (uint64_t i = 0; i < dimension && OB_SUCC(ret); i++) {
          ObIJsonBase *value = nullptr;
          if (OB_FAIL(json_reader.get_array_element(&embedding_jbase, i, value))) {
          } else if (!share::ObJsonHelper::is_number_type(value)) {
            ret = OB_INVALID_ARGUMENT;
          } else {
            float f_value = 0.0;
            if (OB_FAIL(json_reader.get_float_value(value, f_value))) {
            } else {
              tmp_vector[i] = f_value;
            }
          }
        }
      }
    }
  }

  if (OB_SUCC(ret)) {
    vector = tmp_vector;
  }
  return ret;
}

// Get the value of a json number, the number must be finite.
static int get_finite_json_number(ObIAllocator &allocator, ObIJsonBase *node, double &value)
{
  int ret = OB_SUCCESS;
  if (OB_ISNULL(node)) {
    ret = OB_INVALID_DATA;
  } else if (ObJsonNodeType::J_DOUBLE == node->json_type()) {
    value = node->get_double();
  } else if (ObJsonNodeType::J_INT == node->json_type()) {
    value = static_cast<double>(node->get_int());
  } else if (ObJsonNodeType::J_UINT == node->json_type()) {
    value = static_cast<double>(node->get_uint());
  } else if (ObJsonNodeType::J_DECIMAL == node->json_type()) {
    // the json library has no conversion of the decimal to a double
    ObJsonBuffer buffer(&allocator);
    ObString text;
    if (OB_FAIL(node->print(buffer, false))) {
    } else if (OB_FAIL(ob_write_string(allocator, buffer.string(), text, true))) {
    } else {
      char *end = nullptr;
      value = std::strtod(text.ptr(), &end);
      if (end != text.ptr() + text.length()) {
        ret = OB_INVALID_DATA;
      }
    }
  } else {
    ret = OB_INVALID_DATA;
  }
  if (OB_SUCC(ret) && (!std::isfinite(value) || !std::isfinite(static_cast<float>(value)))) {
    ret = OB_INVALID_DATA;
    LOG_WARN("the number is not finite", K(ret), K(value));
  }
  return ret;
}

int ObAIFuncUtils::validate_embedding_result(ObIAllocator &allocator, ObIJsonBase *result, int64_t dimension)
{
  int ret = OB_SUCCESS;
  if (OB_ISNULL(result) || ObJsonNodeType::J_ARRAY != result->json_type()
      || 0 >= result->element_count()) {
    // the answer must hold at least one embedding
    ret = OB_INVALID_DATA;
    LOG_WARN("the embeddings of the answer is not a not empty array", K(ret), K(dimension));
  }
  for (int64_t i = 0; OB_SUCC(ret) && i < result->element_count(); i++) {
    ObIJsonBase *vector = nullptr;
    if (OB_FAIL(result->get_array_element(i, vector))) {
      LOG_WARN("failed to get the embedding of the answer", K(ret), K(i));
    } else if (OB_ISNULL(vector) || ObJsonNodeType::J_ARRAY != vector->json_type()
               || 0 >= vector->element_count()) {
      ret = OB_INVALID_DATA;
      LOG_WARN("the embedding of the answer is not a not empty array", K(ret), K(i));
    } else {
      // the dimension of the request is the expected length of every vector,
      // otherwise the length of the first vector is the expected length
      if (0 >= dimension) {
        dimension = vector->element_count();
      }
      if (vector->element_count() != dimension) {
        ret = OB_INVALID_ARGUMENT;
        LOG_USER_ERROR(OB_INVALID_ARGUMENT,
                       "ai_embed, result dimension is not equal to dimension");
        LOG_WARN("the length of the embeddings are not the same", K(ret),
                 K(vector->element_count()), K(dimension));
      }
      for (int64_t j = 0; OB_SUCC(ret) && j < vector->element_count(); j++) {
        ObIJsonBase *element = nullptr;
        double value = 0.0;
        if (OB_FAIL(vector->get_array_element(j, element))) {
          LOG_WARN("failed to get the element of the embedding", K(ret), K(i), K(j));
        } else if (OB_FAIL(get_finite_json_number(allocator, element, value))) {
          LOG_WARN("the element of the embedding is not a finite number", K(ret), K(i), K(j));
        }
      }
    }
  }
  return ret;
}

int ObAIFuncUtils::normalize_rerank_result(ObIAllocator &allocator, ObIJsonBase *result,
                                           int64_t count, ObJsonArray *&normalized)
{
  int ret = OB_SUCCESS;
  normalized = nullptr;
  ObJsonArray *result_array = nullptr;
  if (count <= 0 || OB_ISNULL(result) || ObJsonNodeType::J_ARRAY != result->json_type()
      || result->element_count() != count) {
    ret = OB_INVALID_DATA;
    LOG_WARN("the results of the rerank answer is invalid", K(ret), K(count));
  } else {
    // the result of the document with the index i is kept in the i-th slot
    ObArray<ObJsonObject *> items;
    ObArray<int64_t> slot_filled;
    ObArray<double> scores;
    if (OB_FAIL(items.prepare_allocate(count))
        || OB_FAIL(slot_filled.prepare_allocate(count))
        || OB_FAIL(scores.prepare_allocate(count))) {
      ret = OB_ALLOCATE_MEMORY_FAILED;
      LOG_WARN("failed to allocate the rerank result slots", K(ret), K(count));
    }
    for (int64_t i = 0; OB_SUCC(ret) && i < count; i++) {
      items.at(i) = nullptr;
      slot_filled.at(i) = 0;
      scores.at(i) = 0.0;
    }
    for (int64_t i = 0; OB_SUCC(ret) && i < count; i++) {
      ObIJsonBase *row = nullptr;
      ObIJsonBase *index_node = nullptr;
      ObIJsonBase *score_node = nullptr;
      int64_t position = -1;
      if (OB_FAIL(result->get_array_element(i, row))) {
        LOG_WARN("failed to get the result of the rerank", K(ret), K(i));
      } else if (OB_ISNULL(row) || ObJsonNodeType::J_OBJECT != row->json_type()) {
        ret = OB_INVALID_DATA;
        LOG_WARN("the result of the rerank is not an object", K(ret), K(i));
      } else if (OB_FAIL(row->get_object_value(ObString("index"), index_node))) {
        LOG_WARN("failed to get the index of the rerank result", K(ret), K(i));
      } else if (OB_FAIL(row->get_object_value(ObString("relevance_score"), score_node))) {
        LOG_WARN("failed to get the score of the rerank result", K(ret), K(i));
      } else {
        if (OB_NOT_NULL(index_node) && ObJsonNodeType::J_INT == index_node->json_type()) {
          position = index_node->get_int();
        } else if (OB_NOT_NULL(index_node) && ObJsonNodeType::J_UINT == index_node->json_type()
                   && index_node->get_uint() < static_cast<uint64_t>(count)) {
          position = static_cast<int64_t>(index_node->get_uint());
        }
        double score = 0.0;
        if (position < 0 || position >= count || 0 != slot_filled.at(position)) {
          ret = OB_INVALID_DATA;
          LOG_WARN("the index of the rerank result is invalid", K(ret), K(position), K(count));
        } else if (OB_FAIL(get_finite_json_number(allocator, score_node, score))) {
          LOG_WARN("the score of the rerank result is invalid", K(ret), K(i));
        } else {
          ObJsonObject *item = OB_NEWx(ObJsonObject, &allocator, &allocator);
          ObJsonInt *index_out = OB_NEWx(ObJsonInt, &allocator, position);
          ObJsonDouble *score_out = OB_NEWx(ObJsonDouble, &allocator, score);
          if (OB_ISNULL(item) || OB_ISNULL(index_out) || OB_ISNULL(score_out)) {
            ret = OB_ALLOCATE_MEMORY_FAILED;
          } else if (OB_FAIL(item->add("index", index_out))) {
          } else if (OB_FAIL(item->add("relevance_score", score_out))) {
          } else {
            items.at(position) = item;
            slot_filled.at(position) = 1;
            scores.at(position) = score;
          }
        }
      }
    }
    if (OB_SUCC(ret) && OB_FAIL(ObAIFuncJsonUtils::get_json_array(allocator, result_array))) {
      LOG_WARN("failed to allocate the normalized rerank result", K(ret));
    }
    int64_t *sorted_indices = nullptr;
    if (OB_FAIL(ret)) {
    } else if (OB_ISNULL(sorted_indices = static_cast<int64_t *>(
                   allocator.alloc(sizeof(int64_t) * count)))) {
      ret = OB_ALLOCATE_MEMORY_FAILED;
      LOG_WARN("failed to allocate the indices of the rerank result", K(ret), K(count));
    } else {
      for (int64_t i = 0; OB_SUCC(ret) && i < count; i++) {
        if (OB_ISNULL(items.at(i))) {
          ret = OB_INVALID_DATA;
          LOG_WARN("the rerank result of a document is missing", K(ret), K(i));
        } else {
          sorted_indices[i] = i;
        }
      }
      if (OB_SUCC(ret)) {
        // sort by the score with the bigger score first, the same score keeps
        // the order of the index
        std::sort(sorted_indices, sorted_indices + count,
                  [&scores](const int64_t left, const int64_t right) {
                    if (scores.at(left) != scores.at(right)) {
                      return scores.at(left) > scores.at(right);
                    }
                    return left < right;
                  });
        for (int64_t i = 0; OB_SUCC(ret) && i < count; i++) {
          if (OB_FAIL(result_array->append(items.at(sorted_indices[i])))) {
            LOG_WARN("failed to append the normalized rerank result", K(ret), K(i));
          }
        }
        if (OB_SUCC(ret)) {
          normalized = result_array;
        }
      }
    }
  }
  return ret;
}

int ObAIFuncUtils::get_ai_func_info(ObIAllocator &allocator, const ObString &model_id, ObAIFuncExprInfo *&info)
{
  int ret = OB_SUCCESS;
  share::schema::ObMultiVersionSchemaService *schema_service = GCTX.schema_service_;
  share::schema::ObSchemaGetterGuard guard;
  
  if (OB_ISNULL(schema_service)) {
    ret = OB_ERR_UNEXPECTED;
  } else if (OB_FAIL(schema_service->get_runtime_schema_guard(guard))) {
  } else if (OB_FAIL(get_ai_func_info(allocator, model_id, guard, info))) {
  }
  return ret;
}

int ObAIFuncModel::call_completion(ObString &prompt, ObJsonObject *config, ObString &result)
{
  int ret = OB_SUCCESS;
  ObArray<ObString> headers;
  ObJsonObject *body = nullptr;
  ObJsonObject *response = nullptr;
  ObIJsonBase *result_base = nullptr;
  ObAIFuncIComplete *complete_provider = nullptr;
  ObString prompt_str;
  ObString result_str;
  ObAIFuncClient client;
  ObString unencrypted_access_key;
  ObString request_model_name = get_request_model_name();
  if (!is_completion_type()) {
    ret = OB_INVALID_ARGUMENT;
    LOG_USER_ERROR(OB_INVALID_ARGUMENT, "ai_complete, info type is not completion");
  } else if (OB_FAIL(ObAIFuncUtils::get_complete_provider(*allocator_, endpoint_info_.get_provider(), complete_provider))) {
  } else if (OB_FAIL(endpoint_info_.get_unencrypted_access_key(*allocator_, unencrypted_access_key))) {
  } else if (OB_FAIL(complete_provider->get_header(*allocator_, unencrypted_access_key, headers))) {
  } else if (OB_FAIL(complete_provider->get_body(*allocator_, request_model_name, prompt_str, prompt, config, body))) {
  } else if (OB_FAIL(client.send_post(*allocator_, endpoint_info_.get_url(), headers, body, response))) {
  } else if (OB_FAIL(complete_provider->parse_output(*allocator_, response, result_base))) {
  } else if (OB_FAIL(ObAIFuncJsonUtils::print_json_to_str(*allocator_, result_base, result_str))) {
  } else {
    result = result_str;
  }
  if (ret == OB_INVALID_DATA) {
    ObString response_str;
    if (OB_SUCCESS == ObAIFuncJsonUtils::print_json_to_str(*allocator_, response, response_str)) {
      char http_message_str[1024];
      snprintf(http_message_str, sizeof(http_message_str), "unexpected http message: %.*s",
               static_cast<int>(response_str.length()), response_str.ptr());
      ObString ob_http_message_str(http_message_str);
      FORWARD_USER_ERROR(ret, ob_http_message_str.ptr());
    } else {
      FORWARD_USER_ERROR(ret, "unexpected http message");
    }
  } 
  return ret;
}

int ObAIFuncModel::call_completion_vector(ObArray<ObString> &prompts, ObJsonObject *config, ObArray<ObString> &results)
{
  int ret = OB_SUCCESS;
  ObArray<ObString> headers;
  ObJsonObject *body = nullptr;
  ObArray<ObJsonObject *> body_array;
  ObArray<ObJsonObject *> response_array;
  ObIJsonBase *result_base = nullptr;
  ObAIFuncIComplete *complete_provider = nullptr;
  ObString prompt_str;
  ObString result_str;
  ObAIFuncClient client;
  ObString unencrypted_access_key;
  ObString request_model_name = get_request_model_name();
  if (!is_completion_type()) {
    ret = OB_INVALID_ARGUMENT;
    LOG_USER_ERROR(OB_INVALID_ARGUMENT, "ai_complete, info type is not completion");
  } else if (OB_FAIL(ObAIFuncUtils::get_complete_provider(*allocator_, endpoint_info_.get_provider(), complete_provider))) {
  } else if (OB_FAIL(endpoint_info_.get_unencrypted_access_key(*allocator_, unencrypted_access_key))) {
  } else if (OB_FAIL(complete_provider->get_header(*allocator_, unencrypted_access_key, headers))) {
  } else {
    for (int i = 0; OB_SUCC(ret) && i < prompts.count(); i++) {
      ObString prompt = prompts[i];
      if (OB_FAIL(complete_provider->get_body(*allocator_, request_model_name, prompt_str, prompt, config, body))) {
      } else if (OB_FAIL(body_array.push_back(body))) {
      }
    }
  }

  if (OB_FAIL(ret)) {
  } else if (OB_FAIL(client.send_post_batch(*allocator_, endpoint_info_.get_url(), headers, body_array, response_array))) {
  } else {
    for (int i = 0; OB_SUCC(ret) && i < response_array.count(); i++) {
      ObJsonObject *response = response_array[i];
      ObStringBuffer result_buf(allocator_);
      if (OB_FAIL(complete_provider->parse_output(*allocator_, response, result_base))) {
      } else if (OB_FAIL(result_base->print(result_buf, 0))) {
      } else {
        results.push_back(result_buf.string());
      }
    }
  }
  return ret;
}

int ObAIFuncModel::call_dense_embedding(ObString &content, ObJsonObject *config, ObString &result)
{
  int ret = OB_SUCCESS;
  ObArray<ObString> contents;
  ObArray<ObString> results;
  if (content.empty()) {
    ret = OB_INVALID_ARGUMENT;
    LOG_USER_ERROR(OB_INVALID_ARGUMENT, "ai_embed, input is empty");
  } else if (!is_dense_embedding_type()) {
    ret = OB_INVALID_ARGUMENT;
    LOG_USER_ERROR(OB_INVALID_ARGUMENT, "ai_embed, info type is not dense embedding");
  } else if (OB_FAIL(contents.push_back(content))) {
  } else if (OB_FAIL(call_dense_embedding_vector_v2(contents, config, results))) {
  } else if (results.count() != 1) {
    ret = OB_ERR_UNEXPECTED;
  } else {
    result = results[0];
  }
  return ret;
}

int ObAIFuncModel::call_dense_embedding_vector(ObArray<ObString> &contents, ObJsonObject *config, ObArray<ObString> &results)
{
  int ret = OB_SUCCESS;
  ObArray<ObString> headers;
  ObJsonObject *body = nullptr;
  ObArray<ObJsonObject *> body_array;
  ObArray<ObJsonObject *> response_array;
  ObIJsonBase *result_base = nullptr;
  ObAIFuncIEmbed *embed_provider = nullptr;
  ObAIFuncClient client;
  ObString request_model_name = get_request_model_name();
  ObString unencrypted_access_key;
  if (!is_dense_embedding_type()) {
    ret = OB_INVALID_ARGUMENT;
    LOG_USER_ERROR(OB_INVALID_ARGUMENT, "ai_embed, info type is not dense embedding");
  } else if (OB_FAIL(ObAIFuncUtils::get_embed_provider(*allocator_, endpoint_info_.get_provider(), embed_provider))) {
  } else if (OB_FAIL(endpoint_info_.get_unencrypted_access_key(*allocator_, unencrypted_access_key))) {
  } else if (OB_FAIL(embed_provider->get_header(*allocator_, unencrypted_access_key, headers))) {
  } else {
    for (int i = 0; OB_SUCC(ret) && i < contents.count(); i++) {
      ObString content = contents[i];
      ObArray<ObString> content_array;
      if (OB_FAIL(content_array.push_back(content))) {
      } else if (OB_FAIL(embed_provider->get_body(*allocator_, request_model_name, content_array, config, body))) {
      } else if (OB_FAIL(body_array.push_back(body))) {
      }
    }
  }

  if (OB_FAIL(ret)) {
  } else if (OB_FAIL(client.send_post_batch(*allocator_, endpoint_info_.get_url(), headers, body_array, response_array))) {
  } else {
    for (int i = 0; OB_SUCC(ret) && i < response_array.count(); i++) {
      ObJsonObject *response = response_array[i];
      ObStringBuffer result_buf(allocator_);
      if (OB_FAIL(embed_provider->parse_output(*allocator_, response, result_base))) {
      } else if (OB_FAIL(result_base->print(result_buf, 0))) {
      } else {
        results.push_back(result_buf.string());
      }
    }
  }
  return ret;
}


int ObAIFuncModel::call_dense_embedding_vector_v2(ObArray<ObString> &contents,
    ObJsonObject *config, ObArray<ObString> &results)
{
  int ret = OB_SUCCESS;
  results.reset();
  ObArray<ObString> headers;
  ObJsonObject *body = nullptr;
  ObJsonObject *response = nullptr;
  ObIJsonBase *result_base = nullptr;
  ObAIFuncIEmbed *embed_provider = nullptr;
  ObAIFuncClient client;
  ObString unencrypted_access_key;
  ObString request_model_name = get_request_model_name();
  int64_t dimension = 0;
  if (OB_NOT_NULL(config)) {
    ObJsonNode *dimension_node = config->get_value("dimensions");
    if (OB_NOT_NULL(dimension_node)) {
      if (ObJsonNodeType::J_INT != dimension_node->json_type()
          || static_cast<ObJsonInt *>(dimension_node)->get_int() <= 0) {
        // the dimension of the request must be a positive integer
        ret = OB_INVALID_ARGUMENT;
        LOG_USER_ERROR(OB_INVALID_ARGUMENT, "ai_embed, dimension parameter must be a positive integer");
      } else {
        dimension = static_cast<ObJsonInt *>(dimension_node)->get_int();
      }
    }
  }
  if (OB_FAIL(ret)) {
  } else if (!is_dense_embedding_type() || contents.empty()) {
    ret = OB_INVALID_ARGUMENT;
    LOG_USER_ERROR(OB_INVALID_ARGUMENT, "ai_embed, info type is not dense embedding or input is empty");
  } else if (OB_FAIL(ObAIFuncUtils::get_embed_provider(*allocator_, endpoint_info_.get_provider(), embed_provider))) {
  } else if (OB_FAIL(endpoint_info_.get_unencrypted_access_key(*allocator_, unencrypted_access_key))) {
  } else if (OB_FAIL(embed_provider->get_header(*allocator_, unencrypted_access_key, headers))) {
  } else {
    // The config of the request is cloned: a provider may change it, for
    // example the dashscope provider renames the dimension parameter.
    ObJsonObject *request_config = nullptr;
    if (OB_NOT_NULL(config)
        && OB_ISNULL(request_config = static_cast<ObJsonObject *>(config->clone(allocator_, true)))) {
      ret = OB_ALLOCATE_MEMORY_FAILED;
      LOG_WARN("failed to clone the config of the request", K(ret));
    } else if (OB_FAIL(embed_provider->get_body(*allocator_, request_model_name, contents, request_config, body))) {
    } else if (OB_FAIL(client.send_post(*allocator_, endpoint_info_.get_url(), headers, body, response))) {
    } else if (OB_FAIL(embed_provider->parse_output(*allocator_, response, result_base))) {
    } else if (OB_FAIL(ObAIFuncUtils::validate_embedding_result(*allocator_, result_base, dimension))) {
    } else if (result_base->element_count() != contents.count()) {
      // the model must answer with one embedding for every input
      ret = OB_INVALID_DATA;
      LOG_WARN("the number of the embeddings is not equal to the number of the inputs",
               K(ret), K(result_base->element_count()), K(contents.count()));
    } else {
      for (int64_t i = 0; OB_SUCC(ret) && i < contents.count(); i++) {
        ObIJsonBase *embedding = nullptr;
        ObString result_str;
        if (OB_FAIL(result_base->get_array_element(i, embedding))) {
          LOG_WARN("failed to get the embedding of the answer", K(ret), K(i));
        } else if (OB_FAIL(ObAIFuncJsonUtils::print_json_to_str(*allocator_, embedding, result_str))) {
        } else if (OB_FAIL(results.push_back(result_str))) {
          LOG_WARN("failed to push the embedding of the answer", K(ret), K(i));
        }
      }
    }
  }
  if (ret == OB_INVALID_DATA) {
    ObString response_str;
    if (OB_SUCCESS == ObAIFuncJsonUtils::print_json_to_str(*allocator_, response, response_str)) {
      char http_message_str[1024];
      snprintf(http_message_str, sizeof(http_message_str), "unexpected http message: %.*s",
               static_cast<int>(response_str.length()), response_str.ptr());
      ObString ob_http_message_str(http_message_str);
      FORWARD_USER_ERROR(ret, ob_http_message_str.ptr());
    } else {
      FORWARD_USER_ERROR(ret, "unexpected http message");
    }
  }
  return ret;
}

int ObAIFuncModel::call_rerank(ObString &query, ObJsonArray *contents, ObJsonArray *&results)
{
  int ret = OB_SUCCESS;
  ObArray<ObString> headers;
  ObJsonObject *body = nullptr;
  ObJsonObject *response = nullptr;
  ObIJsonBase *result_base = nullptr;
  ObAIFuncIRerank *rerank_provider = nullptr;
  ObAIFuncClient client;
  ObString unencrypted_access_key;
  ObString request_model_name = get_request_model_name();
  if (!is_rerank_type()) {
    ret = OB_INVALID_ARGUMENT;
    LOG_USER_ERROR(OB_INVALID_ARGUMENT, "ai_rerank, info type is not rerank");
  } else if (OB_FAIL(ObAIFuncUtils::get_rerank_provider(*allocator_, endpoint_info_.get_provider(), rerank_provider))) {
  } else if (OB_FAIL(endpoint_info_.get_unencrypted_access_key(*allocator_, unencrypted_access_key))) {
  } else if (OB_FAIL(rerank_provider->get_header(*allocator_, unencrypted_access_key, headers))) {
  } else if (OB_FAIL(rerank_provider->get_body(*allocator_, request_model_name, query, contents, nullptr, body))) {
  } else if (OB_FAIL(client.send_post(*allocator_, endpoint_info_.get_url(), headers, body, response))) {
  } else if (OB_FAIL(rerank_provider->parse_output(*allocator_, response, result_base))) {
  } else {
    results = static_cast<ObJsonArray *>(result_base);
  }

  if (ret == OB_INVALID_DATA) {
    ObString response_str;
    if (OB_SUCCESS == ObAIFuncJsonUtils::print_json_to_str(*allocator_, response, response_str)) {
      char http_message_str[1024];
      snprintf(http_message_str, sizeof(http_message_str), "unexpected http message: %.*s",
               static_cast<int>(response_str.length()), response_str.ptr());
      ObString ob_http_message_str(http_message_str);
      FORWARD_USER_ERROR(ret, ob_http_message_str.ptr());
    } else {
      FORWARD_USER_ERROR(ret, "unexpected http message");
    }
  } 
  return ret;
}

bool ObAIFuncJsonUtils::ob_is_json_array_all_str(ObJsonArray* json_array)
{
  bool is_all_str = true;
  if (OB_ISNULL(json_array)) {
    is_all_str = false;
  }
  for (int64_t i = 0; is_all_str && i < json_array->element_count(); i++) {
    ObJsonNode *node = json_array->get_value(i);
    if (OB_ISNULL(node)) {
      is_all_str = false;
    } else if (node->json_type() != ObJsonNodeType::J_STRING) {
      is_all_str = false;
    }
  }
  return is_all_str;
}

int ObAIFuncPromptObjectUtils::construct_prompt_object(ObIAllocator &allocator, ObString &template_str, ObJsonArray *args_array, ObJsonObject *&prompt_object)
{
  INIT_SUCC(ret);
  ObJsonObject *prompt_obj = NULL;
  ObJsonString *template_json_str = NULL;
  if (template_str.empty() || args_array == NULL) {
    ret = OB_INVALID_ARGUMENT;
  } else if (OB_FAIL(ObAIFuncJsonUtils::get_json_string(allocator, template_str, template_json_str))) {
  } else if (OB_FAIL(ObAIFuncJsonUtils::get_json_object(allocator, prompt_obj))) {
  } else if (OB_FAIL(prompt_obj->add(ObAIFuncPromptObjectUtils::prompt_template_key, template_json_str))) {
  } else if (OB_FAIL(prompt_obj->add(ObAIFuncPromptObjectUtils::prompt_args_key, args_array))) {
  }
  if (OB_SUCC(ret)) {
    prompt_object = prompt_obj;
  }
  return ret;
}

namespace
{
// The number of the nested prompt objects that may be rendered, it stops the
// recursion of a malicious template.
constexpr int64_t PROMPT_RENDER_MAX_DEPTH = 64;

// Check whether a json node can be an argument of a prompt object. Every json
// value is accepted but the json null, it carries no value.
static bool is_prompt_arg_valid(const ObJsonNode *arg_node)
{
  return OB_NOT_NULL(arg_node) && ObJsonNodeType::J_NULL != arg_node->json_type();
}

// Parse the placeholder that starts at the given position of the template, a
// placeholder is a '{', an optional minus sign, a decimal index and a '}',
// for example {0}. A negative index is parsed and rejected by the caller.
// @return true when a placeholder starts at the position
static bool parse_prompt_placeholder(const char *template_ptr,
                                     const int64_t template_len,
                                     const int64_t start_pos,
                                     int64_t &end_pos,
                                     int64_t &index,
                                     bool &is_overflow)
{
  bool is_placeholder = false;
  bool overflow = false;
  int64_t digit_count = 0;
  int64_t value = 0;
  int64_t pos = start_pos + 1;
  bool negative = false;
  if (pos < template_len && '-' == template_ptr[pos]) {
    negative = true;
    ++pos;
  }
  while (pos < template_len && template_ptr[pos] >= '0' && template_ptr[pos] <= '9') {
    const int64_t digit = template_ptr[pos] - '0';
    if (!overflow) {
      if (value > (INT64_MAX - digit) / 10) {
        // the index does not fit into an int64
        overflow = true;
      } else {
        value = value * 10 + digit;
      }
    }
    ++digit_count;
    ++pos;
  }
  if (digit_count > 0 && pos < template_len && '}' == template_ptr[pos]) {
    is_placeholder = true;
    end_pos = pos;
    index = negative ? -1 : value;
    is_overflow = overflow;
  }
  return is_placeholder;
}

static int render_prompt_arg_text(ObIAllocator &allocator,
                                  ObJsonNode *arg_node,
                                  const int64_t depth,
                                  ObString &arg_text);

/**
 * @brief Render the template of a prompt object with its arguments.
 *        The same placeholder may be used more than once in the template.
 * @param allocator the allocator of the result
 * @param template_str the template of the prompt object
 * @param args_array the arguments of the prompt object
 * @param depth the depth of the prompt object in the nested prompt objects
 * @param replaced_prompt_str the result of the render
 */
static int render_prompt_template(ObIAllocator &allocator,
                                  const ObString &template_str,
                                  ObJsonArray *args_array,
                                  const int64_t depth,
                                  ObString &replaced_prompt_str)
{
  int ret = OB_SUCCESS;
  if (template_str.empty() || OB_ISNULL(args_array)) {
    ret = OB_INVALID_ARGUMENT;
    LOG_WARN("the template is empty or the args array is null", K(ret));
  } else if (depth > PROMPT_RENDER_MAX_DEPTH) {
    // the arguments must not nest the prompt objects too deeply
    ret = OB_INVALID_ARGUMENT;
    LOG_USER_ERROR(OB_INVALID_ARGUMENT, "ai_prompt: the nested prompt object is too deep");
    LOG_WARN("the prompt object is nested too deeply", K(ret), K(depth));
  } else {
    const int64_t args_count = args_array->element_count();
    const char *template_ptr = template_str.ptr();
    const int64_t template_len = template_str.length();
    // Every argument is converted to the text that replaces it once, the same
    // argument may be used by more than one placeholder.
    ObArray<ObString> arg_texts;
    ObStringBuffer result_buf(&allocator);
    for (int64_t i = 0; OB_SUCC(ret) && i < args_count; i++) {
      ObString arg_text;
      if (OB_FAIL(render_prompt_arg_text(allocator, args_array->get_value(i), depth, arg_text))) {
        LOG_WARN("failed to get the text of the argument", K(ret), K(i));
      } else if (OB_FAIL(arg_texts.push_back(arg_text))) {
        LOG_WARN("failed to push the text of the argument", K(ret), K(i));
      }
    }
    for (int64_t i = 0; OB_SUCC(ret) && i < template_len; i++) {
      const char template_char = template_ptr[i];
      if ('{' == template_char) {
        if (i + 1 < template_len && '{' == template_ptr[i + 1]) {
          // a double left brace is the escaped left brace of the template
          if (OB_FAIL(result_buf.append("{", 1))) {
            LOG_WARN("failed to append the escaped left brace", K(ret), K(i));
          } else {
            i++;
          }
        } else {
          int64_t end_pos = 0;
          int64_t index = 0;
          bool is_overflow = false;
          if (!parse_prompt_placeholder(template_ptr, template_len, i, end_pos, index, is_overflow)) {
            // Keep braces that are not numeric placeholders as normal template
            // text. This preserves prompts containing JSON or ordinary braces.
            if (OB_FAIL(result_buf.append(template_ptr + i, 1))) {
              LOG_WARN("failed to append the left brace", K(ret), K(i));
            }
          } else if (is_overflow || index < 0 || index >= args_count) {
            ret = OB_INVALID_ARGUMENT;
            LOG_USER_ERROR(OB_INVALID_ARGUMENT, "ai_prompt: invalid placeholder index");
            LOG_WARN("the placeholder index is invalid", K(ret), K(index), K(args_count), K(is_overflow));
          } else if (OB_FAIL(result_buf.append(arg_texts.at(index)))) {
            LOG_WARN("failed to append the text of the argument", K(ret), K(index));
          } else {
            i = end_pos;
          }
        }
      } else if ('}' == template_char) {
        if (i + 1 < template_len && '}' == template_ptr[i + 1]) {
          // a double right brace is the escaped right brace of the template
          if (OB_FAIL(result_buf.append("}", 1))) {
            LOG_WARN("failed to append the escaped right brace", K(ret), K(i));
          } else {
            i++;
          }
        } else if (OB_FAIL(result_buf.append(template_ptr + i, 1))) {
          // A standalone right brace is normal template text as well.
          LOG_WARN("failed to append the right brace", K(ret), K(i));
        }
      } else if (OB_FAIL(result_buf.append(template_ptr + i, 1))) {
        LOG_WARN("failed to append the char of the template", K(ret), K(i));
      }
    }
    if (OB_SUCC(ret)) {
      if (OB_FAIL(result_buf.get_result_string(replaced_prompt_str))) {
        LOG_WARN("failed to get the rendered template", K(ret));
      }
    }
  }
  return ret;
}

/**
 * @brief Get the text that replaces an argument of the template.
 *        A string argument is replaced by its content, a nested prompt object
 *        is rendered with its own arguments first and any other json value is
 *        replaced by its json text.
 */
static int render_prompt_arg_text(ObIAllocator &allocator,
                                  ObJsonNode *arg_node,
                                  const int64_t depth,
                                  ObString &arg_text)
{
  int ret = OB_SUCCESS;
  if (OB_ISNULL(arg_node)) {
    ret = OB_INVALID_ARGUMENT;
    LOG_WARN("the argument of the prompt is null", K(ret));
  } else if (ObJsonNodeType::J_NULL == arg_node->json_type()) {
    ret = OB_INVALID_ARGUMENT;
    LOG_WARN("the argument of the prompt is json null", K(ret));
  } else if (ObJsonNodeType::J_STRING == arg_node->json_type()) {
    arg_text = static_cast<ObJsonString *>(arg_node)->get_str();
  } else if (ObJsonNodeType::J_OBJECT == arg_node->json_type()
             && ObAIFuncPromptObjectUtils::is_valid_prompt_object(
                    static_cast<ObJsonObject *>(arg_node))) {
    // a nested prompt object is rendered with its own arguments
    ObJsonObject *nested_object = static_cast<ObJsonObject *>(arg_node);
    ObJsonNode *nested_template_node = nested_object->get_value(
        ObAIFuncPromptObjectUtils::prompt_template_key);
    ObJsonNode *nested_args_node = nested_object->get_value(
        ObAIFuncPromptObjectUtils::prompt_args_key);
    if (OB_ISNULL(nested_template_node) || OB_ISNULL(nested_args_node)) {
      ret = OB_INVALID_ARGUMENT;
      LOG_WARN("the nested prompt object is not valid", K(ret));
    } else if (OB_FAIL(render_prompt_template(allocator,
                                              static_cast<ObJsonString *>(nested_template_node)->get_str(),
                                              static_cast<ObJsonArray *>(nested_args_node),
                                              depth + 1,
                                              arg_text))) {
      LOG_WARN("failed to render the nested prompt object", K(ret), K(depth));
    }
  } else {
    // any other json value is replaced by its json text, the text of a json
    // string is quoted (a string argument is handled above)
    ObJsonBuffer buffer(&allocator);
    if (OB_FAIL(arg_node->print(buffer, true))) {
      LOG_WARN("failed to print the argument of the prompt", K(ret));
    } else if (OB_FAIL(buffer.get_result_string(arg_text))) {
      LOG_WARN("failed to get the text of the argument", K(ret));
    }
  }
  return ret;
}
} // namespace

bool ObAIFuncPromptObjectUtils::is_valid_prompt_object(ObJsonObject *prompt_object)
{
  bool is_valid = true;
  ObJsonNode *template_node = NULL;
  ObJsonNode *args_node = NULL;
  if (OB_ISNULL(prompt_object)) {
    is_valid = false;
  } else if (prompt_object->element_count() != 2) {
    is_valid = false;
  } else if (OB_ISNULL(template_node = prompt_object->get_value(ObAIFuncPromptObjectUtils::prompt_template_key)) ||
            (OB_ISNULL(args_node = prompt_object->get_value(ObAIFuncPromptObjectUtils::prompt_args_key)))) {
    is_valid = false;
  } else if (template_node->json_type() != ObJsonNodeType::J_STRING || args_node->json_type() != ObJsonNodeType::J_ARRAY) {
    is_valid = false;
  }
  if (is_valid) {
    ObJsonArray *args_array = static_cast<ObJsonArray *>(args_node);
    for (int64_t i = 0; is_valid && i < args_array->element_count(); i++) {
      // A json argument is accepted: a string is expanded as it is, an array,
      // an object, a number or a boolean is expanded as its json text and a
      // nested prompt object is rendered with its own arguments. The json null
      // carries no value, it is not a valid argument.
      is_valid = is_prompt_arg_valid(args_array->get_value(i));
    }
  }
  return is_valid;
}

int ObAIFuncPromptObjectUtils::replace_all_str_args_in_template(ObIAllocator &allocator, ObJsonObject* prompt_object, ObString& replaced_prompt_str)
{
  INIT_SUCC(ret);
  ObJsonNode *template_node = NULL;
  ObJsonNode *args_node = NULL;
  if (OB_ISNULL(prompt_object)) {
    ret = OB_INVALID_ARGUMENT;
    LOG_WARN("the prompt object is null", K(ret));
  } else if (!ObAIFuncPromptObjectUtils::is_valid_prompt_object(prompt_object)) {
    ret = OB_INVALID_ARGUMENT;
    LOG_WARN("the prompt object is not valid", K(ret));
  } else if (OB_ISNULL(template_node = prompt_object->get_value(ObAIFuncPromptObjectUtils::prompt_template_key))
             || ObJsonNodeType::J_STRING != template_node->json_type()) {
    ret = OB_INVALID_ARGUMENT;
    LOG_WARN("the template of the prompt object is not a string", K(ret));
  } else if (OB_ISNULL(args_node = prompt_object->get_value(ObAIFuncPromptObjectUtils::prompt_args_key))
             || ObJsonNodeType::J_ARRAY != args_node->json_type()) {
    ret = OB_INVALID_ARGUMENT;
    LOG_WARN("the args of the prompt object is not an array", K(ret));
  } else if (static_cast<ObJsonString *>(template_node)->get_str().empty()) {
    ret = OB_INVALID_ARGUMENT;
    LOG_WARN("the template of the prompt object is empty", K(ret));
  } else {
    // The result buffer grows with the text that it holds, a placeholder may be
    // used more than once and an argument may be a long json text.
    if (OB_FAIL(render_prompt_template(allocator,
                                       static_cast<ObJsonString *>(template_node)->get_str(),
                                       static_cast<ObJsonArray *>(args_node),
                                       0,
                                       replaced_prompt_str))) {
    }
  }
  return ret;
}
bool ObAIFuncPromptObjectUtils::is_valid_prompt_arg(const ObIJsonBase *arg_node)
{
  // every json value is accepted but the json null, it carries no value
  return OB_NOT_NULL(arg_node) && ObJsonNodeType::J_NULL != arg_node->json_type();
}

int ObAIFuncPromptObjectUtils::get_prompt_arg_text(ObIAllocator &allocator,
                                                   ObJsonNode *arg_node,
                                                   ObString &arg_text)
{
  return render_prompt_arg_text(allocator, arg_node, 0, arg_text);
}

const ObString ObAIFuncModel::get_request_model_name()
{
  ObString request_model_name = info_.model_;
  if (!endpoint_info_.get_request_model_name().empty()) {
    request_model_name = endpoint_info_.get_request_model_name();
  }
  return request_model_name;
}

} // namespace common

namespace query
{

int ObAIModelResolver::resolve_model_name(
    common::ObIAllocator &allocator,
    const common::ObString &model_id,
    common::ObString &model_name)
{
  int ret = OB_SUCCESS;
  common::ObAIFuncExprInfo *info = nullptr;
  if (OB_FAIL(common::ObAIFuncUtils::get_ai_func_info(
          allocator, model_id, info))) {
  } else if (OB_ISNULL(info)) {
    ret = OB_ERR_UNEXPECTED;
  } else if (OB_FAIL(ob_write_string(allocator, info->model_, model_name))) {
  }
  return ret;
}

} // namespace query
} // namespace oceanbase
