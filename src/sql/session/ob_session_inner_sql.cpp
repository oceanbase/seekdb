/*
 * Copyright (c) 2025 OceanBase.
 * Licensed under the Apache License, Version 2.0 (the "License");
 */

#define USING_LOG_PREFIX SQL

#include "query/session/ob_session_inner_sql.h"

#include "common/mysqlclient/ob_mysql_proxy.h"
#include "share/ob_server_struct.h"
#include "share/schema/ob_schema_getter_guard.h"
#include "share/schema/ob_multi_version_schema_service.h"
#include "share/schema/ob_schema_struct.h"
#include "sql/session/ob_sql_session_info.h"
#include "sql/session/ob_sql_session_mgr.h"

namespace oceanbase
{
namespace query
{

ObSessionInnerSql::ObSessionInnerSql(void *native_session, void *native_sql_proxy)
  : native_session_(native_session),
    native_sql_proxy_(native_sql_proxy),
    native_connection_(nullptr),
    connection_guard_(),
    owns_connection_(false),
    last_error_(common::OB_SUCCESS)
{}

ObSessionInnerSql::~ObSessionInnerSql()
{
  close_();
}

bool ObSessionInnerSql::is_valid() const
{
  return nullptr != native_session_ && nullptr != native_sql_proxy_;
}

uint32_t ObSessionInnerSql::server_session_id() const
{
  const sql::ObSQLSessionInfo *session =
      static_cast<const sql::ObSQLSessionInfo *>(native_session_);
  return nullptr == session ? common::INVALID_SESSID : session->get_server_sid();
}

int ObSessionInnerSql::open_()
{
  int ret = common::OB_SUCCESS;
  sql::ObSQLSessionInfo *session =
      static_cast<sql::ObSQLSessionInfo *>(native_session_);
  common::ObMySQLProxy *sql_proxy =
      static_cast<common::ObMySQLProxy *>(native_sql_proxy_);
  if (nullptr != native_connection_) {
  } else if (OB_ISNULL(session) || OB_ISNULL(sql_proxy)) {
    ret = common::OB_NOT_INIT;
  } else if (nullptr != session->get_inner_conn()) {
    native_connection_ = session->get_inner_conn();
  } else if (OB_FAIL(
                 ObInnerSQLConnectionAccess::
                     create_connection_with_external_session(
                         session, connection_guard_))) {
    LOG_WARN("acquire session inner SQL connection failed", KR(ret),
             K(server_session_id()));
  } else if (OB_ISNULL(native_connection_ = connection_guard_.get_ptr())) {
    ret = common::OB_ERR_UNEXPECTED;
  } else {
    owns_connection_ = true;
  }
  if (OB_FAIL(ret)) {
    last_error_ = ret;
  }
  return ret;
}

void ObSessionInnerSql::close_()
{
  if (owns_connection_) {
    connection_guard_.reset();
  }
  native_connection_ = nullptr;
  owns_connection_ = false;
}

int ObSessionInnerSql::execute_write(const common::ObSqlString &sql,
                                     int64_t &affected_rows)
{
  int ret = common::OB_SUCCESS;
  common::sqlclient::ObISQLConnection *connection = nullptr;
  if (OB_FAIL(open_())) {
  } else if (OB_ISNULL(connection =
                           static_cast<common::sqlclient::ObISQLConnection *>(
                               native_connection_))) {
    ret = common::OB_ERR_UNEXPECTED;
  } else if (OB_FAIL(connection->execute_write(sql.ptr(), affected_rows))) {
    LOG_WARN("execute session inner write failed", KR(ret), K(sql));
  }
  if (OB_FAIL(ret)) {
    last_error_ = ret;
  }
  return ret;
}

int ObSessionInnerSql::execute_read(const common::ObSqlString &sql,
                                    common::ObISQLClient::ReadResult &result)
{
  int ret = common::OB_SUCCESS;
  common::sqlclient::ObISQLConnection *connection = nullptr;
  if (OB_FAIL(open_())) {
  } else if (OB_ISNULL(connection =
                           static_cast<common::sqlclient::ObISQLConnection *>(
                               native_connection_))) {
    ret = common::OB_ERR_UNEXPECTED;
  } else if (OB_FAIL(connection->execute_read(sql.ptr(), result))) {
    LOG_WARN("execute session inner read failed", KR(ret), K(sql));
  }
  if (OB_FAIL(ret)) {
    last_error_ = ret;
  }
  return ret;
}

} // namespace query
} // namespace oceanbase
