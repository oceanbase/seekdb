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

#ifndef OCEANBAS_SQL_RESOLVER_DCL_OB_GRANT_RESOLVER_
#define OCEANBAS_SQL_RESOLVER_DCL_OB_GRANT_RESOLVER_
#include "sql/resolver/dcl/ob_dcl_resolver.h"
#include "share/schema/ob_schema_struct.h"
#include "sql/resolver/dcl/ob_grant_stmt.h"

namespace oceanbase
{
namespace sql
{
class ObGrantResolver: public ObDCLResolver
{
public:
  explicit ObGrantResolver(ObResolverParams &params);
  virtual ~ObGrantResolver();

  virtual int resolve(const ParseNode &parse_tree);
  
  static int resolve_grant_user(
      const ParseNode *grant_user,
      ObSQLSessionInfo *session_info,
      ObString &user_name,
      ObString &host_name);

  static int resolve_grantee_clause(
      const ParseNode *grantee_clause,
      ObSQLSessionInfo *session_info,
      ObIArray<ObString> &user_name_array,
      ObIArray<ObString> &host_name_array);

  int resolve_grant_role_to_ur(
      const ParseNode *grant_role,
      ObGrantStmt *grant_stmt);

  int resolve_grant_role_mysql(
      const ParseNode *grant_role,
      ObGrantStmt *grant_stmt);

  static int resolve_priv_level(
      share::schema::ObSchemaGetterGuard *guard,
      const ObSQLSessionInfo *session, 
      const ParseNode *node,
      const common::ObString &session_db,
      common::ObString &db,
      common::ObString &table,
      share::schema::ObPrivLevel &grant_level,
      ObIAllocator &allocator,
      common::ObString &catalog);

  static int resolve_priv_level_with_object_type(const ObSQLSessionInfo *session_info,
                                                 const ParseNode *priv_object_node,
                                                 ObPrivLevel &grant_level);

  template<class T>
  static int resolve_priv_object(const ParseNode *priv_object_node,
                                 T *grant_stmt,
                                 ObSchemaChecker *schema_checker_,
                                 common::ObString &db,
                                 common::ObString &table,
                                 common::ObString &catalog,
                                 const uint64_t tenant_id,
                                 ObIAllocator *allocator,
                                 bool is_grant = true); // revoke on object which has been deleted

  template<class T>
  static int resolve_priv_set(
      const uint64_t tenant_id,
      const ParseNode *privs_node,
      share::schema::ObPrivLevel grant_level,
      ObPrivSet &priv_set,
      T *grant_stmt,
      ObSchemaChecker *schema_checker,
      ObSQLSessionInfo *session_info,
      ObIAllocator &allocator);
private:
  int resolve_mysql(const ParseNode &parse_tree);
  template<class T>
  static int resolve_col_names_mysql(
      T *grant_stmt,
      const ObPrivType priv_type,
      ParseNode *column_list,
      ObSchemaChecker *schema_checker,
      ObSQLSessionInfo *session_info,
      ObIAllocator &allocator);
  int resolve_admin_option(
      const ParseNode *admin_option,
      uint64_t &option);

private:
  // disallow copy
  DISALLOW_COPY_AND_ASSIGN(ObGrantResolver);
};


template<class T>
int ObGrantResolver::resolve_col_names_mysql(
    T *grant_stmt,
    const ObPrivType priv_type,
    ParseNode *column_list,
    ObSchemaChecker *schema_checker,
    ObSQLSessionInfo *session_info,
    ObIAllocator &allocator)
{
  int ret = OB_SUCCESS;

  ObString db_name = grant_stmt->get_database_name();
  ObString table_name = grant_stmt->get_table_name();

  ObObjectType object_type = grant_stmt->get_object_type();
  uint64_t obj_id = grant_stmt->get_object_id();
  ObArray<ObString> column_names;
  if (OB_ISNULL(grant_stmt) || OB_ISNULL(column_list) 
      || OB_ISNULL(schema_checker) || OB_ISNULL(session_info) 
      || OB_ISNULL(schema_checker->get_schema_guard())) {
    ret = OB_ERR_UNEXPECTED;
    SQL_RESV_LOG(WARN, "unexpected error", K(ret));
  } else {
    ObString column_name;
    for (int32_t i = 0; OB_SUCCESS == ret && i < column_list->num_child_; ++i) {
      const ParseNode *child_node = NULL;
      if (OB_ISNULL(child_node = column_list->children_[i])) {
        ret = OB_ERR_UNEXPECTED;
        SQL_RESV_LOG(WARN, "child node is null");
      } else {
        const share::schema::ObColumnSchemaV2 *column_schema = NULL;
        const ObSimpleTableSchemaV2 *table_schema = NULL;
        if (OB_FAIL(ob_write_string(allocator, ObString(static_cast<int32_t>(child_node->str_len_), 
                                            const_cast<char *>(child_node->str_value_)), column_name))) {
          SQL_RESV_LOG(WARN, "ob write string failed", K(ret));
        } else if (OB_FAIL(grant_stmt->add_column_privs(column_name, priv_type))) {
          SQL_RESV_LOG(WARN, "push back failed", K(ret));
        }
      }
    }
  }
  return ret;
}

template<class T>
int ObGrantResolver::resolve_priv_set(
    const uint64_t tenant_id,
    const ParseNode *privs_node,
    ObPrivLevel grant_level,
    ObPrivSet &priv_set,
    T *grant_stmt,
    ObSchemaChecker *schema_checker,
    ObSQLSessionInfo *session_info,
    ObIAllocator &allocator)
{
  int ret = OB_SUCCESS;
  if (OB_ISNULL(privs_node)) {
    ret = OB_INVALID_ARGUMENT;
    SQL_RESV_LOG(WARN, "Invalid argument, priv_node_list should not be NULL", K(privs_node), K(ret));
  } else if (OB_PRIV_INVALID_LEVEL == grant_level) {
    ret = OB_INVALID_ARGUMENT;
    SQL_RESV_LOG(WARN, "Invalid argument, grant_level should not be invalid", K(grant_level), K(ret));
  } else if (OB_ISNULL(grant_stmt)) {
    ret = OB_ERR_UNEXPECTED;
    SQL_RESV_LOG(WARN, "grant stmt is null", K(ret));
  } else {
    for (int i = 0; i < privs_node->num_child_ && OB_SUCCESS == ret; ++i) {
      if (OB_NOT_NULL(privs_node->children_[i]) && T_PRIV_TYPE == privs_node->children_[i]->type_) {
        const ObPrivType priv_type = privs_node->children_[i]->value_;
        if (OB_PRIV_USER_LEVEL == grant_level) {
          priv_set |= priv_type;
        } else if (OB_PRIV_DB_LEVEL == grant_level) {
          if (OB_PRIV_ALL == priv_type) {
            priv_set |= OB_PRIV_DB_ACC;
          } else if (priv_type & (~(OB_PRIV_DB_ACC | OB_PRIV_GRANT))) {
            ret = OB_ERR_PRIV_USAGE;
            SQL_RESV_LOG(WARN, "Grant/Revoke privilege than can not be used",
                      "priv_type", ObPrintPrivSet(priv_type), K(ret));
          } else {
            priv_set |= priv_type;
          }
        } else if (OB_PRIV_TABLE_LEVEL == grant_level) {
          if (OB_PRIV_ALL == priv_type) {
            priv_set |= OB_PRIV_TABLE_ACC;
          } else if (priv_type & (~(OB_PRIV_TABLE_ACC | OB_PRIV_GRANT))) {
            ret = OB_ILLEGAL_GRANT_FOR_TABLE;
            SQL_RESV_LOG(WARN, "Grant/Revoke privilege than can not be used",
                      "priv_type", ObPrintPrivSet(priv_type), K(ret));
          } else if (privs_node->children_[i]->num_child_ == 1) {
            if (OB_FAIL(resolve_col_names_mysql(grant_stmt, priv_type,
                                                privs_node->children_[i]->children_[0],
                                                schema_checker, session_info, allocator))) {
              SQL_RESV_LOG(WARN, "resolve col names failed", K(ret));
            }
          } else {
            priv_set |= priv_type;
          }
        } else if (OB_PRIV_ROUTINE_LEVEL == grant_level) {
          if (OB_PRIV_ALL == priv_type) {
            priv_set |= OB_PRIV_ROUTINE_ACC;
          } else if (priv_type & (~(OB_PRIV_ROUTINE_ACC | OB_PRIV_GRANT))) {
            ret = OB_ILLEGAL_GRANT_FOR_TABLE;
            SQL_RESV_LOG(WARN, "Grant/Revoke privilege than can not be used",
                      "priv_type", ObPrintPrivSet(priv_type), K(ret));
          } else {
            priv_set |= priv_type;
          }
        } else if (OB_PRIV_CATALOG_LEVEL == grant_level) {
          if (OB_PRIV_ALL == priv_type) {
            priv_set |= OB_PRIV_CATALOG_ACC;
          } else if (priv_type & (~(OB_PRIV_CATALOG_ACC | OB_PRIV_GRANT))) {
            ret = OB_ILLEGAL_GRANT_FOR_TABLE;
            SQL_RESV_LOG(WARN, "Grant/Revoke privilege than can not be used",
                      "priv_type", ObPrintPrivSet(priv_type), K(ret));
          } else {
            priv_set |= priv_type;
          }
        } else if (OB_PRIV_OBJECT_LEVEL == grant_level) {
          if (OB_PRIV_ALL == priv_type) {
            priv_set |= OB_PRIV_OBJECT_ACC;
          } else if (priv_type & (~(OB_PRIV_OBJECT_ACC | OB_PRIV_GRANT))) {
            ret = OB_ILLEGAL_GRANT_FOR_TABLE;
            SQL_RESV_LOG(WARN, "Grant/Revoke privilege than can not be used",
                      "priv_type", ObPrintPrivSet(priv_type), K(ret));
          } else {
            priv_set |= priv_type;
          }
        } else {
          //do nothing
        }
      } else {
        ret = OB_ERR_PARSER_SYNTAX;
        SQL_RESV_LOG(WARN, "sql_parser parse privileges error", K(ret));
      }
    }
  }
  return ret;
}

template<class T>
int ObGrantResolver::resolve_priv_object(const ParseNode *priv_object_node,
                                         T *grant_stmt,
                                         ObSchemaChecker *schema_checker,
                                         common::ObString &db,
                                         common::ObString &table,
                                         common::ObString &catalog,
                                         const uint64_t tenant_id,
                                         ObIAllocator *allocator,
                                         bool is_grant)
{
  int ret = OB_SUCCESS;
  share::schema::ObObjectType object_type = share::schema::ObObjectType::INVALID;
  uint64_t object_id = OB_INVALID_ID;
  if (OB_ISNULL(grant_stmt) || OB_ISNULL(schema_checker) || OB_ISNULL(allocator)) {
    ret = OB_ERR_UNEXPECTED;
    LOG_WARN("get unexpected null", K(ret));
  } else if (priv_object_node != NULL) {
    if (priv_object_node->value_ == 1) {
      const share::schema::ObTableSchema *table_schema = NULL;
      if (OB_FAIL(schema_checker->get_table_schema(tenant_id, db, table, false, table_schema))) {
        LOG_WARN("get table schema failed", K(ret));
        if (OB_TABLE_NOT_EXIST == ret && !is_grant) {
          ret = OB_SUCCESS;
        }
      } else if (table_schema != NULL) {
        if (table_schema->is_index_table()) {
          object_type = ObObjectType::INDEX;
        } else {
          object_type = ObObjectType::TABLE;
        }
        object_id = table_schema->get_table_id();
      }
    } else if (priv_object_node->value_ == 2 || priv_object_node->value_ == 3) {
      object_type = (priv_object_node->value_ == 2) ? ObObjectType::PROCEDURE : ObObjectType::FUNCTION;
      uint64_t routine_id = 0;
      bool is_proc = false;
      if (OB_FAIL(schema_checker->get_routine_id(tenant_id, db, table, routine_id, is_proc))) {
        LOG_WARN("get routine id failed", K(ret));
        if (OB_ERR_SP_DOES_NOT_EXIST == ret && !is_grant) {
          ret = OB_SUCCESS;
        }
      } else {
        if (is_proc) {
          object_type = ObObjectType::PROCEDURE;
        } else {
          object_type = ObObjectType::FUNCTION;
        }
        object_id = routine_id;
      }
    } else if (priv_object_node->value_ == 4) {
      object_type = ObObjectType::CATALOG;
      if (OB_FAIL(schema_checker->get_catalog_id_name(tenant_id, catalog, object_id, allocator, !is_grant))) {
        LOG_WARN("failed to get catalog id", K(ret));
      } else {
        grant_stmt->set_catalog_name(catalog);
      }
    } else if (priv_object_node->value_ == 5) {
      object_type = ObObjectType::LOCATION;
      if (OB_FAIL(schema_checker->get_location_id(tenant_id, table, object_id))) {
        LOG_WARN("failed to get location id", K(ret));
      }
    }
  } else {
    ObString object_db_name;
    if (db.empty() || table.empty()) {
      object_type = share::schema::ObObjectType::MAX_TYPE;
    }
  }
  grant_stmt->set_object_type(object_type);
  grant_stmt->set_object_id(object_id);
  return ret;
}

} // end namespace sql
} // end namespace oceanbase
#endif
