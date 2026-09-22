/* Copyright (c) 2026 OceanBase. Licensed under the Apache License, Version 2.0. */
#ifndef SEEKDB_SCHEMA_CATALOG_DML_SQL_HELPER_H_
#define SEEKDB_SCHEMA_CATALOG_DML_SQL_HELPER_H_
#include "share/ob_dml_sql_splicer.h"

namespace oceanbase { namespace share { namespace schema {
// Host catalog writers can run on a borrowed user session. Qualify the physical
// system table without changing that session's current database or privileges.
// table_name is a host-owned inner-table name, not user-supplied SQL.
class CatalogDMLSqlHelper final
{
public:
  explicit CatalogDMLSqlHelper(common::ObISQLClient &client) : executor_(client) {}
#define SEEKDB_CATALOG_DML(method) \
  int exec_##method(const char *table_name, const ObDMLSqlSplicer &splicer, int64_t &rows) \
  { \
    rows = 0; \
    if (table_name == nullptr) return common::OB_INVALID_ARGUMENT; \
    common::ObSqlString qualified; \
    const int ret = qualified.assign_fmt("%s.%s", common::OB_SYS_DATABASE_NAME, table_name); \
    return ret == common::OB_SUCCESS ? executor_.exec_##method(qualified.ptr(), splicer, rows) : ret; \
  }
  SEEKDB_CATALOG_DML(insert)
  SEEKDB_CATALOG_DML(insert_update)
  SEEKDB_CATALOG_DML(update)
  SEEKDB_CATALOG_DML(delete)
  SEEKDB_CATALOG_DML(replace)
#undef SEEKDB_CATALOG_DML
private:
  ObDMLExecHelper executor_;
};
} } }
#endif
