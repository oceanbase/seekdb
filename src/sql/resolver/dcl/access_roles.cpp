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
#include "sql/resolver/dcl/ob_alter_user_role_stmt.h"
#include "sql/resolver/dcl/ob_create_role_stmt.h"
#include "sql/resolver/dcl/ob_drop_role_stmt.h"
#include "sql/resolver/dcl/ob_grant_stmt.h"
#include "sql/resolver/dcl/ob_revoke_stmt.h"
#include "sql/engine/ob_exec_context.h"

namespace oceanbase::sql::access_control
{
using namespace common;
using namespace share::schema;
namespace
{
int resolve_create_role(const peg::Node &node, ObResolverParams &params, ObStmt *&stmt)
{
  int ret = OB_SUCCESS;
  ObCreateRoleStmt *created = nullptr;
  OZ (create_resolved_statement(params, created));
  if (OB_SUCC(ret)) {
    if (peg::access_options(node).if_not_exists) { created->set_if_not_exists(); }
    OZ (created->get_user_names().reserve(peg::access_accounts(node).size()));
    OZ (created->get_host_names().reserve(peg::access_accounts(node).size()));
    for (const auto &account : peg::access_accounts(node)) {
      if (OB_FAIL(ret)) { break; }
      ObString name;
      ObString host;
      OZ (resolve_role_account(account, params, name, host));
      name = name.trim_space_only();
      if (OB_SUCC(ret) && name.empty()) {
        ret = OB_CANNOT_USER;
        LOG_USER_ERROR(OB_CANNOT_USER, 11, "CREATE ROLE", 14, "anonymous user");
      }
      OZ (created->get_user_names().push_back(name));
      OZ (created->get_host_names().push_back(host));
    }
  }
  if (OB_SUCC(ret)) { stmt = created; }
  return ret;
}

int resolve_drop_role(const peg::Node &node, ObResolverParams &params, ObStmt *&stmt)
{
  int ret = OB_SUCCESS;
  ObDropRoleStmt *dropped = nullptr;
  OZ (create_resolved_statement(params, dropped));
  if (OB_SUCC(ret)) {
    if (peg::access_options(node).if_exists) { dropped->set_if_exists(); }
    for (const auto &account : peg::access_accounts(node)) {
      if (OB_FAIL(ret)) { break; }
      ObString name;
      ObString host;
      OZ (resolve_dropped_account(account, params, false, name, host));
      OZ (dropped->get_user_names().push_back(name));
      OZ (dropped->get_host_names().push_back(host));
    }
  }
  if (OB_SUCC(ret)) { stmt = dropped; }
  return ret;
}

int role_sql_context(ObResolverParams &params, ObSqlCtx *&context)
{
  int ret = OB_SUCCESS;
  if (OB_ISNULL(params.session_info_->get_cur_exec_ctx())
      || OB_ISNULL(context = params.session_info_->get_cur_exec_ctx()->get_sql_ctx())) {
    ret = OB_ERR_UNEXPECTED;
  }
  return ret;
}

int resolve_grant_roles(const peg::Node &node, ObResolverParams &params, ObStmt *&stmt)
{
  int ret = OB_SUCCESS;
  ObGrantStmt *granted = nullptr;
  OZ (create_resolved_statement(params, granted));
  if (OB_SUCC(ret)) {
    granted->set_stmt_type(stmt::T_GRANT_ROLE);
    auto &args = static_cast<obcall::ObGrantArg &>(granted->get_ddl_arg());
    ObSArray<ObString> names;
    ObSArray<ObString> hosts;
    ObArray<uint64_t> grantees;
    ObSEArray<uint64_t, 4> roles;
    for (const auto &account : peg::access_accounts(node)) {
      if (OB_FAIL(ret)) { break; }
      ObString name;
      ObString host;
      OZ (resolve_account(account, params, AccountNameMode::Verbatim, name, host));
      OZ (check_plugin(account, params, true));
      OZ (names.push_back(name));
      OZ (hosts.push_back(host));
    }
    for (int64_t i = 0; OB_SUCC(ret) && i < names.count(); ++i) {
      const ObUserInfo *user = nullptr;
      const ObString &name = names.at(i);
      const ObString &host = hosts.at(i);
      OZ (params.schema_checker_->get_user_info(name, host, user));
      if (ret == OB_USER_NOT_EXIST || user == nullptr) {
        ret = OB_ERR_UNKNOWN_AUTHID;
        LOG_USER_ERROR(OB_ERR_UNKNOWN_AUTHID, name.length(), name.ptr(), host.length(), host.ptr());
      }
      if (OB_SUCC(ret) && !has_exist_in_array(grantees, user->get_user_id())) {
        if (args.roles_.empty()) {
          OZ (args.roles_.push_back(name));
          OZ (args.roles_.push_back(host));
        } else {
          OZ (args.remain_roles_.push_back(name));
          OZ (args.remain_roles_.push_back(host));
        }
        OZ (grantees.push_back(user->get_user_id()));
        OZ (granted->add_grantee(name));
      }
    }
    ObSqlCtx *context = nullptr;
    OZ (role_sql_context(params, context));
    for (const auto &account : peg::access_roles(node)) {
      if (OB_FAIL(ret)) { break; }
      ObString name;
      ObString host;
      const ObUserInfo *role = nullptr;
      OZ (resolve_role_account(account, params, name, host));
      OZ (params.schema_checker_->get_user_info(name, host, role));
      if (ret == OB_USER_NOT_EXIST || role == nullptr) {
        ret = OB_ERR_UNKNOWN_AUTHID;
        LOG_USER_ERROR(OB_ERR_UNKNOWN_AUTHID, name.length(), name.ptr(), host.length(), host.ptr());
      }
      for (int64_t i = 0; OB_SUCC(ret) && i < names.count(); ++i) {
        if (names.at(i) == name && hosts.at(i) == host) {
          ret = OB_ERR_CIRCULAR_ROLE_GRANT_DETECTED;
          LOG_USER_ERROR(OB_ERR_CIRCULAR_ROLE_GRANT_DETECTED,
                         name.length(), name.ptr(), host.length(), host.ptr(),
                         names.at(i).length(), names.at(i).ptr(), hosts.at(i).length(), hosts.at(i).ptr());
        }
      }
      if (OB_SUCC(ret) && !has_exist_in_array(roles, role->get_user_id())) {
        OZ (roles.push_back(role->get_user_id()));
        OZ (args.roles_.push_back(name));
        OZ (args.roles_.push_back(host));
      }
    }
    if (OB_SUCC(ret)) {
      granted->set_option(peg::access_options(node).admin_option ? ADMIN_OPTION : NO_OPTION);
      OZ (params.schema_checker_->check_mysql_grant_role_priv(*context, roles));
    }
  }
  if (OB_SUCC(ret)) { stmt = granted; }
  return ret;
}

int resolve_revoke_roles(const peg::Node &node, ObResolverParams &params, ObStmt *&stmt)
{
  int ret = OB_SUCCESS;
  bool ignore_error = false;
  ObRevokeStmt *revoked = nullptr;
  const auto options = peg::access_options(node);
  OZ (create_resolved_statement(params, revoked));
  if (OB_SUCC(ret)) {
    revoked->set_stmt_type(stmt::T_REVOKE_ROLE);
    ObSEArray<uint64_t, 4> roles;
    for (const auto &account : peg::access_roles(node)) {
      if (OB_FAIL(ret)) { break; }
      ObString name;
      ObString host;
      const ObUserInfo *role = nullptr;
      OZ (resolve_role_account(account, params, name, host));
      OZ (params.schema_checker_->get_user_info(name, host, role));
      if (ret == OB_USER_NOT_EXIST || role == nullptr) {
        ret = OB_ERR_UNKNOWN_AUTHID;
        ignore_error = options.if_exists;
        LOG_USER(ignore_error ? USER_WARN : USER_ERROR, OB_ERR_UNKNOWN_AUTHID,
                 name.length(), name.ptr(), host.length(), host.ptr());
      } else if (OB_FAIL(revoked->add_role(role->get_user_id()))) {
        if (ret == OB_PRIV_DUP) { ret = OB_SUCCESS; }
      } else {
        OZ (roles.push_back(role->get_user_id()));
      }
    }
    ObSqlCtx *context = nullptr;
    OZ (role_sql_context(params, context));
    OZ (params.schema_checker_->check_mysql_grant_role_priv(*context, roles));
    // Resolve every grantee spelling before looking up any of their schema IDs.
    ObSArray<ObString> names;
    ObSArray<ObString> hosts;
    for (const auto &account : peg::access_accounts(node)) {
      if (OB_FAIL(ret)) { break; }
      ObString name;
      ObString host;
      OZ (resolve_account(account, params, AccountNameMode::Verbatim, name, host));
      OZ (check_plugin(account, params, true));
      OZ (names.push_back(name));
      OZ (hosts.push_back(host));
    }
    for (int64_t i = 0; OB_SUCC(ret) && i < names.count(); ++i) {
      const ObString &name = names.at(i);
      const ObString &host = hosts.at(i);
      uint64_t user_id = OB_INVALID_ID;
      const ObUserInfo *user = nullptr;
      if (OB_FAIL(params.schema_checker_->get_user_id(name, host, user_id))) {
        if (ret == OB_USER_NOT_EXIST) {
          ret = OB_ERR_UNKNOWN_AUTHID;
          ignore_error = options.ignore_unknown_user;
          LOG_USER(ignore_error ? USER_WARN : USER_ERROR, OB_ERR_UNKNOWN_AUTHID,
                   name.length(), name.ptr(), host.length(), host.ptr());
        }
      } else {
        OZ (check_reserved_account(params, user_id));
      }
      OZ (revoked->add_grantee(name));
      OZ (params.schema_checker_->get_user_info(user_id, user));
      OZ (revoked->add_user(user_id));
      if (OB_SUCC(ret) && OB_ISNULL(user)) { ret = OB_ERR_UNEXPECTED; }
    }
    if (OB_SUCC(ret)) {
      if (revoked->get_roles().empty()) {
        ret = OB_INVALID_ARGUMENT;
      } else {
        revoked->set_grant_level(OB_PRIV_USER_LEVEL);
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

int resolve_selected_roles(const peg::Node &node, ObResolverParams &params,
                           obcall::ObAlterUserRoleArg &arg, const ObIArray<uint64_t> &granted_roles,
                           bool for_default)
{
  int ret = OB_SUCCESS;
  switch (peg::access_role_selection(node)) {
    case peg::RoleSelection::All: arg.default_role_flag_ = obcall::OB_DEFAULT_ROLE_ALL; break;
    case peg::RoleSelection::None: arg.default_role_flag_ = obcall::OB_DEFAULT_ROLE_NONE; break;
    case peg::RoleSelection::Default: arg.default_role_flag_ = obcall::OB_DEFAULT_ROLE_DEFAULT; break;
    case peg::RoleSelection::Listed: arg.default_role_flag_ = obcall::OB_DEFAULT_ROLE_LIST; break;
    case peg::RoleSelection::AllExcept: arg.default_role_flag_ = obcall::OB_DEFAULT_ROLE_ALL_EXCEPT; break;
    default: ret = OB_ERR_UNDEFINED;
  }
  const bool except = arg.default_role_flag_ == obcall::OB_DEFAULT_ROLE_ALL_EXCEPT;
  for (const auto &account : peg::access_roles(node)) {
    if (OB_FAIL(ret)) { break; }
    ObString name;
    ObString host;
    const ObUserInfo *role = nullptr;
    OZ (resolve_role_account(account, params, name, host));
    if (OB_FAIL(ret)) {
    } else if (OB_FAIL(params.schema_checker_->get_user_info(name, host, role))) {
      if (ret == OB_USER_NOT_EXIST) {
        if (except) {
          ret = OB_SUCCESS;
        } else {
          ret = OB_ERR_UNKNOWN_AUTHID;
          LOG_USER_ERROR(OB_ERR_UNKNOWN_AUTHID, name.length(), name.ptr(), host.length(), host.ptr());
        }
      }
    } else if (role == nullptr) {
      ret = for_default ? OB_ROLE_NOT_EXIST : OB_ERR_ROLE_NOT_GRANTED_OR_DOES_NOT_EXIST;
      if (for_default) {
        LOG_USER_ERROR(OB_ROLE_NOT_EXIST, name.length(), name.ptr());
      } else {
        LOG_USER_ERROR(OB_ERR_ROLE_NOT_GRANTED_OR_DOES_NOT_EXIST, name.length(), name.ptr());
      }
    } else if (!has_exist_in_array(arg.role_id_array_, role->get_user_id())) {
      const uint64_t role_id = role->get_user_id();
      ObString user_name;
      ObString user_host;
      if (!for_default) {
        if (!has_exist_in_array(granted_roles, role_id)) {
          ret = OB_ERR_ROLE_NOT_GRANTED_TO;
          user_name = params.session_info_->get_user_name();
          user_host = params.session_info_->get_host_name();
        }
      } else {
        for (int64_t i = 0; OB_SUCC(ret) && i < arg.user_ids_.count(); ++i) {
          const ObUserInfo *user = nullptr;
          OZ (params.schema_checker_->get_user_info(arg.user_ids_.at(i), user));
          if (OB_SUCC(ret) && OB_ISNULL(user)) {
            ret = OB_ERR_UNEXPECTED;
          } else if (OB_SUCC(ret) && !has_exist_in_array(user->get_role_id_array(), role_id)) {
            ret = OB_ERR_ROLE_NOT_GRANTED_TO;
            user_name = user->get_user_name_str();
            user_host = user->get_host_name_str();
          }
        }
      }
      if (ret == OB_ERR_ROLE_NOT_GRANTED_TO) {
        if (except) {
          ret = OB_SUCCESS;
        } else {
          LOG_USER_ERROR(OB_ERR_ROLE_NOT_GRANTED_TO,
                         name.length(), name.ptr(), host.length(), host.ptr(),
                         user_name.length(), user_name.ptr(), user_host.length(), user_host.ptr());
        }
      } else {
        OZ (arg.role_id_array_.push_back(role_id));
      }
    }
  }
  return ret;
}

int resolve_role_activation(const peg::Node &node, ObResolverParams &params, ObStmt *&stmt)
{
  int ret = OB_SUCCESS;
  ObAlterUserRoleStmt *changed = nullptr;
  OZ (create_resolved_statement(params, changed));
  if (OB_SUCC(ret)) {
    auto &arg = changed->get_ddl_arg();
    const ObUserInfo *user = nullptr;
    if (node.kind() == peg::NodeKind::SetRole) {
      changed->set_set_role_flag(ObAlterUserRoleStmt::SET_ROLE);
      OZ (params.schema_checker_->get_user_info(params.session_info_->get_priv_user_id(), user));
      if (OB_SUCC(ret) && OB_ISNULL(user)) { ret = OB_ERR_UNEXPECTED; }
      OZ (resolve_selected_roles(node, params, arg, user->get_role_id_array(), false));
    } else {
      changed->set_set_role_flag(ObAlterUserRoleStmt::SET_DEFAULT_ROLE);
      const bool alter_user = node.kind() == peg::NodeKind::AlterUserDefaultRole;
      for (const auto &account : peg::access_accounts(node)) {
        if (OB_FAIL(ret)) { break; }
        ObString name;
        ObString host;
        user = nullptr;
        if (alter_user) {
          OZ (resolve_account(account, params, AccountNameMode::Verbatim, name, host));
        } else {
          OZ (resolve_dropped_account(account, params, false, name, host));
        }
        OZ (params.schema_checker_->get_user_info(name, host, user));
        if (ret == OB_USER_NOT_EXIST || (!alter_user && user == nullptr)) {
          ret = OB_ERR_UNKNOWN_AUTHID;
          LOG_USER_ERROR(OB_ERR_UNKNOWN_AUTHID, name.length(), name.ptr(), host.length(), host.ptr());
        } else if (OB_SUCC(ret) && user == nullptr) {
          ret = OB_USER_NOT_EXIST;
          LOG_USER_ERROR(OB_USER_NOT_EXIST, name.length(), name.ptr());
        }
        OZ (arg.user_ids_.push_back(user->get_user_id()));
      }
      OZ (resolve_selected_roles(node, params, arg, user->get_role_id_array(), true));
      ObSqlCtx *context = nullptr;
      OZ (role_sql_context(params, context));
      for (int64_t i = 0; OB_SUCC(ret) && i < arg.user_ids_.count(); ++i) {
        if (arg.user_ids_.at(i) != params.session_info_->get_priv_user_id()) {
          OZ (params.schema_checker_->check_set_default_role_priv(*context));
        }
      }
    }
  }
  if (OB_SUCC(ret)) { stmt = changed; }
  return ret;
}
}

int resolve_roles(const peg::Node &node, ObResolverParams &params, ObStmt *&stmt)
{
  int ret = OB_SUCCESS;
  switch (node.kind()) {
    case peg::NodeKind::CreateRole: ret = resolve_create_role(node, params, stmt); break;
    case peg::NodeKind::DropRole: ret = resolve_drop_role(node, params, stmt); break;
    case peg::NodeKind::GrantRole: ret = resolve_grant_roles(node, params, stmt); break;
    case peg::NodeKind::RevokeRole: ret = resolve_revoke_roles(node, params, stmt); break;
    case peg::NodeKind::SetRole:
    case peg::NodeKind::SetDefaultRole:
    case peg::NodeKind::AlterUserDefaultRole:
      ret = resolve_role_activation(node, params, stmt);
      break;
    default: ret = OB_INVALID_ARGUMENT;
  }
  return ret;
}
}
