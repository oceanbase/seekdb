/* Copyright (c) 2026 OceanBase. Licensed under the Apache License, Version 2.0. */
#include "share/schema/routine_catalog_transaction.h"
#include "share/schema/routine_catalog_savepoint.h"
#include "data_plane/transaction/ob_tx_seq.h"
#include "plugin_runtime.h"
#include <new>
namespace oceanbase { namespace share { namespace schema {
namespace {
int32_t catalog_operation_step(void *opaque, uint32_t phase, int32_t cause) noexcept
{
  auto &host = *static_cast<ICatalogOperationHost *>(opaque);
  try {
    switch (phase) {
      case SEEKDB_RUNTIME_QUERY_PREFLIGHT: return host.preflight();
      case SEEKDB_RUNTIME_QUERY_PREPARE: return host.prepare();
      case SEEKDB_RUNTIME_QUERY_APPLY: return host.apply();
      case SEEKDB_RUNTIME_QUERY_CLOSE: return host.close();
      case SEEKDB_RUNTIME_QUERY_CHECK_TRANSACTION: return host.check_transaction();
      case SEEKDB_RUNTIME_QUERY_ROLLBACK_DATA: return host.rollback_data();
      case SEEKDB_RUNTIME_QUERY_ROLLBACK_VIEW: return host.rollback_view();
      case SEEKDB_RUNTIME_QUERY_POISON: return host.poison(cause);
      default: return common::OB_INVALID_ARGUMENT;
    }
  } catch (const std::bad_alloc &) { return common::OB_ALLOCATE_MEMORY_FAILED; }
  catch (...) { return common::OB_ERR_UNEXPECTED; }
}
}
int run_catalog_operation(ICatalogOperationHost &host, CatalogOperationResult &result) noexcept
{
  result = {};
  seekdb_runtime_query_operation_result output{};
  const int32_t status = seekdb_runtime_query_operation_run(&host, catalog_operation_step, &output);
  if (status != SEEKDB_RUNTIME_OK) return common::OB_ERR_UNEXPECTED;
  switch (output.outcome) {
    case SEEKDB_RUNTIME_QUERY_NOT_STARTED: result.outcome_ = CatalogOperationResult::Outcome::NOT_STARTED; break;
    case SEEKDB_RUNTIME_QUERY_APPLIED: result.outcome_ = CatalogOperationResult::Outcome::APPLIED; break;
    case SEEKDB_RUNTIME_QUERY_ROLLED_BACK: result.outcome_ = CatalogOperationResult::Outcome::ROLLED_BACK; break;
    case SEEKDB_RUNTIME_QUERY_REQUIRES_ABORT: result.outcome_ = CatalogOperationResult::Outcome::REQUIRES_ABORT; break;
    default: return common::OB_ERR_UNEXPECTED;
  }
  result.failed_phase_ = output.failed_phase;
  result.operation_error_ = output.operation_error;
  result.close_error_ = output.close_error;
  result.identity_error_ = output.identity_error;
  result.data_rollback_error_ = output.data_rollback_error;
  result.view_rollback_error_ = output.view_rollback_error;
  result.poison_error_ = output.poison_error;
  return output.operation_error;
}
RoutineInvalidationQueue::RoutineInvalidationQueue(uint32_t batches, uint32_t requests)
    : handle_(seekdb_runtime_invalidation_queue_create(batches, requests)) {}
RoutineInvalidationQueue::~RoutineInvalidationQueue()
{ seekdb_runtime_invalidation_queue_destroy(handle_); }
void RoutineInvalidationQueue::close() noexcept
{ if (handle_) (void)seekdb_runtime_invalidation_queue_close(handle_, 0); }
int RoutineInvalidationQueue::reserve(RoutineCatalogTransaction &journal, uint64_t transaction_id) noexcept
{
  if (!handle_ || !journal.handle_) return common::OB_NOT_INIT;
  return RoutineCatalogTransaction::translate(seekdb_runtime_query_transaction_reserve_invalidations(
      handle_, journal.handle_, transaction_id), 0);
}
int RoutineInvalidationQueue::process(IRoutineCacheEvictor &evictor, uint32_t budget, uint32_t &processed) noexcept
{
  using namespace common;
  processed = 0;
  if (!handle_) return OB_NOT_INIT;
  if (budget == 0 || budget > 64) return OB_INVALID_ARGUMENT;
  for (uint32_t i = 0; i < budget; ++i) {
    uint64_t token = 0, version = 0;
    seekdb_runtime_routine_invalidation request{};
    int ret = RoutineCatalogTransaction::translate(seekdb_runtime_invalidation_queue_peek(handle_, &token, &version, &request), 0);
    if (ret != OB_SUCCESS || token == 0) return ret;
    try {
      ret = evictor.check_schema_version(static_cast<int64_t>(version));
      if (ret == OB_SUCCESS) ret = evictor.evict(request.database, request.routine);
    }
    catch (const std::bad_alloc &) { ret = OB_ALLOCATE_MEMORY_FAILED; }
    catch (...) { ret = OB_ERR_UNEXPECTED; }
    const int ack = RoutineCatalogTransaction::translate(seekdb_runtime_invalidation_queue_complete(
        handle_, token, request.ticket, ret == OB_SUCCESS ? 1 : 0), 0);
    if (ret != OB_SUCCESS) return ret;
    if (ack != OB_SUCCESS) return ack;
    ++processed;
  }
  return OB_SUCCESS;
}
RoutineCatalogTransaction::RoutineCatalogTransaction(uint64_t transaction_id)
    : handle_(seekdb_runtime_query_transaction_create(transaction_id)) {}
RoutineCatalogTransaction::~RoutineCatalogTransaction()
{ seekdb_runtime_query_transaction_destroy(handle_); }
int RoutineCatalogTransaction::record(uint64_t transaction_id, const transaction::ObTxSEQ &barrier,
    std::shared_ptr<RoutineSchemaOverlay> schema, std::shared_ptr<RoutinePrivilegeOverlay> privileges)
{
    using namespace common;
    if (!handle_) return OB_NOT_INIT;
    if (!barrier.is_valid()) return OB_INVALID_ARGUMENT;
    if (barrier.get_branch() != 0) return OB_NOT_SUPPORTED;
    auto *mark = new (std::nothrow) RoutineCatalogSavepoint(std::move(schema), std::move(privileges));
    if (!mark) return OB_ALLOCATE_MEMORY_FAILED;
    if (!mark->valid()) { delete mark; return OB_INVALID_ARGUMENT; }
    const int ret = translate(seekdb_runtime_query_transaction_record(handle_, transaction_id,
        barrier.get_seq(), mark, undo, release), 0);
    if (ret != OB_SUCCESS) release(mark); // No transfer on failed admission.
    return ret;
  }
int RoutineCatalogTransaction::rollback(uint64_t transaction_id, const transaction::ObTxSEQ &resolved) noexcept
{
    if (!handle_) return common::OB_NOT_INIT;
    if (!resolved.is_valid()) return common::OB_INVALID_ARGUMENT;
    if (resolved.get_branch() != 0) return common::OB_NOT_SUPPORTED;
    int32_t error = 0;
    const int32_t status = seekdb_runtime_query_transaction_rollback(handle_, transaction_id,
        resolved.get_seq(), &error);
    return translate(status, error);
  }
int RoutineCatalogTransaction::prepare_commit(uint64_t transaction_id) noexcept
{
  if (!handle_) return common::OB_NOT_INIT;
  int32_t error = 0;
  const int32_t status = seekdb_runtime_query_transaction_prepare_commit(handle_, transaction_id, &error);
  return translate(status, error);
}
int RoutineCatalogTransaction::fail(uint64_t transaction_id, int host_error) noexcept
{
  if (!handle_) return common::OB_NOT_INIT;
  return translate(seekdb_runtime_query_transaction_fail(handle_, transaction_id, host_error), 0);
}
int RoutineCatalogTransaction::begin_prepare(uint64_t transaction_id,
    uint64_t &version, uint64_t &operations) noexcept
{
  version = 0; operations = 0;
  if (!handle_) return common::OB_NOT_INIT;
  int32_t error = 0;
  const int32_t status = seekdb_runtime_query_transaction_begin_prepare(
      handle_, transaction_id, &version, &operations, &error);
  return translate(status, error);
}
int RoutineCatalogTransaction::prepare_commit(uint64_t transaction_id, ICatalogCommitHost &host) noexcept
{
  using namespace common;
  uint64_t version = 0, operations = 0, epoch = 0, sequence = 0;
  int ret = schema_state(transaction_id, version, operations);
  if (ret != OB_SUCCESS) return ret;
  if (operations == 0) return prepare_commit(transaction_id);
  if ((ret = ddl_admission(transaction_id, epoch, sequence)) != OB_SUCCESS) return ret;
  if (epoch == 0 || sequence == 0) return OB_STATE_NOT_MATCH;
  if ((ret = begin_prepare(transaction_id, version, operations)) != OB_SUCCESS) return ret;
  int64_t prepared = 0;
  try {
    ret = host.check_transaction();
    if (ret == OB_SUCCESS) ret = host.prepare(version, epoch, prepared);
  } catch (const std::bad_alloc &) { ret = OB_ALLOCATE_MEMORY_FAILED; }
  catch (...) { ret = OB_ERR_UNEXPECTED; }
  // No Rust FFI call/borrow remains active during these host effects. Preserve
  // the first error, but always restore the transport before poisoning/sealing.
  int close_ret = OB_SUCCESS;
  try { close_ret = host.close(); }
  catch (const std::bad_alloc &) { close_ret = OB_ALLOCATE_MEMORY_FAILED; }
  catch (...) { close_ret = OB_ERR_UNEXPECTED; }
  if (ret == OB_SUCCESS) ret = close_ret;
  if (ret == OB_SUCCESS) {
    try { ret = host.check_transaction(); }
    catch (...) { ret = OB_ERR_UNEXPECTED; }
  }
  return complete_prepare(transaction_id, prepared > 0 ? prepared : 0, ret);
}
int RoutineCatalogTransaction::check_preparing(uint64_t transaction_id) const noexcept
{
  if (!handle_) return common::OB_NOT_INIT;
  int32_t error = 0;
  const int32_t status = seekdb_runtime_query_transaction_check_preparing(handle_, transaction_id, &error);
  return translate(status, error);
}
int RoutineCatalogTransaction::admit_ddl(uint64_t transaction_id,
    const transaction::ObTxSEQ &barrier, int64_t epoch) noexcept
{
  if (!handle_) return common::OB_NOT_INIT;
  if (!barrier.is_valid() || epoch <= 0) return common::OB_INVALID_ARGUMENT;
  if (barrier.get_branch() != 0) return common::OB_NOT_SUPPORTED;
  return translate(seekdb_runtime_query_transaction_admit_ddl(handle_, transaction_id, barrier.get_seq(), epoch), 0);
}
int RoutineCatalogTransaction::ddl_admission(uint64_t transaction_id,
    uint64_t &epoch, uint64_t &sequence) const noexcept
{
  epoch = 0; sequence = 0;
  if (!handle_) return common::OB_NOT_INIT;
  int32_t error = 0;
  const int32_t status = seekdb_runtime_query_transaction_ddl_admission(handle_, transaction_id, &epoch, &sequence, &error);
  return translate(status, error);
}
int RoutineCatalogTransaction::check_ddl_write(uint64_t transaction_id,
    const transaction::ObTxSEQ &barrier) const noexcept
{
  if (!handle_) return common::OB_NOT_INIT;
  if (!barrier.is_valid()) return common::OB_INVALID_ARGUMENT;
  if (barrier.get_branch() != 0) return common::OB_NOT_SUPPORTED;
  int32_t error = 0;
  const int32_t status = seekdb_runtime_query_transaction_check_ddl_write(handle_, transaction_id, barrier.get_seq(), &error);
  return translate(status, error);
}
int RoutineCatalogTransaction::record_end_sign(uint64_t transaction_id, int64_t version) noexcept
{
  if (!handle_) return common::OB_NOT_INIT;
  if (version <= 0) return common::OB_INVALID_ARGUMENT;
  return translate(seekdb_runtime_query_transaction_record_end_sign(handle_, transaction_id, version), 0);
}
int RoutineCatalogTransaction::complete_prepare(uint64_t transaction_id, uint64_t version,
    int host_result) noexcept
{
  if (!handle_) return common::OB_NOT_INIT;
  int32_t error = 0;
  const int32_t status = seekdb_runtime_query_transaction_complete_prepare(
      handle_, transaction_id, version, host_result, &error);
  return translate(status, error);
}
int RoutineCatalogTransaction::finish(uint64_t transaction_id, bool committed) noexcept
{
    if (!handle_) return common::OB_NOT_INIT;
    int32_t error = 0;
    const int32_t status = seekdb_runtime_query_transaction_finish(handle_, transaction_id,
        committed ? 1 : 0, &error);
    return translate(status, error);
  }
int RoutineCatalogTransaction::record_schema_version(uint64_t transaction_id,
    const transaction::ObTxSEQ &barrier, int64_t version) noexcept
{
  if (!handle_) return common::OB_NOT_INIT;
  if (!barrier.is_valid() || version <= 0) return common::OB_INVALID_ARGUMENT;
  if (barrier.get_branch() != 0) return common::OB_NOT_SUPPORTED;
  return translate(seekdb_runtime_query_transaction_record_schema_version(
      handle_, transaction_id, barrier.get_seq(), version), 0);
}
int RoutineCatalogTransaction::schema_state(uint64_t transaction_id,
    uint64_t &version, uint64_t &operations) noexcept
{
  version = 0; operations = 0;
  if (!handle_) return common::OB_NOT_INIT;
  int32_t error = 0;
  const int32_t status = seekdb_runtime_query_transaction_schema_state(
      handle_, transaction_id, &version, &operations, &error);
  return translate(status, error);
}
int RoutineCatalogTransaction::record_invalidation(uint64_t transaction_id,
    const transaction::ObTxSEQ &barrier, uint64_t database, uint64_t routine) noexcept
{
  if (!handle_) return common::OB_NOT_INIT;
  if (!barrier.is_valid()) return common::OB_INVALID_ARGUMENT;
  if (barrier.get_branch() != 0) return common::OB_NOT_SUPPORTED;
  return translate(seekdb_runtime_query_transaction_record_invalidation(
      handle_, transaction_id, barrier.get_seq(), database, routine), 0);
}
int RoutineCatalogTransaction::invalidation_count(uint64_t transaction_id, uint64_t &count) const noexcept
{
  count = 0;
  if (!handle_) return common::OB_NOT_INIT;
  int32_t error = 0;
  const int32_t status = seekdb_runtime_query_transaction_invalidation_count(handle_, transaction_id, &count, &error);
  return translate(status, error);
}
int RoutineCatalogTransaction::peek_invalidation(uint64_t transaction_id, uint64_t &ticket,
    uint64_t &database, uint64_t &routine) const noexcept
{
  ticket = 0; database = 0; routine = 0;
  if (!handle_) return common::OB_NOT_INIT;
  seekdb_runtime_routine_invalidation request{};
  const int32_t status = seekdb_runtime_query_transaction_peek_invalidation(handle_, transaction_id, &request);
  ticket = request.ticket; database = request.database; routine = request.routine;
  return translate(status, 0);
}
int RoutineCatalogTransaction::ack_invalidation(uint64_t transaction_id, uint64_t ticket) noexcept
{
  if (!handle_) return common::OB_NOT_INIT;
  return translate(seekdb_runtime_query_transaction_ack_invalidation(handle_, transaction_id, ticket), 0);
}
int32_t RoutineCatalogTransaction::undo(void *payload) noexcept
{
    auto *mark = static_cast<RoutineCatalogSavepoint *>(payload);
    const int result = mark->rollback();
    delete mark;
    return result;
  }
void RoutineCatalogTransaction::release(void *payload) noexcept
{
    auto *mark = static_cast<RoutineCatalogSavepoint *>(payload);
    mark->release(); delete mark;
  }
int RoutineCatalogTransaction::translate(int32_t status, int32_t error) noexcept
{
    using namespace common;
    if (error != 0) return error;
    switch (status) {
      case SEEKDB_RUNTIME_OK: return OB_SUCCESS;
      case SEEKDB_RUNTIME_INVALID: return OB_INVALID_ARGUMENT;
      case SEEKDB_RUNTIME_NO_MEMORY: return OB_ALLOCATE_MEMORY_FAILED;
      case SEEKDB_RUNTIME_LIMIT: return OB_SIZE_OVERFLOW;
      default: return OB_STATE_NOT_MATCH;
    }
  }
} } }
