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
#include "query/command/ob_local_command_service.h"
#include "share/ob_server_struct.h"
#include "query/command/ob_root_command_service.h"
#include "lib/stat/ob_diagnostic_info_guard.h"
#include "ob_ddl_executor_util.h"
#include "query/engine/cmd/ob_ddl_execution.h"
#include "share/ob_ddl_task_executor.h"
#include "share/ob_ex_rpc.h"
#include "share/ob_structured_event_logger.h"
#include "share/ob_share_util.h"
#include "share/schema/ob_schema_utils.h"

namespace oceanbase
{
using namespace common;
using namespace share;
using namespace share::schema;
namespace sql
{
int ObDDLExecutorUtil::handle_session_exception(ObSQLSessionInfo &session)
{
  int ret = OB_SUCCESS;
  
  bool is_standby = false;
  if (OB_UNLIKELY(session.is_query_killed())) {
    ret = OB_ERR_QUERY_INTERRUPTED;
    LOG_WARN("query is killed", K(ret));
  } else if (OB_UNLIKELY(session.is_zombie())) {
    ret = OB_SESSION_KILLED;
    LOG_WARN("session is killed", K(ret));
  } else if (OB_FAIL(ObShareUtil::check_if_server_role_is_standby( is_standby))) {
  } else if (is_standby) {
    ret = OB_SESSION_KILLED;
    LOG_WARN("session is killed", KR(ret));
  }
  return ret;
}

int ObDDLExecutorUtil::wait_ddl_finish(const int64_t task_id,
    const bool ddl_need_retry_at_executor,
    ObSQLSessionInfo *session,
    query::ObIQueryRuntimeEnvironment &runtime_environment,
    query::ObILocalCommandService &local_command_service,
    const bool is_support_cancel)
{
  int ret = OB_SUCCESS;
  const int64_t retry_interval = 100 * 1000;
  ObAddr unused_addr;
  bool is_table_exist = false;
  int64_t unused_user_msg_len = 0;
  THIS_WORKER.set_timeout_ts(ObTimeUtility::current_time() + OB_MAX_USER_SPECIFIED_TIMEOUT);
  ObDDLErrorMessageTableOperator::ObBuildDDLErrorMessage error_message;
  if (OB_UNLIKELY(task_id <= 0)) {
    ret = OB_INVALID_ARGUMENT;
    LOG_WARN("invalid argument", K(ret), K(task_id));
  } else {
    SERVER_EVENT_ADD("ddl", "start wait ddl finish",
      "ret", ret,
      "trace_id", *ObCurTraceId::get_trace_id(),
      "task_id", task_id,
      "rpc_dest", GCTX.self_addr());
    LOG_INFO("start wait ddl finsih", K(task_id), "ddl_event_info", ObDDLEventInfo(GCTX.self_addr()));

    int tmp_ret = OB_SUCCESS;
    while (OB_SUCC(ret)) {
      if (OB_SUCCESS == ObDDLErrorMessageTableOperator::get_ddl_error_message(task_id, -1 /* target_object_id */, unused_addr, false /* is_ddl_retry_task */, *GCTX.sql_proxy_, error_message, unused_user_msg_len)) {
        ret = error_message.ret_code_;
        if (OB_SUCCESS != ret) {
          if (ddl_need_retry_at_executor) {
            ret = share::ObIDDLTask::in_ddl_retry_white_list(ret) ? OB_EAGAIN : ret;
            LOG_WARN("is ddl need retry at user", K(ret));
          } else {
            FORWARD_USER_ERROR(ret, error_message.user_message_);
          }
        } else if (error_message.published_schema_version_ != OB_INVALID_VERSION) {
          ObTimeoutCtx ctx;
          int64_t start_time = ObTimeUtility::current_time();
          if (OB_FAIL(ctx.set_timeout(THIS_WORKER.get_timeout_remain()))) {
          } else if (OB_FAIL(ObDDLExecutorUtil::wait_local_schema_visible(
                      ctx, session, error_message.published_schema_version_))) {
          } else {
            int64_t refresh_time = ObTimeUtility::current_time() - start_time;
            LOG_INFO("parallel ddl wait schema", KR(ret), K(refresh_time),
                                                 K_(error_message.published_schema_version));
          }
        }
        break;
      } else {
        if (OB_FAIL(ret)) {
        }

        if (OB_FAIL(ret)) {
        } else if (nullptr != session && OB_FAIL(handle_session_exception(*session))) {
          LOG_WARN("session exeception happened", K(ret), K(is_support_cancel));
          if (is_support_cancel && OB_TMP_FAIL(cancel_ddl_task(local_command_service))) {
            LOG_WARN("cancel ddl task failed", K(tmp_ret));
            ret = OB_SUCCESS;
          } else {
            break;
          }
        } 
        
        if (OB_FAIL(ret)) {
        } else if (is_server_stopped(runtime_environment)) {
          ret = OB_TIMEOUT;
          FORWARD_USER_ERROR(ret, "DDL execution status is undecided, please check later if it finishes successfully or not.");
          LOG_WARN("server is stopping, check whether the ddl task finish successfully or not", K(ret), K(task_id));
        } else {
          ob_usleep(retry_interval);
        }
      }
    }

    SERVER_EVENT_ADD("ddl", "end wait ddl finish",
      "ret", error_message.ret_code_,
      "trace_id", *ObCurTraceId::get_trace_id(),
      "task_id", task_id,
      "rpc_dest", GCTX.self_addr());
    LOG_INFO("finish wait ddl", K(ret), K(task_id), "ddl_event_info", ObDDLEventInfo(GCTX.self_addr()), K(error_message));
  }
  return ret;
}

int ObDDLExecutorUtil::wait_build_index_finish(const int64_t task_id, bool &is_finish)
{
  int ret = OB_SUCCESS;
  int tmp_ret = OB_SUCCESS;
  ObAddr unused_addr;
  int64_t unused_user_msg_len = 0;
  THIS_WORKER.set_timeout_ts(ObTimeUtility::current_time() + OB_MAX_USER_SPECIFIED_TIMEOUT);
  share::ObDDLErrorMessageTableOperator::ObBuildDDLErrorMessage error_message;
  is_finish = false;
  SERVER_EVENT_ADD("ddl", "start wait build index finish",
    "ret", ret,
    "trace_id", *ObCurTraceId::get_trace_id(),
    "task_id", task_id);
  LOG_INFO("start wait build index finish", K(task_id), "ddl_event_info", ObDDLEventInfo(GCTX.self_addr()));

  if (OB_UNLIKELY(task_id <= 0)) {
    ret = OB_INVALID_ARGUMENT;
    LOG_WARN("invalid arguments", K(ret), K(task_id));
  } else if (OB_SUCCESS == share::ObDDLErrorMessageTableOperator::get_ddl_error_message(task_id, -1 /* target_object_id */, unused_addr, false /* is_ddl_retry_task */, *GCTX.sql_proxy_, error_message, unused_user_msg_len)) {
    ret = error_message.ret_code_;
    if (OB_SUCCESS != ret) {
      FORWARD_USER_ERROR(ret, error_message.user_message_);
    }
    is_finish = true;
  }

  SERVER_EVENT_ADD("ddl", "end wait build index finish",
    "ret", error_message.ret_code_,
    "trace_id", *ObCurTraceId::get_trace_id(),
    "task_id", task_id);
  LOG_INFO("finish wait build index", K(ret), "ddl_event_info", ObDDLEventInfo(GCTX.self_addr()), K(error_message));
  return ret;
}

int ObDDLExecutorUtil::wait_ddl_retry_task_finish(const int64_t task_id,
    ObSQLSessionInfo &session,
    query::ObIQueryRuntimeEnvironment &runtime_environment,
    query::ObILocalCommandService &local_command_service,
    int64_t &affected_rows)
{
  int ret = OB_SUCCESS;
  affected_rows = 0;
  const int64_t retry_interval = 100 * 1000;
  ObAddr unused_addr;
  bool is_table_exist = false;
  int64_t forward_user_msg_len = 0;
  THIS_WORKER.set_timeout_ts(ObTimeUtility::current_time() + OB_MAX_USER_SPECIFIED_TIMEOUT);
  ObDDLErrorMessageTableOperator::ObBuildDDLErrorMessage error_message;
  if (OB_UNLIKELY(task_id <= 0)) {
    ret = OB_INVALID_ARGUMENT;
    LOG_WARN("invalid argument", K(ret), K(task_id));
  } else {
    SERVER_EVENT_ADD("ddl", "start wait ddl retry task finish",
      "ret", ret,
      "trace_id", *ObCurTraceId::get_trace_id(),
      "task_id", task_id,
      "rpc_dest", GCTX.self_addr());
    LOG_INFO("start wait ddl retry task finish", K(task_id), "ddl_event_info", ObDDLEventInfo(GCTX.self_addr()), K(error_message));

    bool is_primary_server = true;
    int tmp_ret = OB_SUCCESS;
    while (OB_SUCC(ret)) {
      if (OB_SUCCESS == ObDDLErrorMessageTableOperator::get_ddl_error_message(task_id, -1 /* target_object_id */, unused_addr, true /* is_ddl_retry_task */, *GCTX.sql_proxy_, error_message, forward_user_msg_len)) {
        // Here, `forward_user_msg_len` is the length of serialized hex user message.
        // Forward_user_msg_len is not 0, which means rpc::frame::ObResultCode is not empty. Thus, we need to
        // forward_user_error/ forward_user_warn/ forward_user_note.
        ret = error_message.ret_code_;
        if (OB_UNLIKELY(forward_user_msg_len == 0 && OB_SUCCESS != error_message.ret_code_)) {
          const char *str_user_error = ob_errpkt_strerror(error_message.ret_code_);
          FORWARD_USER_ERROR(error_message.ret_code_, str_user_error);
          FLOG_INFO("error code is not succ, but forward user msg is null", K(ret), K(error_message), K(str_user_error));
        } else if (forward_user_msg_len > 0) {
          int64_t pos = 0;
          int tmp_ret = OB_SUCCESS;
          rpc::frame::ObResultCode result_code;
          if (OB_SUCCESS != (tmp_ret = result_code.deserialize(error_message.user_message_, forward_user_msg_len, pos))) {
          } else if (OB_UNLIKELY(OB_SUCCESS != result_code.rcode_)) {
            FORWARD_USER_ERROR(result_code.rcode_, result_code.msg_);
          } else {
            for (int i = 0; OB_SUCCESS == tmp_ret && i < result_code.warnings_.count(); ++i) {
              const common::ObWarningBuffer::WarningItem warning_item = result_code.warnings_.at(i);
              if (ObLogger::USER_WARN == warning_item.log_level_) {
                FORWARD_USER_WARN(warning_item.code_, warning_item.msg_);
              } else if (ObLogger::USER_NOTE == warning_item.log_level_) {
                FORWARD_USER_NOTE(warning_item.code_, warning_item.msg_);
              } else {
                tmp_ret = OB_ERR_UNEXPECTED;
                LOG_WARN("unknown log type", K(ret), K(tmp_ret), K(warning_item));
              }
            }
          }
        }
        break;
      } else {
        if (OB_FAIL(ret)) {
        } else if (OB_TMP_FAIL(ObShareUtil::is_primary_server(is_primary_server))) {
        } else if (!is_primary_server) {
          ret = OB_STANDBY_DATABASE_READ_ONLY;
          FORWARD_USER_ERROR(ret, "DDL execution status is undecided, please check later if it finishes successfully or not.");
          LOG_WARN("server is standby now, stop wait", K(ret));
          break;
        }
        if (OB_FAIL(ret)) {
        } else if (OB_FAIL(handle_session_exception(session))) {
          LOG_WARN("session exception happened", K(ret));
          if (OB_TMP_FAIL(cancel_ddl_task(local_command_service))) {
            LOG_WARN("cancel ddl task failed", K(tmp_ret));
            ret = OB_SUCCESS;
          } else {
            break;
          }
        } 
        
        if (OB_FAIL(ret)) {
        } else if (is_server_stopped(runtime_environment)) {
          ret = OB_TIMEOUT;
          FORWARD_USER_ERROR(ret, "DDL execution status is undecided, please check later if it finishes successfully or not.");
          LOG_WARN("server is stopping, check whether the ddl task finish successfully or not", K(ret), K(task_id));
        } else {
          ob_usleep(retry_interval);
        }
      }
    }
    affected_rows = error_message.affected_rows_;

    SERVER_EVENT_ADD("ddl", "end wait ddl retry task finish",
      "ret", error_message.ret_code_,
      "trace_id", *ObCurTraceId::get_trace_id(),
      "task_id", task_id,
      "rpc_dest", GCTX.self_addr());
    LOG_INFO("fnish wait ddl retry task", K(ret), K(task_id), "ddl_event_info", ObDDLEventInfo(GCTX.self_addr()), K(error_message));
  }
  return ret;
}

int ObDDLExecutorUtil::cancel_ddl_task(
    query::ObILocalCommandService &local_command_service)
{
  int ret = OB_SUCCESS;
  obcall::ObCancelTaskArg rpc_arg;
  rpc_arg.task_id_ = *ObCurTraceId::get_trace_id();

  if (OB_FAIL(ex_rpc::sync_call(
      [&]{ return local_command_service.cancel_sys_task(rpc_arg.task_id_); }))) {
    if (OB_ENTRY_NOT_EXIST == ret) {
      ret = OB_SUCCESS;
    } else {
      LOG_WARN("failed to cancel sys task", K(ret), K(rpc_arg));
    }
  }
  SERVER_EVENT_ADD("ddl", "finish cancel ddl task",
    "ret", ret,
    "trace_id", *ObCurTraceId::get_trace_id(),
    "rpc_dest", GCTX.self_addr());
  LOG_INFO("finish cancel ddl task", K(ret), K(rpc_arg), "rpc_dest", GCTX.self_addr(), "ddl_event_info", ObDDLEventInfo(GCTX.self_addr()));
  return ret;
}

int ObDDLExecutorUtil::execute_pcreate_table(ObSQLSessionInfo *my_session,
                                            query::ObIRootCommandService &root_commands,
                                            const char* parallel_ddl_type,
                                            const obcall::ObCreateTableArg &arg, obcall::ObCreateTableRes &res)
{
  int ret = OB_SUCCESS;
  const int64_t start_time = ObTimeUtility::current_time();
  ObTimeoutCtx ctx;
  if (OB_FAIL(ctx.set_timeout(THIS_WORKER.get_timeout_remain()))) {
  } else if (OB_FAIL(root_commands.parallel_create_table(arg, res))) {
  } else {
    int64_t refresh_time = ObTimeUtility::current_time();
    if (!res.do_nothing_ && OB_FAIL(ObDDLExecutorUtil::wait_local_schema_visible(
        ctx, my_session, res.schema_version_))) {
      LOG_WARN("fail to wait for local schema visibility", KR(ret), K(res));
    }
    int64_t end_time = ObTimeUtility::current_time();
    LOG_INFO(parallel_ddl_type, KR(ret),
            "cost", end_time - start_time,
            "execute_time", refresh_time - start_time,
            "wait_schema", end_time - refresh_time);
  }
  return ret;
}

bool ObDDLExecutorUtil::is_server_stopped(
    query::ObIQueryRuntimeEnvironment &runtime_environment)
{
  return query::query_server_stopped(runtime_environment);
}

} //end namespace sql
} //end namespace oceanbase

namespace oceanbase
{
namespace sql
{
int ObDDLExecutorUtil::wait_local_schema_visible(
    const ObTimeoutCtx &ctx,
    sql::ObSQLSessionInfo *session,
    const int64_t schema_version)
{
  int ret = OB_SUCCESS;
  ObMultiVersionSchemaService *schema_service = NULL;
  bool schema_visible = false;
  if (OB_ISNULL(session) || OB_UNLIKELY(schema_version <= 0)) {
    ret = OB_INVALID_ARGUMENT;
    LOG_WARN("invalid arg", KR(ret), KP(session), K(schema_version));
  } else if (OB_ISNULL(schema_service = GCTX.schema_service_)) {
    ret = OB_ERR_UNEXPECTED;
    LOG_WARN("schema_service is null", KR(ret));
  }
  while (OB_SUCC(ret) && ctx.get_timeout() > 0 && !schema_visible) {
    int64_t refreshed_schema_version = OB_INVALID_VERSION;
    if (OB_FAIL(ObDDLExecutorUtil::handle_session_exception(*session))) {
    } else if (OB_FAIL(schema_service->get_runtime_refreshed_schema_version(refreshed_schema_version))) {
    } else if (refreshed_schema_version >= schema_version) {
      schema_visible = true;
    } else {
      if (REACH_TIME_INTERVAL(1000 * 1000L)) { // 1s
        LOG_WARN("local schema version not visible", K(refreshed_schema_version), K(schema_version));
      }
      ob_usleep(10 * 1000L); // 10ms
    }
  }
  if (OB_SUCC(ret) && !schema_visible) {
    ret = OB_TIMEOUT;
    LOG_WARN("wait local schema visible timeout", KR(ret), K(schema_version));
  }
  return ret;
}
}  // namespace sql

namespace query
{

int ObDDLExecution::wait_ddl_finish(
    const int64_t task_id,
    const bool ddl_need_retry_at_executor,
    sql::ObSQLSessionInfo *session,
    ObIQueryRuntimeEnvironment &runtime_environment,
    ObILocalCommandService &local_command_service,
    const bool is_support_cancel)
{
  return sql::ObDDLExecutorUtil::wait_ddl_finish(
      task_id, ddl_need_retry_at_executor, session,
      runtime_environment, local_command_service, is_support_cancel);
}

int ObDDLExecution::handle_session_exception(sql::ObSQLSessionInfo &session)
{
  return sql::ObDDLExecutorUtil::handle_session_exception(session);
}

} // namespace query
}  // namespace oceanbase
