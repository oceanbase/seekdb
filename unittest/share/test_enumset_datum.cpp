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

#include "common/datum/datum_parameter.h"
#include "share/object/ob_enumset_str_util.h"
#ifdef OCEANBASE_COMMON_OB_OBJECT_H_
#error "Native enum/set conversion must not require a legacy value object"
#endif
#include "lib/allocator/ob_allocator.h"
#include "lib/container/ob_se_array.h"
#include <gtest/gtest.h>
#include <string>

using namespace oceanbase::common;
namespace {
class RejectParameterGrowth : public ObIAllocator {
public:
  ObArenaAllocator arena_;
  bool reject_ = false;
  void *alloc(int64_t bytes) override { return reject_ ? nullptr : arena_.alloc(bytes); }
  void *alloc(int64_t bytes, const ObMemAttr &) override { return alloc(bytes); }
  void free(void *pointer) override { arena_.free(pointer); }
};
}

TEST(EnumSetDatum, native_enum_set_text_owns_bytes_and_preserves_collation) {
  ObArenaAllocator caller;
  DatumParameter output;
  {
    ObArenaAllocator callee;
    ObSEArray<ObString, 3> labels;
    ASSERT_EQ(OB_SUCCESS, labels.push_back(ObString::make_string("alice")));
    ASSERT_EQ(OB_SUCCESS, labels.push_back(ObString::make_string("bob")));
    ASSERT_EQ(OB_SUCCESS, labels.push_back(ObString::make_string("carol")));
    DatumParameter value;
    ASSERT_EQ(OB_SUCCESS, value.set_uint64(2, callee));
    value.get_meta().set_type(ObEnumType);
    value.get_meta().set_collation_type(CS_TYPE_UTF8MB4_BIN);
    value.set_length(17);
    ASSERT_EQ(OB_SUCCESS, enumset_str::to_varchar(value, labels, caller, output));
    EXPECT_EQ(ObString::make_string("bob"), output.get_string());
    EXPECT_EQ(ObVarcharType, output.get_type());
    EXPECT_EQ(CS_TYPE_UTF8MB4_BIN, output.get_collation_type());
    EXPECT_EQ(CS_LEVEL_IMPLICIT, output.get_collation_level());
    EXPECT_EQ(17, output.get_length());
    ASSERT_EQ(OB_SUCCESS, value.set_uint64(5, callee));
    value.get_meta().set_type(ObSetType);
    value.get_meta().set_collation_type(CS_TYPE_UTF8MB4_BIN);
    ASSERT_EQ(OB_SUCCESS, enumset_str::to_varchar(value, labels, caller, output));
  }
  EXPECT_EQ(ObString::make_string("alice,carol"), output.get_string());
}

TEST(EnumSetDatum, native_enum_set_text_null_zero_and_aliasing) {
  ObArenaAllocator arena;
  ObSEArray<ObString, 1> labels;
  ASSERT_EQ(OB_SUCCESS, labels.push_back(ObString::make_string("one")));
  DatumParameter value, output;
  value.get_meta().set_type(ObEnumType); // typed NULL
  value.get_meta().set_collation_type(CS_TYPE_UTF8MB4_BIN);
  ASSERT_EQ(OB_SUCCESS, enumset_str::to_varchar(value, labels, arena, output));
  EXPECT_TRUE(output.is_null());
  ASSERT_EQ(OB_SUCCESS, value.set_uint64(0, arena));
  value.get_meta().set_type(ObEnumType);
  ASSERT_EQ(OB_SUCCESS, enumset_str::to_varchar(value, labels, arena, output));
  EXPECT_FALSE(output.is_null()); EXPECT_TRUE(output.get_string().empty());
  ASSERT_EQ(OB_SUCCESS, value.set_uint64(1, arena));
  value.get_meta().set_type(ObEnumType);
  ASSERT_EQ(OB_SUCCESS, enumset_str::to_varchar(value, labels, arena, value));
  EXPECT_EQ(ObString::make_string("one"), value.get_string());
  EXPECT_EQ(ObVarcharType, value.get_type());
}

TEST(EnumSetDatum, failed_native_enum_set_text_does_not_publish) {
  ObArenaAllocator arena;
  RejectParameterGrowth owner;
  ObSEArray<ObString, 1> labels;
  ASSERT_EQ(OB_SUCCESS, labels.push_back(ObString::make_string("one")));
  DatumParameter value, output;
  ASSERT_EQ(OB_SUCCESS, output.set_int(42, owner));
  const char *previous = output.datum_.ptr_;
  ASSERT_EQ(OB_SUCCESS, value.set_uint64(2, arena));
  value.get_meta().set_type(ObEnumType);
  EXPECT_EQ(OB_ERR_DATA_TRUNCATED, enumset_str::to_varchar(value, labels, owner, output));
  value.get_meta().set_type(ObSetType);
  EXPECT_EQ(OB_ERR_DATA_TRUNCATED, enumset_str::to_varchar(value, labels, owner, output));
  value.datum_.len_ = 1;
  EXPECT_EQ(OB_INVALID_ARGUMENT, enumset_str::to_varchar(value, labels, owner, output));
  ASSERT_EQ(OB_SUCCESS, value.set_uint64(1, arena));
  value.get_meta().set_type(ObEnumType);
  owner.reject_ = true;
  EXPECT_EQ(OB_ALLOCATE_MEMORY_FAILED, enumset_str::to_varchar(value, labels, owner, output));
  EXPECT_EQ(42, output.get_int()); EXPECT_EQ(previous, output.datum_.ptr_);
}

TEST(EnumSetDatum, long_labels_do_not_require_sql_expression_or_lob) {
  ObArenaAllocator arena;
  std::string long_label(131072, 'x');
  ObSEArray<ObString, 1> labels;
  ASSERT_EQ(OB_SUCCESS, labels.push_back(ObString(long_label.size(), long_label.data())));
  DatumParameter value, output;
  ASSERT_EQ(OB_SUCCESS, value.set_uint64(1, arena));
  value.get_meta().set_type(ObEnumType);
  value.get_meta().set_collation_type(CS_TYPE_UTF8MB4_BIN);
  ASSERT_EQ(OB_SUCCESS, enumset_str::to_varchar(value, labels, arena, output));
  EXPECT_EQ(labels.at(0), output.get_string());
  EXPECT_NE(labels.at(0).ptr(), output.get_string().ptr());
  EXPECT_FALSE(output.has_lob_header());
}

TEST(EnumSetDatum, set_preserves_bytes_for_every_supported_collation) {
  ObArenaAllocator arena;
  const char first[] = "客户", second[] = {'b', 0, 'c'};
  std::string expected(first); expected += ','; expected.append(second, sizeof(second));
  ObSEArray<ObString, 2> labels;
  ASSERT_EQ(OB_SUCCESS, labels.push_back(ObString(sizeof(first) - 1, first)));
  ASSERT_EQ(OB_SUCCESS, labels.push_back(ObString(sizeof(second), second)));
  const ObCollationType collations[] = {CS_TYPE_BINARY, CS_TYPE_UTF8MB4_BIN,
      CS_TYPE_UTF8MB4_GENERAL_CI, CS_TYPE_UTF8MB4_UNICODE_CI};
  for (ObCollationType collation : collations) {
    DatumParameter value, output;
    ASSERT_EQ(OB_SUCCESS, value.set_uint64(3, arena));
    value.get_meta().set_type(ObSetType);
    value.get_meta().set_collation_type(collation);
    ASSERT_EQ(OB_SUCCESS, enumset_str::to_varchar(value, labels, arena, output));
    EXPECT_EQ(ObString(expected.size(), expected.data()), output.get_string());
    EXPECT_EQ(collation, output.get_collation_type());
  }
}

TEST(EnumSetDatum, unsupported_collation_does_not_silently_drop_separator) {
  ObArenaAllocator arena;
  ObSEArray<ObString, 2> labels;
  ASSERT_EQ(OB_SUCCESS, labels.push_back(ObString::make_string("a")));
  ASSERT_EQ(OB_SUCCESS, labels.push_back(ObString::make_string("b")));
  DatumParameter value, output;
  ASSERT_EQ(OB_SUCCESS, value.set_uint64(3, arena));
  value.get_meta().set_type(ObSetType);
  value.get_meta().set_collation_type(CS_TYPE_UTF16_BIN); // not supported by seekdb
  ASSERT_EQ(OB_SUCCESS, output.set_int(42, arena));
  EXPECT_EQ(OB_NOT_SUPPORTED, enumset_str::to_varchar(value, labels, arena, output));
  EXPECT_EQ(42, output.get_int());
}

int main(int argc, char **argv)
{
  ::testing::InitGoogleTest(&argc, argv);
  if (ObCharset::init_charset() != OB_SUCCESS) { return 1; }
  return RUN_ALL_TESTS();
}
