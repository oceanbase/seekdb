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
#include "sql/tablelock/ob_mysql_lock_table_executor.h"
#include "data_plane/tablelock/ob_session_table_lock.h"
#include "query/session/ob_session_inner_sql.h"
#include "sql/engine/ob_exec_context.h"
#include "sql/ob_sql_trans_control.h"
#include "sql/session/ob_sql_session_info.h"

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

int ObMySQLLockTableExecutor::execute(ObExecContext &ctx,
                                      const ObIArray<data_plane::ObTableLockTarget> &lock_targets)
{
  int ret = OB_SUCCESS;
  int tmp_ret = OB_SUCCESS;
  ObSQLSessionInfo *sess = ctx.get_my_session();
  uint32_t session_id = sess->get_server_sid();
  uint64_t session_create_ts = sess->get_sess_create_time();
  bool is_rollback = false;
  ObTxParam tx_param;
  int64_t timeout_us = THIS_WORKER.get_timeout_ts() - ObTimeUtility::current_time();
  query::ObSessionInnerSql session_io(sess);
  const data_plane::ObSessionLockOwner owner(session_id, session_create_ts);
  OZ (ObLockContext::valid_execute_context(ctx));

  if (OB_SUCC(ret)) {
    SMART_VAR(ObLockContext, stack_ctx) {
      OZ (stack_ctx.init(ctx, timeout_us));
      OZ (ObSqlTransControl::build_tx_param(sess, tx_param));
      CK (OB_NOT_NULL(sess->get_tx_desc()));
      for (int64_t i = 0; OB_SUCC(ret) && i < lock_targets.count(); ++i) {
        OZ (data_plane::acquire_mysql_table_lock(session_io,
                                                 *sess->get_tx_desc(),
                                                 tx_param,
                                                 owner,
                                                 lock_targets.at(i),
                                                 timeout_us));
      }
      OX (mark_lock_session_(sess, true));

      is_rollback = (OB_SUCCESS != ret);
      if (OB_TMP_FAIL(stack_ctx.destroy(ctx, is_rollback))) {
        LOG_WARN("stack ctx destroy failed", K(tmp_ret));
        COVER_SUCC(tmp_ret);
      }
    }
  }
  return ret;
}

int ObMySQLUnlockTableExecutor::execute(sql::ObExecContext &ctx)
{
  int ret = OB_SUCCESS;
  int64_t release_cnt = 0;
  OZ (ObUnLockExecutor::execute(ctx, RELEASE_TABLE_LOCK, release_cnt));
  return ret;
}

} // tablelock
} // transaction
} // oceanbase
