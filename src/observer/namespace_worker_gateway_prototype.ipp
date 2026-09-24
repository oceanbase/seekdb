// Included in the Observer composition unit for in-process namespace storage.
#include "observer/namespace_worker_protocol_prototype.h"
#include "rootserver/fork_table/namespace_fork_kernel_prototype.h"
#include "rootserver/ddl_task/ob_ddl_task_util.h"
#include "storage/compaction/ob_freeze_info_mgr.h"
#include <map>
#include <mutex>
#include <string>
#include <vector>
#include "observer/namespace_worker_scan_prototype.ipp"
#include "observer/namespace_worker_write_prototype.ipp"
#include "observer/namespace_worker_direct_insert_prototype.ipp"
#include "observer/namespace_worker_commands_prototype.ipp"
#include "namespace/namespace.h"
namespace oceanbase { namespace observer { namespace namespace_worker_prototype {
using namespace common;
using namespace share::schema;
using storage::NamespaceForkKernelPrototype;
int acquire_storage_snapshot(int64_t &snapshot) {
  snapshot = 0;
  // Freeze metadata remains SQL/schema state in the namespace worker. The
  // helper obtains only the transaction clock through ObITransactionService.
  return rootserver::ObDDLTaskUtil::calc_snapshot_with_gts(snapshot);
}
int reload_storage_freeze_info() {
  auto *freeze = share::server_service<storage::ObFreezeInfoMgr>();
  return freeze ? freeze->reload_for_test() : OB_NOT_INIT;
}
int drain_storage_namespace_access(uint64_t namespace_id) {
  if (namespace_id <= 1 || namespace_id >= (1ULL << 30)) {
    return OB_INVALID_ARGUMENT;
  }
  return storage::NamespaceForkKernelPrototype::drain_access();
}
int release_storage_namespace_schemas(uint64_t namespace_id,
                                      int64_t &table_count,
                                      int64_t &database_count) {
  table_count = 0;
  database_count = 0;
  if (namespace_id <= 1 || namespace_id >= (1ULL << 30)) {
    return OB_INVALID_ARGUMENT;
  }
  return storage::NamespaceForkKernelPrototype::release_namespace_schemas(
      namespace_id, table_count, database_count);
}
struct SessionBinding {
  InProcessStorage *in_process = nullptr;
};
// One native storage context belongs to each forked-namespace SQL session.
struct InProcessStorage {
  const uint64_t ns;
  const StorageSpaceHandle storage_space;
  std::shared_ptr<StorageSessionState> session_state;
  sql::ObSQLSessionInfo &session; // storage-side native session
  std::unique_ptr<EngineWrites> writes;
  ReadScans scans;
  DirectInsertRoute direct_insert;
  DirectInsertRegistry *direct_insert_registry = nullptr;
  RequestTag direct_insert_tag;
  sql::ObSQLSessionInfo *sql_session = nullptr; // switch key, not an owner
  bool initialized = false;
  explicit InProcessStorage(uint64_t namespace_id)
      : ns(namespace_id),
        storage_space(StorageSpaceHandle::namespace_space(namespace_id)),
        session_state(std::make_shared<StorageSessionState>()),
        session(session_state->session),
        scans(storage_space) {
    ::oceanbase::ns::NamespaceRuntime *runtime = nullptr;
    if (::oceanbase::ns::namespace_registry().get(namespace_id, runtime) && runtime != nullptr) {
      direct_insert_registry = static_cast<DirectInsertRegistry *>(
          runtime->service(::oceanbase::ns::NamespaceRuntime::DIRECT_INSERT_REGISTRY));
    }
  }
  ~InProcessStorage() {
    if (direct_insert_registry != nullptr && direct_insert_tag.slot) {
      direct_insert_registry->release(direct_insert_tag);
    }
  }
  InProcessStorage(const InProcessStorage &) = delete;
  InProcessStorage &operator=(const InProcessStorage &) = delete;
  bool owns(StorageSpaceHandle space) const { return space == storage_space; }
};
int call_in_process_rootserver_runtime(
    uint64_t namespace_id,
    const std::function<int(rootserver::ObIRootserverLocalRuntime &,
                            StorageSpaceHandle)> &call)
{
  InProcessServingScope serving(namespace_id);
  IndependentStorageScope scope;
  if (scope.error()) { return scope.error(); }
  InProcessStorage *storage = in_process_storage;
  auto *runtime = share::server_service<rootserver::ObIRootserverLocalRuntime>();
  if (storage == nullptr || !storage->initialized || runtime == nullptr) {
    return OB_NOT_INIT;
  }
  const StorageSpaceHandle space = namespace_id == 0
      ? active_worker_storage_space()
      : StorageSpaceHandle::namespace_space(namespace_id);
  if (!space.is_namespace() || storage->ns != space.namespace_id()) {
    return OB_INVALID_ARGUMENT;
  }
  auto *old_session = THIS_WORKER.get_session();
  const int64_t old_timeout = THIS_WORKER.get_timeout_ts();
  THIS_WORKER.set_session(&storage->session);
  const int ret = call(*runtime, space);
  THIS_WORKER.set_session(old_session);
  THIS_WORKER.set_timeout_ts(old_timeout);
  return ret;
}
int read_in_process_lob(ObLobLocatorV2 &locator, int64_t timeout,
                        ObIAllocator &allocator, ObString &output)
{
  output.reset();
  InProcessStorage *storage = in_process_storage;
  if (storage == nullptr || !storage->initialized || !storage->writes) {
    return OB_NOT_INIT;
  }
  const StorageSpaceHandle space = active_worker_storage_space();
  if (!space.is_namespace() || !locator.is_valid() || !locator.has_lob_header()
      || !locator.is_persist_lob()) {
    return OB_INVALID_ARGUMENT;
  }
  int64_t length = 0;
  int ret = locator.get_lob_data_byte_len(length);
  if (!ret && (length < 0 || length > static_cast<int64_t>(MAX_SQL_MESSAGE - 64))) {
    ret = OB_SIZE_OVERFLOW;
  } else if (!ret && length > 0) {
    char *buffer = static_cast<char *>(allocator.alloc(length));
    if (buffer == nullptr) { ret = OB_ALLOCATE_MEMORY_FAILED; }
    else { output.assign_buffer(buffer, static_cast<int32_t>(length)); }
  }
  if (!ret) {
    auto *old_session = THIS_WORKER.get_session();
    const int64_t old_timeout = THIS_WORKER.get_timeout_ts();
    THIS_WORKER.set_session(&storage->session);
    ret = data_plane::read_lob_to_buffer(allocator, locator,
        std::min(timeout, old_timeout), storage->writes->tx, output);
    THIS_WORKER.set_session(old_session);
    THIS_WORKER.set_timeout_ts(old_timeout);
  }
  return ret;
}
int compare_in_process_lobs(ObLobLocatorV2 &left, ObLobLocatorV2 &right,
                            int64_t timeout, ObTxDesc &tx, bool &equal)
{
  InProcessStorage *storage = in_process_storage;
  if (storage == nullptr || !storage->initialized || !storage->writes) {
    return OB_NOT_INIT;
  }
  const StorageSpaceHandle space = active_worker_storage_space();
  ObTxDesc *storage_tx = storage->writes->tx;
  if (!space.is_namespace() || storage->ns != space.namespace_id()
      || storage_tx == nullptr || storage_tx->get_tx_id() != tx.get_tx_id()) {
    return OB_INVALID_ARGUMENT;
  }
  auto *old_session = THIS_WORKER.get_session();
  const int64_t old_timeout = THIS_WORKER.get_timeout_ts();
  THIS_WORKER.set_session(&storage->session);
  const int ret = data_plane::lob_binary_equal(
      left, right, std::min(timeout, old_timeout), storage_tx, equal);
  THIS_WORKER.set_session(old_session);
  THIS_WORKER.set_timeout_ts(old_timeout);
  return ret;
}
int call_in_process_tx_clock(
    const std::function<int(ObITransactionService &)> &call)
{
  IndependentStorageScope scope;
  if (scope.error()) { return scope.error(); }
  InProcessStorage *storage = in_process_storage;
  ObITransactionService *service = data_plane::query_transaction_service();
  if (storage == nullptr || !storage->initialized || service == nullptr) {
    return OB_NOT_INIT;
  }
  const StorageSpaceHandle space = active_worker_storage_space();
  if (!space.is_namespace() || storage->ns != space.namespace_id()) {
    return OB_INVALID_ARGUMENT;
  }
  auto *old_session = THIS_WORKER.get_session();
  const int64_t old_timeout = THIS_WORKER.get_timeout_ts();
  THIS_WORKER.set_session(&storage->session);
  const int ret = call(*service);
  THIS_WORKER.set_session(old_session);
  THIS_WORKER.set_timeout_ts(old_timeout);
  return ret;
}
int call_in_process_tx_interrupt(const transaction::ObTxDesc &tx, int cause)
{
  if (!tx.get_tx_id().is_valid() || cause == OB_SUCCESS) {
    return OB_INVALID_ARGUMENT;
  }
  IndependentStorageScope scope;
  if (scope.error()) { return scope.error(); }
  InProcessStorage *storage = in_process_storage;
  auto *service = share::server_service<transaction::ObTransService>();
  if (storage == nullptr || !storage->initialized || service == nullptr) {
    return OB_NOT_INIT;
  }
  const StorageSpaceHandle space = active_worker_storage_space();
  if (!space.is_namespace() || storage->ns != space.namespace_id()) {
    return OB_INVALID_ARGUMENT;
  }
  return service->interrupt(tx.get_tx_id(), cause);
}
int call_in_process_tx_snapshot(char operation,
                                transaction::ObTxReadSnapshot &snapshot)
{
  StorageSessionScope scope(THIS_WORKER.get_session(), false);
  if (scope.error()) { return scope.error(); }
  InProcessStorage *storage = in_process_storage;
  auto *service = share::server_service<transaction::ObTransService>();
  if (storage == nullptr || !storage->initialized || service == nullptr) {
    return OB_NOT_INIT;
  }
  const StorageSpaceHandle space = active_worker_storage_space();
  if (!space.is_namespace() || storage->ns != space.namespace_id()) {
    return OB_INVALID_ARGUMENT;
  }
  if (operation == 'v') {
    if (!snapshot.is_valid() || storage->writes == nullptr
        || storage->writes->tx == nullptr
        || snapshot.tx_id() != storage->writes->tx->get_tx_id()) {
      return OB_INVALID_ARGUMENT;
    }
    return service->register_tx_snapshot_verify(snapshot);
  } else if (operation == 'z') {
    return service->refresh_tx_snapshot_verify(snapshot);
  } else if (operation == 'y') {
    return service->unregister_tx_snapshot_verify(snapshot);
  }
  return OB_INVALID_ARGUMENT;
}
int in_process_open(InProcessStorage &ctx, uint32_t sid, bool internal)
{
  int ret = OB_SUCCESS;
  if (ctx.initialized || (!sid && !internal)) {
    ret = OB_INVALID_ARGUMENT;
  } else if (OB_FAIL(ctx.session.test_init(1, static_cast<uint32_t>(sid),
             &ctx.session_state->allocator))) {
  } else if (sid != 0 || !internal) {
    // A sid-less internal route is a short-lived catalog/background storage
    // context; loading its variables can recurse into schema refresh.
    ret = ctx.session.load_default_sys_variable(false, false);
  }
  if (!ret) {
    ctx.writes = std::make_unique<EngineWrites>(
        ctx.storage_space, ctx.session);
    ctx.initialized = true;
  }
  return ret;
}
int open_in_process_scan(StorageSpaceHandle storage_space,
                         const ObVTableScanParam &param,
                         const ObTableSchema &logical_schema, uint64_t &handle)
{
  handle = 0;
  InProcessStorage *ctx = in_process_storage;
  if (ctx == nullptr || !ctx->initialized || !ctx->writes) { return OB_NOT_INIT; }
  if (ctx->ns != serving_namespace() || !ctx->owns(storage_space)) {
    return OB_INVALID_ARGUMENT;
  }
  const int64_t old_timeout = THIS_WORKER.get_timeout_ts();
  auto *old_session = THIS_WORKER.get_session();
  THIS_WORKER.set_session(&ctx->session);
  const int ret = ctx->scans.open(storage_space, param, logical_schema,
      ctx->writes->tx, &ctx->session, handle);
  THIS_WORKER.set_session(old_session);
  THIS_WORKER.set_timeout_ts(old_timeout);
  return ret;
}
int release_in_process_tx(const transaction::ObTxDesc &tx)
{
  InProcessStorage *ctx = in_process_storage;
  const StorageSpaceHandle space = active_worker_storage_space();
  if (ctx == nullptr || !ctx->initialized || !ctx->writes) { return OB_NOT_INIT; }
  if (!space.is_namespace() || ctx->ns != space.namespace_id()) { return OB_INVALID_ARGUMENT; }
  const int64_t old_timeout = THIS_WORKER.get_timeout_ts();
  auto *old_session = THIS_WORKER.get_session();
  THIS_WORKER.set_session(&ctx->session);
  const int ret = ctx->writes->release(tx.get_tx_id().get_id());
  THIS_WORKER.set_session(old_session);
  THIS_WORKER.set_timeout_ts(old_timeout);
  return ret;
}
int call_in_process_tx_state(char operation, transaction::ObTxDesc &view,
                             const ObTxParam *param, int64_t deadline)
{
  InProcessStorage *ctx = in_process_storage;
  const StorageSpaceHandle space = active_worker_storage_space();
  if (ctx == nullptr || !ctx->initialized || !ctx->writes) { return OB_NOT_INIT; }
  if (!space.is_namespace() || ctx->ns != space.namespace_id()
      || (operation != 'A' && operation != 'S' && operation != 'N'
          && operation != 'U' && operation != 'H' && operation != 'C'
          && operation != 'R')) {
    return OB_INVALID_ARGUMENT;
  }
  const int64_t old_timeout = THIS_WORKER.get_timeout_ts();
  auto *old_session = THIS_WORKER.get_session();
  THIS_WORKER.set_session(&ctx->session);
  auto *service = data_plane::query_transaction_service();
  int ret = service ? OB_SUCCESS : OB_NOT_INIT;
  ObTxDesc *&native = ctx->writes->tx;
  if (!ret && !native && (operation == 'A' || operation == 'S')) {
    ret = service->acquire_tx(native, ctx->writes->sid);
  }
  if (!ret && (!native || native->get_tx_id() != view.get_tx_id())) {
    ret = OB_INVALID_ARGUMENT;
  }
  if (!ret && operation != 'A') {
    if (operation == 'S') {
      ret = service->prepare_tx_for_statement(*native);
    } else if (operation == 'N') {
      ret = ctx->writes->writes.empty()
          ? service->prepare_tx_for_autocommit_retry(*native) : OB_INVALID_ARGUMENT;
    } else if (operation == 'U') {
      ret = ctx->writes->writes.empty()
          ? service->reuse_tx(*native) : OB_INVALID_ARGUMENT;
    } else if (operation == 'H') {
      ret = param && param->is_valid()
          ? service->start_tx(*native, *param) : OB_INVALID_ARGUMENT;
    } else if (operation == 'C') {
      ret = ctx->writes->writes.empty()
          ? service->commit_tx(*native, std::min(deadline, THIS_WORKER.get_timeout_ts()))
          : OB_INVALID_ARGUMENT;
    } else if (operation == 'R') {
      ret = ctx->writes->writes.empty()
          ? service->rollback_tx(*native) : OB_ERR_UNEXPECTED;
    }
  }
  if (!ret) { ret = view.sync_serialized_state_from(*native); }
  THIS_WORKER.set_session(old_session);
  THIS_WORKER.set_timeout_ts(old_timeout);
  return ret;
}
int call_in_process_tx_read_snapshot(ObTxDesc &view,
    ObTxIsolationLevel isolation, int64_t deadline, ObTxReadSnapshot &snapshot)
{
  InProcessStorage *ctx = in_process_storage;
  const StorageSpaceHandle space = active_worker_storage_space();
  if (ctx == nullptr || !ctx->initialized || !ctx->writes) { return OB_NOT_INIT; }
  if (!space.is_namespace() || ctx->ns != space.namespace_id()
      || !ctx->writes->tx || ctx->writes->tx->get_tx_id() != view.get_tx_id()) {
    return OB_INVALID_ARGUMENT;
  }
  const int64_t old_timeout = THIS_WORKER.get_timeout_ts();
  auto *old_session = THIS_WORKER.get_session();
  THIS_WORKER.set_session(&ctx->session);
  auto *service = data_plane::query_transaction_service();
  ObTxReadSnapshot staged;
  int ret = service ? service->get_read_snapshot(*ctx->writes->tx, isolation,
      std::min(deadline, THIS_WORKER.get_timeout_ts()), staged) : OB_NOT_INIT;
  if (!ret && (!ctx->session.get_reserved_snapshot_version().is_valid()
      || staged.core_.version_ < ctx->session.get_reserved_snapshot_version())) {
    ctx->session.set_reserved_snapshot_version(staged.core_.version_);
  }
  if (!ret) { ret = view.sync_serialized_state_from(*ctx->writes->tx); }
  if (!ret) { ret = snapshot.assign(staged); }
  THIS_WORKER.set_session(old_session);
  THIS_WORKER.set_timeout_ts(old_timeout);
  return ret;
}
int call_in_process_tx_create_savepoint(ObTxDesc &view, char operation,
    const ObTxParam *param, bool release, int16_t branch, ObTxSEQ &savepoint)
{
  InProcessStorage *ctx = in_process_storage;
  const StorageSpaceHandle space = active_worker_storage_space();
  if (ctx == nullptr || !ctx->initialized || !ctx->writes) { return OB_NOT_INIT; }
  if (!space.is_namespace() || ctx->ns != space.namespace_id()
      || !ctx->writes->tx || ctx->writes->tx->get_tx_id() != view.get_tx_id()
      || (operation != 'P' && operation != 'J' && operation != 'I')) {
    return OB_INVALID_ARGUMENT;
  }
  const int64_t old_timeout = THIS_WORKER.get_timeout_ts();
  auto *old_session = THIS_WORKER.get_session();
  THIS_WORKER.set_session(&ctx->session);
  auto *service = data_plane::query_transaction_service();
  ObTxSEQ staged;
  int ret = service ? OB_SUCCESS : OB_NOT_INIT;
  if (!ret && operation == 'P') {
    ret = param && param->is_valid()
        ? service->create_implicit_savepoint(*ctx->writes->tx, *param, staged, release)
        : OB_INVALID_ARGUMENT;
  } else if (!ret && operation == 'J') {
    ret = service->create_branch_savepoint(*ctx->writes->tx, branch, staged);
  } else if (!ret) {
    ret = service->create_in_txn_implicit_savepoint(*ctx->writes->tx, staged);
  }
  if (!ret) { ret = view.sync_serialized_state_from(*ctx->writes->tx); }
  if (!ret) { savepoint = staged; }
  THIS_WORKER.set_session(old_session);
  THIS_WORKER.set_timeout_ts(old_timeout);
  return ret;
}
int call_in_process_tx_rollback_savepoint(ObTxDesc &view, ObTxSEQ savepoint,
    int64_t deadline, bool touched_storage, ObTxCleanPolicy policy)
{
  InProcessStorage *ctx = in_process_storage;
  const StorageSpaceHandle space = active_worker_storage_space();
  if (ctx == nullptr || !ctx->initialized || !ctx->writes) { return OB_NOT_INIT; }
  if (!space.is_namespace() || ctx->ns != space.namespace_id()
      || !ctx->writes->tx || ctx->writes->tx->get_tx_id() != view.get_tx_id()
      || (policy != FAST_ROLLBACK && policy != ROLLBACK && policy != KEEP)) {
    return OB_INVALID_ARGUMENT;
  }
  const int64_t old_timeout = THIS_WORKER.get_timeout_ts();
  auto *old_session = THIS_WORKER.get_session();
  THIS_WORKER.set_session(&ctx->session);
  // Write contexts publish their state into the native descriptor before rollback.
  for (auto &entry : ctx->writes->writes) { entry.second->release_context(); }
  auto *service = data_plane::query_transaction_service();
  int ret = service ? service->rollback_to_implicit_savepoint(
      *ctx->writes->tx, savepoint, deadline, touched_storage, policy) : OB_NOT_INIT;
  if (!ret) { ret = view.sync_serialized_state_from(*ctx->writes->tx); }
  THIS_WORKER.set_session(old_session);
  THIS_WORKER.set_timeout_ts(old_timeout);
  return ret;
}
int call_in_process_tx_named_savepoint(ObTxDesc &view, char operation,
    const ObString &name, int64_t deadline)
{
  InProcessStorage *ctx = in_process_storage;
  const StorageSpaceHandle space = active_worker_storage_space();
  if (ctx == nullptr || !ctx->initialized || !ctx->writes) { return OB_NOT_INIT; }
  if (!space.is_namespace() || ctx->ns != space.namespace_id()
      || !ctx->writes->tx || ctx->writes->tx->get_tx_id() != view.get_tx_id()
      || (operation != 'F' && operation != 'L' && operation != 'D'
          && operation != 'K')) {
    return OB_INVALID_ARGUMENT;
  }
  if (name.length() < 0
      || name.length() > static_cast<int64_t>(MAX_SQL_MESSAGE - 64)) {
    return OB_SIZE_OVERFLOW;
  }
  const int64_t old_timeout = THIS_WORKER.get_timeout_ts();
  auto *old_session = THIS_WORKER.get_session();
  THIS_WORKER.set_session(&ctx->session);
  auto *service = data_plane::query_transaction_service();
  int ret = service ? OB_SUCCESS : OB_NOT_INIT;
  if (!ret && operation == 'F') {
    ret = service->create_explicit_savepoint(*ctx->writes->tx, name);
  } else if (!ret && operation == 'L') {
    ret = service->rollback_to_explicit_savepoint(*ctx->writes->tx, name, deadline);
  } else if (!ret && operation == 'D') {
    ret = service->release_explicit_savepoint(*ctx->writes->tx, name);
  } else if (!ret) {
    ret = service->create_stash_savepoint(*ctx->writes->tx, name);
  }
  if (!ret) { ret = view.sync_serialized_state_from(*ctx->writes->tx); }
  THIS_WORKER.set_session(old_session);
  THIS_WORKER.set_timeout_ts(old_timeout);
  return ret;
}
int call_in_process_tx_exec_result(ObTxDesc &view, char operation,
    const ObTxExecResult *input, ObTxExecResult *output)
{
  InProcessStorage *ctx = in_process_storage;
  const StorageSpaceHandle space = active_worker_storage_space();
  if (ctx == nullptr || !ctx->initialized || !ctx->writes) { return OB_NOT_INIT; }
  if (!space.is_namespace() || ctx->ns != space.namespace_id()
      || !ctx->writes->tx || ctx->writes->tx->get_tx_id() != view.get_tx_id()
      || (operation != 'a' && operation != 'E')
      || (operation == 'a' && !input) || (operation == 'E' && !output)) {
    return OB_INVALID_ARGUMENT;
  }
  if (input) {
    const int64_t size = input->get_serialize_size();
    if (size < 0 || size > static_cast<int64_t>(MAX_SQL_MESSAGE - 64)) {
      return OB_SIZE_OVERFLOW;
    }
  }
  const int64_t old_timeout = THIS_WORKER.get_timeout_ts();
  auto *old_session = THIS_WORKER.get_session();
  THIS_WORKER.set_session(&ctx->session);
  auto *service = data_plane::query_transaction_service();
  ObTxExecResult staged;
  int ret = service ? OB_SUCCESS : OB_NOT_INIT;
  if (!ret && operation == 'a') {
    ret = service->add_tx_exec_result(*ctx->writes->tx, *input);
  } else if (!ret) {
    ret = service->collect_tx_exec_result(*ctx->writes->tx, staged);
  }
  if (!ret) { ret = view.sync_serialized_state_from(*ctx->writes->tx); }
  if (!ret && output) { ret = output->assign(staged); }
  THIS_WORKER.set_session(old_session);
  THIS_WORKER.set_timeout_ts(old_timeout);
  return ret;
}
int call_in_process_tx_table_lock(ObTxDesc &view,
    obcall::ObInnerSQLTransmitArg::InnerSQLOperationType operation,
    StorageSpaceHandle space, const ObTxParam &param,
    const ObString &payload, const TableLockPlan &plan)
{
  InProcessStorage *ctx = in_process_storage;
  if (ctx == nullptr || !ctx->initialized || !ctx->writes) { return OB_NOT_INIT; }
  if (!ctx->writes->tx || ctx->writes->tx->get_tx_id() != view.get_tx_id()
      || !ctx->owns(space)
      || plan.storage_space != space || !param.is_valid()
      || payload.empty() || payload.length() > static_cast<int64_t>(MAX_SQL_MESSAGE)) {
    return OB_INVALID_ARGUMENT;
  }
  const int64_t old_timeout = THIS_WORKER.get_timeout_ts();
  auto *old_session = THIS_WORKER.get_session();
  THIS_WORKER.set_session(&ctx->session);
  int ret = process_table_lock(
      operation, space, param, payload, plan, *ctx->writes->tx);
  if (!ret) { ret = view.sync_serialized_state_from(*ctx->writes->tx); }
  THIS_WORKER.set_session(old_session);
  THIS_WORKER.set_timeout_ts(old_timeout);
  return ret;
}
int call_in_process_tx_register_mds(ObTxDesc &view,
    ObTxDataSourceType type, StorageSpaceHandle space,
    const char *buffer, int64_t buffer_size,
    const ObRegisterMdsFlag &flag, ObTxSEQ sequence)
{
  InProcessStorage *ctx = in_process_storage;
  if (ctx == nullptr || !ctx->initialized || !ctx->writes) { return OB_NOT_INIT; }
  if (!ctx->writes->tx || ctx->writes->tx->get_tx_id() != view.get_tx_id()
      || !ctx->owns(space)
      || type <= ObTxDataSourceType::UNKNOWN
      || type >= ObTxDataSourceType::MAX_TYPE
      || !buffer || buffer_size <= 0) {
    return OB_INVALID_ARGUMENT;
  }
  if (buffer_size > static_cast<int64_t>(MAX_SQL_MESSAGE - 128)) {
    return OB_SIZE_OVERFLOW;
  }
  const int64_t old_timeout = THIS_WORKER.get_timeout_ts();
  auto *old_session = THIS_WORKER.get_session();
  THIS_WORKER.set_session(&ctx->session);
  const ObString input(static_cast<int32_t>(buffer_size), buffer);
  std::vector<char> storage_buffer;
  bool skip_mds = false;
  int ret = route_tablet_mds(space, type, input, storage_buffer, skip_mds);
  if (ret) {
    fprintf(stderr,
        "PROTOTYPE_NAMESPACE_MDS_ROUTE ns=%llu global=%d type=%lld input=%d ret=%d\n",
        (unsigned long long)space.namespace_id(), space.is_global(),
        (long long)type, input.length(), ret);
  }
  auto *service = data_plane::query_transaction_service();
  if (!ret && !skip_mds && !service) { ret = OB_NOT_INIT; }
  if (!ret && !skip_mds) {
    const char *mds_buffer = storage_buffer.empty()
        ? input.ptr() : storage_buffer.data();
    const int64_t mds_size = storage_buffer.empty()
        ? input.length() : storage_buffer.size();
    ret = service->register_mds_into_tx(
        *ctx->writes->tx, type, mds_buffer, mds_size, flag, sequence);
    if (ret) {
      fprintf(stderr,
          "PROTOTYPE_NAMESPACE_MDS_REGISTER ns=%llu type=%lld input=%d routed=%zu ret=%d\n",
          (unsigned long long)ctx->ns, (long long)type,
          input.length(), storage_buffer.size(), ret);
    }
    if (!ret && type == ObTxDataSourceType::CREATE_TABLET_NEW_MDS) {
      obcall::ObBatchCreateTabletArg create_arg;
      int64_t create_pos = 0;
      if (OB_FAIL(create_arg.deserialize(mds_buffer, mds_size, create_pos))) {
      } else if (create_pos != mds_size || !create_arg.is_valid()) {
        ret = OB_INVALID_ARGUMENT;
      } else if (create_arg.set_binding_info_outside_create()
                 && OB_FAIL(storage::ObTabletBindingMdsHelper::
                     modify_tablet_binding_for_create(
                         create_arg, THIS_WORKER.get_timeout_ts(),
                         *ctx->writes->tx, *service))) {
      }
    }
  }
  if (!ret) { ret = view.sync_serialized_state_from(*ctx->writes->tx); }
  THIS_WORKER.set_session(old_session);
  THIS_WORKER.set_timeout_ts(old_timeout);
  return ret;
}
int call_in_process_tablet_binding(ObTxDesc &view, char operation,
    StorageSpaceHandle space, const ObIArray<ObTabletID> &tablets,
    const ObIArray<ObTabletID> *hidden, int64_t schema_version,
    int64_t deadline)
{
  InProcessStorage *ctx = in_process_storage;
  if (ctx == nullptr || !ctx->initialized || !ctx->writes) { return OB_NOT_INIT; }
  if (!ctx->writes->tx || ctx->writes->tx->get_tx_id() != view.get_tx_id()
      || !ctx->owns(space)
      || (operation != 'd' && operation != 'b' && operation != 'u')
      || schema_version <= 0 || deadline <= 0
      || tablets.count() > static_cast<int64_t>(MAX_SQL_MESSAGE / sizeof(uint64_t))
      || (operation != 'u' && tablets.empty())
      || (operation == 'u' && !hidden)) {
    return OB_INVALID_ARGUMENT;
  }
  if (hidden && (hidden->count() > static_cast<int64_t>(MAX_SQL_MESSAGE / sizeof(uint64_t))
      || tablets.count() + hidden->count()
          > static_cast<int64_t>((MAX_SQL_MESSAGE - 64) / sizeof(uint64_t)))) {
    return OB_SIZE_OVERFLOW;
  }
  const int64_t old_timeout = THIS_WORKER.get_timeout_ts();
  auto *old_session = THIS_WORKER.get_session();
  THIS_WORKER.set_session(&ctx->session);
  ObArray<ObTabletID> routed;
  int ret = space.is_global() ? routed.assign(tablets)
      : route_existing_namespace_tablets(space.namespace_id(), tablets, routed);
  ObArray<ObTabletID> routed_hidden;
  if (!ret && hidden) {
    ret = space.is_global() ? routed_hidden.assign(*hidden)
        : route_existing_namespace_tablets(space.namespace_id(), *hidden, routed_hidden);
  }
  if (ret) {
    THIS_WORKER.set_session(old_session);
    THIS_WORKER.set_timeout_ts(old_timeout);
    return ret;
  }
  auto *service = data_plane::query_transaction_service();
  if (!service) {
    ret = OB_NOT_INIT;
  } else if (operation == 'b' && !routed.empty()) {
    ret = storage::ObTabletBindingMdsHelper::modify_tablet_binding_for_rw_defensive(
        routed, schema_version, std::min(deadline, THIS_WORKER.get_timeout_ts()),
        *ctx->writes->tx, *service);
  } else if (operation == 'd' && !routed.empty()) {
    ret = storage::ObTabletBindingMdsHelper::modify_tablet_binding_for_write_defensive(
        routed, schema_version, std::min(deadline, THIS_WORKER.get_timeout_ts()),
        *ctx->writes->tx, *service);
  } else if (operation == 'u') {
    ret = storage::ObTabletBindingMdsHelper::modify_tablet_binding_for_unbind(
        routed, routed_hidden, schema_version,
        std::min(deadline, THIS_WORKER.get_timeout_ts()),
        *ctx->writes->tx, *service);
  }
  if (!ret) { ret = view.sync_serialized_state_from(*ctx->writes->tx); }
  THIS_WORKER.set_session(old_session);
  THIS_WORKER.set_timeout_ts(old_timeout);
  return ret;
}
int close_in_process_write(transaction::ObTxDesc &view, uint64_t handle)
{
  InProcessStorage *ctx = in_process_storage;
  const StorageSpaceHandle space = active_worker_storage_space();
  if (ctx == nullptr || !ctx->initialized || !ctx->writes) { return OB_NOT_INIT; }
  if (!space.is_namespace() || ctx->ns != space.namespace_id() || handle == 0) {
    return OB_INVALID_ARGUMENT;
  }
  const int64_t old_timeout = THIS_WORKER.get_timeout_ts();
  auto *old_session = THIS_WORKER.get_session();
  THIS_WORKER.set_session(&ctx->session);
  const int ret = ctx->writes->close(view.get_tx_id().get_id(), handle, view);
  THIS_WORKER.set_session(old_session);
  THIS_WORKER.set_timeout_ts(old_timeout);
  return ret;
}
int prepare_in_process_write(const WritePrepareRequest &request,
                             const ObTxDesc &view, uint64_t &handle)
{
  handle = 0;
  InProcessStorage *ctx = in_process_storage;
  if (ctx == nullptr || !ctx->initialized || !ctx->writes) { return OB_NOT_INIT; }
  const StorageSpaceHandle space = request.storage_space;
  if (ctx->ns != serving_namespace() || !ctx->owns(space)) {
    return OB_INVALID_ARGUMENT;
  }
  const int64_t old_timeout = THIS_WORKER.get_timeout_ts();
  auto *old_session = THIS_WORKER.get_session();
  THIS_WORKER.set_session(&ctx->session);
  const int ret = ctx->writes->prepare(request, view, handle);
  THIS_WORKER.set_session(old_session);
  THIS_WORKER.set_timeout_ts(old_timeout);
  return ret;
}
int write_in_process_batch(const ObTxDesc &view, const WriteBatch &batch,
                           int64_t &affected, WriteResult &duplicates)
{
  affected = 0;
  InProcessStorage *ctx = in_process_storage;
  const StorageSpaceHandle space = active_worker_storage_space();
  if (ctx == nullptr || !ctx->initialized || !ctx->writes) { return OB_NOT_INIT; }
  if (ctx->ns != serving_namespace() || !ctx->owns(space)) {
    return OB_INVALID_ARGUMENT;
  }
  const int64_t old_timeout = THIS_WORKER.get_timeout_ts();
  auto *old_session = THIS_WORKER.get_session();
  THIS_WORKER.set_session(&ctx->session);
  const int ret = ctx->writes->batch(view, batch, affected, duplicates);
  THIS_WORKER.set_session(old_session);
  THIS_WORKER.set_timeout_ts(old_timeout);
  return ret;
}
#include "observer/namespace_inprocess_gateway_io.ipp"
#include "observer/namespace_template_registry.ipp"
void close_session(SessionBinding *binding) {
  std::unique_ptr<SessionBinding> owned(binding);
  if (!owned || !owned->in_process) { return; }
  InProcessStorage *ctx = owned->in_process;
  owned->in_process = nullptr;
  if (in_process_storage == ctx) { in_process_storage = nullptr; }
  if (ctx->initialized) {
    const int64_t old_timeout = THIS_WORKER.get_timeout_ts();
    auto *old_session = THIS_WORKER.get_session();
    THIS_WORKER.set_session(&ctx->session);
    ctx->direct_insert.reset();
    ctx->scans.scans.clear();
    ctx->session.reset_reserved_snapshot_version();
    ctx->writes.reset();
    ctx->initialized = false;
    THIS_WORKER.set_session(old_session);
    THIS_WORKER.set_timeout_ts(old_timeout);
  }
  delete ctx;
}
// Keep one storage context per SQL session in a forked namespace.
int open_in_process_storage(sql::ObSQLSessionInfo &session)
{
  SessionBinding *&slot = session.namespace_storage_binding();
  if (slot != nullptr && slot->in_process != nullptr) {
    in_process_storage = slot->in_process;
    return OB_SUCCESS;
  }
  if (slot != nullptr) { return OB_ERR_UNEXPECTED; }
  const uint64_t ns = in_process_session_ns(&session);
  if (ns <= 1) { return OB_INVALID_ARGUMENT; }
  auto owned = std::make_unique<SessionBinding>();
  owned->in_process = new (std::nothrow) InProcessStorage(ns);
  int ret = owned->in_process == nullptr ? OB_ALLOCATE_MEMORY_FAILED : OB_SUCCESS;
  if (!ret) { ret = in_process_open(*owned->in_process, session.get_server_sid(), true); }
  if (!ret) {
    slot = owned.release();
    in_process_storage = slot->in_process;
  }
  return ret;
}
int import_in_process_shadow_tx(ObTxDesc &view)
{
  InProcessStorage *ctx = in_process_storage;
  if (!ctx || !ctx->initialized || !ctx->writes) { return OB_NOT_INIT; }
  if (!view.is_shadow() || ctx->writes->tx) { return OB_INVALID_ARGUMENT; }
  const int64_t old_timeout = THIS_WORKER.get_timeout_ts();
  auto *old_session = THIS_WORKER.get_session();
  THIS_WORKER.set_session(&ctx->session);
  auto *service = share::server_service<ObTransService>();
  int ret = service ? service->acquire_shadow_tx(view, ctx->writes->tx) : OB_NOT_INIT;
  if (!ret) { ret = view.sync_serialized_state_from(*ctx->writes->tx); }
  fprintf(stderr, "PROTOTYPE_SHADOW_IMPORT ns=%llu tx=%lld ret=%d\n",
      (unsigned long long)ctx->ns, (long long)view.get_tx_id().get_id(), ret);
  if (ret && service && ctx->writes->tx) {
    service->release_tx(*ctx->writes->tx);
    ctx->writes->tx = nullptr;
  }
  THIS_WORKER.set_session(old_session);
  THIS_WORKER.set_timeout_ts(old_timeout);
  return ret;
}
StorageSessionScope::StorageSessionScope(sql::ObSQLSessionInfo *session, bool create) {
  if (session && in_process_session_ns(session) > 1
      && (!in_process_storage || in_process_storage->sql_session != session)
      && (create || session->namespace_storage_binding())) {
    switched_ = true;
    previous_in_process_ = in_process_storage;
    const bool created = !session->namespace_storage_binding();
    error_ = open_in_process_storage(*session);
    if (!error_) { in_process_storage->sql_session = session; }
    if (!error_ && created && session->get_tx_desc() && session->get_tx_desc()->is_shadow()) {
      // Import a PX shadow transaction once for this storage session.
      error_ = import_in_process_shadow_tx(*session->get_tx_desc());
      if (error_) {
        close_session(session->namespace_storage_binding());
        session->namespace_storage_binding() = nullptr;
      }
    }
  }
}
StorageSessionScope::~StorageSessionScope() {
  if (switched_) {
    in_process_storage = previous_in_process_;
  }
}
void StorageSessionScope::close(SessionBinding *&binding) {
  if (binding) {
    if (binding->in_process && previous_in_process_ == binding->in_process) { previous_in_process_ = nullptr; }
    close_session(binding); binding = nullptr;
  }
}
IndependentStorageScope::IndependentStorageScope()
    : previous_timeout_(THIS_WORKER.get_timeout_ts()) {
  if (in_process_serving_ns > 1
      && !in_process_storage) {
    // Background storage calls borrow a short-lived context for this namespace.
    if (previous_timeout_ <= 0) { THIS_WORKER.set_timeout_ts(INT64_MAX); }
    previous_in_process_ = in_process_storage;
    auto owned = std::make_unique<SessionBinding>();
    owned->in_process = new (std::nothrow) InProcessStorage(in_process_serving_ns);
    error_ = owned->in_process == nullptr ? OB_ALLOCATE_MEMORY_FAILED : OB_SUCCESS;
    if (!error_) { error_ = in_process_open(*owned->in_process, 0, true); }
    if (!error_) {
      binding_ = owned.release();
      in_process_storage = binding_->in_process;
    }
  }
}
IndependentStorageScope::~IndependentStorageScope() {
  if (binding_) { close_session(binding_); }
  in_process_storage = previous_in_process_;
  THIS_WORKER.set_timeout_ts(previous_timeout_);
}
int fetch_schema_version(bool published, bool core_version, int64_t &version) {
  const uint64_t ns = serving_namespace();
  if (ns > 1) {
    return NamespaceForkKernelPrototype::namespace_schema_version(ns, version);
  }
  auto &service = ObMultiVersionSchemaService::get_instance();
  return published
      ? service.get_published_schema_version(version, core_version)
      : service.get_runtime_refreshed_schema_version(version, core_version);
}
} } }
