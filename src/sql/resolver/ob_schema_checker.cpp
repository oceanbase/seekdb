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

#define USING_LOG_PREFIX SQL_RESV
#include "ob_schema_checker.h"

#include "observer/virtual_table/ob_table_columns.h"
#include "pl/ob_pl_stmt.h"
#include "share/catalog/ob_external_catalog.h"
#include "sql/privilege_check/ob_privilege_check.h"
#include "sql/resolver/ob_stmt_resolver.h"

using namespace oceanbase::sql;
using namespace oceanbase::common;
using namespace oceanbase::share::schema;
using oceanbase::share::schema::ObColumnSchemaV2;
using oceanbase::share::schema::ObTableSchema;
using oceanbase::share::schema::ObTenantSchema;
using oceanbase::share::schema::ObDatabaseSchema;

namespace oceanbase
{
namespace sql
{
ObSchemaChecker::ObSchemaChecker()
  :
  is_inited_(false), schema_mgr_(NULL), sql_schema_mgr_(NULL), flag_(0)
{
}

ObSchemaChecker::~ObSchemaChecker()
{
  schema_mgr_ = NULL;
  sql_schema_mgr_ = NULL;
}

int ObSchemaChecker::init(ObSchemaGetterGuard &schema_mgr, uint64_t session_id)
{
  int ret = OB_SUCCESS;
  if (is_inited_) {
    ret = OB_INIT_TWICE;
    LOG_WARN("inited twice", K(ret));
  } else {
    schema_mgr_ = &schema_mgr;
    is_inited_ = true;
    flag_ = 0;
    schema_mgr.set_session_id(session_id);
    if (OB_INVALID_ID != session_id) {
      LOG_DEBUG("ObSchemaChecker init with valid session id", K(session_id));
    }
  }
  return ret;
}

int ObSchemaChecker::init(ObSqlSchemaGuard &sql_schema_mgr, uint64_t session_id)
{
  int ret = OB_SUCCESS;
  OV (OB_NOT_NULL(sql_schema_mgr.get_schema_guard()));
  OZ (init(*sql_schema_mgr.get_schema_guard(), session_id));
  OX (sql_schema_mgr_ = &sql_schema_mgr);
  return ret;
}



int ObSchemaChecker::check_priv(const share::schema::ObSessionPrivInfo &session_priv,
                                const common::ObIArray<uint64_t> &enable_role_id_array,
                                const share::schema::ObStmtNeedPrivs &stmt_need_privs) const
{
  int ret = OB_SUCCESS;
  if (IS_NOT_INIT) {
    ret = OB_NOT_INIT;
    LOG_WARN("schema checker is not inited", K(is_inited_), K(ret));
  } else if (OB_UNLIKELY(!session_priv.is_valid())) {
    ret = OB_INVALID_ARGUMENT;
    LOG_WARN("session_priv is invalid", K(session_priv), K(ret));
  } else if (OB_FAIL(schema_mgr_->check_priv(session_priv, enable_role_id_array, stmt_need_privs))) {
    LOG_WARN("failed to check_priv", K(session_priv), K(stmt_need_privs), K(ret));
  } else {}
  return ret;
}


int ObSchemaChecker::check_priv_or(const share::schema::ObSessionPrivInfo &session_priv,
                                   const common::ObIArray<uint64_t> &enable_role_id_array,
                                   const share::schema::ObStmtNeedPrivs &stmt_need_privs)
{
  int ret = OB_SUCCESS;
  if (IS_NOT_INIT) {
    ret = OB_NOT_INIT;
    LOG_WARN("schema checker is not inited", K(is_inited_), K(ret));
  } else if (OB_UNLIKELY(!session_priv.is_valid())) {
    ret = OB_INVALID_ARGUMENT;
    LOG_WARN("session_priv is invalid", K(session_priv), K(ret));
  } else if (OB_FAIL(schema_mgr_->check_priv_or(session_priv, enable_role_id_array, stmt_need_privs))) {
    LOG_WARN("failed to check_priv_or", K(session_priv), K(stmt_need_privs), K(ret));
  } else {}
  return ret;
}

int ObSchemaChecker::check_db_access(share::schema::ObSessionPrivInfo &s_priv,
                                     const common::ObIArray<uint64_t> &enable_role_id_array,
                                     const ObString& database_name) const
{
  int ret = OB_SUCCESS;
  if (IS_NOT_INIT) {
    ret = OB_NOT_INIT;
    LOG_WARN("schema checker is not inited", K(is_inited_), K(ret));
  } else if (OB_UNLIKELY(!s_priv.is_valid() || database_name.empty())) {
    ret = OB_INVALID_ARGUMENT;
    LOG_WARN("invalid arguments", K(s_priv), K(database_name), K(ret));
  } else if (OB_FAIL(schema_mgr_->check_db_access(s_priv, enable_role_id_array, database_name))) {
    LOG_WARN("failed to check_db_access", K(s_priv), K(enable_role_id_array), K(database_name), K(ret));
  } else {}
  return ret;
}

int ObSchemaChecker::check_db_access(share::schema::ObSessionPrivInfo &s_priv,
                                     const common::ObIArray<uint64_t> &enable_role_id_array,
                                     const uint64_t catalog_id,
                                     const ObString &database_name) const
{
  int ret = OB_SUCCESS;
  if (IS_NOT_INIT) {
    ret = OB_NOT_INIT;
    LOG_WARN("schema checker is not inited", K(is_inited_), K(ret));
  } else if (is_internal_catalog_id(catalog_id)) {
    ret = check_db_access(s_priv, enable_role_id_array, database_name);
  } else if (is_external_catalog_id(catalog_id)) {
    ret = schema_mgr_->check_catalog_db_access(s_priv, enable_role_id_array, catalog_id, database_name);
  } else {
    ret = OB_ERR_UNEXPECTED;
    LOG_WARN("unexpected", K(ret));
  }
  return ret;
}

int ObSchemaChecker::check_table_show(const share::schema::ObSessionPrivInfo &s_priv,
                                      const common::ObIArray<uint64_t> &enable_role_id_array,
                                      const uint64_t catalog_id,
                                      const ObString &db,
                                      const ObString &table,
                                      bool &allow_show) const
{
  int ret = OB_SUCCESS;
  if (IS_NOT_INIT) {
    ret = OB_NOT_INIT;
    LOG_WARN("schema checker is not inited", K(is_inited_), K(ret));
  } else if (OB_UNLIKELY(!s_priv.is_valid() || db.empty() || table.empty())) {
    ret = OB_INVALID_ARGUMENT;
    LOG_WARN("invalid arguments", K(s_priv), K(db), K(table), K(ret));
  } else if (OB_FAIL(schema_mgr_->check_table_show(s_priv, enable_role_id_array, catalog_id, db, table, allow_show))) {
    LOG_WARN("failed to check_table_show", K(s_priv), K(enable_role_id_array), K(catalog_id), K(db), K(table), K(ret));
  } else {}
  return ret;
}

int ObSchemaChecker::check_column_exists(const uint64_t tenant_id,
                                         const uint64_t table_id,
                                         const ObString &column_name,
                                         bool &is_exist,
                                         bool is_link /* = false */)
{
  int ret = OB_SUCCESS;

  is_exist = false;
  const ObColumnSchemaV2 *column_schema = NULL;
  if (IS_NOT_INIT) {
    ret = OB_NOT_INIT;
    LOG_WARN("schema checker is not inited", K(is_inited_), K(ret));
  } else if (OB_UNLIKELY(OB_INVALID_ID == table_id || column_name.empty())) {
    ret = OB_INVALID_ARGUMENT;
    LOG_WARN("invalid arguments", K(table_id), K(column_name), K(ret));
  } else {
    if (OB_FAIL(ret)) {
      // do nothing
    } else if (OB_FAIL(get_column_schema_inner(tenant_id, table_id, column_name, column_schema, is_link))) {
      LOG_WARN("get column schema failed", K(tenant_id), K(table_id), K(column_name), K(ret));
    }
    if (NULL == column_schema) {
      for (int64_t i = 0; OB_SUCC(ret) && i < tmp_cte_schemas_.count(); ++i) {
        if (tmp_cte_schemas_.at(i)->get_table_id() == table_id) {
          column_schema = tmp_cte_schemas_.at(i)->get_column_schema(column_name);
          break;
        }
      }
    }
    if (NULL == column_schema) {
      is_exist = false;
    } else {
      is_exist = true;
    }
  }

  return ret;
}


int ObSchemaChecker::check_routine_show(const share::schema::ObSessionPrivInfo &s_priv,
                                        const ObString &db,
                                        const ObString &routine,
                                        bool &allow_show) const
{
  int ret = OB_SUCCESS;
  allow_show = true;
  if (IS_NOT_INIT) {
    ret = OB_NOT_INIT;
    LOG_WARN("schema checker is not inited", K(is_inited_), K(ret));
  } else if (OB_UNLIKELY(!s_priv.is_valid() || db.empty() || routine.empty())) {
    ret = OB_INVALID_ARGUMENT;
    LOG_WARN("invalid arguments", K(s_priv), K(db), K(routine), K(ret));
//  } else if (OB_FAIL(schema_mgr_->check_routine_show(s_priv, db, routine, allow_show))) { //TODO: ryan.ly
//    LOG_WARN("failed to check_table_show", K(s_priv), K(db), K(routine), K(ret));
  } else {}
  return ret;
}

int ObSchemaChecker::check_trigger_show(const share::schema::ObSessionPrivInfo &s_priv,
                                        const common::ObIArray<uint64_t> &enable_role_id_array,
                                        const ObString &db,
                                        const ObString &trigger,
                                        bool &allow_show,
                                        const ObString &table) const
{
  int ret = OB_SUCCESS;
  allow_show = true;
  if (IS_NOT_INIT) {
    ret = OB_NOT_INIT;
    LOG_WARN("schema checker is not inited", K(is_inited_), K(ret));
  } else if (OB_UNLIKELY(!s_priv.is_valid() || db.empty() || trigger.empty())) {
    ret = OB_INVALID_ARGUMENT;
    LOG_WARN("invalid arguments", K(s_priv), K(db), K(trigger), K(ret));
  } else {
    bool need_check = false;
    if(OB_FAIL(ObCompatControl::check_feature_enable(s_priv.security_version_,
                                      ObCompatFeatureType::MYSQL_TRIGGER_PRIV_CHECK, need_check))) {
      LOG_WARN("failed to check feature enable", K(ret));
    } else if(need_check && lib::is_mysql_mode()) {
      ObNeedPriv need_priv;
      need_priv.priv_level_ = OB_PRIV_TABLE_LEVEL;
      need_priv.db_ = db;
      need_priv.priv_set_ = OB_PRIV_TRIGGER;
      need_priv.table_ = table;
      OZ (schema_mgr_->check_single_table_priv(s_priv, enable_role_id_array, need_priv));
      if(OB_FAIL(ret)) {
        allow_show = false;
        ret = OB_SUCCESS;
        LOG_WARN("show create trigger not has trigger priv", K(s_priv), K(enable_role_id_array), K(db), K(trigger), K(table), K(ret));
      }
    }
  }
  return ret;
}

int ObSchemaChecker::check_table_or_index_exists(const uint64_t tenant_id,
                                                 const uint64_t catalog_id,
                                                 const uint64_t database_id,
                                                 const ObString &table_name,
                                                 const bool with_hidden_flag,
                                                 const bool is_built_in_index,
                                                 bool &is_exist)
{
  int ret = OB_SUCCESS;
  bool is_index_table = false;
  if (IS_NOT_INIT) {
    ret = OB_NOT_INIT;
    LOG_WARN("schema checker is not inited", K(is_inited_), K(ret));
  } else if (OB_UNLIKELY(OB_INVALID_ID == tenant_id || OB_INVALID_ID == catalog_id || OB_INVALID_ID == database_id || table_name.empty())) {
    ret = OB_INVALID_ARGUMENT;
    LOG_WARN("invalid arguments", K(tenant_id), K(catalog_id), K(database_id), K(table_name), K(ret));
  } else if (OB_FAIL(check_table_exists(tenant_id, catalog_id, database_id, table_name, is_index_table, with_hidden_flag, is_exist))) {
    LOG_WARN("check table exist failed", K(tenant_id), K(catalog_id), K(database_id), K(table_name), K(ret));
  } else if (!is_exist) {
    is_index_table = true;
    if (OB_FAIL(check_table_exists(tenant_id,
                                   catalog_id,
                                   database_id,
                                   table_name,
                                   is_index_table,
                                   with_hidden_flag,
                                   is_exist,
                                   is_built_in_index))) {
      LOG_WARN("check index exist failed", K(tenant_id), K(catalog_id), K(database_id), K(table_name), K(ret), K(is_built_in_index));
    }
  }
  return ret;
}

int ObSchemaChecker::check_table_exists(const uint64_t tenant_id,
                                        const uint64_t catalog_id,
                                        const uint64_t database_id,
                                        const ObString &table_name,
                                        const bool is_index_table,
                                        const bool with_hidden_flag,
                                        bool &is_exist,
                                        const bool is_built_in_index)
{
  int ret = OB_SUCCESS;

  is_exist = false;
  uint64_t table_id = OB_INVALID_ID;
  if (IS_NOT_INIT) {
    ret = OB_NOT_INIT;
    LOG_WARN("schema checker is not inited", K(is_inited_), K(ret));
  } else if (OB_UNLIKELY(OB_INVALID_ID == tenant_id || OB_INVALID_ID == catalog_id || OB_INVALID_ID == database_id || table_name.empty())) {
    ret = OB_INVALID_ARGUMENT;
    LOG_WARN("invalid arguments", K(tenant_id), K(catalog_id), K(database_id), K(table_name), K(ret));
  } else {
    if (is_internal_catalog_id(catalog_id)) {
      if (OB_FAIL(schema_mgr_->get_table_id(tenant_id,
                                             database_id,
                                             table_name,
                                             is_index_table,
                                             with_hidden_flag ? ObSchemaGetterGuard::USER_HIDDEN_TABLE_TYPE
                                                              : ObSchemaGetterGuard::ALL_NON_HIDDEN_TYPES,
                                             table_id,
                                             is_built_in_index))) {
        LOG_WARN("get table id failed", K(ret), K(tenant_id), K(database_id), K(table_name), K(is_index_table));
      }
    } else if (is_external_catalog_id(catalog_id)) {
      if (OB_FAIL(sql_schema_mgr_->get_catalog_table_id(tenant_id, catalog_id, database_id, table_name, table_id))) {
        LOG_WARN("get catalog table id failed", K(ret), K(tenant_id), K(catalog_id), K(database_id), K(table_name));
      }
    } else {
      ret = OB_ERR_UNEXPECTED;
      LOG_WARN("unexpected", K(ret));
    }
  }

  if (OB_SUCC(ret)) {
    is_exist = (OB_INVALID_ID != table_id);
    if (is_exist == false) {
      bool exist = false;
      ObNameCaseMode mode = OB_NAME_CASE_INVALID;
      if (OB_FAIL(schema_mgr_->get_tenant_name_case_mode(tenant_id, mode))) {
        LOG_WARN("fail to get name case mode");
      } else if (OB_NAME_CASE_INVALID == mode) {
        ret = OB_ERR_UNEXPECTED;
        LOG_WARN("invalid case mode", K(ret), K(mode));
      }
      if (OB_FAIL(ret)) {
        //do nothing
      } else if (OB_FAIL(find_fake_cte_schema(table_name, mode, exist))) {
        LOG_WARN("can not find the table", K(ret), K(tenant_id), K(database_id), K(table_name), K(is_index_table));
      } else {
        is_exist = exist;
      }
    }
  }
  return ret;
}

int ObSchemaChecker::check_table_exists(const uint64_t tenant_id,
                                        const ObString &database_name,
                                        const ObString &table_name,
                                        const bool is_index_table,
                                        const bool with_hidden_flag,
                                        bool &is_exist,
                                        const bool is_built_in_index,
                                        const uint64_t catalog_id)
{
  int ret = OB_SUCCESS;

  is_exist = false;
  uint64_t table_id= OB_INVALID_ID;
  if (IS_NOT_INIT) {
    ret = OB_NOT_INIT;
    LOG_WARN("schema checker is not inited", K(is_inited_), K(ret));
  } else if (OB_UNLIKELY(OB_INVALID_ID == tenant_id || OB_INVALID_ID == catalog_id || database_name.empty() || table_name.empty())) {
    ret = OB_INVALID_ARGUMENT;
    LOG_WARN("invalid arguments", K(tenant_id), K(catalog_id), K(database_name), K(table_name), K(ret));
  } else {
    if (is_internal_catalog_id(catalog_id)) {
      if (OB_FAIL(schema_mgr_->get_table_id(tenant_id,
                                            database_name,
                                            table_name,
                                            is_index_table,
                                            with_hidden_flag ? ObSchemaGetterGuard::USER_HIDDEN_TABLE_TYPE
                                                             : ObSchemaGetterGuard::ALL_NON_HIDDEN_TYPES,
                                            table_id,
                                            is_built_in_index))) {
        LOG_WARN("fail to check table exist", K(tenant_id), K(database_name), K(table_name), K(is_index_table), K(ret));
      }
    } else if (is_external_catalog_id(catalog_id)) {
      uint64_t database_id = OB_INVALID_ID;
      if (OB_FAIL(sql_schema_mgr_->get_catalog_database_id(tenant_id, catalog_id, database_name, database_id))) {
        LOG_WARN("fail to get catalog database id", K(ret), K(tenant_id), K(catalog_id), K(database_name));
      } else if (OB_FAIL(sql_schema_mgr_->get_catalog_table_id(tenant_id, catalog_id, database_id, table_name, table_id))) {
        LOG_WARN("fail to check catalog table exist", K(ret), K(tenant_id), K(catalog_id), K(database_name), K(database_id));
      }
    } else {
      ret = OB_ERR_UNEXPECTED;
      LOG_WARN("unexpected", K(ret));
    }
  }

  if (OB_SUCC(ret)) {
    is_exist = (OB_INVALID_ID != table_id);
  }
  return ret;
}

// mock_fk_parent_table begin
int ObSchemaChecker::get_mock_fk_parent_table_with_name(
    const uint64_t tenant_id,
    const uint64_t database_id,
    const common::ObString &name,
    const ObMockFKParentTableSchema *&schema)
{
  int ret = OB_SUCCESS;
  if (OB_FAIL(schema_mgr_->get_mock_fk_parent_table_schema_with_name(tenant_id, database_id, name, schema))) {
    LOG_WARN("failed to get mock fk parent table schema", K(ret));
  }
  return ret;
}
// mock_fk_parent_table end

int ObSchemaChecker::get_database_id(const uint64_t tenant_id, const ObString &database_name, uint64_t &database_id) const
{
  return get_database_id(tenant_id, OB_INTERNAL_CATALOG_ID, database_name, database_id);
}

int ObSchemaChecker::get_database_id(const uint64_t tenant_id,
                                     const uint64_t catalog_id,
                                     const ObString &database_name,
                                     uint64_t &database_id) const
{
  int ret = OB_SUCCESS;
  database_id = OB_INVALID_ID;

  if (IS_NOT_INIT) {
    ret = OB_NOT_INIT;
    LOG_WARN("schema checker is not inited", K(is_inited_), K(ret));
  } else if (OB_UNLIKELY(OB_INVALID_ID == tenant_id || database_name.empty())) {
    ret = OB_INVALID_ARGUMENT;
    LOG_WARN("invalid arguments", K(tenant_id), K(database_name), K(ret));
  } else {
    if (is_internal_catalog_id(catalog_id)) {
      if (OB_FAIL(OB_FAIL(schema_mgr_->get_database_id(tenant_id, database_name, database_id)))) {
        LOG_WARN("fail to get database id", K(tenant_id), K(database_name), K(database_id), K(ret));
      }
    } else if (is_external_catalog_id(catalog_id)) {
      if (OB_ISNULL(sql_schema_mgr_)) {
        ret = OB_ERR_UNEXPECTED;
        LOG_WARN("unexpected null", K(ret));
      } else if (OB_FAIL(sql_schema_mgr_->get_catalog_database_id(tenant_id, catalog_id, database_name, database_id))) {
        LOG_WARN("failed to get catalog database id", K(tenant_id), K(catalog_id), K(database_name), K(database_id), K(ret));
      }
    } else {
      ret = OB_ERR_UNEXPECTED;
      LOG_WARN("unexpected", K(ret));
    }
  }

  if (OB_SUCC(ret) && OB_INVALID_ID == database_id) {
    ret = OB_ERR_BAD_DATABASE;
    LOG_WARN("database is not exist", K(tenant_id), K(catalog_id), K(database_name), K(ret));
  }
  return ret;
}

int ObSchemaChecker::get_catalog_id_name(const uint64_t tenant_id,
                                         common::ObString &catalog_name,
                                         uint64_t &catalog_id,
                                         ObIAllocator *allocator,
                                         bool allow_not_exist) const
{
  int ret = OB_SUCCESS;
  catalog_id = OB_INVALID_ID;
  const ObCatalogSchema *catalog_schema = NULL;
  if (IS_NOT_INIT) {
    ret = OB_NOT_INIT;
    LOG_WARN("schema checker is not inited", K(is_inited_), K(ret));
  } else if (OB_ISNULL(schema_mgr_)) {
    ret = OB_ERR_UNEXPECTED;
    LOG_WARN("get unexpected null", K(ret));
  } else if (OB_UNLIKELY(OB_INVALID_ID == tenant_id || catalog_name.empty())) {
    ret = OB_INVALID_ARGUMENT;
    LOG_WARN("invalid arguments", K(tenant_id), K(catalog_name), K(ret));
  } else if (OB_FAIL(schema_mgr_->get_catalog_schema_by_name(tenant_id, catalog_name, catalog_schema))) {
    LOG_WARN("failed to get catalog schema", K(ret));
  } else if (OB_ISNULL(catalog_schema)) {
    if (allow_not_exist) {
      // do nothing
    } else {
      ret = OB_CATALOG_NOT_EXIST;
      LOG_WARN("catalog not exist", K(ret), K(catalog_name));
      LOG_USER_ERROR(OB_CATALOG_NOT_EXIST, catalog_name.length(), catalog_name.ptr());
    }
  } else {
    catalog_id = catalog_schema->get_catalog_id();
    catalog_name = catalog_schema->get_catalog_name_str();
    if (allocator != NULL && OB_FAIL(ob_write_string(*allocator, catalog_name, catalog_name))) {
      LOG_WARN("failed to deep copy str", K(ret));
    }
  }
  return ret;
}

int ObSchemaChecker::get_column_schema(
    const uint64_t tenant_id,
    const uint64_t table_id,
    const ObString &column_name,
    const ObColumnSchemaV2 *&column_schema,
    bool get_hidden,
    bool is_link /* = false */)
{
  int ret = OB_SUCCESS;
  column_schema = NULL;

  const ObColumnSchemaV2 *column = NULL;
  if (IS_NOT_INIT) {
    ret = OB_NOT_INIT;
    LOG_WARN("schema checker is not inited", K(is_inited_), K(ret));
  } else if (OB_UNLIKELY(OB_INVALID_ID == table_id || column_name.empty())) {
    ret = OB_INVALID_ARGUMENT;
    LOG_WARN("invalid arguments", K(table_id), K(column_name), K(ret));
  } else {
    if (OB_FAIL(get_column_schema_inner(tenant_id, table_id, column_name, column, is_link))) {
      LOG_WARN("get column schema failed", K(tenant_id), K(table_id), K(column_name), K(ret));
    } else if (NULL == column) {
      for (int64_t i = 0; i < tmp_cte_schemas_.count(); i++) {
        if (tmp_cte_schemas_.at(i)->get_table_id() == table_id) {
          column = tmp_cte_schemas_.at(i)->get_column_schema(column_name);
          break;
        }
      }
      if (NULL == column) {
        ret = OB_ERR_BAD_FIELD_ERROR;
        LOG_WARN("column is not exist", K(table_id), K(column_name), K(ret));
      } else {
        column_schema = column;
        LOG_DEBUG("find a cte fake column", K(column_name));
      }
    } else if (!get_hidden && column->is_hidden()) {
      ret = OB_ERR_BAD_FIELD_ERROR;
      LOG_INFO("do not get hidden column", K(table_id), K(column_name), K(ret));
    } else {
      column_schema = column;
    }
  }

  return ret;
}

int ObSchemaChecker::get_column_schema(
    const uint64_t tenant_id,
    const uint64_t table_id,
    const uint64_t column_id,
    const ObColumnSchemaV2 *&column_schema,
    const bool get_hidden,
    bool is_link /* = false*/)
{
  int ret = OB_SUCCESS;
  column_schema = NULL;

  const ObColumnSchemaV2 *column = NULL;
  if (IS_NOT_INIT) {
    ret = OB_NOT_INIT;
    LOG_WARN("schema checker is not inited", K(is_inited_), K(ret));
  } else if (OB_UNLIKELY(OB_INVALID_ID == table_id || OB_INVALID_ID == column_id)) {
    ret = OB_INVALID_ARGUMENT;
    LOG_WARN("invalid arguments", K(table_id), K(column_id), K(ret));
  } else if (OB_FAIL(get_column_schema_inner(tenant_id, table_id, column_id, column, is_link))) {
    LOG_WARN("get column schema failed", K(tenant_id), K(table_id), K(column_id), K(ret));
  } else if (NULL == column) {
    ret = OB_ERR_BAD_FIELD_ERROR;
    LOG_WARN("column is not exist", K(table_id), K(column_id), K(ret));
  } else if (!get_hidden && column->is_hidden()) {
    ret = OB_ERR_BAD_FIELD_ERROR;
    LOG_INFO("do not get hidden column", K(table_id), K(column_id), K(ret));
  } else {
    column_schema = column;
  }

  return ret;
}

int ObSchemaChecker::get_user_id(const uint64_t tenant_id,
                                 const ObString &user_name,
                                 const ObString &host_name,
                                 uint64_t &user_id)
{
  int ret = OB_SUCCESS;
  user_id = OB_INVALID_ID;
  if (IS_NOT_INIT) {
    ret = OB_NOT_INIT;
    LOG_WARN("schema checker is not inited", K(is_inited_), K(ret));
  } else if (OB_UNLIKELY(OB_INVALID_ID == tenant_id || user_name.empty())) {
    ret = OB_INVALID_ARGUMENT;
    LOG_WARN("invalid arguments", K(tenant_id), K(user_name), K(ret));
  } else if (OB_FAIL(schema_mgr_->get_user_id(tenant_id, user_name, host_name, user_id))) {
    LOG_WARN("get user id failed", K(tenant_id), K(user_name), K(host_name), K(ret));
  } else if (OB_INVALID_ID == user_id) {
    ret = OB_USER_NOT_EXIST;
    LOG_WARN("user is not exist", K(tenant_id), K(user_name), K(host_name), K(ret));
  }
  return ret;
}

int ObSchemaChecker::get_user_info(
    const uint64_t tenant_id,
    const uint64_t user_id,
    const ObUserInfo *&user_info)
{
  int ret = OB_SUCCESS;
  if (IS_NOT_INIT) {
    ret = OB_NOT_INIT;
    LOG_WARN("schema checker is not inited", K(is_inited_), K(ret));
  } else if (OB_INVALID_ID == user_id) {
    ret = OB_USER_NOT_EXIST;
    LOG_WARN("user is not exist", K(user_id), K(ret));
  } else if (OB_FAIL(schema_mgr_->get_user_info(tenant_id, user_id, user_info))) {
    LOG_WARN("user is not exist", K(tenant_id), K(user_id), K(ret));
  } else if (NULL == user_info) {
    ret = OB_USER_NOT_EXIST;
    LOG_WARN("user is not exist", K(user_id), K(ret));
  }

  return ret;
}

int ObSchemaChecker::get_user_info(const uint64_t tenant_id,
                                 const ObString &user_name,
                                 const ObString &host_name,
                                 const ObUserInfo *&user_info)
{
  int ret = OB_SUCCESS;
  uint64_t user_id = OB_INVALID_ID;
  if (IS_NOT_INIT) {
    ret = OB_NOT_INIT;
    LOG_WARN("schema checker is not inited", K(is_inited_), K(ret));
  } else if (OB_UNLIKELY(OB_INVALID_ID == tenant_id)) {
    ret = OB_INVALID_ARGUMENT;
    LOG_WARN("invalid arguments", K(tenant_id), K(user_name), K(ret));
  } else if (OB_UNLIKELY(user_name.empty())) {
    ret = OB_USER_NOT_EXIST;
    LOG_WARN("user is not exist", K(tenant_id), K(user_name), K(host_name), K(ret));
  } else if (OB_FAIL(schema_mgr_->get_user_id(tenant_id, user_name, host_name, user_id))) {
    LOG_WARN("get user id failed", K(tenant_id), K(user_name), K(host_name), K(ret));
  } else if (OB_FAIL(get_user_info(tenant_id, user_id, user_info))) {
    LOG_WARN("user is not exist", K(tenant_id), K(user_id), K(user_name), K(host_name), K(ret));
  } else if (NULL == user_info) {
    ret = OB_USER_NOT_EXIST;
    LOG_WARN("user is not exist", K(tenant_id), K(user_name), K(host_name), K(ret));
  }

  return ret;
}

int ObSchemaChecker::get_table_schema_with_synonym(const uint64_t tenant_id,
                                                   const ObString &tbl_db_name,
                                                   const ObString &tbl_name,
                                                   bool is_index_table,
                                                   bool &has_synonym,
                                                   ObString &new_db_name,
                                                   ObString &new_tbl_name,
                                                   const share::schema::ObTableSchema *&tbl_schema)
{
  // synonym has been drop in lite version
  int ret = OB_SUCCESS;
  uint64_t tbl_db_id = OB_INVALID_ID;
  uint64_t obj_db_id = OB_INVALID_ID;
  ObString obj_name;
  new_db_name.reset();
  new_tbl_name.reset();
  has_synonym = false;
  tbl_schema = NULL;
  ObSEArray<uint64_t, 8> syn_id_arr;

  if (OB_FAIL(get_table_schema(tenant_id, tbl_db_name, tbl_name, is_index_table, tbl_schema))) {
      LOG_WARN("get_table_schema failed", K(ret), K(tenant_id), K(tbl_db_name), K(tbl_db_id), K(tbl_name));
  } else {
    has_synonym = false;
  }

  return ret;
}


int ObSchemaChecker::get_table_schema(const uint64_t tenant_id, const ObString &database_name,
                                      const ObString &table_name, const bool is_index_table,
                                      const ObTableSchema *&table_schema, const bool with_hidden_flag,
                                      const bool is_built_in_index)
{
  int ret = OB_SUCCESS;
  table_schema = NULL;

  const ObTableSchema *table = NULL;
  ObCStringHelper helper;
  if (IS_NOT_INIT) {
    ret = OB_NOT_INIT;
    LOG_WARN("schema checker is not inited", K(is_inited_), K(ret));
  } else if (OB_UNLIKELY(OB_INVALID_ID == tenant_id || database_name.empty() || table_name.empty())) {
    ret = OB_INVALID_ARGUMENT;
    LOG_WARN("invalid arguments", K(tenant_id), K(database_name), K(table_name), K(ret));
  } else if (OB_FAIL(schema_mgr_->get_table_schema(tenant_id, database_name, table_name,
                                            is_index_table, table, with_hidden_flag, is_built_in_index))) {
    LOG_WARN("get table schema failed", K(tenant_id), K(database_name), K(table_name), K(ret));
  } else if (NULL == table) {
    ret = OB_TABLE_NOT_EXIST;
    LOG_WARN("table is not exist", K(tenant_id), K(database_name), K(table_name),
        K(ret));
  } else if (false == table->is_tmp_table()
             && 0 != table->get_session_id()
             && OB_INVALID_ID != schema_mgr_->get_session_id()) {
    ret = OB_TABLE_NOT_EXIST;
    LOG_USER_ERROR(OB_TABLE_NOT_EXIST, helper.convert(database_name), helper.convert(table_name));
  } else if (table->is_materialized_view() && !(table->mv_available())) {
    ret = OB_TABLE_NOT_EXIST;
    LOG_USER_ERROR(OB_TABLE_NOT_EXIST, helper.convert(database_name), helper.convert(table_name));
  } else {
    table_schema = table;
  }
  return ret;
}
// Note: this function can only be used in the sql layer
int ObSchemaChecker::get_table_schema(const uint64_t tenant_id,
                                      const uint64_t catalog_id,
                                      const uint64_t database_id,
                                      const ObString &table_name,
                                      const bool is_index_table,
                                      const bool cte_table_fisrt,
                                      const bool with_hidden_flag,
                                      const ObTableSchema *&table_schema,
                                      const bool is_built_in_index /*= false*/)
{
  int ret = OB_SUCCESS;
  table_schema = NULL;

  const ObTableSchema *table = NULL;
  if (IS_NOT_INIT) {
    ret = OB_NOT_INIT;
    LOG_WARN("schema checker is not inited", K(is_inited_), K(ret));
  } else if (OB_UNLIKELY(OB_INVALID_ID == tenant_id || OB_INVALID_ID == database_id
             || table_name.empty())) {
    ret = OB_INVALID_ARGUMENT;
    LOG_WARN("invalid arguments", K(tenant_id), K(database_id), K(table_name), K(ret));
    ret = OB_INVALID_ARGUMENT;
  } else {
    if (is_internal_catalog_id(catalog_id)) {
      if (OB_FAIL(schema_mgr_->get_table_schema(tenant_id, database_id, table_name, is_index_table, table, with_hidden_flag, is_built_in_index))) {
        LOG_WARN("get table schema failed",
             K(tenant_id),
             K(database_id),
             K(table_name),
             K(with_hidden_flag),
             K(is_built_in_index),
             K(is_index_table),
             K(ret));
      }
    } else if (is_external_catalog_id(catalog_id)) {
      if (OB_FAIL(sql_schema_mgr_->get_catalog_table_schema(tenant_id, catalog_id, database_id, table_name, table))) {
        LOG_WARN("get catalog table schema failed", K(tenant_id), K(catalog_id), K(database_id), K(table_name), K(ret));
      }
    } else {
      ret = OB_ERR_UNEXPECTED;
    }
  }

  if (OB_SUCC(ret)) {
    // It is also possible that the temporary CTE recursive table schema conflicts with an existing table,
    // At this point, the cte recursive table schema must take precedence (same with oracle)
    // If found in fake schema, then override the previously found base table
    if (cte_table_fisrt) {
      ObNameCaseMode mode = OB_NAME_CASE_INVALID;
      if (OB_FAIL(schema_mgr_->get_tenant_name_case_mode(tenant_id, mode))) {
        LOG_WARN("fail to get name case mode");
      } else if (OB_NAME_CASE_INVALID == mode) {
        ret = OB_ERR_UNEXPECTED;
        LOG_WARN("invalid case mode", K(ret), K(mode));
      }
      if (OB_SUCC(ret)) {
        for (int64_t i = 0; i < tmp_cte_schemas_.count(); i++) {
          if (ObCharset::case_mode_equal(mode, tmp_cte_schemas_.at(i)->get_table_name_str(), table_name)) {
            table = tmp_cte_schemas_.at(i);
            break;
          }
        }
      }
    }
    if (NULL == table) {
      ret = OB_TABLE_NOT_EXIST;
      LOG_WARN("table is not exist", K(tenant_id), K(database_id), K(table_name), K(ret));
     } else if (false == table->is_tmp_table()
                && 0 != table->get_session_id()
                && OB_INVALID_ID != schema_mgr_->get_session_id()
                && table->get_session_id() != schema_mgr_->get_session_id()) {
      const ObDatabaseSchema  *db_schema = NULL;
      if (OB_FAIL(schema_mgr_->get_database_schema(tenant_id, database_id, db_schema))) {
        LOG_WARN("get database schema failed", K(tenant_id), K(database_id), K(ret));
      } else if (NULL == db_schema) {
        ret = OB_ERR_BAD_DATABASE;
        LOG_WARN("fail to get database schema", K(tenant_id), K(database_id), K(ret));
      } else {
        ret = OB_TABLE_NOT_EXIST;
        ObCStringHelper helper;
        LOG_USER_ERROR(OB_TABLE_NOT_EXIST, db_schema->get_database_name(),
            helper.convert(table_name));
      }
    } else if (table->is_materialized_view() && !(table->mv_available())) {
      const ObDatabaseSchema *db_schema = NULL;
      if (OB_FAIL(schema_mgr_->get_database_schema(tenant_id, database_id, db_schema))) {
        LOG_WARN("get database schema failed", K(tenant_id), K(database_id), K(ret));
      } else if (NULL == db_schema) {
        ret = OB_ERR_BAD_DATABASE;
        LOG_WARN("fail to get database schema", K(tenant_id), K(database_id), K(ret));
      } else {
        ret = OB_TABLE_NOT_EXIST;
        ObCStringHelper helper;
        LOG_USER_ERROR(OB_TABLE_NOT_EXIST, db_schema->get_database_name(),
                       helper.convert(table_name));
      }
    } else {
      table_schema = table;
    }
  }
  return ret;
}
// Note: this function can only be used in the sql layer
int ObSchemaChecker::get_table_schema(const uint64_t tenant_id,
                                      const uint64_t database_id,
                                      const ObString &table_name,
                                      const bool is_index_table,
                                      const bool cte_table_fisrt,
                                      const bool with_hidden_flag,
                                      const ObTableSchema *&table_schema,
                                      const bool is_built_in_index /*= false*/)
{
  return get_table_schema(tenant_id,
                          OB_INTERNAL_CATALOG_ID,
                          database_id,
                          table_name,
                          is_index_table,
                          cte_table_fisrt,
                          with_hidden_flag,
                          table_schema,
                          is_built_in_index);
}
// Note: this function can only be used in the sql layer
// tmp_cte_schemas_ is only maintained in resolver's SchemaChecker
// Transformer's SchemaChecker doesn't have tmp_cte_schemas.
int ObSchemaChecker::get_table_schema(const uint64_t tenant_id, const uint64_t table_id,
                                      const ObTableSchema *&table_schema,
                                      bool is_link /* = false */) const
{
  int ret = OB_SUCCESS;
  table_schema = NULL;
  const ObTableSchema *table = NULL;
  if (IS_NOT_INIT) {
    ret = OB_NOT_INIT;
    LOG_WARN("schema checker is not inited", K(is_inited_), K(ret));
  } else if (OB_UNLIKELY(OB_INVALID_ID == table_id)) {
    ret = OB_INVALID_ARGUMENT;
    LOG_WARN("invalid arguments", K(table_id), K(ret), K(lbt()));
  } else if (!is_link && OB_FAIL(get_table_schema_inner(tenant_id, table_id, table))) {
    LOG_WARN("get table schema failed", K(tenant_id), K(table_id), K(ret));
  } else if (NULL == table) {
    // It could also be a temporary cte recursive table schema
    for (int64_t i = 0; i < tmp_cte_schemas_.count(); i++) {
      if (tmp_cte_schemas_.at(i)->get_table_id() == table_id) {
        table = tmp_cte_schemas_.at(i);
        break;
      }
    }
    if (NULL == table) {
      ret = OB_TABLE_NOT_EXIST;
      LOG_WARN("table is not exist", K(table_id));
    } else {
      table_schema = table;
    }
  } else {
    table_schema = table;
  }
  return ret;
}

int ObSchemaChecker::check_if_partition_key(const uint64_t tenant_id, uint64_t table_id, uint64_t column_id, bool &is_part_key, bool is_link /* = false*/) const
{
  int ret = OB_SUCCESS;
  is_part_key = false;
  const ObTableSchema *tbl_schema = NULL;
  if (!is_link) {
    if (OB_FAIL(get_table_schema(tenant_id, table_id, tbl_schema))) {
      LOG_WARN("get table schema failed", K(ret), K(table_id));
    } else if (tbl_schema->is_partitioned_table()) {
      if (OB_FAIL(tbl_schema->get_partition_key_info().is_rowkey_column(column_id, is_part_key))) {
        LOG_WARN("check if the column_id is partition key failed", K(ret), K(table_id), K(column_id));
      } else if (!is_part_key && PARTITION_LEVEL_TWO == tbl_schema->get_part_level()) {
        if (OB_FAIL(tbl_schema->get_subpartition_key_info().is_rowkey_column(column_id, is_part_key))) {
          LOG_WARN("check if the column_id is subpartition key failed", K(ret), K(table_id), K(column_id));
        }
      }
    }
  }
  return ret;
}

int ObSchemaChecker::get_can_read_index_array(
    const uint64_t tenant_id,
    uint64_t table_id,
    uint64_t *index_tid_array,
    int64_t &size,
    bool with_mv) const
{
  int ret = OB_SUCCESS;
  if (IS_NOT_INIT) {
    ret = OB_NOT_INIT;
    LOG_WARN("schema checker is not inited", K(is_inited_), K(ret));
  } else if (OB_UNLIKELY(OB_INVALID_ID == table_id || size <= 0) || OB_ISNULL(index_tid_array)) {
    ret = OB_INVALID_ARGUMENT;
    LOG_WARN("invalid arguments", K(table_id), K(size), K(index_tid_array), K(ret));
  } else if (OB_NOT_NULL(sql_schema_mgr_)) {
    if (OB_FAIL(sql_schema_mgr_->get_can_read_index_array(
                table_id, index_tid_array, size, with_mv,
                true /* with_global_index*/, true /* with_domin_index*/, false /* with_spatial_index*/))) {
      LOG_WARN("failed to get_can_read_index_array", K(table_id), K(ret));
    }
  } else {
    if (OB_FAIL(schema_mgr_->get_can_read_index_array(
        tenant_id, table_id, index_tid_array, size, with_mv))) {
      LOG_WARN("failed to get_can_read_index_array", K(tenant_id), K(table_id), K(ret));
    }
  }
  return ret;
}

int ObSchemaChecker::get_can_write_index_array(const uint64_t tenant_id,
                                               uint64_t table_id,
                                               uint64_t *index_tid_array,
                                               int64_t &size,
                                               bool only_global,
                                               bool with_mlog) const
{
  int ret = OB_SUCCESS;
  if (IS_NOT_INIT) {
    ret = OB_NOT_INIT;
    LOG_WARN("schema checker is not inited", K(is_inited_), K(ret));
  } else if (OB_UNLIKELY(OB_INVALID_ID == table_id || size <= 0) || OB_ISNULL(index_tid_array)) {
    ret = OB_INVALID_ARGUMENT;
    LOG_WARN("invalid arguments", K(table_id), K(size), K(index_tid_array), K(ret));
  } else if (OB_FAIL(schema_mgr_->get_can_write_index_array(tenant_id, table_id, index_tid_array, size, only_global, with_mlog))) {
    LOG_WARN("failed to get_can_write_index_array", K(tenant_id), K(table_id), K(ret));
  } else {}
  return ret;
}

int ObSchemaChecker::get_tenant_id(const ObString &tenant_name, uint64_t &tenant_id)
{
  int ret = OB_SUCCESS;

  if (IS_NOT_INIT) {
    ret = OB_NOT_INIT;
    LOG_WARN("schema checker is not inited", K(is_inited_), K(ret));
  } else if (OB_UNLIKELY(tenant_name.empty())) {
    ret = OB_INVALID_ARGUMENT;
    LOG_WARN("invalid tenant_name", K(tenant_name), K(ret));
  } else if (OB_FAIL(schema_mgr_->get_tenant_id(tenant_name, tenant_id))) {
    LOG_WARN("get tenant id failed", K(tenant_name), K(ret));
  } else if (OB_INVALID_ID == tenant_id) {
    ret = OB_TENANT_NOT_EXIST;
    LOG_WARN("tenant is not exist", K(tenant_name), K(ret));
  }
  return ret;
}

int ObSchemaChecker::get_tenant_info(const uint64_t &tenant_id,
                                      const ObTenantSchema *&tenant_schema)
{
  int ret = OB_SUCCESS;
  tenant_schema = NULL;

  const ObTenantSchema *tenant = NULL;
  if (IS_NOT_INIT) {
    ret = OB_NOT_INIT;
    LOG_WARN("schema checker is not inited", K(is_inited_), K(ret));
  } else if (OB_UNLIKELY(OB_INVALID_ID == tenant_id)) {
    ret = OB_INVALID_ARGUMENT;
    LOG_WARN("invalid tenant_id", K(tenant_id), K(ret));
  } else if (OB_FAIL(schema_mgr_->get_tenant_info(tenant_id, tenant))) {
    LOG_WARN("get tenant schema failed", K(ret), K(tenant_id));
  } else if (NULL == tenant) {
    ret = OB_TENANT_NOT_EXIST;
    LOG_WARN("tenant is not exist", K(tenant_id), K(ret));
  } else {
    tenant_schema = tenant;
  }
  return ret;
}


int ObSchemaChecker::get_database_schema(const uint64_t tenant_id,
                                         const uint64_t database_id,
                                         const ObDatabaseSchema *&database_schema)
{
  int ret = OB_SUCCESS;
  database_schema = NULL;
  const ObDatabaseSchema *database = NULL;
  if (IS_NOT_INIT) {
    ret = OB_NOT_INIT;
    LOG_WARN("schema checker is not inited", K(is_inited_), K(ret));
  } else if (OB_UNLIKELY(OB_INVALID_ID == tenant_id || OB_INVALID_ID == database_id)) {
    ret = OB_INVALID_ARGUMENT;
    LOG_WARN("invalid arguments", K(tenant_id), K(database_id), K(ret));
  } else if (is_external_object_id(database_id)) {
    // fetch database schema from ObSqlSchemaGuard
    if (OB_ISNULL(sql_schema_mgr_)) {
      ret = OB_ERR_UNEXPECTED;
      LOG_WARN("unexpected null", K(ret));
    } else if (OB_FAIL(sql_schema_mgr_->get_database_schema(database_id, database))) {
      LOG_WARN("get catalog's database schema failed", K(ret));
    }
  } else {
    // follow original logic
    if (OB_FAIL(schema_mgr_->get_database_schema(tenant_id, database_id, database))) {
      LOG_WARN("get database schema failed", K(tenant_id), K(database_id), K(ret));
    }
  }

  if (OB_SUCC(ret)) {
    if (NULL == database) {
      ret = OB_ERR_BAD_DATABASE;
      LOG_WARN("fail to get database schema", K(tenant_id), K(database_id), K(ret));
    } else {
      database_schema = database;
    }
  }
  return ret;
}



int ObSchemaChecker::check_column_has_index(const uint64_t tenant_id, uint64_t table_id, uint64_t column_id, bool &has_index, bool is_link /* = false */)
{
  int ret = OB_SUCCESS;
  const ObColumnSchemaV2 *col_schema = NULL;
  uint64_t index_tid_array[OB_MAX_AUX_TABLE_PER_MAIN_TABLE];
  int64_t index_cnt = OB_MAX_AUX_TABLE_PER_MAIN_TABLE;

  has_index = false;
  if (IS_NOT_INIT) {
    ret = OB_NOT_INIT;
    LOG_WARN("schema checker is not inited", K(is_inited_), K(ret));
  } else if (OB_FAIL(get_can_read_index_array(tenant_id, table_id, index_tid_array, index_cnt, true))) {
    LOG_WARN("get table schema failed", K(tenant_id), K(table_id));
  }
  for (int64_t i = 0; OB_SUCC(ret) && !has_index && i < index_cnt; ++i) {
    if (OB_FAIL(get_column_schema_inner(tenant_id, index_tid_array[i], column_id, col_schema, is_link))) {
      LOG_WARN("get column schema failed", K(ret), K(tenant_id), K(index_tid_array[i]), K(column_id));
    } else if (col_schema != NULL && col_schema->is_index_column()) {
      has_index = true;
    }
  }
  return ret;
}

int ObSchemaChecker::get_routine_info(
    const uint64_t tenant_id,
    const uint64_t routine_id,
    const share::schema::ObRoutineInfo *&routine_info)
{
  int ret = OB_SUCCESS;
  if (IS_NOT_INIT) {
    ret = OB_NOT_INIT;
    LOG_WARN("schema checker is not inited", K_(is_inited));
  } else if (OB_FAIL(schema_mgr_->get_routine_info(tenant_id, routine_id, routine_info))) {
    LOG_WARN("get routine info failed", K(ret), K(tenant_id));
  }
  return ret;
}

int ObSchemaChecker::get_standalone_procedure_info(const uint64_t tenant_id,
                                                const uint64_t db_id,
                                                const ObString &routine_name,
                                                const share::schema::ObRoutineInfo *&routine_info)
{
  int ret = OB_SUCCESS;
  if (IS_NOT_INIT) {
    ret = OB_NOT_INIT;
    LOG_WARN("schema checker is not inited", K_(is_inited));
  } else if (OB_FAIL(schema_mgr_->get_standalone_procedure_info(
                                    tenant_id, db_id, routine_name, routine_info))) {
    LOG_WARN("get routine info failed", K(ret), K(tenant_id), K(routine_name));
  }
  return ret;
}

int ObSchemaChecker::get_standalone_procedure_info(const uint64_t tenant_id,
                                                   const ObString &database_name,
                                                   const ObString &routine_name,
                                                   const share::schema::ObRoutineInfo *&routine_info)
{
  int ret = OB_SUCCESS;
  uint64_t db_id = OB_INVALID_ID;
  if (IS_NOT_INIT) {
    ret = OB_NOT_INIT;
    LOG_WARN("schema checker is not inited", K_(is_inited));
  } else if (OB_FAIL(get_database_id(tenant_id, database_name, db_id))) {
    LOG_WARN("get database id failed", K(ret), K(tenant_id), K(database_name), K(routine_name));
  } else if (OB_FAIL(schema_mgr_->get_standalone_procedure_info(tenant_id, db_id, routine_name, routine_info))) {
    LOG_WARN("get routine info failed", K(ret), K(tenant_id), K(database_name), K(routine_name));
  }
  return ret;
}

int ObSchemaChecker::get_standalone_function_info(const uint64_t tenant_id,
                                                  const uint64_t db_id,
                                                  const ObString &routine_name,
                                                  const share::schema::ObRoutineInfo *&routine_info)
{
  int ret = OB_SUCCESS;
  if (IS_NOT_INIT) {
    ret = OB_NOT_INIT;
    LOG_WARN("schema checker is not inited", K_(is_inited));
  } else if (OB_FAIL(schema_mgr_->get_standalone_function_info(
                                    tenant_id, db_id, routine_name, routine_info))) {
    LOG_WARN("get routine info failed", K(ret), K(tenant_id), K(db_id), K(routine_name));
  }
  return ret;
}

int ObSchemaChecker::get_standalone_function_info(const uint64_t tenant_id,
                                                  const ObString &database_name,
                                                  const ObString &routine_name,
                                                  const share::schema::ObRoutineInfo *&routine_info)
{
  int ret = OB_SUCCESS;
  uint64_t db_id = OB_INVALID_ID;
  if (IS_NOT_INIT) {
    ret = OB_NOT_INIT;
    LOG_WARN("schema checker is not inited", K_(is_inited));
  } else if (OB_FAIL(get_database_id(tenant_id, database_name, db_id))) {
    LOG_WARN("get database id failed", K(ret), K(tenant_id), K(database_name), K(routine_name));
  } else if (OB_FAIL(schema_mgr_->get_standalone_function_info(tenant_id, db_id, routine_name, routine_info))) {
    LOG_WARN("get routine info failed", K(ret), K(tenant_id), K(database_name), K(routine_name));
  }
  return ret;
}

int ObSchemaChecker::get_package_routine_infos(
  const uint64_t tenant_id, const uint64_t package_id, const uint64_t db_id,
  const common::ObString &routine_name,
  const ObRoutineType routine_type,
  common::ObIArray<const share::schema::ObIRoutineInfo *> &routine_infos)
{
  int ret = OB_SUCCESS;
  if (IS_NOT_INIT) {
    ret = OB_NOT_INIT;
    LOG_WARN("schema checker is not inited", K_(is_inited));
  } else if (OB_FAIL(schema_mgr_->get_package_routine_infos(tenant_id, db_id, package_id,
        routine_name, routine_type, routine_infos))) {
    LOG_WARN("get routine infos failed",
        K(ret), K(tenant_id), K(package_id), K(db_id), K(routine_name), K(routine_type));
  }
  return ret;
}

int ObSchemaChecker::get_package_routine_infos(
    const uint64_t tenant_id, const uint64_t package_id,
    const common::ObString &database_name,
    const common::ObString &routine_name,
    const ObRoutineType routine_type,
    common::ObIArray<const share::schema::ObIRoutineInfo *> &routine_infos)
{
  int ret = OB_SUCCESS;
  uint64_t db_id = OB_INVALID_ID;
  if (IS_NOT_INIT) {
    ret = OB_NOT_INIT;
    LOG_WARN("schema checker is not inited", K_(is_inited));
  } else if (OB_FAIL(get_database_id(tenant_id, database_name, db_id))) {
    LOG_WARN("get database id failed", K(ret));
  } else if (OB_FAIL(schema_mgr_->get_package_routine_infos(tenant_id, db_id, package_id,
        routine_name, routine_type, routine_infos))) {
    LOG_WARN("get routine infos failed",
        K(ret), K(tenant_id), K(package_id), K(database_name), K(routine_name), K(routine_type));
  }
  return ret;
}

int ObSchemaChecker::get_package_info(const uint64_t tenant_id,
                                      const ObString &database_name,
                                      const ObString &package_name,
                                      const share::schema::ObPackageType type,
                                      const int64_t compatible_mode,
                                      const ObPackageInfo *&package_info)
{
  int ret = OB_SUCCESS;
  uint64_t db_id = OB_INVALID_ID;
  if (IS_NOT_INIT) {
    ret = OB_NOT_INIT;
    LOG_WARN("schema checker is not inited", K_(is_inited));
  } else if (OB_FAIL(get_database_id(tenant_id, database_name, db_id))) {
    LOG_WARN("get database id failed", K(ret));
  } else if (OB_FAIL(schema_mgr_->get_package_info(tenant_id, db_id, package_name,
                                              type, compatible_mode, package_info))) {
    LOG_WARN("get package id failed", K(ret));
  } else if (OB_ISNULL(package_info)) {
    ret = OB_ERR_PACKAGE_DOSE_NOT_EXIST;
    LOG_WARN("package is not exist", K(tenant_id), K(database_name), K(package_name), K(ret));
  }
  return ret;
}

int ObSchemaChecker::get_trigger_info(const uint64_t tenant_id,
                                      const common::ObString &database_name,
                                      const common::ObString &tg_name,
                                      const share::schema::ObTriggerInfo *&tg_info)
{
  int ret = OB_SUCCESS;
  uint64_t db_id = OB_INVALID_ID;
  if (IS_NOT_INIT) {
    ret = OB_NOT_INIT;
    LOG_WARN("schema checker is not inited", K_(is_inited));
  } else if (OB_FAIL(get_database_id(tenant_id, database_name, db_id))) {
    LOG_WARN("get database id failed", K(ret), K(tenant_id), K(database_name), K(tg_name));
  } else if (OB_FAIL(schema_mgr_->get_trigger_info(tenant_id, db_id, tg_name, tg_info))) {
    LOG_WARN("get trigger info failed", K(ret), K(tenant_id), K(database_name), K(tg_name));
  }
  return ret;
}

int ObSchemaChecker::get_package_id(const uint64_t tenant_id,
                                    const uint64_t database_id,
                                    const ObString &package_name,
                                    const int64_t compatible_mode,
                                    uint64_t &package_id)
{
  int ret = OB_SUCCESS;
  if (IS_NOT_INIT) {
    ret = OB_NOT_INIT;
    LOG_WARN("schema checker is not inited", K_(is_inited));
  } else if (OB_FAIL(schema_mgr_->get_package_id(
                      tenant_id, database_id, package_name, share::schema::PACKAGE_TYPE, compatible_mode, package_id))) {
    LOG_WARN("get package id failed", K(ret));
  } else if (OB_INVALID_ID == package_id) {
    ret = OB_ERR_PACKAGE_DOSE_NOT_EXIST;
    LOG_WARN("package is not exist", K(tenant_id), K(database_id), K(package_name), K(ret));
  }
  return ret;
}

int ObSchemaChecker::get_package_id(const uint64_t tenant_id,
                                    const ObString &database_name,
                                    const ObString &package_name,
                                    const int64_t compatible_mode,
                                    uint64_t &package_id)
{
  int ret = OB_SUCCESS;
  uint64_t db_id = OB_INVALID_ID;
  if (IS_NOT_INIT) {
    ret = OB_NOT_INIT;
    LOG_WARN("schema checker is not inited", K_(is_inited));
  } else if (OB_FAIL(get_database_id(tenant_id, database_name, db_id))) {
    LOG_WARN("get database id failed", K(ret));
  } else if (OB_FAIL(schema_mgr_->get_package_id(tenant_id, db_id, package_name, share::schema::PACKAGE_TYPE, compatible_mode, package_id))) {
    LOG_WARN("get package id failed", K(ret));
  } else if (OB_INVALID_ID == package_id) {
    ret = OB_ERR_PACKAGE_DOSE_NOT_EXIST;
    LOG_WARN("package is not exist", K(tenant_id), K(database_name), K(package_name), K(ret));
  }
  return ret;
}

int ObSchemaChecker::get_routine_id(const uint64_t tenant_id,
                                    const ObString &database_name,
                                    const ObString &routine_name,
                                    uint64_t &routine_id,
                                    bool &is_proc)
{
  int ret = OB_SUCCESS;
  const share::schema::ObRoutineInfo *routine_info = NULL;
  if (IS_NOT_INIT) {
    ret = OB_NOT_INIT;
    LOG_WARN("schema checker is not inited", K_(is_inited));
  } else if (OB_FAIL(get_standalone_procedure_info(tenant_id,
                                                   database_name,
                                                   routine_name,
                                                   routine_info))) {
    LOG_WARN("get standalone procedure failed", K(ret));
  } else if (routine_info == NULL) {
    if (OB_FAIL(get_standalone_function_info(tenant_id,
                                             database_name,
                                             routine_name,
                                             routine_info))) {
      LOG_WARN("get standalone function failed", K(ret));
    } else if (routine_info == NULL) {
      ret = OB_ERR_SP_DOES_NOT_EXIST;
      LOG_WARN("routine is not exist", K(tenant_id), K(database_name), K(routine_name), K(ret));
    } else {
      routine_id = routine_info->get_routine_id();
      is_proc = false;
    }
  } else {
    is_proc = true;
    routine_id = routine_info->get_routine_id();
  }
  return ret;
}





int ObSchemaChecker::add_fake_cte_schema(share::schema::ObTableSchema* tbl_schema)
{
  int ret = OB_SUCCESS;
  bool dup_schame = false;
  for (int64_t i = 0; OB_SUCC(ret) && i < tmp_cte_schemas_.count(); i++) {
    if (tbl_schema->get_table_name() == tmp_cte_schemas_.at(i)->get_table_name()) {
      dup_schame = true;
    }
  }
  if (!dup_schame) {
    if (OB_FAIL(tmp_cte_schemas_.push_back(tbl_schema))) {
      LOG_WARN("push back cte schema failed");
    }
  }
  return ret;
}

int ObSchemaChecker::find_fake_cte_schema(common::ObString tblname, ObNameCaseMode mode, bool& exist)
{
  int ret = OB_SUCCESS;
  exist = false;
  for (int64_t i = 0; OB_SUCC(ret) && i < tmp_cte_schemas_.count(); ++i) {
    if (ObCharset::case_mode_equal(mode, tmp_cte_schemas_.at(i)->get_table_name_str(), tblname)) {
      exist = true;
      break;
    }
  }
  return ret;
}


int ObSchemaChecker::get_schema_version(const uint64_t tenant_id, uint64_t table_id, share::schema::ObSchemaType schema_type, int64_t &schema_version)
{
  int ret = OB_SUCCESS;
  if (OB_ISNULL(schema_mgr_)) {
    ret = OB_NOT_INIT;
    LOG_WARN("schema mgr is null");
  } else {
    ret = schema_mgr_->get_schema_version(schema_type, tenant_id, table_id, schema_version);
  }
  return ret;
}

int ObSchemaChecker::get_tablegroup_schema(const int64_t tenant_id, const ObString &tablegroup_name, const ObTablegroupSchema *&tablegroup_schema)
{
  int ret = OB_SUCCESS;
  uint64_t tablegroup_id = OB_INVALID_ID;
  if (OB_INVALID_ID == tenant_id || tablegroup_name.empty()) {
    ret = OB_INVALID_ARGUMENT;
    LOG_WARN("invalid argument", K(ret), K(tenant_id), K(tablegroup_name));
  } else if (OB_ISNULL(schema_mgr_)) {
    ret = OB_NOT_INIT;
    LOG_WARN("invalid argument", K(ret), K(schema_mgr_));
  } else if (OB_FAIL(schema_mgr_->get_tablegroup_id(tenant_id, tablegroup_name, tablegroup_id))) {
    LOG_WARN("fail to get tablegroup id", K(ret), K(tenant_id), K(tablegroup_name));
  } else if (OB_INVALID_ID == tablegroup_id) {
    ret = OB_TABLEGROUP_NOT_EXIST;
    LOG_WARN("table group not exist", K(ret), K(tablegroup_id));
  } else if (OB_FAIL(schema_mgr_->get_tablegroup_schema(tenant_id, tablegroup_id, tablegroup_schema))) {
    LOG_WARN("fail to get tablegroup schema", K(ret), K(tenant_id), K(tablegroup_id));
  } else if (OB_ISNULL(tablegroup_schema)) {
    ret = OB_TABLEGROUP_NOT_EXIST;
    LOG_WARN("get invalid tablegroup schema", K(ret), K(tenant_id), K(tablegroup_name));
  }
  return ret;
}

int ObSchemaChecker::get_udf_info(uint64_t tenant_id,
                                  const common::ObString &udf_name,
                                  const share::schema::ObUDF *&udf_info,
                                  bool &exist)
{
  int ret = OB_SUCCESS;
  if (OB_INVALID_ID == tenant_id || udf_name.empty()) {
    ret = OB_INVALID_ARGUMENT;
    LOG_WARN("invalid argument", K(ret), K(tenant_id), K(udf_name));
  } else if (OB_ISNULL(schema_mgr_)) {
    ret = OB_NOT_INIT;
    LOG_WARN("invalid argument", K(ret), K(schema_mgr_));
  } else if (OB_FAIL(schema_mgr_->get_udf_info(tenant_id, udf_name, udf_info, exist))) {
    LOG_WARN("failed to get udf schema", K(ret));
  }
  return ret;
}

int ObSchemaChecker::check_sequence_exist_with_name(const uint64_t tenant_id,
                                                    const uint64_t database_id,
                                                    const common::ObString &sequence_name,
                                                    bool &exists,
                                                    uint64_t &sequence_id) const
{
  int ret = OB_SUCCESS;
  bool is_system_generated = false;
  if (IS_NOT_INIT) {
    ret = OB_NOT_INIT;
    LOG_WARN("schema checker is not inited", K_(is_inited));
  } else if (OB_FAIL(schema_mgr_->check_sequence_exist_with_name(tenant_id,
                                                                 database_id,
                                                                 sequence_name,
                                                                 exists,
                                                                 sequence_id,
                                                                 is_system_generated))) {
    LOG_WARN("get sequence id failed",
             K(tenant_id),
             K(database_id),
             K(database_id),
             K(sequence_name),
             K(ret));
  }
  return ret;
}

int ObSchemaChecker::get_sequence_id(const uint64_t tenant_id,
                                     const common::ObString &database_name,
                                     const common::ObString &sequence_name,
                                     uint64_t &sequence_id) const
{
  int ret = OB_SUCCESS;
  bool is_exist = false;
  bool is_system_generated = false;
  uint64_t database_id = 0;
  if (IS_NOT_INIT) {
    ret = OB_NOT_INIT;
    LOG_WARN("schema checker is not inited", K_(is_inited));
  } else if (OB_FAIL(get_database_id(tenant_id, database_name, database_id))) {
    LOG_WARN("fail to get_database_id", K(ret));
  } else if (OB_FAIL(schema_mgr_->check_sequence_exist_with_name(tenant_id,
                                                                 database_id,
                                                                 sequence_name,
                                                                 is_exist,
                                                                 sequence_id,
                                                                 is_system_generated))) {
    LOG_WARN("get sequence id failed", K(tenant_id), K(database_id), K(database_id),
                                       K(sequence_name), K(ret));
  } else if (!is_exist) {
    ret = OB_ERR_SEQ_NOT_EXIST;
  }
  return ret;
}

// only use in oracle mode
// If the function execution ends with table_schema being empty, then it indicates that there is no index corresponding to index_name under the current db
int ObSchemaChecker::get_idx_schema_by_origin_idx_name(const uint64_t tenant_id,
                                                       const uint64_t database_id,
                                                       const ObString &index_name,
                                                       const ObTableSchema *&table_schema)
{
  int ret = OB_SUCCESS;
  table_schema = NULL;

  const ObTableSchema *table = NULL;
  if (IS_NOT_INIT) {
    ret = OB_NOT_INIT;
    LOG_WARN("schema checker is not inited", K(is_inited_), K(ret));
  } else if (OB_UNLIKELY(OB_INVALID_ID == tenant_id || OB_INVALID_ID == database_id
             || index_name.empty())) {
    ret = OB_INVALID_ARGUMENT;
    LOG_WARN("invalid arguments", K(tenant_id), K(database_id), K(index_name), K(ret));
  } else if (OB_FAIL(schema_mgr_->get_idx_schema_by_origin_idx_name(tenant_id, database_id, index_name, table))) {
    LOG_WARN("get table schema failed", K(tenant_id), K(database_id), K(index_name), K(ret));
  } else {
    if (NULL == table) {
      LOG_WARN("index table schema is null", K(index_name), K(ret));
     } else if (false == table->is_tmp_table() && 0 != table->get_session_id() && OB_INVALID_ID != schema_mgr_->get_session_id()) {
      // This scenario is querying a table where the data has not been fully inserted, and the table is not visible to the outside
      // table->get_session_id() is 0 when it can only be a temporary table, or when the query table data insertion is not yet complete
      const ObDatabaseSchema  *db_schema = NULL;
      if (OB_FAIL(schema_mgr_->get_database_schema(tenant_id, database_id, db_schema))) {
        LOG_WARN("get database schema failed", K(tenant_id), K(database_id), K(ret));
      } else if (NULL == db_schema) {
        ret = OB_ERR_BAD_DATABASE;
        LOG_WARN("fail to get database schema", K(tenant_id), K(database_id), K(ret));
      } else {
        ret = OB_TABLE_NOT_EXIST;
        ObCStringHelper helper;
        LOG_USER_ERROR(OB_TABLE_NOT_EXIST, db_schema->get_database_name(),
            helper.convert(index_name));
      }
    } else {
      table_schema = table;
    }
  }
  return ret;
}

int ObSchemaChecker::get_directory_id(const uint64_t tenant_id,
                                      const common::ObString &directory_name,
                                      uint64_t &directory_id)
{
  int ret = OB_SUCCESS;
  const ObDirectorySchema *schema = NULL;
  if (IS_NOT_INIT) {
    ret = OB_NOT_INIT;
    LOG_WARN("schema checker is not inited", K_(is_inited));
  } else if (OB_FAIL(schema_mgr_->get_directory_schema_by_name(tenant_id, directory_name, schema))) {
    LOG_WARN("get directory schema failed", K(ret));
  } else if (OB_ISNULL(schema)) {
    /* comp oracle err code
    SQL> GRANT READ ON DD TO U2;
    GRANT READ ON DD TO U2
                  *
    ERROR at line 1:
    ORA-00942: table or view does not exist
    */
    ret = OB_TABLE_NOT_EXIST;
    LOG_WARN("directory is not exists", K(directory_name));
  } else {
    directory_id = schema->get_directory_id();
  }
  return ret;
}

int ObSchemaChecker::get_location_id(const uint64_t tenant_id,
                                     const common::ObString &location_name,
                                     uint64_t &location_id)
{
  int ret = OB_SUCCESS;
  const ObLocationSchema *schema = NULL;
  if (IS_NOT_INIT) {
    ret = OB_NOT_INIT;
    LOG_WARN("schema checker is not inited", K_(is_inited));
  } else if (OB_FAIL(schema_mgr_->get_location_schema_by_name(tenant_id, location_name, schema))) {
    LOG_WARN("get location schema failed", K(ret));
  } else if (OB_ISNULL(schema)) {
    ret = OB_LOCATION_OBJ_NOT_EXIST;
    LOG_WARN("location is not exists", K(location_name));
  } else {
    location_id = schema->get_location_id();
  }
  return ret;
}

int ObSchemaChecker::get_table_schema_inner(const uint64_t tenant_id, uint64_t table_id,
                                            const ObTableSchema *&table_schema) const
{
  int ret = OB_SUCCESS;
  if (OB_NOT_NULL(sql_schema_mgr_)) {
    OZ (sql_schema_mgr_->get_table_schema(table_id, table_schema), table_id);
  } else {
    OV (OB_NOT_NULL(schema_mgr_));
    OZ (schema_mgr_->get_table_schema(tenant_id, table_id, table_schema), table_id);
  }
  return ret;
}

int ObSchemaChecker::get_column_schema_inner(const uint64_t tenant_id, uint64_t table_id,
                                             const ObString &column_name,
                                             const ObColumnSchemaV2 *&column_schema,
                                             bool is_link /* = false */) const
{
  int ret = OB_SUCCESS;
  if (OB_NOT_NULL(sql_schema_mgr_)) {
    OZ (sql_schema_mgr_->get_column_schema(table_id, column_name, column_schema, is_link),
        table_id, column_name);
  } else {
    OV (OB_NOT_NULL(schema_mgr_));
    OZ (schema_mgr_->get_column_schema(tenant_id, table_id, column_name, column_schema),
        table_id, column_name);
  }
  return ret;
}

int ObSchemaChecker::get_column_schema_inner(const uint64_t tenant_id, uint64_t table_id, const uint64_t column_id,
                                             const ObColumnSchemaV2 *&column_schema,
                                             bool is_link /* = false */) const
{
  int ret = OB_SUCCESS;
  if (OB_NOT_NULL(sql_schema_mgr_)) {
    OZ (sql_schema_mgr_->get_column_schema(table_id, column_id, column_schema, is_link),
        table_id, column_id, is_link);
  } else {
    OV (OB_NOT_NULL(schema_mgr_));
    OZ (schema_mgr_->get_column_schema(tenant_id, table_id, column_id, column_schema),
        table_id, column_id);
  }
  return ret;
}



int ObSchemaChecker::check_exist_same_name_object_with_synonym(const uint64_t tenant_id,
                                     uint64_t database_id,
                                     const common::ObString &object_name,
                                     bool &exist,
                                     bool &is_private_syn)
{
  int ret = OB_SUCCESS;
  exist = false;
  is_private_syn = false;
  common::ObString database_name;
  const ObDatabaseSchema  *db_schema = NULL;
  const share::schema::ObTableSchema *table_schema = NULL;
  if (OB_FAIL(get_database_schema(tenant_id, database_id, db_schema))) {
    LOG_WARN("fail to get db schema", K(ret));
  } else if (OB_NOT_NULL(db_schema)) {
    database_name = db_schema->get_database_name();
    ret = get_table_schema(tenant_id, database_name, object_name, false, table_schema);
    if (OB_SUCC(ret) && OB_NOT_NULL(table_schema)) {
      exist = true;
    }

    //check sequence
    if (OB_TABLE_NOT_EXIST == ret) {
      uint64_t sequence_id = 0;
      if (OB_FAIL(get_sequence_id(tenant_id, database_name, object_name, sequence_id))) {
        if (OB_ERR_SEQ_NOT_EXIST == ret) {
          ret = OB_TABLE_NOT_EXIST;
        }
      } else {
        exist = true;
      }
    }
    //check procedure/function
    if (OB_TABLE_NOT_EXIST == ret) {
      uint64_t routine_id = 0;
      bool is_proc = false;
      if (OB_FAIL(get_routine_id(tenant_id, database_name, object_name, routine_id, is_proc))) {
        if (OB_ERR_SP_DOES_NOT_EXIST == ret) {
          ret = OB_TABLE_NOT_EXIST;
        }
      } else {
        exist = true;
      }
    }
    //check package
    if (OB_TABLE_NOT_EXIST == ret) {
      uint64_t package_id = 0;
      int64_t compatible_mode = COMPATIBLE_MYSQL_MODE;
      if (OB_FAIL(get_package_id(tenant_id, database_name, object_name,
                                  compatible_mode, package_id))) {
        if (OB_ERR_PACKAGE_DOSE_NOT_EXIST == ret) {
          if (OB_FAIL(get_package_id(OB_SYS_TENANT_ID,
                                      OB_SYS_DATABASE_ID,
                                      object_name,
                                      compatible_mode,
                                      package_id))) {
            if (OB_ERR_PACKAGE_DOSE_NOT_EXIST == ret) {
              ret = OB_TABLE_NOT_EXIST;
            }
          } else {
            exist = true;
          }
        }
      } else {
        exist = true;
      }
    }

  }
  if (OB_TABLE_NOT_EXIST == ret) {
    ret = OB_SUCCESS;
  }

  return ret;
}


bool ObSchemaChecker::enable_mysql_pl_priv_check(int64_t tenant_id, ObSchemaGetterGuard &schema_guard)
{
  bool enable = false;
  uint64_t compat_version = 0;
  int ret = OB_SUCCESS;
  if (OB_FAIL(GET_MIN_DATA_VERSION(tenant_id, compat_version))) {
    LOG_WARN("fail to get data version", K(tenant_id));
  } else if (lib::is_mysql_mode()) {
    const ObSysVarSchema *sys_var = NULL;
    ObMalloc alloc(ObModIds::OB_TEMP_VARIABLES);
    ObObj val;
    if (OB_FAIL(schema_guard.get_tenant_system_variable(tenant_id, share::SYS_VAR__ENABLE_MYSQL_PL_PRIV_CHECK, sys_var))) {
      LOG_WARN("fail to get tenant var schema", K(ret));
    } else if (OB_ISNULL(sys_var)) {
      ret = OB_ERR_UNEXPECTED;
      LOG_WARN("sys variable schema is null", KR(ret));
    } else if (OB_FAIL(sys_var->get_value(&alloc, NULL, val))) {
      LOG_WARN("fail to get charset var value", K(ret));
    } else {
      enable = val.get_bool();
    }
  }
  LOG_DEBUG("show enabale mysql routine priv enable", K(enable));
  return enable;
}

int ObSchemaChecker::remove_tmp_cte_schemas(const ObString& cte_table_name)
{
  int ret = OB_SUCCESS;
  for (int64_t i = 0; OB_SUCC(ret) && i < tmp_cte_schemas_.count(); i++) {
    if (cte_table_name == tmp_cte_schemas_.at(i)->get_table_name()) {
      if(OB_FAIL(tmp_cte_schemas_.remove(i))) {
        LOG_WARN("remove from tmp_cte_schemas_ failed.", K(ret));
      } else {
        break;
      }
    }
  }
  return ret;
}

int ObSchemaChecker::check_mysql_grant_role_priv(
    const ObSqlCtx &sql_ctx,
    const ObIArray<uint64_t> &granting_role_ids)
{
  int ret = OB_SUCCESS;

  //check SUPER or ROLE_ADMIN [TODO PRIV]
  ObArenaAllocator alloc;
  ObStmtNeedPrivs stmt_need_privs(alloc);
  ObNeedPriv need_priv("", "", OB_PRIV_USER_LEVEL, OB_PRIV_SUPER, false);
  OZ (stmt_need_privs.need_privs_.init(1));
  OZ (stmt_need_privs.need_privs_.push_back(need_priv));

  if (OB_SUCC(ret) && OB_FAIL(ObPrivilegeCheck::check_privilege(sql_ctx, stmt_need_privs))) {
    int ret_bak = ret;
    ret = OB_SUCCESS;
    const ObUserInfo *user_info = NULL;
    uint64_t user_id = sql_ctx.session_info_->get_priv_user_id();
    OZ (get_user_info(sql_ctx.session_info_->get_effective_tenant_id(), user_id, user_info));
    for (int i = 0; OB_SUCC(ret) && i < granting_role_ids.count(); i++) {
      int64_t idx = -1;
      if (!has_exist_in_array(user_info->get_role_id_array(), granting_role_ids.at(i), &idx)
          || ADMIN_OPTION != user_info->get_admin_option(user_info->get_role_id_option_array().at(idx))) {
        ret = ret_bak;
      }
    }
  }

  return ret;
}


int ObSchemaChecker::check_set_default_role_priv(
    const ObSqlCtx &sql_ctx)
{
  int ret = OB_SUCCESS;
  ObArenaAllocator alloc;
  ObStmtNeedPrivs stmt_need_privs(alloc);
  ObNeedPriv need_priv("mysql", "", OB_PRIV_DB_LEVEL, OB_PRIV_UPDATE, false);

  OZ (stmt_need_privs.need_privs_.init(1));
  OZ (stmt_need_privs.need_privs_.push_back(need_priv));

  //check CREATE USER or UPDATE privilege on mysql
  if (OB_SUCC(ret) && OB_FAIL(ObPrivilegeCheck::check_privilege(sql_ctx, stmt_need_privs))) {
    stmt_need_privs.need_privs_.at(0) =
        ObNeedPriv("", "", OB_PRIV_USER_LEVEL, OB_PRIV_CREATE_USER, false);
    if (OB_FAIL(ObPrivilegeCheck::check_privilege(sql_ctx, stmt_need_privs))) {
      LOG_WARN("no priv", K(ret));
    }
  }

  return ret;
}

}//end of namespace sql
}//end of namespace oceanbase
