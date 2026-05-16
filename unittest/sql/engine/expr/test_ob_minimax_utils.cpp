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

class ObMiniMaxUtilsTest: public ::testing::Test
{
public:
    ObMiniMaxUtilsTest();
    virtual ~ObMiniMaxUtilsTest();
    virtual void SetUp();
    virtual void TearDown();
private:
    // disallow copy
    ObMiniMaxUtilsTest(const ObMiniMaxUtilsTest &other);
    ObMiniMaxUtilsTest& operator=(const ObMiniMaxUtilsTest &other);
protected:
    // data members
};

ObMiniMaxUtilsTest::ObMiniMaxUtilsTest()
{
}

ObMiniMaxUtilsTest::~ObMiniMaxUtilsTest()
{
}

void ObMiniMaxUtilsTest::SetUp()
{
}

void ObMiniMaxUtilsTest::TearDown()
{
}

TEST_F(ObMiniMaxUtilsTest, test_embedding_get_header)
{
    ObArenaAllocator allocator(ObModIds::TEST);
    ObString api_key("minimax-test-key-1234567890");
    ObString authorization("Authorization: Bearer minimax-test-key-1234567890");
    ObString content_type("Content-Type: application/json");
    ObMiniMaxUtils::ObMiniMaxEmbed embedding;
    ObArray<ObString> headers;
    ASSERT_EQ(OB_SUCCESS, embedding.get_header(allocator, api_key, headers));
    ASSERT_EQ(2, headers.count());
    ASSERT_EQ(authorization, headers[0]);
    ASSERT_EQ(content_type, headers[1]);
}

TEST_F(ObMiniMaxUtilsTest, test_embedding_get_body)
{
    ObArenaAllocator allocator(ObModIds::TEST);
    ObString model("embo-01");
    ObString input("oceanbase seekdb is an AI-native search database");
    ObArray<ObString> input_array;
    input_array.push_back(input);
    ObMiniMaxUtils::ObMiniMaxEmbed embedding;
    ObJsonObject *body = nullptr;
    ObJsonObject *config = nullptr;
    ASSERT_EQ(OB_SUCCESS, embedding.get_body(allocator, model, input_array, config, body));

    // Check model field
    ObJsonNode *model_node = body->get_value("model");
    ObStringBuffer model_buf(&allocator);
    model_node->print(model_buf, 0);
    ASSERT_EQ(model, model_buf.string());

    // Check texts field (MiniMax uses "texts" instead of "input")
    ObJsonNode *texts_node = body->get_value("texts");
    ASSERT_TRUE(texts_node != nullptr);
    ObJsonArray *texts_array_node = static_cast<ObJsonArray *>(texts_node);
    ASSERT_EQ(1, texts_array_node->element_count());
    ObJsonNode *text_node = texts_array_node->get_value(0);
    ObStringBuffer text_buf(&allocator);
    text_node->print(text_buf, 0);
    ASSERT_EQ(input, text_buf.string());

    // Check type field (MiniMax requires "type" field)
    ObJsonNode *type_node = body->get_value("type");
    ASSERT_TRUE(type_node != nullptr);
    ObStringBuffer type_buf(&allocator);
    type_node->print(type_buf, 0);
    ASSERT_EQ(ObString("db"), type_buf.string());

    // Verify "input" field does NOT exist (MiniMax uses "texts")
    ObJsonNode *input_node = body->get_value("input");
    ASSERT_TRUE(input_node == nullptr);
}

TEST_F(ObMiniMaxUtilsTest, test_embedding_get_body_multiple_texts)
{
    ObArenaAllocator allocator(ObModIds::TEST);
    ObString model("embo-01");
    ObString text1("vector search is powerful");
    ObString text2("hybrid search combines vector and text");
    ObArray<ObString> input_array;
    input_array.push_back(text1);
    input_array.push_back(text2);
    ObMiniMaxUtils::ObMiniMaxEmbed embedding;
    ObJsonObject *body = nullptr;
    ObJsonObject *config = nullptr;
    ASSERT_EQ(OB_SUCCESS, embedding.get_body(allocator, model, input_array, config, body));

    // Check texts array has 2 elements
    ObJsonNode *texts_node = body->get_value("texts");
    ASSERT_TRUE(texts_node != nullptr);
    ObJsonArray *texts_array = static_cast<ObJsonArray *>(texts_node);
    ASSERT_EQ(2, texts_array->element_count());
}

TEST_F(ObMiniMaxUtilsTest, test_embedding_get_body_empty_model)
{
    ObArenaAllocator allocator(ObModIds::TEST);
    ObString model("");
    ObString input("test");
    ObArray<ObString> input_array;
    input_array.push_back(input);
    ObMiniMaxUtils::ObMiniMaxEmbed embedding;
    ObJsonObject *body = nullptr;
    ObJsonObject *config = nullptr;
    ASSERT_EQ(OB_INVALID_ARGUMENT, embedding.get_body(allocator, model, input_array, config, body));
}

TEST_F(ObMiniMaxUtilsTest, test_embedding_get_body_empty_contents)
{
    ObArenaAllocator allocator(ObModIds::TEST);
    ObString model("embo-01");
    ObArray<ObString> input_array;
    ObMiniMaxUtils::ObMiniMaxEmbed embedding;
    ObJsonObject *body = nullptr;
    ObJsonObject *config = nullptr;
    ASSERT_EQ(OB_INVALID_ARGUMENT, embedding.get_body(allocator, model, input_array, config, body));
}

TEST_F(ObMiniMaxUtilsTest, test_embedding_parse_output)
{
    ObArenaAllocator allocator(ObModIds::TEST);
    ObString content("[0.0023064255, -0.009327292, -0.0028842222]");
    // MiniMax embedding response uses "vectors" instead of "data"
    ObString response(
        "{"
            "\"vectors\": ["
                "[0.0023064255, -0.009327292, -0.0028842222]"
            "],"
            "\"total_tokens\": 10,"
            "\"base_resp\": {"
                "\"status_code\": 0,"
                "\"status_msg\": \"success\""
            "}"
        "}"
    );
    ObMiniMaxUtils::ObMiniMaxEmbed embedding;
    ObIJsonBase *j_base = nullptr;
    ASSERT_EQ(OB_SUCCESS, ObJsonBaseFactory::get_json_base(&allocator, response, ObJsonInType::JSON_TREE, ObJsonInType::JSON_TREE, j_base));
    ObJsonObject *http_response = static_cast<ObJsonObject *>(j_base);
    ObIJsonBase *result = nullptr;
    ASSERT_EQ(OB_SUCCESS, embedding.parse_output(allocator, http_response, result));

    ObJsonArray *embeddings_array = static_cast<ObJsonArray *>(result);
    ASSERT_EQ(1, embeddings_array->element_count());

    ObJsonNode *embedding_node = embeddings_array->get_value(0);
    ObJsonArray *embedding_array = static_cast<ObJsonArray *>(embedding_node);
    ObStringBuffer embedding_buf(&allocator);
    embedding_array->print(embedding_buf, 0);
    ASSERT_EQ(content, embedding_buf.string());
}

TEST_F(ObMiniMaxUtilsTest, test_embedding_parse_output_multiple_vectors)
{
    ObArenaAllocator allocator(ObModIds::TEST);
    // MiniMax returns multiple vectors for batch requests
    ObString response(
        "{"
            "\"vectors\": ["
                "[0.1, 0.2, 0.3],"
                "[0.4, 0.5, 0.6]"
            "],"
            "\"total_tokens\": 20,"
            "\"base_resp\": {"
                "\"status_code\": 0,"
                "\"status_msg\": \"success\""
            "}"
        "}"
    );
    ObMiniMaxUtils::ObMiniMaxEmbed embedding;
    ObIJsonBase *j_base = nullptr;
    ASSERT_EQ(OB_SUCCESS, ObJsonBaseFactory::get_json_base(&allocator, response, ObJsonInType::JSON_TREE, ObJsonInType::JSON_TREE, j_base));
    ObJsonObject *http_response = static_cast<ObJsonObject *>(j_base);
    ObIJsonBase *result = nullptr;
    ASSERT_EQ(OB_SUCCESS, embedding.parse_output(allocator, http_response, result));

    ObJsonArray *embeddings_array = static_cast<ObJsonArray *>(result);
    ASSERT_EQ(2, embeddings_array->element_count());
}

TEST_F(ObMiniMaxUtilsTest, test_embedding_parse_output_empty)
{
    ObArenaAllocator allocator(ObModIds::TEST);
    // Response without "vectors" field should fail
    ObString response(
        "{"
            "\"total_tokens\": 10,"
            "\"base_resp\": {"
                "\"status_code\": 0,"
                "\"status_msg\": \"success\""
            "}"
        "}"
    );
    ObMiniMaxUtils::ObMiniMaxEmbed embedding;
    ObIJsonBase *j_base = nullptr;
    ASSERT_EQ(OB_SUCCESS, ObJsonBaseFactory::get_json_base(&allocator, response, ObJsonInType::JSON_TREE, ObJsonInType::JSON_TREE, j_base));
    ObJsonObject *http_response = static_cast<ObJsonObject *>(j_base);
    ObIJsonBase *result = nullptr;
    ASSERT_EQ(OB_INVALID_DATA, embedding.parse_output(allocator, http_response, result));
}

TEST_F(ObMiniMaxUtilsTest, test_embedding_parse_output_null_response)
{
    ObArenaAllocator allocator(ObModIds::TEST);
    ObMiniMaxUtils::ObMiniMaxEmbed embedding;
    ObIJsonBase *result = nullptr;
    ASSERT_EQ(OB_INVALID_ARGUMENT, embedding.parse_output(allocator, nullptr, result));
}

TEST_F(ObMiniMaxUtilsTest, test_provider_constant)
{
    // Verify that MINIMAX provider constant is correctly defined
    ObString minimax_provider(ObAIFuncProviderUtils::MINIMAX);
    ASSERT_EQ(ObString("MINIMAX"), minimax_provider);
}

TEST_F(ObMiniMaxUtilsTest, test_provider_routing_complete)
{
    ObArenaAllocator allocator(ObModIds::TEST);
    ObString minimax_provider("MINIMAX");
    ObAIFuncIComplete *complete_provider = nullptr;
    // MiniMax completion should route to OpenAI-compatible handler
    ASSERT_EQ(OB_SUCCESS, ObAIFuncUtils::get_complete_provider(allocator, minimax_provider, complete_provider));
    ASSERT_TRUE(complete_provider != nullptr);
}

TEST_F(ObMiniMaxUtilsTest, test_provider_routing_embed)
{
    ObArenaAllocator allocator(ObModIds::TEST);
    ObString minimax_provider("MINIMAX");
    ObAIFuncIEmbed *embed_provider = nullptr;
    // MiniMax embedding should route to MiniMax-specific handler
    ASSERT_EQ(OB_SUCCESS, ObAIFuncUtils::get_embed_provider(allocator, minimax_provider, embed_provider));
    ASSERT_TRUE(embed_provider != nullptr);
}


int main(int argc, char **argv)
{
    ::testing::InitGoogleTest(&argc,argv);
    return RUN_ALL_TESTS();
}
