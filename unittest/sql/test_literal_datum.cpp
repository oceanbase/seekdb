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

#include "common/datum/datum_parameter_codec.h"
#include "sql/engine/expr/ob_expr.h"
#include "sql/resolver/ob_resolver_utils.h"
#include <gtest/gtest.h>
#include <string>
#include <fstream>
#include <map>
#include <sstream>
#include <vector>
using namespace oceanbase::common;
using namespace oceanbase::sql;
namespace {
struct LiteralCase {
  ObItemType type;
  std::string text;
  int64_t integer = 0;
  std::string charset;
  int charset_children = 1;
};
struct Context {
  bool decint = true, mysql_dates = false, from_pl = false, formalize = false, parameterize = false;
  oceanbase::share::ObCompatType compatibility = oceanbase::share::COMPAT_MYSQL57;
  oceanbase::sql::stmt::StmtType statement = oceanbase::sql::stmt::T_SELECT;
};
template <typename Value>
int resolve(LiteralCase &input, ObIAllocator &arena, Value &value, ObString &prefix,
             const Context &context = Context()) {
  ParseNode node = {}, charset = {};
  ParseNode *children[] = {&charset, nullptr};
  node.type_ = input.type;
  node.value_ = input.integer;
  node.str_value_ = input.text.data();
  node.str_len_ = input.text.size();
  if (!input.charset.empty()) {
    charset.type_ = T_CHARSET;
    charset.str_value_ = input.charset.data();
    charset.str_len_ = input.charset.size();
    node.num_child_ = input.charset_children;
    node.children_ = children;
  }
  ObTimeZoneInfo timezone;
  timezone.set_offset(0);
  return ObResolverUtils::resolve_const(&node, context.statement, arena,
      CS_TYPE_UTF8MB4_GENERAL_CI, CS_TYPE_UTF8MB4_GENERAL_CI, &timezone, value,
      context.parameterize, prefix, LS_CHAR, CS_TYPE_UTF8MB4_GENERAL_CI, nullptr,
      0, context.decint, context.compatibility, context.mysql_dates, 8,
      context.from_pl, context.formalize);
}
std::string wire(const DatumParameter &value) {
  int64_t size = 0, pos = 0;
  EXPECT_EQ(OB_SUCCESS, DatumParameterCodec::parameter_size(value, size));
  if (size <= 0) { return {}; }
  std::string result(size, '\0');
  EXPECT_EQ(OB_SUCCESS, DatumParameterCodec::encode_parameter(value, &result[0], size, pos));
  EXPECT_EQ(size, pos);
  return result;
}
class AllocationGate : public ObIAllocator {
public:
  ObArenaAllocator arena;
  // Simulate persistent exhaustion; a parser may recover from one failed
  // decimal attempt by falling back to NUMBER and allocating again.
  int calls = 0, fail_at = 0;
  void *alloc(int64_t size) override {
    ++calls;
    return fail_at > 0 && calls >= fail_at ? nullptr : arena.alloc(size);
  }
  void *alloc(int64_t size, const ObMemAttr &) override { return alloc(size); }
  void free(void *) override {}
};
DatumParameter sentinel() {
  static const int64_t number = 31415;
  DatumParameter p;
  SqlTypeMeta meta; meta.set_int();
  p.set_type_meta(meta);
  p.datum_.set_string(reinterpret_cast<const char *>(&number), sizeof(number));
  p.set_accuracy(ObAccuracy(37, 7));
  p.set_result_flags(0x1234);
  p.set_raw_text_info(12, 33);
  p.set_need_to_check_type(false);
  return p;
}
} // namespace

TEST(LiteralDatum, all_literal_branches_preserve_values_metadata_and_flags) {
  std::vector<LiteralCase> cases = {
    {T_INT, "0", 0}, {T_INT, "1", 1}, {T_INT, "-9223372036854775808", INT64_MIN},
    {T_INT, "9223372036854775807", INT64_MAX}, {T_UINT64, "18446744073709551615", -1},
    {T_FLOAT, "3.14159265"}, {T_FLOAT, "-0.0"}, {T_DOUBLE, "1.2345678901234567"},
    {T_DOUBLE, "1e200"}, {T_DOUBLE, "1e999"}, {T_IEEE754_NAN, "NaN"},
    {T_IEEE754_INFINITE, "Inf"}, {T_NUMBER, "0"}, {T_NUMBER, "-0.000"},
    {T_NUMBER, "12345678901234567890.1250000000"}, {T_NUMBER, "+.12e-3"},
    {T_NUMBER, "1e-99"}, {T_NUMBER, std::string(81, '9')}, {T_NUMBER, std::string(180, '9')},
    {T_BOOL, "true", 1}, {T_BOOL, "false", 0}, {T_QUESTIONMARK, "?", 37},
    {T_NULL, "NULL"}, {T_NULL, "\\N", 1}, {T_HEX_STRING, std::string("\0\xff\0", 3)},
    {T_CHAR, "客户é"}, {T_NCHAR, "客户é"}, {T_VARCHAR, ""}, {T_VARCHAR, std::string(12000, 'x')},
    {T_VARCHAR, "客户é", 0, "utf8mb4"}, {T_VARCHAR, "a", 0, "utf16"},
    {T_VARCHAR, std::string("\xff", 1), 0, "binary"},
    {T_VARCHAR, std::string("\xff", 1), 0, "utf8mb4"},
    {T_VARCHAR, std::string("\xff", 1), 0, "utf8mb4", 2},
    {T_VARCHAR, "a", 0, "not_a_charset"}, {T_YEAR, "1970"}, {T_DATE, "2024-02-29"},
    {T_DATE, "0000-00-00"}, {T_DATE, "2024-02-30"}, {T_TIME, "-12:34:56.123456"},
    {T_DATETIME, "2024-02-29"}, {T_TIMESTAMP, "2024-02-29 12:34:56.123456"},
    {T_TIMESTAMP_TZ, "2024-02-29 12:34:56.123456789 +08:00"}
  };
  auto unhex = [](const std::string &data) {
    std::string result;
    for (size_t i = 0; i < data.size(); i += 2) {
      result += static_cast<char>(std::stoul(data.substr(i, 2), nullptr, 16));
    }
    return result;
  };
  auto bytes = [](const auto &value) {
    return std::string(reinterpret_cast<const char *>(&value), sizeof(value));
  };
  std::ifstream fixture("unittest/sql/data/literal_values.golden");
  ASSERT_TRUE(fixture.good());
  std::map<int, std::string> frames;
  std::vector<std::vector<std::string>> records;
  std::string line;
  while (std::getline(fixture, line)) {
    if (line.empty() || line[0] == '#') { continue; }
    std::vector<std::string> fields;
    std::istringstream row(line);
    std::string field;
    while (std::getline(row, field, '\t')) { fields.push_back(field); }
    ASSERT_GE(fields.size(), 3);
    if (fields[0] == "V") {
      ASSERT_TRUE(frames.emplace(std::stoi(fields[1]), unhex(fields[2])).second);
    } else {
      ASSERT_EQ("R", fields[0]);
      records.push_back(fields);
    }
  }
  ASSERT_EQ(48, frames.size());
  ASSERT_EQ(1376, records.size());
  int compared = 0, successes = 0, errors = 0;
  for (int mode = 0; mode < 16; ++mode) {
    Context ctx;
    ctx.decint = mode & 1; ctx.mysql_dates = mode & 2; ctx.parameterize = mode & 2;
    ctx.from_pl = mode & 4; ctx.formalize = mode & 8;
    ctx.compatibility = mode & 4 ? oceanbase::share::COMPAT_MYSQL8 : oceanbase::share::COMPAT_MYSQL57;
    for (auto statement : {oceanbase::sql::stmt::T_SELECT, oceanbase::sql::stmt::T_CREATE_TABLE}) {
      ctx.statement = statement;
      for (auto &input : cases) {
        SCOPED_TRACE(::testing::Message() << mode << "/" << statement << "/" << input.type << "/" << input.text);
        const auto &expected = records.at(compared);
        const std::string key = std::to_string(mode) + '/' + std::to_string(statement)
            + '/' + std::to_string(&input - cases.data());
        ASSERT_EQ(key, expected[1]);
        const int old_ret = std::stoi(expected[2]);
        ObArenaAllocator native_arena;
        DatumParameter native = sentinel();
        const std::string unchanged = wire(native);
        ObString prefix;
        const int new_ret = resolve(input, native_arena, native, prefix, ctx);
        ASSERT_EQ(old_ret, new_ret);
        EXPECT_EQ(unhex(expected[3]), std::string(prefix.ptr() ? prefix.ptr() : "", prefix.length()));
        ++compared;
        if (OB_SUCCESS != old_ret) {
          ++errors;
          EXPECT_EQ(unchanged, wire(native));
          EXPECT_EQ(12, native.get_raw_text_pos());
          continue;
        }
        ++successes;
        ASSERT_EQ(8, expected.size());
        EXPECT_EQ(frames.at(std::stoi(expected[4])), wire(native));
        EXPECT_EQ(unhex(expected[5]), bytes(native.get_type_meta()));
        EXPECT_EQ(unhex(expected[6]), bytes(native.get_param_meta()));
        EXPECT_EQ(expected[7], std::to_string(native.get_param_flag().flag_) + '/'
            + std::to_string(native.get_param_flag().extend_flag_));
        EXPECT_EQ(-1, native.get_raw_text_pos());
        EXPECT_EQ(-1, native.get_raw_text_len());
      }
    }
  }
  EXPECT_EQ(cases.size() * 32, compared);
  EXPECT_GT(successes, 900);
  EXPECT_GT(errors, 40);
  RecordProperty("comparisons", compared);
  RecordProperty("successful_values", successes);
  RecordProperty("matched_errors", errors);
}

TEST(LiteralDatum, owned_payload_survives_source_and_reused_result) {
  ObArenaAllocator arena;
  std::vector<DatumParameter> values;
  DatumParameter result;
  ObString prefix;
  for (int i = 0; i < 128; ++i) {
    LiteralCase input{T_INT, std::to_string(i), i};
    ASSERT_EQ(OB_SUCCESS, resolve(input, arena, result, prefix));
    values.push_back(result);
  }
  {
    LiteralCase input{T_VARCHAR, std::string(20000, 'q')};
    ASSERT_EQ(OB_SUCCESS, resolve(input, arena, result, prefix));
    values.push_back(result);
    input.text.assign(20000, 'z');
  }
  for (int i = 0; i < 128; ++i) { EXPECT_EQ(i, values[i].datum_.get_int()); }
  EXPECT_EQ(std::string(20000, 'q'), std::string(values.back().datum_.ptr_, values.back().datum_.len_));
  LiteralCase boolean{T_BOOL, "true", 1}, null{T_NULL, "NULL"};
  ASSERT_EQ(OB_SUCCESS, resolve(boolean, arena, result, prefix));
  EXPECT_TRUE(result.get_param_flag().is_boolean_);
  ASSERT_EQ(OB_SUCCESS, resolve(null, arena, result, prefix));
  EXPECT_TRUE(result.is_null());
  EXPECT_FALSE(result.get_param_flag().is_boolean_);
  EXPECT_FALSE(result.has_result_flag(NOT_NULL_FLAG));
}

TEST(LiteralDatum, allocation_failure_does_not_publish_partial_output) {
  std::vector<LiteralCase> inputs = {{T_INT, "7", 7}, {T_VARCHAR, "客户é"},
      {T_NUMBER, "12345678901234567890.1250000000"}, {T_NUMBER, "1e-99"},
      {T_VARCHAR, "abc", 0, "utf8mb4"}};
  int failures = 0;
  for (auto &input : inputs) {
    SCOPED_TRACE(::testing::Message() << input.type << "/" << input.text << "/" << input.charset);
    AllocationGate baseline;
    DatumParameter result;
    ObString prefix;
    ASSERT_EQ(OB_SUCCESS, resolve(input, baseline, result, prefix));
    EXPECT_GT(baseline.calls, 0);
    for (int fail_at = 1; fail_at <= baseline.calls; ++fail_at) {
      SCOPED_TRACE(::testing::Message() << input.type << "/" << input.text << "/" << fail_at);
      AllocationGate gate; gate.fail_at = fail_at;
      result = sentinel();
      const std::string expected = wire(result);
      EXPECT_EQ(OB_ALLOCATE_MEMORY_FAILED, resolve(input, gate, result, prefix));
      EXPECT_EQ(expected, wire(result));
      EXPECT_EQ(12, result.get_raw_text_pos());
      EXPECT_EQ(33, result.get_raw_text_len());
      EXPECT_FALSE(result.need_to_check_type());
      ++failures;
    }
  }
  EXPECT_GE(failures, 7);
  RecordProperty("allocation_failures", failures);
}
