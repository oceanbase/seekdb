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

#include "ob_inner_sql_connection_pool.h"
#include "share/ob_server_struct.h"

namespace oceanbase
{
using namespace common;
using namespace share;
using namespace share::schema;
using namespace sql;

namespace observer
{
ObInnerSQLConnectionPool::ObInnerSQLConnectionPool()
    : inited_(false), stop_(false),
      is_ddl_(false)
{
}

ObInnerSQLConnectionPool::~ObInnerSQLConnectionPool() = default;

int ObInnerSQLConnectionPool::init(const bool is_ddl)
{
  int ret = OB_SUCCESS;
  if (inited_) {
    ret = OB_INIT_TWICE;
    LOG_WARN("init twice", K(ret));
  } else {
    is_ddl_ = is_ddl;
    inited_ = true;
  }
  return ret;
}

int ObInnerSQLConnectionPool::acquire(common::sqlclient::ObISQLConnection *&conn, ObISQLClient *client_addr, const int32_t group_id)
{
  int ret = OB_SUCCESS;
  UNUSED(group_id);
  ObInnerSQLConnection *inner_sql_conn = NULL;
  if (!inited_) {
    ret = OB_NOT_INIT;
    LOG_WARN("not inited", K(ret));
  } else if (stop_) {
    ret = OB_SERVER_IS_STOPPING;
    LOG_WARN("connection pool stopped", K(ret));
  } else if (OB_FAIL(ObInnerSQLConnection::create(
                 client_addr, is_ddl_, group_id, inner_sql_conn))) {
    LOG_WARN("create inner sql connection failed", K(ret));
  } else {
    conn = inner_sql_conn;
  }
  return ret;
}

int ObInnerSQLConnectionPool::release(common::sqlclient::ObISQLConnection *conn, const bool success)
{
  // alway try to destroy connection, ignore success flag.
  UNUSEDx(success);
  int ret = OB_SUCCESS;
  if (!inited_) {
    ret = OB_NOT_INIT;
    LOG_WARN("not inited", K(ret));
  } else if (NULL == conn) {
    // ignore NULL connection release
  } else {
    static_cast<ObInnerSQLConnection *>(conn)->unref();
  }
  return ret;
}

// TODO baihua: implement
int ObInnerSQLConnectionPool::escape(const char *from, const int64_t from_size,
    char *to, const int64_t to_size, int64_t &out_size)
{
  int ret = OB_SUCCESS;
  if (!inited_) {
    ret = OB_NOT_INIT;
    LOG_WARN("not init", K(ret));
  } else {
    if (NULL != from && from_size > 0) {
      if (to_size < from_size * 2) {
        ret = OB_BUF_NOT_ENOUGH;
        LOG_WARN("string buffer not enough", K(ret), K(from_size), K(to_size));
      } else if (NULL == to) {
        ret = OB_INVALID_ARGUMENT;
        LOG_WARN("to buffer is NULL", K(ret), KP(to), KP(from), K(from_size));
      } else {
        MEMCPY(to, from, from_size);
        out_size = from_size;
      }
    } else {
      out_size = 0;
    }
  }
  return ret;
}

int ObInnerSQLConnectionPool::on_client_inactive(ObISQLClient *client_addr)
{
  int ret = OB_SUCCESS;
  if (OB_ISNULL(client_addr)) {
    ret = OB_INVALID_ARGUMENT;
    LOG_WARN("client is null", K(ret));
  } else if (OB_ISNULL(GCTX.session_mgr_)) {
    ret = OB_ERR_UNEXPECTED;
    LOG_WARN("session mgr is null", K(ret));
  } else if (OB_FAIL(
                 GCTX.session_mgr_->kill_inner_sessions_by_client_key(
                     reinterpret_cast<uint64_t>(client_addr)))) {
    LOG_WARN("failed to kill inner sql queries by client", K(ret),
             KP(client_addr));
  }
  return ret;
}

} // end namespace observer
} // end namespace oceanbase
