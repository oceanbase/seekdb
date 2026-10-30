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

#include "common/datum/datum_sql_print.h"
#include "common/json_type/ob_json_bin.h"
#include "common/json_type/ob_json_parse.h"
#include <gtest/gtest.h>
#include <string>

using namespace oceanbase::common;

namespace {
std::string debug_json(const DatumValue &value, bool smart = false)
{
  char buffer[4096] = {};
  int64_t pos = 0;
  const int ret = smart
      ? print_datum_debug_smart(value, buffer, sizeof(buffer), pos)
      : print_datum_debug_json(value, buffer, sizeof(buffer), pos);
  EXPECT_EQ(OB_SUCCESS, ret);
  return std::string(buffer, pos);
}
}

TEST(DatumDebugPrinter, renders_scalar)
{
  alignas(int64_t) char storage[sizeof(int64_t)] = {};
  ObDatum datum(storage, sizeof(storage), false);
  SqlTypeMeta meta;
  datum.set_int(-17);
  meta.set_int();
  EXPECT_EQ("{\"BIGINT\":-17}", debug_json(DatumValue(datum, meta)));
}

TEST(DatumDebugPrinter, renders_decimal)
{
  ObDatum datum;
  SqlTypeMeta meta;
  int64_t decimal_bytes = 12345;
  datum.set_decimal_int_shallow(reinterpret_cast<const ObDecimalInt *>(&decimal_bytes), sizeof(decimal_bytes));
  meta.set_decimal_int(2);
  EXPECT_EQ("{\"DECIMAL\":123.45}", debug_json(DatumValue(datum, meta)));

  datum.set_datetime(0);
  meta.set_datetime();
  EXPECT_EQ("{\"DATETIME\":\"1970-01-01 00:00:00.000000\"}", debug_json(DatumValue(datum, meta)));

  datum.set_null();
  meta.set_null();
  EXPECT_EQ("{\"NULL\":\"NULL\"}", debug_json(DatumValue(datum, meta)));
}

TEST(DatumDebugPrinter, renders_string_collation_and_hex_smart_fallback)
{
  const char printable[] = "abc";
  ObDatum datum;
  SqlTypeMeta meta;
  datum.set_string(ObString::make_string(printable));
  meta.set_varchar();
  meta.set_collation_type(CS_TYPE_UTF8MB4_GENERAL_CI);
  meta.set_collation_level(CS_LEVEL_IMPLICIT);
  EXPECT_EQ("{\"VARCHAR\":\"abc\",\"collation\":\"utf8mb4_general_ci\",\"coercibility\":\"IMPLICIT\"}",
      debug_json(DatumValue(datum, meta)));

  const char binary[] = {static_cast<char>(0xff), 'x'};
  datum.set_string(ObString(sizeof(binary), binary));
  EXPECT_EQ("{\"VARCHAR\":X'FF78',\"collation\":\"utf8mb4_general_ci\",\"coercibility\":\"IMPLICIT\"}",
      debug_json(DatumValue(datum, meta), true));

  meta.set_hex_string();
  EXPECT_EQ("{\"HEX_STRING\":\"FF78\"}", debug_json(DatumValue(datum, meta), true));
}

TEST(DatumDebugPrinter, renders_ext_sentinel)
{
  char storage[DATUM_EXTEND_PAYLOAD_SIZE] = {};
  ObDatum datum;
  datum.ptr_ = storage;
  datum.set_extend(ObActionFlag::OP_MIN_OBJ, 0);
  SqlTypeMeta meta;
  meta.set_ext();
  EXPECT_EQ("{\"EXT\":\"__OB__MIN__\"}", debug_json(DatumValue(datum, meta), true));
}

TEST(DatumDebugPrinter, renders_text_lob_and_json)
{
  const char text[] = "large text";
  ObDatum datum;
  SqlTypeMeta meta;
  datum.set_string(ObString::make_string(text));
  meta.set_type(ObLongTextType);
  meta.set_collation_type(CS_TYPE_UTF8MB4_GENERAL_CI);
  meta.set_collation_level(CS_LEVEL_IMPLICIT);
  EXPECT_EQ("{\"LONGTEXT\":\"large text\",\"collation\":\"utf8mb4_general_ci\",\"coercibility\":\"IMPLICIT\"}",
      debug_json(DatumValue(datum, meta)));

  ObArenaAllocator owner;
  ObJsonNode *tree = nullptr;
  ASSERT_EQ(OB_SUCCESS, ObJsonParser::get_tree(&owner, ObString::make_string("{\"a\":1}"), tree));
  ObJsonBinSerializer serializer(&owner);
  ObString json_bin;
  ASSERT_EQ(OB_SUCCESS, serializer.serialize(tree, json_bin));
  datum.set_string(json_bin);
  meta.set_type(ObJsonType);
  EXPECT_EQ("{\"JSON\":{\"a\": 1}}", debug_json(DatumValue(datum, meta)));
}
