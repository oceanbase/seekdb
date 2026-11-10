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

#include "query/engine/statement_time.h"
#ifdef OCEANBASE_COMMON_OB_OBJECT_H_
#error "Statement clock must not need the general-purpose legacy value"
#endif
#include <gtest/gtest.h>
#include <cstring>
#include <vector>
using namespace oceanbase::common;
using namespace oceanbase::sql;
static_assert(sizeof(StatementTime) == 16, "The fixed statement clock must keep the old field footprint");
TEST(StatementTime, initial_null_and_timestamp_keep_captured_value_wire) {
  StatementTime clock;
  EXPECT_FALSE(clock.has_value()); EXPECT_EQ(0, clock.get_timestamp());
  const unsigned char null_wire[] = {0, 7, 0, 255};
  const unsigned char timestamp_wire[] = {18, 5, 63, 255, 42};
  char bytes[32]; int64_t pos = 0;
  ASSERT_EQ(OB_SUCCESS, clock.serialize(bytes, sizeof(bytes), pos)); EXPECT_EQ(4, pos); EXPECT_EQ(0, memcmp(bytes, null_wire, 4));
  clock.set_timestamp(42); EXPECT_TRUE(clock.has_value()); EXPECT_EQ(42, clock.get_datetime());
  pos = 0; ASSERT_EQ(OB_SUCCESS, clock.serialize(bytes, sizeof(bytes), pos)); EXPECT_EQ(5, pos); EXPECT_EQ(0, memcmp(bytes, timestamp_wire, 5));
}
TEST(StatementTime, scalar_copy_roundtrip_and_nonzero_position) {
  for (int64_t timestamp : {INT64_MIN, int64_t(-7), int64_t(0), int64_t(42), INT64_MAX}) {
    StatementTime clock; clock.set_timestamp(timestamp); StatementTime copy(clock); clock.set_timestamp(99);
    EXPECT_EQ(timestamp, copy.get_timestamp());
    const int64_t size = copy.get_serialize_size(); std::vector<char> bytes(size + 4, 'x'); int64_t pos = 2;
    ASSERT_EQ(OB_SUCCESS, copy.serialize(bytes.data(), bytes.size(), pos)); EXPECT_EQ(size + 2, pos);
    StatementTime output; pos = 2; ASSERT_EQ(OB_SUCCESS, output.deserialize(bytes.data(), bytes.size(), pos));
    EXPECT_EQ(size + 2, pos); EXPECT_TRUE(output.has_value()); EXPECT_EQ(timestamp, output.get_timestamp());
    EXPECT_EQ('x', bytes.front()); EXPECT_EQ('x', bytes.back());
  }
}
TEST(StatementTime, null_decode_keeps_previous_scalar_as_legacy_null_decoder_did) {
  StatementTime clock; clock.set_timestamp(99); const char bytes[] = {0, 7, 0, char(255)}; int64_t pos = 0;
  ASSERT_EQ(OB_SUCCESS, clock.deserialize(bytes, sizeof(bytes), pos)); EXPECT_FALSE(clock.has_value()); EXPECT_EQ(99, clock.get_timestamp());
  clock.set_timestamp(42); EXPECT_TRUE(clock.has_value()); EXPECT_EQ(42, clock.get_timestamp());
}
TEST(StatementTime, invalid_or_truncated_input_preserves_clock_and_position) {
  StatementTime source; source.set_timestamp(INT64_MIN); std::vector<char> wire(source.get_serialize_size()); int64_t end = 0;
  ASSERT_EQ(OB_SUCCESS, source.serialize(wire.data(), wire.size(), end));
  for (int64_t prefix = 0; prefix < end; ++prefix) {
    StatementTime output; output.set_timestamp(99); int64_t pos = 0;
    EXPECT_NE(OB_SUCCESS, output.deserialize(wire.data(), prefix, pos)); EXPECT_EQ(0, pos); EXPECT_TRUE(output.has_value()); EXPECT_EQ(99, output.get_timestamp());
  }
  wire[0] = ObVarcharType; StatementTime output; output.set_timestamp(99); int64_t pos = 0;
  EXPECT_EQ(OB_OBJ_TYPE_ERROR, output.deserialize(wire.data(), wire.size(), pos)); EXPECT_EQ(0, pos); EXPECT_EQ(99, output.get_timestamp());
}
