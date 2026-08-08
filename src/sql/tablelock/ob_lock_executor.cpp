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

#define USING_LOG_PREFIX TABLELOCK
#include "sql/tablelock/ob_lock_executor.h"
#include "query/session/ob_inner_sql_connection_access.h"
#include "query/session/ob_session_inner_sql.h"
#include "query/tablelock/ob_table_lock_runtime.h"
#include "data_plane/transaction/ob_deadlock.h"
#include "share/ob_dml_sql_splicer.h"

#include "common/mysqlclient/ob_mysql_proxy.h"
#include "common/mysqlclient/ob_mysql_result.h"
#include "lib/utility/ob_fast_convert.h"
#include "lib/utility/alloc_assist.h"
#include "share/ob_table_access_helper.h"
#include "sql/engine/ob_exec_context.h"
#include "sql/ob_sql_trans_control.h"
#include "sql/session/ob_sql_session_info.h"
#include "sql/session/ob_inner_sql_connection.h"

namespace oceanbase
{
using namespace sql;
using namespace transaction;
using namespace common;
using namespace observer;

namespace
{

} // namespace

namespace transaction
{

namespace tablelock
{

int ObLockContext::init(ObExecContext &ctx,
                        const int64_t timeout_us)
{
  int ret = OB_SUCCESS;
  ObSQLSessionInfo *session_info = nullptr;

  if (OB_ISNULL(session_info = ctx.get_my_session())) {
    ret = OB_INVALID_ARGUMENT;
    LOG_WARN("session_info is null in ObExecContext", K(ret));
  } else {
    // use smaller timeout if we specified the lock timeout us.
    if (timeout_us > 0
        && (ObTimeUtility::current_time() + timeout_us) < THIS_WORKER.get_timeout_ts()) {
      OX (old_worker_timeout_ts_ = THIS_WORKER.get_timeout_ts());
      OX (THIS_WORKER.set_timeout_ts(ObTimeUtility::current_time() + timeout_us));
      if (OB_SUCC(ret) && OB_NOT_NULL(ctx.get_physical_plan_ctx())) {
        old_phy_plan_timeout_ts_ = ctx.get_physical_plan_ctx()->get_timeout_timestamp();
        ctx.get_physical_plan_ctx()
          ->set_timeout_timestamp(ObTimeUtility::current_time() + timeout_us);
      }
    }
    if (OB_SUCC(ret)) {
      if (session_info->get_local_autocommit()) {
        OX (reset_autocommit_ = true);
        OZ (session_info->set_autocommit(false));
      }
      has_inner_dml_write_ = session_info->has_exec_inner_dml();
      last_insert_id_ = session_info->get_local_last_insert_id();
      session_info->set_has_exec_inner_dml(false);

      ObTransID parent_tx_id;
      parent_tx_id = session_info->get_tx_id();
      OZ (session_info->begin_inner_tx_session(saved_session_));
      OX (have_saved_session_ = true);
      OZ (ObSqlTransControl::explicit_start_trans(ctx, false));
      if (OB_SUCC(ret)) {
        has_inner_tx_ = true;
      }
      if (OB_SUCC(ret) && parent_tx_id.is_valid()) {
        (void) register_for_deadlock_(*session_info, parent_tx_id);
      }
    }
    OX (my_exec_ctx_ = &ctx);
    OZ (open_inner_conn_());
  }
  return ret;
}

int ObLockContext::destroy(ObExecContext &ctx,
                           bool is_rollback)
{
  int tmp_ret = OB_SUCCESS;
  int ret = OB_SUCCESS;
  ObSQLSessionInfo *session_info = nullptr;

  if (OB_ISNULL(session_info = ctx.get_my_session())) {
    ret = OB_INVALID_ARGUMENT;
    LOG_WARN("session_info is null in ObExecContext", K(ret));
  } else {
    if (has_inner_tx_) {
      if (OB_TMP_FAIL(implicit_end_trans_(*session_info, ctx, is_rollback))) {
        LOG_ERROR("failed to rollback trans", K(tmp_ret));
        ret = COVER_SUCC(tmp_ret);
      }
    }
    if (OB_TMP_FAIL(close_inner_conn_())) {
      LOG_WARN("close inner connection failed", K(tmp_ret));
      ret = COVER_SUCC(tmp_ret);
    }
    if (have_saved_session_) {
      if (OB_TMP_FAIL(session_info->end_inner_tx_session(saved_session_))) {
        LOG_ERROR("failed to switch trans", K(tmp_ret));
        ret = COVER_SUCC(tmp_ret);
      }
    }

    // WHY WE NEED THIS
    uint64_t cur_last_insert_id = session_info->get_local_last_insert_id();
    if (cur_last_insert_id != last_insert_id_) {
      ObObj last_insert_id;
      last_insert_id.set_uint64(last_insert_id_);
      tmp_ret = session_info->update_sys_variable(share::SYS_VAR_LAST_INSERT_ID, last_insert_id);
      if (OB_SUCCESS == tmp_ret &&
          OB_TMP_FAIL(session_info->update_sys_variable(share::SYS_VAR_IDENTITY, last_insert_id))) {
        LOG_WARN("succ update last_insert_id, but fail to update identity", K(tmp_ret));
      }
      ret = COVER_SUCC(tmp_ret);
    }
    session_info->set_has_exec_inner_dml(has_inner_dml_write_);
    if (old_worker_timeout_ts_ != 0) {
      THIS_WORKER.set_timeout_ts(old_worker_timeout_ts_);
      if (OB_NOT_NULL(ctx.get_physical_plan_ctx())) {
        ctx.get_physical_plan_ctx()->set_timeout_timestamp(old_phy_plan_timeout_ts_);
      }
    }
    if (reset_autocommit_) {
      if (OB_TMP_FAIL(session_info->set_autocommit(true))) {
        ret = COVER_SUCC(tmp_ret);
        LOG_ERROR("restore autocommit value failed", K(tmp_ret), K(ret));
      }
    }
  }
  return ret;
}

int ObLockContext::implicit_end_trans_(ObSQLSessionInfo &session_info,
                                       ObExecContext &ctx,
                                       bool is_rollback,
                                       bool can_async)
{
  int ret = OB_SUCCESS;
  bool is_async = false;
  if (session_info.is_in_transaction()) {
    is_async = !is_rollback && ctx.is_end_trans_async() && can_async;
    if (!is_async) {
      if (OB_FAIL(ObSqlTransControl::implicit_end_trans(ctx, is_rollback))) {
        LOG_WARN("failed to implicit end trans with sync callback", K(ret));
      }
    } else {
      ObEndTransAsyncCallback &callback = session_info.get_end_trans_cb();
      if (OB_FAIL(ObSqlTransControl::implicit_end_trans(ctx, is_rollback, &callback))) {
        LOG_WARN("failed implicit end trans with async callback", K(ret));
      }
      ctx.get_trans_state().set_end_trans_executed(OB_SUCCESS == ret);
    }
  } else {
    ObSqlTransControl::reset_session_tx_state(&session_info, true);
    ctx.set_need_disconnect(false);
  }
  LOG_TRACE("lock function implicit_end_trans", K(is_async), K(session_info),
            K(can_async), K(is_rollback));
  return ret;
}

int ObLockContext::valid_execute_context(ObExecContext &ctx)
{
  int ret = OB_SUCCESS;
  CK (OB_NOT_NULL(ctx.get_sql_ctx()));
  CK (OB_NOT_NULL(ctx.get_my_session()));
  CK (OB_NOT_NULL(ctx.get_sql_ctx()->schema_guard_));
  return ret;
}

void ObLockContext::register_for_deadlock_(ObSQLSessionInfo &session_info,
                                           const ObTransID &parent_tx_id)
{
  int ret = OB_SUCCESS;
  int64_t query_timeout = 0;
  ObTransID child_tx_id = session_info.get_tx_id();

  if (parent_tx_id != child_tx_id &&
      parent_tx_id.is_valid() &&
      child_tx_id.is_valid()) {
    if (OB_FAIL(session_info.get_query_timeout(query_timeout))) {
      LOG_WARN("get query timeout failed", K(parent_tx_id), K(child_tx_id), KR(ret));
    } else {
      if (OB_FAIL(data_plane::register_autonomous_transaction_dependency(
              parent_tx_id, child_tx_id, query_timeout))) {
        LOG_WARN("autonomous register to deadlock failed", K(parent_tx_id),
                 K(child_tx_id), KR(ret));
      }
    }
  } else {
    ret = OB_ERR_UNEXPECTED;
    LOG_ERROR("not register to deadlock", K(ret), K(parent_tx_id), K(child_tx_id));
  }
}

int ObLockContext::open_inner_conn_()
{
  int ret = OB_SUCCESS;
  ObSQLSessionInfo *session = nullptr;
  common::sqlclient::ObISQLConnection *inner_conn = nullptr;

  if (OB_ISNULL(my_exec_ctx_)) {
    ret = OB_INVALID_ARGUMENT;
    LOG_WARN("ObExecContext in ObLockFuncContext is null", K(ret));
  } else if (OB_ISNULL(session = my_exec_ctx_->get_my_session())) {
    ret = OB_NOT_INIT;
    LOG_WARN("session in ObExecContext is NULL", K(ret), KP(session));
  } else if (OB_NOT_NULL(inner_conn_) || OB_NOT_NULL(store_inner_conn_)) {
    ret = OB_ERR_UNEXPECTED;
    LOG_WARN("inner_conn_ or store_inner_conn_ should be null", K(ret), KP(inner_conn_), KP(store_inner_conn_));
  } else if (FALSE_IT(store_inner_conn_ = session->get_inner_conn())) {
  } else if (FALSE_IT(session->set_inner_conn(nullptr))) {
  } else if (OB_FAIL(
                 query::ObInnerSQLConnectionAccess::
                     create_connection_with_external_session(
                         session, inner_conn_guard_))) {
    LOG_WARN("create inner connection failed", K(ret), KPC(session));
  } else if (OB_ISNULL(inner_conn = inner_conn_guard_.get_ptr())) {
    ret = OB_ERR_UNEXPECTED;
    LOG_WARN("inner connection is still null", KPC(session));
  } else {
    /**
     * session is the only data struct which can pass through multi layer nested sql,
     * so we put inner conn in session to share it within multi layer nested sql.
     */
    inner_conn_ = inner_conn;
    session->set_inner_conn(inner_conn);
    LOG_DEBUG("ObLockFuncContext::open_inner_conn_ successfully",
              KP(inner_conn_),
              KP(store_inner_conn_));
  }
  return ret;
}

int ObLockContext::close_inner_conn_()
{
  int ret = OB_SUCCESS;
  ObSQLSessionInfo *session = nullptr;

  if (OB_ISNULL(my_exec_ctx_)) {
    ret = OB_INVALID_ARGUMENT;
    LOG_WARN("ObExecContext in ObLockFuncContext is null", K(ret));
  } else {
    if (OB_ISNULL(inner_conn_)) {
      ret = OB_NOT_INIT;
      LOG_WARN("inner_conn of session is NULL", K(ret), KP(session), KP(inner_conn_));
    }
    if (OB_ISNULL(session = my_exec_ctx_->get_my_session())) {
      ret = OB_NOT_INIT;
      LOG_WARN("session is NULL", K(ret), KP(session));
    } else if (OB_NOT_NULL(inner_conn_) || OB_NOT_NULL(store_inner_conn_)) {
      // 1. if inner_conn_ is not null, means that we have created inner_conn successfully before, so we must have already
      // set store_inner_conn_ successfully, just restore it to the session.
      // 2. if inner_conn_ is null, it's uncertain whether store_inner_conn_ has been set before. If store_inner_conn_
      // is not null, it must have been set. Otherwise, the inner_conn on the session may be null, or it may have existed
      // with an error code before store_inner_conn_ being set. At this case, we do not set inner_conn on the session.
      session->set_inner_conn(store_inner_conn_);
    }
  }
  inner_conn_ = nullptr;
  store_inner_conn_ = nullptr;
  inner_conn_guard_.reset();
  return ret;
}

int ObLockContext::execute_write(const ObSqlString &sql,
                                 int64_t &affected_rows)
{
  int ret = OB_SUCCESS;
  affected_rows = 0;

  if (OB_ISNULL(inner_conn_)) {
    ret = OB_NOT_INIT;
    LOG_WARN("inner connection is NULL", K(ret));
  } else if (OB_FAIL(inner_conn_->execute_write(sql.ptr(), affected_rows))) {
    LOG_WARN("execute write sql failed", K(ret));
  }
  return ret;
}

int ObLockContext::execute_read(const ObSqlString &sql,
                                ObMySQLProxy::MySQLResult &res)
{
  int ret = OB_SUCCESS;

  if (OB_ISNULL(inner_conn_)) {
    ret = OB_NOT_INIT;
    LOG_WARN("inner connection is NULL", K(ret));
  } else if (OB_FAIL(inner_conn_->execute_read(sql.ptr(), res))) {
    LOG_WARN("execute read sql failed", K(ret));
  }
  return ret;
}

int ObLockExecutor::clear_lock_session_if_no_lock_(ObLockContext &ctx,
                                                   const uint32_t session_id,
                                                   const uint64_t session_create_ts)
{
  int ret = OB_SUCCESS;
  bool owner_exist = false;
  ObSQLSessionInfo *session = nullptr;

  OV (OB_NOT_NULL(ctx.my_exec_ctx_), OB_INVALID_ARGUMENT);
  OV (OB_NOT_NULL(session = ctx.my_exec_ctx_->get_my_session()), OB_INVALID_ARGUMENT);
  query::ObSessionInnerSql session_io(session);
  const data_plane::ObSessionLockOwner owner(session_id, session_create_ts);
  OZ (data_plane::session_has_locks(session_io, owner, owner_exist));
  if (OB_SUCC(ret) && !owner_exist) {
    OX (mark_lock_session_(session, false));
  }
  return ret;
}

int ObLockExecutor::query_lock_id_(ObISQLClient &sql_client,
                                   const ObString &lock_name,
                                   uint64_t &lock_id)
{
  int ret = OB_SUCCESS;
  
  ObStringHolder query_lock_handle;
  // 1. check if there's a lock with the same lock name
  char where_cond[WHERE_CONDITION_BUFFER_SIZE] = {0};
  char table_name[MAX_FULL_TABLE_NAME_LENGTH] = {0};
  // generate corresponding lock handle for the lock name,
  // and insert them into the inner table DBMS_LOCK_ALLOCATED

  lock_id = OB_INVALID_OBJECT_ID;

  ObCStringHelper helper;
  OZ (databuff_printf(where_cond, WHERE_CONDITION_BUFFER_SIZE,
                      "WHERE name = '%s'", helper.convert(lock_name)));
  OZ (databuff_printf(table_name, MAX_FULL_TABLE_NAME_LENGTH,
                      "%s.%s", OB_SYS_DATABASE_NAME, share::OB_ALL_DBMS_LOCK_ALLOCATED_TNAME));
  OZ (ObTableAccessHelper::read_single_row(
                                           sql_client,
                                           { "lockhandle" },
                                           table_name,
                                           where_cond,
                                           query_lock_handle));
  if (OB_EMPTY_RESULT == ret) {
    // there is no lock name.
  } else if (OB_SUCC(ret)) {
    OZ (extract_lock_id_(query_lock_handle.get_ob_string(), lock_id));
  }
  return ret;
}

int ObLockExecutor::query_lock_id_and_lock_handle_(ObISQLClient &sql_client,
                                                   const ObString &lock_name,
                                                   uint64_t &lock_id,
                                                   char *lock_handle_buf)
{
  int ret = OB_SUCCESS;
  
  ObStringHolder query_lock_handle;
  // 1. check if there's a lock with the same lock name
  char where_cond[WHERE_CONDITION_BUFFER_SIZE] = {0};
  char table_name[MAX_FULL_TABLE_NAME_LENGTH] = {0};
  int64_t lock_handle_len = 0;
  // generate corresponding lock handle for the lock name,
  // and insert them into the inner table DBMS_LOCK_ALLOCATED
  ObCStringHelper helper;
  OZ (databuff_printf(where_cond, WHERE_CONDITION_BUFFER_SIZE,
                      "WHERE name = '%s'", helper.convert(lock_name)));
  OZ (databuff_printf(table_name, MAX_FULL_TABLE_NAME_LENGTH,
                      "%s.%s", OB_SYS_DATABASE_NAME, share::OB_ALL_DBMS_LOCK_ALLOCATED_TNAME));
  OZ (ObTableAccessHelper::read_single_row(
                                           sql_client,
                                           { "lockhandle" },
                                           table_name,
                                           where_cond,
                                           query_lock_handle));
  if (OB_EMPTY_RESULT == ret) {
    // there is no lock name.
  } else if (OB_SUCC(ret)) {
    ObString lock_handle_string = query_lock_handle.get_ob_string();
    OZ (extract_lock_id_(lock_handle_string, lock_id));
    OV (lock_handle_string.length() < MAX_LOCK_HANDLE_LEGNTH, OB_INVALID_ARGUMENT, lock_handle_string);
    OX (MEMCPY(lock_handle_buf, lock_handle_string.ptr(), lock_handle_string.length()));
    lock_handle_buf[lock_handle_string.length()] = '\0';
  }
  return ret;
}

int ObLockExecutor::extract_lock_id_(const ObString &lock_handle,
                                     uint64_t &lock_id)
{
  int ret = OB_SUCCESS;

  OV (lock_handle.is_numeric(), OB_INVALID_ARGUMENT, lock_handle);
  OV (lock_handle.length() >= LOCK_ID_LENGTH, OB_INVALID_ARGUMENT, lock_handle, lock_handle.length());
  OX (lock_id = ObFastAtoi<uint64_t>::atoi_positive_unchecked(lock_handle.ptr(), lock_handle.ptr() + LOCK_ID_LENGTH));
  OV (lock_id >= MIN_LOCK_HANDLE_ID && lock_id <= MAX_LOCK_HANDLE_ID, OB_INVALID_ARGUMENT, lock_id);

  return ret;
}

void ObLockExecutor::mark_lock_session_(sql::ObSQLSessionInfo *session,
                                        const bool is_lock_session)
{
  if (session->is_lock_session() != is_lock_session) {
    LOG_INFO("mark lock_session", K(session->get_server_sid()), K(is_lock_session));
    session->set_is_lock_session(is_lock_session);
  } else {
    LOG_DEBUG("the lock_session status on the session won't be changed, no need to mark again",
              K(session->get_server_sid()),
              K(session->is_lock_session()));
  }
}

int ObUnLockExecutor::execute(ObExecContext &ctx,
                              const ReleaseType release_type,
                              int64_t &release_cnt)
{
  int ret = OB_SUCCESS;
  int tmp_ret = OB_SUCCESS;
  uint32_t session_id = 0;
  uint64_t session_create_ts = 0;
  bool is_rollback = false;
  OZ (ObLockContext::valid_execute_context(ctx));
  OX (session_id = ctx.get_my_session()->get_server_sid());
  OX (session_create_ts = ctx.get_my_session()->get_sess_create_time());
  OZ (execute_(ctx,
               session_id,
               session_create_ts,
               release_type,
               release_cnt));
  return ret;
}

int ObUnLockExecutor::execute(uint8_t owner_type, int64_t owner_id)
{
  int ret = OB_SUCCESS;
  int64_t release_cnt = 0;
  const data_plane::ObPersistedLockOwner owner(owner_type, owner_id);
  ObArenaAllocator allocator(ObModIds::OB_SQL_EXPR);
  SMART_VAR(sql::ObSQLSessionInfo, session) {
    SMART_VAR(sql::ObExecContext, exec_ctx, allocator) {
      ObSqlCtx sql_ctx;
      
      ObSchemaGetterGuard guard;
      const ObServerRuntimeSchema *runtime_schema = nullptr;
      LinkExecCtxGuard link_guard(session, exec_ctx);
      sql::ObPhysicalPlanCtx phy_plan_ctx(allocator);
      OZ (session.init(0 /*default session id*/, &allocator));
      OX (session.set_inner_session());
      OZ (GCTX.schema_service_->get_runtime_schema_guard(guard));
      OZ (guard.get_server_runtime_info(runtime_schema));
      OZ (session.init_runtime(runtime_schema->get_runtime_name_str()));
      OZ (session.load_all_sys_vars(guard));
      OZ (session.load_default_configs_in_pc());
      OX (sql_ctx.schema_guard_ = &guard);
      OX (exec_ctx.set_my_session(&session));
      OX (exec_ctx.set_sql_ctx(&sql_ctx));
      OX (exec_ctx.set_physical_plan_ctx(&phy_plan_ctx));

      OZ (ObLockContext::valid_execute_context(exec_ctx));
      OZ (execute_(exec_ctx, owner, release_cnt));
      OX (exec_ctx.set_physical_plan_ctx(nullptr));  // avoid core during release exec_ctx
    }
  }
  LOG_DEBUG("lock_executor debug: release by owner identity", K(ret),
            K(owner_type), K(owner_id), K(release_cnt));
  return ret;
}

int ObUnLockExecutor::execute_(ObExecContext &ctx,
                               const uint32_t session_id,
                               const uint64_t session_create_ts,
                               const ReleaseType release_type,
                               int64_t &release_cnt)
{
  int ret = OB_SUCCESS;
  int tmp_ret = OB_SUCCESS;
  bool is_rollback = false;
  release_cnt = INVALID_RELEASE_CNT;  // means not release successfully
  OZ (ObLockContext::valid_execute_context(ctx));
  if (OB_SUCC(ret)) {
    SMART_VAR(ObLockContext, stack_ctx) {
      OZ (stack_ctx.init(ctx));
      if (OB_SUCC(ret)) {
        ObSQLSessionInfo *session = GET_MY_SESSION(ctx);
        ObTxParam tx_param;
        query::ObSessionInnerSql session_io(session);
        const data_plane::ObSessionLockOwner owner(
            session_id, session_create_ts);
        OZ (ObSqlTransControl::build_tx_param(session, tx_param));
        CK (OB_NOT_NULL(session->get_tx_desc()));
        OZ (data_plane::release_session_locks(
                session_io,
                *session->get_tx_desc(),
                tx_param,
                owner,
                to_scope_(release_type),
                release_cnt));
        OZ (clear_lock_session_if_no_lock_(stack_ctx, session_id,
                                           session_create_ts));
      }
      is_rollback = (OB_SUCCESS != ret);
      if (OB_TMP_FAIL(stack_ctx.destroy(ctx, is_rollback))) {
        LOG_WARN("stack ctx destroy failed", K(tmp_ret));
        COVER_SUCC(tmp_ret);
      }
    }
  }
  // if release_cnt is valid, means we have tried to release,
  // and have not encountered any failures before
  if (INVALID_RELEASE_CNT != release_cnt) {
    ret = OB_SUCCESS;
  }
  return ret;
}

int ObUnLockExecutor::execute_(ObExecContext &ctx,
                               const data_plane::ObPersistedLockOwner &owner,
                               int64_t &release_cnt)
{
  int ret = OB_SUCCESS;
  int tmp_ret = OB_SUCCESS;
  bool is_rollback = false;

  OZ (ObLockContext::valid_execute_context(ctx));
  if (OB_SUCC(ret)) {
    SMART_VAR(ObLockContext, stack_ctx) {
      OZ (stack_ctx.init(ctx));
      if (OB_SUCC(ret)) {
        ObSQLSessionInfo *session = GET_MY_SESSION(ctx);
        ObTxParam tx_param;
        query::ObSessionInnerSql session_io(session);
        OZ (ObSqlTransControl::build_tx_param(session, tx_param));
        CK (OB_NOT_NULL(session->get_tx_desc()));
        OZ (data_plane::release_persisted_locks(
                session_io,
                *session->get_tx_desc(),
                tx_param,
                owner,
                data_plane::ObSessionLockScope::ALL_LOCKS,
                release_cnt));
      }
      is_rollback = (OB_SUCCESS != ret);
      if (OB_TMP_FAIL(stack_ctx.destroy(ctx, is_rollback))) {
        LOG_WARN("stack ctx destroy failed", K(tmp_ret));
        COVER_SUCC(tmp_ret);
      }
    }
  }
  return ret;
}

data_plane::ObSessionLockScope ObUnLockExecutor::to_scope_(ReleaseType release_type)
{
  data_plane::ObSessionLockScope scope = data_plane::ObSessionLockScope::ALL_LOCKS;
  switch (release_type) {
  case RELEASE_OBJ_LOCK: {
    scope = data_plane::ObSessionLockScope::NAMED_LOCK;
    break;
  }
  case RELEASE_TABLE_LOCK: {
    scope = data_plane::ObSessionLockScope::TABLE_LOCK;
    break;
  }
  default: {
    scope = data_plane::ObSessionLockScope::ALL_LOCKS;
  }
  }
  return scope;
}

} // tablelock
} // transaction

namespace query
{

int release_locks_for_dead_owner(uint8_t owner_type, int64_t owner_id)
{
  transaction::tablelock::ObUnLockExecutor executor;
  return executor.execute(owner_type, owner_id);
}

} // namespace query
} // oceanbase
