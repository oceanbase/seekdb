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

#include "data_plane/access/ob_table_param.h"
#include "data_plane/blocksstable/ob_datum_row.h"
#include "share/schema/ob_column_schema.h"
#include "share/schema/ob_table_schema.h"
#include "storage/ob_storage_schema.h"
#include "storage/blocksstable/encoding/ob_new_column_decoder.h"
#include "lib/allocator/ob_allocator.h"
#include "lib/charset/ob_charset.h"
#include <gtest/gtest.h>
#include <array>
#include <cstring>
#include <string>

using namespace oceanbase;
using namespace oceanbase::common;
using namespace oceanbase::share::schema;
using namespace oceanbase::blocksstable;

namespace
{
SqlTypeMeta integer_meta()
{
  SqlTypeMeta result;
  result.set_int();
  return result;
}

void column_identity(ObColumnSchemaV2 &column, uint64_t id, const char *name,
                     SqlValueType kind, bool key = false)
{
  column.set_table_id(50001);
  column.set_column_id(id);
  ASSERT_EQ(OB_SUCCESS, column.set_column_name(name));
  column.set_data_type(kind);
  column.set_data_length(16);
  column.set_collation_type(ob_is_string_type(kind) ? CS_TYPE_UTF8MB4_BIN : CS_TYPE_BINARY);
  column.set_nullable(!key);
  column.set_rowkey_position(key ? 1 : 0);
}

void add_column(ObTableSchema &table, uint64_t id, const char *name, SqlValueType kind,
                const DatumValue &value, bool key = false)
{
  ObColumnSchemaV2 column;
  column_identity(column, id, name, kind, key);
  ASSERT_EQ(OB_SUCCESS, column.set_orig_default_value(value));
  ASSERT_EQ(OB_SUCCESS, column.set_cur_default_value(value));
  ASSERT_EQ(OB_SUCCESS, table.add_column(column));
}

std::string bytes(const ObDatum &datum)
{
  return std::string(datum.ptr_, datum.len_);
}
}

TEST(NativeStorageDefaults, actual_schema_to_param_producer_owns_values_and_marks_generated_nop)
{
  ObArenaAllocator input, owner;
  ObColumnParam param(owner);
  ObColumnSchemaV2 column;
  column_identity(column, 16, "value", ObIntType);
  DatumValue original, current;
  ASSERT_EQ(OB_SUCCESS, original.set_int(INT64_MIN, input));
  const char raw[] = {'a', '\0', 'b'};
  ASSERT_EQ(OB_SUCCESS, current.set_varchar(ObString(sizeof(raw), raw), input));
  current.set_collation_type(CS_TYPE_UTF8MB4_BIN);
  ASSERT_EQ(OB_SUCCESS, column.set_orig_default_value(original));
  ASSERT_EQ(OB_SUCCESS, column.set_cur_default_value(current));
  ASSERT_EQ(OB_SUCCESS, ObTableParam::convert_column_schema_to_param(column, param));
  column.reset();
  input.reset();
  EXPECT_EQ(INT64_MIN, param.get_orig_default_datum().get_int());
  EXPECT_EQ(ObVarcharType, param.get_cur_default_meta().get_type());
  EXPECT_EQ(std::string(raw, sizeof(raw)),
            std::string(static_cast<const char *>(param.get_cur_default_datum().get_reference()),
                        param.get_cur_default_datum().length()));

  column_identity(column, 16, "generated", ObIntType);
  column.set_column_flags(VIRTUAL_GENERATED_COLUMN_FLAG);
  ASSERT_TRUE(column.is_generated_column());
  ASSERT_EQ(OB_SUCCESS, ObTableParam::convert_column_schema_to_param(column, param));
  EXPECT_TRUE(param.get_orig_default_datum().is_nop());
  EXPECT_TRUE(param.get_cur_default_datum().is_nop());
  EXPECT_EQ(ObExtendType, param.get_orig_default_meta().get_type());
  param.destroy();
}

TEST(NativeStorageDefaults, actual_new_column_decoder_copies_scalar_and_decimal_into_reserved_outputs)
{
  ObArenaAllocator owner, input;
  ObColumnParam param(owner);
  ObNewColumnCommonDecoder decoder;
  Datum integer;
  integer.set_int(INT64_MIN);
  ASSERT_EQ(OB_SUCCESS, param.set_orig_default_datum(integer, integer_meta()));
  alignas(8) char buffers[3][64] = {};
  ObDatum output[3];
  for (int i = 0; i < 3; ++i) { output[i].ptr_ = buffers[i]; }
  ASSERT_EQ(OB_SUCCESS, decoder.batch_decode(&param, &owner, 3, output));
  int64_t nulls = -1;
  ASSERT_EQ(OB_SUCCESS, decoder.get_null_count(&param, 3, nulls));
  EXPECT_EQ(0, nulls);
  param.reset();
  for (int i = 0; i < 3; ++i) {
    EXPECT_EQ(buffers[i], output[i].ptr_);
    EXPECT_EQ(INT64_MIN, output[i].get_int());
  }

  for (int width : {4, 8, 16, 32, 64}) {
    SCOPED_TRACE(width);
    alignas(8) char raw[64];
    for (int i = 0; i < width; ++i) { raw[i] = static_cast<char>(i + 1); }
    DatumValue decimal;
    ASSERT_EQ(OB_SUCCESS, decimal.set_decimal_int(width, 5,
        reinterpret_cast<const ObDecimalInt *>(raw), input));
    ASSERT_EQ(OB_SUCCESS, param.set_orig_default_value(decimal));
    alignas(8) char result[64] = {};
    ObDatum cell;
    cell.ptr_ = result;
    ASSERT_EQ(OB_SUCCESS, decoder.decode(&param, &owner, cell));
    EXPECT_EQ(width, cell.len_);
    EXPECT_EQ(result, cell.ptr_);
    param.reset();
    input.reset();
    EXPECT_EQ(std::string(raw, width), bytes(cell));
  }
  param.destroy();
}

TEST(NativeStorageDefaults, actual_new_column_decoder_keeps_reference_and_null_domains)
{
  ObArenaAllocator owner, input;
  ObColumnParam param(owner);
  ObNewColumnCommonDecoder decoder;
  const char raw[] = {'x', '\0', 'y'};
  DatumValue text;
  ASSERT_EQ(OB_SUCCESS, text.set_varchar(ObString(sizeof(raw), raw), input));
  text.set_collation_type(CS_TYPE_UTF8MB4_BIN);
  ASSERT_EQ(OB_SUCCESS, param.set_orig_default_value(text));
  input.reset();
  alignas(8) char reserved[64] = {};
  ObDatum output;
  output.ptr_ = reserved;
  ASSERT_EQ(OB_SUCCESS, decoder.decode(&param, &owner, output));
  EXPECT_EQ(param.get_orig_default_datum().get_reference(), output.ptr_);
  EXPECT_EQ(std::string(raw, sizeof(raw)), bytes(output));
  // String borrowing is valid only while the ColumnParam remains alive.
  Datum null;
  ASSERT_EQ(OB_SUCCESS, param.set_orig_default_datum(null, integer_meta()));
  ASSERT_EQ(OB_SUCCESS, decoder.decode(&param, &owner, output));
  EXPECT_TRUE(output.is_null());
  int64_t nulls = -1;
  ASSERT_EQ(OB_SUCCESS, decoder.get_null_count(&param, 7, nulls));
  EXPECT_EQ(7, nulls);
  param.destroy();
}

TEST(NativeStorageDefaults, actual_storage_schema_default_row_preserves_owner_trim_and_hidden_columns)
{
  ObArenaAllocator input, storage_owner, row_owner;
  ObTableSchema table;
  table.set_table_id(50001);
  table.set_database_id(1001);
  table.set_schema_version(1);
  table.set_table_type(USER_TABLE);
  table.set_index_type(INDEX_TYPE_IS_NOT);
  table.set_rowkey_column_num(1);
  table.set_max_used_column_id(18);
  ASSERT_EQ(OB_SUCCESS, table.set_table_name(ObString::make_string("native_default_consumer")));
  DatumValue key, text, null;
  ASSERT_EQ(OB_SUCCESS, key.set_int(918, input));
  ASSERT_EQ(OB_SUCCESS, text.set_string(ObCharType, ObString::make_string("ab   "), input));
  text.set_collation_type(CS_TYPE_UTF8MB4_BIN);
  add_column(table, 16, "id", ObIntType, key, true);
  add_column(table, 17, "padded", ObCharType, text);
  add_column(table, 18, "nullable", ObIntType, null);
  ASSERT_TRUE(table.is_valid());
  storage::ObStorageSchema schema;
  ASSERT_EQ(OB_SUCCESS, schema.init(storage_owner, table));
  table.reset();
  input.reset();
  ObSEArray<ObColDesc, 5> ids;
  for (uint64_t id : {uint64_t(16), uint64_t(17), uint64_t(18),
                     OB_HIDDEN_TRANS_VERSION_COLUMN_ID, OB_HIDDEN_SQL_SEQUENCE_COLUMN_ID}) {
    ObColDesc desc;
    desc.col_id_ = id;
    ASSERT_EQ(OB_SUCCESS, ids.push_back(desc));
  }
  ObDatumRow row;
  ASSERT_EQ(OB_SUCCESS, row.init(row_owner, 5));
  row.count_ = 5;
  ASSERT_EQ(OB_SUCCESS, schema.get_orig_default_row(ids, false, row));
  EXPECT_EQ(918, row.storage_datums_[0].get_int());
  EXPECT_TRUE(row.storage_datums_[0].is_local_buf());
  EXPECT_EQ("ab   ", bytes(row.storage_datums_[1]));
  EXPECT_TRUE(row.storage_datums_[2].is_null());
  EXPECT_EQ(0, row.storage_datums_[3].get_int());
  EXPECT_EQ(0, row.storage_datums_[4].get_int());
  ASSERT_EQ(OB_SUCCESS, schema.get_orig_default_row(ids, true, row));
  EXPECT_EQ("ab", bytes(row.storage_datums_[1]));
  const auto *stored = schema.get_column_schema(17);
  ASSERT_NE(nullptr, stored);
  EXPECT_EQ(stored->default_checksum_, row.storage_datums_[1].checksum(0));
  schema.reset();
  EXPECT_EQ(918, row.storage_datums_[0].get_int());
  EXPECT_EQ(0, row.storage_datums_[3].get_int());
}

int main(int argc, char **argv)
{
  ::testing::InitGoogleTest(&argc, argv);
  if (ObCharset::init_charset() != OB_SUCCESS) { return 1; }
  return RUN_ALL_TESTS();
}
