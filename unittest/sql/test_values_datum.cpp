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
#include "share/datum/typed_datum_compare.h"
#include "sql/engine/ob_exec_context.h"
#include "sql/engine/ob_physical_plan_ctx.h"
#include "sql/rewrite/ob_query_range_define.h"
#include "sql/rewrite/ob_range_generator.h"
#include "sql/engine/basic/ob_values_table_access_op.h"
#include "sql/resolver/expr/ob_raw_expr_copier.h"
#include <algorithm>
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
  const int64_t size = spec.get_serialize_size();
  EXPECT_GE(size, 0);
  if (size < 0) { return std::string(); }
  std::string result(size, '\0');
  int64_t pos = 0;
  EXPECT_EQ(OB_SUCCESS, spec.serialize(&result[0], result.size(), pos));
  EXPECT_EQ(result.size(), pos);
  return result;
}
std::string encode_value(const DatumParameter &value) {
  int64_t size = 0;
  EXPECT_EQ(OB_SUCCESS, DatumParameterCodec::native_value_size(value.datum_, value.get_type_meta(), size));
  std::string result(size, '\0');
  int64_t pos = 0;
  EXPECT_EQ(OB_SUCCESS, DatumParameterCodec::encode_native_value(value.datum_, value.get_type_meta(),
                                                         &result[0], size, pos));
  EXPECT_EQ(size, pos);
  return result;
}
// These six captured *present* values have only bare metadata. The old wire
// contains no source labels, schema graph or declaration identity; target column
// expressions are absent and could not substitute for a source declaration.
bool missing_source_declaration(const std::string &name)
{
  return name == "value-type-32" || name == "value-type-33" || name == "value-type-34"
      || name == "value-type-35" || name == "value-type-38" || name == "value-type-40";
}

std::string encode_value(const DatumValue &value)
{
  DatumParameter view;
  view.set_datum(value.datum_);
  view.set_type_meta(value.meta_);
  return encode_value(view);
}

void expect_value_plan_shape(const ObValuesTableAccessSpec &spec)
{
  EXPECT_EQ(ObValuesTableDef::ACCESS_VALUE, spec.access_type_);
  EXPECT_TRUE(spec.column_exprs_.empty());
  EXPECT_TRUE(spec.value_exprs_.empty());
  EXPECT_EQ(-1, spec.start_param_idx_);
  EXPECT_EQ(-1, spec.end_param_idx_);
}
} // namespace

TEST(ValuesTableDatum, plan_and_statement_constants_are_native) {
  using PlanValue = typename std::decay<decltype(std::declval<ObValuesTableAccessSpec>().datum_values_.at(0))>::type;
  using StatementValue = typename std::decay<decltype(std::declval<ObValuesTableDef>().access_values_.at(0))>::type;
  static_assert(std::is_same<PlanValue, DatumValue>::value, "Compact native plan constants");
  static_assert(std::is_same<StatementValue, DatumParameter>::value, "Native statement constants");
  static_assert(sizeof(DatumValue) == 24, "Native value/meta constants avoid parameter attributes");
  static_assert(ObValuesTableDef::ACCESS_VALUE == 3, "Keep the existing access enum wire value");
}

TEST(ValuesTableDatum, captured_legacy_plan_wire_and_native_owned_constants_match) {
  const auto expected = plan_frames();
  ASSERT_EQ(152, expected.size());
  int covered = 0, rejected = 0, restored = 0;
  for (const auto &frame : expected) {
    SCOPED_TRACE(frame.first);
    ObArenaAllocator output;
    ObValuesTableAccessSpec spec(output, PHY_VALUES_TABLE_ACCESS);
    spec.access_type_ = ObValuesTableDef::ACCESS_VALUE;
    std::string expected_value;
    const bool needs_declaration = missing_source_declaration(frame.first);
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
        ASSERT_EQ(OB_SUCCESS, source.assign_native_borrowed(entry.const_val_, entry.const_meta_));
        ASSERT_EQ(OB_SUCCESS, spec.datum_values_.prepare_allocate(1));
        ASSERT_EQ(OB_SUCCESS, clone_typed_value(source.value_view(), spec.datum_values_.at(0), output));
        expected_value = bare_value(f.canonical); // independent captured value wire
        EXPECT_EQ(expected_value, encode_value(spec.datum_values_.at(0)));
        if (needs_declaration) {
          EXPECT_FALSE(source.is_null());
          EXPECT_TRUE(source.get_meta().is_enum_or_set() || source.get_meta().is_enumset_inner_type()
              || source.get_type() == ObUserDefinedSQLType || source.get_type() == ObCollectionSQLType);
        } else {
          ASSERT_EQ(OB_SUCCESS, spec.datum_value_types_.prepare_allocate(1));
          SqlTypeMeta type_meta = source.get_type_meta();
          if (type_meta.is_ext() && (source.is_null() || source.is_min_value()
              || source.is_max_value() || source.is_nop_value())) {
            // Checked no-resource states may retain their captured Ref15
            // wrapper/meta instead of the canonical bare native state.
            type_meta.set_extend_type(0);
          }
          ASSERT_EQ(OB_SUCCESS, SqlType::from_legacy(type_meta, ObAccuracy(), output, spec.datum_value_types_.at(0)));
        }
        ++covered;
        break;
      }
      ASSERT_TRUE(found);
    }
    // Both producer inputs have retired before these owned outputs are read.
    ObArenaAllocator decoded_arena;
    ObValuesTableAccessSpec decoded(decoded_arena, PHY_VALUES_TABLE_ACCESS);
    int64_t pos = 0;
    std::string legacy_input = frame.second;
    const int old_ret = decoded.deserialize(legacy_input.data(), legacy_input.size(), pos);
    if (needs_declaration) {
      EXPECT_EQ(OB_NOT_SUPPORTED, old_ret);
      EXPECT_TRUE(decoded.datum_values_.empty());
      EXPECT_TRUE(decoded.datum_value_types_.empty());
      // An incomplete native constant must also be refused by the writer.
      EXPECT_EQ(-1, spec.get_serialize_size());
      ++rejected;
      continue;
    }
    ASSERT_EQ(OB_SUCCESS, old_ret);
    EXPECT_EQ(frame.second.size(), pos);
    std::fill(legacy_input.begin(), legacy_input.end(), char(0xa5));
    expect_value_plan_shape(decoded);
    ASSERT_EQ(spec.datum_values_.count(), decoded.datum_values_.count());
    ASSERT_EQ(decoded.datum_values_.count(), decoded.datum_value_types_.count());
    if (!spec.datum_values_.empty()) {
      EXPECT_EQ(expected_value, encode_value(decoded.datum_values_.at(0)));
      ASSERT_NE(nullptr, decoded.datum_value_types_.at(0));
      EXPECT_TRUE(spec.datum_value_types_.at(0)->equals(*decoded.datum_value_types_.at(0)));
    }
    const std::string native_wire = encode_plan(spec);
    EXPECT_NE(frame.second, native_wire); // intentional v2 declaration wire
    EXPECT_EQ(native_wire, encode_plan(decoded));
    ObArenaAllocator native_arena;
    ObValuesTableAccessSpec native_decoded(native_arena, PHY_VALUES_TABLE_ACCESS);
    pos = 0;
    std::string native_input = native_wire;
    ASSERT_EQ(OB_SUCCESS, native_decoded.deserialize(native_input.data(), native_input.size(), pos));
    EXPECT_EQ(native_wire.size(), pos);
    std::fill(native_input.begin(), native_input.end(), char(0xa5));
    expect_value_plan_shape(native_decoded);
    ASSERT_EQ(spec.datum_values_.count(), native_decoded.datum_values_.count());
    if (!spec.datum_values_.empty()) {
      EXPECT_EQ(expected_value, encode_value(native_decoded.datum_values_.at(0)));
      ASSERT_NE(nullptr, native_decoded.datum_value_types_.at(0));
      EXPECT_TRUE(spec.datum_value_types_.at(0)->equals(*native_decoded.datum_value_types_.at(0)));
    }
    EXPECT_EQ(native_wire, encode_plan(native_decoded));
    ++restored;
  }
  EXPECT_EQ(151, covered);
  EXPECT_EQ(6, rejected);
  EXPECT_EQ(146, restored);
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
      ASSERT_EQ(OB_SUCCESS, parameter.assign_native_borrowed(entry.const_val_, entry.const_meta_));
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
    if (entry.const_meta_.is_string_type() && !entry.const_val_.is_null() &&
        !ObCharset::is_valid_collation(entry.const_meta_.get_collation_type())) {
      continue;
    }
    ObDatum physical;
    alignas(8) char scratch[64];
    ASSERT_EQ(OB_SUCCESS, entry.export_const_value(physical, scratch, sizeof(scratch)));
    bool all_seeds_succeeded = true;
    for (uint64_t seed : {uint64_t(0), uint64_t(7), UINT64_MAX}) {
      uint64_t first = 0, second = 0;
      const int first_ret = hash_datum_for_ndv(physical, entry.const_meta_, seed, first);
      const int second_ret = hash_datum_for_ndv(physical, entry.const_meta_, seed, second);
      ASSERT_EQ(first_ret, second_ret);
      if (OB_SUCCESS == first_ret) {
        EXPECT_EQ(first, second);
        if (!entry.const_val_.is_null()
            && (ob_is_json(entry.const_meta_.get_type())
                || ob_is_geometry(entry.const_meta_.get_type()))) {
          const uint64_t expected = ObCharset::hash(
              entry.const_meta_.get_collation_type(), physical.ptr_,
              physical.len_, seed, false, NULL);
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
