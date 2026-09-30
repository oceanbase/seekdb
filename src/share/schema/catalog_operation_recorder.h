/* Copyright (c) 2026 OceanBase. Licensed under the Apache License, Version 2.0. */
#ifndef SEEKDB_SCHEMA_CATALOG_OPERATION_RECORDER_H_
#define SEEKDB_SCHEMA_CATALOG_OPERATION_RECORDER_H_
#include <cstdint>
namespace oceanbase { namespace share { namespace schema {
// Host SQL-client capability. Explicitly replaces thread-local version tracking
// for borrowed caller transactions. No plugin authorization or commit authority.
class ICatalogOperationRecorder
{
public:
  virtual ~ICatalogOperationRecorder() = default;
  virtual int check_schema_operation() const = 0;
  // Called even on failure; SQL error wins over cleanup/recording failures.
  // A recording failure after SQL success requires enclosing data rollback.
  virtual int finish_schema_operation(int64_t version, int sql_result) = 0;
};
} } }
#endif
