/*
 * Copyright (c) 2026 OceanBase.
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

#include "sql/resolver/dcl/access_control_common.h"
#include "sql/resolver/dcl/ob_grant_stmt.h"
#include "sql/resolver/dcl/ob_revoke_stmt.h"
#include "sql/ob_sql_utils.h"
#include "share/schema/ob_table_schema.h"

namespace oceanbase::sql::access_control
{
using namespace common;
using namespace share::schema;
namespace
{
ObPrivType privilege_type(rust::Slice<const uint8_t> bytes)
{
  const auto name = syntax_text(bytes);
  if (name == "all") return OB_PRIV_ALL;
  if (name == "alter") return OB_PRIV_ALTER;
  if (name == "create") return OB_PRIV_CREATE;
  if (name == "create user") return OB_PRIV_CREATE_USER;
  if (name == "delete") return OB_PRIV_DELETE;
  if (name == "drop") return OB_PRIV_DROP;
  if (name == "grant option") return OB_PRIV_GRANT;
  if (name == "insert") return OB_PRIV_INSERT;
  if (name == "update") return OB_PRIV_UPDATE;
  if (name == "select") return OB_PRIV_SELECT;
  if (name == "index") return OB_PRIV_INDEX;
  if (name == "create view") return OB_PRIV_CREATE_VIEW;
  if (name == "show view") return OB_PRIV_SHOW_VIEW;
  if (name == "show databases") return OB_PRIV_SHOW_DB;
  if (name == "super") return OB_PRIV_SUPER;
  if (name == "process") return OB_PRIV_PROCESS;
  if (name == "file") return OB_PRIV_FILE;
  if (name == "replication slave") return OB_PRIV_REPL_SLAVE;
  if (name == "replication client") return OB_PRIV_REPL_CLIENT;
  if (name == "alter system") return OB_PRIV_ALTER_SYSTEM;
  if (name == "execute") return OB_PRIV_EXECUTE;
  if (name == "alter routine") return OB_PRIV_ALTER_ROUTINE;
  if (name == "create routine") return OB_PRIV_CREATE_ROUTINE;
  if (name == "shutdown") return OB_PRIV_SHUTDOWN;
  if (name == "reload") return OB_PRIV_RELOAD;
  if (name == "lock tables") return OB_PRIV_LOCK_TABLE;
  if (name == "references") return OB_PRIV_REFERENCES;
  if (name == "create role") return OB_PRIV_CREATE_ROLE;
  if (name == "drop role") return OB_PRIV_DROP_ROLE;
  if (name == "trigger") return OB_PRIV_TRIGGER;
  if (name == "read") return OB_PRIV_READ;
  if (name == "write") return OB_PRIV_WRITE;
  if (name == "create ai model") return OB_PRIV_CREATE_AI_MODEL;
  if (name == "alter ai model") return OB_PRIV_ALTER_AI_MODEL;
  if (name == "drop ai model") return OB_PRIV_DROP_AI_MODEL;
  if (name == "access ai model") return OB_PRIV_ACCESS_AI_MODEL;
  if (name == "usage") return OB_PRIV_SET_EMPTY;
  return -1;
}

int normalize_target(ObResolverParams &params, ObString &database, ObString &object)
{
  int ret = OB_SUCCESS;
  ObNameCaseMode mode = OB_NAME_CASE_INVALID;
  ObCollationType collation = CS_TYPE_INVALID;
  if (OB_FAIL(params.session_info_->get_name_case_mode(mode))) {
  } else if (OB_FAIL(params.session_info_->get_collation_connection(collation))) {
  } else {
    const bool preserve_case = mode != OB_LOWERCASE_AND_INSENSITIVE;
    if (!database.empty()) {
      OZ (ObSQLUtils::check_and_convert_db_name(collation, preserve_case, database));
    }
    if (!object.empty()) {
      OZ (ObSQLUtils::check_and_convert_table_name(collation, preserve_case, object));
    }
    if (OB_SUCC(ret) && !database.empty()) {
      OZ (ObSQLUtils::cvt_db_name_to_org(*params.schema_checker_->get_schema_guard(),
                                       params.session_info_, database, params.allocator_));
    }
  }
  return ret;
}

template <typename Statement>
int resolve_target(const peg::Node &node, ObResolverParams &params, Statement &stmt,
                   ObPrivLevel &level, bool granting)
{
  int ret = OB_SUCCESS;
  const auto target = peg::access_target(node);
  ObString database;
  ObString object;
  const bool routine = target.object == peg::PrivilegeObject::Procedure
                        || target.object == peg::PrivilegeObject::Function;
  if (target.scope == peg::PrivilegeScope::WildcardQualifiedObject) {
    ret = OB_ERR_PARSE_SQL;
  } else if (OB_ISNULL(params.schema_checker_->get_schema_guard())) {
    ret = OB_ERR_UNEXPECTED;
  } else if (target.scope == peg::PrivilegeScope::Global) {
    level = OB_PRIV_USER_LEVEL;
  } else {
    level = target.scope == peg::PrivilegeScope::Database ? OB_PRIV_DB_LEVEL : OB_PRIV_TABLE_LEVEL;
    if (target.explicit_database) {
      OZ (copy_text(peg::access_target_database(node), params, database));
      OZ (ObSQLUtils::cvt_db_name_to_org(*params.schema_checker_->get_schema_guard(),
                                       params.session_info_, database, params.allocator_));
    } else if (params.session_info_->get_database_name().empty()) {
      ret = OB_ERR_NO_DB_SELECTED;
    } else {
      OZ (ob_write_string(*params.allocator_, params.session_info_->get_database_name(), database));
    }
    if (OB_SUCC(ret) && target.scope == peg::PrivilegeScope::Object) {
      OZ (copy_text(peg::access_target_object(node), params, object));
    }
  }
  if (OB_SUCC(ret)) {
    if (level == OB_PRIV_TABLE_LEVEL && object.empty()) {
      ret = OB_WRONG_TABLE_NAME;
      LOG_USER_ERROR(OB_WRONG_TABLE_NAME, object.length(), object.ptr());
    } else if (level != OB_PRIV_USER_LEVEL && database.empty()) {
      ret = OB_WRONG_DB_NAME;
      LOG_USER_ERROR(OB_WRONG_DB_NAME, database.length(), database.ptr());
    } else if (routine && level != OB_PRIV_TABLE_LEVEL) {
      ret = OB_ILLEGAL_GRANT_FOR_TABLE;
    } else {
      if (routine) { level = OB_PRIV_ROUTINE_LEVEL; }
      stmt.set_grant_level(level);
    }
  }
  OZ (normalize_target(params, database, object));
  OZ (stmt.set_database_name(database));
  OZ (stmt.set_table_name(object));

  ObObjectType object_type = ObObjectType::INVALID;
  uint64_t object_id = OB_INVALID_ID;
  if (OB_SUCC(ret)) {
    if (target.object == peg::PrivilegeObject::Table) {
      const ObTableSchema *table_schema = nullptr;
      if (OB_FAIL(params.schema_checker_->get_table_schema(database, object, false, table_schema))) {
        if (ret == OB_TABLE_NOT_EXIST && !granting) { ret = OB_SUCCESS; }
      } else if (table_schema != nullptr) {
        object_type = table_schema->is_index_table() ? ObObjectType::INDEX : ObObjectType::TABLE;
        object_id = table_schema->get_table_id();
      }
    } else if (routine) {
      object_type = target.object == peg::PrivilegeObject::Procedure
                      ? ObObjectType::PROCEDURE : ObObjectType::FUNCTION;
      uint64_t routine_id = 0;
      bool procedure = false;
      if (OB_FAIL(params.schema_checker_->get_routine_id(database, object, routine_id, procedure))) {
        if (ret == OB_ERR_SP_DOES_NOT_EXIST && !granting) { ret = OB_SUCCESS; }
      } else {
        object_type = procedure ? ObObjectType::PROCEDURE : ObObjectType::FUNCTION;
        object_id = routine_id;
      }
    } else if (database.empty() || object.empty()) {
      object_type = ObObjectType::MAX_TYPE;
    }
    stmt.set_object_type(object_type);
    stmt.set_object_id(object_id);
  }
  return ret;
}

template <typename Statement>
int resolve_privilege_set(const peg::Node &node, ObResolverParams &params, Statement &stmt,
                          ObPrivLevel level)
{
  int ret = OB_SUCCESS;
  ObPrivSet privileges = 0;
  for (const auto &privilege : peg::access_privileges(node)) {
    if (OB_FAIL(ret)) { break; }
    const ObPrivType type = privilege.kind() == peg::NodeKind::Privilege
        ? privilege_type(peg::privilege_name(privilege)) : static_cast<ObPrivType>(-1);
    if (type == static_cast<ObPrivType>(-1)) {
      ret = OB_ERR_PARSER_SYNTAX;
    } else if (level == OB_PRIV_USER_LEVEL) {
      privileges |= type;
    } else {
      const ObPrivSet allowed = level == OB_PRIV_DB_LEVEL ? OB_PRIV_DB_ACC
                                : level == OB_PRIV_TABLE_LEVEL ? OB_PRIV_TABLE_ACC
                                : OB_PRIV_ROUTINE_ACC;
      if (type == OB_PRIV_ALL) {
        privileges |= allowed;
      } else if (type & ~(allowed | OB_PRIV_GRANT)) {
        ret = level == OB_PRIV_DB_LEVEL ? OB_ERR_PRIV_USAGE : OB_ILLEGAL_GRANT_FOR_TABLE;
      } else if (level == OB_PRIV_TABLE_LEVEL && !peg::privilege_columns(privilege).empty()) {
        for (const auto &column : peg::privilege_columns(privilege)) {
          if (OB_FAIL(ret)) { break; }
          ObString name;
          OZ (copy_text(column.text(), params, name));
          OZ (stmt.add_column_privs(name, type));
        }
      } else {
        privileges |= type;
      }
    }
  }
  if (OB_SUCC(ret)) {
    if (peg::access_options(node).grant_option) { privileges |= OB_PRIV_GRANT; }
    stmt.set_priv_set(privileges);
  }
  return ret;
}

int resolve_grant_accounts(const peg::Node &node, ObResolverParams &params, ObGrantStmt &stmt)
{
  int ret = OB_SUCCESS;
  stmt.set_need_create_user(!is_no_auto_create_user(params.session_info_->get_sql_mode()));
  for (const auto &account : peg::access_accounts(node)) {
    if (OB_FAIL(ret)) { break; }
    ObString name;
    ObString host;
    ObString password;
    ObString need_enc = ObString::make_string("NO");
    OZ (resolve_account(account, params, AccountNameMode::Verbatim, name, host));
    OZ (check_plugin(account, params, true));
    const auto authentication = peg::account_authentication(account);
    if (OB_SUCC(ret) && authentication != peg::AuthenticationKind::Absent) {
      if (name != params.session_info_->get_user_name()) {
        stmt.set_need_create_user_priv(true);
      }
      OZ (copy_text(peg::account_password(account), params, password));
      if (OB_SUCC(ret) && authentication == peg::AuthenticationKind::Encoded) {
        if (!valid_encoded_password(password)) {
          ret = OB_ERR_PASSWORD_FORMAT;
          LOG_WARN("invalid password hash format", K(ret));
        }
      } else if (OB_SUCC(ret)) {
        OZ (check_password_strength(password, params));
        need_enc = ObString::make_string("YES");
      }
    }
    if (OB_SUCC(ret) && name.length() > OB_MAX_USER_NAME_LENGTH) {
      ret = OB_WRONG_USER_NAME_LENGTH;
      LOG_USER_ERROR(OB_WRONG_USER_NAME_LENGTH, name.length(), name.ptr());
    }
    OZ (check_reserved_account(params, name, host));
    OZ (stmt.add_grantee(name));
    OZ (stmt.add_user(name, host, password, need_enc));
  }
  return ret;
}

int resolve_grant(const peg::Node &node, ObResolverParams &params, ObStmt *&stmt)
{
  int ret = OB_SUCCESS;
  ObGrantStmt *granted = nullptr;
  ObPrivLevel level = OB_PRIV_INVALID_LEVEL;
  OZ (create_resolved_statement(params, granted));
  if (OB_SUCC(ret)) {
    granted->set_stmt_type(stmt::T_GRANT);
    OZ (resolve_target(node, params, *granted, level, true));
    OZ (resolve_privilege_set(node, params, *granted, level));
    if (OB_SUCC(ret) && level == OB_PRIV_TABLE_LEVEL) {
      bool exists = false;
      const ObString &database = granted->get_database_name();
      const ObString &table = granted->get_table_name();
      if (OB_FAIL(params.schema_checker_->check_table_exists(database, table, false, false, exists))) {
      } else if (!exists && !(OB_PRIV_CREATE & granted->get_priv_set())
                 && !params.is_restore_ && !params.is_ddl_from_primary_) {
        ret = OB_TABLE_NOT_EXIST;
        ObCStringHelper helper;
        LOG_USER_ERROR(OB_TABLE_NOT_EXIST, helper.convert(database), helper.convert(table));
      }
    }
    OZ (resolve_grant_accounts(node, params, *granted));
  }
  if (OB_SUCC(ret)) { stmt = granted; }
  return ret;
}

int check_revoke_all_privilege(ObResolverParams &params)
{
  int ret = OB_SUCCESS;
  ObSessionPrivInfo session_priv;
  ObArenaAllocator allocator;
  ObStmtNeedPrivs needed(allocator);
  ObNeedPriv privilege("mysql", "", OB_PRIV_DB_LEVEL, OB_PRIV_UPDATE, false);
  OZ (needed.need_privs_.init(1));
  OZ (needed.need_privs_.push_back(privilege));
  params.session_info_->get_session_priv_info(session_priv);
  const auto &roles = params.session_info_->get_enable_role_array();
  if (OB_SUCC(ret) && OB_FAIL(params.schema_checker_->check_priv(session_priv, roles, needed))) {
    needed.need_privs_.at(0) = ObNeedPriv("", "", OB_PRIV_USER_LEVEL, OB_PRIV_CREATE_USER, false);
    ret = params.schema_checker_->check_priv(session_priv, roles, needed);
  }
  return ret;
}

int resolve_revoke(const peg::Node &node, ObResolverParams &params, ObStmt *&stmt)
{
  int ret = OB_SUCCESS;
  ObRevokeStmt *revoked = nullptr;
  ObPrivLevel level = OB_PRIV_INVALID_LEVEL;
  bool ignore_error = false;
  OZ (create_resolved_statement(params, revoked));
  if (OB_SUCC(ret)) {
    revoked->set_stmt_type(stmt::T_REVOKE);
    if (node.kind() == peg::NodeKind::RevokeAll) {
      revoked->set_revoke_all(true);
      revoked->set_grant_level(OB_PRIV_USER_LEVEL);
      OZ (check_revoke_all_privilege(params));
    } else {
      OZ (resolve_target(node, params, *revoked, level, false));
      OZ (resolve_privilege_set(node, params, *revoked, level));
    }
    for (const auto &account : peg::access_accounts(node)) {
      if (OB_FAIL(ret)) { break; }
      ObString name;
      ObString host;
      uint64_t user_id = OB_INVALID_ID;
      OZ (resolve_account(account, params, AccountNameMode::Verbatim, name, host));
      if (OB_SUCC(ret) && name.length() > OB_MAX_USER_NAME_LENGTH) {
        ret = OB_WRONG_USER_NAME_LENGTH;
        LOG_USER_ERROR(OB_WRONG_USER_NAME_LENGTH, name.length(), name.ptr());
      }
      OZ (revoked->add_grantee(name));
      if (OB_FAIL(ret)) {
      } else if (OB_FAIL(params.schema_checker_->get_user_id(name, host, user_id))) {
        if (ret == OB_USER_NOT_EXIST) {
          ignore_error = peg::access_options(node).ignore_unknown_user;
          LOG_USER(ignore_error ? USER_WARN : USER_ERROR, OB_ERR_UNKNOWN_AUTHID,
                   name.length(), name.ptr(), host.length(), host.ptr());
        }
      } else if (is_root_user(user_id) && level == OB_PRIV_USER_LEVEL) {
        ret = OB_NOT_SUPPORTED;
        LOG_USER_ERROR(OB_NOT_SUPPORTED, "Revoke privilege from root at global level");
      } else {
        OZ (check_reserved_account(params, user_id));
        OZ (revoked->add_user(user_id));
      }
    }
  }
  if (OB_FAIL(ret) && ignore_error) {
    revoked->set_has_warning();
    ret = OB_SUCCESS;
  }
  if (OB_SUCC(ret)) { stmt = revoked; }
  return ret;
}
}

int resolve_privileges(const peg::Node &node, ObResolverParams &params, ObStmt *&stmt)
{
  return node.kind() == peg::NodeKind::Grant ? resolve_grant(node, params, stmt)
                                           : resolve_revoke(node, params, stmt);
}
}
