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

#include "ob_lock_inner_connection_util.h"
#include "share/rc/ob_module_provider.h"
#include "observer/ob_inner_sql_connection_pool.h"
#include "observer/ob_inner_sql_result.h"
#include "storage/tablelock/ob_table_lock_service.h"
#include "sql/session/ob_sql_session_info.h"


using namespace oceanbase::observer;
namespace oceanbase
{
namespace transaction
{
namespace tablelock
{

#define CONVERT_TYPE_AND_DO_LOCK(T, arg, tx_desc, tx_param)                    \
  const T lock_req = static_cast<const T &>(arg);                              \
  if (OB_FAIL(share::g_mp->table_lock_service()->lock(tx_desc, tx_param, lock_req))) { \
    LOG_WARN("lock failed", K(ret), K(lock_req));                              \
  }                                                                            \
  break;

#define CONVERT_TYPE_AND_DO_UNLOCK(T, arg, tx_desc, tx_param)                      \
  const T lock_req = static_cast<const T &>(arg);                                  \
  T &unlock_req = const_cast<T &>(lock_req);                                       \
  unlock_req.set_to_unlock_type();                                                 \
  if (OB_FAIL(share::g_mp->table_lock_service()->unlock(tx_desc, tx_param, unlock_req))) { \
    LOG_WARN("unlock failed", K(ret), K(unlock_req));                              \
  }                                                                                \
  break;

int ObInnerConnectionLockUtil::lock_table(
    const uint64_t table_id,
    const ObTableLockMode lock_mode,
    const int64_t timeout_us,
    observer::ObInnerSQLConnection *conn,
    const ObTableLockOwnerID owner_id,
    const ObTableLockPriority lock_priority)
{
  int ret = OB_SUCCESS;
  ObLockTableRequest lock_arg;
  lock_arg.owner_id_ = owner_id;
  lock_arg.lock_mode_ = lock_mode;
  lock_arg.op_type_ = IN_TRANS_COMMON_LOCK;
  lock_arg.timeout_us_ = timeout_us;
  lock_arg.table_id_ = table_id;
  lock_arg.lock_priority_ = lock_priority;

  ret = request_lock_(lock_arg,
                      LockOperationType::LOCK_TABLE,
                      conn);
  return ret;
}

int ObInnerConnectionLockUtil::lock_table(
    const ObLockTableRequest &arg,
    observer::ObInnerSQLConnection *conn)
{
  return request_lock_(arg, LockOperationType::LOCK_TABLE, conn);
}

int ObInnerConnectionLockUtil::unlock_table(
    const ObUnLockTableRequest &arg,
    observer::ObInnerSQLConnection *conn)
{
  int ret = OB_SUCCESS;
  if (OB_UNLIKELY(ObTableLockOpType::OUT_TRANS_UNLOCK != arg.op_type_)) {
    ret = OB_INVALID_ARGUMENT;
    LOG_WARN("only OUT_TRANS_LOCK should unlock.", K(ret), K(arg));
  } else {
    ret = request_lock_(arg, LockOperationType::UNLOCK_TABLE, conn);
  }
  return ret;
}





int ObInnerConnectionLockUtil::lock_tablet(
    const uint64_t table_id,
    const ObTabletID tablet_id,
    const ObTableLockMode lock_mode,
    const int64_t timeout_us,
    observer::ObInnerSQLConnection *conn)
{
  int ret = OB_SUCCESS;
  ObLockTabletsRequest lock_arg;
  lock_arg.owner_id_.set_default();
  lock_arg.lock_mode_ = lock_mode;
  lock_arg.op_type_ = IN_TRANS_COMMON_LOCK;
  lock_arg.timeout_us_ = timeout_us;
  lock_arg.table_id_ = table_id;
  if (OB_FAIL(lock_arg.tablet_ids_.push_back(tablet_id))) {
    LOG_WARN("add tablet id failed", K(ret), K(tablet_id));
  } else if (OB_FAIL(request_lock_(lock_arg, LockOperationType::LOCK_TABLET, conn))) {
    LOG_WARN("request lock for tablet failed", K(ret), K(lock_arg));
  }
  return ret;
}

int ObInnerConnectionLockUtil::lock_tablet(
    const uint64_t table_id,
    const ObIArray<ObTabletID> &tablet_ids,
    const ObTableLockMode lock_mode,
    const int64_t timeout_us,
    observer::ObInnerSQLConnection *conn)
{
  int ret = OB_SUCCESS;
  ObLockTabletsRequest lock_arg;
  lock_arg.owner_id_.set_default();
  lock_arg.lock_mode_ = lock_mode;
  lock_arg.op_type_ = IN_TRANS_COMMON_LOCK;
  lock_arg.timeout_us_ = timeout_us;
  lock_arg.table_id_ = table_id;

  if (OB_FAIL(lock_arg.tablet_ids_.assign(tablet_ids))) {
    LOG_WARN("assign tablet id failed", K(ret), K(tablet_ids));
  } else if (OB_FAIL(request_lock_(lock_arg, LockOperationType::LOCK_TABLET, conn))) {
    LOG_WARN("request lock for tablets failed", K(ret), K(lock_arg));
  }
  return ret;
}



int ObInnerConnectionLockUtil::lock_tablet(
    const ObLockAloneTabletRequest &arg,
    observer::ObInnerSQLConnection *conn)
{
  return request_lock_(arg, LockOperationType::LOCK_ALONE_TABLET, conn);
}

int ObInnerConnectionLockUtil::unlock_tablet(
    const ObUnLockAloneTabletRequest &arg,
    observer::ObInnerSQLConnection *conn)
{
  int ret = OB_SUCCESS;
  if (OB_UNLIKELY(ObTableLockOpType::OUT_TRANS_UNLOCK != arg.op_type_)) {
    ret = OB_INVALID_ARGUMENT;
    LOG_WARN("only OUT_TRANS_LOCK should unlock.", K(ret), K(arg));
  } else {
    ret = request_lock_(arg, LockOperationType::UNLOCK_ALONE_TABLET, conn);
  }
  return ret;
}

int ObInnerConnectionLockUtil::lock_obj(
    const ObLockObjRequest &arg,
    observer::ObInnerSQLConnection *conn)
{
  return request_lock_(arg, LockOperationType::LOCK_OBJ, conn);
}

int ObInnerConnectionLockUtil::unlock_obj(
    const ObUnLockObjRequest &arg,
    observer::ObInnerSQLConnection *conn)
{
  int ret = OB_SUCCESS;
  if (OB_UNLIKELY(ObTableLockOpType::OUT_TRANS_UNLOCK != arg.op_type_)) {
    ret = OB_INVALID_ARGUMENT;
    LOG_WARN("only OUT_TRANS_LOCK should unlock.", K(ret), K(arg));
  } else {
    ret = request_lock_(arg, LockOperationType::UNLOCK_OBJ, conn);
  }
  return ret;
}

int ObInnerConnectionLockUtil::lock_obj(
    const ObLockObjsRequest &arg,
    observer::ObInnerSQLConnection *conn)
{
  return request_lock_(arg, LockOperationType::LOCK_OBJS, conn);
}

int ObInnerConnectionLockUtil::unlock_obj(
    const ObUnLockObjsRequest &arg,
    observer::ObInnerSQLConnection *conn)
{
  int ret = OB_SUCCESS;
  if (OB_UNLIKELY(ObTableLockOpType::OUT_TRANS_UNLOCK != arg.op_type_)) {
    ret = OB_INVALID_ARGUMENT;
    LOG_WARN("only OUT_TRANS_LOCK should unlock.", K(ret), K(arg));
  } else {
    ret = request_lock_(arg, LockOperationType::UNLOCK_OBJS, conn);
  }
  return ret;
}

int ObInnerConnectionLockUtil::replace_lock(
      const ObReplaceLockRequest &req,
      observer::ObInnerSQLConnection *conn)
{
  int ret = OB_SUCCESS;
  observer::ObReqTimeGuard req_timeinfo_guard;

  SMART_VAR(ObInnerSQLResult, res, conn->get_session(), conn->is_inner_session())
  {

    if (OB_SUCC(ret)) {
      if (!conn->is_in_trans()) {
        ret = OB_ERR_UNEXPECTED;
        LOG_WARN("inner conn must be already in trans", K(ret));
      } else if (OB_FAIL(res.init())) {
        LOG_WARN("init result set", K(ret));
      } else {
        if (OB_FAIL(replace_lock_(req, conn, res))) {
          LOG_WARN("replace lock failed", KR(ret), K(req));
        }
      }
    }
  }

  return ret;
}

int ObInnerConnectionLockUtil::replace_lock(const ObReplaceAllLocksRequest &req,
                                            observer::ObInnerSQLConnection *conn)
{
  int ret = OB_SUCCESS;
  observer::ObReqTimeGuard req_timeinfo_guard;

  SMART_VAR(ObInnerSQLResult, res, conn->get_session(), conn->is_inner_session())
  {

    if (OB_SUCC(ret)) {
      if (!conn->is_in_trans()) {
        ret = OB_ERR_UNEXPECTED;
        LOG_WARN("inner conn must be already in trans", K(ret));
      } else if (OB_FAIL(res.init())) {
        LOG_WARN("init result set", K(ret));
      } else {
        if (OB_FAIL(replace_lock_(req, conn, res))) {
          LOG_WARN("replace lock failed", KR(ret), K(req));
        }
      }
    }
  }
  return ret;
}

int ObInnerConnectionLockUtil::replace_lock_(const ObReplaceLockRequest &req,
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
      if (OB_FAIL(share::g_mp->table_lock_service()->replace_lock(*tx_desc, tx_param, req))) {
        LOG_WARN("replace lock failed", K(ret), K(req));
      } else if (OB_FAIL(res.close())) {
        LOG_WARN("close result set failed", K(ret));
      }
    }
  }
  return ret;
}

int ObInnerConnectionLockUtil::replace_lock_(const ObReplaceAllLocksRequest &req,
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
      if (OB_FAIL(share::g_mp->table_lock_service()->replace_lock(*tx_desc, tx_param, req))) {
        LOG_WARN("replace lock failed", K(ret), K(req));
      } else if (OB_FAIL(res.close())) {
        LOG_WARN("close result set failed", K(ret));
      }
    }
  }
  return ret;
}

int ObInnerConnectionLockUtil::create_inner_conn(sql::ObSQLSessionInfo *session_info,
                                                 common::ObMySQLProxy *sql_proxy,
                                                 observer::ObInnerSQLConnection *&inner_conn)
{
  int ret = OB_SUCCESS;
  observer::ObInnerSQLConnectionPool *pool = nullptr;
  common::sqlclient::ObISQLConnection *conn = nullptr;

  if (OB_ISNULL(session_info) || OB_ISNULL(sql_proxy)) {
    ret = OB_NOT_INIT;
    LOG_WARN("session or sql_proxy is NULL", KP(session_info), KP(sql_proxy));
  } else if (OB_NOT_NULL(inner_conn = static_cast<observer::ObInnerSQLConnection *>(session_info->get_inner_conn()))) {
    LOG_INFO("session has had inner connection, no need to create again", KPC(session_info));
  } else if (OB_ISNULL(pool = static_cast<observer::ObInnerSQLConnectionPool *>(sql_proxy->get_pool()))) {
    ret = OB_NOT_INIT;
    LOG_WARN("connection pool is NULL", K(ret));
  } else if (common::sqlclient::INNER_POOL != pool->get_type()) {
    LOG_WARN("connection pool type is not inner", K(ret), K(pool->get_type()));
    // NOTICE: the pool acquire no longer needs a compatibility-mode flag.
  } else if (OB_FAIL(pool->acquire(session_info, conn))) {
    LOG_WARN("acquire connection from inner sql connection pool failed", KR(ret), KPC(session_info));
  } else if (OB_ISNULL(conn)) {
    ret = OB_ERR_UNEXPECTED;
    LOG_ERROR("acquire new connection but it's null", KR(ret), KPC(session_info));
  } else {
    inner_conn = static_cast<observer::ObInnerSQLConnection *>(conn);
  }

  return ret;
}

int ObInnerConnectionLockUtil::execute_write_sql(observer::ObInnerSQLConnection *conn,
                                                 const ObSqlString &sql,
                                                 int64_t &affected_rows)
{
  int ret = OB_SUCCESS;
  if (OB_ISNULL(conn)) {
    ret = OB_ERR_UNEXPECTED;
    LOG_ERROR("inner_conn is nullptr", K(ret), K(sql));
  } else if (OB_FAIL(conn->execute_write(sql.ptr(), affected_rows))) {
    LOG_WARN("execute write sql failed", K(ret), K(sql));
  }
  return ret;
}

int ObInnerConnectionLockUtil::execute_read_sql(observer::ObInnerSQLConnection *conn,
                                                const ObSqlString &sql,
                                                ObISQLClient::ReadResult &res)
{
  int ret = OB_SUCCESS;
  if (OB_ISNULL(conn)) {
    ret = OB_ERR_UNEXPECTED;
    LOG_ERROR("inner_conn is nullptr", K(ret), K(sql));
  } else if (OB_FAIL(conn->execute_read(sql.ptr(), res))) {
    LOG_WARN("execute read sql failed", K(ret), K(sql));
  }
  return ret;
}

int ObInnerConnectionLockUtil::build_tx_param(sql::ObSQLSessionInfo *session_info, ObTxParam &tx_param, const bool *readonly)
{
  int ret = OB_SUCCESS;
  int64_t tx_timeout_us = 0;
  OX (
    session_info->get_tx_timeout(tx_timeout_us);

    tx_param.timeout_us_ = tx_timeout_us;
    tx_param.lock_timeout_us_ = session_info->get_trx_lock_timeout();
    bool ro = OB_NOT_NULL(readonly) ? *readonly : session_info->get_tx_read_only();
    tx_param.access_mode_ = ro ? ObTxAccessMode::RD_ONLY : ObTxAccessMode::RW;
    tx_param.isolation_ = session_info->get_tx_isolation();
  )

  return ret;
}

int ObInnerConnectionLockUtil::do_obj_lock_(const ObLockRequest &arg,
    const LockOperationType operation_type,
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
        LOG_WARN("handle request by operation_type failed", K(tx_param), K(arg), K(operation_type));
      }
      if (OB_SUCC(ret) && OB_FAIL(res.close())) {
        LOG_WARN("close result set failed", K(ret));
      }
    }
  } // else
  return ret;
}

int ObInnerConnectionLockUtil::request_lock_(const ObLockRequest &arg,
    const LockOperationType operation_type,
    observer::ObInnerSQLConnection *conn)
{
  int ret = OB_SUCCESS;
  observer::ObReqTimeGuard req_timeinfo_guard;

  SMART_VAR(ObInnerSQLResult, res, conn->get_session(), conn->is_inner_session())
  {

    if (OB_SUCC(ret)) {
      if (!conn->is_in_trans()) {
        ret = OB_ERR_UNEXPECTED;
        LOG_WARN("inner conn must be already in trans", K(ret));
      } else if (OB_FAIL(res.init())) {
        LOG_WARN("init result set", K(ret));
      } else {
        if (OB_FAIL(do_obj_lock_(arg, operation_type, conn, res))) {
          LOG_WARN("do obj lock failed", KR(ret), K(operation_type), K(arg));
        }
      }
    }
  }

  return ret;
}

int ObInnerConnectionLockUtil::handle_request_by_operation_type_(
  ObTxDesc &tx_desc,
  const ObTxParam &tx_param,
  const ObLockRequest &arg,
  const LockOperationType operation_type)
{
  int ret = OB_SUCCESS;
  switch (operation_type) {
  case LockOperationType::LOCK_TABLE: {
    CONVERT_TYPE_AND_DO_LOCK(ObLockTableRequest, arg, tx_desc, tx_param);
  }
  case LockOperationType::UNLOCK_TABLE: {
    CONVERT_TYPE_AND_DO_UNLOCK(ObUnLockTableRequest, arg, tx_desc, tx_param);
  }
  case LockOperationType::LOCK_TABLET: {
    CONVERT_TYPE_AND_DO_LOCK(ObLockTabletsRequest, arg, tx_desc, tx_param);
  }
  case LockOperationType::LOCK_OBJ: {
    const ObLockObjRequest &lock_arg = static_cast<const ObLockObjRequest &>(arg);
    ObLockObjsRequest new_lock_arg;
    if (OB_FAIL(new_lock_arg.assign(lock_arg))) {
      LOG_WARN("assign ObLockObjsRequest failed", K(ret), K(lock_arg));
    } else {
      CONVERT_TYPE_AND_DO_LOCK(ObLockObjsRequest, new_lock_arg, tx_desc, tx_param);
    }
  }
  case LockOperationType::LOCK_OBJS: {
    CONVERT_TYPE_AND_DO_LOCK(ObLockObjsRequest, arg, tx_desc, tx_param);
  }
  case LockOperationType::UNLOCK_OBJ: {
    const ObUnLockObjRequest &lock_arg = static_cast<const ObUnLockObjRequest &>(arg);
    ObUnLockObjsRequest new_lock_arg;
    if (OB_FAIL(new_lock_arg.assign(lock_arg))) {
      LOG_WARN("assign ObLockObjsRequest failed", K(ret), K(lock_arg));
    } else {
      CONVERT_TYPE_AND_DO_UNLOCK(ObUnLockObjsRequest, new_lock_arg, tx_desc, tx_param);
    }
  }
  case LockOperationType::UNLOCK_OBJS: {
    CONVERT_TYPE_AND_DO_UNLOCK(ObUnLockObjsRequest, arg, tx_desc, tx_param);
  }
  case LockOperationType::LOCK_ALONE_TABLET: {
    CONVERT_TYPE_AND_DO_LOCK(ObLockAloneTabletRequest, arg, tx_desc, tx_param);
  }
  case LockOperationType::UNLOCK_ALONE_TABLET: {
    CONVERT_TYPE_AND_DO_UNLOCK(ObUnLockAloneTabletRequest, arg, tx_desc, tx_param);
  }
  default: {
    LOG_WARN("operation_type is not expected", K(operation_type));
    ret = OB_ERR_UNEXPECTED;
  }
  }
  return ret;
}


} // tablelock
} // transaction
} // oceanbase
