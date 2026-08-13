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

#define USING_LOG_PREFIX SERVER

#include "data_plane/vector/ob_vector_refresh_transaction.h"

#include "observer/ob_inner_sql_connection.h"
#include "query/session/ob_session_access.h"
#include "share/config/ob_server_config.h"
#include "share/ob_share_util.h"
#include "storage/tablelock/ob_lock_inner_connection_util.h"

namespace oceanbase
{
namespace data_plane
{
using namespace common;
using namespace common::sqlclient;
using namespace observer;
using namespace sql;
using namespace transaction::tablelock;

ObVectorRefreshTransaction::ObSessionParamSaved::ObSessionParamSaved()
  : session_info_(nullptr), is_inner_(false), autocommit_(false)
{
}

ObVectorRefreshTransaction::ObSessionParamSaved::~ObSessionParamSaved()
{
  int ret = OB_SUCCESS;
  if (nullptr != session_info_ && OB_FAIL(restore())) {
    LOG_WARN("fail to restore session param", KR(ret));
  }
}

int ObVectorRefreshTransaction::ObSessionParamSaved::save(
    ObSQLSessionInfo *session_info)
{
  int ret = OB_SUCCESS;
  if (OB_NOT_NULL(session_info_)) {
    ret = OB_ERR_UNEXPECTED;
    LOG_WARN("already save one session param", KR(ret), KP(session_info_),
             KP(session_info));
  } else if (OB_ISNULL(session_info)) {
    ret = OB_INVALID_ARGUMENT;
    LOG_WARN("invalid args", KR(ret), KP(session_info));
  } else {
    bool autocommit = false;
    if (OB_FAIL(query::ObSessionAccess::get_autocommit(
            session_info, autocommit))) {
    } else {
      session_info_ = session_info;
      is_inner_ = query::ObSessionAccess::is_inner(session_info);
      autocommit_ = autocommit;
      query::ObSessionAccess::set_inner_session(session_info);
      if (OB_FAIL(query::ObSessionAccess::set_autocommit(
              session_info, false))) {
      } else {
        query::ObSessionAccess::set_dummy_ddl_visibility(session_info, true);
      }
    }
  }
  return ret;
}

int ObVectorRefreshTransaction::ObSessionParamSaved::restore()
{
  int ret = OB_SUCCESS;
  if (OB_NOT_NULL(session_info_)) {
    if (is_inner_) {
      query::ObSessionAccess::set_inner_session(session_info_);
    } else {
      query::ObSessionAccess::set_user_session(session_info_);
    }
    if (OB_FAIL(query::ObSessionAccess::set_autocommit(
            session_info_, autocommit_))) {
    }
    query::ObSessionAccess::set_dummy_ddl_visibility(session_info_, false);
    session_info_ = nullptr;
  }
  return ret;
}

ObVectorRefreshTransaction::ObVectorRefreshTransaction()
  : in_transaction_(false)
{
}

ObVectorRefreshTransaction::~ObVectorRefreshTransaction()
{
  int ret = OB_SUCCESS;
  if (in_transaction_ && OB_FAIL(end(OB_SUCCESS == get_errno()))) {
    LOG_WARN("fail to end vector refresh transaction", KR(ret));
  }
}

int ObVectorRefreshTransaction::connect_(ObSQLSessionInfo *session_info,
                                         ObISQLClient *sql_client)
{
  int ret = OB_SUCCESS;
  if (OB_UNLIKELY(nullptr != sql_client_ || conn_.is_valid())) {
    ret = OB_INNER_STAT_ERROR;
    LOG_WARN("transaction can only be started once", KR(ret),
             K(sql_client_), K(conn_));
  } else if (OB_UNLIKELY(nullptr == session_info || nullptr == sql_client)) {
    ret = OB_INVALID_ARGUMENT;
    LOG_WARN("invalid args", KR(ret), KP(session_info), KP(sql_client));
  } else {
    if (OB_FAIL(
            ObInnerSQLConnection::
                create_spi_connection_with_external_session(
                    session_info, conn_))) {
    } else if (!conn_.is_valid()) {
      ret = OB_INNER_STAT_ERROR;
      LOG_WARN("connection can not be NULL", KR(ret));
    } else {
      sql_client_ = sql_client;
    }
  }
  return ret;
}

int ObVectorRefreshTransaction::start_transaction_()
{
  int ret = OB_SUCCESS;
  ObISQLConnection *connection = get_connection();
  if (OB_ISNULL(connection)) {
    ret = OB_INNER_STAT_ERROR;
    LOG_WARN("connection is NULL", KR(ret));
  } else if (OB_FAIL(connection->start_transaction(false))) {
  }
  if (OB_SUCCESS == get_errno()) {
    set_errno(ret);
  }
  return ret;
}

int ObVectorRefreshTransaction::end_transaction_(const bool commit)
{
  int ret = OB_SUCCESS;
  ObISQLConnection *connection = get_connection();
  if (OB_ISNULL(connection)) {
    ret = OB_INNER_STAT_ERROR;
    LOG_WARN("connection is NULL", KR(ret));
  } else if (commit) {
    if (OB_FAIL(connection->commit())) {
    }
  } else if (OB_FAIL(connection->rollback())) {
  }
  if (OB_SUCCESS == get_errno()) {
    set_errno(ret);
  }
  return ret;
}

int ObVectorRefreshTransaction::start(ObSQLSessionInfo *session_info,
                                      ObISQLClient *sql_client)
{
  int ret = OB_SUCCESS;
  if (OB_UNLIKELY(in_transaction_)) {
    ret = OB_ERR_UNEXPECTED;
    LOG_WARN("already in transaction", KR(ret));
  } else if (OB_UNLIKELY(nullptr == session_info || nullptr == sql_client)) {
    ret = OB_INVALID_ARGUMENT;
    LOG_WARN("invalid args", KR(ret), KP(session_info), KP(sql_client));
  } else if (OB_UNLIKELY(
                 query::ObSessionAccess::is_in_transaction(session_info))) {
    ret = OB_ERR_UNEXPECTED;
    LOG_WARN("unexpected session is in transaction", KR(ret));
  } else if (OB_FAIL(session_param_saved_.save(session_info))) {
  } else if (OB_FAIL(connect_(session_info, sql_client))) {
  } else if (OB_FAIL(start_transaction_())) {
  } else {
    in_transaction_ = true;
  }
  if (OB_FAIL(ret)) {
    int tmp_ret = OB_SUCCESS;
    close();
    if (OB_TMP_FAIL(session_param_saved_.restore())) {
    }
  }
  return ret;
}

int ObVectorRefreshTransaction::end(const bool commit)
{
  int ret = OB_SUCCESS;
  int tmp_ret = OB_SUCCESS;
  if (in_transaction_) {
    if (OB_FAIL(end_transaction_(commit))) {
    } else {
    }
    in_transaction_ = false;
  }
  close();
  if (OB_TMP_FAIL(session_param_saved_.restore())) {
    LOG_ERROR("fail to restore session param", KR(tmp_ret));
    ret = COVER_SUCC(tmp_ret);
  }
  return ret;
}

int ObVectorRefreshTransaction::lock_domain_table(
    const uint64_t domain_table_id, const bool try_lock)
{
  int ret = OB_SUCCESS;
  ObTableLockOwnerID owner_id;
  ObInnerSQLConnection *connection = nullptr;
  if (OB_UNLIKELY(!in_transaction_ || OB_INVALID_ID == domain_table_id)) {
    ret = OB_INVALID_ARGUMENT;
    LOG_WARN("invalid args", KR(ret), K(in_transaction_), K(domain_table_id));
  } else if (OB_FAIL(owner_id.convert_from_value(
                 ObLockOwnerType::DEFAULT_OWNER_TYPE, get_tid_cache()))) {
  } else if (OB_ISNULL(connection = static_cast<ObInnerSQLConnection *>(
                           get_connection()))) {
    ret = OB_ERR_UNEXPECTED;
    LOG_WARN("connection is NULL", KR(ret));
  } else {
    ObLockObjRequest lock_arg;
    lock_arg.obj_type_ = ObLockOBJType::OBJ_TYPE_REFRESH_VECTOR_INDEX;
    lock_arg.obj_id_ = domain_table_id;
    lock_arg.owner_id_ = owner_id;
    lock_arg.lock_mode_ = EXCLUSIVE;
    lock_arg.op_type_ = ObTableLockOpType::IN_TRANS_COMMON_LOCK;
    if (try_lock) {
      lock_arg.timeout_us_ = 0;
    } else {
      ObTimeoutCtx timeout_ctx;
      if (OB_FAIL(share::ObShareUtil::set_default_timeout_ctx(
              timeout_ctx, GCONF.internal_sql_execute_timeout))) {
      } else {
        lock_arg.timeout_us_ = timeout_ctx.get_timeout();
      }
    }
    if (OB_SUCC(ret)) {
      if (OB_FAIL(ObInnerConnectionLockUtil::lock_obj(lock_arg, connection))) {
      }
    }
  }
  return ret;
}

} // namespace data_plane
} // namespace oceanbase
