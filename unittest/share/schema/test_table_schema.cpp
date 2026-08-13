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
#include "share/schema/ob_column_schema.h"
#include "share/schema/ob_table_schema.h"

using namespace oceanbase;
using namespace common;
using namespace share::schema;

TEST(ObTableSchema, alter_column_updates_name_hash)
{
  ObTableSchema table_schema;
  ObColumnSchemaV2 column;
  table_schema.set_table_id(500001);
  table_schema.set_database_id(201001);
  ASSERT_EQ(OB_SUCCESS, table_schema.set_table_name("t1"));

  column.set_table_id(table_schema.get_table_id());
  column.set_column_id(OB_APP_MIN_COLUMN_ID);
  ASSERT_EQ(OB_SUCCESS, column.set_column_name("c1"));
  column.set_data_type(ObIntType);
  column.set_rowkey_position(1);
  ASSERT_EQ(OB_SUCCESS, table_schema.add_column(column));

  column.reset();
  column.set_table_id(table_schema.get_table_id());
  column.set_column_id(OB_APP_MIN_COLUMN_ID + 1);
  ASSERT_EQ(OB_SUCCESS, column.set_column_name("c2"));
  column.set_data_type(ObIntType);
  ASSERT_EQ(OB_SUCCESS, table_schema.add_column(column));
  table_schema.set_max_used_column_id(OB_APP_MIN_COLUMN_ID + 1);

  ObColumnSchemaV2 renamed_column;
  const ObColumnSchemaV2 *original_column = table_schema.get_column_schema("c1");
  ASSERT_NE(nullptr, original_column);
  ASSERT_EQ(OB_SUCCESS, renamed_column.assign(*original_column));
  ASSERT_EQ(OB_SUCCESS, renamed_column.set_column_name("c3"));
  ASSERT_EQ(OB_SUCCESS, table_schema.alter_column(
      renamed_column, ObTableSchema::CHECK_MODE_OFFLINE, false));

  EXPECT_EQ(nullptr, table_schema.get_column_schema("c1"));
  ASSERT_NE(nullptr, table_schema.get_column_schema("c3"));
  EXPECT_EQ(OB_SUCCESS, table_schema.reorder_column(
      ObString::make_string("c3"), false, ObString::make_string("c2"), ObString()));
}
