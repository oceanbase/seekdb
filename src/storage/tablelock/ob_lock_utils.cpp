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

#include "storage/tablelock/ob_lock_utils.h"
#include "observer/ob_inner_sql_connection.h" // ObInnerSQLConnection
#include "storage/tablelock/ob_lock_inner_connection_util.h"

namespace oceanbase
{
using namespace common;
using namespace observer;
using namespace share;
namespace transaction
{
namespace tablelock
{
int ObInnerTableLockUtil::lock_inner_table_in_trans(
    common::ObMySQLTransaction &trans,
    const uint64_t inner_table_id,
    const ObTableLockMode &lock_mode,
    const bool is_from_sql)
{
  int ret = OB_SUCCESS;
  ObInnerSQLConnection *conn = NULL;
  ObTimeoutCtx ctx;
  const int64_t DEFAULT_TIMEOUT = GCONF.internal_sql_execute_timeout;
  if (OB_UNLIKELY(!is_inner_table(inner_table_id)
      || !is_lock_mode_valid(lock_mode))) {
    ret = OB_INVALID_ARGUMENT;
    LOG_WARN("invalid args", KR(ret), K(inner_table_id), K(lock_mode));
  } else if (OB_ISNULL(conn = static_cast<ObInnerSQLConnection *>(trans.get_connection()))) {
    ret = OB_ERR_UNEXPECTED;
    LOG_WARN("connection is null", KR(ret));
  } else if (OB_FAIL(ObShareUtil::set_default_timeout_ctx(ctx, DEFAULT_TIMEOUT))) {
    LOG_WARN("fail to set default_timeout_ctx", KR(ret));
  } else {
    ObLockTableRequest table_lock_arg;
    table_lock_arg.lock_mode_ = lock_mode;
    table_lock_arg.timeout_us_ = ctx.get_timeout();
    table_lock_arg.table_id_ = inner_table_id;
    table_lock_arg.op_type_ = IN_TRANS_COMMON_LOCK;
    table_lock_arg.is_from_sql_ = is_from_sql;
    if (OB_FAIL(ObInnerConnectionLockUtil::lock_table(table_lock_arg, conn))) {
      LOG_WARN("lock table failed", KR(ret), K(table_lock_arg));
    }
  }
  return ret;
}

} // end namespace tablelock
} // end namespace transaction
} // end namespace oceanbase
