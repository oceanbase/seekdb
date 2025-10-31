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

#ifndef OCEANBASE_SQL_RESOLVER_SCHEMA_CHECKER2_
#define OCEANBASE_SQL_RESOLVER_SCHEMA_CHECKER2_

#include <stdint.h>
#include "lib/string/ob_string.h"
#include "share/schema/ob_column_schema.h"
#include "share/schema/ob_routine_info.h"
#include "share/schema/ob_package_info.h"
#include "sql/resolver/ob_stmt_type.h"
//#include "sql/resolver/dml/ob_select_stmt.h"
#include "share/schema/ob_trigger_info.h"

#define PRIV_CHECK_FLAG_NORMAL     0
#define PRIV_CHECK_FLAG_DISABLE    1
#define PRIV_CHECK_FLAG_IN_PL      2

namespace oceanbase
{
namespace common
{
class ObString;
}
namespace sql {
}
namespace share
{
namespace schema
{
class ObTenantSchema;
class ObUserInfo;
class ObDatabaseSchema;
class ObTablegroupSchema;
class ObTableSchema;
class ObSimpleTableSchemaV2;
class ObColumnSchemaV2;
struct ObSessionPrivInfo;
struct ObStmtNeedPrivs;
class ObSchemaGetterGuard;
class ObUDF;
}
}
namespace sql
{
// wrapper of schema manager which is used by SQL module

#define LBCA_OP_FLAG  1

class ObSqlSchemaGuard;
class ObSchemaChecker
{
public:
  ObSchemaChecker();
  virtual ~ObSchemaChecker();
  int init(share::schema::ObSchemaGetterGuard &schema_mgr, uint64_t session_id = common::OB_INVALID_ID);
  int init(ObSqlSchemaGuard &schema_guard, uint64_t session_id = common::OB_INVALID_ID);
  ObSqlSchemaGuard *get_sql_schema_guard() { return sql_schema_mgr_; }
  share::schema::ObSchemaGetterGuard *get_schema_guard() { return schema_mgr_; }
  // need satifing each priv in stmt_need_privs
  int check_priv(const share::schema::ObSessionPrivInfo &session_priv,
                 const common::ObIArray<uint64_t> &enable_role_id_array,
                 const share::schema::ObStmtNeedPrivs &stmt_need_privs) const;

  // need satifing one of stmt_need_privs
  int check_priv_or(const share::schema::ObSessionPrivInfo &session_priv,
                    const common::ObIArray<uint64_t> &enable_role_id_array,
                    const share::schema::ObStmtNeedPrivs &stmt_need_privs);

  int check_db_access(share::schema::ObSessionPrivInfo &s_priv,
                      const common::ObIArray<uint64_t> &enable_role_id_array,
                      const common::ObString &database_name) const;
  int check_db_access(share::schema::ObSessionPrivInfo &s_priv,
                      const common::ObIArray<uint64_t> &enable_role_id_array,
                      const uint64_t catalog_id,
                      const common::ObString &database_name) const;

  int check_table_show(const share::schema::ObSessionPrivInfo &s_priv,
                       const common::ObIArray<uint64_t> &enable_role_id_array,
                       const uint64_t catalog_id,
                       const common:: ObString &db,
                       const common::ObString &table,
                       bool &allow_show) const;
  int check_routine_show(const share::schema::ObSessionPrivInfo &s_priv,
                         const common::ObString &db,
                         const common::ObString &routine,
                         bool &allow_show) const;
  int check_trigger_show(const share::schema::ObSessionPrivInfo &s_priv,
                         const common::ObIArray<uint64_t> &enable_role_id_array,
                         const common::ObString &db,
                         const common::ObString &trigger,
                         bool &allow_show,
                         const ObString &table) const;
  int check_column_exists(const uint64_t tenant_id, const uint64_t table_id,
                          const common::ObString &column_name,
                          bool &is_exist,
                          bool is_link = false);
  int check_table_or_index_exists(const uint64_t tenant_id,
                                  const uint64_t catalog_id,
                                  const uint64_t database_id,
                                  const common::ObString &table_name,
                                  const bool with_hidden_flag,
                                  const bool is_built_in_index,
                                  bool &is_exist);
  int check_table_exists(const uint64_t tenant_id,
                         const uint64_t catalog_id,
                         const uint64_t database_id,
                         const common::ObString &table_name,
                         const bool is_index,
                         const bool with_hidden_flag,
                         bool &is_exist,
                         const bool is_built_in_index = false);
  //int check_table_exists(uint64_t table_id, bool &is_exist) const;
  int check_table_exists(const uint64_t tenant_id,
                         const common::ObString &database_name,
                         const common::ObString &table_name,
                         const bool is_index_table,
                         const bool with_hidden_flag,
                         bool &is_exist,
                         const bool is_built_in_index = false,
                         const uint64_t catalog_id = OB_INTERNAL_CATALOG_ID); // to many place used this function, assign default catalog_id

  // mock_fk_parent_table begin
  int get_mock_fk_parent_table_with_name(
      const uint64_t tenant_id,
      const uint64_t database_id,
      const common::ObString &name,
      const share::schema::ObMockFKParentTableSchema *&schema);
  // mock_fk_parent_table end

  int get_table_id(const uint64_t tenant_id,
                   const uint64_t database_id,
                   const common::ObString &table_name,
                   const bool is_index_table,
                   uint64_t &table_id);
  //int get_database_name(const uint64_t tenant_id,
  //                      const uint64_t database_id,
  //                      common::ObString &database_name) const;

  int get_database_id(const uint64_t tenant_id, const common::ObString &database_name, uint64_t &database_id) const;
  int get_database_id(const uint64_t tenant_id,
                      const uint64_t catalog_id,
                      const common::ObString &database_name,
                      uint64_t &database_id) const;
  int get_catalog_id_name(const uint64_t tenant_id,
                          common::ObString &catalog_name,
                          uint64_t &catalog_id,
                          ObIAllocator *allocator = NULL,
                          bool allow_not_exist = false) const;
  //int get_local_table_id(const uint64_t tenant_id,
  //                       const uint64_t database_id,
  //                       const common::ObString &table_name,
  //                       uint64_t &table_id) const;
  int get_user_id(const uint64_t tenant_id,
                  const common::ObString &user_name,
                  const common::ObString &host_name,
                  uint64_t &user_id);
  int get_user_info(const uint64_t tenant_id,
                  const common::ObString &user_name,
                  const common::ObString &host_name,
                  const share::schema::ObUserInfo *&user_info);
  int get_user_info(const uint64_t tenant_id,
                    const uint64_t user_id,
                    const share::schema::ObUserInfo *&user_info);
  // First try to get the schema of tbl_name, if it does not exist, treat tbl_name as a synonym name, and get the synonym
  // The schema of the represented table
  int get_table_schema_with_synonym(const uint64_t tenant_id,
                                    const common::ObString &tbl_db_name,
                                    const common::ObString &tbl_name,
                                    bool is_index_table,
                                    bool &has_synonym,
                                    common::ObString &new_db_name,
                                    common::ObString &new_tbl_name,
                                    const share::schema::ObTableSchema *&tbl_schema);
  int get_table_schema(const uint64_t tenant_id,
                       const common::ObString &database_name,
                       const common::ObString &table_name,
                       const bool is_index_table,
                       const share::schema::ObTableSchema *&table_schema,
                       const bool with_hidden_flag = false,
                       const bool is_built_in_index = false);
  int get_table_schema(const uint64_t tenant_id,
                       const uint64_t catalog_id,
                       const uint64_t database_id,
                       const common::ObString &table_name,
                       const bool is_index_table,
                       const bool cte_table_fisrt,
                       const bool with_hidden_flag,
                       const share::schema::ObTableSchema *&table_schema,
                       const bool is_built_in_index = false);
  int get_table_schema(const uint64_t tenant_id,
                       const uint64_t database_id,
                       const common::ObString &table_name,
                       const bool is_index_table,
                       const bool cte_table_fisrt,
                       const bool with_hidden_flag,
                       const share::schema::ObTableSchema *&table_schema,
                       const bool is_built_in_index = false);
  int get_table_schema(const uint64_t tenant_id, const uint64_t table_id, const share::schema::ObTableSchema *&table_schema, bool is_link = false) const;
  //int column_can_be_droped(const uint64_t table_id, const uint64_t column_id, bool &can_be_drop) const;
  int get_column_schema(const uint64_t tenant_id, const uint64_t table_id,
                        const common::ObString &column_name,
                        const share::schema::ObColumnSchemaV2 *&column_schema,
                        const bool get_hidden = false,
                        bool is_link = false);
  int get_column_schema(const uint64_t tenant_id,
                        const uint64_t table_id,
                        const uint64_t column_id,
                        const share::schema::ObColumnSchemaV2 *&column_schema,
                        const bool get_hidden = false,
                        bool is_link = false);
  //int check_is_rowkey_column(const uint64_t tenant_id,
  //                      const uint64_t database_id,
  //                      const common::ObString &table_name,
  //                      const common::ObString &column_name,
  //                      const bool is_index_table,
  //                      bool &is_rowkey_column) const;
  //int check_is_index_table(uint64_t table_id, bool &is_index_table) const;
  int get_can_read_index_array(const uint64_t tenant_id, uint64_t table_id, uint64_t *index_tid_array, int64_t &size, bool with_mv) const;
  int get_can_write_index_array(const uint64_t tenant_id, uint64_t table_id, uint64_t *index_tid_array, int64_t &size, bool only_global = false, bool with_mlog = false) const;
  // tenant
  int get_tenant_id(const common::ObString &tenant_name, uint64_t &teannt_id);
  int get_tenant_info(const uint64_t &tenant_id, const share::schema::ObTenantSchema *&tenant_schema);
  int get_database_schema(const uint64_t tenant_id,
                          const uint64_t database_id,
                          const share::schema::ObDatabaseSchema *&database_schema);
  //check if there is an index on this column
  int check_column_has_index(const uint64_t tenant_id, uint64_t table_id, uint64_t column_id, bool &has_index, bool is_link = false);
  int check_if_partition_key(const uint64_t tenant_id, uint64_t table_id, uint64_t column_id, bool &is_part_key, bool is_link = false) const;
  //int get_collation_info_from_database(const uint64_t tenant_id,
  //                                     const uint64_t database_id,
  //                                     common::ObCharsetType &char_type,
  //                                     common::ObCollationType &coll_type);
  //int get_collation_info_from_tenant(const uint64_t tenant_id,
  //                                   common::ObCharsetType &char_type,
  //                                   common::ObCollationType &coll_type);

  int get_routine_info(const uint64_t tenant_id,
                       const uint64_t routine_id,
                       const share::schema::ObRoutineInfo *&routine_info);
  int get_standalone_procedure_info(const uint64_t tenant_id,
                                   const uint64_t db_id,
                                   const ObString &routine_name,
                                   const share::schema::ObRoutineInfo *&routine_info);
  int get_standalone_procedure_info(const uint64_t tenant_id,
                                    const common::ObString &database_name,
                                    const common::ObString &routine_name,
                                    const share::schema::ObRoutineInfo *&routine_info);
  int get_standalone_function_info(const uint64_t tenant_id,
                                   const uint64_t db_id,
                                   const ObString &routine_name,
                                   const share::schema::ObRoutineInfo *&routine_info);
  int get_standalone_function_info(const uint64_t tenant_id,
                                   const common::ObString &database_name,
                                   const common::ObString &routine_name,
                                   const share::schema::ObRoutineInfo *&routine_info);
  int get_package_routine_infos(const uint64_t tenant_id,
                        const uint64_t package_id,
                        const uint64_t db_id,
                        const common::ObString &routine_name,
                        const share::schema::ObRoutineType routine_type,
                        common::ObIArray<const share::schema::ObIRoutineInfo *> &routine_infos);
  int get_package_routine_infos(const uint64_t tenant_id,
                       const uint64_t package_id,
                       const common::ObString &database_name,
                       const common::ObString &routine_name,
                       const share::schema::ObRoutineType routine_type,
                       common::ObIArray<const share::schema::ObIRoutineInfo *> &routine_infos);
  int get_package_info(const uint64_t tenant_id,
                       const common::ObString &database_name,
                       const common::ObString &package_name,
                       const share::schema::ObPackageType type,
                       const int64_t compatible_mode,
                       const share::schema::ObPackageInfo *&package_info);
  int get_trigger_info(const uint64_t tenant_id,
                       const common::ObString &database_name,
                       const common::ObString &tg_name,
                       const share::schema::ObTriggerInfo *&tg_info);
  int get_package_id(const uint64_t tenant_id,
                     const uint64_t database_id,
                     const common::ObString &package_name,
                     const int64_t compatible_mode,
                     uint64_t &package_id);
  int get_package_id(const uint64_t tenant_id,
                     const common::ObString &database_name,
                     const common::ObString &package_name,
                     const int64_t compatible_mode,
                     uint64_t &package_id);
  int get_routine_id(const uint64_t tenant_id,
                     const ObString &database_name,
                     const ObString &routine_name,
                     uint64_t &routine_id,
                     bool &is_proc);
  int get_udf_info(uint64_t tenant_id,
                   const common::ObString &udf_name,
                   const share::schema::ObUDF *&udf_info,
                   bool &exist);
  int check_sequence_exist_with_name(const uint64_t tenant_id,
                                     const uint64_t database_id,
                                     const common::ObString &sequence_name,
                                     bool &exists,
                                     uint64_t &sequence_id) const;
  int get_sequence_id(const uint64_t tenant_id,
                      const common::ObString &database_name,
                      const common::ObString &sequence_name,
                      uint64_t &sequence_id) const;
  int add_fake_cte_schema(share::schema::ObTableSchema* tbl_schema);
  int find_fake_cte_schema(common::ObString tblname, ObNameCaseMode mode, bool& exist);
  int get_schema_version(const uint64_t tenant_id, uint64_t table_id, share::schema::ObSchemaType schema_type, int64_t &schema_version);
  share::schema::ObSchemaGetterGuard *get_schema_mgr() { return schema_mgr_; }
  int get_tablegroup_schema(const int64_t tenant_id, const common::ObString &tablegroup_name,
                            const share::schema::ObTablegroupSchema *&tablegroup_schema);
  int get_idx_schema_by_origin_idx_name(const uint64_t tenant_id,
                                        const uint64_t database_id,
                                        const common::ObString &index_name,
                                        const share::schema::ObTableSchema *&table_schema);
  int check_exist_same_name_object_with_synonym(const uint64_t tenant_id,
                                                uint64_t database_id,
                                                const common::ObString &object_name,
                                                bool &exist,
                                                bool &is_private_syn);
  int check_mysql_grant_role_priv(const ObSqlCtx &sql_ctx,
                                  const common::ObIArray<uint64_t> &granting_role_ids);
  int check_set_default_role_priv(const ObSqlCtx &sql_ctx);

  static bool enable_mysql_pl_priv_check(int64_t tenant_id, share::schema::ObSchemaGetterGuard &schema_guard);

  // directory
  int get_directory_id(const uint64_t tenant_id,
                       const common::ObString &directory_name,
                       uint64_t &directory_id);

  int remove_tmp_cte_schemas(const ObString& cte_table_name);
  // location
  int get_location_id(const uint64_t tenant_id,
                      const common::ObString &location_name,
                      uint64_t &location_id);
private:

  int get_table_schema_inner(const uint64_t tenant_id, uint64_t table_id,
                             const share::schema::ObTableSchema *&table_schema) const;
  int get_column_schema_inner(const uint64_t tenant_id, uint64_t table_id,
                              const common::ObString &column_name,
                              const share::schema::ObColumnSchemaV2 *&column_schema,
                              bool is_link = false) const;
  int get_column_schema_inner(const uint64_t tenant_id, uint64_t table_id, const uint64_t column_id,
                              const share::schema::ObColumnSchemaV2 *&column_schema,
                              bool is_link = false) const;
private:
  bool is_inited_;
  share::schema::ObSchemaGetterGuard *schema_mgr_;
  ObSqlSchemaGuard *sql_schema_mgr_;
  // cte tmp schema, used for recursive cte service, lifecycle is only valid for this query
  common::ObArray<share::schema::ObTableSchema*,
                  common::ModulePageAllocator, true> tmp_cte_schemas_;
  // Record additional information of checker, such as the operator's actions etc.
  int flag_;
  // disallow copy
  DISALLOW_COPY_AND_ASSIGN(ObSchemaChecker);
};
} // end namespace sql
} // end namespace oceanbase

#endif /* OCEANBASE_SQL_RESOLVER_SCHEMA_CHECKER2_ */
