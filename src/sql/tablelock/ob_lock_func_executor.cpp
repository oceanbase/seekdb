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
#include "sql/tablelock/ob_lock_func_executor.h"
#include "share/ob_dml_sql_splicer.h"

#include "data_plane/tablelock/ob_session_table_lock.h"
#include "query/session/ob_session_inner_sql.h"
#include "sql/engine/ob_exec_context.h"
#include "sql/ob_sql_trans_control.h"

namespace oceanbase
{
using namespace sql;
using namespace transaction;
using namespace common;
using namespace observer;

namespace transaction
{

namespace tablelock
{
int ObGetLockExecutor::execute(ObExecContext &ctx,
                               const ObString &lock_name,
                               const int64_t timeout_us)
{
  int ret = OB_SUCCESS;
  int tmp_ret = OB_SUCCESS;
  ObSQLSessionInfo *sess = ctx.get_my_session();
  uint32_t session_id = sess->get_server_sid();
  uint64_t session_create_ts = sess->get_sess_create_time();
  uint64_t lock_id = 0;
  bool is_rollback = false;
  ObTxParam tx_param;
  query::ObSessionInnerSql session_io(sess);
  const data_plane::ObSessionLockOwner owner(session_id, session_create_ts);

  OZ (ObLockContext::valid_execute_context(ctx));
  // 1. generate lock_id and update DBMS_LOCK_ALLOCATED table
  // 2. modify inner table
  // 4.1 add session into CLIENT_TO_SERVER_SESSION_INFO table
  // 4.2 add lock_obj into DETECT_LOCK_INFO table
  // 5. lock obj

  if (OB_SUCC(ret)) {
    SMART_VAR(ObLockContext, stack_ctx1) {
      OZ (stack_ctx1.init(ctx, timeout_us));
      OZ (generate_lock_id_(stack_ctx1, *ctx.get_sql_proxy(), lock_name, timeout_us, lock_id));

      is_rollback = (OB_SUCCESS != ret);
      if (OB_TMP_FAIL(stack_ctx1.destroy(ctx, is_rollback))) {
        LOG_WARN("stack ctx destroy failed", K(tmp_ret));
        COVER_SUCC(tmp_ret);
      }
    }
  }
  if (OB_SUCC(ret)) {
    SMART_VAR(ObLockContext, stack_ctx2) {
      OZ (stack_ctx2.init(ctx, timeout_us));
      OZ (ObSqlTransControl::build_tx_param(sess, tx_param));
      CK (OB_NOT_NULL(sess->get_tx_desc()));
      OZ (data_plane::acquire_named_lock(session_io,
                                         *sess->get_tx_desc(),
                                         tx_param,
                                         owner,
                                         lock_id,
                                         timeout_us));
      OX (mark_lock_session_(sess, true));

      is_rollback = (OB_SUCCESS != ret);
      if (OB_TMP_FAIL(stack_ctx2.destroy(ctx, is_rollback))) {
        LOG_WARN("stack ctx destroy failed", K(tmp_ret));
        COVER_SUCC(tmp_ret);
      }
    }
  }
  return ret;
}

int ObGetLockExecutor::generate_lock_id_(ObLockContext &ctx,
                                         ObISQLClient &sql_client,
                                         const ObString &lock_name,
                                         const int64_t timeout_us,
                                         uint64_t &lock_id)
{
  int ret = OB_SUCCESS;
  char lock_handle_buf[MAX_LOCK_HANDLE_LEGNTH] = {0};
  OZ (query_lock_id_and_lock_handle_(sql_client, lock_name, lock_id, lock_handle_buf));
  if (OB_EMPTY_RESULT == ret) {
    // there is no result, should create one
    ret = OB_SUCCESS;
    OZ (generate_lock_id_(lock_name, lock_id, lock_handle_buf));
  }
  OZ (write_lock_id_(ctx, lock_name, timeout_us, lock_id, lock_handle_buf));
  return ret;
}

int ObGetLockExecutor::generate_lock_id_(const ObString &lock_name,
                                         uint64_t &lock_id,
                                         char *lock_handle)
{
  int ret = OB_SUCCESS;
  uint64_t hash_val = 0;
  if (OB_ISNULL(lock_handle)) {
    ret = OB_ERR_UNEXPECTED;
    LOG_WARN("lock handle can not be null", K(ret));
  } else if (OB_FAIL(data_plane::generate_named_lock_identity(
                 lock_name,
                 MIN_LOCK_HANDLE_ID,
                 MAX_LOCK_HANDLE_ID,
                 lock_id,
                 hash_val))) {
    LOG_WARN("generate unique id for lock handle failed", K(ret));
  } else {
    snprintf(lock_handle, MAX_LOCK_HANDLE_LEGNTH, "%" PRIu64 "%" PRIu64, lock_id, hash_val);
  }
  return ret;
}

int ObGetLockExecutor::write_lock_id_(ObLockContext &ctx,
                                      const ObString &lock_name,
                                      const int64_t timeout_us,
                                      const uint64_t &lock_id,
                                      const char *lock_handle_buf)
{
  int ret = OB_SUCCESS;
  share::ObDMLSqlSplicer insert_dml;
  ObSqlString delete_sql;
  ObSqlString insert_sql;
  int64_t affected_rows = 0;
  const int64_t now = ObTimeUtility::current_time();
  char table_name[MAX_FULL_TABLE_NAME_LENGTH] = {0};
  OZ (databuff_printf(table_name, MAX_FULL_TABLE_NAME_LENGTH,
                      "%s.%s", OB_SYS_DATABASE_NAME, share::OB_ALL_DBMS_LOCK_ALLOCATED_TNAME));

  OZ (insert_dml.add_gmt_create(now));
  OZ (insert_dml.add_gmt_modified(now));
  OZ (insert_dml.add_pk_column("name", lock_name));
  // make sure lock_obj will be timeout or success before lock_id is expired
  OZ (insert_dml.add_time_column("expiration", now + timeout_us + DEFAULT_EXPIRATION_US));
  OZ (insert_dml.add_column("lockid", lock_id));
  OZ (insert_dml.add_column("lockhandle", lock_handle_buf));
  OZ (insert_dml.splice_insert_update_sql(table_name,
                                          insert_sql));
  OZ (ctx.execute_write(insert_sql, affected_rows));
  CK (OB_LIKELY(1 == affected_rows || 2 == affected_rows));

  return ret;
}

int ObReleaseLockExecutor::execute(ObExecContext &ctx,
                                   const ObString &lock_name,
                                   int64_t &release_cnt)
{
  int ret = OB_SUCCESS;
  int tmp_ret = OB_SUCCESS;
  uint32_t session_id = 0;
  uint64_t session_create_ts = 0;
  uint64_t lock_id = 0;
  bool is_rollback = false;

  release_cnt = INVALID_RELEASE_CNT;  // means not release successfully

  OZ (ObLockContext::valid_execute_context(ctx));
  if (OB_SUCC(ret)) {
    SMART_VAR(ObLockContext, stack_ctx) {
      session_id = ctx.get_my_session()->get_server_sid();
      session_create_ts = ctx.get_my_session()->get_sess_create_time();
      OZ (stack_ctx.init(ctx));
      if (OB_SUCC(ret)) {
        ObSQLSessionInfo *session = GET_MY_SESSION(ctx);
        ObTxParam tx_param;
        query::ObSessionInnerSql session_io(session);
        const data_plane::ObSessionLockOwner owner(session_id,
                                                    session_create_ts);
        // 1. get lock id from inner table
        // 2. unlock obj and update its metadata records.
        OZ (query_lock_id_(*ctx.get_sql_proxy(), lock_name, lock_id));
        if (OB_EMPTY_RESULT == ret) {
          release_cnt = LOCK_NOT_EXIST_RELEASE_CNT;
        }
        OZ (ObSqlTransControl::build_tx_param(session, tx_param));
        CK (OB_NOT_NULL(session->get_tx_desc()));
        OZ (data_plane::release_named_lock(session_io,
                                           *session->get_tx_desc(),
                                           tx_param,
                                           owner,
                                           lock_id,
                                           release_cnt));
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

int ObReleaseAllLockExecutor::execute(ObExecContext &ctx,
                                      int64_t &release_cnt)
{
  int ret = OB_SUCCESS;
  OZ (ObUnLockExecutor::execute(ctx, RELEASE_OBJ_LOCK, release_cnt));
  return ret;
}

int ObISFreeLockExecutor::execute(ObExecContext &ctx,
                                  const ObString &lock_name)
{
  UNUSED(ctx);
  int ret = OB_SUCCESS;
  uint64_t lock_id = 0;
  ObSQLSessionInfo *sess = ctx.get_my_session();
  bool exist = false;
  query::ObSessionInnerSql session_io(sess);
  OZ (query_lock_id_(*ctx.get_sql_proxy(), lock_name, lock_id));
  OZ (data_plane::named_lock_exists(session_io, lock_id, exist));

  if (OB_SUCC(ret) && !exist) {
    ret = OB_EMPTY_RESULT;
  }
  return ret;
}

int ObISUsedLockExecutor::execute(ObExecContext &ctx,
                                  const ObString &lock_name,
                                  uint32_t &sess_id)
{
  UNUSED(ctx);
  int ret = OB_SUCCESS;
  uint64_t lock_id = 0;

  OZ (query_lock_id_(*ctx.get_sql_proxy(), lock_name, lock_id));
  OZ (data_plane::get_named_lock_owner_session(*ctx.get_sql_proxy(), lock_id, sess_id));
  return ret;
}


} // tablelock
} // transaction
} // oceanbase
