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

#include <gtest/gtest.h>
#include "sql/engine/expr/ob_expr_ai/ob_ai_func_utils.h"
#include "lib/allocator/ob_malloc.h"
#include "share/json/ob_json_helper.h"

using namespace oceanbase;
using namespace oceanbase::common;
using namespace oceanbase::sql;

class ObOpenAIUtilsTest : public ::testing::Test
{
protected:
  void SetUp() override {}
  void TearDown() override {}
};

TEST_F(ObOpenAIUtilsTest, OpenAICompleteSerializationAndParsing)
{
  ObArenaAllocator allocator(ObModIds::TEST);
  ObOpenAIUtils::ObOpenAIComplete openai_complete;

  ObArray<ObString> headers;
  ObString api_key("sk-test-key");
  ASSERT_EQ(OB_SUCCESS, openai_complete.get_header(allocator, api_key, headers));
  EXPECT_TRUE(headers.count() > 0);

  ObString model("gpt-4");
  ObString prompt("System prompt");
  ObString content("User content");
  ObJsonObject *body = NULL;
  ASSERT_EQ(OB_SUCCESS, openai_complete.get_body(allocator, model, prompt, content, NULL, body));
  ASSERT_TRUE(body != NULL);

  // Mock response parsing
  ObJsonObject *http_response = NULL;
  ASSERT_EQ(OB_SUCCESS, ObAIFuncJsonUtils::get_json_object(allocator, http_response));
  
  ObJsonArray *choices_array = NULL;
  ASSERT_EQ(OB_SUCCESS, ObAIFuncJsonUtils::get_json_array(allocator, choices_array));
  ObJsonObject *choice_obj = NULL;
  ASSERT_EQ(OB_SUCCESS, ObAIFuncJsonUtils::get_json_object(allocator, choice_obj));
  ObJsonObject *message_obj = NULL;
  ASSERT_EQ(OB_SUCCESS, ObAIFuncJsonUtils::get_json_object(allocator, message_obj));
  
  ObJsonString *content_str = NULL;
  ASSERT_EQ(OB_SUCCESS, ObAIFuncJsonUtils::get_json_string(allocator, ObString("Assistant response"), content_str));
  ASSERT_EQ(OB_SUCCESS, message_obj->add(ObString("content"), content_str));
  ASSERT_EQ(OB_SUCCESS, choice_obj->add(ObString("message"), message_obj));
  ASSERT_EQ(OB_SUCCESS, choices_array->append(choice_obj));
  ASSERT_EQ(OB_SUCCESS, http_response->add(ObString("choices"), choices_array));

  ObIJsonBase *result_base = NULL;
  ASSERT_EQ(OB_SUCCESS, openai_complete.parse_output(allocator, http_response, result_base));
  ASSERT_TRUE(result_base != NULL);
}

TEST_F(ObOpenAIUtilsTest, OpenAIEmbedDimensionAndElementChecks)
{
  ObArenaAllocator allocator(ObModIds::TEST);
  ObOpenAIUtils::ObOpenAIEmbed openai_embed;

  ObString model("text-embedding-v1");
  ObArray<ObString> contents;
  ASSERT_EQ(OB_SUCCESS, contents.push_back(ObString("hello")));
  ASSERT_EQ(OB_SUCCESS, contents.push_back(ObString("world")));

  ObJsonObject *body = NULL;
  ASSERT_EQ(OB_SUCCESS, openai_embed.get_body(allocator, model, contents, NULL, body));
  ASSERT_TRUE(body != NULL);

  // Mock response parsing with embedding vectors (out of order index recovery test)
  ObJsonObject *http_response = NULL;
  ASSERT_EQ(OB_SUCCESS, ObAIFuncJsonUtils::get_json_object(allocator, http_response));

  ObJsonArray *data_array = NULL;
  ASSERT_EQ(OB_SUCCESS, ObAIFuncJsonUtils::get_json_array(allocator, data_array));

  // Add items out of order (index 1 first, then index 0)
  for (int idx = 1; idx >= 0; --idx) {
    ObJsonObject *item_obj = NULL;
    ASSERT_EQ(OB_SUCCESS, ObAIFuncJsonUtils::get_json_object(allocator, item_obj));
    ObJsonArray *embedding_array = NULL;
    ASSERT_EQ(OB_SUCCESS, ObAIFuncJsonUtils::get_json_array(allocator, embedding_array));
    ObJsonNumber *num = NULL;
    ASSERT_EQ(OB_SUCCESS, ObAIFuncJsonUtils::get_json_number(allocator, 0.5, num));
    ASSERT_EQ(OB_SUCCESS, embedding_array->append(num));
    
    ASSERT_EQ(OB_SUCCESS, item_obj->add(ObString("embedding"), embedding_array));
    ObJsonNumber *index_num = NULL;
    ASSERT_EQ(OB_SUCCESS, ObAIFuncJsonUtils::get_json_number(allocator, static_cast<double>(idx), index_num));
    ASSERT_EQ(OB_SUCCESS, item_obj->add(ObString("index"), index_num));
    ASSERT_EQ(OB_SUCCESS, data_array->append(item_obj));
  }
  ASSERT_EQ(OB_SUCCESS, http_response->add(ObString("data"), data_array));

  ObIJsonBase *result_base = NULL;
  ASSERT_EQ(OB_SUCCESS, openai_embed.parse_output(allocator, http_response, result_base));
  ASSERT_TRUE(result_base != NULL);
}

TEST_F(ObOpenAIUtilsTest, OllamaUtilsCompleteAndEmbed)
{
  ObArenaAllocator allocator(ObModIds::TEST);
  ObOllamaUtils::ObOllamaComplete ollama_complete;

  ObArray<ObString> headers;
  ObString api_key("");
  ASSERT_EQ(OB_SUCCESS, ollama_complete.get_header(allocator, api_key, headers));

  ObString model("llama3");
  ObString prompt("prompt");
  ObString content("content");
  ObJsonObject *body = NULL;
  ASSERT_EQ(OB_SUCCESS, ollama_complete.get_body(allocator, model, prompt, content, NULL, body));
  ASSERT_TRUE(body != NULL);

  // Parse output
  ObJsonObject *http_response = NULL;
  ASSERT_EQ(OB_SUCCESS, ObAIFuncJsonUtils::get_json_object(allocator, http_response));
  ObJsonString *resp_str = NULL;
  ASSERT_EQ(OB_SUCCESS, ObAIFuncJsonUtils::get_json_string(allocator, ObString("Ollama reply"), resp_str));
  ASSERT_EQ(OB_SUCCESS, http_response->add(ObString("response"), resp_str));

  ObIJsonBase *result_base = NULL;
  ASSERT_EQ(OB_SUCCESS, ollama_complete.parse_output(allocator, http_response, result_base));
  ASSERT_TRUE(result_base != NULL);

  // Ollama Embed
  ObOllamaUtils::ObOllamaEmbed ollama_embed;
  ObArray<ObString> contents;
  ASSERT_EQ(OB_SUCCESS, contents.push_back(ObString("embed test")));
  ObJsonObject *embed_body = NULL;
  ASSERT_EQ(OB_SUCCESS, ollama_embed.get_body(allocator, model, contents, NULL, embed_body));
  ASSERT_TRUE(embed_body != NULL);

  ObJsonObject *embed_response = NULL;
  ASSERT_EQ(OB_SUCCESS, ObAIFuncJsonUtils::get_json_object(allocator, embed_response));
  ObJsonArray *embeddings_array = NULL;
  ASSERT_EQ(OB_SUCCESS, ObAIFuncJsonUtils::get_json_array(allocator, embeddings_array));
  ObJsonArray *vec_array = NULL;
  ASSERT_EQ(OB_SUCCESS, ObAIFuncJsonUtils::get_json_array(allocator, vec_array));
  ObJsonNumber *vnum = NULL;
  ASSERT_EQ(OB_SUCCESS, ObAIFuncJsonUtils::get_json_number(allocator, 0.1, vnum));
  ASSERT_EQ(OB_SUCCESS, vec_array->append(vnum));
  ASSERT_EQ(OB_SUCCESS, embeddings_array->append(vec_array));
  ASSERT_EQ(OB_SUCCESS, embed_response->add(ObString("embeddings"), embeddings_array));

  ObIJsonBase *embed_result = NULL;
  ASSERT_EQ(OB_SUCCESS, ollama_embed.parse_output(allocator, embed_response, embed_result));
  ASSERT_TRUE(embed_result != NULL);
}
