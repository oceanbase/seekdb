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
#include "ob_expr_test_utils.h"
#include "sql/engine/expr/ob_expr_ai/ob_ai_func_utils.h"

using namespace oceanbase::common;
using namespace oceanbase::sql;

class ObAIRerankProviderTest : public ::testing::Test
{
public:
  ObAIRerankProviderTest() = default;
  virtual ~ObAIRerankProviderTest() = default;
  virtual void SetUp() {}
  virtual void TearDown() {}
};

TEST_F(ObAIRerankProviderTest, openai_compatible_provider_should_work)
{
  ObArenaAllocator allocator(ObModIds::TEST);
  ObAIFuncIRerank *rerank_provider = nullptr;
  ASSERT_EQ(OB_SUCCESS, ObAIFuncUtils::get_rerank_provider(allocator, ObString("openai"), rerank_provider));
  ASSERT_NE(nullptr, rerank_provider);

  ObArray<ObString> headers;
  ObString api_key("sk-123");
  ASSERT_EQ(OB_SUCCESS, rerank_provider->get_header(allocator, api_key, headers));
  ASSERT_EQ(2, headers.count());
  ASSERT_EQ(ObString("Authorization: Bearer sk-123"), headers[0]);
  ASSERT_EQ(ObString("Content-Type: application/json"), headers[1]);

  ObString model("bge-reranker-v2");
  ObString query("Apple");
  ObString documents("[\"apple\",\"banana\"]");
  ObIJsonBase *j_base = nullptr;
  ASSERT_EQ(OB_SUCCESS,
            ObJsonBaseFactory::get_json_base(
                &allocator, documents, ObJsonInType::JSON_TREE, ObJsonInType::JSON_TREE, j_base));
  ObJsonArray *document_array = static_cast<ObJsonArray *>(j_base);
  ObJsonObject config(&allocator);
  ObJsonInt *top_n_node = nullptr;
  ASSERT_EQ(OB_SUCCESS, ObAIFuncJsonUtils::get_json_int(allocator, 2, top_n_node));
  ASSERT_EQ(OB_SUCCESS, config.add("top_n", top_n_node));
  ObJsonObject *body = nullptr;
  ASSERT_EQ(OB_SUCCESS, rerank_provider->get_body(allocator, model, query, document_array, &config, body));
  ASSERT_NE(nullptr, body);
  ObJsonNode *model_node = body->get_value("model");
  ASSERT_NE(nullptr, model_node);
  ObStringBuffer model_buf(&allocator);
  ASSERT_EQ(OB_SUCCESS, model_node->print(model_buf, 0));
  ASSERT_EQ(model, model_buf.string());

  ObJsonNode *query_node = body->get_value("query");
  ASSERT_NE(nullptr, query_node);
  ObStringBuffer query_buf(&allocator);
  ASSERT_EQ(OB_SUCCESS, query_node->print(query_buf, 0));
  ASSERT_EQ(query, query_buf.string());

  ObJsonNode *documents_node = body->get_value("documents");
  ASSERT_NE(nullptr, documents_node);
  ASSERT_EQ(ObJsonNodeType::J_ARRAY, documents_node->json_type());
  ObJsonArray *documents_array = static_cast<ObJsonArray *>(documents_node);
  ASSERT_EQ(2, documents_array->element_count());
  ObJsonNode *doc0 = documents_array->get_value(0);
  ASSERT_NE(nullptr, doc0);
  ObStringBuffer doc0_buf(&allocator);
  ASSERT_EQ(OB_SUCCESS, doc0->print(doc0_buf, 0));
  ASSERT_EQ(ObString("apple"), doc0_buf.string());
  ObJsonNode *doc1 = documents_array->get_value(1);
  ASSERT_NE(nullptr, doc1);
  ObStringBuffer doc1_buf(&allocator);
  ASSERT_EQ(OB_SUCCESS, doc1->print(doc1_buf, 0));
  ASSERT_EQ(ObString("banana"), doc1_buf.string());

  ObJsonNode *top_n_body_node = body->get_value("top_n");
  ASSERT_NE(nullptr, top_n_body_node);
  ObStringBuffer top_n_buf(&allocator);
  ASSERT_EQ(OB_SUCCESS, top_n_body_node->print(top_n_buf, 0));
  ASSERT_EQ(ObString("2"), top_n_buf.string());

  ObString response(
      "{"
      "\"id\": 0,"
      "\"results\": ["
      "  {\"index\": 0, \"relevance_score\": 0.5, \"document\": {\"text\": \"apple\"}}"
      "]"
      "}");
  ObIJsonBase *resp_base = nullptr;
  ASSERT_EQ(OB_SUCCESS,
            ObJsonBaseFactory::get_json_base(
                &allocator, response, ObJsonInType::JSON_TREE, ObJsonInType::JSON_TREE, resp_base));
  ObJsonObject *http_response = static_cast<ObJsonObject *>(resp_base);
  ObIJsonBase *result = nullptr;
  ASSERT_EQ(OB_SUCCESS, rerank_provider->parse_output(allocator, http_response, result));
  ASSERT_NE(nullptr, result);
  ObJsonArray *results_array = static_cast<ObJsonArray *>(result);
  ASSERT_EQ(1, results_array->element_count());
  ObJsonNode *result_node = results_array->get_value(0);
  ASSERT_NE(nullptr, result_node);
  ObJsonObject *result_obj = static_cast<ObJsonObject *>(result_node);
  ASSERT_NE(nullptr, result_obj->get_value("index"));
}

TEST_F(ObAIRerankProviderTest, provider_routing_should_be_backward_compatible)
{
  ObArenaAllocator allocator(ObModIds::TEST);
  ObAIFuncIRerank *rerank_provider = nullptr;

  ASSERT_EQ(OB_SUCCESS, ObAIFuncUtils::get_rerank_provider(allocator, ObString("siliconflow"), rerank_provider));
  ASSERT_NE(nullptr, rerank_provider);

  rerank_provider = nullptr;
  ASSERT_EQ(OB_SUCCESS, ObAIFuncUtils::get_rerank_provider(allocator, ObString("aliyun-dashscope"), rerank_provider));
  ASSERT_NE(nullptr, rerank_provider);

  rerank_provider = nullptr;
  ASSERT_EQ(OB_SUCCESS, ObAIFuncUtils::get_rerank_provider(allocator, ObString("aliyun-openai"), rerank_provider));
  ASSERT_NE(nullptr, rerank_provider);

  rerank_provider = nullptr;
  ASSERT_EQ(OB_SUCCESS, ObAIFuncUtils::get_rerank_provider(allocator, ObString("hunyuan-openai"), rerank_provider));
  ASSERT_NE(nullptr, rerank_provider);
}

TEST_F(ObAIRerankProviderTest, invalid_provider_should_fail_fast)
{
  ObArenaAllocator allocator(ObModIds::TEST);
  ObAIFuncIRerank *rerank_provider = nullptr;

  ASSERT_EQ(OB_INVALID_ARGUMENT, ObAIFuncUtils::get_rerank_provider(allocator, ObString(""), rerank_provider));
  ASSERT_EQ(nullptr, rerank_provider);

  rerank_provider = nullptr;
  ASSERT_EQ(OB_NOT_SUPPORTED, ObAIFuncUtils::get_rerank_provider(allocator, ObString("unknown"), rerank_provider));
  ASSERT_EQ(nullptr, rerank_provider);

  rerank_provider = nullptr;
  ASSERT_EQ(OB_NOT_SUPPORTED, ObAIFuncUtils::get_rerank_provider(allocator, ObString("deepseek"), rerank_provider));
  ASSERT_EQ(nullptr, rerank_provider);
}

TEST_F(ObAIRerankProviderTest, parse_output_should_fail_without_results)
{
  ObArenaAllocator allocator(ObModIds::TEST);
  ObAIFuncIRerank *rerank_provider = nullptr;
  ASSERT_EQ(OB_SUCCESS, ObAIFuncUtils::get_rerank_provider(allocator, ObString("openai"), rerank_provider));
  ASSERT_NE(nullptr, rerank_provider);

  ObString response("{\"id\": 0}");
  ObIJsonBase *j_base = nullptr;
  ASSERT_EQ(OB_SUCCESS,
            ObJsonBaseFactory::get_json_base(
                &allocator, response, ObJsonInType::JSON_TREE, ObJsonInType::JSON_TREE, j_base));
  ObJsonObject *http_response = static_cast<ObJsonObject *>(j_base);
  ObIJsonBase *result = nullptr;
  ASSERT_EQ(OB_INVALID_DATA, rerank_provider->parse_output(allocator, http_response, result));
  ASSERT_EQ(nullptr, result);
}

TEST_F(ObAIRerankProviderTest, parse_output_should_fail_with_non_array_results)
{
  ObArenaAllocator allocator(ObModIds::TEST);
  ObAIFuncIRerank *rerank_provider = nullptr;
  ASSERT_EQ(OB_SUCCESS, ObAIFuncUtils::get_rerank_provider(allocator, ObString("openai"), rerank_provider));
  ASSERT_NE(nullptr, rerank_provider);

  ObString response("{\"id\": 0, \"results\": {}}");
  ObIJsonBase *j_base = nullptr;
  ASSERT_EQ(OB_SUCCESS,
            ObJsonBaseFactory::get_json_base(
                &allocator, response, ObJsonInType::JSON_TREE, ObJsonInType::JSON_TREE, j_base));
  ObJsonObject *http_response = static_cast<ObJsonObject *>(j_base);
  ObIJsonBase *result = nullptr;
  ASSERT_EQ(OB_INVALID_DATA, rerank_provider->parse_output(allocator, http_response, result));
  ASSERT_EQ(nullptr, result);
}

int main(int argc, char **argv)
{
  ::testing::InitGoogleTest(&argc, argv);
  return RUN_ALL_TESTS();
}
