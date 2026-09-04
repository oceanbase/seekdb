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
#include "storage/ddl/ob_ddl_direct_load_utils.h"
#include "share/rc/ob_server_runtime.h"
#include "storage/tx_storage/ob_ls_service.h"
#include "storage/ddl/ob_tablet_ddl_kv.h"
namespace oceanbase
{
namespace storage
{


/* use to check ddl need to do major merge,
*  notice don't use it to judge whether major exist 
*/
int ObDDLDirectLoadUtil::is_ddl_need_major_merge(const ObTablet &tablet, bool &ddl_need_merging)
{
  int ret = OB_SUCCESS;
  ddl_need_merging = false;
  ObTableStoreIterator ddl_iter;
  ObArenaAllocator arena(ObMemAttr("Ddl_Com_MgrU"));
  ObTabletDDLCompleteMdsUserData ddl_complete;
  if (!tablet.is_valid()) {
    ret = OB_INVALID_ARGUMENT;
    LOG_WARN("invalid argument", K(ret));
  } else if (tablet.get_major_table_count() > 0) { /*check major exist */
    ddl_need_merging = false;
  } else if (OB_FAIL(tablet.get_ddl_sstables(ddl_iter))) {
  } else if (ddl_iter.is_valid()) { // indicates the existence of ddl sstable
    ddl_need_merging = true;
    LOG_WARN("major sstable do not exit, need to wait ddl merge", K(ret), "tablet_id", tablet.get_tablet_meta().tablet_id_);
  } else if (OB_FAIL(tablet.get_ddl_complete(SCN::max_scn(), arena, ddl_complete))) {
  } else if (ddl_complete.has_complete_) {
    ddl_need_merging = true;
    LOG_WARN("major sstable do not exit, need to wait ddl merge", K(ret), "tablet_id", tablet.get_tablet_meta().tablet_id_);
  }
  return ret;
}



ObDirectLoadType ObDDLDirectLoadUtil::ddl_get_direct_load_type()
{
  return ObDirectLoadType::IDEM_DIRECT_LOAD_DDL;
}

int ObDDLDirectLoadUtil::generate_merge_param(const ObTabletDDLCompleteArg &arg, ObDDLTableMergeDagParam &merge_param)
{
  int ret = OB_SUCCESS;
  if (!arg.is_valid()) {
    ret = OB_INVALID_ARGUMENT;
    LOG_WARN("invalid argument", K(ret), K(arg));
  } else {
    merge_param.direct_load_type_ = arg.direct_load_type_;
    merge_param.tablet_id_ = arg.tablet_id_;
    merge_param.data_format_version_ = arg.data_format_version_;
    merge_param.snapshot_version_ = arg.snapshot_version_;
    merge_param.start_scn_ = arg.start_scn_;
    merge_param.table_key_ = arg.table_key_;
    merge_param.is_commit_ = true;
  }
  return ret;
}

int ObDDLDirectLoadUtil::generate_merge_param(const ObTabletDDLCompleteMdsUserData &data, ObTablet &tablet, ObDDLTableMergeDagParam &merge_param)
{
  int ret = OB_SUCCESS;
  share::SCN mock_scn;
  mock_scn.convert_for_tx(DDL_START_SCN_VAL);

  if (!data.is_valid()) {
    ret = OB_INVALID_ARGUMENT;
    LOG_WARN("invalid argument", K(ret), K(data));
  } else if (data.has_complete_) {  /* generate param for major merge */
    merge_param.direct_load_type_ = data.direct_load_type_;
    merge_param.tablet_id_ = tablet.get_tablet_id();
    merge_param.data_format_version_ = data.data_format_version_;
    merge_param.snapshot_version_ =   data.snapshot_version_;
    merge_param.start_scn_ = mock_scn;
    merge_param.rec_scn_   = mock_scn;
    merge_param.table_key_ = data.table_key_; 
    merge_param.is_commit_ = true;

    if (OB_FAIL(merge_param.user_data_.assign(merge_param.arena_, data))) {
    }
  } else {  /* generate param for freeze */
    ObDDLKvMgrHandle ddl_kv_mgr_handle;
    ObArray<ObDDLKVHandle> ddl_kvs_handle;
    if (OB_FAIL(tablet.get_ddl_kv_mgr(ddl_kv_mgr_handle, false /*not create*/))) {
    } else if (OB_FAIL(ddl_kv_mgr_handle.get_obj()->get_ddl_kvs(false/*frozen_only*/, ddl_kvs_handle))) {
    } else if (OB_FAIL(ddl_kvs_handle.empty())) {
      ret = OB_ERR_UNEXPECTED;
      LOG_WARN("failed to get ddl kv_mgr_handle", K(ret));
    } else {
      merge_param.direct_load_type_    = ObDirectLoadType::IDEM_DIRECT_LOAD_DDL; // mock type
      merge_param.tablet_id_           = tablet.get_tablet_id();
      merge_param.data_format_version_ = ddl_kvs_handle.at(0).get_obj()->get_data_format_version();
      merge_param.snapshot_version_    = ddl_kvs_handle.at(0).get_obj()->get_snapshot_version();
      merge_param.start_scn_           = mock_scn;
      merge_param.is_commit_           = false;
      merge_param.table_key_.table_type_ = ObITable::TableType::DDL_DUMP_SSTABLE;
      merge_param.table_key_.tablet_id_ = tablet.get_tablet_id();
      merge_param.table_key_.scn_range_.start_scn_ = SCN::scn_dec(mock_scn);
      merge_param.table_key_.scn_range_.end_scn_ = mock_scn;
      merge_param.table_key_.version_range_.snapshot_version_ = ddl_kvs_handle.at(0).get_obj()->get_snapshot_version();
    }
  }
  return ret;
}

int ObDDLDirectLoadUtil::prepare_schema_item_for_vec_idx_data(ObSchemaGetterGuard &schema_guard,
    const ObTableSchema *table_schema,
    const ObTableSchema *&data_table_schema,
    ObIAllocator &allocator,
    ObTableSchemaItem &schema_item)
{
  int ret = OB_SUCCESS;
  ObSEArray<uint64_t , 1> col_ids;
  uint64_t with_param_table_tid;
  // for hnsw, table_schema here is snapshot table, need to get related delta buffer table.
  ObIndexType index_type = INDEX_TYPE_VEC_DELTA_BUFFER_LOCAL;

  // ivf param is saved in centroid table's schema
  if (table_schema->is_vec_ivfflat_index()) {
    index_type = INDEX_TYPE_VEC_IVFFLAT_CENTROID_LOCAL;
  } else if (table_schema->is_vec_ivfsq8_index()) {
    index_type = INDEX_TYPE_VEC_IVFSQ8_CENTROID_LOCAL;
  } else if (table_schema->is_vec_ivfpq_index()) {
    index_type = INDEX_TYPE_VEC_IVFPQ_CENTROID_LOCAL;
  }
  const ObTableSchema *with_param_table_schema = nullptr;
  // get data schema
  if (OB_FAIL(schema_guard.get_table_schema( table_schema->get_data_table_id(), data_table_schema))) {
  } else if (OB_ISNULL(data_table_schema)) {
    ret = OB_TABLE_NOT_EXIST;
    LOG_WARN("table not exist", K(ret), K(table_schema->get_data_table_id()));
  } else if (OB_FAIL(ObVectorIndexUtil::get_vector_index_column_id(*data_table_schema, *table_schema, col_ids))) {
  } else if (col_ids.count() != 1) {
    ret = OB_ERR_UNEXPECTED;
    LOG_WARN("get invalid col id array", K(ret), K(col_ids));
  } else {
    if (index_type == INDEX_TYPE_VEC_DELTA_BUFFER_LOCAL) {
      ObString index_prefix;
      if (OB_FAIL(ObVectorIndexUtil::get_vector_index_prefix(*table_schema, index_prefix))) {
      } else if (OB_FAIL(ObVectorIndexUtil::get_vector_index_tid_with_index_prefix(&schema_guard,
                                                                                   *data_table_schema,
                                                                                   index_type,
                                                                                   col_ids.at(0),
                                                                                   index_prefix,
                                                                                   with_param_table_tid))) {
      }
    } else { // ivf centroid tables
      if (OB_FAIL(ObVectorIndexUtil::get_vector_index_tid(&schema_guard,
                                                          *data_table_schema,
                                                          index_type,
                                                          col_ids.at(0),
                                                          with_param_table_tid))) {
      }
    }
  }

  if (OB_FAIL(ret)) {
  } else if (OB_FAIL(schema_guard.get_table_schema( with_param_table_tid, with_param_table_schema))) {
  } else if (OB_ISNULL(with_param_table_schema)) {
    ret = OB_TABLE_NOT_EXIST;
    LOG_WARN("table not exist", K(ret), K(with_param_table_tid));
  } else if (OB_FAIL(ObVectorIndexUtil::get_vector_index_column_dim(*with_param_table_schema, *data_table_schema, schema_item.vec_dim_))) {
  } else if (schema_item.vec_dim_ == 0) {
    ret = OB_ERR_UNEXPECTED;
    LOG_WARN("get vector dim is zero, fail to calc", K(ret), K(schema_item.vec_dim_), KPC(with_param_table_schema));
  } else if (OB_FAIL(ob_write_string(allocator, with_param_table_schema->get_index_params(), schema_item.vec_idx_param_))) {
  }
  return ret;
}

int ObDDLDirectLoadUtil::get_tablet_handle(const ObTabletID &tablet_id, ObTabletHandle &tablet_handle)
{
  int ret = OB_SUCCESS;
  ObLS *ls = nullptr;
  ObLSService * ls_service = nullptr;
  tablet_handle.reset();
  if (!tablet_id.is_valid()) {
    ret = OB_INVALID_ARGUMENT;
    LOG_WARN("invalid argument", K(ret), K(tablet_id));
  } else if (OB_ISNULL(ls_service = ::oceanbase::share::server_service<::oceanbase::storage::ObLSService>())) {
    ret = OB_ERR_UNEXPECTED;
    LOG_WARN("unexpected err", K(ret));
  } else if (OB_FAIL(ls_service->get_ls(ls))) {
  } else if (OB_FAIL(ObDDLStorageUtil::ddl_get_tablet(ls, tablet_id, tablet_handle, ObMDSGetTabletMode::READ_ALL_COMMITED))) {
  } else if (OB_UNLIKELY(!tablet_handle.is_valid())) {
    ret = OB_ERR_UNEXPECTED;
    LOG_WARN("invalid tablet handle", K(ret), K(tablet_handle));
  }
  return ret;
}
} //namespace staroge
} //namespace oceanbase
