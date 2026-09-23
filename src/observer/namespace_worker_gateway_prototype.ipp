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
int catalog_schema_guard(int64_t version, ObSchemaGetterGuard &guard) {
  auto &service = ObMultiVersionSchemaService::get_instance();
  int64_t current = OB_INVALID_VERSION;
  int ret = service.get_runtime_refreshed_schema_version(current, false);
  // A current guard must use the native current-version entry. During recovery
  // that is the core schema; the historical entry promotes it to the durable
  // baseline before full schema is available. Validate again after acquisition
  // so a concurrent refresh cannot silently change this request's snapshot.
  if (!ret) { ret = service.get_runtime_schema_guard(guard, version == current ? OB_INVALID_VERSION : version); }
  int64_t actual = OB_INVALID_VERSION;
  if (!ret) { ret = guard.get_schema_version(actual); }
  if (!ret && version != OB_INVALID_VERSION && actual != version) {
    fprintf(stderr,
        "PROTOTYPE_CATALOG_VERSION_MISMATCH requested=%lld current=%lld actual=%lld\n",
        (long long)version, (long long)current, (long long)actual);
    ret = OB_SCHEMA_EAGAIN;
  } else if (ret == OB_SCHEMA_EAGAIN) {
    fprintf(stderr,
        "PROTOTYPE_CATALOG_GUARD_EAGAIN requested=%lld current=%lld actual=%lld\n",
        (long long)version, (long long)current, (long long)actual);
  }
  return ret;
}
int catalog(uint64_t ns, Frame &request, Frame &reply) {
  int ret = OB_SUCCESS;
  const uint64_t id = request.number();
  const ObString name = request.string();
  const int64_t snapshot_version = static_cast<int64_t>(request.number());
  if (request.type() == 'k') {
    int64_t version = OB_INVALID_VERSION;
    if (!request.consumed() || id > 1 || snapshot_version != OB_INVALID_VERSION
        || (!name.empty() && name != "published")) { ret = OB_INVALID_ARGUMENT; }
    else if (ns == 1) {
      auto &service = ObMultiVersionSchemaService::get_instance();
      ret = name.empty() ? service.get_runtime_refreshed_schema_version(version, id != 0)
          : service.get_published_schema_version(version, id != 0);
    } else {
      ret = NamespaceForkKernelPrototype::namespace_schema_version(ns, version);
    }
    reply = Frame('c'); reply.number(ret); reply.number(version); return reply.ret;
  }
  const ObDatabaseSchema *database = nullptr;
  const ObTableSchema *table = nullptr;
  const ObUserInfo *user = nullptr;
  const ObSysVariableSchema *variables = nullptr;
  // Owner of the requested id, used below to reject ids that do not belong to
  // this channel's namespace. Raw ids only exist inside namespace 1.
  const uint64_t owner = NamespaceForkKernelPrototype::namespace_of(id);
  int64_t namespace_schema_version = OB_INVALID_VERSION;
  ObSchemaGetterGuard guard;
  if (ns == 1 && request.consumed() && (request.type() == 'd' || owns_table(ns, id))) {
    ret = catalog_schema_guard(snapshot_version, guard);
    if (!ret && request.type() == 'd') { ret = guard.get_database_schema(name, database); }
    else if (!ret && request.type() == 'b') { ret = guard.get_database_schema(id, database); }
    else if (!ret && (request.type() == 't' || request.type() == 'j')) { ret = guard.get_table_schema(id, name, request.type() == 'j', table); }
    else if (!ret && request.type() == 'i') { ret = guard.get_table_schema(id, table); }
    else if (!ret && request.type() == 'n') { ret = id == 1 ? guard.get_sys_variable_schema(variables) : OB_INVALID_ARGUMENT; }
    else if (!ret && request.type() == 'u') {
      if (id) { ret = name.empty() ? guard.get_user_info(id, user) : OB_INVALID_ARGUMENT; }
      else {
        ObSEArray<const ObUserInfo *, 4> users;
        ret = name.empty() ? guard.get_user_schemas_in_runtime(users) : guard.get_user_info(name, users);
        reply = Frame('c'); reply.number(ret); reply.number(users.count());
        for (const auto *item : users) { if (!ret) { reply.append(*item); } }
        return reply.ret;
      }
    }
  } else if (!request.consumed() || (request.type() == 'd' ? id != ns : owner != ns)) {
    ret = OB_INVALID_ARGUMENT;
  } else if (snapshot_version != OB_INVALID_VERSION
      && OB_FAIL(NamespaceForkKernelPrototype::namespace_schema_version(ns, namespace_schema_version))) {
  } else if (snapshot_version != OB_INVALID_VERSION && namespace_schema_version != snapshot_version) {
    ret = OB_SCHEMA_EAGAIN;
  } else if (request.type() == 'd') {
    ret = NamespaceForkKernelPrototype::database_in_namespace(ns, name, database);
  } else if (request.type() == 'b') {
    ret = NamespaceForkKernelPrototype::database_by_id(id, database);
  } else if (request.type() == 't') {
    ret = NamespaceForkKernelPrototype::schema_by_name(id, name, table);
  } else if (request.type() == 'i') {
    ret = NamespaceForkKernelPrototype::schema_by_id(id, table);
  } else if (request.type() == 'l') {
    ObArray<const ObTableSchema *> tables;
    ret = name.empty() ? NamespaceForkKernelPrototype::list_schemas(id, tables) : OB_INVALID_ARGUMENT;
    int64_t current_version = OB_INVALID_VERSION;
    if (!ret && snapshot_version != OB_INVALID_VERSION) {
      ret = NamespaceForkKernelPrototype::namespace_schema_version(ns, current_version);
      if (!ret && current_version != snapshot_version) { ret = OB_SCHEMA_EAGAIN; }
    }
    reply = Frame('c'); reply.number(ret); reply.number(ret ? 0 : tables.count());
    for (int64_t i = 0; !ret && i < tables.count(); ++i) { reply.append(*tables.at(i)); }
    return reply.ret;
  } else { ret = OB_NOT_SUPPORTED; }
  reply = Frame('c'); reply.number(ret); reply.number(database || table || user || variables ? 1 : 0);
  if (!ret && database) { reply.append(*database); }
  if (!ret && table) { reply.append(*table); }
  if (!ret && user) { reply.append(*user); }
  if (!ret && variables) { reply.append(*variables); }
  return reply.ret;
}
int serve_storage(StorageSpaceHandle storage_space, ReadScans *scans,
    EngineWrites *writes, int state, Frame &input, Frame &result) {
    const uint64_t ns = storage_space.namespace_id();
    int ret = OB_SUCCESS;
    if (!storage_space.is_namespace()) { return OB_INVALID_ARGUMENT; }
    if (input.type() == 'd' || input.type() == 'b' || input.type() == 't' || input.type() == 'i' || input.type() == 'j' || input.type() == 'k' || input.type() == 'l'
        || input.type() == 'u' || input.type() == 'n' || input.type() == 'p') {
      result = Frame('c');
      if (state) { result.number(state); }
      else { ret = catalog(ns, input, result); }
    } else if (input.type() == 'h') {
      result = Frame('r');
      if (state) { result.number(state); }
      else { ret = process_lob_read(
          storage_space, input, result, writes ? writes->tx : nullptr); }
    } else if (input.type() == 'Y') {
      result = Frame('w');
      if (state) { result.number(state); }
      else { ret = process_rootserver_local_runtime(storage_space, input, result); }
    } else if (scans && (input.type() == 'O' || input.type() == 'F' || input.type() == 'X' || input.type() == 'R')) {
      result = Frame('s');
      if (state && input.type() != 'X') { result.number(state); }
      else { ret = scans->process(input, result, writes ? writes->tx : nullptr, writes ? &writes->session : nullptr); }
    } else if (input.type() == 'M') {
      result = Frame('g');
      const uint64_t operation = input.number();
      if (operation == 2 || operation == 4 || operation == 5 || operation == 6) {
        const int64_t schema_version = operation == 4 || operation == 6
            ? static_cast<int64_t>(input.number()) : 0;
        int command_ret = state;
        if (!command_ret && !input.consumed()) {
          command_ret = OB_INVALID_ARGUMENT;
        } else if (!command_ret && operation == 2) {
          command_ret = storage::NamespaceForkKernelPrototype::begin_schema_change(ns);
        } else if (!command_ret && operation == 4) {
          command_ret = storage::NamespaceForkKernelPrototype::finish_schema_change(
              ns, schema_version);
        } else if (!command_ret && operation == 5) {
          bool recovery_needed = false;
          command_ret = storage::NamespaceForkKernelPrototype::begin_schema_recovery(
              ns, recovery_needed);
          result.number(0);
          result.number(command_ret);
          if (!command_ret) { result.number(recovery_needed); }
          return result.ret;
        } else if (!command_ret) {
          command_ret = storage::NamespaceForkKernelPrototype::finish_schema_recovery(
              ns, schema_version);
        }
        result.number(0);
        result.number(command_ret);
        return result.ret;
      }
      if (operation == 3) {
        const int command_ret = state ? state : apply_namespace_schema_delta(ns, input);
        result.number(0);
        result.number(command_ret);
        return result.ret;
      }
      ret = OB_INVALID_ARGUMENT;
    } else if (writes && (input.type() == 'T' || input.type() == 'W')) {
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
  if (input.type() == 'e' || input.type() == 'v') {
    const bool closing = input.type() == 'v';
    ret = input.consumed() ? OB_SUCCESS : OB_INVALID_ARGUMENT;
    if (!ret) {
      ctx.direct_insert.reset();
      ctx.scans.scans.clear();
      ctx.session.reset_reserved_snapshot_version();
      if (closing) {
        ctx.writes.reset();
        ctx.initialized = false;
      } else if (ctx.writes) {
        ret = ctx.writes->check_finished();
      }
    }
    result.number(ret);
  } else if (input.type() == 'J') {
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
        &ctx.scans, ctx.writes.get(), OB_SUCCESS, input, result);
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
    Frame request('v');
    in_process_send(*ctx, request, true);
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
