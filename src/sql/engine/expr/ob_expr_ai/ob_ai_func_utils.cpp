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
#include "common/json_type/json_builder.h"
#include "common/json_type/ob_json_parse.h"
#include "lib/container/ob_se_array.h"
#include "lib/utility/ob_common_utility.h"
#include "ob_ai_func_client.h"
#include "rust/json.h"
#include "query/engine/expr/ob_ai_model_resolver.h"

namespace oceanbase 
{
namespace common 
{

namespace {
int select_response(ObIAllocator &allocator, ObIJsonBase &response,
                    const ObString &path, int missing_error, ObIJsonBase *&result)
{
  int ret = OB_SUCCESS;
  ObString input;
  if (OB_FAIL(response.get_raw_binary(input, &allocator))) {
  } else {
    auto parsed = json::parse_mysql_path(rust::Slice<const uint8_t>(
        reinterpret_cast<const uint8_t *>(path.ptr()), path.length()));
    auto query = json::new_query(rust::Slice<const uint8_t>(
        reinterpret_cast<const uint8_t *>(input.ptr()), input.length()), false, 100);
    if (parsed->status() != json::Status::Ok || query->status() != json::Status::Ok) {
      ret = OB_ERR_UNEXPECTED;
    } else {
      const json::BinaryResult selected = query->first_value(parsed->path(), false);
      if (selected.status != json::Status::Ok) {
        ret = OB_ERR_UNEXPECTED;
      } else if (selected.binary.empty()) {
        ret = missing_error;
      } else {
        ObString binary;
        if (OB_FAIL(ob_write_string(allocator, ObString(selected.binary.size(),
            reinterpret_cast<const char *>(selected.binary.data())), binary))) {
        } else {
          ret = ObJsonBaseFactory::get_json_base(&allocator, binary,
              ObJsonInType::JSON_BIN, result);
        }
      }
    }
  }
  return ret;
}
}

int ObOpenAIUtils::get_header(common::ObIAllocator &allocator,
                              ObString &api_key,
                              common::ObArray<ObString> &headers) 
{
  int ret = OB_SUCCESS;
  if (api_key.empty()) {
    ret = OB_INVALID_ARGUMENT;
  } else {
    //["Authorization: Bearer %.*s", "Content-Type: application/json"]
    int auth_header_len = 1024;
    char *auth_header_str = (char *)allocator.alloc(auth_header_len);
    ObString content_type_str("Content-Type: application/json");
    ObString content_type_c_str;
    if (OB_FAIL(ob_write_string(allocator, content_type_str, content_type_c_str, true))) {
    } else if (OB_ISNULL(auth_header_str)) {
      ret = OB_ALLOCATE_MEMORY_FAILED;
      LOG_WARN("Failed to allocate memory for auth header string and content type string", K(ret));
    } else {
      int auth_header_pos = snprintf(auth_header_str, auth_header_len,
                         "Authorization: Bearer %.*s", api_key.length(), api_key.ptr());
      if (auth_header_pos < 0) {
        ret = OB_ERR_UNEXPECTED;
      } else if (OB_FAIL(headers.push_back(ObString(auth_header_str)))) {
      } else if (OB_FAIL(headers.push_back(content_type_c_str))) {
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
                                              common::ObIJsonBase *&config,
                                              common::ObIJsonBase *&body)
{
  int ret = OB_SUCCESS;
  if (model.empty() || content.empty()) {
    ret = OB_INVALID_ARGUMENT;
  } else {
    // {"model": "*", "messages": [{"role": "system", "content": "*"}, {"role": "user", "content": "*"}]}
    ObIJsonBase *body_obj = nullptr;
    ObIJsonBase *model_str = nullptr;
    ObIJsonBase *messages_array = nullptr;
    if (OB_FAIL(ObAIFuncJsonUtils::get_json_object(allocator, body_obj))) {
    } else if (OB_FAIL(ObAIFuncJsonUtils::get_json_string(allocator, model, model_str))) {
    } else if (OB_FAIL(ObAIFuncJsonUtils::object_set(allocator, body_obj, "model", model_str))) {
    } else if (OB_FAIL(construct_messages_array(allocator, prompt, content, messages_array))) {
    } else if (OB_FAIL(ObAIFuncJsonUtils::object_set(allocator, body_obj, "messages", messages_array))) {
    } else if (OB_FAIL(ObAIFuncJsonUtils::compact_json_object(allocator, config, body_obj))) {
    } else {
      body = body_obj;
    }
  }
  return ret;
}

int ObOpenAIUtils::ObOpenAIComplete::construct_messages_array(ObIAllocator &allocator, ObString &prompt, ObString &content, ObIJsonBase *&messages)
{
  int ret = OB_SUCCESS;
  JsonContainerBuilder builder(false);
  if (content.empty()) {
    ret = OB_INVALID_ARGUMENT;
  } else {
    //messages: [{"role": "system", "content": "You are a helpful assistant."}, {"role": "user", "content": "What is the capital of France?"}]
    ObIJsonBase *messages_array = nullptr;
    ObIJsonBase *sys_message_obj = nullptr;
    ObIJsonBase *user_message_obj = nullptr;
    ObString system_str("system");
    ObString user_str("user");
    if (OB_FAIL(ObAIFuncJsonUtils::get_json_array(allocator, messages_array))) {
    } else if(!prompt.empty()) {
      if (OB_FAIL(construct_message_obj(allocator, system_str, prompt, sys_message_obj))) {
      } else if (OB_FAIL(ObAIFuncJsonUtils::append_value(allocator, builder, sys_message_obj))) {
      }
    } 
    if (OB_SUCC(ret)) {
      if (OB_FAIL(construct_message_obj(allocator, user_str, content, user_message_obj))) {
      } else if (OB_FAIL(ObAIFuncJsonUtils::append_value(allocator, builder, user_message_obj))) {
      } else if (OB_FAIL(ObAIFuncJsonUtils::finish_array(allocator, builder, messages_array))) {
      } else {
        messages = messages_array;
      }
    }
  }
  return ret;
}

int ObOpenAIUtils::ObOpenAIComplete::construct_message_obj(ObIAllocator &allocator, ObString &role, ObString &content, ObIJsonBase *&message)
{
  int ret = OB_SUCCESS;
  if (role.empty() || content.empty()) {
    ret = OB_INVALID_ARGUMENT;
  } else {
    ObIJsonBase *message_obj = nullptr;
    ObIJsonBase *role_json_str = nullptr;
    ObIJsonBase *content_json_str = nullptr;
    if (OB_FAIL(ObAIFuncJsonUtils::get_json_object(allocator, message_obj))) {
    } else if (OB_FAIL(ObAIFuncJsonUtils::get_json_string(allocator, role, role_json_str))) {
    } else if (OB_FAIL(ObAIFuncJsonUtils::object_set(allocator, message_obj, "role", role_json_str))) {
    } else if (OB_FAIL(ObAIFuncJsonUtils::get_json_string(allocator, content, content_json_str))) {
    } else if (OB_FAIL(ObAIFuncJsonUtils::object_set(allocator, message_obj, "content", content_json_str))) {
    } else {
      message = message_obj;
    }
  }
  return ret;
}

int ObOpenAIUtils::ObOpenAIComplete::set_config_json_format(common::ObIAllocator &allocator, common::ObIJsonBase *&config)
{
  int ret = OB_SUCCESS;
  if (OB_ISNULL(config)) {
    ret = OB_INVALID_ARGUMENT;
  } else {
    // {"response_format":{"type":"json_object"}}
    ObString json_str("json_object");
    ObIJsonBase *json_str_obj = nullptr;
    ObIJsonBase *json_obj = nullptr;
    if (OB_FAIL(ObAIFuncJsonUtils::get_json_object(allocator, json_obj))) {
    } else if (OB_FAIL(ObAIFuncJsonUtils::get_json_string(allocator, json_str, json_str_obj))) {
    } else if (OB_FAIL(ObAIFuncJsonUtils::object_set(allocator, json_obj, "type", json_str_obj))) {
    } else if (OB_FAIL(ObAIFuncJsonUtils::object_set(allocator, config, "response_format", json_obj))) {
    }
  }
  return ret;
}

int ObOpenAIUtils::ObOpenAIComplete::parse_output(common::ObIAllocator &allocator, 
                                                  common::ObIJsonBase *http_response,
                                                  common::ObIJsonBase *&result) 
{
  int ret = OB_SUCCESS;
  if (OB_ISNULL(http_response)) {
    ret = OB_INVALID_ARGUMENT;
  } else {
    ret = select_response(allocator, *http_response, ObString("$.choices[0].message.content"), OB_INVALID_DATA, result);
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
                                           common::ObIJsonBase *&config,
                                           common::ObIJsonBase *&body)
{
  int ret = OB_SUCCESS;
  if (model.empty() || contents.empty()) {
    ret = OB_INVALID_ARGUMENT;
  } else {
    ObIJsonBase *body_obj = nullptr;
    ObIJsonBase *model_str = nullptr;
    ObIJsonBase *input_array = nullptr;
    if (OB_FAIL(ObAIFuncJsonUtils::get_json_object(allocator, body_obj))) {
    } else if (OB_FAIL(ObAIFuncJsonUtils::get_json_string(allocator, model, model_str))) {
    } else if (OB_FAIL(ObAIFuncJsonUtils::object_set(allocator, body_obj, "model", model_str))) {
    } else if (OB_FAIL(ObAIFuncJsonUtils::transform_array_to_json_array(allocator, contents, input_array))) {
    } else if (OB_FAIL(ObAIFuncJsonUtils::object_set(allocator, body_obj, "input", input_array))) {
    } else if (OB_FAIL(ObAIFuncJsonUtils::compact_json_object(allocator, config, body_obj))) {
    } else {
      body = body_obj;
    }
  }
  return ret;
}

int ObOpenAIUtils::ObOpenAIEmbed::parse_output(common::ObIAllocator &allocator, 
                                               common::ObIJsonBase *http_response,
                                               common::ObIJsonBase *&result) 
{
  int ret = OB_SUCCESS;
  JsonContainerBuilder builder(false);
  if (OB_ISNULL(http_response)) {
    ret = OB_INVALID_ARGUMENT;
  } else {
    ObIJsonBase *result_array = nullptr;
    ObIJsonBase *data_node = nullptr;
    if (OB_FAIL(ObAIFuncJsonUtils::get_json_array(allocator, result_array))) {
    } else if (OB_ISNULL(data_node = ObAIFuncJsonUtils::get_value(http_response, "data"))) {
      ret = OB_INVALID_DATA;
    } else {
      ObIJsonBase *data_array = data_node;
      ObIJsonBase *embedding_node = nullptr;
      for (int64_t i = 0; OB_SUCC(ret) && i < data_array->element_count(); i++) {
        if (OB_ISNULL(embedding_node = ObAIFuncJsonUtils::get_value(data_array, i))) {
          ret = OB_INVALID_DATA;
        } else if (embedding_node->json_type() != ObJsonNodeType::J_OBJECT) {
          ret = OB_INVALID_DATA;
        } else {
          ObIJsonBase *embedding_obj = embedding_node;
          ObIJsonBase *embedding = ObAIFuncJsonUtils::get_value(embedding_obj, "embedding");
          if (OB_ISNULL(embedding)) {
            ret = OB_INVALID_DATA;
          } else if (OB_FAIL(ObAIFuncJsonUtils::append_value(allocator, builder, embedding))) {
          }
        }
      }
      if (OB_FAIL(ret)) {
      } else if (OB_FAIL(ObAIFuncJsonUtils::finish_array(allocator, builder, result_array))) {
      } else {
        result = result_array;
      }
    }
  }
  return ret;
}

int ObOllamaUtils::get_header(common::ObIAllocator &allocator,
                              common::ObArray<ObString> &headers) 
{
  int ret = OB_SUCCESS;
  // ollama header is empty, do nothing
  return ret;
}

int ObOllamaUtils::ObOllamaComplete::get_header(common::ObIAllocator &allocator,
                                                common::ObString &api_key,
                                                common::ObArray<ObString> &headers) 
{
  return ObOllamaUtils::get_header(allocator, headers);
}

int ObOllamaUtils::ObOllamaComplete::get_body(common::ObIAllocator &allocator,
                                              common::ObString &model,
                                              common::ObString &prompt,
                                              common::ObString &content,
                                              common::ObIJsonBase *&config,
                                              common::ObIJsonBase *&body)
{
  int ret = OB_SUCCESS;
  if (model.empty() || content.empty()) { 
    ret = OB_INVALID_ARGUMENT;
  } else {
    // {"model": "llama3.1", "prompt": "What is the capital of France?"}
    ObIJsonBase *body_obj = nullptr;
    ObIJsonBase *model_str = nullptr;
    ObIJsonBase *content_str = nullptr;
    if (OB_FAIL(ObAIFuncJsonUtils::get_json_object(allocator, body_obj))) {
    } else if (OB_FAIL(ObAIFuncJsonUtils::get_json_string(allocator, model, model_str))) {
    } else if (OB_FAIL(ObAIFuncJsonUtils::object_set(allocator, body_obj, "model", model_str))) {
    } else if (OB_FAIL(ObAIFuncJsonUtils::get_json_string(allocator, content, content_str))) {
    } else if (OB_FAIL(ObAIFuncJsonUtils::object_set(allocator, body_obj, "prompt", content_str))) {
    } else if (OB_FAIL(ObAIFuncJsonUtils::compact_json_object(allocator, config, body_obj))) {
    } else {
      body = body_obj;
    }
  }
  return ret;
}

int ObOllamaUtils::ObOllamaComplete::set_config_json_format(common::ObIAllocator &allocator, common::ObIJsonBase *&config)
{
  int ret = OB_SUCCESS;
  if (OB_ISNULL(config)) {
    ret = OB_INVALID_ARGUMENT;
  } else {
    // {"format": "json"}
    ObString json_str("json");
    ObIJsonBase *json_str_obj = nullptr;
    if (OB_FAIL(ObAIFuncJsonUtils::get_json_string(allocator, json_str, json_str_obj))) {
    } else if (OB_FAIL(ObAIFuncJsonUtils::object_set(allocator, config, "format", json_str_obj))) {
    } 
  }
  return ret;
}

int ObOllamaUtils::ObOllamaComplete::parse_output(common::ObIAllocator &allocator,
                                                  common::ObIJsonBase *http_response,
                                                  common::ObIJsonBase *&result) 
{
  int ret = OB_SUCCESS;
  if (OB_ISNULL(http_response)) {
    ret = OB_INVALID_ARGUMENT;
  } else {
    ret = select_response(allocator, *http_response, ObString("$.response"), OB_ERR_UNEXPECTED, result);
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
                                           common::ObIJsonBase *&config,
                                           common::ObIJsonBase *&body)
{
  int ret = OB_SUCCESS;
  if (model.empty() || contents.empty()) {
    ret = OB_INVALID_ARGUMENT;
  } else {
    ObIJsonBase *body_obj = nullptr;
    ObIJsonBase *model_str = nullptr;
    ObIJsonBase *input_array = nullptr;
    if (OB_FAIL(ObAIFuncJsonUtils::get_json_object(allocator, body_obj))) {
    } else if (OB_FAIL(ObAIFuncJsonUtils::get_json_string(allocator, model, model_str))) {
    } else if (OB_FAIL(ObAIFuncJsonUtils::object_set(allocator, body_obj, "model", model_str))) {
    } else if (OB_FAIL(ObAIFuncJsonUtils::transform_array_to_json_array(allocator, contents, input_array))) {
    } else if (OB_FAIL(ObAIFuncJsonUtils::object_set(allocator, body_obj, "input", input_array))) {
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
                                               common::ObIJsonBase *http_response,
                                               common::ObIJsonBase *&result) 
{
  int ret = OB_SUCCESS;
  if (OB_ISNULL(http_response)) {
    ret = OB_INVALID_ARGUMENT;
  } else {
    ret = select_response(allocator, *http_response, ObString("$.embeddings"), OB_ERR_UNEXPECTED, result);
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
                                                    common::ObIJsonBase *&config,
                                                    common::ObIJsonBase *&body)
{
  int ret = OB_SUCCESS;
  if (OB_ISNULL(model) || OB_ISNULL(content)) {
    ret = OB_INVALID_ARGUMENT;
  } else {
    // {"model": "*", "input": {"messages": [{"role": "system", "content": "*"}, {"role": "user", "content": "*"}]}, "parameters": {}}
    ObIJsonBase *body_obj = nullptr;
    ObIJsonBase *model_str = nullptr;
    ObIJsonBase *input_obj = nullptr;

    if (OB_ISNULL(config)) {
      if (OB_FAIL(ObAIFuncJsonUtils::get_json_object(allocator, config))) {
      }
    }
    if (OB_SUCC(ret)) {
      if (OB_FAIL(ObAIFuncJsonUtils::get_json_object(allocator, body_obj))) {
      } else if (OB_FAIL(ObAIFuncJsonUtils::get_json_string(allocator, model, model_str))) {
      } else if (OB_FAIL(ObAIFuncJsonUtils::object_set(allocator, body_obj, "model", model_str))) {
      } else if (OB_FAIL(ObDashscopeUtils::ObDashscopeComplete::construct_input_obj(allocator, prompt, content, input_obj))) {
      } else if (OB_FAIL(ObAIFuncJsonUtils::object_set(allocator, body_obj, "input", input_obj))) {
      } else if (OB_FAIL(ObDashscopeUtils::ObDashscopeComplete::set_config_result_format(allocator, config))) {
      } else if (OB_FAIL(ObAIFuncJsonUtils::object_set(allocator, body_obj, "parameters", config))) {
      } else {
        body = body_obj;
      }
    }
  }
  return ret;
}

int ObDashscopeUtils::ObDashscopeComplete::set_config_result_format(ObIAllocator &allocator, ObIJsonBase *&config)
{
  int ret = OB_SUCCESS;
  if (OB_ISNULL(config)) {
    ret = OB_INVALID_ARGUMENT;
  } 
  if (OB_SUCC(ret)) {
    // {"result_format": "message"}
    ObIJsonBase *result_format_str = nullptr;
    ObString result_format_str_val("message");
    if (OB_FAIL(ObAIFuncJsonUtils::get_json_string(allocator, result_format_str_val, result_format_str))) {
    } else if (OB_FAIL(ObAIFuncJsonUtils::object_set(allocator, config, "result_format", result_format_str))) {
    }
  } 
  return ret;
}

int ObDashscopeUtils::ObDashscopeComplete::construct_input_obj(ObIAllocator &allocator, ObString &prompt, ObString &content, ObIJsonBase *&input_obj)
{
  int ret = OB_SUCCESS;
  if (OB_ISNULL(content)) {
    ret = OB_INVALID_ARGUMENT;
  } else {
    ObIJsonBase *obj = nullptr;
    ObIJsonBase *messages_array = nullptr;
    if (OB_FAIL(ObAIFuncJsonUtils::get_json_object(allocator, obj))) {
    } else if (OB_FAIL(ObOpenAIUtils::ObOpenAIComplete::construct_messages_array(allocator, prompt, content, messages_array))) {
    } else if (OB_FAIL(ObAIFuncJsonUtils::object_set(allocator, obj, "messages", messages_array))) {
    } else {
      input_obj = obj;
    }
  }
  return ret;
}

int ObDashscopeUtils::ObDashscopeComplete::set_config_json_format(ObIAllocator &allocator, ObIJsonBase *&config)
{
  int ret = OB_SUCCESS;
  if (OB_ISNULL(config)) {
    ret = OB_INVALID_ARGUMENT;
  } else {
    //{"type": "json_object"}
    ObIJsonBase *type_str = nullptr;
    ObString type_str_val("json_object");
    if (OB_FAIL(ObAIFuncJsonUtils::get_json_string(allocator, type_str_val, type_str))) {
    } else if (OB_FAIL(ObAIFuncJsonUtils::object_set(allocator, config, "type", type_str))) {
    }
  }
  return ret;
}

int ObDashscopeUtils::ObDashscopeComplete::parse_output(ObIAllocator &allocator, ObIJsonBase *http_response, ObIJsonBase *&result)
{
  int ret = OB_SUCCESS;
  if (OB_ISNULL(http_response)) {
    ret = OB_INVALID_ARGUMENT;
  } else {
    ObIJsonBase *output_obj = nullptr;
    ObIJsonBase *choices_array = nullptr;
    ObIJsonBase *choice_obj = nullptr;
    ObIJsonBase *message_obj = nullptr;
    ObIJsonBase *content_str = nullptr;
    ObString response_str;
    if (OB_ISNULL(output_obj = ObAIFuncJsonUtils::get_value(http_response, "output"))) {
      ret = OB_INVALID_DATA;
    } else if (OB_ISNULL(choices_array = ObAIFuncJsonUtils::get_value(output_obj, "choices"))) {
      ret = OB_INVALID_DATA;
    } else if (OB_ISNULL(choice_obj = ObAIFuncJsonUtils::get_value(choices_array, 0))) {
      ret = OB_INVALID_DATA;
    } else if (OB_ISNULL(message_obj = ObAIFuncJsonUtils::get_value(choice_obj, "message"))) {
      ret = OB_INVALID_DATA;
    } else if (OB_ISNULL(content_str = ObAIFuncJsonUtils::get_value(message_obj, "content"))) {
      ret = OB_INVALID_DATA;
    } else {
      result = content_str;
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
                                                 common::ObIJsonBase *&config,
                                                 common::ObIJsonBase *&body)
{
  int ret = OB_SUCCESS;
  if (OB_ISNULL(model) || contents.empty()) {
    ret = OB_INVALID_ARGUMENT;
  } else {
    // {"model": "*", "input": {"texts": ["*"]}, "parameters": {}}
    ObIJsonBase *body_obj = nullptr;
    ObIJsonBase *model_str = nullptr;
    ObIJsonBase *input_obj = nullptr;
    ObIJsonBase *texts_array = nullptr;
    if (OB_FAIL(ObAIFuncJsonUtils::get_json_object(allocator, body_obj))) {
    } else if (OB_FAIL(ObAIFuncJsonUtils::get_json_string(allocator, model, model_str))) {
    } else if (OB_FAIL(ObAIFuncJsonUtils::object_set(allocator, body_obj, "model", model_str))) {
    } else if (OB_FAIL(ObAIFuncJsonUtils::get_json_object(allocator, input_obj))) {
    } else if (OB_FAIL(ObAIFuncJsonUtils::transform_array_to_json_array(allocator, contents, texts_array))) {
    } else if (OB_FAIL(ObAIFuncJsonUtils::object_set(allocator, input_obj, "texts", texts_array))) {
    } else if (OB_FAIL(ObAIFuncJsonUtils::object_set(allocator, body_obj, "input", input_obj))) {
    } else if (config != nullptr && config->element_count() > 0) {
      ObIJsonBase *dimensions_node = ObAIFuncJsonUtils::get_value(config, "dimensions");
      if (OB_ISNULL(dimensions_node)) {
        // do nothing
      } else if (OB_FAIL(ObAIFuncJsonUtils::rename_key(allocator, config, "dimensions", "dimension"))) {
      } 
      if (OB_SUCC(ret)) {
        if (OB_FAIL(ObAIFuncJsonUtils::object_set(allocator, body_obj, "parameters", config))) {
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
                                                    common::ObIJsonBase *http_response,
                                                    common::ObIJsonBase *&result) 
{
  int ret = OB_SUCCESS;
  JsonContainerBuilder builder(false);
  if (OB_ISNULL(http_response)) {
    ret = OB_INVALID_ARGUMENT;
  } else {
    // {"output": {"embeddings": [{"embedding": ["*"]}]}}
    ObIJsonBase *output_obj = nullptr;
    ObIJsonBase *embeddings_array = nullptr;
    ObIJsonBase *embedding_obj = nullptr;
    ObIJsonBase *embedding_array = nullptr;
    ObIJsonBase *result_array = nullptr;
    if (OB_FAIL(ObAIFuncJsonUtils::get_json_array(allocator, result_array))) {
    } else if (OB_ISNULL(output_obj = ObAIFuncJsonUtils::get_value(http_response, "output"))) {
      ret = OB_INVALID_DATA;
    } else if (OB_ISNULL(embeddings_array = ObAIFuncJsonUtils::get_value(output_obj, "embeddings"))) {
      ret = OB_INVALID_DATA;
    } else {
      for (int64_t i = 0; OB_SUCC(ret) && i < embeddings_array->element_count(); ++i) {
        if (OB_ISNULL(embedding_obj = ObAIFuncJsonUtils::get_value(embeddings_array, i))) {
          ret = OB_INVALID_DATA;
        } else if (OB_ISNULL(embedding_array = ObAIFuncJsonUtils::get_value(embedding_obj, "embedding"))) {
          ret = OB_INVALID_DATA;
        } else if (OB_FAIL(ObAIFuncJsonUtils::append_value(allocator, builder, embedding_array))) {
        }
      }
      if (OB_FAIL(ret)) {
      } else if (OB_FAIL(ObAIFuncJsonUtils::finish_array(allocator, builder, result_array))) {
      } else {
        result = result_array;
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
                                                  common::ObIJsonBase *document_array,
                                                  common::ObIJsonBase *&config,
                                                  common::ObIJsonBase *&body)
{
  int ret = OB_SUCCESS;
  if (OB_ISNULL(model) || OB_ISNULL(query) || OB_ISNULL(document_array)) {
    ret = OB_INVALID_ARGUMENT;
  } else {
    ObIJsonBase *body_obj = nullptr;
    ObIJsonBase *model_str = nullptr;
    ObIJsonBase *input_obj = nullptr;
    if (OB_FAIL(ObAIFuncJsonUtils::get_json_object(allocator, body_obj))) {
    } else if (OB_FAIL(ObAIFuncJsonUtils::get_json_string(allocator, model, model_str))) {
    } else if (OB_FAIL(ObAIFuncJsonUtils::object_set(allocator, body_obj, "model", model_str))) {
    } else if (OB_FAIL(ObDashscopeUtils::ObDashscopeRerank::get_input_obj(allocator, query, document_array, input_obj))) {
    } else if (OB_FAIL(ObAIFuncJsonUtils::object_set(allocator, body_obj, "input", input_obj))) {
    } else if (config != nullptr && config->element_count() > 0) {
      if (OB_FAIL(ObAIFuncJsonUtils::object_set(allocator, body_obj, "parameters", config))) {
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
                                                      common::ObIJsonBase *document_array,
                                                      common::ObIJsonBase *&input_obj)
{
  int ret = OB_SUCCESS;
  if (OB_ISNULL(query) || OB_ISNULL(document_array)) {
    ret = OB_INVALID_ARGUMENT;
  } else {
    ObIJsonBase *obj = nullptr;
    ObIJsonBase *query_str = nullptr;
    if (OB_FAIL(ObAIFuncJsonUtils::get_json_object(allocator, obj))) {
    } else if (OB_FAIL(ObAIFuncJsonUtils::get_json_string(allocator, query, query_str))) {
    } else if (OB_FAIL(ObAIFuncJsonUtils::object_set(allocator, obj, "query", query_str))) {
    } else if (OB_FAIL(ObAIFuncJsonUtils::object_set(allocator, obj, "documents", document_array))) {
    } else {
      input_obj = obj;
    }
  }
  return ret;
}

int ObDashscopeUtils::ObDashscopeRerank::parse_output(common::ObIAllocator &allocator,
                                                      common::ObIJsonBase *http_response,
                                                      common::ObIJsonBase *&result) 
{
  int ret = OB_SUCCESS;
  ObIJsonBase *output_obj = nullptr;
  ObIJsonBase *results_array = nullptr;
  if (OB_ISNULL(http_response)) {
    ret = OB_INVALID_ARGUMENT;
  } else if (OB_ISNULL(output_obj = ObAIFuncJsonUtils::get_value(http_response, "output"))) {
    ret = OB_INVALID_DATA;
  } else if (OB_ISNULL(results_array = ObAIFuncJsonUtils::get_value(output_obj, "results"))) {
    ret = OB_INVALID_DATA;
  } else {
    result = results_array;
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
                                                  common::ObIJsonBase *document_array,
                                                  common::ObIJsonBase *&config,
                                                  common::ObIJsonBase *&body)
{
  int ret = OB_SUCCESS;
  if (OB_ISNULL(model) || OB_ISNULL(query) || OB_ISNULL(document_array)) {
    ret = OB_INVALID_ARGUMENT;
  } else {
    ObIJsonBase *body_obj = nullptr;
    ObIJsonBase *model_str = nullptr;
    ObIJsonBase *query_str = nullptr;
    if (OB_FAIL(ObAIFuncJsonUtils::get_json_object(allocator, body_obj))) {
    } else if (OB_FAIL(ObAIFuncJsonUtils::get_json_string(allocator, model, model_str))) {
    } else if (OB_FAIL(ObAIFuncJsonUtils::object_set(allocator, body_obj, "model", model_str))) {
    } else if (OB_FAIL(ObAIFuncJsonUtils::get_json_string(allocator, query, query_str))) {
    } else if (OB_FAIL(ObAIFuncJsonUtils::object_set(allocator, body_obj, "query", query_str))) {
    } else if (OB_FAIL(ObAIFuncJsonUtils::object_set(allocator, body_obj, "documents", document_array))) {
    } else if (OB_FAIL(ObAIFuncJsonUtils::compact_json_object(allocator, config, body_obj))) {
    } else {
      body = body_obj;
    }
  }
  return ret;
}

int ObSiliconflowUtils::ObSiliconflowRerank::parse_output(common::ObIAllocator &allocator,
                                                      common::ObIJsonBase *http_response,
                                                      common::ObIJsonBase *&result) 
{
  int ret = OB_SUCCESS;
  ObIJsonBase *results_array = nullptr;
  if (OB_ISNULL(http_response)) {
    ret = OB_INVALID_ARGUMENT;
  } else if (OB_ISNULL(results_array = ObAIFuncJsonUtils::get_value(http_response, "results"))) {
    ret = OB_INVALID_DATA;
  } else {
    result = results_array;
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
                                    ObIJsonBase *&config,
                                    ObIJsonBase *&body)
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

int ObAIFuncUtils::set_json_format_config(ObIAllocator &allocator, const ObString &provider, ObIJsonBase *&config)
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
                                  ObIJsonBase *&config,
                                  ObIJsonBase *&body)
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
                                   ObIJsonBase *document_array,
                                   ObIJsonBase *&config,
                                   ObIJsonBase *&body)
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
                                        ObIJsonBase *http_response,
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
                                      ObIJsonBase *http_response,
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
                                       ObIJsonBase *http_response,
                                       ObIJsonBase *&result) 
{
  int ret = OB_SUCCESS;
  ObAIFuncIRerank *rerank_provider = nullptr;
  if (OB_FAIL(get_rerank_provider(allocator, endpoint_info.get_provider(), rerank_provider))) {
  } else if (OB_FAIL(rerank_provider->parse_output(allocator, http_response, result))) {
  }
  return ret;
}

static int rebuild_container(ObIAllocator &allocator, ObIJsonBase *&container,
                             bool object, const ObString &key, ObIJsonBase *value)
{
  int ret = OB_SUCCESS;
  JsonContainerBuilder builder(object);
  ObString source;
  ObString child;
  ObString output;
  if (container == nullptr || value == nullptr) { ret = OB_INVALID_ARGUMENT; }
  else if (OB_FAIL(container->get_raw_binary(source, &allocator))) {
  } else if (OB_FAIL(value->get_raw_binary(child, &allocator))) {
  } else if (OB_FAIL(builder.extend(source))) {
  } else if (OB_FAIL(builder.append(key, child))) {
  } else if (OB_FAIL(builder.finish(output))) {
  } else {
    ObIJsonBase *updated = nullptr;
    if (OB_FAIL(copy_json_binary(allocator, output, updated))) {
    } else { container = updated; }
  }
  return ret;
}

int ObAIFuncJsonUtils::rename_key(ObIAllocator &allocator, ObIJsonBase *&object,
                                 const ObString &old_key, const ObString &new_key)
{
  int ret = OB_SUCCESS;
  JsonContainerBuilder builder(true);
  ObString source;
  ObString binary;
  ObIJsonBase *updated = nullptr;
  if (object == nullptr) { ret = OB_INVALID_ARGUMENT; }
  else if (OB_FAIL(object->get_raw_binary(source, &allocator))) {
  } else if (OB_FAIL(builder.extend(source))) {
  } else if (OB_FAIL(builder.rename_key(old_key, new_key))) {
  } else if (OB_FAIL(builder.finish(binary))) {
  } else if (OB_FAIL(copy_json_binary(allocator, binary, updated))) {
  } else { object = updated; }
  return ret;
}

int ObAIFuncJsonUtils::object_set(ObIAllocator &allocator, ObIJsonBase *&object,
                                 const ObString &key, ObIJsonBase *value)
{
  return rebuild_container(allocator, object, true, key, value);
}

int ObAIFuncJsonUtils::append_value(ObIAllocator &allocator, JsonContainerBuilder &builder,
                                    ObIJsonBase *value)
{
  ObString binary;
  int ret = value == nullptr ? OB_INVALID_ARGUMENT : value->get_raw_binary(binary, &allocator);
  if (ret == OB_SUCCESS) { ret = builder.append(ObString(), binary); }
  return ret;
}

int ObAIFuncJsonUtils::finish_array(ObIAllocator &allocator, JsonContainerBuilder &builder,
                                    ObIJsonBase *&result)
{
  ObString binary;
  ObIJsonBase *updated = nullptr;
  int ret = builder.finish(binary);
  if (ret == OB_SUCCESS) { ret = copy_json_binary(allocator, binary, updated); }
  if (ret == OB_SUCCESS) { result = updated; }
  return ret;
}

ObIJsonBase *ObAIFuncJsonUtils::get_value(ObIJsonBase *value, const ObString &key)
{
  ObIJsonBase *result = nullptr;
  if (value != nullptr) { value->get_object_value(key, result); }
  return result;
}

ObIJsonBase *ObAIFuncJsonUtils::get_value(ObIJsonBase *value, uint64_t index)
{
  ObIJsonBase *result = nullptr;
  if (value != nullptr) {
    if (value->json_type() == ObJsonNodeType::J_OBJECT) { value->get_object_value(index, result); }
    else { value->get_array_element(index, result); }
  }
  return result;
}

ObString ObAIFuncJsonUtils::string_value(ObIJsonBase *value)
{
  return value == nullptr ? ObString() : ObString(value->get_data_length(), value->get_data());
}

ObIJsonBase *ObAIFuncJsonUtils::clone_json(ObIAllocator &allocator, ObIJsonBase *value)
{
  ObIJsonBase *result = nullptr;
  ObString binary;
  if (value != nullptr && OB_SUCCESS == value->get_raw_binary(binary, &allocator)) {
    copy_json_binary(allocator, binary, result);
  }
  return result;
}

int ObAIFuncJsonUtils::get_json_object(ObIAllocator &allocator, ObIJsonBase *&value)
{
  JsonContainerBuilder builder(true);
  ObString binary;
  int ret = builder.finish(binary);
  if (ret == OB_SUCCESS) { ret = copy_json_binary(allocator, binary, value); }
  return ret;
}

int ObAIFuncJsonUtils::get_json_array(ObIAllocator &allocator, ObIJsonBase *&value)
{
  JsonContainerBuilder builder(false);
  ObString binary;
  int ret = builder.finish(binary);
  if (ret == OB_SUCCESS) { ret = copy_json_binary(allocator, binary, value); }
  return ret;
}

int ObAIFuncJsonUtils::get_json_string(ObIAllocator &allocator, ObString &text, ObIJsonBase *&value)
{
  return make_json_string(allocator, text, value);
}

int ObAIFuncJsonUtils::get_json_int(ObIAllocator &allocator, int64_t integer, ObIJsonBase *&value)
{
  return make_json_integer(allocator, integer, value);
}

int ObAIFuncJsonUtils::get_json_boolean(ObIAllocator &allocator, bool boolean, ObIJsonBase *&value)
{
  const ObString text = boolean ? ObString("true") : ObString("false");
  return ObJsonBaseFactory::get_json_base(&allocator, text, ObJsonInType::JSON_TEXT, value);
}

int ObAIFuncJsonUtils::print_json_to_str(ObIAllocator &allocator, ObIJsonBase *base_node, ObString &str)
{
  int ret = OB_SUCCESS;
  ObJsonBuffer j_buf(&allocator);
  if (OB_FAIL(base_node->print(j_buf, 0))) {
  } else {
    str = j_buf.string();
  }
  return ret;
}

int ObAIFuncJsonUtils::get_json_object_form_str(ObIAllocator &allocator, ObString &str, ObIJsonBase *&obj_node)
{
  int ret = OB_SUCCESS;
  ObIJsonBase *j_base = NULL;
  if (OB_FAIL(ObJsonBaseFactory::get_json_base(&allocator, str, ObJsonInType::JSON_TEXT, j_base))) {
  } else {
    obj_node = j_base;
  }
  return ret;
}

int ObAIFuncJsonUtils::compact_json_object(ObIAllocator &allocator, ObIJsonBase *object, ObIJsonBase *&target)
{
  int ret = OB_SUCCESS;
  JsonContainerBuilder builder(true);
  ObString binary;
  ObString source;
  ObIJsonBase *updated = nullptr;
  if (target == nullptr) { ret = OB_INVALID_ARGUMENT; }
  else if (object == nullptr) {
  } else if (OB_FAIL(target->get_raw_binary(binary, &allocator))) {
  } else if (OB_FAIL(object->get_raw_binary(source, &allocator))) {
  } else if (OB_FAIL(builder.extend(binary))) {
  } else if (OB_FAIL(builder.extend(source))) {
  } else if (OB_FAIL(builder.finish(binary))) {
  } else if (OB_FAIL(copy_json_binary(allocator, binary, updated))) {
  } else { target = updated; }
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

int ObAIFuncJsonUtils::transform_array_to_json_array(ObIAllocator &allocator, ObArray<ObString> &contents, ObIJsonBase *&array)
{
  int ret = OB_SUCCESS;
  JsonContainerBuilder builder(false);
  for (int64_t i = 0; OB_SUCC(ret) && i < contents.count(); ++i) {
    ret = builder.append_string(ObString(), contents.at(i));
  }
  ObString binary;
  if (OB_FAIL(ret)) {
  } else if (OB_FAIL(builder.finish(binary))) {
  } else { ret = copy_json_binary(allocator, binary, array); }
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

int ObAIFuncModel::call_completion(ObString &prompt, ObIJsonBase *&config, ObString &result)
{
  int ret = OB_SUCCESS;
  ObArray<ObString> headers;
  ObIJsonBase *body = nullptr;
  ObIJsonBase *response = nullptr;
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
      snprintf(http_message_str, sizeof(http_message_str), "unexpected http message: %s", response_str.ptr());
      ObString ob_http_message_str(http_message_str);
      FORWARD_USER_ERROR(ret, ob_http_message_str.ptr());
    } else {
      FORWARD_USER_ERROR(ret, "unexpected http message");
    }
  } 
  return ret;
}

int ObAIFuncModel::call_completion_vector(ObArray<ObString> &prompts, ObIJsonBase *&config, ObArray<ObString> &results)
{
  int ret = OB_SUCCESS;
  ObArray<ObString> headers;
  ObIJsonBase *body = nullptr;
  ObArray<ObIJsonBase *> body_array;
  ObArray<ObIJsonBase *> response_array;
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
      ObIJsonBase *response = response_array[i];
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

int ObAIFuncModel::call_dense_embedding(ObString &content, ObIJsonBase *&config, ObString &result)
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

int ObAIFuncModel::call_dense_embedding_vector(ObArray<ObString> &contents, ObIJsonBase *&config, ObArray<ObString> &results)
{
  int ret = OB_SUCCESS;
  ObArray<ObString> headers;
  ObIJsonBase *body = nullptr;
  ObArray<ObIJsonBase *> body_array;
  ObArray<ObIJsonBase *> response_array;
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
      ObIJsonBase *response = response_array[i];
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


int ObAIFuncModel::call_dense_embedding_vector_v2(ObArray<ObString> &content, ObIJsonBase *&config, ObArray<ObString> &results)
{
  int ret = OB_SUCCESS;
  ObArray<ObString> headers;
  ObIJsonBase *body = nullptr;
  ObIJsonBase *response = nullptr;
  ObIJsonBase *result_base = nullptr;
  ObAIFuncIEmbed *embed_provider = nullptr;
  ObString result_str;
  ObAIFuncClient client;
  int64_t dimension = 0;
  if (OB_NOT_NULL(config)) {
    ObIJsonBase *dimension_node = ObAIFuncJsonUtils::get_value(config, "dimensions");
    if (OB_ISNULL(dimension_node)) {
      // do nothing
    } else if (dimension_node->json_type() != ObJsonNodeType::J_INT) {
      ret = OB_ERR_UNEXPECTED;
    } else {
      dimension = dimension_node->get_int();
    }
  }
  
  ObString unencrypted_access_key;
  ObString request_model_name = get_request_model_name();
  if (!is_dense_embedding_type()) {
    ret = OB_INVALID_ARGUMENT;
    LOG_USER_ERROR(OB_INVALID_ARGUMENT, "ai_embed, info type is not dense embedding");
  } else if (OB_FAIL(ObAIFuncUtils::get_embed_provider(*allocator_, endpoint_info_.get_provider(), embed_provider))) {
  } else if (OB_FAIL(endpoint_info_.get_unencrypted_access_key(*allocator_, unencrypted_access_key))) {
  } else if (OB_FAIL(embed_provider->get_header(*allocator_, unencrypted_access_key, headers))) {
  } else if (OB_FAIL(embed_provider->get_body(*allocator_, request_model_name, content, config, body))) {
  } else if (OB_FAIL(client.send_post(*allocator_, endpoint_info_.get_url(), headers, body, response))) {
  } else if (OB_FAIL(embed_provider->parse_output(*allocator_, response, result_base))) {
  } else {
    ObIJsonBase *result_array = result_base;
    int64_t count = result_array->element_count();
    if (content.count() != count) {
      ret = OB_ERR_UNEXPECTED;
    } else {
      for (int64_t i = 0; OB_SUCC(ret) && i < count; i++) {
        ObIJsonBase *j_base = ObAIFuncJsonUtils::get_value(result_array, i);
        if (OB_ISNULL(j_base)) {
          ret = OB_ERR_UNEXPECTED;
        } else if (dimension > 0 && j_base->element_count() != dimension) {
          ret = OB_INVALID_ARGUMENT;
          LOG_USER_ERROR(OB_INVALID_ARGUMENT, "ai_embed, result dimension is not equal to dimension");
        } else if (OB_FAIL(ObAIFuncJsonUtils::print_json_to_str(*allocator_, j_base, result_str))) {
        } else {
          results.push_back(result_str);
        } 
      }
    }
  }
  if (ret == OB_INVALID_DATA) {
    ObString response_str;
    if (OB_SUCCESS == ObAIFuncJsonUtils::print_json_to_str(*allocator_, response, response_str)) {
      char http_message_str[1024];
      snprintf(http_message_str, sizeof(http_message_str), "unexpected http message: %s", response_str.ptr());
      ObString ob_http_message_str(http_message_str);
      FORWARD_USER_ERROR(ret, ob_http_message_str.ptr());
    } else {
      FORWARD_USER_ERROR(ret, "unexpected http message");
    }
  }
  return ret;
}

int ObAIFuncModel::call_rerank(ObString &query, ObIJsonBase *contents, ObIJsonBase *&results)
{
  int ret = OB_SUCCESS;
  ObArray<ObString> headers;
  ObIJsonBase *body = nullptr;
  ObIJsonBase *config = nullptr;
  ObIJsonBase *response = nullptr;
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
  } else if (OB_FAIL(rerank_provider->get_body(*allocator_, request_model_name, query, contents, config, body))) {
  } else if (OB_FAIL(client.send_post(*allocator_, endpoint_info_.get_url(), headers, body, response))) {
  } else if (OB_FAIL(rerank_provider->parse_output(*allocator_, response, result_base))) {
  } else {
    results = result_base;
  }

  if (ret == OB_INVALID_DATA) {
    ObString response_str;
    if (OB_SUCCESS == ObAIFuncJsonUtils::print_json_to_str(*allocator_, response, response_str)) {
      char http_message_str[1024];
      snprintf(http_message_str, sizeof(http_message_str), "unexpected http message: %s", response_str.ptr());
      ObString ob_http_message_str(http_message_str);
      FORWARD_USER_ERROR(ret, ob_http_message_str.ptr());
    } else {
      FORWARD_USER_ERROR(ret, "unexpected http message");
    }
  } 
  return ret;
}

int ObAIFuncPromptObjectUtils::construct_prompt_object(
    ObIAllocator &allocator, const ObString &template_str, const ObString &args,
    ObIJsonBase *&prompt_object)
{
  int ret = OB_SUCCESS;
  JsonContainerBuilder builder(true);
  ObString binary;
  if (template_str.empty()) {
    ret = OB_INVALID_ARGUMENT;
  } else if (OB_FAIL(builder.append_string(prompt_template_key, template_str))) {
  } else if (OB_FAIL(builder.append(prompt_args_key, args))) {
  } else if (OB_FAIL(builder.finish(binary))) {
  } else if (OB_FAIL(copy_json_binary(allocator, binary, prompt_object))) {
  }
  return ret;
}

namespace {

int get_prompt_parts(ObIJsonBase &object, ObIJsonBase *&template_node, ObIJsonBase *&args_node)
{
  int ret = OB_SUCCESS;
  if (object.json_type() != ObJsonNodeType::J_OBJECT || object.element_count() != 2) {
    ret = OB_SEARCH_NOT_FOUND;
  } else if (OB_FAIL(object.get_object_value(ObAIFuncPromptObjectUtils::prompt_template_key,
                                             template_node))) {
  } else if (OB_FAIL(object.get_object_value(ObAIFuncPromptObjectUtils::prompt_args_key, args_node))) {
  } else if (template_node->json_type() != ObJsonNodeType::J_STRING
             || args_node->json_type() != ObJsonNodeType::J_ARRAY) {
    ret = OB_SEARCH_NOT_FOUND;
  }
  return ret;
}

bool is_prompt_digit(char ch)
{
  return ch >= '0' && ch <= '9';
}

int append_prompt_text(ObStringBuffer &output, const char *text, uint64_t length)
{
  return length > OB_MAX_LONGTEXT_LENGTH - output.length()
      ? OB_SIZE_OVERFLOW : output.append(text, length, 0);
}

int render_prompt_template(ObIAllocator &allocator, const ObString &text,
                           ObIJsonBase &args, int depth, ObString &result);

int render_prompt_arg(ObIAllocator &allocator, ObIJsonBase &value, int depth, ObString &result)
{
  int ret = OB_SUCCESS;
  if (value.json_type() == ObJsonNodeType::J_STRING) {
    result = ObAIFuncJsonUtils::string_value(&value);
  } else if (value.json_type() == ObJsonNodeType::J_ARRAY) {
    ret = ObAIFuncJsonUtils::print_json_to_str(allocator, &value, result);
  } else if (value.json_type() == ObJsonNodeType::J_OBJECT) {
    ObIJsonBase *nested_template = nullptr;
    ObIJsonBase *nested_args = nullptr;
    const int part_ret = get_prompt_parts(value, nested_template, nested_args);
    if (part_ret == OB_SEARCH_NOT_FOUND) {
      ret = ObAIFuncJsonUtils::print_json_to_str(allocator, &value, result);
    } else if (part_ret != OB_SUCCESS) {
      ret = part_ret;
    } else if (depth >= JSON_DOCUMENT_MAX_DEPTH) {
      ret = OB_ERR_JSON_OUT_OF_DEPTH;
    } else {
      ret = render_prompt_template(allocator, ObAIFuncJsonUtils::string_value(nested_template),
                                   *nested_args, depth + 1, result);
    }
  } else {
    ret = OB_INVALID_ARGUMENT;
    LOG_USER_ERROR(OB_INVALID_ARGUMENT, "ai_prompt: invalid argument type");
  }
  return ret;
}

int render_prompt_template(ObIAllocator &allocator, const ObString &text,
                           ObIJsonBase &args, int depth, ObString &result)
{
  int ret = OB_SUCCESS;
  struct RenderedArg {
    ObString text_;
    bool ready_ = false;
  };
  ObSEArray<RenderedArg, 8> rendered_args;
  ObStringBuffer output(&allocator);
  const uint64_t arg_count = args.element_count();
  const char *bytes = text.ptr();
  const int64_t length = text.length();
  if (text.empty()) {
    ret = OB_INVALID_ARGUMENT;
    LOG_USER_ERROR(OB_INVALID_ARGUMENT, "ai_prompt: empty template");
  } else if (OB_FAIL(check_stack_overflow())) {
  } else if (OB_FAIL(rendered_args.prepare_allocate(arg_count))) {
  }

  int64_t literal_start = 0;
  for (int64_t pos = 0; OB_SUCC(ret) && pos < length;) {
    const char ch = bytes[pos++];
    const bool has_next = pos < length;
    const bool escape = has_next && (ch == '{' || ch == '}') && bytes[pos] == ch;
    const bool placeholder = has_next && ch == '{'
        && (is_prompt_digit(bytes[pos]) || ((bytes[pos] == '-' || bytes[pos] == '+')
            && pos + 1 < length && is_prompt_digit(bytes[pos + 1])));
    // Single braces outside numeric-looking placeholders are literal.
    if (!escape && !placeholder) {
      continue;
    }

    if (OB_FAIL(append_prompt_text(output, bytes + literal_start, pos - 1 - literal_start))) {
    } else if (escape) {
      ret = append_prompt_text(output, bytes + pos++, 1);
    } else {
      // Keep index below arg_count before multiplying again, even for arbitrarily long tokens.
      uint64_t index = 0;
      const int64_t digits_start = pos;
      while (pos < length && is_prompt_digit(bytes[pos]) && index < arg_count) {
        index = index * 10 + (bytes[pos++] - '0');
      }
      if (pos == digits_start || pos == length || bytes[pos] != '}' || index >= arg_count) {
        ret = OB_INVALID_ARGUMENT;
        LOG_USER_ERROR(OB_INVALID_ARGUMENT, "ai_prompt: invalid placeholder index");
      } else {
        ++pos;
        RenderedArg &arg = rendered_args.at(index);
        if (!arg.ready_) {
          ObIJsonBase *value = nullptr;
          if (OB_FAIL(args.get_array_element(index, value))) {
          } else if (OB_FAIL(render_prompt_arg(allocator, *value, depth, arg.text_))) {
          } else {
            arg.ready_ = true;
          }
        }
        if (OB_SUCC(ret)) {
          ret = append_prompt_text(output, arg.text_.ptr(), arg.text_.length());
        }
      }
    }
    literal_start = pos;
  }

  if (OB_FAIL(ret)) {
  } else if (OB_FAIL(append_prompt_text(output, bytes + literal_start, length - literal_start))) {
  } else if (OB_FAIL(output.get_result_string(result))) {
  }
  return ret;
}

} // namespace

int ObAIFuncPromptObjectUtils::render_prompt(
    ObIAllocator &allocator, ObIJsonBase *prompt_object, ObString &result)
{
  int ret = OB_SUCCESS;
  ObIJsonBase *template_node = nullptr;
  ObIJsonBase *args_node = nullptr;
  if (OB_ISNULL(prompt_object)) {
    ret = OB_INVALID_ARGUMENT;
  } else if (OB_FAIL(get_prompt_parts(*prompt_object, template_node, args_node))) {
    if (ret == OB_SEARCH_NOT_FOUND) {
      ret = OB_INVALID_ARGUMENT;
      LOG_USER_ERROR(OB_INVALID_ARGUMENT, "ai_complete: invalid prompt object");
    }
  } else {
    ret = render_prompt_template(allocator, ObAIFuncJsonUtils::string_value(template_node),
                                 *args_node, 1, result);
  }
  return ret;
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
