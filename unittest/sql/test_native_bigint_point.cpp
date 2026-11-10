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

#include "sql/rewrite/ob_query_range_define.h"
#include "lib/allocator/ob_allocator.h"
#include <gtest/gtest.h>

using namespace oceanbase::common;
using namespace oceanbase::sql;

namespace {
class PointAllocator final : public ObIAllocator
{
public:
  PointAllocator() : arena(), calls(0), reject(false) {}
  void *alloc(int64_t size) override { ++calls; return reject ? nullptr : arena.alloc(size); }
  void *alloc(int64_t size, const ObMemAttr &) override { return alloc(size); }
  void free(void *pointer) override { arena.free(pointer); }
  ObArenaAllocator arena;
  int64_t calls;
  bool reject;
};

class NativeBigintPoint : public ::testing::Test
{
protected:
  NativeBigintPoint()
    : owner(), graph(owner), node(owner), columns(), parameters(ObWrapperAllocator(owner)),
      start{0, 1, 2}, end{0, 1, 2}, raw{} {}

  void SetUp() override
  {
    ObSEArray<ColumnItem, 3> items;
    for (int64_t i = 0; i < 3; ++i) {
      columns[i].set_ref_id(1001, 16 + i);
      columns[i].set_data_type(ObIntType);
      ColumnItem item; item.expr_ = &columns[i]; item.set_ref_id(1001, 16 + i);
      ASSERT_EQ(OB_SUCCESS, items.push_back(item));
      write_parameter(i, 10 + i);
      DatumParameter parameter;
      SqlTypeMeta physical; physical.set_int();
      const ObDatum legacy(raw[i] + 1, sizeof(int64_t), false);
      ASSERT_EQ(OB_SUCCESS, parameter.assign_legacy_borrowed(legacy, physical));
      ASSERT_EQ(OB_SUCCESS, parameters.push_back(parameter));
    }
    ASSERT_EQ(OB_SUCCESS, graph.fill_column_metas(items));
    ASSERT_EQ(OB_SUCCESS, graph.get_range_map().expr_final_infos_.prepare_allocate(3));
    for (int64_t i = 0; i < 3; ++i) {
      auto &info = graph.get_range_map().expr_final_infos_.at(i);
      info.is_param_ = true; info.param_idx_ = i;
    }
    node.column_cnt_ = 3; node.start_keys_ = start; node.end_keys_ = end;
    node.min_offset_ = 0; node.max_offset_ = 2;
    node.include_start_ = true; node.include_end_ = true;
    graph.set_range_head(&node); graph.set_node_count(1);
    graph.set_is_precise_get(true); graph.set_is_standard_range(true);
    graph.set_is_equal_range(true); graph.set_is_get(true);
    // Ordinary generated graphs may retain range_size_ == 0. Their shape and
    // precise-get metadata, rather than that legacy estimate, prove one point.
    ASSERT_EQ(0, graph.get_range_size());
  }

  void write_parameter(int64_t index, int64_t value)
  {
    MEMCPY(raw[index] + 1, &value, sizeof(value));
    if (index < parameters.count()) { parameters.at(index).datum_.set_int(value); }
  }

  void expect_fallback()
  {
    int64_t values[3];
    for (int64_t i = 0; i < 3; ++i) { values[i] = 100 + i; }
    bool supported = true;
    const ObQueryRangeProvider &provider = graph;
    ASSERT_EQ(OB_SUCCESS, provider.try_get_native_bigint_point(parameters, values, supported));
    EXPECT_FALSE(supported);
    for (int64_t i = 0; i < 3; ++i) { EXPECT_EQ(100 + i, values[i]); }
  }

  PointAllocator owner;
  ObPreRangeGraph graph;
  ObRangeNode node;
  ObColumnRefRawExpr columns[3];
  DatumParamStore parameters;
  int64_t start[3];
  int64_t end[3];
  char raw[3][9];
};
}

TEST_F(NativeBigintPoint, real_provider_imports_extremes_without_allocating_or_borrowing)
{
  write_parameter(0, INT64_MIN); write_parameter(1, 0); write_parameter(2, INT64_MAX);
  const int64_t before = owner.calls; owner.reject = true;
  int64_t values[3]; bool supported = false;
  const ObQueryRangeProvider &provider = graph;
  ASSERT_EQ(OB_SUCCESS, provider.try_get_native_bigint_point(parameters, values, supported));
  EXPECT_TRUE(supported); EXPECT_EQ(before, owner.calls);
  MEMSET(raw, 0x55, sizeof(raw));
  for (int64_t i = 0; i < parameters.count(); ++i) { parameters.at(i).datum_.set_null(); }
  EXPECT_EQ(INT64_MIN, values[0]); EXPECT_EQ(0, values[1]);
  EXPECT_EQ(INT64_MAX, values[2]);
  EXPECT_EQ(24U, sizeof(values));
}

TEST_F(NativeBigintPoint, final_result_indices_and_execution_parameter_indices_both_reorder)
{
  start[0] = end[0] = 2; start[1] = end[1] = 0; start[2] = end[2] = 1;
  graph.get_range_map().expr_final_infos_.at(0).param_idx_ = 2;
  graph.get_range_map().expr_final_infos_.at(1).param_idx_ = 0;
  graph.get_range_map().expr_final_infos_.at(2).param_idx_ = 1;
  int64_t values[3]; bool supported = false;
  ASSERT_EQ(OB_SUCCESS, graph.try_get_native_bigint_point(parameters, values, supported));
  EXPECT_TRUE(supported);
  EXPECT_EQ(11, values[0]); EXPECT_EQ(12, values[1]); EXPECT_EQ(10, values[2]);
}

TEST_F(NativeBigintPoint, repeated_parameter_and_repeated_final_index_are_supported)
{
  graph.get_range_map().expr_final_infos_.at(0).param_idx_ = 2;
  graph.get_range_map().expr_final_infos_.at(1).param_idx_ = 2;
  graph.get_range_map().expr_final_infos_.at(2).param_idx_ = 2;
  int64_t values[3]; bool supported = false;
  ASSERT_EQ(OB_SUCCESS, graph.try_get_native_bigint_point(parameters, values, supported));
  EXPECT_TRUE(supported);
  for (const int64_t value : values) { EXPECT_EQ(12, value); }
  start[0] = start[1] = start[2] = 1;
  end[0] = end[1] = end[2] = 1;
  ASSERT_EQ(OB_SUCCESS, graph.try_get_native_bigint_point(parameters, values, supported));
  EXPECT_TRUE(supported);
  for (const int64_t value : values) { EXPECT_EQ(12, value); }
}

TEST_F(NativeBigintPoint, adjacent_executions_read_fresh_values)
{
  int64_t first[3], second[3]; bool supported = false;
  ASSERT_EQ(OB_SUCCESS, graph.try_get_native_bigint_point(parameters, first, supported)); EXPECT_TRUE(supported);
  write_parameter(0, -91); write_parameter(1, -92); write_parameter(2, -93);
  ASSERT_EQ(OB_SUCCESS, graph.try_get_native_bigint_point(parameters, second, supported)); EXPECT_TRUE(supported);
  for (int64_t i = 0; i < 3; ++i) {
    EXPECT_EQ(10 + i, first[i]); EXPECT_EQ(-91 - i, second[i]);
  }
}

TEST_F(NativeBigintPoint, typed_null_and_null_safe_conditions_fall_back_without_partial_values)
{
  for (int64_t i = 0; i < 3; ++i) {
    const Datum previous = parameters.at(i).datum_;
    parameters.at(i).datum_.set_null();
    EXPECT_EQ(ObIntType, parameters.at(i).get_type());
    expect_fallback(); parameters.at(i).datum_ = previous;
    auto &info = graph.get_range_map().expr_final_infos_.at(i);
    info.null_safe_ = true; expect_fallback(); info.null_safe_ = false;
  }
}

TEST_F(NativeBigintPoint, casts_unsigned_and_other_parameter_types_fall_back)
{
  const SqlValueType kinds[] = {ObUInt64Type, ObInt32Type, ObVarcharType, ObDecimalIntType, ObExtendType};
  for (const SqlValueType kind : kinds) {
    for (int64_t i = 0; i < 3; ++i) {
      const DatumParameter previous = parameters.at(i);
      DatumParameter input;
      if (kind == ObUInt64Type) { ASSERT_EQ(OB_SUCCESS, input.set_uint64(12, owner)); }
      else if (kind == ObInt32Type) { ASSERT_EQ(OB_SUCCESS, input.set_int32(12, owner)); }
      else if (kind == ObVarcharType) { ASSERT_EQ(OB_SUCCESS, input.set_varchar(ObString::make_string("12"), owner)); }
      else if (kind == ObDecimalIntType) {
        const int64_t coefficient = 12;
        SqlTypeMeta meta; meta.set_decimal_int(0);
        ObDatum legacy(reinterpret_cast<const char *>(&coefficient), sizeof(coefficient), false);
        ASSERT_EQ(OB_SUCCESS, input.assign_legacy_owned(legacy, meta, owner));
      } else { input.get_meta().set_ext(); input.datum_.set_nop(); }
      parameters.at(i) = input;
      expect_fallback(); parameters.at(i) = previous;
    }
  }
}

TEST_F(NativeBigintPoint, malformed_or_extended_payloads_never_partially_publish)
{
  const DatumParameter previous = parameters.at(2);
  SqlTypeMeta meta; meta.set_int();
  const char bytes[16] = {};
  for (const int64_t length : {int64_t(0), int64_t(4), int64_t(16)}) {
    const ObDatum malformed(bytes, length, false);
    EXPECT_EQ(OB_INVALID_ARGUMENT, parameters.at(2).assign_legacy_borrowed(malformed, meta));
    EXPECT_EQ(previous.get_int(), parameters.at(2).get_int());
    EXPECT_EQ(0, parameters.at(2).get_datum().length());
  }
  ObDatum missing(nullptr, sizeof(int64_t), false);
  EXPECT_EQ(OB_INVALID_ARGUMENT, parameters.at(2).assign_legacy_borrowed(missing, meta));
  EXPECT_EQ(previous.get_int(), parameters.at(2).get_int());
  // Canonical SQL states and the original disallowed flags still exercise the
  // actual provider rejection. Invalid old byte extents cannot enter a native
  // wrapper through the checked physical import above.
  parameters.at(2).datum_.set_nop(); expect_fallback(); parameters.at(2) = previous;
  parameters.at(2).datum_.set_outrow(); expect_fallback(); parameters.at(2) = previous;
  parameters.at(2).datum_.set_has_lob_header(); expect_fallback(); parameters.at(2) = previous;
}

TEST_F(NativeBigintPoint, nonparameter_and_expression_modifiers_fall_back)
{
  auto &info = graph.get_range_map().expr_final_infos_.at(2);
  const uint32_t unsupported_flags[] = {0, 2, 3, 4, 8, 16, 32, uint32_t(1) << 31};
  for (const uint32_t flags : unsupported_flags) {
    info.flags_ = flags; expect_fallback(); info.flags_ = 1;
  }
}

TEST_F(NativeBigintPoint, invalid_final_and_parameter_indices_leave_values_untouched)
{
  const int64_t invalid_indices[] = {-1, 3, OB_RANGE_NULL_VALUE, OB_RANGE_MAX_VALUE};
  for (const int64_t index : invalid_indices) {
    start[2] = end[2] = index; expect_fallback(); start[2] = end[2] = 2;
    graph.get_range_map().expr_final_infos_.at(2).param_idx_ = index;
    expect_fallback(); graph.get_range_map().expr_final_infos_.at(2).param_idx_ = 2;
  }
  end[2] = 1; expect_fallback();
}

TEST_F(NativeBigintPoint, graph_exec_geo_fast_nlj_and_multinode_shapes_fall_back)
{
  graph.set_is_precise_get(false); expect_fallback(); graph.set_is_precise_get(true);
  graph.set_contain_exec_param(true); expect_fallback(); graph.set_contain_exec_param(false);
  graph.set_contain_geo_filters(true); expect_fallback(); graph.set_contain_geo_filters(false);
  graph.set_fast_nlj_range(true); expect_fallback(); graph.set_fast_nlj_range(false);
  graph.set_node_count(2); expect_fallback(); graph.set_node_count(1);
  graph.set_range_head(nullptr); expect_fallback(); graph.set_range_head(&node);
  node.and_next_ = &node; expect_fallback(); node.and_next_ = nullptr;
  node.or_next_ = &node; expect_fallback(); node.or_next_ = nullptr;
  ObSEArray<ObFastFinalPos, 1> positions;
  ASSERT_EQ(OB_SUCCESS, graph.set_general_nlj_range_extraction(positions)); expect_fallback();
}

TEST_F(NativeBigintPoint, node_bounds_and_nonpoint_flags_fall_back)
{
  const uint32_t previous = node.flags_;
  const uint32_t nonpoint_bits[] = {1, 2, 16, 32, 64, 128, uint32_t(1) << 31};
  for (const uint32_t bit : nonpoint_bits) {
    node.flags_ = previous | bit; expect_fallback(); node.flags_ = previous;
  }
  node.include_start_ = false; expect_fallback(); node.include_start_ = true;
  node.include_end_ = false; expect_fallback(); node.include_end_ = true;
  node.min_offset_ = 1; expect_fallback(); node.min_offset_ = 0;
  node.max_offset_ = 1; expect_fallback(); node.max_offset_ = 2;
  node.column_cnt_ = 2; expect_fallback(); node.column_cnt_ = 3;
  node.start_keys_ = nullptr; expect_fallback(); node.start_keys_ = start;
  node.end_keys_ = nullptr; expect_fallback(); node.end_keys_ = end;
  node.in_param_count_ = 1; expect_fallback();
}

TEST_F(NativeBigintPoint, target_columns_must_be_exact_signed_bigint_and_present)
{
  const SqlValueType kinds[] = {ObInt32Type, ObUInt64Type, ObVarcharType};
  for (const SqlValueType kind : kinds) {
    graph.get_column_meta(2)->column_type_.set_type(kind); expect_fallback();
    graph.get_column_meta(2)->column_type_.set_int();
  }
  ObRangeColumnMeta *previous = graph.get_column_metas().at(2);
  graph.get_column_metas().at(2) = nullptr; expect_fallback(); graph.get_column_metas().at(2) = previous;
}

TEST_F(NativeBigintPoint, providers_without_the_specialization_use_default_fallback)
{
  int64_t values[3];
  for (int64_t i = 0; i < 3; ++i) { values[i] = 100 + i; }
  bool supported = true;
  ASSERT_EQ(OB_SUCCESS, graph.ObQueryRangeProvider::try_get_native_bigint_point(parameters, values, supported));
  EXPECT_FALSE(supported);
  for (int64_t i = 0; i < 3; ++i) { EXPECT_EQ(100 + i, values[i]); }
}

int main(int argc, char **argv)
{
  ::testing::InitGoogleTest(&argc, argv);
  return RUN_ALL_TESTS();
}
