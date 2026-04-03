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

#include "src/share/schema/ob_schema_guard_wrapper.h"
#include "share/schema/ob_multi_version_schema_service.h"
#include "src/rootserver/ob_ddl_service.h"

using namespace oceanbase::lib;
using namespace oceanbase::common;
using namespace oceanbase::share;
using namespace oceanbase::share::schema;

ObSchemaGuardWrapper::ObSchemaGuardWrapper(const uint64_t tenant_id,
                                           share::schema::ObMultiVersionSchemaService *schema_service,
                                           const bool is_local_guard)
    : tenant_id_(tenant_id), schema_service_(schema_service),
      latest_schema_guard_(schema_service, tenant_id), local_schema_guard_(), is_local_guard_(is_local_guard) {}

ObSchemaGuardWrapper::~ObSchemaGuardWrapper() {}

int ObSchemaGuardWrapper::init(rootserver::ObDDLService *ddl_service)
{
  int ret = OB_SUCCESS;
  if (is_local_guard_) {
    if (OB_ISNULL(ddl_service)) {
      ret = OB_INVALID_ARGUMENT;
      LOG_WARN("ddl_service is null", KR(ret));
    } else {
      if (OB_FAIL(ddl_service->get_tenant_schema_guard_with_version_in_inner_table(
                  tenant_id_, local_schema_guard_))) {
        LOG_WARN("fail to get tenant schema guard with version in inner table",
                 KR(ret), K(tenant_id_));
      } else {
        LOG_INFO("get local schema guard success", K(tenant_id_));
      }
    }
  }
  return ret;
}

int ObSchemaGuardWrapper::check_inner_stat_() const
{
  int ret = OB_SUCCESS;
  if (OB_UNLIKELY(OB_INVALID_ID == tenant_id_)) {
    ret = OB_NOT_INIT;
    LOG_WARN("tenant_id is invalid", KR(ret));
  } else if (OB_ISNULL(schema_service_)) {
    ret = OB_NOT_INIT;
    LOG_WARN("schema service is null", KR(ret));
  } else if (is_local_guard_) {
    if (!local_schema_guard_.is_inited()) {
      ret = OB_NOT_INIT;
      LOG_WARN("local_schema_guard not init", KR(ret));
    }
  }
  return ret;
}

int ObSchemaGuardWrapper::get_local_schema_version(int64_t &schema_version) const
{
  int ret = OB_SUCCESS;
  schema_version = OB_INVALID_VERSION;
  if (OB_FAIL(check_inner_stat_())) {
    LOG_WARN("not init", KR(ret));
  } else if (is_local_guard_ && OB_FAIL(local_schema_guard_.get_schema_version(tenant_id_, schema_version))) {
    LOG_WARN("fail to get schema version", KR(ret), K(tenant_id_));
  } else if (!is_local_guard_) {
    ret = OB_INVALID_ARGUMENT;
    LOG_WARN("not local guard can not get local schema version", KR(ret), K(is_local_guard_));
  }
  return ret;
}

int ObSchemaGuardWrapper::get_foreign_key_id(const uint64_t database_id,
                                             const ObString &foreign_key_name,
                                             uint64_t &foreign_key_id)
{
  int ret = OB_SUCCESS;
  foreign_key_id = OB_INVALID_ID;
  if (OB_FAIL(check_inner_stat_())) {
    LOG_WARN("not init", KR(ret));
  } else if (is_local_guard_) {
    if (OB_FAIL(local_schema_guard_.get_foreign_key_id(
                tenant_id_, database_id, foreign_key_name, foreign_key_id))) {
      LOG_WARN("fail to get foreign key id", KR(ret), K(database_id),
               K(foreign_key_name));
    }
  } else if (OB_FAIL(latest_schema_guard_.get_foreign_key_id(
                     database_id, foreign_key_name, foreign_key_id))) {
    LOG_WARN("fail to get foreign key id", KR(ret), K(database_id),
             K(foreign_key_name));
  }
  return ret;
}

int ObSchemaGuardWrapper::get_constraint_id(const uint64_t database_id,
                                            const ObString &constraint_name,
                                            uint64_t &constraint_id)
{
  int ret = OB_SUCCESS;
  constraint_id = OB_INVALID_ID;
  if (OB_FAIL(check_inner_stat_())) {
    LOG_WARN("not init", KR(ret));
  } else if (is_local_guard_) {
    if (OB_FAIL(local_schema_guard_.get_constraint_id(
                tenant_id_, database_id, constraint_name, constraint_id))) {
      LOG_WARN("fail to get constraint id", KR(ret), K(database_id),
               K(constraint_name));
    }
  } else if (OB_FAIL(latest_schema_guard_.get_constraint_id(
                     database_id, constraint_name, constraint_id))) {
    LOG_WARN("fail to get constraint id", KR(ret), K(database_id),
             K(constraint_name));
  }
  return ret;
}

int ObSchemaGuardWrapper::get_mock_fk_parent_table_id(
    const uint64_t database_id, const ObString &table_name,
    uint64_t &mock_fk_parent_table_id)
{
  int ret = OB_SUCCESS;
  mock_fk_parent_table_id = OB_INVALID_ID;
  if (OB_FAIL(check_inner_stat_())) {
    LOG_WARN("not init", KR(ret));
  } else if (is_local_guard_) {
    const ObMockFKParentTableSchema *mock_fk_parent_table_ptr = NULL;
    if (OB_FAIL(local_schema_guard_.get_mock_fk_parent_table_schema_with_name(
                tenant_id_, database_id, table_name, mock_fk_parent_table_ptr))) {
      LOG_WARN("fail to get mock fk parent table id", KR(ret), K(database_id),
               K(table_name));
    } else if (OB_NOT_NULL(mock_fk_parent_table_ptr)) {
      mock_fk_parent_table_id = mock_fk_parent_table_ptr->get_table_id();
    }
  } else if (OB_FAIL(latest_schema_guard_.get_mock_fk_parent_table_id(
                     database_id, table_name, mock_fk_parent_table_id))) {
    LOG_WARN("fail to get mock fk parent table id", KR(ret), K(database_id),
             K(table_name));
  }
  return ret;
}

int ObSchemaGuardWrapper::get_mock_fk_parent_table_schema(
    const uint64_t mock_fk_parent_table_id,
    const ObMockFKParentTableSchema *&mock_fk_parent_table_schema)
{
  int ret = OB_SUCCESS;
  mock_fk_parent_table_schema = nullptr;
  if (OB_FAIL(check_inner_stat_())) {
    LOG_WARN("not init", KR(ret));
  } else if (is_local_guard_) {
    if (OB_FAIL(local_schema_guard_.get_mock_fk_parent_table_schema_with_id(
                tenant_id_, mock_fk_parent_table_id,
                mock_fk_parent_table_schema))) {
      LOG_WARN("fail to get mock fk parent table schema", KR(ret),
               K(mock_fk_parent_table_id));
    }
  } else if (OB_FAIL(latest_schema_guard_.get_mock_fk_parent_table_schema(
                     mock_fk_parent_table_id, mock_fk_parent_table_schema))) {
    LOG_WARN("fail to get mock fk parent table schema", KR(ret),
             K(mock_fk_parent_table_id));
  }
  return ret;
}

int ObSchemaGuardWrapper::check_oracle_object_exist(
    const uint64_t database_id, const uint64_t session_id,
    const ObString &object_name, const ObSchemaType &schema_type,
    const ObRoutineType &routine_type, const bool is_or_replace)
{
  int ret = OB_SUCCESS;
  if (OB_FAIL(check_inner_stat_())) {
    LOG_WARN("not init", KR(ret));
  } else if (is_local_guard_) {
    ObArray<ObSchemaType> conflict_schema_types;
    local_schema_guard_.set_session_id(session_id);
    if (OB_FAIL(local_schema_guard_.check_oracle_object_exist(
                tenant_id_, database_id, object_name, schema_type, routine_type,
                is_or_replace, conflict_schema_types))) {
      LOG_WARN("fail to check oracle object exist", KR(ret), K(database_id),
               K(object_name), K(schema_type), K(routine_type),
               K(is_or_replace));
    } else if (conflict_schema_types.count() > 0) {
      ret = OB_ERR_EXIST_OBJECT;
      LOG_WARN("Name is already used by an existing object", KR(ret),
               K(conflict_schema_types));
    }
  } else if (OB_FAIL(latest_schema_guard_.check_oracle_object_exist(
                     database_id, session_id, object_name, schema_type,
                     routine_type, is_or_replace))) {
    LOG_WARN("fail to check oracle object exist", KR(ret), K(database_id),
             K(object_name), K(schema_type), K(routine_type), K(is_or_replace));
  }
  return ret;
}

int ObSchemaGuardWrapper::get_table_schema(const uint64_t table_id,
                                           const ObTableSchema *&table_schema)
{
  int ret = OB_SUCCESS;
  if (OB_FAIL(check_inner_stat_())) {
    LOG_WARN("not init", KR(ret));
  } else if (is_local_guard_) {
    if (OB_FAIL(local_schema_guard_.get_table_schema(tenant_id_, table_id, table_schema))) {
      LOG_WARN("fail to get table schema", KR(ret), K_(tenant_id), K(table_id));
    }
  } else if (OB_FAIL(latest_schema_guard_.get_table_schema(table_id, table_schema))) {
    LOG_WARN("fail to get table schema", KR(ret), K_(tenant_id), K(table_id));
  }
  return ret;
}

int ObSchemaGuardWrapper::get_database_id(const common::ObString &database_name,
                                          uint64_t &database_id)
{
  int ret = OB_SUCCESS;
  if (OB_FAIL(check_inner_stat_())) {
    LOG_WARN("not init", KR(ret));
  } else if (is_local_guard_) {
    if (OB_FAIL(local_schema_guard_.get_database_id(tenant_id_, database_name, database_id))) {
      LOG_WARN("fail to get database id", KR(ret), K_(tenant_id), K(database_name));
    }
  } else if (OB_FAIL(latest_schema_guard_.get_database_id(database_name, database_id))) {
    LOG_WARN("fail to get database id", KR(ret), K_(tenant_id), K(database_name));
  }
  return ret;
}

int ObSchemaGuardWrapper::get_database_schema(const uint64_t database_id,
                                              const ObDatabaseSchema *&database_schema)
{
  int ret = OB_SUCCESS;
  if (OB_FAIL(check_inner_stat_())) {
    LOG_WARN("not init", KR(ret));
  } else if (is_local_guard_) {
    if (OB_FAIL(local_schema_guard_.get_database_schema(tenant_id_, database_id, database_schema))) {
      LOG_WARN("fail to get database schema", KR(ret), K_(tenant_id), K(database_id));
    }
  } else if (OB_FAIL(latest_schema_guard_.get_database_schema(database_id, database_schema))) {
    LOG_WARN("fail to get database schema", KR(ret), K_(tenant_id), K(database_id));
  }
  return ret;
}

int ObSchemaGuardWrapper::get_table_id(const uint64_t database_id,
                                       const uint64_t session_id,
                                       const ObString &table_name,
                                       uint64_t &table_id,
                                       ObTableType &table_type,
                                       int64_t &schema_version)
{
  int ret = OB_SUCCESS;
  if (OB_FAIL(check_inner_stat_())) {
    LOG_WARN("not init", KR(ret));
  } else if (is_local_guard_) {
    const ObSimpleTableSchemaV2 *table_schema = nullptr;
    local_schema_guard_.set_session_id(session_id);
    if (OB_FAIL(local_schema_guard_.get_simple_table_schema(tenant_id_, database_id, table_name, false/*is_index*/, table_schema))) {
      LOG_WARN("fail to get table schema", KR(ret), K_(tenant_id), K(database_id), K(table_name));
    } else if (OB_NOT_NULL(table_schema)) {
      table_id = table_schema->get_table_id();
      table_type = table_schema->get_table_type();
      schema_version = table_schema->get_schema_version();
    }
  } else if (OB_FAIL(latest_schema_guard_.get_table_id(database_id, session_id, table_name, table_id, table_type, schema_version))) {
    LOG_WARN("fail to get table id", KR(ret), K_(tenant_id), K(session_id), K(database_id), K(table_name));
  }
  return ret;
}

int ObSchemaGuardWrapper::get_tenant_schema(const uint64_t tenant_id,
                                            const ObTenantSchema *&tenant_schema)
{
  int ret = OB_SUCCESS;
  if (OB_FAIL(check_inner_stat_())) {
    LOG_WARN("not init", KR(ret));
  } else if (is_local_guard_) {
    if (OB_FAIL(local_schema_guard_.get_tenant_info(tenant_id, tenant_schema))) {
      LOG_WARN("fail to get tenant schema", KR(ret), K_(tenant_id));
    }
  } else if (OB_FAIL(latest_schema_guard_.get_tenant_schema(tenant_id, tenant_schema))) {
    LOG_WARN("fail to get tenant schema", KR(ret), K_(tenant_id));
  }
  return ret;
}

int ObSchemaGuardWrapper::get_tablegroup_id(const common::ObString &tablegroup_name,
                                            uint64_t &tablegroup_id)
{
  int ret = OB_SUCCESS;
  if (OB_FAIL(check_inner_stat_())) {
    LOG_WARN("not init", KR(ret));
  } else if (is_local_guard_) {
    if (OB_FAIL(local_schema_guard_.get_tablegroup_id(tenant_id_, tablegroup_name, tablegroup_id))) {
      LOG_WARN("fail to get tablegroup id", KR(ret), K_(tenant_id), K(tablegroup_name));
    }
  } else if (OB_FAIL(latest_schema_guard_.get_tablegroup_id(tablegroup_name, tablegroup_id))) {
    LOG_WARN("fail to get tablegroup id", KR(ret), K_(tenant_id), K(tablegroup_name));
  }
  return ret;
}

int ObSchemaGuardWrapper::get_tablegroup_schema(const uint64_t tablegroup_id,
                                                const ObTablegroupSchema *&tablegroup_schema)
{
  int ret = OB_SUCCESS;
  if (OB_FAIL(check_inner_stat_())) {
    LOG_WARN("not init", KR(ret));
  } else if (is_local_guard_) {
    if (OB_FAIL(local_schema_guard_.get_tablegroup_schema(tenant_id_, tablegroup_id, tablegroup_schema))) {
      LOG_WARN("fail to get tablegroup schema", KR(ret), K_(tenant_id), K(tablegroup_id));
    }
  } else if (OB_FAIL(latest_schema_guard_.get_tablegroup_schema(tablegroup_id, tablegroup_schema))) {
    LOG_WARN("fail to get tablegroup schema", KR(ret), K_(tenant_id), K(tablegroup_id));
  }
  return ret;
}

#ifndef GET_OBJ_SCHEMA_VERSIONS
#define GET_OBJ_SCHEMA_VERSIONS(OBJECT_NAME, SCHEMA_TYPE) \
  int ObSchemaGuardWrapper::get_##OBJECT_NAME##_schema_versions(const common::ObIArray<uint64_t> &obj_ids, \
                                                                common::ObIArray<ObSchemaIdVersion> &versions) \
  { \
    int ret = OB_SUCCESS; \
    if (OB_FAIL(check_inner_stat_())) { \
      LOG_WARN("not init", KR(ret)); \
    } else if (is_local_guard_) { \
      int64_t version = OB_INVALID_VERSION; \
      ObSchemaIdVersion idversion; \
      for (int i = 0; OB_SUCC(ret) && i < obj_ids.count(); ++i) { \
        version = OB_INVALID_VERSION; \
        idversion.reset(); \
        if (OB_FAIL(local_schema_guard_.get_schema_version( SCHEMA_TYPE, tenant_id_, obj_ids.at(i), version))) { \
          LOG_WARN("fail to get table schema versions", KR(ret), K_(tenant_id), K(obj_ids)); \
        } else if (OB_FAIL(idversion.init(obj_ids.at(i), version))) { \
          LOG_WARN("fail to init idversion", KR(ret), K(obj_ids), K(version)); \
        } else if (OB_FAIL(versions.push_back(idversion))) { \
          LOG_WARN("fail to push back", KR(ret)); \
        } \
      } \
    } else if (OB_FAIL(latest_schema_guard_.get_##OBJECT_NAME##_schema_versions(obj_ids, versions))) { \
      LOG_WARN("fail to get table schema versions", KR(ret), K_(tenant_id), K(obj_ids)); \
    } \
    return ret; \
  }

  GET_OBJ_SCHEMA_VERSIONS(table, TABLE_SCHEMA);
  GET_OBJ_SCHEMA_VERSIONS(mock_fk_parent_table, MOCK_FK_PARENT_TABLE_SCHEMA);
#undef GET_OBJ_SCHEMA_VERSIONS
#endif

int ObSchemaGuardWrapper::get_obj_privs(const uint64_t obj_id,
                                        const ObObjectType obj_type,
                                        common::ObIArray<ObObjPriv> &obj_privs)
{
  int ret = OB_SUCCESS;
  if (OB_FAIL(check_inner_stat_())) {
    LOG_WARN("not init", KR(ret));
  } else if (is_local_guard_) {
    ObArray<const ObObjPriv*> obj_privs_pointer;
    if (OB_FAIL(local_schema_guard_.get_obj_priv_with_obj_id(tenant_id_, obj_id, static_cast<uint64_t>(obj_type), obj_privs_pointer, true /*reset flag*/))) {
      LOG_WARN("fail to get user info", KR(ret), K_(tenant_id), K(obj_id));
    } else {
      for (int64_t i = 0; OB_SUCC(ret) && i < obj_privs_pointer.count(); ++i) {
        if (OB_ISNULL(obj_privs_pointer.at(i))) {
          ret = OB_ERR_UNEXPECTED;
          LOG_WARN("obj_privs_pointer contains NULL", KR(ret), K(i));
        } else if(OB_FAIL(obj_privs.push_back(*(obj_privs_pointer.at(i))))) {
          LOG_WARN("obj_privs fail to push back", KR(ret), K(i));
        }
      }
    }
  } else if (OB_FAIL(latest_schema_guard_.get_obj_privs(obj_id, obj_type, obj_privs))) {
    LOG_WARN("fail to get obj privs", KR(ret), K(obj_id), K(obj_type));
  }
  return ret;
}

int ObSchemaGuardWrapper::get_trigger_info(const uint64_t trigger_id,
                                           const ObTriggerInfo *&trigger_info)
{
  int ret = OB_SUCCESS;
  if (OB_FAIL(check_inner_stat_())) {
    LOG_WARN("not init", KR(ret));
  } else if (is_local_guard_) {
    if (OB_FAIL(local_schema_guard_.get_trigger_info(tenant_id_, trigger_id, trigger_info))) {
      LOG_WARN("fail to get trigger info", KR(ret), K_(tenant_id), K(trigger_id));
    }
  } else if (OB_FAIL(latest_schema_guard_.get_trigger_info(trigger_id, trigger_info))) {
    LOG_WARN("fail to get trigger info", KR(ret), K_(tenant_id), K(trigger_id));
  }
  return ret;
}

int ObSchemaGuardWrapper::get_coded_index_name_info_mysql(common::ObIAllocator &allocator,
                                                          const uint64_t database_id,
                                                          const uint64_t data_table_id,
                                                          const ObString &index_name,
                                                          const bool is_built_in,
                                                          ObIndexSchemaInfo &index_info)
{
  int ret = OB_SUCCESS;
  if (OB_FAIL(check_inner_stat_())) {
    LOG_WARN("not init", KR(ret));
  } else if (is_local_guard_) {
    const ObTableSchema *data_table_schema = nullptr;
    bool is_oracle_mode = false;
    ObSchemaService *schema_service_impl = nullptr;
    if (OB_FAIL(ObCompatModeGetter::check_is_oracle_mode_with_tenant_id(
               tenant_id_, is_oracle_mode))) {
      LOG_WARN("fail to check is oracle mode", KR(ret), K_(tenant_id));
    } else if (OB_UNLIKELY(is_oracle_mode)) {
      ret = OB_INVALID_ARGUMENT;
      LOG_WARN("should use in mysql mode", KR(ret), K_(tenant_id));
    } else if (OB_FAIL(local_schema_guard_.get_table_schema(tenant_id_, data_table_id, data_table_schema))) {
      LOG_WARN("fail to get simple table schema", KR(ret), K_(tenant_id), K(data_table_id));
    } else if (OB_ISNULL(data_table_schema)) {
      // this interface don't care about whehter the data table is exist or not.
      LOG_WARN("data table not exist", KR(ret), K_(tenant_id), K(data_table_id));
    } else if (OB_ISNULL(schema_service_impl = schema_service_->get_schema_service())) {
      ret = OB_ERR_UNEXPECTED;
      LOG_WARN("schema service impl is null", KR(ret), K_(tenant_id));
    } else {
      ObSEArray<ObAuxTableMetaInfo, 16> simple_index_infos;
      bool has_same_index_name = false;
      if (OB_FAIL(data_table_schema->get_simple_index_infos(simple_index_infos))) {
        LOG_WARN("get simple_index_infos failed", K(ret));
      }
      for (int64_t i = 0; OB_SUCC(ret) && !has_same_index_name && i < simple_index_infos.count(); ++i) {
        const ObTableSchema *index_table_schema = nullptr;
        ObString tmp_coded_index_name;
        if (OB_FAIL(local_schema_guard_.get_table_schema(tenant_id_,
                                                         simple_index_infos.at(i).table_id_,
                                                         index_table_schema))) {
          LOG_WARN("get_table_schema failed", KR(ret), "table id", simple_index_infos.at(i).table_id_);
        } else if (OB_ISNULL(index_table_schema)) {
          ret = OB_ERR_UNEXPECTED;
          LOG_WARN("table schema should not be null", K(ret));
        } else {
          if (schema_service_impl->schema_name_is_equal(index_name,
                                                        index_table_schema->get_table_name(),
                                                        true/*case_compare*/,
                                                        true/*collation*/)) {
            has_same_index_name = true;
            if (OB_FAIL(ob_write_string(allocator, index_table_schema->get_table_name_str(), tmp_coded_index_name, true/*c_style*/))) {
              LOG_WARN("fail to write string", KR(ret));
            } else if (OB_FAIL(index_info.init(tmp_coded_index_name, index_table_schema->get_table_id(),
                                               index_table_schema->get_schema_version(), index_table_schema->get_index_type()))) {
              LOG_WARN("fail to init index info", KR(ret), K(tmp_coded_index_name),
                                                  K(index_table_schema->get_table_id()),
                                                  K(index_table_schema->get_schema_version()));
            }
          }
        }
      }
    }
  } else if (OB_FAIL(latest_schema_guard_.get_coded_index_name_info_mysql(allocator, database_id, data_table_id,
                                                                          index_name, false /*is built in index*/,index_info))) {
    LOG_WARN("fail to get coded index name info", KR(ret), K(database_id), K(data_table_id), K(index_name));
  }
  return ret;
}

int ObSchemaGuardWrapper::get_sequence_schema(const uint64_t sequence_id,
                                              const ObSequenceSchema *&sequence_schema)
{
  int ret = OB_SUCCESS;
  if (OB_FAIL(check_inner_stat_())) {
    LOG_WARN("not init", KR(ret));
  } else if (is_local_guard_) {
    if(OB_FAIL(local_schema_guard_.get_sequence_schema(tenant_id_, sequence_id, sequence_schema))) {
      LOG_WARN("fail to get sequence schema", KR(ret), K(tenant_id_), K(sequence_id));
    }
  } else if (OB_FAIL(latest_schema_guard_.get_sequence_schema(sequence_id, sequence_schema))) {
    LOG_WARN("fail to get sequence schema", KR(ret), K(sequence_id));
  }
  return ret;
}
// TODO: impelete local guard way later
int ObSchemaGuardWrapper::get_table_id_and_table_name_in_tablegroup(
                          const uint64_t tablegroup_id,
                          common::ObIArray<ObString> &table_names,
                          common::ObIArray<uint64_t> &table_ids)
{
  int ret = OB_SUCCESS;
  if (OB_FAIL(check_inner_stat_())) {
    LOG_WARN("not init", KR(ret));
  } else if (OB_FAIL(latest_schema_guard_.get_table_id_and_table_name_in_tablegroup(tablegroup_id, table_names, table_ids))) {
    LOG_WARN("fail to get table id and table name in tablegroup", KR(ret), K_(tenant_id), K(tablegroup_id));
  }
  return ret;
}

int ObSchemaGuardWrapper::get_table_schemas_in_tablegroup(
                          const uint64_t tablegroup_id,
                          common::ObIArray<const ObTableSchema *> &table_schemas)
{
  int ret = OB_SUCCESS;
  if (OB_FAIL(check_inner_stat_())) {
    LOG_WARN("not init", KR(ret));
  } else if (is_local_guard_) {
    if(OB_FAIL(local_schema_guard_.get_table_schemas_in_tablegroup(tenant_id_, tablegroup_id, table_schemas))) {
      LOG_WARN("fail to get table schemas in tablegroup", KR(ret), K(tenant_id_), K(tablegroup_id));
    }
  } else if (OB_FAIL(latest_schema_guard_.get_table_schemas_in_tablegroup(tablegroup_id, table_schemas))) {
    LOG_WARN("fail to get table schemas in tablegroup", KR(ret), K_(tenant_id), K(tablegroup_id));
  }
  return ret;
}

int ObSchemaGuardWrapper::check_database_exists_in_tablegroup(
                          const uint64_t tablegroup_id,
                          bool &exists)
{
  int ret = OB_SUCCESS;
  if (OB_FAIL(check_inner_stat_())) {
    LOG_WARN("not init", KR(ret));
  } else if (OB_FAIL(latest_schema_guard_.check_database_exists_in_tablegroup(tablegroup_id, exists))) {
    LOG_WARN("fail to check database exists in tablegroup", KR(ret), K_(tenant_id), K(tablegroup_id));
  }
  return ret;
}

int ObSchemaGuardWrapper::get_sys_variable_schema(const ObSysVariableSchema *&sys_var_schema)
{
  int ret = OB_SUCCESS;
  sys_var_schema = nullptr;
  if (OB_FAIL(check_inner_stat_())) {
    LOG_WARN("not init", KR(ret));
  } else if (is_local_guard_) {
    if (OB_FAIL(local_schema_guard_.get_sys_variable_schema(tenant_id_, sys_var_schema))) {
      LOG_WARN("fail to get tenant system variable", KR(ret), K(tenant_id_));
    }
  } else {
    if (OB_FAIL(latest_schema_guard_.get_sys_variable_schema(sys_var_schema))) {
      LOG_WARN("fail to get sys variable schema", KR(ret), K(tenant_id_));
    }
  }

  return ret;
}
