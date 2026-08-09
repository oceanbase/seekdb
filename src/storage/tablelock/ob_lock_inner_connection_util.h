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

#ifndef OCEABASE_OB_LOCK_INNER_CONNECTION_UTIL_
#define OCEABASE_OB_LOCK_INNER_CONNECTION_UTIL_

#include "data_plane/ob_inner_sql_transmit_arg.h"
#include "storage/tablelock/ob_table_lock_common.h"
#include "storage/tablelock/ob_table_lock_rpc_struct.h"

namespace oceanbase
{
namespace common
{
namespace sqlclient
{
class ObISQLConnection;
}
}
namespace transaction
{
namespace tablelock
{
class ObLockRequest;
class ObLockObjRequest;
class ObLockObjsRequest;
class ObLockTableRequest;
class ObLockTabletRequest;
class ObLockPartitionRequest;
class ObLockAloneTabletRequest;
class ObUnLockObjRequest;
class ObUnLockObjsRequest;
class ObUnLockTableRequest;
class ObUnLockPartitionRequest;
class ObUnLockTabletRequest;
class ObUnLockAloneTabletRequest;

class ObIInnerConnectionLockRuntime
{
public:
  virtual ~ObIInnerConnectionLockRuntime() = default;
  virtual int process_lock_rpc(
      const obcall::ObInnerSQLTransmitArg &arg,
      common::sqlclient::ObISQLConnection *conn) = 0;
  virtual int lock_table(
      uint64_t table_id,
      ObTableLockMode lock_mode,
      int64_t timeout_us,
      common::sqlclient::ObISQLConnection *conn,
      ObTableLockOwnerID owner_id,
      ObTableLockPriority lock_priority) = 0;
  virtual int lock_table(
      const ObLockTableRequest &arg,
      common::sqlclient::ObISQLConnection *conn) = 0;
  virtual int unlock_table(
      const ObUnLockTableRequest &arg,
      common::sqlclient::ObISQLConnection *conn) = 0;
  virtual int lock_tablet(
      uint64_t table_id,
      ObTabletID tablet_id,
      ObTableLockMode lock_mode,
      int64_t timeout_us,
      common::sqlclient::ObISQLConnection *conn) = 0;
  virtual int lock_tablet(
      uint64_t table_id,
      const ObIArray<ObTabletID> &tablet_ids,
      ObTableLockMode lock_mode,
      int64_t timeout_us,
      common::sqlclient::ObISQLConnection *conn) = 0;
  virtual int lock_tablet(
      const ObLockAloneTabletRequest &arg,
      common::sqlclient::ObISQLConnection *conn) = 0;
  virtual int unlock_tablet(
      const ObUnLockAloneTabletRequest &arg,
      common::sqlclient::ObISQLConnection *conn) = 0;
  virtual int lock_obj(
      const ObLockObjRequest &arg,
      common::sqlclient::ObISQLConnection *conn) = 0;
  virtual int unlock_obj(
      const ObUnLockObjRequest &arg,
      common::sqlclient::ObISQLConnection *conn) = 0;
  virtual int lock_obj(
      const ObLockObjsRequest &arg,
      common::sqlclient::ObISQLConnection *conn) = 0;
  virtual int unlock_obj(
      const ObUnLockObjsRequest &arg,
      common::sqlclient::ObISQLConnection *conn) = 0;
  virtual int replace_lock(
      const ObReplaceLockRequest &req,
      common::sqlclient::ObISQLConnection *conn) = 0;
  virtual int replace_lock(
      const ObReplaceAllLocksRequest &req,
      common::sqlclient::ObISQLConnection *conn) = 0;
  virtual int execute_write_sql(
      common::sqlclient::ObISQLConnection *conn,
      const ObSqlString &sql,
      int64_t &affected_rows) = 0;
  virtual int execute_read_sql(
      common::sqlclient::ObISQLConnection *conn,
      const ObSqlString &sql,
      ObISQLClient::ReadResult &res) = 0;
};

class ObInnerConnectionLockUtil
{
public:
  static int process_lock_rpc(
      const obcall::ObInnerSQLTransmitArg &arg,
      common::sqlclient::ObISQLConnection *conn);
  static int lock_table(
      uint64_t table_id,
      ObTableLockMode lock_mode,
      int64_t timeout_us,
      common::sqlclient::ObISQLConnection *conn,
      ObTableLockOwnerID owner_id = ObTableLockOwnerID::default_owner(),
      ObTableLockPriority lock_priority = ObTableLockPriority::NORMAL);
  static int lock_table(
      const ObLockTableRequest &arg,
      common::sqlclient::ObISQLConnection *conn);
  static int unlock_table(
      const ObUnLockTableRequest &arg,
      common::sqlclient::ObISQLConnection *conn);
  static int lock_tablet(
      uint64_t table_id,
      ObTabletID tablet_id,
      ObTableLockMode lock_mode,
      int64_t timeout_us,
      common::sqlclient::ObISQLConnection *conn);
  static int lock_tablet(
      uint64_t table_id,
      const ObIArray<ObTabletID> &tablet_ids,
      ObTableLockMode lock_mode,
      int64_t timeout_us,
      common::sqlclient::ObISQLConnection *conn);
  static int lock_tablet(
      const ObLockAloneTabletRequest &arg,
      common::sqlclient::ObISQLConnection *conn);
  static int unlock_tablet(
      const ObUnLockAloneTabletRequest &arg,
      common::sqlclient::ObISQLConnection *conn);
  static int lock_obj(
      const ObLockObjRequest &arg,
      common::sqlclient::ObISQLConnection *conn);
  static int unlock_obj(
      const ObUnLockObjRequest &arg,
      common::sqlclient::ObISQLConnection *conn);
  static int lock_obj(
      const ObLockObjsRequest &arg,
      common::sqlclient::ObISQLConnection *conn);
  static int unlock_obj(
      const ObUnLockObjsRequest &arg,
      common::sqlclient::ObISQLConnection *conn);
  static int replace_lock(
      const ObReplaceLockRequest &req,
      common::sqlclient::ObISQLConnection *conn);
  static int replace_lock(
      const ObReplaceAllLocksRequest &req,
      common::sqlclient::ObISQLConnection *conn);
  static int execute_write_sql(
      common::sqlclient::ObISQLConnection *conn,
      const ObSqlString &sql,
      int64_t &affected_rows);
  static int execute_read_sql(
      common::sqlclient::ObISQLConnection *conn,
      const ObSqlString &sql,
      ObISQLClient::ReadResult &res);
};

} // tablelock
} // transaction
} // oceanbase

#endif
