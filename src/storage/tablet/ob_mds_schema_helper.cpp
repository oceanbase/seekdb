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

#include "storage/tablet/ob_mds_schema_helper.h"

#include "share/schema/ob_column_schema.h"

#define USING_LOG_PREFIX STORAGE

using namespace oceanbase::common;
using namespace oceanbase::share;
using namespace oceanbase::share::schema;

namespace oceanbase
{
namespace storage
{
ObMdsSchemaHelper::ObMdsSchemaHelper()
  : is_inited_(false),
    allocator_(),
    table_schema_(),
    storage_schema_(),
    rowkey_read_info_()
{
}

ObMdsSchemaHelper::~ObMdsSchemaHelper()
{
  reset();
}

ObMdsSchemaHelper &ObMdsSchemaHelper::get_instance()
{
  static ObMdsSchemaHelper helper;
  return helper;
}

int ObMdsSchemaHelper::init()
{
  int ret = OB_SUCCESS;

  if (OB_UNLIKELY(is_inited_)) {
    ret = OB_INIT_TWICE;
    LOG_WARN("init twice", K(ret), K_(is_inited));
  } else {
     // mock
    if (OB_FAIL(build_table_schema(DATABASE_ID, MDS_TABLE_ID, MDS_TABLE_NAME, table_schema_))) {
    } else if (OB_UNLIKELY(!table_schema_.is_valid())) {
      ret = OB_ERR_UNEXPECTED;
      LOG_WARN("invalid table schema", K(ret), K_(table_schema));
    } else if (OB_FAIL(storage_schema_.init(allocator_, table_schema_))) {
    } else if (OB_UNLIKELY(!storage_schema_.is_valid())) {
      ret = OB_ERR_UNEXPECTED;
      LOG_WARN("invalid storage schema", K(ret), K_(storage_schema));
    } else if (OB_FAIL(build_rowkey_read_info(allocator_, storage_schema_, rowkey_read_info_))) {
    } else if (OB_UNLIKELY(!rowkey_read_info_.is_valid())) {
      ret = OB_ERR_UNEXPECTED;
      LOG_WARN("invalid rowkey read info", K(ret), K_(rowkey_read_info));
    } else {
      is_inited_ = true;
    }

    if (OB_FAIL(ret)) {
      reset();
    }
  }

  return ret;
}

void ObMdsSchemaHelper::reset()
{
  rowkey_read_info_.reset();
  storage_schema_.reset();
  table_schema_.reset();
  allocator_.reset();
  is_inited_ = false;
}

const ObStorageSchema *ObMdsSchemaHelper::get_storage_schema() const
{
  int ret = OB_SUCCESS;
  const ObStorageSchema *ptr = nullptr;

  if (OB_UNLIKELY(!is_inited_)) {
    ret = OB_NOT_INIT;
    LOG_WARN("not inited", K(ret), K_(is_inited));
  } else {
    ptr = &storage_schema_;
  }

  return ptr;
}

const share::schema::ObTableSchema *ObMdsSchemaHelper::get_table_schema() const
{
  int ret = OB_SUCCESS;
  const share::schema::ObTableSchema *ptr = nullptr;

  if (OB_UNLIKELY(!is_inited_)) {
    ret = OB_NOT_INIT;
    LOG_WARN("not inited", K(ret), K_(is_inited));
  } else {
    ptr = &table_schema_;
  }

  return ptr;
}

const ObRowkeyReadInfo *ObMdsSchemaHelper::get_rowkey_read_info() const
{
  int ret = OB_SUCCESS;
  const ObRowkeyReadInfo *ptr = nullptr;

  if (OB_UNLIKELY(!is_inited_)) {
    ret = OB_NOT_INIT;
    LOG_WARN("not inited", K(ret), K_(is_inited));
  } else {
    ptr = &rowkey_read_info_;
  }

  return ptr;
}


int ObMdsSchemaHelper::build_table_schema(const int64_t database_id,
    const uint64_t table_id,
    const char *table_name,
    share::schema::ObTableSchema &table_schema)
{
  int ret = OB_SUCCESS;

  /*
   * mds schema:
   *
   * MDS_TYPE UDF_KEY SNAPSHOT SEQ_NO META_INFO USER_DATA
   * tiny_int binary  int      int    binary    binary
   *
   * SNAPSHOT column and SEQ_NO column are multi version columns
   */
  ObObjMeta mds_type_meta;
  mds_type_meta.set_tinyint();
  ObObjMeta udf_key_meta;
  udf_key_meta.set_binary();
  ObObjMeta meta_info_meta;
  meta_info_meta.set_binary();
  ObObjMeta user_data_meta;
  user_data_meta.set_binary();

  ObColumnSchemaV2 mds_type_column_schema;
  ObColumnSchemaV2 udf_key_column_schema;
  ObColumnSchemaV2 meta_info_column_schema;
  ObColumnSchemaV2 user_data_column_schema;

  if (OB_FAIL(build_column_schema(table_id,
      MDS_TYPE_COLUMN_ID,
      MDS_TYPE_COLUMN_NAME,
      COLUMN_SCHEMA_VERSION,
      1/*rowkey_position*/,
      ObOrderType::ASC,
      mds_type_meta,
      MDS_TYPE_DATA_LENGTH,
      mds_type_column_schema))) {
  } else if (OB_FAIL(build_column_schema(table_id,
      UDF_KEY_COLUMN_ID,
      UDF_KEY_COLUMN_NAME,
      COLUMN_SCHEMA_VERSION,
      2/*rowkey_position*/,
      ObOrderType::ASC,
      udf_key_meta,
      UDF_KEY_DATA_LENGTH,
      udf_key_column_schema))) {
  } else if (OB_FAIL(build_column_schema(table_id,
      META_INFO_COLUMN_ID,
      META_INFO_COLUMN_NAME,
      COLUMN_SCHEMA_VERSION,
      0/*rowkey_position*/,
      ObOrderType::ASC,
      meta_info_meta,
      META_INFO_DATA_LENGTH,
      meta_info_column_schema))) {
  } else if (OB_FAIL(build_column_schema(table_id,
      USER_DATA_COLUMN_ID,
      USER_DATA_COLUMN_NAME,
      COLUMN_SCHEMA_VERSION,
      0/*rowkey_position*/,
      ObOrderType::ASC,
      user_data_meta,
      USER_DATA_DATA_LENGTH,
      user_data_column_schema))) {
  }

  if (OB_SUCC(ret)) {
    
    table_schema.set_database_id(database_id);
    table_schema.set_table_id(table_id);
    table_schema.set_rowkey_column_num(ROWKEY_COLUMN_NUM);
    table_schema.set_compress_func_name("none");
    table_schema.set_row_store_type(ObRowStoreType::FLAT_ROW_STORE);
    table_schema.set_table_name(MDS_TABLE_NAME);
    table_schema.set_schema_version(MDS_SCHEMA_VERSION);
  }

  if (OB_FAIL(ret)) {
  } else if (OB_FAIL(table_schema.add_column(mds_type_column_schema))) {
  } else if (OB_FAIL(table_schema.add_column(udf_key_column_schema))) {
  } else if (OB_FAIL(table_schema.add_column(meta_info_column_schema))) {
  } else if (OB_FAIL(table_schema.add_column(user_data_column_schema))) {
  }

  if (OB_FAIL(ret)) {
    table_schema.reset();
  }

  return ret;
}

int ObMdsSchemaHelper::build_rowkey_read_info(
    common::ObIAllocator &allocator,
    const ObStorageSchema &storage_schema,
    ObRowkeyReadInfo &rowkey_read_info)
{
  int ret = OB_SUCCESS;
  common::ObSEArray<share::schema::ObColDesc, 16> cols_desc;
  int64_t full_stored_col_cnt = 0;

  if (OB_FAIL(storage_schema.get_mulit_version_rowkey_column_ids(cols_desc))) {
  } else if (OB_FAIL(storage_schema.get_store_column_count(full_stored_col_cnt, true/*full_col*/))) {
  } else if (OB_FAIL(rowkey_read_info.init(
      allocator,
      full_stored_col_cnt,
      storage_schema.get_rowkey_column_num(),
      cols_desc))) {
  }

  return ret;
}

int ObMdsSchemaHelper::build_column_schema(const uint64_t table_id,
    const uint64_t column_id,
    const char *column_name,
    const int64_t schema_version,
    const int64_t rowkey_position,
    const common::ObOrderType &order_in_rowkey,
    const common::ObObjMeta &meta_type,
    const int64_t data_length,
    share::schema::ObColumnSchemaV2 &column_schema)
{
  int ret = OB_SUCCESS;

  
  column_schema.set_table_id(table_id);
  column_schema.set_column_id(column_id);
  column_schema.set_schema_version(schema_version);
  column_schema.set_rowkey_position(rowkey_position);
  column_schema.set_order_in_rowkey(order_in_rowkey);
  column_schema.set_meta_type(meta_type);
  column_schema.set_data_length(data_length);

  if (OB_FAIL(column_schema.set_column_name(column_name))) {
  }

  return ret;
}
} // namespace storage
} // namespace oceanbase
