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


int ObSQLClientRetry::read(ReadResult &res, const int64_t cluster_id, const char *sql)
{
  //TODO if need across cluster
  UNUSEDx(res, cluster_id, sql);
  return OB_NOT_SUPPORTED;
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

sqlclient::ObISQLConnectionPool *ObSQLClientRetry::get_pool()
{
  sqlclient::ObISQLConnectionPool *pool = NULL;
  if (NULL != sql_client_) {
    pool = sql_client_->get_pool();
  }
  return pool;
}

sqlclient::ObISQLConnection *ObSQLClientRetry::get_connection()
{
  sqlclient::ObISQLConnection *conn = NULL;
  if (NULL != sql_client_) {
    conn = sql_client_->get_connection();
  }
  return conn;
}
