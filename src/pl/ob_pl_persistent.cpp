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

#define USING_LOG_PREFIX PL_STORAGEROUTINE
#include "ob_pl_persistent.h"
#include "ob_pl_build.h"
#include "share/ob_version.h"

namespace oceanbase
{
namespace pl
{

int ObRoutinePersistentInfo::has_same_name_dependency_with_public_synonym(
                                                                  ObSchemaGetterGuard &schema_guard,
                                                                  const ObPLDependencyTable &dep_schema_objs,
                                                                  bool& exist,
                                                                  ObSQLSessionInfo &session_info)
{
  int ret = OB_SUCCESS;
  exist = false;
  
  ObSchemaChecker schema_checker;
  ObString obj_name;
  uint64_t obj_id;
  OZ (schema_checker.init(schema_guard, session_info.get_server_sid()));
  for (int64_t i = 0; !exist && OB_SUCC(ret) && i < dep_schema_objs.count(); ++i) {
    obj_id = dep_schema_objs.at(i).object_id_;
    if (dep_schema_objs.at(i).is_db_explicit() && 
       (SYNONYM_SCHEMA != dep_schema_objs.at(i).get_schema_type())) {
      continue;
    }
    if (PACKAGE_SCHEMA == dep_schema_objs.at(i).get_schema_type()
        || UDT_SCHEMA == dep_schema_objs.at(i).get_schema_type()
        || ROUTINE_SCHEMA == dep_schema_objs.at(i).get_schema_type()) {
    }
    switch (dep_schema_objs.at(i).get_schema_type()) {
      case SEQUENCE_SCHEMA:
        {
          const ObSequenceSchema *sequence_schema = NULL;
          if (OB_FAIL(schema_guard.get_sequence_schema( 
                                                        obj_id, sequence_schema))) {
            LOG_WARN("failed to get sequence schema", K(ret), K(obj_id));
          } else if (nullptr == sequence_schema) {
            LOG_WARN("get an unexpected null sequence schema", K(obj_id));
          } else {
            obj_name = sequence_schema->get_sequence_name();
          }
          break;
        }
      case ROUTINE_SCHEMA:
        {
          const ObRoutineInfo *routine_schema = NULL;
          if (OB_FAIL(schema_guard.get_routine_info( 
                                                        obj_id, routine_schema))) {
            LOG_WARN("failed to get routine_schema", K(ret), K(obj_id));
          } else if (nullptr == routine_schema) {
            LOG_WARN("get an unexpected null routine_schema", K(obj_id));
          } else {
            obj_name = routine_schema->get_routine_name();
          }
          break;
        }
      case PACKAGE_SCHEMA:
        {
          const ObPackageInfo *package_info = NULL;
          if (OB_FAIL(schema_guard.get_package_info( 
                                                        obj_id, package_info))) {
            LOG_WARN("failed to get package_info", K(ret), K(obj_id));
          } else if (nullptr == package_info) {
            LOG_WARN("get an unexpected null package_info", K(obj_id));
          } else {
            obj_name = package_info->get_package_name();
          }
          break;
        }
      case TABLE_SCHEMA:
        {
          const ObSimpleTableSchemaV2 *table_schema = nullptr;
          if (OB_FAIL(schema_guard.get_simple_table_schema(
                                                          obj_id,
                                                          table_schema))) {
            LOG_WARN("failed to get table schema", K(ret), K(obj_id));
          } else if (nullptr == table_schema) {
            LOG_WARN("get an unexpected null table schema", K(obj_id));
          } else {
            obj_name = table_schema->get_table_name_str();
          }
          break;
        }
      default:
          break;
    }
    if (OB_FAIL(ret)) {
      LOG_WARN("failed to get obj name using dependency id", K(ret), K(obj_id));
    } else if (OB_ISNULL(obj_name)) {
      ret = OB_INVALID_ARGUMENT;
      LOG_WARN("get null obj name using dependency id", K(ret), K(obj_id));
    }
    if (OB_ERR_UNEXPECTED != ret) {
      ret = OB_SUCCESS;
    }
  }
  return ret;
}
template<typename DependencyTable>
int ObRoutinePersistentInfo::check_dep_schema(ObSchemaGetterGuard &schema_guard,
                                              const DependencyTable &dep_schema_objs,
                                              int64_t merge_version,
                                              bool &match)
{
  int ret = OB_SUCCESS;
  
  match = true;
  for (int64_t i = 0; OB_SUCC(ret) && match && i < dep_schema_objs.count(); ++i) {
    if (TABLE_SCHEMA != dep_schema_objs.at(i).get_schema_type()) {
      int64_t new_version = 0;
      if (PACKAGE_SCHEMA == dep_schema_objs.at(i).get_schema_type()
          || UDT_SCHEMA == dep_schema_objs.at(i).get_schema_type()
          || ROUTINE_SCHEMA == dep_schema_objs.at(i).get_schema_type()) {
      }
      if (OB_FAIL(schema_guard.get_schema_version(dep_schema_objs.at(i).get_schema_type(),
                                                  dep_schema_objs.at(i).object_id_,
                                                  new_version))) {
        LOG_WARN("failed to get schema version",
                  K(ret), K(dep_schema_objs.at(i)));
      } else if (new_version <= merge_version) {
        match = true;
      } else {
        match = false;
      }
    } else {
      const ObSimpleTableSchemaV2 *table_schema = nullptr;
      if (OB_FAIL(schema_guard.get_simple_table_schema(
                                                      dep_schema_objs.at(i).object_id_,
                                                      table_schema))) {
        LOG_WARN("failed to get table schema", K(ret), K(dep_schema_objs.at(i)));
      } else if (nullptr == table_schema) {
        ret = OB_ERR_UNEXPECTED;
        LOG_WARN("get an unexpected null table schema", K(dep_schema_objs.at(i).object_id_));
      } else if (table_schema->is_index_table()) {
        // do nothing
      } else if (table_schema->get_schema_version() <= merge_version) {
        match = true;
      } else {
        match = false;
      }
    }
    if (OB_SUCC(ret) && !match) {
      LOG_INFO("not match schema", K(merge_version), K(dep_schema_objs.at(i)));
    }
  }

  return ret;
}

template int ObRoutinePersistentInfo::check_dep_schema<ObPLDependencyTable>(ObSchemaGetterGuard &schema_guard,
                                          const ObPLDependencyTable &dep_schema_objs,
                                          int64_t merge_version,
                                          bool &match);

template int ObRoutinePersistentInfo::check_dep_schema<sql::DependenyTableStore>(ObSchemaGetterGuard &schema_guard,
                                          const sql::DependenyTableStore &dep_schema_objs,
                                          int64_t merge_version,
                                          bool &match);


int ObRoutinePersistentInfo::delete_dll_from_disk(common::ObISQLClient &trans,
                                              uint64_t key_id,
                                              uint64_t database_id)
{
  int ret = OB_SUCCESS;

  ObMySQLProxy *sql_proxy = nullptr;
  bool is_primary_cluster = true;
  if (OB_ISNULL(sql_proxy = GCTX.sql_proxy_)) {
    ret = OB_ERR_UNEXPECTED;
    LOG_WARN("unexpected sql proxy", K(ret));
  } else if (OB_FAIL(ObShareUtil::is_primary_cluster(is_primary_cluster))) {
    LOG_WARN("fail to check whether is primary cluster", KR(ret), K(is_primary_cluster));
  } else if (!is_primary_cluster) {
    // do nothing
  } else {
    
    ObSqlString sql;
    int64_t affected_rows = 0;
    ObMySQLProxy *sql_proxy = nullptr;
    if (OB_INVALID_ID == key_id) {
      ret = OB_ERR_UNEXPECTED;
      LOG_WARN("unexpected key id.", K(ret));
    } else if (OB_FAIL(sql.assign_fmt("delete FROM %s where database_id = %ld and key_id = %ld", OB_ALL_NCOMP_DLL_V2_TNAME, database_id, key_id))) {
      LOG_WARN("delete from __all_ncomp_dll_v2 table failed.", K(ret), K(key_id));
    } else {
      if (OB_FAIL(trans.write(sql.ptr(), affected_rows))) {
        LOG_WARN("execute query failed", K(ret), K(sql));
      } else {
        // do nothing
        LOG_INFO("succ to delete dll", K(key_id), K(affected_rows));
      }
    }
  }

  return ret;
}


} // namespace pl
} // namespace oceanbase
