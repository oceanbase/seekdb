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
#include "lib/allocator/ob_malloc.h"
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
    : inited_(false), stop_(false), total_conn_cnt_(0),
      schema_service_(NULL),
      ob_sql_(NULL),
      vt_iter_creator_(NULL),
      config_(NULL),
      is_ddl_(false)
{
}

ObInnerSQLConnectionPool::~ObInnerSQLConnectionPool() = default;

int ObInnerSQLConnectionPool::init(ObMultiVersionSchemaService *schema_service,
                                   ObSql *ob_sql,
                                   ObVTIterCreator *vt_iter_creator,
                                   common::ObServerConfig *config,
                                   const bool is_ddl)
{
  int ret = OB_SUCCESS;
  if (inited_) {
    ret = OB_INIT_TWICE;
    LOG_WARN("init twice", K(ret));
  } else if (NULL == schema_service ||
      NULL == ob_sql ||
      NULL == vt_iter_creator) {
    ret = OB_INVALID_ARGUMENT;
    LOG_WARN("invalid argument", K(ret), KP(schema_service), KP(ob_sql),
             KP(vt_iter_creator));
  } else if (OB_FAIL(cond_.init(ObWaitEventIds::INNER_CONNECTION_POOL_COND_WAIT))) {
    LOG_WARN("fail to init cond, ", K(ret));
  } else {
    schema_service_ = schema_service;
    ob_sql_ = ob_sql;
    vt_iter_creator_ = vt_iter_creator;
    config_ = config;
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
  } else if (OB_FAIL(alloc_conn(inner_sql_conn))) {
    LOG_WARN("alloc connection from pool failed", K(ret));
  } else if (OB_FAIL(inner_sql_conn->init(this, schema_service_, ob_sql_, vt_iter_creator_,
                                          config_, nullptr /* session_info */, client_addr,
                                          nullptr/*sql modifer*/, is_ddl_, group_id))) {
    LOG_WARN("init connection failed", K(ret));
  } else {
    inner_sql_conn->ref();
    conn = inner_sql_conn;
  }

  if (OB_FAIL(ret)) {
    if (NULL != inner_sql_conn) {
      int tmp_ret = inner_sql_conn->destroy();
      if (OB_SUCCESS != tmp_ret) {
        LOG_WARN("destroy connection failed", "ret", tmp_ret);
      }
      // continue executing while destroy error.
      tmp_ret = free_conn(inner_sql_conn);
      if (OB_SUCCESS != tmp_ret) {
        LOG_WARN("free connection failed", "ret", tmp_ret);
      }
    }
  }

  return ret;
}

//@notice: performance optimization
//spi inner sql connection will be called frequently by PL(test in TPCC PL)
//before this, spi inner sql connection was management by inner sql connection pool
//when acquire inner sql connection, need wrlock to protect concurrency problem
//this action has serious performance problems
//cache SPI inner sql connection to ObServerObjectPool
//ObServerObjectPool has independent allocator on each core
//so it can reduce the conflict of threads acquiring spi connection
int ObInnerSQLConnectionPool::acquire_spi_conn(sql::ObSQLSessionInfo *session_info, ObInnerSQLConnection *&conn)
{
  int ret = OB_SUCCESS;
  if (OB_ISNULL(conn = rp_alloc(ObInnerSQLConnection, ObInnerSQLConnection::LABEL))) {
    ret = OB_ALLOCATE_MEMORY_FAILED;
    LOG_WARN("allocate spi connection failed", K(ret));
  } else if (OB_FAIL(conn->init(this,
                                schema_service_,
                                ob_sql_,
                                vt_iter_creator_,
                                config_,
                                session_info,
                                nullptr /* client_addr */,
                                nullptr /* sql_modifier */,
                                true /* use_static_engine */))) {
    LOG_WARN("init connection failed", K(ret));
  } else {
    conn->ref();
    conn->set_spi_connection(true);
  }
  return ret;
}

int ObInnerSQLConnectionPool::acquire(
    sql::ObSQLSessionInfo *session_info,
    common::sqlclient::ObISQLConnection *&conn)
{
  int ret = OB_SUCCESS;
  ObInnerSQLConnection *inner_sql_conn = NULL;
  if (!inited_) {
    ret = OB_NOT_INIT;
    LOG_WARN("not inited", K(ret));
  } else if (OB_FAIL(alloc_conn(inner_sql_conn))) {
    LOG_WARN("alloc connection from pool failed", K(ret));
  } else if (OB_FAIL(inner_sql_conn->init(this, schema_service_, ob_sql_, vt_iter_creator_, config_,
                                          session_info, NULL, NULL, false, 0/*group_id*/))) {
    LOG_WARN("init connection failed", K(ret));
  } else {
    if (0 != inner_sql_conn->get_ref()) {
      LOG_WARN("ref is not ZERO after acquire", KP(inner_sql_conn),
               "ref_cnt", inner_sql_conn->get_ref(), K(lbt()));
    }
    inner_sql_conn->ref();
    conn = inner_sql_conn;
  }

  if (OB_FAIL(ret)) {
    if (NULL != inner_sql_conn) {
      int tmp_ret = inner_sql_conn->destroy();
      if (OB_SUCCESS != tmp_ret) {
        LOG_WARN("destroy connection failed", "ret", tmp_ret);
      }
      // continue executing while destroy error.
      tmp_ret = free_conn(inner_sql_conn);
      if (OB_SUCCESS != tmp_ret) {
        LOG_WARN("free connection failed", "ret", tmp_ret);
      }
    }
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

int ObInnerSQLConnectionPool::revert(ObInnerSQLConnection *conn)
{
  int ret = OB_SUCCESS;
  if (!inited_) {
    LOG_WARN("not init", K(ret));
  } else if (NULL == conn) {
    ret = OB_INVALID_ARGUMENT;
    LOG_WARN("invalid argument", K(ret));
  } else {
    if (conn->is_spi_conn()) {
      //spi connection come from ObServerObjectPool, so release it to ObServerObjectPool
      rp_free(conn, ObInnerSQLConnection::LABEL);
    } else {
      int tmp_ret = conn->destroy();
      if (OB_SUCCESS != tmp_ret) {
        ret = tmp_ret;
        LOG_WARN("connection destroy failed", K(ret));
      }
      // The connection object itself is independently allocated and must be
      // released even if cleaning up its session reports an error.
      tmp_ret = free_conn(conn);
      if (OB_SUCCESS != tmp_ret) {
        ret = OB_SUCCESS == ret ? tmp_ret : ret;
        LOG_WARN("free connection failed", K(tmp_ret));
      }
    }
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

int ObInnerSQLConnectionPool::alloc_conn(ObInnerSQLConnection *&conn)
{
  int ret = OB_SUCCESS;
  ObInnerSQLConnection *inner_sql_conn = NULL;
  if (!inited_) {
    ret = OB_NOT_INIT;
    LOG_WARN("not init", K(ret));
  } else if (stop_) {
    ret = OB_SERVER_IS_STOPPING;
    LOG_WARN("connection pool stoped", K(ret));
  } else {
    ObThreadCondGuard guard(cond_);
    void *mem = ob_malloc(sizeof(*conn), SET_USE_500(ObModIds::OB_INNER_SQL_CONN_POOL));
    if (NULL == mem) {
      ret = OB_ALLOCATE_MEMORY_FAILED;
      LOG_ERROR("alloc memory failed", K(ret), K_(total_conn_cnt));
    } else {
      total_conn_cnt_++;
      inner_sql_conn = new (mem) ObInnerSQLConnection();
    }
    if (OB_SUCC(ret)) {
      conn = inner_sql_conn;
    }
  }

  return ret;
}

int ObInnerSQLConnectionPool::free_conn(ObInnerSQLConnection *conn)
{
  int ret = OB_SUCCESS;
  if (!inited_) {
    ret = OB_NOT_INIT;
    LOG_WARN("not init", K(ret));
  } else if (NULL == conn) {
    ret = OB_INVALID_ARGUMENT;
    LOG_WARN("invalid argument", K(ret), KP(conn));
  } else {
    conn->~ObInnerSQLConnection();
    ob_free(conn);
    ObThreadCondGuard guard(cond_);
    if (OB_UNLIKELY(total_conn_cnt_ <= 0)) {
      ret = OB_ERR_UNEXPECTED;
      LOG_ERROR("invalid total connection count", K(ret), K_(total_conn_cnt));
    } else {
      --total_conn_cnt_;
      if (stop_ && 0 == total_conn_cnt_) {
        cond_.signal();
      }
    }
  }

  return ret;
}

int ObInnerSQLConnectionPool::wait()
{
  int ret = OB_SUCCESS;
  const int64_t WAIT_TIME_MS = 1000;
  if (!inited_) {
    ret = OB_NOT_INIT;
    LOG_WARN("not init", K(ret));
  } else if (!stop_) {
    ret = OB_INNER_STAT_ERROR;
    LOG_WARN("not stoped", K(ret));
  } else {
    ObThreadCondGuard guard(cond_);
    while (total_conn_cnt_ > 0) {
      cond_.wait(WAIT_TIME_MS);
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
