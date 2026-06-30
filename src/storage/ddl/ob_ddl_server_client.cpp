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

#define USING_LOG_PREFIX STORAGE

#include "ob_ddl_server_client.h"
#include "rootserver/ob_rs_serial_call.h"
#include "share/ob_ddl_sim_point.h"
#include "sql/engine/cmd/ob_ddl_executor_util.h"
#include "observer/ob_server_event_history_table_operator.h"
#include "rootserver/ddl_task/ob_table_redefinition_task.h" // for ObTableRedefinitionTask

namespace oceanbase
{
namespace storage
{


int ObDDLServerClient::create_hidden_table(
    const obcall::ObCreateHiddenTableArg &arg, 
    obcall::ObCreateHiddenTableRes &res, 
    int64_t &snapshot_version,
    uint64_t &data_format_version,
    sql::ObSQLSessionInfo &session)
{
  int ret = OB_SUCCESS;
  ObAddr rs_leader_addr = GCTX.self_addr();
  const int64_t retry_interval = 100 * 1000L;
  if (OB_UNLIKELY(!arg.is_valid())) {
    ret = OB_INVALID_ARGUMENT;
    LOG_WARN("invalid arg", K(ret), K(arg));
  }

  while (OB_SUCC(ret)) {
    if (OB_FAIL(rootserver::serial_call([&]{ return GCTX.root_service_->create_hidden_table(arg, res); }))) {
    } else {
      break;
    }
    if (OB_FAIL(ret) && is_ddl_stmt_packet_retry_err(ret)) {
      ob_usleep(retry_interval);
      if (OB_FAIL(THIS_WORKER.check_status())) {
      }
    }
  }

  if (OB_FAIL(ret)) {
  } else if (OB_FAIL(OB_DDL_HEART_BEAT_TASK_CONTAINER.set_register_task_id(res.task_id_))) {
  } 
  if (OB_SUCC(ret)) {
    if (OB_FAIL(wait_task_reach_pending(res.task_id_, snapshot_version, data_format_version, *GCTX.sql_proxy_, res.is_no_logging_))) {
    }
#ifdef ERRSIM
    if (OB_SUCC(ret)) {
      ret = OB_E(common::EventTable::EN_DDL_DIRECT_LOAD_WAIT_TABLE_LOCK_FAIL) OB_SUCCESS;
      LOG_INFO("wait table lock failed errsim", K(ret));
    }
#endif
    if (OB_FAIL(ret)) {
      int tmp_ret = OB_SUCCESS;
      obcall::ObAbortRedefTableArg abort_redef_table_arg;
      abort_redef_table_arg.task_id_ = res.task_id_;
      
      
      if (OB_TMP_FAIL(abort_redef_table(abort_redef_table_arg, &session))) {
      }
      // abort_redef_table() function last step must remove heart_beat task, so there is no need to call heart_beat_clear()
    }
  }
  SERVER_EVENT_ADD("ddl", "create hidden table",
    "ret", ret,
    "trace_id", *ObCurTraceId::get_trace_id(),
    "task_id", res.task_id_,
    "table_id", res.table_id_,
    "schema_version", res.schema_version_);
  LOG_INFO("finish create hidden table.", K(ret), "ddl_event_info", ObDDLEventInfo(), K(arg), K(res));
  return ret;
}


int ObDDLServerClient::copy_table_dependents(
    const obcall::ObCopyTableDependentsArg &arg, 
    sql::ObSQLSessionInfo &session)
{
  int ret = OB_SUCCESS;
  
  const int64_t retry_interval = 100 * 1000L;
  ObAddr rs_leader_addr = GCTX.self_addr();
  if (OB_UNLIKELY(!arg.is_valid())) {
    ret = OB_INVALID_ARGUMENT;
    LOG_WARN("invalid arg", K(ret), K(arg));
    while (OB_SUCC(ret)) {
      int tmp_ret = OB_SUCCESS;
      if (OB_FAIL(check_need_stop())) {
      } else if (OB_FAIL(ObDDLExecutorUtil::handle_session_exception(session))) {
        LOG_WARN("fail to handle session exception", K(ret));
        if (OB_TMP_FAIL(ObDDLExecutorUtil::cancel_ddl_task())) {
        }
      } else if (OB_FAIL(GCTX.root_service_->copy_table_dependents(arg))) {
        LOG_WARN("copy table dependents failed", K(ret), K(arg));
        if (OB_ENTRY_NOT_EXIST == ret) {
          LOG_WARN("ddl task not exist", K(ret), K(arg));
          break;
        } else if (OB_NOT_SUPPORTED == ret) {
          LOG_WARN("not supported copy table dependents", K(ret), K(arg));
          break;
        } else {
          LOG_INFO("ddl task exist, try again", K(arg));
          ret = OB_SUCCESS;
          ob_usleep(retry_interval);
        }
      } else {
        LOG_INFO("copy table dependents success", K(arg));
        break;
      }
    }
  }

  SERVER_EVENT_ADD("ddl", "copy table dependents",
    "ret", ret,
    "trace_id", *ObCurTraceId::get_trace_id(),
    "task_id", arg.task_id_,
    "rpc_dst", rs_leader_addr);
  LOG_INFO("finish copy table dependents.", K(ret), "ddl_event_info", ObDDLEventInfo(), K(arg), K(rs_leader_addr));
  return ret;
}

int ObDDLServerClient::abort_redef_table(const obcall::ObAbortRedefTableArg &arg, sql::ObSQLSessionInfo *session)
{
  int ret = OB_SUCCESS;
  
  const int64_t retry_interval = 100 * 1000L;
  ObAddr rs_leader_addr = GCTX.self_addr();
  if (OB_UNLIKELY(!arg.is_valid())) {
    ret = OB_INVALID_ARGUMENT;
    LOG_WARN("invalid arg", K(ret), K(arg));
  } else {
    while (OB_SUCC(ret)) {
      int tmp_ret = OB_SUCCESS;
      if (OB_FAIL(check_need_stop())) {
      } else if (OB_FAIL(GCTX.root_service_->abort_redef_table(arg))) {
        LOG_WARN("abort redef table failed", K(ret), K(arg));
        if (OB_ENTRY_NOT_EXIST == ret) {
          break;
        } else if (OB_NOT_SUPPORTED == ret) {
          LOG_WARN("not supported abort direct load task", K(ret), K(arg));
          break;
        } else if (OB_ALLOCATE_MEMORY_FAILED == ret) {
          LOG_WARN("no enough memory to abort", K(ret), K(arg));
          break;
        } else if (OB_SIZE_OVERFLOW == ret) {
          LOG_WARN("no enough queue size to abort", K(ret), K(arg), K(rs_leader_addr));
          break;
        } else {
          LOG_INFO("ddl task exist, try again", K(arg));
          ret = OB_SUCCESS;
          ob_usleep(retry_interval);
        }
      } else {
        LOG_INFO("abort task success");
        break;
      }
    }
    if (OB_ENTRY_NOT_EXIST == ret) {
      ret = OB_SUCCESS;
    }
    if (OB_SUCC(ret)) {
      const int64_t origin_timeout_ts = THIS_WORKER.get_timeout_ts();
      int64_t MAX_ABORT_WAIT_TIMEOUT = 60 * 1000 * 1000; //60s
      THIS_WORKER.set_timeout_ts(ObTimeUtility::current_time() + MAX_ABORT_WAIT_TIMEOUT);
      if (OB_FAIL(sql::ObDDLExecutorUtil::wait_ddl_finish(arg.task_id_, DDL_DIRECT_LOAD, session))) {
        if (OB_CANCELED == ret) {
          ret = OB_SUCCESS;
          LOG_INFO("ddl abort success", K_(arg.task_id));
        } else {
          LOG_WARN("wait ddl finish failed", K(ret), K(arg.task_id_));
        }
      }
      THIS_WORKER.set_timeout_ts(origin_timeout_ts);
    }
    int tmp_ret = OB_SUCCESS;
    if (OB_TMP_FAIL(heart_beat_clear(arg.task_id_))) {
    }
  }

  SERVER_EVENT_ADD("ddl", "abort redef table",
    "ret", ret,
    "trace_id", *ObCurTraceId::get_trace_id(),
    "task_id", arg.task_id_,
    "rpc_dst", rs_leader_addr);
  LOG_INFO("abort redef table.", K(ret), "ddl_event_info", ObDDLEventInfo(), K(arg), K(rs_leader_addr));
  return ret;
}

int ObDDLServerClient::finish_redef_table(const obcall::ObFinishRedefTableArg &finish_redef_arg,
                                          const obcall::ObDDLBuildSingleReplicaResponseArg &build_single_arg,
                                          sql::ObSQLSessionInfo &session)
{
  int ret = OB_SUCCESS;
  int tmp_ret = OB_SUCCESS;
  
  const int64_t retry_interval = 100 * 1000L;
  ObAddr rs_leader_addr = GCTX.self_addr();
  if (OB_UNLIKELY(!finish_redef_arg.is_valid() || !build_single_arg.is_valid())) {
    ret = OB_INVALID_ARGUMENT;
    LOG_WARN("invalid arg", K(ret), K(finish_redef_arg), K(build_single_arg));
  } else {
    while (OB_SUCC(ret)) {
      int tmp_ret = OB_SUCCESS;
      if (OB_FAIL(check_need_stop())) {
      } else if (OB_FAIL(ObDDLExecutorUtil::handle_session_exception(session))) {
        LOG_WARN("session execption happened", K(ret));
        if (OB_TMP_FAIL(ObDDLExecutorUtil::cancel_ddl_task())) {
          LOG_WARN("cancel ddl task failed", K(tmp_ret));
          ret = OB_SUCCESS;
        }
      } else if (OB_FAIL(GCTX.root_service_->finish_redef_table(finish_redef_arg))) {
        LOG_WARN("finish redef table failed", K(ret), K(finish_redef_arg));
        if (OB_ENTRY_NOT_EXIST == ret) {
          break;
        } else if (OB_NOT_SUPPORTED == ret) {
          LOG_WARN("not supported finish redef table", K(ret), K(finish_redef_arg));
          break;
        } else {
          LOG_INFO("ddl task exist, try again", K(finish_redef_arg));
          ret = OB_SUCCESS;
          ob_usleep(retry_interval);
        }
      } else {
        LOG_INFO("finish redef table success", K(finish_redef_arg));
        break;
      }
    }
    if (OB_FAIL(ret)) {
    } else if (OB_FAIL(build_ddl_single_replica_response(build_single_arg))) {
    } else if (OB_FAIL(sql::ObDDLExecutorUtil::wait_ddl_finish(finish_redef_arg.task_id_, DDL_DIRECT_LOAD, &session))) {
    }
    if (OB_TMP_FAIL(heart_beat_clear(finish_redef_arg.task_id_))) {
    }
  }

  SERVER_EVENT_ADD("ddl", "finish redef table",
    "ret", ret,
    "trace_id", *ObCurTraceId::get_trace_id(),
    "task_id", finish_redef_arg.task_id_,
    "snapshot_version", build_single_arg.snapshot_version_,
    "rpc_dst", rs_leader_addr,
    "info", build_single_arg.ls_id_);
  LOG_INFO("finish redef table.", K(ret), "ddl_event_info", ObDDLEventInfo(), K(finish_redef_arg), K(build_single_arg), K(rs_leader_addr));
  return ret;
}

int ObDDLServerClient::build_ddl_single_replica_response(const obcall::ObDDLBuildSingleReplicaResponseArg &arg)
{
  int ret = OB_SUCCESS;
  if (OB_UNLIKELY(!arg.is_valid())) {
    ret = OB_INVALID_ARGUMENT;
    LOG_WARN("invalid arg", K(ret), K(arg));
  } else if (OB_ISNULL(GCTX.root_service_)) {
    ret = OB_ERR_UNEXPECTED;
    LOG_WARN("root service is null", K(ret));
  } else if (OB_FAIL(GCTX.root_service_->build_ddl_single_replica_response(arg))) {
  }
  return ret;
}

int ObDDLServerClient::wait_task_reach_pending(const int64_t task_id, 
    int64_t &snapshot_version, 
    uint64_t &data_format_version,
    ObMySQLProxy &sql_proxy,
    bool &is_no_logging)
{
  int ret = OB_SUCCESS;
  ObSqlString sql_string;
  snapshot_version = 0;
  data_format_version = 0;
  const int64_t retry_interval = 100 * 1000;
  THIS_WORKER.set_timeout_ts(ObTimeUtility::current_time() + OB_MAX_USER_SPECIFIED_TIMEOUT);
  SMART_VAR(ObMySQLProxy::MySQLResult, res) {
    sqlclient::ObMySQLResult *result = NULL;
    if (OB_UNLIKELY(task_id <= 0)) {
      ret = OB_INVALID_ARGUMENT;
      LOG_WARN("invalid argument", K(ret), K(task_id));
    } else if (OB_FAIL(DDL_SIM(task_id, WAIT_REDEF_TASK_REACH_PENDING_FAILED))) {
    } else {
      while (OB_SUCC(ret)) {
        uint64_t unused_target_object_id = 0;
        int64_t unused_schema_version = 0;
        share::ObDDLTaskStatus task_status = share::ObDDLTaskStatus::PREPARE;
        bool unused_is_offline_index_rebuild = false;
        if (OB_FAIL(ObDDLUtil::get_data_information(task_id, data_format_version,
            snapshot_version, task_status, unused_target_object_id, unused_schema_version, is_no_logging, unused_is_offline_index_rebuild))) {
          if (OB_LIKELY(OB_ITER_END == ret)) {
            ret = OB_ENTRY_NOT_EXIST;
            ObAddr unused_addr;
            int64_t forward_user_msg_len = 0;
            ObDDLErrorMessageTableOperator::ObBuildDDLErrorMessage error_message;
            if (OB_SUCCESS == ObDDLErrorMessageTableOperator::get_ddl_error_message(task_id, -1 /*target_object_id*/, 
                              unused_addr, false/*is_ddl_retry_task*/, 
                              *GCTX.sql_proxy_, error_message, forward_user_msg_len)) {
              if (OB_SUCCESS != error_message.ret_code_) {
                ret = error_message.ret_code_;
              }
            }
            LOG_WARN("ddl task execute end", K(ret));
          } else {
            LOG_WARN("get information failed", K(ret), K(task_id));
          }
        } else if (rootserver::ObTableRedefinitionTask::check_task_status_is_pending(task_status)) {
          break;
        }
      }
    }
  }
  return ret;
}

int ObDDLServerClient::heart_beat_clear(const int64_t task_id)
{
  int ret = OB_SUCCESS;
  if (task_id <= 0) {
    ret = OB_INVALID_ARGUMENT;
    LOG_WARN("invalid argument", K(ret), K(task_id));
  } else if (OB_FAIL(OB_DDL_HEART_BEAT_TASK_CONTAINER.remove_register_task_id(task_id))) {
  }
  return ret;
}

int ObDDLServerClient::check_need_stop()
{
  // form-6 (other-failure) collapse: single-sys-tenant never dropped / never standby, so the
  // check_tenant_status_normal -> DROPPED/STANDBY error model is dead. Only server-stop remains.
  int ret = OB_SUCCESS;
  if (observer::ObServer::get_instance().is_stopped()) {
    ret = OB_TIMEOUT;
    LOG_WARN("server is stopping", K(ret));
  }
  return ret;
}


}  // end of namespace storage
}  // end of namespace oceanbase
