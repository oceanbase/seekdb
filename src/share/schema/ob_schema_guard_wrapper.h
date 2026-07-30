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

#ifndef OB_OCEANBASE_SCHEMA_OB_SCHEMA_GUARD_WRAPPER_H_
#define OB_OCEANBASE_SCHEMA_OB_SCHEMA_GUARD_WRAPPER_H_

#include "share/schema/ob_schema_getter_guard.h"
#include "share/schema/ob_latest_schema_guard.h"
namespace oceanbase { namespace rootserver { class ObDDLService; } }
namespace oceanbase
{
namespace common
{
class ObMySQLProxy;
}
namespace share
{
namespace schema
{
// this class is used in parallel ddl for letting parallel ddl using local guard in serial mode
class ObSchemaGuardWrapper
{
public:
  ObSchemaGuardWrapper() = delete;
  ObSchemaGuardWrapper(share::schema::ObMultiVersionSchemaService *schema_service,
                       const bool is_local_guard);
  ~ObSchemaGuardWrapper();
  int init(rootserver::ObDDLService *ddl_service);
  int get_local_schema_version(int64_t &schema_version) const;
  int get_foreign_key_id(const uint64_t database_id,
                         const ObString &foreign_key_name,
                         uint64_t &foreign_key_id);
  int get_constraint_id(const uint64_t database_id,
                        const ObString &constraint_name,
                        uint64_t &constraint_id);
  int get_mock_fk_parent_table_id(const uint64_t database_id,
                                  const ObString &table_name,
                                  uint64_t &mock_fk_parent_table_id);
  int get_mock_fk_parent_table_schema(const uint64_t mock_fk_parent_table_id,
                                      const ObMockFKParentTableSchema *&mock_fk_parent_table_schema);
  int get_table_schema(const uint64_t table_id,
                       const ObTableSchema *&table_schema);
  int get_database_id(const common::ObString &database_name,
                      uint64_t &database_id);
  int get_database_schema(const uint64_t database_id,
                          const ObDatabaseSchema *&database_schema);
  int get_table_id(const uint64_t database_id,
                   const uint64_t session_id,
                   const ObString &table_name,
                   uint64_t &table_id,
                   ObTableType &table_type,
                   int64_t &schema_version);
#ifndef GET_OBJ_SCHEMA_VERSIONS
#define GET_OBJ_SCHEMA_VERSIONS(OBJECT_NAME) \
  int get_##OBJECT_NAME##_schema_versions(const common::ObIArray<uint64_t> &obj_ids, \
                                          common::ObIArray<ObSchemaIdVersion> &versions);

  GET_OBJ_SCHEMA_VERSIONS(table);
  GET_OBJ_SCHEMA_VERSIONS(mock_fk_parent_table);
#undef GET_OBJ_SCHEMA_VERSIONS
#endif

int get_obj_privs(const uint64_t obj_id,
                  const ObObjectType obj_type,
                  common::ObIArray<ObObjPriv> &obj_privs);
int get_trigger_info(const uint64_t trigger_id,
                     const ObTriggerInfo *&trigger_info);
  int get_coded_index_name_info_mysql(common::ObIAllocator &allocator,
                                      const uint64_t database_id,
                                      const uint64_t data_table_id,
                                      const ObString &index_name,
                                      const bool is_built_in,
                                      ObIndexSchemaInfo &index_info);
  int get_sys_variable_schema(const ObSysVariableSchema *&sys_var_schema);
  ObLatestSchemaGuard* get_latest_schema_guard() { return &latest_schema_guard_; }
private:
  int check_inner_stat_() const;
private:
  
  ObMultiVersionSchemaService *schema_service_;
  ObLatestSchemaGuard latest_schema_guard_;
  ObSchemaGetterGuard local_schema_guard_;
  const bool is_local_guard_;
};




} //end of namespace schema
} //end of namespace share
} //end of namespace oceanbase
#endif //OB_OCEANBASE_SCHEMA_OB_SCHEMA_GUARD_WRAPPER_H_
