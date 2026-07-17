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

#include "share/schema/ob_latest_schema_guard.h"
#include "share/schema/ob_multi_version_schema_service.h"

using namespace oceanbase::lib;
using namespace oceanbase::common;
using namespace oceanbase::share;
using namespace oceanbase::share::schema;

ObLatestSchemaGuard::ObLatestSchemaGuard(
  ObMultiVersionSchemaService *schema_service,
  ObISQLClient *sql_client)
  : schema_service_(schema_service),
    local_allocator_("LastestSchGuard"),
    schema_objs_(OB_MALLOC_NORMAL_BLOCK_SIZE, ModulePageAllocator(local_allocator_)),
    sql_client_(sql_client)
{
}

ObLatestSchemaGuard::~ObLatestSchemaGuard()
{
}

int ObLatestSchemaGuard::check_inner_stat_()
{
  int ret = OB_SUCCESS;
  int64_t schema_version = OB_INVALID_VERSION;
  if (OB_ISNULL(schema_service_)) {
    ret = OB_NOT_INIT;
    LOG_WARN("schema service is null", KR(ret));
  } else if (OB_FAIL(schema_service_->get_tenant_refreshed_schema_version(schema_version))) {
    if (OB_ENTRY_NOT_EXIST == ret) {
      ret = OB_TENANT_NOT_EXIST;
      LOG_WARN("tenant not exist", KR(ret));
    } else {
      LOG_WARN("fail to get tenant refreshed schema version", KR(ret));
    }
  }
  return ret;
}

int ObLatestSchemaGuard::check_and_get_service_(
    ObSchemaService *&schema_service_impl,
    ObISQLClient *&sql_client)
{
  int ret = OB_SUCCESS;
  if (OB_FAIL(check_inner_stat_())) {
    LOG_WARN("fail to check inner stat", KR(ret));
  } else if (OB_ISNULL(schema_service_impl = schema_service_->get_schema_service())) {
    ret = OB_ERR_UNEXPECTED;
    LOG_WARN("schema service impl is null", KR(ret));
  } else if (OB_NOT_NULL(sql_client_)) {
    sql_client = sql_client_;
  } else if (OB_ISNULL(sql_client = schema_service_->get_sql_proxy())) {
    ret = OB_ERR_UNEXPECTED;
    LOG_WARN("sql proxy is null", KR(ret));
  }
  return ret;
}

template<typename T>
int ObLatestSchemaGuard::get_schema_(
    const ObSchemaType schema_type,
    const uint64_t schema_id,
    const T *&schema)
{
  int ret = OB_SUCCESS;
  const ObSchema *base_schema = NULL;
  schema = NULL;
  if (OB_FAIL(check_inner_stat_())) {
    LOG_WARN("fail to check inner stat", KR(ret));
  } else if (OB_UNLIKELY(!is_normal_schema(schema_type)
             || OB_INVALID_ID == schema_id)) {
    ret = OB_INVALID_ARGUMENT;
    LOG_WARN("invalid argument", KR(ret), K(schema_type), K(schema_id));
  } else if (OB_FAIL(get_from_local_cache_(schema_type, schema_id, schema))) {
    if (OB_ENTRY_NOT_EXIST != ret) {
      LOG_WARN("fail to get schema from cache", KR(ret), K(schema_type), K(schema_id));
    } else if (OB_FAIL(schema_service_->get_latest_schema(
               local_allocator_, schema_type, schema_id, base_schema))) {
      LOG_WARN("fail to get latest schema", KR(ret), K(schema_type), K(schema_id));
    } else if (OB_ISNULL(base_schema)) {
      // schema not exist
    } else if (OB_FAIL(put_to_local_cache_(schema_type, schema_id, base_schema))) {
      LOG_WARN("fail to put to local cache", KR(ret), K(schema_type), K(schema_id));
    } else {
      schema = static_cast<const T*>(base_schema);
    }
  }
  return ret;
}

template<typename T>
int ObLatestSchemaGuard::get_from_local_cache_(
    const ObSchemaType schema_type,
    const uint64_t schema_id,
    const T *&schema)
{
  int ret = OB_SUCCESS;
  schema = NULL;
  if (OB_UNLIKELY(OB_INVALID_ID == schema_id
      || !is_normal_schema(schema_type))) {
    ret = OB_INVALID_ARGUMENT;
    LOG_WARN("invalid argument", KR(ret), K(schema_type), K(schema_id));
  } else {
    const ObSchema *tmp_schema = NULL;
    bool found = false;
    FOREACH_CNT_X(id_schema, schema_objs_, !found) {
      if (id_schema->schema_type_ == schema_type
          && id_schema->schema_id_ == schema_id) {
        tmp_schema = id_schema->schema_;
        found = true;
      }
    }
    if (!found) {
      ret = OB_ENTRY_NOT_EXIST;
      LOG_TRACE("local cache miss [id to schema]", KR(ret), K(schema_type), K(schema_id));
    } else if (OB_ISNULL(tmp_schema)) {
      ret = OB_ERR_UNEXPECTED;
      LOG_WARN("tmp schema is NULL", KR(ret), K(schema_type), K(schema_id));
    } else {
      schema = static_cast<const T *>(tmp_schema);
      LOG_TRACE("schema cache hit", K(schema_type), K(schema_id));
    }
  }
  return ret;
}

template<typename T>
int ObLatestSchemaGuard::put_to_local_cache_(
    const ObSchemaType schema_type,
    const uint64_t schema_id,
    const T *&schema)
{
  int ret = OB_SUCCESS;
  SchemaObj schema_obj;
  schema_obj.schema_type_ = schema_type;
  
  schema_obj.schema_id_ = schema_id;
  schema_obj.schema_ = const_cast<ObSchema*>(schema);
  if (OB_FAIL(schema_objs_.push_back(schema_obj))) {
    LOG_WARN("add schema object failed", KR(ret), K(schema_type), K(schema_id));
  }
  return ret;
}

int ObLatestSchemaGuard::get_tablegroup_id(
    const common::ObString &tablegroup_name,
    uint64_t &tablegroup_id)
{
  int ret = OB_SUCCESS;
  ObSchemaService *schema_service_impl = NULL;
  ObISQLClient *sql_client = NULL;
  tablegroup_id = OB_INVALID_ID;
  if (OB_FAIL(check_and_get_service_(schema_service_impl, sql_client))) {
    LOG_WARN("fail to check and get service", KR(ret));
  } else if (OB_UNLIKELY(tablegroup_name.empty())) {
    ret = OB_INVALID_ARGUMENT;
    LOG_WARN("tablegroup_name is empty", KR(ret), K(tablegroup_name));
  } else if (OB_FAIL(schema_service_impl->get_tablegroup_id(
             *sql_client, tablegroup_name, tablegroup_id))) {
    LOG_WARN("fail to get tablegroup id", KR(ret), K(tablegroup_name));
  } else if (OB_UNLIKELY(OB_INVALID_ID == tablegroup_id)) {
    LOG_INFO("tablegroup not exist", KR(ret), K(tablegroup_name));
  }
  return ret;
}

int ObLatestSchemaGuard::get_database_id(
    const common::ObString &database_name,
    uint64_t &database_id)
{
  int ret = OB_SUCCESS;
  ObSchemaService *schema_service_impl = NULL;
  ObISQLClient *sql_client = NULL;
  database_id = OB_INVALID_ID;
  if (OB_FAIL(check_and_get_service_(schema_service_impl, sql_client))) {
    LOG_WARN("fail to check and get service", KR(ret));
  } else if (OB_UNLIKELY(database_name.empty())) {
    ret = OB_INVALID_ARGUMENT;
    LOG_WARN("database_name is empty", KR(ret), K(database_name));
  } else if (OB_FAIL(schema_service_impl->get_database_id(
             *sql_client, database_name, database_id))) {
    LOG_WARN("fail to get database id", KR(ret), K(database_name));
  } else if (OB_UNLIKELY(OB_INVALID_ID == database_id)) {
    LOG_INFO("database not exist", KR(ret), K(database_name));
  }
  return ret;
}

int ObLatestSchemaGuard::get_table_id(
    const uint64_t database_id,
    const uint64_t session_id,
    const ObString &table_name,
    uint64_t &table_id,
    ObTableType &table_type,
    int64_t &schema_version)
{
  int ret = OB_SUCCESS;
  ObSchemaService *schema_service_impl = NULL;
  ObISQLClient *sql_client = NULL;
  table_id = OB_INVALID_ID;
  table_type = ObTableType::MAX_TABLE_TYPE;
  schema_version = OB_INVALID_VERSION;
  if (OB_FAIL(check_and_get_service_(schema_service_impl, sql_client))) {
    LOG_WARN("fail to check and get service", KR(ret));
  } else if (OB_UNLIKELY(OB_INVALID_ID == database_id
             || table_name.empty())) {
    ret = OB_INVALID_ARGUMENT;
    LOG_WARN("database_id/table_name is invalid",
             KR(ret), K(database_id), K(table_name));
  } else if (OB_FAIL(schema_service_impl->get_table_id(
             *sql_client, database_id, session_id,
             table_name, table_id, table_type, schema_version))) {
    LOG_WARN("fail to get database id", KR(ret), K(database_id), K(session_id), K(table_name));
  } else if (OB_UNLIKELY(OB_INVALID_ID == table_id)) {
    LOG_INFO("table not exist", KR(ret),  K(database_id), K(session_id), K(table_name));
  }
  return ret;
}

int ObLatestSchemaGuard::get_mock_fk_parent_table_id(
    const uint64_t database_id,
    const ObString &table_name,
    uint64_t &mock_fk_parent_table_id)
{
  int ret = OB_SUCCESS;
  ObSchemaService *schema_service_impl = NULL;
  ObISQLClient *sql_client = NULL;
  if (OB_FAIL(check_and_get_service_(schema_service_impl, sql_client))) {
    LOG_WARN("fail to check and get service", KR(ret));
  } else if (OB_UNLIKELY(OB_INVALID_ID == database_id
             || table_name.empty())) {
    ret = OB_INVALID_ARGUMENT;
    LOG_WARN("database_id/table_name is invalid",
             KR(ret), K(database_id), K(table_name));
  } else if (OB_FAIL(schema_service_impl->get_mock_fk_parent_table_id(
             *sql_client, database_id, table_name, mock_fk_parent_table_id))) {
    LOG_WARN("fail to get mock parent table id", KR(ret), K(database_id), K(table_name));
  } else if (OB_UNLIKELY(OB_INVALID_ID == mock_fk_parent_table_id)) {
    LOG_INFO("mock parent table not exist", KR(ret), K(database_id), K(table_name));
  }
  return ret;
}

int ObLatestSchemaGuard::get_constraint_id(
    const uint64_t database_id,
    const ObString &constraint_name,
    uint64_t &constraint_id)
{
  int ret = OB_SUCCESS;
  ObSchemaService *schema_service_impl = NULL;
  ObISQLClient *sql_client = NULL;
  constraint_id = OB_INVALID_ID;
  if (OB_FAIL(check_and_get_service_(schema_service_impl, sql_client))) {
    LOG_WARN("fail to check and get service", KR(ret));
  } else if (OB_UNLIKELY(OB_INVALID_ID == database_id
             || constraint_name.empty())) {
    ret = OB_INVALID_ARGUMENT;
    LOG_WARN("database_id/constraint_name is invalid",
             KR(ret), K(database_id), K(constraint_name));
  } else if (OB_FAIL(schema_service_impl->get_constraint_id(
             *sql_client, database_id, constraint_name, constraint_id))) {
    LOG_WARN("fail to get constraint id", KR(ret), K(database_id), K(constraint_name));
  } else if (OB_UNLIKELY(OB_INVALID_ID == constraint_id)) {
    LOG_INFO("constraint not exist", KR(ret), K(database_id), K(constraint_name));
  }
  return ret;
}

int ObLatestSchemaGuard::get_foreign_key_id(
    const uint64_t database_id,
    const ObString &foreign_key_name,
    uint64_t &foreign_key_id)
{
  int ret = OB_SUCCESS;
  ObSchemaService *schema_service_impl = NULL;
  ObISQLClient *sql_client = NULL;
  foreign_key_id = OB_INVALID_ID;
  if (OB_FAIL(check_and_get_service_(schema_service_impl, sql_client))) {
    LOG_WARN("fail to check and get service", KR(ret));
  } else if (OB_UNLIKELY(OB_INVALID_ID == database_id
             || foreign_key_name.empty())) {
    ret = OB_INVALID_ARGUMENT;
    LOG_WARN("database_id/foreign_key_name is invalid",
             KR(ret), K(database_id), K(foreign_key_name));
  } else if (OB_FAIL(schema_service_impl->get_foreign_key_id(
             *sql_client, database_id, foreign_key_name, foreign_key_id))) {
    LOG_WARN("fail to get foreign_key id", KR(ret), K(database_id), K(foreign_key_name));
  } else if (OB_UNLIKELY(OB_INVALID_ID == foreign_key_id)) {
    LOG_INFO("foreign_key not exist", KR(ret), K(database_id), K(foreign_key_name));
  }
  return ret;
}

int ObLatestSchemaGuard::get_sequence_id(
    const uint64_t database_id,
    const ObString &sequence_name,
    uint64_t &sequence_id,
    bool &is_system_generated)
{
  int ret = OB_SUCCESS;
  ObSchemaService *schema_service_impl = NULL;
  ObISQLClient *sql_client = NULL;
  sequence_id = OB_INVALID_ID;
  is_system_generated = false;
  if (OB_FAIL(check_and_get_service_(schema_service_impl, sql_client))) {
    LOG_WARN("fail to check and get service", KR(ret));
  } else if (OB_UNLIKELY(OB_INVALID_ID == database_id
             || sequence_name.empty())) {
    ret = OB_INVALID_ARGUMENT;
    LOG_WARN("database_id/sequence_name is invalid",
             KR(ret), K(database_id), K(sequence_name));
  } else if (OB_FAIL(schema_service_impl->get_sequence_id(
             *sql_client, database_id,
             sequence_name, sequence_id, is_system_generated))) {
    LOG_WARN("fail to get sequence id", KR(ret), K(database_id), K(sequence_name));
  } else if (OB_UNLIKELY(OB_INVALID_ID == sequence_id)) {
    LOG_INFO("sequence not exist", KR(ret), K(database_id), K(sequence_id));
  }
  return ret;
}

int ObLatestSchemaGuard::get_package_id(
    const uint64_t database_id,
    const ObString &package_name,
    const ObPackageType package_type,
    const int64_t compatible_mode,
    uint64_t &package_id)
{
  int ret = OB_SUCCESS;
  ObSchemaService *schema_service_impl = NULL;
  ObISQLClient *sql_client = NULL;
  package_id = OB_INVALID_ID;
  if (OB_FAIL(check_and_get_service_(schema_service_impl, sql_client))) {
    LOG_WARN("fail to check and get service", KR(ret));
  } else if (OB_UNLIKELY(OB_INVALID_ID == database_id
             || package_name.empty()
             || INVALID_PACKAGE_TYPE == package_type
             || compatible_mode < 0)) {
    ret = OB_INVALID_ARGUMENT;
    LOG_WARN("database_id/package_name/package_type/compatible_mode is invalid",
             KR(ret), K(database_id), K(package_name),
             K(package_type), K(compatible_mode));
  } else if (OB_FAIL(schema_service_impl->get_package_id(
             *sql_client, database_id, package_name,
             package_type, compatible_mode, package_id))) {
    LOG_WARN("fail to get package id", KR(ret),
             K(database_id), K(package_name), K(compatible_mode));
  } else if (OB_UNLIKELY(OB_INVALID_ID == package_id)) {
    LOG_INFO("package not exist", KR(ret), K(database_id),
             K(package_name), K(package_type), K(compatible_mode));
  }

  return ret;
}

int ObLatestSchemaGuard::get_routine_id(
    const uint64_t database_id,
    const uint64_t package_id,
    const uint64_t overload,
    const ObString &routine_name,
    common::ObIArray<std::pair<uint64_t, share::schema::ObRoutineType>> &routine_pairs)
{
  int ret = OB_SUCCESS;
  ObSchemaService *schema_service_impl = NULL;
  ObISQLClient *sql_client = NULL;
  routine_pairs.reset();
  if (OB_FAIL(check_and_get_service_(schema_service_impl, sql_client))) {
    LOG_WARN("fail to check and get service", KR(ret));
  } else if (OB_UNLIKELY(OB_INVALID_ID == database_id
             || routine_name.empty())) {
    ret = OB_INVALID_ARGUMENT;
    LOG_WARN("database_id/routine_name is invalid",
             KR(ret), K(database_id), K(routine_name));
  } else if (OB_FAIL(schema_service_impl->get_routine_id(
             *sql_client, database_id, package_id,
             overload, routine_name, routine_pairs))) {
    LOG_WARN("fail to get routine id", KR(ret),
             K(database_id), K(package_id), K(overload), K(routine_name));
  } else if (OB_UNLIKELY(routine_pairs.empty())) {
    LOG_INFO("routine not exist", KR(ret), K(database_id),
             K(package_id), K(routine_name), K(overload), K(routine_name));
  }
  return ret;
}

int ObLatestSchemaGuard::get_table_schema(
    const uint64_t table_id,
    const ObTableSchema *&table_schema)
{
  int ret = OB_SUCCESS;
  if (OB_FAIL(check_inner_stat_())) {
    LOG_WARN("fail to check inner stat", KR(ret));
  } else if (OB_FAIL(get_schema_(TABLE_SCHEMA,
             table_id, table_schema))) {
    LOG_WARN("fail to get table table", KR(ret), K(table_id));
  } else if (OB_ISNULL(table_schema)) {
    LOG_INFO("table not exist", KR(ret), K(table_id));
  }
  return ret;
}

int ObLatestSchemaGuard::get_mock_fk_parent_table_schema(
    const uint64_t mock_fk_parent_table_id,
    const ObMockFKParentTableSchema *&mock_fk_parent_table_schema)
{
  int ret = OB_SUCCESS;
  if (OB_FAIL(check_inner_stat_())) {
    LOG_WARN("fail to check inner stat", KR(ret));
  } else if (OB_FAIL(get_schema_(MOCK_FK_PARENT_TABLE_SCHEMA,
             mock_fk_parent_table_id, mock_fk_parent_table_schema))) {
    LOG_WARN("fail to get mock fk parent table", KR(ret), K(mock_fk_parent_table_id));
  } else if (OB_ISNULL(mock_fk_parent_table_schema)) {
    LOG_INFO("mock fk parent table not exist", KR(ret), K(mock_fk_parent_table_id));
  }
  return ret;
}

int ObLatestSchemaGuard::get_tablegroup_schema_(
    ObISQLClient &sql_client,
    const uint64_t tablegroup_id,
    const ObTablegroupSchema *&tablegroup_schema)
{
  int ret = OB_SUCCESS;
  if (OB_UNLIKELY(OB_INVALID_ID == tablegroup_id)) {
    ret = OB_INVALID_ARGUMENT;
    LOG_WARN("tablegroup_id is invalid", KR(ret), K(tablegroup_id));
  } else if (OB_FAIL(get_from_local_cache_(TABLEGROUP_SCHEMA,
      tablegroup_id, tablegroup_schema))) {
    if (OB_ENTRY_NOT_EXIST != ret) {
      LOG_WARN("fail to get schema from cache", KR(ret), K(tablegroup_id));
    } else {
      ObRefreshSchemaStatus schema_status;
      
      const int64_t schema_version = INT64_MAX;
      const ObSchema *base_schema = NULL;
      ObTablegroupSchema *tmp_tablegroup_schema = NULL;
      if (OB_FAIL(schema_service_->get_schema_service()->get_tablegroup_schema(schema_status,
          tablegroup_id, schema_version, sql_client, local_allocator_, tmp_tablegroup_schema))) {
        LOG_WARN("fail to get latest schema", KR(ret), K(tablegroup_id));
      } else if (OB_ISNULL(tmp_tablegroup_schema)) {
        // schema not exist
      } else if (FALSE_IT(tablegroup_schema = tmp_tablegroup_schema))  {
      } else if (FALSE_IT(base_schema = tmp_tablegroup_schema))  {
      } else if (OB_FAIL(put_to_local_cache_(TABLEGROUP_SCHEMA,
          tablegroup_id, base_schema))) {
        LOG_WARN("fail to put to local cache", KR(ret), K(tablegroup_id));
      }
    }
  }
  return ret;
}

int ObLatestSchemaGuard::get_tablegroup_schema(
    const uint64_t tablegroup_id,
    const ObTablegroupSchema *&tablegroup_schema)
{
  int ret = OB_SUCCESS;
  tablegroup_schema = NULL;
  if (OB_FAIL(check_inner_stat_())) {
    LOG_WARN("fail to check inner stat", KR(ret));
  } else if (OB_UNLIKELY(OB_INVALID_ID == tablegroup_id)) {
    ret = OB_INVALID_ARGUMENT;
    LOG_WARN("tablegroup_id is invalid", KR(ret), K(tablegroup_id));
  } else if (OB_NOT_NULL(sql_client_)) {
    // 'sql_client_ not null' means a transcation (ddl transaction is child class of ObISQLClient) is passed in 
    // and we should use this sql_client to get visible tablegroup schema in current transaction
    if (OB_FAIL(get_tablegroup_schema_(*sql_client_, tablegroup_id, tablegroup_schema))) {
      LOG_WARN("fail to get tablegroup", KR(ret), K(tablegroup_id));
    }
  } else if (OB_FAIL(get_schema_(TABLEGROUP_SCHEMA,
             tablegroup_id, tablegroup_schema))) {
    LOG_WARN("fail to get tablegroup", KR(ret), K(tablegroup_id));
  } 

  if (OB_SUCC(ret) && OB_ISNULL(tablegroup_schema)) {
    LOG_INFO("tablegroup not exist", KR(ret), K(tablegroup_id));
  }
  return ret;
}

int ObLatestSchemaGuard::get_database_schema(
    const uint64_t database_id,
    const ObDatabaseSchema *&database_schema)
{
  int ret = OB_SUCCESS;
  database_schema = NULL;
  if (OB_FAIL(check_inner_stat_())) {
    LOG_WARN("fail to check inner stat", KR(ret));
  } else if (OB_UNLIKELY(OB_INVALID_ID == database_id)) {
    ret = OB_INVALID_ARGUMENT;
    LOG_WARN("database_id is invalid", KR(ret), K(database_id));
  } else if (OB_FAIL(get_schema_(DATABASE_SCHEMA,
             database_id, database_schema))) {
    LOG_WARN("fail to get database", KR(ret), K(database_id));
  } else if (OB_ISNULL(database_schema)) {
    LOG_INFO("database not exist", KR(ret), K(database_id));
  }
  return ret;
}

int ObLatestSchemaGuard::get_tenant_schema(
    const ObTenantSchema *&tenant_schema)
{
  int ret = OB_SUCCESS;
  tenant_schema = NULL;
  if (OB_FAIL(check_inner_stat_())) {
    LOG_WARN("fail to check inner stat", KR(ret));
  } else if (OB_FAIL(get_schema_(TENANT_SCHEMA,
             1, tenant_schema))) {
    LOG_WARN("fail to get tenant", KR(ret));
  } else if (OB_ISNULL(tenant_schema)) {
    LOG_INFO("tenant not exist", KR(ret));
  }
  return ret;
}

int ObLatestSchemaGuard::get_coded_index_name_info_mysql(
    common::ObIAllocator &allocator,
    const uint64_t database_id,
    const uint64_t data_table_id,
    const ObString &index_name,
    const bool is_built_in,
    ObIndexSchemaInfo &index_info)
{
  int ret = OB_SUCCESS;
  ObISQLClient *sql_client = NULL;
  ObSchemaService *schema_service_impl = nullptr;
  ObArray<ObIndexSchemaInfo> index_infos;
  if (OB_FAIL(check_inner_stat_())) {
    LOG_WARN("fail to check inner stat", KR(ret));
  } else if (OB_UNLIKELY(OB_INVALID_ID == database_id
                  || OB_INVALID_ID == data_table_id
                  || index_name.empty())) {
    ret = OB_INVALID_ARGUMENT;
    LOG_WARN("should use in mysql mode", KR(ret));
  } else if (OB_FAIL(check_and_get_service_(schema_service_impl, sql_client))) {
    LOG_WARN("fail to check and get service", KR(ret));
  } else if (OB_FAIL(schema_service_impl->get_table_index_infos(allocator, *sql_client, database_id, data_table_id, index_infos))) {
    LOG_WARN("fail to get table index name in mysql", KR(ret),
      K(data_table_id), K(data_table_id));
  }
  for (uint64_t i = 0; OB_SUCC(ret) && i < index_infos.count(); ++i)
  {
    if (schema_service_impl->schema_name_is_equal(index_name,
                                                  index_infos.at(i).get_index_name(),
                                                  true/*case_compare*/,
                                                  true/*collation*/)) {
      if (is_built_in == schema::is_built_in_index(index_infos.at(i).get_index_type())) {
        if (OB_FAIL(index_info.assign(index_infos.at(i)))) {
          LOG_WARN("fail to assign index info", KR(ret));
        }
        break;
      }
    }
  }
  return ret;
}

#ifndef GET_OBJ_SCHEMA_VERSIONS
#define GET_OBJ_SCHEMA_VERSIONS(OBJECT_NAME) \
  int ObLatestSchemaGuard::get_##OBJECT_NAME##_schema_versions(const common::ObIArray<uint64_t> &obj_ids, \
                                                               common::ObIArray<ObSchemaIdVersion> &versions) \
    { \
      int ret = OB_SUCCESS; \
      ObISQLClient *sql_client = nullptr; \
      ObSchemaService *schema_service_impl = nullptr; \
      if (OB_FAIL(check_inner_stat_())) { \
        LOG_WARN("fail to check inner stat", KR(ret)); \
      } else if (OB_UNLIKELY(obj_ids.count() <= 0)) { \
        ret = OB_INVALID_ARGUMENT; \
        LOG_WARN("obj_ids is empty", KR(ret)); \
      } else if (OB_FAIL(check_and_get_service_(schema_service_impl, sql_client))) { \
        LOG_WARN("fail to check and get service", KR(ret)); \
      } else if (OB_FAIL(schema_service_impl->get_##OBJECT_NAME##_schema_versions(*sql_client, obj_ids, versions))) { \
        LOG_WARN("fail to get obj schema versions", KR(ret), K(obj_ids)); \
      } \
      return ret; \
    }

  GET_OBJ_SCHEMA_VERSIONS(table);
  GET_OBJ_SCHEMA_VERSIONS(mock_fk_parent_table);
#undef GET_OBJ_SCHEMA_VERSIONS
#endif


int ObLatestSchemaGuard::get_obj_privs(const uint64_t obj_id,
                                       const ObObjectType obj_type,
                                       common::ObIArray<ObObjPriv> &obj_privs)
{
  int ret = OB_SUCCESS;
  ObISQLClient *sql_client = NULL;
  ObSchemaService *schema_service_impl = nullptr;
  if (OB_FAIL(check_inner_stat_())) {
    LOG_WARN("fail to check inner stat", KR(ret));
  } else if (OB_UNLIKELY(OB_INVALID_ID == obj_id)) {
    ret = OB_INVALID_ARGUMENT;
    LOG_WARN("invalid argument", KR(ret), K(obj_id));
  } else if (OB_FAIL(check_and_get_service_(schema_service_impl, sql_client))) {
    LOG_WARN("fail to check and get service", KR(ret));
  } else if (OB_FAIL(schema_service_impl->get_obj_priv_with_obj_id(*sql_client,
             obj_id, static_cast<uint64_t>(obj_type), obj_privs))) {
    LOG_WARN("fail to get obj priv", KR(ret), K(obj_id));
  }
  return ret;
}

int ObLatestSchemaGuard::get_sequence_schema(const uint64_t sequence_id,
                                             const ObSequenceSchema *&sequence_schema)
{
  int ret = OB_SUCCESS;
  sequence_schema = NULL;
  if (OB_FAIL(check_inner_stat_())) {
    LOG_WARN("fail to check inner stat", KR(ret));
  } else if (OB_UNLIKELY(OB_INVALID_ID == sequence_id)) {
    ret = OB_INVALID_ARGUMENT;
    LOG_WARN("sequence_id is invalid", KR(ret), K(sequence_id));
  } else if (OB_FAIL(get_schema_(SEQUENCE_SCHEMA, sequence_id, sequence_schema))) {
    LOG_WARN("fail to get sequence", KR(ret), K(sequence_id));
  } else if (OB_ISNULL(sequence_schema)) {
    LOG_INFO("sequence not exist", KR(ret));
  }
  return ret;
}

int ObLatestSchemaGuard::get_trigger_info(const uint64_t trigger_id,
                                          const ObTriggerInfo *&trigger_info)
{
  int ret = OB_SUCCESS;
  trigger_info = NULL;
  if (OB_FAIL(check_inner_stat_())) {
    LOG_WARN("fail to check inner stat", KR(ret));
  } else if (OB_UNLIKELY(OB_INVALID_ID == trigger_id)) {
    ret = OB_INVALID_ARGUMENT;
    LOG_WARN("trigger_id is invalid", KR(ret), K(trigger_id));
  } else if (OB_FAIL(get_schema_(TRIGGER_SCHEMA, trigger_id, trigger_info))) {
    LOG_WARN("fail to get trigger", KR(ret), K(trigger_id));
  } else if (OB_ISNULL(trigger_info)) {
    LOG_INFO("trigger not exist", KR(ret), K(trigger_info));
  }
  return ret;
}

int ObLatestSchemaGuard::get_table_schemas_in_tablegroup(
    const uint64_t tablegroup_id,
    ObIArray<const ObTableSchema *> &table_schemas)
{
  int ret = OB_SUCCESS;
  ObSchemaService *schema_service_impl = NULL;
  ObISQLClient *sql_client = NULL;
  if (OB_FAIL(check_and_get_service_(schema_service_impl, sql_client))) {
    LOG_WARN("fail to check and get service", KR(ret));
  } else if (OB_FAIL(schema_service_impl->get_table_schemas_in_tablegroup(local_allocator_,
      *sql_client, tablegroup_id, table_schemas))) {
    LOG_WARN("failed to get table schemas in tablegroup", KR(ret), K(tablegroup_id));
  }
  return ret;
}

int ObLatestSchemaGuard::check_database_exists_in_tablegroup(
    const uint64_t tablegroup_id,
    bool &exists)
{
  int ret = OB_SUCCESS;
  ObSchemaService *schema_service_impl = NULL;
  ObISQLClient *sql_client = NULL;
  if (OB_FAIL(check_and_get_service_(schema_service_impl, sql_client))) {
    LOG_WARN("fail to check and get service", KR(ret));
  } else if (OB_FAIL(schema_service_impl->check_database_exists_in_tablegroup(*sql_client, tablegroup_id, exists))) {
    LOG_WARN("failed to check database exists in tablegroup", KR(ret), K(tablegroup_id));
  }
  return ret;
}

int ObLatestSchemaGuard::get_table_id_and_table_name_in_tablegroup(
    const uint64_t tablegroup_id,
    ObIArray<ObString> &table_names,
    ObIArray<uint64_t> &table_ids)
{
  int ret = OB_SUCCESS;
  ObSchemaService *schema_service_impl = NULL;
  ObISQLClient *sql_client = NULL;
  if (OB_FAIL(check_and_get_service_(schema_service_impl, sql_client))) {
    LOG_WARN("fail to check and get service", KR(ret));
  } else if (OB_FAIL(schema_service_impl->get_table_id_and_table_name_in_tablegroup(local_allocator_, *sql_client, tablegroup_id, table_names, table_ids))) {
    LOG_WARN("fail to get table names and ids in tablegroup", KR(ret), K(tablegroup_id));
  }
  return ret;
}

int ObLatestSchemaGuard::get_sys_variable_schema(const ObSysVariableSchema *&sys_variable_schema)
{
  int ret = OB_SUCCESS;
  sys_variable_schema = NULL;
  if (OB_FAIL(check_inner_stat_())) {
    LOG_WARN("fail to check inner stat", KR(ret));
  } else if (OB_FAIL(get_schema_(SYS_VARIABLE_SCHEMA, 1/*schema_id*/, sys_variable_schema))) {
    LOG_WARN("fail to get tenant system variable", KR(ret), KPC(sys_variable_schema));
  } else if (OB_ISNULL(sys_variable_schema)) {
    LOG_INFO("sys_variable_schema is null", KR(ret));
  }
  return ret;
}
