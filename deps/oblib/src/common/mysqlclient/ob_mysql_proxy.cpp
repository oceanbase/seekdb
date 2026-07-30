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
#include "common/mysqlclient/ob_isql_connection_pool.h"
#include "common/mysqlclient/ob_mysql_proxy.h"
#include "common/sql_mode/ob_sql_mode_utils.h"
using namespace oceanbase::common;
using namespace oceanbase::common::sqlclient;

OB_SERIALIZE_MEMBER(ObSessionDDLInfo, ddl_info_.ddl_info_, // FARM COMPAT WHITELIST
                                      session_id_);

ObCommonSqlProxy::ObCommonSqlProxy() : pool_(NULL)
{
}

ObCommonSqlProxy::~ObCommonSqlProxy()
{
}

int ObCommonSqlProxy::init(ObISQLConnectionPool *pool)
{
  int ret = OB_SUCCESS;
  if (is_inited()) {
    ret = OB_INIT_TWICE;
    LOG_WARN("init twice");
  } else if (NULL == pool) {
    ret = OB_INVALID_ARGUMENT;
    LOG_WARN("invalid argument");
  } else {
    pool_ = pool;
  }
  return ret;
}

void ObCommonSqlProxy::operator=(const ObCommonSqlProxy &o)
{
  this->ObISQLClient::operator=(o);
  active_ = o.active_;
  pool_ = o.pool_;
}

int ObCommonSqlProxy::read(ReadResult &result, const char *sql, const int32_t group_id)
{
  int ret = OB_SUCCESS;
  ObISQLConnection *conn = NULL;
  if (OB_FAIL(acquire(conn, group_id))) {
    LOG_WARN("acquire connection failed", K(ret), K(conn));
  } else if (OB_FAIL(read(conn, result, sql))) {
    LOG_WARN("read failed", K(ret));
  }
  close(conn, ret);
  return ret;
}

int ObCommonSqlProxy::read(ReadResult &result, const char *sql, const ObSessionParam *session_param, int64_t user_set_timeout)
{
  int ret = OB_SUCCESS;
  ObISQLConnection *conn = NULL;
  if (OB_FAIL(acquire(conn, 0/*group_id*/))) {
    LOG_WARN("acquire connection failed", K(ret), K(conn));
  } else if (nullptr != session_param) {
    conn->set_ddl_info(&session_param->ddl_info_);
    if (nullptr != session_param->sql_mode_) {
      if (OB_FAIL(conn->set_session_variable("sql_mode", *session_param->sql_mode_))) {
        LOG_WARN("set inner connection sql mode failed", K(ret));
      }
    }
    if (OB_SUCC(ret) && nullptr != session_param && nullptr != session_param->tz_info_wrap_) {
      if (OB_FAIL(conn->set_tz_info_wrap(*session_param->tz_info_wrap_))) {
        LOG_WARN("fail to set time zone info wrap", K(ret));
      }
    }

  }

  if (OB_FAIL(ret)) {
  } else if (FALSE_IT(conn->set_user_timeout(user_set_timeout))) {
  } else if (OB_FAIL(read(conn, result, sql))) {
    LOG_WARN("read failed", K(ret));
  }
  close(conn, ret);
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
  } else if (!is_active()) { // check client active after connection acquired
    ret = OB_INACTIVE_SQL_CLIENT;
    LOG_WARN("in active sql client", K(ret), KCSTRING(sql));
  } else {
    if (OB_FAIL(conn->execute_read(sql, result))) {
      LOG_WARN("query failed", K(ret), K(conn), K(start), KCSTRING(sql));
    }
  }
  LOG_TRACE("execute sql", KCSTRING(sql), K(ret));
  return ret;
}

int ObCommonSqlProxy::write(const char *sql, const int32_t group_id, int64_t &affected_rows)
{
  int ret = OB_SUCCESS;
  int64_t start = ::oceanbase::common::ObTimeUtility::current_time();
  ObISQLConnection *conn = NULL;
  if (OB_ISNULL(sql)) {
    ret = OB_INVALID_ARGUMENT;
    LOG_WARN("empty sql");
  } else if (OB_FAIL(acquire(conn, group_id))) {
    LOG_WARN("acquire connection failed", K(ret), K(conn));
  } else if (OB_ISNULL(conn)) {
    ret = OB_INNER_STAT_ERROR;
    LOG_WARN("connection can not be NULL");
  } else if (!is_active()) { // check client active after connection acquired
    ret = OB_INACTIVE_SQL_CLIENT;
    LOG_WARN("in active sql client", K(ret), KCSTRING(sql));
  } else {
    if (OB_FAIL(conn->execute_write(sql, affected_rows))) {
      LOG_WARN("execute sql failed", K(ret), K(conn), K(start), KCSTRING(sql));
    }
  }
  close(conn, ret);
  LOG_TRACE("execute sql", KCSTRING(sql), K(ret));
  return ret;
}

int ObCommonSqlProxy::write(const ObString sql,
                        int64_t &affected_rows,
                        const ObSessionParam *param /* = nullptr*/)
{
  int ret = OB_SUCCESS;
  bool is_user_sql = false;
  int64_t start = ::oceanbase::common::ObTimeUtility::current_time();
  ObISQLConnection *conn = NULL;
  if (OB_UNLIKELY(sql.empty())) {
    ret = OB_INVALID_ARGUMENT;
    LOG_WARN("empty sql");
  } else if (OB_FAIL(acquire(conn, 0/*group_id*/))) {
    LOG_WARN("acquire connection failed", K(ret), K(conn));
  } else if (OB_ISNULL(conn)) {
    ret = OB_INNER_STAT_ERROR;
    LOG_WARN("connection can not be NULL");
  } else if (!is_active()) { // check client active after connection acquired
    ret = OB_INACTIVE_SQL_CLIENT;
    LOG_WARN("in active sql client", K(ret), K(sql));
  }
  if (OB_SUCC(ret) && nullptr != param) {
    conn->set_is_load_data_exec(param->is_load_data_exec_);
    conn->set_ob_enable_pl_cache(param->enable_pl_cache_);
    if (param->is_load_data_exec_) {
      is_user_sql = true;
    }
    if (OB_FAIL(conn->set_ddl_info(&param->ddl_info_))) {
      LOG_WARN("fail to set ddl info", K(ret));
    }
    if (param->ddl_info_.is_ddl()) {
    }
    if (!param->secure_file_priv_.empty()) {
      conn->set_session_variable("secure_file_priv", param->secure_file_priv_);
    }
  }
  if (OB_SUCC(ret) && nullptr != param && nullptr != param->sql_mode_) {
    if (OB_FAIL(conn->set_session_variable("sql_mode", *param->sql_mode_))) {
      LOG_WARN("set inner connection sql mode failed", K(ret));
    }
  }
  if (OB_SUCC(ret) && nullptr != param && nullptr != param->tz_info_wrap_) {
    if (OB_FAIL(conn->set_tz_info_wrap(*param->tz_info_wrap_))) {
      LOG_WARN("fail to set time zone info wrap", K(ret));
    }
  }
  if (OB_SUCC(ret)) {
    if (OB_FAIL(conn->execute_write(sql, affected_rows, is_user_sql))) {
      LOG_WARN("execute sql failed", K(ret), K(conn), K(start), K(sql));
    } else {
      LOG_TRACE("execute sql successfully", K(sql));
    }
  }
  close(conn, ret);
  LOG_TRACE("execute sql", K(sql), K(ret));
  return ret;
}


int ObCommonSqlProxy::close(ObISQLConnection *conn, const int succ)
{
  int ret = OB_SUCCESS;
  if (conn != NULL) {
    ret = pool_->release(conn, OB_SUCCESS == succ);
    if (OB_FAIL(ret)) {
      LOG_WARN("release connection failed", K(ret), K(conn));
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
  } else if (OB_FAIL(pool_->escape(from, from_size, to, to_size, out_size))) {
    LOG_WARN("escape string failed",
        "from", ObString(from_size, from), K(from_size),
        "to", static_cast<void *>(to), K(to_size));
  }
  return ret;
}


int ObCommonSqlProxy::acquire(sqlclient::ObISQLConnection *&conn, const int32_t group_id)
{
  int ret = OB_SUCCESS;
  if (!is_inited()) {
    ret = OB_NOT_INIT;
    LOG_WARN("mysql proxy not inited", K(ret));
  } else if (OB_FAIL(pool_->acquire(conn, this, group_id))) {
    LOG_WARN("acquire connection failed", K(ret), K(conn));
  } else if (OB_ISNULL(conn)) {
    ret = OB_ERR_UNEXPECTED;
    LOG_ERROR("connection must not be null", K(ret), K(conn));
  }
  return ret;
}
