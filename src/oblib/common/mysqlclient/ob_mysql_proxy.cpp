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

#define USING_LOG_PREFIX COMMON_MYSQLP
#include "common/mysqlclient/ob_isql_connection.h"
#include "common/mysqlclient/ob_mysql_proxy.h"
#include "common/sql_mode/ob_sql_mode_utils.h"
using namespace oceanbase::common;
using namespace oceanbase::common::sqlclient;

namespace oceanbase
{
namespace common
{

int OB_WEAK_SYMBOL create_inner_sql_connection_for_proxy(
    bool is_ddl,
    int32_t group_id,
    sqlclient::ObISQLConnectionGuard &conn)
{
  UNUSEDx(is_ddl, group_id);
  conn.reset();
  return OB_NOT_SUPPORTED;
}

} // end namespace common
} // end namespace oceanbase

OB_SERIALIZE_MEMBER(ObSessionDDLInfo, ddl_info_.ddl_info_, // FARM COMPAT WHITELIST
                                      session_id_);

ObCommonSqlProxy::ObCommonSqlProxy()
    : inited_(false),
      is_ddl_(false),
      stopped_(false),
      connection_factory_(nullptr)
{
}

ObCommonSqlProxy::~ObCommonSqlProxy()
{
}

int ObCommonSqlProxy::init(
    const bool is_ddl,
    InnerSqlConnectionFactory connection_factory)
{
  int ret = OB_SUCCESS;
  if (is_inited()) {
    ret = OB_INIT_TWICE;
    LOG_WARN("init twice", K(ret));
  } else {
    inited_ = true;
    is_ddl_ = is_ddl;
    connection_factory_ = connection_factory;
  }
  return ret;
}

void ObCommonSqlProxy::operator=(const ObCommonSqlProxy &o)
{
  this->ObISQLClient::operator=(o);
  inited_ = o.inited_;
  is_ddl_ = o.is_ddl_;
  stopped_ = o.stopped_;
  connection_factory_ = o.connection_factory_;
}

int ObCommonSqlProxy::read(ReadResult &result, const char *sql, const int32_t group_id)
{
  int ret = OB_SUCCESS;
  ObISQLConnectionGuard conn;
  if (OB_FAIL(acquire(conn, group_id))) {
  } else if (OB_FAIL(read(conn.get_ptr(), result, sql))) {
  }
  return ret;
}

int ObCommonSqlProxy::read(ReadResult &result, const char *sql, const ObSessionParam *session_param, int64_t user_set_timeout)
{
  int ret = OB_SUCCESS;
  ObISQLConnectionGuard conn;
  if (OB_FAIL(acquire(conn, 0/*group_id*/))) {
  } else if (nullptr != session_param) {
    conn->set_ddl_info(&session_param->ddl_info_);
    if (nullptr != session_param->sql_mode_) {
      if (OB_FAIL(conn->set_session_variable("sql_mode", *session_param->sql_mode_))) {
      }
    }
    if (OB_SUCC(ret) && nullptr != session_param && nullptr != session_param->tz_info_wrap_) {
      if (OB_FAIL(conn->set_tz_info_wrap(*session_param->tz_info_wrap_))) {
      }
    }

  }

  if (OB_FAIL(ret)) {
  } else if (FALSE_IT(conn->set_user_timeout(user_set_timeout))) {
  } else if (OB_FAIL(read(conn.get_ptr(), result, sql))) {
  }
  return ret;
}

int ObCommonSqlProxy::read(ObISQLConnection *conn, ReadResult &result, const char *sql)
{
  int ret = OB_SUCCESS;
  const int64_t start = ::oceanbase::common::ObTimeUtility::current_time();
  result.reset();
  if (OB_ISNULL(sql) || OB_ISNULL(conn)) {
    ret = OB_INVALID_ARGUMENT;
    LOG_WARN("empty sql or null conn", K(ret), KP(sql), KP(conn));
  } else if (stopped_) { // check stop state again after connection acquired
    ret = OB_INACTIVE_SQL_CLIENT;
    LOG_WARN("sql proxy stopped", K(ret), KCSTRING(sql));
  } else {
    if (OB_FAIL(conn->execute_read(sql, result))) {
    }
  }
  return ret;
}

int ObCommonSqlProxy::write(const char *sql, const int32_t group_id, int64_t &affected_rows)
{
  int ret = OB_SUCCESS;
  int64_t start = ::oceanbase::common::ObTimeUtility::current_time();
  ObISQLConnectionGuard conn;
  if (OB_ISNULL(sql)) {
    ret = OB_INVALID_ARGUMENT;
    LOG_WARN("empty sql");
  } else if (OB_FAIL(acquire(conn, group_id))) {
  } else if (!conn.is_valid()) {
    ret = OB_INNER_STAT_ERROR;
    LOG_WARN("connection can not be NULL");
  } else if (stopped_) { // check stop state again after connection acquired
    ret = OB_INACTIVE_SQL_CLIENT;
    LOG_WARN("sql proxy stopped", K(ret), KCSTRING(sql));
  } else {
    if (OB_FAIL(conn->execute_write(sql, affected_rows))) {
    }
  }
  return ret;
}

int ObCommonSqlProxy::write(const ObString sql,
                        int64_t &affected_rows,
                        const ObSessionParam *param /* = nullptr*/)
{
  int ret = OB_SUCCESS;
  bool is_user_sql = false;
  int64_t start = ::oceanbase::common::ObTimeUtility::current_time();
  ObISQLConnectionGuard conn;
  if (OB_UNLIKELY(sql.empty())) {
    ret = OB_INVALID_ARGUMENT;
    LOG_WARN("empty sql");
  } else if (OB_FAIL(acquire(conn, 0/*group_id*/))) {
  } else if (!conn.is_valid()) {
    ret = OB_INNER_STAT_ERROR;
    LOG_WARN("connection can not be NULL");
  } else if (stopped_) { // check stop state again after connection acquired
    ret = OB_INACTIVE_SQL_CLIENT;
    LOG_WARN("sql proxy stopped", K(ret), K(sql));
  }
  if (OB_SUCC(ret) && nullptr != param) {
    conn->set_is_load_data_exec(param->is_load_data_exec_);
    conn->set_ob_enable_pl_cache(param->enable_pl_cache_);
    if (param->is_load_data_exec_) {
      is_user_sql = true;
    }
    if (OB_FAIL(conn->set_ddl_info(&param->ddl_info_))) {
    }
    if (param->ddl_info_.is_ddl()) {
    }
    if (!param->secure_file_priv_.empty()) {
      conn->set_session_variable("secure_file_priv", param->secure_file_priv_);
    }
  }
  if (OB_SUCC(ret) && nullptr != param && nullptr != param->sql_mode_) {
    if (OB_FAIL(conn->set_session_variable("sql_mode", *param->sql_mode_))) {
    }
  }
  if (OB_SUCC(ret) && nullptr != param && nullptr != param->tz_info_wrap_) {
    if (OB_FAIL(conn->set_tz_info_wrap(*param->tz_info_wrap_))) {
    }
  }
  if (OB_SUCC(ret)) {
    if (OB_FAIL(conn->execute_write(sql, affected_rows, is_user_sql))) {
    } else {
    }
  }
  return ret;
}


int ObCommonSqlProxy::escape(const char *from, const int64_t from_size,
    char *to, const int64_t to_size, int64_t &out_size)
{
  int ret = OB_SUCCESS;
  if (!is_inited()) {
    ret = OB_NOT_INIT;
    LOG_WARN("mysql proxy not inited");
  } else if (NULL != from && from_size > 0) {
    if (to_size < from_size * 2) {
      ret = OB_BUF_NOT_ENOUGH;
      LOG_WARN("string buffer not enough", K(ret), K(from_size), K(to_size));
    } else if (OB_ISNULL(to)) {
      ret = OB_INVALID_ARGUMENT;
      LOG_WARN("to buffer is NULL", K(ret), KP(to), KP(from), K(from_size));
    } else {
      MEMCPY(to, from, from_size);
      out_size = from_size;
    }
  } else {
    out_size = 0;
  }
  return ret;
}

int ObCommonSqlProxy::acquire_connection(
    ObISQLConnectionGuard &conn,
    const int32_t group_id)
{
  int ret = OB_SUCCESS;
  conn.reset();
  if (!is_inited()) {
    ret = OB_NOT_INIT;
    LOG_WARN("mysql proxy not inited", K(ret));
  } else if (stopped_) {
    ret = OB_INACTIVE_SQL_CLIENT;
    LOG_WARN("sql proxy stopped", K(ret));
  } else if (nullptr != connection_factory_) {
    ret = connection_factory_(is_ddl_, group_id, conn);
  } else {
    ret = create_inner_sql_connection_for_proxy(is_ddl_, group_id, conn);
  }
  return ret;
}

int ObCommonSqlProxy::acquire(sqlclient::ObISQLConnectionGuard &conn, const int32_t group_id)
{
  int ret = OB_SUCCESS;
  if (!is_inited()) {
    ret = OB_NOT_INIT;
    LOG_WARN("mysql proxy not inited", K(ret));
  } else if (OB_FAIL(acquire_connection(conn, group_id))) {
  } else if (!conn.is_valid()) {
    ret = OB_ERR_UNEXPECTED;
    LOG_ERROR("connection must not be null", K(ret));
  }
  return ret;
}
