/*
 * Copyright (c) 2026 OceanBase.
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

#include "common/json_type/ob_json_base.h"
#include "common/json_type/ob_json_bin.h"
#include "common/json_type/ob_json_diff.h"
#include "common/json_type/ob_json_parse.h"
#include "lib/allocator/page_arena.h"
#include <gtest/gtest.h>
#include <cmath>
#include <string>
#include <vector>

namespace oceanbase
{
namespace common
{

class TestJsonParser : public ::testing::Test
{
protected:
  int parse(const std::string &text, ObJsonNode *&node, uint32_t flags = 0)
  {
    node = nullptr;
    return ObJsonParser::get_tree(&allocator_, text.data(), text.size(), node, flags);
  }

  ObArenaAllocator allocator_;
};

TEST_F(TestJsonParser, StandardSyntaxCorpus)
{
  const std::vector<std::string> valid = {
      "null", "true", "false", "\"text\"", "[]", "{}",
      "{\"a\":[1,-2,3.5e-2,true,false,null],\"b\":{\"c\":\"d\"}}",
      "\"\\u4f60\\u597d\\ud83d\\ude00\"", "\"雪😀\"",
      "0", "-0", "0.0", "-0.0", "1e308", "5e-324"};
  for (const auto &text : valid) {
    ObJsonNode *node = nullptr;
    ASSERT_EQ(OB_SUCCESS, parse(text, node)) << text;
    ASSERT_NE(nullptr, node) << text;
  }

  const std::vector<std::string> rejected = {
      "{a:1}", "{'a':1}", "/* comment */null", "[1,]", "{\"a\":1,}",
      "TRUE", "False", "NULL", "+1", ".5", "01", "0x10", "NaN",
      "Infinity", "1.", "\"unterminated", "[1 2]", "true false",
      "\"\\x20\"", "\"\\ud800\""};
  for (const auto &text : rejected) {
    ObJsonNode *node = nullptr;
    EXPECT_EQ(OB_ERR_INVALID_JSON_TEXT, parse(text, node)) << text;
    EXPECT_EQ(OB_ERR_INVALID_JSON_TEXT,
        ObJsonParser::check_json_syntax(ObString(text.size(), text.data()), &allocator_)) << text;
  }
  const std::string invalid_utf8("\"\xff\"", 3);
  ObJsonNode *node = nullptr;
  EXPECT_EQ(OB_ERR_INVALID_JSON_TEXT, parse(invalid_utf8, node));
}

TEST_F(TestJsonParser, NumericClassification)
{
  ObJsonNode *node = nullptr;
  ASSERT_EQ(OB_SUCCESS, parse("9223372036854775807", node));
  ASSERT_EQ(ObJsonNodeType::J_UINT, node->json_type());
  EXPECT_EQ(INT64_MAX, static_cast<ObJsonUint *>(node)->value());
  ASSERT_EQ(OB_SUCCESS, parse("4294967295", node));
  ASSERT_EQ(ObJsonNodeType::J_INT, node->json_type());
  EXPECT_EQ(UINT32_MAX, static_cast<ObJsonInt *>(node)->value());
  ASSERT_EQ(OB_SUCCESS, parse("4294967296", node));
  ASSERT_EQ(ObJsonNodeType::J_UINT, node->json_type());
  EXPECT_EQ(UINT64_C(4294967296), static_cast<ObJsonUint *>(node)->value());
  ASSERT_EQ(OB_SUCCESS, parse("9223372036854775808", node));
  ASSERT_EQ(ObJsonNodeType::J_UINT, node->json_type());
  EXPECT_EQ(UINT64_C(9223372036854775808), static_cast<ObJsonUint *>(node)->value());
  ASSERT_EQ(OB_SUCCESS, parse("18446744073709551615", node));
  ASSERT_EQ(ObJsonNodeType::J_UINT, node->json_type());
  EXPECT_EQ(UINT64_MAX, static_cast<ObJsonUint *>(node)->value());
  ASSERT_EQ(OB_SUCCESS, parse("18446744073709551616", node));
  EXPECT_EQ(ObJsonNodeType::J_DOUBLE, node->json_type());
  ASSERT_EQ(OB_SUCCESS, parse("-0", node));
  ASSERT_EQ(ObJsonNodeType::J_INT, node->json_type());
  EXPECT_EQ(0, static_cast<ObJsonInt *>(node)->value());
  ASSERT_EQ(OB_SUCCESS, parse("-0.0", node));
  ASSERT_EQ(ObJsonNodeType::J_DOUBLE, node->json_type());
  EXPECT_TRUE(std::signbit(static_cast<ObJsonDouble *>(node)->value()));
}

TEST_F(TestJsonParser, DuplicatePolicies)
{
  const std::string text = "{\"a\":[1,2],\"b\":0,\"a\":{\"x\":3},\"c\":4}";
  ObJsonNode *node = nullptr;
  ASSERT_EQ(OB_SUCCESS, parse(text, node));
  ASSERT_EQ(ObJsonNodeType::J_OBJECT, node->json_type());
  ObJsonObject *object = static_cast<ObJsonObject *>(node);
  EXPECT_EQ(3, object->element_count());
  ASSERT_NE(nullptr, object->get_value("a"));
  EXPECT_EQ(ObJsonNodeType::J_OBJECT, object->get_value("a")->json_type());

  ASSERT_EQ(OB_SUCCESS, parse(text, node, ObJsonParser::JSN_PRESERVE_DUP_FLAG));
  EXPECT_EQ(4, static_cast<ObJsonObject *>(node)->element_count());
  EXPECT_EQ(OB_ERR_DUPLICATE_KEY, parse(text, node, ObJsonParser::JSN_UNIQUE_FLAG));
  ASSERT_EQ(OB_SUCCESS, parse(text, node, ObJsonParser::JSN_SCHEMA_FLAG));
  object = static_cast<ObJsonObject *>(node);
  ASSERT_NE(nullptr, object->get_value("a"));
  EXPECT_EQ(ObJsonNodeType::J_ARRAY, object->get_value("a")->json_type());
}

TEST_F(TestJsonParser, DepthSchemaAndByteOffsets)
{
  ObJsonNode *node = nullptr;
  const std::string at_limit(101, '[');
  const std::string valid = at_limit + "0" + std::string(101, ']');
  EXPECT_EQ(OB_SUCCESS, parse(valid, node));
  const std::string beyond_limit = "[" + valid + "]";
  EXPECT_EQ(OB_ERR_INVALID_JSON_TEXT, parse(beyond_limit, node));
  EXPECT_EQ(OB_ERR_JSON_OUT_OF_DEPTH,
      ObJsonParser::check_json_syntax(ObString(valid.size(), valid.data()), &allocator_));
  EXPECT_EQ(OB_ERR_UNSUPPROTED_REF_IN_JSON_SCHEMA,
      parse("{\"$ref\":\"https://example.com/a\"}", node, ObJsonParser::JSN_SCHEMA_FLAG));

  const std::string malformed = "{\"雪\":1,}";
  char message_buffer[ObJsonParser::PARSE_SYNTAXERR_MESSAGE_LENGTH] = {0};
  const char *message = message_buffer;
  uint64_t offset = 0;
  EXPECT_EQ(OB_ERR_INVALID_JSON_TEXT,
      ObJsonParser::parse_json_text(&allocator_, malformed.data(), malformed.size(),
          message, &offset, node));
  EXPECT_EQ(malformed.size() - 1, offset);
  ASSERT_NE(nullptr, message);
}

TEST_F(TestJsonParser, BinaryConversionRejectsActiveUpdateContext)
{
  ObJsonBinUpdateCtx update_ctx(&allocator_);
  ObJsonBinCtx bin_ctx;
  bin_ctx.update_ctx_ = &update_ctx;
  ObJsonBin json_bin(&allocator_, &bin_ctx);
  ObIJsonBase *result = &json_bin;
  const std::string text = "{\"a\":1}";

  EXPECT_EQ(OB_ERR_UNEXPECTED,
      ObJsonBaseFactory::get_json_base(&allocator_, text.data(), text.size(),
          ObJsonInType::JSON_TREE, ObJsonInType::JSON_BIN, result));
  EXPECT_EQ(&json_bin, result);
  EXPECT_EQ(&update_ctx, json_bin.get_update_ctx());
}

} // namespace common
} // namespace oceanbase

int main(int argc, char **argv)
{
  ::testing::InitGoogleTest(&argc, argv);
  return RUN_ALL_TESTS();
}
