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

#include "storage/tablelock/ob_lock_inner_connection_util.h"
#include "share/rc/ob_server_runtime.h"

namespace oceanbase
{
namespace transaction
{
namespace tablelock
{
namespace
{
ObIInnerConnectionLockRuntime *runtime()
{
  return ::oceanbase::share::server_service<
      ::oceanbase::transaction::tablelock::ObIInnerConnectionLockRuntime>();
}
}

#define FORWARD_INNER_LOCK_CALL(call)                       \
  ObIInnerConnectionLockRuntime *adapter = runtime();       \
  return OB_ISNULL(adapter) ? common::OB_NOT_INIT : adapter->call

int ObInnerConnectionLockUtil::process_lock_rpc(
    const obcall::ObInnerSQLTransmitArg &arg,
    common::sqlclient::ObISQLConnection *conn)
{
  FORWARD_INNER_LOCK_CALL(process_lock_rpc(arg, conn));
}

int ObInnerConnectionLockUtil::lock_table(
    uint64_t table_id,
    ObTableLockMode lock_mode,
    int64_t timeout_us,
    common::sqlclient::ObISQLConnection *conn,
    ObTableLockOwnerID owner_id,
    ObTableLockPriority lock_priority)
{
  FORWARD_INNER_LOCK_CALL(
      lock_table(table_id, lock_mode, timeout_us, conn, owner_id, lock_priority));
}

int ObInnerConnectionLockUtil::lock_table(
    const ObLockTableRequest &arg,
    common::sqlclient::ObISQLConnection *conn)
{
  FORWARD_INNER_LOCK_CALL(lock_table(arg, conn));
}

int ObInnerConnectionLockUtil::unlock_table(
    const ObUnLockTableRequest &arg,
    common::sqlclient::ObISQLConnection *conn)
{
  FORWARD_INNER_LOCK_CALL(unlock_table(arg, conn));
}

int ObInnerConnectionLockUtil::lock_tablet(
    uint64_t table_id,
    ObTabletID tablet_id,
    ObTableLockMode lock_mode,
    int64_t timeout_us,
    common::sqlclient::ObISQLConnection *conn)
{
  FORWARD_INNER_LOCK_CALL(
      lock_tablet(table_id, tablet_id, lock_mode, timeout_us, conn));
}

int ObInnerConnectionLockUtil::lock_tablet(
    uint64_t table_id,
    const ObIArray<ObTabletID> &tablet_ids,
    ObTableLockMode lock_mode,
    int64_t timeout_us,
    common::sqlclient::ObISQLConnection *conn)
{
  FORWARD_INNER_LOCK_CALL(
      lock_tablet(table_id, tablet_ids, lock_mode, timeout_us, conn));
}

int ObInnerConnectionLockUtil::lock_tablet(
    const ObLockAloneTabletRequest &arg,
    common::sqlclient::ObISQLConnection *conn)
{
  FORWARD_INNER_LOCK_CALL(lock_tablet(arg, conn));
}

int ObInnerConnectionLockUtil::unlock_tablet(
    const ObUnLockAloneTabletRequest &arg,
    common::sqlclient::ObISQLConnection *conn)
{
  FORWARD_INNER_LOCK_CALL(unlock_tablet(arg, conn));
}

int ObInnerConnectionLockUtil::lock_obj(
    const ObLockObjRequest &arg,
    common::sqlclient::ObISQLConnection *conn)
{
  FORWARD_INNER_LOCK_CALL(lock_obj(arg, conn));
}

int ObInnerConnectionLockUtil::unlock_obj(
    const ObUnLockObjRequest &arg,
    common::sqlclient::ObISQLConnection *conn)
{
  FORWARD_INNER_LOCK_CALL(unlock_obj(arg, conn));
}

int ObInnerConnectionLockUtil::lock_obj(
    const ObLockObjsRequest &arg,
    common::sqlclient::ObISQLConnection *conn)
{
  FORWARD_INNER_LOCK_CALL(lock_obj(arg, conn));
}

int ObInnerConnectionLockUtil::unlock_obj(
    const ObUnLockObjsRequest &arg,
    common::sqlclient::ObISQLConnection *conn)
{
  FORWARD_INNER_LOCK_CALL(unlock_obj(arg, conn));
}

int ObInnerConnectionLockUtil::replace_lock(
    const ObReplaceLockRequest &req,
    common::sqlclient::ObISQLConnection *conn)
{
  FORWARD_INNER_LOCK_CALL(replace_lock(req, conn));
}

int ObInnerConnectionLockUtil::replace_lock(
    const ObReplaceAllLocksRequest &req,
    common::sqlclient::ObISQLConnection *conn)
{
  FORWARD_INNER_LOCK_CALL(replace_lock(req, conn));
}

int ObInnerConnectionLockUtil::execute_write_sql(
    common::sqlclient::ObISQLConnection *conn,
    const ObSqlString &sql,
    int64_t &affected_rows)
{
  FORWARD_INNER_LOCK_CALL(execute_write_sql(conn, sql, affected_rows));
}

int ObInnerConnectionLockUtil::execute_read_sql(
    common::sqlclient::ObISQLConnection *conn,
    const ObSqlString &sql,
    ObISQLClient::ReadResult &res)
{
  FORWARD_INNER_LOCK_CALL(execute_read_sql(conn, sql, res));
}

#undef FORWARD_INNER_LOCK_CALL

} // namespace tablelock
} // namespace transaction
} // namespace oceanbase
