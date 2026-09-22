/* Copyright (c) 2026 OceanBase. Licensed under the Apache License, Version 2.0. */
#ifndef SEEKDB_SQL_CREATE_EXTENSION_EXECUTOR_H_
#define SEEKDB_SQL_CREATE_EXTENSION_EXECUTOR_H_
namespace oceanbase { namespace sql {
class ObExecContext;
class CreateExtensionStmt;
class CreateExtensionExecutor final
{
public:
  int execute(ObExecContext &ctx, const CreateExtensionStmt &statement);
};
} }
#endif
