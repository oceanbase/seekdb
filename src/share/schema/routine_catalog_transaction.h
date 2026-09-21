/* Copyright (c) 2026 OceanBase. Licensed under the Apache License, Version 2.0. */
#ifndef SEEKDB_SCHEMA_ROUTINE_CATALOG_TRANSACTION_H_
#define SEEKDB_SCHEMA_ROUTINE_CATALOG_TRANSACTION_H_
#include <cstdint>
#include <memory>

struct seekdb_runtime_query_transaction;
struct seekdb_runtime_invalidation_queue;
namespace oceanbase {
namespace transaction { class ObTxSEQ; }
namespace share { namespace schema {
class RoutineSchemaOverlay;
class RoutinePrivilegeOverlay;
// Host-only effect frame for ONE operation in the caller's transaction. No
// method may commit/publish or substitute an independent installation TX.
// preflight is read-only ordinary parsing/ACL; prepare records real data/view
// barriers; apply performs admitted catalog writes; close always releases all
// resources. Rollback methods tolerate a prepare failure before any barrier.
// Each effect checks captured session/TX identity before touching host state.
class ICatalogOperationHost
{
public:
  virtual ~ICatalogOperationHost() = default;
  virtual int preflight() = 0;
  virtual int prepare() = 0;
  virtual int apply() = 0;
  virtual int close() = 0;
  virtual int check_transaction() = 0;
  virtual int rollback_data() = 0;
  virtual int rollback_view() = 0;
  // No allocation required: poison the captured journal and retire its paired
  // views BEFORE any optional data abort. Never poison a replacement session TX.
  virtual int poison(int cause) = 0;
};
struct CatalogOperationResult
{
  enum class Outcome { NOT_STARTED, APPLIED, ROLLED_BACK, REQUIRES_ABORT };
  Outcome outcome_ = Outcome::NOT_STARTED;
  uint32_t failed_phase_ = 0;
  int operation_error_ = 0;
  int close_error_ = 0;
  int identity_error_ = 0;
  int data_rollback_error_ = 0;
  int view_rollback_error_ = 0;
  int poison_error_ = 0;
};
// Rust owns effect ordering, C++ owns the host frame. No Rust journal borrow
// spans host SQL. APPLIED is provisional; only then may host expose object IDs.
int run_catalog_operation(ICatalogOperationHost &host, CatalogOperationResult &result) noexcept;
// Host-only effects for the final commit stage. No transaction ownership and
// no plugin callbacks. close() must release all borrowed SQL resources even
// after prepare fails; check_transaction() must not execute SQL.
class ICatalogCommitHost
{
public:
  virtual ~ICatalogCommitHost() = default;
  virtual int check_transaction() = 0;
  virtual int prepare(int64_t last_version, int64_t captured_epoch, int64_t &prepared_version) = 0;
  virtual int close() = 0;
};
// Host-only, exclusively owned transaction-local view marks. No transaction
// start/commit/publication authority. See plugin-catalog-savepoints.md.
class RoutineCatalogTransaction final
{
public:
  explicit RoutineCatalogTransaction(uint64_t transaction_id);
  ~RoutineCatalogTransaction();
  RoutineCatalogTransaction(const RoutineCatalogTransaction &) = delete;
  RoutineCatalogTransaction &operator=(const RoutineCatalogTransaction &) = delete;
  bool valid() const noexcept { return handle_ != nullptr; }
  // Record before mutation, after obtaining the actual root-branch data barrier.
  int record(uint64_t transaction_id, const transaction::ObTxSEQ &barrier,
      std::shared_ptr<RoutineSchemaOverlay> schema,
      std::shared_ptr<RoutinePrivilegeOverlay> privileges);
  // Called only after successful data rollback.
  int rollback(uint64_t transaction_id, const transaction::ObTxSEQ &resolved) noexcept;
  // Unsafe host cleanup: no further operation/commit admission. Does not undo,
  // abort data or retire schema storage; the host also revokes the paired view.
  int fail(uint64_t transaction_id, int host_error) noexcept;
  // View-only fast path. Schema writes require the preparation protocol below.
  int prepare_commit(uint64_t transaction_id) noexcept;
  // Freeze -> host epoch/end-sign/MDS/watermark -> close -> identity check ->
  // seal. Caller retains this journal across host SQL and aborts on any error.
  int prepare_commit(uint64_t transaction_id, ICatalogCommitHost &host) noexcept;
  // Freeze ordinary marks/operations/savepoint rollback before host database
  // preparation. No Rust borrow spans SQL or any host callback.
  int begin_prepare(uint64_t transaction_id, uint64_t &version, uint64_t &operations) noexcept;
  int record_end_sign(uint64_t transaction_id, int64_t version) noexcept;
  int check_preparing(uint64_t transaction_id) const noexcept;
  int admit_ddl(uint64_t transaction_id, const transaction::ObTxSEQ &barrier, int64_t epoch) noexcept;
  int ddl_admission(uint64_t transaction_id, uint64_t &epoch, uint64_t &sequence) const noexcept;
  int check_ddl_write(uint64_t transaction_id, const transaction::ObTxSEQ &barrier) const noexcept;
  // One attempt, after SQL/MDS/watermark AND transport cleanup. Failure leaves
  // only full abort/discard legal; success seals but does not commit or publish.
  int complete_prepare(uint64_t transaction_id, uint64_t version, int host_result) noexcept;
  int record_schema_version(uint64_t transaction_id, const transaction::ObTxSEQ &barrier,
      int64_t version) noexcept;
  int schema_state(uint64_t transaction_id, uint64_t &version, uint64_t &operations) noexcept;
  // Same admitted barrier as a successful schema write. Private request only;
  // savepoint rollback removes it. Does not flush, commit, or authorize DROP.
  int record_invalidation(uint64_t transaction_id, const transaction::ObTxSEQ &barrier,
      uint64_t database, uint64_t routine) noexcept;
  int invalidation_count(uint64_t transaction_id, uint64_t &count) const noexcept;
  // Available ONLY after finish(true). Empty returns all zeros. Retain this
  // owner across host scheduling; acknowledge only after successful handoff.
  int peek_invalidation(uint64_t transaction_id, uint64_t &ticket,
      uint64_t &database, uint64_t &routine) const noexcept;
  int ack_invalidation(uint64_t transaction_id, uint64_t ticket) noexcept;
  // Notification of a VERIFIED data outcome, not commit/abort authority.
  // Unknown outcome: invalidate access then destroy the private view owner.
  // Known commit retains pending cache requests until host acknowledgement.
  int finish(uint64_t transaction_id, bool committed) noexcept;
private:
  friend class RoutineInvalidationQueue;
  static int32_t undo(void *payload) noexcept;
  static void release(void *payload) noexcept;
  static int translate(int32_t status, int32_t error) noexcept;
  seekdb_runtime_query_transaction *handle_;
};
class IRoutineCacheEvictor
{
public:
  virtual ~IRoutineCacheEvictor() = default;
  virtual int check_schema_version(int64_t required_version) = 0;
  virtual int evict(uint64_t database, uint64_t routine) = 0;
};
// One owner per volatile plan cache. Reservation holds scalar data independent
// of session/journal lifetime; a single cache worker drains it. Destruction is
// cache retirement: only legal after the worker and all cache access have ended.
class RoutineInvalidationQueue final
{
public:
  explicit RoutineInvalidationQueue(uint32_t batches = 256, uint32_t requests = 65536);
  ~RoutineInvalidationQueue();
  RoutineInvalidationQueue(const RoutineInvalidationQueue &) = delete;
  RoutineInvalidationQueue &operator=(const RoutineInvalidationQueue &) = delete;
  bool valid() const noexcept { return handle_ != nullptr; }
  int reserve(RoutineCatalogTransaction &journal, uint64_t transaction_id) noexcept;
  // Close admission, not already accepted promises. Worker can continue draining.
  void close() noexcept;
  // No Rust borrow spans evict(). Failed eviction remains queued for retry;
  // each call is bounded. Callbacks must not destroy/reenter this queue.
  int process(IRoutineCacheEvictor &evictor, uint32_t budget, uint32_t &processed) noexcept;
private:
  seekdb_runtime_invalidation_queue *handle_;
};
} } }
#endif
