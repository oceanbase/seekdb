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

#define USING_LOG_PREFIX SQL_SESSION
#include "sql/privilege_check/ob_privilege_check.h"

#include "sql/resolver/ddl/ob_create_table_stmt.h"
#include "sql/resolver/ddl/ob_create_mlog_stmt.h"
#include "sql/resolver/ddl/ob_drop_mlog_stmt.h"
#include "sql/resolver/ddl/ob_create_database_stmt.h"
#include "sql/resolver/ddl/ob_alter_table_stmt.h"
#include "src/sql/resolver/ddl/ob_sequence_stmt.h"
#include "sql/resolver/ddl/ob_create_outline_stmt.h"
#include "sql/resolver/ddl/ob_alter_outline_stmt.h"
#include "sql/resolver/ddl/ob_drop_outline_stmt.h"
#include "sql/resolver/ddl/ob_drop_database_stmt.h"
#include "sql/resolver/ddl/ob_drop_index_stmt.h"
#include "sql/resolver/ddl/ob_lock_tenant_stmt.h"
#include "sql/resolver/ddl/ob_drop_tenant_stmt.h"
#include "sql/resolver/ddl/ob_drop_table_stmt.h"
#include "sql/resolver/dcl/ob_revoke_stmt.h"
#include "sql/resolver/dcl/ob_set_password_stmt.h"
#include "sql/resolver/dml/ob_update_stmt.h"
#include "sql/resolver/dcl/ob_grant_stmt.h"
#include "sql/resolver/dcl/ob_revoke_stmt.h"
#include "sql/resolver/ddl/ob_modify_tenant_stmt.h"
#include "sql/resolver/ddl/ob_alter_database_stmt.h"
#include "sql/resolver/ddl/ob_truncate_table_stmt.h"
#include "sql/resolver/ddl/ob_rename_table_stmt.h"
#include "sql/resolver/ddl/ob_create_table_like_stmt.h"
#include "sql/resolver/ddl/ob_drop_tablegroup_stmt.h"
#include "sql/resolver/ddl/ob_flashback_stmt.h"
#include "sql/resolver/cmd/ob_call_procedure_stmt.h"
#include "sql/resolver/ddl/ob_lock_table_stmt.h"
#include "sql/resolver/ddl/ob_alter_routine_stmt.h"
#include "sql/resolver/ddl/ob_drop_routine_stmt.h"
#include "sql/resolver/ddl/ob_trigger_stmt.h"
#include "sql/resolver/ddl/ob_create_ccl_rule_stmt.h"
#include "sql/resolver/dcl/ob_alter_user_profile_stmt.h"
#include "sql/optimizer/ob_optimizer_util.h"
#include "sql/resolver/cmd/ob_event_stmt.h"
#include "sql/resolver/cmd/ob_location_utils_stmt.h"

namespace oceanbase {
using namespace share;
using namespace share::schema;
using namespace common;
namespace sql {
#define ADD_NEED_PRIV(need_priv)                                              \
    if (OB_SUCC(ret) && OB_FAIL(need_privs.push_back(need_priv))) {           \
      LOG_WARN("Fail to add need_priv", K(need_priv), K(ret));                \
    }

int err_stmt_type_priv(
    const ObSessionPrivInfo &session_priv,
    const ObStmt *basic_stmt,
    ObIArray<ObNeedPriv> &need_privs)
{
  UNUSED(session_priv);
  UNUSED(need_privs);
  int ret = OB_SUCCESS;
  if (OB_ISNULL(basic_stmt)) {
    ret = OB_INVALID_ARGUMENT;
    LOG_WARN("Basic stmt should not be NULL", K(ret));
  } else {
    ret = OB_ERR_UNEXPECTED;
    LOG_ERROR("Stmt type should not be here", K(ret), "stmt type", basic_stmt->get_stmt_type());
  }
  return ret;
}

///@brief if you sure no priv needed for the stmt
int no_priv_needed(
    const ObSessionPrivInfo &session_priv,
    const ObStmt *basic_stmt,
    ObIArray<ObNeedPriv> &need_privs)
{
  UNUSED(session_priv);
  UNUSED(basic_stmt);
  UNUSED(need_privs);
  return OB_SUCCESS;
}

/* Determine if expr contains columns from the specified table, the table level is marked by expr level and rel ids */
int expr_has_col_in_tab(
    const ObRawExpr *expr,
    const ObRelIds &rel_ids,
    bool& exists)
{
  int ret = OB_SUCCESS;
  bool is_stack_overflow = false;
  if (OB_ISNULL(expr)) {
    ret = OB_ERR_UNEXPECTED;
    LOG_WARN("expr is null", K(ret));
  } else if (OB_FAIL(check_stack_overflow(is_stack_overflow))) {
    LOG_WARN("failed to check stack overflow", K(ret));
  } else if (is_stack_overflow) {
    ret = OB_SIZE_OVERFLOW;
    LOG_WARN("too deep recursive", K(ret), K(is_stack_overflow));
  } else if (expr->is_column_ref_expr()) {
    if (rel_ids.is_superset(expr->get_relation_ids())) {
      exists = true;
    }
  } else if (expr->has_flag(CNT_COLUMN)) {
    for (int64_t i = 0; OB_SUCC(ret) && !exists && i < expr->get_param_count(); ++i) {
      OZ (expr_has_col_in_tab(expr->get_param_expr(i), rel_ids, exists));
    }
  }
  return ret;
}

int add_col_priv_to_need_priv(
    const ObStmt *basic_stmt,
    const TableItem &table_item,
    ObIArray<ObNeedPriv> &need_privs)
{
  int ret = OB_SUCCESS;
  ObStmtExprGetter visitor;
  ObNeedPriv need_priv;
  need_priv.catalog_ = table_item.catalog_name_;
  need_priv.db_ = table_item.database_name_;
  need_priv.table_ = table_item.table_name_;
  need_priv.is_sys_table_ = table_item.is_system_table_;
  need_priv.is_for_update_ = table_item.for_update_;
  need_priv.priv_level_ = OB_PRIV_TABLE_LEVEL;
  if (need_priv.catalog_ != OB_INTERNAL_CATALOG_NAME) {
    need_priv.priv_level_ = OB_PRIV_CATALOG_LEVEL;
    need_priv.priv_set_ = OB_PRIV_USE_CATALOG;
    ADD_NEED_PRIV(need_priv);
  }
  const uint64_t table_id = table_item.table_id_;
  visitor.set_relation_scope();
  visitor.remove_scope(SCOPE_DML_COLUMN);
  visitor.remove_scope(SCOPE_DML_CONSTRAINT);
  visitor.remove_scope(SCOPE_DMLINFOS);
  ObSEArray<ObRawExpr *, 4> col_exprs;
  bool has_dml_info = false;
  if (OB_ISNULL(basic_stmt)) {
    ret = OB_ERR_UNEXPECTED;
    LOG_WARN("basic_stmt is NULL", K(ret));
  } else if (basic_stmt->is_dml_write_stmt() &&
             OB_FAIL(static_cast<const ObDelUpdStmt*>(basic_stmt)->has_dml_table_info(
                                                            table_item.table_id_, has_dml_info))) {
    LOG_WARN("failed to check has dml table info", K(ret));
  } else if (has_dml_info) {
    stmt::StmtType stmt_type = basic_stmt->get_stmt_type();
    switch (stmt_type) {
      case stmt::T_DELETE: {
        need_priv.priv_set_ = OB_PRIV_DELETE;
        ADD_NEED_PRIV(need_priv);
        break;
      }
      case stmt::T_REPLACE:
      case stmt::T_INSERT: {
        visitor.remove_scope(SCOPE_INSERT_DESC);
        visitor.remove_scope(SCOPE_DML_VALUE);
        const ObInsertStmt *insert_stmt = NULL;
        insert_stmt = static_cast<const ObInsertStmt*>(basic_stmt);
        if (OB_ISNULL(insert_stmt)) {
          ret = OB_ERR_UNEXPECTED;
          LOG_WARN("insert_stmt is NULL", K(ret));
        } else {
          ObColumnRefRawExpr *value_desc = NULL;
          if (insert_stmt->is_replace()) {
            need_priv.priv_set_ = OB_PRIV_DELETE;
            ADD_NEED_PRIV(need_priv);
            need_priv.columns_.reuse();
          }
          need_priv.priv_set_ = OB_PRIV_INSERT;
          for (int i = 0; OB_SUCC(ret) && i < insert_stmt->get_values_desc().count(); ++i) {
            if (OB_ISNULL(value_desc = insert_stmt->get_values_desc().at(i))) {
              ret = OB_ERR_UNEXPECTED;
              LOG_WARN("value_desc is null", K(ret));
            } else if (table_id == value_desc->get_table_id()
                      && value_desc->get_column_id() >= OB_APP_MIN_COLUMN_ID) {
              OZ (need_priv.columns_.push_back(value_desc->get_column_name()));
            }
          }
          if (OB_SUCC(ret)) {
            ADD_NEED_PRIV(need_priv);
            need_priv.columns_.reuse();
          }

          if (OB_FAIL(ret)) {
          } else if (insert_stmt->is_insert_up()) {
            const ObIArray<ObAssignment> &assigns = insert_stmt->get_table_assignments();
            need_priv.priv_set_ = OB_PRIV_UPDATE;
            for (int j = 0; OB_SUCC(ret) && j < assigns.count(); ++j) {
              if (!assigns.at(j).is_implicit_) {
                const ObColumnRefRawExpr *col_expr = assigns.at(j).column_expr_;
                const ObRawExpr *expr = assigns.at(j).expr_;
                if (OB_ISNULL(col_expr) || OB_ISNULL(expr)) {
                  ret = OB_ERR_UNEXPECTED;
                  LOG_WARN("col_expr is null");
                } else if (OB_FAIL(ObRawExprUtils::extract_column_exprs(expr, col_exprs))) {
                  LOG_WARN("extract column exprs failed", K(ret));
                } else if (col_expr->get_table_id() == table_id 
                        && col_expr->get_column_id() >= OB_APP_MIN_COLUMN_ID) {
                  OZ (need_priv.columns_.push_back(col_expr->get_column_name()));
                }
              }
            }
            if (OB_SUCC(ret)) {
              ADD_NEED_PRIV(need_priv);
              need_priv.columns_.reuse();
            }
          }
        }

        if (OB_SUCC(ret)) {
          if (OB_FAIL(append(col_exprs, insert_stmt->get_insert_table_info().column_in_values_vector_))) {
            LOG_WARN("append failed", K(ret));
          }
        }

        break;
      }
      case stmt::T_UPDATE: {
        need_priv.priv_set_ = OB_PRIV_UPDATE;
        const ObUpdateStmt *update_stmt = NULL;
        update_stmt = static_cast<const ObUpdateStmt*>(basic_stmt);
        if (OB_ISNULL(update_stmt)) {
          ret = OB_ERR_UNEXPECTED;
          LOG_WARN("update_stmt is null", K(ret));
        } else {
          for (int i = 0; OB_SUCC(ret) && i < update_stmt->get_update_table_info().count(); ++i) {
            ObUpdateTableInfo* table_info = update_stmt->get_update_table_info().at(i);
            if (OB_ISNULL(table_info)) {
              ret = OB_ERR_UNEXPECTED;
              LOG_WARN("get null table info", K(ret));
            } else if (table_info->table_id_ == table_id) {
              const ObAssignments &assigns = table_info->assignments_;
              for (int j = 0; OB_SUCC(ret) && j < assigns.count(); ++j) {
                if (!assigns.at(j).is_implicit_) {
                  const ObColumnRefRawExpr *col_expr = assigns.at(j).column_expr_;
                  const ObRawExpr *value_expr = assigns.at(j).expr_;
                  if (OB_ISNULL(col_expr)) {
                    ret = OB_ERR_UNEXPECTED;
                    LOG_WARN("col_expr is null");
                  } else if (col_expr->get_table_id() == table_id 
                          && col_expr->get_column_id() >= OB_APP_MIN_COLUMN_ID) {
                    OZ (need_priv.columns_.push_back(col_expr->get_column_name()));
                  } else if (OB_FAIL(ObRawExprUtils::extract_column_exprs(value_expr, col_exprs))) {
                    LOG_WARN("extract column exprs failed", K(ret));
                  }
                }
              }
            }
          }
        }
        if (OB_SUCC(ret)) {
          ADD_NEED_PRIV(need_priv);
          need_priv.columns_.reuse();
        }
        break;
      }
      default : {
        break;
      }
    }
  }
  if (OB_SUCC(ret)) {
    ObSEArray<ObRawExpr *, 4> rel_exprs;
    need_priv.priv_set_ = OB_PRIV_SELECT;
    if (OB_FAIL(static_cast<const ObDMLStmt *>(basic_stmt)->get_relation_exprs(rel_exprs, visitor))) {
      LOG_WARN("get rel exprs failed", K(ret));
    } else if (OB_FAIL(ObRawExprUtils::extract_column_exprs(rel_exprs, col_exprs))) {
      LOG_WARN("extract column exprs failed", K(ret));
    } else {
      for (int64_t i = 0; OB_SUCC(ret) && i < col_exprs.count(); i++) {
        if (OB_ISNULL(col_exprs.at(i)) || OB_UNLIKELY(!col_exprs.at(i)->is_column_ref_expr())) {
          ret = OB_ERR_UNEXPECTED;
          LOG_WARN("unexpected error", K(ret));
        } else {
          ObColumnRefRawExpr *col_expr = static_cast<ObColumnRefRawExpr *>(col_exprs.at(i));
          if (OB_ISNULL(col_expr)) {
            ret = OB_ERR_UNEXPECTED;
            LOG_WARN("unexpected error", K(ret));
          } else if (col_expr->get_table_id() == table_id && col_expr->get_column_id() >= OB_APP_MIN_COLUMN_ID) {
            OZ (need_priv.columns_.push_back(col_expr->get_column_name()));
          }
        }
      }
      if (OB_SUCC(ret)) {
        if (need_priv.columns_.empty()) {
          if (basic_stmt->is_select_stmt()) {
            need_priv.check_any_column_priv_ = true;
            ADD_NEED_PRIV(need_priv);
            need_priv.check_any_column_priv_ = false;
          }
        } else {
          ADD_NEED_PRIV(need_priv);
          need_priv.columns_.reuse();
        }
      }
    }
  }
  return ret;
}

int get_dml_stmt_need_privs(
    const ObSessionPrivInfo &session_priv,
    const ObStmt *basic_stmt,
    ObIArray<ObNeedPriv> &need_privs)
{
  int ret = OB_SUCCESS;
  if (OB_ISNULL(basic_stmt)) {
    ret = OB_INVALID_ARGUMENT;
    LOG_WARN("Basic stmt should not be NULL", K(ret));
  } else {
    ObNeedPriv need_priv;
    stmt::StmtType stmt_type = basic_stmt->get_stmt_type();
    switch (stmt_type) {
      case stmt::T_SELECT : {
        const ObSelectStmt *select_stmt = static_cast<const ObSelectStmt*>(basic_stmt);
        if (select_stmt->is_from_show_stmt()) {
          //do not check priv for show stmt.
          // Now most show statements filter out information that the user does not have permission to see
          break;
        }
      }//fall through for non-show select
      case stmt::T_INSERT :
      case stmt::T_REPLACE :
      case stmt::T_DELETE :
      case stmt::T_UPDATE : {
        ObPrivSet priv_set = 0;
        ObString op_literal;
        if (stmt::T_SELECT == stmt_type) {
          priv_set = OB_PRIV_SELECT;
          op_literal = ObString::make_string("SELECT");
          if (static_cast<const ObSelectStmt*>(basic_stmt)->is_select_into_outfile()) {
            ObNeedPriv need_priv;
            need_priv.priv_set_ = OB_PRIV_FILE;
            need_priv.priv_level_ = OB_PRIV_USER_LEVEL;
            ADD_NEED_PRIV(need_priv);
          }
        } else if (stmt::T_INSERT == stmt_type) {
          priv_set = OB_PRIV_INSERT;
          op_literal = ObString::make_string("INSERT");
          if (static_cast<const ObInsertStmt*>(basic_stmt)->is_insert_up()) {
            priv_set |= (OB_PRIV_UPDATE | OB_PRIV_SELECT);
            op_literal = ObString::make_string("INSERT, UPDATE");
          }
          op_literal = ObString::make_string("INSERT");
        } else if (stmt::T_REPLACE == stmt_type) {
          priv_set = OB_PRIV_INSERT | OB_PRIV_DELETE;
          op_literal = ObString::make_string("INSERT, DELETE");
        } else if (stmt::T_DELETE == stmt_type) {
          priv_set = OB_PRIV_DELETE | OB_PRIV_SELECT;
          op_literal = ObString::make_string("DELETE");
        } else if (stmt::T_UPDATE == stmt_type) {
          priv_set = OB_PRIV_UPDATE | OB_PRIV_SELECT;
          op_literal = ObString::make_string("UPDATE");
        } else { } //do nothing
        const ObDMLStmt *dml_stmt = static_cast<const ObDMLStmt*>(basic_stmt);
        int64_t table_size = dml_stmt->get_table_size();
        if (stmt::T_SELECT != stmt_type) {
          ObSEArray<const ObDmlTableInfo*, 4> table_infos;
          if (OB_FAIL(static_cast<const ObDelUpdStmt*>(basic_stmt)->get_dml_table_infos(table_infos))) {
            LOG_WARN("failed to get dml table infos", K(ret));
          } else if (OB_FAIL(ObPrivilegeCheck::can_do_operation_on_db(session_priv, table_infos, op_literal))) {
            LOG_WARN("cann't do this operation on this database", K(ret), K(stmt_type));
          }
        }
        for (int64_t i = 0; OB_SUCC(ret) && i < table_size; i++) {
          const TableItem *table_item = dml_stmt->get_table_item(i);
          if (OB_ISNULL(table_item)) {
            ret = OB_ERR_UNEXPECTED;
            LOG_WARN("table item is null");
          } else if (table_item->is_link_table()) {
            // skip link table
          } else if (TableItem::BASE_TABLE == table_item->type_
            || TableItem::ALIAS_TABLE == table_item->type_
            || table_item->is_view_table_) {
            need_priv.catalog_ = table_item->catalog_name_;
            need_priv.db_ = table_item->database_name_;
            need_priv.table_ = table_item->table_name_;
            need_priv.is_sys_table_ = table_item->is_system_table_;
            need_priv.is_for_update_ = table_item->for_update_;
            need_priv.priv_level_ = OB_PRIV_TABLE_LEVEL;
            if (is_mysql_mode() && need_priv.catalog_ != OB_INTERNAL_CATALOG_NAME) {
              need_priv.priv_level_ = OB_PRIV_CATALOG_LEVEL;
              priv_set |= OB_PRIV_USE_CATALOG;
            }
            //no check for information_schema select
            if (stmt::T_SELECT != dml_stmt->get_stmt_type()) {
              if (OB_FAIL(ObPrivilegeCheck::can_do_operation_on_db(session_priv, table_item->database_name_))) {
                LOG_WARN("Can not do this operation on the database", K(session_priv),
                         K(ret), "stmt_type", dml_stmt->get_stmt_type());
              } else if (ObPrivilegeCheck::is_mysql_org_table(table_item->database_name_, table_item->table_name_)) {
                ret = OB_ERR_NO_TABLE_PRIVILEGE;
                LOG_USER_ERROR(OB_ERR_NO_TABLE_PRIVILEGE, op_literal.length(), op_literal.ptr(),
                               session_priv.user_name_.length(), session_priv.user_name_.ptr(),
                               session_priv.host_name_.length(),session_priv.host_name_.ptr(),
                               table_item->table_name_.length(), table_item->table_name_.ptr());
              }
            }
            if (OB_SUCC(ret)) {
              bool has = false;
              if (stmt::T_SELECT == dml_stmt->get_stmt_type()) {
                need_priv.priv_set_ = priv_set;
              } else if (OB_FAIL(static_cast<const ObDelUpdStmt*>(dml_stmt)->has_dml_table_info(
                                                              table_item->table_id_, has))) {
                LOG_WARN("failed to check has dml table info", K(ret));
              } else {
                need_priv.priv_set_ = has ? priv_set : OB_PRIV_SELECT;
              }
            }
            if (OB_SUCC(ret)) {
              if (session_priv.is_tenant_changed()
                  && 0 != table_item->database_name_.case_compare(OB_SYS_DATABASE_NAME)) {
                ret = OB_ERR_NO_DB_PRIVILEGE;
                LOG_USER_ERROR(OB_ERR_NO_DB_PRIVILEGE, session_priv.user_name_.length(), session_priv.user_name_.ptr(),
                               session_priv.host_name_.length(),session_priv.host_name_.ptr(),
                               table_item->database_name_.length(), table_item->database_name_.ptr());
              }
            }
            if (OB_SUCC(ret)) {
              if (table_item->is_view_table_ && !table_item->alias_name_.empty()) {
                if (table_item->is_oracle_all_or_user_sys_view_for_alias()) {
                  need_priv.priv_set_ &= ~OB_PRIV_SELECT;
                }
              } else if (table_item->is_oracle_all_or_user_sys_view()) {
                need_priv.priv_set_ &= ~OB_PRIV_SELECT;
              }
            }
            
            if (OB_SUCC(ret)
               && ObTableType::EXTERNAL_TABLE == table_item->table_type_
               && common::OB_INVALID_ID != table_item->external_location_id_) {
              ObSchemaGetterGuard schema_guard;
              CK(GCTX.schema_service_ != NULL);
              OZ(GCTX.schema_service_->get_tenant_schema_guard(session_priv.tenant_id_, schema_guard));
              // const ObTableSchema *table_schema = NULL;
              // LOG_INFO("table id", K(table_item->table_id_), K(table_item->ref_id_), K(table_item->database_name_), K(table_item->table_name_));
              // if (OB_FAIL(schema_guard.get_table_schema(session_priv.tenant_id_, table_item->ref_id_, table_schema))) {
              //   LOG_WARN("failed to get table schema", K(ret));
              // } else if (OB_ISNULL(table_schema)) {
              //   ret = OB_ERR_UNEXPECTED;
              //   LOG_WARN("table schema is null", K(ret));
              // } else if (OB_INVALID_ID != table_schema->get_external_location_id()) {
                const ObLocationSchema *location_schema = NULL;
                if (OB_FAIL(schema_guard.get_location_schema_by_id(session_priv.tenant_id_, table_item->external_location_id_, location_schema))) {
                  LOG_WARN("failed to get location schema", K(ret));
                } else if (OB_ISNULL(location_schema)) {
                  ret = OB_ERR_UNEXPECTED;
                  LOG_WARN("location schema is null", K(ret));
                } else {
                  ObNeedPriv tmp_need_priv;
                  tmp_need_priv.db_ = table_item->database_name_;
                  tmp_need_priv.table_ = location_schema->get_location_name();
                  tmp_need_priv.priv_level_ = OB_PRIV_OBJECT_LEVEL;
                  tmp_need_priv.priv_set_ = OB_PRIV_READ;
                  tmp_need_priv.obj_type_ = ObObjectType::LOCATION;
                  ADD_NEED_PRIV(tmp_need_priv);
                }
              // }
            }

            if (OB_SUCC(ret)) {
              if (lib::is_mysql_mode()) {
                if (OB_FAIL(add_col_priv_to_need_priv(basic_stmt, *table_item, need_privs))) {
                  LOG_WARN("add col id array to need priv failed", K(ret));
                }
              } else {
                ADD_NEED_PRIV(need_priv);
              }
            }
          }
        }
        break;
      }
      default: {
        ret = OB_ERR_UNEXPECTED;
        LOG_WARN("Stmt type error, should be DML stmt", K(ret), K(stmt_type));
      }
    }
  }
  return ret;
}

int get_alter_table_stmt_need_privs(
    const ObSessionPrivInfo &session_priv,
    const ObStmt *basic_stmt,
    ObIArray<ObNeedPriv> &need_privs)
{
  int ret = OB_SUCCESS;
  bool need_check = false;
  if (OB_ISNULL(basic_stmt)) {
    ret = OB_INVALID_ARGUMENT;
    LOG_WARN("Basic stmt should be not be NULL", K(ret));
  } else if (OB_UNLIKELY(stmt::T_ALTER_TABLE != basic_stmt->get_stmt_type())) {
    ret = OB_INVALID_ARGUMENT;
    LOG_WARN("Stmt type should be T_ALTER_TABLE",
             K(ret), "stmt type", basic_stmt->get_stmt_type());
  } else {
    ObNeedPriv need_priv;
    const ObAlterTableStmt *stmt = static_cast<const ObAlterTableStmt*>(basic_stmt);
    const ObSArray<obrpc::ObCreateForeignKeyArg> &foreign_keys = stmt->get_read_only_foreign_key_arg_list();
    if (OB_FAIL(ObPrivilegeCheck::can_do_operation_on_db(session_priv, stmt->get_org_database_name()))) {
      LOG_WARN("Can not alter table in the database", K(session_priv), K(ret),
               "database_name", stmt->get_org_database_name());
    } else if (ObPrivilegeCheck::is_mysql_org_table(stmt->get_org_database_name(),
                                  stmt->get_org_table_name())
               && session_priv.tenant_id_ != OB_SYS_TENANT_ID) {
      ret = OB_ERR_NO_TABLE_PRIVILEGE;
      LOG_USER_ERROR(OB_ERR_NO_TABLE_PRIVILEGE, (int)strlen("ALTER"), "ALTER",
                     session_priv.user_name_.length(),session_priv.user_name_.ptr(),
                     session_priv.host_name_.length(),session_priv.host_name_.ptr(),
                     stmt->get_org_table_name().length(), stmt->get_org_table_name().ptr());
    } else if (stmt->has_rename_action()) {
      if (OB_FAIL(ObPrivilegeCheck::can_do_operation_on_db(session_priv, stmt->get_database_name()))) {
        LOG_WARN("Can not alter table in the database", K(session_priv), K(ret),
                 "database_name", stmt->get_database_name());
      } else {
        need_priv.db_ = stmt->get_org_database_name();
        need_priv.table_ = stmt->get_org_table_name();
        need_priv.priv_set_ = OB_PRIV_ALTER | OB_PRIV_DROP;
        need_priv.priv_level_ = OB_PRIV_TABLE_LEVEL;
        ADD_NEED_PRIV(need_priv);
        need_priv.db_ = stmt->get_database_name();
        need_priv.table_ = stmt->get_table_name();
        need_priv.priv_set_ = OB_PRIV_CREATE | OB_PRIV_INSERT;
        need_priv.priv_level_ = OB_PRIV_TABLE_LEVEL;
        ADD_NEED_PRIV(need_priv);
      }
    } else {
      need_priv.db_ = stmt->get_org_database_name();
      need_priv.table_ = stmt->get_org_table_name();
      need_priv.priv_set_ = OB_PRIV_ALTER;
      need_priv.priv_level_ = OB_PRIV_TABLE_LEVEL;
      ADD_NEED_PRIV(need_priv);

      const AlterTableSchema &alter_schema = const_cast<ObAlterTableStmt*>(stmt)->get_alter_table_arg().alter_table_schema_;
      if (alter_schema.alter_option_bitset_.has_member(obrpc::ObAlterTableArg::SESSION_ACTIVE_TIME)
          && session_priv.tenant_id_ != OB_SYS_TENANT_ID) {
        ret = OB_ERR_NO_PRIVILEGE;
        LOG_USER_ERROR(OB_ERR_NO_PRIVILEGE, "SUPER");
      }
    }
    // check references privilege
    if (OB_SUCC(ret)) {
      if (OB_FAIL(ObPrivilegeCheck::get_priv_need_check(session_priv,
                                                        ObCompatFeatureType::MYSQL_REFERENCES_PRIV_ENHANCE,
                                                        need_check))) {
        LOG_WARN("failed to get priv need check", K(ret));
      } else if (lib::is_mysql_mode() && need_check) {
        for (int64_t i = 0; OB_SUCC(ret) && i < foreign_keys.count(); i++) {
          need_priv.db_ = foreign_keys.at(i).parent_database_;
          need_priv.table_ = foreign_keys.at(i).parent_table_;
          need_priv.priv_level_ = OB_PRIV_TABLE_LEVEL;
          need_priv.priv_set_ = OB_PRIV_REFERENCES;
          ADD_NEED_PRIV(need_priv);
        }
      }
    }
  }
  return ret;
}

int get_create_database_stmt_need_privs(
    const ObSessionPrivInfo &session_priv,
    const ObStmt *basic_stmt,
    ObIArray<ObNeedPriv> &need_privs)
{
  int ret = OB_SUCCESS;
  if (OB_ISNULL(basic_stmt)) {
    ret = OB_INVALID_ARGUMENT;
    LOG_WARN("Basic stmt should be not be NULL", K(ret));
  } else if (OB_UNLIKELY(stmt::T_CREATE_DATABASE != basic_stmt->get_stmt_type())) {
    ret = OB_INVALID_ARGUMENT;
    LOG_WARN("Stmt type should be T_CREATE_DATABASE",
             K(ret), "stmt type", basic_stmt->get_stmt_type());
  } else {
    ObNeedPriv need_priv;
    const ObCreateDatabaseStmt *stmt = static_cast<const ObCreateDatabaseStmt*>(basic_stmt);
    if (OB_FAIL(ret)) {
    } else if (OB_FAIL(ObPrivilegeCheck::can_do_operation_on_db(session_priv, stmt->get_database_name()))) {
      LOG_WARN("Can not create in the database", K(session_priv), "database_name", stmt->get_database_name(), K(ret));
    } else {
      need_priv.db_ = stmt->get_database_name();
      need_priv.priv_set_ = OB_PRIV_CREATE;
      need_priv.priv_level_ = OB_PRIV_DB_LEVEL;
      ADD_NEED_PRIV(need_priv);
    }
  }
  return ret;
}

int get_alter_database_stmt_need_privs(
    const ObSessionPrivInfo &session_priv,
    const ObStmt *basic_stmt,
    ObIArray<ObNeedPriv> &need_privs)
{
  int ret = OB_SUCCESS;
  if (OB_ISNULL(basic_stmt)) {
    ret = OB_INVALID_ARGUMENT;
    LOG_WARN("Basic stmt should be not be NULL", K(ret));
  } else if (OB_UNLIKELY(stmt::T_ALTER_DATABASE != basic_stmt->get_stmt_type())) {
    ret = OB_INVALID_ARGUMENT;
    LOG_WARN("Stmt type should be T_ALTER_DATABASE",
             K(ret), "stmt type", basic_stmt->get_stmt_type());
  } else {
    ObNeedPriv need_priv;
    const ObAlterDatabaseStmt *stmt = static_cast<const ObAlterDatabaseStmt*>(basic_stmt);
    const ObString &db_name = stmt->get_database_name();
    if (0 == db_name.case_compare(OB_INFORMATION_SCHEMA_NAME)
        || (0 == db_name.case_compare(OB_SYS_DATABASE_NAME)
            && !stmt->only_alter_primary_zone())) {
      ret = OB_ERR_NO_DB_PRIVILEGE;
      LOG_USER_ERROR(OB_ERR_NO_DB_PRIVILEGE, session_priv.user_name_.length(), session_priv.user_name_.ptr(),
                     session_priv.host_name_.length(), session_priv.host_name_.ptr(),
                     db_name.length(), db_name.ptr());
    } else {
      need_priv.db_ = stmt->get_database_name();
      need_priv.priv_set_ = OB_PRIV_ALTER;
      need_priv.priv_level_ = OB_PRIV_DB_LEVEL;
      ADD_NEED_PRIV(need_priv);
    }
  }
  return ret;
}

int get_drop_database_stmt_need_privs(
    const ObSessionPrivInfo &session_priv,
    const ObStmt *basic_stmt,
    ObIArray<ObNeedPriv> &need_privs)
{
  int ret = OB_SUCCESS;
  if (OB_ISNULL(basic_stmt)) {
    ret = OB_INVALID_ARGUMENT;
    LOG_WARN("Basic stmt should be not be NULL", K(ret));
  } else if (OB_UNLIKELY(stmt::T_DROP_DATABASE != basic_stmt->get_stmt_type())) {
    ret = OB_INVALID_ARGUMENT;
    LOG_WARN("Stmt type should be T_DROP_DATABASE",
             K(ret), "stmt type", basic_stmt->get_stmt_type());
  } else {
    ObNeedPriv need_priv;
    const ObDropDatabaseStmt *stmt = static_cast<const ObDropDatabaseStmt*>(basic_stmt);
    if (OB_FAIL(ObPrivilegeCheck::can_do_drop_operation_on_db(session_priv, stmt->get_database_name()))) {
      LOG_WARN("Can not drop information_schema database", K(ret));
    } else {
      need_priv.db_ = stmt->get_database_name();
      need_priv.priv_set_ = OB_PRIV_DROP;
      need_priv.priv_level_ = OB_PRIV_DB_LEVEL;
      ADD_NEED_PRIV(need_priv);
    }
  }
  return ret;
}

int get_create_table_stmt_need_privs(
    const ObSessionPrivInfo &session_priv,
    const ObStmt *basic_stmt,
    ObIArray<ObNeedPriv> &need_privs)
{
  int ret = OB_SUCCESS;
  bool need_check = false;
  if (OB_ISNULL(basic_stmt)) {
    ret = OB_INVALID_ARGUMENT;
    LOG_WARN("Basic stmt should be not be NULL", K(ret));
  } else if (OB_UNLIKELY(stmt::T_CREATE_TABLE != basic_stmt->get_stmt_type())) {
    ret = OB_INVALID_ARGUMENT;
    LOG_WARN("Stmt type should be T_CREATE_TABLE", K(ret), "stmt type", basic_stmt->get_stmt_type());
  } else {
    ObNeedPriv need_priv;
    const ObCreateTableStmt *stmt = static_cast<const ObCreateTableStmt*>(basic_stmt);
    if (OB_FAIL(ret)) {
    } else if (OB_FAIL(ObPrivilegeCheck::can_do_operation_on_db(session_priv, stmt->get_database_name()))) {
      LOG_WARN("Can not create table in information_schema database", K(session_priv), K(ret));
    } else if (stmt->is_view_table()){
      for (int64_t i = 0; i < stmt->get_view_need_privs().count(); i++) {
        LOG_INFO("output need privs", K(stmt->get_view_need_privs().at(i)), K(i));
      }
      if (OB_FAIL(need_privs.assign(stmt->get_view_need_privs()))) {
        LOG_WARN("fail to assign need_privs", K(ret));
      }
    } else {
      const ObSelectStmt *select_stmt = stmt->get_sub_select();
      const ObSArray<obrpc::ObCreateForeignKeyArg> &foreign_keys = stmt->get_read_only_foreign_key_arg_list();
      if (NULL != select_stmt) {
        need_priv.priv_set_ = OB_PRIV_CREATE | OB_PRIV_INSERT;
      } else {
        need_priv.priv_set_ = OB_PRIV_CREATE;
      }
      need_priv.db_ = stmt->get_database_name();
      need_priv.table_ = stmt->get_table_name();
      need_priv.priv_level_ = OB_PRIV_TABLE_LEVEL;
      ADD_NEED_PRIV(need_priv);
      if (OB_SUCC(ret) && NULL != select_stmt) {
        OZ (ObPrivilegeCheck::get_stmt_need_privs(session_priv, select_stmt, need_privs));
      }
      // check references privilege
      if (OB_SUCC(ret)) {
        if (OB_FAIL(ObPrivilegeCheck::get_priv_need_check(session_priv,
                                                          ObCompatFeatureType::MYSQL_REFERENCES_PRIV_ENHANCE,
                                                          need_check))) {
          LOG_WARN("failed to get priv need check", K(ret));
        } else if (lib::is_mysql_mode() && need_check) {
          for (int64_t i = 0; OB_SUCC(ret) && i < foreign_keys.count(); i++) {
            need_priv.db_ = foreign_keys.at(i).parent_database_;
            need_priv.table_ = foreign_keys.at(i).parent_table_;
            need_priv.priv_level_ = OB_PRIV_TABLE_LEVEL;
            need_priv.priv_set_ = OB_PRIV_REFERENCES;
            ADD_NEED_PRIV(need_priv);
          }
        }
      }

      // check location object
      if (OB_SUCC(ret) && ObTableType::EXTERNAL_TABLE == stmt->get_table_type()
          && OB_INVALID_ID != stmt->get_external_location_id()) {
        ObSchemaGetterGuard schema_guard;
        const ObLocationSchema *location_schema = NULL;
        CK(GCTX.schema_service_ != NULL);
        OZ(GCTX.schema_service_->get_tenant_schema_guard(session_priv.tenant_id_, schema_guard));
        if (OB_FAIL(schema_guard.get_location_schema_by_id(session_priv.tenant_id_,
                                                           stmt->get_external_location_id(),
                                                           location_schema))) {
          LOG_WARN("failed to get location schema");
        } else if (OB_ISNULL(location_schema)) {
          ret = OB_ERR_UNEXPECTED;
          LOG_WARN("location schema is null");
        } else {
          ObNeedPriv tmp_need_priv;
          tmp_need_priv.db_ = stmt->get_database_name();
          tmp_need_priv.table_ = location_schema->get_location_name();
          tmp_need_priv.priv_level_ = OB_PRIV_OBJECT_LEVEL;
          tmp_need_priv.priv_set_ = OB_PRIV_READ;
          tmp_need_priv.obj_type_ = ObObjectType::LOCATION;
          ADD_NEED_PRIV(tmp_need_priv);
        }
      }
    }
  }
  return ret;
}

int get_drop_table_stmt_need_privs(
    const ObSessionPrivInfo &session_priv,
    const ObStmt *basic_stmt,
    ObIArray<ObNeedPriv> &need_privs)
{
  int ret = OB_SUCCESS;
  if (OB_ISNULL(basic_stmt)) {
    ret = OB_INVALID_ARGUMENT;
    LOG_WARN("Basic stmt should be not be NULL", K(ret));
  } else if (OB_UNLIKELY(stmt::T_DROP_TABLE != basic_stmt->get_stmt_type())) {
    ret = OB_INVALID_ARGUMENT;
    LOG_WARN("Stmt type should be T_DROP_TABLE",
             K(ret), "stmt type", basic_stmt->get_stmt_type());
  } else {
    ObNeedPriv need_priv;
    const ObDropTableStmt *stmt = static_cast<const ObDropTableStmt*>(basic_stmt);
    const ObIArray<obrpc::ObTableItem> &tables = stmt->get_drop_table_arg().tables_;
    for (int64_t i = 0; OB_SUCC(ret) && i < tables.count(); i++) {
      const obrpc::ObTableItem &table_item = tables.at(i);
      if (OB_FAIL(ObPrivilegeCheck::can_do_operation_on_db(session_priv, table_item.database_name_))) {
        LOG_WARN("Can not drop table in information_schema database", K(session_priv), K(ret));
      } else if (ObPrivilegeCheck::is_mysql_org_table(table_item.database_name_, table_item.table_name_)
                 && session_priv.tenant_id_ != OB_SYS_TENANT_ID) {
        ret = OB_ERR_NO_TABLE_PRIVILEGE;
        LOG_USER_ERROR(OB_ERR_NO_TABLE_PRIVILEGE, (int)strlen("DROP"), "DROP",
                       session_priv.user_name_.length(), session_priv.user_name_.ptr(),
                       session_priv.host_name_.length(),session_priv.host_name_.ptr(),
                       table_item.table_name_.length(), table_item.table_name_.ptr());
      } else {
        need_priv.db_ = table_item.database_name_;
        need_priv.table_ = table_item.table_name_;
        need_priv.priv_set_ = OB_PRIV_DROP;
        need_priv.priv_level_ = OB_PRIV_TABLE_LEVEL;
        ADD_NEED_PRIV(need_priv);
      }
    }
    if ((share::schema::TMP_TABLE == stmt->get_drop_table_arg().table_type_
        || share::schema::TMP_TABLE_ALL == stmt->get_drop_table_arg().table_type_)
        && common::OB_INVALID_ID != stmt->get_drop_table_arg().session_id_
        && session_priv.tenant_id_ != OB_SYS_TENANT_ID) {
        ret = OB_ERR_NO_PRIVILEGE;
        LOG_USER_ERROR(OB_ERR_NO_PRIVILEGE, "SUPER");
      }
  }
  return ret;
}

int get_create_sequence_stmt_need_privs(
    const ObSessionPrivInfo &session_priv,
    const ObStmt *basic_stmt,
    ObIArray<ObNeedPriv> &need_privs)
{
  int ret = OB_SUCCESS;
  ObNeedPriv need_priv;
  bool need_check = false;
  if (OB_ISNULL(basic_stmt)) {
    ret = OB_INVALID_ARGUMENT;
    LOG_WARN("Basic stmt should be not be NULL", K(ret));
  } else if (OB_UNLIKELY(stmt::T_CREATE_SEQUENCE != basic_stmt->get_stmt_type())) {
    ret = OB_INVALID_ARGUMENT;
    LOG_WARN("Stmt type should be T_CREATE_SEQUENCE", K(ret), "stmt type", basic_stmt->get_stmt_type());
  } else if (OB_FAIL(ObPrivilegeCheck::get_priv_need_check(session_priv,
                                          ObCompatFeatureType::MYSQL_PRIV_ENHANCE, need_check))) {
    LOG_WARN("failed to get priv need check", K(ret));
  } else if (lib::is_mysql_mode() && need_check) {
    const ObCreateSequenceStmt *stmt = static_cast<const ObCreateSequenceStmt*>(basic_stmt);
    if (OB_FAIL(ObPrivilegeCheck::can_do_operation_on_db(session_priv,
                                                         stmt->get_arg().get_database_name()))) {
      LOG_WARN("Can not create sequence in current database", K(session_priv), K(ret));
    } else {
      need_priv.db_ = stmt->get_arg().get_database_name();
      need_priv.priv_set_ = OB_PRIV_CREATE;
      need_priv.priv_level_ = OB_PRIV_DB_LEVEL;
      ADD_NEED_PRIV(need_priv);
    }
  }
  return ret;
}

int get_alter_sequence_stmt_need_privs(
    const ObSessionPrivInfo &session_priv,
    const ObStmt *basic_stmt,
    ObIArray<ObNeedPriv> &need_privs)
{
  int ret = OB_SUCCESS;
  ObNeedPriv need_priv;
  bool need_check = false;
  if (OB_ISNULL(basic_stmt)) {
    ret = OB_INVALID_ARGUMENT;
    LOG_WARN("Basic stmt should be not be NULL", K(ret));
  } else if (OB_UNLIKELY(stmt::T_ALTER_SEQUENCE != basic_stmt->get_stmt_type())) {
    ret = OB_INVALID_ARGUMENT;
    LOG_WARN("Stmt type should be T_ALTER_SEQUENCE", K(ret), "stmt type", basic_stmt->get_stmt_type());
  } else if (OB_FAIL(ObPrivilegeCheck::get_priv_need_check(session_priv,
                                          ObCompatFeatureType::MYSQL_PRIV_ENHANCE, need_check))) {
    LOG_WARN("failed to get priv need check", K(ret));
  } else if (lib::is_mysql_mode() && need_check) {
    const ObAlterSequenceStmt *stmt = static_cast<const ObAlterSequenceStmt*>(basic_stmt);
    if (OB_FAIL(ObPrivilegeCheck::can_do_operation_on_db(session_priv,
                                                         stmt->get_arg().get_database_name()))) {
      LOG_WARN("Can not alter sequence in current database", K(session_priv), K(ret));
    } else {
      need_priv.db_ = stmt->get_arg().get_database_name();
      need_priv.priv_set_ = OB_PRIV_ALTER;
      need_priv.priv_level_ = OB_PRIV_DB_LEVEL;
      ADD_NEED_PRIV(need_priv);
    }
  }
  return ret;
}

int get_drop_sequence_stmt_need_privs(
    const ObSessionPrivInfo &session_priv,
    const ObStmt *basic_stmt,
    ObIArray<ObNeedPriv> &need_privs)
{
  int ret = OB_SUCCESS;
  ObNeedPriv need_priv;
  bool need_check = false;
  if (OB_ISNULL(basic_stmt)) {
    ret = OB_INVALID_ARGUMENT;
    LOG_WARN("Basic stmt should be not be NULL", K(ret));
  } else if (OB_UNLIKELY(stmt::T_DROP_SEQUENCE != basic_stmt->get_stmt_type())) {
    ret = OB_INVALID_ARGUMENT;
    LOG_WARN("Stmt type should be T_DROP_SEQUENCE", K(ret), "stmt type", basic_stmt->get_stmt_type());
  } else if (OB_FAIL(ObPrivilegeCheck::get_priv_need_check(session_priv,
                                          ObCompatFeatureType::MYSQL_PRIV_ENHANCE, need_check))) {
    LOG_WARN("failed to get priv need check", K(ret));
  } else if (lib::is_mysql_mode() && need_check) {
    const ObDropSequenceStmt *stmt = static_cast<const ObDropSequenceStmt*>(basic_stmt);
    if (OB_FAIL(ObPrivilegeCheck::can_do_operation_on_db(session_priv,
                                                         stmt->get_arg().get_database_name()))) {
      LOG_WARN("Can not drop sequence in current database", K(session_priv), K(ret));
    } else {
      need_priv.db_ = stmt->get_arg().get_database_name();
      need_priv.priv_set_ = OB_PRIV_DROP;
      need_priv.priv_level_ = OB_PRIV_DB_LEVEL;
      ADD_NEED_PRIV(need_priv);
    }
  }
  return ret;
}

int get_create_outline_stmt_need_privs(
    const ObSessionPrivInfo &session_priv,
    const ObStmt *basic_stmt,
    ObIArray<ObNeedPriv> &need_privs)
{
  int ret = OB_SUCCESS;
  ObNeedPriv need_priv;
  bool need_check = false;
  if (OB_ISNULL(basic_stmt)) {
    ret = OB_INVALID_ARGUMENT;
    LOG_WARN("Basic stmt should be not be NULL", K(ret));
  } else if (OB_UNLIKELY(stmt::T_CREATE_OUTLINE != basic_stmt->get_stmt_type())) {
    ret = OB_INVALID_ARGUMENT;
    LOG_WARN("Stmt type should be T_CREATE_OUTLINE", K(ret), "stmt type", basic_stmt->get_stmt_type());
  } else if (OB_FAIL(ObPrivilegeCheck::get_priv_need_check(session_priv,
                                          ObCompatFeatureType::MYSQL_PRIV_ENHANCE, need_check))) {
    LOG_WARN("failed to get priv need check", K(ret));
  } else if (lib::is_mysql_mode() && need_check) {
    const ObCreateOutlineStmt *stmt = static_cast<const ObCreateOutlineStmt*>(basic_stmt);
    if (OB_FAIL(ObPrivilegeCheck::can_do_operation_on_db(session_priv,
                                                         stmt->get_create_outline_arg().db_name_))) {
      LOG_WARN("Can not create outline in current database", K(session_priv), K(ret));
    } else {
      need_priv.db_ = stmt->get_create_outline_arg().db_name_;
      need_priv.priv_set_ = OB_PRIV_CREATE;
      need_priv.priv_level_ = OB_PRIV_DB_LEVEL;
      ADD_NEED_PRIV(need_priv);
    }
  }
  return ret;
}

int get_alter_outline_stmt_need_privs(
    const ObSessionPrivInfo &session_priv,
    const ObStmt *basic_stmt,
    ObIArray<ObNeedPriv> &need_privs)
{
  int ret = OB_SUCCESS;
  ObNeedPriv need_priv;
  bool need_check = false;
  if (OB_ISNULL(basic_stmt)) {
    ret = OB_INVALID_ARGUMENT;
    LOG_WARN("Basic stmt should be not be NULL", K(ret));
  } else if (OB_UNLIKELY(stmt::T_ALTER_OUTLINE != basic_stmt->get_stmt_type())) {
    ret = OB_INVALID_ARGUMENT;
    LOG_WARN("Stmt type should be T_ALTER_OUTLINE", K(ret), "stmt type", basic_stmt->get_stmt_type());
  } else if (OB_FAIL(ObPrivilegeCheck::get_priv_need_check(session_priv,
                                          ObCompatFeatureType::MYSQL_PRIV_ENHANCE, need_check))) {
    LOG_WARN("failed to get priv need check", K(ret));
  } else if (lib::is_mysql_mode() && need_check) {
    const ObAlterOutlineStmt *stmt = static_cast<const ObAlterOutlineStmt*>(basic_stmt);
    if (OB_FAIL(ObPrivilegeCheck::can_do_operation_on_db(session_priv,
                                                         stmt->get_alter_outline_arg().db_name_))) {
      LOG_WARN("Can not alter outline in current database", K(session_priv), K(ret));
    } else {
      need_priv.db_ = stmt->get_alter_outline_arg().db_name_;
      need_priv.priv_set_ = OB_PRIV_ALTER;
      need_priv.priv_level_ = OB_PRIV_DB_LEVEL;
      ADD_NEED_PRIV(need_priv);
    }
  }
  return ret;
}

int get_drop_outline_stmt_need_privs(
    const ObSessionPrivInfo &session_priv,
    const ObStmt *basic_stmt,
    ObIArray<ObNeedPriv> &need_privs)
{
  int ret = OB_SUCCESS;
  ObNeedPriv need_priv;
  bool need_check = false;
  if (OB_ISNULL(basic_stmt)) {
    ret = OB_INVALID_ARGUMENT;
    LOG_WARN("Basic stmt should be not be NULL", K(ret));
  } else if (OB_UNLIKELY(stmt::T_DROP_OUTLINE != basic_stmt->get_stmt_type())) {
    ret = OB_INVALID_ARGUMENT;
    LOG_WARN("Stmt type should be T_DROP_OUTLINE", K(ret), "stmt type", basic_stmt->get_stmt_type());
  } else if (OB_FAIL(ObPrivilegeCheck::get_priv_need_check(session_priv,
                                          ObCompatFeatureType::MYSQL_PRIV_ENHANCE, need_check))) {
    LOG_WARN("failed to get priv need check", K(ret));
  } else if (lib::is_mysql_mode() && need_check) {
    const ObDropOutlineStmt *stmt = static_cast<const ObDropOutlineStmt*>(basic_stmt);
    if (OB_FAIL(ObPrivilegeCheck::can_do_operation_on_db(session_priv,
                                                         stmt->get_drop_outline_arg().db_name_))) {
      LOG_WARN("Can not drop outline in current database", K(session_priv), K(ret));
    } else {
      need_priv.db_ = stmt->get_drop_outline_arg().db_name_;
      need_priv.priv_set_ = OB_PRIV_DROP;
      need_priv.priv_level_ = OB_PRIV_DB_LEVEL;
      ADD_NEED_PRIV(need_priv);
    }
  }
  return ret;
}

int get_create_synonym_priv(
    const ObSessionPrivInfo &session_priv,
    const ObStmt *basic_stmt,
    ObIArray<ObNeedPriv> &need_privs)
{
  int ret = OB_SUCCESS;
  return ret;
}

int get_create_tablespace_priv(
    const ObSessionPrivInfo &session_priv,
    const ObStmt *basic_stmt,
    ObIArray<ObNeedPriv> &need_privs)
{
  int ret = OB_SUCCESS;
  ObNeedPriv need_priv;
  bool need_check = false;
  if (OB_ISNULL(basic_stmt)) {
    ret = OB_INVALID_ARGUMENT;
    LOG_WARN("Basic stmt should be not be NULL", K(ret));
  } else if (OB_UNLIKELY(stmt::T_CREATE_TABLESPACE != basic_stmt->get_stmt_type()
                         && stmt::T_DROP_TABLESPACE != basic_stmt->get_stmt_type()
                         && stmt::T_ALTER_TABLESPACE != basic_stmt->get_stmt_type())) {
    ret = OB_ERR_UNEXPECTED;
    LOG_WARN("unexpected stmt type", K(basic_stmt->get_stmt_type()), K(ret));
  } else if (OB_FAIL(ObPrivilegeCheck::get_priv_need_check(session_priv,
                                          ObCompatFeatureType::MYSQL_PRIV_ENHANCE, need_check))) {
    LOG_WARN("failed to get priv need check", K(ret));
  } else if (lib::is_mysql_mode() && need_check) {
    need_priv.priv_set_ = OB_PRIV_CREATE_TABLESPACE;
    need_priv.priv_level_ = OB_PRIV_USER_LEVEL;
    ADD_NEED_PRIV(need_priv);
  }
  return ret;
}

int get_drop_synonym_priv(
    const ObSessionPrivInfo &session_priv,
    const ObStmt *basic_stmt,
    ObIArray<ObNeedPriv> &need_privs)
{
  int ret = OB_SUCCESS;
  return ret;
}

int get_create_index_stmt_need_privs(
    const ObSessionPrivInfo &session_priv,
    const ObStmt *basic_stmt,
    ObIArray<ObNeedPriv> &need_privs)
{
  int ret = OB_SUCCESS;
  if (OB_ISNULL(basic_stmt)) {
    ret = OB_INVALID_ARGUMENT;
    LOG_WARN("Basic stmt should be not be NULL", K(ret));
  } else if (OB_UNLIKELY(stmt::T_CREATE_INDEX != basic_stmt->get_stmt_type())) {
    ret = OB_INVALID_ARGUMENT;
    LOG_WARN("Stmt type should be T_CREATE_INDEX",
             K(ret), "stmt type", basic_stmt->get_stmt_type());
  } else {
    ObNeedPriv need_priv;
    const ObCreateIndexStmt *stmt = static_cast<const ObCreateIndexStmt*>(basic_stmt);
    if (OB_FAIL(ObPrivilegeCheck::can_do_operation_on_db(session_priv, stmt->get_database_name()))) {
      LOG_WARN("Can not create index in information_schema database", K(session_priv), K(ret));
    } else {
      need_priv.db_ = stmt->get_database_name();
      need_priv.table_ = stmt->get_table_name();
      need_priv.priv_set_ = OB_PRIV_INDEX;
      need_priv.priv_level_ = OB_PRIV_TABLE_LEVEL;
      ADD_NEED_PRIV(need_priv);
    }
  }
  return ret;
}

int get_drop_index_stmt_need_privs(
    const ObSessionPrivInfo &session_priv,
    const ObStmt *basic_stmt,
    ObIArray<ObNeedPriv> &need_privs)
{
  int ret = OB_SUCCESS;
  if (OB_ISNULL(basic_stmt)) {
    ret = OB_INVALID_ARGUMENT;
    LOG_WARN("Basic stmt should be not be NULL", K(ret));
  } else if (OB_UNLIKELY(stmt::T_DROP_INDEX != basic_stmt->get_stmt_type())) {
    ret = OB_INVALID_ARGUMENT;
    LOG_WARN("Stmt type should be T_DROP_INDEX",
             K(ret), "stmt type", basic_stmt->get_stmt_type());
  } else {
    ObNeedPriv need_priv;
    const ObDropIndexStmt *stmt = static_cast<const ObDropIndexStmt*>(basic_stmt);
    if (OB_FAIL(ObPrivilegeCheck::can_do_operation_on_db(session_priv, stmt->get_database_name()))) {
      LOG_WARN("Can not drop index in information_schema database", K(session_priv), K(ret));
    } else {
      need_priv.db_ = stmt->get_database_name();
      need_priv.table_ = stmt->get_table_name();
      need_priv.priv_set_ = OB_PRIV_INDEX;
      need_priv.priv_level_ = OB_PRIV_TABLE_LEVEL;
      ADD_NEED_PRIV(need_priv);
    }
  }
  return ret;
}

int get_create_mlog_stmt_need_privs(
    const ObSessionPrivInfo &session_priv,
    const ObStmt *basic_stmt,
    ObIArray<ObNeedPriv> &need_privs)
{
  int ret = OB_SUCCESS;
  if (OB_ISNULL(basic_stmt)) {
    ret = OB_INVALID_ARGUMENT;
    LOG_WARN("Basic stmt should be not be NULL", KR(ret));
  } else if (OB_UNLIKELY(stmt::T_CREATE_MLOG != basic_stmt->get_stmt_type())) {
    ret = OB_INVALID_ARGUMENT;
    LOG_WARN("Stmt type should be T_CREATE_MLOG",
        KR(ret), "stmt type", basic_stmt->get_stmt_type());
  } else {
    ObNeedPriv need_priv;
    const ObCreateMLogStmt *stmt = static_cast<const ObCreateMLogStmt*>(basic_stmt);
    if (OB_FAIL(ObPrivilegeCheck::can_do_operation_on_db(session_priv, stmt->get_database_name()))) {
      LOG_WARN("Can not create materialized view log in information_schema database",
          KR(ret), K(session_priv));
    } else {
      // create mlog requires select privilege on base table
      need_priv.db_ = stmt->get_database_name();
      need_priv.table_ = stmt->get_table_name();
      need_priv.priv_set_ = OB_PRIV_SELECT;
      need_priv.priv_level_ = OB_PRIV_TABLE_LEVEL;
      ADD_NEED_PRIV(need_priv);

      need_priv.db_ = stmt->get_database_name();
      need_priv.table_ = stmt->get_mlog_name();
      need_priv.priv_set_ = OB_PRIV_CREATE;
      need_priv.priv_level_ = OB_PRIV_TABLE_LEVEL;
      ADD_NEED_PRIV(need_priv);
    }
  }
  return ret;
}

int get_drop_mlog_stmt_need_privs(
    const ObSessionPrivInfo &session_priv,
    const ObStmt *basic_stmt,
    ObIArray<ObNeedPriv> &need_privs)
{
  int ret = OB_SUCCESS;
  if (OB_ISNULL(basic_stmt)) {
    ret = OB_INVALID_ARGUMENT;
    LOG_WARN("Basic stmt should be not be NULL", KR(ret));
  } else if (OB_UNLIKELY(stmt::T_DROP_MLOG != basic_stmt->get_stmt_type())) {
    ret = OB_INVALID_ARGUMENT;
    LOG_WARN("Stmt type should be T_DROP_MLOG",
        KR(ret), "stmt type", basic_stmt->get_stmt_type());
  } else {
    ObNeedPriv need_priv;
    const ObDropMLogStmt *stmt = static_cast<const ObDropMLogStmt*>(basic_stmt);
    if (OB_FAIL(ObPrivilegeCheck::can_do_operation_on_db(session_priv, stmt->get_database_name()))) {
      LOG_WARN("Can not drop materialized view log in information_schema database",
          KR(ret), K(session_priv));
    } else {
      need_priv.db_ = stmt->get_database_name();
      need_priv.table_ = stmt->get_mlog_name();
      need_priv.priv_set_ = OB_PRIV_DROP;
      need_priv.priv_level_ = OB_PRIV_TABLE_LEVEL;
      ADD_NEED_PRIV(need_priv);
    }
  }
  return ret;
}

int get_grant_stmt_need_privs(
    const ObSessionPrivInfo &session_priv,
    const ObStmt *basic_stmt,
    ObIArray<ObNeedPriv> &need_privs)
{
  int ret = OB_SUCCESS;
  if (OB_ISNULL(basic_stmt)) {
    ret = OB_INVALID_ARGUMENT;
    LOG_WARN("Basic stmt should be not be NULL", K(ret));
  } else if (OB_UNLIKELY(stmt::T_GRANT != basic_stmt->get_stmt_type())) {
    ret = OB_INVALID_ARGUMENT;
    LOG_WARN("Stmt type should be T_GRANT",
             K(ret), "stmt type", basic_stmt->get_stmt_type());
  } else {
    ObNeedPriv need_priv;
    const ObGrantStmt *stmt = static_cast<const ObGrantStmt *>(basic_stmt);
    if (OB_FAIL(ObPrivilegeCheck::can_do_grant_on_db_table(session_priv, stmt->get_priv_set(),
                                         stmt->get_database_name(),
                                         stmt->get_table_name()))) {
      LOG_WARN("Can not grant information_schema database", K(ret));
    } else if (stmt->need_create_user_priv() &&
               !(session_priv.user_priv_set_ & OB_PRIV_CREATE_USER)) {
      ret = OB_ERR_CREATE_USER_WITH_GRANT;
      LOG_WARN("Need create user priv", K(ret), "user priv", ObPrintPrivSet(session_priv.user_priv_set_));
    } else if (is_root_user(session_priv.user_id_)) {
      //not neccessary
    } else {
      need_priv.catalog_ = stmt->get_catalog_name();
      need_priv.db_ = stmt->get_database_name();
      need_priv.table_ = stmt->get_table_name();
      need_priv.priv_set_ = stmt->get_priv_set() | OB_PRIV_GRANT;
      need_priv.priv_level_ = stmt->get_grant_level();
      need_priv.obj_type_ = stmt->get_object_type();
      ADD_NEED_PRIV(need_priv);
      #define DEF_COLUM_NEED_PRIV(priv_prefix, priv_type) \
        ObNeedPriv priv_prefix##_need_priv;  \
        priv_prefix##_need_priv.db_ = stmt->get_database_name(); \
        priv_prefix##_need_priv.table_ = stmt->get_table_name(); \
        priv_prefix##_need_priv.priv_set_ = priv_type | OB_PRIV_GRANT; \
        priv_prefix##_need_priv.priv_level_ = OB_PRIV_TABLE_LEVEL;

      DEF_COLUM_NEED_PRIV(sel, OB_PRIV_SELECT);
      DEF_COLUM_NEED_PRIV(ins, OB_PRIV_INSERT);
      DEF_COLUM_NEED_PRIV(upd, OB_PRIV_UPDATE);
      DEF_COLUM_NEED_PRIV(ref, OB_PRIV_REFERENCES);

      #undef DEF_COLUM_NEED_PRIV

      for (int64_t i = 0; OB_SUCC(ret) && i < stmt->get_column_privs().count(); i++) {
        const ObString &column_name = stmt->get_column_privs().at(i).first;
        ObPrivSet priv_set = stmt->get_column_privs().at(i).second;
        if ((stmt->get_priv_set() & priv_set) != 0) {
          //contains in table priv already.
        } else if (0 != (priv_set & OB_PRIV_SELECT)) {
          ret = sel_need_priv.columns_.push_back(column_name);
        } else if (0 != (priv_set & OB_PRIV_INSERT)) {
          ret = ins_need_priv.columns_.push_back(column_name);
        } else if (0 != (priv_set & OB_PRIV_UPDATE)) {
          ret = upd_need_priv.columns_.push_back(column_name);
        } else if (0 != (priv_set & OB_PRIV_REFERENCES)) {
          ret = ref_need_priv.columns_.push_back(column_name);
        }
      }

      #define ADD_COLUMN_NEED_PRIV(priv_prefix) \
        if (OB_FAIL(ret)) { \
        } else if (priv_prefix##_need_priv.columns_.count() != 0) { \
          ADD_NEED_PRIV(priv_prefix##_need_priv); \
        }

      ADD_COLUMN_NEED_PRIV(sel);
      ADD_COLUMN_NEED_PRIV(ins);
      ADD_COLUMN_NEED_PRIV(upd);
      ADD_COLUMN_NEED_PRIV(ref);
      #undef ADD_COLUMN_NEED_PRIV
    }
  }
  return ret;
}

int get_revoke_stmt_need_privs(
    const ObSessionPrivInfo &session_priv,
    const ObStmt *basic_stmt,
    ObIArray<ObNeedPriv> &need_privs)
{
  int ret = OB_SUCCESS;
  bool check_revoke_all_user_create_user = false;
  if (OB_ISNULL(basic_stmt)) {
    ret = OB_INVALID_ARGUMENT;
    LOG_WARN("Basic stmt should be not be NULL", K(ret));
  } else if (OB_UNLIKELY(stmt::T_REVOKE != basic_stmt->get_stmt_type())) {
    ret = OB_INVALID_ARGUMENT;
    LOG_WARN("Stmt type should be T_REVOKE",
             K(ret), "stmt type", basic_stmt->get_stmt_type());
  } else if (OB_FAIL(ObPrivilegeCheck::get_priv_need_check(session_priv,
                                          ObCompatFeatureType::MYSQL_USER_REVOKE_ALL_ENHANCE, check_revoke_all_user_create_user))) {
        LOG_WARN("failed to get priv need check", K(ret));
  } else if (is_root_user(session_priv.user_id_)) {
    // not necessary
  } else {
    ObNeedPriv need_priv;
    const ObRevokeStmt *stmt = static_cast<const ObRevokeStmt *>(basic_stmt);
    if (check_revoke_all_user_create_user &&
      stmt->get_grant_level() == OB_PRIV_USER_LEVEL && stmt->get_priv_set() == OB_PRIV_ALL) {
      need_priv.db_ = stmt->get_database_name();
      need_priv.table_ = stmt->get_table_name();
      need_priv.priv_set_ = OB_PRIV_CREATE_USER;
      need_priv.priv_level_ = stmt->get_grant_level();
      need_priv.obj_type_ = stmt->get_object_type();
      ADD_NEED_PRIV(need_priv);

      ObSchemaGetterGuard schema_guard;
      bool need_add = false;
      CK (GCTX.schema_service_ != NULL);
      OZ(GCTX.schema_service_->get_tenant_schema_guard(session_priv.tenant_id_, schema_guard));
      for (int i = 0; OB_SUCC(ret) && i < stmt->get_users().count(); i++) {
        const ObUserInfo *user_info = NULL;
        OZ(schema_guard.get_user_info(session_priv.tenant_id_, stmt->get_users().at(i), user_info));
        CK (user_info != NULL);
        OX(need_add = (0 != (user_info->get_priv_set() & OB_PRIV_SUPER)));
      }
      if (OB_FAIL(ret)) {
      } else if (need_add) { //mysql8.0 if exists dynamic privs, then need SYSTEM_USER dynamic privilge to revoke all, now use SUPER to do so.
        need_priv.catalog_ = stmt->get_catalog_name();
        need_priv.db_ = stmt->get_database_name();
        need_priv.table_ = stmt->get_table_name();
        need_priv.priv_set_ = OB_PRIV_SUPER;
        need_priv.priv_level_ = stmt->get_grant_level();
        need_priv.obj_type_ = stmt->get_object_type();
        ADD_NEED_PRIV(need_priv);
      }
    } else if (OB_FAIL(ObPrivilegeCheck::can_do_grant_on_db_table(session_priv, stmt->get_priv_set(),
                                         stmt->get_database_name(),
                                         stmt->get_table_name()))) {
      LOG_WARN("Can not grant information_schema database", K(ret));
    } else if (lib::is_mysql_mode() && stmt->get_revoke_all()) {
      //check privs at resolver
    } else {
      need_priv.catalog_ = stmt->get_catalog_name();
      need_priv.db_ = stmt->get_database_name();
      need_priv.table_ = stmt->get_table_name();
      need_priv.priv_set_ = stmt->get_priv_set() | OB_PRIV_GRANT;
      need_priv.priv_level_ = stmt->get_grant_level();
      need_priv.obj_type_ = stmt->get_object_type();
      bool check_revoke_all_with_pl_priv = false;
      if (OB_FAIL(ObPrivilegeCheck::get_priv_need_check(session_priv,
                          ObCompatFeatureType::MYSQL_USER_REVOKE_ALL_WITH_PL_PRIV_CHECK, check_revoke_all_with_pl_priv))) {
        LOG_WARN("failed to get priv need check", K(ret));
      } else if (check_revoke_all_with_pl_priv) {
        //do nothing
      } else {
        need_priv.priv_set_ &= ~(OB_PRIV_EXECUTE | OB_PRIV_ALTER_ROUTINE | OB_PRIV_CREATE_ROUTINE);
      }

      ADD_NEED_PRIV(need_priv);
      #define DEF_COLUM_NEED_PRIV(priv_prefix, priv_type) \
        ObNeedPriv priv_prefix##_need_priv;  \
        priv_prefix##_need_priv.db_ = stmt->get_database_name(); \
        priv_prefix##_need_priv.table_ = stmt->get_table_name(); \
        priv_prefix##_need_priv.priv_set_ = priv_type | OB_PRIV_GRANT; \
        priv_prefix##_need_priv.priv_level_ = OB_PRIV_TABLE_LEVEL;

      DEF_COLUM_NEED_PRIV(sel, OB_PRIV_SELECT);
      DEF_COLUM_NEED_PRIV(ins, OB_PRIV_INSERT);
      DEF_COLUM_NEED_PRIV(upd, OB_PRIV_UPDATE);
      DEF_COLUM_NEED_PRIV(ref, OB_PRIV_REFERENCES);

      #undef DEF_COLUM_NEED_PRIV

      for (int64_t i = 0; OB_SUCC(ret) && i < stmt->get_column_privs().count(); i++) {
        const ObString &column_name = stmt->get_column_privs().at(i).first;
        ObPrivSet priv_set = stmt->get_column_privs().at(i).second;
        if ((stmt->get_priv_set() & priv_set) != 0) {
          //contains in table priv already.
        } else if (0 != (priv_set & OB_PRIV_SELECT)) {
          ret = sel_need_priv.columns_.push_back(column_name);
        } else if (0 != (priv_set & OB_PRIV_INSERT)) {
          ret = ins_need_priv.columns_.push_back(column_name);
        } else if (0 != (priv_set & OB_PRIV_UPDATE)) {
          ret = upd_need_priv.columns_.push_back(column_name);
        } else if (0 != (priv_set & OB_PRIV_REFERENCES)) {
          ret = ref_need_priv.columns_.push_back(column_name);
        }
      }

      #define ADD_COLUMN_NEED_PRIV(priv_prefix) \
        if (OB_FAIL(ret)) { \
        } else if (priv_prefix##_need_priv.columns_.count() != 0) { \
          ADD_NEED_PRIV(priv_prefix##_need_priv); \
        }

      ADD_COLUMN_NEED_PRIV(sel);
      ADD_COLUMN_NEED_PRIV(ins);
      ADD_COLUMN_NEED_PRIV(upd);
      ADD_COLUMN_NEED_PRIV(ref);
      #undef ADD_COLUMN_NEED_PRIV
    }
  }
  return ret;
}

int get_create_user_privs(
    const ObSessionPrivInfo &session_priv,
    const ObStmt *basic_stmt,
    ObIArray<ObNeedPriv> &need_privs)
{
  UNUSED(session_priv);
  int ret = OB_SUCCESS;
  if (OB_ISNULL(basic_stmt)) {
    ret = OB_INVALID_ARGUMENT;
    LOG_WARN("Basic stmt should be not be NULL", K(ret));
  } else {
    ObNeedPriv need_priv;
    stmt::StmtType stmt_type = basic_stmt->get_stmt_type();
    switch (stmt_type) {//TODO deleted switch
      case stmt::T_LOCK_USER :
      case stmt::T_ALTER_USER_PROFILE :
      case stmt::T_ALTER_USER_PRIMARY_ZONE:
      case stmt::T_ALTER_USER:
      case stmt::T_SET_PASSWORD :
      case stmt::T_RENAME_USER :
      case stmt::T_DROP_USER :
      case stmt::T_CREATE_USER : {
        if (stmt::T_SET_PASSWORD == stmt_type
            && static_cast<const ObSetPasswordStmt*>(basic_stmt)->get_for_current_user()) {
        } else if (stmt::T_ALTER_USER_PROFILE == stmt_type
                   && lib::is_mysql_mode()
                   && !!static_cast<const ObAlterUserProfileStmt*>(basic_stmt)->get_set_role_flag()) {
        } else {
          need_priv.priv_set_ = OB_PRIV_CREATE_USER;
          need_priv.priv_level_ = OB_PRIV_USER_LEVEL;
          ADD_NEED_PRIV(need_priv);
        }
        break;
      }
      default: {
        ret = OB_INVALID_ARGUMENT;
        LOG_WARN("Stmt type not in types dealt in this function", K(ret), K(stmt_type));
        break;
      }
    }
  }
  return ret;
}

int get_role_privs(
    const ObSessionPrivInfo &session_priv,
    const ObStmt *basic_stmt,
    ObIArray<ObNeedPriv> &need_privs)
{
  UNUSED(session_priv);
  int ret = OB_SUCCESS;
  if (OB_ISNULL(basic_stmt)) {
    ret = OB_INVALID_ARGUMENT;
    LOG_WARN("Basic stmt should be not be NULL", K(ret));
  } else {
    ObNeedPriv need_priv;
    stmt::StmtType stmt_type = basic_stmt->get_stmt_type();
    switch (stmt_type) {
      case stmt::T_CREATE_ROLE: {
        need_priv.priv_set_ = OB_PRIV_CREATE_USER | OB_PRIV_CREATE_ROLE;
        need_priv.priv_level_ = OB_PRIV_USER_LEVEL;
        need_priv.priv_check_type_ = OB_PRIV_CHECK_ANY;
        ADD_NEED_PRIV(need_priv);
        break;
      }
      case stmt::T_DROP_ROLE: {
        need_priv.priv_set_ = OB_PRIV_CREATE_USER | OB_PRIV_DROP_ROLE;
        need_priv.priv_level_ = OB_PRIV_USER_LEVEL;
        need_priv.priv_check_type_ = OB_PRIV_CHECK_ANY;
        ADD_NEED_PRIV(need_priv);
        break;
      }
      case stmt::T_GRANT_ROLE: {
        need_priv.priv_set_ = OB_PRIV_SUPER;
        need_priv.priv_level_ = OB_PRIV_USER_LEVEL;
        ADD_NEED_PRIV(need_priv);
        break;
      }
      case stmt::T_REVOKE_ROLE: {
        need_priv.priv_set_ = OB_PRIV_SUPER;
        need_priv.priv_level_ = OB_PRIV_USER_LEVEL;
        ADD_NEED_PRIV(need_priv);
        break;
      }
      default: {
        ret = OB_INVALID_ARGUMENT;
        LOG_WARN("Stmt type not in types dealt in this function", K(ret), K(stmt_type));
        break;
      }
    }
  }
  return ret;
}

int get_catalog_privs(
    const ObSessionPrivInfo &session_priv,
    const ObStmt *basic_stmt,
    ObIArray<ObNeedPriv> &need_privs)
{
  int ret = OB_SUCCESS;
  if (OB_ISNULL(basic_stmt)) {
    ret = OB_INVALID_ARGUMENT;
    LOG_WARN("Basic stmt should be not be NULL", K(ret));
  } else {
    ObNeedPriv need_priv;
    stmt::StmtType stmt_type = basic_stmt->get_stmt_type();
    switch (stmt_type) {
      case stmt::T_CREATE_CATALOG:
      case stmt::T_ALTER_CATALOG:
      case stmt::T_DROP_CATALOG: {
        need_priv.priv_set_ = OB_PRIV_CREATE_CATALOG;
        need_priv.priv_level_ = OB_PRIV_USER_LEVEL;
        ADD_NEED_PRIV(need_priv);
        break;
      }
      default: {
        ret = OB_INVALID_ARGUMENT;
        LOG_WARN("Stmt type not in types dealt in this function", K(ret), K(stmt_type));
        break;
      }
    }
  }
  return ret;
}

int get_location_privs(const ObSessionPrivInfo &session_priv,
                       const ObStmt *basic_stmt,
                       ObIArray<ObNeedPriv> &need_privs)
{
  int ret = OB_SUCCESS;
  if (OB_ISNULL(basic_stmt)) {
    ret = OB_INVALID_ARGUMENT;
    LOG_WARN("Basic stmt should be not be NULL", K(ret));
  } else if (lib::is_oracle_mode()) {
    ret = no_priv_needed(session_priv, basic_stmt, need_privs);
  } else {
    ObNeedPriv need_priv;
    stmt::StmtType stmt_type = basic_stmt->get_stmt_type();
    switch (stmt_type) {
      case stmt::T_CREATE_LOCATION:
      case stmt::T_DROP_LOCATION: {
        need_priv.priv_set_ = OB_PRIV_CREATE_LOCATION;
        need_priv.priv_level_ = OB_PRIV_USER_LEVEL;
        ADD_NEED_PRIV(need_priv);
        break;
      }
      default: {
        ret = OB_INVALID_ARGUMENT;
        LOG_WARN("Stmt type not in types dealt in this function", K(ret), K(stmt_type));
        break;
      }
    }
  }
  return ret;
}

int get_location_util_privs(const ObSessionPrivInfo &session_priv,
                            const ObStmt *basic_stmt,
                            ObIArray<ObNeedPriv> &need_privs)
{
  int ret = OB_SUCCESS;
  if (OB_ISNULL(basic_stmt)) {
    ret = OB_INVALID_ARGUMENT;
    LOG_WARN("Basic stmt should be not be NULL", K(ret));
  } else if (lib::is_oracle_mode()) {
    ret = no_priv_needed(session_priv, basic_stmt, need_privs);
  } else {
    ObNeedPriv need_priv;
    stmt::StmtType stmt_type = basic_stmt->get_stmt_type();
    const ObLocationUtilsStmt *stmt = static_cast<const ObLocationUtilsStmt*>(basic_stmt);
    switch (stmt_type) {
      case stmt::T_LOCATION_UTILS: {
        ObNeedPriv tmp_need_priv;
        tmp_need_priv.table_ = stmt->get_location_name();
        tmp_need_priv.priv_level_ = OB_PRIV_OBJECT_LEVEL;
        tmp_need_priv.priv_set_ = OB_PRIV_WRITE;
        tmp_need_priv.obj_type_ = ObObjectType::LOCATION;
        ADD_NEED_PRIV(tmp_need_priv);
        break;
      }
      default: {
        ret = OB_INVALID_ARGUMENT;
        LOG_WARN("Stmt type not in types dealt in this function", K(ret), K(stmt_type));
        break;
      }
    }
  }
  return ret;
}

int get_variable_set_stmt_need_privs(
    const ObSessionPrivInfo &session_priv,
    const ObStmt *basic_stmt,
    ObIArray<ObNeedPriv> &need_privs)
{
  UNUSED(session_priv);
  int ret = OB_SUCCESS;
  if (OB_ISNULL(basic_stmt)) {
    ret = OB_INVALID_ARGUMENT;
    LOG_WARN("Basic stmt should be not be NULL", K(ret));
  } else if (OB_UNLIKELY(stmt::T_VARIABLE_SET != basic_stmt->get_stmt_type())) {
    ret = OB_INVALID_ARGUMENT;
    LOG_WARN("Stmt type should be T_VARIABLE_SET",
             K(ret), "stmt type", basic_stmt->get_stmt_type());
  } else {
    ObNeedPriv need_priv;
    const ObVariableSetStmt *stmt = static_cast<const ObVariableSetStmt *>(basic_stmt);
    if (stmt->has_global_variable()) {
      // Return alter system instead of super.
      need_priv.priv_set_ = OB_PRIV_ALTER_SYSTEM;
      need_priv.priv_level_ = OB_PRIV_USER_LEVEL;
      ADD_NEED_PRIV(need_priv);
    }
  }
  return ret;
}

int get_modify_tenant_stmt_need_privs(
    const ObSessionPrivInfo &session_priv,
    const ObStmt *basic_stmt,
    ObIArray<ObNeedPriv> &need_privs)
{
  int ret = OB_SUCCESS;
  if (OB_ISNULL(basic_stmt)) {
    ret = OB_INVALID_ARGUMENT;
    LOG_WARN("Basic stmt should be not be NULL", K(ret));
  } else if (OB_UNLIKELY(stmt::T_MODIFY_TENANT != basic_stmt->get_stmt_type())) {
    ret = OB_INVALID_ARGUMENT;
    LOG_WARN("Stmt type should be T_MODIFY_TENANT",
             K(ret), "stmt type", basic_stmt->get_stmt_type());
  } else {
    ObNeedPriv need_priv;
    const ObModifyTenantStmt *stmt = static_cast<const ObModifyTenantStmt *>(basic_stmt);
    if (OB_SYS_TENANT_ID != session_priv.tenant_id_) {
      if (!stmt->is_for_current_tenant()) {
        ret = OB_ERR_NO_PRIVILEGE;
        LOG_WARN("Only sys tenant can do this operation for other tenant", K(ret));
      } else {
        bool normal_tenant_can_do = false;
        if (OB_FAIL(stmt->check_normal_tenant_can_do(normal_tenant_can_do))) {
          LOG_WARN("Failed to check normal tenant can do the job", K(ret));
        } else if (!normal_tenant_can_do) {
          ret = OB_ERR_NO_PRIVILEGE;
          LOG_WARN("Include operation normal tenant can not do", K(ret));
        } else { }
      }
    }
    if (OB_SUCC(ret)) {
      need_priv.priv_set_ = OB_PRIV_ALTER_TENANT;
      need_priv.priv_level_ = OB_PRIV_USER_LEVEL;
      ADD_NEED_PRIV(need_priv);
    }
  }
  return ret;
}

int get_lock_tenant_stmt_need_privs(
    const ObSessionPrivInfo &session_priv,
    const ObStmt *basic_stmt,
    ObIArray<ObNeedPriv> &need_privs)
{
  int ret = OB_SUCCESS;
  if (OB_ISNULL(basic_stmt)) {
    ret = OB_INVALID_ARGUMENT;
    LOG_WARN("Basic stmt should be not be NULL", K(ret));
  } else if (OB_UNLIKELY(stmt::T_LOCK_TENANT != basic_stmt->get_stmt_type())) {
    ret = OB_INVALID_ARGUMENT;
    LOG_WARN("Stmt type should be T_LOCK_TENANT",
             K(ret), "stmt type", basic_stmt->get_stmt_type());
  } else {
    ObNeedPriv need_priv;
    const ObLockTenantStmt *stmt = static_cast<const ObLockTenantStmt *>(basic_stmt);
    if (OB_SYS_TENANT_ID != session_priv.tenant_id_) {
      ret = OB_ERR_NO_PRIVILEGE;
      LOG_WARN("Only sys tenant can do this operation", K(ret));
    } else if (stmt->get_tenant_name() == OB_SYS_TENANT_NAME) {
      ret = OB_ERR_NO_PRIVILEGE;
      LOG_WARN("Can not lock sys tenant", K(ret));
    } else {
      need_priv.priv_set_ = OB_PRIV_SUPER;
      need_priv.priv_level_ = OB_PRIV_USER_LEVEL;
      ADD_NEED_PRIV(need_priv);
    }
  }
  return ret;
}

int get_routine_stmt_need_privs(
    const ObSessionPrivInfo &session_priv,
    const ObStmt *basic_stmt,
    ObIArray<ObNeedPriv> &need_privs)
{
  int ret = OB_SUCCESS;
  if (OB_ISNULL(basic_stmt)) {
    ret = OB_INVALID_ARGUMENT;
    LOG_WARN("Basic stmt should be not be NULL", K(ret));
  } else if (OB_UNLIKELY(stmt::T_CREATE_ROUTINE != basic_stmt->get_stmt_type() 
                        && stmt::T_DROP_ROUTINE != basic_stmt->get_stmt_type()
                        && stmt::T_ALTER_ROUTINE != basic_stmt->get_stmt_type())) {
    ret = OB_INVALID_ARGUMENT;
    LOG_WARN("Stmt type should be routine stmt",
             K(ret), "stmt type", basic_stmt->get_stmt_type());
  } else if (stmt::T_CREATE_ROUTINE == basic_stmt->get_stmt_type()) {
    const ObCreateRoutineStmt *stmt = static_cast<const ObCreateRoutineStmt*>(basic_stmt); 
    if (stmt->get_routine_arg().routine_info_.get_routine_type() == ObRoutineType::ROUTINE_PROCEDURE_TYPE 
        || stmt->get_routine_arg().routine_info_.get_routine_type() == ObRoutineType::ROUTINE_FUNCTION_TYPE) { 
      ObNeedPriv need_priv;
      need_priv.table_ = stmt->get_routine_arg().routine_info_.get_routine_name(); 
      need_priv.db_ = stmt->get_routine_arg().db_name_; 
      need_priv.obj_type_ = stmt->get_routine_arg().routine_info_.get_routine_type() == ObRoutineType::ROUTINE_PROCEDURE_TYPE ? ObObjectType::PROCEDURE : ObObjectType::FUNCTION; 
      need_priv.priv_level_ = OB_PRIV_ROUTINE_LEVEL; 
      need_priv.priv_set_ = OB_PRIV_CREATE_ROUTINE; 
      ADD_NEED_PRIV(need_priv); 
    }
  } else if (stmt::T_ALTER_ROUTINE == basic_stmt->get_stmt_type()) {
    const ObAlterRoutineStmt *stmt = static_cast<const ObAlterRoutineStmt*>(basic_stmt); 
    if (stmt->get_routine_arg().routine_info_.get_routine_type() == ObRoutineType::ROUTINE_PROCEDURE_TYPE 
        || stmt->get_routine_arg().routine_info_.get_routine_type() == ObRoutineType::ROUTINE_FUNCTION_TYPE) { 
      ObNeedPriv need_priv;
      need_priv.table_ = stmt->get_routine_arg().routine_info_.get_routine_name(); 
      need_priv.db_ = stmt->get_routine_arg().db_name_; 
      need_priv.obj_type_ = stmt->get_routine_arg().routine_info_.get_routine_type() == ObRoutineType::ROUTINE_PROCEDURE_TYPE ? ObObjectType::PROCEDURE : ObObjectType::FUNCTION; 
      need_priv.priv_level_ = OB_PRIV_ROUTINE_LEVEL; 
      need_priv.priv_set_ = OB_PRIV_ALTER_ROUTINE; 
      ADD_NEED_PRIV(need_priv); 
    }
  } else if (stmt::T_DROP_ROUTINE == basic_stmt->get_stmt_type()) {
    const ObDropRoutineStmt *stmt = static_cast<const ObDropRoutineStmt*>(basic_stmt); 
    if (stmt->get_routine_arg().routine_type_ == ObRoutineType::ROUTINE_PROCEDURE_TYPE 
        || stmt->get_routine_arg().routine_type_ == ObRoutineType::ROUTINE_FUNCTION_TYPE) { 
      ObNeedPriv need_priv;
      need_priv.table_ = stmt->get_routine_arg().routine_name_; 
      need_priv.db_ = stmt->get_routine_arg().db_name_; 
      need_priv.obj_type_ = stmt->get_routine_arg().routine_type_ == ObRoutineType::ROUTINE_PROCEDURE_TYPE ? ObObjectType::PROCEDURE : ObObjectType::FUNCTION; 
      need_priv.priv_level_ = OB_PRIV_ROUTINE_LEVEL; 
      need_priv.priv_set_ = OB_PRIV_ALTER_ROUTINE; 
      ADD_NEED_PRIV(need_priv); 
    }
  }
  return ret;
}

int get_trigger_stmt_need_privs(
    const ObSessionPrivInfo &session_priv,
    const ObStmt *basic_stmt,
    ObIArray<ObNeedPriv> &need_privs)
{
  int ret = OB_SUCCESS;
  bool need_check = false;
  if (OB_ISNULL(basic_stmt)) {
    ret = OB_INVALID_ARGUMENT;
    LOG_WARN("Basic stmt should be not be NULL", K(ret));
  } else if (OB_UNLIKELY(stmt::T_CREATE_TRIGGER != basic_stmt->get_stmt_type()
                        && stmt::T_DROP_TRIGGER != basic_stmt->get_stmt_type())) {
    ret = OB_INVALID_ARGUMENT;
    LOG_WARN("Stmt type should be trigger stmt",
             K(ret), "stmt type", basic_stmt->get_stmt_type());
  } else if (OB_FAIL(ObPrivilegeCheck::get_priv_need_check(session_priv,
                     ObCompatFeatureType::MYSQL_TRIGGER_PRIV_CHECK, need_check))) {
    LOG_WARN("failed to get priv need check", K(ret));
  } else if (lib::is_mysql_mode() && need_check) {
    if (stmt::T_CREATE_TRIGGER == basic_stmt->get_stmt_type()) {
      const ObCreateTriggerStmt *stmt = static_cast<const ObCreateTriggerStmt*>(basic_stmt);
      ObNeedPriv need_priv;
      need_priv.table_ = stmt->get_trigger_arg().base_object_name_;
      need_priv.db_ = stmt->get_trigger_arg().base_object_database_;
      need_priv.priv_level_ = OB_PRIV_TABLE_LEVEL;
      need_priv.priv_set_ = OB_PRIV_TRIGGER;
      ADD_NEED_PRIV(need_priv);
    } else if (stmt::T_DROP_TRIGGER == basic_stmt->get_stmt_type()) {
      const ObDropTriggerStmt *stmt = static_cast<const ObDropTriggerStmt*>(basic_stmt);
      if(stmt->is_exist) {
        ObNeedPriv need_priv;
        need_priv.table_ = stmt->trigger_table_name_;
        need_priv.db_ = stmt->get_trigger_arg().trigger_database_;
        need_priv.priv_level_ = OB_PRIV_TABLE_LEVEL;
        need_priv.priv_set_ = OB_PRIV_TRIGGER;
        ADD_NEED_PRIV(need_priv);
      }
    }
  }
  return ret;
}

int get_event_stmt_need_privs(
    const ObSessionPrivInfo &session_priv,
    const ObStmt *basic_stmt,
    ObIArray<ObNeedPriv> &need_privs)
{
  int ret = OB_SUCCESS;
  bool need_check = false;
  if (OB_ISNULL(basic_stmt)) {
    ret = OB_INVALID_ARGUMENT;
    LOG_WARN("Basic stmt should be not be NULL", K(ret));
  } else if (OB_UNLIKELY(stmt::T_EVENT_JOB_CREATE != basic_stmt->get_stmt_type()
                        && stmt::T_EVENT_JOB_ALTER != basic_stmt->get_stmt_type()
                        && stmt::T_EVENT_JOB_DROP != basic_stmt->get_stmt_type())) {
    ret = OB_INVALID_ARGUMENT;
    LOG_WARN("Stmt type should be event stmt",
             K(ret), "stmt type", basic_stmt->get_stmt_type());
  } else if (OB_FAIL(ObPrivilegeCheck::get_priv_need_check(session_priv,
                     ObCompatFeatureType::MYSQL_EVENT_PRIV_CHECK, need_check))) {
    LOG_WARN("failed to get priv need check", K(ret));
  } else if (lib::is_mysql_mode() && need_check) {
    if (stmt::T_EVENT_JOB_CREATE == basic_stmt->get_stmt_type()) {
      const ObCreateEventStmt *stmt = static_cast<const ObCreateEventStmt*>(basic_stmt);
      ObNeedPriv need_priv;
      need_priv.db_ = stmt->get_event_info().get_event_database();
      need_priv.priv_level_ = OB_PRIV_DB_LEVEL;
      need_priv.priv_set_ = OB_PRIV_EVENT;
      ADD_NEED_PRIV(need_priv);
    } else if (stmt::T_EVENT_JOB_ALTER == basic_stmt->get_stmt_type()) {
      const ObAlterEventStmt *stmt = static_cast<const ObAlterEventStmt*>(basic_stmt);
      ObNeedPriv need_priv;
      need_priv.db_ = stmt->get_event_info().get_event_database();
      need_priv.priv_level_ = OB_PRIV_DB_LEVEL;
      need_priv.priv_set_ = OB_PRIV_EVENT;
      ADD_NEED_PRIV(need_priv);
    } else if (stmt::T_EVENT_JOB_DROP == basic_stmt->get_stmt_type()) {
      const ObDropEventStmt *stmt = static_cast<const ObDropEventStmt*>(basic_stmt);
      ObNeedPriv need_priv;
      need_priv.db_ = stmt->get_event_info().get_event_database();
      need_priv.priv_level_ = OB_PRIV_DB_LEVEL;
      need_priv.priv_set_ = OB_PRIV_EVENT;
      ADD_NEED_PRIV(need_priv);
    }
  }
  return ret;
}

int get_drop_tenant_stmt_need_privs(
    const ObSessionPrivInfo &session_priv,
    const ObStmt *basic_stmt,
    ObIArray<ObNeedPriv> &need_privs)
{
  int ret = OB_SUCCESS;
  if (OB_ISNULL(basic_stmt)) {
    ret = OB_INVALID_ARGUMENT;
    LOG_WARN("Basic stmt should be not be NULL", K(ret));
  } else if (OB_UNLIKELY(stmt::T_DROP_TENANT != basic_stmt->get_stmt_type())) {
    ret = OB_INVALID_ARGUMENT;
    LOG_WARN("Stmt type should be T_DROP_TENANT",
             K(ret), "stmt type", basic_stmt->get_stmt_type());
  } else {
    ObNeedPriv need_priv;
    const ObDropTenantStmt *stmt = static_cast<const ObDropTenantStmt *>(basic_stmt);
    if (OB_SYS_TENANT_ID != session_priv.tenant_id_) {
      ret = OB_ERR_NO_PRIVILEGE;
      LOG_WARN("Only sys tenant can do this operation", K(ret));
    } else if (stmt->get_tenant_name() == OB_SYS_TENANT_NAME) {
      ret = OB_ERR_NO_PRIVILEGE;
      LOG_WARN("Can not drop sys or gts tenant", K(ret));
    } else {
      need_priv.priv_set_ = OB_PRIV_SUPER;
      need_priv.priv_level_ = OB_PRIV_USER_LEVEL;
      ADD_NEED_PRIV(need_priv);
    }
  }
  return ret;
}

int get_truncate_table_stmt_need_privs(
    const ObSessionPrivInfo &session_priv,
    const ObStmt *basic_stmt,
    ObIArray<ObNeedPriv> &need_privs)
{
  int ret = OB_SUCCESS;
  if (OB_ISNULL(basic_stmt)) {
    ret = OB_INVALID_ARGUMENT;
    LOG_WARN("Basic stmt should be not be NULL", K(ret));
  } else if (OB_UNLIKELY(stmt::T_TRUNCATE_TABLE != basic_stmt->get_stmt_type())) {
    ret = OB_INVALID_ARGUMENT;
    LOG_WARN("Stmt type should be T_TRUNCATE_TABLE",
             K(ret), "stmt type", basic_stmt->get_stmt_type());
  } else {
    ObNeedPriv need_priv;
    const ObTruncateTableStmt *stmt = static_cast<const ObTruncateTableStmt *>(basic_stmt);
    //as there is tenant id is truncate_table_arg, so I check this.
    //And not allow truncate other tenant's table. Even sys tenant.
    if (session_priv.tenant_id_ != stmt->get_tenant_id()) {
      ret = OB_ERR_NO_PRIVILEGE;
      LOG_WARN("Can not truncate other tenant's table. Should not be here except change"
               "tenant which not suggested", K(ret));
    } else if (OB_FAIL(ObPrivilegeCheck::can_do_operation_on_db(session_priv, stmt->get_database_name()))) {
      LOG_WARN("Can not do this operation on the database", K(session_priv), K(ret));
    } else {
      need_priv.db_ = stmt->get_database_name();
      need_priv.table_ = stmt->get_table_name();
      need_priv.priv_set_ = OB_PRIV_DROP;
      need_priv.priv_level_ = OB_PRIV_TABLE_LEVEL;
      ADD_NEED_PRIV(need_priv);
    }
  }
  return ret;
}

int get_rename_table_stmt_need_privs(
    const ObSessionPrivInfo &session_priv,
    const ObStmt *basic_stmt,
    ObIArray<ObNeedPriv> &need_privs)
{
  int ret = OB_SUCCESS;
  if (OB_ISNULL(basic_stmt)) {
    ret = OB_INVALID_ARGUMENT;
    LOG_WARN("Basic stmt should be not be NULL", K(ret));
  } else if (OB_UNLIKELY(stmt::T_RENAME_TABLE != basic_stmt->get_stmt_type())) {
    ret = OB_INVALID_ARGUMENT;
    LOG_WARN("Stmt type should be T_RENAME_TABLE",
             K(ret), "stmt type", basic_stmt->get_stmt_type());
  } else {
    ObNeedPriv need_priv;
    const ObRenameTableStmt *stmt = static_cast<const ObRenameTableStmt *>(basic_stmt);
    if (session_priv.tenant_id_ != stmt->get_tenant_id()) {
      ret = OB_ERR_NO_PRIVILEGE;
      LOG_WARN("Can not rename other tenant's table. Should not be here except change"
               "tenant which not suggested", K(ret));
    } else {
      const obrpc::ObRenameTableArg &arg = stmt->get_rename_table_arg();
      for (int64_t idx = 0; OB_SUCC(ret) && idx < arg.rename_table_items_.count(); ++idx) {
        const obrpc::ObRenameTableItem &table_item = arg.rename_table_items_.at(idx);
        if (OB_FAIL(ObPrivilegeCheck::can_do_operation_on_db(session_priv, table_item.origin_db_name_))) {
          LOG_WARN("Can not do this operation on the database", K(session_priv), K(ret));
        } else if (OB_FAIL(ObPrivilegeCheck::can_do_operation_on_db(session_priv, table_item.new_db_name_))) {
          LOG_WARN("Can not do this operation on the database", K(session_priv), K(ret));
        } else {
          need_priv.db_ = table_item.origin_db_name_;
          need_priv.table_ = table_item.origin_table_name_;
          need_priv.priv_set_ = OB_PRIV_DROP | OB_PRIV_ALTER;
          need_priv.priv_level_ = OB_PRIV_TABLE_LEVEL;
          ADD_NEED_PRIV(need_priv);
          need_priv.db_ = table_item.new_db_name_;
          need_priv.table_ = table_item.new_table_name_;
          need_priv.priv_set_ = OB_PRIV_CREATE | OB_PRIV_INSERT;
          need_priv.priv_level_ = OB_PRIV_TABLE_LEVEL;
          ADD_NEED_PRIV(need_priv);
        }
      }
    }
  }
  return ret;
}

int get_create_table_like_stmt_need_privs(
    const ObSessionPrivInfo &session_priv,
    const ObStmt *basic_stmt,
    ObIArray<ObNeedPriv> &need_privs)
{
  int ret = OB_SUCCESS;
  if (OB_ISNULL(basic_stmt)) {
    ret = OB_INVALID_ARGUMENT;
    LOG_WARN("Basic stmt should be not be NULL", K(ret));
  } else if (OB_UNLIKELY(stmt::T_CREATE_TABLE_LIKE != basic_stmt->get_stmt_type())) {
    ret = OB_INVALID_ARGUMENT;
    LOG_WARN("Stmt type should be T_CREATE_TABLE_LIKE",
             K(ret), "stmt type", basic_stmt->get_stmt_type());
  } else {
    ObNeedPriv need_priv;
    const ObCreateTableLikeStmt *stmt = static_cast<const ObCreateTableLikeStmt *>(basic_stmt);
    if (OB_FAIL(ret)) {
    } else if (session_priv.tenant_id_ != stmt->get_tenant_id()) {
      ret = OB_ERR_NO_PRIVILEGE;
      LOG_WARN("Can not create other tenant's table. Should not be here except change"
               "tenant which not suggested", K(ret));
    } else if (OB_FAIL(ObPrivilegeCheck::can_do_operation_on_db(session_priv, stmt->get_new_db_name()))) {
      LOG_WARN("Can not do this operation on the database", K(session_priv), K(ret));
    } else {
      need_priv.db_ = stmt->get_origin_db_name();
      need_priv.table_ = stmt->get_origin_table_name();
      need_priv.priv_set_ = OB_PRIV_SELECT;
      need_priv.priv_level_ = OB_PRIV_TABLE_LEVEL;
      ADD_NEED_PRIV(need_priv);
      need_priv.db_ = stmt->get_new_db_name();
      need_priv.table_ = stmt->get_new_table_name();
      need_priv.priv_set_ = OB_PRIV_CREATE;
      need_priv.priv_level_ = OB_PRIV_TABLE_LEVEL;
      ADD_NEED_PRIV(need_priv);
    }
  }
  return ret;
}

int get_sys_tenant_super_priv(
    const ObSessionPrivInfo &session_priv,
    const ObStmt *basic_stmt,
    ObIArray<ObNeedPriv> &need_privs)
{
  int ret = OB_SUCCESS;
  if (OB_ISNULL(basic_stmt)) {
    ret = OB_INVALID_ARGUMENT;
    LOG_WARN("Basic stmt should be not be NULL", K(ret));
  } else if (OB_SYS_TENANT_ID != session_priv.tenant_id_ &&
             stmt::T_ALTER_SYSTEM_SET_PARAMETER != basic_stmt->get_stmt_type() &&
             stmt::T_SWITCHOVER != basic_stmt->get_stmt_type()) {
    ret = OB_ERR_NO_PRIVILEGE;
    LOG_WARN("Only sys tenant can do this operation",
             K(ret), "stmt type", basic_stmt->get_stmt_type());
  } else {
    ObNeedPriv need_priv;
    need_priv.priv_set_ = OB_PRIV_SUPER;
    need_priv.priv_level_ = OB_PRIV_USER_LEVEL;
    ADD_NEED_PRIV(need_priv);
  }
  return ret;
}

int get_sys_tenant_alter_system_priv(
    const ObSessionPrivInfo &session_priv,
    const ObStmt *basic_stmt,
    ObIArray<ObNeedPriv> &need_privs)
{
  int ret = OB_SUCCESS;
  if (OB_ISNULL(basic_stmt)) {
    ret = OB_INVALID_ARGUMENT;
    LOG_WARN("Basic stmt should be not be NULL", K(ret));
  } else if (OB_SYS_TENANT_ID != session_priv.tenant_id_ &&
             stmt::T_FLUSH_CACHE != basic_stmt->get_stmt_type() &&
             stmt::T_ALTER_SYSTEM_SET_PARAMETER != basic_stmt->get_stmt_type() &&
             stmt::T_FREEZE != basic_stmt->get_stmt_type() &&
             stmt::T_CLEAR_MERGE_ERROR != basic_stmt->get_stmt_type() &&
             stmt::T_ADMIN_MERGE != basic_stmt->get_stmt_type() &&
             stmt::T_ARCHIVE_LOG != basic_stmt->get_stmt_type() &&
             stmt::T_BACKUP_DATABASE != basic_stmt->get_stmt_type() && 
             stmt::T_BACKUP_MANAGE != basic_stmt->get_stmt_type() &&
             stmt::T_BACKUP_CLEAN != basic_stmt->get_stmt_type() &&
             stmt::T_DELETE_POLICY != basic_stmt->get_stmt_type() &&
             stmt::T_BACKUP_KEY != basic_stmt->get_stmt_type() &&
             stmt::T_RECOVER != basic_stmt->get_stmt_type() &&
             stmt::T_TABLE_TTL != basic_stmt->get_stmt_type() &&
             stmt::T_ALTER_SYSTEM_RESET_PARAMETER != basic_stmt->get_stmt_type() &&
             stmt::T_TRANSFER_PARTITION != basic_stmt->get_stmt_type() &&
             stmt::T_MODULE_DATA != basic_stmt->get_stmt_type() &&
             stmt::T_SERVICE_NAME != basic_stmt->get_stmt_type() &&
             stmt::T_ALTER_LS_REPLICA != basic_stmt->get_stmt_type() &&
             stmt::T_TRIGGER_STORAGE_CACHE != basic_stmt->get_stmt_type()) {
    ret = OB_ERR_NO_PRIVILEGE;
    LOG_WARN("Only sys tenant can do this operation",
             K(ret), "stmt type", basic_stmt->get_stmt_type());
  } else {
    ObNeedPriv need_priv;
    need_priv.priv_set_ = OB_PRIV_ALTER_SYSTEM;
    need_priv.priv_level_ = OB_PRIV_USER_LEVEL;
    ADD_NEED_PRIV(need_priv);
  }
  return ret;
}

int get_sys_tenant_create_resource_pool_priv(
    const ObSessionPrivInfo &session_priv,
    const ObStmt *basic_stmt,
    ObIArray<ObNeedPriv> &need_privs)
{
  int ret = OB_SUCCESS;
  if (OB_ISNULL(basic_stmt)) {
    ret = OB_INVALID_ARGUMENT;
    LOG_WARN("Basic stmt should be not be NULL", K(ret));
  } else if (OB_SYS_TENANT_ID != session_priv.tenant_id_) {
    ret = OB_ERR_NO_PRIVILEGE;
    LOG_WARN("Only sys tenant can do this operation",
             K(ret), "stmt type", basic_stmt->get_stmt_type());
  } else if (stmt::T_CREATE_RESOURCE_POOL != basic_stmt->get_stmt_type()
             && stmt::T_DROP_RESOURCE_POOL  != basic_stmt->get_stmt_type()
             && stmt::T_ALTER_RESOURCE_POOL != basic_stmt->get_stmt_type()
             && stmt::T_SPLIT_RESOURCE_POOL != basic_stmt->get_stmt_type()
             && stmt::T_ALTER_RESOURCE_TENANT != basic_stmt->get_stmt_type()) {
    ret = OB_INVALID_ARGUMENT;
    LOG_WARN("Stmt type should be create/alter/drop/split resource pool",
             K(ret), "stmt type", basic_stmt->get_stmt_type());
  } else {
    ObNeedPriv need_priv;
    need_priv.priv_set_ = OB_PRIV_CREATE_RESOURCE_POOL;
    need_priv.priv_level_ = OB_PRIV_USER_LEVEL;
    ADD_NEED_PRIV(need_priv);
  }
  return ret;
}

int get_sys_tenant_create_resource_unit_priv(
    const ObSessionPrivInfo &session_priv,
    const ObStmt *basic_stmt,
    ObIArray<ObNeedPriv> &need_privs)
{
  int ret = OB_SUCCESS;
  if (OB_ISNULL(basic_stmt)) {
    ret = OB_INVALID_ARGUMENT;
    LOG_WARN("Basic stmt should be not be NULL", K(ret));
  } else if (OB_SYS_TENANT_ID != session_priv.tenant_id_) {
    ret = OB_ERR_NO_PRIVILEGE;
    LOG_WARN("Only sys tenant can do this operation",
             K(ret), "stmt type", basic_stmt->get_stmt_type());
  } else if (stmt::T_CREATE_RESOURCE_UNIT != basic_stmt->get_stmt_type()
             && stmt::T_ALTER_RESOURCE_UNIT  != basic_stmt->get_stmt_type()
             && stmt::T_DROP_RESOURCE_UNIT != basic_stmt->get_stmt_type()) {
    ret = OB_INVALID_ARGUMENT;
    LOG_WARN("Stmt type should be create/alter/drop resource",
             K(ret), "stmt type", basic_stmt->get_stmt_type());
  } else {
    ObNeedPriv need_priv;
    need_priv.priv_set_ = OB_PRIV_CREATE_RESOURCE_UNIT;
    need_priv.priv_level_ = OB_PRIV_USER_LEVEL;
    ADD_NEED_PRIV(need_priv);
  }
  return ret;
}

int get_change_tenant_privs(
    const ObSessionPrivInfo &session_priv,
    const ObStmt *basic_stmt,
    ObIArray<ObNeedPriv> &need_privs)
{
  int ret = OB_SUCCESS;
  UNUSED(need_privs);
  if (OB_ISNULL(basic_stmt)) {
    ret = OB_INVALID_ARGUMENT;
    LOG_WARN("Basic stmt should be not be NULL", K(ret));
  } else if (OB_UNLIKELY(stmt::T_CHANGE_TENANT != basic_stmt->get_stmt_type())) {
    ret = OB_INVALID_ARGUMENT;
    LOG_WARN("Stmt type should be T_CHANGE_TENANT",
             K(ret), "stmt type", basic_stmt->get_stmt_type());
  } else if (OB_SYS_TENANT_ID != session_priv.tenant_id_) {
    ret = OB_ERR_NO_PRIVILEGE;
    LOG_WARN("Only sys tenant can do this operation",
             K(ret), "stmt type", basic_stmt->get_stmt_type());
  }
  return ret;
}

int get_boot_strap_stmt_need_privs(
    const ObSessionPrivInfo &session_priv,
    const ObStmt *basic_stmt,
    ObIArray<ObNeedPriv> &need_privs)
{
  int ret = OB_SUCCESS;
  if (OB_ISNULL(basic_stmt)) {
    ret = OB_INVALID_ARGUMENT;
    LOG_WARN("Basic stmt should be not be NULL", K(ret));
  } else if (OB_UNLIKELY(stmt::T_BOOTSTRAP != basic_stmt->get_stmt_type())) {
    ret = OB_INVALID_ARGUMENT;
    LOG_WARN("Stmt type should be T_BOOTSTRAP",
             K(ret), "stmt type", basic_stmt->get_stmt_type());
  } else if (OB_SYS_TENANT_ID != session_priv.tenant_id_) {
    ret = OB_ERR_NO_PRIVILEGE;
    LOG_WARN("Only sys tenant can do this operation", K(ret));
  } else {
    ObNeedPriv need_priv;
    need_priv.priv_set_ = OB_PRIV_BOOTSTRAP;
    need_priv.priv_level_ = OB_PRIV_USER_LEVEL;
    ADD_NEED_PRIV(need_priv);
  }
  return ret;
}

int get_load_data_stmt_need_privs(
    const ObSessionPrivInfo &session_priv,
    const ObStmt *basic_stmt,
    ObIArray<ObNeedPriv> &need_privs)
{
  UNUSED(session_priv);
  int ret = OB_SUCCESS;
  if (OB_ISNULL(basic_stmt)) {
    ret = OB_INVALID_ARGUMENT;
    LOG_WARN("Basic stmt should be not be NULL", K(ret));
  } else if (OB_UNLIKELY(stmt::T_LOAD_DATA != basic_stmt->get_stmt_type())) {
    ret = OB_INVALID_ARGUMENT;
    LOG_WARN("Stmt type should be T_LOAD_DATA",
             K(ret), "stmt type", basic_stmt->get_stmt_type());
  } else {
    const ObLoadDataStmt *load_data_stmt = static_cast<const ObLoadDataStmt *>(basic_stmt);
    if (OB_SUCC(ret)) {
      ObNeedPriv need_priv;
      need_priv.db_ = load_data_stmt->get_load_arguments().database_name_;
      need_priv.table_ = load_data_stmt->get_load_arguments().table_name_;
      need_priv.priv_set_ = OB_PRIV_INSERT;
      need_priv.is_sys_table_ = false;
      need_priv.is_for_update_ = false;
      need_priv.priv_level_ = OB_PRIV_TABLE_LEVEL;
      ADD_NEED_PRIV(need_priv);
    }
    if (OB_SUCC(ret)) {
      ObNeedPriv need_priv;
      need_priv.priv_set_ = OB_PRIV_FILE;
      need_priv.priv_level_ = OB_PRIV_USER_LEVEL;
      ADD_NEED_PRIV(need_priv);
    }
  }

  return ret;
}

int get_create_tablegroup_stmt_need_privs(
    const ObSessionPrivInfo &session_priv,
    const ObStmt *basic_stmt,
    ObIArray<ObNeedPriv> &need_privs)
{
  UNUSED(session_priv);
  int ret = OB_SUCCESS;
  UNUSED(session_priv);
  if (OB_ISNULL(basic_stmt)) {
    ret = OB_INVALID_ARGUMENT;
    LOG_WARN("Basic stmt should be not be NULL", K(ret));
  } else if (OB_UNLIKELY(stmt::T_CREATE_TABLEGROUP != basic_stmt->get_stmt_type())) {
    ret = OB_INVALID_ARGUMENT;
    LOG_WARN("Stmt type should be T_CREATE_TABLEGROUP",
             K(ret), "stmt type", basic_stmt->get_stmt_type());
  } else {
    if (OB_SUCC(ret)) {
      ObNeedPriv need_priv;
      need_priv.priv_set_ = OB_PRIV_CREATE;
      need_priv.priv_level_ = OB_PRIV_USER_LEVEL;
      ADD_NEED_PRIV(need_priv);
    }
  }
  return ret;
}

int get_drop_tablegroup_stmt_need_privs(
    const ObSessionPrivInfo &session_priv,
    const ObStmt *basic_stmt,
    ObIArray<ObNeedPriv> &need_privs)
{
  int ret = OB_SUCCESS;
  if (OB_ISNULL(basic_stmt)) {
    ret = OB_INVALID_ARGUMENT;
    LOG_WARN("Basic stmt should be not be NULL", K(ret));
  } else if (OB_UNLIKELY(stmt::T_DROP_TABLEGROUP != basic_stmt->get_stmt_type())) {
    ret = OB_INVALID_ARGUMENT;
    LOG_WARN("Stmt type should be T_DROP_TABLEGROUP",
             K(ret), "stmt type", basic_stmt->get_stmt_type());
  } else {
    const ObDropTablegroupStmt *stmt = static_cast<const ObDropTablegroupStmt *>(basic_stmt);
    if (OB_SYS_TENANT_ID != session_priv.tenant_id_
        && 0 == stmt->get_tablegroup_name().compare(OB_SYS_TABLEGROUP_NAME)) { //tablegroup case sensetitive
      ret = OB_ERR_NO_PRIVILEGE;
      LOG_WARN("Only sys tenant can do drop sys tablegroup",
               K(ret), "stmt type", basic_stmt->get_stmt_type());
    } else {
      ObNeedPriv need_priv;
      need_priv.priv_set_ = OB_PRIV_DROP;
      need_priv.priv_level_ = OB_PRIV_USER_LEVEL;
      ADD_NEED_PRIV(need_priv);
    }
  }
  return ret;
}

int get_alter_tablegroup_stmt_need_privs(
    const ObSessionPrivInfo &session_priv,
    const ObStmt *basic_stmt,
    ObIArray<ObNeedPriv> &need_privs)
{
  UNUSED(session_priv);
  int ret = OB_SUCCESS;
  if (OB_ISNULL(basic_stmt)) {
    ret = OB_INVALID_ARGUMENT;
    LOG_WARN("Basic stmt should be not be NULL", K(ret));
  } else if (OB_UNLIKELY(stmt::T_ALTER_TABLEGROUP != basic_stmt->get_stmt_type())) {
    ret = OB_INVALID_ARGUMENT;
    LOG_WARN("Stmt type should be T_ALTER_TABLEGROUP",
             K(ret), "stmt type", basic_stmt->get_stmt_type());
  } else {
    ObNeedPriv need_priv;
    need_priv.priv_set_ = OB_PRIV_ALTER;
    need_priv.priv_level_ = OB_PRIV_USER_LEVEL;
    ADD_NEED_PRIV(need_priv);
  }
  return ret;
}

int get_flashback_table_stmt_need_privs(
    const ObSessionPrivInfo &session_priv,
    const ObStmt *basic_stmt,
    ObIArray<ObNeedPriv> &need_privs)
{
  UNUSED(session_priv);
  int ret = OB_SUCCESS;
  if (OB_ISNULL(basic_stmt)) {
    ret = OB_INVALID_ARGUMENT;
    LOG_WARN("Basic stmt should be not be NULL", K(ret));
  } else if (stmt::T_FLASHBACK_TABLE_FROM_RECYCLEBIN != basic_stmt->get_stmt_type()
      && stmt::T_FLASHBACK_TABLE_TO_SCN != basic_stmt->get_stmt_type()) {
    ret = OB_INVALID_ARGUMENT;
    LOG_WARN("Stmt type should be T_FLASHBACK_TABLE",
             K(ret), "stmt type", basic_stmt->get_stmt_type());
  } else {
    ObNeedPriv need_priv;
    need_priv.priv_set_ = OB_PRIV_SUPER;
    need_priv.priv_level_ = OB_PRIV_USER_LEVEL;
    ADD_NEED_PRIV(need_priv);
  }
  return ret;
}

int get_purge_recyclebin_stmt_need_privs(
    const ObSessionPrivInfo &session_priv,
    const ObStmt *basic_stmt,
    ObIArray<ObNeedPriv> &need_privs)
{
  UNUSED(session_priv);
  int ret = OB_SUCCESS;
  if (OB_ISNULL(basic_stmt)) {
    ret = OB_INVALID_ARGUMENT;
    LOG_WARN("Basic stmt should be not be NULL", K(ret));
  } else if (stmt::T_PURGE_RECYCLEBIN != basic_stmt->get_stmt_type()) {
    ret = OB_INVALID_ARGUMENT;
    LOG_WARN("Stmt type should be T_PURGE_RECYCLEBIN",
             K(ret), "stmt type", basic_stmt->get_stmt_type());
  } else {
    ObNeedPriv need_priv;
    need_priv.priv_set_ = OB_PRIV_DROP;
    need_priv.priv_level_ = OB_PRIV_USER_LEVEL;
    ADD_NEED_PRIV(need_priv);
  }
  return ret;
}

int get_flashback_index_stmt_need_privs(
    const ObSessionPrivInfo &session_priv,
    const ObStmt *basic_stmt,
    ObIArray<ObNeedPriv> &need_privs)
{
  UNUSED(session_priv);
  int ret = OB_SUCCESS;
  if (OB_ISNULL(basic_stmt)) {
    ret = OB_INVALID_ARGUMENT;
    LOG_WARN("Basic stmt should not be NULL", K(ret));
  } else if (OB_UNLIKELY(stmt::T_FLASHBACK_INDEX != basic_stmt->get_stmt_type())) {
    ret = OB_INVALID_ARGUMENT;
    LOG_WARN("Stmt type should be T_FLASHBACK_TABLE",
             K(ret), "stmt type", basic_stmt->get_stmt_type());
  } else {
    ObNeedPriv need_priv;
    need_priv.priv_set_ = OB_PRIV_SUPER;
    need_priv.priv_level_ = OB_PRIV_USER_LEVEL;
    ADD_NEED_PRIV(need_priv);
  }

  return ret;
}

int get_flashback_database_stmt_need_privs(
    const ObSessionPrivInfo &session_priv,
    const ObStmt *basic_stmt,
    ObIArray<ObNeedPriv> &need_privs)
{
  UNUSED(session_priv);
  int ret = OB_SUCCESS;
  if (OB_ISNULL(basic_stmt)) {
    ret = OB_INVALID_ARGUMENT;
    LOG_WARN("Basic stmt should be not be NULL", K(ret));
  } else if (OB_UNLIKELY(stmt::T_FLASHBACK_DATABASE != basic_stmt->get_stmt_type())) {
    ret = OB_INVALID_ARGUMENT;
    LOG_WARN("Stmt type should be T_FLASHBACK_DATABASE",
             K(ret), "stmt type", basic_stmt->get_stmt_type());
  } else {
    ObNeedPriv need_priv;
    need_priv.priv_set_ = OB_PRIV_SUPER;
    need_priv.priv_level_ = OB_PRIV_USER_LEVEL;
    ADD_NEED_PRIV(need_priv);
  }
  return ret;
}

int get_purge_table_stmt_need_privs(
    const ObSessionPrivInfo &session_priv,
    const ObStmt *stmt,
    ObIArray<ObNeedPriv> &need_privs)
{
  UNUSED(session_priv);
  int ret = OB_SUCCESS;
  if (OB_ISNULL(stmt)) {
    ret = OB_INVALID_ARGUMENT;
    LOG_WARN("Basic stmt should be not be NULL", K(ret));
  } else if (OB_UNLIKELY(stmt::T_PURGE_TABLE != stmt->get_stmt_type())) {
    ret = OB_INVALID_ARGUMENT;
    LOG_WARN("Stmt type should be T_PURGE_TABLE",
             K(ret), "stmt type", stmt->get_stmt_type());
  } else {
    ObNeedPriv need_priv;
    need_priv.priv_set_ = OB_PRIV_SUPER;
    need_priv.priv_level_ = OB_PRIV_USER_LEVEL;
    ADD_NEED_PRIV(need_priv);
  }
  return ret;
}

int get_purge_index_stmt_need_privs(
    const ObSessionPrivInfo &session_priv,
    const ObStmt *stmt,
    ObIArray<ObNeedPriv> &need_privs)
{
  UNUSED(session_priv);
  int ret = OB_SUCCESS;
  if (OB_ISNULL(stmt)) {
    ret = OB_INVALID_ARGUMENT;
    LOG_WARN("Basic stmt should not be NULL", K(ret));
  } else if (OB_UNLIKELY(stmt::T_PURGE_INDEX != stmt->get_stmt_type())) {
    ret = OB_INVALID_ARGUMENT;
    LOG_WARN("Stmt type should be T_PURGE_INDEX",
             K(ret), "stmt type", stmt->get_stmt_type());
  } else {
    ObNeedPriv need_priv;
    need_priv.priv_set_ = OB_PRIV_SUPER;
    need_priv.priv_level_ = OB_PRIV_USER_LEVEL;
    ADD_NEED_PRIV(need_priv);
  }
  return ret;
}

int get_purge_database_stmt_need_privs(
    const ObSessionPrivInfo &session_priv,
    const ObStmt *stmt,
    ObIArray<ObNeedPriv> &need_privs)
{
  UNUSED(session_priv);
  int ret = OB_SUCCESS;
  if (OB_ISNULL(stmt)) {
    ret = OB_INVALID_ARGUMENT;
    LOG_WARN("Basic stmt should be not be NULL", K(ret));
  } else if (OB_UNLIKELY(stmt::T_PURGE_DATABASE != stmt->get_stmt_type())) {
    ret = OB_INVALID_ARGUMENT;
    LOG_WARN("Stmt type should be PURGE_DATABASE",
             K(ret), "stmt type", stmt->get_stmt_type());
  } else {
    ObNeedPriv need_priv;
    need_priv.priv_set_ = OB_PRIV_SUPER;
    need_priv.priv_level_ = OB_PRIV_USER_LEVEL;
    ADD_NEED_PRIV(need_priv);
  }
  return ret;

}

int get_flashback_tenant_stmt_need_privs(
    const ObSessionPrivInfo &session_priv,
    const ObStmt *basic_stmt,
    ObIArray<ObNeedPriv> &need_privs)
{
  UNUSED(session_priv);
  int ret = OB_SUCCESS;
  if (OB_ISNULL(basic_stmt)) {
    ret = OB_INVALID_ARGUMENT;
    LOG_WARN("Basic stmt should be not be NULL", K(ret));
  } else if (OB_UNLIKELY(stmt::T_FLASHBACK_TENANT != basic_stmt->get_stmt_type())) {
    ret = OB_INVALID_ARGUMENT;
    LOG_WARN("Stmt type should be T_FLASHBACK_TENANT",
             K(ret), "stmt type", basic_stmt->get_stmt_type());
  } else {
    ObNeedPriv need_priv;
    if (OB_SYS_TENANT_ID != session_priv.tenant_id_) {
      ret = OB_ERR_NO_PRIVILEGE;
      LOG_WARN("Only sys tenant can do this operation", K(ret));
    } else {
      need_priv.priv_set_ = OB_PRIV_SUPER;
      need_priv.priv_level_ = OB_PRIV_USER_LEVEL;
      ADD_NEED_PRIV(need_priv);
    }
  }
  return ret;
}


int get_purge_tenant_stmt_need_privs(
    const ObSessionPrivInfo &session_priv,
    const ObStmt *stmt,
    ObIArray<ObNeedPriv> &need_privs)
{
  UNUSED(session_priv);
  int ret = OB_SUCCESS;
  if (OB_ISNULL(stmt)) {
    ret = OB_INVALID_ARGUMENT;
    LOG_WARN("Basic stmt should be not be NULL", K(ret));
  } else if (OB_UNLIKELY(stmt::T_PURGE_TENANT != stmt->get_stmt_type())) {
    ret = OB_INVALID_ARGUMENT;
    LOG_WARN("Stmt type should be PURGE_TENANT",
             K(ret), "stmt type", stmt->get_stmt_type());
  } else {
    ObNeedPriv need_priv;
    if (OB_SYS_TENANT_ID != session_priv.tenant_id_) {
      ret = OB_ERR_NO_PRIVILEGE;
      LOG_WARN("Only sys tenant can do this operation", K(ret));
    } else {
      need_priv.priv_set_ = OB_PRIV_SUPER;
      need_priv.priv_level_ = OB_PRIV_USER_LEVEL;
      ADD_NEED_PRIV(need_priv);
    }
  }
  return ret;
}

int get_restore_point_priv(
    const ObSessionPrivInfo &session_priv,
    const ObStmt *basic_stmt,
    ObIArray<ObNeedPriv> &need_privs)
{
  int ret = OB_SUCCESS;
  if (OB_ISNULL(basic_stmt)) {
    ret = OB_INVALID_ARGUMENT;
    LOG_WARN("Basic stmt should be not be NULL", K(ret));
  } else if (OB_SYS_TENANT_ID == session_priv.tenant_id_) {
    ret = OB_ERR_NO_PRIVILEGE;
    LOG_WARN("Only non sys tenant can do this operation",
             K(ret), "stmt type", basic_stmt->get_stmt_type());
  } else {
    ObNeedPriv need_priv;
    need_priv.priv_set_ = OB_PRIV_SELECT;
    need_priv.priv_level_ = OB_PRIV_USER_LEVEL;
    ADD_NEED_PRIV(need_priv);
  }
  return ret;
}

int get_lock_table_priv(
    const ObSessionPrivInfo &session_priv,
    const ObStmt *basic_stmt,
    ObIArray<ObNeedPriv> &need_privs)
{
  int ret = OB_SUCCESS;
  ObNeedPriv need_priv;
  bool need_check = false;
  if (OB_ISNULL(basic_stmt)) {
    ret = OB_INVALID_ARGUMENT;
    LOG_WARN("Basic stmt should be not be NULL", K(ret));
  } else if (OB_UNLIKELY(stmt::T_LOCK_TABLE != basic_stmt->get_stmt_type())) {
    ret = OB_ERR_UNEXPECTED;
    LOG_WARN("unexpected stmt type", K(basic_stmt->get_stmt_type()), K(ret));
  } else if (OB_FAIL(ObPrivilegeCheck::get_priv_need_check(session_priv,
                                                           ObCompatFeatureType::MYSQL_LOCK_TABLES_PRIV_ENHANCE,
                                                           need_check))) {
    LOG_WARN("failed to get priv need check", K(ret));
  } else if (lib::is_mysql_mode() && need_check) {
    const ObLockTableStmt *stmt = static_cast<const ObLockTableStmt*>(basic_stmt);
    int64_t table_size = stmt->get_table_size();
    for (int64_t i = 0; OB_SUCC(ret) && i < table_size; i++) {
      const TableItem *table_item = stmt->get_table_item(i);
      if (OB_ISNULL(table_item)) {
        ret = OB_ERR_UNEXPECTED;
        LOG_WARN("table item is null");
      } else if (OB_FAIL(ObPrivilegeCheck::can_do_operation_on_db(session_priv,
                                                                  table_item->database_name_))) {
        LOG_WARN("Can not do this operation on the database", K(session_priv),
                 K(ret), "stmt_type", stmt->get_stmt_type());
      } else {
        need_priv.db_ = table_item->database_name_;
        need_priv.priv_set_ = OB_PRIV_LOCK_TABLE;
        need_priv.priv_level_ = OB_PRIV_DB_LEVEL;
        ADD_NEED_PRIV(need_priv);

        if (OB_SUCC(ret)) {
          need_priv.db_ = table_item->database_name_;
          need_priv.table_ = table_item->table_name_;
          need_priv.is_sys_table_ = table_item->is_system_table_;
          need_priv.is_for_update_ = table_item->for_update_;
          need_priv.priv_set_ = OB_PRIV_SELECT;
          need_priv.priv_level_ = OB_PRIV_TABLE_LEVEL;
          ADD_NEED_PRIV(need_priv);
        }
      }
    }
  }
  return ret;
}

int get_create_ccl_priv(
    const ObSessionPrivInfo &session_priv,
    const ObStmt *basic_stmt,
    ObIArray<ObNeedPriv> &need_privs)
{
  int ret = OB_SUCCESS;
  UNUSED(session_priv);
  ObNeedPriv need_priv;
  if (OB_ISNULL(basic_stmt)) {
    ret = OB_INVALID_ARGUMENT;
    LOG_WARN("Basic stmt should be not be NULL", K(ret));
  } else if (stmt::T_CREATE_CCL_RULE != basic_stmt->get_stmt_type()) {
    ret = OB_ERR_UNEXPECTED;
    LOG_WARN("unexpected stmt type", K(basic_stmt->get_stmt_type()), K(ret));
  } else {
    need_priv.priv_set_ = OB_PRIV_CREATE;
    need_priv.priv_level_ = OB_PRIV_USER_LEVEL;
    ADD_NEED_PRIV(need_priv);
  }
  return ret;
}

int get_drop_ccl_priv(
    const ObSessionPrivInfo &session_priv,
    const ObStmt *basic_stmt,
    ObIArray<ObNeedPriv> &need_privs)
{
  int ret = OB_SUCCESS;
  UNUSED(session_priv);
  ObNeedPriv need_priv;
  if (OB_ISNULL(basic_stmt)) {
    ret = OB_INVALID_ARGUMENT;
    LOG_WARN("Basic stmt should be not be NULL", K(ret));
  } else if (stmt::T_DROP_CCL_RULE != basic_stmt->get_stmt_type()) {
    ret = OB_ERR_UNEXPECTED;
    LOG_WARN("unexpected stmt type", K(basic_stmt->get_stmt_type()), K(ret));
  } else {
    need_priv.priv_set_ = OB_PRIV_DROP;
    need_priv.priv_level_ = OB_PRIV_USER_LEVEL;
    ADD_NEED_PRIV(need_priv);
  }
  return ret;
}

const ObGetStmtNeedPrivsFunc ObPrivilegeCheck::priv_check_funcs_[] =
{
#define OB_STMT_TYPE_DEF(stmt_type, priv_check_func, id, action_type) priv_check_func,
#include "sql/resolver/ob_stmt_type.h"
#undef OB_STMT_TYPE_DEF
};

int ObPrivilegeCheck::check_read_only(const ObSqlCtx &ctx,
                                      const stmt::StmtType stmt_type,
                                      const bool has_global_variable,
                                      const ObStmtNeedPrivs &stmt_need_privs)
{
  int ret = OB_SUCCESS;
  const bool is_mysql_mode = lib::is_mysql_mode();
  if (OB_ISNULL(ctx.session_info_) || OB_ISNULL(ctx.schema_guard_)) {
    ret = OB_INVALID_ARGUMENT;
    LOG_WARN("Session is NULL");
  } else if (ctx.session_info_->has_user_super_privilege()) {
    // super priv is only supported in mysql mode on design firstly. But some customer may use it in oracle mode to avoid this check in later time.
    // for upgrade compatibility, we still retain the oracle mode super priv checking here
  } else if (is_mysql_mode) {
    if (ObStmt::is_write_stmt(stmt_type, has_global_variable) &&
        OB_FAIL(ctx.schema_guard_->verify_read_only(ctx.session_info_->get_effective_tenant_id(),
                                                    stmt_need_privs))) {
      LOG_WARN("database or table is read only, cannot execute this stmt", K(ret));
    }
  } else if (!is_mysql_mode) {
    if (ObStmt::is_dml_write_stmt(stmt_type) &&
        OB_FAIL(ctx.schema_guard_->verify_read_only(ctx.session_info_->get_effective_tenant_id(),
                                                    stmt_need_privs))) {
      LOG_WARN("database or table is read only, cannot execute this stmt", K(ret));
    }
  }
  return ret;
}

/* New entry point for permission checking.
  mysql mode, or oracle mode _enable_priv_check = false, use the old permission check logic,
  oracle mode and _enable_priv_check = true, use the new permission check logic */
int ObPrivilegeCheck::check_privilege_new(
    const ObSqlCtx &ctx,
    const ObStmt *basic_stmt,
    ObStmtNeedPrivs &stmt_need_privs)
{
  int ret = OB_SUCCESS;
  OZ (check_privilege(ctx, basic_stmt, stmt_need_privs));
  return ret;
}

int ObPrivilegeCheck::check_privilege(
    const ObSqlCtx &ctx,
    const ObStmt *basic_stmt,
    ObStmtNeedPrivs &stmt_need_privs)
{
  int ret = OB_SUCCESS;
  if (ctx.disable_privilege_check_ == PRIV_CHECK_FLAG_DISABLE || ctx.is_remote_sql_) {
    //do not check privilege
    // Remote SQL generation and execution plan do not need to check permissions, permission checks are all completed on the control end, unified process
  } else {
    if (OB_ISNULL(basic_stmt)
        || OB_ISNULL(ctx.session_info_)
        || OB_ISNULL(ctx.schema_guard_)) {
      ret = OB_INVALID_ARGUMENT;
      LOG_WARN("basic_stmt, ctx.session_info or ctx.schema_manager is NULL",
          K(ret), K(basic_stmt), "session_info", ctx.session_info_,
          "schema manager", ctx.schema_guard_);
    } else if (basic_stmt->is_explain_stmt()) {
      basic_stmt = static_cast<const ObExplainStmt*>(basic_stmt)->get_explain_query_stmt();
      if (OB_ISNULL(basic_stmt)) {
        ret = OB_ERR_UNEXPECTED;
        LOG_WARN("Explain query stmt is NULL", K(ret));
      }
    } else {
      //do nothing
    }

    if (OB_SUCC(ret)) {
      common::ObSEArray<ObNeedPriv, 4> tmp_need_privs;
      ObSessionPrivInfo session_priv;
      bool has_global_variable = false;
      if (OB_FAIL(ctx.session_info_->get_session_priv_info(session_priv))) {
        LOG_WARN("fail to get session priv info", K(ret));
      } else if (FALSE_IT(session_priv.set_effective_tenant_id(
                                          ctx.session_info_->get_effective_tenant_id()))) {
      } else if (OB_FAIL(get_stmt_need_privs(session_priv, basic_stmt, tmp_need_privs))) {
        LOG_WARN("Get stmt need privs error", K(ret));
      } else if (OB_FAIL(stmt_need_privs.need_privs_.assign(tmp_need_privs))) {
        LOG_WARN("fail to assign need_privs", K(ret));
      } else if (basic_stmt->get_stmt_type() == stmt::T_VARIABLE_SET) {
        has_global_variable = 
                           static_cast<const ObVariableSetStmt*>(basic_stmt)->has_global_variable();
      }
      OZ (check_read_only(ctx, basic_stmt->get_stmt_type(), has_global_variable, stmt_need_privs));
      if (OB_SUCC(ret) && OB_FAIL(check_privilege(ctx, stmt_need_privs))) {
        LOG_WARN("privilege check not passed", K(ret));
      }
    }
  }
  return ret;
}

int adjust_session_priv(ObSchemaGetterGuard &schema_guard,
                        ObSessionPrivInfo &session_priv) {
  int ret = OB_SUCCESS;
  const ObUserInfo *user_info = NULL;
  if (OB_ISNULL(user_info = schema_guard.get_user_info(session_priv.tenant_id_, session_priv.user_id_))) {
    ret = OB_USER_NOT_EXIST;
    LOG_WARN("fail to get user_info", K(ret));
  } else {
    session_priv.user_name_ = user_info->get_user_name_str();
    session_priv.host_name_ = user_info->get_host_name_str();
  }
  return ret;
}

int ObPrivilegeCheck::check_privilege(
    const ObSqlCtx &ctx,
    const ObStmtNeedPrivs &stmt_need_priv)
{
  int ret = OB_SUCCESS;
  if (ctx.disable_privilege_check_ == PRIV_CHECK_FLAG_DISABLE) {
    //do not check privilege
  } else {
    ObSessionPrivInfo session_priv;
    if (OB_ISNULL(ctx.session_info_) || OB_ISNULL(ctx.schema_guard_)) {
      ret = OB_INVALID_ARGUMENT;
      LOG_WARN("Session is NULL");
    } else {
      if (OB_FAIL(ctx.session_info_->get_session_priv_info(session_priv))) {
        LOG_WARN("fail to get session priv info", K(ret));
      } else if (ctx.session_info_->get_user_id() != ctx.session_info_->get_priv_user_id()
          && OB_FAIL(adjust_session_priv(*ctx.schema_guard_, session_priv))) {
        LOG_WARN("fail to assign enable role id array", K(ret));
      } else if (OB_UNLIKELY(!session_priv.is_valid())) {
        ret = OB_INVALID_ARGUMENT;
        LOG_WARN("Session priv is invalid", "tenant_id", session_priv.tenant_id_,
                 "user_id", session_priv.user_id_, K(ret));
      } else if (OB_FAIL(const_cast<ObSchemaGetterGuard *>(ctx.schema_guard_)->check_priv(
               session_priv, ctx.session_info_->get_enable_role_array(), stmt_need_priv))) {
        LOG_WARN("No privilege", K(session_priv),
                 "disable check", ctx.disable_privilege_check_, K(ret));
      } else {
        //do nothing
      }
      LOG_DEBUG("check priv",
                K(session_priv),
                "enable_roles", ctx.session_info_->get_enable_role_array());
    }
  }
  return ret;
}

int ObPrivilegeCheck::get_stmt_need_privs(const ObSessionPrivInfo &session_priv,
                                          const ObStmt *basic_stmt,
                                          ObIArray<ObNeedPriv> &need_privs)
{
  int ret = OB_SUCCESS;
  const ObDMLStmt *dml_stmt = NULL;
  if (OB_ISNULL(basic_stmt)) {
    ret = OB_INVALID_ARGUMENT;
    LOG_WARN("Stmt is NULL", K(ret));
  } else if (OB_FAIL(one_level_stmt_need_priv(session_priv, basic_stmt, need_privs))) {
    LOG_WARN("Failed to get one level stmt need priv", K(ret));
  } else if (basic_stmt->is_show_stmt()
             || (stmt::T_SELECT == basic_stmt->get_stmt_type()
                 && static_cast<const ObSelectStmt*>(basic_stmt)->is_from_show_stmt())) {
    //do not check sub-stmt of show_stmt
  } else if ((dml_stmt = dynamic_cast<const ObDMLStmt*>(basic_stmt))) {
    ObArray<ObSelectStmt*> child_stmts;
    if (OB_FAIL(dml_stmt->get_child_stmts(child_stmts))) {
      LOG_WARN("get child stmt failed", K(ret));
    }
    for (int64_t i = 0; OB_SUCC(ret) && i < child_stmts.count(); ++i) {
      const ObSelectStmt *sub_stmt = child_stmts.at(i);
      if (OB_ISNULL(sub_stmt)) {
        ret = OB_ERR_UNEXPECTED;
        LOG_WARN("Sub-stmt is NULL", K(ret));
      } else if (sub_stmt->is_view_stmt() && ObStmt::is_dml_stmt(basic_stmt->get_stmt_type())) {
        //do not check privilege of view stmt
      } else if (OB_FAIL(get_stmt_need_privs(session_priv, sub_stmt, need_privs))) {
        LOG_WARN("Failed to extract priv info of shild stmts", K(i), K(dml_stmt), K(ret));
      } else {
        //do nothing
      }
    }
  }
  return ret;
}


int ObPrivilegeCheck::one_level_stmt_need_priv(const ObSessionPrivInfo &session_priv,
                                               const ObStmt *basic_stmt,
                                               ObIArray<ObNeedPriv> &need_privs)
{
  int ret = OB_SUCCESS;
  if (OB_ISNULL(basic_stmt)) {
    ret = OB_INVALID_ARGUMENT;
    LOG_WARN("Stmt is NULL", K(basic_stmt), K(ret));
  } else if (OB_UNLIKELY(!session_priv.is_valid())) {
    ret = OB_ERR_UNEXPECTED;
    LOG_WARN("Session priv is invalid", K(ret), K(session_priv));
  } else {
    stmt::StmtType stmt_type = basic_stmt->get_stmt_type();
    if (stmt_type < 0
        || stmt::get_stmt_type_idx(stmt_type) >= static_cast<int64_t>(sizeof(priv_check_funcs_) / sizeof(ObGetStmtNeedPrivsFunc))) {
      ret = OB_ERR_UNEXPECTED;
      LOG_WARN("Stmt type is error", K(ret), K(stmt_type));
    } else if (session_priv.is_tenant_changed()
               && !ObStmt::check_change_tenant_stmt(stmt_type)
               && stmt_type != stmt::T_SYSTEM_GRANT) {
      ret = OB_NOT_SUPPORTED;
      LOG_WARN("stmt invalid", K(ret), K(stmt_type), K(session_priv));
      LOG_USER_ERROR(OB_NOT_SUPPORTED, "tenant changed, statement");
    } else if (OB_ISNULL(priv_check_funcs_[stmt::get_stmt_type_idx(stmt_type)])) {
      ret = OB_ERR_UNEXPECTED;
      LOG_WARN("No stmt privilege check function", K(ret), K(stmt_type));
    } else if (OB_FAIL(priv_check_funcs_[stmt::get_stmt_type_idx(stmt_type)](session_priv, basic_stmt, need_privs))) {
      LOG_WARN("Failed to check priv", K(ret), K(stmt_type));
    } else { }//do nothing
  }
  return ret;
}

int ObPrivilegeCheck::can_do_operation_on_db(
    const ObSessionPrivInfo &session_priv,
    const ObString &db_name)
{
  int ret = OB_SUCCESS;
  if (session_priv.is_tenant_changed()
      && 0 != db_name.case_compare(OB_SYS_DATABASE_NAME)) {
    ret = OB_ERR_NO_DB_PRIVILEGE;
    LOG_USER_ERROR(OB_ERR_NO_DB_PRIVILEGE, session_priv.user_name_.length(), session_priv.user_name_.ptr(),
                   session_priv.host_name_.length(),session_priv.host_name_.ptr(),
                   db_name.length(), db_name.ptr());
  } else if (0 == db_name.case_compare(OB_INFORMATION_SCHEMA_NAME)
             || 0 == db_name.case_compare(OB_RECYCLEBIN_SCHEMA_NAME)
             //|| 0 == db_name.case_compare(OB_MYSQL_SCHEMA_NAME)
             || 0 == db_name.case_compare(OB_SYS_DATABASE_NAME)) {
    if (session_priv.tenant_id_ != OB_SYS_TENANT_ID) {
      if ((0 == db_name.case_compare(OB_RECYCLEBIN_SCHEMA_NAME))
          && ((0 == session_priv.user_name_.compare(OB_RESTORE_USER_NAME))
              || (0 == session_priv.user_name_.compare(OB_DRC_USER_NAME)))) {
        // do nothing, only allow sync ddl user to operate recyclebin
      } else {
				ret = OB_ERR_NO_DB_PRIVILEGE;
				LOG_USER_ERROR(OB_ERR_NO_DB_PRIVILEGE, session_priv.user_name_.length(), session_priv.user_name_.ptr(),
											 session_priv.host_name_.length(),session_priv.host_name_.ptr(),
											 db_name.length(), db_name.ptr());
			}
		} else {
      //do nothing
		}
  } else {
		//do nothing
  }
  return ret;
}

int ObPrivilegeCheck::can_do_operation_on_db(const ObSessionPrivInfo &session_priv,
                                             const ObIArray<const ObDmlTableInfo*> &table_infos,
                                             const ObString &op_literal)
{
  int ret = OB_SUCCESS;
  if (is_sys_tenant(session_priv.tenant_id_)) {
    /* system tenant, no checking */
  } else {
    for (int64_t i = 0; OB_SUCC(ret) && i < table_infos.count(); i++) {
      const ObDmlTableInfo *table_info = table_infos.at(i);
      if (OB_ISNULL(table_info)) {
        ret = OB_ERR_UNEXPECTED;
        LOG_WARN("table info is null");
      } else if (table_info->is_link_table_) {
        // skip link table
      } else if (is_inner_table(table_info->ref_table_id_)) {
        ret = OB_ERR_NO_TABLE_PRIVILEGE;
        LOG_USER_ERROR(OB_ERR_NO_TABLE_PRIVILEGE, op_literal.length(), op_literal.ptr(),
                      session_priv.user_name_.length(), session_priv.user_name_.ptr(),
                      session_priv.host_name_.length(),session_priv.host_name_.ptr(),
                      table_info->table_name_.length(), table_info->table_name_.ptr());
      }
    }
  }
  return ret;
}

int ObPrivilegeCheck::can_do_grant_on_db_table(
    const ObSessionPrivInfo &session_priv,
    const ObPrivSet priv_set,
    const ObString &db_name,
    const ObString &table_name)
{
  int ret = OB_SUCCESS;
  if (session_priv.is_tenant_changed()) {
    ret = OB_ERR_NO_DB_PRIVILEGE;
    LOG_USER_ERROR(OB_ERR_NO_DB_PRIVILEGE, session_priv.user_name_.length(), session_priv.user_name_.ptr(),
                   session_priv.host_name_.length(),session_priv.host_name_.ptr(),
                   db_name.length(), db_name.ptr());
  } else if (session_priv.tenant_id_ != OB_SYS_TENANT_ID) {
    if (0 == db_name.case_compare(OB_INFORMATION_SCHEMA_NAME)
      || 0 == db_name.case_compare(OB_RECYCLEBIN_SCHEMA_NAME)
      || 0 == db_name.case_compare(OB_PUBLIC_SCHEMA_NAME)
      || 0 == db_name.case_compare(OB_SYS_DATABASE_NAME)) {
      if (0 == db_name.case_compare(OB_INFORMATION_SCHEMA_NAME)
        || OB_PRIV_HAS_OTHER(priv_set, OB_PRIV_SELECT)) {
        ret = OB_ERR_NO_DB_PRIVILEGE;
        LOG_USER_ERROR(OB_ERR_NO_DB_PRIVILEGE, session_priv.user_name_.length(), session_priv.user_name_.ptr(),
                       session_priv.host_name_.length(),session_priv.host_name_.ptr(),
                       db_name.length(), db_name.ptr());
      }
    } else if (ObPrivilegeCheck::is_mysql_org_table(db_name, table_name)) {
      if (OB_PRIV_HAS_OTHER(priv_set, OB_PRIV_SELECT)) {
        ret = OB_ERR_NO_TABLE_PRIVILEGE;
        const char *command = "NOT-SELECT";
        LOG_USER_ERROR(OB_ERR_NO_TABLE_PRIVILEGE, (int)strlen(command), command,
                       session_priv.user_name_.length(), session_priv.user_name_.ptr(),
                       session_priv.host_name_.length(),session_priv.host_name_.ptr(),
                       table_name.length(), table_name.ptr());
      }
    } else { }//do nothing
  } else {
    if (0 == db_name.case_compare(OB_INFORMATION_SCHEMA_NAME)) {
      ret = OB_ERR_NO_DB_PRIVILEGE;
      LOG_USER_ERROR(OB_ERR_NO_DB_PRIVILEGE, session_priv.user_name_.length(), session_priv.user_name_.ptr(),
                     session_priv.host_name_.length(),session_priv.host_name_.ptr(),
                     db_name.length(), db_name.ptr());
    }
  }
  return ret;
}

int ObPrivilegeCheck::can_do_drop_operation_on_db(
    const ObSessionPrivInfo &session_priv,
    const ObString &db_name)
{
  int ret = OB_SUCCESS;
  if (session_priv.is_tenant_changed()) {
    ret = OB_ERR_NO_DB_PRIVILEGE;
    LOG_USER_ERROR(OB_ERR_NO_DB_PRIVILEGE, session_priv.user_name_.length(), session_priv.user_name_.ptr(),
                   session_priv.host_name_.length(),session_priv.host_name_.ptr(),
                   db_name.length(), db_name.ptr());
  } else if (0 == db_name.case_compare(OB_INFORMATION_SCHEMA_NAME)
      || 0 == db_name.case_compare(OB_SYS_DATABASE_NAME)
      || 0 == db_name.case_compare(OB_RECYCLEBIN_SCHEMA_NAME)
      || 0 == db_name.case_compare(OB_PUBLIC_SCHEMA_NAME)
      || 0 == db_name.case_compare(OB_MYSQL_SCHEMA_NAME)) {
      ret = OB_ERR_NO_DB_PRIVILEGE;
      LOG_USER_ERROR(OB_ERR_NO_DB_PRIVILEGE, session_priv.user_name_.length(), session_priv.user_name_.ptr(),
                     session_priv.host_name_.length(),session_priv.host_name_.ptr(),
                     db_name.length(), db_name.ptr());
  }
  return ret;
}

bool ObPrivilegeCheck::is_mysql_org_table(const ObString &db_name, const ObString &table_name)
{
  bool bret = false;
  if (0 == db_name.case_compare(OB_MYSQL_SCHEMA_NAME)) {
     if (0 == table_name.case_compare("user") || 0 == table_name.case_compare("db")){
       bret = true;
     }
  }
  return bret;
}

int ObPrivilegeCheck::check_password_expired(const ObSqlCtx &ctx, const stmt::StmtType stmt_type)
{
  int ret = OB_SUCCESS;
  if (stmt::T_SET_PASSWORD == stmt_type) {
    // do nothing
  } else if (OB_ISNULL(ctx.session_info_)) {
    ret = OB_NOT_INIT;
    LOG_WARN("params's session info is null", K(ret));
  } else if (ctx.session_info_->is_password_expired()) {
    ret = OB_ERR_MUST_CHANGE_PASSWORD;
    LOG_WARN("the password is out of date, please change the password", K(ret));
  }
  return ret;
}

int ObPrivilegeCheck::check_password_expired_on_connection(
    const uint64_t tenant_id,
    const uint64_t user_id,
    ObSchemaGetterGuard &schema_guard,
    ObSQLSessionInfo &session)
{
  int ret = OB_SUCCESS;
  if (is_root_user(user_id)) {
    //do nothing
  } else if (MYSQL_MODE == session.get_compatibility_mode()
             && OB_FAIL(check_password_life_time_mysql(tenant_id, user_id, schema_guard, session))) {
    LOG_WARN("The current user's password may be out of date", K(ret), K(tenant_id), K(user_id));
  }
  return ret;
}

int ObPrivilegeCheck::check_password_life_time_mysql(
    const uint64_t tenant_id,
    const uint64_t user_id,
    ObSchemaGetterGuard &schema_guard,
    ObSQLSessionInfo &session)
{
  int ret = OB_SUCCESS;
  const share::schema::ObUserInfo *user_info = NULL;
  uint64_t password_life_time = 0;
  if (OB_FAIL(session.get_sys_variable(share::SYS_VAR_DEFAULT_PASSWORD_LIFETIME,
                                              password_life_time))) {
    LOG_WARN("fail to get default_password_lifetime variable", K(ret));
  } else if (password_life_time != ObPasswordLifeTime::FOREVER) {
    if (OB_FAIL(schema_guard.get_user_info(tenant_id, user_id, user_info))) {
      LOG_WARN("fail to get user info", K(ret), K(tenant_id), K(user_id));
    } else if (NULL == user_info) {
      ret = OB_USER_NOT_EXIST;
      LOG_WARN("user is not exist", K(user_id), K(ret));
    } else if (check_password_expired_time(user_info->get_password_last_changed(),
                                                  password_life_time)) {
      session.set_password_expired(true);
      LOG_WARN("the password may have expired", K(ret));
    }
  }
  return ret;
}

bool ObPrivilegeCheck::check_password_expired_time(int64_t password_last_changed_ts,
                                                  int64_t password_life_time)
{
  bool is_expired = false;
  int64_t now = ObClockGenerator::getClock();
  int64_t timeline = now - password_life_time * 24 * 60 * 60 * 1000 * 1000UL;
  if (OB_UNLIKELY(timeline < 0)) {
    /*do nothing*/
  } else if ((uint64_t)(timeline) > (uint64_t)password_last_changed_ts) {
    is_expired = true;
    LOG_WARN_RET(OB_SUCCESS, "the password is out of date, please change the password");
  }
  return is_expired;
}


int ObPrivilegeCheck::get_priv_need_check(const ObSessionPrivInfo &session_priv,
                                          const ObCompatFeatureType feature_type,
                                          bool &need_check)
{
  int ret = OB_SUCCESS;
  if (OB_FAIL(ObCompatControl::check_feature_enable(session_priv.security_version_,
                                                    feature_type, need_check))) {
    LOG_WARN("failed to check feature enable", K(ret), K(feature_type));
  }
  return ret;
}

#undef ADD_NEED_PRIV

}
}
