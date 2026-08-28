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

#define USING_LOG_PREFIX SQL_ENG
#include "sql/engine/cmd/ob_dcl_executor.h"
#include "query/command/ob_root_service_serialization.h"
#include "query/command/ob_root_command_service.h"

#include "lib/encrypt/ob_encrypted_helper.h"
#include "sql/engine/ob_exec_context.h"
#include "sql/resolver/dcl/ob_grant_stmt.h"
#include "sql/resolver/dcl/ob_revoke_stmt.h"
#include "sql/engine/cmd/ob_user_cmd_executor.h"

namespace oceanbase
{
using namespace common;
using namespace share::schema;
namespace sql
{

int ObGrantExecutor::execute(ObExecContext &ctx, ObGrantStmt &stmt)
{
  int ret = OB_SUCCESS;
  ObSqlExecutorCtx *task_exec_ctx = NULL;
  ObSQLSessionInfo *session_info = NULL;
  
  const ObStrings &users = stmt.get_users();
  ObIAllocator &allocator = ctx.get_allocator();
  obcall::ObGrantArg &arg = static_cast<obcall::ObGrantArg &>(stmt.get_ddl_arg());
  const bool is_role = arg.roles_.count() > 0;
  ObSchemaGetterGuard schema_guard;
  const ObUserInfo *user_info = NULL;

  if (OB_ISNULL(task_exec_ctx = GET_SQL_EXECUTOR_CTX(ctx))) {
    ret = OB_NOT_INIT;
    LOG_WARN("get task executor context failed");
  } else if (OB_ISNULL(session_info = ctx.get_my_session())) {
    ret = OB_NOT_INIT;
    LOG_WARN("Get my session error");
  } else if (OB_ISNULL(GCTX.schema_service_)) {
    ret = OB_NOT_INIT;
    LOG_WARN("get schema service failed", K(ret));
  } else if (!is_role) {
    ObString user_name;
    ObString host_name;
    ObString pwd;
    ObString need_enc;
    //i += 4, each with user_name, pwd, need_enc
    if (OB_UNLIKELY(users.count() <= 0) || OB_UNLIKELY(0 != users.count() % 4)) {
      ret = OB_ERR_UNEXPECTED;
      LOG_WARN("Resolve users error. Users should have user and pwd",
               "ObStrings count", users.count(), K(ret));
    }
    for (int64_t i = 0; OB_SUCC(ret) && i < users.count(); i += 4) {
      if (OB_FAIL(users.get_string(i, user_name))) {
      } else if (OB_FAIL(users.get_string(i + 1, host_name))) {
      } else if (OB_FAIL(users.get_string(i + 2, pwd))) {
      } else if (OB_FAIL(users.get_string(i + 3, need_enc))) {
      } else {
        if (OB_FAIL(arg.users_passwd_.push_back(user_name))) {
        } else if (OB_FAIL(arg.hosts_.push_back(host_name))) {
        } else if (ObString::make_string("YES") == need_enc) {
          ObString pwd_enc;
          if (pwd.length() > 0) {
            char *enc_buf = NULL;
            if (NULL == (enc_buf = static_cast<char *>(allocator.alloc(ENC_BUF_LEN)))) {
              ret = OB_ALLOCATE_MEMORY_FAILED;
              LOG_ERROR("Failed to allocate memory", K(ret), K(ENC_BUF_LEN));
            } else if (OB_FAIL(ObCreateUserExecutor::encrypt_passwd(pwd, 
                                                                    pwd_enc, 
                                                                    enc_buf, 
                                                                    ENC_BUF_LEN))) {
            } else { }//do nothing
          }
          if (OB_FAIL(ret)) {
          } else if (OB_FAIL(arg.users_passwd_.push_back(pwd_enc))) {
          } else { }//do nothing
        } else {
          if (OB_FAIL(arg.users_passwd_.push_back(pwd))) {
          }
        }
      }
    }
  }
  if (OB_FAIL(ret)) {
    // do nothing.
  } else if (OB_ISNULL(stmt.get_query_ctx())) {
    ret = OB_ERR_UNEXPECTED;
    LOG_WARN("query ctx is null", K(ret));
  } else {
    
    arg.db_ = stmt.get_database_name();
    arg.table_ = stmt.get_table_name();
    arg.priv_set_ = stmt.get_priv_set();
    arg.priv_level_ = stmt.get_grant_level();
    arg.need_create_user_ = stmt.get_need_create_user();
    arg.has_create_user_priv_ = session_info->get_user_priv_set() & OB_PRIV_CREATE_USER;
    arg.ddl_stmt_str_ = stmt.get_query_ctx()->get_sql_stmt();
    arg.option_ = stmt.get_option();
    arg.object_type_ = stmt.get_object_type();
    arg.object_id_ = stmt.get_object_id();
    arg.grantor_id_ = stmt.get_grantor_id();
    arg.is_inner_ = session_info->is_inner();
    arg.based_schema_object_infos_.reset();
    if (OB_FAIL(append(arg.column_names_priv_, stmt.get_column_privs()))
        || OB_FAIL(append(arg.ins_col_ids_, stmt.get_ins_col_ids()))
        || OB_FAIL(append(arg.upd_col_ids_, stmt.get_upd_col_ids()))
        || OB_FAIL(append(arg.ref_col_ids_, stmt.get_ref_col_ids()))) {
        LOG_WARN("append failed", K(ret));
    } else if (arg.object_type_ == ObObjectType::TABLE
                && arg.column_names_priv_.count() > 0) {
      if (arg.object_id_ == OB_INVALID_ID) {
        // No schema dependency can be recorded without an object id.
      } else if (stmt.get_table_schema_version() != 0 
                  && OB_FAIL(arg.based_schema_object_infos_.push_back(ObBasedSchemaObjectInfo(arg.object_id_,
                                                                            TABLE_SCHEMA,
                                                                            stmt.get_table_schema_version())))) {
        LOG_WARN("push back failed", K(ret));
      }
    }
    if (OB_FAIL(ret)) {
    } else if (OB_FAIL(GCTX.schema_service_->get_runtime_schema_guard(schema_guard))) {
    } else if (OB_FAIL(schema_guard.get_user_info(session_info->get_priv_user_id(),
                                                  user_info))) {
    } else if (OB_ISNULL(user_info)) {
      // ignore ret
      LOG_WARN("user info is unexpected null", K(ret));
    } else if (OB_FAIL(ob_write_string(allocator, user_info->get_user_name_str(), arg.grantor_))) {
    } else if (OB_FAIL(ob_write_string(allocator, user_info->get_host_name_str(), arg.grantor_host_))) {
    }
    int tmp_ret = OB_SUCCESS;
    if (OB_TMP_FAIL(schema_guard.reset())) {
      LOG_WARN("failed to reset schema guard", K(tmp_ret));
      if (OB_SUCC(ret)) {
        ret = tmp_ret;
      }
    }
    if (OB_FAIL(ret)) {
    } else if (OB_FAIL(query::serialize_root_service_call(
        [&]{ return ctx.root_command_service().grant(arg); }))) {
    }
  }
  return ret;
}

int ObRevokeExecutor::execute(ObExecContext &ctx, ObRevokeStmt &stmt)
{
  int ret = OB_SUCCESS;
  ObSqlExecutorCtx *task_exec_ctx = NULL;

  if (OB_ISNULL(task_exec_ctx = GET_SQL_EXECUTOR_CTX(ctx))) {
    ret = OB_NOT_INIT;
    LOG_WARN("get task executor context failed");
  } else if (stmt.get_has_warning()) {
    //do nothing
  } else {
    switch (stmt.get_grant_level()) {
      case OB_PRIV_USER_LEVEL: {
        if (OB_FAIL(revoke_user(stmt, ctx))) {
        }
        break;
      }
      case OB_PRIV_DB_LEVEL: {
        if (OB_FAIL(revoke_db(stmt, ctx))) {
        }
        break;
      }
      case OB_PRIV_TABLE_LEVEL: {
        if (OB_FAIL(revoke_table(stmt, ctx))) {
        }
        break;
      }
      case OB_PRIV_ROUTINE_LEVEL: {
        if (OB_FAIL(revoke_routine(stmt, ctx))) {
        }
        break;
      }
      default: {
        ret = OB_ERR_UNEXPECTED;
        LOG_WARN("Resolver may be error, invalid grant_level",
                   "grant_level", ob_priv_level_str(stmt.get_grant_level()), K(ret));
        break;
      }
    }
  }
  return ret;
}

int ObRevokeExecutor::revoke_user(ObRevokeStmt &stmt, ObExecContext &ctx)
{
  int ret = OB_SUCCESS;
  obcall::ObRevokeUserArg &arg = static_cast<obcall::ObRevokeUserArg &>(stmt.get_ddl_arg());
  
  const ObIArray<uint64_t> &user_ids = stmt.get_users();
  const bool is_role = arg.role_ids_.count() > 0;
  if (is_role) {
    for (int i = 0; OB_SUCC(ret) && i < user_ids.count(); ++i) {
      arg.user_id_ = user_ids.at(i);
      if (OB_FAIL(query::serialize_root_service_call(
          [&]{ return ctx.root_command_service().revoke_user(arg); }))) {
      }
    }
  } else if (0 == user_ids.count()) {
    ret = OB_ERR_UNEXPECTED;
    LOG_WARN("User ids is empty, resolver may be error", K(ret));
  } else {
    arg.revoke_all_ = stmt.get_revoke_all();
    arg.priv_set_ = stmt.get_priv_set();
    for (int i = 0; OB_SUCC(ret) && i < user_ids.count(); i++) {
      arg.user_id_ = user_ids.at(i);
      if (OB_FAIL(query::serialize_root_service_call(
          [&]{ return ctx.root_command_service().revoke_user(arg); }))) {
      }
    }
  }
  return ret;
}

int ObRevokeExecutor::revoke_db(ObRevokeStmt &stmt, ObExecContext &ctx)
{
  int ret = OB_SUCCESS;
  obcall::ObRevokeDBArg &arg = static_cast<obcall::ObRevokeDBArg &>(stmt.get_ddl_arg());
  
  arg.priv_set_ = stmt.get_priv_set();
  arg.db_ = stmt.get_database_name();
  const ObIArray<uint64_t> &user_ids = stmt.get_users();
  if (0 == user_ids.count()) {
    ret = OB_ERR_UNEXPECTED;
    LOG_WARN("User ids is empty, resolver may be error", K(ret));
  } else {
    for (int i = 0; OB_SUCC(ret) && i < user_ids.count(); i++) {
      arg.user_id_ = user_ids.at(i);
      if (OB_FAIL(query::serialize_root_service_call(
          [&]{ return ctx.root_command_service().revoke_database(arg); }))) {
      }
    }
  }
  return ret;
}

int ObRevokeExecutor::revoke_table(ObRevokeStmt &stmt,
                                   ObExecContext &ctx)
{
  int ret = OB_SUCCESS;
  ObSQLSessionInfo *session_info = NULL;
  ObSchemaGetterGuard schema_guard;
  const ObUserInfo *user_info = NULL;
  if (OB_ISNULL(GCTX.schema_service_)) {
    ret = OB_INVALID_ARGUMENT;
    LOG_WARN("Input argument error", K(ret));
  } else if (OB_ISNULL(session_info = ctx.get_my_session())) {
    ret = OB_NOT_INIT;
    LOG_WARN("Get my session error");
  } else {
    obcall::ObRevokeTableArg &arg = static_cast<obcall::ObRevokeTableArg &>(stmt.get_ddl_arg());
    
    arg.priv_set_ = stmt.get_priv_set();
    arg.db_ = stmt.get_database_name();
    arg.table_ = stmt.get_table_name();
    arg.obj_id_ = stmt.get_object_id();
    arg.obj_type_ = static_cast<uint64_t>(stmt.get_object_type());
    arg.grantor_id_ = stmt.get_grantor_id();
    arg.revoke_all_ora_ = stmt.get_revoke_all_ora();
    arg.based_schema_object_infos_.reset();
    if (OB_FAIL(append(arg.column_names_priv_, stmt.get_column_privs()))) {
    } else if (stmt.get_object_type() == ObObjectType::TABLE
            && (arg.column_names_priv_.count() > 0)) {
      if (arg.obj_id_ == OB_INVALID_ID) {
        // No schema dependency can be recorded without an object id.
      } else if (stmt.get_table_schema_version()!= 0 &&
                OB_FAIL(arg.based_schema_object_infos_.push_back(ObBasedSchemaObjectInfo(arg.obj_id_,
                                                                            TABLE_SCHEMA,
                                                                            stmt.get_table_schema_version())))) {
        LOG_WARN("push back failed", K(ret));
      }
    } else {
      //todo: pl routine and others
    }
    if (OB_FAIL(ret)) {
    } else if (OB_FAIL(GCTX.schema_service_->get_runtime_schema_guard(schema_guard))) {
    } else if (OB_FAIL(schema_guard.get_user_info(session_info->get_priv_user_id(),
                                                  user_info))) {
    } else if (OB_ISNULL(user_info)) {
      // ignore ret
      LOG_WARN("user info is unexpected null", K(ret));
    } else if (OB_FAIL(ob_write_string(ctx.get_allocator(), user_info->get_user_name_str(), arg.grantor_))) {
    } else if (OB_FAIL(ob_write_string(ctx.get_allocator(), user_info->get_host_name_str(), arg.grantor_host_))) {
    }
    int tmp_ret = OB_SUCCESS;
    if (OB_TMP_FAIL(schema_guard.reset())) {
      LOG_WARN("failed to reset schema guard", K(tmp_ret));
      if (OB_SUCC(ret)) {
        ret = tmp_ret;
      }
    }
    const ObIArray<uint64_t> &user_ids = stmt.get_users();
    if (OB_FAIL(ret)) {
    } else if (0 == user_ids.count()) {
      ret = OB_ERR_UNEXPECTED;
      LOG_WARN("User ids is empty, resolver may be error", K(ret));
    } else {
      for (int i = 0; OB_SUCC(ret) && i < user_ids.count(); i++) {
        arg.user_id_ = user_ids.at(i);
        if (OB_FAIL(query::serialize_root_service_call(
            [&]{ return ctx.root_command_service().revoke_table(arg); }))) {
        }
      }
    }
  }
  return ret;
}

int ObRevokeExecutor::revoke_routine(ObRevokeStmt &stmt,
                                     ObExecContext &ctx)
{
  int ret = OB_SUCCESS;
  ObSQLSessionInfo *session_info = NULL;
  ObSchemaGetterGuard schema_guard;
  const ObUserInfo *user_info = NULL;
  if (OB_ISNULL(session_info = ctx.get_my_session())) {
    ret = OB_NOT_INIT;
    LOG_WARN("Get my session error");
  } else {
    obcall::ObRevokeRoutineArg &arg = static_cast<obcall::ObRevokeRoutineArg &>(stmt.get_ddl_arg());
    
    arg.priv_set_ = stmt.get_priv_set();
    arg.db_ = stmt.get_database_name();
    arg.routine_ = stmt.get_table_name();
    arg.obj_id_ = stmt.get_object_id();
    arg.obj_type_ = static_cast<uint64_t>(stmt.get_object_type());
    arg.grantor_id_ = stmt.get_grantor_id();
    arg.revoke_all_ora_ = stmt.get_revoke_all_ora();
    if (OB_FAIL(ret)) {
    } else if (OB_FAIL(GCTX.schema_service_->get_runtime_schema_guard(schema_guard))) {
    } else if (OB_FAIL(schema_guard.get_user_info(session_info->get_priv_user_id(),
                                                  user_info))) {
    } else if (OB_ISNULL(user_info)) {
      // ignore ret
      LOG_WARN("user info is unexpected null", K(ret));
    } else if (OB_FAIL(ob_write_string(ctx.get_allocator(), user_info->get_user_name_str(), arg.grantor_))) {
    } else if (OB_FAIL(ob_write_string(ctx.get_allocator(), user_info->get_host_name_str(), arg.grantor_host_))) {
    }
    int tmp_ret = OB_SUCCESS;
    if (OB_TMP_FAIL(schema_guard.reset())) {
      LOG_WARN("failed to reset schema guard", K(tmp_ret));
      if (OB_SUCC(ret)) {
        ret = tmp_ret;
      }
    }
    const ObIArray<uint64_t> &user_ids = stmt.get_users();
    if (OB_FAIL(ret)) {
    } else if (0 == user_ids.count()) {
      ret = OB_ERR_UNEXPECTED;
      LOG_WARN("User ids is empty, resolver may be error", K(ret));
    } else {
      for (int i = 0; OB_SUCC(ret) && i < user_ids.count(); i++) {
        arg.user_id_ = user_ids.at(i);
        if (OB_FAIL(query::serialize_root_service_call(
            [&]{ return ctx.root_command_service().revoke_routine(arg); }))) {
        }
      }
    }
  }
  return ret;
}

}// ns sql
}// ns oceanbase
