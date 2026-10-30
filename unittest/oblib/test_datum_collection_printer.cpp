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

// Focused structured collection plain-printer regression cases.
// Expected strings below follow the existing native ObIArrayType::print format;
// this is NOT an independently frozen legacy-value oracle.
// Keep in a separate translation unit: collection metadata currently pulls the
// old value definition, while the main native printer test retains its #error.
#include "common/datum/datum_sql_print.h"
#include "common/udt/ob_array_fixed_size.h"
#include "common/udt/ob_array_binary.h"
#include "common/udt/ob_array_nested.h"
#include "common/udt/ob_map_type.h"
#include "common/udt/ob_vector_type.h"
#include "common/lob/lob_types.h"
#include "lib/allocator/page_arena.h"
#include <gtest/gtest.h>
#include <cstring>
#include <string>

using namespace oceanbase::common;

TEST(DatumCollectionPrinter, vector_element_access_returns_native_typed_datum)
{
  char output_bytes[16];
  ObDatum output;
  output.ptr_ = output_bytes;
  float floats[] = {1.25F, -2.5F};
  ObVectorF32Data float_vector(2, floats);
  ASSERT_EQ(OB_SUCCESS, float_vector.elem_at_datum(1, output));
  EXPECT_EQ(ObFloatType, float_vector.get_element_type());
  EXPECT_FLOAT_EQ(-2.5F, output.get_float());
  EXPECT_EQ(OB_ERR_UNEXPECTED, float_vector.elem_at_datum(2, output));

  uint8_t bytes[] = {0, 127, 255};
  ObVectorU8Data byte_vector(3, bytes);
  ASSERT_EQ(OB_SUCCESS, byte_vector.elem_at_datum(2, output));
  EXPECT_EQ(ObUTinyIntType, byte_vector.get_element_type());
  EXPECT_EQ(255U, output.get_uint());
  EXPECT_EQ(OB_ERR_UNEXPECTED, byte_vector.elem_at_datum(3, output));
}

namespace {
struct CollectionPrintFixture
{
  ObArenaAllocator owner;
  ObCollectionBasicType element;
  ObCollectionArrayType array_type;
  ObCollectionArrayType outer_type;
  ObSqlCollectionInfo info;
  ObIArrayType *array;
  CollectionPrintFixture()
      : owner(), element(), array_type(owner), outer_type(owner), info(owner), array(nullptr) {}

  int init(SqlValueType type, bool nested = false, ObPrecision precision = 9, ObScale scale = 0)
  {
    element.type_id_ = OB_BASIC_TYPE;
    element.basic_meta_.set_obj_type(type);
    element.basic_meta_.set_precision(precision);
    element.basic_meta_.set_scale(scale);
    array_type.type_id_ = OB_ARRAY_TYPE;
    array_type.element_type_ = &element;
    outer_type.type_id_ = OB_ARRAY_TYPE;
    outer_type.element_type_ = &array_type;
    info.collection_meta_ = nested ? &outer_type : &array_type;
    int ret = ObArrayTypeObjFactory::construct(owner, *info.collection_meta_, array, false);
    if (ret == OB_SUCCESS) { ret = array->init(); }
    return ret;
  }

  int payload(std::string &raw)
  {
    int ret = array->init();
    if (ret == OB_SUCCESS) {
      raw.resize(array->get_raw_binary_len());
      ret = array->get_raw_binary(&raw[0], raw.size());
    }
    return ret;
  }
};

std::string inrow_payload(const std::string &raw)
{
  ObLobCommon header;
  std::string lob(sizeof(header) + raw.size(), '\0');
  std::memcpy(&lob[0], &header, sizeof(header));
  if (!raw.empty()) { std::memcpy(&lob[sizeof(header)], raw.data(), raw.size()); }
  return lob;
}

DatumValue collection_view(const std::string &lob)
{
  ObDatum datum;
  datum.set_string(lob.data(), static_cast<uint32_t>(lob.size()));
  SqlTypeMeta meta;
  meta.set_type(ObCollectionSQLType);
  meta.set_has_lob_header();
  return DatumValue(datum, meta);
}

struct MapDistinctFixture
{
  ObArenaAllocator owner;
  ObCollectionBasicType key_element;
  ObCollectionBasicType value_element;
  ObCollectionArrayType key_array_type;
  ObCollectionArrayType value_array_type;
  ObCollectionMapType map_type;
  ObIArrayType *map;

  MapDistinctFixture()
      : owner(), key_element(), value_element(), key_array_type(owner), value_array_type(owner),
        map_type(owner), map(nullptr)
  {
    key_element.type_id_ = OB_BASIC_TYPE;
    key_element.basic_meta_.set_obj_type(ObTinyIntType);
    key_element.basic_meta_.meta_.set_tinyint();
    value_element.type_id_ = OB_BASIC_TYPE;
    value_element.basic_meta_.set_obj_type(ObIntType);
    value_element.basic_meta_.set_int();
    key_array_type.type_id_ = OB_ARRAY_TYPE;
    key_array_type.element_type_ = &key_element;
    value_array_type.type_id_ = OB_ARRAY_TYPE;
    value_array_type.element_type_ = &value_element;
    map_type.type_id_ = OB_MAP_TYPE;
    map_type.key_type_ = &key_array_type;
    map_type.value_type_ = &value_array_type;
  }

  int init()
  {
    int ret = ObArrayTypeObjFactory::construct(owner, map_type, map, false);
    if (OB_SUCC(ret)) { ret = map->init(); }
    return ret;
  }
};

void expect_text(CollectionPrintFixture &fixture, const std::string &expected)
{
  std::string raw;
  ASSERT_EQ(OB_SUCCESS, fixture.payload(raw));
  const std::string lob = inrow_payload(raw);
  const DatumValue value = collection_view(lob);
  for (bool use_memcpy : {false, true}) {
    for (int64_t prefix : {0, 3}) {
      SCOPED_TRACE(use_memcpy);
      SCOPED_TRACE(prefix);
      char output[512];
      std::memset(output, '!', sizeof(output));
      ObValuePrintParams params;
      params.coll_meta_ = &fixture.info;
      params.use_memcpy_ = use_memcpy;
      int64_t pos = prefix;
      ASSERT_EQ(OB_SUCCESS, value.print_plain_str_literal(output, sizeof(output), pos, params));
      EXPECT_EQ(prefix + static_cast<int64_t>(expected.size()), pos);
      EXPECT_EQ(std::string(prefix, '!') + expected, std::string(output, pos));
      // memcpy does not promise a terminator; printf does.
      EXPECT_EQ(use_memcpy ? '!' : '\0', output[pos]);
      EXPECT_EQ('!', output[sizeof(output) - 1]);
    }
  }
}
} // namespace

TEST(DatumCollectionPrinter, fixed_array_element_access_preserves_type_null_and_decimal_scale)
{
  CollectionPrintFixture integers;
  ASSERT_EQ(OB_SUCCESS, integers.init(ObIntType));
  integers.element.basic_meta_.set_int();
  auto *int_array = static_cast<ObArrayFixedSize<int64_t> *>(integers.array);
  ASSERT_EQ(OB_SUCCESS, int_array->push_back(-123456));
  ASSERT_EQ(OB_SUCCESS, int_array->push_null());
  ASSERT_EQ(OB_SUCCESS, int_array->init());
  char output_bytes[16];
  ObDatum output;
  output.ptr_ = output_bytes;
  ASSERT_EQ(OB_SUCCESS, int_array->elem_at_datum(0, output));
  EXPECT_EQ(ObIntType, integers.element.basic_meta_.get_obj_type());
  EXPECT_EQ(-123456, output.get_int());
  EXPECT_EQ(CS_LEVEL_NUMERIC, integers.element.basic_meta_.get_collation_level());
  ASSERT_EQ(OB_SUCCESS, int_array->elem_at_datum(1, output));
  EXPECT_TRUE(output.is_null());
  EXPECT_EQ(OB_ERR_UNEXPECTED, int_array->elem_at_datum(2, output));

  CollectionPrintFixture floats;
  ASSERT_EQ(OB_SUCCESS, floats.init(ObFloatType));
  floats.element.basic_meta_.meta_.set_float();
  auto *float_array = static_cast<ObArrayFixedSize<float> *>(floats.array);
  ASSERT_EQ(OB_SUCCESS, float_array->push_back(-3.25F));
  ASSERT_EQ(OB_SUCCESS, float_array->init());
  ASSERT_EQ(OB_SUCCESS, float_array->elem_at_datum(0, output));
  EXPECT_EQ(ObFloatType, floats.element.basic_meta_.get_obj_type());
  EXPECT_FLOAT_EQ(-3.25F, output.get_float());

  CollectionPrintFixture unsigneds;
  ASSERT_EQ(OB_SUCCESS, unsigneds.init(ObUInt32Type));
  unsigneds.element.basic_meta_.meta_.set_uint32();
  auto *uint_array = static_cast<ObArrayFixedSize<uint32_t> *>(unsigneds.array);
  ASSERT_EQ(OB_SUCCESS, uint_array->push_back(UINT32_MAX));
  ASSERT_EQ(OB_SUCCESS, uint_array->init());
  ASSERT_EQ(OB_SUCCESS, uint_array->elem_at_datum(0, output));
  EXPECT_EQ(ObUInt32Type, unsigneds.element.basic_meta_.get_obj_type());
  EXPECT_EQ(UINT32_MAX, output.get_uint32());

  CollectionPrintFixture decimals;
  ASSERT_EQ(OB_SUCCESS, decimals.init(ObDecimalIntType, false, 18, 3));
  decimals.element.basic_meta_.meta_.set_decimal_int(3);
  auto *decimal_array = static_cast<ObArrayFixedSize<int64_t> *>(decimals.array);
  ASSERT_EQ(OB_SUCCESS, decimal_array->push_back(12345));
  ASSERT_EQ(OB_SUCCESS, decimal_array->init());
  ObDatum decimal;
  decimal.ptr_ = output_bytes;
  ASSERT_EQ(OB_SUCCESS, decimal_array->elem_at_datum(0, decimal));
  EXPECT_EQ(ObDecimalIntType, decimals.element.basic_meta_.get_obj_type());
  EXPECT_EQ(3, decimals.element.basic_meta_.get_scale());
  EXPECT_EQ(12345, decimal.get_decimal_int64());
}

TEST(DatumCollectionPrinter, variable_width_array_element_access_returns_native_datum)
{
  CollectionPrintFixture strings;
  ASSERT_EQ(OB_SUCCESS, strings.init(ObVarcharType));
  auto *string_array = static_cast<ObArrayBinary *>(strings.array);
  ASSERT_EQ(OB_SUCCESS, string_array->push_back(ObString("datum")));
  ASSERT_EQ(OB_SUCCESS, string_array->push_null());
  ASSERT_EQ(OB_SUCCESS, string_array->init());

  ObDatum output;
  ASSERT_EQ(OB_SUCCESS, string_array->elem_at_datum(0, output));
  EXPECT_EQ(0, output.get_string().compare(ObString("datum")));
  ASSERT_EQ(OB_SUCCESS, string_array->elem_at_datum(1, output));
  EXPECT_TRUE(output.is_null());
  EXPECT_EQ(OB_ERR_UNEXPECTED, string_array->elem_at_datum(2, output));
}

TEST(DatumCollectionPrinter, narrow_fixed_array_datum_preserves_signed_and_unsigned_values)
{
  alignas(uint64_t) char output_bytes[sizeof(uint64_t)];
  ObDatum output;
  output.ptr_ = output_bytes;

  CollectionPrintFixture signed_values;
  ASSERT_EQ(OB_SUCCESS, signed_values.init(ObTinyIntType));
  signed_values.element.basic_meta_.meta_.set_tinyint();
  auto *signed_array = static_cast<ObArrayFixedSize<int8_t> *>(signed_values.array);
  ASSERT_EQ(OB_SUCCESS, signed_array->push_back(-101));
  ASSERT_EQ(OB_SUCCESS, signed_array->init());
  ASSERT_EQ(OB_SUCCESS, signed_array->elem_at_datum(0, output));
  EXPECT_EQ(-101, output.get_tinyint());

  CollectionPrintFixture unsigned_values;
  ASSERT_EQ(OB_SUCCESS, unsigned_values.init(ObUTinyIntType));
  unsigned_values.element.basic_meta_.meta_.set_utinyint();
  auto *unsigned_array = static_cast<ObArrayFixedSize<uint8_t> *>(unsigned_values.array);
  ASSERT_EQ(OB_SUCCESS, unsigned_array->push_back(UINT8_MAX));
  ASSERT_EQ(OB_SUCCESS, unsigned_array->init());
  ASSERT_EQ(OB_SUCCESS, unsigned_array->elem_at_datum(0, output));
  EXPECT_EQ(UINT8_MAX, output.get_utinyint());
}

TEST(DatumCollectionPrinter, map_distinct_uses_native_keys_and_keeps_one_null_and_last_duplicate)
{
  MapDistinctFixture fixture;
  ASSERT_EQ(OB_SUCCESS, fixture.init());
  auto *map = static_cast<ObMapType *>(fixture.map);
  auto *keys = static_cast<ObArrayFixedSize<int8_t> *>(map->get_key_array());
  auto *values = static_cast<ObArrayFixedSize<int64_t> *>(map->get_value_array());
  ASSERT_EQ(OB_SUCCESS, keys->push_back(2));
  ASSERT_EQ(OB_SUCCESS, values->push_back(100));
  ASSERT_EQ(OB_SUCCESS, keys->push_back(2));
  ASSERT_EQ(OB_SUCCESS, values->push_back(101));
  ASSERT_EQ(OB_SUCCESS, keys->push_back(-3));
  ASSERT_EQ(OB_SUCCESS, values->push_back(30));
  ASSERT_EQ(OB_SUCCESS, keys->push_null());
  ASSERT_EQ(OB_SUCCESS, values->push_back(99));
  ASSERT_EQ(OB_SUCCESS, keys->push_back(5));
  ASSERT_EQ(OB_SUCCESS, values->push_back(50));
  ASSERT_EQ(OB_SUCCESS, map->init());

  ObIArrayType *distinct = nullptr;
  ASSERT_EQ(OB_SUCCESS, map->distinct(fixture.owner, distinct));
  auto *distinct_map = static_cast<ObMapType *>(distinct);
  auto *distinct_keys = static_cast<ObArrayFixedSize<int8_t> *>(distinct_map->get_key_array());
  auto *distinct_values = static_cast<ObArrayFixedSize<int64_t> *>(distinct_map->get_value_array());
  ASSERT_EQ(4U, distinct_keys->size());
  ASSERT_TRUE(distinct_keys->is_null(0));
  EXPECT_EQ(99, (*distinct_values)[0]);
  EXPECT_EQ(-3, (*distinct_keys)[1]);
  EXPECT_EQ(30, (*distinct_values)[1]);
  EXPECT_EQ(2, (*distinct_keys)[2]);
  EXPECT_EQ(101, (*distinct_values)[2]);
  EXPECT_EQ(5, (*distinct_keys)[3]);
  EXPECT_EQ(50, (*distinct_values)[3]);
}

TEST(DatumCollectionPrinter, signed_numeric_and_empty_arrays)
{
  CollectionPrintFixture populated;
  ASSERT_EQ(OB_SUCCESS, populated.init(ObIntType));
  auto *numbers = static_cast<ObArrayFixedSize<int64_t> *>(populated.array);
  ASSERT_EQ(OB_SUCCESS, numbers->push_back(7));
  ASSERT_EQ(OB_SUCCESS, numbers->push_back(-9));
  expect_text(populated, "[7,-9]");
  ASSERT_EQ(OB_SUCCESS, numbers->push_null());
  expect_text(populated, "[7,-9,NULL]");

  CollectionPrintFixture empty;
  ASSERT_EQ(OB_SUCCESS, empty.init(ObIntType));
  expect_text(empty, "[]");
}

TEST(DatumCollectionPrinter, floating_values_and_null)
{
  CollectionPrintFixture fixture;
  ASSERT_EQ(OB_SUCCESS, fixture.init(ObDoubleType));
  auto *numbers = static_cast<ObArrayFixedSize<double> *>(fixture.array);
  ASSERT_EQ(OB_SUCCESS, numbers->push_back(1.5));
  ASSERT_EQ(OB_SUCCESS, numbers->push_back(-2.25));
  ASSERT_EQ(OB_SUCCESS, numbers->push_null());
  expect_text(fixture, "[1.5,-2.25,NULL]");
}

TEST(DatumCollectionPrinter, strings_empty_string_quote_and_null)
{
  CollectionPrintFixture fixture;
  ASSERT_EQ(OB_SUCCESS, fixture.init(ObVarcharType));
  auto *strings = static_cast<ObArrayBinary *>(fixture.array);
  ASSERT_EQ(OB_SUCCESS, strings->push_back(ObString("abc")));
  ASSERT_EQ(OB_SUCCESS, strings->push_back(ObString("")));
  ASSERT_EQ(OB_SUCCESS, strings->push_back(ObString("a\"b")));
  ASSERT_EQ(OB_SUCCESS, strings->push_null());
  expect_text(fixture, "[\"abc\",\"\",\"a\\\"b\",NULL]");
}

TEST(DatumCollectionPrinter, decimal_precision_scale_and_null)
{
  CollectionPrintFixture fixture;
  ASSERT_EQ(OB_SUCCESS, fixture.init(ObDecimalIntType, false, 9, 2));
  auto *numbers = static_cast<ObArrayFixedSize<int32_t> *>(fixture.array);
  ASSERT_EQ(OB_SUCCESS, numbers->push_back(12345));
  ASSERT_EQ(OB_SUCCESS, numbers->push_back(-250));
  ASSERT_EQ(OB_SUCCESS, numbers->push_null());
  expect_text(fixture, "[123.45,-2.50,NULL]");
}

TEST(DatumCollectionPrinter, nested_array_with_null_and_empty_child)
{
  CollectionPrintFixture first, empty, last, outer;
  ASSERT_EQ(OB_SUCCESS, first.init(ObIntType));
  ASSERT_EQ(OB_SUCCESS, empty.init(ObIntType));
  ASSERT_EQ(OB_SUCCESS, last.init(ObIntType));
  ASSERT_EQ(OB_SUCCESS, outer.init(ObIntType, true));
  auto *left = static_cast<ObArrayFixedSize<int64_t> *>(first.array);
  auto *right = static_cast<ObArrayFixedSize<int64_t> *>(last.array);
  ASSERT_EQ(OB_SUCCESS, left->push_back(1));
  ASSERT_EQ(OB_SUCCESS, left->push_back(2));
  ASSERT_EQ(OB_SUCCESS, right->push_back(3));
  ASSERT_EQ(OB_SUCCESS, first.array->init());
  ASSERT_EQ(OB_SUCCESS, last.array->init());
  auto *nested = static_cast<ObArrayNested *>(outer.array);
  ASSERT_EQ(OB_SUCCESS, nested->push_back(*first.array));
  ASSERT_EQ(OB_SUCCESS, nested->push_null());
  ASSERT_EQ(OB_SUCCESS, nested->push_back(*empty.array));
  ASSERT_EQ(OB_SUCCESS, nested->push_back(*last.array));
  expect_text(outer, "[[1,2],NULL,[],[3]]");
}

TEST(DatumCollectionPrinter, bounded_output_preserves_printf_and_memcpy_contracts)
{
  CollectionPrintFixture fixture;
  ASSERT_EQ(OB_SUCCESS, fixture.init(ObIntType));
  auto *numbers = static_cast<ObArrayFixedSize<int64_t> *>(fixture.array);
  ASSERT_EQ(OB_SUCCESS, numbers->push_back(123));
  std::string raw;
  ASSERT_EQ(OB_SUCCESS, fixture.payload(raw));
  const std::string lob = inrow_payload(raw);
  const DatumValue value = collection_view(lob);
  ObValuePrintParams params;
  params.coll_meta_ = &fixture.info;
  // Expected structured text is [123]. Both old helpers require room beyond
  // payload bytes, but only printf partially writes and advances on overflow.
  for (bool use_memcpy : {false, true}) {
    for (int64_t length : {4, 7}) {
      char output[32];
      std::memset(output, '!', sizeof(output));
      params.use_memcpy_ = use_memcpy;
      int64_t pos = 2;
      ASSERT_EQ(OB_SIZE_OVERFLOW, value.print_plain_str_literal(output, length, pos, params));
      if (use_memcpy) {
        EXPECT_EQ(2, pos);
        EXPECT_EQ(std::string(sizeof(output), '!'), std::string(output, sizeof(output)));
      } else {
        EXPECT_EQ(length - 1, pos);
        EXPECT_EQ("!!" + std::string("[123]").substr(0, length - 3), std::string(output, pos));
        EXPECT_EQ('\0', output[pos]);
        EXPECT_EQ(std::string(sizeof(output) - length, '!'),
            std::string(output + length, sizeof(output) - length));
      }
    }
  }
}

TEST(DatumCollectionPrinter, malformed_payload_and_missing_metadata_leave_output_unchanged)
{
  CollectionPrintFixture fixture;
  ASSERT_EQ(OB_SUCCESS, fixture.init(ObIntType));
  const std::string lob = inrow_payload(std::string(1, 'x'));
  const DatumValue value = collection_view(lob);
  ObValuePrintParams params;
  params.coll_meta_ = &fixture.info;
  for (bool use_memcpy : {false, true}) {
    char output[64];
    std::memset(output, '!', sizeof(output));
    int64_t pos = 3;
    params.use_memcpy_ = use_memcpy;
    EXPECT_EQ(OB_ERR_UNEXPECTED, value.print_plain_str_literal(output, sizeof(output), pos, params));
    EXPECT_EQ(3, pos);
    EXPECT_EQ(std::string(sizeof(output), '!'), std::string(output, sizeof(output)));
  }
  ObSqlCollectionInfo missing(fixture.owner);
  params.coll_meta_ = &missing;
  char output[64];
  std::memset(output, '!', sizeof(output));
  int64_t pos = 3;
  EXPECT_EQ(OB_INVALID_ARGUMENT, value.print_plain_str_literal(output, sizeof(output), pos, params));
  EXPECT_EQ(3, pos);
  EXPECT_EQ(std::string(sizeof(output), '!'), std::string(output, sizeof(output)));
}
