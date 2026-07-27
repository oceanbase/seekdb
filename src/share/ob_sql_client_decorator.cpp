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
#include "ob_sql_client_decorator.h"
#include "common/mysqlclient/ob_isql_connection.h"
using namespace oceanbase::common;

namespace
{
class ObQuerySensitiveSysVarRefreshGuard
{
public:
  ObQuerySensitiveSysVarRefreshGuard(
      sqlclient::ObISQLConnection &conn,
      const bool enabled)
      : conn_(conn),
        saved_enabled_(conn.is_query_sensitive_sys_var_refresh_enabled())
  {
    conn_.set_query_sensitive_sys_var_refresh_enabled(enabled);
  }

  ~ObQuerySensitiveSysVarRefreshGuard()
  {
    conn_.set_query_sensitive_sys_var_refresh_enabled(saved_enabled_);
  }

private:
  sqlclient::ObISQLConnection &conn_;
  const bool saved_enabled_;
  DISALLOW_COPY_AND_ASSIGN(ObQuerySensitiveSysVarRefreshGuard);
};
}

int ObSQLClientRetry::escape(const char *from, const int64_t from_size,
                             char *to, const int64_t to_size, int64_t &out_size)
{
  int ret = OB_SUCCESS;
  if (OB_ISNULL(sql_client_)) {
    ret = OB_INNER_STAT_ERROR;
  } else {
    ret = sql_client_->escape(from, from_size, to, to_size, out_size);
  }
  return ret;
}


int ObSQLClientRetry::read(ReadResult &res, const char *sql, const int32_t group_id)
{
  int ret = OB_SUCCESS;
  if (OB_ISNULL(sql_client_)) {
    ret = OB_INNER_STAT_ERROR;
  } else {
    ret = sql_client_->read(res, sql, group_id);
    if (OB_FAIL(ret)) {
      for (int32_t retry = 0; retry < retry_limit_ && OB_SUCCESS != ret; retry++) {
        LOG_WARN("retry execute query when failed", K(ret), K(retry), K_(retry_limit), K(sql));
        ret = sql_client_->read(res, sql, group_id);
      }
    }
  }
  return ret;
}

int ObSQLClientRetry::write(const char *sql, const int32_t group_id, int64_t &affected_rows)
{
  int ret = OB_SUCCESS;
  if (OB_ISNULL(sql_client_)) {
    ret = OB_INNER_STAT_ERROR;
  } else {
    ret = sql_client_->write(sql, group_id, affected_rows);
  }
  return ret;
}

sqlclient::ObISQLConnection *ObSQLClientRetry::get_connection()
{
  sqlclient::ObISQLConnection *conn = NULL;
  if (NULL != sql_client_) {
    conn = sql_client_->get_connection();
  }
  return conn;
}

int ObSQLClientRetry::acquire_connection(
    sqlclient::ObISQLConnection *&conn,
    const int32_t group_id)
{
  int ret = OB_SUCCESS;
  if (OB_ISNULL(sql_client_)) {
    ret = OB_NOT_INIT;
  } else {
    ret = sql_client_->acquire_connection(conn, group_id);
  }
  return ret;
}

int ObSQLClientRetry::release_connection(sqlclient::ObISQLConnection *conn)
{
  int ret = OB_SUCCESS;
  if (OB_ISNULL(sql_client_)) {
    ret = OB_NOT_INIT;
  } else {
    ret = sql_client_->release_connection(conn);
  }
  return ret;
}
