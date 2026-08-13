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
#include "sql/engine/cmd/ob_user_cmd_executor.h"
#include "query/command/ob_root_service_serialization.h"
#include "query/command/ob_root_command_service.h"

#include "lib/encrypt/ob_encrypted_helper.h"
#include "sql/resolver/dcl/ob_create_user_stmt.h"
#include "sql/resolver/dcl/ob_drop_user_stmt.h"
#include "sql/resolver/dcl/ob_lock_user_stmt.h"
#include "sql/resolver/dcl/ob_rename_user_stmt.h"
#include "sql/resolver/dcl/ob_alter_user_role_stmt.h"
#include "sql/engine/ob_exec_context.h"

namespace oceanbase
{
using namespace common;
using namespace obcall;
using namespace share::schema;
namespace sql
{
int ObCreateUserExecutor::encrypt_passwd(const common::ObString& pwd,
                                         common::ObString& encrypted_pwd,
                                         char *enc_buf,
                                         int64_t buf_len)
{
  int ret = OB_SUCCESS;
  if (OB_ISNULL(enc_buf)) {
    ret = OB_INVALID_ARGUMENT;
    LOG_WARN("enc_buf is NULL", K(ret));
  } else if (buf_len < ENC_BUF_LEN) {
    ret = OB_BUF_NOT_ENOUGH;
    LOG_WARN("Encrypt buf not enough");
  } else {
    encrypted_pwd.assign_ptr(enc_buf, ENC_STRING_BUF_LEN);
    if (OB_FAIL(ObEncryptedHelper::encrypt_passwd_to_stage2(pwd, encrypted_pwd))) {
    }
  }
  return ret;
}

int ObCreateUserExecutor::check_user_valid(ObSchemaGetterGuard& schema_guard, 
                                           uint64_t priv_set,
                                           const ObString &user_name,
                                           const ObString &host_name,
                                           const ObString &opreation_name)
{
  int ret = OB_SUCCESS;
  ObSqlString full_user_name;
  bool existed = false;
  if (OB_FAIL(full_user_name.append_fmt("%.*s@%.*s", user_name.length(), user_name.ptr(),
                                                host_name.length(), host_name.ptr()))) {
  } else if (OB_FAIL(schema_guard.check_routine_definer_existed(full_user_name.string(), existed))) {
  } else if (existed) {
    if ((priv_set & OB_PRIV_SUPER) != 0) {
      LOG_USER_WARN(OB_ERR_USER_REFFERD_AS_DEFINER, user_name.length(), user_name.ptr(), host_name.length(), host_name.ptr());
    } else {
      ret = OB_ERR_OPERATION_ON_USER_REFERRED_AS_DEFINER;
      LOG_WARN("create user has definer", K(ret));
      LOG_USER_ERROR(OB_ERR_OPERATION_ON_USER_REFERRED_AS_DEFINER, opreation_name.length(), opreation_name.ptr(),
                        user_name.length(), user_name.ptr(), host_name.length(), host_name.ptr());
    }
  }
  return ret;
}

int ObCreateUserExecutor::userinfo_extract_user_name(
      const common::ObIArray<share::schema::ObUserInfo> &user_infos,
      const common::ObIArray<int64_t> &index,
      common::ObIArray<common::ObString> &users,
      common::ObIArray<common::ObString> &hosts)
{
  int ret = OB_SUCCESS;
  users.reset();
  hosts.reset();
  for (int64_t i = 0; OB_SUCC(ret) && i < index.count(); ++i) {
    int64_t in = index.at(i);
    if (OB_UNLIKELY(in < 0) || OB_UNLIKELY(in >= user_infos.count())) {
      ret = OB_INVALID_ARGUMENT;
      LOG_WARN("Userinfo index out of range", K(user_infos), K(index), K(in));
    } else if (OB_FAIL(users.push_back(user_infos.at(in).get_user_name_str()))) {
    } else if (OB_FAIL(hosts.push_back(user_infos.at(in).get_host_name_str()))) {
    }
  }
  return ret;
}

int ObCreateUserExecutor::execute(ObExecContext &ctx, ObCreateUserStmt &stmt)
{
  int ret = OB_SUCCESS;
  ObSqlExecutorCtx *task_exec_ctx = NULL;
  
  const ObStrings &users = stmt.get_users();
  const bool if_not_exist = stmt.get_if_not_exists();
  const int64_t FIX_MEMBER_CNT = 4;
  if (OB_ISNULL(GCTX.schema_service_)) {
    ret = OB_ERR_UNEXPECTED;
    LOG_WARN("schema service is null", K(ret));
  } else if (OB_ISNULL(ctx.get_my_session())) {
    ret = OB_ERR_UNEXPECTED;
    LOG_WARN("unexpected error", K(ret));
  } else if (OB_ISNULL(task_exec_ctx = GET_SQL_EXECUTOR_CTX(ctx))) {
    ret = OB_NOT_INIT;
    LOG_WARN("get task executor context failed");
  } else if (OB_UNLIKELY(users.count() <= FIX_MEMBER_CNT) || OB_UNLIKELY(0 != users.count() % FIX_MEMBER_CNT)) {
    ret = OB_ERR_UNEXPECTED;
    LOG_WARN("Resolve create user error. Users should have user and pwd", "ObStrings count", users.count());
  } else {
    ObString user_name;
    ObString host_name;
    ObString pwd;
    ObString need_enc;
    ObString ssl_type;
    ObString ssl_cipher;
    ObString x509_issuer;
    ObString x509_subject;
    ObSSLType ssl_type_enum = ObSSLType::SSL_TYPE_NOT_SPECIFIED;
    ObCreateUserArg &arg = static_cast<ObCreateUserArg &>(stmt.get_ddl_arg());
    
    arg.user_infos_.reset();
    arg.if_not_exist_ = if_not_exist;
    const int64_t users_cnt = users.count() - FIX_MEMBER_CNT;

    if (OB_FAIL(users.get_string(users_cnt, ssl_type))) {
    } else if (OB_FAIL(users.get_string(users_cnt + 1, ssl_cipher))) {
    } else if (OB_FAIL(users.get_string(users_cnt + 2, x509_issuer))) {
    } else if (OB_FAIL(users.get_string(users_cnt + 3, x509_subject))) {
    } else if (OB_UNLIKELY(ObSSLType::SSL_TYPE_MAX == (ssl_type_enum = get_ssl_type_from_string(ssl_type)))) {
      ret = OB_ERR_UNEXPECTED;
      LOG_WARN("known ssl_type", K(ssl_type), K(ret));
    }

    for (int64_t i = 0; OB_SUCC(ret) && i < users_cnt; i += FIX_MEMBER_CNT) {
      if (OB_FAIL(users.get_string(i, user_name))) {
      } else if (OB_FAIL(users.get_string(i + 1, host_name))) {
      } else if (OB_FAIL(users.get_string(i + 2, pwd))) {
      } else if (OB_FAIL(users.get_string(i + 3, need_enc))) {
      } else {
        ObUserInfo user_info;
        if (ObString::make_string("YES") == need_enc) {
          if (pwd.length() > 0) {
            ObString pwd_enc;
            char enc_buf[ENC_BUF_LEN] = {0};
            if (OB_FAIL(encrypt_passwd(pwd, pwd_enc, enc_buf, ENC_BUF_LEN))) {
            } else if (OB_FAIL(user_info.set_passwd(pwd_enc))) {
            }
          }
        } else {
          if (OB_FAIL(user_info.set_passwd(pwd))) {
          }
        }

        if (OB_SUCC(ret)) {
          ObSchemaGetterGuard schema_guard;
          if (OB_FAIL(GCTX.schema_service_->get_runtime_schema_guard(schema_guard))) {
          } else if (OB_FAIL(ObCreateUserExecutor::check_user_valid(schema_guard, ctx.get_my_session()->get_user_priv_set(),
                                                                    user_name, host_name, "CREATE USER"))) {
          } else if (OB_FAIL(user_info.set_user_name(user_name))) {
          } else if (OB_FAIL(user_info.set_host(host_name))) {
          } else if (FALSE_IT(user_info.set_ssl_type(ssl_type_enum))) {
            LOG_WARN("set ssl_type failed", K(ret));
          } else if (OB_FAIL(user_info.set_ssl_cipher(ssl_cipher))) {
          } else if (OB_FAIL(user_info.set_x509_issuer(x509_issuer))) {
          } else if (OB_FAIL(user_info.set_x509_subject(x509_subject))) {
          } else if (FALSE_IT(user_info.set_password_last_changed(ObTimeUtility::current_time()))) {
            LOG_WARN("set set_password_last_changed failed", K(ret));
          } else {
            
            if (user_name.empty()) {
              user_info.set_user_id(OB_EMPTY_USER_ID);
            }
            user_info.set_max_connections(stmt.get_max_connections_per_hour());
            user_info.set_max_user_connections(stmt.get_max_user_connections());
            if (OB_FAIL(arg.user_infos_.push_back(user_info))) {
            } else {
            }
          }
        }
      }
    }

    if (OB_SUCC(ret)) {
      if (OB_FAIL(create_user(arg, ctx.root_command_service()))) {
      }
    }
  }
  return ret;
}

int ObCreateUserExecutor::create_user(
    const obcall::ObCreateUserArg& arg,
    query::ObIRootCommandService &root_commands) const
{
  int ret = OB_SUCCESS;
  ObSArray<int64_t> failed_index;
  ObSqlString fail_msg;
  obcall::ObCreateUserArg user_arg = arg;
  if (OB_FAIL(query::serialize_root_service_call([&]{ return root_commands.create_user(user_arg, failed_index); }))) {
  } else if (0 != failed_index.count()) {
    ObSArray<ObString> failed_users;
    ObSArray<ObString> failed_hosts;
    if (OB_FAIL(userinfo_extract_user_name(arg.user_infos_, failed_index, failed_users, failed_hosts))) {
    } else if (OB_FAIL(ObDropUserExecutor::build_fail_msg(failed_users, failed_hosts, fail_msg))) {
    } else {
      ret = OB_CANNOT_USER;
      LOG_USER_ERROR(OB_CANNOT_USER, (int)strlen("CREATE USER"), "CREATE USER",
          (int)fail_msg.length(), fail_msg.ptr());
    }
  } else {
    //Create user completely success
  }
  return ret;
}

int ObDropUserExecutor::build_fail_msg_for_one(const ObString &user, const ObString &host,
                                               common::ObSqlString &msg) {
  int ret = OB_SUCCESS;
  if (OB_FAIL(msg.append_fmt("'%.*s'@'%.*s'",
                                    user.length(), user.ptr(),
                                    host.length(), host.ptr()))) {
  }
  return ret;
}

int ObDropUserExecutor::build_fail_msg(const common::ObIArray<common::ObString> &users,
    const common::ObIArray<common::ObString> &hosts, common::ObSqlString &msg)
{
  int ret = OB_SUCCESS;
  if (OB_UNLIKELY(users.count() < 1) || OB_UNLIKELY(users.count() != hosts.count())) {
    ret = OB_INVALID_ARGUMENT;
    LOG_WARN("invalid argument", K(users.count()), K(hosts.count()), K(ret));
  } else {
    for (int64_t i = 0; OB_SUCC(ret) && i < users.count(); ++i) {
      if (0 != i && OB_FAIL(msg.append_fmt(","))) {
        LOG_WARN("Build msg fail", K(ret));
      }
      if (OB_SUCC(ret)) {
        const ObString &user = users.at(i);
        const ObString &host = hosts.at(i);
        if (OB_FAIL(build_fail_msg_for_one(user, host, msg))) {
        }
      }
    }
  }
  return ret;
}

int ObDropUserExecutor::string_array_index_extract(const common::ObIArray<common::ObString> &src_users,
    const common::ObIArray<common::ObString> &src_hosts, const common::ObIArray<int64_t> &index,
    common::ObIArray<common::ObString> &dst_users, common::ObIArray<common::ObString> &dst_hosts)
{
  int ret = OB_SUCCESS;
  dst_users.reset();
  dst_hosts.reset();
  int64_t in = 0;
  for (int64_t i = 0; OB_SUCC(ret) && i < index.count(); ++i) {
    in = index.at(i);
    if (in >= src_users.count() || in < 0) {
      ret = OB_INVALID_ARGUMENT;
      LOG_WARN("String index out of range", K(ret), K(in), K(src_users.count()));
    } else if (OB_FAIL(dst_users.push_back(src_users.at(in)))) {
    } else if (OB_FAIL(dst_hosts.push_back(src_hosts.at(in)))) {
    }
  }
  return ret;
}

int ObDropUserExecutor::execute(ObExecContext &ctx, ObDropUserStmt &stmt)
{
  int ret = OB_SUCCESS;
  ObSqlExecutorCtx *task_exec_ctx = NULL;
  
  const ObStrings *user_names = NULL;

  if (OB_ISNULL(GCTX.schema_service_)) {
    ret = OB_ERR_UNEXPECTED;
    LOG_WARN("schema service is null", K(ret));
  } else if (OB_ISNULL(ctx.get_my_session())) {
    ret = OB_ERR_UNEXPECTED;
    LOG_WARN("unexpected error", K(ret));
  } else if (OB_ISNULL(user_names = stmt.get_users())) {
    ret = OB_INVALID_ARGUMENT;
    LOG_WARN("names is NULL", K(ret));
  } else if (OB_UNLIKELY(user_names->count() % 2 != 0)) {
    ret = OB_INVALID_ARGUMENT;
    LOG_WARN("user not specified", K(ret));
  } else if (OB_ISNULL(task_exec_ctx = GET_SQL_EXECUTOR_CTX(ctx))) {
    ret = OB_NOT_INIT;
    LOG_WARN("get task executor context failed", K(ret));
  } else {
    ObString user_name;
    ObString host_name;
    ObDropUserArg &arg = static_cast<ObDropUserArg &>(stmt.get_ddl_arg());
    
    {
      ObSchemaGetterGuard schema_guard;
      if (OB_FAIL(GCTX.schema_service_->get_runtime_schema_guard(schema_guard))) {
      }
      for (int64_t i = 0; OB_SUCC(ret) && i < user_names->count(); i += 2) {
        if (OB_FAIL(user_names->get_string(i, user_name))) {
        } else if (OB_FAIL(user_names->get_string(i + 1, host_name))) {
        } else if (OB_FAIL(arg.users_.push_back(user_name))) {
        } else if (OB_FAIL(arg.hosts_.push_back(host_name))) {
        } else if (OB_FAIL(ObCreateUserExecutor::check_user_valid(schema_guard, ctx.get_my_session()->get_user_priv_set(),
                                                                  user_name, host_name, "DROP USER"))) {
        }
      }
    }
    if (OB_SUCC(ret)) {
      if (OB_FAIL(drop_user(arg, stmt.get_if_exists(), ctx.root_command_service()))) {
      } else {
        //do nothing
      }
    }
  }
  return ret;
}

int ObDropUserExecutor::drop_user(const obcall::ObDropUserArg &arg,
                                  bool if_exist_stmt,
                                  query::ObIRootCommandService &root_commands)
{
  int ret = OB_SUCCESS;
  if (OB_UNLIKELY(!arg.is_valid())) {
    ret = OB_INVALID_ARGUMENT;
    LOG_WARN("arg is invalid", K(arg), K(ret));
  } else if (OB_UNLIKELY(arg.users_.count() < 1)) {
    ret = OB_INVALID_ARGUMENT;
    LOG_WARN("user not specified", K(ret));
  } else {
    ObSArray<int64_t> failed_index;
    ObSqlString fail_msg;
    if (OB_FAIL(query::serialize_root_service_call([&]{ return root_commands.drop_user(arg, failed_index); }))) {
    }
    if (0 != failed_index.count()) {
      ObSArray<ObString> failed_users;
      ObSArray<ObString> failed_hosts;
      if (OB_FAIL(ObDropUserExecutor::string_array_index_extract(arg.users_, arg.hosts_,
                                                                 failed_index, failed_users,
                                                                 failed_hosts))) {
      } else if (if_exist_stmt) {
        if (OB_UNLIKELY(failed_users.count() < 1) || OB_UNLIKELY(failed_users.count() != failed_users.count())) {
          ret = OB_INVALID_ARGUMENT;
          LOG_WARN("invalid argument", K(failed_users.count()), K(failed_users.count()), K(ret));
        } else {
          for (int i = 0; OB_SUCC(ret) && i < failed_users.count(); ++i) {
            ObSqlString fail_msg_one;
            if (OB_FAIL(ObDropUserExecutor::build_fail_msg_for_one(failed_users.at(i), failed_hosts.at(i), fail_msg_one))) {
            } else {
              LOG_USER_WARN(OB_CANNOT_USER_IF_EXISTS, (int)fail_msg_one.length(), fail_msg_one.ptr());
            }
          }
        }
      } else if (!if_exist_stmt) {
        if (OB_FAIL(ObDropUserExecutor::build_fail_msg(failed_users, failed_hosts, fail_msg))) {
        } else {
          const char *ERR_CMD = (arg.is_role_) ? "DROP ROLE" : "DROP USER";
          ret = OB_CANNOT_USER;
          LOG_USER_ERROR(OB_CANNOT_USER, (int)strlen(ERR_CMD), ERR_CMD, (int)fail_msg.length(), fail_msg.ptr());
        }
      }
    }
  }
  return ret;
}

int ObLockUserExecutor::execute(ObExecContext &ctx, ObLockUserStmt &stmt)
{
  int ret = OB_SUCCESS;
  ObSqlExecutorCtx *task_exec_ctx = NULL;
  
  const ObStrings *user_names = NULL;
  if (OB_ISNULL(user_names = stmt.get_users())) {
    ret = OB_INVALID_ARGUMENT;
    LOG_WARN("names is NULL", K(ret));
  } else if (OB_UNLIKELY(user_names->count() % 2 != 0)) {
    ret = OB_INVALID_ARGUMENT;
    LOG_WARN("user not specified", K(ret));
  } else if (OB_ISNULL(task_exec_ctx = GET_SQL_EXECUTOR_CTX(ctx))) {
    ret = OB_NOT_INIT;
    LOG_WARN("get task executor context failed", K(ret));
  } else {
    ObString user_name;
    ObString host_name;
    ObLockUserArg &arg = static_cast<ObLockUserArg &>(stmt.get_ddl_arg());
    
    arg.locked_ = stmt.is_locked();
    for (int64_t i = 0; OB_SUCC(ret) && i < user_names->count(); i += 2) {
      if (OB_FAIL(user_names->get_string(i, user_name))) {
      } else if (OB_FAIL(user_names->get_string(i + 1, host_name))) {
      } else if (OB_FAIL(arg.users_.push_back(user_name))) {
      } else if (OB_FAIL(arg.hosts_.push_back(host_name))) {
      } else {
        //do nothing
      }
    }
    if (OB_SUCC(ret)) {
      if (OB_FAIL(lock_user(arg, ctx.root_command_service()))) {
      } else {
        //do nothing
      }
    }
  }
  return ret;
}

int ObLockUserExecutor::lock_user(
    const obcall::ObLockUserArg &arg,
    query::ObIRootCommandService &root_commands)
{
  int ret = OB_SUCCESS;
  if (OB_UNLIKELY(!arg.is_valid())) {
    ret = OB_INVALID_ARGUMENT;
    LOG_WARN("arg is invalid", K(arg), K(ret));
  } else if (OB_UNLIKELY(arg.users_.count() < 1)) {
    ret = OB_INVALID_ARGUMENT;
    LOG_WARN("user not specified", K(ret));
  } else {
    ObSArray<int64_t> failed_index;
    ObSqlString fail_msg;
    if (OB_FAIL(query::serialize_root_service_call([&]{ return root_commands.lock_user(arg, failed_index); }))) {
      LOG_WARN("Lock user failed", K(ret));
      if (OB_FAIL(ObDropUserExecutor::build_fail_msg(arg.users_, arg.hosts_, fail_msg))) {
      } else {
        ret = OB_CANNOT_USER;
        LOG_USER_ERROR(OB_CANNOT_USER, (int)strlen("LOCK USER"), "LOCK USER", (int)fail_msg.length(), fail_msg.ptr());
      }
    } else if (0 != failed_index.count()) {
      ObSArray<ObString> failed_users;
      ObSArray<ObString> failed_hosts;
      if (OB_FAIL(ObDropUserExecutor::string_array_index_extract(
          arg.users_, arg.hosts_, failed_index, failed_users, failed_hosts))) {
      } else {
        if (OB_FAIL(ObDropUserExecutor::build_fail_msg(failed_users, failed_hosts, fail_msg))) {
        } else {
          ret = OB_CANNOT_USER;
          LOG_USER_ERROR(OB_CANNOT_USER, (int)strlen("LOCK USER"), "LOCK USER", (int)fail_msg.length(), fail_msg.ptr());
        }
      }
    } else {
      //do nothing
    }
  }
  return ret;
}

int ObAlterUserRoleExecutor::set_role_exec(ObExecContext &ctx, ObAlterUserRoleStmt &stmt)
{
  int ret = OB_SUCCESS;
  ObSQLSessionInfo *session = NULL;
  uint64_t role_id = OB_INVALID_ID;
  CK (ObAlterUserRoleStmt::SET_ROLE == stmt.get_set_role_flag());
  if (OB_ISNULL(session = ctx.get_my_session())) {
    ret = OB_ERR_UNEXPECTED;
    LOG_WARN("session is NULL", K(ret));
  } else {
    const uint64_t user_id = session->get_priv_user_id();
    const ObUserInfo * user_info = NULL;
    common::ObArray<uint64_t> enable_role_id_array;
    ObSchemaGetterGuard schema_guard;

    obcall::ObAlterUserRoleArg &arg = static_cast<obcall::ObAlterUserRoleArg &>(stmt.get_ddl_arg());
    OZ (GCTX.schema_service_->get_runtime_schema_guard(
                  schema_guard));
    OZ (schema_guard.get_user_info(user_id, user_info));
    if (OB_SUCC(ret) && NULL == user_info) {
      ret = OB_ERR_UNEXPECTED;
      LOG_WARN("user info is null", K(ret));
    }
    if (OB_SUCC(ret)) {
      switch (arg.default_role_flag_) {
      case OB_DEFAULT_ROLE_ALL:
        //OZ (session->set_enable_role_array(user_info->get_role_id_array()));
        OZ (append(enable_role_id_array, user_info->get_role_id_array()));
        break;
      case OB_DEFAULT_ROLE_NONE:
        OX (enable_role_id_array.reset());
        break;
      case OB_DEFAULT_ROLE_LIST:
        OX (enable_role_id_array.reset());
        for (int i = 0; OB_SUCC(ret) && i < arg.role_id_array_.count(); i++) {
          OX (role_id = arg.role_id_array_.at(i));
          OZ (enable_role_id_array.push_back(role_id));
        }
        break;
      case OB_DEFAULT_ROLE_ALL_EXCEPT:
        OX (enable_role_id_array.reset());
        /* scan all role granted to the user */
        for (int i = 0; OB_SUCC(ret) && i < user_info->get_role_id_array().count(); i++) {
          OX (role_id = user_info->get_role_id_array().at(i));
          /* if not in execpt set, then push back */
          if (OB_SUCC(ret) && !has_exist_in_array(arg.role_id_array_, role_id)) {
            OZ (enable_role_id_array.push_back(role_id));
          }
        }
        break;
      case OB_DEFAULT_ROLE_DEFAULT:
        OX (enable_role_id_array.reset());
        for (int i = 0; OB_SUCC(ret) && i < user_info->get_role_id_array().count(); i++) {
          if (user_info->get_disable_option(user_info->get_role_id_option_array().at(i)) == 0) {
            OZ (enable_role_id_array.push_back(user_info->get_role_id_array().at(i)));
          }
        }
        break;
      }
      OZ (session->set_enable_role_array(enable_role_id_array));
    }
  }
  return ret;
}

int ObAlterUserRoleExecutor::execute(ObExecContext &ctx, ObAlterUserRoleStmt &stmt)
{
  int ret = OB_SUCCESS;
  ObSqlExecutorCtx *task_exec_ctx = NULL;

  if (OB_ISNULL(task_exec_ctx = GET_SQL_EXECUTOR_CTX(ctx))) {
    ret = OB_NOT_INIT;
    LOG_WARN("get task executor context failed", K(ret));
  } else if (ObAlterUserRoleStmt::SET_ROLE == stmt.get_set_role_flag()) {
    OZ (set_role_exec(ctx, stmt));
  } else if (ObAlterUserRoleStmt::SET_DEFAULT_ROLE == stmt.get_set_role_flag()) {
    if (OB_FAIL(query::serialize_root_service_call([&] {
          return ctx.root_command_service().alter_user_default_role(
              stmt.get_ddl_arg());
        }))) {
    }
  } else {
    ret = OB_ERR_UNEXPECTED;
    LOG_WARN("unexpected alter user role operation", K(ret), K(stmt.get_set_role_flag()));
  }
  return ret;
}

int ObRenameUserExecutor::execute(ObExecContext &ctx, ObRenameUserStmt &stmt)
{
  int ret = OB_SUCCESS;
  ObSqlExecutorCtx *task_exec_ctx = NULL;
  
  const ObStrings *rename_infos = NULL;
  if (OB_ISNULL(GCTX.schema_service_)) {
    ret = OB_ERR_UNEXPECTED;
    LOG_WARN("schema service is null", K(ret));
  } else if (OB_ISNULL(ctx.get_my_session())) {
    ret = OB_ERR_UNEXPECTED;
    LOG_WARN("unexpected error", K(ret));
  } else if (OB_ISNULL(rename_infos = stmt.get_rename_infos())) {
    ret = OB_INVALID_ARGUMENT;
    LOG_WARN("names is NULL", K(ret));
  } else if (rename_infos->count() < 1) {
    ret = OB_INVALID_ARGUMENT;
    LOG_WARN("user not specified", K(ret));
  } else if (rename_infos->count() % 4 != 0) {
    ret = OB_INVALID_ARGUMENT;
    LOG_WARN("old and new names count not match", K(ret));
  } else if (OB_ISNULL(task_exec_ctx = GET_SQL_EXECUTOR_CTX(ctx))) {
    ret = OB_NOT_INIT;
    LOG_WARN("get task executor context failed", K(ret));
  } else {
    ObString old_username;
    ObString old_hostname;
    ObString new_username;
    ObString new_hostname;
    ObRenameUserArg &arg = static_cast<ObRenameUserArg &>(stmt.get_ddl_arg());
    
    {
      ObSchemaGetterGuard schema_guard;
      if (OB_FAIL(GCTX.schema_service_->get_runtime_schema_guard(schema_guard))) {
      }
      //rename_infos arr contains old names and new names in pairs, so step is 2
      for (int64_t i = 0; OB_SUCC(ret) && i < rename_infos->count(); i += 4) {
        if (OB_FAIL(rename_infos->get_string(i, old_username))) {
        } else if (OB_FAIL(rename_infos->get_string(i + 1, old_hostname))) {
        } else if (OB_FAIL(rename_infos->get_string(i + 2, new_username))) {
        } else if (OB_FAIL(rename_infos->get_string(i + 3, new_hostname))) {
        } else if (OB_FAIL(arg.old_users_.push_back(old_username))) {
        } else if (OB_FAIL(arg.old_hosts_.push_back(old_hostname))) {
        } else if (OB_FAIL(arg.new_users_.push_back(new_username))) {
        } else if (OB_FAIL(arg.new_hosts_.push_back(new_hostname))) {
        } else if (OB_FAIL(ObCreateUserExecutor::check_user_valid(schema_guard, ctx.get_my_session()->get_user_priv_set(),
                                                                  old_username, old_hostname, "RENAME USER"))) {
        } else if (OB_FAIL(ObCreateUserExecutor::check_user_valid(schema_guard, ctx.get_my_session()->get_user_priv_set(),
                                                                  new_username, new_hostname, "RENAME USER"))) {
        }
      }
    }
    if (OB_SUCC(ret)) {
      if (OB_FAIL(rename_user(arg, ctx.root_command_service()))) {
      }
    }
  }
  return ret;
}

int ObRenameUserExecutor::rename_user(
    const obcall::ObRenameUserArg &arg,
    query::ObIRootCommandService &root_commands)
{
  int ret = OB_SUCCESS;
  if (OB_UNLIKELY(!arg.is_valid())) {
    ret = OB_INVALID_ARGUMENT;
    LOG_WARN("Invalid arg", K(arg), K(ret));
  } else if (OB_UNLIKELY(arg.old_users_.count() < 1)) {
    ret = OB_INVALID_ARGUMENT;
    LOG_WARN("user not specified", K(arg), K(ret));
  } else {
    ObSArray<int64_t> failed_index;
    ObSqlString fail_msg;
    if (OB_FAIL(query::serialize_root_service_call([&]{ return root_commands.rename_user(arg, failed_index); }))) {
      LOG_WARN("Rename user failed", K(ret));
      if (OB_FAIL(ObDropUserExecutor::build_fail_msg(arg.old_users_, arg.old_hosts_, fail_msg))) {
      } else {
        ret = OB_CANNOT_USER;
        LOG_USER_ERROR(OB_CANNOT_USER, (int)strlen("RENAME USER"), "RENAME USER", (int)fail_msg.length(), fail_msg.ptr());
      }
    } else if (0 != failed_index.count()) {
      ObSArray<ObString> failed_users;
      ObSArray<ObString> failed_hosts;
      if (OB_FAIL(ObDropUserExecutor::string_array_index_extract(
          arg.old_users_, arg.old_hosts_, failed_index, failed_users, failed_hosts))) {
      } else {
        if (OB_FAIL(ObDropUserExecutor::build_fail_msg(failed_users, failed_hosts, fail_msg))) {
        } else {
          ret = OB_CANNOT_USER;
          LOG_USER_ERROR(OB_CANNOT_USER, (int)strlen("RENAME USER"), "RENAME USER", (int)fail_msg.length(), fail_msg.ptr());
        }
      }
    } else {
      //Rename user completely success
    }
  }
  return ret;
}

}// ns sql
}// ns oceanbase
