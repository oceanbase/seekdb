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

#include "common/json_type/ob_json_bin.h"
#include "common/json_type/ob_json_parse.h"
#include "lib/allocator/page_arena.h"
#include <chrono>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <string>
#include <vector>

namespace oceanbase
{
namespace common
{

static int old_binary(const std::string &text, uint32_t flags, std::string &bytes)
{
  ObArenaAllocator allocator;
  ObJsonNode *node = nullptr;
  int ret = ObJsonParser::get_tree(&allocator, text.data(), text.size(), node, flags);
  ObString binary;
  if (OB_SUCCESS == ret) {
    ObJsonBinSerializer serializer(&allocator);
    ret = serializer.serialize(node, binary);
  }
  if (OB_SUCCESS == ret) {
    bytes.assign(binary.ptr(), binary.length());
  }
  return ret;
}

static int direct_binary(const std::string &text, uint32_t flags, std::string &bytes)
{
  ObArenaAllocator allocator;
  ObString binary;
  int ret = ObJsonParser::get_binary(&allocator, text.data(), text.size(), binary, flags);
  if (OB_SUCCESS == ret) {
    bytes.assign(binary.ptr(), binary.length());
  }
  return ret;
}

static int differential()
{
  struct Case {
    const char *name;
    std::string text;
    uint32_t flags;
  };
  std::vector<Case> cases = {
      {"null", "null", 0}, {"true", "true", 0}, {"false", "false", 0},
      {"empty_array", "[]", 0}, {"empty_object", "{}", 0},
      {"mixed", "{\"a\":[1,-2,3.5e-2,true,false,null],\"b\":{\"c\":\"d\"}}", 0},
      {"typical", "{\"id\":123456,\"name\":\"alice\",\"active\":true,\"score\":-12.5,\"tags\":[\"a\",\"b\"],\"profile\":{\"city\":\"HZ\",\"age\":30}}", 0},
      {"unicode", "\"\\u4f60\\u597d\\ud83d\\ude00雪😀\"", 0},
      {"empty_string", "\"\"", 0}, {"int32max", "4294967295", 0},
      {"int32plus", "4294967296", 0}, {"int64max", "9223372036854775807", 0},
      {"int64min", "-9223372036854775808", 0}, {"uint64max", "18446744073709551615", 0},
      {"uint64plus", "18446744073709551616", 0}, {"negativezero", "-0", 0},
      {"negativezerofloat", "-0.0", 0}, {"floatmax", "1.7976931348623157e308", 0},
      {"subnormal", "5e-324", 0},
      {"inline_edges", "[-129,-128,-1,0,127,128,255,256,32767,32768,65535,65536]", 0},
      {"duplicate_overwrite", "{\"a\":[1,2],\"b\":0,\"a\":{\"x\":3},\"c\":4}", 0},
      {"duplicate_preserve", "{\"a\":[1,2],\"b\":0,\"a\":{\"x\":3},\"c\":4}", 16},
      {"duplicate_schema", "{\"a\":[1,2],\"b\":0,\"a\":{\"x\":3},\"c\":4}", 8},
      {"duplicate_reject", "{\"a\":1,\"a\":2}", 4},
      {"depth100", std::string(100, '[') + "0" + std::string(100, ']'), 0},
      {"depth101", std::string(101, '[') + "0" + std::string(101, ']'), 0},
      {"internal_ref", "{\"$ref\":\"#/definitions/a\"}", 8},
      {"external_ref", "{\"$ref\":\"https://example.com/a\"}", 8},
      {"unquoted", "{a:1}", 0}, {"comment", "/* comment */null", 0},
      {"trailing", "[1,]", 0}, {"uppercase", "TRUE", 0}, {"hex", "0x10", 0},
      {"malformed_utf8", std::string("\"\xff\"", 3), 0}, {"trailing_document", "true false", 0},
  };
  for (int count : {1, 10, 100, 255, 256, 300, 5000}) {
    std::string array = "[";
    std::string object = "{";
    for (int index = 0; index < count; ++index) {
      if (index != 0) {
        array += ',';
        object += ',';
      }
      array += std::to_string(index);
      object += "\"key" + std::to_string(index) + "\":" + std::to_string(index);
    }
    cases.push_back({"array_boundary", array + "]", 0});
    cases.push_back({"object_boundary", object + "}", 0});
  }
  for (int length : {1, 127, 128, 255, 256, 16383, 16384}) {
    cases.push_back({"string_varint", "\"" + std::string(length, 's') + "\"", 0});
    cases.push_back({"key_varint", "{\"" + std::string(length, 'k') + "\":0}", 0});
  }
  std::string wide_array = "[";
  for (int index = 0; index < 70000; ++index) {
    if (index != 0) {
      wide_array += ',';
    }
    wide_array += '0';
  }
  cases.push_back({"wide_array_offsets", wide_array + "]", 0});
  std::string large_duplicate = "{\"key0\":0";
  for (int index = 1; index < 20; ++index) {
    large_duplicate += ",\"key" + std::to_string(index) + "\":" + std::to_string(index);
  }
  large_duplicate += ",\"key0\":20}";
  for (uint32_t flags : {0U, 4U, 8U, 16U}) {
    cases.push_back({"large_duplicate", large_duplicate, flags});
  }
  int failures = 0;
  for (const Case &item : cases) {
    std::string old_bytes;
    std::string direct_bytes;
    int old_ret = old_binary(item.text, item.flags, old_bytes);
    int direct_ret = direct_binary(item.text, item.flags, direct_bytes);
    if (old_ret != direct_ret || old_bytes != direct_bytes) {
      ++failures;
      std::printf("FAIL %s old_ret=%d direct_ret=%d old_size=%zu direct_size=%zu\n",
                  item.name, old_ret, direct_ret, old_bytes.size(), direct_bytes.size());
      size_t common = std::min(old_bytes.size(), direct_bytes.size());
      size_t offset = 0;
      while (offset < common && old_bytes[offset] == direct_bytes[offset]) {
        ++offset;
      }
      std::printf("first_difference=%zu\n", offset);
    }
  }
  std::printf("differential cases=%zu failures=%d\n", cases.size(), failures);
  return failures == 0 ? 0 : 1;
}

static std::string make_document(const std::string &shape, size_t target)
{
  if (shape == "small") {
    return "{\"id\":123456789,\"name\":\"serde_json\",\"ok\":true,\"items\":[1,2,3]}";
  }
  if (shape == "typical") {
    return "{\"id\":123456,\"name\":\"alice\",\"active\":true,\"score\":-12.5,"
           "\"tags\":[\"a\",\"b\"],\"profile\":{\"city\":\"HZ\",\"age\":30}}";
  }
  std::string text = "[";
  size_t index = 0;
  while (text.size() < target + 16) {
    if (index != 0) {
      text += ',';
    }
    if (shape == "numbers") {
      text += std::to_string(index % 1000000);
      text += index % 3 == 0 ? ".25" : "";
    } else if (shape == "strings") {
      text += index % 2 == 0 ? "\"雪😀hello\\nworld\"" : "\"abcdefghijklmnop\\u4f60\"";
    } else {
      text += "{\"id\":" + std::to_string(index) +
              ",\"name\":\"雪😀abcdefghijklmnop\",\"active\":true,\"value\":-0.25}";
    }
    ++index;
  }
  return text + "]";
}

static uint64_t hash_bytes(const std::string &bytes)
{
  uint64_t hash = UINT64_C(1469598103934665603);
  for (uint8_t byte : bytes) {
    hash ^= byte;
    hash *= UINT64_C(1099511628211);
  }
  return hash;
}

static int benchmark(const std::string &mode, const std::string &shape, size_t target, int iterations)
{
  std::string text = make_document(shape, target);
  uint64_t checksum = 0;
  uint64_t errors = 0;
  int64_t elapsed = 0;
  for (int iteration = -2; iteration < iterations; ++iteration) {
    ObArenaAllocator allocator;
    ObString binary;
    auto start = std::chrono::steady_clock::now();
    int ret = OB_SUCCESS;
    if (mode == "old") {
      ObJsonNode *node = nullptr;
      ret = ObJsonParser::get_tree(&allocator, text.data(), text.size(), node);
      if (OB_SUCCESS == ret) {
        ObJsonBinSerializer serializer(&allocator);
        ret = serializer.serialize(node, binary);
      }
    } else if (mode == "validate") {
      ret = ObJsonParser::check_json_syntax(ObString(text.size(), text.data()), &allocator);
    } else {
      ret = ObJsonParser::get_binary(&allocator, text.data(), text.size(), binary);
    }
    auto end = std::chrono::steady_clock::now();
    if (OB_SUCCESS != ret) {
      ++errors;
    } else {
      checksum ^= hash_bytes(std::string(binary.ptr(), binary.length()));
    }
    if (iteration >= 0) {
      elapsed += std::chrono::duration_cast<std::chrono::nanoseconds>(end - start).count();
    }
  }
  std::printf("mode=%s shape=%s bytes=%zu iterations=%d ns_per_doc=%.0f errors=%lu checksum=%lu\n",
              mode.c_str(), shape.c_str(), text.size(), iterations,
              static_cast<double>(elapsed) / iterations, static_cast<unsigned long>(errors),
              static_cast<unsigned long>(checksum));
  return errors == 0 ? 0 : 1;
}

} // namespace common
} // namespace oceanbase

int main(int argc, char **argv)
{
  if (argc == 1 || (argc == 2 && std::strcmp(argv[1], "differential") == 0)) {
    return oceanbase::common::differential();
  }
  if (argc == 6 && std::strcmp(argv[1], "benchmark") == 0) {
    return oceanbase::common::benchmark(argv[2], argv[3], std::strtoull(argv[4], nullptr, 10),
                                       std::atoi(argv[5]));
  }
  std::fprintf(stderr, "usage: %s [differential] | benchmark old|direct|validate SHAPE BYTES ITERATIONS\n", argv[0]);
  return 2;
}
