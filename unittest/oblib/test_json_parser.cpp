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
#include "rust/json.h"
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

TEST_F(TestJsonParser, MutationDeltaPreservesLegacyWireFormatAndOwnership)
{
  ObJsonBinUpdateCtx update(&allocator_);
  char path[] = "$[0]";
  char value[] = {1, static_cast<char>(0xff)};
  ASSERT_EQ(OB_SUCCESS, update.record_inline_diff(ObJsonDiffOp::REPLACE,
      0x82, ObString(4, path), 2, ObString(2, value)));
  ASSERT_EQ(OB_SUCCESS, update.record_binary_diff(42, 5));
  memset(path, 0, sizeof(path));
  memset(value, 0, sizeof(value));
  const unsigned char golden[] = {
      0, 1, 1, 0x8d, 0x80, 0x80, 0x80, 0, 1, 0x82, 2,
      4, '$', '[', '0', ']', 0, 2, 1, 0xff, 0};
  char encoded[sizeof(golden)] = {};
  int64_t pos = 0;
  ASSERT_EQ(sizeof(golden), update.logical_serialize_size());
  ASSERT_EQ(OB_SUCCESS, update.serialize_logical(encoded, sizeof(encoded), pos));
  ASSERT_EQ(sizeof(golden), pos);
  EXPECT_EQ(0, memcmp(golden, encoded, sizeof(golden)));
  EXPECT_EQ(1, update.logical_diff_count());
  EXPECT_EQ(1, update.binary_diff_count());
  EXPECT_EQ(42, update.binary_diff(0).offset);
  EXPECT_EQ(5, update.binary_diff(0).length);
  EXPECT_TRUE(update.intersects(43, 1));
  EXPECT_FALSE(update.intersects(47, 1));

  int64_t legacy_pos = 2;
  int64_t version = 0;
  int64_t length = 0;
  int32_t operation = 0;
  int8_t type = 0;
  int32_t flags = 0;
  ObString restored_path;
  ObString restored_value;
  ASSERT_EQ(OB_SUCCESS, serialization::decode_vi64(encoded, sizeof(encoded), legacy_pos, &version));
  ASSERT_EQ(OB_SUCCESS, serialization::decode_vi64(encoded, sizeof(encoded), legacy_pos, &length));
  ASSERT_EQ(OB_SUCCESS, serialization::decode_vi32(encoded, sizeof(encoded), legacy_pos, &operation));
  ASSERT_EQ(OB_SUCCESS, serialization::decode_i8(encoded, sizeof(encoded), legacy_pos, &type));
  ASSERT_EQ(OB_SUCCESS, serialization::decode_vi32(encoded, sizeof(encoded), legacy_pos, &flags));
  ASSERT_EQ(OB_SUCCESS, restored_path.deserialize(encoded, sizeof(encoded), legacy_pos));
  ASSERT_EQ(OB_SUCCESS, restored_value.deserialize(encoded, sizeof(encoded), legacy_pos));
  EXPECT_EQ(1, version);
  EXPECT_EQ(13, length);
  EXPECT_EQ(1, operation);
  EXPECT_EQ(0x82, static_cast<uint8_t>(type));
  EXPECT_EQ(2, flags);
  EXPECT_EQ(ObString("$[0]"), restored_path);
  EXPECT_EQ(ObString(2, reinterpret_cast<const char *>(golden) + sizeof(golden) - 3), restored_value);
  EXPECT_EQ(sizeof(golden), legacy_pos);

  ObJsonBinUpdateCtx replay(&allocator_);
  ObSEArray<BinaryDiff, 1> physical;
  ASSERT_EQ(OB_SUCCESS, physical.push_back(BinaryDiff{42, 5}));
  ASSERT_EQ(OB_SUCCESS, replay.restore_delta(encoded, sizeof(encoded), physical));
  EXPECT_EQ(1, replay.logical_diff_count());
  EXPECT_EQ(1, replay.binary_diff_count());
  EXPECT_FALSE(replay.is_rebuild_all());
  replay.mark_rebuild_all();
  EXPECT_TRUE(replay.is_rebuild_all());
  EXPECT_EQ(OB_DESERIALIZE_ERROR, replay.restore_delta(encoded, sizeof(encoded) - 1, physical));
  EXPECT_EQ(1, replay.logical_diff_count());
  EXPECT_EQ(1, replay.binary_diff_count());
}

TEST_F(TestJsonParser, MutationDeltaLifetimeAndAllocationFailure)
{
  class TrackingAllocator : public ObIAllocator
  {
  public:
    void *alloc(const int64_t size) override
    {
      void *value = fail_ ? nullptr : arena_.alloc(size);
      if (value != nullptr) { ++live_; }
      return value;
    }
    void *alloc(const int64_t size, const ObMemAttr &) override { return alloc(size); }
    void free(void *value) override
    {
      if (value != nullptr) { --live_; arena_.free(value); }
    }
    ObArenaAllocator arena_;
    bool fail_ = false;
    int64_t live_ = 0;
  } tracking;
  {
    ObJsonBinUpdateCtx update(&tracking);
    ASSERT_EQ(OB_SUCCESS, update.init_status());
    EXPECT_EQ(1, tracking.live_);
    ASSERT_EQ(OB_SUCCESS, update.record_remove_diff(ObString("$.a")));
  }
  EXPECT_EQ(0, tracking.live_);
  tracking.fail_ = true;
  ObJsonBinUpdateCtx update(&tracking);
  EXPECT_EQ(OB_ALLOCATE_MEMORY_FAILED, update.init_status());
  EXPECT_EQ(OB_ALLOCATE_MEMORY_FAILED, update.record_remove_diff(ObString("$.a")));
  EXPECT_EQ(OB_ALLOCATE_MEMORY_FAILED, update.record_binary_diff(0, 1));
  char bytes[2] = {};
  int64_t pos = 0;
  EXPECT_EQ(OB_ALLOCATE_MEMORY_FAILED, update.serialize_logical(bytes, sizeof(bytes), pos));
  EXPECT_EQ(0, pos);
}

TEST_F(TestJsonParser, BinaryMetadata)
{
  const std::string text = "{\"a\":[1,{\"b\":2}]}";
  ObString binary;
  ASSERT_EQ(OB_SUCCESS, ObJsonParser::get_binary(&allocator_, text.data(), text.size(), binary));
  const uint8_t *input = reinterpret_cast<const uint8_t *>(binary.ptr());
  json::BinaryMetadataResult metadata = json::inspect_binary(
      rust::Slice<const uint8_t>(input, binary.length()), true);
  ASSERT_EQ(json::Status::Ok, metadata.status);
  EXPECT_EQ(static_cast<uint8_t>(ObJsonNodeType::J_OBJECT), metadata.value_type);
  EXPECT_EQ(4, metadata.depth);
  EXPECT_EQ(1, metadata.length);

  json::BinaryMetadataResult type_only = json::inspect_binary(
      rust::Slice<const uint8_t>(input, binary.length()), false);
  ASSERT_EQ(json::Status::Ok, type_only.status);
  EXPECT_EQ(0, type_only.depth);
}

TEST_F(TestJsonParser, RustPathQuery)
{
  const std::string text = "{\"a\":[0,1,2,3],\"b\":{\"a\":5}}";
  ObString binary;
  ASSERT_EQ(OB_SUCCESS, ObJsonParser::get_binary(&allocator_, text.data(), text.size(), binary));
  const uint8_t *input = reinterpret_cast<const uint8_t *>(binary.ptr());
  rust::Slice<const uint8_t> slice(input, binary.length());
  rust::Box<json::PathParseResult> parsed = json::parse_mysql_path(
      rust::Slice<const uint8_t>(reinterpret_cast<const uint8_t *>(text.data()), 0));
  EXPECT_EQ(json::Status::InvalidPath, parsed->status());

  const std::string path_text = "$**.a";
  parsed = json::parse_mysql_path(rust::Slice<const uint8_t>(
      reinterpret_cast<const uint8_t *>(path_text.data()), path_text.size()));
  ASSERT_EQ(json::Status::Ok, parsed->status());
  EXPECT_TRUE(parsed->path().can_match_many());
  json::PathQueryResult result = json::query_binary(parsed->path(), slice, true, false);
  ASSERT_EQ(json::Status::Ok, result.status);
  EXPECT_EQ(2, result.match_count);

  result = json::query_binary(parsed->path(), slice, true, true);
  ASSERT_EQ(json::Status::Ok, result.status);
  EXPECT_EQ(1, result.match_count);
  EXPECT_EQ(4, result.single_length);

  result = json::query_text(parsed->path(),
      rust::Slice<const uint8_t>(reinterpret_cast<const uint8_t *>(text.data()), text.size()),
      true, false, 101);
  ASSERT_EQ(json::Status::Ok, result.status);
  EXPECT_EQ(2, result.match_count);
}

} // namespace common
} // namespace oceanbase

int main(int argc, char **argv)
{
  ::testing::InitGoogleTest(&argc, argv);
  return RUN_ALL_TESTS();
}
