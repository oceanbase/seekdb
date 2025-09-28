/**
 * Copyright (c) 2021 OceanBase
 * OceanBase CE is licensed under Mulan PubL v2.
 * You can use this software according to the terms and conditions of the Mulan PubL v2.
 * You may obtain a copy of Mulan PubL v2 at:
 *          http://license.coscl.org.cn/MulanPubL-2.0
 * THIS SOFTWARE IS PROVIDED ON AN "AS IS" BASIS, WITHOUT WARRANTIES OF ANY KIND,
 * EITHER EXPRESS OR IMPLIED, INCLUDING BUT NOT LIMITED TO NON-INFRINGEMENT,
 * MERCHANTABILITY OR FIT FOR A PARTICULAR PURPOSE.
 * See the Mulan PubL v2 for more details.
 */

#define USING_LOG_PREFIX SQL_ENG
#include "sql/engine/cmd/ob_role_cmd_executor.h"

#include "lib/encrypt/ob_encrypted_helper.h"
#include "sql/resolver/dcl/ob_create_role_stmt.h"
#include "sql/resolver/dcl/ob_drop_role_stmt.h"
#include "sql/resolver/dcl/ob_alter_role_stmt.h"
#include "sql/engine/ob_exec_context.h"
#include "sql/engine/cmd/ob_user_cmd_executor.h"

namespace oceanbase
{
using namespace common;
using namespace obrpc;
using namespace share::schema;
namespace sql
{

int ObCreateRoleExecutor::execute(ObExecContext &ctx, ObCreateRoleStmt &stmt)
{
  int ret = OB_SUCCESS;
  ObTaskExecutorCtx *task_exec_ctx = NULL;
  obrpc::ObCommonRpcProxy *common_rpc_proxy = NULL;
  ObSQLSessionInfo *mysession = NULL;
  const uint64_t tenant_id = stmt.get_tenant_id();
  const ObString &role_name = stmt.get_role_name();
  const ObString &pwd = stmt.get_password();
  ObCreateUserArg arg;

  if (OB_ISNULL(task_exec_ctx = GET_TASK_EXECUTOR_CTX(ctx))) {
    ret = OB_NOT_INIT;
    LOG_WARN("get task executor context failed");
  } else if (OB_ISNULL(common_rpc_proxy = task_exec_ctx->get_common_rpc())) {
    ret = OB_NOT_INIT;
    LOG_WARN("get common rpc proxy failed");
  } else if (OB_ISNULL(mysession = ctx.get_my_session())) {
    ret = OB_ERR_UNEXPECTED;
    LOG_WARN("failed to get mysession", K(ret));
  } else {
    arg.tenant_id_ = tenant_id;
    arg.exec_tenant_id_ = tenant_id;
    arg.creator_id_ = mysession->get_user_id();
  }

  if (OB_SUCC(ret) && lib::is_mysql_mode()) {
    ObSArray<int64_t> failed_index;
    arg.if_not_exist_ = stmt.get_if_not_exists();
    arg.is_create_role_ = true;
    for (int i = 0; OB_SUCC(ret) && i < stmt.get_user_names().count(); i++) {
      ObUserInfo user_info;
      user_info.set_tenant_id(tenant_id);
      user_info.set_type(OB_ROLE);
      user_info.set_user_name(stmt.get_user_names().at(i));
      user_info.set_host(stmt.get_host_names().at(i));
      user_info.set_is_locked(true);
      OZ (arg.user_infos_.push_back(user_info));
    }
    OZ (common_rpc_proxy->create_user(arg, failed_index));
  }

  return ret;
}

int ObDropRoleExecutor::execute(ObExecContext &ctx, ObDropRoleStmt &stmt)
{
  int ret = OB_SUCCESS;
  ObTaskExecutorCtx *task_exec_ctx = NULL;
  obrpc::ObCommonRpcProxy *common_rpc_proxy = NULL;
  const uint64_t tenant_id = stmt.get_tenant_id();
  ObDropUserArg &arg = static_cast<ObDropUserArg &>(stmt.get_ddl_arg());
  if (OB_INVALID_ID == tenant_id) {
    ret = OB_INVALID_ARGUMENT;
    LOG_WARN("tenant is invalid", K(ret));
  } else if (OB_ISNULL(task_exec_ctx = GET_TASK_EXECUTOR_CTX(ctx))) {
    ret = OB_NOT_INIT;
    LOG_WARN("get task executor context failed", K(ret));
  } else if (NULL == (common_rpc_proxy = task_exec_ctx->get_common_rpc())) {
    ret = OB_NOT_INIT;
    LOG_WARN("get common rpc proxy failed", K(ret));
  } else {
    arg.tenant_id_ = tenant_id;
    arg.exec_tenant_id_ = tenant_id;
    arg.is_role_ = true;
    for (int i = 0; OB_SUCC(ret) && i < stmt.get_user_names().count(); i++) {
      OZ (arg.users_.push_back(stmt.get_user_names().at(i)));
      OZ (arg.hosts_.push_back(stmt.get_host_names().at(i)));
    }
    OZ (ObDropUserExecutor::drop_user(common_rpc_proxy, arg, stmt.get_if_exists()));
  }

  return ret;
}

int ObAlterRoleExecutor::execute(ObExecContext &ctx, ObAlterRoleStmt &stmt)
{
  int ret = OB_SUCCESS;
  ObTaskExecutorCtx *task_exec_ctx = NULL;
  obrpc::ObCommonRpcProxy *common_rpc_proxy = NULL;
  if (OB_ISNULL(task_exec_ctx = GET_TASK_EXECUTOR_CTX(ctx))) {
    ret = OB_NOT_INIT;
    LOG_WARN("get task executor context failed");
  } else if (OB_ISNULL(common_rpc_proxy = task_exec_ctx->get_common_rpc())) {
    ret = OB_NOT_INIT;
    LOG_WARN("get common rpc proxy failed");
  } else {
    ObAlterRoleArg &arg = static_cast<ObAlterRoleArg &>(stmt.get_ddl_arg());
    char enc_buf[ENC_BUF_LEN] = {0};
    arg.tenant_id_ = stmt.get_tenant_id();
    arg.role_name_ = stmt.get_role_name();
    arg.host_name_ = ObString(OB_DEFAULT_HOST_NAME);
    const ObString &pwd = stmt.get_password();
    ObString pwd_enc;
    if (pwd.length() > 0 && stmt.get_need_enc()) {
      // 采用OB统一的加密方式
      if (OB_FAIL(ObCreateUserExecutor::encrypt_passwd(pwd, pwd_enc, enc_buf, ENC_BUF_LEN))) {
        LOG_WARN("Encrypt password failed", K(ret));
      }
    } else {
      pwd_enc = pwd;
    }
    arg.pwd_enc_ = pwd_enc;

    if (OB_FAIL(ret)) {
    } else if (OB_FAIL(common_rpc_proxy->alter_role(arg))) {
      LOG_WARN("Alter user error", K(ret));
    }
  }
  return ret;
}

}// ns sql
}// ns oceanbase
