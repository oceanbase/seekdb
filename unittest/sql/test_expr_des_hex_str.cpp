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
#include <cstring>
#include "common/datum/datum_value.h"
#include "common/ob_action_flag.h"
#include "lib/utility/serialization.h"
#include "sql/engine/expr/ob_expr_des_hex_str.h"

using namespace oceanbase::common;
using namespace oceanbase::sql;

namespace {
const char HEX[] = "0123456789ABCDEF";

template <typename T>
ObString scalar_wire(ObArenaAllocator &allocator, const SqlTypeMeta &meta, T value)
{
  char *bytes = static_cast<char *>(allocator.alloc(64));
  EXPECT_NE(nullptr, bytes);
  if (bytes == nullptr) { return ObString(); }
  int64_t pos = 0;
  EXPECT_EQ(OB_SUCCESS, serialization::encode(bytes, 64, pos, meta));
  EXPECT_EQ(OB_SUCCESS, serialization::encode(bytes, 64, pos, value));
  return ObString(static_cast<int32_t>(pos), bytes);
}

ObString string_wire(ObArenaAllocator &allocator, const SqlTypeMeta &meta, const char *value)
{
  char *bytes = static_cast<char *>(allocator.alloc(256));
  EXPECT_NE(nullptr, bytes);
  if (bytes == nullptr) { return ObString(); }
  int64_t pos = 0;
  ObString str(static_cast<int32_t>(strlen(value)), value);
  EXPECT_EQ(OB_SUCCESS, serialization::encode(bytes, 256, pos, meta));
  EXPECT_EQ(OB_SUCCESS, serialization::encode(bytes, 256, pos, str));
  return ObString(static_cast<int32_t>(pos), bytes);
}

ObString number_wire(ObArenaAllocator &allocator, const char *text, int32_t text_len, ObScale scale)
{
  number::ObNumber number;
  EXPECT_EQ(OB_SUCCESS, number.from(text, text_len, allocator));
  alignas(uint32_t) char compact[128] = {};
  int64_t compact_pos = 0;
  EXPECT_EQ(OB_SUCCESS, number.encode(compact, sizeof(compact), compact_pos));
  number::ObNumber::Desc desc{};
  MEMCPY(&desc, compact, sizeof(desc));
  const uint32_t *digits = reinterpret_cast<const uint32_t *>(compact + sizeof(desc));

  char *bytes = static_cast<char *>(allocator.alloc(256));
  EXPECT_NE(nullptr, bytes);
  if (bytes == nullptr) { return ObString(); }
  SqlTypeMeta meta;
  meta.set_number();
  meta.set_scale(scale);
  int64_t pos = 0;
  EXPECT_EQ(OB_SUCCESS, serialization::encode(bytes, 256, pos, meta));
  EXPECT_EQ(OB_SUCCESS, serialization::encode_number_type(bytes, 256, pos, desc, digits));
  return ObString(static_cast<int32_t>(pos), bytes);
}

ObString to_hex(ObArenaAllocator &allocator, const ObString &bytes)
{
  char *hex = static_cast<char *>(allocator.alloc(bytes.length() * 2));
  EXPECT_NE(nullptr, hex);
  if (hex == nullptr) { return ObString(); }
  for (int32_t i = 0; i < bytes.length(); ++i) {
    const uint8_t byte = static_cast<uint8_t>(bytes[i]);
    hex[2 * i] = HEX[byte >> 4];
    hex[2 * i + 1] = HEX[byte & 0x0f];
  }
  return ObString(bytes.length() * 2, hex);
}

void expect_literal(const ObString &hex, const char *expected)
{
  ObArenaAllocator allocator;
  DatumValue value;
  ASSERT_EQ(OB_SUCCESS, ObExprDesHexStr::deserialize_hex_cstr(hex.ptr(), hex.length(), allocator, value));
  char result[512] = {};
  int64_t pos = 0;
  ASSERT_EQ(OB_SUCCESS, value.print_plain_str_literal(result, sizeof(result), pos));
  EXPECT_STREQ(expected, result);
}
} // namespace

TEST(DesHexStr, legacy_numeric_and_string_golden_literals)
{
  ObArenaAllocator allocator;
  SqlTypeMeta meta;
  meta.set_null();
  char *null_bytes = static_cast<char *>(allocator.alloc(16));
  ASSERT_NE(nullptr, null_bytes);
  int64_t null_pos = 0;
  ASSERT_EQ(OB_SUCCESS, serialization::encode(null_bytes, 16, null_pos, meta));
  expect_literal(to_hex(allocator, ObString(static_cast<int32_t>(null_pos), null_bytes)), "NULL");

  meta.set_int();
  expect_literal(to_hex(allocator, scalar_wire<int64_t>(allocator, meta, -4242)), "-4242");

  meta.set_type(ObTinyIntType);
  expect_literal(to_hex(allocator, scalar_wire<int8_t>(allocator, meta, -8)), "-8");
  meta.set_type(ObSmallIntType);
  expect_literal(to_hex(allocator, scalar_wire<int16_t>(allocator, meta, -16)), "-16");
  meta.set_type(ObMediumIntType);
  expect_literal(to_hex(allocator, scalar_wire<int32_t>(allocator, meta, -24)), "-24");
  meta.set_type(ObInt32Type);
  expect_literal(to_hex(allocator, scalar_wire<int32_t>(allocator, meta, -32)), "-32");

  meta.set_uint64();
  expect_literal(to_hex(allocator, scalar_wire<uint64_t>(allocator, meta, UINT64_C(18446744073709551610))),
                 "18446744073709551610");
  meta.set_type(ObUTinyIntType);
  expect_literal(to_hex(allocator, scalar_wire<uint8_t>(allocator, meta, 8)), "8");
  meta.set_type(ObUSmallIntType);
  expect_literal(to_hex(allocator, scalar_wire<uint16_t>(allocator, meta, 16)), "16");
  meta.set_type(ObUMediumIntType);
  expect_literal(to_hex(allocator, scalar_wire<uint32_t>(allocator, meta, 24)), "24");
  meta.set_type(ObUInt32Type);
  expect_literal(to_hex(allocator, scalar_wire<uint32_t>(allocator, meta, 32)), "32");

  meta.set_float();
  expect_literal(to_hex(allocator, scalar_wire<float>(allocator, meta, 1.25f)), "1.25");
  meta.set_double();
  expect_literal(to_hex(allocator, scalar_wire<double>(allocator, meta, -2.5)), "-2.5");
  meta.set_ufloat();
  expect_literal(to_hex(allocator, scalar_wire<float>(allocator, meta, 3.5f)), "3.5");
  meta.set_udouble();
  expect_literal(to_hex(allocator, scalar_wire<double>(allocator, meta, 4.5)), "4.5");

  expect_literal(to_hex(allocator, number_wire(allocator, "123.45", 6, 2)), "123.45");

  meta.set_varchar();
  meta.set_collation_level(CS_LEVEL_IMPLICIT);
  meta.set_collation_type(CS_TYPE_UTF8MB4_GENERAL_CI);
  expect_literal(to_hex(allocator, string_wire(allocator, meta, "seekdb datum")), "'seekdb datum'");
}

TEST(DesHexStr, preserves_odd_half_byte_trailing_bytes_and_uppercase_validation)
{
  ObArenaAllocator allocator;
  SqlTypeMeta meta;
  meta.set_int();
  ObString bytes = scalar_wire<int64_t>(allocator, meta, 4242);
  ObString hex = to_hex(allocator, bytes);

  char *odd = static_cast<char *>(allocator.alloc(hex.length() + 1));
  ASSERT_NE(nullptr, odd);
  MEMCPY(odd, hex.ptr(), hex.length());
  odd[hex.length()] = 'A';
  DatumValue decoded;
  ASSERT_EQ(OB_SUCCESS, ObExprDesHexStr::deserialize_hex_cstr(odd, hex.length() + 1, allocator, decoded));
  char result[64] = {};
  int64_t pos = 0;
  ASSERT_EQ(OB_SUCCESS, decoded.print_plain_str_literal(result, sizeof(result), pos));
  EXPECT_STREQ("4242", result);

  char *with_tail = static_cast<char *>(allocator.alloc(hex.length() + 2));
  ASSERT_NE(nullptr, with_tail);
  MEMCPY(with_tail, hex.ptr(), hex.length());
  with_tail[hex.length()] = 'F';
  with_tail[hex.length() + 1] = 'F';
  ASSERT_EQ(OB_SUCCESS, ObExprDesHexStr::deserialize_hex_cstr(
      with_tail, hex.length() + 2, allocator, decoded));

  const char lower_hex[] = "a0";
  EXPECT_EQ(OB_ERR_UNEXPECTED, ObExprDesHexStr::deserialize_hex_cstr(
      lower_hex, sizeof(lower_hex) - 1, allocator, decoded));
}

TEST(DesHexStr, legacy_date_lob_decimal_int_and_ext_wire_cases)
{
  ObArenaAllocator allocator;
  SqlTypeMeta meta;
  char *wire = static_cast<char *>(allocator.alloc(256));
  ASSERT_NE(nullptr, wire);
  int64_t pos = 0;
  DatumValue decoded;
  char result[256] = {};
  int64_t result_pos = 0;

  // Protocol definition: scalar temporal payloads are encoded as their
  // native signed/unsigned integer representation following SqlTypeMeta.
  meta.set_date();
  ASSERT_EQ(OB_SUCCESS, serialization::encode(wire, 256, pos, meta));
  ASSERT_EQ(OB_SUCCESS, serialization::encode(wire, 256, pos, int32_t(0)));
  ASSERT_EQ(OB_SUCCESS, ObExprDesHexStr::deserialize_hex_cstr(
      to_hex(allocator, ObString(static_cast<int32_t>(pos), wire)).ptr(), pos * 2, allocator, decoded));
  EXPECT_EQ(ObDateType, decoded.get_type());
  EXPECT_EQ(0, decoded.get_date());

  // LOB text values use the normal serialized ObString payload. Plain
  // rendering is type-specific in DatumValue and should quote the content.
  meta.set_type(ObLongTextType);
  meta.set_collation_type(CS_TYPE_UTF8MB4_GENERAL_CI);
  pos = 0;
  ASSERT_EQ(OB_SUCCESS, serialization::encode(wire, 256, pos, meta));
  ASSERT_EQ(OB_SUCCESS, serialization::encode(wire, 256, pos, ObString("lob text")));
  ObString lob_hex = to_hex(allocator, ObString(static_cast<int32_t>(pos), wire));
  ASSERT_EQ(OB_SUCCESS, ObExprDesHexStr::deserialize_hex_cstr(lob_hex.ptr(), lob_hex.length(), allocator, decoded));
  result_pos = 0;
  ASSERT_EQ(OB_SUCCESS, decoded.print_plain_str_literal(result, sizeof(result), result_pos));
  EXPECT_STREQ("'lob text'", result);

  // DecimalInt protocol is val_len, scale, then raw coefficient bytes.
  // This fixture uses the protocol-defined 8-byte coefficient representation.
  const int64_t coefficient = 12345;
  meta.set_decimal_int(2);
  pos = 0;
  ASSERT_EQ(OB_SUCCESS, serialization::encode(wire, 256, pos, meta));
  ASSERT_EQ(OB_SUCCESS, serialization::encode(wire, 256, pos, int32_t(sizeof(coefficient))));
  ASSERT_EQ(OB_SUCCESS, serialization::encode(wire, 256, pos, ObScale(2)));
  MEMCPY(wire + pos, &coefficient, sizeof(coefficient));
  pos += sizeof(coefficient);
  ObString dec_hex = to_hex(allocator, ObString(static_cast<int32_t>(pos), wire));
  ASSERT_EQ(OB_SUCCESS, ObExprDesHexStr::deserialize_hex_cstr(dec_hex.ptr(), dec_hex.length(), allocator, decoded));
  EXPECT_EQ(ObDecimalIntType, decoded.get_type());
  result_pos = 0;
  ASSERT_EQ(OB_SUCCESS, decoded.print_plain_str_literal(result, sizeof(result), result_pos));
  EXPECT_STREQ("123.45", result);

  // Legacy plain printer accepts only the four documented scalar markers.
  meta.set_ext();
  meta.set_extend_type(0);
  pos = 0;
  ASSERT_EQ(OB_SUCCESS, serialization::encode(wire, 256, pos, meta));
  ASSERT_EQ(OB_SUCCESS, serialization::encode(wire, 256, pos, int64_t(ObActionFlag::OP_NOP)));
  ObString ext_hex = to_hex(allocator, ObString(static_cast<int32_t>(pos), wire));
  ASSERT_EQ(OB_SUCCESS, ObExprDesHexStr::deserialize_hex_cstr(ext_hex.ptr(), ext_hex.length(), allocator, decoded));
  result_pos = 0;
  ASSERT_EQ(OB_SUCCESS, decoded.print_plain_str_literal(result, sizeof(result), result_pos));
  EXPECT_STREQ("__OB__NOP__", result);

  pos = 0;
  ASSERT_EQ(OB_SUCCESS, serialization::encode(wire, 256, pos, meta));
  ASSERT_EQ(OB_SUCCESS, serialization::encode(wire, 256, pos, int64_t(99)));
  ObString bad_ext_hex = to_hex(allocator, ObString(static_cast<int32_t>(pos), wire));
  EXPECT_EQ(OB_NOT_SUPPORTED, ObExprDesHexStr::deserialize_hex_cstr(
      bad_ext_hex.ptr(), bad_ext_hex.length(), allocator, decoded));
}

TEST(DesHexStr, legacy_udt_collection_and_sql_array_wire_boundaries)
{
  ObArenaAllocator allocator;
  char *wire = static_cast<char *>(allocator.alloc(256));
  ASSERT_NE(nullptr, wire);
  int64_t pos = 0;
  SqlTypeMeta meta;
  DatumValue decoded;

  // UDT and Collection wire values are subschema id, flags, then ObString.
  for (SqlValueType type : {ObUserDefinedSQLType, ObCollectionSQLType}) {
    meta.set_type(type);
    meta.set_subschema_id(7);
    meta.set_udt_flags(1);
    pos = 0;
    ASSERT_EQ(OB_SUCCESS, serialization::encode(wire, 256, pos, meta));
    ASSERT_EQ(OB_SUCCESS, serialization::encode(wire, 256, pos, uint16_t(7)));
    ASSERT_EQ(OB_SUCCESS, serialization::encode(wire, 256, pos, uint8_t(1)));
    ASSERT_EQ(OB_SUCCESS, serialization::encode(wire, 256, pos, ObString("payload")));
    ObString hex = to_hex(allocator, ObString(static_cast<int32_t>(pos), wire));
    ASSERT_EQ(OB_SUCCESS, ObExprDesHexStr::deserialize_hex_cstr(hex.ptr(), hex.length(), allocator, decoded));
    EXPECT_EQ(type, decoded.get_type());
    EXPECT_EQ(7, decoded.get_meta().get_subschema_id());
    EXPECT_EQ(1, decoded.get_meta().get_udt_flags());
  }

  // SQL arrays use an Ext meta tag, ext=0, positive serialized body length,
  // and body bytes. DES_HEX's historical plain printer rejects this type.
  meta.set_ext();
  meta.set_extend_type(T_EXT_SQL_ARRAY);
  pos = 0;
  ASSERT_EQ(OB_SUCCESS, serialization::encode(wire, 256, pos, meta));
  ASSERT_EQ(OB_SUCCESS, serialization::encode(wire, 256, pos, int64_t(0)));
  ASSERT_EQ(OB_SUCCESS, serialization::encode(wire, 256, pos, int64_t(3)));
  wire[pos++] = 'a'; wire[pos++] = 'b'; wire[pos++] = 'c';
  ObString array_hex = to_hex(allocator, ObString(static_cast<int32_t>(pos), wire));
  EXPECT_EQ(OB_INVALID_ARGUMENT, ObExprDesHexStr::deserialize_hex_cstr(
      array_hex.ptr(), array_hex.length(), allocator, decoded));
}

int main(int argc, char **argv)
{
  ::testing::InitGoogleTest(&argc, argv);
  return RUN_ALL_TESTS();
}
