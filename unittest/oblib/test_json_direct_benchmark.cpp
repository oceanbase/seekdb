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
  // Captured from the retired C++ tree serializer before deletion. The capture
  // also compared every byte with the native direct parser (68 cases, zero differences).
  struct Golden { int status; size_t length; uint64_t hash; };
  const Golden goldens[] = {
    {0, 2, UINT64_C(0x08328807b4eb6fed)}, // 0
    {0, 2, UINT64_C(0x084db707b5028782)}, // 1
    {0, 2, UINT64_C(0x084db807b5028935)}, // 2
    {0, 12, UINT64_C(0x0618bdd70a573c95)}, // 3
    {0, 12, UINT64_C(0x116091cc40222a84)}, // 4
    {0, 58, UINT64_C(0x8ec78c7abb63cec7)}, // 5
    {0, 119, UINT64_C(0xd073ed1caee7e960)}, // 6
    {0, 19, UINT64_C(0xa505bee6e447e29b)}, // 7
    {0, 2, UINT64_C(0x08218a07b4dd0020)}, // 8
    {0, 6, UINT64_C(0xfa3efd2f98ebc51a)}, // 9
    {0, 6, UINT64_C(0xb62437c5ca6c8c46)}, // 10
    {0, 10, UINT64_C(0x74f0af7d46949e0f)}, // 11
    {0, 11, UINT64_C(0xe50d54fe7444f97a)}, // 12
    {0, 11, UINT64_C(0x4b4b3fdeeffa264a)}, // 13
    {0, 9, UINT64_C(0x9a444dc3d3c3ad76)}, // 14
    {0, 2, UINT64_C(0x08395407b4f1363f)}, // 15
    {0, 9, UINT64_C(0x985bacc3d2252af3)}, // 16
    {0, 9, UINT64_C(0x1fa894435e5641fb)}, // 17
    {0, 9, UINT64_C(0x796065bac7350752)}, // 18
    {0, 64, UINT64_C(0x5ed35d1c6c312f36)}, // 19
    {0, 36, UINT64_C(0xcff01d1e7460deea)}, // 20
    {0, 49, UINT64_C(0x4bcc2b3597b00260)}, // 21
    {0, 35, UINT64_C(0xa2372a75b751a4fd)}, // 22
    {-5453, 0, UINT64_C(0xcbf29ce484222325)}, // 23
    {0, 724, UINT64_C(0x3317a83016fd7347)}, // 24
    {0, 732, UINT64_C(0x5e90d7506b04d1db)}, // 25
    {0, 37, UINT64_C(0x82ed31060b6039ca)}, // 26
    {-6284, 0, UINT64_C(0xcbf29ce484222325)}, // 27
    {-5411, 0, UINT64_C(0xcbf29ce484222325)}, // 28
    {-5411, 0, UINT64_C(0xcbf29ce484222325)}, // 29
    {-5411, 0, UINT64_C(0xcbf29ce484222325)}, // 30
    {-5411, 0, UINT64_C(0xcbf29ce484222325)}, // 31
    {-5411, 0, UINT64_C(0xcbf29ce484222325)}, // 32
    {-5411, 0, UINT64_C(0xcbf29ce484222325)}, // 33
    {-5411, 0, UINT64_C(0xcbf29ce484222325)}, // 34
    {0, 14, UINT64_C(0xf0f7206f9692423c)}, // 35
    {0, 20, UINT64_C(0x5873fa23177cffa8)}, // 36
    {0, 32, UINT64_C(0x9ebc98ae0a47c65b)}, // 37
    {0, 92, UINT64_C(0x2813f6b4582de194)}, // 38
    {0, 313, UINT64_C(0x7e9de440e132d9d5)}, // 39
    {0, 1203, UINT64_C(0x6c916bf4465796ca)}, // 40
    {0, 778, UINT64_C(0xf5ee7ae3812a1136)}, // 41
    {0, 3218, UINT64_C(0xd1a649e07498f025)}, // 42
    {0, 782, UINT64_C(0xb3e8039d37bb84f6)}, // 43
    {0, 3232, UINT64_C(0x955d918b47a9ba15)}, // 44
    {0, 914, UINT64_C(0x90a85791ce8a5947)}, // 45
    {0, 3804, UINT64_C(0x0e76220a52a2d1eb)}, // 46
    {0, 15014, UINT64_C(0x5254e0ca2163b82b)}, // 47
    {0, 98906, UINT64_C(0x82dd8617631a2057)}, // 48
    {0, 3, UINT64_C(0xae00de185389c2e0)}, // 49
    {0, 17, UINT64_C(0x678941352c7e6967)}, // 50
    {0, 129, UINT64_C(0xfc47dda4972d8eec)}, // 51
    {0, 143, UINT64_C(0x6e7075c3af619bea)}, // 52
    {0, 131, UINT64_C(0x6eb6b289400aa713)}, // 53
    {0, 144, UINT64_C(0xe9c65eb65d8895be)}, // 54
    {0, 258, UINT64_C(0xde57ed26bef3d05f)}, // 55
    {0, 275, UINT64_C(0x8d603f79dc6a7233)}, // 56
    {0, 259, UINT64_C(0x1e5d874591e61546)}, // 57
    {0, 276, UINT64_C(0x653a457b145825ea)}, // 58
    {0, 16386, UINT64_C(0xbe327ec26914ab21)}, // 59
    {0, 16403, UINT64_C(0x1a7ba7d7b2f61991)}, // 60
    {0, 16388, UINT64_C(0x9bcd7de2dbbc67d3)}, // 61
    {0, 16404, UINT64_C(0x3a201568b8b544ca)}, // 62
    {0, 350018, UINT64_C(0x08f055a96b74abd8)}, // 63
    {0, 182, UINT64_C(0x7db531723b09b205)}, // 64
    {-5453, 0, UINT64_C(0xcbf29ce484222325)}, // 65
    {0, 182, UINT64_C(0x14520a450acc8d61)}, // 66
    {0, 190, UINT64_C(0x831678d51863a951)}, // 67
  };
  if (cases.size() != sizeof(goldens) / sizeof(goldens[0])) { return 1; }
  int failures = 0;
  for (size_t index = 0; index < cases.size(); ++index) {
    const Case &item = cases[index];
    std::string bytes;
    const int ret = direct_binary(item.text, item.flags, bytes);
    uint64_t hash = UINT64_C(14695981039346656037);
    for (unsigned char byte : bytes) {
      hash ^= byte;
      hash *= UINT64_C(1099511628211);
    }
    const Golden &golden = goldens[index];
    if (ret != golden.status || bytes.size() != golden.length || hash != golden.hash) {
      ++failures;
      std::printf("FAIL %s index=%zu ret=%d size=%zu hash=%016lx\n",
          item.name, index, ret, bytes.size(), static_cast<unsigned long>(hash));
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
    if (mode == "validate") {
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
  std::fprintf(stderr, "usage: %s [differential] | benchmark direct|validate SHAPE BYTES ITERATIONS\n", argv[0]);
  return 2;
}
