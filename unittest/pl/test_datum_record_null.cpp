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
#include <gtest/gtest.h>
#include "common/datum/ob_datum.h"
#include "pl/ob_pl_type.h"
#include "pl/ob_pl_user_type.h"

using namespace oceanbase::common;
using namespace oceanbase::pl;

TEST(DatumRecordNull, native_record_preserves_live_object_state)
{
  ObPLRecord record;
  char buf[DATUM_LEGACY_EXTEND_SIZE];
  ObDatum value;
  value.ptr_ = buf;
  value.set_extend(reinterpret_cast<int64_t>(&record), PL_RECORD_TYPE, sizeof(record));
  bool is_null = false;
  record.set_is_null(true);
  ASSERT_EQ(OB_SUCCESS, ObPLDataType::datum_is_null(&value, true, is_null));
  ASSERT_TRUE(is_null);
  record.set_is_null(false);
  ASSERT_EQ(OB_SUCCESS, ObPLDataType::datum_is_null(&value, true, is_null));
  ASSERT_FALSE(is_null);
}

TEST(DatumRecordNull, old_record_byte_view_is_still_readable)
{
  ObPLRecord record;
  char raw[DATUM_LEGACY_EXTEND_SIZE] = {};
  raw[0] = static_cast<char>(ObExtendType);
  raw[1] = static_cast<char>(CS_LEVEL_INVALID);
  raw[3] = static_cast<char>(PL_RECORD_TYPE);
  const int32_t size = sizeof(record);
  const int64_t address = reinterpret_cast<int64_t>(&record);
  memcpy(raw + 4, &size, sizeof(size));
  memcpy(raw + 8, &address, sizeof(address));
  ObDatum value(raw, sizeof(raw), false);
  value.set_flag(ObDatumDesc::EXT);
  bool is_null = false;
  record.set_is_null(true);
  ASSERT_EQ(OB_SUCCESS, ObPLDataType::datum_is_null(&value, true, is_null));
  ASSERT_TRUE(is_null);
  record.set_is_null(false);
  ASSERT_EQ(OB_SUCCESS, ObPLDataType::datum_is_null(&value, true, is_null));
  ASSERT_FALSE(is_null);
}

TEST(DatumRecordNull, normal_and_legacy_nulls_do_not_dereference_an_address)
{
  ObDatum value;
  value.set_null();
  bool is_null = false;
  ASSERT_EQ(OB_SUCCESS, ObPLDataType::datum_is_null(&value, true, is_null));
  ASSERT_TRUE(is_null);
  char raw[DATUM_LEGACY_EXTEND_SIZE] = {};
  raw[1] = static_cast<char>(CS_LEVEL_IGNORABLE);
  raw[2] = static_cast<char>(CS_TYPE_BINARY);
  value.ptr_ = raw;
  value.set_ext(DATUM_LEGACY_EXTEND_SIZE);
  ASSERT_EQ(OB_SUCCESS, ObPLDataType::datum_is_null(&value, true, is_null));
  ASSERT_TRUE(is_null);
}

TEST(DatumRecordNull, rejects_missing_record_and_wrong_value_kind)
{
  char buf[DATUM_LEGACY_EXTEND_SIZE];
  ObDatum value;
  value.ptr_ = buf;
  value.set_extend(0, PL_RECORD_TYPE);
  bool is_null = false;
  ASSERT_EQ(OB_ERR_UNEXPECTED, ObPLDataType::datum_is_null(&value, true, is_null));
  value.set_int(123);
  ASSERT_EQ(OB_ERR_UNEXPECTED, ObPLDataType::datum_is_null(&value, true, is_null));
  ASSERT_EQ(OB_SUCCESS, ObPLDataType::datum_is_null(&value, false, is_null));
  ASSERT_FALSE(is_null);
}

int main(int argc, char **argv)
{
  ::testing::InitGoogleTest(&argc, argv);
  return RUN_ALL_TESTS();
}
