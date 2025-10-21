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

#include "observer/ob_inner_sql_rpc_proxy.h"
#include "storage/tablelock/ob_table_lock_common.h"
#include "storage/tablelock/ob_table_lock_rpc_struct.h"

namespace oceanbase
{
namespace observer
{
class ObInnerSQLConnection;
}
namespace common
{
namespace sqlclient
{
class ObISQLConnection;
}
}
namespace observer
{
class ObInnerSQLResult;
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

class ObInnerConnectionLockUtil
{
// --------------------- interface for inner connection rpc processor -----------------------
public:
  static int process_lock_rpc(
      const obrpc::ObInnerSQLTransmitArg &arg,
      common::sqlclient::ObISQLConnection *conn);
private:
  static int process_lock_table_(
      const obrpc::ObInnerSQLTransmitArg::InnerSQLOperationType operation_type,
      const obrpc::ObInnerSQLTransmitArg &arg,
      observer::ObInnerSQLConnection *conn);
  static int process_lock_tablet_(
      const obrpc::ObInnerSQLTransmitArg::InnerSQLOperationType operation_type,
      const obrpc::ObInnerSQLTransmitArg &arg,
      observer::ObInnerSQLConnection *conn);
  static int process_replace_lock_(const obrpc::ObInnerSQLTransmitArg &arg, observer::ObInnerSQLConnection *conn);
  static int process_replace_all_locks_(const obrpc::ObInnerSQLTransmitArg &arg, observer::ObInnerSQLConnection *conn);
  // --------------------- interface for inner connection client -----------------------
public:
  static int lock_table(
      const uint64_t tenant_id,
      const uint64_t table_id,
      const ObTableLockMode lock_mode,
      const int64_t timeout_us,
      observer::ObInnerSQLConnection *conn,
      const ObTableLockOwnerID owner_id = ObTableLockOwnerID::default_owner(),
      const ObTableLockPriority lock_priority = ObTableLockPriority::NORMAL);
  static int lock_table(
      const uint64_t tenant_id,
      const ObLockTableRequest &arg,
      observer::ObInnerSQLConnection *conn);
  static int unlock_table(
      const uint64_t tenant_id,
      const ObUnLockTableRequest &arg,
      observer::ObInnerSQLConnection *conn);
  static int lock_tablet(
      const uint64_t tenant_id,
      const uint64_t table_id,
      const ObTabletID tablet_id,
      const ObTableLockMode lock_mode,
      const int64_t timeout_us,
      observer::ObInnerSQLConnection *conn);
  static int lock_tablet(
      const uint64_t tenant_id,
      const uint64_t table_id,
      const ObIArray<ObTabletID> &tablet_ids,
      const ObTableLockMode lock_mode,
      const int64_t timeout_us,
      observer::ObInnerSQLConnection *conn);
  static int lock_tablet(
      const uint64_t tenant_id,
      const ObLockAloneTabletRequest &arg,
      observer::ObInnerSQLConnection *conn);
  static int unlock_tablet(
      const uint64_t tenant_id,
      const ObUnLockAloneTabletRequest &arg,
      observer::ObInnerSQLConnection *conn);
  static int lock_obj(
      const uint64_t tenant_id,
      const ObLockObjRequest &arg,
      observer::ObInnerSQLConnection *conn);
  static int unlock_obj(
      const uint64_t tenant_id,
      const ObUnLockObjRequest &arg,
      observer::ObInnerSQLConnection *conn);
  static int lock_obj(
      const uint64_t tenant_id,
      const ObLockObjsRequest &arg,
      observer::ObInnerSQLConnection *conn);
  static int unlock_obj(
      const uint64_t tenant_id,
      const ObUnLockObjsRequest &arg,
      observer::ObInnerSQLConnection *conn);
  static int replace_lock(
      const uint64_t tenant_id,
      const ObReplaceLockRequest &req,
      observer::ObInnerSQLConnection *conn);
  static int replace_lock(
      const uint64_t tenant_id,
      const ObReplaceAllLocksRequest &req,
      observer::ObInnerSQLConnection *conn);
  static int create_inner_conn(sql::ObSQLSessionInfo *session_info,
                               common::ObMySQLProxy *sql_proxy,
                               observer::ObInnerSQLConnection *&inner_conn);
  static int execute_write_sql(observer::ObInnerSQLConnection *conn, const ObSqlString &sql, int64_t &affected_rows);
  static int execute_read_sql(observer::ObInnerSQLConnection *conn,
                              const ObSqlString &sql,
                              ObISQLClient::ReadResult &res);
  static int build_tx_param(sql::ObSQLSessionInfo *session_info, ObTxParam &tx_param, const bool *readonly = nullptr);

private:
  static int replace_lock_(
      const uint64_t tenant_id,
      const ObReplaceLockRequest &req,
      observer::ObInnerSQLConnection *conn,
      observer::ObInnerSQLResult &res);
  static int replace_lock_(
      const uint64_t tenant_id,
      const ObReplaceAllLocksRequest &req,
      observer::ObInnerSQLConnection *conn,
      observer::ObInnerSQLResult &res);
  static int do_obj_lock_(
      const uint64_t tenant_id,
      const ObLockRequest &arg,
      const obrpc::ObInnerSQLTransmitArg::InnerSQLOperationType operation_type,
      observer::ObInnerSQLConnection *conn,
      observer::ObInnerSQLResult &res);
  static int handle_request_by_operation_type_(
    ObTxDesc &tx_desc,
    const ObTxParam &tx_param,
    const ObLockRequest &arg,
    const obrpc::ObInnerSQLTransmitArg::InnerSQLOperationType operation_type);
  static int request_lock_(
      const uint64_t tenant_id,
      const uint64_t table_id,
      const ObTabletID tablet_id, //just used when lock_tablet
      const ObTableLockMode lock_mode,
      const int64_t timeout_us,
      const obrpc::ObInnerSQLTransmitArg::InnerSQLOperationType operation_type,
      observer::ObInnerSQLConnection *conn);
  static int request_lock_(
      const uint64_t tenant_id,
      const ObLockRequest &arg,
      const obrpc::ObInnerSQLTransmitArg::InnerSQLOperationType operation_type,
      observer::ObInnerSQLConnection *conn);
  static int get_org_cluster_id_(sql::ObSQLSessionInfo *session, int64_t &org_cluster_id);
  static int set_to_mysql_compat_mode_(observer::ObInnerSQLConnection *conn,
                                       bool &need_reset_sess_mode,
                                       bool &need_reset_conn_mode);
  static int reset_compat_mode_(observer::ObInnerSQLConnection *conn,
                                const bool need_reset_sess_mode,
                                const bool need_reset_conn_mode);
};

} // tablelock
} // transaction
} // oceanbase

#endif
