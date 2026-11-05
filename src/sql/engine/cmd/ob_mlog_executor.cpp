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
#include "lib/stat/ob_diagnostic_info_guard.h"
#include "sql/engine/cmd/ob_mlog_executor.h"
#include "rootserver/ob_rs_serial_call.h"
#include "rootserver/ob_root_service.h"
#include "sql/resolver/ddl/ob_create_mlog_stmt.h"
#include "sql/resolver/ddl/ob_drop_mlog_stmt.h"
#include "sql/resolver/ob_resolver_utils.h"
#include "sql/engine/cmd/ob_ddl_executor_util.h"
#include "sql/engine/cmd/ob_index_executor.h"
#include "observer/ob_server_event_history_table_operator.h"

namespace oceanbase
{
using namespace common;
namespace sql
{
ObCreateMLogExecutor::ObCreateMLogExecutor()
{

}

ObCreateMLogExecutor::~ObCreateMLogExecutor()
{

}

int ObCreateMLogExecutor::execute(ObExecContext &ctx, ObCreateMLogStmt &stmt)
{
  int ret = OB_SUCCESS;
  obcall::ObCreateMLogArg &create_mlog_arg = stmt.get_create_mlog_arg();
  obcall::ObCreateMLogRes create_mlog_res;
  ObString first_stmt;
  ObSQLSessionInfo *my_session = ctx.get_my_session();
  ObTaskExecutorCtx *task_exec_ctx = nullptr;
  bool is_sync_ddl_user = false;

  if (OB_ISNULL(my_session)) {
    ret = OB_ERR_UNEXPECTED;
    LOG_WARN("failed to get my session", KR(ret), K(ctx));
  } else if (OB_FAIL(stmt.get_first_stmt(first_stmt))) {
    LOG_WARN("failed to get first statement", KR(ret));
  } else if (OB_FALSE_IT(create_mlog_arg.ddl_stmt_str_ = first_stmt)) {
  } else if (OB_ISNULL(task_exec_ctx = GET_TASK_EXECUTOR_CTX(ctx))) {
    ret = OB_NOT_INIT;
    LOG_WARN("failed to get task executor context", KR(ret));
  } else if (OB_INVALID_ID == create_mlog_arg.session_id_
             && FALSE_IT(create_mlog_arg.session_id_ = my_session->get_sessid_for_table())) {
    //impossible
  } else if (OB_FAIL(rootserver::serial_call([&]{ return GCTX.root_service_->create_mlog(create_mlog_arg, create_mlog_res); }))) {
    LOG_WARN("failed to create mlog", KR(ret), K(create_mlog_arg));
  } else if (OB_FAIL(ObResolverUtils::check_sync_ddl_user(my_session, is_sync_ddl_user))) {
    LOG_WARN("failed to check sync ddl user", KR(ret));
  } else if (!is_sync_ddl_user) {
    if (OB_UNLIKELY(OB_INVALID_ID == create_mlog_res.mlog_table_id_)) {
      ret = OB_ERR_UNEXPECTED;
      LOG_WARN("invalid mlog table id", KR(ret), K(create_mlog_res));
    } else if (OB_INVALID_VERSION == create_mlog_res.schema_version_) {
      ret = OB_ERR_UNEXPECTED;
      LOG_WARN("unexpected schema version", KR(ret), K(create_mlog_res));
    } else if (OB_FAIL(ObDDLExecutorUtil::wait_ddl_finish(create_mlog_res.task_id_, false/*do not need retry at executor*/, my_session))) {
      LOG_WARN("failed to wait ddl finish", KR(ret));
    }
  }

  return ret;
}

ObDropMLogExecutor::ObDropMLogExecutor()
{
}

ObDropMLogExecutor::~ObDropMLogExecutor()
{
}

int ObDropMLogExecutor::execute(ObExecContext &ctx, ObDropMLogStmt &stmt)
{
  int ret = OB_SUCCESS;
  ObTaskExecutorCtx *task_exec_ctx = NULL;
  ObSQLSessionInfo *my_session = ctx.get_my_session();
  obcall::ObDropIndexArg &drop_index_arg = stmt.get_drop_index_arg();
  obcall::ObDropIndexRes drop_index_res;
  ObString first_stmt;

  if (OB_ISNULL(my_session)) {
    ret = OB_ERR_UNEXPECTED;
    LOG_WARN("failed to get my session", KR(ret), K(ctx));
  } else if (OB_ISNULL(task_exec_ctx = GET_TASK_EXECUTOR_CTX(ctx))) {
    ret = OB_NOT_INIT;
    LOG_WARN("failed to get task executor context");
  } else if (OB_FAIL(stmt.get_first_stmt(first_stmt))) {
    LOG_WARN("failed to get first statement", KR(ret));
  } else if (OB_FALSE_IT(drop_index_arg.ddl_stmt_str_ = first_stmt)) {
  }  else if ((OB_INVALID_ID == drop_index_arg.session_id_)
      && FALSE_IT(drop_index_arg.session_id_ = my_session->get_sessid_for_table())) {
    //impossible
  } else if (FALSE_IT(drop_index_arg.consumer_group_id_ = THIS_WORKER.get_group_id())) {
  } else if (FALSE_IT(drop_index_arg.is_add_to_scheduler_ = true)) {
  } else if (OB_FAIL(rootserver::serial_call([&]{ return GCTX.root_service_->drop_index(drop_index_arg, drop_index_res); }))) {
    LOG_WARN("rpc proxy drop index failed", "dst", GCTX.self_addr(), KR(ret));
  } else if (OB_FAIL(ObDropIndexExecutor::wait_drop_index_finish(drop_index_res.task_id_,
                                                                 *my_session))) {
    LOG_WARN("failed to wait drop index finish", KR(ret));
  }
  SERVER_EVENT_ADD("ddl", "drop mlog execute finish",
    "ret", ret,
    "trace_id", *ObCurTraceId::get_trace_id(),
    "task_id", drop_index_res.task_id_,
    "table_id", drop_index_res.index_table_id_,
    "schema_version", drop_index_res.schema_version_);
  SQL_ENG_LOG(INFO, "finish drop mlog execute.", KR(ret),
      "ddl_event_info", ObDDLEventInfo(), K(stmt), K(drop_index_arg));

  return ret;
}
} // namespace sql
} // namespace oceanbase
