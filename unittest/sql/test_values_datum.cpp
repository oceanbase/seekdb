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
#include "share/datum/datum_ndv_hash.h"
#include "sql/engine/ob_exec_context.h"
#include "sql/engine/ob_physical_plan_ctx.h"
#include "sql/rewrite/ob_query_range_define.h"
#include "sql/rewrite/ob_range_generator.h"
#include "sql/engine/basic/ob_values_table_access_op.h"
#include "sql/resolver/expr/ob_raw_expr_copier.h"
#include <fstream>
#include <gtest/gtest.h>
#include <sstream>
#include <string>
#include <type_traits>
#include <vector>
using namespace oceanbase::common;
using namespace oceanbase::sql;
namespace {
struct RangeFrame {
  std::string name, wire, canonical;
};
std::string unhex(const std::string &s) {
  std::string out;
  for (size_t i = 0; i < s.size(); i += 2) {
    out += char(std::stoul(s.substr(i, 2), nullptr, 16));
  }
  return out;
}
std::vector<RangeFrame> frames() {
  std::ifstream file("unittest/sql/data/range_map.golden");
  EXPECT_TRUE(file.good());
  std::vector<RangeFrame> out;
  std::string line;
  while (std::getline(file, line)) {
    if (line.empty() || line[0] == '#') {
      continue;
    }
    std::istringstream row(line);
    std::string name, wire, canonical;
    std::getline(row, name, '\t');
    std::getline(row, wire, '\t');
    std::getline(row, canonical);
    out.push_back({name, unhex(wire), unhex(canonical)});
  }
  return out;
}
std::string bare_value(const std::string &wire) {
  int64_t p = 0, v = 0, n = 0;
  EXPECT_EQ(OB_SUCCESS, serialization::decode(wire.data(), wire.size(), p, v));
  EXPECT_EQ(OB_SUCCESS, serialization::decode(wire.data(), wire.size(), p, n));
  EXPECT_EQ(1, static_cast<unsigned char>(wire[p]));
  ++p;
  ++p;
  return wire.substr(p, wire.size() - p - 1);
}
std::vector<std::pair<std::string, std::string>> plan_frames() {
  std::ifstream file("unittest/sql/data/values_table_access.golden");
  EXPECT_TRUE(file.good());
  std::vector<std::pair<std::string, std::string>> result;
  std::string line;
  while (std::getline(file, line)) {
    if (line.empty() || line[0] == '#') { continue; }
    const auto tab = line.find('\t');
    result.emplace_back(line.substr(0, tab), unhex(line.substr(tab + 1)));
  }
  return result;
}
std::string encode_plan(const ObValuesTableAccessSpec &spec) {
  std::string result(spec.get_serialize_size(), '\0');
  int64_t pos = 0;
  EXPECT_EQ(OB_SUCCESS, spec.serialize(&result[0], result.size(), pos));
  EXPECT_EQ(result.size(), pos);
  return result;
}
std::string encode_value(const DatumParameter &value) {
  int64_t size = 0;
  EXPECT_EQ(OB_SUCCESS, DatumParameterCodec::value_size(value.datum_, value.get_type_meta(), size));
  std::string result(size, '\0');
  int64_t pos = 0;
  EXPECT_EQ(OB_SUCCESS, DatumParameterCodec::encode_value(value.datum_, value.get_type_meta(),
                                                         &result[0], size, pos));
  EXPECT_EQ(size, pos);
  return result;
}
} // namespace

TEST(ValuesTableDatum, plan_and_statement_constants_are_native) {
  using PlanValue = typename std::decay<decltype(std::declval<ObValuesTableAccessSpec>().datum_values_.at(0))>::type;
  using StatementValue = typename std::decay<decltype(std::declval<ObValuesTableDef>().access_values_.at(0))>::type;
  static_assert(std::is_same<PlanValue, DatumValue>::value, "Compact native plan constants");
  static_assert(std::is_same<StatementValue, DatumParameter>::value, "Native statement constants");
  static_assert(sizeof(DatumValue) == 16, "Do not store full 64-byte parameters in plan constants");
  static_assert(ObValuesTableDef::ACCESS_VALUE == 3, "Keep the existing access enum wire value");
}

TEST(ValuesTableDatum, captured_legacy_plan_wire_and_native_owned_constants_match) {
  const auto expected = plan_frames();
  EXPECT_EQ(152, expected.size());
  int covered = 0;
  for (const auto &frame : expected) {
    SCOPED_TRACE(frame.first);
    ObArenaAllocator output;
    ObValuesTableAccessSpec spec(output, PHY_VALUES_TABLE_ACCESS);
    spec.access_type_ = ObValuesTableDef::ACCESS_VALUE;
    if (frame.first != "empty") {
      ObArenaAllocator input;
      bool found = false;
      for (const auto &f : frames()) {
        if (f.name != frame.first) { continue; }
        found = true;
        ObRangeMap map(input);
        int64_t pos = 0;
        ASSERT_EQ(OB_SUCCESS, map.deserialize(f.wire.data(), f.wire.size(), pos));
        const auto &entry = map.expr_final_infos_.at(0);
        DatumParameter source;
        source.set_datum(*entry.const_val_);
        source.set_type_meta(entry.const_meta_);
        ASSERT_EQ(OB_SUCCESS, spec.datum_values_.prepare_allocate(1));
        ASSERT_EQ(OB_SUCCESS, clone_datum_value(source.datum_, source.get_type_meta(),
                                              spec.datum_values_.at(0).datum_, output));
        spec.datum_values_.at(0).meta_ = source.get_type_meta();
        ++covered;
        break;
      }
      ASSERT_TRUE(found);
    }
    // The producer arena has gone away before serialization of the copied plan.
    EXPECT_EQ(frame.second, encode_plan(spec));
    ObArenaAllocator decoded_arena;
    ObValuesTableAccessSpec decoded(decoded_arena, PHY_VALUES_TABLE_ACCESS);
    int64_t pos = 0;
    ASSERT_EQ(OB_SUCCESS, decoded.deserialize(frame.second.data(), frame.second.size(), pos));
    EXPECT_EQ(frame.second.size(), pos);
    EXPECT_EQ(frame.second, encode_plan(decoded));
    EXPECT_EQ(spec.datum_values_.count(), decoded.datum_values_.count());
  }
  EXPECT_EQ(151, covered);
}

TEST(ValuesTableDatum, captured_plan_rejects_truncated_constant_payloads) {
  int checked = 0;
  for (const auto &frame : plan_frames()) {
    if (frame.first == "empty") { continue; }
    ObArenaAllocator arena;
    ObValuesTableAccessSpec output(arena, PHY_VALUES_TABLE_ACCESS);
    int64_t pos = 0;
    EXPECT_NE(OB_SUCCESS, output.deserialize(frame.second.data(), frame.second.size() - 1, pos));
    ++checked;
  }
  EXPECT_EQ(151, checked);
}

TEST(ValuesTableDatum, statement_deep_copy_owns_values_and_preserves_parameter_metadata) {
  ObArenaAllocator output;
  ObValuesTableDef copied;
  std::vector<std::string> expected;
  {
    ObArenaAllocator input;
    ObValuesTableDef source;
    for (const auto &f : frames()) {
      if (f.name == "empty" || f.name == "mixed" || f.name == "sql-array-writer-only") { continue; }
      ObRangeMap map(input);
      int64_t pos = 0;
      ASSERT_EQ(OB_SUCCESS, map.deserialize(f.wire.data(), f.wire.size(), pos));
      const auto &entry = map.expr_final_infos_.at(0);
      DatumParameter parameter;
      parameter.set_datum(*entry.const_val_);
      parameter.set_type_meta(entry.const_meta_);
      parameter.set_accuracy(ObAccuracy(37, 7));
      parameter.set_result_flags(0x1234);
      parameter.set_raw_text_info(13, 29);
      ASSERT_EQ(OB_SUCCESS, source.access_values_.push_back(parameter));
      expected.push_back(bare_value(f.canonical));
    }
    source.access_type_ = ObValuesTableDef::ACCESS_VALUE;
    source.column_cnt_ = 1;
    source.row_cnt_ = source.access_values_.count();
    ObRawExprFactory factory(output);
    ObRawExprCopier copier(factory);
    ASSERT_EQ(OB_SUCCESS, copied.deep_copy(source, copier, &output));
  }
  EXPECT_EQ(151, copied.access_values_.count());
  EXPECT_EQ(151, copied.row_cnt_);
  for (int64_t i = 0; i < copied.access_values_.count(); ++i) {
    const auto &value = copied.access_values_.at(i);
    EXPECT_EQ(expected[i], encode_value(value));
    EXPECT_EQ(37, value.get_precision());
    EXPECT_EQ(7, value.get_scale());
    EXPECT_EQ(0x1234, value.get_result_flag());
    EXPECT_EQ(13, value.get_raw_text_pos());
    EXPECT_EQ(29, value.get_raw_text_len());
  }
}

TEST(ValuesTableDatum, native_ndv_hash_is_deterministic_for_range_map_values) {
  int checked = 0, seen = 0;
  for (const auto &f : frames()) {
    if (f.name == "empty" || f.name == "mixed" || f.name == "sql-array-writer-only") { continue; }
    SCOPED_TRACE(f.name);
    ObArenaAllocator arena;
    ObRangeMap map(arena);
    int64_t pos = 0;
    ASSERT_EQ(OB_SUCCESS, map.deserialize(f.wire.data(), f.wire.size(), pos));
    const auto &entry = map.expr_final_infos_.at(0);
    ++seen;
    // Wire fixtures also exercise intentionally invalid collation IDs. They
    // can be round-tripped, but cannot be passed to a charset hash function.
    if (entry.const_meta_.is_string_type() && !entry.const_val_->is_null() &&
        !ObCharset::is_valid_collation(entry.const_meta_.get_collation_type())) {
      continue;
    }
    bool all_seeds_succeeded = true;
    for (uint64_t seed : {uint64_t(0), uint64_t(7), UINT64_MAX}) {
      uint64_t first = 0, second = 0;
      const int first_ret = hash_datum_for_ndv(*entry.const_val_, entry.const_meta_, seed, first);
      const int second_ret = hash_datum_for_ndv(*entry.const_val_, entry.const_meta_, seed, second);
      ASSERT_EQ(first_ret, second_ret);
      if (OB_SUCCESS == first_ret) {
        EXPECT_EQ(first, second);
        if (!entry.const_val_->is_null()
            && (ob_is_json(entry.const_meta_.get_type())
                || ob_is_geometry(entry.const_meta_.get_type()))) {
          const uint64_t expected = ObCharset::hash(
              entry.const_meta_.get_collation_type(), entry.const_val_->ptr_,
              entry.const_val_->len_, seed, false, NULL);
          EXPECT_EQ(expected, first);
        }
      } else {
        all_seeds_succeeded = false;
      }
    }
    if (all_seeds_succeeded) { ++checked; }
  }
  EXPECT_EQ(151, seen);
  EXPECT_GE(checked, 130);
  RecordProperty("hash_samples", checked);
}
