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

#pragma once

#include "common/object/ob_object.h"
#include "common/json_type/json_builder.h"
#include "rust/json.h"
#include <string>
#include <cstring>
#include <stdexcept>
#include <vector>

namespace oceanbase { namespace common {

struct JsonHashInput {
  std::string name;
  std::vector<uint8_t> binary;
};

inline uint64_t json_hash_fixture_fnv(const void *data, uint64_t length, uint64_t seed)
{
  uint64_t hash = seed ^ UINT64_C(14695981039346656037);
  const uint8_t *bytes = static_cast<const uint8_t *>(data);
  for (uint64_t i = 0; i < length; ++i) { hash = (hash ^ bytes[i]) * UINT64_C(1099511628211); }
  return hash;
}

inline std::vector<JsonHashInput> json_hash_inputs()
{
  std::vector<JsonHashInput> result;
  const char *documents[] = {
      "null", "true", "false", "0", "-0", "1", "-1", "255", "256", "65536",
      "9223372036854775807", "18446744073709551615", "0.0", "-0.0", "0.1", "1.0",
      "1e-200", "1e200", "5e-324", R"("")", R"("abc")", R"("雪😀")", R"("a\u0000b")",
      "[]", "{}", "[null,true,false]", "[1,2,3]", "[[1],[2,[3]]]",
      R"({"a":1,"bb":[true,null,"x"],"ccc":{"a":2}})", R"({"":0,"a":1,"雪":["😀"]})"};
  for (const char *document : documents) {
    const auto parsed = json::parse_binary({reinterpret_cast<const uint8_t *>(document), std::strlen(document)},
        json::Duplicate::Overwrite, 101, false);
    if (parsed.status != json::Status::Ok) { throw std::runtime_error("hash fixture parsing failed"); }
    result.push_back({document, std::vector<uint8_t>(parsed.binary.begin(), parsed.binary.end())});
  }
  const std::string duplicate = R"({"a":1,"a":2,"b":[]})";
  auto preserved = json::parse_binary({reinterpret_cast<const uint8_t *>(duplicate.data()), duplicate.size()},
      json::Duplicate::Preserve, 101, false);
  result.push_back({"duplicate_members", {preserved.binary.begin(), preserved.binary.end()}});
  auto add_scalar = [&](const std::string &name, uint8_t kind, int64_t integer, uint64_t unsigned_value,
                        double real, const std::vector<uint8_t> &payload, uint16_t field_type = 0) {
    const auto encoded = json::encode_scalar(kind, integer, unsigned_value, real,
        {payload.data(), payload.size()}, field_type);
    if (encoded.status != 0) { throw std::runtime_error("hash fixture scalar encoding failed"); }
    result.push_back({name, {encoded.binary.begin(), encoded.binary.end()}});
  };
  auto payload64 = [](int64_t value) {
    const uint8_t *bytes = reinterpret_cast<const uint8_t *>(&value);
    return std::vector<uint8_t>(bytes, bytes + sizeof(value));
  };
  auto payload32 = [](int32_t value) {
    const uint8_t *bytes = reinterpret_cast<const uint8_t *>(&value);
    return std::vector<uint8_t>(bytes, bytes + sizeof(value));
  };
  for (uint8_t kind : {9, 24, 32}) { add_scalar("date_tag_" + std::to_string(kind), kind, 0, 0, 0, payload32(0)); }
  for (uint8_t kind : {10, 11, 12, 25, 26, 27, 33}) {
    add_scalar("time_tag_" + std::to_string(kind), kind, 0, 0, 0, payload64(0));
  }
  add_scalar("time_fraction_a", 10, 0, 0, 0, payload64(123456000001));
  add_scalar("time_fraction_b", 10, 0, 0, 0, payload64(123456000002));
  for (uint8_t kind : {20, 21, 22, 23, 28, 29}) {
    add_scalar("string_tag_" + std::to_string(kind), kind, 0, 0, 0, {3, 'a', 0, 'b'});
  }
  add_scalar("oracle_int", 18, 0, 0, 0, {42});
  add_scalar("oracle_uint", 19, 0, 0, 0, {42});
  const float float_value = 0.1F;
  const uint8_t *float_bytes = reinterpret_cast<const uint8_t *>(&float_value);
  add_scalar("oracle_float", 15, 0, 0, 0, {float_bytes, float_bytes + sizeof(float_value)});
  const double double_value = 0.1;
  const uint8_t *double_bytes = reinterpret_cast<const uint8_t *>(&double_value);
  add_scalar("oracle_double", 16, 0, 0, 0, {double_bytes, double_bytes + sizeof(double_value)});
  add_scalar("opaque", 13, 0, 0, 0, {'a', 0, 'b'}, 27);
  add_scalar("opaque_other_type", 13, 0, 0, 0, {'a', 0, 'b'}, 26);
  for (uint8_t kind : {1, 17}) {
    // Precision -1, scale 4, one positive base-1e9 digit 100000000 with exponent -1.
    add_scalar("decimal_tag_" + std::to_string(kind), kind, 0, 0, 0,
        {255, 255, 0, 4, 191, 0, 0, 1, 0, 225, 245, 5});
  }
  JsonContainerBuilder array(false), object(true);
  const size_t scalar_count = result.size();
  for (size_t i = 31; i < scalar_count; ++i) {
    const ObString binary(result[i].binary.size(), reinterpret_cast<const char *>(result[i].binary.data()));
    if (array.append(ObString(), binary) != OB_SUCCESS
        || object.append(ObString(result[i].name.size(), result[i].name.data()), binary) != OB_SUCCESS) {
      throw std::runtime_error("hash fixture construction failed");
    }
  }
  ObString binary;
  if (array.finish(binary) != OB_SUCCESS) { throw std::runtime_error("hash array fixture failed"); }
  result.push_back({"database_array", {reinterpret_cast<const uint8_t *>(binary.ptr()), reinterpret_cast<const uint8_t *>(binary.ptr()) + binary.length()}});
  if (object.finish(binary) != OB_SUCCESS) { throw std::runtime_error("hash object fixture failed"); }
  result.push_back({"database_object", {reinterpret_cast<const uint8_t *>(binary.ptr()), reinterpret_cast<const uint8_t *>(binary.ptr()) + binary.length()}});
  return result;
}

}} // namespace oceanbase::common
