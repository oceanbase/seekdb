/* Copyright (c) 2026 OceanBase. Licensed under the Apache License, Version 2.0. */
#ifndef SEEKDB_SQL_ALTER_EXTENSION_EXECUTOR_H_
#define SEEKDB_SQL_ALTER_EXTENSION_EXECUTOR_H_
namespace oceanbase { namespace sql {
class ObExecContext;
class AlterExtensionStmt;
class AlterExtensionExecutor final
{
public:
  int execute(ObExecContext &ctx, const AlterExtensionStmt &statement);
};
} }
#endif
