// Copyright (c) 2026 OceanBase. Licensed under the Apache License, Version 2.0.
#include "common/mysqlclient/ob_mysql_proxy.h"
#include "common/mysqlclient/ob_isql_connection.h"
#include <cstdio>
#include <cstdlib>

// This link test must never dispatch a server request.
void request_finish_callback() { std::abort(); }

#ifdef TEST_SQL_FACTORY_OVERRIDE
namespace oceanbase {
namespace common {
int create_inner_sql_connection_for_proxy(
    bool is_ddl, int32_t group_id, sqlclient::ObISQLConnectionGuard &conn)
{
  conn.reset();
  return is_ddl ? group_id : OB_INVALID_ARGUMENT;
}
}
}
#endif

int main()
{
  using namespace oceanbase::common;
  ObCommonSqlProxy proxy;
  if (proxy.init(true) != OB_SUCCESS) { return 1; }
  const int groups[] = {17, 29};
  for (int group_id : groups) {
    sqlclient::ObISQLConnectionGuard conn;
    const int result = proxy.acquire_connection(conn, group_id);
#ifdef TEST_SQL_FACTORY_OVERRIDE
    const int expected = group_id;
#else
    const int expected = OB_NOT_SUPPORTED;
#endif
    if (result != expected || conn.is_valid()) {
      std::fprintf(stderr, "factory result=%d expected=%d group=%d\n", result, expected, group_id);
      return 1;
    }
  }
  std::puts("SQL_FACTORY_LINK_PASS");
  return 0;
}
