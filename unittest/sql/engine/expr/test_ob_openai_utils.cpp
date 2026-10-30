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

#include <string>
#include <gtest/gtest.h>
#include "lib/allocator/page_arena.h"
#include "sql/engine/expr/ob_expr_ai/ob_ai_func_utils.h"
#include "share/ai_service/ob_ai_service_struct.h"

namespace oceanbase
{
namespace common
{

namespace
{

std::string to_std_string(const ObString &str)
{
  return str.empty() ? std::string() : std::string(str.ptr(), static_cast<size_t>(str.length()));
}

} // namespace

// Covers the provider adapters: what goes on the wire, and how a (possibly
// reordered or malformed) answer is turned back into one value per input row.
class TestObOpenAIUtils : public ::testing::Test
{
public:
  TestObOpenAIUtils() : allocator_() {}
  virtual ~TestObOpenAIUtils() {}
  virtual void SetUp() { allocator_.reset(); }
  virtual void TearDown() { allocator_.reset(); }

protected:
  int build_object(const char *json_literal, ObJsonObject *&obj)
  {
    int ret = OB_SUCCESS;
    ObString json_str = ObString::make_string(json_literal);
    if (OB_FAIL(ObAIFuncJsonUtils::get_json_object_form_str(allocator_, json_str, obj))) {
    }
    return ret;
  }

  std::string stringify(ObIJsonBase *base)
  {
    std::string result;
    ObString str;
    if (OB_NOT_NULL(base) && OB_SUCCESS == ObAIFuncJsonUtils::print_json_to_str(allocator_, base, str)) {
      result = to_std_string(str);
    }
    return result;
  }

protected:
  ObArenaAllocator allocator_;
};

// ---------------------------------------------------------------------------
// embedding answers
// ---------------------------------------------------------------------------

TEST_F(TestObOpenAIUtils, embedding_answer_is_restored_to_request_order)
{
  ObJsonObject *response = NULL;
  ASSERT_EQ(OB_SUCCESS,
            build_object("{\"data\": [{\"index\": 2, \"embedding\": [7, 8, 9]},"
                         " {\"index\": 0, \"embedding\": [1, 2, 3]},"
                         " {\"index\": 1, \"embedding\": [4, 5, 6]}]}",
                         response));
  ObIJsonBase *result = NULL;
  ObOpenAIUtils::ObOpenAIEmbed provider;
  ASSERT_EQ(OB_SUCCESS, provider.parse_output(allocator_, response, result));
  EXPECT_EQ(std::string("[[1,2,3],[4,5,6],[7,8,9]]"), stringify(result));
}

TEST_F(TestObOpenAIUtils, embedding_answer_without_index_keeps_provider_order)
{
  ObJsonObject *response = NULL;
  ASSERT_EQ(OB_SUCCESS,
            build_object("{\"data\": [{\"embedding\": [1, 2]}, {\"embedding\": [3, 4]}]}", response));
  ObIJsonBase *result = NULL;
  ObOpenAIUtils::ObOpenAIEmbed provider;
  ASSERT_EQ(OB_SUCCESS, provider.parse_output(allocator_, response, result));
  EXPECT_EQ(std::string("[[1,2],[3,4]]"), stringify(result));
}

TEST_F(TestObOpenAIUtils, partial_index_is_treated_as_no_index)
{
  // A provider which only marks some rows is not trustworthy: the whole answer
  // is used in its original order instead of being half reordered.
  ObJsonObject *response = NULL;
  ASSERT_EQ(OB_SUCCESS,
            build_object("{\"data\": [{\"index\": 1, \"embedding\": [1, 2]},"
                         " {\"embedding\": [3, 4]}]}",
                         response));
  ObIJsonBase *result = NULL;
  ObOpenAIUtils::ObOpenAIEmbed provider;
  ASSERT_EQ(OB_SUCCESS, provider.parse_output(allocator_, response, result));
  EXPECT_EQ(std::string("[[1,2],[3,4]]"), stringify(result));
}

TEST_F(TestObOpenAIUtils, duplicated_index_is_rejected)
{
  ObJsonObject *response = NULL;
  ASSERT_EQ(OB_SUCCESS,
            build_object("{\"data\": [{\"index\": 0, \"embedding\": [1]},"
                         " {\"index\": 0, \"embedding\": [2]}]}",
                         response));
  ObIJsonBase *result = NULL;
  ObOpenAIUtils::ObOpenAIEmbed provider;
  EXPECT_EQ(OB_INVALID_DATA, provider.parse_output(allocator_, response, result));
}

TEST_F(TestObOpenAIUtils, out_of_range_index_is_rejected)
{
  ObJsonObject *response = NULL;
  ASSERT_EQ(OB_SUCCESS,
            build_object("{\"data\": [{\"index\": 0, \"embedding\": [1]},"
                         " {\"index\": 5, \"embedding\": [2]}]}",
                         response));
  ObIJsonBase *result = NULL;
  ObOpenAIUtils::ObOpenAIEmbed provider;
  EXPECT_EQ(OB_INVALID_DATA, provider.parse_output(allocator_, response, result));
}

TEST_F(TestObOpenAIUtils, empty_embedding_answer_is_rejected)
{
  ObJsonObject *response = NULL;
  ObIJsonBase *result = NULL;
  ObOpenAIUtils::ObOpenAIEmbed provider;

  ASSERT_EQ(OB_SUCCESS, build_object("{\"data\": []}", response));
  EXPECT_EQ(OB_INVALID_DATA, provider.parse_output(allocator_, response, result));
}

TEST_F(TestObOpenAIUtils, embedding_answer_without_data_is_rejected)
{
  ObJsonObject *response = NULL;
  ObIJsonBase *result = NULL;
  ObOpenAIUtils::ObOpenAIEmbed provider;

  ASSERT_EQ(OB_SUCCESS, build_object("{\"object\": \"list\"}", response));
  EXPECT_EQ(OB_INVALID_DATA, provider.parse_output(allocator_, response, result));

  ASSERT_EQ(OB_SUCCESS, build_object("{\"data\": \"not-an-array\"}", response));
  EXPECT_EQ(OB_INVALID_DATA, provider.parse_output(allocator_, response, result));
}

TEST_F(TestObOpenAIUtils, malformed_embedding_element_is_rejected)
{
  ObIJsonBase *result = NULL;
  ObOpenAIUtils::ObOpenAIEmbed provider;
  ObJsonObject *response = NULL;

  // the element itself is not an object
  ASSERT_EQ(OB_SUCCESS, build_object("{\"data\": [1, 2]}", response));
  EXPECT_EQ(OB_INVALID_DATA, provider.parse_output(allocator_, response, result));

  // the embedding member is missing
  ASSERT_EQ(OB_SUCCESS, build_object("{\"data\": [{\"index\": 0}]}", response));
  EXPECT_EQ(OB_INVALID_DATA, provider.parse_output(allocator_, response, result));

  // the embedding member is not an array
  ASSERT_EQ(OB_SUCCESS,
            build_object("{\"data\": [{\"index\": 0, \"embedding\": \"oops\"}]}", response));
  EXPECT_EQ(OB_INVALID_DATA, provider.parse_output(allocator_, response, result));
}

TEST_F(TestObOpenAIUtils, null_http_response_is_rejected)
{
  ObIJsonBase *result = NULL;
  ObOpenAIUtils::ObOpenAIEmbed embed;
  ObOpenAIUtils::ObOpenAIComplete complete;
  EXPECT_EQ(OB_INVALID_ARGUMENT, embed.parse_output(allocator_, NULL, result));
  EXPECT_EQ(OB_INVALID_ARGUMENT, complete.parse_output(allocator_, NULL, result));
}

TEST_F(TestObOpenAIUtils, dashscope_embedding_answer_is_restored_to_request_order)
{
  ObJsonObject *response = NULL;
  ASSERT_EQ(OB_SUCCESS,
            build_object("{\"output\": {\"embeddings\": ["
                         "{\"text_index\": 1, \"embedding\": [3, 4]},"
                         "{\"text_index\": 0, \"embedding\": [1, 2]}]}}",
                         response));
  ObIJsonBase *result = NULL;
  ObDashscopeUtils::ObDashscopeEmbed provider;
  ASSERT_EQ(OB_SUCCESS, provider.parse_output(allocator_, response, result));
  EXPECT_EQ(std::string("[[1,2],[3,4]]"), stringify(result));
}

// ---------------------------------------------------------------------------
// completion answers
// ---------------------------------------------------------------------------

TEST_F(TestObOpenAIUtils, completion_answer_returns_the_content)
{
  ObJsonObject *response = NULL;
  ASSERT_EQ(OB_SUCCESS,
            build_object("{\"choices\": [{\"message\": {\"role\": \"assistant\","
                         " \"content\": \"hello\"}}]}",
                         response));
  ObIJsonBase *result = NULL;
  ObOpenAIUtils::ObOpenAIComplete provider;
  ASSERT_EQ(OB_SUCCESS, provider.parse_output(allocator_, response, result));
  // the content node is printed unquoted, that is what the caller returns
  EXPECT_EQ(std::string("hello"), stringify(result));
}

TEST_F(TestObOpenAIUtils, completion_answer_without_content_is_rejected)
{
  ObIJsonBase *result = NULL;
  ObOpenAIUtils::ObOpenAIComplete provider;
  ObJsonObject *response = NULL;

  ASSERT_EQ(OB_SUCCESS, build_object("{\"choices\": []}", response));
  EXPECT_EQ(OB_INVALID_DATA, provider.parse_output(allocator_, response, result));

  ASSERT_EQ(OB_SUCCESS, build_object("{\"choices\": [{\"message\": {}}]}", response));
  EXPECT_EQ(OB_INVALID_DATA, provider.parse_output(allocator_, response, result));

  // a null content means the model refused to answer
  ASSERT_EQ(OB_SUCCESS,
            build_object("{\"choices\": [{\"message\": {\"content\": null}}]}", response));
  EXPECT_EQ(OB_INVALID_DATA, provider.parse_output(allocator_, response, result));

  // an empty string is not an answer either
  ASSERT_EQ(OB_SUCCESS,
            build_object("{\"choices\": [{\"message\": {\"content\": \"\"}}]}", response));
  EXPECT_EQ(OB_INVALID_DATA, provider.parse_output(allocator_, response, result));

  // a structured content cannot be returned as a string
  ASSERT_EQ(OB_SUCCESS,
            build_object("{\"choices\": [{\"message\": {\"content\": {\"a\": 1}}}]}", response));
  EXPECT_EQ(OB_INVALID_DATA, provider.parse_output(allocator_, response, result));
}

// ---------------------------------------------------------------------------
// completion config
// ---------------------------------------------------------------------------

TEST_F(TestObOpenAIUtils, completion_config_is_optional)
{
  EXPECT_EQ(OB_SUCCESS, ObAIFuncUtils::check_completion_config(NULL));
  ObJsonObject *empty = NULL;
  ASSERT_EQ(OB_SUCCESS, build_object("{}", empty));
  EXPECT_EQ(OB_SUCCESS, ObAIFuncUtils::check_completion_config(empty));
}

TEST_F(TestObOpenAIUtils, valid_completion_config)
{
  ObJsonObject *config = NULL;
  ASSERT_EQ(OB_SUCCESS,
            build_object("{\"temperature\": 0.7, \"max_tokens\": 128, \"top_p\": 0.9}", config));
  EXPECT_EQ(OB_SUCCESS, ObAIFuncUtils::check_completion_config(config));

  // the two boundaries of temperature are inclusive
  ObJsonObject *low = NULL;
  ASSERT_EQ(OB_SUCCESS, build_object("{\"temperature\": 0}", low));
  EXPECT_EQ(OB_SUCCESS, ObAIFuncUtils::check_completion_config(low));
  ObJsonObject *high = NULL;
  ASSERT_EQ(OB_SUCCESS, build_object("{\"temperature\": 2}", high));
  EXPECT_EQ(OB_SUCCESS, ObAIFuncUtils::check_completion_config(high));
}

TEST_F(TestObOpenAIUtils, invalid_completion_config)
{
  ObJsonObject *config = NULL;

  ASSERT_EQ(OB_SUCCESS, build_object("{\"temperature\": \"hot\"}", config));
  EXPECT_EQ(OB_INVALID_ARGUMENT, ObAIFuncUtils::check_completion_config(config));

  ASSERT_EQ(OB_SUCCESS, build_object("{\"temperature\": 2.5}", config));
  EXPECT_EQ(OB_INVALID_ARGUMENT, ObAIFuncUtils::check_completion_config(config));

  ASSERT_EQ(OB_SUCCESS, build_object("{\"temperature\": -1}", config));
  EXPECT_EQ(OB_INVALID_ARGUMENT, ObAIFuncUtils::check_completion_config(config));

  ASSERT_EQ(OB_SUCCESS, build_object("{\"max_tokens\": 0}", config));
  EXPECT_EQ(OB_INVALID_ARGUMENT, ObAIFuncUtils::check_completion_config(config));

  ASSERT_EQ(OB_SUCCESS, build_object("{\"max_tokens\": -5}", config));
  EXPECT_EQ(OB_INVALID_ARGUMENT, ObAIFuncUtils::check_completion_config(config));

  ASSERT_EQ(OB_SUCCESS, build_object("{\"max_tokens\": 12.5}", config));
  EXPECT_EQ(OB_INVALID_ARGUMENT, ObAIFuncUtils::check_completion_config(config));

  ASSERT_EQ(OB_SUCCESS, build_object("{\"max_tokens\": \"128\"}", config));
  EXPECT_EQ(OB_INVALID_ARGUMENT, ObAIFuncUtils::check_completion_config(config));
}

// ---------------------------------------------------------------------------
// ollama provider
// ---------------------------------------------------------------------------

TEST_F(TestObOpenAIUtils, ollama_completion_request_is_non_streaming)
{
  ObString model = ObString::make_string("llama3.1");
  ObString prompt = ObString::make_string("");
  ObString content = ObString::make_string("what is the capital of France?");
  ObJsonObject *body = NULL;
  ObOllamaUtils::ObOllamaComplete provider;
  ASSERT_EQ(OB_SUCCESS, provider.get_body(allocator_, model, prompt, content, NULL, body));
  const std::string body_str = stringify(body);
  EXPECT_NE(std::string::npos, body_str.find("\"stream\": false")) << body_str;
  EXPECT_NE(std::string::npos, body_str.find("\"model\": \"llama3.1\"")) << body_str;
  EXPECT_NE(std::string::npos, body_str.find("what is the capital of France?")) << body_str;
}

TEST_F(TestObOpenAIUtils, ollama_completion_ignores_stream_in_config)
{
  ObString model = ObString::make_string("llama3.1");
  ObString prompt = ObString::make_string("");
  ObString content = ObString::make_string("hi");
  ObJsonObject *config = NULL;
  ASSERT_EQ(OB_SUCCESS, build_object("{\"stream\": true, \"temperature\": 0.5}", config));
  ObJsonObject *body = NULL;
  ObOllamaUtils::ObOllamaComplete provider;
  ASSERT_EQ(OB_SUCCESS, provider.get_body(allocator_, model, prompt, content, config, body));
  const std::string body_str = stringify(body);
  // the streaming protocol is refused, everything else from the config is kept
  EXPECT_NE(std::string::npos, body_str.find("\"stream\": false")) << body_str;
  EXPECT_EQ(std::string::npos, body_str.find("\"stream\": true")) << body_str;
  EXPECT_NE(std::string::npos, body_str.find("temperature")) << body_str;
}

TEST_F(TestObOpenAIUtils, ollama_completion_requires_model_and_content)
{
  ObString model = ObString::make_string("llama3.1");
  ObString empty_model = ObString::make_string("");
  ObString prompt = ObString::make_string("");
  ObString content = ObString::make_string("hi");
  ObJsonObject *body = NULL;
  ObOllamaUtils::ObOllamaComplete provider;
  EXPECT_EQ(OB_INVALID_ARGUMENT, provider.get_body(allocator_, empty_model, prompt, content, NULL, body));
  EXPECT_EQ(OB_INVALID_ARGUMENT, provider.get_body(allocator_, model, prompt, empty_model, NULL, body));
}

TEST_F(TestObOpenAIUtils, ollama_completion_answer_returns_the_response)
{
  ObJsonObject *response = NULL;
  ASSERT_EQ(OB_SUCCESS, build_object("{\"response\": \"hello\", \"done\": true}", response));
  ObIJsonBase *result = NULL;
  ObOllamaUtils::ObOllamaComplete provider;
  ASSERT_EQ(OB_SUCCESS, provider.parse_output(allocator_, response, result));
  EXPECT_EQ(std::string("hello"), stringify(result));
}

TEST_F(TestObOpenAIUtils, ollama_completion_answer_without_response_is_rejected)
{
  ObIJsonBase *result = NULL;
  ObOllamaUtils::ObOllamaComplete provider;
  ObJsonObject *response = NULL;

  ASSERT_EQ(OB_SUCCESS, build_object("{\"done\": true}", response));
  EXPECT_EQ(OB_INVALID_DATA, provider.parse_output(allocator_, response, result));

  ASSERT_EQ(OB_SUCCESS, build_object("{\"response\": \"\"}", response));
  EXPECT_EQ(OB_INVALID_DATA, provider.parse_output(allocator_, response, result));

  ASSERT_EQ(OB_SUCCESS, build_object("{\"response\": 42}", response));
  EXPECT_EQ(OB_INVALID_DATA, provider.parse_output(allocator_, response, result));
}

TEST_F(TestObOpenAIUtils, ollama_embedding_request_carries_every_input)
{
  ObString model = ObString::make_string("nomic-embed-text");
  ObArray<ObString> contents;
  ASSERT_EQ(OB_SUCCESS, contents.push_back(ObString::make_string("first")));
  ASSERT_EQ(OB_SUCCESS, contents.push_back(ObString::make_string("second")));
  ObJsonObject *body = NULL;
  ObOllamaUtils::ObOllamaEmbed provider;
  ASSERT_EQ(OB_SUCCESS, provider.get_body(allocator_, model, contents, NULL, body));
  EXPECT_EQ(std::string("{\"input\": [\"first\",\"second\"],\"model\": \"nomic-embed-text\"}"),
            stringify(body));
}

TEST_F(TestObOpenAIUtils, ollama_embedding_answer)
{
  ObJsonObject *response = NULL;
  ObIJsonBase *result = NULL;
  ObOllamaUtils::ObOllamaEmbed provider;

  ASSERT_EQ(OB_SUCCESS, build_object("{\"embeddings\": [[1, 2], [3, 4]]}", response));
  ASSERT_EQ(OB_SUCCESS, provider.parse_output(allocator_, response, result));
  EXPECT_EQ(std::string("[[1,2],[3,4]]"), stringify(result));

  // a batch answer which is not an array of arrays is refused
  ASSERT_EQ(OB_SUCCESS, build_object("{\"embeddings\": \"oops\"}", response));
  EXPECT_EQ(OB_INVALID_DATA, provider.parse_output(allocator_, response, result));
}

// ---------------------------------------------------------------------------
// endpoint validation
// ---------------------------------------------------------------------------

TEST_F(TestObOpenAIUtils, ollama_endpoint_does_not_require_an_access_key)
{
  ObAiModelEndpointInfo endpoint;
  ObJsonObject *params = NULL;
  ObString name = ObString::make_string("local_ollama");
  ASSERT_EQ(OB_SUCCESS,
            build_object("{\"ai_model_name\": \"llama3.1\","
                         " \"url\": \"http://127.0.0.1:11434\","
                         " \"provider\": \"ollama\"}",
                         params));
  // the provider name is matched case insensitively, and no api key is needed
  ASSERT_EQ(OB_SUCCESS, endpoint.parse_from_json_base(allocator_, name, *params));
  EXPECT_EQ(OB_SUCCESS, endpoint.check_valid());
  EXPECT_EQ(0, endpoint.get_provider().case_compare("ollama"));
}

TEST_F(TestObOpenAIUtils, ollama_provider_name_is_case_insensitive)
{
  const char *providers[] = {"OLLAMA", "Ollama", "oLLaMa"};
  for (int64_t i = 0; i < 3; i++) {
    ObAiModelEndpointInfo endpoint;
    ObJsonObject *params = NULL;
    ObString name = ObString::make_string("local_ollama");
    std::string json = "{\"ai_model_name\": \"llama3.1\","
                       " \"url\": \"http://127.0.0.1:11434\", \"provider\": \"";
    json.append(providers[i]);
    json.append("\"}");
    ASSERT_EQ(OB_SUCCESS, build_object(json.c_str(), params)) << providers[i];
    ASSERT_EQ(OB_SUCCESS, endpoint.parse_from_json_base(allocator_, name, *params)) << providers[i];
  }
}

TEST_F(TestObOpenAIUtils, other_providers_still_require_an_access_key)
{
  ObJsonObject *params = NULL;
  ObString name = ObString::make_string("remote_openai");
  ASSERT_EQ(OB_SUCCESS,
            build_object("{\"ai_model_name\": \"gpt-4o\","
                         " \"url\": \"https://api.openai.com/v1\","
                         " \"provider\": \"OPENAI\"}",
                         params));
  ObAiModelEndpointInfo endpoint;
  EXPECT_EQ(OB_AI_FUNC_PARAM_EMPTY, endpoint.parse_from_json_base(allocator_, name, *params));

  // with a key the very same endpoint is accepted
  ObAiModelEndpointInfo endpoint_with_key;
  ObJsonObject *params_with_key = NULL;
  ASSERT_EQ(OB_SUCCESS,
            build_object("{\"ai_model_name\": \"gpt-4o\","
                         " \"url\": \"https://api.openai.com/v1\","
                         " \"provider\": \"OPENAI\", \"access_key\": \"sk-test\"}",
                         params_with_key));
  EXPECT_EQ(OB_SUCCESS,
            endpoint_with_key.parse_from_json_base(allocator_, name, *params_with_key));
}

TEST_F(TestObOpenAIUtils, unknown_provider_is_rejected)
{
  ObJsonObject *params = NULL;
  ObString name = ObString::make_string("bogus");
  ASSERT_EQ(OB_SUCCESS,
            build_object("{\"ai_model_name\": \"m\","
                         " \"url\": \"http://127.0.0.1:1234\","
                         " \"provider\": \"NOT-A-PROVIDER\"}",
                         params));
  ObAiModelEndpointInfo endpoint;
  EXPECT_EQ(OB_AI_FUNC_PARAM_VALUE_INVALID,
            endpoint.parse_from_json_base(allocator_, name, *params));
}

TEST_F(TestObOpenAIUtils, endpoint_url_must_carry_a_scheme)
{
  ObJsonObject *params = NULL;
  ObString name = ObString::make_string("local_ollama");
  ASSERT_EQ(OB_SUCCESS,
            build_object("{\"ai_model_name\": \"llama3.1\","
                         " \"url\": \"127.0.0.1:11434\","
                         " \"provider\": \"ollama\"}",
                         params));
  ObAiModelEndpointInfo endpoint;
  EXPECT_EQ(OB_AI_FUNC_PARAM_VALUE_INVALID,
            endpoint.parse_from_json_base(allocator_, name, *params));
}

TEST_F(TestObOpenAIUtils, is_provider_without_access_key)
{
  EXPECT_TRUE(ObAiModelEndpointInfo::is_provider_without_access_key(ObString::make_string("OLLAMA")));
  EXPECT_TRUE(ObAiModelEndpointInfo::is_provider_without_access_key(ObString::make_string("ollama")));
  EXPECT_TRUE(ObAiModelEndpointInfo::is_provider_without_access_key(ObString::make_string("Ollama")));
  EXPECT_FALSE(ObAiModelEndpointInfo::is_provider_without_access_key(ObString::make_string("OPENAI")));
  EXPECT_FALSE(ObAiModelEndpointInfo::is_provider_without_access_key(ObString::make_string("DEEPSEEK")));
  EXPECT_FALSE(ObAiModelEndpointInfo::is_provider_without_access_key(ObString::make_string("")));
}

TEST_F(TestObOpenAIUtils, provider_dispatch_selects_the_ollama_adapter)
{
  ObAIFuncIComplete *complete = NULL;
  ObAIFuncIEmbed *embed = NULL;
  EXPECT_EQ(OB_SUCCESS,
            ObAIFuncUtils::get_complete_provider(allocator_, ObString::make_string("OLLAMA"), complete));
  EXPECT_NE(static_cast<ObAIFuncIComplete *>(NULL), complete);
  EXPECT_EQ(OB_SUCCESS,
            ObAIFuncUtils::get_embed_provider(allocator_, ObString::make_string("ollama"), embed));
  EXPECT_NE(static_cast<ObAIFuncIEmbed *>(NULL), embed);
}

} // namespace common
} // namespace oceanbase

int main(int argc, char **argv)
{
  ::testing::InitGoogleTest(&argc, argv);
  return RUN_ALL_TESTS();
}
