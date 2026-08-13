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

#include "storage/tablelock/ob_lock_inner_connection_util.h"
#include "share/rc/ob_server_runtime.h"
#include "observer/ob_server.h"
#include "observer/ob_inner_sql_connection.h"
#include "observer/ob_inner_sql_result.h"
#include "storage/tablelock/ob_table_lock_service.h"


using namespace oceanbase::observer;
using namespace oceanbase::obcall;
namespace oceanbase
{
namespace transaction
{
namespace tablelock
{

class ObInnerConnectionLockRuntime final : public ObIInnerConnectionLockRuntime
{
public:
  int process_lock_rpc(
      const obcall::ObInnerSQLTransmitArg &arg,
      common::sqlclient::ObISQLConnection *conn) override;
  int lock_table(
      uint64_t table_id,
      ObTableLockMode lock_mode,
      int64_t timeout_us,
      common::sqlclient::ObISQLConnection *conn,
      ObTableLockOwnerID owner_id,
      ObTableLockPriority lock_priority) override;
  int lock_table(
      const ObLockTableRequest &arg,
      common::sqlclient::ObISQLConnection *conn) override;
  int unlock_table(
      const ObUnLockTableRequest &arg,
      common::sqlclient::ObISQLConnection *conn) override;
  int lock_tablet(
      uint64_t table_id,
      ObTabletID tablet_id,
      ObTableLockMode lock_mode,
      int64_t timeout_us,
      common::sqlclient::ObISQLConnection *conn) override;
  int lock_tablet(
      uint64_t table_id,
      const ObIArray<ObTabletID> &tablet_ids,
      ObTableLockMode lock_mode,
      int64_t timeout_us,
      common::sqlclient::ObISQLConnection *conn) override;
  int lock_tablet(
      const ObLockAloneTabletRequest &arg,
      common::sqlclient::ObISQLConnection *conn) override;
  int unlock_tablet(
      const ObUnLockAloneTabletRequest &arg,
      common::sqlclient::ObISQLConnection *conn) override;
  int lock_obj(
      const ObLockObjRequest &arg,
      common::sqlclient::ObISQLConnection *conn) override;
  int unlock_obj(
      const ObUnLockObjRequest &arg,
      common::sqlclient::ObISQLConnection *conn) override;
  int lock_obj(
      const ObLockObjsRequest &arg,
      common::sqlclient::ObISQLConnection *conn) override;
  int unlock_obj(
      const ObUnLockObjsRequest &arg,
      common::sqlclient::ObISQLConnection *conn) override;
  int replace_lock(
      const ObReplaceLockRequest &req,
      common::sqlclient::ObISQLConnection *conn) override;
  int replace_lock(
      const ObReplaceAllLocksRequest &req,
      common::sqlclient::ObISQLConnection *conn) override;
  int execute_write_sql(
      common::sqlclient::ObISQLConnection *conn,
      const ObSqlString &sql,
      int64_t &affected_rows) override;
  int execute_read_sql(
      common::sqlclient::ObISQLConnection *conn,
      const ObSqlString &sql,
      ObISQLClient::ReadResult &res) override;

private:
  int process_lock_table_(
      obcall::ObInnerSQLTransmitArg::InnerSQLOperationType operation_type,
      const obcall::ObInnerSQLTransmitArg &arg,
      observer::ObInnerSQLConnection *conn);
  int process_lock_tablet_(
      obcall::ObInnerSQLTransmitArg::InnerSQLOperationType operation_type,
      const obcall::ObInnerSQLTransmitArg &arg,
      observer::ObInnerSQLConnection *conn);
  int process_replace_lock_(
      const obcall::ObInnerSQLTransmitArg &arg,
      observer::ObInnerSQLConnection *conn);
  int process_replace_all_locks_(
      const obcall::ObInnerSQLTransmitArg &arg,
      observer::ObInnerSQLConnection *conn);
  int replace_lock_(
      const ObReplaceLockRequest &req,
      observer::ObInnerSQLConnection *conn,
      observer::ObInnerSQLResult &res);
  int replace_lock_(
      const ObReplaceAllLocksRequest &req,
      observer::ObInnerSQLConnection *conn,
      observer::ObInnerSQLResult &res);
  int do_obj_lock_(
      const ObLockRequest &arg,
      obcall::ObInnerSQLTransmitArg::InnerSQLOperationType operation_type,
      observer::ObInnerSQLConnection *conn,
      observer::ObInnerSQLResult &res);
  int handle_request_by_operation_type_(
      ObTxDesc &tx_desc,
      const ObTxParam &tx_param,
      const ObLockRequest &arg,
      obcall::ObInnerSQLTransmitArg::InnerSQLOperationType operation_type);
  int request_lock_(
      uint64_t table_id,
      ObTabletID tablet_id,
      ObTableLockMode lock_mode,
      int64_t timeout_us,
      obcall::ObInnerSQLTransmitArg::InnerSQLOperationType operation_type,
      observer::ObInnerSQLConnection *conn);
  int request_lock_(
      const ObLockRequest &arg,
      obcall::ObInnerSQLTransmitArg::InnerSQLOperationType operation_type,
      observer::ObInnerSQLConnection *conn);
};

#define __REQUEST_LOCK_CHECK_VERSION(T, operation_type, arg, conn)                     \
  {                                                                                    \
    int64_t pos = 0;                                                                   \
    T lock_arg;                                                                        \
    if (OB_FAIL(lock_arg.deserialize(arg.get_inner_sql().ptr(),                        \
                                            arg.get_inner_sql().length(),              \
                                            pos))) {                                   \
      LOG_WARN("deserialize multi source data str failed", K(ret), K(arg), K(pos));    \
     } else if (OB_FAIL(request_lock_(\
                                      lock_arg,                                        \
                                      operation_type,                                  \
                                      conn))) {                                        \
      LOG_WARN("request lock failed", K(ret), K(lock_arg));    \
     }                                                                                 \
  }

#define REQUEST_LOCK(T, operation_type, arg, conn)                                 \
  __REQUEST_LOCK_CHECK_VERSION(T, operation_type, arg, conn)

#define REPLACE_LOCK(T, arg, conn, replace_req, buf, len, pos)                        \
  T unlock_req;                                                                       \
  if (OB_FAIL(unlock_req.deserialize(buf, len, pos))) {                               \
    LOG_WARN("deserialize unlock_req in replace_req failed", K(ret), K(replace_req)); \
  } else if (FALSE_IT(replace_req.unlock_req_ = &unlock_req)) {                       \
  } else if (OB_FAIL(replace_lock(replace_req, conn))) {         \
    LOG_WARN("replace lock failed", K(ret), K(replace_req));                          \
  }                                                                                   \
  break;

#define CONVERT_TYPE_AND_DO_LOCK(T, arg, tx_desc, tx_param)                    \
  const T lock_req = static_cast<const T &>(arg);                              \
  if (OB_FAIL(::oceanbase::share::server_service<::oceanbase::transaction::tablelock::ObTableLockService>()->lock(tx_desc, tx_param, lock_req))) { \
    LOG_WARN("lock failed", K(ret), K(lock_req));                              \
  }                                                                            \
  break;

#define CONVERT_TYPE_AND_DO_UNLOCK(T, arg, tx_desc, tx_param)                      \
  const T lock_req = static_cast<const T &>(arg);                                  \
  T &unlock_req = const_cast<T &>(lock_req);                                       \
  unlock_req.set_to_unlock_type();                                                 \
  if (OB_FAIL(::oceanbase::share::server_service<::oceanbase::transaction::tablelock::ObTableLockService>()->unlock(tx_desc, tx_param, unlock_req))) { \
    LOG_WARN("unlock failed", K(ret), K(unlock_req));                              \
  }                                                                                \
  break;

int ObInnerConnectionLockRuntime::process_lock_rpc(
    const ObInnerSQLTransmitArg &arg,
    common::sqlclient::ObISQLConnection *conn)
{
  int ret = OB_SUCCESS;
  observer::ObInnerSQLConnection *inner_conn = static_cast<observer::ObInnerSQLConnection *>(conn);
  if (OB_ISNULL(conn)) {
    ret = OB_INVALID_ARGUMENT;
    LOG_WARN("invalid argument", KR(ret), K(arg), KP(conn));
  } else {
    const obcall::ObInnerSQLTransmitArg::InnerSQLOperationType operation_type = arg.get_operation_type();
    switch (operation_type) {
      case ObInnerSQLTransmitArg::OPERATION_TYPE_LOCK_TABLE: {
        if (OB_FAIL(process_lock_table_(operation_type,
                                        arg,
                                        inner_conn))) {
        }
        break;
      }
      case ObInnerSQLTransmitArg::OPERATION_TYPE_UNLOCK_TABLE: {
        REQUEST_LOCK(ObUnLockTableRequest, operation_type, arg, inner_conn);
        break;
      }
      case ObInnerSQLTransmitArg::OPERATION_TYPE_LOCK_TABLET: {
        if (OB_FAIL(process_lock_tablet_(operation_type,
                                         arg,
                                         inner_conn))) {
        }
        break;
      }
      case ObInnerSQLTransmitArg::OPERATION_TYPE_UNLOCK_TABLET: {
        REQUEST_LOCK(ObUnLockTabletRequest, operation_type, arg, inner_conn);
        break;
      }
      case ObInnerSQLTransmitArg::OPERATION_TYPE_LOCK_OBJ: {
        REQUEST_LOCK(ObLockObjRequest, operation_type, arg, inner_conn);
        break;
      }
      case ObInnerSQLTransmitArg::OPERATION_TYPE_UNLOCK_OBJ: {
        REQUEST_LOCK(ObUnLockObjRequest, operation_type, arg, inner_conn);
        break;
      }
      case ObInnerSQLTransmitArg::OPERATION_TYPE_LOCK_OBJS: {
        REQUEST_LOCK(ObLockObjsRequest, operation_type, arg, inner_conn);
        break;
      }
      case ObInnerSQLTransmitArg::OPERATION_TYPE_UNLOCK_OBJS: {
        REQUEST_LOCK(ObUnLockObjsRequest, operation_type, arg, inner_conn);
        break;
      }
      case ObInnerSQLTransmitArg::OPERATION_TYPE_LOCK_PART: {
        REQUEST_LOCK(ObLockPartitionRequest, operation_type, arg, inner_conn);
        break;
      }
      case ObInnerSQLTransmitArg::OPERATION_TYPE_UNLOCK_PART: {
        REQUEST_LOCK(ObUnLockPartitionRequest, operation_type, arg, inner_conn);
        break;
      }
      case ObInnerSQLTransmitArg::OPERATION_TYPE_LOCK_SUBPART: {
        REQUEST_LOCK(ObLockPartitionRequest, operation_type, arg, inner_conn);
        break;
      }
      case ObInnerSQLTransmitArg::OPERATION_TYPE_UNLOCK_SUBPART: {
        REQUEST_LOCK(ObUnLockPartitionRequest, operation_type, arg, inner_conn);
        break;
      }
      case ObInnerSQLTransmitArg::OPERATION_TYPE_LOCK_ALONE_TABLET: {
        REQUEST_LOCK(ObLockAloneTabletRequest, operation_type, arg, inner_conn);
        break;
      }
      case ObInnerSQLTransmitArg::OPERATION_TYPE_UNLOCK_ALONE_TABLET: {
        REQUEST_LOCK(ObUnLockAloneTabletRequest, operation_type, arg, inner_conn);
        break;
      }
      case ObInnerSQLTransmitArg::OPERATION_TYPE_REPLACE_LOCK: {
        if (OB_FAIL(process_replace_lock_(arg, inner_conn))) {
        }
        break;
      }
      case ObInnerSQLTransmitArg::OPERATION_TYPE_REPLACE_LOCKS: {
        if (OB_FAIL(process_replace_all_locks_(arg, inner_conn))) {
        }
        break;
      }
      default: {
        ret = OB_NOT_SUPPORTED;
        LOG_WARN("Unknown operation type", K(ret), K(operation_type));
        break;
      }
    }
  }
  return ret;
}

int ObInnerConnectionLockRuntime::process_lock_table_(
    const obcall::ObInnerSQLTransmitArg::InnerSQLOperationType operation_type,
    const ObInnerSQLTransmitArg &arg,
    observer::ObInnerSQLConnection *conn)
{
  int ret = OB_SUCCESS;
  int64_t pos = 0;
  ObTabletID no_used;

  REQUEST_LOCK(ObLockTableRequest, operation_type, arg, conn);

  return ret;
}

int ObInnerConnectionLockRuntime::process_lock_tablet_(
    const obcall::ObInnerSQLTransmitArg::InnerSQLOperationType operation_type,
    const ObInnerSQLTransmitArg &arg,
    observer::ObInnerSQLConnection *conn)
{
  int ret = OB_SUCCESS;
  int64_t pos = 0;

  REQUEST_LOCK(ObLockTabletsRequest, operation_type, arg, conn);

  return ret;
}

int ObInnerConnectionLockRuntime::process_replace_lock_(
  const ObInnerSQLTransmitArg &arg,
  observer::ObInnerSQLConnection *conn)
{
  int ret = OB_SUCCESS;
  ObReplaceLockRequest replace_req;
  ObUnLockRequest unlock_req;
  const char *buf = arg.get_inner_sql().ptr();
  const int64_t data_len = arg.get_inner_sql().length();
  int64_t pos = 0;
  int64_t tmp_pos = 0;
  if (OB_FAIL(replace_req.deserialize_and_check_header(buf, data_len, pos))) {
  } else if (OB_FAIL(replace_req.deserialize_new_lock_mode_and_owner(buf, data_len, pos))) {
  } else if (FALSE_IT(tmp_pos = pos)) {
  } else if (OB_FAIL(unlock_req.deserialize(buf, data_len, tmp_pos))) {
  } else {
    switch (unlock_req.type_) {
      case ObLockRequest::ObLockMsgType::UNLOCK_OBJ_REQ:{
        REPLACE_LOCK(ObUnLockObjsRequest, arg, conn, replace_req, buf, data_len, pos);
      }
      case ObLockRequest::ObLockMsgType::UNLOCK_TABLE_REQ: {
        REPLACE_LOCK(ObUnLockTableRequest, arg, conn, replace_req, buf, data_len, pos);
      }
      case ObLockRequest::ObLockMsgType::UNLOCK_PARTITION_REQ: {
        REPLACE_LOCK(ObUnLockPartitionRequest, arg, conn, replace_req, buf, data_len, pos);
      }
      case ObLockRequest::ObLockMsgType::UNLOCK_TABLET_REQ: {
        REPLACE_LOCK(ObUnLockTabletsRequest, arg, conn, replace_req, buf, data_len, pos);
      }
      case ObLockRequest::ObLockMsgType::UNLOCK_ALONE_TABLET_REQ: {
        REPLACE_LOCK(ObUnLockAloneTabletRequest, arg, conn, replace_req, buf, data_len, pos);
      }
      default: {
        ret = OB_INVALID_ARGUMENT;
        LOG_WARN("meet not supportted replace request", K(unlock_req), K(arg));
      }
    }
  }
  return ret;
}

int ObInnerConnectionLockRuntime::process_replace_all_locks_(
  const ObInnerSQLTransmitArg &arg,
  observer::ObInnerSQLConnection *conn)
{
  int ret = OB_SUCCESS;
  ObArenaAllocator allocator;
  ObReplaceAllLocksRequest replace_req(allocator);
  const char *buf = arg.get_inner_sql().ptr();
  const int64_t data_len = arg.get_inner_sql().length();
  int64_t pos = 0;
  if (OB_FAIL(replace_req.deserialize(buf, data_len, pos))) {
  } else if (OB_FAIL(replace_lock(replace_req, conn))) {
  }
  replace_req.reset();
  return ret;
}

int ObInnerConnectionLockRuntime::lock_table(
    const uint64_t table_id,
    const ObTableLockMode lock_mode,
    const int64_t timeout_us,
    common::sqlclient::ObISQLConnection *conn,
    const ObTableLockOwnerID owner_id,
    const ObTableLockPriority lock_priority)
{
  int ret = OB_SUCCESS;
  observer::ObInnerSQLConnection *inner_conn =
      static_cast<observer::ObInnerSQLConnection *>(conn);
  ObLockTableRequest lock_arg;
  lock_arg.owner_id_ = owner_id;
  lock_arg.lock_mode_ = lock_mode;
  lock_arg.op_type_ = IN_TRANS_COMMON_LOCK;
  lock_arg.timeout_us_ = timeout_us;
  lock_arg.table_id_ = table_id;
  lock_arg.lock_priority_ = lock_priority;

  ret = request_lock_(lock_arg,
                      ObInnerSQLTransmitArg::OPERATION_TYPE_LOCK_TABLE,
                      inner_conn);
  return ret;
}

int ObInnerConnectionLockRuntime::lock_table(
    const ObLockTableRequest &arg,
    common::sqlclient::ObISQLConnection *conn)
{
  return request_lock_(arg,
                       ObInnerSQLTransmitArg::OPERATION_TYPE_LOCK_TABLE,
                       static_cast<observer::ObInnerSQLConnection *>(conn));
}

int ObInnerConnectionLockRuntime::unlock_table(
    const ObUnLockTableRequest &arg,
    common::sqlclient::ObISQLConnection *conn)
{
  int ret = OB_SUCCESS;
  if (OB_UNLIKELY(ObTableLockOpType::OUT_TRANS_UNLOCK != arg.op_type_)) {
    ret = OB_INVALID_ARGUMENT;
    LOG_WARN("only OUT_TRANS_LOCK should unlock.", K(ret), K(arg));
  } else {
    ret = request_lock_(arg,
                        ObInnerSQLTransmitArg::OPERATION_TYPE_UNLOCK_TABLE,
                        static_cast<observer::ObInnerSQLConnection *>(conn));
  }
  return ret;
}





int ObInnerConnectionLockRuntime::lock_tablet(
    const uint64_t table_id,
    const ObTabletID tablet_id,
    const ObTableLockMode lock_mode,
    const int64_t timeout_us,
    common::sqlclient::ObISQLConnection *conn)
{
  int ret = OB_SUCCESS;
  observer::ObInnerSQLConnection *inner_conn =
      static_cast<observer::ObInnerSQLConnection *>(conn);
  ObLockTabletsRequest lock_arg;
  lock_arg.owner_id_.set_default();
  lock_arg.lock_mode_ = lock_mode;
  lock_arg.op_type_ = IN_TRANS_COMMON_LOCK;
  lock_arg.timeout_us_ = timeout_us;
  lock_arg.table_id_ = table_id;
  if (OB_FAIL(lock_arg.tablet_ids_.push_back(tablet_id))) {
  } else if (OB_FAIL(request_lock_(lock_arg,
                                   ObInnerSQLTransmitArg::OPERATION_TYPE_LOCK_TABLET,
                                   inner_conn))) {
  }
  return ret;
}

int ObInnerConnectionLockRuntime::lock_tablet(
    const uint64_t table_id,
    const ObIArray<ObTabletID> &tablet_ids,
    const ObTableLockMode lock_mode,
    const int64_t timeout_us,
    common::sqlclient::ObISQLConnection *conn)
{
  int ret = OB_SUCCESS;
  observer::ObInnerSQLConnection *inner_conn =
      static_cast<observer::ObInnerSQLConnection *>(conn);
  ObLockTabletsRequest lock_arg;
  lock_arg.owner_id_.set_default();
  lock_arg.lock_mode_ = lock_mode;
  lock_arg.op_type_ = IN_TRANS_COMMON_LOCK;
  lock_arg.timeout_us_ = timeout_us;
  lock_arg.table_id_ = table_id;

  if (OB_FAIL(lock_arg.tablet_ids_.assign(tablet_ids))) {
  } else if (OB_FAIL(request_lock_(lock_arg,
                                   ObInnerSQLTransmitArg::OPERATION_TYPE_LOCK_TABLET,
                                   inner_conn))) {
  }
  return ret;
}



int ObInnerConnectionLockRuntime::lock_tablet(
    const ObLockAloneTabletRequest &arg,
    common::sqlclient::ObISQLConnection *conn)
{
  return request_lock_(arg,
                       ObInnerSQLTransmitArg::OPERATION_TYPE_LOCK_ALONE_TABLET,
                       static_cast<observer::ObInnerSQLConnection *>(conn));
}

int ObInnerConnectionLockRuntime::unlock_tablet(
    const ObUnLockAloneTabletRequest &arg,
    common::sqlclient::ObISQLConnection *conn)
{
  int ret = OB_SUCCESS;
  if (OB_UNLIKELY(ObTableLockOpType::OUT_TRANS_UNLOCK != arg.op_type_)) {
    ret = OB_INVALID_ARGUMENT;
    LOG_WARN("only OUT_TRANS_LOCK should unlock.", K(ret), K(arg));
  } else {
    ret = request_lock_(arg,
                        ObInnerSQLTransmitArg::OPERATION_TYPE_UNLOCK_ALONE_TABLET,
                        static_cast<observer::ObInnerSQLConnection *>(conn));
  }
  return ret;
}

int ObInnerConnectionLockRuntime::lock_obj(
    const ObLockObjRequest &arg,
    common::sqlclient::ObISQLConnection *conn)
{
  return request_lock_(arg,
                       ObInnerSQLTransmitArg::OPERATION_TYPE_LOCK_OBJ,
                       static_cast<observer::ObInnerSQLConnection *>(conn));
}

int ObInnerConnectionLockRuntime::unlock_obj(
    const ObUnLockObjRequest &arg,
    common::sqlclient::ObISQLConnection *conn)
{
  int ret = OB_SUCCESS;
  if (OB_UNLIKELY(ObTableLockOpType::OUT_TRANS_UNLOCK != arg.op_type_)) {
    ret = OB_INVALID_ARGUMENT;
    LOG_WARN("only OUT_TRANS_LOCK should unlock.", K(ret), K(arg));
  } else {
    ret = request_lock_(arg,
                        ObInnerSQLTransmitArg::OPERATION_TYPE_UNLOCK_OBJ,
                        static_cast<observer::ObInnerSQLConnection *>(conn));
  }
  return ret;
}

int ObInnerConnectionLockRuntime::lock_obj(
    const ObLockObjsRequest &arg,
    common::sqlclient::ObISQLConnection *conn)
{
  return request_lock_(arg,
                       ObInnerSQLTransmitArg::OPERATION_TYPE_LOCK_OBJS,
                       static_cast<observer::ObInnerSQLConnection *>(conn));
}

int ObInnerConnectionLockRuntime::unlock_obj(
    const ObUnLockObjsRequest &arg,
    common::sqlclient::ObISQLConnection *conn)
{
  int ret = OB_SUCCESS;
  if (OB_UNLIKELY(ObTableLockOpType::OUT_TRANS_UNLOCK != arg.op_type_)) {
    ret = OB_INVALID_ARGUMENT;
    LOG_WARN("only OUT_TRANS_LOCK should unlock.", K(ret), K(arg));
  } else {
    ret = request_lock_(arg,
                        ObInnerSQLTransmitArg::OPERATION_TYPE_UNLOCK_OBJS,
                        static_cast<observer::ObInnerSQLConnection *>(conn));
  }
  return ret;
}

int ObInnerConnectionLockRuntime::replace_lock(
      const ObReplaceLockRequest &req,
      common::sqlclient::ObISQLConnection *conn)
{
  int ret = OB_SUCCESS;
  observer::ObReqTimeGuard req_timeinfo_guard;
  observer::ObInnerSQLConnection *inner_conn =
      static_cast<observer::ObInnerSQLConnection *>(conn);

  SMART_VAR(ObInnerSQLResult, res, inner_conn->get_session(),
            inner_conn->get_sql_engine()->get_plan_cache_access_service(),
            inner_conn->is_inner_session())
  {
    if (!inner_conn->is_in_trans()) {
      ret = OB_ERR_UNEXPECTED;
      LOG_WARN("inner conn must be already in trans", K(ret));
    } else if (OB_FAIL(res.init())) {
    } else if (OB_FAIL(replace_lock_(req, inner_conn, res))) {
    }
  }

  return ret;
}

int ObInnerConnectionLockRuntime::replace_lock(const ObReplaceAllLocksRequest &req,
                                            common::sqlclient::ObISQLConnection *conn)
{
  int ret = OB_SUCCESS;
  observer::ObReqTimeGuard req_timeinfo_guard;
  observer::ObInnerSQLConnection *inner_conn =
      static_cast<observer::ObInnerSQLConnection *>(conn);

  SMART_VAR(ObInnerSQLResult, res, inner_conn->get_session(),
            inner_conn->get_sql_engine()->get_plan_cache_access_service(),
            inner_conn->is_inner_session())
  {
    if (!inner_conn->is_in_trans()) {
      ret = OB_ERR_UNEXPECTED;
      LOG_WARN("inner conn must be already in trans", K(ret));
    } else if (OB_FAIL(res.init())) {
    } else if (OB_FAIL(replace_lock_(req, inner_conn, res))) {
    }
  }
  return ret;
}

int ObInnerConnectionLockRuntime::replace_lock_(const ObReplaceLockRequest &req,
    observer::ObInnerSQLConnection *conn,
    observer::ObInnerSQLResult &res)
{
  int ret = OB_SUCCESS;
  transaction::ObTxDesc *tx_desc = nullptr;

  if (OB_ISNULL(conn)) {
    ret = OB_INVALID_ARGUMENT;
    LOG_WARN("Invalid conn", KR(ret));
  } else if (OB_ISNULL(tx_desc = conn->get_session().get_tx_desc())) {
    ret = OB_INVALID_ARGUMENT;
    LOG_WARN("Invalid tx_desc");
  } else {
    transaction::ObTxParam tx_param;
    tx_param.access_mode_ = transaction::ObTxAccessMode::RW;
    tx_param.isolation_ = conn->get_session().get_tx_isolation();
    conn->get_session().get_tx_timeout(tx_param.timeout_us_);
    tx_param.lock_timeout_us_ = conn->get_session().get_trx_lock_timeout();

    SERVER_MODULE_SCOPE {
      if (OB_FAIL(::oceanbase::share::server_service<::oceanbase::transaction::tablelock::ObTableLockService>()->replace_lock(*tx_desc, tx_param, req))) {
      } else if (OB_FAIL(res.close())) {
      }
    }
  }
  return ret;
}

int ObInnerConnectionLockRuntime::replace_lock_(const ObReplaceAllLocksRequest &req,
    observer::ObInnerSQLConnection *conn,
    observer::ObInnerSQLResult &res)
{
  int ret = OB_SUCCESS;
  transaction::ObTxDesc *tx_desc = nullptr;

  if (OB_ISNULL(conn)) {
    ret = OB_INVALID_ARGUMENT;
    LOG_WARN("Invalid conn", KR(ret));
  } else if (OB_ISNULL(tx_desc = conn->get_session().get_tx_desc())) {
    ret = OB_INVALID_ARGUMENT;
    LOG_WARN("Invalid tx_desc");
  } else {
    transaction::ObTxParam tx_param;
    tx_param.access_mode_ = transaction::ObTxAccessMode::RW;
    tx_param.isolation_ = conn->get_session().get_tx_isolation();
    conn->get_session().get_tx_timeout(tx_param.timeout_us_);
    tx_param.lock_timeout_us_ = conn->get_session().get_trx_lock_timeout();

    SERVER_MODULE_SCOPE {
      if (OB_FAIL(::oceanbase::share::server_service<::oceanbase::transaction::tablelock::ObTableLockService>()->replace_lock(*tx_desc, tx_param, req))) {
      } else if (OB_FAIL(res.close())) {
      }
    }
  }
  return ret;
}

int ObInnerConnectionLockRuntime::execute_write_sql(common::sqlclient::ObISQLConnection *conn,
                                                 const ObSqlString &sql,
                                                 int64_t &affected_rows)
{
  int ret = OB_SUCCESS;
  observer::ObInnerSQLConnection *inner_conn =
      static_cast<observer::ObInnerSQLConnection *>(conn);

  if (OB_ISNULL(inner_conn)) {
    ret = OB_ERR_UNEXPECTED;
    LOG_ERROR("inner_conn is nullptr", K(ret), K(sql));
  } else if (OB_FAIL(inner_conn->execute_write(sql.ptr(), affected_rows))) {
  }
  return ret;
}

int ObInnerConnectionLockRuntime::execute_read_sql(common::sqlclient::ObISQLConnection *conn,
                                                const ObSqlString &sql,
                                                ObISQLClient::ReadResult &res)
{
  int ret = OB_SUCCESS;
  observer::ObInnerSQLConnection *inner_conn =
      static_cast<observer::ObInnerSQLConnection *>(conn);

  if (OB_ISNULL(inner_conn)) {
    ret = OB_ERR_UNEXPECTED;
    LOG_ERROR("inner_conn is nullptr", K(ret), K(sql));
  } else if (OB_FAIL(inner_conn->execute_read(sql.ptr(), res))) {
  }
  return ret;
}

int ObInnerConnectionLockRuntime::do_obj_lock_(const ObLockRequest &arg,
    const obcall::ObInnerSQLTransmitArg::InnerSQLOperationType operation_type,
    observer::ObInnerSQLConnection *conn,
    observer::ObInnerSQLResult &res)
{
  int ret = OB_SUCCESS;
  transaction::ObTxDesc *tx_desc = nullptr;

  if (OB_ISNULL(conn)) {
    ret = OB_INVALID_ARGUMENT;
    LOG_WARN("Invalid conn", KR(ret));
  } else if (OB_ISNULL(tx_desc = conn->get_session().get_tx_desc())) {
    ret = OB_INVALID_ARGUMENT;
    LOG_WARN("Invalid tx_desc");
  } else {
    transaction::ObTxParam tx_param;
    tx_param.access_mode_ = transaction::ObTxAccessMode::RW;
    tx_param.isolation_ = conn->get_session().get_tx_isolation();
    conn->get_session().get_tx_timeout(tx_param.timeout_us_);
    tx_param.lock_timeout_us_ = conn->get_session().get_trx_lock_timeout();

    SERVER_MODULE_SCOPE {
      if (OB_FAIL(handle_request_by_operation_type_(*tx_desc, tx_param, arg, operation_type))) {
      }
      if (OB_SUCC(ret) && OB_FAIL(res.close())) {
        LOG_WARN("close result set failed", K(ret));
      }
    } // MTL_SWITCH
  } // else
  return ret;
}

int ObInnerConnectionLockRuntime::request_lock_(const ObLockRequest &arg,
    const obcall::ObInnerSQLTransmitArg::InnerSQLOperationType operation_type,
    observer::ObInnerSQLConnection *conn)
{
  int ret = OB_SUCCESS;
  observer::ObReqTimeGuard req_timeinfo_guard;

  SMART_VAR(ObInnerSQLResult, res, conn->get_session(),
            conn->get_sql_engine()->get_plan_cache_access_service(),
            conn->is_inner_session())
  {
    if (!conn->is_in_trans()) {
      ret = OB_ERR_UNEXPECTED;
      LOG_WARN("inner conn must be already in trans", K(ret));
    } else if (OB_FAIL(res.init())) {
    } else if (OB_FAIL(do_obj_lock_(arg, operation_type, conn, res))) {
    }
  }

  return ret;
}

// for version 4.0
int ObInnerConnectionLockRuntime::request_lock_(const uint64_t table_id, // as obj_id when lock_obj
    const ObTabletID tablet_id, //just used when lock_tablet
    const ObTableLockMode lock_mode,
    const int64_t timeout_us,
    const obcall::ObInnerSQLTransmitArg::InnerSQLOperationType operation_type,
    observer::ObInnerSQLConnection *conn)
{
  int ret = OB_SUCCESS;
  observer::ObReqTimeGuard req_timeinfo_guard;

  SMART_VAR(ObInnerSQLResult, res, conn->get_session(),
            conn->get_sql_engine()->get_plan_cache_access_service(),
            conn->is_inner_session())
  {
    if (!conn->is_in_trans()) {
      ret = OB_ERR_UNEXPECTED;
      LOG_WARN("inner conn must be already in trans", K(ret));
    } else if (OB_FAIL(res.init())) {
    } else {
        // we can safely rewrite the argument here, because it is only used local.
        ObLockRequest *lock_arg = nullptr;
        ObLockTableRequest lock_table_arg;
        ObLockTabletRequest lock_tablet_arg;
        switch (operation_type) {
        case ObInnerSQLTransmitArg::OPERATION_TYPE_LOCK_TABLE: {
          lock_arg = &lock_table_arg;
          lock_table_arg.owner_id_.set_default();
          lock_table_arg.lock_mode_ = lock_mode;
          lock_table_arg.op_type_ = IN_TRANS_COMMON_LOCK;
          lock_table_arg.timeout_us_ = timeout_us;
          lock_table_arg.table_id_ = table_id;
          break;
        }
        case ObInnerSQLTransmitArg::OPERATION_TYPE_LOCK_TABLET: {
          lock_arg = &lock_tablet_arg;
          lock_tablet_arg.owner_id_.set_default();
          lock_tablet_arg.lock_mode_ = lock_mode;
          lock_tablet_arg.op_type_ = IN_TRANS_COMMON_LOCK;
          lock_tablet_arg.timeout_us_ = timeout_us;
          lock_tablet_arg.table_id_ = table_id;
          lock_tablet_arg.tablet_id_ = tablet_id;
          break;
        }
        default:
          LOG_WARN("operation_type is not expected", K(operation_type));
          ret = OB_ERR_UNEXPECTED;
        }
        if (OB_SUCC(ret) && OB_FAIL(do_obj_lock_(*lock_arg, operation_type, conn, res))) {
          LOG_WARN("close result set failed", K(ret));
        }
    }
  }

  return ret;
}

int ObInnerConnectionLockRuntime::handle_request_by_operation_type_(
  ObTxDesc &tx_desc,
  const ObTxParam &tx_param,
  const ObLockRequest &arg,
  const obcall::ObInnerSQLTransmitArg::InnerSQLOperationType operation_type)
{
  int ret = OB_SUCCESS;
  switch (operation_type) {
  case ObInnerSQLTransmitArg::OPERATION_TYPE_LOCK_TABLE: {
    CONVERT_TYPE_AND_DO_LOCK(ObLockTableRequest, arg, tx_desc, tx_param);
  }
  case ObInnerSQLTransmitArg::OPERATION_TYPE_UNLOCK_TABLE: {
    CONVERT_TYPE_AND_DO_UNLOCK(ObUnLockTableRequest, arg, tx_desc, tx_param);
  }
  case ObInnerSQLTransmitArg::OPERATION_TYPE_LOCK_TABLET: {
    CONVERT_TYPE_AND_DO_LOCK(ObLockTabletsRequest, arg, tx_desc, tx_param);
  }
  case ObInnerSQLTransmitArg::OPERATION_TYPE_UNLOCK_TABLET: {
    CONVERT_TYPE_AND_DO_UNLOCK(ObUnLockTabletsRequest, arg, tx_desc, tx_param);
  }
  case ObInnerSQLTransmitArg::OPERATION_TYPE_LOCK_PART:
  case ObInnerSQLTransmitArg::OPERATION_TYPE_LOCK_SUBPART: {
    CONVERT_TYPE_AND_DO_LOCK(ObLockPartitionRequest, arg, tx_desc, tx_param);
  }
  case ObInnerSQLTransmitArg::OPERATION_TYPE_UNLOCK_PART:
  case ObInnerSQLTransmitArg::OPERATION_TYPE_UNLOCK_SUBPART: {
    CONVERT_TYPE_AND_DO_UNLOCK(ObUnLockPartitionRequest, arg, tx_desc, tx_param);
  }
  case ObInnerSQLTransmitArg::OPERATION_TYPE_LOCK_OBJ: {
    const ObLockObjRequest &lock_arg = static_cast<const ObLockObjRequest &>(arg);
    ObLockObjsRequest new_lock_arg;
    if (OB_FAIL(new_lock_arg.assign(lock_arg))) {
    } else {
      CONVERT_TYPE_AND_DO_LOCK(ObLockObjsRequest, new_lock_arg, tx_desc, tx_param);
    }
  }
  case ObInnerSQLTransmitArg::OPERATION_TYPE_LOCK_OBJS: {
    CONVERT_TYPE_AND_DO_LOCK(ObLockObjsRequest, arg, tx_desc, tx_param);
  }
  case ObInnerSQLTransmitArg::OPERATION_TYPE_UNLOCK_OBJ: {
    const ObUnLockObjRequest &lock_arg = static_cast<const ObUnLockObjRequest &>(arg);
    ObUnLockObjsRequest new_lock_arg;
    if (OB_FAIL(new_lock_arg.assign(lock_arg))) {
    } else {
      CONVERT_TYPE_AND_DO_UNLOCK(ObUnLockObjsRequest, new_lock_arg, tx_desc, tx_param);
    }
  }
  case ObInnerSQLTransmitArg::OPERATION_TYPE_UNLOCK_OBJS: {
    CONVERT_TYPE_AND_DO_UNLOCK(ObUnLockObjsRequest, arg, tx_desc, tx_param);
  }
  case ObInnerSQLTransmitArg::OPERATION_TYPE_LOCK_ALONE_TABLET: {
    CONVERT_TYPE_AND_DO_LOCK(ObLockAloneTabletRequest, arg, tx_desc, tx_param);
  }
  case ObInnerSQLTransmitArg::OPERATION_TYPE_UNLOCK_ALONE_TABLET: {
    CONVERT_TYPE_AND_DO_UNLOCK(ObUnLockAloneTabletRequest, arg, tx_desc, tx_param);
  }
  default: {
    LOG_WARN("operation_type is not expected", K(operation_type));
    ret = OB_ERR_UNEXPECTED;
  }
  }
  return ret;
}


ObIInnerConnectionLockRuntime *inner_connection_lock_runtime_instance()
{
  static ObInnerConnectionLockRuntime runtime;
  return &runtime;
}

} // tablelock
} // transaction

namespace observer
{
transaction::tablelock::ObIInnerConnectionLockRuntime *
ObServer::inner_connection_lock_runtime()
{
  return transaction::tablelock::inner_connection_lock_runtime_instance();
}
} // namespace observer

} // oceanbase
