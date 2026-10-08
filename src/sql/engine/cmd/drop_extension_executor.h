/* Copyright (c) 2026 OceanBase. Licensed under the Apache License, Version 2.0. */
#ifndef SEEKDB_SQL_DROP_EXTENSION_EXECUTOR_H_
#define SEEKDB_SQL_DROP_EXTENSION_EXECUTOR_H_
namespace oceanbase { namespace sql {
class ObExecContext;
class DropExtensionStmt;
class DropExtensionExecutor final
{
public:
  int execute(ObExecContext &ctx, const DropExtensionStmt &statement);
};
} }
#endif
