/* Copyright (c) 2026 OceanBase. Licensed under the Apache License, Version 2.0. */
#ifndef SEEKDB_SQL_EXTENSION_ROUTINE_BATCH_H_
#define SEEKDB_SQL_EXTENSION_ROUTINE_BATCH_H_

#include "share/ob_rpc_struct.h"
#include "share/plugin/extension_routine_update.h"
#include <memory>

namespace oceanbase { namespace sql {

// Owns the complete existing DDL wire representation, including the backing
// storage of deserialized ObString views, plus routine/parameter schema versions
// which the ordinary RPC codec omits. No resolver arena or schema guard is
// retained. This is data ownership, not permission to execute the arguments.
class ExtensionRoutineBatch final
{
public:
  ExtensionRoutineBatch();
  ~ExtensionRoutineBatch();
  ExtensionRoutineBatch(const ExtensionRoutineBatch &) = delete;
  ExtensionRoutineBatch &operator=(const ExtensionRoutineBatch &) = delete;
  // Input must not alias args(). Failure clears the batch; no partial output.
  int assign(const common::ObIArray<const obcall::ObCreateRoutineArg *> &source);
  void reset();
  const common::ObIArray<const obcall::ObCreateRoutineArg *> &args() const { return args_; }
private:
  struct Impl;
  std::unique_ptr<Impl> impl_;
  common::ObSEArray<const obcall::ObCreateRoutineArg *, 16> args_;
};

// Owns mixed UPDATE commands in script order using the complete ordinary DDL
// wire codec plus the same host-only schema versions. These scalar versions do
// not confer write authority or replace Root's reservation tokens.
// GRANT/REVOKE snapshots are also owned in-order, including target/actor/roles,
// all recipients and option/behavior fields. Their presence does not admit DCL
// in a SQL package: executors must support borrowed-transaction ACL staging.
// Empty updates are valid (version-only/no-op); CREATE installation
// batches above remain nonempty. Neither class performs semantic resolution.
class ExtensionRoutineUpdateBatch final
{
public:
  using Operation = share::plugin::ExtensionRoutineUpdateOperation;
  static constexpr int64_t MAX_OPERATIONS = 4096;
  static constexpr int64_t MAX_WIRE_BYTES = 64 * 1024 * 1024;
  ExtensionRoutineUpdateBatch();
  ~ExtensionRoutineUpdateBatch();
  ExtensionRoutineUpdateBatch(const ExtensionRoutineUpdateBatch &) = delete;
  ExtensionRoutineUpdateBatch &operator=(const ExtensionRoutineUpdateBatch &) = delete;
  // Source views may alias operations(): copy completely before replacing the
  // backing storage. Any failure clears the batch, never exposes a partial plan.
  int assign(const common::ObIArray<Operation> &source);
  void reset();
  const common::ObIArray<Operation> &operations() const { return operations_; }
private:
  struct Impl;
  std::unique_ptr<Impl> impl_;
  common::ObSEArray<Operation, 16> operations_;
};

} }
#endif
