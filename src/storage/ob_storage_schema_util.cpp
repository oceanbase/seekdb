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

#define USING_LOG_PREFIX STORAGE

#include "ob_storage_schema_util.h"

namespace oceanbase
{

using namespace common;
using namespace share::schema;

namespace storage
{

int ObStorageSchemaUtil::update_tablet_storage_schema(
    const common::ObTabletID &tablet_id,
    common::ObIAllocator &allocator,
    const ObStorageSchema &old_schema_on_tablet,
    const ObStorageSchema &param_schema,
    ObStorageSchema *&new_storage_schema_ptr)
{
  int ret = OB_SUCCESS;
  int64_t tablet_schema_stored_col_cnt = 0;
  int64_t param_schema_stored_col_cnt = 0;

  if (OB_UNLIKELY(!old_schema_on_tablet.is_valid() || !param_schema.is_valid() || NULL != new_storage_schema_ptr)) {
    ret = OB_INVALID_ARGUMENT;
    LOG_WARN("input schema is invalid", K(ret), K(old_schema_on_tablet), K(param_schema), KPC(new_storage_schema_ptr));
  } else if (OB_FAIL(old_schema_on_tablet.get_store_column_count(tablet_schema_stored_col_cnt, true/*full_col*/))) {
  } else if (OB_FAIL(param_schema.get_store_column_count(param_schema_stored_col_cnt, true/*full_col*/))) {
  } else {
    const int64_t tablet_schema_version = old_schema_on_tablet.schema_version_;
    const int64_t param_schema_version = param_schema.schema_version_;
    // A schema from major merge contains complete column information, so prefer the
    // parameter schema when both schemas have the same stored-column count.
    const ObStorageSchema *input_schema = tablet_schema_stored_col_cnt > param_schema_stored_col_cnt
                        ? &old_schema_on_tablet
                        : &param_schema;
    const ObStorageSchema *other_schema = input_schema == &old_schema_on_tablet 
                        ? &param_schema 
                        : &old_schema_on_tablet;
    const int64_t result_schema_column_cnt = MAX(old_schema_on_tablet.get_column_count(), param_schema.get_column_count());
    const bool column_info_simplified = input_schema->get_store_column_schemas().count() != result_schema_column_cnt;
    const int64_t input_progressive_merge_round = input_schema->get_progressive_merge_round();
    const int64_t other_progressive_merge_round = other_schema->get_progressive_merge_round();
    if (OB_FAIL(alloc_storage_schema(allocator, new_storage_schema_ptr))) {
    } else if (OB_FAIL(new_storage_schema_ptr->init(allocator, *input_schema, column_info_simplified))) {
    } else {
      new_storage_schema_ptr->column_cnt_ = result_schema_column_cnt;
      new_storage_schema_ptr->store_column_cnt_ = MAX(tablet_schema_stored_col_cnt, param_schema_stored_col_cnt);
      new_storage_schema_ptr->schema_version_ = MAX(tablet_schema_version, param_schema_version);
      if (other_progressive_merge_round > input_progressive_merge_round) {
        new_storage_schema_ptr->progressive_merge_round_ = other_schema->get_progressive_merge_round();
        new_storage_schema_ptr->row_store_type_ = other_schema->get_row_store_type();
        new_storage_schema_ptr->block_size_ = other_schema->get_block_size();
        new_storage_schema_ptr->compressor_type_ = other_schema->get_compressor_type();
      }
      if (OB_UNLIKELY(!new_storage_schema_ptr->is_valid())) {
        ret = OB_ERR_UNEXPECTED;
        LOG_ERROR("generated schema is invalid", KR(ret), KPC(new_storage_schema_ptr), K(old_schema_on_tablet), K(param_schema));
      } else if (param_schema_version > tablet_schema_version
          || param_schema_stored_col_cnt > tablet_schema_stored_col_cnt) {
        // ATTENTION! Critical diagnostic log, DO NOT CHANGE!!!
        LOG_INFO("success to init storage schema from param_schema",
            K(tablet_id), K(tablet_schema_version), K(param_schema_version),
            K(tablet_schema_stored_col_cnt), K(param_schema_stored_col_cnt),
            K(input_progressive_merge_round), K(other_progressive_merge_round),
            KPC(new_storage_schema_ptr), K(lbt()));
      }
    }
  }

  if (OB_FAIL(ret)) {
    free_storage_schema(allocator, new_storage_schema_ptr);
  }

  return ret;
}

int ObStorageSchemaUtil::alloc_storage_schema(
    common::ObIAllocator &allocator,
    ObStorageSchema *&new_storage_schema)
{
  int ret = OB_SUCCESS;
  void *buffer = allocator.alloc(sizeof(ObStorageSchema));

  if (OB_ISNULL(buffer)) {
    ret = common::OB_ALLOCATE_MEMORY_FAILED;
    STORAGE_LOG(WARN, "fail to allocate mem for storage schema", K(ret));
  } else {
    new_storage_schema = new (buffer) ObStorageSchema();
  }
  return ret;
}

void ObStorageSchemaUtil::free_storage_schema(
    common::ObIAllocator &allocator,
    ObStorageSchema *&storage_schema)
{
  if (NULL != storage_schema) {
    storage_schema->~ObStorageSchema();
    allocator.free(storage_schema);
    storage_schema = nullptr;
  }
}

} // namespace storage
} // namespace oceanbase
