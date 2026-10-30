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

#include <gtest/gtest.h>
#include <vector>
#include "sql/engine/ob_physical_plan_ctx.h"
#include "query/engine/expr/expr_extra_serialize_info.h"
using namespace oceanbase::common;
using namespace oceanbase::sql;
TEST(StatementTimeContext, execution_context_roundtrips_null_and_present_clock) {
  for (bool present : {false, true}) {
    ObArenaAllocator arena; ObPhysicalPlanCtx source(arena), output(arena);
    if (present) { source.set_cur_time(1234567890123456LL); }
    std::vector<char> bytes(source.get_serialize_size()); int64_t pos = 0;
    ASSERT_EQ(OB_SUCCESS, source.serialize(bytes.data(), bytes.size(), pos)); EXPECT_EQ(bytes.size(), pos);
    int64_t read = 0; ASSERT_EQ(OB_SUCCESS, output.deserialize(bytes.data(), bytes.size(), read)); EXPECT_EQ(pos, read);
    EXPECT_EQ(present, output.has_cur_time()); EXPECT_EQ(source.get_cur_time().get_timestamp(), output.get_cur_time().get_timestamp());
    EXPECT_EQ(source.get_cur_time_tardy_value(), output.get_cur_time_tardy_value());
    std::vector<char> repeated(output.get_serialize_size()); pos = 0;
    ASSERT_EQ(OB_SUCCESS, output.serialize(repeated.data(), repeated.size(), pos)); EXPECT_EQ(bytes, repeated);
  }
}
TEST(StatementTimeContext, px_expression_extra_info_roundtrips_clock_and_trace) {
  StatementTime source_time, output_time; source_time.set_timestamp(1234567890123456LL);
  ObCurTraceId::TraceId source_trace, output_trace;
  ObExprExtraSerializeInfo source, output;
  source.current_time_ = &source_time; source.last_trace_id_ = &source_trace;
  output.current_time_ = &output_time; output.last_trace_id_ = &output_trace;
  std::vector<char> bytes(source.get_serialize_size()); int64_t pos = 0;
  ASSERT_EQ(OB_SUCCESS, source.serialize(bytes.data(), bytes.size(), pos)); EXPECT_EQ(bytes.size(), pos);
  int64_t read = 0; ASSERT_EQ(OB_SUCCESS, output.deserialize(bytes.data(), bytes.size(), read)); EXPECT_EQ(pos, read);
  EXPECT_EQ(source_time.get_timestamp(), output_time.get_timestamp()); EXPECT_TRUE(output_time.has_value());
  std::vector<char> repeated(output.get_serialize_size()); pos = 0;
  ASSERT_EQ(OB_SUCCESS, output.serialize(repeated.data(), repeated.size(), pos)); EXPECT_EQ(bytes, repeated);
}
TEST(StatementTimeContext, px_frame_keeps_legacy_clock_wire_and_member_order) {
  const unsigned char null_wire[] = {0, 7, 0, 255}, timestamp_wire[] = {18, 5, 63, 255, 42};
  for (bool present : {false, true}) {
    StatementTime clock; if (present) { clock.set_timestamp(42); }
    ObCurTraceId::TraceId trace; ObExprExtraSerializeInfo info; info.current_time_ = &clock; info.last_trace_id_ = &trace;
    const int64_t clock_size = present ? sizeof(timestamp_wire) : sizeof(null_wire);
    const int64_t payload_size = clock_size + trace.get_serialize_size();
    std::vector<char> expected(info.get_serialize_size()), actual(info.get_serialize_size()); int64_t pos = 0;
    ASSERT_EQ(OB_SUCCESS, serialization::encode_vi64(expected.data(), expected.size(), pos, 1));
    ASSERT_EQ(OB_SUCCESS, serialization::encode_fixed_bytes_i64(expected.data(), expected.size(), pos, payload_size));
    memcpy(expected.data() + pos, present ? timestamp_wire : null_wire, clock_size); pos += clock_size;
    ASSERT_EQ(OB_SUCCESS, trace.serialize(expected.data(), expected.size(), pos)); EXPECT_EQ(expected.size(), pos);
    pos = 0; ASSERT_EQ(OB_SUCCESS, info.serialize(actual.data(), actual.size(), pos)); EXPECT_EQ(expected, actual);
  }
}
TEST(StatementTimeContext, consumes_nonempty_obsolete_value_array_without_legacy_objects) {
  ObArenaAllocator arena; ObPhysicalPlanCtx source(arena); source.set_cur_time(42);
  std::vector<char> original(source.get_serialize_size()); int64_t pos = 0;
  ASSERT_EQ(OB_SUCCESS, source.serialize(original.data(), original.size(), pos));
  int64_t cursor = 0, version = 0, payload_size = 0;
  ASSERT_EQ(OB_SUCCESS, serialization::decode(original.data(), original.size(), cursor, version));
  const int64_t length_offset = cursor;
  ASSERT_EQ(OB_SUCCESS, serialization::decode(original.data(), original.size(), cursor, payload_size));
  int64_t snapshot = 0, timeout = 0;
  ObConsistencyLevel consistency; StatementTime clock; DatumParamStore parameters{ObWrapperAllocator(arena)};
  ASSERT_EQ(OB_SUCCESS, serialization::decode(original.data(), original.size(), cursor, snapshot));
  ASSERT_EQ(OB_SUCCESS, clock.deserialize(original.data(), original.size(), cursor));
  ASSERT_EQ(OB_SUCCESS, serialization::decode(original.data(), original.size(), cursor, timeout));
  ASSERT_EQ(OB_SUCCESS, serialization::decode(original.data(), original.size(), cursor, consistency));
  ASSERT_EQ(OB_SUCCESS, parameters.deserialize(original.data(), original.size(), cursor));
  const int64_t begin = cursor; int64_t old_count = -1;
  ASSERT_EQ(OB_SUCCESS, serialization::decode(original.data(), original.size(), cursor, old_count)); ASSERT_EQ(0, old_count);
  // Historical slot: vi64 count, followed by bare legacy value wire (not parameter wire).
  const unsigned char values[] = {2, 5, 5, 63, 255, 42, 0, 7, 0, 255};
  for (bool valid : {true, false}) {
    std::vector<char> replacement;
    if (valid) { replacement.assign(reinterpret_cast<const char *>(values), reinterpret_cast<const char *>(values) + sizeof(values)); }
    else { replacement.resize(16); int64_t n = 0; ASSERT_EQ(OB_SUCCESS, serialization::encode_vi64(replacement.data(), replacement.size(), n, INT64_MAX)); replacement.resize(n); }
    std::vector<char> bytes; bytes.insert(bytes.end(), original.begin(), original.begin() + begin); bytes.insert(bytes.end(), replacement.begin(), replacement.end()); bytes.insert(bytes.end(), original.begin() + cursor, original.end());
    const int64_t changed_length = payload_size + replacement.size() - (cursor - begin); int64_t length_pos = length_offset;
    ASSERT_EQ(OB_SUCCESS, serialization::encode_fixed_bytes_i64(bytes.data(), bytes.size(), length_pos, changed_length));
    ObPhysicalPlanCtx output(arena); int64_t read = 0;
    if (valid) {
      ASSERT_EQ(OB_SUCCESS, output.deserialize(bytes.data(), bytes.size(), read)); EXPECT_EQ(bytes.size(), read); EXPECT_EQ(42, output.get_cur_time().get_timestamp());
      std::vector<char> canonical(output.get_serialize_size()); read = 0; ASSERT_EQ(OB_SUCCESS, output.serialize(canonical.data(), canonical.size(), read)); EXPECT_EQ(original, canonical);
    } else { EXPECT_EQ(OB_DESERIALIZE_ERROR, output.deserialize(bytes.data(), bytes.size(), read)); }
  }
}

int main(int argc, char **argv) { ::testing::InitGoogleTest(&argc, argv); return RUN_ALL_TESTS(); }
