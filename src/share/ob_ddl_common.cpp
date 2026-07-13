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

#define USING_LOG_PREFIX SHARE

#include "lib/compress/ob_compress_util.h"
#include "ob_ddl_common.h"
#include "common/datum/ob_datum.h"  // ObDatum complete type(previously hidden behind the block_sstable_struct include chain)
#include "share/ob_rpc_struct.h"
#include "share/system_variable/ob_system_variable_alias.h"
#include "lib/worker.h"
#include "share/ob_ddl_checksum.h"
#include "share/ob_ddl_sim_point.h"
#include "common/object/ob_object.h"
#include "share/compaction/ob_shared_storage_compaction_util.h"
#ifdef OB_BUILD_SHARED_STORAGE
#include "close_modules/shared_storage/meta_store/ob_shared_storage_obj_meta.h"
#endif
#include "share/tablet/ob_tablet_table_operator.h"
#include "share/storage/ob_tablet_replica_checksum_table_storage.h"

using namespace oceanbase::share;
using namespace oceanbase::common;
using namespace oceanbase::share::schema;
using namespace oceanbase::obcall;
using namespace oceanbase::sql;
using namespace oceanbase::storage;
using namespace oceanbase::blocksstable;

const char *oceanbase::share::get_ddl_type(ObDDLType ddl_type)
{
  const char *ret_name = "UNKNOWN_DDL_TYPE";
  switch (ddl_type) {
    case ObDDLType::DDL_INVALID:
      ret_name = "DDL_INVALID";
      break;
    case ObDDLType::DDL_CHECK_CONSTRAINT:
      ret_name = "DDL_CHECK_CONSTRAINT";
      break;
    case ObDDLType::DDL_FOREIGN_KEY_CONSTRAINT:
      ret_name = "DDL_FOREIGN_KEY_CONSTRAINT";
      break;
    case ObDDLType::DDL_ADD_NOT_NULL_COLUMN:
      ret_name = "DDL_ADD_NOT_NULL_COLUMN";
      break;
    case ObDDLType::DDL_MODIFY_AUTO_INCREMENT:
      ret_name = "DDL_MODIFY_AUTO_INCREMENT";
      break;
    case ObDDLType::DDL_CREATE_INDEX:
      ret_name = "DDL_CREATE_INDEX";
      break;
    case ObDDLType::DDL_DROP_INDEX:
      ret_name = "DDL_DROP_INDEX";
      break;
    case ObDDLType::DDL_CREATE_FTS_INDEX:
      ret_name = "DDL_CREATE_FTS_INDEX";
      break;
    case ObDDLType::DDL_CREATE_PARTITIONED_LOCAL_INDEX:
      ret_name = "DDL_CREATE_PARTITIONED_LOCAL_INDEX";
      break;
    case ObDDLType::DDL_DROP_LOB:
      ret_name = "DDL_DROP_LOB";
      break;
    case ObDDLType::DDL_DROP_FTS_INDEX:
      ret_name = "DDL_DROP_FTS_INDEX";
      break;
    case ObDDLType::DDL_DROP_MULVALUE_INDEX:
      ret_name = "DDL_DROP_MULVALUE_INDEX";
      break;
    case ObDDLType::DDL_DROP_VEC_INDEX:
      ret_name = "DDL_DROP_VEC_INDEX";
      break;
    case ObDDLType::DDL_CREATE_VEC_INDEX:
      ret_name = "DDL_CREATE_VEC_INDEX";
      break;
    case ObDDLType::DDL_CREATE_MULTIVALUE_INDEX:
      ret_name = "DDL_CREATE_MULTIVALUE_INDEX";
      break;
    case ObDDLType::DDL_REBUILD_INDEX:
      ret_name = "DDL_REBUILD_INDEX";
      break;
    case ObDDLType::DDL_DROP_SCHEMA_AVOID_CONCURRENT_TRANS:
      ret_name = "DDL_DROP_SCHEMA_AVOID_CONCURRENT_TRANS";
      break;
    case ObDDLType::DDL_DROP_DATABASE:
      ret_name = "DDL_DROP_DATABASE";
      break;
    case ObDDLType::DDL_DROP_TABLE:
      ret_name = "DDL_DROP_TABLE";
    case ObDDLType::DDL_TRUNCATE_TABLE:
      ret_name = "DDL_TRUNCATE_TABLE";
      break;
    case ObDDLType::DDL_DROP_PARTITION:
      ret_name = "DDL_DROP_PARTITION";
      break;
    case ObDDLType::DDL_DROP_SUB_PARTITION:
      ret_name = "DDL_DROP_SUB_PARTITION";
      break;
    case ObDDLType::DDL_TRUNCATE_PARTITION:
      ret_name = "DDL_TRUNCATE_PARTITION";
      break;
    case ObDDLType::DDL_TRUNCATE_SUB_PARTITION:
      ret_name = "DDL_TRUNCATE_SUB_PARTITION";
      break;
    case ObDDLType::DDL_RENAME_PARTITION:
      ret_name = "DDL_RENAME_PARTITION";
      break;
    case ObDDLType::DDL_RENAME_SUB_PARTITION:
      ret_name = "DDL_RENAME_SUB_PARTITION";
      break;
    case ObDDLType::DDL_DOUBLE_TABLE_OFFLINE:
      ret_name = "DDL_DOUBLE_TABLE_OFFLINE";
      break;
    case ObDDLType::DDL_MODIFY_COLUMN:
      ret_name = "DDL_MODIFY_COLUMN";
      break;
    case ObDDLType::DDL_ADD_PRIMARY_KEY:
      ret_name = "DDL_ADD_PRIMARY_KEY";
      break;
    case ObDDLType::DDL_DROP_PRIMARY_KEY:
      ret_name = "DDL_DROP_PRIMARY_KEY";
      break;
    case ObDDLType::DDL_ALTER_PRIMARY_KEY:
      ret_name = "DDL_ALTER_PRIMARY_KEY";
      break;
    case ObDDLType::DDL_ALTER_PARTITION_BY:
      ret_name = "DDL_ALTER_PARTITION_BY";
      break;
    case ObDDLType::DDL_DROP_COLUMN:
      ret_name = "DDL_DROP_COLUMN";
      break;
    case ObDDLType::DDL_CONVERT_TO_CHARACTER:
      ret_name = "DDL_CONVERT_TO_CHARACTER";
      break;
    case ObDDLType::DDL_ADD_COLUMN_OFFLINE:
      ret_name = "DDL_ADD_COLUMN_OFFLINE";
      break;
    case ObDDLType::DDL_COLUMN_REDEFINITION:
      ret_name = "DDL_COLUMN_REDEFINITION";
      break;
    case ObDDLType::DDL_TABLE_REDEFINITION:
      ret_name = "DDL_TABLE_REDEFINITION";
      break;
    case ObDDLType::DDL_DIRECT_LOAD:
      ret_name = "DDL_DIRECT_LOAD";
      break;
    case ObDDLType::DDL_DIRECT_LOAD_INSERT:
      ret_name = "DDL_DIRECT_LOAD_INSERT";
      break;
    case ObDDLType::DDL_MODIFY_AUTO_INCREMENT_WITH_REDEFINITION:
      ret_name = "DDL_MODIFY_AUTO_INCREMENT_WITH_REDEFINITION";
      break;
    case ObDDLType::DDL_NORMAL_TYPE:
      ret_name = "DDL_NORMAL_TYPE";
      break;
    case ObDDLType::DDL_ADD_COLUMN_ONLINE:
      ret_name = "DDL_ADD_COLUMN_ONLINE";
      break;
    case ObDDLType::DDL_CHANGE_COLUMN_NAME:
      ret_name = "DDL_CHANGE_COLUMN_NAME";
      break;
    case ObDDLType::DDL_DROP_COLUMN_INSTANT:
      ret_name = "DDL_DROP_COLUMN_INSTANT";
      break;
    case ObDDLType::DDL_ADD_COLUMN_INSTANT:
      ret_name = "DDL_ADD_COLUMN_INSTANT";
      break;
    case ObDDLType::DDL_COMPOUND_INSTANT:
      ret_name = "DDL_COMPOUND_INSTANT";
      break;
    case ObDDLType::DDL_FORK_TABLE:
      ret_name = "DDL_FORK_TABLE";
      break;
    default:
      break;
  }
  return ret_name;
}

int ObColumnNameMap::init(const ObTableSchema &orig_table_schema,
                          const ObTableSchema &new_table_schema,
                          const AlterTableSchema &alter_table_schema)
{
  int ret = OB_SUCCESS;
  if (OB_FAIL(col_name_map_.create(32, "ColNameMap"))) {
    LOG_WARN("failed to create column name map", K(ret));
  } else {
    for (ObTableSchema::const_column_iterator it = orig_table_schema.column_begin();
        OB_SUCC(ret) && it != orig_table_schema.column_end(); it++) {
      ObColumnSchemaV2 *column = *it;
      if (OB_ISNULL(column)) {
        ret = OB_ERR_UNEXPECTED;
        LOG_WARN("invalid column", K(ret));
      } else if (column->is_unused()) {
        // unused column, extra column compared to the hidden table.
      } else if (OB_FAIL(set(column->get_column_name_str(), column->get_column_name_str()))) {
        LOG_WARN("failed to set colum name map", K(ret));
      }
    }
    for (ObTableSchema::const_column_iterator it = alter_table_schema.column_begin();
        OB_SUCC(ret) && it < alter_table_schema.column_end(); it++) {
      const AlterColumnSchema *alter_column_schema = nullptr;
      if (OB_ISNULL(alter_column_schema = static_cast<AlterColumnSchema *>(*it))) {
        ret = OB_ERR_UNEXPECTED;
        LOG_WARN("*it_begin is NULL", K(ret));
      } else {
        const ObString &orig_column_name = alter_column_schema->get_origin_column_name();
        const ObString &new_column_name = alter_column_schema->get_column_name_str();
        const ObColumnNameHashWrapper orig_column_key(orig_column_name);
        const ObSchemaOperationType op_type = alter_column_schema->alter_type_;
        switch (op_type) {
        case OB_DDL_DROP_COLUMN: {
          // can only drop original table columns
          if (OB_FAIL(col_name_map_.erase_refactored(orig_column_key))) {
            LOG_WARN("failed to erase from col name map", K(ret));
          }
          break;
        }
        case OB_DDL_ADD_COLUMN: {
          break;
        }
        case OB_DDL_CHANGE_COLUMN:
        case OB_DDL_MODIFY_COLUMN: {
          const ObColumnSchemaV2 *orig_column = nullptr;
          if (OB_ISNULL(orig_column = orig_table_schema.get_column_schema(orig_column_name))) {
            ret = OB_ERR_UNEXPECTED;
            LOG_WARN("column not in orig table", K(ret));
          } else if (orig_column->get_column_name_str() != new_column_name) {
            const ObString &column_name = orig_column->get_column_name_str();
            if (OB_FAIL(col_name_map_.erase_refactored(ObColumnNameHashWrapper(column_name)))) {
              LOG_WARN("failed to erase col name map", K(ret));
            } else if (OB_FAIL(set(column_name, new_column_name))) {
              LOG_WARN("failed to set col name map", K(ret));
            }
          }
          break;
        }
        default: {
          LOG_DEBUG("ignore unexpected operator", K(ret), KPC(alter_column_schema));
          break;
        }
        }
      }
    }
  }
  return ret;
}

int ObColumnNameMap::assign(const ObColumnNameMap &other)
{
  int ret = OB_SUCCESS;
  if (!other.col_name_map_.created()) {
    ret = OB_NOT_INIT;
    LOG_WARN("assign from uninitialized name map", K(ret));
  } else if (!col_name_map_.created()) {
    if (OB_FAIL(col_name_map_.create(32, "ColNameMap"))) {
      LOG_WARN("failed to create col name map", K(ret));
    }
  } else if (OB_FAIL(col_name_map_.reuse())) {
    LOG_WARN("failed to clear map", K(ret));
  }
  if (OB_SUCC(ret)) {
    allocator_.reuse();
    for (common::hash::ObHashMap<ObColumnNameHashWrapper, ObString>::const_iterator it = other.col_name_map_.begin();
        OB_SUCC(ret) && it != other.col_name_map_.end(); it++) {
      if (OB_FAIL(set(it->first.column_name_, it->second))) {
        LOG_WARN("failed to copy col name map entry", K(ret));
      }
    }
  }
  return ret;
}

int ObColumnNameMap::set(const ObString &orig_column_name, const ObString &new_column_name)
{
  int ret = OB_SUCCESS;
  ObString orig_name;
  ObString new_name;
  if (OB_FAIL(deep_copy_ob_string(allocator_, orig_column_name, orig_name))) {
    LOG_WARN("failed to copy string", K(ret));
  } else if (OB_FAIL(deep_copy_ob_string(allocator_, new_column_name, new_name))) {
    LOG_WARN("failed to copy string", K(ret));
  } else if (OB_FAIL(col_name_map_.set_refactored(ObColumnNameHashWrapper(orig_name), new_name))) {
    LOG_WARN("failed to set col name map", K(ret));
  }
  return ret;
}

int ObColumnNameMap::get(const ObString &orig_column_name, ObString &new_column_name) const
{
  int ret = OB_SUCCESS;
  ret = col_name_map_.get_refactored(ObColumnNameHashWrapper(orig_column_name), new_column_name);
  if (OB_HASH_NOT_EXIST == ret) {
    ret = OB_ENTRY_NOT_EXIST;
  }
  return ret;
}

int ObColumnNameMap::get_orig_column_name(const ObString &new_column_name, ObString &orig_column_name) const
{
  int ret = OB_SUCCESS;
  if (OB_UNLIKELY(!col_name_map_.created())) {
    ret = OB_NOT_INIT;
    LOG_WARN("invalid column name map", K(ret));
  } else {
    const ObColumnNameHashWrapper new_column_key = ObColumnNameHashWrapper(new_column_name);
    bool found = false;
    for (common::hash::ObHashMap<ObColumnNameHashWrapper, ObString>::const_iterator it = col_name_map_.begin();
        OB_SUCC(ret) && !found && it != col_name_map_.end(); it++) {
      if (ObColumnNameHashWrapper(it->second) == new_column_key) {
        orig_column_name = it->first.column_name_;
        found = true;
      }
    }
    if (OB_SUCC(ret) && !found) {
      ret = OB_ENTRY_NOT_EXIST;
    }
  }
  return ret;
}

int ObColumnNameMap::get_changed_names(ObIArray<std::pair<ObString, ObString>> &changed_names) const
{
  int ret = OB_SUCCESS;
  if (OB_UNLIKELY(!col_name_map_.created())) {
    ret = OB_NOT_INIT;
    LOG_WARN("invalid column name map", K(ret));
  }
  for (common::hash::ObHashMap<ObColumnNameHashWrapper, ObString>::const_iterator it = col_name_map_.begin();
      OB_SUCC(ret) && it != col_name_map_.end(); it++) {
    if (it->first.column_name_ != it->second) {
      if (OB_FAIL(changed_names.push_back(std::make_pair(it->first.column_name_, it->second)))) {
        LOG_WARN("failed to push back changed name", K(ret));
      }
    }
  }
  return ret;
}


/******************           ObDDLUtil         *************/
// moved definition to the upper-layer owner cpp(transitional state)


int ObDDLUtil::get_tablets(
    const int64_t table_id,
    common::ObIArray<common::ObTabletID> &tablet_ids)
{
  int ret = OB_SUCCESS;
  share::schema::ObSchemaGetterGuard schema_guard;
  const share::schema::ObTableSchema *table_schema = nullptr;
  if (OB_INVALID_ID == table_id) {
    ret = OB_INVALID_ARGUMENT;
    LOG_WARN("invalid arguments", K(ret), K(table_id));
  } else if (OB_FAIL(share::schema::ObMultiVersionSchemaService::get_instance().get_tenant_schema_guard(
      schema_guard))) {
    LOG_WARN("get tenant schema guard failed", K(ret));
  } else if (OB_FAIL(schema_guard.get_table_schema( table_id, table_schema))) {
    LOG_WARN("get table schema failed", K(ret), K(table_id));
  } else if (OB_ISNULL(table_schema)) {
    ret = OB_TABLE_NOT_EXIST;
    LOG_WARN("get table schema failed", K(ret), K(table_id));
  } else if (OB_FAIL(table_schema->get_tablet_ids(tablet_ids))) {
    LOG_WARN("get tablets failed", K(ret), KPC(table_schema));
  }
  return ret;
}

int ObDDLUtil::get_tablet_count(const int64_t table_id,
                              int64_t &tablet_count)
{
  int ret = OB_SUCCESS;
  tablet_count = 0;
  share::schema::ObSchemaGetterGuard schema_guard;
  const share::schema::ObTableSchema *table_schema = nullptr;
  if (OB_INVALID_ID == table_id) {
    ret = OB_INVALID_ARGUMENT;
    LOG_WARN("invalid arguments", K(ret), K(table_id));
  } else if (OB_FAIL(share::schema::ObMultiVersionSchemaService::get_instance().get_tenant_schema_guard(
      schema_guard))) {
    LOG_WARN("get tenant schema guard failed", K(ret));
  } else if (OB_FAIL(schema_guard.get_table_schema( table_id, table_schema))) {
    LOG_WARN("get table schema failed", K(ret), K(table_id));
  } else if (OB_ISNULL(table_schema)) {
    ret = OB_TABLE_NOT_EXIST;
    LOG_WARN("get table schema failed", K(ret), K(table_id));
  } else {
    tablet_count = table_schema->get_all_part_num();
  }
  return ret;
}

int ObDDLUtil::get_all_indexes_tablets_count(
    ObSchemaGetterGuard &schema_guard,
    const uint64_t data_table_id,
    int64_t &all_tablet_count)
{
  int ret = OB_SUCCESS;
  all_tablet_count = 0;
  const ObTableSchema *data_table_schema = nullptr;
  if (OB_UNLIKELY(OB_INVALID_ID == data_table_id)) {
    ret = OB_INVALID_ARGUMENT;
    LOG_WARN("invalid arg", K(ret), K(data_table_id));
  } else if (OB_FAIL(schema_guard.get_table_schema( data_table_id, data_table_schema))) {
    LOG_WARN("get table schema failed", K(ret), K(data_table_id));
  } else if (OB_ISNULL(data_table_schema)) {
    ret = OB_TABLE_NOT_EXIST;
    LOG_WARN("table not exist", K(ret), K(data_table_id));
  } else {
    const common::ObIArray<ObAuxTableMetaInfo> &index_infos = data_table_schema->get_simple_index_infos();
    for (int64_t i = 0; OB_SUCC(ret) && i < index_infos.count(); i++) {
      const uint64_t index_tid = index_infos.at(i).table_id_;
      const ObTableSchema *index_schema = nullptr;
      if (OB_FAIL(schema_guard.get_table_schema( index_tid, index_schema))) {
        LOG_WARN("get table schema failed", K(ret), K(index_tid));
      } else if (OB_ISNULL(index_schema)) {
        ret = OB_TABLE_NOT_EXIST;
        LOG_WARN("get table schema failed", K(ret), K(index_tid));
      } else {
        all_tablet_count += index_schema->get_all_part_num();
      }
    }
  }
  return ret;
}

int ObDDLUtil::refresh_alter_table_arg(const int64_t orig_table_id,
    const uint64_t foreign_key_id,
    obcall::ObAlterTableArg &alter_table_arg)
{
  int ret = OB_SUCCESS;
  share::schema::ObSchemaGetterGuard schema_guard;
  const share::schema::ObTableSchema *table_schema = nullptr;
  const ObDatabaseSchema *db_schema = nullptr;
  if (OB_INVALID_ID == orig_table_id) {
    ret = OB_INVALID_ARGUMENT;
    LOG_WARN("invalid arguments", K(ret), K(orig_table_id));
  } else if (OB_FAIL(share::schema::ObMultiVersionSchemaService::get_instance().get_tenant_schema_guard(
      schema_guard))) {
    LOG_WARN("get tenant schema guard failed", K(ret));
  } else if (OB_FAIL(schema_guard.get_table_schema( orig_table_id, table_schema))) {
    LOG_WARN("get table schema failed", K(ret), K(orig_table_id));
  } else if (OB_ISNULL(table_schema)) {
    ret = OB_TABLE_NOT_EXIST;
    LOG_WARN("table dropped", K(ret), K(orig_table_id));
  } else if (OB_FAIL(alter_table_arg.alter_table_schema_.set_origin_table_name(table_schema->get_table_name_str()))) {
    LOG_WARN("failed to set orig table name", K(ret));
  } else if (OB_FAIL(schema_guard.get_database_schema( table_schema->get_database_id(), db_schema))) {
    LOG_WARN("fail to get database schema", K(ret));
  } else if (OB_ISNULL(db_schema)) {
    ret = OB_TABLE_NOT_EXIST;
    LOG_WARN("database dropped", K(ret), K(table_schema->get_database_id()));
  } else if (OB_FAIL(alter_table_arg.alter_table_schema_.set_origin_database_name(db_schema->get_database_name_str()))) {
    LOG_WARN("failed to set orig database name", K(ret));
  }

  // refresh constraint
  for (ObTableSchema::const_constraint_iterator it = alter_table_arg.alter_table_schema_.constraint_begin();
       OB_SUCC(ret) && it != alter_table_arg.alter_table_schema_.constraint_end(); it++) {
    ObConstraint *cst = (*it);
    const ObConstraint *cur_cst = nullptr;
    if (OB_ISNULL(cst)) {
      ret = OB_ERR_UNEXPECTED;
      LOG_WARN("invalid cst", K(ret));
    } else if (OB_ISNULL(cur_cst = table_schema->get_constraint(cst->get_constraint_id()))) {
      ret = OB_ERR_CONTRAINT_NOT_FOUND;
      LOG_WARN("current constraint not exists, maybe dropped", K(ret), KPC(cst), K(table_schema));
    } else if (OB_FAIL(cst->set_constraint_name(cur_cst->get_constraint_name_str()))) {
      LOG_WARN("failed to set new constraint name", K(ret));
    } else {
      cst->set_name_generated_type(cur_cst->get_name_generated_type());
    }
  }

  // refresh fk arg list
  if (OB_FAIL(ret)) {
  } else if (OB_INVALID_ID == foreign_key_id) {
    if (OB_UNLIKELY(0 != alter_table_arg.foreign_key_arg_list_.count())) {
      ret = OB_ERR_UNEXPECTED;
      LOG_WARN("must specify foreign key id to refresh fk arg list", K(ret), K(alter_table_arg.foreign_key_arg_list_));
    }
  } else {
    if (1 != alter_table_arg.foreign_key_arg_list_.count()) {
      ret = OB_ERR_UNEXPECTED;
      LOG_WARN("only support refresh one fk arg", K(ret));
    } else {
      const ObIArray<ObForeignKeyInfo> &fk_infos = table_schema->get_foreign_key_infos();
      const ObForeignKeyInfo *found_fk_info = nullptr;
      for (int64_t i = 0; nullptr == found_fk_info && i < fk_infos.count(); i++) {
        const ObForeignKeyInfo &fk_info = fk_infos.at(i);
        if (fk_info.foreign_key_id_ == foreign_key_id) {
          found_fk_info = &fk_info;
        }
      }
      if (OB_ISNULL(found_fk_info)) {
        ret = OB_ERR_CONTRAINT_NOT_FOUND;
        LOG_WARN("fk info not found, maybe dropped", K(ret), K(orig_table_id), K(foreign_key_id), K(fk_infos));
      } else if (OB_FAIL(ob_write_string(alter_table_arg.allocator_, found_fk_info->foreign_key_name_, alter_table_arg.foreign_key_arg_list_.at(0).foreign_key_name_, true/*c_style*/))) {
        LOG_WARN("failed to deep copy str", K(ret));
      }
    }
  }

  if (OB_SUCC(ret)) {
    if (OB_FAIL(alter_table_arg.based_schema_object_infos_.push_back(ObBasedSchemaObjectInfo(
        table_schema->get_table_id(),
        TABLE_SCHEMA,
        table_schema->get_schema_version())))) {
      LOG_WARN("failed to push back base schema object info", K(ret));
    } else if (OB_FAIL(alter_table_arg.based_schema_object_infos_.push_back(ObBasedSchemaObjectInfo(
        db_schema->get_database_id(),
        DATABASE_SCHEMA,
        db_schema->get_schema_version())))) {
      LOG_WARN("failed to push back base schema object info", K(ret));
    }
  }
  return ret;
}

int ObDDLUtil::generate_column_name_str(
    const common::ObIArray<ObColumnNameInfo> &column_names,
    const bool with_origin_name,
    const bool with_alias_name,
    const bool use_heap_table_ddl_plan,
    ObSqlString &sql_string)
{
  int ret = OB_SUCCESS;
  if (OB_UNLIKELY(column_names.count() == 0)) {
    ret = OB_INVALID_ARGUMENT;
    LOG_WARN("invalid arguments", K(ret), K(column_names.count()));
  } else {
    bool with_comma = false;
    for (int64_t i = 0; OB_SUCC(ret) && i < column_names.count(); ++i) {
      if (use_heap_table_ddl_plan && column_names.at(i).column_name_ == OB_HIDDEN_PK_INCREMENT_COLUMN_NAME) {
      } else if (OB_FAIL(generate_column_name_str(column_names.at(i), with_origin_name, with_alias_name, with_comma, sql_string))) {
        LOG_WARN("generate column name string failed", K(ret));
      } else {
        with_comma = true;
      }
    }
  }
  return ret;
}

int ObDDLUtil::generate_order_by_str(
    const ObIArray<int64_t> &select_column_ids,
    const ObIArray<int64_t> &order_column_ids,
    ObSqlString &sql_string)
{
  int ret = OB_SUCCESS;
  if (OB_UNLIKELY(select_column_ids.count() <= 0
        || order_column_ids.count() <= 0)) {
    ret = OB_INVALID_ARGUMENT;
    LOG_WARN("invalid arguments", K(ret), K(select_column_ids), K(order_column_ids));
  } else if (OB_FAIL(sql_string.append("order by "))) {
    LOG_WARN("append failed", K(ret));
  } else {
    bool append_comma = false;
    for (int64_t i = 0; OB_SUCC(ret) && i < order_column_ids.count(); ++i) {
      for (int64_t j = 0; OB_SUCC(ret) && j < select_column_ids.count(); ++j) {
        if (select_column_ids.at(j) == order_column_ids.at(i)) {
          if (OB_FAIL(sql_string.append_fmt("%s %ld", append_comma ? ",": "", j + 1))) {
            LOG_WARN("append fmt failed", K(ret));
          } else if (!append_comma) {
            append_comma = true;
          }
        }
      }
    }
  }
  return ret;
}

int ObDDLUtil::generate_column_name_str(
    const ObColumnNameInfo &column_name_info,
    const bool with_origin_name,
    const bool with_alias_name,
    const bool with_comma,
    ObSqlString &sql_string)
{
  int ret = OB_SUCCESS;
  const char *split_char = "`";
  // append comma
  if (with_comma) {
    if (OB_FAIL(sql_string.append_fmt(", "))) {
      LOG_WARN("append fmt failed", K(ret));
    }
  }
  // append original column name
  if (OB_SUCC(ret) && with_origin_name) {
    if (column_name_info.is_enum_set_need_cast_) {
      // Enum and set in Recover restore table ddl operation will be cast to unsigned, and then append into macro block.
      if (OB_FAIL(sql_string.append_fmt("cast(%s%.*s%s as unsigned)", split_char, column_name_info.column_name_.length(), column_name_info.column_name_.ptr(), split_char))) {
        LOG_WARN("append origin column name failed", K(ret));
      }
    } else if (OB_FAIL(sql_string.append_fmt("%s%.*s%s", split_char, column_name_info.column_name_.length(), column_name_info.column_name_.ptr(), split_char))) {
      LOG_WARN("append origin column name failed", K(ret));
    }
  }
  // append AS
  if (OB_SUCC(ret) && with_origin_name && with_alias_name) {
    if (OB_FAIL(sql_string.append_fmt(" AS "))) {
      LOG_WARN("append as failed", K(ret));
    }
  }
  // append alias column name
  if (OB_SUCC(ret) && with_alias_name) {
    if (OB_FAIL(sql_string.append_fmt("%s%s%.*s%s", split_char, column_name_info.is_shadow_column_ ? "__SHADOW_" : "",
        column_name_info.column_name_.length(), column_name_info.column_name_.ptr(), split_char))) {
      LOG_WARN("append alias name failed", K(ret));
    }
  }
  return ret;
}

int ObDDLUtil::generate_ddl_schema_hint_str(
    const ObString &table_name,
    const int64_t schema_version,
    ObSqlString &sql_string)
{
  int ret = OB_SUCCESS;
  if (OB_FAIL(sql_string.append_fmt("ob_ddl_schema_version(`%.*s`, %ld)",
      static_cast<int>(table_name.length()), table_name.ptr(), schema_version))) {
    LOG_WARN("append origin column name failed", K(ret));
  }
  return ret;
}


int ObDDLUtil::generate_spatial_index_column_names(const ObTableSchema &dest_table_schema,
                                                   const ObTableSchema &source_table_schema,
                                                   ObArray<ObColumnNameInfo> &insert_column_names,
                                                   ObArray<ObColumnNameInfo> &column_names,
                                                   ObArray<int64_t> &select_column_ids)
{
  int ret = OB_SUCCESS;
  if (dest_table_schema.is_spatial_index()) {
    uint64_t geo_col_id = OB_INVALID_ID;
    ObArray<ObColDesc> column_ids;
    const ObColumnSchemaV2 *column_schema = nullptr;
    if (column_names.count() > select_column_ids.count()) {
      for (int64_t i = 0; OB_SUCC(ret) && i < column_names.count(); ++i) {
        if (OB_ISNULL(column_schema = source_table_schema.get_column_schema(column_names.at(i).column_name_))) {
          ret = OB_ERR_UNEXPECTED;
          LOG_WARN("error unexpected, column schema must not be nullptr", K(ret));
        } else if (is_contain(select_column_ids, static_cast<int64_t>(column_schema->get_column_id()))) {
          // do nothing
        } else if (OB_FAIL(select_column_ids.push_back(column_schema->get_column_id()))) {
          LOG_WARN("push back select column id failed", K(ret));
        }
      }
    }
    // get dest table column names
    if (OB_FAIL(ret)) {
    } else if (OB_FAIL(dest_table_schema.get_column_ids(column_ids))) {
      LOG_WARN("fail to get column ids", K(ret));
    } else {
      for (int64_t i = 0; OB_SUCC(ret) && i < column_ids.count(); ++i) {
        const int64_t col_id =  column_ids.at(i).col_id_;
        if (OB_ISNULL(column_schema = dest_table_schema.get_column_schema(col_id))) {
          ret = OB_ERR_UNEXPECTED;
          LOG_WARN("error unexpected, column schema must not be nullptr", K(ret));
        } else if (is_contain(select_column_ids, col_id)) {
          // do nothing
        } else if (OB_FAIL(insert_column_names.push_back(ObColumnNameInfo(column_schema->get_column_name_str(), false)))) {
          LOG_WARN("push back insert column name failed", K(ret));
        } else if (OB_FAIL(column_names.push_back(ObColumnNameInfo(column_schema->get_column_name_str(), false)))) {
          LOG_WARN("push back rowkey column name failed", K(ret));
        } else if (OB_FAIL(select_column_ids.push_back(col_id))) {
          LOG_WARN("push back select column id failed", K(ret), K(col_id));
        } else if (OB_NOT_NULL(column_schema = source_table_schema.get_column_schema(col_id))
                   && !column_schema->is_rowkey_column()
                   && geo_col_id == OB_INVALID_ID) {
          geo_col_id = column_schema->get_geo_col_id();
        }
      }
      if (OB_SUCC(ret)) {
        if (geo_col_id == OB_INVALID_ID) {
          ret = OB_ERR_UNEXPECTED;
          LOG_WARN("error unexpected, get geo column failed", K(ret));
        } else if (OB_ISNULL(column_schema = source_table_schema.get_column_schema(geo_col_id))) {
          ret = OB_ERR_UNEXPECTED;
          LOG_WARN("error unexpected, column schema must not be nullptr", K(ret));
        } else if (OB_FAIL(column_names.push_back(ObColumnNameInfo(column_schema->get_column_name_str(), false)))) {
          LOG_WARN("push back geo column name failed", K(ret));
        } else if (OB_FAIL(select_column_ids.push_back(geo_col_id))) {
          LOG_WARN("push back select column id failed", K(ret), K(geo_col_id));
        }
      }
    }
  }
  return ret;
}


int ObDDLUtil::append_multivalue_extra_column(const ObTableSchema &dest_table_schema,
                                              const share::schema::ObTableSchema &source_table_schema,
                                              ObArray<ObColumnNameInfo> &column_names,
                                              ObArray<int64_t> &select_column_ids)
{
  int ret = OB_SUCCESS;
  if (dest_table_schema.is_multivalue_index_aux()) {
    ObArray<ObColDesc> column_ids;
    const ObColumnSchemaV2 *column_schema = nullptr;
    const ObColumnSchemaV2 *array_column = nullptr;
    // get dest table column names
    if (OB_FAIL(dest_table_schema.get_column_ids(column_ids))) {
      LOG_WARN("fail to get column ids", K(ret));
    } else {
      for (int64_t i = 0; OB_SUCC(ret) && i < column_ids.count(); ++i) {
        const int64_t col_id =  column_ids.at(i).col_id_;
        if (OB_ISNULL(column_schema = source_table_schema.get_column_schema(col_id))) {
          ret = OB_ERR_UNEXPECTED;
          LOG_WARN("error unexpected, column schema must not be nullptr", K(ret));
        } else if (column_schema->is_multivalue_generated_column()) {
          array_column = source_table_schema.get_column_schema(col_id + 1);
          break;
        }
      } // end for

      if (OB_FAIL(ret)) {
      } else if (OB_ISNULL(array_column)) {
        ret = OB_ERR_UNEXPECTED;
        LOG_WARN("error unexpected, array column schema must not be nullptr", K(ret));
      } else {
        if (OB_FAIL(column_names.push_back(ObColumnNameInfo(array_column->get_column_name_str(), false)))) {
          LOG_WARN("push back rowkey column name failed", K(ret));
        } else if (OB_FAIL(select_column_ids.push_back(array_column->get_column_id()))) {
          LOG_WARN("push back select column id failed", K(ret));
        }
      }
    }
  }
  return ret;
}
// Used in offline ddl to delete all checksum record in __all_ddl_checksum
// DELETE FROM __all_ddl_checksum WHERE


bool ObDDLUtil::need_reshape(const ObObjMeta &col_type)
{
  return col_type.is_binary() || col_type.is_fixed_len_char_type();
}

// int ObDDLUtil::check_null_and_length moved definition to storage/ddl/ob_ddl_common_storage_impl.cpp(accesses blocksstable members)



// int ObDDLUtil::init_datum_row_with_snapshot moved definition to storage/ddl/ob_ddl_common_storage_impl.cpp(accesses blocksstable members)






// moved definition to the upper-layer owner cpp(transitional state)



// the lob is processed by column here,
// ddl routine require idempotence, so the lob cells must write by row. in order to build the row, invlaid lob cells (like null or nop) need output.
// direct load routine does not require idempotence, so the invalid lob cells need not to output.
// moved definition to sql/resolver/ddl/ob_ddl_resolver.cpp(vector vocabulary)


// moved definition to sql/resolver/ddl/ob_ddl_resolver.cpp(vector vocabulary)







int ObDDLUtil::get_tablet_ids(
    const int64_t table_id,
    const int64_t target_table_id,
    common::ObIArray<common::ObTabletID> &tablet_ids)
{
  int ret = OB_SUCCESS;
  ObMultiVersionSchemaService &schema_service = ObMultiVersionSchemaService::get_instance();
  ObSchemaGetterGuard schema_guard;
  const ObTableSchema *data_table_schema = nullptr;
  const ObTableSchema *dest_table_schema = nullptr;
  tablet_ids.reset();
  if (OB_UNLIKELY(OB_INVALID_ID == table_id || OB_INVALID_ID == target_table_id)) {
    ret = OB_INVALID_ARGUMENT;
    LOG_WARN("there are invalid arguments", K(ret), K(table_id), K(target_table_id));
  } else if (OB_FAIL(schema_service.get_tenant_schema_guard(schema_guard))) {
    LOG_WARN("get tenant schema guard failed", K(ret));
  } else if (OB_FAIL(schema_guard.get_table_schema( table_id, data_table_schema))) {
    LOG_WARN("get table schema failed", K(ret), K(table_id));
  } else if (OB_FAIL(schema_guard.get_table_schema( target_table_id, dest_table_schema))) {
    LOG_WARN("get table schema failed", K(ret), K(target_table_id));
  }

  if (OB_FAIL(ret)) {
  } else if (OB_ISNULL(data_table_schema)) {
    LOG_INFO("table not exist", K(ret), K(table_id), K(target_table_id), KP(data_table_schema));
  } else if (OB_FAIL(ObDDLUtil::get_tablets(table_id, tablet_ids))) {
    LOG_WARN("failed to get data table snapshot", K(ret));
  } else if (data_table_schema->get_aux_lob_meta_tid() != OB_INVALID_ID &&
            OB_FAIL(ObDDLUtil::get_tablets(data_table_schema->get_aux_lob_meta_tid(), tablet_ids))) {
    LOG_WARN("failed to get data lob meta table snapshot", K(ret));
  } else if (data_table_schema->get_aux_lob_piece_tid() != OB_INVALID_ID &&
            OB_FAIL(ObDDLUtil::get_tablets(data_table_schema->get_aux_lob_piece_tid(), tablet_ids))) {
    LOG_WARN("failed to get data lob piece table snapshot", K(ret));
  }

  if (OB_FAIL(ret)) {
  } else if (OB_ISNULL(dest_table_schema)) {
    LOG_INFO("table not exist", K(ret), K(table_id), K(target_table_id), KP(dest_table_schema));
  } else if (OB_FAIL(ObDDLUtil::get_tablets(target_table_id, tablet_ids))) {
    LOG_WARN("failed to get dest table snapshot", K(ret));
  } else if (dest_table_schema->get_aux_lob_meta_tid() != OB_INVALID_ID &&
            OB_FAIL(ObDDLUtil::get_tablets(dest_table_schema->get_aux_lob_meta_tid(), tablet_ids))) {
    LOG_WARN("failed to get dest lob meta table snapshot", K(ret));
  } else if (dest_table_schema->get_aux_lob_piece_tid() != OB_INVALID_ID &&
            OB_FAIL(ObDDLUtil::get_tablets(dest_table_schema->get_aux_lob_piece_tid(), tablet_ids))) {
    LOG_WARN("failed to get dest lob piece table snapshot", K(ret));
  }
  return ret;
}




int ObDDLUtil::check_need_acquire_lob_snapshot(
    const ObTableSchema *data_table_schema,
    const ObTableSchema *index_table_schema,
    bool &need_acquire)
{
  int ret = OB_SUCCESS;
  need_acquire = false;
  if (OB_ISNULL(data_table_schema) || OB_ISNULL(index_table_schema)) {
    ret = OB_INVALID_ARGUMENT;
    LOG_WARN("there are invalid arguments", K(ret), KP(data_table_schema), KP(index_table_schema));
  } else {
    ObTableSchema::const_column_iterator iter = index_table_schema->column_begin();
    ObTableSchema::const_column_iterator iter_end = index_table_schema->column_end();
    for (; OB_SUCC(ret) && !need_acquire && iter != iter_end; iter++) {
      const ObColumnSchemaV2 *index_col = *iter;
      if (OB_ISNULL(index_col)) {
        ret = OB_ERR_UNEXPECTED;
        LOG_WARN("column schema is null", K(ret));
      } else {
        const ObColumnSchemaV2 *col = data_table_schema->get_column_schema(index_col->get_column_id());
        if (OB_ISNULL(col)) {
        } else if (col->is_generated_column()) {
          ObSEArray<uint64_t, 8> ref_columns;
          if (OB_FAIL(col->get_cascaded_column_ids(ref_columns))) {
            STORAGE_LOG(WARN, "Failed to get cascaded column ids", K(ret));
          } else {
            for (int64_t i = 0; OB_SUCC(ret) && !need_acquire && i < ref_columns.count(); i++) {
              const ObColumnSchemaV2 *data_table_col = data_table_schema->get_column_schema(ref_columns.at(i));
              if (OB_ISNULL(data_table_col)) {
                ret = OB_ERR_UNEXPECTED;
                LOG_WARN("column schema is null", K(ret));
              } else if (is_lob_storage(data_table_col->get_data_type())) {
                need_acquire = true;
              }
            }
          }
        }
      }
    }
  }
  return ret;
}

int ObDDLUtil::get_table_lob_col_idx(const ObTableSchema &table_schema, ObIArray<uint64_t> &lob_col_idxs)
{
  int ret = OB_SUCCESS;
  lob_col_idxs.reuse();
  ObArray<ObColDesc> all_column_ids;
  if (OB_FAIL(table_schema.get_store_column_ids(all_column_ids))) {
    LOG_WARN("failed to get column ids", K(ret), K(table_schema));
  }
  for (int64_t i = 0; OB_SUCC(ret) && i < all_column_ids.count(); ++i) {
    if (all_column_ids.at(i).col_type_.is_lob_storage() &&
        OB_FAIL(lob_col_idxs.push_back(i))) {
      LOG_WARN("failed to push back lob idx", K(ret));
    }
  }
  return ret;
}




int ObDDLUtil::get_index_table_batch_partition_names(
    const int64_t &data_table_id,
    const int64_t &index_table_id,
    const ObIArray<ObTabletID> &tablets,
    common::ObIAllocator &allocator,
    ObIArray<ObString> &partition_names)
{
  int ret = OB_SUCCESS;
  if ((OB_UNLIKELY(OB_INVALID_ID == data_table_id || OB_INVALID_ID == index_table_id || tablets.count() < 1))) {
    ret = OB_INVALID_ARGUMENT;
    LOG_WARN("the parameters is invalid", K(ret), K(data_table_id), K(index_table_id), K(tablets.count()));
  } else {
    ObSchemaGetterGuard schema_guard;
    const ObTableSchema *data_table_schema = nullptr;
    const ObTableSchema *index_schema = nullptr;
    if (OB_FAIL(ObMultiVersionSchemaService::get_instance().get_tenant_schema_guard(schema_guard))) {
      LOG_WARN("fail to get schema guard", K(ret));
    } else if (OB_FAIL(schema_guard.get_table_schema( data_table_id, data_table_schema))) {
      LOG_WARN("get table schema failed", K(ret), K(data_table_id));
    } else if (OB_ISNULL(data_table_schema)) {
      ret = OB_TABLE_NOT_EXIST;
      LOG_WARN("error unexpected, data table schema is null", K(ret), K(data_table_id));
    } else if (OB_FAIL(schema_guard.get_table_schema( index_table_id, index_schema))) {
      LOG_WARN("get table schema failed", K(ret), K(index_table_id));
    } else if (OB_ISNULL(index_schema)) {
      ret = OB_TABLE_NOT_EXIST;
      LOG_WARN("error unexpected, index table schema is null", K(ret), K(index_table_id));
    } else {
      const ObPartitionOption &data_part_option = data_table_schema->get_part_option();
      const ObPartitionOption &index_part_option = index_schema->get_part_option();
      if (OB_UNLIKELY(data_part_option.get_part_num() < 1)) {
        ret = OB_ERR_UNEXPECTED;
        LOG_WARN("data table part num less than 1", K(ret), K(data_part_option));
      } else if (OB_UNLIKELY(index_part_option.get_part_num() < 1)) {
        ret = OB_ERR_UNEXPECTED;
        LOG_WARN("index table part num less than 1", K(ret), K(index_part_option));
      } else if (OB_UNLIKELY(data_part_option.get_part_num() != index_part_option.get_part_num())) {
        ret = OB_ERR_UNEXPECTED;
        LOG_WARN("error unexpected, data table partition num not equal to index table partition num", K(ret), K(data_part_option.get_part_num()), K(index_part_option.get_part_num()));
      } else {
        ObPartition **data_partitions = data_table_schema->get_part_array();
        const ObPartitionLevel part_level = data_table_schema->get_part_level();
        if (OB_ISNULL(data_partitions)) {
          ret = OB_PARTITION_NOT_EXIST;
          LOG_WARN("data table part array is null", K(ret));
        } else {
          int64_t part_index = -1;
          int64_t subpart_index = -1;
          for (int64_t i = 0; i < tablets.count() && OB_SUCC(ret); i++) {
            if (OB_FAIL(index_schema->get_part_idx_by_tablet(tablets.at(i), part_index, subpart_index))) {
              LOG_WARN("failed to get part idx by tablet", K(ret), K(tablets.at(i)), K(part_index), K(subpart_index));
            } else {
              ObString tmp_name;
              if (PARTITION_LEVEL_ONE == part_level) {
                if OB_FAIL(deep_copy_ob_string(allocator,
                                               data_partitions[part_index]->get_part_name(),
                                               tmp_name)) {
                  LOG_WARN("fail to deep copy partition names", K(ret), K(data_partitions[part_index]->get_part_name()), K(tmp_name));
                } else if (OB_FAIL(partition_names.push_back(tmp_name))) {
                  LOG_WARN("fail to push back", K(ret), K(data_partitions[part_index]->get_part_name()), K(tmp_name), K(partition_names));
                }
              } else if (PARTITION_LEVEL_TWO == part_level) {
                ObSubPartition **data_subpart_array = data_partitions[part_index]->get_subpart_array();
                if (OB_ISNULL(data_subpart_array)) {
                  ret = OB_ERR_UNEXPECTED;
                  LOG_WARN("part array is null", K(ret), K(part_index));
                } else if OB_FAIL(deep_copy_ob_string(allocator,
                                                      data_subpart_array[subpart_index]->get_part_name(),
                                                      tmp_name)) {
                  LOG_WARN("fail to deep copy partition names", K(ret), K(data_subpart_array[subpart_index]->get_part_name()), K(tmp_name));
                } else if (OB_FAIL(partition_names.push_back(tmp_name))) {
                  LOG_WARN("fail to push back", K(ret), K(data_subpart_array[subpart_index]->get_part_name()), K(tmp_name), K(partition_names));
                }
              }
            }
          }
        }
      }
    }
  }
  return ret;
}

int ObDDLUtil::get_tablet_data_size(
    const common::ObTabletID &tablet_id,
    int64_t &data_size)
{
  int ret = OB_SUCCESS;
  data_size = 0;
  if (!tablet_id.is_valid()) {
    ret = OB_INVALID_ARGUMENT;
    LOG_WARN("invalid argument", K(ret), K(tablet_id));
  } else if (OB_ISNULL(GCTX.meta_db_pool_)) {
    ret = OB_NOT_INIT;
    LOG_WARN("meta_db_pool_ is not initialized", K(ret));
  } else {
    ObTabletMetaTableStorage storage;
    if (OB_FAIL(storage.init(GCTX.meta_db_pool_))) {
      LOG_WARN("failed to init storage", K(ret));
    } else if (OB_FAIL(storage.get_max_data_size(tablet_id, data_size))) {
      LOG_WARN("failed to get max data size", K(ret), K(tablet_id));
    }
  }
  return ret;
}

int ObDDLUtil::get_tablet_data_row_cnt(
    const common::ObTabletID &tablet_id,
    int64_t &data_row_cnt)
{
  int ret = OB_SUCCESS;
  data_row_cnt = 0;
  if (!tablet_id.is_valid()) {
    ret = OB_INVALID_ARGUMENT;
    LOG_WARN("invalid argument", K(ret), K(tablet_id));
  } else if (OB_ISNULL(GCTX.meta_db_pool_)) {
    ret = OB_NOT_INIT;
    LOG_WARN("meta_db_pool_ not initialized", K(ret));
  } else {
    ObTabletReplicaChecksumTableStorage storage;
    if (OB_FAIL(storage.init(GCTX.meta_db_pool_))) {
      LOG_WARN("failed to init storage", K(ret));
    } else if (OB_FAIL(storage.get_max_row_count(tablet_id, data_row_cnt))) {
      LOG_WARN("failed to get max row count from storage", K(ret), K(tablet_id));
    }
  }
  return ret;
}

int ObDDLUtil::get_ls_host_left_disk_space(uint64_t &left_space_size)
{
  int ret = OB_SUCCESS;
  left_space_size = 0;
  SMART_VAR(ObMySQLProxy::MySQLResult, res) {
    ObSqlString query_string;
    sqlclient::ObMySQLResult *result = NULL;
    if (OB_FAIL(query_string.assign_fmt("SELECT free_size FROM %s LIMIT 1",
        OB_ALL_VIRTUAL_DISK_STAT_TNAME))) {
      LOG_WARN("assign sql string failed", K(ret), K(OB_ALL_VIRTUAL_DISK_STAT_TNAME));
    } else if (OB_FAIL(GCTX.sql_proxy_->read(res, query_string.ptr()))) {
      LOG_WARN("read record failed", K(ret), K(query_string));
    } else if (OB_ISNULL(result = res.get_result())) {
      ret = OB_ERR_UNEXPECTED;
      LOG_WARN("fail to get sql result", K(ret), K(query_string));
    } else if (OB_FAIL(result->next())) {
      if (OB_ITER_END == ret) {
        ret = OB_SUCCESS;
      } else {
        LOG_WARN("fail to get next", K(ret), K(query_string));
      }
    } else {
      EXTRACT_INT_FIELD_MYSQL(*result, "free_size", left_space_size, uint64_t);
    }
  }
  return ret;
}



int ObDDLUtil::check_table_exist(
    const uint64_t table_id,
    ObSchemaGetterGuard &schema_guard)
{
  int ret = OB_SUCCESS;
  const ObTableSchema *table_schema = nullptr;
  const ObDatabaseSchema *database_schema = nullptr;
  uint64_t database_id = OB_INVALID_ID;
  if (OB_FAIL(schema_guard.get_table_schema( table_id, table_schema))) {
    LOG_WARN("failed to get table schema", K(ret));
  } else if (OB_ISNULL(table_schema) || table_schema->is_in_recyclebin()) {
    ret = OB_TABLE_NOT_EXIST;
    LOG_WARN("table not exist", K(ret), K(table_id), K(table_schema));
  } else if (OB_FALSE_IT(database_id = table_schema->get_database_id())) {
  } else if (OB_FAIL(schema_guard.get_database_schema( database_id, database_schema))) {
    LOG_WARN("failed to get database schema", K(ret), K(table_id), K(database_id));
  } else if (OB_ISNULL(database_schema) || database_schema->is_in_recyclebin()) {
    ret = OB_TABLE_NOT_EXIST;
    LOG_WARN("database not exist", K(ret), K(table_id), K(database_id), K(database_schema));
  }
  return ret;
}

int ObDDLUtil::get_ddl_rpc_timeout(const int64_t tablet_count, int64_t &ddl_rpc_timeout_us)
{
  int ret = OB_SUCCESS;
  const int64_t rpc_timeout_upper = 20L * 60L * 1000L * 1000L; // upper 20 minutes
  const int64_t cost_per_tablet = 20L * 60L * 100L; // 10000 tablets use 20 minutes, so 1 tablet use 20 * 60 * 100 us
  ddl_rpc_timeout_us = tablet_count * cost_per_tablet;
  ddl_rpc_timeout_us = min(ddl_rpc_timeout_us, rpc_timeout_upper);
  ddl_rpc_timeout_us = max(ddl_rpc_timeout_us, GCONF._ob_ddl_timeout);
  return ret;
}

int ObDDLUtil::get_ddl_rpc_timeout_by_table(const int64_t table_id, int64_t &ddl_rpc_timeout_us)
{
  int ret = OB_SUCCESS;
  int64_t tablet_count = 0;
  if (OB_UNLIKELY(OB_INVALID_ID == table_id)) {
    ret = OB_INVALID_ARGUMENT;
    LOG_WARN("invalid argument", K(ret), K(table_id));
  } else if (OB_FAIL(get_tablet_count(table_id, tablet_count))) {
    ret = OB_SUCCESS; // force succ
    tablet_count = 0;
  }
  if (OB_FAIL(ret)) {
  } else if (OB_FAIL(get_ddl_rpc_timeout(tablet_count, ddl_rpc_timeout_us))) {
    LOG_WARN("get ddl rpc timeout failed", K(ret));
  }
  return ret;
}

void ObDDLUtil::get_ddl_rpc_timeout_for_database(const int64_t database_id, int64_t &ddl_rpc_timeout_us)
{
  int ret = OB_SUCCESS;
  const int64_t cost_per_tablet = 100 * 1000L; // 100ms
  share::schema::ObSchemaGetterGuard schema_guard;
  ObArray<uint64_t> table_ids;
  if (OB_INVALID_ID == database_id) {
    ret = OB_INVALID_ARGUMENT;
    LOG_WARN("invalid arguments", K(ret), K(database_id));
  } else if (OB_FAIL(share::schema::ObMultiVersionSchemaService::get_instance().get_tenant_schema_guard(
      schema_guard))) {
    LOG_WARN("get tenant schema guard failed", K(ret));
  } else if (OB_FAIL(schema_guard.get_table_ids_in_database(database_id,
                                                            table_ids))) {
    LOG_WARN("failed to get table ids in database", K(ret));
  }
  for (int64_t i = 0; i < table_ids.count(); i++) {
    int64_t tablet_count = 0;
    if (OB_SUCCESS != get_tablet_count(table_ids[i], tablet_count)) {
      tablet_count = 0;
    }
    ddl_rpc_timeout_us += tablet_count * cost_per_tablet;
  }
  ddl_rpc_timeout_us = max(ddl_rpc_timeout_us, get_default_ddl_rpc_timeout());
  ddl_rpc_timeout_us = max(ddl_rpc_timeout_us, GCONF._ob_ddl_timeout);
  return;
}

int ObDDLUtil::get_ddl_tx_timeout(const int64_t tablet_count, int64_t &ddl_tx_timeout_us)
{
  int ret = OB_SUCCESS;
  if (OB_FAIL(get_ddl_rpc_timeout(tablet_count, ddl_tx_timeout_us))) {
    LOG_WARN("get ddl rpc timeout faild", K(ret));
  }
  return ret;
}

int64_t ObDDLUtil::get_default_ddl_rpc_timeout()
{
  return min(static_cast<int64_t>(20L * 60L * 1000L * 1000L), max(GCONF.rpc_timeout, static_cast<int64_t>(9 * 1000 * 1000L)));
}



/*
* return the map between tablet id & slice cnt;
* note that pair <0, 0> may exist when result is not partition table
*/

int ObDDLUtil::get_data_information(const uint64_t task_id,
    uint64_t &data_format_version,
    int64_t &snapshot_version,
    share::ObDDLTaskStatus &task_status)
{
  uint64_t target_object_id = 0;
  int64_t schema_version = 0;
  bool is_no_logging = false;
  bool is_offline_index_rebuild = false;
  return get_data_information(task_id,
      data_format_version,
      snapshot_version,
      task_status,
      target_object_id,
      schema_version,
      is_no_logging,
      is_offline_index_rebuild);
}

int ObDDLUtil::reshape_ddl_column_obj(
    common::ObDatum &datum,
    const ObObjMeta &obj_meta)
{
  int ret = OB_SUCCESS;
  if (datum.is_null()) {
    // do not need to reshape
  } else if (obj_meta.is_lob_storage()) {
    ObLobLocatorV2 lob(datum.get_string(), obj_meta.has_lob_header());
    ObString disk_loc;
    if (!lob.is_valid()) {
      ret = OB_ERR_UNEXPECTED;
      LOG_WARN("invalid lob locator", K(ret));
    } else if (!lob.is_lob_disk_locator() && !lob.is_persist_lob()) {
      ret = OB_ERR_UNEXPECTED;
      LOG_WARN("invalid lob locator, should be persist lob", K(ret), K(lob));
    } else if (OB_FAIL(lob.get_disk_locator(disk_loc))) {
      LOG_WARN("get disk locator failed", K(ret), K(lob));
    }
    if (OB_SUCC(ret)) {
      datum.set_string(disk_loc);
    }
  } else if (OB_UNLIKELY(!obj_meta.is_fixed_len_char_type())) {
    ret = OB_ERR_UNEXPECTED;
    LOG_WARN("no need to reshape non-char", K(ret));
  } else {
    const char *ptr = datum.ptr_;
    int32_t len = datum.len_;
    int32_t trunc_len_byte = static_cast<int32_t>(ObCharset::strlen_byte_no_sp(
        obj_meta.get_collation_type(), ptr, len));
    datum.set_string(ObString(trunc_len_byte, ptr));
  }
  return ret;
}

int ObDDLUtil::get_tenant_schema_guard(
    share::schema::ObSchemaGetterGuard &hold_buf_src_tenant_schema_guard,
    share::schema::ObSchemaGetterGuard &hold_buf_dst_tenant_schema_guard,
    share::schema::ObSchemaGetterGuard *&src_tenant_schema_guard,
    share::schema::ObSchemaGetterGuard *&dst_tenant_schema_guard)
{
  int ret = OB_SUCCESS;
  UNUSED(hold_buf_src_tenant_schema_guard);
  src_tenant_schema_guard = nullptr;
  dst_tenant_schema_guard = nullptr;
  if (OB_ISNULL(GCTX.schema_service_)) {
    ret = OB_ERR_SYS;
    LOG_WARN("error sys, root service must not be nullptr", K(ret));
  } else {
    share::schema::ObMultiVersionSchemaService &schema_service = *GCTX.schema_service_;
    if (OB_FAIL(schema_service.get_tenant_schema_guard(hold_buf_dst_tenant_schema_guard))) {
      LOG_WARN("get tanant schema guard failed", K(ret));
    } else {
      src_tenant_schema_guard = &hold_buf_dst_tenant_schema_guard;
      dst_tenant_schema_guard = &hold_buf_dst_tenant_schema_guard;
    }
  }
  if (OB_SUCC(ret)) {
    if (OB_UNLIKELY(nullptr == src_tenant_schema_guard || nullptr == dst_tenant_schema_guard)) {
      ret = OB_TENANT_NOT_EXIST;
      LOG_WARN("tenant not exist", K(ret), KP(src_tenant_schema_guard), KP(dst_tenant_schema_guard));
    }
  }
  return ret;
}


int ObDDLUtil::check_schema_version_refreshed(const int64_t target_schema_version)
{
  int ret = OB_SUCCESS;
  int64_t refreshed_schema_version = 0;
  if (OB_UNLIKELY(false || target_schema_version <= 0)) {
    ret = OB_INVALID_ARGUMENT;
    LOG_WARN("invalid args", K(ret), K(target_schema_version));
  } else if (OB_FAIL(ObMultiVersionSchemaService::get_instance().get_tenant_refreshed_schema_version(
      refreshed_schema_version))) {
    LOG_WARN("get refreshed schema version failed", K(ret), K(refreshed_schema_version));
    if (OB_ENTRY_NOT_EXIST == ret) {
      ret = OB_SCHEMA_EAGAIN;
    }
  } else if (!ObSchemaService::is_formal_version(refreshed_schema_version) || refreshed_schema_version < target_schema_version) {
    ret = OB_SCHEMA_EAGAIN;
    if (REACH_TIME_INTERVAL(1000L * 1000L)) {
      LOG_INFO("tenant schema not refreshed to the target version", K(ret), K(target_schema_version), K(refreshed_schema_version));
    }
  }
  return ret;
}

bool ObDDLUtil::reach_time_interval(const int64_t i, volatile int64_t &last_time)
{
  bool bret = false;
  const int64_t old_time = last_time;
  const int64_t cur_time = common::ObTimeUtility::fast_current_time();
  if (OB_UNLIKELY((i + last_time) < cur_time)
      && old_time == ATOMIC_CAS(&last_time, old_time, cur_time))
  {
    bret = true;
  }
  return bret;
}

int ObDDLUtil::get_temp_store_compress_type(const share::schema::ObTableSchema *table_schema,
                                            const int64_t parallel,
                                            ObCompressorType &compr_type)
{
  int ret = OB_SUCCESS;
  if (OB_ISNULL(table_schema)) {
    ret  = OB_INVALID_ARGUMENT;
    LOG_WARN("invalid argument", K(ret), KP(table_schema));
  } else {
    ObCompressorType schema_compr_type = table_schema->get_compressor_type();
    if (NONE_COMPRESSOR == schema_compr_type && table_schema->get_row_store_type() != FLAT_ROW_STORE) { // encoding without compress
      schema_compr_type = ZSTD_1_3_8_COMPRESSOR;
    }
    ret = get_temp_store_compress_type(schema_compr_type, parallel, compr_type);
  }
  return ret;
}

int ObDDLUtil::get_temp_store_compress_type(const ObCompressorType schema_compr_type,
                                            const int64_t parallel,
                                            ObCompressorType &compr_type)
{
  int ret = OB_SUCCESS;
  compr_type = NONE_COMPRESSOR;
  if (OB_UNLIKELY(!true)) {
    ret = OB_ERR_UNEXPECTED;
    LOG_WARN("fail get tenant_config", K(ret));
  } else {
    if (0 == GCONF._ob_ddl_temp_file_compress_func.get_value_string().case_compare("NONE")) {
      compr_type = NONE_COMPRESSOR;
    } else if (0 == GCONF._ob_ddl_temp_file_compress_func.get_value_string().case_compare("ZSTD")) {
      compr_type = ZSTD_1_3_8_COMPRESSOR;
    } else if (0 == GCONF._ob_ddl_temp_file_compress_func.get_value_string().case_compare("LZ4")) {
      compr_type = ZSTD_1_3_8_COMPRESSOR;
    } else if (0 == GCONF._ob_ddl_temp_file_compress_func.get_value_string().case_compare("AUTO")) {
      UNUSED(parallel);
      if (schema_compr_type > INVALID_COMPRESSOR && schema_compr_type < MAX_COMPRESSOR) {
        compr_type = schema_compr_type;
      } else {
        compr_type = NONE_COMPRESSOR;
      }
    } else {
      ret = OB_ERR_UNEXPECTED;
      LOG_WARN("the temp store format config is unexpected", K(ret), K(GCONF._ob_ddl_temp_file_compress_func.get_value_string()));
    }
  }
  LOG_INFO("get compressor type", K(ret), K(compr_type), K(schema_compr_type));
  return ret;
}

int ObDDLUtil::check_table_compaction_checksum_error(const uint64_t table_id)
{
  int ret = OB_SUCCESS;
  if (OB_INVALID_ID == table_id) {
    ret = OB_INVALID_ARGUMENT;
    LOG_WARN("invalid argument", KR(ret), K(table_id));
  } else if (OB_FAIL(check_table_column_checksum_error(table_id))) {
    LOG_WARN("check_table_column_checksum_error fail", KR(ret), K(table_id));
  } else if (OB_FAIL(check_tablet_checksum_error(table_id))) {
    LOG_WARN("check_tablet_checksum_error fail", KR(ret), K(table_id));
  }
  return ret;
}

int ObDDLUtil::check_table_column_checksum_error(const int64_t table_id)
{
  int ret = OB_SUCCESS;
  if (OB_INVALID_ID == table_id) {
    ret = OB_INVALID_ARGUMENT;
    LOG_WARN("invalid argument", KR(ret), K(table_id));
  } else {
    ObSqlString query_string;
    sqlclient::ObMySQLResult *result = nullptr;
    ObTimeoutCtx timeout_ctx;
    SMART_VAR(ObMySQLProxy::MySQLResult, res) {
      if OB_FAIL(ret) {
        LOG_WARN("fail to create object ObMySQLProxy::MySQLResult", KR(ret), K(table_id));
      } else if (OB_FAIL(query_string.append_fmt("SELECT data_table_id FROM %s WHERE data_table_id = %lu LIMIT 1",
          OB_ALL_VIRTUAL_COLUMN_CHECKSUM_ERROR_INFO_TNAME, table_id))) {
        LOG_WARN("assign sql string failed", KR(ret), K(query_string));
      } else if (OB_ISNULL(GCTX.sql_proxy_)) {
        ret = OB_INVALID_ARGUMENT;
        LOG_WARN("invalid arg", K(ret), KP(GCTX.sql_proxy_));
      } else if (OB_FAIL(ObShareUtil::set_default_timeout_ctx(timeout_ctx, GCONF.internal_sql_execute_timeout))) {
        LOG_WARN("failed to set timeout ctx", K(ret), K(timeout_ctx));
      } else if (OB_FAIL(GCTX.sql_proxy_->read(res, query_string.ptr()))) {
        LOG_WARN("read record failed", K(ret), K(query_string));
      } else if (OB_ISNULL(result = res.get_result())) {
        ret = OB_ERR_UNEXPECTED;
        LOG_WARN("fail to get sql result", K(ret), KP(result));
      } else if (OB_FAIL(result->next()) && ret != OB_ITER_END ) {
        LOG_WARN("fail to get sql result", K(ret), KP(result));
      } else if (OB_ITER_END == ret) {
        ret = OB_SUCCESS;
      } else {
        ret = OB_NOT_SUPPORTED; // we expect the sql to return an empty result
        LOG_ERROR("table index checksum error", K(ret), K(table_id));
        LOG_USER_ERROR(OB_NOT_SUPPORTED, "Redefinition on compaction checksum error table is");
      }
    }
  }
  return ret;
}

int ObDDLUtil::check_tablet_checksum_error(const int64_t table_id)
{
  int ret = OB_SUCCESS;
  if (OB_INVALID_ID == table_id) {
    ret = OB_INVALID_ARGUMENT;
    LOG_WARN("invalid argument", KR(ret), K(table_id));
  } else {
    ObArray<ObTabletID> tablet_ids;
    if (OB_FAIL(ObDDLUtil::get_tablets(table_id, tablet_ids))) {
      LOG_WARN("fail to get tablets", K(ret), K(tablet_ids));
    } else {
      int64_t start_idx = 0;
      int64_t end_idx = min(ObDDLUtil::MAX_BATCH_COUNT, tablet_ids.count());
      while (OB_SUCC(ret) && start_idx < tablet_ids.count()) {
        if (OB_FAIL(batch_check_tablet_checksum(start_idx, end_idx, tablet_ids))) {
          LOG_WARN("fail to batch get teablet_ids", K(ret), K(table_id));
        } else {
          start_idx = end_idx;
          end_idx = min(start_idx + ObDDLUtil::MAX_BATCH_COUNT, tablet_ids.count());
        }
      }
    }
  }
  return ret;
}


int ObDDLUtil::batch_check_tablet_checksum(const int64_t start_idx,
    const int64_t end_idx,
    const ObIArray<ObTabletID> &tablet_ids)
{
  int ret = OB_SUCCESS;
  if (start_idx < 0 || end_idx > tablet_ids.count()
      || start_idx >= end_idx || tablet_ids.count() <= 0) {
    ret = OB_INVALID_ARGUMENT;
    LOG_WARN("invalid argument", KR(ret), K(start_idx), K(end_idx));
  } else if (OB_ISNULL(GCTX.meta_db_pool_)) {
    ret = OB_NOT_INIT;
    LOG_WARN("meta_db_pool_ not initialized", K(ret));
  } else {
    ObTabletReplicaChecksumTableStorage storage;
    bool has_error = false;
    if (OB_FAIL(storage.init(GCTX.meta_db_pool_))) {
      LOG_WARN("failed to init storage", K(ret));
    } else if (OB_FAIL(storage.batch_check_checksum(tablet_ids, start_idx, end_idx, has_error))) {
      LOG_WARN("failed to batch check checksum from storage", K(ret), K(start_idx), K(end_idx));
    } else if (has_error) {
      ret = OB_CHECKSUM_ERROR;
      LOG_ERROR("tablet checksum error detected", K(ret));
    }
  }
  return ret;
}

bool ObDDLUtil::use_idempotent_mode()
{
  return true;
}

// int ObDDLUtil::init_macro_block_seq moved definition to storage/ddl/ob_ddl_common_storage_impl.cpp(accesses blocksstable members)

// int64_t ObDDLUtil::get_parallel_idx moved definition to storage/ddl/ob_ddl_common_storage_impl.cpp(accesses blocksstable members)


#ifdef OB_BUILD_SHARED_STORAGE
int ObDDLUtil::upload_block_for_ss(const char *buf, const int64_t len, const blocksstable::MacroBlockId &macro_block_id)
{
  int ret = OB_SUCCESS;
  if (nullptr == buf || 0 == len || !macro_block_id.is_valid()) {
    ret = OB_INVALID_ARGUMENT;
    LOG_WARN("invalid argumen", K(ret), KP(buf), K(len), K(macro_block_id));
  } else {
    ObStorageObjectHandle object_handle;
    ObStorageObjectWriteInfo object_info;
    object_info.buffer_ = buf;
    object_info.offset_ = 0;
    object_info.size_ = len;
    object_info.io_desc_.set_wait_event(ObWaitEventIds::DB_FILE_COMPACT_WRITE);
    object_info.io_desc_.set_unsealed();
    object_info.io_desc_.set_sys_module_id(ObIOModule::SHARED_BLOCK_RW_IO);
    object_info.ls_epoch_id_ = 0;

    if (OB_FAIL(OB_STORAGE_OBJECT_MGR.async_write_object(macro_block_id, object_info, object_handle))) {
      LOG_WARN("failed to write info", K(ret), K(macro_block_id), K(object_info), K(object_handle));
    } else if (OB_FAIL(object_handle.wait())) {
      LOG_WARN("failed to wai object handle finish", K(ret));
    }
  }
  return ret;
}

/*
 used for adding gc info when ddl update tablet
 ddl may retry and generate same major which need to skip
*/
int ObDDLUtil::update_tablet_gc_info(const ObTabletID &tablet_id, const int64_t pre_snapshot_version, const int64_t new_snapshot_version)
{
  int ret = OB_SUCCESS;
  ObGCTabletMetaInfoList tablet_meta_version_list;
  ObTenantStorageMetaService *meta_service = MTL(ObTenantStorageMetaService*);
  bool is_exist = false;

  if (!tablet_id.is_valid() || OB_INVALID_TIMESTAMP == new_snapshot_version) {
    ret = OB_INVALID_ARGUMENT;
    LOG_WARN("invalid arguments", K(ret), K(tablet_id), K(new_snapshot_version));
  } else if (OB_ISNULL(meta_service)) {
    ret = OB_ERR_UNEXPECTED;
    LOG_WARN("meta service should not be null", K(ret));
  } else if (pre_snapshot_version == new_snapshot_version) {
    /* skip */
  } else if (OB_FAIL(ObTenantStorageMetaService::ss_is_meta_list_exist(tablet_id, is_exist))) {
    LOG_WARN("fail to check existence", K(ret), K(tablet_id));
  } else if (is_exist) {
    /* skip */
  } else {
    ObGCTabletMetaInfo meta_info;
    ObGCTabletMetaInfoList tablet_meta_version_list;
    if (OB_FAIL(meta_info.scn_.convert_for_tx(new_snapshot_version))) {
      LOG_WARN("fail to convert for tx", K(ret), K(new_snapshot_version));
    } else if (OB_FAIL(tablet_meta_version_list.tablet_version_arr_.push_back(meta_info))) {
      LOG_WARN("failed to push back gc info", K(ret));
    } else if (OB_FAIL(meta_service->write_gc_tablet_scn_arr(tablet_id, ObStorageObjectType::SHARED_MAJOR_META_LIST, tablet_meta_version_list))) {
      LOG_WARN("failed to write gc info arr", K(ret), K(tablet_id));
    }
  }
  return ret;
}

#endif

int ObDDLUtil::get_global_index_table_ids(const schema::ObTableSchema &table_schema, ObIArray<uint64_t> &global_index_table_ids, ObSchemaGetterGuard &schema_guard)
{
  int ret = OB_SUCCESS;
  ObSEArray<ObAuxTableMetaInfo, 16> simple_index_infos;
  global_index_table_ids.reset();
  if (OB_UNLIKELY(!table_schema.is_valid())) {
    ret = OB_INVALID_ARGUMENT;
    LOG_WARN("invalid argument", K(ret), K(table_schema), K(table_schema.is_valid()));
  } else if (OB_FAIL(table_schema.get_simple_index_infos(simple_index_infos))) {
    LOG_WARN("get simple index infos failed", K(ret));
  } else {

    for (int64_t i = 0; OB_SUCC(ret) && i < simple_index_infos.count(); i++) {
      const ObTableSchema *aux_table_schema = NULL;
      if (OB_FAIL(schema_guard.get_table_schema( simple_index_infos.at(i).table_id_, aux_table_schema))) {
        LOG_WARN("get table schema failed", K(ret), K(simple_index_infos.at(i).table_id_));
      } else if (OB_ISNULL(aux_table_schema)) {
        ret = OB_ERR_UNEXPECTED;
        LOG_WARN("table schema should not be null", K(ret));
      } else if (aux_table_schema->is_global_index_table()) {
        if (OB_FAIL(global_index_table_ids.push_back(aux_table_schema->get_table_id()))) {
          LOG_WARN("failed to push back", K(ret), K(aux_table_schema->get_table_id()));
        }
      }
    }
  }
  return ret;
}

int ObDDLUtil::get_no_logging_param(bool &is_no_logging)
{
  int ret = OB_SUCCESS;
  is_no_logging = false;
  if (!true) {
    ret = OB_ERR_UNEXPECTED;
    LOG_WARN("tenant config is invalid", K(ret));
  } else {
    is_no_logging = GCONF._no_logging;
  }
  return ret;
}

bool ObDDLUtil::is_vector_index_complement(const ObIndexType index_type)
{
  return is_vec_index_snapshot_data_type(index_type)
      || is_local_vec_ivf_centroid_index(index_type)
      || is_vec_ivfsq8_meta_index(index_type)
      || is_vec_ivfpq_pq_centroid_index(index_type)
      || is_hybrid_vec_index_embedded_type(index_type);
}

int64_t ObDDLUtil::generate_idempotent_value(
    const int64_t slice_count,
    const int64_t slice_idx,
    const int64_t range_interval,
    const int64_t slice_row_idx)
{
  const int64_t range_id = slice_row_idx / range_interval;
  const int64_t row_id_in_range = slice_row_idx % range_interval;
  return range_id * slice_count * range_interval + slice_idx * range_interval + row_id_in_range;
}

// moved definition to sql/resolver/ddl/ob_ddl_resolver.cpp(transitional state)

int ObSqlMonitorStats::init(const int64_t task_id, const ObDDLType ddl_type)
{
  int ret = OB_SUCCESS;
  if (false || task_id <= 0 || ddl_type == DDL_INVALID) {
    ret = OB_INVALID_ARGUMENT;
    LOG_WARN("invalid argument", K(ret), K(task_id_), K(ddl_type));
  } else {

    task_id_ = task_id;
    ddl_type_ = ddl_type;
    is_inited_ = true;
  }
  return ret;
}

int ObSqlMonitorStats::clean_invalid_data(const int64_t execution_id)
{
  int ret = OB_SUCCESS;
  if (!is_inited_) {
    ret = OB_NOT_INIT;
    LOG_WARN("not init", K(ret));
  } else if (execution_id > execution_id_ && ddl_type_ != ObDDLType::DDL_CREATE_PARTITIONED_LOCAL_INDEX) {
    reuse();
  }
  execution_id_ = OB_MAX(execution_id, execution_id_);

  return ret;
}

// moved definition to sql/resolver/ddl/ob_ddl_resolver.cpp(transitional state)

int ObSqlMonitorStatsCollector::get_scan_monitor_stats_batch(sqlclient::ObMySQLResult *scan_result)
{
  int ret = OB_SUCCESS;
  if (OB_ISNULL(scan_result)) {
    ret = OB_INVALID_ARGUMENT;
    LOG_WARN("scan result is null", K(ret));
  } else {
    char trace_id_str[OB_MAX_TRACE_ID_BUFFER_SIZE] = "";
    common::ObCurTraceId::TraceId inner_sql_trace_id;
    ScanMonitorNodeInfo scan_node_info;
    EXTRACT_INT_FIELD_MYSQL(*scan_result, "TASK_ID", scan_node_info.task_id_, int64_t);

    EXTRACT_TIMESTAMP_FIELD_MYSQL_SKIP_RET(*scan_result, "FIRST_CHANGE_TIME", scan_node_info.first_change_time_);
    EXTRACT_TIMESTAMP_FIELD_MYSQL_SKIP_RET(*scan_result, "LAST_CHANGE_TIME", scan_node_info.last_change_time_);
    EXTRACT_TIMESTAMP_FIELD_MYSQL_SKIP_RET(*scan_result, "LAST_REFRESH_TIME", scan_node_info.last_refresh_time_);
    EXTRACT_INT_FIELD_MYSQL(*scan_result, "OUTPUT_ROWS", scan_node_info.output_rows_, int64_t);
    int trace_id_len = 0;
    EXTRACT_STRBUF_FIELD_MYSQL(*scan_result, "TRACE_ID", trace_id_str, OB_MAX_TRACE_ID_BUFFER_SIZE, trace_id_len);
    if (OB_FAIL(ret)) {
      LOG_WARN("failed to extract field from mysql", K(ret));
    } else if (OB_FAIL(inner_sql_trace_id.parse_from_buf(trace_id_str))) {
      LOG_WARN("failed to parse trace id from buf", KR(ret), K(trace_id_str));
    } else if (FALSE_IT(scan_node_info.execution_id_ = inner_sql_trace_id.get_execution_id())) {
    } else if (OB_FAIL(scan_res_.push_back(scan_node_info))) {
      LOG_WARN("failed to push back sort monitor node info", K(ret));
    }
  }
  return ret;
}

int ObSqlMonitorStatsCollector::get_sort_monitor_stats_batch(sqlclient::ObMySQLResult *scan_result)
{
  int ret = OB_SUCCESS;
  if (OB_ISNULL(scan_result)) {
    ret = OB_INVALID_ARGUMENT;
    LOG_WARN("scan result is null", K(ret));
  } else {
    char trace_id_str[OB_MAX_TRACE_ID_BUFFER_SIZE] = "";
    common::ObCurTraceId::TraceId inner_sql_trace_id;
    SortMonitorNodeInfo sort_node_info;
    EXTRACT_INT_FIELD_MYSQL(*scan_result, "TASK_ID", sort_node_info.task_id_, int64_t);

    EXTRACT_INT_FIELD_MYSQL(*scan_result, "THREAD_ID", sort_node_info.thread_id_, int64_t);
    EXTRACT_TIMESTAMP_FIELD_MYSQL_SKIP_RET(*scan_result, "FIRST_CHANGE_TIME", sort_node_info.first_change_time_);
    EXTRACT_TIMESTAMP_FIELD_MYSQL_SKIP_RET(*scan_result, "LAST_CHANGE_TIME", sort_node_info.last_change_time_);
    EXTRACT_INT_FIELD_MYSQL(*scan_result, "OUTPUT_ROWS", sort_node_info.output_rows_, int64_t);
    EXTRACT_INT_FIELD_MYSQL(*scan_result, "OTHERSTAT_1_VALUE", sort_node_info.row_sorted_, int64_t);
    EXTRACT_INT_FIELD_MYSQL(*scan_result, "OTHERSTAT_6_VALUE", sort_node_info.dump_size_, int64_t);
    EXTRACT_INT_FIELD_MYSQL(*scan_result, "OTHERSTAT_7_VALUE", sort_node_info.row_count_, int64_t);
    EXTRACT_INT_FIELD_MYSQL(*scan_result, "OTHERSTAT_7_ID", sort_node_info.row_count_id_, int16_t);
    EXTRACT_INT_FIELD_MYSQL(*scan_result, "OTHERSTAT_8_VALUE", sort_node_info.sort_expected_round_count_, int64_t);
    EXTRACT_INT_FIELD_MYSQL(*scan_result, "OTHERSTAT_9_VALUE", sort_node_info.merge_sort_start_time_, int64_t);
    EXTRACT_INT_FIELD_MYSQL(*scan_result, "OTHERSTAT_10_VALUE", sort_node_info.compress_type_, int64_t);
    int trace_id_len = 0;
    EXTRACT_STRBUF_FIELD_MYSQL(*scan_result, "TRACE_ID", trace_id_str, OB_MAX_TRACE_ID_BUFFER_SIZE, trace_id_len);
    if (OB_FAIL(ret)) {
      LOG_WARN("failed to extract field from mysql", K(ret));
    } else if (OB_FAIL(inner_sql_trace_id.parse_from_buf(trace_id_str))) {
      LOG_WARN("failed to parse trace id from buf", KR(ret), K(trace_id_str));
    } else if (FALSE_IT(sort_node_info.execution_id_ = inner_sql_trace_id.get_execution_id())) {
    } else if (OB_FAIL(sort_res_.push_back(sort_node_info))) {
      LOG_WARN("failed to push back sort monitor node info", K(ret));
    }
  }
  return ret;
}

int ObSqlMonitorStatsCollector::get_insert_monitor_stats_batch(sqlclient::ObMySQLResult *scan_result)
{
  int ret = OB_SUCCESS;
  if (OB_ISNULL(scan_result)) {
    ret = OB_INVALID_ARGUMENT;
    LOG_WARN("scan result is null", K(ret));
  } else {
    char trace_id_str[OB_MAX_TRACE_ID_BUFFER_SIZE] = "";
    common::ObCurTraceId::TraceId inner_sql_trace_id;
    InsertMonitorNodeInfo insert_node_info;
    EXTRACT_INT_FIELD_MYSQL(*scan_result, "TASK_ID", insert_node_info.task_id_, int64_t);

    EXTRACT_INT_FIELD_MYSQL(*scan_result, "THREAD_ID", insert_node_info.thread_id_, int64_t);
    EXTRACT_TIMESTAMP_FIELD_MYSQL_SKIP_RET(*scan_result, "LAST_REFRESH_TIME", insert_node_info.last_refresh_time_);
    EXTRACT_INT_FIELD_MYSQL(*scan_result, "OTHERSTAT_2_VALUE", insert_node_info.sstable_row_inserted_, int64_t);
    EXTRACT_INT_FIELD_MYSQL(*scan_result, "OTHERSTAT_8_VALUE", insert_node_info.vec_task_thread_pool_cnt_, int64_t);
    EXTRACT_INT_FIELD_MYSQL(*scan_result, "OTHERSTAT_9_VALUE", insert_node_info.vec_task_total_cnt_, int64_t);
    EXTRACT_INT_FIELD_MYSQL(*scan_result, "OTHERSTAT_10_VALUE", insert_node_info.vec_task_finish_cnt_, int64_t);
    int trace_id_len = 0;
    EXTRACT_STRBUF_FIELD_MYSQL(*scan_result, "TRACE_ID", trace_id_str, OB_MAX_TRACE_ID_BUFFER_SIZE, trace_id_len);
    if (OB_FAIL(ret)) {
      LOG_WARN("failed to extract field from mysql", K(ret));
    } else if (OB_FAIL(inner_sql_trace_id.parse_from_buf(trace_id_str))) {
      LOG_WARN("failed to parse trace id from buf", KR(ret), K(trace_id_str));
    } else if (FALSE_IT(insert_node_info.execution_id_ = inner_sql_trace_id.get_execution_id())) {
    } else if (OB_FAIL(insert_res_.push_back(insert_node_info))) {
      LOG_WARN("failed to push back sort monitor node info", K(ret));
    }
  }
  return ret;
}

int ObSqlMonitorStatsCollector::get_next_sql_plan_monitor_stat(ObSqlMonitorStats &sql_monitor_stats)
{
  int ret = OB_SUCCESS;

  task_id_ = sql_monitor_stats.task_id_;
  ddl_type_ = sql_monitor_stats.ddl_type_;
  if (!is_inited_) {
    ret = OB_NOT_INIT;
    LOG_WARN("not init", K(ret));
  } else if (OB_UNLIKELY(false || task_id_ <= 0)) {
    ret = OB_INVALID_ARGUMENT;
    LOG_WARN("invalid argument", K(ret), K(task_id_));
  } else if (OB_FAIL(get_next_scanned_stats(sql_monitor_stats))) {
    LOG_WARN("get next scanned stats failed", K(ret));
  } else if (!sql_monitor_stats.is_empty_ && OB_FAIL(get_next_sorted_stats(sql_monitor_stats))) {
    LOG_WARN("get next sorted stats failed", K(ret));
  } else if (!sql_monitor_stats.is_empty_ && OB_FAIL(get_next_inserted_stats(sql_monitor_stats))) {
    LOG_WARN("get next inserted stats failed", K(ret));
  }
  return ret;
}

int ObSqlMonitorStatsCollector::get_next_scanned_stats(ObSqlMonitorStats &sql_monitor_stats)
{
  int ret = OB_SUCCESS;
  if (!is_inited_) {
    ret = OB_NOT_INIT;
    LOG_WARN("not init", K(ret));
  }
  for (; OB_SUCC(ret) && scan_index_id_ < scan_res_.count(); scan_index_id_++) {
    const ScanMonitorNodeInfo &scan_monitor_node = scan_res_.at(scan_index_id_);

    const int64_t task_id = scan_monitor_node.task_id_;
    const int64_t execution_id = scan_monitor_node.execution_id_;
    if (next_ddl_monitor_node(task_id)) {
      break;
    } else if (previous_ddl_monitor_node(task_id)) {
    } else if (outdated_monitor_node(execution_id)) {
    } else if (OB_FAIL(sql_monitor_stats.clean_invalid_data(execution_id))) {
      LOG_WARN("failed to clean invalid data", K(ret), K(execution_id));
    } else if (scan_monitor_node.output_rows_ == 0) {
    } else if (OB_FAIL(sql_monitor_stats.scan_node_.push_back(scan_monitor_node))) {
      LOG_WARN("failed to push back scan node", K(ret));
    } else {
      execution_id_ = sql_monitor_stats.execution_id_;
      sql_monitor_stats.is_empty_ = false;
    }
  }
  return ret;
}

int ObSqlMonitorStatsCollector::get_next_sorted_stats(ObSqlMonitorStats &sql_monitor_stats)
{
  int ret = OB_SUCCESS;
  if (!is_inited_) {
    ret = OB_NOT_INIT;
    LOG_WARN("not init", K(ret));
  }
  for (; OB_SUCC(ret) && sort_index_id_ < sort_res_.count(); sort_index_id_++) {
    const SortMonitorNodeInfo &sort_monitor_node = sort_res_.at(sort_index_id_);

    const int64_t task_id = sort_monitor_node.task_id_;
    const int64_t execution_id = sort_monitor_node.execution_id_;
    if (next_ddl_monitor_node(task_id)) {
      break;
    } else if (previous_ddl_monitor_node(task_id)) {
    } else if (outdated_monitor_node(execution_id)) {
    } else if (OB_FAIL(sql_monitor_stats.sort_node_.push_back(sort_monitor_node))) {
      LOG_WARN("failed to push back sort node", K(ret));
    }
  }
  return ret;
}

int ObSqlMonitorStatsCollector::get_next_inserted_stats(ObSqlMonitorStats &sql_monitor_stats)
{
  int ret = OB_SUCCESS;
  if (!is_inited_) {
    ret = OB_NOT_INIT;
    LOG_WARN("not init", K(ret));
  }
  for (; OB_SUCC(ret) && insert_index_id_ < insert_res_.count(); insert_index_id_++) {
    const InsertMonitorNodeInfo &insert_monitor_node = insert_res_.at(insert_index_id_);

    const int64_t task_id = insert_monitor_node.task_id_;
    const int64_t execution_id = insert_monitor_node.execution_id_;
    if (next_ddl_monitor_node(task_id)) {
      break;
    } else if (previous_ddl_monitor_node(task_id)) {
    } else if (outdated_monitor_node(execution_id)) {
    } else if (OB_FAIL(sql_monitor_stats.insert_node_.push_back(insert_monitor_node))) {
      LOG_WARN("failed to push back insert node", K(ret));
    }
  }
  return ret;
}

int ObDDLDiagnoseInfo::init(const int64_t task_id, const ObDDLType ddl_type, const int64_t execution_id)
{
  int ret = OB_SUCCESS;
  if (false || task_id <= 0 || ddl_type == DDL_INVALID) {
    ret = OB_INVALID_ARGUMENT;
    LOG_WARN("invalid argument", K(ret), K(task_id_), K(ddl_type));
  } else {
    task_id_ = task_id;
    ddl_type_ = ddl_type;
    finish_ddl_ = execution_id < -1 ? true : false;
    is_inited_ = true;
  }
  return ret;
}

int ObDDLDiagnoseInfo::diagnose(const ObSqlMonitorStats &sql_monitor_stats)
{
  int ret = OB_SUCCESS;
  if (!is_inited_) {
    ret = OB_NOT_INIT;
    LOG_WARN("not init", K(ret));
  } else if (OB_FAIL(calculate_sql_plan_monitor_node_info(sql_monitor_stats))) {
    LOG_INFO("failed to calculate sql plan monitor node info", K(ret));
  } else if (is_skip_case()) {
    ret = OB_EMPTY_RESULT;
  } else if (ddl_type_ == ObDDLType::DDL_CREATE_PARTITIONED_LOCAL_INDEX && execution_id_ > 1) {
    if (OB_FAIL(local_index_diagnose())) {
      LOG_WARN("failed to diagnose local index", K(ret));
    }
  } else if (finish_ddl_) {
    if (OB_FAIL(finish_ddl_diagnose())) {
      LOG_WARN("failed to diagnose finish ddl", K(ret));
    }
  } else if (is_empty_) { // before scan
  } else if (OB_FAIL(running_ddl_diagnose())) {
    LOG_WARN("failed to diagnose running ddl", K(ret));
  }
  return ret;
}

int ObDDLDiagnoseInfo::process_sql_monitor_and_generate_longops_message(const ObSqlMonitorStats &sql_monitor_stats, ObDDLTaskStatInfo &stat_info, int64_t &pos)
{
  int ret = OB_SUCCESS;
  if (!is_inited_) {
    ret = OB_NOT_INIT;
    LOG_WARN("not init", K(ret));
  } else if (OB_FAIL(calculate_sql_plan_monitor_node_info(sql_monitor_stats))) {
    LOG_INFO("failed to calculate sql plan monitor node info", K(ret));
  } else if (OB_FAIL(diagnose_stats_analysis())) {
    LOG_WARN("failed to diagnose stats analysis ", K(ret));
  } else if (OB_FAIL(generate_session_longops_message(stat_info, pos))) {
    LOG_WARN("failed to generate session longops message", K(ret), K(stat_info), K(pos));
  }
  return ret;
}

int ObDDLDiagnoseInfo::calculate_sql_plan_monitor_node_info(const ObSqlMonitorStats &sql_monitor_stats)
{
  int ret = OB_SUCCESS;
  if (FALSE_IT(execution_id_ = sql_monitor_stats.execution_id_)) {
  } else if (sql_monitor_stats.is_empty_) {
  } else if (OB_FAIL(calculate_scan_monitor_node_info(sql_monitor_stats))) {
    LOG_WARN("failed to calculate scan monitor node info", K(ret));
  } else if (OB_FAIL(calculate_sort_and_insert_info(sql_monitor_stats))) {
    LOG_WARN("failed to calculate sort and insert info", K(ret));
  } else {
    is_empty_ = false;
  }
  return ret;
}

int ObDDLDiagnoseInfo::calculate_scan_monitor_node_info(const ObSqlMonitorStats &sql_monitor_stats)
{
  int ret = OB_SUCCESS;
  for (int64_t i = 0; OB_SUCC(ret) && i < sql_monitor_stats.scan_node_.count(); ++i) {
    const ScanMonitorNodeInfo &scan_monitor_node = sql_monitor_stats.scan_node_.at(i);
    row_scanned_ += scan_monitor_node.output_rows_;
    max_row_scan_ = OB_MAX(max_row_scan_, scan_monitor_node.output_rows_);
    min_row_scan_ = OB_MIN(min_row_scan_, scan_monitor_node.output_rows_);
    scan_start_time_ = OB_MAX(scan_start_time_, scan_monitor_node.first_change_time_);
    scan_end_time_ = OB_MAX(scan_end_time_, scan_monitor_node.last_change_time_);
    if (scan_monitor_node.last_refresh_time_ == 0) {
      scan_thread_num_++;
    }
  }
  return ret;
}

// moved definition to sql/resolver/ddl/ob_ddl_resolver.cpp(transitional state)

int ObDDLDiagnoseInfo::calculate_inmem_sort_info(
    const int64_t row_sorted,
    const int64_t row_count,
    const int64_t first_change_time,
    const int64_t thread_id)
{
  int ret = OB_SUCCESS;
  if (0 == row_sorted || 0 == row_count) {
  } else if (row_sorted <= row_count) {
    row_sorted_ += row_sorted;
    if (0 == first_change_time) {
      double inmem_sort_progress_tmp = static_cast<double>(row_sorted) / row_count;
      if (inmem_sort_progress_tmp > 0) {
        int64_t spend_time = ObTimeUtility::fast_current_time() - scan_end_time_;
        double inmem_sort_remain_time = spend_time / inmem_sort_progress_tmp - spend_time;
        inmem_sort_thread_num_++;
        inmem_sort_remain_time_ = OB_MAX(inmem_sort_remain_time_, inmem_sort_remain_time);
        if (inmem_sort_progress_tmp <= inmem_sort_progress_) {
          inmem_sort_spend_time_ = spend_time;
          inmem_sort_slowest_thread_id_ = thread_id;
          min_inmem_sort_row_ = row_sorted;
          inmem_sort_progress_ = inmem_sort_progress_tmp;
        }
      }
    }
  }
  return ret;
}

int ObDDLDiagnoseInfo::calculate_merge_sort_info(
    const int64_t row_count,
    const int64_t row_sorted,
    const SortMonitorNodeInfo &sort_monitor_node)
{
  int ret = OB_SUCCESS;
  dump_size_ += sort_monitor_node.dump_size_;
  compress_type_ = sort_monitor_node.compress_type_;
  if (row_sorted > row_count && row_count > 0) {
    int64_t real_merge_count = row_sorted - row_count;
    row_sorted_ += row_count;
    row_merge_sorted_ += real_merge_count;
    int64_t expected_round_tmp = sort_monitor_node.sort_expected_round_count_;
    if (expected_round_tmp > 0 && sort_monitor_node.first_change_time_ == 0) { // first_change_time_ > 0 means sort phase has finished
      double merge_sort_progress_tmp = static_cast<double>(real_merge_count) / (row_count * expected_round_tmp);
      int64_t spend_time = ObTimeUtility::fast_current_time() - sort_monitor_node.merge_sort_start_time_;
      if (merge_sort_progress_tmp > 0) {
        double merge_sort_remain_time = spend_time / merge_sort_progress_tmp - spend_time;
        merge_sort_thread_num_++;
        merge_sort_remain_time_ = OB_MAX(merge_sort_remain_time_, merge_sort_remain_time);
        if (merge_sort_progress_tmp <= merge_sort_progress_) {
          merge_sort_spend_time_ = spend_time;
          merge_sort_slowest_thread_id_ = sort_monitor_node.thread_id_;
          min_merge_sort_row_ = real_merge_count;
          merge_sort_progress_ = merge_sort_progress_tmp;
        }
      }
    }
  }
  return ret;
}

int ObDDLDiagnoseInfo::calculate_insert_info(
    const int64_t row_count,
    const SortMonitorNodeInfo &sort_info,
    const ObSqlMonitorStats &sql_monitor_stats)
{
  int ret = OB_SUCCESS;
  int64_t thread_id = sort_info.thread_id_;
  int64_t change_time = sort_info.first_change_time_;
  if (row_count > row_max_) {
    row_max_ = row_count;
    row_max_thread_ = thread_id;
  }

  if (row_min_ == 0 || row_count < row_min_) {
    row_min_ = row_count;
    row_min_thread_ = thread_id;
  }
  if (change_time > 0) {
    sort_end_time_ = OB_MAX(sort_end_time_, change_time);
    while (OB_SUCC(ret) && thread_index_ < sql_monitor_stats.insert_node_.count()) {
      const InsertMonitorNodeInfo &insert_monitor_node = sql_monitor_stats.insert_node_.at(thread_index_);
      uint64_t thread_id_tmp = insert_monitor_node.thread_id_;
      if (thread_id_tmp < thread_id || (thread_id_tmp == thread_id && insert_monitor_node.execution_id_ < sort_info.execution_id_)) {
      } else if (thread_id_tmp > thread_id || insert_monitor_node.execution_id_ > sort_info.execution_id_ ) {
        break;
      } else {
        int64_t row_inserted_file_tmp = insert_monitor_node.sstable_row_inserted_;
        row_inserted_file_ += row_inserted_file_tmp;
        int64_t finish_time_tmp = insert_monitor_node.last_refresh_time_;
        if (finish_time_tmp > insert_end_time_) {
          insert_end_time_ = finish_time_tmp;
          slowest_thread_id_ = thread_id_tmp;
        }
        if (0 == row_inserted_file_tmp || 0 == row_count) {
        } else if (row_inserted_file_tmp < row_count) {
          double insert_progress_tmp = static_cast<double>(row_inserted_file_tmp) / row_count;
          int64_t spend_time = ObTimeUtility::fast_current_time() - change_time;
          if (insert_progress_tmp > 0) {
            double remain_time = spend_time / insert_progress_tmp - spend_time;
            insert_thread_num_++;
            insert_remain_time_ = OB_MAX(insert_remain_time_, remain_time);
            if (insert_progress_tmp <= insert_progress_) {
              insert_spend_time_ = spend_time;
              insert_slowest_thread_id_ = thread_id_tmp;
              min_insert_row_ = row_inserted_file_tmp;
              insert_progress_ = insert_progress_tmp;
            }
          }
        }
        vec_task_thread_pool_cnt_ = OB_MAX(vec_task_thread_pool_cnt_, insert_monitor_node.vec_task_thread_pool_cnt_);
        if (vec_task_thread_pool_cnt_ > 0 && OB_FAIL(calculate_vec_task_info(insert_monitor_node))) {
          LOG_WARN("failed to calculate vec task info", K(ret));
        }
      }
     thread_index_++;
    }
  }
  return ret;
}

int ObDDLDiagnoseInfo::calculate_vec_task_info(const InsertMonitorNodeInfo &insert_monitor_node)
{
  int ret = OB_SUCCESS;
  int64_t vec_index_task_total_cnt_tmp = insert_monitor_node.vec_task_total_cnt_;
  int64_t vec_index_task_finish_cnt_tmp = insert_monitor_node.vec_task_finish_cnt_;
  vec_task_total_cnt_ += vec_index_task_total_cnt_tmp;
  vec_task_finish_cnt_ += vec_index_task_finish_cnt_tmp;
  if (vec_index_task_total_cnt_tmp == 0 || vec_index_task_finish_cnt_tmp == 0) {
  } else if (vec_index_task_finish_cnt_tmp <= vec_index_task_total_cnt_tmp) {
    vec_task_trigger_cnt_++;
    double vec_task_progress_tmp = static_cast<double>(vec_index_task_finish_cnt_tmp) / vec_index_task_total_cnt_tmp;
    if (vec_task_progress_tmp <= vec_task_progress_) {
      vec_task_progress_ = vec_task_progress_tmp;
    }
  }
  return ret;
}

int ObDDLDiagnoseInfo::local_index_diagnose()
{
  int ret = OB_SUCCESS;
  if (OB_FAIL(databuff_printf(diagnose_message_, common::OB_DIAGNOSE_INFO_LENGTH, pos_,
                              "build local index batch num: %ld, "
                              "THREAD_INFO: { parallel_num : %ld, row_max: %ld, row_max_thread_id: %ld, row_min: %ld, row_min_thread_id: %ld }",
                              execution_id_, parallelism_, row_max_, row_max_thread_, row_min_, row_min_thread_))) {
    LOG_WARN("failed to print message", K(ret), K(diagnose_message_), K(pos_));
  } else if (is_thread_without_data()
             && OB_FAIL(databuff_printf(diagnose_message_, common::OB_DIAGNOSE_INFO_LENGTH, pos_,
                                        ", DIAGNOSE_CASE:{ The number of threads with data is less than the dop. real_parallelism: %ld }",
                                        real_parallelism_))) {
    LOG_WARN("failed to print diagnose message", K(diagnose_message_), K(pos_), K(ret));
  }
  return ret;
}

int ObDDLDiagnoseInfo::finish_ddl_diagnose()
{
  int ret = OB_SUCCESS;
  double scan_time = OB_MAX(0.0, static_cast<double>(scan_end_time_ - scan_start_time_) / (1000 * 1000));
  double sort_time = OB_MAX(0.0, static_cast<double>(sort_end_time_ - scan_end_time_) / (1000 * 1000));
  double insert_time = OB_MAX(0.0, static_cast<double>(insert_end_time_ - sort_end_time_) / (1000 * 1000));
  if (execution_id_ > 1 && OB_FAIL(databuff_printf(diagnose_message_, common::OB_DIAGNOSE_INFO_LENGTH, pos_, "try count: %ld, ", execution_id_))) {
    LOG_WARN("failed to print ddl try count message", K(ret));
  } else if (OB_FAIL(databuff_printf(diagnose_message_, common::OB_DIAGNOSE_INFO_LENGTH, pos_,
                                     "THREAD_INFO: { parallel_num : %ld, row_max: %ld, row_max_thread_id: %ld, row_min: %ld, row_min_thread_id: %ld slowest_thread_id: %ld }, "
                                     "TIME_INFO: { scan_time: %.3fs, sort_time: %.3fs, insert_time: %.3fs }",
                                     parallelism_, row_max_, row_max_thread_, row_min_, row_min_thread_, slowest_thread_id_,
                                     scan_time, sort_time, insert_time))) {
    LOG_WARN("failed to print message", K(ret));
  } else if (OB_FAIL(check_diagnose_case())) {
    LOG_WARN("failed to check diagnose case", K(ret));
  }
  return ret;
}

int ObDDLDiagnoseInfo::running_ddl_diagnose()
{
  int ret = OB_SUCCESS;
  if (execution_id_ > 1 && OB_FAIL(databuff_printf(diagnose_message_, common::OB_DIAGNOSE_INFO_LENGTH, pos_, "try count: %ld, ", execution_id_))) {
    LOG_WARN("failed to print ddl try count message", K(ret));
  } else if (real_parallelism_ == 0) {
    if (OB_FAIL(databuff_printf(diagnose_message_, common::OB_DIAGNOSE_INFO_LENGTH, pos_, "Scanning"))) {
      LOG_WARN("failed to print message", K(ret));
    }
  } else if (OB_FAIL(databuff_printf(diagnose_message_, common::OB_DIAGNOSE_INFO_LENGTH, pos_,
                                     "THREAD_INFO: { parallel_num : %ld, row_max: %ld, row_max_thread_id: %ld, row_min: %ld, row_min_thread_id: %ld }",
                                     parallelism_, row_max_, row_max_thread_, row_min_, row_min_thread_))) {
    LOG_WARN("failed to print thread info message", K(ret));
  } else if (OB_FAIL(check_diagnose_case())) {
    LOG_WARN("failed to check diagnose case", K(ret));
  }
  return ret;
}

int ObDDLDiagnoseInfo::check_diagnose_case()
{
  int ret = OB_SUCCESS;
  if (is_data_skew() || is_thread_without_data()) {
    if (OB_FAIL(databuff_printf(diagnose_message_, common::OB_DIAGNOSE_INFO_LENGTH, pos_, ", DIAGNOSE_CASE: {"))) {
      LOG_WARN("failed to print diagnose message", K(ret));
    } else if (OB_SUCC(ret)
              && is_data_skew()
              && OB_FAIL(databuff_printf(diagnose_message_, common::OB_DIAGNOSE_INFO_LENGTH, pos_,
                                        " The data skew is significant, with a low sampling rate or uneven sampling."))) {
      LOG_WARN("failed to print diagnose message", K(ret));
    } else if (OB_SUCC(ret)
              && is_thread_without_data()
              && OB_FAIL(databuff_printf(diagnose_message_, common::OB_DIAGNOSE_INFO_LENGTH, pos_,
                                          " The number of threads with data is less than the dop. real_parallelism: %ld.",
                                          real_parallelism_))) {
      LOG_WARN("failed to print diagnose message", K(ret));
    }  else if (OB_SUCC(ret)
              && OB_FAIL(databuff_printf(diagnose_message_, common::OB_DIAGNOSE_INFO_LENGTH, pos_,
                                          " }"))) {
      LOG_WARN("failed to print diagnose message", K(ret));
    }
  }
  return ret;
}

int ObDDLDiagnoseInfo::diagnose_stats_analysis()
{
  int ret = OB_SUCCESS;
  if (row_scanned_ == 0) {
    state_ = RedefinitionState::BEFORESCAN;
  } else if (scan_thread_num_ > 0 || row_sorted_ == 0) {
    parallelism_ = scan_thread_num_;
    state_ = RedefinitionState::SCAN;
    scan_spend_time_ = ObTimeUtility::fast_current_time() - scan_start_time_;
  } else {
    parallelism_ = inmem_sort_thread_num_ + merge_sort_thread_num_ + insert_thread_num_;
    if (inmem_sort_thread_num_ > 0) {
      state_ = RedefinitionState::INMEM_SORT;
    } else if (merge_sort_thread_num_ > 0) {
      state_ = RedefinitionState::MERGE_SORT;
    } else if (insert_thread_num_ > 0){
      state_ = RedefinitionState::INSERT;
    } else {
      state_ = RedefinitionState::DDL_DIAGNOSE_V1;
    }
  }
  return ret;
}

ObDDLTaskStatInfo::ObDDLTaskStatInfo()
  : start_time_(0), finish_time_(0), time_remaining_(0), percentage_(0), op_name_(), target_(), message_()
{
}

int ObDDLTaskStatInfo::init(const char *&ddl_type_str, const uint64_t table_id)
{
  int ret = OB_SUCCESS;
  MEMSET(op_name_, 0, common::MAX_LONG_OPS_NAME_LENGTH);
  MEMSET(target_, 0, common::MAX_LONG_OPS_TARGET_LENGTH);
  if (OB_FAIL(databuff_printf(op_name_, common::MAX_LONG_OPS_NAME_LENGTH, "%s", ddl_type_str))) {
    LOG_WARN("failed to print ddl type str", K(ret));
  } else if (OB_FAIL(databuff_printf(target_, common::MAX_LONG_OPS_TARGET_LENGTH, "%lu", table_id))) {
    LOG_WARN("failed to print ddl table name", K(ret), K(table_id));
  } else {
    start_time_ = ObTimeUtility::current_time();
  }
  return ret;
}

int ObDDLDiagnoseInfo::generate_session_longops_message(ObDDLTaskStatInfo &stat_info, int64_t &pos)
{
  int ret = OB_SUCCESS;
  if (ddl_type_ == share::ObDDLType::DDL_CREATE_PARTITIONED_LOCAL_INDEX
      && execution_id_ > 1
      && OB_FAIL(databuff_printf(stat_info.message_,
                                 MAX_LONG_OPS_MESSAGE_LENGTH,
                                 pos,
                                 "build local index batch num: %ld, ",
                                 execution_id_))) {
    LOG_WARN("failed to print", K(ret));
  } else if (state_ == RedefinitionState::DDL_DIAGNOSE_V1) {
    if (OB_FAIL(generate_session_longops_message_v1(stat_info, pos))) {
      LOG_WARN("failed to print", K(ret));
    }
  } else {
    switch (state_) {
      case RedefinitionState::BEFORESCAN: {
        if (OB_FAIL(databuff_printf(stat_info.message_,
                                    MAX_LONG_OPS_MESSAGE_LENGTH,
                                    pos,
                                    "STATUS: REPLICA BUILD, BEFORE-SCAN"))) {
          LOG_WARN("failed to print", K(ret));
        }
        break;
      }

      case RedefinitionState::SCAN: {
        if (OB_FAIL(databuff_printf(stat_info.message_,
                                    MAX_LONG_OPS_MESSAGE_LENGTH,
                                    pos,
                                    "STATUS: REPLICA BUILD, SCANNING, PARALLELISM: %ld, "
                                    "ROW_COUNT_INFO:{ ROW_SCANNED: %ld, ROW_SORTED: %ld, ROW_INSERTED: %ld }, "
                                    "SCAN_INFO:{ SCAN_TIME_ELAPSED: %.3fs, MAX_THREAD_ROW_SCANNED: %ld, MIN_THREAD_ROW_SCANNED: %ld }",
                                    parallelism_,
                                    row_scanned_, row_sorted_ + row_merge_sorted_, row_inserted_file_,
                                    scan_spend_time_ / (1000 * 1000), max_row_scan_, min_row_scan_))) {
          LOG_WARN("failed to print", K(ret));
        }
        break;
      }

      case RedefinitionState::INMEM_SORT: {
        if (OB_FAIL(databuff_printf(stat_info.message_,
                                    MAX_LONG_OPS_MESSAGE_LENGTH,
                                    pos,
                                    "STATUS: REPLICA BUILD, SORT_PHASE1, PARALLELISM: %ld, SORT_PHASE1_THREAD_NUM: %ld, "
                                    "ROW_COUNT_INFO:{ ROW_SCANNED: %ld, ROW_SORTED: %ld, ROW_INSERTED: %ld }, "
                                    "SORT_PHASE1_PROGRESS_INFO:{ SORT_PHASE1_TIME_ELAPSED: %.3fs, SORT_PHASE1_PROGRESS: %.2f%%, SORT_PHASE1_TIME_REMAINING: %.3fs }, "
                                    "SLOWEST_THREAD_INFO:{ THREAD_ID: %ld, SORTED_ROW_COUNT: %ld }",
                                    parallelism_, inmem_sort_thread_num_,
                                    row_scanned_, row_sorted_ + row_merge_sorted_, row_inserted_file_,
                                    inmem_sort_spend_time_ / (1000 * 1000), inmem_sort_progress_ * 100, inmem_sort_remain_time_ / (1000 * 1000),
                                    inmem_sort_slowest_thread_id_, min_inmem_sort_row_))) {
          LOG_WARN("failed to print", K(ret));
        }
        break;
      }
      case RedefinitionState::MERGE_SORT: {
        if (OB_FAIL(databuff_printf(stat_info.message_,
                                    MAX_LONG_OPS_MESSAGE_LENGTH,
                                    pos,
                                    "STATUS: REPLICA BUILD, SORT_PHASE2, PARALLELISM: %ld, SORT_PHASE2_THREAD_NUM: %ld, "
                                    "ROW_COUNT_INFO:{ ROW_SCANNED: %ld, ROW_SORTED: %ld, ROW_INSERTED: %ld }, "
                                    "SORT_PHASE2_PROGRESS_INFO:{ SORT_PHASE2_TIME_ELAPSED: %.3fs, SORT_PHASE2_PROGRESS: %.2f%%, SORT_PHASE2_TIME_REMAINING: %.3fs }, "
                                    "SLOWEST_THREAD_INFO:{ THREAD_ID: %ld, SORTRD_ROW_COUNT: %ld }, "
                                    "TEMP_FILE_INFO:{ DUMP_SIZE: %ld, COMPRESS_TYPE: %s }",
                                    parallelism_, merge_sort_thread_num_,
                                    row_scanned_, row_sorted_ + row_merge_sorted_, row_inserted_file_,
                                    merge_sort_spend_time_ / (1000 * 1000), merge_sort_progress_ * 100, merge_sort_remain_time_/ (1000 * 1000),
                                    merge_sort_slowest_thread_id_, min_merge_sort_row_,
                                    dump_size_, all_compressor_name[compress_type_]))) {
          LOG_WARN("failed to print", K(ret));
        }
        break;
      }
      case RedefinitionState::INSERT: {
        if (OB_FAIL(databuff_printf(stat_info.message_,
                                    MAX_LONG_OPS_MESSAGE_LENGTH,
                                    pos,
                                    "STATUS: REPLICA BUILD, INSERT, PARALLELISM: %ld, INSERT_THREAD: %ld, "
                                    "ROW_COUNT_INFO:{ ROW_SCANNED: %ld, ROW_SORTED: %ld, ROW_INSERTED: %ld }, "
                                    "INSERT_PROGRESS_INFO:{ INSERT_TIME_ELAPSED: %.3fs, INSERT_PROGRESS: %.2f%%, INSERT_TIME_REMAINING: %.3fs }, "
                                    "SLOWEST_THREAD_INFO:{ THREAD_ID: %ld, INSERTED_ROW_COUNT: %ld }",
                                    parallelism_, insert_thread_num_,
                                    row_scanned_, row_sorted_ + row_merge_sorted_, row_inserted_file_,
                                    insert_spend_time_ / (1000 * 1000), insert_progress_ * 100, insert_remain_time_ / (1000 * 1000),
                                    insert_slowest_thread_id_, min_insert_row_))) {
          LOG_WARN("failed to print", K(ret));
        } else if (vec_task_trigger_cnt_ > 0) {
          if (OB_FAIL(databuff_printf(
                                    stat_info.message_, MAX_LONG_OPS_MESSAGE_LENGTH, pos,
                                    ", VEC_TASK:{ TRIGGER_CNT: %ld, POOL_CNT: %ld, TOTAL: %ld, FINISH: %ld }",
                                    vec_task_trigger_cnt_, vec_task_thread_pool_cnt_, vec_task_total_cnt_, vec_task_finish_cnt_))) {
            LOG_WARN("failed to print", K(ret));
          }
        }
        break;
      }
      default: {
        ret = OB_ERR_UNEXPECTED;
        LOG_WARN("not expected status", K(ret), K(state_), K(*this));
        break;
      }
    }
  }
  return ret;
}

int ObDDLDiagnoseInfo::generate_session_longops_message_v1(ObDDLTaskStatInfo &stat_info, int64_t &pos)
{
  int ret = OB_SUCCESS;
  if (OB_FAIL(databuff_printf(stat_info.message_,
                              MAX_LONG_OPS_MESSAGE_LENGTH,
                              pos,
                              "STATUS: REPLICA BUILD, PARALLELISM: %ld, ROW_SCANNED: %ld, ROW_SORTED: %ld, ROW_INSERTED: %ld",
                              ObDDLUtil::get_real_parallelism(parallelism_),
                              row_scanned_,
                              row_sorted_ + row_merge_sorted_,
                              row_inserted_file_))) {
    LOG_WARN("failed to print", K(ret));
  } else if (vec_task_trigger_cnt_ > 0) {
    if (OB_FAIL(databuff_printf(stat_info.message_, MAX_LONG_OPS_MESSAGE_LENGTH, pos,
                                ", VEC_TASK:{ TRIGGER_CNT: %ld, POOL_CNT: %ld, TOTAL: %ld, FINISH: %ld }",
                                vec_task_trigger_cnt_, vec_task_thread_pool_cnt_, vec_task_total_cnt_, vec_task_finish_cnt_))) {
      LOG_WARN("failed to print", K(ret));
    }
  }
  return ret;
}

/******************           ObCheckTabletDataComplementOp         *************/

int ObCheckTabletDataComplementOp::check_task_inner_sql_session_status(
    const common::ObAddr &inner_sql_exec_addr,
    const common::ObCurTraceId::TraceId &trace_id,
    const int64_t task_id,
    const int64_t scn,
    bool &is_old_task_session_exist)
{
  int ret = OB_SUCCESS;
  is_old_task_session_exist = false;
  if (OB_ISNULL(GCTX.sql_proxy_)) {
    ret = OB_INVALID_ARGUMENT;
    LOG_WARN("invalid argument", KR(ret), KP(GCTX.sql_proxy_));
  } else if (OB_UNLIKELY(trace_id.is_invalid())) {
    ret = OB_INVALID_ARGUMENT;
    LOG_WARN("invalid argument", K(ret), K(trace_id), K(inner_sql_exec_addr));
  } else {
    ret = OB_SUCCESS;
    common::ObMySQLProxy &proxy = *GCTX.sql_proxy_;
    ObSqlString sql_string;
    SMART_VAR(ObMySQLProxy::MySQLResult, res) {
      sqlclient::ObMySQLResult *result = NULL;
      char trace_id_str[64] = { 0 };
      char charater = '%';
      const char *trace_id_like = nullptr;
      if (OB_UNLIKELY(0 > trace_id.to_string(trace_id_str, sizeof(trace_id_str)))) {
        ret = OB_ERR_UNEXPECTED;
        LOG_WARN("get trace id string failed", K(ret), K(trace_id));
      } else if (OB_ISNULL(trace_id_like = ObString(trace_id_str).find('-'))) {
        ret = OB_ERR_UNEXPECTED;
        LOG_WARN("get trace id string failed", K(ret), K(trace_id_str));
      } else if (!inner_sql_exec_addr.is_valid()) {
        if (OB_FAIL(sql_string.assign_fmt(" SELECT id as session_id FROM %s WHERE trace_id like \"%c%s\" "
              " and info like \"%cINSERT%c('ddl_task_id', %ld)%cINTO%cSELECT%c%ld%c\" ",
            OB_ALL_VIRTUAL_SESSION_INFO_TNAME,
            charater,
            trace_id_like,
            charater,
            charater,
            task_id,
            charater,
            charater,
            charater,
            scn,
            charater ))) {
          LOG_WARN("assign sql string failed", K(ret));
        }
      } else {
        // vtable is local, query will be routed to inner_sql_exec_addr via proxy.read()
        if (OB_FAIL(sql_string.assign_fmt(" SELECT id as session_id FROM %s WHERE trace_id like \"%c%s\" "
              " and info like \"%cINSERT%c('ddl_task_id', %ld)%cINTO%cSELECT%c%ld%c\" ",
            OB_ALL_VIRTUAL_SESSION_INFO_TNAME,
            charater,
            trace_id_like,
            charater,
            charater,
            task_id,
            charater,
            charater,
            charater,
            scn,
            charater ))) {
          LOG_WARN("assign sql string failed", K(ret));
        }
      }
      if (REACH_TIME_INTERVAL(10L * 1000L * 1000L)) { // every 10s
        LOG_INFO("check task inner sql string", K(sql_string));
      }

      if (OB_FAIL(ret)) {
      } else if (OB_FAIL(proxy.read(res, sql_string.ptr(), &inner_sql_exec_addr))) {
        LOG_WARN("query ddl task record failed", K(ret), K(sql_string));
      } else if (OB_ISNULL((result = res.get_result()))) {
        ret = OB_ERR_UNEXPECTED;
        LOG_WARN("fail to get sql result", K(ret), KP(result));
      } else {
        uint64_t session_id = 0;
        while (OB_SUCC(ret)) {
          if (OB_FAIL(result->next())) {
            if (OB_ITER_END == ret) {
              ret = OB_SUCCESS;
              break;
            } else {
              LOG_WARN("fail to get next row", K(ret));
            }
          } else {
            is_old_task_session_exist =  true;
            EXTRACT_UINT_FIELD_MYSQL(*result, "session_id", session_id, uint64_t);
          }
        }
      }
    }
  }
  return ret;
}

int ObCheckTabletDataComplementOp::update_replica_merge_status(
    const ObTabletID &tablet_id,
    const bool merge_status,
    hash::ObHashMap<ObTabletID, int32_t> &tablets_commited_map)
{
  int ret = OB_SUCCESS;
  if (OB_UNLIKELY(!tablet_id.is_valid())) {
    ret = OB_INVALID_ARGUMENT;
    LOG_WARN("update replica merge status fail.", K(ret));
  } else {
    int32_t commited_count = 0;
    if (OB_SUCC(tablets_commited_map.get_refactored(tablet_id, commited_count))) {
      // overwrite
      if (merge_status) {
        commited_count++;
        if (OB_FAIL(tablets_commited_map.set_refactored(tablet_id, commited_count, true /* overwrite */))) {
          LOG_WARN("fail to insert map status", K(ret));
        }
      }
    } else if (OB_HASH_NOT_EXIST == ret) {  // new insert
      ret = OB_SUCCESS;
      if (merge_status) {
        commited_count = 1;
        if (OB_FAIL(tablets_commited_map.set_refactored(tablet_id, commited_count, true /* overwrite */))) {
          LOG_WARN("fail to insert map status", K(ret));
        }
      }
    } else {
      ret = OB_ERR_UNEXPECTED;
      LOG_WARN("failed to update replica merge status", K(ret));
    }
    LOG_INFO("success to update replica merge status.", K(tablet_id), K(merge_status));
  }
  return ret;
}


int ObCheckTabletDataComplementOp::calculate_build_finish(const common::ObIArray<common::ObTabletID> &tablet_ids,
  hash::ObHashMap<ObTabletID, int32_t> &tablets_commited_map,
  int64_t &build_succ_count)
{
  int ret = OB_SUCCESS;
  build_succ_count = 0;

  if (OB_UNLIKELY(false)) {
    ret = OB_INVALID_ARGUMENT;
    LOG_WARN("fail to check tablets commit status", K(ret));
  } else if (tablets_commited_map.size() <= 0) {
    // do nothing
  } else {
    int commited_count = 0;
    for (int64_t tablet_idx = 0; OB_SUCC(ret) && tablet_idx < tablet_ids.count(); ++tablet_idx) {
      common::ObTabletID tablet_id = tablet_ids.at(tablet_idx);
      if (OB_FAIL(tablets_commited_map.get_refactored(tablet_id, commited_count))){
        LOG_WARN("fail to get tablet commited map, unexpected!", K(ret), K(tablet_id));
      } else if (commited_count < 1) {
        // do nothing
      } else {
        build_succ_count++;
      }

    }
    LOG_INFO("succ check and commit count", K(build_succ_count));
  }
  return ret;
}



int ObCheckTabletDataComplementOp::check_tablet_merge_status(const ObIArray<common::ObTabletID> &tablet_ids,
  const int64_t snapshot_version,
  bool &is_all_tablets_commited)
{
  int ret = OB_SUCCESS;
  is_all_tablets_commited = false;

  hash::ObHashMap<ObTabletID, int32_t> tablets_commited_map;

  const int64_t max_map_hash_bucket = tablet_ids.count();

  if (OB_UNLIKELY( tablet_ids.count() <= 0 || OB_INVALID_TIMESTAMP == snapshot_version)) {
    ret = OB_INVALID_ARGUMENT;
    LOG_WARN("invalid argument", K(ret), K(tablet_ids.count()), K(snapshot_version));
  } else if (OB_FAIL(tablets_commited_map.create(max_map_hash_bucket, "DdlTablet"))){
    LOG_WARN("fail to create tablets_commited_map", K(ret));
  } else {
    const static int64_t batch_size = 100;  // batch tablet number
    int64_t total_build_succ_count = 0;
    int64_t one_batch_build_succ_count = 0;
    ObArray<ObTabletID> batch_tablet_ids;
    for (int64_t i = 0; OB_SUCC(ret) && i < tablet_ids.count(); ++i) {
      const ObTabletID &tablet_id = tablet_ids.at(i);
      if (OB_FAIL(batch_tablet_ids.push_back(tablet_id))) {
        LOG_WARN("fail to push back tablet id", K(ret), K(tablet_id));
      } else if (batch_tablet_ids.count() >= batch_size || i == tablet_ids.count() - 1) {
        if (OB_FAIL(do_check_tablets_merge_status(snapshot_version,
                                                  batch_tablet_ids,
                                                  tablets_commited_map,
                                                  one_batch_build_succ_count))) {
          LOG_WARN("do check tablets merge status fail", K(ret), K(batch_tablet_ids));
        } else {
          total_build_succ_count += one_batch_build_succ_count;
          batch_tablet_ids.reuse();
        }
      }
    }
    int64_t total_tablets_count = tablet_ids.count();
    if (total_build_succ_count == total_tablets_count) {
      is_all_tablets_commited = true;
      LOG_INFO("all tablet finished create sstables", K(ret), K(total_tablets_count), K(total_build_succ_count));
    } else {
      LOG_WARN("not all tablets finished create sstables", K(ret), K(total_tablets_count), K(total_build_succ_count));
    }
  }

  tablets_commited_map.destroy();

  return ret;
}

int ObCheckTabletDataComplementOp::check_tablet_checksum_update_status(const uint64_t index_table_id,
  const uint64_t ddl_task_id,
  const int64_t execution_id,
  const ObIArray<ObTabletID> &tablet_ids,
  bool &is_checksums_all_report)
{
  int ret = OB_SUCCESS;
  is_checksums_all_report = false;
  common::hash::ObHashMap<uint64_t, bool> tablet_checksum_status_map;
  int64_t tablet_count = tablet_ids.count();

  if (OB_UNLIKELY(OB_INVALID_ID == index_table_id ||
      execution_id < 0 || tablet_count <= 0 || ddl_task_id == OB_INVALID_ID)) {
    ret = OB_INVALID_ARGUMENT;
    LOG_WARN("fail to check and wait complement task",
      K(ret), K(index_table_id), K(tablet_ids), K(execution_id), K(ddl_task_id));
  } else if (OB_FAIL(DDL_SIM(ddl_task_id, CHECK_TABLET_CHECKSUM_STATUS_FAILED))) {
    LOG_WARN("ddl sim failure", K(ret), K(ddl_task_id));
  } else if (OB_FAIL(tablet_checksum_status_map.create(tablet_count, ObModIds::OB_SSTABLE_CREATE_INDEX))) {
    LOG_WARN("fail to create column checksum map", K(ret));
  } else if (OB_ISNULL(GCTX.sql_proxy_)) {
    ret = OB_INVALID_ARGUMENT;
    LOG_WARN("invalid argument", KR(ret), KP(GCTX.sql_proxy_));
  } else if (OB_FAIL(ObDDLChecksumOperator::get_tablet_checksum_record(execution_id,
      index_table_id,
      ddl_task_id,
      tablet_ids,
      *GCTX.sql_proxy_,
      tablet_checksum_status_map))) {
    LOG_WARN("fail to get tablet checksum status",
      K(ret), K(execution_id), K(index_table_id), K(ddl_task_id));
  } else {
    int64_t report_checksum_cnt = 0;
    int64_t tablet_idx = 0;
    for (tablet_idx = 0; OB_SUCC(ret) && tablet_idx < tablet_count; ++tablet_idx) {
      const ObTabletID &tablet_id = tablet_ids.at(tablet_idx);
      uint64_t tablet_id_id = tablet_id.id();
      bool status = false;
      if (OB_FAIL(tablet_checksum_status_map.get_refactored(tablet_id_id, status))) {
        LOG_WARN("fail to get tablet checksum record from map", K(ret), K(tablet_id_id));
        if (OB_HASH_NOT_EXIST == ret) {
          ret = OB_SUCCESS;
          break;
        }
      } else if (!status) {
        break;
      } else {
        report_checksum_cnt++;
      }
    }
    if (OB_SUCC(ret)) {
      if (report_checksum_cnt == tablet_count) {
        is_checksums_all_report = true;
      } else {
        is_checksums_all_report = false;
        LOG_INFO("not all tablet has update checksum",
          K(ret), K(tablet_idx), K(tablet_count), K(is_checksums_all_report));
      }
    }
  }
  if (tablet_checksum_status_map.created()) {
    tablet_checksum_status_map.destroy();
  }
  return ret;
}

/*
 * 1. Get tablets for the index table.
 * 2. Check tablet merge status on the single local log stream.
 * 3. Check tablet checksum report status after all SSTables are built.
 */
int ObCheckTabletDataComplementOp::check_all_tablet_sstable_status(const uint64_t index_table_id,
    const int64_t snapshot_version,
    const int64_t execution_id,
    const uint64_t ddl_task_id,
    bool &is_all_sstable_build_finished)
{
  int ret = OB_SUCCESS;
  ObArray<ObTabletID> dest_tablet_ids;
  bool is_checksums_all_report = false;
  is_all_sstable_build_finished = false;

  if (OB_UNLIKELY(OB_INVALID_ID == index_table_id || OB_INVALID_TIMESTAMP == snapshot_version ||
      ddl_task_id == OB_INVALID_ID || execution_id < 0)) {
    ret = OB_INVALID_ARGUMENT;
    LOG_WARN("fail to check and wait complement task", K(ret), K(index_table_id), K(snapshot_version), K(execution_id), K(ddl_task_id));
  } else if (OB_FAIL(ObDDLUtil::get_tablets(index_table_id, dest_tablet_ids))) {
    LOG_WARN("fail to get tablets", K(ret), K(index_table_id));
  } else if (OB_FAIL(check_tablet_merge_status(dest_tablet_ids, snapshot_version, is_all_sstable_build_finished))){
    LOG_WARN("fail to check tablet merge status.", K(ret), K(dest_tablet_ids), K(snapshot_version));
  } else {
    if (is_all_sstable_build_finished) {
      if (OB_FAIL(check_tablet_checksum_update_status(index_table_id, ddl_task_id, execution_id, dest_tablet_ids, is_checksums_all_report))) {
        LOG_WARN("fail to check tablet checksum update status.", K(ret), K(dest_tablet_ids), K(execution_id));
      }
      is_all_sstable_build_finished &= is_checksums_all_report;
    }
  }
  return ret;
}

int ObCheckTabletDataComplementOp::check_finish_report_checksum(const uint64_t index_table_id,
  const int64_t execution_id,
  const uint64_t ddl_task_id)
{
  int ret = OB_SUCCESS;
  bool is_checksums_all_report = false;
  ObArray<ObTabletID> dest_tablet_ids;
#ifdef ERRSIM
  if (GCONF.errsim_ddl_major_delay_time.get() > 0) {
    return OB_SUCCESS;
  }
#endif
  if (OB_UNLIKELY(OB_INVALID_ID == index_table_id ||
      ddl_task_id == OB_INVALID_ID || execution_id < 0)) {
    ret = OB_INVALID_ARGUMENT;
    LOG_ERROR("fail to check report checksum finished", K(ret), K(index_table_id), K(execution_id), K(ddl_task_id));
  } else if (OB_FAIL(ObDDLUtil::get_tablets(index_table_id, dest_tablet_ids))) {
    LOG_WARN("fail to get tablets", K(ret), K(index_table_id));
  } else if (OB_FALSE_IT(lib::ob_sort(dest_tablet_ids.begin(), dest_tablet_ids.end()))) { // sort in ASC order.
  } else if (OB_FAIL(check_tablet_checksum_update_status(index_table_id, ddl_task_id, execution_id, dest_tablet_ids, is_checksums_all_report))) {
    LOG_WARN("fail to check tablet checksum update status, maybe EAGAIN", K(ret), K(dest_tablet_ids), K(execution_id));
  } else if (!is_checksums_all_report) {
    ret = OB_EAGAIN;
    LOG_ERROR("tablets checksum not all report!", K(is_checksums_all_report), K(ret));
  }
  return ret;
}

/*
 * This func is used to check duplicate data completement inner sql
 * if has running inner sql, we should wait until finished. But
 * if not has running inner sql, we should found if all tablet sstable
 * has builded already. If not all builded and no inner sql running, or
 * error case happen, we still execute new inner sql outside.
 */
int ObCheckTabletDataComplementOp::check_and_wait_old_complement_task(const uint64_t table_id,
    const int64_t ddl_task_id,
    const int64_t execution_id,
    const common::ObAddr &inner_sql_exec_addr,
    const common::ObCurTraceId::TraceId &trace_id,
    const int64_t schema_version,
    const int64_t scn,
    bool &need_exec_new_inner_sql)
{
  int ret = OB_SUCCESS;
  need_exec_new_inner_sql = true; // default need execute new inner sql
  bool is_old_task_session_exist = true;
  bool is_dst_checksums_all_report = false;

  if (OB_UNLIKELY(OB_INVALID_ID == table_id)) {
    ret = OB_INVALID_ARGUMENT;
    LOG_WARN("fail to check and wait complement task", K(ret), K(table_id));
  } else if (OB_FAIL(DDL_SIM(ddl_task_id, CHECK_OLD_COMPLEMENT_TASK_FAILED))) {
    LOG_WARN("ddl sim failure: check old complement task failed", K(ret), K(ddl_task_id));
  } else {
    if (OB_FAIL(check_task_inner_sql_session_status(inner_sql_exec_addr, trace_id, ddl_task_id, scn, is_old_task_session_exist))) {
      LOG_WARN("fail check task inner sql session status", K(ret), K(trace_id), K(inner_sql_exec_addr));
    } else if (is_old_task_session_exist) {
      ret = OB_EAGAIN;
    } else {
      LOG_INFO("old inner sql session is not exist.", K(ret));
    }

    // After old session exits, the rule of retry is specified as follows
    //
    // A. for dst table merge checksums of this execution,
    // - if complete, goto B (need_exec_new_inner_sql = false)
    // - else if all tablets has been merged, this means some checksum report failed, retry
    // - else old session must fail/crash, retry
    //
    // B. do checksum validation against src table scan checksums of this execution,
    // - if src checksums are complete, this is exactly a validation
    // - else old session must fail/crash "unexpectedly" (because complete dst checksum in A
    //   guarantees at least one preivous execution has successfully finished table scan),
    //   the validation may returns error due to lack of src checksum records

    ObArray<ObTabletID> dest_tablet_ids;
    if (OB_FAIL(ret)) {
    } else if (OB_FAIL(ObDDLUtil::get_tablets(table_id, dest_tablet_ids))) {
      LOG_WARN("fail to get tablets", K(ret), K(table_id));
    } else if (OB_FAIL(check_tablet_checksum_update_status(table_id, ddl_task_id, execution_id, dest_tablet_ids, is_dst_checksums_all_report))) {
      LOG_WARN("fail to check tablet checksum update status.", K(ret), K(dest_tablet_ids), K(execution_id));
    } else if (is_dst_checksums_all_report) {
      need_exec_new_inner_sql = false;
      LOG_INFO("no need execute because all tablet sstable has build finished", K(need_exec_new_inner_sql));
    }
  }
  if (OB_EAGAIN != ret) {
    LOG_INFO("end to check and wait complement task", K(ret),
      K(table_id), K(is_old_task_session_exist), K(is_dst_checksums_all_report), K(need_exec_new_inner_sql));
  }
  return ret;
}

//record trace_id
ObDDLEventInfo::ObDDLEventInfo()
  : addr_(GCTX.self_addr()),
    sub_id_(0),
    event_ts_(ObTimeUtility::fast_current_time())
{
  init_sub_trace_id(sub_id_);
}

//modify trace_id
ObDDLEventInfo::ObDDLEventInfo(const int32_t sub_id)
  : addr_(GCTX.self_addr()),
    sub_id_(sub_id),
    event_ts_(ObTimeUtility::fast_current_time())
{
  init_sub_trace_id(sub_id_);
}

void ObDDLEventInfo::init_sub_trace_id(const int32_t sub_id)
{
  parent_trace_id_ = *ObCurTraceId::get_trace_id();
  if (sub_id == 0) {
    // ignore
  } else {
    ObCurTraceId::set_sub_id(sub_id);
  }
  trace_id_ = *ObCurTraceId::get_trace_id();
}


void ObDDLEventInfo::set_inner_sql_id(const int64_t execution_id)
{
  parent_trace_id_ = *ObCurTraceId::get_trace_id();
  ObCurTraceId::set_inner_sql_id(execution_id);
  trace_id_ = *ObCurTraceId::get_trace_id();
}
