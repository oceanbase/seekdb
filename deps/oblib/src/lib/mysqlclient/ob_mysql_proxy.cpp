/**
 * Copyright (c) 2021 OceanBase
 * OceanBase CE is licensed under Mulan PubL v2.
 * You can use this software according to the terms and conditions of the Mulan PubL v2.
 * You may obtain a copy of Mulan PubL v2 at:
 *          http://license.coscl.org.cn/MulanPubL-2.0
 * THIS SOFTWARE IS PROVIDED ON AN "AS IS" BASIS, WITHOUT WARRANTIES OF ANY KIND,
 * EITHER EXPRESS OR IMPLIED, INCLUDING BUT NOT LIMITED TO NON-INFRINGEMENT,
 * MERCHANTABILITY OR FIT FOR A PARTICULAR PURPOSE.
 * See the Mulan PubL v2 for more details.
 */

#define USING_LOG_PREFIX COMMON_MYSQLP
#include "lib/mysqlclient/ob_isql_connection_pool.h"
#include "lib/mysqlclient/ob_isql_connection_pool.h"
#include "lib/mysqlclient/ob_mysql_proxy.h"
#include "common/sql_mode/ob_sql_mode_utils.h"
#include "lib/mysqlclient/ob_dblink_error_trans.h"
using namespace oceanbase::common;
using namespace oceanbase::common::sqlclient;

OB_SERIALIZE_MEMBER(ObSessionDDLInfo, ddl_info_);

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

int ObCommonSqlProxy::read(ReadResult &result, const uint64_t tenant_id, const char *sql, const int32_t group_id)
{
  int ret = OB_SUCCESS;
  ObISQLConnection *conn = NULL;
  if (OB_FAIL(acquire(tenant_id, conn, group_id))) {
    LOG_WARN("acquire connection failed", K(ret), K(conn));
  } else if (OB_FAIL(read(conn, result, tenant_id, sql))) {
    LOG_WARN("read failed", K(ret));
  }
  close(conn, ret);
  return ret;
}

int ObCommonSqlProxy::read(ReadResult &result, const uint64_t tenant_id, const char *sql, const common::ObAddr *exec_sql_addr)
{
  int ret = OB_SUCCESS;
  ObISQLConnection *conn = NULL;
  if (OB_ISNULL(exec_sql_addr)) {
    ret = OB_INVALID_ARGUMENT;
    LOG_WARN("read with typically exec addr failed", K(ret), K(exec_sql_addr));
  } else if (OB_FAIL(acquire(tenant_id, conn, 0/*group_id*/))) {
    LOG_WARN("acquire connection failed", K(ret), K(conn));
  } else if (OB_FAIL(read(conn, result, tenant_id, sql, exec_sql_addr))) {
    LOG_WARN("read failed", K(ret));
  }
  close(conn, ret);
  return ret;
}

int ObCommonSqlProxy::read(ReadResult &result, const uint64_t tenant_id, const char *sql, const ObSessionParam *session_param, int64_t user_set_timeout)
{
  int ret = OB_SUCCESS;
  ObISQLConnection *conn = NULL;
  if (OB_FAIL(acquire(tenant_id, conn, 0/*group_id*/))) {
    LOG_WARN("acquire connection failed", K(ret), K(conn));
  } else if (nullptr != session_param) {
    conn->set_ddl_info(&session_param->ddl_info_);
    conn->set_use_external_session(session_param->use_external_session_);
    conn->set_group_id(session_param->consumer_group_id_);
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

    if (session_param->ddl_info_.is_ddl()) {
      conn->set_force_remote_exec(true);
    }
  }

  if (OB_FAIL(ret)) {
  } else if (FALSE_IT(conn->set_user_timeout(user_set_timeout))) {
  } else if (OB_FAIL(read(conn, result, tenant_id, sql))) {
    LOG_WARN("read failed", K(ret));
  }
  close(conn, ret);
  return ret;
}

int ObCommonSqlProxy::read(ObISQLConnection *conn, ReadResult &result,
                           const uint64_t tenant_id, const char *sql, const common::ObAddr *exec_sql_addr)
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
    if (OB_FAIL(conn->execute_read(tenant_id, sql, result, exec_sql_addr))) {
      LOG_WARN("query failed", K(ret), K(conn), K(start), KCSTRING(sql));
    }
  }
  LOG_TRACE("execute sql", KCSTRING(sql), K(ret));
  return ret;
}

int ObCommonSqlProxy::write(const uint64_t tenant_id, const char *sql, const int32_t group_id, int64_t &affected_rows)
{
  int ret = OB_SUCCESS;
  int64_t start = ::oceanbase::common::ObTimeUtility::current_time();
  ObISQLConnection *conn = NULL;
  if (OB_ISNULL(sql)) {
    ret = OB_INVALID_ARGUMENT;
    LOG_WARN("empty sql");
  } else if (OB_FAIL(acquire(tenant_id, conn, group_id))) {
    LOG_WARN("acquire connection failed", K(ret), K(conn));
  } else if (OB_ISNULL(conn)) {
    ret = OB_INNER_STAT_ERROR;
    LOG_WARN("connection can not be NULL");
  } else if (!is_active()) { // check client active after connection acquired
    ret = OB_INACTIVE_SQL_CLIENT;
    LOG_WARN("in active sql client", K(ret), KCSTRING(sql));
  } else {
    if (OB_FAIL(conn->execute_write(tenant_id, sql, affected_rows))) {
      LOG_WARN("execute sql failed", K(ret), K(conn), K(start), KCSTRING(sql));
    }
  }
  close(conn, ret);
  LOG_TRACE("execute sql", KCSTRING(sql), K(ret));
  return ret;
}

int ObCommonSqlProxy::write(const uint64_t tenant_id, const ObString sql,
                        int64_t &affected_rows, int64_t compatibility_mode, 
                        const ObSessionParam *param /* = nullptr*/,
                        const common::ObAddr *sql_exec_addr)
{
  int ret = OB_SUCCESS;
  bool is_user_sql = false;
  int64_t start = ::oceanbase::common::ObTimeUtility::current_time();
  ObISQLConnection *conn = NULL;
  if (OB_UNLIKELY(sql.empty())) {
    ret = OB_INVALID_ARGUMENT;
    LOG_WARN("empty sql");
  } else if (OB_FAIL(acquire(tenant_id, conn, 0/*group_id*/))) {
    LOG_WARN("acquire connection failed", K(ret), K(conn));
  } else if (OB_ISNULL(conn)) {
    ret = OB_INNER_STAT_ERROR;
    LOG_WARN("connection can not be NULL");
  } else if (!is_active()) { // check client active after connection acquired
    ret = OB_INACTIVE_SQL_CLIENT;
    LOG_WARN("in active sql client", K(ret), K(sql));
  }
  int64_t old_compatibility_mode;
  int64_t old_sql_mode = 0;
  if (OB_SUCC(ret)) {
    if (OB_FAIL(conn->get_session_variable("ob_compatibility_mode", old_compatibility_mode))) {
      LOG_WARN("fail to get inner connection compatibility mode", K(ret));
    } else if (old_compatibility_mode != compatibility_mode
               && OB_FAIL(conn->set_session_variable("ob_compatibility_mode", compatibility_mode))) {
      LOG_WARN("fail to set inner connection compatibility mode", K(ret), K(compatibility_mode));
    } else {
      if (is_oracle_compatible(static_cast<ObCompatibilityMode>(compatibility_mode))) {
        conn->set_oracle_compat_mode();
      } else {
        conn->set_mysql_compat_mode();
      }
      LOG_TRACE("compatibility mode switch successfully!",
                K(old_compatibility_mode), K(compatibility_mode));
    }
  }
  if (OB_SUCC(ret) && nullptr != param) {
    conn->set_is_load_data_exec(param->is_load_data_exec_);
    conn->set_use_external_session(param->use_external_session_);
    conn->set_group_id(param->consumer_group_id_);
    conn->set_ob_enable_pl_cache(param->enable_pl_cache_);
    if (param->is_load_data_exec_) {
      is_user_sql = true;
    }
    if (OB_FAIL(conn->set_ddl_info(&param->ddl_info_))) {
      LOG_WARN("fail to set ddl info", K(ret));
    }
    if (param->ddl_info_.is_ddl()) {
      conn->set_force_remote_exec(true);
      conn->set_nls_formats(param->nls_formats_);
    }
    if (!param->secure_file_priv_.empty()) {
      conn->set_session_variable("secure_file_priv", param->secure_file_priv_);
    }
  }
  if (OB_SUCC(ret) && nullptr != param && nullptr != param->sql_mode_) {
    // TODO(cangdi): fix get_session_variable not working
    /*if (OB_FAIL(conn->get_session_variable("sql_mode", old_sql_mode))) {
      LOG_WARN("get inner connection sql mode", K(ret));
    } else*/ if (OB_FAIL(conn->set_session_variable("sql_mode", *param->sql_mode_))) {
      LOG_WARN("set inner connection sql mode failed", K(ret));
    }
  }
  if (OB_SUCC(ret) && nullptr != param && nullptr != param->tz_info_wrap_) {
    if (OB_FAIL(conn->set_tz_info_wrap(*param->tz_info_wrap_))) {
      LOG_WARN("fail to set time zone info wrap", K(ret));
    }
  }
  if (OB_SUCC(ret)) {
    if (OB_FAIL(conn->execute_write(tenant_id, sql, affected_rows, is_user_sql, sql_exec_addr))) {
      LOG_WARN("execute sql failed", K(ret), K(tenant_id), K(conn), K(start), K(sql));
    } else if (old_compatibility_mode != compatibility_mode
               && OB_FAIL(conn->set_session_variable("ob_compatibility_mode", old_compatibility_mode))) {
      LOG_WARN("fail to recover inner connection sql mode", K(ret));
    /*} else if (nullptr != sql_mode && old_sql_mode != *sql_mode && OB_FAIL(conn->set_session_variable("sql_mode", old_sql_mode))) {
      LOG_WARN("set inner connection sql mode failed", K(ret));*/
    } else {
      if (is_oracle_compatible(static_cast<ObCompatibilityMode>(old_compatibility_mode))) {
        conn->set_oracle_compat_mode();
      } else {
        conn->set_mysql_compat_mode();
      }
      LOG_TRACE("compatibility mode switch successfully!",
                K(compatibility_mode), K(old_compatibility_mode));
    }
  }
  close(conn, ret);
  LOG_TRACE("execute sql with sql mode", K(sql), K(compatibility_mode), K(ret));
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


int ObCommonSqlProxy::acquire(const uint64_t tenant_id, sqlclient::ObISQLConnection *&conn, const int32_t group_id)
{
  int ret = OB_SUCCESS;
  if (!is_inited()) {
    ret = OB_NOT_INIT;
    LOG_WARN("mysql proxy not inited", K(ret));
  } else if (OB_FAIL(pool_->acquire(tenant_id, conn, this, group_id))) {
    LOG_WARN("acquire connection failed", K(ret), K(conn));
  } else if (OB_ISNULL(conn)) {
    ret = OB_ERR_UNEXPECTED;
    LOG_ERROR("connection must not be null", K(ret), K(conn));
  }
  return ret;
}

int ObCommonSqlProxy::read(
    ReadResult &result,
    const int64_t cluster_id,
    const uint64_t tenant_id,
    const char *sql)
{
  int ret = OB_SUCCESS;
  ObISQLConnection *conn = NULL;
  const int64_t start = ::oceanbase::common::ObTimeUtility::current_time();
  result.reset();
  if (!is_inited()) {
    ret = OB_NOT_INIT;
    LOG_WARN("mysql proxy not inited", K(ret), K(cluster_id), K(tenant_id), KCSTRING(sql));
  } else if (NULL == sql) {
    ret = OB_INVALID_ARGUMENT;
    LOG_WARN("empty sql");
  } else if (OB_FAIL(acquire(tenant_id, conn, 0/*group_id*/))) {
    LOG_WARN("acquire connection failed", K(ret), K(conn));
  } else if (NULL == conn) {
    ret = OB_INNER_STAT_ERROR;
    LOG_WARN("connection can not be NULL");
  } else if (!is_active()) { // check client active after connection acquired
    ret = OB_INACTIVE_SQL_CLIENT;
    LOG_WARN("in active sql client", K(ret), KCSTRING(sql));
  } else {
    if (OB_FAIL(conn->execute_read(cluster_id, tenant_id, sql, result))) {
      LOG_WARN("query failed", K(ret), K(conn), K(start), KCSTRING(sql), K(cluster_id));
    }
  }
  close(conn, ret);
  LOG_TRACE("execute sql", KCSTRING(sql), K(ret));
  return ret;
}
