/**
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

#include <gtest/gtest.h>
#include <string>
#include "lib/allocator/page_arena.h"
#include "lib/container/ob_array.h"
#include "common/json_type/ob_json_common.h"
#include "sql/engine/expr/ob_expr_ai/ob_ai_func_utils.h"

using namespace oceanbase::common;

namespace oceanbase
{
namespace sql
{

/**
 * OpenAI / Ollama provider 的请求与响应处理单元测试：
 * 1. Embedding 请求体与响应解析；
 * 2. 响应乱序时按 index 恢复成与请求一致的顺序；
 * 3. 非法响应（data 非数组、元素非对象、缺少 embedding）报错；
 * 4. Ollama 使用原生协议，且不需要 access key。
 */
class ObOpenAIUtilsTest : public ::testing::Test
{
public:
  ObOpenAIUtilsTest() = default;
  virtual ~ObOpenAIUtilsTest() = default;

  // 解析一段 JSON 文本
  static int parse_response(ObIAllocator &allocator,
                            const char *response_str,
                            ObJsonObject *&response)
  {
    int ret = OB_SUCCESS;
    ObString str(response_str);
    if (OB_FAIL(ObAIFuncJsonUtils::get_json_object_form_str(allocator, str, response))) {
    }
    return ret;
  }

  static std::string to_std_str(const ObString &str)
  {
    return std::string(str.ptr(), str.length());
  }

  // 取出解析结果中第 index 个 embedding 的第 element 个分量
  static double embedding_element(ObIJsonBase *result, int64_t index, int64_t element)
  {
    ObJsonArray *array = static_cast<ObJsonArray *>(result);
    ObJsonArray *embedding = static_cast<ObJsonArray *>(array->get_value(index));
    ObIJsonBase *value = embedding->get_value(element);
    EXPECT_EQ(ObJsonNodeType::J_DOUBLE, value->json_type());
    return static_cast<ObJsonDouble *>(value)->value();
  }
};

// provider 名称大小写不敏感
TEST_F(ObOpenAIUtilsTest, test_provider_is_case_insensitive)
{
  ObArenaAllocator allocator;
  ObAIFuncIEmbed *embed_provider = nullptr;
  ObAIFuncIComplete *complete_provider = nullptr;

  ASSERT_EQ(OB_SUCCESS, ObAIFuncUtils::get_embed_provider(allocator, ObString("OLLAMA"), embed_provider));
  ASSERT_NE(nullptr, embed_provider);
  ASSERT_EQ(OB_SUCCESS, ObAIFuncUtils::get_embed_provider(allocator, ObString("Ollama"), embed_provider));
  ASSERT_NE(nullptr, embed_provider);

  ASSERT_EQ(OB_SUCCESS, ObAIFuncUtils::get_complete_provider(allocator, ObString("OLLAMA"), complete_provider));
  ASSERT_NE(nullptr, complete_provider);
  ASSERT_EQ(OB_SUCCESS, ObAIFuncUtils::get_complete_provider(allocator, ObString("openai"), complete_provider));
  ASSERT_NE(nullptr, complete_provider);

  // 未知的 provider 不被支持
  ASSERT_EQ(OB_NOT_SUPPORTED,
            ObAIFuncUtils::get_embed_provider(allocator, ObString("not-exist"), embed_provider));
}

// OpenAI embedding 请求体：model + input 数组 + 可选配置
TEST_F(ObOpenAIUtilsTest, test_openai_embed_get_body)
{
  ObArenaAllocator allocator;
  ObOpenAIUtils::ObOpenAIEmbed provider;
  ObArray<ObString> contents;
  ASSERT_EQ(OB_SUCCESS, contents.push_back(ObString("first")));
  ASSERT_EQ(OB_SUCCESS, contents.push_back(ObString("second")));

  ObJsonObject *config = nullptr;
  ASSERT_EQ(OB_SUCCESS, ObAIFuncJsonUtils::get_json_object(allocator, config));
  ObJsonInt *dimension = nullptr;
  ASSERT_EQ(OB_SUCCESS, ObAIFuncJsonUtils::get_json_int(allocator, 768, dimension));
  ASSERT_EQ(OB_SUCCESS, config->add("dimensions", dimension));

  ObString model("text-embedding-v1");
  ObJsonObject *body = nullptr;
  ASSERT_EQ(OB_SUCCESS, provider.get_body(allocator, model, contents, config, body));
  ASSERT_NE(nullptr, body);

  ASSERT_NE(nullptr, body->get_value("model"));
  ASSERT_EQ("text-embedding-v1",
            to_std_str(static_cast<ObJsonString *>(body->get_value("model"))->get_str()));
  ObJsonArray *input = static_cast<ObJsonArray *>(body->get_value("input"));
  ASSERT_NE(nullptr, input);
  ASSERT_EQ(2, input->element_count());
  ASSERT_EQ("first", to_std_str(static_cast<ObJsonString *>(input->get_value(0))->get_str()));
  ASSERT_EQ("second", to_std_str(static_cast<ObJsonString *>(input->get_value(1))->get_str()));
  ASSERT_NE(nullptr, body->get_value("dimensions"));
  ASSERT_EQ(768, static_cast<ObJsonInt *>(body->get_value("dimensions"))->value());

  // model 为空或者没有内容时报错
  ObString empty_model;
  ASSERT_EQ(OB_INVALID_ARGUMENT, provider.get_body(allocator, empty_model, contents, nullptr, body));
  ObArray<ObString> empty_contents;
  ASSERT_EQ(OB_INVALID_ARGUMENT, provider.get_body(allocator, model, empty_contents, nullptr, body));
}

// OpenAI embedding 响应：按 index 恢复成与请求一致的顺序
TEST_F(ObOpenAIUtilsTest, test_openai_embed_parse_output_shuffled)
{
  ObArenaAllocator allocator;
  ObOpenAIUtils::ObOpenAIEmbed provider;
  // 响应故意乱序：index 1 在前，index 0 在后
  ObJsonObject *response = nullptr;
  ASSERT_EQ(OB_SUCCESS,
            parse_response(allocator,
                           "{\"data\": ["
                           "{\"index\": 1, \"embedding\": [0.2, 0.4]},"
                           "{\"index\": 0, \"embedding\": [0.1, 0.3]}]}",
                           response));
  ASSERT_NE(nullptr, response);

  ObIJsonBase *result = nullptr;
  ASSERT_EQ(OB_SUCCESS, provider.parse_output(allocator, response, result));
  ASSERT_NE(nullptr, result);
  ASSERT_EQ(2, static_cast<ObJsonArray *>(result)->element_count());
  // 归位之后，第 0 个结果对应请求的第 0 个内容
  ASSERT_DOUBLE_EQ(0.1, embedding_element(result, 0, 0));
  ASSERT_DOUBLE_EQ(0.3, embedding_element(result, 0, 1));
  ASSERT_DOUBLE_EQ(0.2, embedding_element(result, 1, 0));
  ASSERT_DOUBLE_EQ(0.4, embedding_element(result, 1, 1));
}

// 响应没有 index 时退化为按响应顺序返回
TEST_F(ObOpenAIUtilsTest, test_openai_embed_parse_output_without_index)
{
  ObArenaAllocator allocator;
  ObOpenAIUtils::ObOpenAIEmbed provider;
  ObJsonObject *response = nullptr;
  ASSERT_EQ(OB_SUCCESS,
            parse_response(allocator,
                           "{\"data\": ["
                           "{\"embedding\": [0.1, 0.3]},"
                           "{\"embedding\": [0.2, 0.4]}]}",
                           response));

  ObIJsonBase *result = nullptr;
  ASSERT_EQ(OB_SUCCESS, provider.parse_output(allocator, response, result));
  ASSERT_NE(nullptr, result);
  ASSERT_EQ(2, static_cast<ObJsonArray *>(result)->element_count());
  ASSERT_DOUBLE_EQ(0.1, embedding_element(result, 0, 0));
  ASSERT_DOUBLE_EQ(0.2, embedding_element(result, 1, 0));
}

// 非法响应报错
TEST_F(ObOpenAIUtilsTest, test_openai_embed_parse_output_invalid)
{
  ObArenaAllocator allocator;
  ObOpenAIUtils::ObOpenAIEmbed provider;
  ObIJsonBase *result = nullptr;
  ObJsonObject *response = nullptr;

  // 缺少 data 字段
  ASSERT_EQ(OB_SUCCESS, parse_response(allocator, "{\"object\": \"list\"}", response));
  ASSERT_EQ(OB_INVALID_DATA, provider.parse_output(allocator, response, result));

  // data 不是数组
  ASSERT_EQ(OB_SUCCESS, parse_response(allocator, "{\"data\": {}}", response));
  ASSERT_EQ(OB_INVALID_DATA, provider.parse_output(allocator, response, result));

  // data 的元素不是对象
  ASSERT_EQ(OB_SUCCESS, parse_response(allocator, "{\"data\": [1]}", response));
  ASSERT_EQ(OB_INVALID_DATA, provider.parse_output(allocator, response, result));

  // 元素缺少 embedding
  ASSERT_EQ(OB_SUCCESS, parse_response(allocator, "{\"data\": [{\"index\": 0}]}", response));
  ASSERT_EQ(OB_INVALID_DATA, provider.parse_output(allocator, response, result));

  // 空响应
  ASSERT_EQ(OB_INVALID_ARGUMENT, provider.parse_output(allocator, nullptr, result));
}

// Ollama 请求：不需要 access key，但必须声明 JSON 请求体
TEST_F(ObOpenAIUtilsTest, test_ollama_embed_request)
{
  ObArenaAllocator allocator;
  ObOllamaUtils::ObOllamaEmbed provider;
  ObString api_key;
  ObArray<ObString> headers;
  ASSERT_EQ(OB_SUCCESS, provider.get_header(allocator, api_key, headers));
  ASSERT_EQ(1, headers.count());
  ASSERT_EQ("Content-Type: application/json", to_std_str(headers.at(0)));

  ObArray<ObString> contents;
  ASSERT_EQ(OB_SUCCESS, contents.push_back(ObString("hello")));
  ObString model("nomic-embed-text");
  ObJsonObject *body = nullptr;
  ASSERT_EQ(OB_SUCCESS, provider.get_body(allocator, model, contents, nullptr, body));
  ASSERT_NE(nullptr, body);

  ASSERT_NE(nullptr, body->get_value("model"));
  ASSERT_EQ("nomic-embed-text",
            to_std_str(static_cast<ObJsonString *>(body->get_value("model"))->get_str()));
  ObJsonArray *input = static_cast<ObJsonArray *>(body->get_value("input"));
  ASSERT_NE(nullptr, input);
  ASSERT_EQ(1, input->element_count());
  ASSERT_EQ("hello", to_std_str(static_cast<ObJsonString *>(input->get_value(0))->get_str()));
}

// Ollama completion 请求与响应
TEST_F(ObOpenAIUtilsTest, test_ollama_complete_request_and_response)
{
  ObArenaAllocator allocator;
  ObOllamaUtils::ObOllamaComplete provider;
  ObString api_key;
  ObArray<ObString> headers;
  ASSERT_EQ(OB_SUCCESS, provider.get_header(allocator, api_key, headers));
  ASSERT_EQ(1, headers.count());

  ObString model("qwen2.5");
  ObString prompt("describe");
  ObString content("hello");
  ObJsonObject *body = nullptr;
  ASSERT_EQ(OB_SUCCESS, provider.get_body(allocator, model, prompt, content, nullptr, body));
  ASSERT_NE(nullptr, body);
  ASSERT_NE(nullptr, body->get_value("model"));
  ASSERT_EQ("qwen2.5",
            to_std_str(static_cast<ObJsonString *>(body->get_value("model"))->get_str()));
  ASSERT_NE(nullptr, body->get_value("prompt"));
  ASSERT_EQ("hello", to_std_str(static_cast<ObJsonString *>(body->get_value("prompt"))->get_str()));

  // 响应从 $.response 中取结果
  ObJsonObject *response = nullptr;
  ASSERT_EQ(OB_SUCCESS, parse_response(allocator, "{\"response\": \"world\"}", response));
  ObIJsonBase *result = nullptr;
  ASSERT_EQ(OB_SUCCESS, provider.parse_output(allocator, response, result));
  ASSERT_NE(nullptr, result);
  ASSERT_EQ("world", to_std_str(static_cast<ObJsonString *>(result)->get_str()));
}

// Ollama embedding 响应从 $.embeddings 中取结果
TEST_F(ObOpenAIUtilsTest, test_ollama_embed_parse_output)
{
  ObArenaAllocator allocator;
  ObOllamaUtils::ObOllamaEmbed provider;
  ObJsonObject *response = nullptr;
  ASSERT_EQ(OB_SUCCESS,
            parse_response(allocator, "{\"embeddings\": [[0.1, 0.2, 0.3]]}", response));

  ObIJsonBase *result = nullptr;
  ASSERT_EQ(OB_SUCCESS, provider.parse_output(allocator, response, result));
  ASSERT_NE(nullptr, result);
  ASSERT_EQ(1, static_cast<ObJsonArray *>(result)->element_count());
  ASSERT_DOUBLE_EQ(0.1, embedding_element(result, 0, 0));
  ASSERT_DOUBLE_EQ(0.3, embedding_element(result, 0, 2));

  // 缺少 embeddings 字段时报错
  ObJsonObject *bad_response = nullptr;
  ASSERT_EQ(OB_SUCCESS, parse_response(allocator, "{\"other\": 1}", bad_response));
  ObIJsonBase *bad_result = nullptr;
  ASSERT_NE(OB_SUCCESS, provider.parse_output(allocator, bad_response, bad_result));
}

} // namespace sql
} // namespace oceanbase
