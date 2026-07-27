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

#define private public
#define protected public

#include "share/schema/ob_table_schema.h"
#include "share/schema/ob_column_schema.h"
#include "storage/access/ob_table_param.h"

namespace oceanbase
{
using namespace common;
using namespace storage;

namespace unittest
{
class TestSchemaPrepare
{
public:
  static void prepare_schema(
    share::schema::ObTableSchema &table_schema,
    const int64_t rowkey_column_cnt = TEST_ROWKEY_COLUMN_CNT,
    const int64_t column_cnt = TEST_COLUMN_CNT,
    const int64_t micro_block_size = DEFAULT_MICRO_BLOCK_SIZE);
  
  static const int64_t TABLE_ID = 7777;
  static const int64_t TEST_ROWKEY_COLUMN_CNT = 3;
  static const int64_t TEST_COLUMN_CNT = 6;
  static const int64_t DEFAULT_MICRO_BLOCK_SIZE = 16 * 1024;

};

void TestSchemaPrepare::prepare_schema(
  share::schema::ObTableSchema &table_schema,
  const int64_t rowkey_column_cnt,
  const int64_t column_cnt,
  const int64_t micro_block_size)
{
  int ret = OB_SUCCESS;
  const uint64_t table_id = TABLE_ID;
  ASSERT_TRUE(column_cnt >= rowkey_column_cnt);
  share::schema::ObColumnSchemaV2 column;

  //generate data table schema
  table_schema.reset();
  ret = table_schema.set_table_name("test_merge_multi_version");
  ASSERT_EQ(OB_SUCCESS, ret);
  table_schema.set_database_id(1);
  table_schema.set_table_id(table_id);
  table_schema.set_rowkey_column_num(rowkey_column_cnt);
  table_schema.set_max_used_column_id(common::OB_APP_MIN_COLUMN_ID + column_cnt);
  table_schema.set_block_size(micro_block_size);
  table_schema.set_compress_func_name("none");
  table_schema.set_row_store_type(FLAT_ROW_STORE);
  table_schema.set_pctfree(10);
  //init column
  char name[OB_MAX_FILE_NAME_LENGTH];
  memset(name, 0, sizeof(name));
  for(int64_t i = 0; i < column_cnt; ++i){
    ObObjType obj_type = ObIntType;
    const int64_t column_id = common::OB_APP_MIN_COLUMN_ID + i;

    if (i == 1) {
      obj_type = ObVarcharType;
    }
    column.reset();
    column.set_table_id(table_id);
    column.set_column_id(column_id);
    sprintf(name, "test%020ld", i);
    ASSERT_EQ(OB_SUCCESS, column.set_column_name(name));
    column.set_data_type(obj_type);
    column.set_collation_type(CS_TYPE_UTF8MB4_GENERAL_CI);
    column.set_data_length(10);
    if (i < rowkey_column_cnt) {
      column.set_rowkey_position(i + 1);
    } else {
      column.set_rowkey_position(0);
    }

    share::schema::ObSkipIndexColumnAttr skip_idx_attr;
    if (!is_lob_storage(obj_type)) {
      skip_idx_attr.set_min_max();
      column.set_skip_index_attr(skip_idx_attr.get_packed_value());
    }
    COMMON_LOG(INFO, "add column", K(i), K(column));
    ASSERT_EQ(OB_SUCCESS, table_schema.add_column(column));
  }
  COMMON_LOG(INFO, "dump stable schema", LITERAL_K(TEST_ROWKEY_COLUMN_CNT), K(table_schema));
}

}
}
