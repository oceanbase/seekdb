/* Copyright (c) 2026 OceanBase. Licensed under the Apache License, Version 2.0. */
#ifndef SEEKDB_SQL_CALLER_CATALOG_TRANSACTION_H_
#define SEEKDB_SQL_CALLER_CATALOG_TRANSACTION_H_
#include <memory>
#include <cstdint>
namespace oceanbase {
namespace common { class ObMySQLTransaction; class ObISQLClient; }
namespace transaction { class ObTxSEQ; }
namespace rootserver { class IRoutineCacheInvalidation; }
namespace share { namespace schema {
class RoutineCatalogTransaction;
class ObMultiVersionSchemaService;
class RoutineSchemaOverlay;
class RoutinePrivilegeOverlay;
struct CatalogOperationResult;
} }
namespace sql {
class ObExecContext;
class ObSQLSessionInfo;
// Trusted Query implementation, NOT a callback exposed to native plugins.
// preflight performs ordinary semantic resolution/ACL without effects. apply
// uses that owned result after DDL admission, rechecking object/dependency state
// and using normal reservations/writers. No borrowed SQL result or reservation
// may escape apply, including on exceptions. It must stage schema and ACL only
// after admission, on the supplied pair, and must not commit/publish/start a TX.
class ICallerCatalogMutation
{
public:
  virtual ~ICallerCatalogMutation() = default;
  virtual int preflight(ObExecContext &context) = 0;
  virtual int apply(ObExecContext &context, common::ObMySQLTransaction &transaction,
      share::schema::RoutineSchemaOverlay &schema,
      share::schema::RoutinePrivilegeOverlay &privileges,
      rootserver::IRoutineCacheInvalidation &invalidation) = 0;
};
// Concrete query frame: actual statement/data savepoints, session-owned views,
// DDL epoch/lock, borrowed SQL and abort/poison. Rust drives the effect ordering.
// Requires exclusive session ownership and the real current SQL execution.
int run_caller_catalog_operation(ObExecContext &context, ICallerCatalogMutation &mutation,
    share::schema::CatalogOperationResult &result) noexcept;
// Host-only SQL transport for already authorized catalog operations. Call after
// preparing the caller's plugin SQL/statement savepoints and acquiring schema
// locks. It neither creates a transaction nor grants catalog privileges.
// All result sets and reservation tokens must die before close/destruction.
// Entire lifetime is exclusive and on the opening thread; it cannot cross an
// async boundary or outlive the caller frame/session.
class CallerCatalogTransaction final
{
public:
  CallerCatalogTransaction();
  ~CallerCatalogTransaction();
  CallerCatalogTransaction(const CallerCatalogTransaction &) = delete;
  CallerCatalogTransaction &operator=(const CallerCatalogTransaction &) = delete;
  int open(ObExecContext &context);
  // barrier must be the enclosing object's actual data savepoint, already
  // registered with the session's paired view journal. No interleaved mutation.
  int open(ObExecContext &context, const transaction::ObTxSEQ &barrier);
  // Query transport: AFTER object ACL and paired view/data barrier, BEFORE any
  // generated catalog write. Records or reuses this transaction's DDL lock/epoch.
  // fresh_reader is a host current-read client, not this caller's RR snapshot.
  int admit_ddl(share::schema::RoutineCatalogTransaction &journal,
      share::schema::ObMultiVersionSchemaService &service, common::ObISQLClient &fresh_reader,
      int64_t refreshed_schema_version, int64_t absolute_deadline);
  // Commit-phase transport: no physical plan is required. The session must own
  // this journal in Preparing, with the expected actual data transaction. Host
  // must already own DDL locks/captured epoch and the exclusive session lock.
  // Only finalizer-generated SQL is allowed; logged operations are end-signs.
  // Close BEFORE complete_prepare/commit and before destroying the journal.
  int open_for_commit(ObSQLSessionInfo &session,
      share::schema::RoutineCatalogTransaction &journal, int64_t transaction_id,
      int64_t sequence_base, int64_t absolute_deadline);
  // Preflight for generated catalog SQL, also usable before opening transport.
  static int validate_statement(ObSQLSessionInfo &session, const char *sql, bool write);
  common::ObMySQLTransaction *transaction();
  int status() const;
  // Borrowed host-only sink for an admitted query writer; null in commit mode
  // or on invalid context. The sink dies at close, records only, and rechecks
  // session/transaction identity on every invocation. No cache/SQL callbacks.
  rootserver::IRoutineCacheInvalidation *invalidation_sink();
  // Returns the first transport/context/restore failure; NEVER commits/aborts.
  int close();
  // Separate restoration failure from the sticky SQL/operation failure. The
  // query operation driver must not mistake an ordinary failed write, followed
  // by successful cleanup, for an unsafe context that cannot roll back.
  int close(int &cleanup_result);
private:
  int open_impl(ObExecContext &context, const transaction::ObTxSEQ &barrier);
  struct Impl;
  std::unique_ptr<Impl> impl_;
  bool used_ = false;
  int result_ = 0;
  int cleanup_result_ = 0;
};
} }
#endif
