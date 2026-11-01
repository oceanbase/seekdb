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
#include "sql/engine/ob_exec_context.h"
#include "sql/engine/ob_physical_plan_ctx.h"
#include "sql/rewrite/ob_query_range_define.h"
#include "sql/rewrite/ob_range_generator.h"
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
std::string encode(const ObRangeMap &map) {
  std::string out(map.get_serialize_size(), '\0');
  int64_t p = 0;
  EXPECT_EQ(OB_SUCCESS, map.serialize(&out[0], out.size(), p));
  EXPECT_EQ(out.size(), p);
  return out;
}
std::string encode_value(const DatumValue &value) {
  int64_t size = 0, pos = 0;
  EXPECT_EQ(OB_SUCCESS, DatumParameterCodec::native_value_size(value.datum_, value.meta_, size));
  std::string out(size, '\0');
  EXPECT_EQ(OB_SUCCESS, DatumParameterCodec::encode_native_value(
      value.datum_, value.meta_, &out[0], out.size(), pos));
  EXPECT_EQ(out.size(), pos);
  return out;
}
std::string wire_body(const std::string &body) {
  std::string out(1 + serialization::OB_SERIALIZE_SIZE_NEED_BYTES + body.size(), '\0');
  int64_t p = 0;
  EXPECT_EQ(OB_SUCCESS, serialization::encode_vi64(&out[0], out.size(), p, 1));
  EXPECT_EQ(OB_SUCCESS, serialization::encode_fixed_bytes_i64(&out[0], out.size(), p, body.size()));
  memcpy(&out[p], body.data(), body.size());
  return out;
}
std::string vi64(int64_t n) {
  char b[16];
  int64_t p = 0;
  EXPECT_EQ(OB_SUCCESS, serialization::encode_vi64(b, sizeof(b), p, n));
  return std::string(b, p);
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
class CountingAllocator : public ObIAllocator {
public:
  ObArenaAllocator arena;
  int64_t calls = 0, bytes = 0;
  bool reject = false;
  int64_t reject_at = -1;
  void *alloc(int64_t n) override {
    ++calls;
    bytes += n;
    return reject || calls == reject_at ? nullptr : arena.alloc(n);
  }
  void *alloc(int64_t n, const ObMemAttr &) override { return alloc(n); }
  void free(void *) override {}
};
} // namespace
TEST(RangeMapDatum, constants_are_datums_with_metadata_in_existing_padding) {
  static_assert(std::is_same<decltype(ObRangeMap::ExprFinalInfo::const_val_), Datum>::value,
                "No legacy value in range constants");
  EXPECT_EQ(32, sizeof(ObRangeMap::ExprFinalInfo));
  EXPECT_EQ(4, offsetof(ObRangeMap::ExprFinalInfo, const_meta_));
  auto corpus = frames();
  ASSERT_EQ(154, corpus.size());
  for (auto f : corpus) {
    SCOPED_TRACE(f.name);
    ObArenaAllocator arena;
    ObRangeMap map(arena);
    int64_t p = 0;
    ASSERT_EQ(OB_SUCCESS, map.deserialize(f.wire.data(), f.wire.size(), p));
    EXPECT_EQ(f.wire.size(), p);
    EXPECT_EQ(f.canonical, encode(map));
    memset(&f.wire[0], 0xa5, f.wire.size());
    EXPECT_EQ(f.canonical, encode(map));
    if (f.name == "mixed") {
      ASSERT_EQ(3, map.expr_final_infos_.count());
      EXPECT_TRUE(map.expr_final_infos_.at(0).is_param_);
      EXPECT_EQ(17, map.expr_final_infos_.at(0).param_idx_);
      EXPECT_EQ(ObVarcharType, map.expr_final_infos_.at(1).const_meta_.get_type());
      EXPECT_TRUE(map.expr_final_infos_.at(2).is_expr_);
      const ObTempExpr *inert = map.expr_final_infos_.at(2).temp_expr_;
      ASSERT_NE(nullptr, inert);
      EXPECT_EQ(0, inert->need_ctx_cnt_);
      EXPECT_TRUE(inert->rt_exprs_.empty());
      EXPECT_TRUE(inert->const_frame_ptrs_.empty());
      EXPECT_TRUE(inert->const_frame_.empty());
      EXPECT_TRUE(inert->param_frame_.empty());
      EXPECT_TRUE(inert->datum_frame_.empty());
      EXPECT_TRUE(inert->dynamic_frame_.empty());
      EXPECT_EQ(OB_INVALID_INDEX, inert->expr_idx_);
      EXPECT_TRUE(inert->idx_col_arr_.empty());
      ObExecContext exec(arena);
      Datum unused;
      EXPECT_EQ(OB_INVALID_ARGUMENT, inert->eval_datum(exec, arena, unused));
      ASSERT_EQ(1, map.in_params_.count());
      ASSERT_EQ(3, map.in_params_.at(0)->count());
      EXPECT_EQ(-3, map.in_params_.at(0)->at(2));
    }
  }
}
TEST(RangeMapDatum, deep_copy_of_constants_and_arrays_outlives_source_arena) {
  for (const auto &f : frames()) {
    if (f.name == "empty" || f.name == "mixed") {
      continue;
    }
    SCOPED_TRACE(f.name);
    ObArenaAllocator output_arena;
    ObPreRangeGraph copied(output_arena);
    {
      ObArenaAllocator source_arena;
      ObRangeMap source(source_arena);
      int64_t p = 0;
      ASSERT_EQ(OB_SUCCESS, source.deserialize(f.wire.data(), f.wire.size(), p));
      ASSERT_EQ(OB_SUCCESS, copied.deep_copy_range_map(source));
    }
    EXPECT_EQ(f.canonical, encode(copied.get_range_map()));
  }
}
TEST(RangeMapDatum, truncated_and_invalid_records_never_expose_partial_maps) {
  for (const auto &f : frames()) {
    SCOPED_TRACE(f.name);
    for (size_t n = 0; n < f.wire.size(); ++n) {
      ObArenaAllocator a;
      ObRangeMap map(a);
      int64_t p = 0;
      EXPECT_NE(OB_SUCCESS, map.deserialize(f.wire.data(), n, p));
      EXPECT_EQ(0, map.expr_final_infos_.count());
      EXPECT_EQ(0, map.in_params_.count());
    }
  }
  std::vector<std::string> bodies = {vi64(-1),
                                     vi64(INT64_MAX),
                                     std::string("\1\2", 2),
                                     std::string("\1\1", 2),
                                     std::string("\1\4", 2),
                                     std::string("\0", 1) + vi64(-1),
                                     std::string("\0", 1) + vi64(INT64_MAX),
                                     std::string("\0\1", 2) + vi64(INT64_MAX)};
  for (const auto &body : bodies) {
    ObArenaAllocator a;
    ObRangeMap map(a);
    std::string wire = wire_body(body);
    int64_t p = 0;
    EXPECT_NE(OB_SUCCESS, map.deserialize(wire.data(), wire.size(), p));
    EXPECT_EQ(0, map.expr_final_infos_.count());
    EXPECT_EQ(0, map.in_params_.count());
  }
}
TEST(RangeMapDatum, reused_decoder_replaces_in_lists_and_clears_failed_constants) {
  auto corpus = frames();
  const RangeFrame *mixed = nullptr, *empty = nullptr;
  for (const auto &f : corpus) {
    if (f.name == "mixed") {
      mixed = &f;
    }
    if (f.name == "empty") {
      empty = &f;
    }
  }
  ASSERT_NE(nullptr, mixed);
  ASSERT_NE(nullptr, empty);
  ObArenaAllocator arena;
  ObRangeMap map(arena);
  int64_t p = 0;
  ASSERT_EQ(OB_SUCCESS, map.deserialize(mixed->wire.data(), mixed->wire.size(), p));
  ASSERT_EQ(1, map.in_params_.count());
  p = 0;
  ASSERT_EQ(OB_SUCCESS, map.deserialize(empty->wire.data(), empty->wire.size(), p));
  EXPECT_EQ(0, map.in_params_.count());
  EXPECT_EQ(0, map.expr_final_infos_.count());
  const std::string bad = wire_body(std::string("\1\2", 2));
  p = 0;
  EXPECT_NE(OB_SUCCESS, map.deserialize(bad.data(), bad.size(), p));
  EXPECT_EQ(0, map.expr_final_infos_.count());
  EXPECT_EQ(0, map.in_params_.count());
}
TEST(RangeMapDatum, native_constants_keep_captured_value_wire_at_range_boundary) {
  int covered = 0;
  for (const auto &f : frames()) {
    if (f.name != "value-type-0" && f.name != "value-type-5" && f.name != "value-type-10" &&
        f.name != "value-type-15" && f.name != "value-type-17" && f.name != "value-type-22" &&
        f.name != "value-decimal-width-4" && f.name != "sql-array-writer-only") {
      continue;
    }
    SCOPED_TRACE(f.name);
    ObArenaAllocator a;
    ObRangeMap map(a);
    ObExecContext ctx(a);
    ASSERT_EQ(OB_SUCCESS, ctx.create_physical_plan_ctx());
    int64_t p = 0;
    ASSERT_EQ(OB_SUCCESS, map.deserialize(f.wire.data(), f.wire.size(), p));
    DatumValue value;
    bool valid = false;
    ASSERT_EQ(OB_SUCCESS, ObRangeGenerator::calc_result_value(a, map, 0, value, valid, ctx));
    EXPECT_TRUE(valid);
    EXPECT_EQ(bare_value(f.wire), encode_value(value));
    ++covered;
  }
  EXPECT_EQ(8, covered);
}
TEST(RangeMapDatum, copying_values_uses_one_owned_payload_without_unused_reserve) {
  CountingAllocator strings;
  char bytes[] = "abc";
  DatumParameter source;
  source.set_varchar_view(ObString(3, bytes));
  source.set_collation_type(CS_TYPE_UTF8MB4_BIN);
  source.set_result_flag(0x1234);
  source.set_raw_text_info(17, 19);
  DatumParameter value;
  ASSERT_EQ(OB_SUCCESS, clone_datum_parameter(source, value, strings));
  EXPECT_EQ(1, strings.calls);
  EXPECT_EQ(3, strings.bytes);
  memset(bytes, 'x', 3);
  EXPECT_EQ(ObString("abc"), value.datum_.get_string());
  EXPECT_EQ(0x1234, value.get_result_flag());
  EXPECT_EQ(17, value.get_raw_text_pos());
  EXPECT_EQ(19, value.get_raw_text_len());
  CountingAllocator decimals;
  int32_t raw = -1234;
  source.reset();
  ASSERT_EQ(OB_SUCCESS, source.datum_.set_reference(&raw, sizeof(raw)));
  source.get_meta().set_decimal_int(2);
  source.set_precision(38);
  ASSERT_EQ(OB_SUCCESS, clone_datum_parameter(source, value, decimals));
  EXPECT_EQ(1, decimals.calls);
  EXPECT_EQ(sizeof(raw), decimals.bytes);
  raw = 0;
  EXPECT_EQ(-1234, *reinterpret_cast<const int32_t *>(value.datum_.get_reference()));
}
TEST(RangeMapDatum, failed_value_clone_leaves_datum_and_type_unchanged) {
  CountingAllocator rejecting;
  rejecting.reject = true;
  DatumParameter source;
  source.set_varchar_view(ObString::make_string("payload"));
  DatumParameter destination;
  int64_t scalar = 77;
  destination.datum_.set_int(scalar);
  destination.get_meta().set_int();
  const DatumParameter before = destination;
  EXPECT_EQ(OB_ALLOCATE_MEMORY_FAILED, clone_datum_parameter(source, destination, rejecting));
  EXPECT_EQ(0, MEMCMP(&before.datum_, &destination.datum_, sizeof(Datum)));
  EXPECT_EQ(before.get_meta(), destination.get_meta());
}
TEST(RangeMapDatum, fast_nlj_constant_bounds_survive_plan_arena_and_keep_null_rules) {
  ObArenaAllocator output_arena;
  char *memory = static_cast<char *>(output_arena.alloc(sizeof(ObNewRange) + 2 * sizeof(DatumValue)));
  ASSERT_NE(nullptr, memory);
  auto *range = new (memory) ObNewRange();
  new (memory + sizeof(ObNewRange)) DatumValue();
  new (memory + sizeof(ObNewRange) + sizeof(DatumValue)) DatumValue();
  {
    ObArenaAllocator plan_arena;
    ObPreRangeGraph graph(plan_arena);
    ObRangeNode node(plan_arena);
    int64_t key = 0;
    node.column_cnt_ = 1;
    node.start_keys_ = &key;
    node.end_keys_ = &key;
    node.include_start_ = true;
    node.include_end_ = true;
    graph.set_range_head(&node);
    DatumParameter input, owned;
    input.set_varchar_view(ObString::make_string("lookup key"));
    input.set_collation_type(CS_TYPE_UTF8MB4_BIN);
    ASSERT_EQ(OB_SUCCESS, clone_datum_parameter(input, owned, plan_arena));
    SqlTypeMeta meta = owned.get_meta();
    ASSERT_EQ(OB_SUCCESS, graph.get_range_map().expr_final_infos_.prepare_allocate(1));
    auto &info = graph.get_range_map().expr_final_infos_.at(0);
    ASSERT_EQ(OB_SUCCESS, info.set_const_value(owned.datum_, meta, plan_arena));
    ObRangeColumnMeta column;
    column.column_type_.set_type(ObVarcharType);
    column.column_type_.set_collation_type(CS_TYPE_UTF8MB4_BIN);
    column.column_type_.set_collation_level(meta.get_collation_level());
    ObSEArray<ObRangeColumnMeta *, 1> columns;
    ASSERT_EQ(OB_SUCCESS, columns.push_back(&column));
    ASSERT_EQ(OB_SUCCESS, graph.deep_copy_column_metas(columns));
    ObPhysicalPlanCtx no_parameters(plan_arena);
    bool valid = false;
    ASSERT_EQ(OB_SUCCESS,
              ObRangeGenerator::check_can_final_fast_nlj_range(graph, no_parameters, valid));
    EXPECT_TRUE(valid);
    ASSERT_EQ(OB_SUCCESS,
              ObRangeGenerator::generate_fast_nlj_range(graph, no_parameters, output_arena, range));
  }
  EXPECT_EQ(ObString("lookup key"), range->start_key_.get_cell(0).get_string());
  EXPECT_EQ(ObString("lookup key"), range->end_key_.get_cell(0).get_string());
  ObArenaAllocator a;
  ObPreRangeGraph graph(a);
  ObRangeNode node(a);
  int64_t key = 0;
  node.column_cnt_ = 1;
  node.start_keys_ = &key;
  node.end_keys_ = &key;
  graph.set_range_head(&node);
  ASSERT_EQ(OB_SUCCESS, graph.get_range_map().expr_final_infos_.prepare_allocate(1));
  auto &info = graph.get_range_map().expr_final_infos_.at(0);
  Datum null;
  SqlTypeMeta null_meta;
  null_meta.set_null();
  ASSERT_EQ(OB_SUCCESS, info.set_const_value(null, null_meta, a));
  ObPhysicalPlanCtx empty(a);
  ASSERT_EQ(OB_SUCCESS, ObRangeGenerator::generate_fast_nlj_range(graph, empty, a, range));
  EXPECT_TRUE(range->start_key_.get_cell(0).is_max_value());
  EXPECT_TRUE(range->end_key_.get_cell(0).is_min_value());
  info.null_safe_ = true;
  ASSERT_EQ(OB_SUCCESS, ObRangeGenerator::generate_fast_nlj_range(graph, empty, a, range));
  EXPECT_TRUE(range->start_key_.get_cell(0).is_null());
  EXPECT_TRUE(range->end_key_.get_cell(0).is_null());
}
TEST(RangeMapDatum, native_value_clone_preserves_every_captured_value_and_owns_payload) {
  int count = 0;
  for (const auto &f : frames()) {
    if (f.name == "empty" || f.name == "mixed") {
      continue;
    }
    SCOPED_TRACE(f.name);
    ObArenaAllocator destination_arena;
    ObRangeMap output(destination_arena);
    ASSERT_EQ(OB_SUCCESS, output.expr_final_infos_.prepare_allocate(1));
    {
      ObArenaAllocator source_arena;
      ObRangeMap source(source_arena);
      int64_t p = 0;
      ASSERT_EQ(OB_SUCCESS, source.deserialize(f.wire.data(), f.wire.size(), p));
      const auto &entry = source.expr_final_infos_.at(0);
      auto &copy = output.expr_final_infos_.at(0);
      ASSERT_EQ(OB_SUCCESS, copy.set_const_value(entry.const_val_, entry.const_meta_, destination_arena));
      copy.flags_ = entry.flags_;
    }
    EXPECT_EQ(f.canonical, encode(output));
    ++count;
  }
  EXPECT_EQ(152, count);
}

TEST(RangeMapDatum, native_parameter_boundaries_keep_all_captured_scalar_values_and_own_bytes) {
  int covered = 0;
  for (const auto &f : frames()) {
    if (f.name == "empty" || f.name == "mixed" || f.name == "sql-array-writer-only") {
      continue;
    }
    SCOPED_TRACE(f.name);
    ObArenaAllocator output;
    char *buffer = static_cast<char *>(output.alloc(sizeof(ObNewRange) + 2 * sizeof(DatumValue)));
    ASSERT_NE(nullptr, buffer);
    auto *range = new (buffer) ObNewRange();
    new (buffer + sizeof(ObNewRange)) DatumValue();
    new (buffer + sizeof(ObNewRange) + sizeof(DatumValue)) DatumValue();
    {
      ObArenaAllocator input;
      ObRangeMap original(input);
      int64_t pos = 0;
      ASSERT_EQ(OB_SUCCESS, original.deserialize(f.wire.data(), f.wire.size(), pos));
      DatumParameter parameter;
      ASSERT_EQ(OB_SUCCESS, parameter.assign_native_borrowed(
          original.expr_final_infos_.at(0).const_val_, original.expr_final_infos_.at(0).const_meta_));
      parameter.set_type_meta(original.expr_final_infos_.at(0).const_meta_);
      // Captured legacy values carry physical metadata only. Keep this full
      // corpus on the existing constant boundary, without fabricating enum,
      // UDT or odd-collation declarations for a ready parameter binding.
      ObPhysicalPlanCtx parameters(input);
      ObPreRangeGraph graph(input);
      ObRangeNode node(input);
      int64_t key = 0;
      node.column_cnt_ = 1;
      node.start_keys_ = &key;
      node.end_keys_ = &key;
      graph.set_range_head(&node);
      ASSERT_EQ(OB_SUCCESS, graph.get_range_map().expr_final_infos_.prepare_allocate(1));
      auto &entry = graph.get_range_map().expr_final_infos_.at(0);
      ASSERT_EQ(OB_SUCCESS, entry.set_const_value(parameter.datum_, parameter.get_type_meta(), input));
      entry.null_safe_ = true;
      ASSERT_EQ(OB_SUCCESS,
                ObRangeGenerator::generate_fast_nlj_range(graph, parameters, output, range));
    }
    EXPECT_EQ(bare_value(f.canonical), encode_value(range->start_key_.get_cell(0)));
    ++covered;
  }
  EXPECT_EQ(151, covered);
}
TEST(RangeMapDatum, native_fast_parameter_bounds_keep_composite_types_and_lifetime) {
  ObArenaAllocator output;
  char *memory = static_cast<char *>(output.alloc(sizeof(ObNewRange) + 8 * sizeof(DatumValue)));
  ASSERT_NE(nullptr, memory);
  auto *range = new (memory) ObNewRange();
  for (int i = 0; i < 8; ++i) {
    new (memory + sizeof(ObNewRange) + i * sizeof(DatumValue)) DatumValue();
  }
  {
    ObArenaAllocator input;
    ObPreRangeGraph graph(input);
    ObRangeNode node(input);
    int64_t keys[] = {0, 1, 2, 3};
    node.column_cnt_ = 4;
    node.start_keys_ = keys;
    node.end_keys_ = keys;
    node.include_start_ = true;
    node.include_end_ = true;
    graph.set_range_head(&node);
    ASSERT_EQ(OB_SUCCESS, graph.get_range_map().expr_final_infos_.prepare_allocate(4));
    DatumParameter raw[4];
    ASSERT_EQ(OB_SUCCESS, raw[0].set_int(INT64_MIN, input));
    ASSERT_EQ(OB_SUCCESS, raw[1].set_uint64(UINT64_MAX, input));
    char text[] = "客户-key";
    raw[2].set_varchar_view(ObString::make_string(text));
    raw[2].set_collation_type(CS_TYPE_UTF8MB4_BIN);
    ASSERT_EQ(OB_SUCCESS, raw[3].set_datetime(1792702301123456LL, input));
    ObExecContext execution(input);
    ASSERT_EQ(OB_SUCCESS, execution.create_physical_plan_ctx());
    ObPhysicalPlanCtx &parameters = *execution.get_physical_plan_ctx();
    ObRangeColumnMeta columns[4];
    ObSEArray<ObRangeColumnMeta *, 4> column_refs;
    for (int i = 0; i < 4; ++i) {
      DatumParameter value;
      ASSERT_EQ(OB_SUCCESS, clone_datum_parameter(raw[i], value, input));
      ASSERT_EQ(OB_SUCCESS, parameters.append_parameter_input(value));
      auto &entry = graph.get_range_map().expr_final_infos_.at(i);
      entry.is_param_ = true;
      entry.param_idx_ = i;
      columns[i].column_type_.set_type(value.get_type());
      columns[i].column_type_.set_collation_type(value.get_collation_type());
      columns[i].column_type_.set_collation_level(value.get_collation_level());
      ASSERT_EQ(OB_SUCCESS, column_refs.push_back(&columns[i]));
    }
    ASSERT_EQ(OB_SUCCESS, graph.deep_copy_column_metas(column_refs));
    parameters.set_original_param_cnt(4);
    ASSERT_EQ(OB_SUCCESS, parameters.init_datum_param_store());
    ASSERT_TRUE(parameters.native_parameters_ready());
    ASSERT_NE(nullptr, parameters.get_native_parameter_type(0));
    ASSERT_NE(nullptr, parameters.get_native_parameter_type(3));
    bool valid = false;
    ASSERT_EQ(OB_SUCCESS,
              ObRangeGenerator::check_can_final_fast_nlj_range(graph, parameters, valid));
    EXPECT_TRUE(valid);
    ASSERT_EQ(OB_SUCCESS,
              ObRangeGenerator::generate_fast_nlj_range(graph, parameters, output, range));
    memset(text, 'x', sizeof(text) - 1);
  }
  const DatumValue *row = range->start_key_.get_values();
  EXPECT_EQ(INT64_MIN, row[0].get_int());
  EXPECT_EQ(UINT64_MAX, row[1].get_uint64());
  EXPECT_EQ(ObString("客户-key"), row[2].get_string());
  EXPECT_EQ(1792702301123456LL, row[3].get_datetime());
  EXPECT_TRUE(range->border_flag_.inclusive_start());
  EXPECT_TRUE(range->border_flag_.inclusive_end());
}
TEST(RangeMapDatum, native_fast_parameter_bounds_preserve_null_rules_and_reject_missing_slots) {
  ObArenaAllocator arena;
  ObPreRangeGraph graph(arena);
  ObRangeNode node(arena);
  int64_t key = 0;
  node.column_cnt_ = 1;
  node.start_keys_ = &key;
  node.end_keys_ = &key;
  graph.set_range_head(&node);
  ASSERT_EQ(OB_SUCCESS, graph.get_range_map().expr_final_infos_.prepare_allocate(1));
  auto &entry = graph.get_range_map().expr_final_infos_.at(0);
  entry.is_param_ = true;
  entry.param_idx_ = 0;
  ObRangeColumnMeta column;
  column.column_type_.set_int();
  ObSEArray<ObRangeColumnMeta *, 1> columns;
  ASSERT_EQ(OB_SUCCESS, columns.push_back(&column));
  ASSERT_EQ(OB_SUCCESS, graph.deep_copy_column_metas(columns));
  char *memory = static_cast<char *>(arena.alloc(sizeof(ObNewRange) + 2 * sizeof(DatumValue)));
  ASSERT_NE(nullptr, memory);
  auto *range = new (memory) ObNewRange();
  new (memory + sizeof(ObNewRange)) DatumValue();
  new (memory + sizeof(ObNewRange) + sizeof(DatumValue)) DatumValue();
  ObExecContext execution(arena);
  ASSERT_EQ(OB_SUCCESS, execution.create_physical_plan_ctx());
  ObPhysicalPlanCtx &parameters = *execution.get_physical_plan_ctx();
  bool valid = true;
  EXPECT_EQ(OB_ERR_UNEXPECTED,
            ObRangeGenerator::check_can_final_fast_nlj_range(graph, parameters, valid));
  EXPECT_EQ(OB_ERR_UNEXPECTED,
            ObRangeGenerator::generate_fast_nlj_range(graph, parameters, arena, range));
  DatumParameter parameter;
  SqlTypeMeta integer_declaration; integer_declaration.set_int();
  parameter.set_param_meta(integer_declaration);
  parameter.set_accuracy(ObAccuracy(20, 0));
  ASSERT_EQ(OB_SUCCESS, parameters.append_parameter_input(parameter));
  parameters.set_original_param_cnt(1);
  ASSERT_EQ(OB_SUCCESS, parameters.init_datum_param_store());
  ASSERT_TRUE(parameters.native_parameters_ready());
  ASSERT_EQ(ObIntType, parameters.get_native_parameter_type(0)->kind());
  ASSERT_EQ(OB_SUCCESS, ObRangeGenerator::generate_fast_nlj_range(graph, parameters, arena, range));
  EXPECT_TRUE(range->start_key_.get_cell(0).is_max_value());
  EXPECT_TRUE(range->end_key_.get_cell(0).is_min_value());
  entry.null_safe_ = true;
  ASSERT_EQ(OB_SUCCESS, ObRangeGenerator::generate_fast_nlj_range(graph, parameters, arena, range));
  EXPECT_TRUE(range->start_key_.get_cell(0).is_null());
  EXPECT_TRUE(range->end_key_.get_cell(0).is_null());
  int32_t raw = 123;
  DatumParameter decimal;
  ASSERT_EQ(OB_SUCCESS, decimal.datum_.set_reference(&raw, sizeof(raw)));
  decimal.get_meta().set_decimal_int(2);
  decimal.set_precision(38); decimal.set_scale(2);
  SqlType decimal_declaration;
  ASSERT_EQ(OB_SUCCESS, SqlType::from_legacy(decimal.get_type_meta(),
      decimal.get_accuracy(), decimal_declaration));
  alignas(8) char decimal_scratch[64];
  ObDatum decimal_view; Datum native_decimal;
  ASSERT_EQ(OB_SUCCESS, decimal.export_legacy(decimal_view, decimal_scratch, sizeof(decimal_scratch)));
  ASSERT_EQ(OB_SUCCESS, DatumCopyPlan(decimal_declaration).import_legacy(decimal_view, native_decimal));
  ASSERT_EQ(OB_SUCCESS, parameters.set_runtime_parameter(0, native_decimal,
      decimal_declaration, decimal.get_metadata()));
  column.column_type_.set_type(ObDecimalIntType);
  column.column_type_.set_scale(2);
  column.column_type_.set_precision(38);
  ASSERT_EQ(OB_SUCCESS, graph.deep_copy_column_metas(columns));
  ASSERT_EQ(OB_SUCCESS, ObRangeGenerator::check_can_final_fast_nlj_range(graph, parameters, valid));
  EXPECT_FALSE(valid);
}

TEST(RangeMapDatum, retained_constant_scalars_and_states_need_no_payload_allocation) {
  CountingAllocator owner;
  owner.reject = true;
  Datum values[10];
  SqlTypeMeta metas[10];
  values[0].set_int(INT64_MIN); metas[0].set_int();
  values[1].set_uint(UINT64_MAX); metas[1].set_uint64();
  values[2].set_double(1.5); metas[2].set_double();
  values[3].set_float(-2.5f); metas[3].set_float();
  values[4].set_date(-123); metas[4].set_date();
  values[5].set_year(255); metas[5].set_year();
  values[6].set_null(); metas[6].set_decimal_int(7);
  values[7].set_min(); metas[7].set_ext();
  values[8].set_max(); metas[8].set_ext();
  values[9].set_nop(); metas[9].set_ext();
  ObRangeMap::ExprFinalInfo retained[10];
  for (int i = 0; i < 10; ++i) {
    retained[i].is_param_ = true;
    retained[i].param_idx_ = 17;
    retained[i].null_safe_ = true;
    retained[i].cnt_exec_param_ = true;
    ASSERT_EQ(OB_SUCCESS, retained[i].set_const_value(values[i], metas[i], owner));
    EXPECT_TRUE(retained[i].is_const_);
    EXPECT_FALSE(retained[i].is_param_);
    EXPECT_FALSE(retained[i].is_expr_);
    EXPECT_TRUE(retained[i].null_safe_);
    EXPECT_TRUE(retained[i].cnt_exec_param_);
    EXPECT_EQ(metas[i], retained[i].const_meta_);
    EXPECT_EQ(0, retained[i].const_val_.length());
    values[i].set_null();
  }
  EXPECT_EQ(0, owner.calls);
  EXPECT_EQ(0, owner.bytes);
  EXPECT_EQ(INT64_MIN, retained[0].const_val_.get_int());
  EXPECT_EQ(UINT64_MAX, retained[1].const_val_.get_uint());
  EXPECT_DOUBLE_EQ(1.5, retained[2].const_val_.get_double());
  EXPECT_FLOAT_EQ(-2.5f, retained[3].const_val_.get_float());
  EXPECT_EQ(-123, retained[4].const_val_.get_date());
  EXPECT_EQ(255, retained[5].const_val_.get_year());
  EXPECT_TRUE(retained[6].const_val_.is_null());
  EXPECT_EQ(ObDecimalIntType, retained[6].const_meta_.get_type());
  EXPECT_EQ(7, retained[6].const_meta_.get_scale());
  EXPECT_TRUE(retained[7].const_val_.is_min());
  EXPECT_TRUE(retained[8].const_val_.is_max());
  EXPECT_TRUE(retained[9].const_val_.is_nop());

  ObDatum unchanged;
  unchanged.set_null();
  alignas(8) char short_scratch[7];
  EXPECT_EQ(OB_BUF_NOT_ENOUGH,
            retained[0].export_const_value(unchanged, short_scratch, sizeof(short_scratch)));
  EXPECT_TRUE(unchanged.is_null());
}

TEST(RangeMapDatum, retained_reference_failure_preserves_slot_and_retry_owns_bytes) {
  CountingAllocator owner;
  ObRangeMap::ExprFinalInfo retained;
  Datum scalar;
  scalar.set_int(77);
  SqlTypeMeta scalar_meta;
  scalar_meta.set_int();
  ASSERT_EQ(OB_SUCCESS, retained.set_const_value(scalar, scalar_meta, owner));
  retained.null_safe_ = true;
  const auto before = retained;
  char text[] = "payload";
  Datum source;
  ASSERT_EQ(OB_SUCCESS, source.set_string(text, sizeof(text) - 1));
  SqlTypeMeta text_meta;
  text_meta.set_varchar();
  text_meta.set_collation_type(CS_TYPE_UTF8MB4_BIN);
  owner.reject = true;
  EXPECT_EQ(OB_ALLOCATE_MEMORY_FAILED, retained.set_const_value(source, text_meta, owner));
  EXPECT_EQ(1, owner.calls);
  EXPECT_EQ(sizeof(text) - 1, owner.bytes);
  EXPECT_EQ(before.flags_, retained.flags_);
  EXPECT_EQ(before.const_meta_, retained.const_meta_);
  EXPECT_EQ(0, MEMCMP(&before.const_val_, &retained.const_val_, sizeof(Datum)));
  EXPECT_EQ(77, retained.const_val_.get_int());

  // An illegal scalar/reference pair is rejected before allocation/publication.
  EXPECT_EQ(OB_INVALID_ARGUMENT, retained.set_const_value(source, scalar_meta, owner));
  EXPECT_EQ(1, owner.calls);
  EXPECT_EQ(before.flags_, retained.flags_);
  EXPECT_EQ(before.const_meta_, retained.const_meta_);
  EXPECT_EQ(77, retained.const_val_.get_int());
  owner.reject = false;
  ASSERT_EQ(OB_SUCCESS, retained.set_const_value(source, text_meta, owner));
  EXPECT_EQ(2, owner.calls);
  EXPECT_NE(static_cast<const void *>(text), retained.const_val_.get_reference());
  memset(text, 'x', sizeof(text) - 1);
  EXPECT_EQ(ObString("payload"), retained.const_val_.get_string());
  const void *first_copy = retained.const_val_.get_reference();
  ASSERT_EQ(OB_SUCCESS, retained.set_const_value(retained.const_val_, retained.const_meta_, owner));
  EXPECT_EQ(3, owner.calls);
  EXPECT_NE(first_copy, retained.const_val_.get_reference());
  EXPECT_TRUE(retained.null_safe_);
  EXPECT_EQ(ObString("payload"), retained.const_val_.get_string());
}

TEST(RangeMapDatum, retained_decimal_actual_width_and_unknown_reference_keep_captured_wire) {
  int widths = 0;
  bool unknown_checked = false;
  for (auto frame : frames()) {
    const std::string prefix = "value-decimal-width-";
    if (frame.name.compare(0, prefix.size(), prefix) != 0 && frame.name != "value-type-26") { continue; }
    SCOPED_TRACE(frame.name);
    const bool unknown = frame.name == "value-type-26";
    const int width = unknown ? 8 : std::stoi(frame.name.substr(prefix.size()));
    CountingAllocator payload_owner;
    ObArenaAllocator entries_owner;
    ObRangeMap retained(entries_owner);
    ASSERT_EQ(OB_SUCCESS, retained.expr_final_infos_.prepare_allocate(1));
    auto &copy = retained.expr_final_infos_.at(0);
    {
      ObArenaAllocator source_owner;
      ObRangeMap source(source_owner);
      int64_t pos = 0;
      ASSERT_EQ(OB_SUCCESS, source.deserialize(frame.wire.data(), frame.wire.size(), pos));
      const auto &entry = source.expr_final_infos_.at(0);
      ASSERT_EQ(OB_SUCCESS, copy.set_const_value(entry.const_val_, entry.const_meta_, payload_owner));
      copy.flags_ = entry.flags_;
      EXPECT_EQ(width, copy.const_val_.length());
      EXPECT_EQ(1, payload_owner.calls);
      EXPECT_EQ(width == 0 ? 1 : width, payload_owner.bytes);
      if (width > 0) { EXPECT_NE(entry.const_val_.get_reference(), copy.const_val_.get_reference()); }
      if (unknown) {
        EXPECT_EQ(ObUnknownType, copy.const_meta_.get_type());
        EXPECT_EQ(-7, copy.const_val_.get_unknown());
        unknown_checked = true;
      } else {
        EXPECT_EQ(ObDecimalIntType, copy.const_meta_.get_type());
        EXPECT_EQ(-9, copy.const_meta_.get_scale());
        ++widths;
      }
    }
    memset(&frame.wire[0], 0xa5, frame.wire.size());
    EXPECT_EQ(frame.canonical, encode(retained));
  }
  EXPECT_EQ(6, widths);
  EXPECT_TRUE(unknown_checked);
}

TEST(RangeMapDatum, native_decode_has_no_scalar_payload_and_reference_oom_clears_map) {
  const auto corpus = frames();
  const RangeFrame *integer = nullptr, *text = nullptr;
  for (const auto &frame : corpus) {
    if (frame.name == "value-type-5") { integer = &frame; }
    if (frame.name == "value-type-22") { text = &frame; }
  }
  ASSERT_NE(nullptr, integer);
  ASSERT_NE(nullptr, text);
  CountingAllocator scalar_owner;
  scalar_owner.reject_at = 2;
  ObRangeMap scalar_map(scalar_owner);
  int64_t pos = 0;
  ASSERT_EQ(OB_SUCCESS, scalar_map.deserialize(integer->wire.data(), integer->wire.size(), pos));
  EXPECT_EQ(integer->wire.size(), pos);
  ASSERT_EQ(1, scalar_map.expr_final_infos_.count());
  EXPECT_EQ(-7, scalar_map.expr_final_infos_.at(0).const_val_.get_int());
  EXPECT_EQ(1, scalar_owner.calls); // only the actual ExprFinalInfo array
  EXPECT_EQ(sizeof(ObRangeMap::ExprFinalInfo), scalar_owner.bytes);
  EXPECT_EQ(integer->canonical, encode(scalar_map));

  CountingAllocator reference_owner;
  reference_owner.reject_at = 2; // entry allocation succeeds; actual string payload fails
  ObRangeMap reference_map(reference_owner);
  pos = 0;
  EXPECT_EQ(OB_ALLOCATE_MEMORY_FAILED,
            reference_map.deserialize(text->wire.data(), text->wire.size(), pos));
  EXPECT_EQ(2, reference_owner.calls);
  EXPECT_EQ(0, reference_map.expr_final_infos_.count());
  EXPECT_EQ(0, reference_map.in_params_.count());
  reference_owner.reject_at = -1;
  pos = 0;
  ASSERT_EQ(OB_SUCCESS, reference_map.deserialize(text->wire.data(), text->wire.size(), pos));
  EXPECT_EQ(text->wire.size(), pos);
  EXPECT_EQ(text->canonical, encode(reference_map));

  // The owner retains typed NULL metadata; historical wire deliberately
  // normalizes only its value type/collation, retaining the old scale byte.
  ObArenaAllocator null_owner;
  ObRangeMap null_map(null_owner), decoded(null_owner);
  ASSERT_EQ(OB_SUCCESS, null_map.expr_final_infos_.prepare_allocate(1));
  Datum null;
  SqlTypeMeta declared;
  declared.set_decimal_int(7);
  ASSERT_EQ(OB_SUCCESS, null_map.expr_final_infos_.at(0).set_const_value(null, declared, null_owner));
  std::string wire = encode(null_map);
  pos = 0;
  ASSERT_EQ(OB_SUCCESS, decoded.deserialize(wire.data(), wire.size(), pos));
  ASSERT_EQ(1, decoded.expr_final_infos_.count());
  SqlTypeMeta expected = declared;
  expected.set_null();
  EXPECT_EQ(declared, null_map.expr_final_infos_.at(0).const_meta_);
  EXPECT_EQ(expected, decoded.expr_final_infos_.at(0).const_meta_);
  EXPECT_TRUE(decoded.expr_final_infos_.at(0).const_val_.is_null());
  EXPECT_EQ(wire, encode(decoded));
}

int main(int argc, char **argv) {
  ::testing::InitGoogleTest(&argc, argv);
  return RUN_ALL_TESTS();
}
