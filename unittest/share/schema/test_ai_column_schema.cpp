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

#define USING_LOG_PREFIX SHARE_SCHEMA

#include <gtest/gtest.h>
#include "share/schema/ob_ai_column_schema.h"
#include "share/schema/ob_column_schema.h"
#include "share/schema/ob_table_schema.h"

using namespace oceanbase;
using namespace common;
using namespace share::schema;

namespace
{

void fill_valid_schema(ObAIColumnSchema &schema)
{
  schema.set_table_id(500001);
  schema.set_column_id(OB_APP_MIN_COLUMN_ID + 2);
  ASSERT_EQ(OB_SUCCESS, schema.set_column_name(ObString::make_string("summary")));
  ASSERT_EQ(OB_SUCCESS, schema.set_expr_text(ObString::make_string("AI_COMPLETE('m1', content)")));
  ASSERT_EQ(OB_SUCCESS, schema.set_normalized_expr(ObString::make_string("AI_COMPLETE(?1,COL(16))")));
  ASSERT_EQ(OB_SUCCESS, schema.add_dependency_column_id(OB_APP_MIN_COLUMN_ID));
  schema.set_sql_mode(0);
  ASSERT_EQ(OB_SUCCESS, schema.set_time_zone(ObString::make_string("+08:00")));
  schema.set_spec_version(1);
  schema.set_max_retries(5);
  schema.set_state_column_id(OB_APP_MIN_COLUMN_ID + 3);
  schema.set_request_column_id(OB_APP_MIN_COLUMN_ID + 4);
  schema.set_result_spec_column_id(OB_APP_MIN_COLUMN_ID + 5);
  schema.set_schema_version(200);
}

} // namespace

TEST(ObAIColumnSchema, validity_and_assign)
{
  ObAIColumnSchema schema;
  EXPECT_FALSE(schema.is_valid());
  fill_valid_schema(schema);
  ASSERT_TRUE(schema.is_valid());

  ObAIColumnSchema assigned;
  ASSERT_EQ(OB_SUCCESS, assigned.assign(schema));
  ASSERT_TRUE(assigned.is_valid());
  EXPECT_EQ(schema.get_table_id(), assigned.get_table_id());
  EXPECT_EQ(schema.get_column_id(), assigned.get_column_id());
  EXPECT_EQ(schema.get_column_name(), assigned.get_column_name());
  ASSERT_EQ(1, assigned.get_dependency_column_ids().count());
  EXPECT_EQ(OB_APP_MIN_COLUMN_ID, assigned.get_dependency_column_ids().at(0));
  EXPECT_NE(schema.get_expr_text().ptr(), assigned.get_expr_text().ptr());

  assigned.reset();
  EXPECT_FALSE(assigned.is_valid());
  EXPECT_TRUE(assigned.get_expr_text().empty());
  EXPECT_TRUE(assigned.get_dependency_column_ids().empty());
}

TEST(ObAIColumnSchema, serialization_round_trip)
{
  ObAIColumnSchema schema;
  fill_valid_schema(schema);

  const int64_t serialize_size = schema.get_serialize_size();
  ASSERT_GT(serialize_size, 0);
  char buf[8192] = {};
  ASSERT_LE(serialize_size, sizeof(buf));
  int64_t pos = 0;
  ASSERT_EQ(OB_SUCCESS, schema.serialize(buf, serialize_size, pos));
  EXPECT_EQ(serialize_size, pos);

  ObAIColumnSchema decoded;
  pos = 0;
  ASSERT_EQ(OB_SUCCESS, decoded.deserialize(buf, serialize_size, pos));
  EXPECT_EQ(serialize_size, pos);
  ASSERT_TRUE(decoded.is_valid());
  EXPECT_EQ(schema.get_expr_text(), decoded.get_expr_text());
  EXPECT_EQ(schema.get_column_name(), decoded.get_column_name());
  EXPECT_EQ(schema.get_normalized_expr(), decoded.get_normalized_expr());
  EXPECT_EQ(schema.get_spec_version(), decoded.get_spec_version());
}

TEST(ObAIColumnSchema, persistable_before_hidden_column_materialization)
{
  ObAIColumnSchema schema;
  fill_valid_schema(schema);
  schema.set_state_column_id(OB_INVALID_ID);
  schema.set_request_column_id(OB_INVALID_ID);
  schema.set_result_spec_column_id(OB_INVALID_ID);

  EXPECT_TRUE(schema.is_valid_for_persistence());
  EXPECT_FALSE(schema.is_valid());
}

TEST(ObAIColumnSchema, table_schema_ownership_and_serialization)
{
  ObAIColumnSchema ai_schema;
  fill_valid_schema(ai_schema);

  ObTableSchema table_schema;
  table_schema.set_table_id(ai_schema.get_table_id());
  ASSERT_EQ(OB_SUCCESS, table_schema.add_ai_column_schema(ai_schema));
  ASSERT_EQ(1, table_schema.get_ai_column_schema_count());
  ASSERT_NE(nullptr, table_schema.get_ai_column_schema(ai_schema.get_column_id()));
  EXPECT_NE(ai_schema.get_expr_text().ptr(),
            table_schema.get_ai_column_schema(ai_schema.get_column_id())->get_expr_text().ptr());
  EXPECT_EQ(OB_ENTRY_EXIST, table_schema.add_ai_column_schema(ai_schema));

  ObTableSchema assigned;
  ASSERT_EQ(OB_SUCCESS, assigned.assign(table_schema));
  ASSERT_EQ(1, assigned.get_ai_column_schema_count());
  ASSERT_NE(nullptr, assigned.get_ai_column_schema(ai_schema.get_column_id()));

  const int64_t serialize_size = table_schema.get_serialize_size();
  ASSERT_GT(serialize_size, 0);
  char buf[65536] = {};
  ASSERT_LE(serialize_size, sizeof(buf));
  int64_t pos = 0;
  ASSERT_EQ(OB_SUCCESS, table_schema.serialize(buf, serialize_size, pos));
  EXPECT_EQ(serialize_size, pos);

  ObTableSchema decoded;
  pos = 0;
  ASSERT_EQ(OB_SUCCESS, decoded.deserialize(buf, serialize_size, pos));
  EXPECT_EQ(serialize_size, pos);
  ASSERT_EQ(1, decoded.get_ai_column_schema_count());
  ASSERT_NE(nullptr, decoded.get_ai_column_schema(ai_schema.get_column_id()));
  EXPECT_EQ(ai_schema.get_normalized_expr(),
            decoded.get_ai_column_schema(ai_schema.get_column_id())->get_normalized_expr());

  ASSERT_EQ(OB_SUCCESS, decoded.delete_ai_column_schema(ai_schema.get_column_id()));
  EXPECT_EQ(0, decoded.get_ai_column_schema_count());
  EXPECT_EQ(nullptr, decoded.get_ai_column_schema(ai_schema.get_column_id()));
}

TEST(ObAIColumnSchema, pending_add_column_uses_name_key)
{
  ObAIColumnSchema pending;
  fill_valid_schema(pending);
  pending.set_column_id(OB_INVALID_ID);
  ASSERT_EQ(OB_SUCCESS, pending.set_column_name(ObString::make_string("embedding")));
  EXPECT_FALSE(pending.is_valid());

  ObTableSchema table_schema;
  table_schema.set_table_id(pending.get_table_id());
  ObColumnSchemaV2 column;
  column.set_table_id(pending.get_table_id());
  column.set_column_id(OB_APP_MIN_COLUMN_ID + 10);
  column.set_data_type(ObLongTextType);
  column.set_charset_type(CHARSET_UTF8MB4);
  column.set_collation_type(CS_TYPE_UTF8MB4_GENERAL_CI);
  column.set_is_ai_column(true);
  ASSERT_EQ(OB_SUCCESS, column.set_column_name(ObString::make_string("embedding")));
  ASSERT_EQ(OB_SUCCESS, table_schema.add_column(column));
  ASSERT_EQ(OB_SUCCESS, table_schema.add_ai_column_schema(pending));
  ASSERT_NE(nullptr, table_schema.get_ai_column_schema(ObString::make_string("embedding")));
  EXPECT_EQ(OB_INVALID_ID,
            table_schema.get_ai_column_schema(ObString::make_string("embedding"))->get_column_id());
  ASSERT_EQ(OB_SUCCESS, table_schema.bind_ai_column_schema_ids());
  EXPECT_EQ(column.get_column_id(),
            table_schema.get_ai_column_schema(ObString::make_string("embedding"))->get_column_id());
  EXPECT_EQ(table_schema.get_table_id(),
            table_schema.get_ai_column_schema(ObString::make_string("embedding"))->get_table_id());
  EXPECT_EQ(OB_SUCCESS,
            table_schema.delete_ai_column_schema(ObString::make_string("embedding")));
  EXPECT_EQ(0, table_schema.get_ai_column_schema_count());
}

TEST(ObAIColumnSchema, ddl_column_id_redistribution_updates_dependencies)
{
  ObAIColumnSchema ai_schema;
  fill_valid_schema(ai_schema);

  ObTableSchema table_schema;
  table_schema.set_table_id(ai_schema.get_table_id());
  ObColumnSchemaV2 base_column;
  base_column.set_table_id(ai_schema.get_table_id());
  base_column.set_column_id(OB_APP_MIN_COLUMN_ID);
  base_column.set_data_type(ObIntType);
  ASSERT_EQ(OB_SUCCESS,
            base_column.set_column_name(ObString::make_string("content")));
  ASSERT_EQ(OB_SUCCESS, table_schema.add_column(base_column));

  ObColumnSchemaV2 ai_column;
  ai_column.set_table_id(ai_schema.get_table_id());
  ai_column.set_column_id(ai_schema.get_column_id());
  ai_column.set_data_type(ObLongTextType);
  ai_column.set_charset_type(CHARSET_UTF8MB4);
  ai_column.set_collation_type(CS_TYPE_UTF8MB4_GENERAL_CI);
  ai_column.set_is_ai_column(true);
  ASSERT_EQ(OB_SUCCESS,
            ai_column.set_column_name(ObString::make_string("summary")));
  ASSERT_EQ(OB_SUCCESS, table_schema.add_column(ai_column));
  ASSERT_EQ(OB_SUCCESS, table_schema.add_ai_column_schema(ai_schema));

  common::hash::ObHashMap<uint64_t, uint64_t> column_id_map;
  ASSERT_EQ(OB_SUCCESS, column_id_map.create(8, "AIColIdMap"));
  ASSERT_EQ(OB_SUCCESS, column_id_map.set_refactored(
                            OB_APP_MIN_COLUMN_ID,
                            OB_APP_MIN_COLUMN_ID + 10));
  ASSERT_EQ(OB_SUCCESS, column_id_map.set_refactored(
                            ai_schema.get_column_id(),
                            OB_APP_MIN_COLUMN_ID + 11));
  ASSERT_EQ(OB_SUCCESS, table_schema.convert_column_ids_for_ddl(column_id_map));

  const ObAIColumnSchema *converted =
      table_schema.get_ai_column_schema_by_idx(0);
  ASSERT_NE(nullptr, converted);
  EXPECT_EQ(OB_APP_MIN_COLUMN_ID + 11, converted->get_column_id());
  ASSERT_EQ(1, converted->get_dependency_column_ids().count());
  EXPECT_EQ(OB_APP_MIN_COLUMN_ID + 10,
            converted->get_dependency_column_ids().at(0));
}

TEST(ObAIColumnSchema, materialize_three_hidden_columns_idempotently)
{
  ObAIColumnSchema ai_schema;
  fill_valid_schema(ai_schema);
  ai_schema.set_state_column_id(OB_INVALID_ID);
  ai_schema.set_request_column_id(OB_INVALID_ID);
  ai_schema.set_result_spec_column_id(OB_INVALID_ID);

  ObTableSchema table_schema;
  table_schema.set_table_id(ai_schema.get_table_id());
  table_schema.set_schema_version(ai_schema.get_schema_version());

  ObColumnSchemaV2 base_column;
  base_column.set_table_id(ai_schema.get_table_id());
  base_column.set_column_id(OB_APP_MIN_COLUMN_ID);
  base_column.set_data_type(ObVarcharType);
  base_column.set_data_length(128);
  base_column.set_charset_type(CHARSET_UTF8MB4);
  base_column.set_collation_type(CS_TYPE_UTF8MB4_GENERAL_CI);
  ASSERT_EQ(OB_SUCCESS,
            base_column.set_column_name(ObString::make_string("content")));
  ASSERT_EQ(OB_SUCCESS, table_schema.add_column(base_column));

  ObColumnSchemaV2 ai_column;
  ai_column.set_table_id(ai_schema.get_table_id());
  ai_column.set_column_id(ai_schema.get_column_id());
  ai_column.set_data_type(ObLongTextType);
  ai_column.set_charset_type(CHARSET_UTF8MB4);
  ai_column.set_collation_type(CS_TYPE_UTF8MB4_GENERAL_CI);
  ai_column.set_is_ai_column(true);
  ASSERT_EQ(OB_SUCCESS,
            ai_column.set_column_name(ObString::make_string("summary")));
  ASSERT_EQ(OB_SUCCESS, table_schema.add_column(ai_column));
  ASSERT_EQ(OB_SUCCESS, table_schema.add_ai_column_schema(ai_schema));
  ASSERT_EQ(OB_SUCCESS, table_schema.bind_ai_column_schema_ids());

  ASSERT_EQ(OB_SUCCESS, table_schema.materialize_ai_column_hidden_columns());
  const ObAIColumnSchema *materialized =
      table_schema.get_ai_column_schema(ai_schema.get_column_id());
  ASSERT_NE(nullptr, materialized);
  const ObColumnSchemaV2 *state_column =
      table_schema.get_column_schema(materialized->get_state_column_id());
  const ObColumnSchemaV2 *request_column =
      table_schema.get_column_schema(materialized->get_request_column_id());
  const ObColumnSchemaV2 *result_spec_column =
      table_schema.get_column_schema(materialized->get_result_spec_column_id());
  ASSERT_NE(nullptr, state_column);
  ASSERT_NE(nullptr, request_column);
  ASSERT_NE(nullptr, result_spec_column);
  EXPECT_TRUE(state_column->is_hidden());
  EXPECT_TRUE(request_column->is_hidden());
  EXPECT_TRUE(result_spec_column->is_hidden());
  EXPECT_EQ(ObTinyIntType, state_column->get_data_type());
  EXPECT_EQ(ObCharType, request_column->get_data_type());
  EXPECT_EQ(16, request_column->get_data_length());
  EXPECT_EQ(ObUInt64Type, result_spec_column->get_data_type());
  EXPECT_EQ(5, table_schema.get_column_count());

  ASSERT_EQ(OB_SUCCESS, table_schema.materialize_ai_column_hidden_columns());
  EXPECT_EQ(5, table_schema.get_column_count());
  EXPECT_EQ(materialized->get_state_column_id(),
            table_schema.get_ai_column_schema(ai_schema.get_column_id())
                ->get_state_column_id());
}

TEST(ObColumnSchema, ai_column_flag_serialization)
{
  ObColumnSchemaV2 column;
  column.set_table_id(500001);
  column.set_column_id(OB_APP_MIN_COLUMN_ID);
  ASSERT_EQ(OB_SUCCESS, column.set_column_name("ai_result"));
  column.set_data_type(ObVarcharType);

  EXPECT_FALSE(column.is_ai_column());
  column.set_is_ai_column(true);
  EXPECT_TRUE(column.is_ai_column());
  EXPECT_NE(0, column.get_stored_column_flags() & AI_COLUMN_FLAG);
  EXPECT_FALSE(column.is_generated_column());

  const int64_t serialize_size = column.get_serialize_size();
  ASSERT_GT(serialize_size, 0);
  char buf[4096] = {};
  ASSERT_LE(serialize_size, sizeof(buf));
  int64_t pos = 0;
  ASSERT_EQ(OB_SUCCESS, column.serialize(buf, serialize_size, pos));
  EXPECT_EQ(serialize_size, pos);

  ObColumnSchemaV2 decoded;
  pos = 0;
  ASSERT_EQ(OB_SUCCESS, decoded.deserialize(buf, serialize_size, pos));
  EXPECT_EQ(serialize_size, pos);
  EXPECT_TRUE(decoded.is_ai_column());
  EXPECT_FALSE(decoded.is_generated_column());
  EXPECT_NE(0, decoded.get_stored_column_flags() & AI_COLUMN_FLAG);
  decoded.set_is_ai_column(false);
  EXPECT_FALSE(decoded.is_ai_column());
}
