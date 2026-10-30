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
#include "share/datum/datum_string_accuracy.h"
#include "share/object/ob_array_cast.h"
#include <gtest/gtest.h>
#include <string>

using namespace oceanbase::common;
using namespace oceanbase::sql;

namespace {
int check(const char *text, int32_t bytes, ObLength limit, ObCastMode mode,
          ObDatum &output, int &warning)
{
  ObDatum input;
  input.set_string(text, bytes);
  return datum_varchar_accuracy_check(mode, limit, CS_TYPE_UTF8MB4_BIN,
                                      input, output, warning);
}

struct VarcharArrayFixture {
  ObArenaAllocator allocator;
  ObCollectionBasicType element;
  ObCollectionArrayType array_type;
  ObIArrayType *array;

  VarcharArrayFixture()
      : allocator(), element(), array_type(allocator), array(nullptr)
  {
    element.type_id_ = OB_BASIC_TYPE;
    element.basic_meta_.set_obj_type(ObVarcharType);
    element.basic_meta_.set_length(1);
    element.basic_meta_.set_collation_type(CS_TYPE_UTF8MB4_BIN);
    array_type.type_id_ = OB_ARRAY_TYPE;
    array_type.element_type_ = &element;
  }

  int init()
  {
    int ret = ObArrayTypeObjFactory::construct(allocator, array_type, array, false);
    if (OB_SUCC(ret)) { ret = array->init(); }
    return ret;
  }
};

}

TEST(DatumStringAccuracy, multibyte_length_counts_characters_and_truncates)
{
  const char text[] = "é猫";
  ObDatum output;
  int warning = OB_SUCCESS;
  ASSERT_EQ(OB_SUCCESS, check(text, sizeof(text) - 1, 1, CM_WARN_ON_FAIL, output, warning));
  EXPECT_EQ(OB_ERR_DATA_TOO_LONG, warning);
  EXPECT_EQ(2, output.len_);
  EXPECT_EQ("é", std::string(output.ptr_, output.len_));
}

TEST(DatumStringAccuracy, strict_mode_errors_on_non_space_overflow)
{
  const char text[] = "é猫";
  ObDatum output;
  int warning = OB_SUCCESS;
  EXPECT_EQ(OB_ERR_DATA_TOO_LONG, check(text, sizeof(text) - 1, 1, CM_NONE, output, warning));
  EXPECT_EQ(OB_SUCCESS, warning);
}

TEST(DatumStringAccuracy, trailing_spaces_warn_and_truncate_even_in_strict_mode)
{
  const char text[] = "a  ";
  ObDatum output;
  int warning = OB_SUCCESS;
  ASSERT_EQ(OB_SUCCESS, check(text, sizeof(text) - 1, 1, CM_NONE, output, warning));
  EXPECT_EQ(OB_ERR_DATA_TOO_LONG, warning);
  EXPECT_EQ("a", std::string(output.ptr_, output.len_));
}

TEST(DatumStringAccuracy, array_stores_the_checked_value)
{
  VarcharArrayFixture fixture;
  ASSERT_EQ(OB_SUCCESS, fixture.init());
  DatumValue input;
  ASSERT_EQ(OB_SUCCESS, input.set_string(ObVarcharType,
      ObString::make_string("é猫"), fixture.allocator));
  input.set_collation_type(CS_TYPE_UTF8MB4_BIN);
  ASSERT_EQ(OB_SUCCESS, ObArrayCastUtils::cast_add_element(
      fixture.allocator, input, fixture.array, &fixture.element, CM_WARN_ON_FAIL));
  ASSERT_EQ(OB_SUCCESS, fixture.array->init());
  auto *array = static_cast<ObArrayBinary *>(fixture.array);
  ASSERT_EQ(1U, array->size());
  const ObString stored = array->operator[](0);
  EXPECT_EQ(2, stored.length());
  EXPECT_EQ("é", std::string(stored.ptr(), stored.length()));
}

int main(int argc, char **argv)
{
  ::testing::InitGoogleTest(&argc, argv);
  if (ObCharset::init_charset() != OB_SUCCESS) { return 1; }
  return RUN_ALL_TESTS();
}
