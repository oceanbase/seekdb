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
#include "common/json_type/json_builder.h"
#include "json_mutation_golden.h"
#include "json_hash_inputs.h"
#include "json_hash_golden.h"
#include "rust/json.h"
#include "lib/allocator/page_arena.h"
#include <gtest/gtest.h>
#include <cmath>
#include <limits>
#include <string>
#include <vector>

namespace oceanbase
{
namespace common
{

class TestJsonParser : public ::testing::Test
{
protected:
  int parse(const std::string &text, ObIJsonBase *&node, uint32_t flags = 0)
  {
    node = nullptr;
    ObString binary;
    const int ret = ObJsonParser::get_binary(&allocator_, text.data(), text.size(), binary, flags);
    return ret == OB_SUCCESS ? ObJsonBaseFactory::get_json_base(&allocator_, binary,
        ObJsonInType::JSON_BIN, node) : ret;
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
    ObIJsonBase *node = nullptr;
    ASSERT_EQ(OB_SUCCESS, parse(text, node)) << text;
    ASSERT_NE(nullptr, node) << text;
  }

  const std::vector<std::string> rejected = {
      "{a:1}", "{'a':1}", "/* comment */null", "[1,]", "{\"a\":1,}",
      "TRUE", "False", "NULL", "+1", ".5", "01", "0x10", "NaN",
      "Infinity", "1.", "\"unterminated", "[1 2]", "true false",
      "\"\\x20\"", "\"\\ud800\""};
  for (const auto &text : rejected) {
    ObIJsonBase *node = nullptr;
    EXPECT_EQ(OB_ERR_INVALID_JSON_TEXT, parse(text, node)) << text;
    EXPECT_EQ(OB_ERR_INVALID_JSON_TEXT,
        ObJsonParser::check_json_syntax(ObString(text.size(), text.data()), &allocator_)) << text;
  }
  const std::string invalid_utf8("\"\xff\"", 3);
  ObIJsonBase *node = nullptr;
  EXPECT_EQ(OB_ERR_INVALID_JSON_TEXT, parse(invalid_utf8, node));
}

TEST_F(TestJsonParser, NumericClassification)
{
  ObIJsonBase *node = nullptr;
  ASSERT_EQ(OB_SUCCESS, parse("9223372036854775807", node));
  ASSERT_EQ(ObJsonNodeType::J_UINT, node->json_type());
  EXPECT_EQ(INT64_MAX, node->get_uint());
  ASSERT_EQ(OB_SUCCESS, parse("4294967295", node));
  ASSERT_EQ(ObJsonNodeType::J_INT, node->json_type());
  EXPECT_EQ(UINT32_MAX, node->get_int());
  ASSERT_EQ(OB_SUCCESS, parse("4294967296", node));
  ASSERT_EQ(ObJsonNodeType::J_UINT, node->json_type());
  EXPECT_EQ(UINT64_C(4294967296), node->get_uint());
  ASSERT_EQ(OB_SUCCESS, parse("9223372036854775808", node));
  ASSERT_EQ(ObJsonNodeType::J_UINT, node->json_type());
  EXPECT_EQ(UINT64_C(9223372036854775808), node->get_uint());
  ASSERT_EQ(OB_SUCCESS, parse("18446744073709551615", node));
  ASSERT_EQ(ObJsonNodeType::J_UINT, node->json_type());
  EXPECT_EQ(UINT64_MAX, node->get_uint());
  ASSERT_EQ(OB_SUCCESS, parse("18446744073709551616", node));
  EXPECT_EQ(ObJsonNodeType::J_DOUBLE, node->json_type());
  ASSERT_EQ(OB_SUCCESS, parse("-0", node));
  ASSERT_EQ(ObJsonNodeType::J_INT, node->json_type());
  EXPECT_EQ(0, node->get_int());
  ASSERT_EQ(OB_SUCCESS, parse("-0.0", node));
  ASSERT_EQ(ObJsonNodeType::J_DOUBLE, node->json_type());
  EXPECT_TRUE(std::signbit(node->get_double()));
}

TEST_F(TestJsonParser, DuplicatePolicies)
{
  const std::string text = "{\"a\":[1,2],\"b\":0,\"a\":{\"x\":3},\"c\":4}";
  ObIJsonBase *node = nullptr;
  ASSERT_EQ(OB_SUCCESS, parse(text, node));
  ASSERT_EQ(ObJsonNodeType::J_OBJECT, node->json_type());
  ObIJsonBase *object = node;
  EXPECT_EQ(3, object->element_count());
  ObIJsonBase *member = nullptr;
  ASSERT_EQ(OB_SUCCESS, object->get_object_value(ObString("a"), member));
  ASSERT_NE(nullptr, member);
  EXPECT_EQ(ObJsonNodeType::J_OBJECT, member->json_type());

  ASSERT_EQ(OB_SUCCESS, parse(text, node, ObJsonParser::JSN_PRESERVE_DUP_FLAG));
  EXPECT_EQ(4, node->element_count());
  EXPECT_EQ(OB_ERR_DUPLICATE_KEY, parse(text, node, ObJsonParser::JSN_UNIQUE_FLAG));
  ASSERT_EQ(OB_SUCCESS, parse(text, node, ObJsonParser::JSN_SCHEMA_FLAG));
  object = node;
  ASSERT_EQ(OB_SUCCESS, object->get_object_value(ObString("a"), member));
  ASSERT_NE(nullptr, member);
  EXPECT_EQ(ObJsonNodeType::J_ARRAY, member->json_type());
}

TEST_F(TestJsonParser, DepthSchemaAndByteOffsets)
{
  ObIJsonBase *node = nullptr;
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
  const auto malformed_result = json::parse_binary(
      rust::Slice<const uint8_t>(reinterpret_cast<const uint8_t *>(malformed.data()), malformed.size()),
      json::Duplicate::Overwrite, 101, false);
  EXPECT_EQ(json::Status::Syntax, malformed_result.status);
  EXPECT_EQ(malformed.size() - 1, malformed_result.byte_offset);
  EXPECT_FALSE(malformed_result.message.empty());
}

TEST_F(TestJsonParser, NativeScalarAdaptersPreserveDatabasePayloads)
{
  ObString binary;
  ObJsonBin value(&allocator_);
  ASSERT_EQ(OB_SUCCESS, encode_json_integer(allocator_, INT64_MIN, binary));
  ASSERT_EQ(OB_SUCCESS, value.reset(binary, 0, nullptr));
  EXPECT_EQ(INT64_MIN, value.get_int());
  ASSERT_EQ(OB_SUCCESS, encode_json_unsigned(allocator_, UINT64_MAX, binary));
  ASSERT_EQ(OB_SUCCESS, value.reset(binary, 0, nullptr));
  EXPECT_EQ(UINT64_MAX, value.get_uint());
  ASSERT_EQ(OB_SUCCESS, encode_json_real(allocator_, -0.0, binary));
  ASSERT_EQ(OB_SUCCESS, value.reset(binary, 0, nullptr));
  EXPECT_TRUE(std::signbit(value.get_double()));
  EXPECT_EQ(OB_INVALID_NUMERIC,
      encode_json_real(allocator_, std::numeric_limits<double>::infinity(), binary));
  ASSERT_EQ(OB_SUCCESS, encode_json_boolean(allocator_, true, binary));
  ASSERT_EQ(OB_SUCCESS, value.reset(binary, 0, nullptr));
  EXPECT_TRUE(value.get_boolean());
  const char embedded_null[] = {'a', '\0', 'b'};
  const ObString text(sizeof(embedded_null), embedded_null);
  ASSERT_EQ(OB_SUCCESS, encode_json_text(allocator_, text, binary));
  ASSERT_EQ(OB_SUCCESS, value.reset(binary, 0, nullptr));
  EXPECT_EQ(text, ObString(value.get_data_length(), value.get_data()));
  ASSERT_EQ(OB_SUCCESS, encode_json_opaque(allocator_, text, static_cast<uint16_t>(ObBitType), binary));
  ASSERT_EQ(OB_SUCCESS, value.reset(binary, 0, nullptr));
  EXPECT_EQ(ObBitType, value.field_type());
  EXPECT_EQ(text, ObString(value.get_data_length(), value.get_data()));

  number::ObNumber number;
  ASSERT_EQ(OB_SUCCESS, number.from("123.4500", allocator_));
  ASSERT_EQ(OB_SUCCESS, encode_json_decimal(allocator_, number, 19, 4, binary));
  ASSERT_EQ(OB_SUCCESS, value.reset(binary, 0, nullptr));
  EXPECT_EQ(19, value.get_decimal_precision());
  EXPECT_EQ(4, value.get_decimal_scale());
  EXPECT_EQ(0, number.compare(value.get_decimal_data()));

  ObTime date(DT_TYPE_DATE);
  ASSERT_EQ(OB_SUCCESS, ObTimeConverter::date_to_ob_time(20000, date));
  ASSERT_EQ(OB_SUCCESS, encode_json_time(allocator_, ObJsonNodeType::J_DATE, date, binary));
  ASSERT_EQ(OB_SUCCESS, value.reset(binary, 0, nullptr));
  EXPECT_EQ(ObJsonNodeType::J_DATE, value.json_type());
  EXPECT_EQ(20000, value.get_int());
  ASSERT_EQ(OB_SUCCESS, encode_json_time(allocator_, ObJsonNodeType::J_MYSQL_DATE, date, binary));
  ASSERT_EQ(OB_SUCCESS, value.reset(binary, 0, nullptr));
  EXPECT_EQ(ObJsonNodeType::J_MYSQL_DATE, value.json_type());
  ObTime decoded_date(DT_TYPE_DATE);
  ASSERT_EQ(OB_SUCCESS, value.get_obtime(decoded_date));
  EXPECT_EQ(20000, ObTimeConverter::ob_time_to_date(decoded_date));

  ObTime datetime(DT_TYPE_DATETIME);
  const int64_t timestamp = INT64_C(17280000123456);
  ASSERT_EQ(OB_SUCCESS, ObTimeConverter::datetime_to_ob_time(timestamp, nullptr, datetime));
  for (const auto type : {ObJsonNodeType::J_DATETIME, ObJsonNodeType::J_TIMESTAMP,
                         ObJsonNodeType::J_MYSQL_DATETIME}) {
    ASSERT_EQ(OB_SUCCESS, encode_json_time(allocator_, type, datetime, binary));
    ASSERT_EQ(OB_SUCCESS, value.reset(binary, 0, nullptr));
    EXPECT_EQ(type, value.json_type());
    ObTime decoded(DT_TYPE_DATETIME);
    ASSERT_EQ(OB_SUCCESS, value.get_obtime(decoded));
    decoded.parts_[DT_DATE] = ObTimeConverter::ob_time_to_date(decoded);
    ObTimeConvertCtx context(nullptr, false);
    int64_t restored = 0;
    ASSERT_EQ(OB_SUCCESS, ObTimeConverter::ob_time_to_datetime(decoded, context, restored));
    EXPECT_EQ(timestamp, restored);
  }
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
          ObJsonInType::JSON_TEXT, result));
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

TEST_F(TestJsonParser, NativeMutationPreservesBinaryMutationGoldens)
{
  struct EditCase {
    std::string document;
    std::string value;
    std::vector<uint64_t> ancestors;
    uint8_t operation;
    uint64_t index;
    std::string key;
  };
  std::vector<EditCase> cases = {
      {"[1,2]", "-128", {}, 1, 0, ""},
      {"[1,2]", "128", {}, 1, 0, ""},
      {"[1,2]", "null", {}, 1, 1, ""},
      {"[1,2]", "true", {}, 1, 1, ""},
      {"[1,2]", "\"new string\"", {}, 1, 0, ""},
      {"[\"original long string\",2]", "\"x\"", {}, 1, 0, ""},
      {"[\"original long string\",2]", "1.25", {}, 1, 0, ""},
      {"[1,2]", "42", {}, 2, 0, ""},
      {"[1,2]", "42", {}, 2, 1, ""},
      {"[1,2]", "42", {}, 2, 99, ""},
      {"{\"a\":1,\"bb\":2}", "42", {}, 2, 0, "c"},
      {"{\"a\":1,\"bb\":2}", "42", {}, 2, 0, "a"},
      {"{\"a\":1,\"bb\":2}", "null", {}, 3, 0, ""},
      {"{\"a\":1,\"bb\":2}", "null", {}, 3, 1, ""},
      {"[1,2,3]", "null", {}, 3, 0, ""},
      {"[1,2,3]", "null", {}, 3, 1, ""},
      {"{\"a\":[1,2],\"b\":{\"c\":3}}", "42", {0}, 2, 1, ""},
      {"{\"a\":[1,2],\"b\":{\"c\":3}}", "42", {1}, 2, 0, "d"},
      {"{\"a\":[1,2],\"b\":{\"c\":3}}", "null", {0}, 3, 0, ""},
      {"{\"a\":[1,2],\"b\":{\"c\":3}}", "null", {1}, 3, 0, ""},
  };
  for (int length : {120, 240, 250, 256, 1000, 65500, 65540}) {
    cases.push_back({"[1,2]", "\"" + std::string(length, 'x') + "\"", {}, 1, 0, ""});
    cases.push_back({"[1,2]", "\"" + std::string(length, 'x') + "\"", {}, 2, 0, ""});
    cases.push_back({"{\"a\":[1,2],\"b\":3}", "\"" + std::string(length, 'x') + "\"", {0}, 2, 0, ""});
  }
  size_t golden_index = 0;
  for (const auto &test : cases) {
    SCOPED_TRACE(test.document.substr(0, 100) + " op=" + std::to_string(test.operation)
        + " index=" + std::to_string(test.index) + " value bytes=" + std::to_string(test.value.size()));
    ObString original;
    ASSERT_EQ(OB_SUCCESS, ObJsonParser::get_binary(&allocator_,
        test.document.data(), test.document.size(), original));
    ObLobInRowCursor initial(original);
    ObLobInRowUpdateCursor native_cursor(&allocator_);
    ASSERT_EQ(OB_SUCCESS, native_cursor.init(&initial));
    ObJsonBinUpdateCtx native_delta(&allocator_);
    const int rounds = test.operation == 3 ? 1 : 3;
    for (int round = 0; round < rounds; ++round) {
      SCOPED_TRACE("round=" + std::to_string(round));
      const std::string value = round == 0 ? test.value
          : "\"" + std::string(round * 300, 'y') + "\"";
      ObString replacement;
      ASSERT_EQ(OB_SUCCESS, ObJsonParser::get_binary(&allocator_, value.data(), value.size(), replacement));
      json::BinarySource source(native_cursor);
      rust::Box<BinaryEdit> edit = json::plan_binary_edit(source,
          rust::Slice<const uint64_t>(test.ancestors.data(), test.ancestors.size()),
          test.operation, test.index,
          rust::Slice<const uint8_t>(reinterpret_cast<const uint8_t *>(test.key.data()), test.key.size()),
          rust::Slice<const uint8_t>(reinterpret_cast<const uint8_t *>(replacement.ptr()), replacement.length()));
      ASSERT_EQ(0, edit->edit_status());
      for (size_t index = 0; index < edit->edit_write_count(); ++index) {
        const auto bytes = edit->edit_write_bytes(index);
        ASSERT_EQ(OB_SUCCESS, native_cursor.set(edit->edit_write_offset(index),
            reinterpret_cast<const char *>(bytes.data()), bytes.size()));
      }
      ObString native_bytes;
      ASSERT_EQ(OB_SUCCESS, native_cursor.get_data(native_bytes));
      ASSERT_EQ(OB_SUCCESS, native_delta.merge_edit(*edit));
      ASSERT_LT(golden_index, sizeof(MUTATION_GOLDENS) / sizeof(MUTATION_GOLDENS[0]));
      const MutationGolden &golden = MUTATION_GOLDENS[golden_index++];
      const auto hash_bytes = [](const char *data, size_t length) {
        uint64_t hash = UINT64_C(14695981039346656037);
        for (size_t index = 0; index < length; ++index) {
          hash = (hash ^ static_cast<unsigned char>(data[index])) * UINT64_C(1099511628211);
        }
        return hash;
      };
      EXPECT_EQ(golden.binary_hash, hash_bytes(native_bytes.ptr(), native_bytes.length()));
      EXPECT_EQ(golden.binary_length, native_bytes.length());
      EXPECT_EQ(golden.rebuilt, native_delta.is_rebuild_all());
      ASSERT_EQ(golden.physical.size(), native_delta.binary_diff_count());
      int64_t range_index = 0;
      for (const auto &range : golden.physical) {
        EXPECT_EQ(range.first, native_delta.binary_diff(range_index).offset);
        EXPECT_EQ(range.second, native_delta.binary_diff(range_index).length);
        ++range_index;
      }
      std::vector<char> native_wire(native_delta.logical_serialize_size());
      int64_t native_pos = 0;
      ASSERT_EQ(OB_SUCCESS, native_delta.serialize_logical(native_wire.data(), native_wire.size(), native_pos));
      EXPECT_EQ(golden.logical_hash, hash_bytes(native_wire.data(), native_wire.size()));
    }
  }
  EXPECT_EQ(sizeof(MUTATION_GOLDENS) / sizeof(MUTATION_GOLDENS[0]), golden_index);
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

TEST_F(TestJsonParser, NativeSchemaRegexCacheAndCodec)
{
  auto slice = [](const std::string &text) {
    return rust::Slice<const uint8_t>(reinterpret_cast<const uint8_t *>(text.data()), text.size());
  };
  auto cache = json::new_schema_cache();
  struct Case { const char *schema; const char *document; bool valid; };
  const Case cases[] = {
      {"{\"pattern\": \"a(?=b)\"}", "\"ab\"", true},
      {"{\"pattern\": \"(a)\\\\1\"}", "\"aa\"", true},
      {"{\"pattern\": \"b\"}", "\"a\\u0000b\"", false},
      {"{\"pattern\": \"a\\u0000b\"}", "\"a\\u0000b\"", false},
      {"{\"pattern\": \"[\"}", "\"anything\"", true},
      {"{\"patternProperties\": {\"b\": {}}, \"additionalProperties\": false}", "{\"b\":1}", true},
      {"{\"properties\": {\"x\": {\"type\": \"integer\"}}}", "{\"x\":1.0}", false},
      {"{\"enum\": [{\"x\": 1}, [2]]}", "{\"x\":1.0}", true},
  };
  for (const auto &item : cases) {
    const std::string schema_text(item.schema), document_text(item.document);
    ASSERT_EQ(json::Status::Ok, cache->prepare(1, slice(schema_text), false, false).status);
    auto input = json::parse_binary(slice(document_text), json::Duplicate::Preserve, 101, false);
    ASSERT_EQ(json::Status::Ok, input.status);
    const rust::Slice<const uint8_t> document(input.binary.data(), input.binary.size());
    auto result = cache->validate_schema(1, document, true);
    ASSERT_EQ(json::Status::Ok, result.status);
    EXPECT_EQ(item.valid, result.valid) << schema_text << " " << document_text;
    ObJsonBin report(reinterpret_cast<const char *>(result.report.data()), result.report.size(), &allocator_);
    ASSERT_EQ(OB_SUCCESS, report.reset_iter());
    ObIJsonBase *valid = nullptr;
    ASSERT_EQ(OB_SUCCESS, report.get_object_value(ObString::make_string("valid"), valid));
    EXPECT_EQ(item.valid, valid->get_boolean());

    auto source = json::parse_binary(slice(schema_text), json::Duplicate::Overwrite, 101, true);
    ASSERT_EQ(json::Status::Ok, source.status);
    auto normalized = json::compile_schema({source.binary.data(), source.binary.size()}, true, 101);
    ASSERT_EQ(json::Status::Ok, normalized.status);
    ASSERT_EQ(json::Status::Ok, cache->prepare(2, {normalized.binary.data(), normalized.binary.size()}, true, true).status);
    auto decoded = cache->validate_schema(2, document, true);
    ASSERT_EQ(json::Status::Ok, decoded.status);
    EXPECT_EQ(result.valid, decoded.valid);
    EXPECT_EQ(std::string(reinterpret_cast<const char *>(result.report.data()), result.report.size()),
              std::string(reinterpret_cast<const char *>(decoded.report.data()), decoded.report.size()));
  }
  const std::string invalid_schema = R"({"allOf":[false]})";
  EXPECT_EQ(json::Status::InvalidSchema, cache->prepare(1, slice(invalid_schema), false, false).status);
}

TEST_F(TestJsonParser, NativeHashPreservesCppGoldens)
{
  const auto inputs = json_hash_inputs();
  ASSERT_EQ(59, inputs.size());
  size_t golden_index = 0;
  const hash_algo algorithms[] = {nullptr, ObMurmurHash::hash, ObXxHash::hash, ObWyHash::hash, json_hash_fixture_fnv};
  const uint64_t seeds[] = {0, 1, UINT64_C(0x123456789abcdef), UINT64_MAX};
  for (size_t index = 0; index < inputs.size(); ++index) {
    const auto &input = inputs[index];
    ObJsonBin value(reinterpret_cast<const char *>(input.binary.data()), input.binary.size(), &allocator_);
    ASSERT_EQ(OB_SUCCESS, value.reset_iter()) << input.name;
    for (size_t algorithm = 0; algorithm < 5; ++algorithm) {
      for (size_t seed = 0; seed < 4; ++seed) {
        uint64_t result = UINT64_C(0x1122334455667788);
        const int status = value.calc_json_hash_value(seeds[seed], algorithms[algorithm], result);
        ASSERT_LT(golden_index, sizeof(JSON_HASH_GOLDENS) / sizeof(JSON_HASH_GOLDENS[0]));
        const auto &golden = JSON_HASH_GOLDENS[golden_index++];
        EXPECT_EQ(golden.status, status) << input.name << " algorithm " << algorithm << " seed " << seed;
        EXPECT_EQ(golden.value, result) << input.name << " algorithm " << algorithm << " seed " << seed;
      }
    }
  }
  EXPECT_EQ(sizeof(JSON_HASH_GOLDENS) / sizeof(JSON_HASH_GOLDENS[0]), golden_index);
}

TEST_F(TestJsonParser, NativeHashHandlesDeepBinaryWithoutRecursiveTraversal)
{
  for (const bool object : {false, true}) {
    auto writer = json::new_binary_writer();
    std::vector<uint64_t> containers;
    for (int64_t i = 0; i < 12000; ++i) {
      const auto container = writer->writer_begin(object, 1, 2);
      ASSERT_EQ(0, container.status);
      containers.push_back(container.handle);
      if (object) { ASSERT_EQ(0, writer->writer_key({reinterpret_cast<const uint8_t *>("a"), 1})); }
    }
    ASSERT_EQ(0, writer->writer_string({reinterpret_cast<const uint8_t *>("leaf"), 4}));
    for (auto it = containers.rbegin(); it != containers.rend(); ++it) { ASSERT_EQ(0, writer->writer_end(*it)); }
    auto output = json::writer_finish(std::move(writer));
    ASSERT_EQ(0, output.status);
    const json::HashCapability capability(json_hash_fixture_fnv);
    const auto direct = json::hash_binary({output.binary.data(), output.binary.size()}, 123, capability);
    ASSERT_EQ(json::Status::Ok, direct.status);
    ASSERT_EQ(0, direct.platform_error);
    ObJsonBin value(reinterpret_cast<const char *>(output.binary.data()), output.binary.size(), &allocator_);
    ASSERT_EQ(OB_SUCCESS, value.reset_iter());
    uint64_t adapted = 0;
    ASSERT_EQ(OB_SUCCESS, value.calc_json_hash_value(123, json_hash_fixture_fnv, adapted));
    EXPECT_EQ(direct.value, adapted);
  }
}

} // namespace common
} // namespace oceanbase

int main(int argc, char **argv)
{
  ::testing::InitGoogleTest(&argc, argv);
  return RUN_ALL_TESTS();
}
