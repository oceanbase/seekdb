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
#include "sql/engine/ob_physical_plan_ctx.h"
#include <fstream>
#include <gtest/gtest.h>
#include <sstream>
#include <string>
#include <vector>
using namespace oceanbase::common;
using namespace oceanbase::sql;
namespace {
struct ContextFrame {
  std::string name, input, canonical;
};
std::string unhex(const std::string &s) {
  std::string out;
  for (size_t i = 0; i < s.size(); i += 2) {
    out += char(std::stoul(s.substr(i, 2), nullptr, 16));
  }
  return out;
}
std::vector<ContextFrame> frames() {
  std::ifstream file("unittest/sql/data/physical_plan_context.golden");
  EXPECT_TRUE(file.good());
  std::vector<ContextFrame> out;
  std::string line;
  while (std::getline(file, line)) {
    if (line.empty() || line[0] == '#') {
      continue;
    }
    std::istringstream row(line);
    std::string name, input, canonical;
    std::getline(row, name, '\t');
    std::getline(row, input, '\t');
    std::getline(row, canonical);
    out.push_back({name, unhex(input), unhex(canonical)});
  }
  return out;
}
std::string encode(const ObPhysicalPlanCtx &ctx) {
  std::string out(ctx.get_serialize_size(), '\0');
  int64_t p = 0;
  EXPECT_EQ(OB_SUCCESS, ctx.serialize(&out[0], out.size(), p));
  EXPECT_EQ(out.size(), p);
  return out;
}
struct Offsets {
  int64_t dense, sparse_count, sparse_items, payload_length, payload_begin;
};
Offsets offsets(const std::string &wire, ObIAllocator &arena) {
  int64_t p = 0, version = 0, size = 0, number = 0;
  ObConsistencyLevel consistency;
  StatementTime clock;
  DatumParamStore params{ObWrapperAllocator(arena)};
  EXPECT_EQ(OB_SUCCESS,
            serialization::decode(wire.data(), wire.size(), p, version));
  const int64_t length_pos = p;
  EXPECT_EQ(OB_SUCCESS,
            serialization::decode(wire.data(), wire.size(), p, size));
  const int64_t begin = p;
  EXPECT_EQ(OB_SUCCESS,
            serialization::decode(wire.data(), wire.size(), p, number));
  EXPECT_EQ(OB_SUCCESS, clock.deserialize(wire.data(), wire.size(), p));
  EXPECT_EQ(OB_SUCCESS,
            serialization::decode(wire.data(), wire.size(), p, number));
  EXPECT_EQ(OB_SUCCESS,
            serialization::decode(wire.data(), wire.size(), p, consistency));
  const int64_t dense = p;
  EXPECT_EQ(OB_SUCCESS, params.deserialize(wire.data(), wire.size(), p));
  EXPECT_EQ(OB_SUCCESS,
            serialization::decode(wire.data(), wire.size(), p, number));
  EXPECT_EQ(0, number);
  uint64_t mode = 0;
  EXPECT_EQ(OB_SUCCESS,
            serialization::decode(wire.data(), wire.size(), p, mode));
  ObSEArray<oceanbase::share::AutoincParam, 1> autoinc;
  EXPECT_EQ(OB_SUCCESS, autoinc.deserialize(wire.data(), wire.size(), p));
  oceanbase::share::ObTabletAutoincParam tablet;
  EXPECT_EQ(OB_SUCCESS, tablet.deserialize(wire.data(), wire.size(), p));
  EXPECT_EQ(OB_SUCCESS,
            serialization::decode(wire.data(), wire.size(), p, number));
  char message[1] = {'\0'};
  EXPECT_EQ(OB_SUCCESS,
            serialization::decode(wire.data(), wire.size(), p, message));
  EXPECT_EQ(OB_SUCCESS,
            serialization::decode(wire.data(), wire.size(), p, number));
  bool ignored = false;
  EXPECT_EQ(OB_SUCCESS,
            serialization::decode(wire.data(), wire.size(), p, ignored));
  const int64_t sparse = p;
  EXPECT_EQ(OB_SUCCESS,
            serialization::decode(wire.data(), wire.size(), p, number));
  return {dense, sparse, p, length_pos, begin};
}
std::string replace_tail(const std::string &wire, int64_t begin, int64_t end,
                         const std::string &replacement, const Offsets &at) {
  std::string out = wire.substr(0, begin) + replacement + wire.substr(end);
  int64_t p = at.payload_length;
  EXPECT_EQ(OB_SUCCESS,
            serialization::encode_fixed_bytes_i64(
                &out[0], out.size(), p, out.size() - at.payload_begin));
  return out;
}
std::string varint(int64_t n) {
  char bytes[16];
  int64_t p = 0;
  EXPECT_EQ(OB_SUCCESS, serialization::encode_vi64(bytes, sizeof(bytes), p, n));
  return std::string(bytes, p);
}
} // namespace
TEST(ContextDatumParameters,
     real_old_context_frames_decode_to_owned_native_store) {
  auto data = frames();
  ASSERT_EQ(161, data.size());
  for (auto frame : data) {
    SCOPED_TRACE(frame.name);
    ObArenaAllocator arena;
    ObPhysicalPlanCtx ctx(arena);
    int64_t p = 0;
    ASSERT_EQ(OB_SUCCESS,
              ctx.deserialize(frame.input.data(), frame.input.size(), p));
    EXPECT_EQ(frame.input.size(), p);
    EXPECT_EQ(frame.canonical, encode(ctx));
    ASSERT_EQ(ctx.get_param_store().count(),
              ctx.get_datum_param_store().count());
    memset(&frame.input[0], 0xa5, frame.input.size());
    if (frame.name == "dense-mixed") {
      ASSERT_EQ(5, ctx.get_datum_param_store().count());
      EXPECT_EQ(INT64_MIN, ctx.get_datum_param_store().at(0).datum_.get_int());
      EXPECT_EQ(UINT64_MAX,
                ctx.get_datum_param_store().at(1).datum_.get_uint());
      EXPECT_EQ(ObString(10, "a\0中文  "),
                ctx.get_datum_param_store().at(2).datum_.get_string());
      EXPECT_EQ(ctx.get_param_store().at(2).get_string().ptr(),
                ctx.get_datum_param_store().at(2).datum_.ptr_);
      EXPECT_TRUE(ctx.get_datum_param_store().at(4).is_null());
    }
    if (frame.name == "nested-array") {
      auto *outer =
          ctx.get_datum_param_store().at(0).get_array<DatumParameterArray>();
      ASSERT_NE(nullptr, outer);
      auto *inner = outer->data_[0].get_array<DatumParameterArray>();
      ASSERT_NE(nullptr, inner);
      EXPECT_EQ(42, inner->data_[0].datum_.get_int());
      EXPECT_EQ(ObString("nested bytes"), inner->data_[1].datum_.get_string());
      EXPECT_EQ(reinterpret_cast<int64_t>(outer),
                ctx.get_param_store().at(0).get_ext());
    }
    if (frame.name == "sparse") {
      ASSERT_EQ(5, ctx.get_datum_param_store().count());
      for (int64_t i = 0; i < 5; ++i) {
        if (i % 2) {
          EXPECT_TRUE(ctx.get_datum_param_store().at(i).is_null());
        } else {
          EXPECT_EQ(100 + i,
                    ctx.get_datum_param_store().at(i).datum_.get_int());
        }
      }
    }
    EXPECT_EQ(frame.canonical, encode(ctx));
  }
}
TEST(ContextDatumParameters,
     numeric_and_declared_null_metadata_match_existing_typing_rules) {
  // Expected values are the pre-migration protocol typing rules, not a second
  // invocation of the same native implementation used as an alleged oracle.
  struct Case { SqlValueType type; int precision; bool integer; };
  const Case cases[] = {{ObTinyIntType, 4, true}, {ObSmallIntType, 6, true},
      {ObMediumIntType, 9, true}, {ObInt32Type, 11, true}, {ObIntType, 20, true},
      {ObUTinyIntType, 3, true}, {ObUSmallIntType, 5, true},
      {ObUMediumIntType, 8, true}, {ObUInt32Type, 10, true}, {ObUInt64Type, 20, true},
      {ObNumberType, 10, false}, {ObUNumberType, 10, false}, {ObDecimalIntType, 10, false}};
  for (const Case &c : cases) {
    for (int meta_scale : {-1, 0, 6}) {
      for (int accuracy_scale : {-1, 0, 3}) {
        SCOPED_TRACE(c.type);
        DatumParameter value;
        SqlTypeMeta meta; meta.set_type(c.type); meta.set_scale(meta_scale);
        value.set_type_meta(meta);
        ObAccuracy accuracy; accuracy.set_precision(PRECISION_UNKNOWN_YET);
        accuracy.set_scale(accuracy_scale); value.set_accuracy(accuracy);
        value.set_param_meta();
        EXPECT_EQ(c.precision, value.get_precision());
        const int expected_scale = accuracy_scale >= 0 ? accuracy_scale
            : meta_scale >= 0 ? meta_scale : c.integer ? 0 : -1;
        EXPECT_EQ(expected_scale, value.get_scale());
        // Declared metadata is captured before applying numeric defaults.
        EXPECT_EQ(meta, value.get_param_meta());
        value.set_precision(31); value.set_param_meta();
        EXPECT_EQ(31, value.get_precision());
      }
    }
  }
  DatumParameter null_value;
  SqlTypeMeta declared; declared.set_varchar(); null_value.set_param_meta(declared);
  SqlTypeMeta null_meta; null_meta.set_null(); null_value.set_param_meta(null_meta);
  null_value.set_null(); null_value.set_param_meta();
  EXPECT_EQ(declared, null_value.get_param_meta());
}
TEST(ContextDatumParameters,
     all_old_frame_truncated_prefixes_fail_without_exposing_parameters) {
  for (const auto &frame : frames()) {
    SCOPED_TRACE(frame.name);
    for (size_t n = 0; n < frame.input.size(); ++n) {
      ObArenaAllocator arena;
      ObPhysicalPlanCtx ctx(arena);
      int64_t p = 0;
      EXPECT_NE(OB_SUCCESS, ctx.deserialize(frame.input.data(), n, p));
      EXPECT_EQ(0, ctx.get_param_store().count());
      EXPECT_EQ(0, ctx.get_datum_param_store().count());
    }
  }
}
TEST(ContextDatumParameters,
     malformed_dense_and_sparse_counts_and_indexes_are_rejected) {
  for (const auto &frame : frames()) {
    if (frame.name != "sparse" && frame.name != "dense-mixed") {
      continue;
    }
    ObArenaAllocator scratch;
    const auto at = offsets(frame.input, scratch);
    std::vector<std::string> bad;
    bad.push_back(replace_tail(frame.input, at.dense, at.dense + 1,
                               varint(INT64_MAX), at));
    bad.push_back(
        replace_tail(frame.input, at.dense, at.dense + 1, varint(-1), at));
    if (frame.name == "sparse") {
      for (int64_t count : {int64_t(-1), INT64_MAX}) {
        bad.push_back(replace_tail(frame.input, at.sparse_count,
                                   at.sparse_items, varint(count), at));
      }
      std::string negative = frame.input;
      int64_t p = at.sparse_items;
      EXPECT_EQ(OB_SUCCESS, serialization::encode_i32(&negative[0],
                                                      negative.size(), p, -1));
      bad.push_back(negative);
      // First entry follows the fixed four-byte selected count. Replace its
      // slot.
      bad.push_back(replace_tail(frame.input, at.sparse_items + 4,
                                 at.sparse_items + 5, varint(5), at));
    }
    for (auto &wire : bad) {
      ObArenaAllocator arena;
      ObPhysicalPlanCtx ctx(arena);
      int64_t p = 0;
      EXPECT_NE(OB_SUCCESS, ctx.deserialize(wire.data(), wire.size(), p));
      EXPECT_TRUE(ctx.get_param_store().empty());
      EXPECT_TRUE(ctx.get_datum_param_store().empty());
    }
  }
}
TEST(ContextDatumParameters, sparse_duplicate_indexes_preserve_last_value) {
  for (const auto &frame : frames()) {
    if (frame.name != "sparse") {
      continue;
    }
    ObArenaAllocator arena;
    const auto at = offsets(frame.input, arena);
    std::string wire = frame.input;
    int64_t p = at.sparse_items;
    int32_t count = 0;
    ASSERT_EQ(OB_SUCCESS,
              serialization::decode_i32(wire.data(), wire.size(), p, &count));
    ASSERT_EQ(3, count);
    int64_t index = 0;
    DatumParameter value;
    ASSERT_EQ(OB_SUCCESS,
              serialization::decode(wire.data(), wire.size(), p, index));
    ASSERT_EQ(OB_SUCCESS, DatumParameterCodec::decode_parameter(
                              wire.data(), wire.size(), p, arena, value));
    // The second value (102) overwrites first slot 0; slot 2 stays NULL.
    wire[p] = 0;
    ObPhysicalPlanCtx ctx(arena);
    p = 0;
    ASSERT_EQ(OB_SUCCESS, ctx.deserialize(wire.data(), wire.size(), p));
    EXPECT_EQ(102, ctx.get_datum_param_store().at(0).datum_.get_int());
    EXPECT_TRUE(ctx.get_datum_param_store().at(2).is_null());
    EXPECT_EQ(104, ctx.get_datum_param_store().at(4).datum_.get_int());
  }
}

TEST(ContextDatumParameters,
     null_compatibility_view_keeps_exact_wire_metadata) {
  for (bool explicit_null : {false, true}) {
    DatumParameter source;
    SqlTypeMeta meta;
    if (explicit_null) {
      meta.set_null();
    }
    source.set_type_meta(meta);
    DatumParameter output;
    ASSERT_EQ(OB_SUCCESS, assign_datum_parameter(source, output));
    EXPECT_TRUE(output.is_null());
    EXPECT_EQ(0, memcmp(&meta, &output.get_meta(), sizeof(meta)));
  }
}

TEST(ContextDatumParameters,
     native_arrays_are_owned_without_a_deferred_group_marker) {
  bool covered = false;
  for (const auto &frame : frames()) {
    if (frame.name != "nested-array") {
      continue;
    }
    covered = true;
    ObArenaAllocator source_arena, output_arena;
    ObPhysicalPlanCtx source(source_arena), output(output_arena);
    int64_t pos = 0;
    ASSERT_EQ(OB_SUCCESS,
              source.deserialize(frame.input.data(), frame.input.size(), pos));
    source.get_array_param_groups().reset();
    std::string wire = encode(source);
    pos = 0;
    ASSERT_EQ(OB_SUCCESS, output.deserialize(wire.data(), wire.size(), pos));
    ASSERT_EQ(1, output.get_datum_param_store().count());
    EXPECT_EQ(0, output.get_array_param_groups().count());
    auto *outer =
        output.get_datum_param_store().at(0).get_array<DatumParameterArray>();
    ASSERT_NE(nullptr, outer);
    EXPECT_NE(
        source.get_datum_param_store().at(0).get_array<DatumParameterArray>(),
        outer);
    memset(&wire[0], 0xa5, wire.size());
    auto *inner = outer->data_[0].get_array<DatumParameterArray>();
    ASSERT_NE(nullptr, inner);
    EXPECT_EQ(42, inner->data_[0].datum_.get_int());
    EXPECT_EQ(ObString("nested bytes"), inner->data_[1].datum_.get_string());
  }
  ASSERT_TRUE(covered);
}
namespace {
class RejectContextAllocator : public ObIAllocator {
public:
  int calls = 0;
  void *alloc(int64_t) override {
    ++calls;
    return nullptr;
  }
  void *alloc(int64_t, const ObMemAttr &) override {
    ++calls;
    return nullptr;
  }
  void free(void *) override {}
};
} // namespace
TEST(ContextDatumParameters,
     allocation_failure_exposes_neither_parameter_store) {
  bool covered = false;
  for (const auto &frame : frames()) {
    if (frame.name != "dense-mixed") {
      continue;
    }
    covered = true;
    RejectContextAllocator arena;
    ObPhysicalPlanCtx ctx(arena);
    int64_t pos = 0;
    EXPECT_EQ(OB_ALLOCATE_MEMORY_FAILED,
              ctx.deserialize(frame.input.data(), frame.input.size(), pos));
    EXPECT_GT(arena.calls, 0);
    EXPECT_TRUE(ctx.get_param_store().empty());
    EXPECT_TRUE(ctx.get_datum_param_store().empty());
  }
  ASSERT_TRUE(covered);
}

TEST(ContextDatumParameters,
     raw_decimal_parameter_scale_survives_compatibility_view) {
  int32_t value = -1234;
  DatumParameter parameter;
  SqlTypeMeta meta;
  meta.set_decimal_int(-9);
  parameter.set_type_meta(meta);
  parameter.datum_.set_string(reinterpret_cast<const char *>(&value),
                              sizeof(value));
  DatumParameter output;
  ASSERT_EQ(OB_SUCCESS, assign_datum_parameter(parameter, output));
  EXPECT_EQ(parameter.get_scale(), output.get_scale());
  EXPECT_EQ(-9, output.get_meta().get_scale());
  EXPECT_EQ(meta, output.get_meta());
  EXPECT_EQ(sizeof(value), output.get_int_bytes());
  EXPECT_EQ(0, memcmp(&value, output.get_decimal_int(), sizeof(value)));
}
