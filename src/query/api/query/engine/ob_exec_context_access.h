/*
 * Copyright (c) 2025 OceanBase.
 * Licensed under the Apache License, Version 2.0 (the "License");
 */

#ifndef OCEANBASE_QUERY_API_ENGINE_OB_EXEC_CONTEXT_ACCESS_H_
#define OCEANBASE_QUERY_API_ENGINE_OB_EXEC_CONTEXT_ACCESS_H_

#include <stdint.h>

namespace oceanbase
{
namespace common
{
class ObMySQLProxy;
}
namespace share
{
namespace schema
{
class ObSchemaGetterGuard;
}
}
namespace sql
{
class ObExecContext;
class ObSQLSessionInfo;
}
namespace query
{

// Transitional capability facade for data-plane command handlers.  It keeps
// ObExecContext layout and ObSqlCtx private while exposing only the resources
// those handlers currently consume.
class ObExecContextAccess
{
public:
  static sql::ObSQLSessionInfo *get_session(sql::ObExecContext &ctx);
  static common::ObMySQLProxy *get_sql_proxy(sql::ObExecContext &ctx);
  static share::schema::ObSchemaGetterGuard *get_schema_guard(
      sql::ObExecContext &ctx);
  static int check_status(sql::ObExecContext &ctx);
  static int get_error_code(const sql::ObExecContext &ctx);
  static uint64_t get_server_session_id(
      const sql::ObSQLSessionInfo *session);
  static uint64_t get_priv_user_id(
      const sql::ObSQLSessionInfo *session);
};

} // namespace query
} // namespace oceanbase

#endif // OCEANBASE_QUERY_API_ENGINE_OB_EXEC_CONTEXT_ACCESS_H_
