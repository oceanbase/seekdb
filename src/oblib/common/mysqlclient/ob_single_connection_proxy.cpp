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
#include "ob_single_connection_proxy.h"
#include "common/mysqlclient/ob_isql_connection.h"

using namespace oceanbase::common;
using namespace oceanbase::common::sqlclient;

ObSingleConnectionProxy::ObSingleConnectionProxy()
    :errno_(OB_SUCCESS),
     statement_count_(0),
     conn_(),
     sql_client_(NULL)
{
}

ObSingleConnectionProxy::~ObSingleConnectionProxy()
{
  (void)close();
}

int ObSingleConnectionProxy::connect(const int32_t group_id, ObISQLClient *sql_client)
{
  int ret = OB_SUCCESS;
  if (NULL == sql_client || group_id < 0) {
    ret = OB_INVALID_ARGUMENT;
  } else if (NULL != sql_client_ || conn_.is_valid()) {
    ret = OB_INNER_STAT_ERROR;
    LOG_WARN("transaction can only be started once", K_(sql_client), KP(conn_.get_ptr()));
  } else {
    if (OB_FAIL(sql_client->acquire_connection(conn_, group_id))) {
    } else if (!conn_.is_valid()) {
      ret = OB_INNER_STAT_ERROR;
    } else {
      sql_client_ = sql_client;
    }
    if (OB_FAIL(ret)) {
      conn_.reset();
      sql_client_ = NULL;
    }
  }
  return ret;
}

int ObSingleConnectionProxy::acquire_connection(
    ObISQLConnectionGuard &conn,
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

int ObSingleConnectionProxy::read(ReadResult &res, const char *sql, const int32_t group_id)
{
  int ret = OB_SUCCESS;
  res.reset();
  UNUSED(group_id);
  if (!check_inner_stat()) {
    ret = OB_INNER_STAT_ERROR;
    LOG_WARN("check inner stat failed");
  } else if (OB_FAIL(conn_->execute_read(sql, res))) {
    errno_ = ret;
    const int ERR_LOCK_WAIT_TIMEOUT = -1205;
    if (ERR_LOCK_WAIT_TIMEOUT == ret) {
      LOG_INFO("execute query failed", K(ret), KCSTRING(sql), K_(conn));
    } else {
    }
  }
  ++statement_count_;
  return ret;
}

int ObSingleConnectionProxy::write(
    const char *sql, const int32_t group_id, int64_t &affected_rows)
{
  int ret = OB_SUCCESS;
  UNUSED(group_id);
  if (!check_inner_stat()) {
    ret = OB_INNER_STAT_ERROR;
    LOG_WARN("check inner stat failed");
  } else if (NULL == sql_client_) {
    ret = OB_INACTIVE_SQL_CLIENT;
  } else if (OB_FAIL(conn_->execute_write(sql, affected_rows))) {
    errno_ = ret;
  }
  ++statement_count_;
  return ret;
}

void ObSingleConnectionProxy::close()
{
  conn_.reset();
  sql_client_ = NULL;
  errno_ = OB_SUCCESS;
}

int ObSingleConnectionProxy::escape(const char *from, const int64_t from_size,
    char *to, const int64_t to_size, int64_t &out_size)
{
  int ret = OB_SUCCESS;
  if (NULL == sql_client_) {
    ret = OB_INNER_STAT_ERROR;
    LOG_WARN("transcation not started");
  } else if (OB_FAIL(sql_client_->escape(from, from_size, to, to_size, out_size))) {
  }
  return ret;
}
