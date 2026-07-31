/*
 * Copyright (c) 2025 OceanBase.
 * Licensed under the Apache License, Version 2.0 (the "License");
 */

#ifndef OCEANBASE_QUERY_API_SESSION_OB_SESSION_INNER_SQL_H_
#define OCEANBASE_QUERY_API_SESSION_OB_SESSION_INNER_SQL_H_

#include <stdint.h>

#include "query/session/ob_inner_sql_connection_access.h"
#include "share/ob_lock_metadata_session.h"

namespace oceanbase
{
namespace query
{

// Opaque, query-owned access to the inner SQL connection associated with a
// session.  Data-plane modules can persist their own metadata without knowing
// ObSQLSessionInfo, ObExecContext, or ObInnerSQLConnection.  The native
// pointers are supplied only by the query adapter at the seam.
class ObSessionInnerSql final : public share::ObILockMetadataSession
{
public:
  ObSessionInnerSql(void *native_session, void *native_sql_proxy);
  ObSessionInnerSql(const ObSessionInnerSql &) = delete;
  ObSessionInnerSql &operator=(const ObSessionInnerSql &) = delete;
  ~ObSessionInnerSql();

  bool is_valid() const override;
  uint32_t server_session_id() const override;
  int execute_write(const common::ObSqlString &sql,
                    int64_t &affected_rows) override;
  int execute_read(const common::ObSqlString &sql,
                   common::ObISQLClient::ReadResult &result) override;

private:
  int open_();
  void close_();

private:
  void *native_session_;
  void *native_sql_proxy_;
  void *native_connection_;
  common::sqlclient::ObISQLConnectionGuard connection_guard_;
  bool owns_connection_;
  int last_error_;
};

} // namespace query
} // namespace oceanbase

#endif // OCEANBASE_QUERY_API_SESSION_OB_SESSION_INNER_SQL_H_
