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

#include "storage/ddl/ob_tablet_rebuild_util.h"

#include "storage/blocksstable/ob_sstable.h"
#include "storage/ob_storage_schema.h"
#include "storage/tablet/ob_tablet.h"
#include "storage/tablet/ob_tablet_create_sstable_param.h"

namespace oceanbase
{
namespace storage
{

int ObTabletRebuildUtil::get_clipped_storage_schema_on_demand(
    common::ObIAllocator &allocator,
    common::hash::ObHashMap<ObITable::TableKey, ObStorageSchema*> &clipped_schemas_map,
    const common::ObTabletID &tablet_id,
    const blocksstable::ObSSTable &sstable,
    const ObStorageSchema &latest_schema,
    const bool try_create,
    const ObStorageSchema *&storage_schema)
{
  int ret = OB_SUCCESS;
  storage_schema = nullptr;
  ObSSTableMetaHandle meta_handle;
  if (OB_UNLIKELY(!latest_schema.is_valid())) {
    ret = OB_INVALID_ARGUMENT;
    LOG_WARN("invalid arg", K(ret), K(latest_schema));
  } else if (!sstable.is_major_sstable()) {
    storage_schema = &latest_schema;
  } else if (!clipped_schemas_map.created() && OB_FAIL(clipped_schemas_map.create(8/*bucket_num*/, "ClippedSchema"))) {
    LOG_WARN("create clipped schema map failed", K(ret));
  } else if (OB_FAIL(sstable.get_meta(meta_handle))) {
    LOG_WARN("get sstable meta failed", K(ret), K(sstable));
  } else {
    int64_t schema_stored_cols_cnt = 0;
    ObStorageSchema *target_storage_schema = nullptr;
    schema_stored_cols_cnt = meta_handle.get_sstable_meta().get_schema_column_count();
    const ObITable::TableKey &table_key = sstable.get_key();
    if (OB_FAIL(clipped_schemas_map.get_refactored(table_key, target_storage_schema))) {
      void *buf = nullptr;
      target_storage_schema = nullptr;
      ObUpdateCSReplicaSchemaParam update_param;
      if (OB_HASH_NOT_EXIST != ret) {
        LOG_WARN("get storage schema failed", K(ret), K(table_key));
      } else if (OB_UNLIKELY(!try_create)) {
        ret = OB_ERR_UNEXPECTED;
        LOG_WARN("clipped storage schema not found", K(ret), K(schema_stored_cols_cnt), K(sstable));
      } else if (OB_ISNULL(buf = allocator.alloc(sizeof(ObStorageSchema)))) {
        ret = OB_ALLOCATE_MEMORY_FAILED;
        LOG_WARN("alloc mem failed", K(ret));
      } else if (OB_FALSE_IT(target_storage_schema = new(buf) ObStorageSchema())) {
      } else if (OB_FAIL(update_param.init(tablet_id,
          schema_stored_cols_cnt + ObMultiVersionRowkeyHelpper::get_extra_rowkey_col_cnt(),
          ObUpdateCSReplicaSchemaParam::UpdateType::TRUNCATE_COLUMN_ARRAY))) {
        LOG_WARN("update param init failed", K(ret), K(tablet_id));
      } else if (OB_FAIL(target_storage_schema->init(allocator,
          latest_schema/*old_schema*/,
          false/*skip_column_info*/,
          nullptr/*column_group_schema*/,
          false/*generate_cs_replica_cg_array*/,
          &update_param))) {
        LOG_WARN("init storage schema failed", K(ret), K(update_param));
      } else if (OB_FAIL(clipped_schemas_map.set_refactored(table_key, target_storage_schema))) {
        LOG_WARN("set clipped schema failed", K(ret), K(table_key));
      } else {
        target_storage_schema->schema_version_ = meta_handle.get_sstable_meta().get_schema_version();
        target_storage_schema->progressive_merge_round_ = meta_handle.get_sstable_meta().get_progressive_merge_round();
        storage_schema = target_storage_schema;
      }

      if (OB_FAIL(ret) && nullptr != buf) {
        if (nullptr != target_storage_schema) {
          target_storage_schema->~ObStorageSchema();
          target_storage_schema = nullptr;
        }
        allocator.free(buf);
        buf = nullptr;
      }
    } else {
      storage_schema = target_storage_schema;
    }
  }

  if (OB_SUCC(ret) && OB_UNLIKELY(nullptr == storage_schema || !storage_schema->is_valid())) {
    ret = OB_ERR_UNEXPECTED;
    LOG_WARN("unexpected storage schema", K(ret), K(sstable), KPC(storage_schema));
  }
  return ret;
}

int ObTabletRebuildUtil::check_need_fill_empty_sstable(
    ObLSHandle &ls_handle,
    const bool is_minor_sstable,
    const ObITable::TableKey &table_key,
    const common::ObTabletID &dst_tablet_id,
    bool &need_fill_empty_sstable,
    share::SCN &end_scn)
{
  int ret = OB_SUCCESS;
  ObTabletHandle dst_tablet_handle;
  ObTabletMemberWrapper<ObTabletTableStore> table_store_handle;
  need_fill_empty_sstable = false;
  end_scn.reset();

  if (is_minor_sstable) {
    if (OB_FAIL(ObDDLUtil::ddl_get_tablet(ls_handle, dst_tablet_id, dst_tablet_handle,
        ObMDSGetTabletMode::READ_ALL_COMMITED))) {
      LOG_WARN("get tablet failed", K(ret), K(dst_tablet_id));
    } else if (OB_FAIL(dst_tablet_handle.get_obj()->fetch_table_store(table_store_handle))) {
      LOG_WARN("fetch table store failed", K(ret), K(dst_tablet_id));
    } else {
      ObITable *first_dst_table = table_store_handle.get_member()->get_minor_sstables().get_boundary_table(false/*is_last*/);
      const share::SCN dst_start_scn = nullptr != first_dst_table
          ? first_dst_table->get_key().get_start_scn()
          : dst_tablet_handle.get_obj()->get_tablet_meta().clog_checkpoint_scn_;
      if (table_key.get_end_scn() < dst_start_scn) {
        need_fill_empty_sstable = true;
        end_scn = dst_start_scn;
      }
    }
  }
  return ret;
}

int ObTabletRebuildUtil::build_create_empty_sstable_param(
    const blocksstable::ObSSTableBasicMeta &meta,
    const ObITable::TableKey &table_key,
    const common::ObTabletID &dst_tablet_id,
    const share::SCN &end_scn,
    ObTabletCreateSSTableParam &create_sstable_param)
{
  int ret = OB_SUCCESS;
  if (OB_UNLIKELY(!meta.is_valid() || !table_key.is_valid() || !dst_tablet_id.is_valid())) {
    ret = OB_INVALID_ARGUMENT;
    LOG_WARN("invalid arg", K(ret), K(meta), K(table_key), K(dst_tablet_id));
  } else if (OB_FAIL(create_sstable_param.init_for_split_empty_minor_sstable(
      dst_tablet_id,
      table_key.get_end_scn()/*start_scn*/,
      end_scn,
      meta))) {
    LOG_WARN("init empty sstable param failed", K(ret), K(meta), K(table_key), K(dst_tablet_id), K(end_scn));
  }
  return ret;
}

} // namespace storage
} // namespace oceanbase
