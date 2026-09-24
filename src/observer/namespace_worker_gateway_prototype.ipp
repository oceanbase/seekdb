// Included in the Observer composition unit for in-process namespace storage.
#include "observer/namespace_worker_protocol_prototype.h"
#include "rootserver/fork_table/namespace_fork_kernel_prototype.h"
#include "rootserver/ddl_task/ob_ddl_task_util.h"
#include "storage/compaction/ob_freeze_info_mgr.h"
#include <map>
#include <mutex>
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
int serve_storage(StorageSpaceHandle storage_space,
    EngineWrites *writes, int state, Frame &input, Frame &result) {
    int ret = OB_SUCCESS;
    if (!storage_space.is_namespace()) { return OB_INVALID_ARGUMENT; }
    if (writes && input.type() == 'T') {
      result = Frame('w');
      if (state && !cleanup_write(input)) { result.number(state); }
      else { ret = writes->process(input, result); }
    } else { ret = OB_INVALID_ARGUMENT; }
    return ret;
}
// One native storage context belongs to each forked-namespace SQL session.
struct InProcessStorage {
  const uint64_t ns;
  std::shared_ptr<StorageSessionState> session_state;
  sql::ObSQLSessionInfo &session; // storage-side native session
  std::unique_ptr<EngineWrites> writes;
  ReadScans scans;
  DirectInsertRoute direct_insert;
  DirectInsertRegistry *direct_insert_registry = nullptr;
  RequestTag direct_insert_tag;
  sql::ObSQLSessionInfo *sql_session = nullptr; // switch key, not an owner
  Frame reply;
  bool initialized = false;
  explicit InProcessStorage(uint64_t namespace_id)
      : ns(namespace_id),
        session_state(std::make_shared<StorageSessionState>()),
        session(session_state->session),
        scans(StorageSpaceHandle::namespace_space(namespace_id)) {
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
        StorageSpaceHandle::namespace_space(ctx.ns), ctx.session);
    ctx.initialized = true;
  }
  return ret;
}
// Deliver storage calls synchronously through the bound native session.
int in_process_send(InProcessStorage &ctx, const Frame &frame, bool)
{
  Frame input = frame;
  Frame result('l');
  int ret = OB_SUCCESS;
  const int64_t old_timeout = THIS_WORKER.get_timeout_ts();
  auto *old_session = THIS_WORKER.get_session();
  THIS_WORKER.set_session(ctx.initialized ? &ctx.session : nullptr);
  if (input.type() == 'J') {
    if (ctx.direct_insert_registry == nullptr) {
      ret = OB_NOT_INIT;
    } else {
      if (!ctx.direct_insert_tag.slot) {
        ctx.direct_insert_tag = ctx.direct_insert_registry->acquire();
      }
      if (!ctx.direct_insert_tag.slot) {
        ret = OB_EAGAIN;
      } else {
        ret = ctx.direct_insert.process(
            StorageSpaceHandle::namespace_space(ctx.ns),
            ctx.direct_insert_tag, *ctx.direct_insert_registry,
            ctx.session_state, input, result);
      }
    }
  } else if (!ctx.initialized) {
    ret = OB_NOT_INIT;
  } else {
    ret = serve_storage(StorageSpaceHandle::namespace_space(ctx.ns),
        ctx.writes.get(), OB_SUCCESS, input, result);
  }
  THIS_WORKER.set_session(old_session);
  THIS_WORKER.set_timeout_ts(old_timeout);
  if (!ret) { ctx.reply = std::move(result); }
  return ret;
}
int in_process_read(InProcessStorage &ctx, Frame &frame)
{
  frame = std::move(ctx.reply);
  ctx.reply = Frame();
  return OB_SUCCESS;
}
int open_in_process_scan(StorageSpaceHandle storage_space,
                         const ObVTableScanParam &param,
                         const ObTableSchema &logical_schema, uint64_t &handle)
{
  handle = 0;
  InProcessStorage *ctx = in_process_storage;
  if (ctx == nullptr || !ctx->initialized || !ctx->writes) { return OB_NOT_INIT; }
  if (ctx->ns != serving_namespace()
      || !(storage_space.is_namespace() && storage_space.namespace_id() == ctx->ns)
          && !(storage_space.is_global() && ctx->ns == 1)) {
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
  if (ctx->ns != serving_namespace()
      || (!(space.is_namespace() && space.namespace_id() == ctx->ns)
          && !(space.is_global() && ctx->ns == 1))) {
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
  if (ctx->ns != serving_namespace()
      || (!(space.is_namespace() && space.namespace_id() == ctx->ns)
          && !(space.is_global() && ctx->ns == 1))) {
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
int call_in_process_direct_insert_simple(RequestTag parent, uint64_t generation,
                                         char operation, bool &is_final)
{
  InProcessStorage *ctx = in_process_storage;
  const StorageSpaceHandle space = active_worker_storage_space();
  if (ctx == nullptr || !ctx->initialized || !ctx->direct_insert_registry) { return OB_NOT_INIT; }
  if (!space.is_namespace() || ctx->ns != space.namespace_id()) { return OB_INVALID_ARGUMENT; }
  const int64_t old_timeout = THIS_WORKER.get_timeout_ts();
  auto *old_session = THIS_WORKER.get_session();
  THIS_WORKER.set_session(&ctx->session);
  const int ret = ctx->direct_insert.simple(space, parent, generation,
      *ctx->direct_insert_registry, operation, is_final);
  THIS_WORKER.set_session(old_session);
  THIS_WORKER.set_timeout_ts(old_timeout);
  return ret;
}
int resolve_in_process_direct_insert_policy(RequestTag parent, uint64_t generation,
                                            const ObDirectInsertPlanFacts &facts,
                                            ObDirectInsertWritePolicy &policy)
{
  InProcessStorage *ctx = in_process_storage;
  const StorageSpaceHandle space = active_worker_storage_space();
  if (ctx == nullptr || !ctx->initialized || !ctx->direct_insert_registry) { return OB_NOT_INIT; }
  if (!space.is_namespace() || ctx->ns != space.namespace_id()) { return OB_INVALID_ARGUMENT; }
  const int64_t old_timeout = THIS_WORKER.get_timeout_ts();
  auto *old_session = THIS_WORKER.get_session();
  THIS_WORKER.set_session(&ctx->session);
  const int ret = ctx->direct_insert.resolve_policy(space, parent, generation,
      *ctx->direct_insert_registry, facts, policy);
  THIS_WORKER.set_session(old_session);
  THIS_WORKER.set_timeout_ts(old_timeout);
  return ret;
}
template <class Call>
int with_in_process_direct_insert(Call &&call)
{
  InProcessStorage *ctx = in_process_storage;
  const StorageSpaceHandle space = active_worker_storage_space();
  if (ctx == nullptr || !ctx->initialized || !ctx->direct_insert_registry) { return OB_NOT_INIT; }
  if (!space.is_namespace() || ctx->ns != space.namespace_id()) { return OB_INVALID_ARGUMENT; }
  const int64_t old_timeout = THIS_WORKER.get_timeout_ts();
  auto *old_session = THIS_WORKER.get_session();
  THIS_WORKER.set_session(&ctx->session);
  const int ret = call(ctx->direct_insert, space, *ctx->direct_insert_registry);
  THIS_WORKER.set_session(old_session);
  THIS_WORKER.set_timeout_ts(old_timeout);
  return ret;
}
int build_in_process_direct_insert_autoinc(RequestTag parent, uint64_t generation,
                                           ObDirectInsertAutoincScope scope,
                                           const ObTabletID &tablet, int64_t slice,
                                           ObDirectInsertAutoincParam &param)
{
  return with_in_process_direct_insert([&](DirectInsertRoute &route,
      StorageSpaceHandle space, DirectInsertRegistry &registry) {
    return route.build_autoinc(space, parent, generation, registry,
        scope, tablet, slice, param);
  });
}
int sync_in_process_direct_insert_autoinc(RequestTag parent, uint64_t generation,
                                          const ObTabletID &tablet,
                                          const ObTabletID &target,
                                          int64_t slice, int64_t rows)
{
  return with_in_process_direct_insert([&](DirectInsertRoute &route,
      StorageSpaceHandle space, DirectInsertRegistry &registry) {
    return route.sync_autoinc(space, parent, generation, registry,
        tablet, target, slice, rows);
  });
}
int prepare_in_process_direct_insert_ordered(RequestTag parent, uint64_t generation,
    const ObIArray<ObDDLTabletSliceCount> &slice_counts)
{
  return with_in_process_direct_insert([&](DirectInsertRoute &route,
      StorageSpaceHandle space, DirectInsertRegistry &registry) {
    return route.prepare_ordered(space, parent, generation, registry, slice_counts);
  });
}
int finish_in_process_direct_insert(RequestTag parent, uint64_t generation)
{
  return with_in_process_direct_insert([&](DirectInsertRoute &route,
      StorageSpaceHandle space, DirectInsertRegistry &registry) {
    return route.finish(space, in_process_storage->direct_insert_tag,
        parent, generation, registry);
  });
}
int create_in_process_direct_insert_writer(RequestTag parent, uint64_t generation,
    const ObDirectInsertWriterRequest &request, uint64_t &writer_id)
{
  return with_in_process_direct_insert([&](DirectInsertRoute &route,
      StorageSpaceHandle space, DirectInsertRegistry &registry) {
    return route.create_writer(space, parent, generation, registry, request, writer_id);
  });
}
int control_in_process_direct_insert_writer(RequestTag parent, uint64_t generation,
    uint64_t writer_id, char operation, int64_t &rows)
{
  return with_in_process_direct_insert([&](DirectInsertRoute &route,
      StorageSpaceHandle space, DirectInsertRegistry &registry) {
    return route.control_writer(space, parent, generation, registry,
        writer_id, operation, rows);
  });
}
int append_in_process_direct_insert_writer(RequestTag parent, uint64_t generation,
    uint64_t writer_id, ObDatum *cells, int64_t row_count,
    int64_t column_count, int64_t &rows)
{
  return with_in_process_direct_insert([&](DirectInsertRoute &route,
      StorageSpaceHandle space, DirectInsertRegistry &registry) {
    return route.append_writer(space, parent, generation, registry,
        writer_id, cells, row_count, column_count, rows);
  });
}
int fetch_in_process_scan(uint64_t handle, ScanBatch &batch)
{
  InProcessStorage *ctx = in_process_storage;
  const StorageSpaceHandle space = active_worker_storage_space();
  if (ctx == nullptr || !ctx->initialized) { return OB_NOT_INIT; }
  if (!space.is_namespace() || ctx->ns != space.namespace_id() || handle == 0) {
    return OB_INVALID_ARGUMENT;
  }
  const int64_t old_timeout = THIS_WORKER.get_timeout_ts();
  auto *old_session = THIS_WORKER.get_session();
  THIS_WORKER.set_session(&ctx->session);
  const int ret = ctx->scans.fetch(handle, batch);
  THIS_WORKER.set_session(old_session);
  THIS_WORKER.set_timeout_ts(old_timeout);
  return ret;
}
int close_in_process_scan(uint64_t handle)
{
  InProcessStorage *ctx = in_process_storage;
  const StorageSpaceHandle space = active_worker_storage_space();
  if (ctx == nullptr || !ctx->initialized) { return OB_NOT_INIT; }
  if (!space.is_namespace() || ctx->ns != space.namespace_id() || handle == 0) {
    return OB_INVALID_ARGUMENT;
  }
  const int64_t old_timeout = THIS_WORKER.get_timeout_ts();
  auto *old_session = THIS_WORKER.get_session();
  THIS_WORKER.set_session(&ctx->session);
  const int ret = ctx->scans.close(handle);
  THIS_WORKER.set_session(old_session);
  THIS_WORKER.set_timeout_ts(old_timeout);
  return ret;
}
int rescan_in_process_scan(uint64_t handle, const ObVTableScanParam &param)
{
  InProcessStorage *ctx = in_process_storage;
  const StorageSpaceHandle space = active_worker_storage_space();
  if (ctx == nullptr || !ctx->initialized) { return OB_NOT_INIT; }
  if (!space.is_namespace() || ctx->ns != space.namespace_id() || handle == 0) {
    return OB_INVALID_ARGUMENT;
  }
  const int64_t old_timeout = THIS_WORKER.get_timeout_ts();
  auto *old_session = THIS_WORKER.get_session();
  THIS_WORKER.set_session(&ctx->session);
  const int ret = ctx->scans.rescan(handle, param);
  THIS_WORKER.set_session(old_session);
  THIS_WORKER.set_timeout_ts(old_timeout);
  return ret;
}
uint64_t in_process_bound_namespace()
{
  return in_process_storage ? in_process_storage->ns : 0;
}
uint64_t in_process_session_ns(sql::ObSQLSessionInfo *session)
{
  ns::NamespaceRuntime *runtime = session ? session->ns_runtime() : nullptr;
  return runtime != nullptr && runtime->ns().id() > 1
      ? runtime->ns().id() : 0;
}
int restore_namespace_registry() {
  if (!GCTX.sql_proxy_) { return OB_NOT_INIT; }
  ObMySQLProxy::MySQLResult result;
  sqlclient::ObMySQLResult *rows = nullptr;
  int ret = GCTX.sql_proxy_->read(result,
      "SELECT namespace_id,name FROM __fork_proto_meta.namespaces "
      "WHERE state=0 AND name!='__template__' ORDER BY namespace_id");
  if (OB_SUCC(ret) && OB_ISNULL(rows = result.get_result())) {
    ret = OB_ERR_UNEXPECTED;
  }
  while (OB_SUCC(ret)) {
    ret = rows->next();
    if (ret == OB_ITER_END) { ret = OB_SUCCESS; break; }
    uint64_t namespace_id = 0;
    ObString name;
    char name_buf[ns::Namespace::MAX_NAME_LEN];
    if (OB_FAIL(rows->get_uint(0L, namespace_id))) {
    } else if (OB_FAIL(rows->get_varchar(1L, name))) {
    } else if (namespace_id == 0 || namespace_id >= (1ULL << 30)
               || name.empty() || name.length() >= sizeof(name_buf)) {
      ret = OB_INVALID_ARGUMENT;
    } else {
      MEMCPY(name_buf, name.ptr(), name.length());
      name_buf[name.length()] = '\0';
      if (ns::namespace_registry().add(namespace_id, name_buf) != 0) {
        ret = OB_ERR_UNEXPECTED;
      }
    }
  }
  return ret;
}
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
int worker_send(const Frame &frame, bool cleanup) {
  return frame.ret ? frame.ret : in_process_storage
      ? in_process_send(*in_process_storage, frame, cleanup)
      : OB_ERR_UNEXPECTED;
}
int worker_read(Frame &frame) {
  return in_process_storage
      ? in_process_read(*in_process_storage, frame)
      : OB_ERR_UNEXPECTED;
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
      Frame request, reply; request.append(*session->get_tx_desc());
      error_ = tx_rpc('t', *session->get_tx_desc(), request, reply);
      if (!error_ && !reply.consumed()) { error_ = OB_INVALID_ARGUMENT; }
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
