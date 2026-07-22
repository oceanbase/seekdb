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
#include "sql/resolver/cmd/ob_kill_stmt.h"
#include "sql/engine/cmd/ob_kill_session_arg.h"
#include "sql/engine/cmd/ob_kill_executor.h"
#include "observer/ob_server.h"
namespace oceanbase
{
using namespace common;
namespace sql
{

int ObKillExecutor::execute(ObExecContext &ctx, ObKillStmt &stmt)
{
  int ret = OB_SUCCESS;
  ObKillSessionArg arg;
  ObSQLSessionMgr &session_mgr = OBSERVER.get_sql_session_mgr();
  if (OB_FAIL(arg.init(ctx, stmt))) {
    LOG_WARN("fail to init kill_session arg", K(ret), K(arg), K(ctx), K(stmt));
  } else if (OB_FAIL(kill_session(arg, session_mgr))) {
    if (OB_ENTRY_NOT_EXIST == ret) {
      ret = OB_UNKNOWN_CONNECTION;
    } else {
      LOG_WARN("fail to kill session", K(ret), K(arg));
    }
  }

  if (OB_UNKNOWN_CONNECTION == ret) {
    LOG_USER_ERROR(OB_UNKNOWN_CONNECTION, static_cast<uint64_t>(arg.sess_id_));
  } else if (OB_ERR_KILL_DENIED == ret) {
    LOG_USER_ERROR(OB_ERR_KILL_DENIED, static_cast<uint64_t>(arg.sess_id_));
  }
  return ret;
}

//If you are in system tenant, you can kill all threads and statements in any tenant.
//If you have the SUPER privilege, you can kill all threads and statements at your Tenant.
//Otherwise, you can kill only your own threads and statements.
int ObKillSession::kill_session(const ObKillSessionArg &arg, ObSQLSessionMgr &sess_mgr)
{
  int ret = OB_SUCCESS;
  ObSQLSessionInfo *sess_info = NULL;
  uint32_t sess_id = arg.sess_id_;
  ObSessionGetterGuard guard(sess_mgr, sess_id);
  if (OB_FAIL(guard.get_session(sess_info))) {
    LOG_WARN("fail to get session", K(ret), K(sess_id));
  } else if (OB_ISNULL(sess_info)) {
    ret = OB_ERR_UNEXPECTED;
    LOG_WARN("session info is NULL", K(ret), K(arg));
  } else if (sess_info->is_real_inner_session()) {
    ret = OB_ERR_KILL_DENIED;
    LOG_WARN("It is not allowed to close the inner session", K(ret), K(arg));
  } else if (OB_FAIL(arg.check_auth_for_kill(1UL, sess_info->get_user_id()))) {
    ret = OB_ERR_KILL_DENIED;
    LOG_WARN("no permissions for kill", K(ret), K(arg.sess_id_));
  } else {
    if (arg.is_query_) {
      if (OB_FAIL(sess_mgr.kill_query(*sess_info))) {
        LOG_WARN("fail to kill query", K(ret), K(arg));
      }
    } else {
      if (OB_FAIL(sess_mgr.kill_session(*sess_info))) {
        LOG_WARN("fail to kill session", K(ret), K(arg));
      }
    }
  }
  return ret;
}

}// sql
}// oceanbase
