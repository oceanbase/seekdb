/* Copyright (c) 2026 OceanBase. Licensed under the Apache License, Version 2.0. */
#ifndef SEEKDB_SQL_EXTENSION_ROUTINE_RESOLVER_H_
#define SEEKDB_SQL_EXTENSION_ROUTINE_RESOLVER_H_

#include "share/ob_rpc_struct.h"
#include "sql/resolver/ddl/extension_routine_batch.h"
#include "share/plugin/catalog_builder.h"
#include "sql/engine/expr/caller_catalog_transaction.h"
#include <memory>
#include <string>

namespace oceanbase { namespace sql {
class ExtensionScript;
class ExtensionUpdatePlan;
struct ObResolverParams;
struct ObSqlCtx;

// Host-owned query mutation. Resolution is read-only; apply requires the
// caller frame's DDL admission and exact borrowed transaction/view pair.
// Single-use; never creates Extension membership or publishes/commits schema.
class CallerRoutineMutation final : public ICallerCatalogMutation
{
public:
  CallerRoutineMutation(const std::string &sql, std::string &error) : sql_(sql), error_(error) {}
  int preflight(ObExecContext &context) override;
  int apply(ObExecContext &context, common::ObMySQLTransaction &transaction,
      share::schema::RoutineSchemaOverlay &schema,
      share::schema::RoutinePrivilegeOverlay &privileges,
      rootserver::IRoutineCacheInvalidation &invalidation) override;
  uint64_t object_id() const { return object_id_; }
private:
  const std::string sql_;
  std::string &error_;
  ExtensionRoutineUpdateBatch batch_;
  bool attempted_ = false;
  bool ready_ = false;
  uint64_t object_id_ = 0;
  ObExecContext *context_ = nullptr;
  uint64_t database_id_ = 0;
  uint64_t principal_id_ = 0;
  uint64_t sql_mode_ = 0;
};

// Owns every resolved operation for a synchronous Root-driven sequence. Host
// services, session and immutable plan outlive this object; no plugin may bind
// them. Each resolve consumes the next index, with no retry after failure.
class ExtensionRoutineScriptResolver final : public share::plugin::IExtensionRoutineScript
{
public:
  ExtensionRoutineScriptResolver(const ExtensionUpdatePlan &plan, const ObResolverParams &services,
      const ObSqlCtx &context);
  ExtensionRoutineScriptResolver(const ExtensionScript &script,
      const share::plugin::ExtensionInstallSpec &spec, const ObResolverParams &services,
      const ObSqlCtx &context, share::plugin::ICatalogBuildProgram *program = nullptr);
  ~ExtensionRoutineScriptResolver();
  int64_t statement_count() const override;
  int preflight(const share::plugin::ExtensionUpdateRequest &request, std::string &error) override;
  int preflight_install(const share::plugin::ExtensionInstallSpec &spec, std::string &error) override;
  int validate_view(share::schema::ObSchemaGetterGuard &view, std::string &error) override;
  int resolve(int64_t index, share::schema::ObSchemaGetterGuard &view,
      const share::plugin::ExtensionRoutineUpdateOperation *&operation, std::string &error) override;
  bool has_builder() const override;
  int build(share::schema::ObSchemaGetterGuard &view, const StageRoutine &stage, std::string &error) override;
private:
  int preflight_common(std::string &error);
  struct Impl;
  std::unique_ptr<Impl> impl_;
};

// Core-only semantic bridge, never a native plugin authority surface. Successful
// resolution returns owned wire snapshots, valid until reset/destruction. The
// authenticated session/schema guard are borrowed only during resolution.
class ExtensionRoutineResolver final
{
public:
  ExtensionRoutineResolver();
  ~ExtensionRoutineResolver();
  ExtensionRoutineResolver(const ExtensionRoutineResolver &) = delete;
  ExtensionRoutineResolver &operator=(const ExtensionRoutineResolver &) = delete;
  // Services must be bound by the host, not supplied by a third-party plugin.
  // All statements are preflighted before resolving any; failure exposes no args.
  // Does not start transactions, execute commands, or install Extension members.
  int resolve(const ExtensionScript &script, const ObResolverParams &services,
              const ObSqlCtx &context, uint64_t database_id, std::string &error);
  // Resolve exactly one CREATE/ALTER/DROP against the caller's current view.
  // Each call has fresh query/expression/package/diagnostic state and returns one owned
  // wire operation, or clears output on failure. It neither stages the result
  // nor allocates IDs: the host must admit/stage it before resolving the next
  // statement. context may carry the coordinator's routine overlay; an overlay
  // is not authorization and all ordinary privilege checks still run.
  static int resolve_statement(const ExtensionScript &script, int64_t index,
      const ObResolverParams &services, const ObSqlCtx &context, uint64_t database_id,
      ExtensionRoutineUpdateBatch &output, std::string &error);
  // Executes one operation in the current transaction via the Rust operation
  // driver. Only APPLIED exposes an ID; it remains provisional until commit.
  static int mutate(ObExecContext &context, const std::string &sql, uint64_t &object_id,
      share::schema::CatalogOperationResult &result, std::string &error);
  // Root owns the transaction, reserves identities/versions, and advances its
  // view between callbacks. Releases the caller's old guard before Root entry.
  static int update(const ExtensionUpdatePlan &plan, const ObResolverParams &services,
      const ObSqlCtx &context, uint64_t &extension_id, bool &changed,
      int &publication_status, std::string &error);
  // Host orchestration for SQL routine members, optionally associated with an
  // already installed native module (no module load/unload here). Resolver
  // checks precede the internally serialized Rootserver command. Not SQL syntax.
  // An active caller transaction is rejected, never implicitly committed here.
  // Consumes/releases context.schema_guard_ after successful preflight, before
  // entering Rootserver; the caller must not use that old schema snapshot again.
  int install(const ExtensionScript &script, const ObResolverParams &services,
              const ObSqlCtx &context, uint64_t database_id,
              uint64_t &extension_id, int &publication_status, std::string &error,
              share::plugin::ICatalogBuildProgram *program = nullptr);
  void reset();
  const common::ObIArray<const obcall::ObCreateRoutineArg *> &args() const { return batch_.args(); }

private:
  struct Impl;
  ExtensionRoutineBatch batch_;
};

} }
#endif
