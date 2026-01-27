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
  ObJsonObject *body = nullptr;
  ASSERT_EQ(OB_SUCCESS, rerank_provider->get_body(allocator, model, query, document_array, &config, body));
  ASSERT_NE(nullptr, body);
  ASSERT_NE(nullptr, body->get_value("model"));
  ASSERT_NE(nullptr, body->get_value("query"));
  ASSERT_NE(nullptr, body->get_value("documents"));
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

int main(int argc, char **argv)
{
  ::testing::InitGoogleTest(&argc, argv);
  return RUN_ALL_TESTS();
}

