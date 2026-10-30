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

#include <cstring>
#include <vector>
#include <gtest/gtest.h>
#include "common/datum/datum_parameter.h"
#include "lib/allocator/ob_allocator.h"

using namespace oceanbase::common;

namespace {
class RejectAllocator : public ObIAllocator
{
public:
  void *alloc(int64_t) override { return nullptr; }
  void *alloc(int64_t, const ObMemAttr &) override { return nullptr; }
  void free(void *) override {}
};
}

TEST(DatumParameter, keeps_null_type_accuracy_flags_and_source_position_independent)
{
  DatumParameter parameter;
  EXPECT_TRUE(parameter.is_null());
  SqlTypeMeta actual, declared;
  actual.set_null(); declared.set_varchar();
  declared.set_collation_type(CS_TYPE_UTF8MB4_BIN);
  declared.set_collation_level(CS_LEVEL_COERCIBLE);
  parameter.set_type_meta(actual); parameter.set_param_meta(declared);
  parameter.datum_.set_null();
  ObAccuracy accuracy; accuracy.set_accuracy(0x123456789abcdef0ULL);
  parameter.set_accuracy(accuracy);
  ParamFlag flags; flags.flag_ = 0xa5; flags.extend_flag_ = 0x5a;
  parameter.set_param_flag(flags); parameter.set_result_flags(0xfedcba98);
  parameter.set_raw_text_info(31, 97);
  DatumParameter copy(parameter);
  EXPECT_TRUE(copy.is_null()); EXPECT_EQ(ObNullType, copy.get_type());
  EXPECT_EQ(declared, copy.get_param_meta());
  EXPECT_EQ(accuracy, copy.get_accuracy());
  EXPECT_EQ(0xa5, copy.get_param_flag().flag_);
  EXPECT_EQ(0x5a, copy.get_param_flag().extend_flag_);
  EXPECT_EQ(0xfedcba98U, copy.get_result_flag());
  EXPECT_EQ(31, copy.get_raw_text_pos()); EXPECT_EQ(97, copy.get_raw_text_len());
  copy.reset(); EXPECT_EQ(-1, copy.get_raw_text_pos());
  EXPECT_EQ(0U, copy.get_result_flag()); EXPECT_TRUE(copy.get_param_meta().is_null());
}

TEST(DatumParameter, complete_lob_metadata_survives_copy)
{
  DatumParameter parameter;
  SqlTypeMeta meta; meta.set_blob(); meta.set_has_lob_header();
  parameter.set_type_meta(meta);
  DatumParameter copy(parameter);
  EXPECT_EQ(meta, copy.get_type_meta());
  EXPECT_TRUE(copy.has_lob_header());
  EXPECT_EQ(CS_TYPE_BINARY, copy.get_collation_type());
  EXPECT_EQ(CS_LEVEL_IMPLICIT, copy.get_collation_level());
}

TEST(DatumParameter, deep_copy_outlives_payload_arena)
{
  DatumParameter destination;
  std::string expected("\0unicode\xe4\xb8\xad\xe6\x96\x87\xff", 16);
  ObArenaAllocator destination_arena;
  {
    ObArenaAllocator source_arena;
    DatumParameter source;
    SqlTypeMeta meta; meta.set_varbinary(); source.set_type_meta(meta);
    char *payload = static_cast<char *>(source_arena.alloc(expected.size()));
    ASSERT_NE(nullptr, payload); memcpy(payload, expected.data(), expected.size());
    source.datum_.set_string(payload, expected.size());
    source.set_raw_text_info(3, 16);
    ASSERT_EQ(OB_SUCCESS, destination.deep_copy(source, destination_arena));
    EXPECT_NE(source.datum_.ptr_, destination.datum_.ptr_);
    memset(payload, 0x5a, expected.size());
  }
  EXPECT_EQ(expected.size(), destination.datum_.len_);
  EXPECT_EQ(0, memcmp(expected.data(), destination.datum_.ptr_, expected.size()));
  EXPECT_EQ(3, destination.get_raw_text_pos());
}

TEST(DatumParameter, container_reallocation_never_points_into_moved_parameter)
{
  ObArenaAllocator arena;
  std::vector<DatumParameter> parameters;
  for (int64_t i = 0; i < 513; ++i) {
    DatumParameter parameter; SqlTypeMeta meta; meta.set_int();
    parameter.set_type_meta(meta);
    ASSERT_EQ(OB_SUCCESS, parameter.alloc_datum_reserved_buff(meta, -1, arena));
    parameter.datum_.set_int(i); parameters.push_back(parameter);
  }
  for (int64_t i = 0; i < 513; ++i) {
    EXPECT_EQ(i, parameters[i].datum_.get_int());
    EXPECT_EQ(ObIntType, parameters[i].get_type());
  }
}

TEST(DatumParameter, failed_clone_is_atomic_and_self_clone_owns_new_bytes)
{
  ObArenaAllocator arena;
  RejectAllocator reject;
  DatumParameter source, destination;
  SqlTypeMeta meta; meta.set_int();
  source.set_type_meta(meta); destination.set_type_meta(meta);
  ASSERT_EQ(OB_SUCCESS, source.alloc_datum_reserved_buff(meta, -1, arena));
  ASSERT_EQ(OB_SUCCESS, destination.alloc_datum_reserved_buff(meta, -1, arena));
  source.datum_.set_int(123); destination.datum_.set_int(456);
  const char *old = destination.datum_.ptr_;
  EXPECT_EQ(OB_ALLOCATE_MEMORY_FAILED, destination.deep_copy(source, reject));
  EXPECT_EQ(old, destination.datum_.ptr_); EXPECT_EQ(456, destination.datum_.get_int());
  old = source.datum_.ptr_;
  ASSERT_EQ(OB_SUCCESS, source.deep_copy(source, arena));
  EXPECT_NE(old, source.datum_.ptr_); EXPECT_EQ(123, source.datum_.get_int());
}

TEST(DatumParameter, array_payload_copy_keeps_pointee_ownership_explicit)
{
  struct Array { int count; } array{7};
  char address[8]; DatumParameter source, copy;
  SqlTypeMeta meta; meta.set_ext(); meta.set_extend_type(T_EXT_SQL_ARRAY);
  source.set_type_meta(meta); source.datum_.ptr_ = address;
  source.set_array_address(&array);
  ObArenaAllocator arena;
  ASSERT_EQ(OB_SUCCESS, copy.deep_copy(source, arena));
  EXPECT_EQ(&array, copy.get_array<Array>());
  EXPECT_NE(source.datum_.ptr_, copy.datum_.ptr_);
  array.count = 11; EXPECT_EQ(11, copy.get_array<Array>()->count);
}

int main(int argc, char **argv)
{
  ::testing::InitGoogleTest(&argc, argv);
  return RUN_ALL_TESTS();
}
