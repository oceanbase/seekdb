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

#define USING_LOG_PREFIX SQL_DAS
#include "ob_das_tablet_mapper.h"
#include "share/schema/ob_part_mgr_util.h"
#include "sql/engine/ob_exec_context.h"

namespace oceanbase
{
using namespace common;
using namespace share;
using namespace share::schema;
using namespace transaction;
namespace sql
{

OB_SERIALIZE_MEMBER(DASRelatedTabletMap::MapEntry,
                    key_.src_tablet_id_,
                    key_.related_table_id_,
                    val_.tablet_id_,
                    val_.part_id_,
                    val_.first_level_part_id_);

int DASRelatedTabletMap::add_related_tablet_id(ObTabletID src_tablet_id,
                                               ObTableID related_table_id,
                                               ObTabletID related_tablet_id,
                                               ObObjectID related_part_id,
                                               ObObjectID related_first_level_part_id)
{
  int ret = OB_SUCCESS;
  if (nullptr == get_related_tablet_id(src_tablet_id, related_table_id)) {
    MapEntry map_entry;
    map_entry.key_.src_tablet_id_ = src_tablet_id;
    map_entry.key_.related_table_id_ = related_table_id;
    map_entry.val_.tablet_id_ = related_tablet_id;
    map_entry.val_.part_id_ = related_part_id;
    map_entry.val_.first_level_part_id_ = related_first_level_part_id;
    if (OB_FAIL(list_.push_back(map_entry))) {
    } else if (list_.size() > FAST_LOOP_LIST_LEN) {
      //The length of the list is already long enough,
      //and searching through it using iteration will be slow.
      //Therefore, constructing a map can accelerate the search in this situation.
      if (OB_FAIL(insert_related_tablet_map())) {
      }
    }
  }
  return ret;
}

const DASRelatedTabletMap::Value *DASRelatedTabletMap::get_related_tablet_id(ObTabletID src_tablet_id,
                                                                             ObTableID related_table_id)
{
  const Value *val = nullptr;
  if (list_.size() > FAST_LOOP_LIST_LEN) {
    Key tmp_key;
    tmp_key.src_tablet_id_ = src_tablet_id;
    tmp_key.related_table_id_ = related_table_id;
    Value* const *val_ptr = map_.get(&tmp_key);
    val = (val_ptr != nullptr ? *val_ptr : nullptr);
  } else {
    MapEntry *final_entry = nullptr;
    FOREACH_X(node, list_, final_entry == nullptr) {
      MapEntry &entry = *node;
      if (entry.key_.src_tablet_id_ == src_tablet_id &&
          entry.key_.related_table_id_ == related_table_id) {
        final_entry = &entry;
      }
    }
    if (OB_LIKELY(final_entry != nullptr)) {
      val = &final_entry->val_;
    }
  }
  return val;
}

int DASRelatedTabletMap::assign(const RelatedTabletList &list)
{
  int ret = OB_SUCCESS;
  clear();
  FOREACH_X(node, list, OB_SUCC(ret)) {
    const MapEntry &entry = *node;
    if (OB_FAIL(add_related_tablet_id(entry.key_.src_tablet_id_,
                                      entry.key_.related_table_id_,
                                      entry.val_.tablet_id_,
                                      entry.val_.part_id_,
                                      entry.val_.first_level_part_id_))) {
    }
  }
  return ret;
}

int DASRelatedTabletMap::insert_related_tablet_map()
{
  int ret = OB_SUCCESS;
  if (!map_.created()) {
    if (OB_FAIL(map_.create(1000, "DASRelTblKey", "DASRelTblVal"))) {
    }
  }
  if (OB_SUCC(ret)) {
    if (map_.empty()) {
      FOREACH_X(node, list_, OB_SUCC(ret)) {
        MapEntry &entry = *node;
        if (OB_FAIL(map_.set_refactored(&entry.key_, &entry.val_))) {
        }
      }
    } else if (!list_.empty()) {
      MapEntry &final_entry = list_.get_last();
      if (OB_FAIL(map_.set_refactored(&final_entry.key_, &final_entry.val_))) {
      }
    }
  }
  return ret;
}

int ObDASTabletMapper::get_all_virtual_tablet_and_object_id(
    ObIArray<ObTabletID> &tablet_ids,
    ObIArray<ObObjectID> &object_ids)
{
  int ret = OB_SUCCESS;
  if (OB_UNLIKELY(!is_virtual_table(virtual_table_id_))) {
    ret = OB_ERR_UNEXPECTED;
    LOG_WARN("invalid virtual table id", K(ret), K(virtual_table_id_));
  } else if (OB_FAIL(object_ids.push_back(1))) {
  } else if (OB_FAIL(tablet_ids.push_back(ObTabletID(1)))) {
  }
  return ret;
}

int ObDASTabletMapper::get_tablet_and_object_id(
    const ObPartitionLevel part_level,
    const ObPartID part_id,
    const ObNewRange &range,
    ObIArray<ObTabletID> &tablet_ids,
    ObIArray<ObObjectID> &object_ids)
{
  int ret = OB_SUCCESS;
  ObSEArray<ObTabletID, 4> tmp_tablet_ids;
  ObSEArray<ObObjectID, 4> tmp_part_ids;
  if (OB_NOT_NULL(table_schema_)) {
    share::schema::RelatedTableInfo *related_info_ptr = nullptr;
    if (related_info_.related_tids_ != nullptr && !related_info_.related_tids_->empty()) {
      related_info_ptr = &related_info_;
    }
    if (OB_FAIL(ret)) {
    } else if (PARTITION_LEVEL_ZERO == part_level) {
      ObTabletID tablet_id;
      ObObjectID object_id;
      if (OB_FAIL(ObPartitionUtils::get_tablet_and_object_id(
          *table_schema_, tablet_id, object_id, related_info_ptr))) {
      } else if (OB_FAIL(tmp_tablet_ids.push_back(tablet_id))) {
      } else if (OB_FAIL(tmp_part_ids.push_back(object_id))) {
      }
    } else if (PARTITION_LEVEL_ONE == part_level) {
      if (OB_FAIL(ObPartitionUtils::get_tablet_and_part_id(
          *table_schema_, range, tmp_tablet_ids, tmp_part_ids, related_info_ptr))) {
      }
    } else if (PARTITION_LEVEL_TWO == part_level) {
      if (OB_FAIL(ObPartitionUtils::get_tablet_and_subpart_id(
          *table_schema_, part_id, range, tmp_tablet_ids, tmp_part_ids, related_info_ptr))) {
      } else if (OB_FAIL(set_partition_id_map(part_id, tmp_part_ids))) {
      }
    } else {
      ret = OB_INVALID_ARGUMENT;
      LOG_WARN("invalid part level", KR(ret), K(part_level));
    }
    OZ(append_array_no_dup(tablet_ids, tmp_tablet_ids));
    OZ(append_array_no_dup(object_ids, tmp_part_ids));
  } else {
    if (part_level == PARTITION_LEVEL_TWO) {
      ret = OB_NOT_SUPPORTED;
      LOG_WARN("virtual table with subpartition table not supported", KR(ret), K(virtual_table_id_));
      LOG_USER_ERROR(OB_NOT_SUPPORTED, "virtual table with subpartition table");
    } else if (!range.is_whole_range()) {
      ret = OB_NOT_SUPPORTED;
      LOG_WARN("virtual table get tablet_id only with whole range is supported", KR(ret), K(virtual_table_id_));
      LOG_USER_ERROR(OB_NOT_SUPPORTED, "virtual table get tablet_id with precise range info");
    } else if (OB_FAIL(get_all_virtual_tablet_and_object_id(tablet_ids, object_ids))) {
    } else if (OB_FAIL(mock_vtable_related_tablet_id_map(tablet_ids, object_ids))) {
    }
  }
  return ret;
}

int ObDASTabletMapper::get_tablet_and_object_id(const ObPartitionLevel part_level,
                                                const ObPartID part_id,
                                                const ObNewRow &row,
                                                ObTabletID &tablet_id,
                                                ObObjectID &object_id)
{
  int ret = OB_SUCCESS;
  tablet_id = ObTabletID::INVALID_TABLET_ID;
  if (OB_NOT_NULL(table_schema_)) {
    share::schema::RelatedTableInfo *related_info_ptr = nullptr;
    if (related_info_.related_tids_ != nullptr && !related_info_.related_tids_->empty()) {
      related_info_ptr = &related_info_;
    }
    if (OB_FAIL(ret)) {
    } else if (PARTITION_LEVEL_ZERO == part_level) {
      if (OB_FAIL(ObPartitionUtils::get_tablet_and_object_id(
          *table_schema_, tablet_id, object_id, related_info_ptr))) {
      }
    } else if (PARTITION_LEVEL_ONE == part_level) {
      if (OB_FAIL(ObPartitionUtils::get_tablet_and_part_id(
          *table_schema_, row, tablet_id, object_id, related_info_ptr))) {
      }
    } else if (PARTITION_LEVEL_TWO == part_level) {
      if (OB_FAIL(ObPartitionUtils::get_tablet_and_subpart_id(
          *table_schema_, part_id, row, tablet_id, object_id, related_info_ptr))) {
      } else if (OB_FAIL(set_partition_id_map(part_id, object_id))) {
      }
    } else {
      ret = OB_INVALID_ARGUMENT;
      LOG_WARN("invalid part level", KR(ret), K(part_level));
    }
  } else {
    //virtual table, only supported partition by list(svr_ip, svr_port) ...
    ObAddr svr_addr;
    if (part_level == PARTITION_LEVEL_TWO) {
      ret = OB_NOT_SUPPORTED;
      LOG_WARN("virtual table with subpartition table not supported", KR(ret), K(virtual_table_id_));
      LOG_USER_ERROR(OB_NOT_SUPPORTED, "virtual table with subpartition table");
    } else if (row.get_count() != 2) {
      ret = OB_NOT_SUPPORTED;
      LOG_WARN("virtual table, only supported partition by list(svr_ip, svr_port)", KR(ret), K(row));
      LOG_USER_ERROR(OB_NOT_SUPPORTED, "virtual table partition by other than list(svr_ip, svr_port)");
    } else {
      const ObObj &svr_ip = row.get_cell(0);
      const ObObj &port_obj = row.get_cell(1);
      int64_t port_int = port_obj.get_int();
      svr_addr.set_ip_addr(svr_ip.get_string(), port_int);
    }
    if (OB_SUCC(ret) && svr_addr == GCTX.self_addr()) {
      object_id = 1;
      tablet_id = ObTabletID(1);
    }
    if (OB_SUCC(ret) && tablet_id.is_valid()
        && OB_FAIL(mock_vtable_related_tablet_id_map(tablet_id, object_id))) {
      LOG_WARN("fail to mock vtable related tablet id map", KR(ret), K(tablet_id), K(object_id));
    }
  }
  return ret;
}

int ObDASTabletMapper::get_tablet_and_object_id(const share::schema::ObPartitionLevel part_level,
                             const common::ObPartID part_id,
                             const int64_t target_partition_id,
                             common::ObTabletID &tablet_id,
                             common::ObObjectID &object_id)
{
  int ret = OB_SUCCESS;
  tablet_id = ObTabletID::INVALID_TABLET_ID;
  if (OB_NOT_NULL(table_schema_)) {
    share::schema::RelatedTableInfo *related_info_ptr = nullptr;
    if (related_info_.related_tids_ != nullptr && !related_info_.related_tids_->empty()) {
      related_info_ptr = &related_info_;
    }
    if (OB_FAIL(ret)) {
    } else if (PARTITION_LEVEL_ZERO == part_level) {
      if (OB_FAIL(ObPartitionUtils::get_tablet_and_object_id(
          *table_schema_, tablet_id, object_id, related_info_ptr))) {
      } else if (object_id != target_partition_id) {
        ret = OB_ERR_UNEXPECTED;
        LOG_WARN("get unexpected partition id", K(target_partition_id), K(object_id));
      }
    } else if (PARTITION_LEVEL_ONE == part_level) {
      if (OB_FAIL(ObPartitionUtils::get_tablet_and_part_id(
          *table_schema_, target_partition_id, tablet_id, object_id, related_info_ptr))) {
      }
    } else if (PARTITION_LEVEL_TWO == part_level) {
      if (OB_FAIL(ObPartitionUtils::get_tablet_and_subpart_id(
          *table_schema_, part_id, target_partition_id, tablet_id, object_id, related_info_ptr))) {
      } else if (OB_FAIL(set_partition_id_map(part_id, object_id))) {
      }
    } else {
      ret = OB_INVALID_ARGUMENT;
      LOG_WARN("invalid part level", KR(ret), K(part_level));
    }
  } else {
    //virtual table, only supported partition by list(svr_ip, svr_port) ...
    ret = OB_NOT_SUPPORTED;
    LOG_WARN("get partition id by target partition for virtual table not support", KR(ret));
    LOG_USER_ERROR(OB_NOT_SUPPORTED, "get partition id by target partition for virtual table");
  }
  return ret;
}

int ObDASTabletMapper::mock_vtable_related_tablet_id_map(
    const ObIArray<ObTabletID> &tablet_ids,
    const ObIArray<ObObjectID> &part_ids)
{
  int ret = OB_SUCCESS;
  if (!tablet_ids.empty() && related_info_.related_tids_ != nullptr && !related_info_.related_tids_->empty()) {
    for (int64_t i = 0; OB_SUCC(ret) && i < tablet_ids.count(); ++i) {
      const ObTabletID &src_tablet_id = tablet_ids.at(i);
      const ObObjectID &src_part_id = part_ids.at(i);
      if (OB_FAIL(mock_vtable_related_tablet_id_map(src_tablet_id, src_part_id))) {
      }
    }
  }
  return ret;
}

int ObDASTabletMapper::mock_vtable_related_tablet_id_map(
    const ObTabletID &tablet_id,
    const ObObjectID &part_id)
{
  int ret = OB_SUCCESS;
  if (tablet_id.is_valid() && related_info_.related_tids_ != nullptr && !related_info_.related_tids_->empty()) {
    for (int64_t i = 0; OB_SUCC(ret) && i < related_info_.related_tids_->count(); ++i) {
      ObTableID related_table_id = related_info_.related_tids_->at(i);
      ObTabletID related_tablet_id = tablet_id;
      ObObjectID related_object_id = part_id;
      if (OB_FAIL(related_info_.related_map_->add_related_tablet_id(tablet_id,
                                                                    related_table_id,
                                                                    related_tablet_id,
                                                                    related_object_id,
                                                                    OB_INVALID_ID))) {
      } else {
      }
    }
  }
  return ret;
}

int ObDASTabletMapper::get_non_partition_tablet_id(ObIArray<ObTabletID> &tablet_ids,
                                                   ObIArray<ObObjectID> &out_part_ids)
{
  int ret = OB_SUCCESS;
  if (is_non_partition_optimized_) {
    if (OB_FAIL(tablet_ids.push_back(tablet_id_))) {
    } else if (OB_FAIL(out_part_ids.push_back(object_id_))) {
    } else {
      DASRelatedTabletMap *map = static_cast<DASRelatedTabletMap *>(related_info_.related_map_);
      if (OB_NOT_NULL(map) && OB_NOT_NULL(related_list_)
          && OB_FAIL(map->assign(*related_list_))) {
        LOG_WARN("failed to assign related map list", K(ret));
      }
    }
  } else {
    ObNewRange range;
    // here need whole range, for virtual table calc tablet and object id
    range.set_whole_range();
    OZ(get_tablet_and_object_id(PARTITION_LEVEL_ZERO, OB_INVALID_ID,
                                range, tablet_ids, out_part_ids));
  }
  return ret;
}

int ObDASTabletMapper::get_tablet_and_object_id(const ObPartitionLevel part_level,
                                                const ObPartID part_id,
                                                const ObIArray<ObNewRange*> &ranges,
                                                ObIArray<ObTabletID> &tablet_ids,
                                                ObIArray<ObObjectID> &out_part_ids)
{
  int ret = OB_SUCCESS;
  ObSEArray<ObTabletID, 4> tmp_tablet_ids;
  ObSEArray<ObObjectID, 4> tmp_part_ids;
  for (int64_t i = 0; OB_SUCC(ret) && i < ranges.count(); i++) {
    tmp_tablet_ids.reset();
    tmp_part_ids.reset();
    OZ(get_tablet_and_object_id(part_level, part_id, *ranges.at(i), tmp_tablet_ids, tmp_part_ids));
    OZ(append_array_no_dup(tablet_ids, tmp_tablet_ids));
    OZ(append_array_no_dup(out_part_ids, tmp_part_ids));
  }

  return ret;
}

int ObDASTabletMapper::get_tablet_and_object_id(const ObPartitionLevel part_level,
                                                const ObPartID part_id,
                                                const ObObj &value,
                                                ObIArray<ObTabletID> &tablet_ids,
                                                ObIArray<ObObjectID> &out_part_ids)
{
  int ret = OB_SUCCESS;
  uint64_t table_id = NULL == table_schema_ ? virtual_table_id_
                                            : table_schema_->get_table_id();
  ObRowkey rowkey(const_cast<ObObj*>(&value), 1);
  ObNewRange range;
  ObSEArray<ObTabletID, 4> tmp_tablet_ids;
  ObSEArray<ObObjectID, 4> tmp_part_ids;
  if (OB_FAIL(range.build_range(table_id, rowkey))) {
  } else if (OB_FAIL(get_tablet_and_object_id(part_level, part_id, range, tmp_tablet_ids, tmp_part_ids))) {
  } else {
    OZ(append_array_no_dup(tablet_ids, tmp_tablet_ids));
    OZ(append_array_no_dup(out_part_ids, tmp_part_ids));
  }

  return ret;
}

int ObDASTabletMapper::get_all_tablet_and_object_id(const ObPartitionLevel part_level,
                                                    const ObPartID part_id,
                                                    ObIArray<ObTabletID> &tablet_ids,
                                                    ObIArray<ObObjectID> &out_part_ids)
{
  int ret = OB_SUCCESS;
  uint64_t table_id = NULL == table_schema_ ? virtual_table_id_
                                            : table_schema_->get_table_id();
  ObNewRange whole_range;
  whole_range.set_whole_range();
  whole_range.table_id_ = table_id;
  ObSEArray<ObTabletID, 4> tmp_tablet_ids;
  ObSEArray<ObObjectID, 4> tmp_part_ids;
  OZ (get_tablet_and_object_id(part_level, part_id, whole_range, tmp_tablet_ids, tmp_part_ids));
  OZ(append_array_no_dup(tablet_ids, tmp_tablet_ids));
  OZ(append_array_no_dup(out_part_ids, tmp_part_ids));

  return ret;
}

int ObDASTabletMapper::get_all_tablet_and_object_id(ObIArray<ObTabletID> &tablet_ids,
                                                    ObIArray<ObObjectID> &out_part_ids)
{
  int ret = OB_SUCCESS;
  if (OB_NOT_NULL(table_schema_)) {
    if (!table_schema_->is_partitioned_table()) {
      if (OB_FAIL(get_non_partition_tablet_id(tablet_ids, out_part_ids))) {
      }
    } else if (PARTITION_LEVEL_ONE == table_schema_->get_part_level()) {
      if (OB_FAIL(get_all_tablet_and_object_id(PARTITION_LEVEL_ONE, OB_INVALID_ID,
                                             tablet_ids, out_part_ids))) {
      }
    } else {
      ObArray<ObTabletID> tmp_tablet_ids;
      ObArray<ObObjectID> tmp_part_ids;
      if (OB_FAIL(get_all_tablet_and_object_id(PARTITION_LEVEL_ONE, OB_INVALID_ID,
                                               tmp_tablet_ids, tmp_part_ids))) {
      }
      for (int64_t idx = 0; OB_SUCC(ret) && idx < tmp_part_ids.count(); ++idx) {
        ObObjectID part_id = tmp_part_ids.at(idx);
        if (OB_FAIL(get_all_tablet_and_object_id(PARTITION_LEVEL_TWO, part_id,
                                                 tablet_ids, out_part_ids))) {
        }
      }
    }
  }
  return ret;
}

//If the part_id calculated by the partition filter in the where clause is empty,
//we will use the default part id in this query as the final part_id,
//because optimizer needs at least one part_id to generate a plan
int ObDASTabletMapper::get_default_tablet_and_object_id(const ObPartitionLevel part_level,
                                                        const ObIArray<ObObjectID> &part_hint_ids,
                                                        ObTabletID &tablet_id,
                                                        ObObjectID &object_id)
{
  int ret = OB_SUCCESS;
  if (OB_LIKELY(OB_INVALID_ID == virtual_table_id_)) {
    ObCheckPartitionMode check_partition_mode = CHECK_PARTITION_MODE_NORMAL;
    ObPartitionSchemaIter iter(*table_schema_, check_partition_mode);
    ObPartitionSchemaIter::Info info;
    while (OB_SUCC(ret) && !tablet_id.is_valid()) {
      if (OB_FAIL(iter.next_partition_info(info))) {
        if (OB_ITER_END != ret) {
          LOG_WARN("switch the src partition info failed", K(ret));
        }
      } else if (part_hint_ids.empty()) {
        //if partition hint is empty,
        //we use the first partition in table schema as the default partition
        object_id = info.object_id_;
        tablet_id = info.tablet_id_;
      } else if (info.object_id_ == part_hint_ids.at(0)) {
        //if partition hint is specified, we must use the first part id in part_hint_ids_
        //as the default partition,
        //and can't use the first part id in table schema,
        //otherwise, the result of some cases will be incorrect,
        //such as:
        //create table t1(a int primary key, b int) partition by hash(a) partitions 2;
        //select * from t1 partition(p1) where a=0;
        //if where a=0 prune result is partition_id=0 and first_part_id in table schema is 0
        //but query specify that use partition_id=1 to access table, so the result is empty
        //if we use the first part id in table schema as the default partition to access table
        //the result of this query will not be empty
        object_id = info.object_id_;
        tablet_id = info.tablet_id_;
      }
      if (OB_FAIL(ret)) {
      } else if (!tablet_id.is_valid()) {
        // no nothing
      } else if (PARTITION_LEVEL_TWO == part_level &&
                OB_NOT_NULL(info.part_) &&
                OB_FAIL(set_partition_id_map(info.part_->get_part_id(), object_id))) {
        LOG_WARN("failed to set partition id map");
      } else if (related_info_.related_tids_ != nullptr &&
                 !related_info_.related_tids_->empty()) {
        //calculate related partition id and tablet id
        ObSchemaGetterGuard guard;

        if (OB_ISNULL(GCTX.schema_service_)) {
          ret = OB_INVALID_ARGUMENT;
          LOG_ERROR("invalid schema service", KR(ret));
        } else if (OB_FAIL(GCTX.schema_service_->get_runtime_schema_guard(guard))) {
        }
        for (int64_t i = 0; OB_SUCC(ret) && i < related_info_.related_tids_->count(); ++i) {
          ObTableID related_table_id = related_info_.related_tids_->at(i);
          const ObSimpleTableSchemaV2 *table_schema = nullptr;
          ObObjectID related_part_id = OB_INVALID_ID;
          ObObjectID related_first_level_part_id = OB_INVALID_ID;
          ObTabletID related_tablet_id;
          if (OB_FAIL(guard.get_simple_table_schema( related_table_id, table_schema))) {
          } else if (OB_ISNULL(table_schema)) {
            ret = OB_SCHEMA_EAGAIN;
            LOG_WARN("fail to get table schema", KR(ret), K(related_table_id));
          } else if (OB_FAIL(table_schema->get_part_id_and_tablet_id_by_idx(info.part_idx_,
                                                                            info.subpart_idx_,
                                                                            related_part_id,
                                                                            related_first_level_part_id,
                                                                            related_tablet_id))) {
          } else if (OB_FAIL(related_info_.related_map_->add_related_tablet_id(tablet_id,
                                                                               related_table_id,
                                                                               related_tablet_id,
                                                                               related_part_id,
                                                                               related_first_level_part_id))) {
          } else {
          }
        }
      }
    }
    if (OB_ITER_END == ret) {
      ret = OB_SUCCESS;
    }
  } else if (!part_hint_ids.empty()) {
    ret = OB_NOT_SUPPORTED;
    LOG_WARN("specify partition name in virtual table not supported", K(ret));
    LOG_USER_ERROR(OB_NOT_SUPPORTED, "specify partition name in virtual table");
  } else {
    object_id = EMPTY_VIRTUAL_TABLE_TABLET_ID;
    tablet_id = ObTabletID(EMPTY_VIRTUAL_TABLE_TABLET_ID);
    if (related_info_.related_tids_ != nullptr && !related_info_.related_tids_->empty()) {
      for (int64_t i = 0; OB_SUCC(ret) && i < related_info_.related_tids_->count(); ++i) {
        ObTableID related_table_id = related_info_.related_tids_->at(i);
        //all related tables have the same part_id and tablet_id
        if (OB_FAIL(related_info_.related_map_->add_related_tablet_id(tablet_id, related_table_id, tablet_id,
                                                                      object_id, OB_INVALID_ID))) {
        }
      }
    }
  }
  if (OB_SUCC(ret) && !tablet_id.is_valid()) {
    ret = OB_ERR_UNEXPECTED;
    LOG_WARN("invalid first tablet id", K(ret), KPC(table_schema_), K(virtual_table_id_));
  }
  return ret;
}

//get the local index partition id by data table partition id
//or get the local index partition id by other local index partition id
//or get the data table partition id by its local index partition id
int ObDASTabletMapper::get_related_partition_id(const ObTableID &src_table_id,
                                                const ObObjectID &src_part_id,
                                                const ObTableID &dst_table_id,
                                                ObObjectID &dst_object_id)
{
  int ret = OB_SUCCESS;
  if (src_table_id == dst_table_id || OB_INVALID_ID != virtual_table_id_) {
    dst_object_id = src_part_id;
  } else {
    bool is_found = false;
    ObCheckPartitionMode check_partition_mode = CHECK_PARTITION_MODE_NORMAL;
    ObPartitionSchemaIter iter(*table_schema_, check_partition_mode);
    ObPartitionSchemaIter::Info info;
    while (OB_SUCC(ret) && !is_found) {
      if (OB_FAIL(iter.next_partition_info(info))) {
        if (OB_ITER_END != ret) {
          LOG_WARN("switch the src partition info failed", K(ret));
        }
      } else if (info.object_id_ == src_part_id) {
        //find the partition array offset by search partition id
        is_found = true;
      }
    }
    if (OB_ITER_END == ret) {
      ret = OB_SUCCESS;
    }
    if (OB_SUCC(ret) && is_found) {
      ObSchemaGetterGuard guard;
      const ObSimpleTableSchemaV2 *dst_table_schema = nullptr;
      ObObjectID related_part_id = OB_INVALID_ID;
      ObObjectID related_first_level_part_id = OB_INVALID_ID;
      ObTabletID related_tablet_id;
      if (OB_ISNULL(GCTX.schema_service_)) {
        ret = OB_INVALID_ARGUMENT;
        LOG_ERROR("invalid schema service", KR(ret));
      } else if (OB_FAIL(GCTX.schema_service_->get_runtime_schema_guard(guard))) {
      } else if (OB_FAIL(guard.get_simple_table_schema( dst_table_id, dst_table_schema))) {
      } else if (OB_ISNULL(dst_table_schema)) {
        ret = OB_SCHEMA_EAGAIN;
        LOG_WARN("fail to get table schema", KR(ret), K(dst_table_id));
      } else if (OB_FAIL(dst_table_schema->get_part_id_and_tablet_id_by_idx(info.part_idx_,
                                                                            info.subpart_idx_,
                                                                            related_part_id,
                                                                            related_first_level_part_id,
                                                                            related_tablet_id))) {
      } else {
        dst_object_id = related_part_id;
      }
    }
  }
  return ret;
}

int ObDASTabletMapper::set_partition_id_map(ObObjectID first_level_part_id,
                                            ObIArray<ObObjectID> &partition_ids)
{
  int ret = OB_SUCCESS;
  if (OB_NOT_NULL(partition_id_map_)) {
    for (int64_t i = 0; OB_SUCC(ret) && i < partition_ids.count(); ++i) {
      if (OB_FAIL(partition_id_map_->set_refactored(partition_ids.at(i), first_level_part_id))) {
        if (OB_LIKELY(OB_HASH_EXIST == ret)) {
          ret = OB_SUCCESS;
        } else {
          LOG_WARN("failed to set partition map", K(first_level_part_id), K(partition_ids.at(i)));
        }
      }
    }
  }
  return ret;
}

int ObDASTabletMapper::set_partition_id_map(ObObjectID first_level_part_id,
                                            ObObjectID partition_id)
{
  int ret = OB_SUCCESS;
  if (OB_NOT_NULL(partition_id_map_)) {
    if (OB_FAIL(partition_id_map_->set_refactored(partition_id, first_level_part_id))) {
      if (OB_LIKELY(OB_HASH_EXIST == ret)) {
        ret = OB_SUCCESS;
      } else {
        LOG_WARN("failed to set partition map", K(first_level_part_id), K(partition_id));
      }
    }
  }
  return ret;
}

int ObDASTabletMapper::get_partition_id_map(ObObjectID partition_id,
                                            ObObjectID &first_level_part_id)
{
  int ret = OB_SUCCESS;
  if (OB_NOT_NULL(partition_id_map_)) {
    if (OB_FAIL(partition_id_map_->get_refactored(partition_id, first_level_part_id))) {
      if (OB_LIKELY(OB_HASH_NOT_EXIST == ret)) {
        // do nothing
      } else {
        LOG_WARN("failed to set partition map", K(partition_id), K(first_level_part_id));
      }
    }
  }
  return ret;
}


/* only for list part */
int ObDASTabletMapper::get_tablet_and_object_id(
    const ObPartitionLevel part_level,
    const ObPartID part_id,
    ObExecContext &exec_ctx,
    const ParamStore &params,
    const ObDataTypeCastParams &dtc_params,
    const common::ObIArray<ValueItemExpr*> &vies,
    ObIArray<ObTabletID> &tablet_ids,
    ObIArray<ObObjectID> &object_ids)
{
  int ret = OB_SUCCESS;
  ObSEArray<ObTabletID, 4> tmp_tablet_ids;
  ObSEArray<ObObjectID, 4> tmp_part_ids;
  if (OB_NOT_NULL(table_schema_)) {
    share::schema::RelatedTableInfo *related_info_ptr = nullptr;
    if (related_info_.related_tids_ != nullptr && !related_info_.related_tids_->empty()) {
      related_info_ptr = &related_info_;
    }
    if (OB_FAIL(ret)) {
    } else if (PARTITION_LEVEL_ZERO == part_level) {
      ObTabletID tablet_id;
      ObObjectID object_id;
      if (OB_FAIL(ObPartitionUtils::get_tablet_and_object_id(
          *table_schema_, tablet_id, object_id, related_info_ptr))) {
      } else if (OB_FAIL(tmp_tablet_ids.push_back(tablet_id))) {
      } else if (OB_FAIL(tmp_part_ids.push_back(object_id))) {
      }
    } else if (PARTITION_LEVEL_ONE == part_level) {
      if (OB_FAIL(get_tablet_and_part_id_for_list_part(
          *table_schema_, exec_ctx, params, dtc_params, vies, tmp_tablet_ids, tmp_part_ids, related_info_ptr))) {
      }
    } else if (PARTITION_LEVEL_TWO == part_level) {
      if (OB_FAIL(get_tablet_and_subpart_id_for_list_part(
          *table_schema_, part_id, exec_ctx, params, dtc_params, vies, tmp_tablet_ids, tmp_part_ids, related_info_ptr))) {
      } else if (OB_FAIL(set_partition_id_map(part_id, tmp_part_ids))) {
      }
    } else {
      ret = OB_INVALID_ARGUMENT;
      LOG_WARN("invalid part level", KR(ret), K(part_level));
    }
    OZ(append_array_no_dup(tablet_ids, tmp_tablet_ids));
    OZ(append_array_no_dup(object_ids, tmp_part_ids));
  } else {
    if (part_level == PARTITION_LEVEL_TWO) {
      ret = OB_NOT_SUPPORTED;
      LOG_WARN("virtual table with subpartition table not supported", KR(ret), K(virtual_table_id_));
      LOG_USER_ERROR(OB_NOT_SUPPORTED, "virtual table with subpartition table");
    } else if (OB_FAIL(get_all_virtual_tablet_and_object_id(tablet_ids, object_ids))) {
    } else if (OB_FAIL(mock_vtable_related_tablet_id_map(tablet_ids, object_ids))) {
    }
  }
  return ret;
}

int ObDASTabletMapper::get_tablet_and_part_id_for_list_part(const share::schema::ObTableSchema &table_schema,
                                                            ObExecContext &exec_ctx,
                                                            const ParamStore &params,
                                                            const ObDataTypeCastParams &dtc_params,
                                                            const common::ObIArray<ValueItemExpr*> &vies,
                                                            common::ObIArray<common::ObTabletID> &tablet_ids,
                                                            common::ObIArray<common::ObObjectID> &part_ids,
                                                            RelatedTableInfo *related_table /*= NULL*/)
{
  int ret = OB_SUCCESS;
  ObSEArray<PartitionIndex, 4> partition_indexes;
  ObPartitionLevel part_level = table_schema.get_part_level();
  const uint64_t table_id = table_schema.get_table_id();
  if (OB_FAIL(ObPartitionUtils::check_param_valid(table_schema, related_table))) {
  } else if (PARTITION_LEVEL_ONE != part_level && PARTITION_LEVEL_TWO != part_level) {
    ret = OB_NOT_SUPPORTED;
    LOG_WARN("not supported part level", K(table_id), K(part_level));
  } else if (!table_schema.is_list_part()) {
    ret = OB_NOT_SUPPORTED;
    LOG_WARN("not suppored part option", K(table_id), "part_option", table_schema.get_part_option());
  } else {
    ObPartition * const* part_array = table_schema.get_part_array();
    const int64_t part_num = table_schema.get_partition_num();
    if (OB_ISNULL(part_array) || OB_UNLIKELY(part_num <= 0)) {
      ret = OB_ERR_UNEXPECTED;
      LOG_WARN("get unexpected part array", KP(part_array), K(part_num));
    } else {
      for (int64_t i = 0; OB_SUCC(ret) && i < part_num; i++) {
        const ObIArray<common::ObNewRow> &list_row_values = part_array[i]->get_list_row_values();
        bool is_match = false;
        // partition with default value always match
        if (list_row_values.count() == 1 &&
            list_row_values.at(0).get_count() >= 1 &&
            list_row_values.at(0).get_cell(0).is_max_value()) {
          is_match = true;
        }
        for (int64_t j = 0; OB_SUCC(ret) && !is_match && j < list_row_values.count(); j++) {
          const ObNewRow &list_row = list_row_values.at(j);
          bool all_match = true;
          for (int64_t k = 0; OB_SUCC(ret) && all_match && k < vies.count(); ++k) {
            ObObj res;
            if (OB_ISNULL(vies.at(k))) {
              ret = OB_ERR_UNEXPECTED;
              LOG_WARN("get null vie");
            } else {
              ObCastCtx cast_ctx(&exec_ctx.get_allocator(), &dtc_params, CM_NONE, vies.at(k)->dst_cs_type_);
              if (OB_FAIL(ObTableLocation::se_calc_value_item(cast_ctx, exec_ctx, params,
                                                              *vies.at(k), list_row, res))) {
              } else if (res.get_int() == 0) {
                all_match = false;
              }
            }
          }
          if (OB_SUCC(ret) && all_match) {
            is_match = true;
          }
        } // end for
        if (OB_SUCC(ret) && is_match) {
          if (OB_FAIL(partition_indexes.push_back(PartitionIndex(i, OB_INVALID_INDEX)))) {
          }
        }
      } // end dor

      if (OB_SUCC(ret)) {
        if (OB_UNLIKELY(partition_indexes.empty())) {
          // return invalid part_id/tablet_id if partition not found.
        }
      }
    }

    const bool fill_tablet_id = (PARTITION_LEVEL_ONE == part_level);
    if (FAILEDx(ObPartitionUtils::fill_tablet_and_object_ids(fill_tablet_id,
                                                             OB_INVALID_INDEX /*part_idx*/,
                                                             partition_indexes,
                                                             table_schema,
                                                             related_table,
                                                             tablet_ids,
                                                             part_ids))) {
      LOG_WARN("fail to fill tablet and part_ids", K(fill_tablet_id), K(table_id), K(partition_indexes));
    }
  }
  return ret;
}

int ObDASTabletMapper::get_tablet_and_subpart_id_for_list_part(const ObTableSchema &table_schema,
                                                               const ObPartID &part_id,
                                                               ObExecContext &exec_ctx,
                                                               const ParamStore &params,
                                                               const ObDataTypeCastParams &dtc_params,
                                                               const ObIArray<ValueItemExpr*> &vies,
                                                               ObIArray<ObTabletID> &tablet_ids,
                                                               ObIArray<ObObjectID> &subpart_ids,
                                                               RelatedTableInfo *related_table /*= NULL*/)
{
  int ret = OB_SUCCESS;
  ObSEArray<PartitionIndex, 4> partition_indexes;
  ObPartitionLevel part_level = table_schema.get_part_level();
  const uint64_t table_id = table_schema.get_table_id();
  const ObPartition *partition = NULL;
  int64_t part_idx = OB_INVALID_ID;
  if (OB_FAIL(ObPartitionUtils::check_param_valid(table_schema, related_table))) {
  } else if (PARTITION_LEVEL_TWO != part_level) {
    ret = OB_NOT_SUPPORTED;
    LOG_WARN("not supported part level", K(part_level));
  } else if (!table_schema.is_list_subpart()) {
    ret = OB_NOT_SUPPORTED;
    LOG_WARN("not supported subpart option", K(table_id), "subpart_option", table_schema.get_sub_part_option());
  } else if (OB_FAIL(table_schema.get_partition_index_by_id(part_id,
                                                            CHECK_PARTITION_MODE_NORMAL,
                                                            part_idx))) {
  } else if (OB_FAIL(table_schema.get_partition_by_partition_index(part_idx,
                                                                   CHECK_PARTITION_MODE_NORMAL,
                                                                   partition))) {
  } else if (OB_ISNULL(partition)) {
    ret = OB_ERR_UNEXPECTED;
    LOG_WARN("partition not exist", K(part_id), K(part_idx));
  } else {
    ObSubPartition * const* subpart_array = partition->get_subpart_array();
    int64_t subpart_num = partition->get_subpartition_num();
    if (OB_ISNULL(subpart_array) || OB_UNLIKELY(subpart_num <= 0)) {
      ret = OB_ERR_UNEXPECTED;
      LOG_WARN("get unexpected subpartition array", KP(subpart_array), K(subpart_num));
    } else {
      for (int64_t i = 0; OB_SUCC(ret) && i < subpart_num; i++) {
        const ObIArray<common::ObNewRow> &list_row_values = subpart_array[i]->get_list_row_values();
        bool is_match = false;
        // partition with default value always match
        if (list_row_values.count() == 1
            && list_row_values.at(0).get_count() >= 1
            && list_row_values.at(0).get_cell(0).is_max_value()) {
          is_match = true;
        }
        for (int64_t j = 0; OB_SUCC(ret) && !is_match && j < list_row_values.count(); j++) {
          const ObNewRow &list_row = list_row_values.at(j);
          bool all_match = true;
          for (int64_t k = 0; OB_SUCC(ret) && all_match && k < vies.count(); ++k) {
            ObObj res;
            if (OB_ISNULL(vies.at(k))) {
              ret = OB_ERR_UNEXPECTED;
              LOG_WARN("get null vie");
            } else {
              ObCastCtx cast_ctx(&exec_ctx.get_allocator(), &dtc_params, CM_NONE, vies.at(k)->dst_cs_type_);
              if (OB_FAIL(ObTableLocation::se_calc_value_item(cast_ctx, exec_ctx, params,
                                                              *vies.at(k), list_row, res))) {
              } else if (res.get_int() == 0) {
                all_match = false;
              }
            }
          }
          if (OB_SUCC(ret) && all_match) {
            is_match = true;
          }
        } // end for
        if (OB_SUCC(ret) && is_match) {
          const ObSubPartition *subpartition = NULL;
          if (OB_ISNULL(subpartition = subpart_array[i])) {
            ret = OB_ERR_UNEXPECTED;
            LOG_WARN("subpartition is null", K(i));
          } else if (OB_UNLIKELY(static_cast<ObPartID>(subpartition->get_part_id()) != part_id)) {
            ret = OB_ERR_UNEXPECTED;
            LOG_WARN("part_id not match", KPC(subpartition), K(part_id));
          } else if (OB_UNLIKELY(!subpartition->get_tablet_id().is_valid())) {
            ret = OB_ERR_UNEXPECTED;
            LOG_WARN("invalid tablet_id", KPC(subpartition), K(i));
          } else if (OB_FAIL(partition_indexes.push_back(PartitionIndex(OB_INVALID_INDEX, i)))) {
          }
        }
      } // end dor

      if (OB_SUCC(ret)) {
        if (OB_UNLIKELY(partition_indexes.empty())) {
          // return invalid part_id/tablet_id if partition not found.
        }
      }
    }
    const bool fill_tablet_id = true;
    if (FAILEDx(ObPartitionUtils::fill_tablet_and_object_ids(fill_tablet_id,
                                                             part_idx,
                                                             partition_indexes,
                                                             table_schema,
                                                             related_table,
                                                             tablet_ids,
                                                             subpart_ids))) {
      LOG_WARN("fail to fill tablet and subpart_ids", K(fill_tablet_id), K(table_id), K(partition_indexes));
    }
  }
  return ret;
}

}  // namespace sql
}  // namespace oceanbase
