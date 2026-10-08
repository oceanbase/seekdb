/*
 * Copyright (c) 2026 OceanBase.
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
#pragma once
#include "sql/resolver/dcl/ob_grant_stmt.h"
#include "sql/resolver/dcl/ob_revoke_stmt.h"
#include "sql/session/ob_sql_session_info.h"
#include "share/schema/ob_schema_getter_guard.h"

namespace oceanbase { namespace sql {

// Host-only semantic preparation against the caller's current (possibly
// private) guard. Never invokes an executor, starts a transaction, creates a
// user, changes a password or publishes schema. Result borrows statement/guard
// strings and must be wire-copied before their lifetime ends.
class NativeRoutineDclRequest final
{
public:
  static int prepare(ObGrantStmt &stmt, ObSQLSessionInfo &session,
      share::schema::ObSchemaGetterGuard &guard)
  {
    using namespace common;
    using namespace share::schema;
    if (stmt.get_grant_level() != OB_PRIV_ROUTINE_LEVEL || !stmt.native_target().resolved_ ||
        stmt.get_object_type() != ObObjectType::FUNCTION) return OB_NOT_SUPPORTED;
    auto &arg = static_cast<obcall::ObGrantArg &>(stmt.get_ddl_arg());
    arg.users_passwd_.reset(); arg.hosts_.reset(); arg.based_schema_object_infos_.reset();
    arg.db_ = stmt.get_database_name(); arg.table_ = stmt.get_table_name();
    arg.priv_set_ = stmt.get_priv_set(); arg.priv_level_ = stmt.get_grant_level();
    arg.object_type_ = stmt.get_object_type(); arg.object_id_ = stmt.get_object_id();
    arg.option_ = stmt.get_option(); arg.need_create_user_ = false;
    arg.has_create_user_priv_ = false; arg.is_inner_ = false;
    const ObPrivSet rights = arg.priv_set_ & ~OB_PRIV_GRANT;
    if (!rights || (rights & ~(OB_PRIV_EXECUTE | OB_PRIV_ALTER_ROUTINE)) ||
        !stmt.get_role_names().empty() || !stmt.get_priv_array().empty() || !stmt.get_obj_priv_array().empty() ||
        !stmt.get_column_privs().empty() || !stmt.get_sel_col_ids().empty() || !stmt.get_ins_col_ids().empty() ||
        !stmt.get_upd_col_ids().empty() || !stmt.get_ref_col_ids().empty() || arg.option_ > GRANT_OPTION)
      return OB_NOT_SUPPORTED;
    const auto &users = stmt.get_users();
    if (users.count() <= 0 || users.count() % 4 || users.count() / 4 > 16384) return OB_INVALID_ARGUMENT;
    int ret = OB_SUCCESS;
    for (int64_t i = 0; OB_SUCC(ret) && i < users.count(); i += 4) {
      ObString name, host, password, encryption;
      const ObUserInfo *recipient = nullptr;
      if (OB_FAIL(users.get_string(i, name)) || OB_FAIL(users.get_string(i + 1, host)) ||
          OB_FAIL(users.get_string(i + 2, password)) || OB_FAIL(users.get_string(i + 3, encryption))) {
      } else if (!password.empty() || encryption == ObString::make_string("YES")) ret = OB_NOT_SUPPORTED;
      else if (OB_FAIL(guard.get_user_info(name, host, recipient))) {
      } else if (!recipient) ret = OB_USER_NOT_EXIST;
      else if (OB_FAIL(arg.users_passwd_.push_back(name)) || OB_FAIL(arg.users_passwd_.push_back(ObString())) ||
               OB_FAIL(arg.hosts_.push_back(host))) {
      }
    }
    if (OB_SUCC(ret)) ret = bind_actor(arg, session, guard);
    if (OB_SUCC(ret)) ret = arg.based_schema_object_infos_.push_back(ObBasedSchemaObjectInfo(
        arg.object_id_, ROUTINE_SCHEMA, arg.native_target_.routine_.get_schema_version()));
    if (OB_SUCC(ret) && !arg.is_valid()) ret = OB_INVALID_ARGUMENT;
    return ret;
  }

  static int prepare(ObRevokeStmt &stmt, ObSQLSessionInfo &session,
      share::schema::ObSchemaGetterGuard &guard)
  {
    using namespace common;
    using namespace share::schema;
    if (stmt.get_grant_level() != OB_PRIV_ROUTINE_LEVEL || !stmt.native_target().resolved_ ||
        stmt.get_object_type() != ObObjectType::FUNCTION) return OB_NOT_SUPPORTED;
    auto &arg = static_cast<obcall::ObRevokeRoutineArg &>(stmt.get_ddl_arg());
    arg.db_ = stmt.get_database_name(); arg.routine_ = stmt.get_table_name();
    arg.priv_set_ = stmt.get_priv_set(); arg.obj_id_ = stmt.get_object_id();
    arg.obj_type_ = uint64_t(stmt.get_object_type()); arg.revoke_all_ora_ = stmt.get_revoke_all_ora();
    arg.based_schema_object_infos_.reset();
    if (!arg.priv_set_ || (arg.priv_set_ & ~(OB_PRIV_EXECUTE | OB_PRIV_ALTER_ROUTINE)) ||
        !arg.obj_priv_array_.empty() || !stmt.get_roles().empty() || arg.revoke_all_ora_)
      return OB_NOT_SUPPORTED;
    // Batch recipients are valid only after the target is bound to an actor,
    // matching the standalone REVOKE executor's preparation order.
    int ret = bind_actor(arg, session, guard);
    if (OB_SUCC(ret)) ret = arg.set_native_grantees(stmt.get_users());
    for (int64_t i = 0; OB_SUCC(ret) && i < arg.native_grantees_.count(); ++i) {
      const ObUserInfo *recipient = nullptr;
      if (OB_FAIL(guard.get_user_info(arg.native_grantees_.at(i), recipient))) {
      } else if (!recipient) ret = OB_USER_NOT_EXIST;
    }
    if (OB_SUCC(ret)) ret = arg.based_schema_object_infos_.push_back(ObBasedSchemaObjectInfo(
        arg.obj_id_, ROUTINE_SCHEMA, arg.native_target_.routine_.get_schema_version()));
    if (OB_SUCC(ret) && !arg.is_valid()) ret = OB_INVALID_ARGUMENT;
    return ret;
  }
private:
  template<class Arg>
  static int bind_actor(Arg &arg, ObSQLSessionInfo &session, share::schema::ObSchemaGetterGuard &guard)
  {
    using namespace common;
    const share::schema::ObUserInfo *user = nullptr;
    int ret = guard.get_user_info(session.get_priv_user_id(), user);
    if (OB_SUCC(ret) && !user) ret = OB_USER_NOT_EXIST;
    if (OB_SUCC(ret) && user->is_role()) ret = OB_ERR_NO_PRIVILEGE;
    if (OB_SUCC(ret)) {
      arg.grantor_id_ = user->get_user_id(); arg.grantor_ = user->get_user_name_str();
      arg.grantor_host_ = user->get_host_name_str();
      ret = arg.native_target_.bind_actor(arg.grantor_id_, session.get_enable_role_array());
    }
    return ret;
  }
};
} }
