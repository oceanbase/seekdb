// Included in the Observer composition unit. Shared storage serves SQL workers.
#include "observer/namespace_worker_protocol_prototype.h"
#include "rootserver/fork_table/namespace_fork_kernel_prototype.h"
#include "rootserver/ddl_task/ob_ddl_task_util.h"
#include "storage/compaction/ob_freeze_info_mgr.h"
#include <map>
#include <mutex>
#include "rpc/ob_sql_request_operator.h"
#include "share/rpc/ob_server_task.h"
#include "rpc/frame/ob_req_processor.h"
extern "C" {
void *namespace_proto_spawn(uint64_t, uint64_t, uint32_t *);
int namespace_proto_send(void *, const char *, size_t);
int namespace_proto_receive(void *, const char **, size_t *, uint64_t);
void namespace_proto_stop(void *);
void namespace_proto_interrupt(void *);
int namespace_proto_dispatch(void *, void (*)(void *, const char *, size_t), void *);
int namespace_proto_worker_read(void (*)(void *, const char *, size_t), void *);
int namespace_proto_worker_write(const char *, size_t);
}
#include "observer/namespace_worker_multiplex_prototype.ipp"
#include "observer/namespace_worker_scan_prototype.ipp"
#include "observer/namespace_worker_write_prototype.ipp"
#include "observer/namespace_worker_range_prototype.ipp"
#include "observer/namespace_worker_direct_insert_prototype.ipp"
#include "observer/namespace_worker_commands_prototype.ipp"
#include "observer/namespace_worker_privileges_prototype.ipp"
#include "namespace/namespace.h"
namespace oceanbase { namespace observer { namespace namespace_worker_prototype {
using namespace common;
using namespace share::schema;
using storage::NamespaceForkKernelPrototype;
int check_sql_execution_role() {
  // User SQL executes only in namespace workers; the shared process owns
  // storage, fork control and the thin TCP router.
  // Ticket 05a gate: with SEEKDB_NAMESPACE_NS1_IN_PROCESS the shared process
  // also executes namespace-1 SQL in process (single-process Phase 1).
  // Ticket 05c gate: SEEKDB_NAMESPACE_FORKED_IN_PROCESS extends that to
  // forked namespaces.
  if (!worker_process && !ns1_in_process() && !forked_in_process()) {
    fprintf(stderr, "PROTOTYPE_V18_SHARED_SQL_REJECT\n");
    return OB_NOT_SUPPORTED;
  }
  return OB_SUCCESS;
}
int acquire_storage_snapshot(int64_t &snapshot) {
  snapshot = 0;
  // Freeze metadata remains SQL/schema state in the namespace worker. The
  // helper obtains only the transaction clock through ObITransactionService.
  return rootserver::ObDDLTaskUtil::calc_snapshot_with_gts(snapshot);
}
int reload_storage_freeze_info() {
  if (!worker_process) {
    auto *freeze = share::server_service<storage::ObFreezeInfoMgr>();
    return freeze ? freeze->reload_for_test() : OB_NOT_INIT;
  }
  Frame request('C'), reply; request.number(2);
  int ret = worker_send(request);
  if (!ret) { ret = worker_read(reply); }
  if (!ret && reply.type() != 'c') { ret = OB_INVALID_ARGUMENT; }
  if (!ret) { ret = static_cast<int>(reply.number()); }
  if (!ret && !reply.consumed()) { ret = OB_INVALID_ARGUMENT; }
  return ret;
}
bool worker_config_requires_restart(const common::ObConfigItem &config,
                                    const char *name)
{
  return config.reboot_effective()
      || 0 == std::strcmp(name, "ssl_client_authentication")
      || 0 == std::strcmp(name, "sql_protocol_min_tls_version");
}
int apply_dynamic_worker_config(const obcall::ObAdminSetConfigArg &arg,
                                int64_t &applied,
                                int64_t &restart_required) {
  applied = 0;
  restart_required = 0;
  int ret = arg.is_valid() ? OB_SUCCESS : OB_INVALID_ARGUMENT;
  for (int64_t i = 0; !ret && i < arg.items_.count(); ++i) {
    const auto &item = arg.items_.at(i);
    common::ObConfigItem *const *config = GCONF.get_container().get(
        common::ObConfigStringKey(item.name_.ptr()));
    if (OB_ISNULL(config) || OB_ISNULL(*config)) {
      ret = OB_ERR_SYS_CONFIG_UNKNOWN;
    } else if (worker_config_requires_restart(**config, item.name_.ptr())) {
      ++restart_required;
    } else {
      ObSqlString extra;
      if (OB_FAIL(extra.assign_fmt("%s=%s", item.name_.ptr(), item.value_.ptr()))) {
      } else if (OB_FAIL(GCONF.add_extra_config(
                     extra.ptr(), GCONF.update_version(), true))) {
      } else {
        ++applied;
      }
    }
  }
  return ret;
}
int broadcast_dynamic_worker_config(const obcall::ObAdminSetConfigArg &arg,
                                    uint64_t excluded_namespace);
int drain_storage_namespace_access(uint64_t namespace_id) {
  if (namespace_id <= 1 || namespace_id >= (1ULL << 30)) {
    return OB_INVALID_ARGUMENT;
  }
  if (!worker_process) {
    return storage::NamespaceForkKernelPrototype::drain_access();
  }
  if (worker_namespace != 1) { return OB_NOT_SUPPORTED; }
  Frame request('C'), reply;
  request.number(5);
  request.number(namespace_id);
  int ret = worker_send(request);
  if (!ret) { ret = worker_read(reply); }
  if (!ret && reply.type() != 'c') { ret = OB_INVALID_ARGUMENT; }
  if (!ret) { ret = static_cast<int>(reply.number()); }
  if (!ret && !reply.consumed()) { ret = OB_INVALID_ARGUMENT; }
  return ret;
}
int release_storage_namespace_schemas(uint64_t namespace_id,
                                      int64_t &table_count,
                                      int64_t &database_count) {
  table_count = 0;
  database_count = 0;
  if (namespace_id <= 1 || namespace_id >= (1ULL << 30)) {
    return OB_INVALID_ARGUMENT;
  }
  if (!worker_process) {
    return storage::NamespaceForkKernelPrototype::release_namespace_schemas(
        namespace_id, table_count, database_count);
  }
  if (worker_namespace != 1) { return OB_NOT_SUPPORTED; }
  Frame request('C'), reply;
  request.number(6);
  request.number(namespace_id);
  int ret = worker_send(request);
  if (!ret) { ret = worker_read(reply); }
  if (!ret && reply.type() != 'c') { ret = OB_INVALID_ARGUMENT; }
  if (!ret) { ret = static_cast<int>(reply.number()); }
  if (!ret) {
    table_count = static_cast<int64_t>(reply.number());
    database_count = static_cast<int64_t>(reply.number());
  }
  if (!ret && !reply.consumed()) { ret = OB_INVALID_ARGUMENT; }
  return ret;
}
int admin_set_config(obcall::ObAdminSetConfigArg &arg) {
  if (worker_namespace != 1) { return OB_NOT_SUPPORTED; }
  Frame request('M'), reply; request.number(1); request.append(arg);
  int ret = worker_send(request);
  if (!ret) { ret = worker_read(reply); }
  if (!ret && reply.type() != 'g') { ret = OB_INVALID_ARGUMENT; }
  if (!ret) { ret = static_cast<int>(reply.number()); }
  if (!ret && !reply.consumed()) { ret = OB_INVALID_ARGUMENT; }
  // The shared process validates and persists the authoritative configuration.
  // Apply the committed values to the SQL process that issued ALTER SYSTEM as
  // well; otherwise packet admission and SQL features keep using stale GCONF
  // values until this Worker is restarted.
  int64_t applied = 0;
  int64_t restart_required = 0;
  if (!ret) {
    ret = apply_dynamic_worker_config(arg, applied, restart_required);
  }
  fprintf(stderr,
      "PROTOTYPE_NAMESPACE_WORKER_CONFIG_APPLIED ns=%llu applied=%lld restart=%lld ret=%d\n",
      static_cast<unsigned long long>(worker_namespace),
      static_cast<long long>(applied),
      static_cast<long long>(restart_required), ret);
  return ret;
}
bool is_storage_request(char type) {
  return type == 'C'
      || type == 'd' || type == 'b' || type == 't' || type == 'i' || type == 'j' || type == 'k' || type == 'l'
      || type == 'u' || type == 'n' || type == 'p'
      || type == 'O' || type == 'F' || type == 'X' || type == 'M' || type == 'T' || type == 'W' || type == 'G' || type == 'J' || type == 'Y'
      || type == 'R'
      || type == 'h' || type == 'A';
}
// One admitted storage RPC at a time per SQL request. Native request workers
// execute it; the pipe reader only submits the task. No per-session thread.
struct StorageDispatch : std::enable_shared_from_this<StorageDispatch> {
  std::mutex mutex;
  std::mutex execution_mutex;
  std::condition_variable changed;
  size_t active = 0;
  bool closed = false;
  std::function<int(Frame &)> process;
  void finish() {
    std::lock_guard<std::mutex> guard(mutex);
    --active; changed.notify_all();
  }
  void close() {
    std::unique_lock<std::mutex> guard(mutex);
    closed = true;
    changed.wait(guard, [&] { return active == 0; });
    process = {};
  }
  struct Task final : rpc::ObSrvTask {
    struct Processor final : rpc::frame::ObReqProcessor {
      std::shared_ptr<StorageDispatch> owner;
      Frame input;
      bool finished = false;
      Processor(std::shared_ptr<StorageDispatch> context, Frame frame)
          : owner(std::move(context)), input(std::move(frame)) {}
      ~Processor() { if (!finished) { owner->finish(); } }
      int run() override {
        std::lock_guard<std::mutex> guard(owner->execution_mutex);
        // Dispatch tasks are not deadline-driven requests. Runtime threads
        // recycle a stale timeout from earlier request processors, which would
        // make inner SQL below instantly time out.
        THIS_WORKER.set_timeout_ts(INT64_MAX);
        ObCurTraceId::TraceId trace;
        if (is_storage_request(input.type()) || (input.tag().slot & WORKER_REQUEST)) {
          input.read(trace);
        }
        // Background metadata and local cleanup can start without a SQL trace.
        // Native DDL task admission requires a valid request identity as well.
        if (!trace.is_valid()) { trace.init(GCTX.self_addr()); }
        ObTraceIdGuard trace_guard(trace);
        const int ret = input.ret ? input.ret : owner->process(input);
        owner->finish(); finished = true;
        return ret;
      }
    } processor;
    Task(std::shared_ptr<StorageDispatch> context, Frame frame)
        : processor(std::move(context), std::move(frame)) {}
    rpc::frame::ObReqProcessor &get_processor() override { return processor; }
  };
  int submit(Frame frame) {
    {
      std::lock_guard<std::mutex> guard(mutex);
      if (closed) { return OB_CONNECT_ERROR; }
      // The successor may arrive after reply publication but before run()
      // returns. Keep one such slot and serialize access to native owners.
      if (active >= 2) { return OB_SIZE_OVERFLOW; }
      ++active;
    }
    auto *task = OB_NEW(Task, "NsStorageRPC", shared_from_this(), std::move(frame));
    if (!task) { finish(); return OB_ALLOCATE_MEMORY_FAILED; }
    int ret = share::check_server_runtime_ready();
    if (!ret) { ret = static_cast<omt::ObServerRuntime *>(share::server_runtime())->recv_request(*task); }
    if (ret) { ob_delete(task); }
    return ret;
  }
};
struct Channel;
int receive_direct_storage(Channel &channel, Frame input);
void stop_direct_storage(Channel &channel);
int recover_channel(uint64_t ns, uint64_t failed_generation);
struct Channel : std::enable_shared_from_this<Channel> {
  explicit Channel(StorageSpaceHandle space) : storage_space(space) {}
  void *handle = nullptr;
  uint64_t generation = 0;
  uint32_t pid = 0;
  std::string client_endpoint;
  std::atomic<bool> closed{false};
  std::atomic<bool> restart_on_failure{false};
  RequestRoutes routes;
  RequestRoutes storage_routes{WORKER_REQUEST};
  const StorageSpaceHandle storage_space;
  std::mutex bindings_mutex;
  SessionBinding *bindings = nullptr;
  ~Channel() {
    restart_on_failure = false;
    fail();
    namespace_proto_stop(handle);
  }
  void fail();
  int send(const Frame &frame) {
    if (closed) { return OB_CONNECT_ERROR; }
    const int ret = frame.ret ? frame.ret : namespace_proto_send(handle, frame.data.data(), frame.data.size()) == 0
        ? OB_SUCCESS : OB_CONNECT_ERROR;
    if (ret) { fail(); }
    return ret;
  }
  int receive(Frame &frame, uint64_t timeout = 30000) {
    const char *data = nullptr; size_t n = 0;
    const int result = namespace_proto_receive(handle, &data, &n, timeout);
    if (result) { return result == 1 ? OB_TIMEOUT : OB_CONNECT_ERROR; }
    if (n < Frame::HEADER_SIZE) { return OB_INVALID_ARGUMENT; }
    frame = Frame(); frame.data.assign(data, data + n); return OB_SUCCESS;
  }
  static void receive_frame(void *context, const char *data, size_t size) {
    auto &channel = *static_cast<Channel *>(context);
    if (channel.closed) { return; }
    if (!data || size < Frame::HEADER_SIZE) { channel.fail(); return; }
    Frame frame; frame.data.assign(data, data + size);
    if (frame.tag().slot & WORKER_REQUEST) {
      if (receive_direct_storage(channel, std::move(frame))) { channel.fail(); }
      return;
    }
    auto request = channel.routes.find(frame.tag());
    // The Rust pipe reader only dispatches. It never waits for a request's
    // consumer, executes storage work, or calls a client's packet sender.
    if (request) {
      const int ret = is_storage_request(frame.type()) && request->dispatch_storage
          ? request->dispatch_storage(std::move(frame)) : request->post(std::move(frame));
      if (ret) { channel.fail(); }
    }
  }

};
struct Child {
  std::mutex mutex;
  std::shared_ptr<Channel> current;
  uint64_t published_generation = 0;
};
std::mutex children_mutex;
std::map<uint64_t, std::shared_ptr<Child>> children;
std::mutex worker_config_overrides_mutex;
std::map<std::string, std::string> worker_config_overrides;
bool worker_config_overrides_loaded = false;
uint64_t next_generation = 0;
int ensure_channel(uint64_t ns, std::shared_ptr<Channel> &channel);
int stop_channel(uint64_t ns);
struct SessionBinding {
  std::shared_ptr<Channel> channel;
  uint64_t slot = 0, slot_generation = 0;
  sql::ObSQLSessionInfo *gateway = nullptr;
  bool internal = false;
  std::shared_ptr<PendingRequest> direct_request;
  std::unique_ptr<EngineWrites> writes;
  // In-process (ticket 05c) storage context; owned explicitly by
  // close_session because InProcessStorage is complete only later in this
  // unit. Always null on worker-mode channel bindings.
  InProcessStorage *in_process = nullptr;
  SessionBinding *previous = nullptr, *next = nullptr;
  bool linked = false;
  ~SessionBinding() {
    if (linked) {
      std::lock_guard<std::mutex> guard(channel->bindings_mutex);
      if (previous) { previous->next = next; } else { channel->bindings = next; }
      if (next) { next->previous = previous; }
    }
    writes.reset();
    if (gateway && !internal) { share::server_service<sql::ObSQLSessionMgr>()->revert_session(gateway); }
  }
};
void Channel::fail() {
  bool restart = false;
  if (!closed.exchange(true)) {
    stop_direct_storage(*this);
    routes.fail(); namespace_proto_interrupt(handle);
    // Shutdown only schedules native connection teardown. The IPC reader does
    // not wait for a session lock, execute rollback, or run another SQL engine.
    std::lock_guard<std::mutex> guard(bindings_mutex);
    for (auto *binding = bindings; binding; binding = binding->next) {
      auto &socket = binding->gateway->get_sock_desc();
      if (socket.sock_desc_) { SQL_REQ_OP.disconnect_by_sql_sock_desc(socket); }
    }
    restart = restart_on_failure && storage_space.is_namespace();
  }
  if (restart) {
    const uint64_t namespace_id = storage_space.namespace_id();
    const uint64_t failed_generation = generation;
    auto recovery = std::make_shared<StorageDispatch>();
    recovery->process = [namespace_id, failed_generation](Frame &) {
      // ObServerRuntime may reuse a worker whose previous request deadline has
      // expired. Recovery is an independent background operation; otherwise
      // its GLOBAL endpoint write can fail before it is sent to namespace 1.
      const int64_t previous_timeout = THIS_WORKER.get_timeout_ts();
      THIS_WORKER.set_timeout_ts(INT64_MAX);
      const int ret = recover_channel(namespace_id, failed_generation);
      THIS_WORKER.set_timeout_ts(previous_timeout);
      return ret;
    };
    const int ret = recovery->submit(Frame());
    if (ret != OB_SUCCESS) {
      fprintf(stderr,
          "PROTOTYPE_NAMESPACE_ENDPOINT_RECOVERY_SUBMIT ns=%llu generation=%llu ret=%d\n",
          static_cast<unsigned long long>(namespace_id),
          static_cast<unsigned long long>(failed_generation), ret);
    }
  }
}
sql::ObSQLSessionInfo *bound_session(SessionBinding *binding) { return binding ? binding->gateway : nullptr; }
int attach(uint64_t ns, std::shared_ptr<Child> &child) {
  if (ns == 0 || ns >= (1ULL << 30)) { return OB_NOT_SUPPORTED; }
  std::lock_guard<std::mutex> guard(children_mutex);
  auto it = children.find(ns);
  if (it == children.end()) {
    it = children.emplace(ns, std::make_shared<Child>()).first;
  }
  child = it->second; return OB_SUCCESS;
}
// These scalar mirrors are needed by gateway protocol encoding. SQL variables
// and their allocator remain in the worker; no complete session is serialized.
const share::ObSysVarClassType state_vars[] = {
  share::SYS_VAR_CHARACTER_SET_CLIENT, share::SYS_VAR_CHARACTER_SET_CONNECTION,
  share::SYS_VAR_CHARACTER_SET_RESULTS, share::SYS_VAR_COLLATION_CONNECTION,
  share::SYS_VAR_COLLATION_DATABASE, share::SYS_VAR_SQL_MODE, share::SYS_VAR_OB_QUERY_TIMEOUT,
  share::SYS_VAR_AUTOCOMMIT
};
int append_session_state(sql::ObSQLSessionInfo &session, Frame &frame, bool identity) {
  frame.number(session.get_database_id()); frame.string(session.get_database_name());
  int ret = OB_SUCCESS;
  for (auto id : state_vars) {
    ObObj value;
    if ((ret = session.get_sys_variable(id, value))) { return ret; }
    frame.number(value.is_uint64() ? value.get_uint64() : static_cast<uint64_t>(value.get_int()));
  }
  if (identity) {
    // Authentication is performed before the worker session is opened. Carry
    // the verified identity so permission checks in the worker use the same user.
    frame.number(session.get_user_id());
    frame.string(session.get_user_name());
    frame.string(session.get_host_name());
  }
  return frame.ret;
}
int apply_session_state(sql::ObSQLSessionInfo &session, Frame &frame) {
  const uint64_t db = frame.number(); const ObString name = frame.string();
  int ret = frame.ret;
  if (!ret) {
    ObString logical_name = name;
    const ObDatabaseSchema *database = nullptr;
    if (NamespaceForkKernelPrototype::is_namespace_address(name)
        && NamespaceForkKernelPrototype::database_by_address(name, database) == OB_SUCCESS
        && database != nullptr) {
      logical_name = database->get_database_name_str();
    }
    ret = session.set_default_database(logical_name); session.set_database_id(db);
  }
  for (auto id : state_vars) {
    const uint64_t value = frame.number();
    if (!ret) { ret = frame.ret ? frame.ret : session.update_sys_variable(id, static_cast<int64_t>(value)); }
  }
  return ret ? ret : frame.ret;
}
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
  if (request.type() == 'p') {
    if (!request.consumed() || ns != 1 || id != 1) {
      reply = Frame('c'); reply.number(OB_INVALID_ARGUMENT); return reply.ret;
    }
    return process_privilege_read(snapshot_version, name, reply);
  }
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
    if (input.type() == 'C') {
      const uint64_t operation = input.number();
      int command_ret = state;
      std::shared_ptr<Channel> endpoint;
      uint64_t target_namespace = 0;
      int64_t released_tables = 0;
      int64_t released_databases = 0;
      if (!command_ret
          && (operation == 3 || operation == 4 || operation == 5 || operation == 6)) {
        target_namespace = input.number();
      }
      if (!command_ret && !input.consumed()) {
        command_ret = OB_INVALID_ARGUMENT;
      } else if (!command_ret && operation == 2) {
        auto *freeze = share::server_service<storage::ObFreezeInfoMgr>();
        command_ret = freeze ? freeze->reload_for_test() : OB_NOT_INIT;
      } else if (!command_ret && operation == 3) {
        command_ret = ns != 1 || target_namespace <= 1
            ? OB_NOT_SUPPORTED : ensure_channel(target_namespace, endpoint);
      } else if (!command_ret && operation == 4) {
        command_ret = ns != 1 || target_namespace <= 1
            ? OB_NOT_SUPPORTED : stop_channel(target_namespace);
      } else if (!command_ret && operation == 5) {
        command_ret = ns != 1 || target_namespace <= 1
            ? OB_NOT_SUPPORTED : NamespaceForkKernelPrototype::drain_access();
        fprintf(stderr,
            "PROTOTYPE_NAMESPACE_ACCESS_DRAINED ns=%llu ret=%d\n",
            (unsigned long long)target_namespace, command_ret);
      } else if (!command_ret && operation == 6) {
        command_ret = ns != 1 || target_namespace <= 1
            ? OB_NOT_SUPPORTED
            : NamespaceForkKernelPrototype::release_namespace_schemas(
                target_namespace, released_tables, released_databases);
        fprintf(stderr,
            "PROTOTYPE_NAMESPACE_SCHEMA_HOLDERS_RELEASED ns=%llu tables=%lld databases=%lld ret=%d\n",
            (unsigned long long)target_namespace,
            (long long)released_tables, (long long)released_databases, command_ret);
      } else if (!command_ret) {
        command_ret = OB_INVALID_ARGUMENT;
      }
      result = Frame('c'); result.number(command_ret);
      if (!command_ret && operation == 3) {
        result.string(ObString(endpoint->client_endpoint.size(), endpoint->client_endpoint.data()));
        result.number(endpoint->generation);
        result.number(endpoint->pid);
      } else if (!command_ret && operation == 6) {
        result.number(released_tables);
        result.number(released_databases);
      }
      ret = result.ret;
    } else if (input.type() == 'd' || input.type() == 'b' || input.type() == 't' || input.type() == 'i' || input.type() == 'j' || input.type() == 'k' || input.type() == 'l'
        || input.type() == 'u' || input.type() == 'n' || input.type() == 'p') {
      result = Frame('c');
      if (state) { result.number(state); }
      else { ret = catalog(ns, input, result); }
    } else if (input.type() == 'h') {
      result = Frame('r');
      if (state) { result.number(state); }
      else { ret = process_lob_read(
          storage_space, input, result, writes ? writes->tx : nullptr); }
    } else if (input.type() == 'A') {
      result = Frame('g');
      if (state) { result.number(state); }
      else { ret = process_tablet_autoincrement_cache_invalidation(
          storage_space, input, result); }
    } else if (input.type() == 'Q') {
      result = Frame('g');
      if (state) { result.number(state); }
      else { ret = process_tablet_autoincrement_next_value(
          storage_space, input, result); }
    } else if (input.type() == 'G') {
      result = Frame('g');
      if (state) { result.number(state); }
      else { ret = process_ranges(storage_space, input, result); }
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
      obcall::ObAdminSetConfigArg arg;
      input.read(arg);
      int command_ret = state ? state : ns != 1 || operation != 1 ? OB_NOT_SUPPORTED
          : !input.consumed() || !arg.is_valid() ? OB_INVALID_ARGUMENT : OB_SUCCESS;
      if (!command_ret) { command_ret = ObServer::get_instance().get_local_management_service().admin_set_config(arg); }
      if (!command_ret) { command_ret = broadcast_dynamic_worker_config(arg, ns); }
      result.number(command_ret);
    } else if (writes && (input.type() == 'T' || input.type() == 'W')) {
      result = Frame('w');
      if (state && !cleanup_write(input)) { result.number(state); }
      else { ret = writes->process(input, result); }
    } else { ret = OB_INVALID_ARGUMENT; }
    return ret;
}
// A direct client has one storage context for its connection lifetime. This is
// the native transaction/session metadata required by storage, without a second
// network connection, SQL plan cache, or protocol session in the shared process.
struct DirectStorageContext {
  std::shared_ptr<StorageSessionState> session_state = std::make_shared<StorageSessionState>();
  sql::ObSQLSessionInfo &session = session_state->session;
  std::unique_ptr<EngineWrites> writes;
  ReadScans scans;
  DirectInsertRoute direct_insert;
  std::weak_ptr<Channel> channel;
  RequestTag tag;
  bool initialized = false;
  DirectStorageContext(std::shared_ptr<Channel> owner, RequestTag request)
      : scans(owner->storage_space), channel(owner), tag(request) {}
  int process(Frame &input) {
    auto owner = channel.lock();
    if (!owner) { return OB_CONNECT_ERROR; }
    Frame result('l');
    int ret = OB_SUCCESS;
    bool closing = false;
    const int64_t timeout = static_cast<int64_t>(input.number());
    if (input.ret || timeout <= 0) { return OB_INVALID_ARGUMENT; }
    const int64_t old_timeout = THIS_WORKER.get_timeout_ts();
    auto *old_session = THIS_WORKER.get_session();
    THIS_WORKER.set_timeout_ts(timeout);
    THIS_WORKER.set_session(initialized ? &session : nullptr);
    if (input.type() == 'L') {
      const uint64_t sid = input.number();
      const uint64_t internal = input.number();
      if (initialized || !input.consumed() || internal > 1 || (!sid && !internal) || sid > UINT32_MAX) { ret = OB_INVALID_ARGUMENT; }
      else { ret = session.test_init(1, static_cast<uint32_t>(sid), &session_state->allocator); }
      // A sid-less internal route is a short-lived catalog/background storage
      // context. Loading variables for it can recurse into a worker that is
      // still initializing; transaction sessions with a real SID stay native.
      if (!ret && (sid != 0 || !internal)) { ret = session.load_default_sys_variable(false, false); }
      if (!ret) {
        writes = std::make_unique<EngineWrites>(owner->storage_space, session);
        initialized = true;
      }
      fprintf(stderr, "PROTOTYPE_NATIVE_DIRECT_OPEN ns=%llu sid=%llu internal=%llu ret=%d\n",
          (unsigned long long)owner->storage_space.namespace_id(),
          (unsigned long long)sid,
          (unsigned long long)internal, ret);
      result.number(ret);
    } else if (!initialized) {
      ret = OB_NOT_INIT;
    } else if (input.type() == 'e' || input.type() == 'v') {
      closing = input.type() == 'v';
      ret = input.consumed() ? OB_SUCCESS : OB_INVALID_ARGUMENT;
      if (!ret) {
        direct_insert.reset();
        scans.scans.clear(); session.reset_reserved_snapshot_version();
        if (closing) { writes.reset(); initialized = false; }
        else { ret = writes->check_finished(); }
      }
      result.number(ret);
    } else if (input.type() == 'J') {
      ret = direct_insert.process(owner->storage_space, tag, owner->storage_routes,
          session_state, input, result);
    } else {
      ret = serve_storage(owner->storage_space, &scans, writes.get(),
          OB_SUCCESS, input, result);
    }
    THIS_WORKER.set_session(old_session);
    THIS_WORKER.set_timeout_ts(old_timeout);
    if (!ret) {
      Frame credit('K'); credit.tag(tag);
      ret = owner->send(credit);
    }
    result.tag(tag);
    if (closing) { owner->storage_routes.release(tag); }
    if (!ret) { ret = owner->send(result); }
    if (ret) { owner->fail(); }
    return ret;
  }
};
// ---------------------------------------------------------------------------
// Ticket 05c: in-process storage context, the channel-free twin of
// DirectStorageContext above. Owns the native storage session, open scans
// and write engine for one SQL session of one in-process namespace. Lives on
// the session's SessionBinding; frames are delivered synchronously.
// ---------------------------------------------------------------------------
struct InProcessStorage {
  const uint64_t ns;
  std::shared_ptr<StorageSessionState> session_state;
  sql::ObSQLSessionInfo &session; // storage-side native session
  std::unique_ptr<EngineWrites> writes;
  ReadScans scans;
  DirectInsertRoute direct_insert;
  RequestRoutes *direct_insert_routes = nullptr;
  std::shared_ptr<PendingRequest> direct_insert_request;
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
      direct_insert_routes = static_cast<RequestRoutes *>(
          runtime->service(::oceanbase::ns::NamespaceRuntime::DIRECT_INSERT_ROUTES));
    }
  }
  ~InProcessStorage() {
    if (direct_insert_routes != nullptr && direct_insert_request) {
      direct_insert_routes->release(direct_insert_request->tag, true);
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
// Synchronous twin of DirectStorageContext::process minus the channel: the
// 'L' attach is in_process_open above, credits/tags/trace envelopes do not
// exist on a function-call boundary, and cancellation is the session's own
// interrupt check.
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
    if (ctx.direct_insert_routes == nullptr) {
      ret = OB_NOT_INIT;
    } else {
      if (!ctx.direct_insert_request) {
        ctx.direct_insert_request = ctx.direct_insert_routes->allocate(false);
      }
      if (!ctx.direct_insert_request) {
        ret = OB_EAGAIN;
      } else {
        ret = ctx.direct_insert.process(
            StorageSpaceHandle::namespace_space(ctx.ns),
            ctx.direct_insert_request->tag, *ctx.direct_insert_routes,
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
  return !worker_process && forked_in_process()
      && runtime != nullptr && runtime->ns().id() > 1
      ? runtime->ns().id() : 0;
}
int receive_direct_storage(Channel &channel, Frame input) {
  auto request = channel.storage_routes.find(input.tag());
  if (input.type() == 'L') {
    if (request) { return OB_INVALID_ARGUMENT; }
    request = channel.storage_routes.accept(input.tag());
    if (!request) { return OB_SIZE_OVERFLOW; }
    auto state = std::make_shared<DirectStorageContext>(channel.shared_from_this(), input.tag());
    auto dispatch = std::make_shared<StorageDispatch>();
    dispatch->process = [state](Frame &frame) { return state->process(frame); };
    request->dispatch_storage = [dispatch](Frame frame) { return dispatch->submit(std::move(frame)); };
  }
  if (!request || !request->dispatch_storage) { return OB_INVALID_ARGUMENT; }
  return request->dispatch_storage(std::move(input));
}
void stop_direct_storage(Channel &channel) {
  // Move the route owners to a native task. Destruction rolls back idle
  // transactions and releases scans off the IPC reader. An executing RPC keeps
  // its own reference until it finishes; no cross-thread owner destruction.
  std::vector<std::shared_ptr<PendingRequest>> detached;
  {
    std::lock_guard<std::mutex> guard(channel.storage_routes.mutex);
    channel.storage_routes.closed = true;
    for (auto &slot : channel.storage_routes.slots) {
      if (slot.request) {
        slot.request->fail(OB_CONNECT_ERROR);
        detached.push_back(std::move(slot.request));
      }
    }
  }
  if (!detached.empty()) {
    auto cleanup = std::make_shared<StorageDispatch>();
    cleanup->process = [owners = std::move(detached)](Frame &) mutable {
      owners.clear(); return OB_SUCCESS;
    };
    // During normal channel failure the shared runtime is still running.
    // Shutdown ordering keeps storage alive until outstanding tasks drain.
    const int ret = cleanup->submit(Frame());
    if (ret) { fprintf(stderr, "PROTOTYPE_V19_STORAGE_CLEANUP_SUBMIT ret=%d\n", ret); }
  }
}
// SQL results retain credit-based streaming. Storage RPCs execute independently
// through the native shared runtime, including while the result consumer sleeps.
struct Exchange {
  Channel &channel;
  ReadScans *scans;
  EngineWrites *writes;
  bool query, finished = false;
  int error = OB_SUCCESS;
  std::atomic<int> cancelled{OB_SUCCESS};
  std::shared_ptr<PendingRequest> pending;
  std::shared_ptr<StorageDispatch> storage;
  Exchange(Channel &c, Frame request, ReadScans *s, int64_t deadline, EngineWrites *w)
      : channel(c), scans(s), writes(w),
        query(request.type() == 'Q' || request.type() == 'U' || request.type() == 'I') {
    pending = channel.routes.allocate(!query);
    if (!pending) { error = channel.closed ? OB_CONNECT_ERROR : OB_EAGAIN; return; }
    // A positive explicit deadline applies to every frame type. Callers that
    // omit it keep the historical policy: queries are unbounded and control
    // exchanges get a 30-second safety limit.
    pending->deadline = deadline > 0 ? deadline
        : query ? INT64_MAX : ObTimeUtility::current_time() + 30L * 1000000;
    storage = std::make_shared<StorageDispatch>();
    storage->process = [this](Frame &input) { return serve(input); };
    pending->dispatch_storage = [context = storage](Frame input) { return context->submit(std::move(input)); };
    if (request.type() == 'a' || request.type() == 'I') {
      // Preserve the initiating call across inner connections and callbacks.
      // Background callers establish a trace once on their native thread.
      if (!ObCurTraceId::get_trace_id()->is_valid()) { ObCurTraceId::init(GCTX.self_addr()); }
      Frame tagged(request.type(), request.limit);
      tagged.append(*ObCurTraceId::get_trace_id());
      tagged.data.insert(tagged.data.end(), request.data.begin() + Frame::HEADER_SIZE, request.data.end());
      if (!tagged.ret) { tagged.ret = request.ret; }
      if (tagged.data.size() > tagged.limit) { tagged.ret = OB_SIZE_OVERFLOW; }
      request = std::move(tagged);
    }
    request.tag(pending->tag); error = channel.send(request);
  }
  ~Exchange() {
    if (pending) { channel.routes.release(pending->tag, true); }
    // The borrowed native session and scan owners outlive this Exchange.
    // On pipe failure, drain an already admitted task before releasing either.
    if (storage) { storage->close(); }
  }
  int cancel(int reason) {
    if (cancelled || finished || !pending) { return error; }
    cancelled = reason;
    Frame message('Z'); message.tag(pending->tag);
    message.number(reason == OB_TIMEOUT ? OB_TIMEOUT : OB_ERR_QUERY_INTERRUPTED);
    fprintf(stderr, "PROTOTYPE_V13_CANCEL request=%llu generation=%llu ret=%d\n",
        (unsigned long long)pending->tag.slot, (unsigned long long)pending->tag.generation, reason);
    return error = channel.send(message);
  }
  int serve(Frame &input) {
    const int64_t saved_timeout = THIS_WORKER.get_timeout_ts();
    auto *saved_session = THIS_WORKER.get_session();
    THIS_WORKER.set_timeout_ts(pending->deadline);
    THIS_WORKER.set_session(writes ? &writes->session : nullptr);
    Frame result;
    int ret = serve_storage(channel.storage_space, scans, writes,
        cancelled.load(), input, result);
    THIS_WORKER.set_session(saved_session);
    THIS_WORKER.set_timeout_ts(saved_timeout);
    result.tag(pending->tag);
    // Send credit before the RPC reply; the SQL worker needs both before its
    // next send. Dispatch serializes a successor arriving during publication.
    if (!ret) {
      Frame credit('K'); credit.tag(pending->tag);
      ret = channel.send(credit);
    }
    if (!ret) { ret = channel.send(result); }
    if (ret) { channel.fail(); }
    return ret;
  }
  int next(Frame &reply) {
    if (finished) { return OB_ITER_END; }
    while (!error) {
      error = pending->take(reply, cancelled != OB_SUCCESS);
      if (error == OB_TIMEOUT && query && !cancelled) { cancel(error); continue; }
      if (error) { break; }
      if (reply.type() == 'D') { finished = true; return OB_SUCCESS; }
      Frame credit('K'); credit.tag(pending->tag);
      if ((error = channel.send(credit))) { break; }
      if (!cancelled) { return OB_SUCCESS; }
    }
    channel.fail(); return error;
  }
};
int exchange(Channel &channel, Frame request, ReadScans *scans,
             const std::function<int(Frame &)> &response, int64_t deadline = 0,
             EngineWrites *writes = nullptr) {
  Exchange pump(channel, std::move(request), scans, deadline, writes);
  Frame reply;
  int ret = OB_SUCCESS;
  while (!(ret = pump.next(reply))) {
    if (reply.type() == 'D') {
      const int query_ret = static_cast<int>(reply.number());
      if (pump.query && !reply.ret && !reply.consumed()) { ret = response(reply); }
      if (!ret && !reply.consumed()) { ret = OB_INVALID_ARGUMENT; }
      if (ret) { channel.fail(); return ret; }
      const int transaction_ret = writes ? writes->check_finished() : OB_SUCCESS;
      return pump.cancelled ? pump.cancelled.load() : query_ret ? query_ret : transaction_ret;
    }
    if ((ret = response(reply))) {
      if (!pump.query || channel.closed || pump.cancel(ret)) {
        channel.fail(); return ret;
      }
    }
  }
  return ret;
}
int load_worker_config_overrides_locked()
{
  int ret = OB_SUCCESS;
  if (!worker_config_overrides_loaded) {
    if (OB_ISNULL(GCTX.config_mgr_)) {
      ret = OB_NOT_INIT;
    } else if (OB_FAIL(GCTX.config_mgr_->get_storage().load_namespace_worker_configs(
                   worker_config_overrides))) {
    } else {
      worker_config_overrides_loaded = true;
      fprintf(stderr,
          "PROTOTYPE_NAMESPACE_WORKER_CONFIG_RESTORED count=%llu\n",
          static_cast<unsigned long long>(worker_config_overrides.size()));
    }
  }
  return ret;
}
int remember_dynamic_worker_config(const obcall::ObAdminSetConfigArg &arg)
{
  std::lock_guard<std::mutex> guard(worker_config_overrides_mutex);
  int ret = arg.is_valid() ? OB_SUCCESS : OB_INVALID_ARGUMENT;
  if (!ret) { ret = load_worker_config_overrides_locked(); }
  for (int64_t i = 0; !ret && i < arg.items_.count(); ++i) {
    const auto &item = arg.items_.at(i);
    common::ObConfigItem *const *config = GCONF.get_container().get(
        common::ObConfigStringKey(item.name_.ptr()));
    if (OB_ISNULL(config) || OB_ISNULL(*config)) {
      ret = OB_ERR_SYS_CONFIG_UNKNOWN;
    } else if (!worker_config_requires_restart(**config, item.name_.ptr())) {
      ret = GCTX.config_mgr_->get_storage().upsert_namespace_worker_config(
          item.name_.ptr(), item.value_.ptr());
      if (!ret) {
        worker_config_overrides[item.name_.ptr()] = item.value_.ptr();
      }
    }
  }
  return ret;
}
int remembered_dynamic_worker_config(obcall::ObAdminSetConfigArg &arg)
{
  std::lock_guard<std::mutex> guard(worker_config_overrides_mutex);
  int ret = load_worker_config_overrides_locked();
  for (const auto &entry : worker_config_overrides) {
    obcall::ObAdminSetConfigItem item;
    common::ObConfigItem *const *config = GCONF.get_container().get(
        common::ObConfigStringKey(entry.first.c_str()));
    if (OB_ISNULL(config) || OB_ISNULL(*config)) {
      ret = OB_ERR_SYS_CONFIG_UNKNOWN;
    } else if (worker_config_requires_restart(**config, entry.first.c_str())) {
      continue;
    } else if (OB_FAIL(item.name_.assign(entry.first.c_str()))) {
    } else if (OB_FAIL(item.value_.assign(entry.second.c_str()))) {
    } else {
      ret = arg.items_.push_back(item);
    }
  }
  return ret;
}
int send_dynamic_worker_config(Channel &channel,
                               const obcall::ObAdminSetConfigArg &arg)
{
  if (arg.items_.empty()) { return OB_SUCCESS; }
  Frame request('V');
  request.append(arg);
  return request.ret ? request.ret
      : exchange(channel, std::move(request), nullptr,
                 [](Frame &) { return OB_INVALID_ARGUMENT; });
}
int broadcast_dynamic_worker_config(const obcall::ObAdminSetConfigArg &arg,
                                    uint64_t excluded_namespace)
{
  int ret = remember_dynamic_worker_config(arg);
  std::vector<std::pair<uint64_t, std::shared_ptr<Child>>> snapshot;
  {
    std::lock_guard<std::mutex> guard(children_mutex);
    snapshot.assign(children.begin(), children.end());
  }
  int64_t delivered = 0;
  if (!ret) {
    for (const auto &entry : snapshot) {
      if (entry.first == excluded_namespace) { continue; }
      std::shared_ptr<Channel> channel;
      {
        std::lock_guard<std::mutex> guard(entry.second->mutex);
        channel = entry.second->current;
      }
      if (channel && !channel->closed) {
        const int send_ret = send_dynamic_worker_config(*channel, arg);
        if (!send_ret) { ++delivered; }
        else if (!ret) { ret = send_ret; }
      }
    }
  }
  fprintf(stderr,
      "PROTOTYPE_NAMESPACE_WORKER_CONFIG_BROADCAST excluded=%llu delivered=%lld ret=%d\n",
      static_cast<unsigned long long>(excluded_namespace),
      static_cast<long long>(delivered), ret);
  return ret;
}
int send_system_package_ready(Channel &channel, const bool ready)
{
  Frame request('E');
  request.number(ready ? 1 : 0);
  return exchange(channel, std::move(request), nullptr,
                  [](Frame &) { return OB_INVALID_ARGUMENT; });
}
int broadcast_system_package_ready(const bool ready)
{
  if (worker_process) { return OB_NOT_SUPPORTED; }
  std::vector<std::shared_ptr<Child>> snapshot;
  {
    std::lock_guard<std::mutex> guard(children_mutex);
    for (const auto &entry : children) { snapshot.push_back(entry.second); }
  }
  int ret = OB_SUCCESS;
  int64_t delivered = 0;
  for (const auto &child : snapshot) {
    std::shared_ptr<Channel> channel;
    {
      std::lock_guard<std::mutex> guard(child->mutex);
      channel = child->current;
    }
    if (channel && !channel->closed) {
      const int send_ret = send_system_package_ready(*channel, ready);
      if (!send_ret) { ++delivered; }
      else if (!ret) { ret = send_ret; }
    }
  }
  fprintf(stderr,
      "PROTOTYPE_NAMESPACE_SYSTEM_PACKAGE_READY ready=%d delivered=%lld ret=%d\n",
      ready, static_cast<long long>(delivered), ret);
  return ret;
}
int ensure_channel(uint64_t ns, std::shared_ptr<Channel> &channel) {
  channel.reset();
  std::shared_ptr<Child> child;
  int ret = attach(ns, child);
  if (ret) { return ret; }
  {
    // Only worker activation is serialized; existing queries continue running.
    std::lock_guard<std::mutex> guard(child->mutex);
    if (child->current && !child->current->closed) {
      const int ping = exchange(*child->current, Frame('P'), nullptr,
          [](Frame &) { return OB_INVALID_ARGUMENT; });
      if (ping == OB_EAGAIN) { return ping; }
      if (ping) { child->current->fail(); }
    }
    if (!child->current || child->current->closed) {
      auto next = std::make_shared<Channel>(StorageSpaceHandle::namespace_space(ns));
      { std::lock_guard<std::mutex> lock(children_mutex); next->generation = ++next_generation; }
      next->handle = namespace_proto_spawn(ns, next->generation, &next->pid);
      Frame bootstrap('B');
      char logical_ip[OB_IP_STR_BUFF] = {};
      const auto &logical_address = ObServer::get_instance().get_self();
      if (!next->handle || !logical_address.ip_to_string(logical_ip, sizeof(logical_ip))) {
        next->fail(); return OB_CONNECT_ERROR;
      }
      bootstrap.number(logical_address.get_port());
      bootstrap.string(ObString::make_string(logical_ip));
      bootstrap.number(GCONF.namespace_sql_worker_memory_budget);
      bootstrap.number(GCONF.ssl_client_authentication);
      bootstrap.string(GCONF.sql_protocol_min_tls_version.str());
      bootstrap.string(GCONF.ob_ssl_invited_common_names.str());
      bootstrap.number(ATOMIC_LOAD(&GCTX.sys_package_ready_) ? 1 : 0);
      if (next->send(bootstrap)) { return OB_CONNECT_ERROR; }
      Frame ready;
      bool worker_ready = false;
      while (!worker_ready && !ret) {
        ret = next->handle ? next->receive(ready) : OB_CONNECT_ERROR;
        fprintf(stderr, "PROTOTYPE_NATIVE_BOOTSTRAP_FRAME ns=%llu ret=%d type=%c slot=%llu\n",
            (unsigned long long)ns, ret, ready.type(),
            (unsigned long long)ready.tag().slot);
        if (!ret && ready.type() == 'Y') {
          worker_ready = true;
        } else if (!ret && (ready.tag().slot & WORKER_REQUEST)) {
          ret = receive_direct_storage(*next, std::move(ready));
        } else if (!ret) {
          ret = OB_INVALID_ARGUMENT;
        }
      }
      if (ret || !worker_ready || ready.number() != ns) {
        next->fail(); return OB_CONNECT_ERROR;
      }
      const ObString client_endpoint = ready.string();
      if (!ready.consumed() || client_endpoint.empty()) {
        next->fail(); return OB_INVALID_ARGUMENT;
      }
      next->client_endpoint.assign(client_endpoint.ptr(), client_endpoint.length());
      if (namespace_proto_dispatch(next->handle, Channel::receive_frame, next.get())) {
        next->fail(); return OB_CONNECT_ERROR;
      }
      obcall::ObAdminSetConfigArg worker_config;
      if (OB_FAIL(remembered_dynamic_worker_config(worker_config))
          || OB_FAIL(send_dynamic_worker_config(*next, worker_config))) {
        next->fail(); return ret;
      }
      if (ns == 1 || std::getenv("SEEKDB_NAMESPACE_SQL_WORKER_DIRECT_PROBE")) {
        Frame health('H');
        health.number(ns == 1 && !GCTX.in_bootstrap_ ? 1 : 0);
        ret = exchange(*next, std::move(health), nullptr,
            [](Frame &) { return OB_INVALID_ARGUMENT; }, INT64_MAX);
        if (ret) { next->fail(); return ret; }
      }
      next->restart_on_failure = true;
      child->current = next;
      fprintf(stderr, "PROTOTYPE_V10_WORKER_READY ns=%llu generation=%llu pid=%u endpoint=%s\n",
          (unsigned long long)ns, (unsigned long long)next->generation,
          next->pid, next->client_endpoint.c_str());
    }
    channel = child->current;
  }
  return channel && !channel->closed ? OB_SUCCESS : OB_CONNECT_ERROR;
}
int stop_channel(uint64_t ns) {
  std::shared_ptr<Child> child;
  {
    std::lock_guard<std::mutex> guard(children_mutex);
    const auto it = children.find(ns);
    if (it == children.end()) { return OB_SUCCESS; }
    child = it->second;
  }
  std::shared_ptr<Channel> endpoint;
  {
    std::lock_guard<std::mutex> guard(child->mutex);
    endpoint = std::move(child->current);
  }
  if (endpoint) {
    endpoint->restart_on_failure = false;
    fprintf(stderr,
        "PROTOTYPE_NAMESPACE_ENDPOINT_STOP ns=%llu generation=%llu pid=%u endpoint=%s\n",
        (unsigned long long)ns, (unsigned long long)endpoint->generation,
        endpoint->pid, endpoint->client_endpoint.c_str());
    endpoint->fail();
  }
  return OB_SUCCESS;
}
int write_endpoint_registry(const ObSqlString &statement) {
  if (!GCTX.sql_proxy_) { return OB_NOT_INIT; }
  int64_t affected_rows = 0;
  return GCTX.sql_proxy_->write(statement.ptr(), affected_rows);
}
int publish_endpoint(uint64_t namespace_id, uint64_t generation,
                     uint32_t pid, const std::string &client_endpoint) {
  if (namespace_id == 0 || namespace_id >= (1ULL << 30)
      || generation == 0 || pid == 0 || client_endpoint.empty()) {
    return OB_INVALID_ARGUMENT;
  }
  ObSqlString statement;
  int ret = statement.assign_fmt(
      "INSERT INTO __fork_proto_meta.endpoints(namespace_id,generation,worker_pid,endpoint) "
      "VALUES(%lu,%lu,%u,'%s') ON DUPLICATE KEY UPDATE "
      "generation=VALUES(generation),worker_pid=VALUES(worker_pid),endpoint=VALUES(endpoint)",
      namespace_id, generation, pid, client_endpoint.c_str());
  return ret ? ret : write_endpoint_registry(statement);
}
int recover_channel(uint64_t namespace_id, uint64_t failed_generation) {
  std::shared_ptr<Child> child;
  int ret = attach(namespace_id, child);
  std::shared_ptr<Channel> endpoint;
  if (OB_SUCC(ret)) {
    std::lock_guard<std::mutex> guard(child->mutex);
    if (!child->current
        || (child->current->generation == failed_generation
            && !child->current->closed)) {
      return OB_SUCCESS;
    }
    if (child->current->generation != failed_generation) {
      // A lazy session open may have already respawned the worker. That path
      // cannot publish the endpoint because the GLOBAL registry write routes
      // through this namespace itself. Adopt the live replacement and publish
      // it below.
      if (child->current->closed
          || child->current->generation == child->published_generation) {
        return OB_SUCCESS;
      }
      endpoint = child->current;
    }
  }
  int64_t schema_version = OB_INVALID_VERSION;
  if (OB_SUCC(ret) && namespace_id > 1) {
    ret = NamespaceForkKernelPrototype::namespace_schema_version(
        namespace_id, schema_version);
  }
  if (OB_SUCC(ret) && !endpoint) { ret = ensure_channel(namespace_id, endpoint); }
  if (OB_SUCC(ret)) {
    ret = publish_endpoint(namespace_id, endpoint->generation,
        endpoint->pid, endpoint->client_endpoint);
    if (OB_SUCC(ret)) {
      std::lock_guard<std::mutex> guard(child->mutex);
      if (child->current == endpoint) {
        child->published_generation = endpoint->generation;
      }
    }
  }
  fprintf(stderr,
      "PROTOTYPE_NAMESPACE_ENDPOINT_RECOVERED ns=%llu failed_generation=%llu "
      "generation=%llu pid=%u endpoint=%s schema_version=%lld ret=%d\n",
      static_cast<unsigned long long>(namespace_id),
      static_cast<unsigned long long>(failed_generation),
      static_cast<unsigned long long>(endpoint ? endpoint->generation : 0),
      endpoint ? endpoint->pid : 0,
      endpoint ? endpoint->client_endpoint.c_str() : "",
      static_cast<long long>(schema_version), ret);
  return ret;
}
int remove_endpoint(uint64_t namespace_id) {
  if (namespace_id == 0 || namespace_id >= (1ULL << 30)) { return OB_INVALID_ARGUMENT; }
  ObSqlString statement;
  int ret = statement.assign_fmt(
      "DELETE FROM __fork_proto_meta.endpoints WHERE namespace_id=%lu", namespace_id);
  return ret ? ret : write_endpoint_registry(statement);
}
int reset_endpoint_registry() {
  ObSqlString statement;
  int ret = statement.assign("DELETE FROM __fork_proto_meta.endpoints");
  return ret ? ret : write_endpoint_registry(statement);
}
int activate_namespace(uint64_t namespace_id) {
  if (!worker_process || worker_namespace != 1 || namespace_id <= 1
      || namespace_id >= (1ULL << 30)) {
    return OB_NOT_SUPPORTED;
  }
  Frame request('C'), reply;
  request.number(3); request.number(namespace_id);
  int ret = worker_send(request);
  if (!ret) { ret = worker_read(reply); }
  if (!ret && reply.type() != 'c') { ret = OB_INVALID_ARGUMENT; }
  if (!ret) { ret = static_cast<int>(reply.number()); }
  const ObString endpoint = !ret ? reply.string() : ObString();
  const uint64_t generation = !ret ? reply.number() : 0;
  const uint64_t pid = !ret ? reply.number() : 0;
  if (!ret && (!reply.consumed() || endpoint.empty()
      || generation == 0 || pid == 0 || pid > UINT32_MAX)) {
    ret = OB_INVALID_ARGUMENT;
  }
  if (!ret) {
    ret = publish_endpoint(namespace_id, generation,
        static_cast<uint32_t>(pid),
        std::string(endpoint.ptr(), endpoint.length()));
  }
  fprintf(stderr,
      "PROTOTYPE_NAMESPACE_ENDPOINT_ACTIVATED ns=%llu generation=%llu pid=%llu endpoint=%.*s ret=%d\n",
      (unsigned long long)namespace_id, (unsigned long long)generation,
      (unsigned long long)pid, endpoint.length(), endpoint.ptr(), ret);
  return ret;
}
int deactivate_namespace(uint64_t namespace_id) {
  if (!worker_process || worker_namespace != 1 || namespace_id <= 1
      || namespace_id >= (1ULL << 30)) {
    return OB_NOT_SUPPORTED;
  }
  Frame request('C'), reply;
  request.number(4); request.number(namespace_id);
  int ret = worker_send(request);
  if (!ret) { ret = worker_read(reply); }
  if (!ret && reply.type() != 'c') { ret = OB_INVALID_ARGUMENT; }
  if (!ret) { ret = static_cast<int>(reply.number()); }
  if (!ret && !reply.consumed()) { ret = OB_INVALID_ARGUMENT; }
  if (!ret) { ret = remove_endpoint(namespace_id); }
  fprintf(stderr, "PROTOTYPE_NAMESPACE_ENDPOINT_DEACTIVATED ns=%llu ret=%d\n",
      (unsigned long long)namespace_id, ret);
  return ret;
}
int reconcile_namespace_workers() {
  if (worker_process || !GCTX.sql_proxy_) { return OB_NOT_SUPPORTED; }
  std::vector<uint64_t> namespaces;
  // In-process ns1 (ticket 05a) spawns no worker for the system namespace.
  if (!ns1_in_process()) { namespaces.push_back(1); }
  int ret = OB_SUCCESS;
  if (!ret) {
    ObMySQLProxy::MySQLResult result;
    sqlclient::ObMySQLResult *rows = nullptr;
    ret = GCTX.sql_proxy_->read(result,
        "SELECT namespace_id FROM __fork_proto_meta.namespaces "
        "WHERE namespace_id>1 AND state=0 ORDER BY namespace_id");
    if (!ret && !(rows = result.get_result())) { ret = OB_ERR_UNEXPECTED; }
    while (!ret) {
      ret = rows->next();
      if (ret == OB_ITER_END) { ret = OB_SUCCESS; break; }
      uint64_t namespace_id = 0;
      if (!ret) { ret = rows->get_uint(0L, namespace_id); }
      if (!ret && (namespace_id <= 1 || namespace_id >= (1ULL << 30))) {
        ret = OB_INVALID_ARGUMENT;
      }
      if (!ret) { namespaces.push_back(namespace_id); }
    }
  }
  if (!ret) { ret = reset_endpoint_registry(); }
  for (uint64_t namespace_id : namespaces) {
    std::shared_ptr<Channel> endpoint;
    if (OB_SUCC(ret)) { ret = ensure_channel(namespace_id, endpoint); }
    if (OB_SUCC(ret)) {
      ret = publish_endpoint(namespace_id, endpoint->generation,
          endpoint->pid, endpoint->client_endpoint);
    }
    if (OB_SUCC(ret)) {
      fprintf(stderr,
          "PROTOTYPE_NAMESPACE_ENDPOINT_RECONCILED ns=%llu generation=%llu pid=%u endpoint=%s\n",
          (unsigned long long)namespace_id,
          (unsigned long long)endpoint->generation,
          endpoint->pid, endpoint->client_endpoint.c_str());
    }
  }
  return ret;
}
int open_session(uint64_t ns, sql::ObSQLSessionInfo &gateway, SessionBinding *&binding, bool internal) {
  binding = nullptr;
  // The shared process coordinates package installation through an internal
  // namespace-1 session.  Do not expose an external Worker session until that
  // DDL is complete: otherwise a process crash can make an unrelated user DDL
  // the first waiter on the recovering package transaction's runtime lock.
  if (!worker_process && !internal
      && !ATOMIC_LOAD(&GCTX.sys_package_ready_)) {
    return OB_EAGAIN;
  } else if (ns > 1) {
    int64_t schema_version = common::OB_INVALID_VERSION;
    const int namespace_ret = storage::NamespaceForkKernelPrototype::namespace_schema_version(
        ns, schema_version);
    if (namespace_ret != common::OB_SUCCESS || schema_version <= 0) {
      return namespace_ret != common::OB_SUCCESS
          ? namespace_ret : common::OB_STATE_NOT_MATCH;
    }
  }
  std::unique_ptr<SessionBinding> owned(new SessionBinding());
  int ret = ensure_channel(ns, owned->channel);
  if (ret) { return ret; }
  owned->internal = internal;
  if (internal) { owned->gateway = &gateway; }
  else if ((ret = share::server_service<sql::ObSQLSessionMgr>()->get_session(gateway.get_server_sid(), owned->gateway))) { return ret; }
  owned->writes = std::make_unique<EngineWrites>(owned->channel->storage_space, gateway);
  Frame request(internal ? 'a' : 'A'); request.number(gateway.get_server_sid());
  request.number(gateway.get_capability().capability_);
  if ((ret = append_session_state(gateway, request, !internal))) { return ret; }
  bool opened = false;
  // A newly spawned worker may still be constructing its native core schema
  // when the first client session arrives. Retry only the transient schema
  // visibility errors; protocol/authentication errors remain terminal.
  for (int attempt = 0; attempt < 8 && !opened; ++attempt) {
    Frame attempt_request = request;
    ret = exchange(*owned->channel, std::move(attempt_request), nullptr, [&](Frame &reply) {
      if (reply.type() != 'a' || opened) { return OB_INVALID_ARGUMENT; }
      owned->slot = reply.number(); owned->slot_generation = reply.number();
      opened = reply.consumed() && owned->slot_generation != 0;
      return opened ? OB_SUCCESS : OB_INVALID_ARGUMENT;
    });
    if (!opened && (ret == OB_TABLE_NOT_EXIST || ret == OB_EAGAIN)) {
      ob_usleep(10 * 1000);
    } else {
      break;
    }
  }
  if (!ret && !opened) { owned->channel->fail(); ret = OB_INVALID_ARGUMENT; }
  if (!ret) {
    std::lock_guard<std::mutex> guard(owned->channel->bindings_mutex);
    if (owned->channel->closed) { ret = OB_CONNECT_ERROR; }
    else {
      owned->next = owned->channel->bindings;
      if (owned->next) { owned->next->previous = owned.get(); }
      owned->channel->bindings = owned.get(); owned->linked = true;
      binding = owned.release();
    }
  }
  return ret;
}
void close_session(SessionBinding *binding) {
  std::unique_ptr<SessionBinding> owned(binding);
  if (!owned) { return; }
  if (owned->in_process) {
    // In-process (ticket 05c): mirror the direct-request 'v' close; engine
    // destruction rolls back an idle transaction and releases scans inline.
    InProcessStorage *ctx = owned->in_process;
    owned->in_process = nullptr;
    if (in_process_storage == ctx) { in_process_storage = nullptr; }
    if (ctx->initialized) {
      Frame request('v');
      in_process_send(*ctx, request, true);
    }
    delete ctx;
    return;
  }
  if (owned->direct_request) {
    auto *previous = worker_request;
    worker_request = owned->direct_request.get();
    worker_request->deadline = INT64_MAX;
    if (owned->gateway) { share::server_service<sql::ObSQLSessionMgr>()->disconnect_session(*owned->gateway); }
    Frame request('v'), reply;
    int ret = worker_send(request, true);
    if (!ret) { ret = worker_read(reply); }
    worker_storage_routes.release(worker_request->tag, true);
    worker_request = previous == owned->direct_request.get() ? nullptr : previous;
    return;
  }
  auto close = [&] {
    // Disconnect runs independently of the query task. Wait before destroying
    // either its bound transaction or the binding borrowed by that task.
    if (!owned->channel->closed) {
      Frame request('C'); request.number(owned->slot); request.number(owned->slot_generation);
      const int ret = exchange(*owned->channel, request, nullptr,
          [](Frame &) { return OB_INVALID_ARGUMENT; });
      if (ret) { owned->channel->fail(); }
    }
    owned->writes.reset();
  };
  if (owned->internal) { close(); } // The native inner connection already owns this lock.
  else { sql::ObSQLSessionInfo::LockGuard lock(owned->gateway->get_query_lock()); close(); }
}
void stop_all() {
  std::map<uint64_t, std::shared_ptr<Child>> detached;
  { std::lock_guard<std::mutex> guard(children_mutex); detached.swap(children); }
  for (auto &entry : detached) {
    std::lock_guard<std::mutex> guard(entry.second->mutex);
    if (entry.second->current) {
      entry.second->current->restart_on_failure = false;
      entry.second->current->fail();
    }
  }
}
int worker_send_wire(Frame frame) {
  return frame.ret ? frame.ret : namespace_proto_worker_write(frame.data.data(), frame.data.size()) == 0
      ? OB_SUCCESS : OB_CONNECT_ERROR;
}
int worker_read_wire(Frame &frame) {
  frame = Frame();
  const int ret = namespace_proto_worker_read([](void *context, const char *data, size_t size) {
    auto &frame = *static_cast<Frame *>(context);
    frame.data.assign(data, data + size);
  }, &frame);
  return ret ? OB_CONNECT_ERROR : frame.data.size() >= Frame::HEADER_SIZE ? OB_SUCCESS : OB_INVALID_ARGUMENT;
}
int worker_send(const Frame &frame, bool cleanup) {
  if (frame.ret) { return frame.ret; }
  if (in_process_storage) {
    return in_process_send(*in_process_storage, frame, cleanup);
  }
  if (!worker_request) {
    return is_storage_request(frame.type()) ? OB_ERR_UNEXPECTED : worker_send_wire(frame);
  }
  const int ret = worker_request->take_credit(cleanup);
  if (ret) { return ret; }
  Frame output = frame;
  if (is_storage_request(frame.type()) || (worker_request->tag.slot & WORKER_REQUEST)) {
    // Request context follows each storage call, across both routing directions.
    // Connection routes survive commands and must not retain old query context.
    output = Frame(frame.type(), frame.limit);
    // Native inner SQL may create statement traces. Keep the initiating call's
    // identity on callbacks so cooperative execution cannot select unrelated SQL.
    output.append(worker_call_trace ? *worker_call_trace : *ObCurTraceId::get_trace_id());
    if (worker_request->tag.slot & WORKER_REQUEST) {
      output.number(THIS_WORKER.get_timeout_ts());
    }
    output.data.insert(output.data.end(), frame.data.begin() + Frame::HEADER_SIZE, frame.data.end());
    if (output.data.size() > output.limit) { output.ret = OB_SIZE_OVERFLOW; }
  }
  output.tag(worker_request->tag);
  return worker_send_wire(std::move(output));
}
int worker_read(Frame &frame) {
  // Once an RPC is sent, consume its reply even after cancellation so cleanup
  // cannot mistake an earlier operation's reply for its own.
  if (in_process_storage) { return in_process_read(*in_process_storage, frame); }
  if (!worker_request) { return OB_ERR_UNEXPECTED; }
  if (worker_bootstrapping) {
    int ret = OB_SUCCESS;
    while (!ret) {
      Frame incoming;
      if ((ret = worker_read_wire(incoming))) { break; }
      if (incoming.tag().slot != worker_request->tag.slot
          || incoming.tag().generation != worker_request->tag.generation) {
        ret = OB_INVALID_ARGUMENT;
      } else {
        const char incoming_type = incoming.type();
        ret = worker_request->post(std::move(incoming));
        if (!ret && incoming_type != 'K') {
          ret = worker_request->take(frame, true);
          break;
        }
      }
    }
    return ret;
  }
  return worker_request->take(frame, true);
}
int begin_direct_request(uint32_t sid, SessionBinding *&binding, bool internal) {
  if (!worker_process) { return OB_SUCCESS; }
  if (binding) {
    if (!binding->direct_request) { return OB_INVALID_ARGUMENT; }
    worker_request = binding->direct_request.get();
    worker_request->cancelled = OB_SUCCESS;
    worker_request->deadline = INT64_MAX;
    return OB_SUCCESS;
  }
  auto owner = std::make_unique<SessionBinding>();
  owner->direct_request = worker_storage_routes.allocate(false);
  if (!owner->direct_request) { return OB_EAGAIN; }
  worker_request = owner->direct_request.get();
  Frame request('L'), reply; request.number(sid); request.number(internal);
  int ret = worker_send(request);
  if (!ret) { ret = worker_read(reply); }
  if (!ret) { ret = reply.type() == 'l' ? static_cast<int>(reply.number()) : OB_INVALID_ARGUMENT; }
  if (!ret && !reply.consumed()) { ret = OB_INVALID_ARGUMENT; }
  if (ret) {
    worker_storage_routes.release(worker_request->tag, true);
    worker_request = nullptr;
  } else { binding = owner.release(); }
  return ret;
}
int finish_direct_request() {
  if (!worker_request || !(worker_request->tag.slot & WORKER_REQUEST)) { return OB_SUCCESS; }
  Frame request('e'), reply;
  int ret = worker_send(request, true);
  if (!ret) { ret = worker_read(reply); }
  if (!ret) { ret = reply.type() == 'l' ? static_cast<int>(reply.number()) : OB_INVALID_ARGUMENT; }
  if (!ret && !reply.consumed()) { ret = OB_INVALID_ARGUMENT; }
  worker_request = nullptr;
  return ret;
}
int bind_direct_session(SessionBinding *binding, sql::ObSQLSessionInfo &session) {
  if (!worker_process || !binding || !binding->direct_request) { return OB_SUCCESS; }
  if (binding->gateway) { share::server_service<sql::ObSQLSessionMgr>()->revert_session(binding->gateway); binding->gateway = nullptr; }
  binding->direct_request->sql_session = &session;
  return share::server_service<sql::ObSQLSessionMgr>()->get_session(session.get_server_sid(), binding->gateway);
}
// Create (or rebind) the in-process storage context for a session served in
// the shared process (ticket 05c). Mirrors the worker-mode begin_direct_request
// path: one storage context per SQL session, keyed on the session's binding.
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
  owned->internal = true;
  owned->in_process = new (std::nothrow) InProcessStorage(ns);
  int ret = owned->in_process == nullptr ? OB_ALLOCATE_MEMORY_FAILED : OB_SUCCESS;
  if (!ret) { ret = in_process_open(*owned->in_process, session.get_server_sid(), true); }
  if (!ret) {
    slot = owned.release();
    in_process_storage = slot->in_process;
  }
  return ret;
}
StorageSessionScope::StorageSessionScope(sql::ObSQLSessionInfo *session, bool create)
    : previous_(worker_request) {
  if (worker_process && worker_namespace && session
      && (!worker_request || worker_request->sql_session != session)
      && (create || session->namespace_storage_binding())) {
    switched_ = true;
    const bool created = !session->namespace_storage_binding();
    error_ = begin_direct_request(session->get_server_sid(), session->namespace_storage_binding(), true);
    if (!error_) { worker_request->sql_session = session; }
    if (!error_ && created && session->get_tx_desc() && session->get_tx_desc()->is_shadow()) {
      // Import the native transaction execution copy once for this PX session.
      // The shared service gives it native shadow ownership, never a new tx.
      Frame request, reply; request.append(*session->get_tx_desc());
      error_ = tx_rpc('t', *session->get_tx_desc(), request, reply);
      if (!error_ && !reply.consumed()) { error_ = OB_INVALID_ARGUMENT; }
      if (error_) {
        close_session(session->namespace_storage_binding());
        session->namespace_storage_binding() = nullptr;
      }
    }
  } else if (!worker_process && session && in_process_session_ns(session) > 1
      && (!in_process_storage || in_process_storage->sql_session != session)
      && (create || session->namespace_storage_binding())) {
    // In-process serving of a forked namespace (ticket 05c): bind the
    // session's storage context for the duration of the storage call.
    switched_ = true;
    previous_in_process_ = in_process_storage;
    const bool created = !session->namespace_storage_binding();
    error_ = open_in_process_storage(*session);
    if (!error_) { in_process_storage->sql_session = session; }
    if (!error_ && created && session->get_tx_desc() && session->get_tx_desc()->is_shadow()) {
      // Same shadow-transaction import as the worker-mode branch above.
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
    worker_request = previous_;
    in_process_storage = previous_in_process_;
  }
}
void StorageSessionScope::close(SessionBinding *&binding) {
  if (binding) {
    if (binding->direct_request && previous_ == binding->direct_request.get()) { previous_ = nullptr; }
    if (binding->in_process && previous_in_process_ == binding->in_process) { previous_in_process_ = nullptr; }
    close_session(binding); binding = nullptr;
  }
}
IndependentStorageScope::IndependentStorageScope()
    : previous_(worker_request), previous_timeout_(THIS_WORKER.get_timeout_ts()) {
  if (worker_process && worker_namespace && !worker_request) {
    if (previous_timeout_ <= 0) { THIS_WORKER.set_timeout_ts(INT64_MAX); }
    error_ = begin_direct_request(0, binding_, true);
  } else if (!worker_process && forked_in_process() && in_process_serving_ns > 1
      && !in_process_storage) {
    // In-process background/native storage call (ticket 05c): a short-lived
    // internal context for the serving namespace, like the worker's route.
    if (previous_timeout_ <= 0) { THIS_WORKER.set_timeout_ts(INT64_MAX); }
    previous_in_process_ = in_process_storage;
    auto owned = std::make_unique<SessionBinding>();
    owned->internal = true;
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
  worker_request = previous_;
  THIS_WORKER.set_timeout_ts(previous_timeout_);
}
int fetch_catalog(char type, uint64_t id, const ObString &name, int64_t version, Frame &reply) {
  // Greeting construction and native background tasks read metadata before a
  // SQL session exists. Give those calls a short-lived independent route.
  SessionBinding *temporary = nullptr;
  const int64_t previous_timeout = THIS_WORKER.get_timeout_ts();
  int ret = OB_SUCCESS;
  if (!worker_request && !in_process_storage
      && forked_in_process() && serving_namespace() > 1) {
    // In-process (ticket 05c): the same short-lived internal route, delivered
    // synchronously to the serving namespace's storage.
    IndependentStorageScope scope;
    if (scope.error()) { return scope.error(); }
    Frame request(type); request.number(id); request.string(name); request.number(version);
    ret = worker_send(request);
    if (!ret) { ret = worker_read(reply); }
    if (!ret && reply.type() != 'c') { ret = OB_INVALID_ARGUMENT; }
    if (!ret) { ret = static_cast<int>(reply.number()); }
    THIS_WORKER.set_timeout_ts(previous_timeout);
    return ret ? ret : reply.ret;
  }
  if (!worker_request) {
    THIS_WORKER.set_timeout_ts(previous_timeout > 0 ? previous_timeout : INT64_MAX);
    ret = begin_direct_request(0, temporary, true);
  }
  Frame request(type); request.number(id); request.string(name); request.number(version);
  if (!ret) { ret = worker_send(request); }
  if (!ret) { ret = worker_read(reply); }
  if (!ret && reply.type() != 'c') { ret = OB_INVALID_ARGUMENT; }
  if (!ret) { ret = static_cast<int>(reply.number()); }
  close_session(temporary);
  THIS_WORKER.set_timeout_ts(previous_timeout);
  return ret ? ret : reply.ret;
}
int fetch_schema_version(bool published, bool core_version, int64_t &version) {
  Frame reply;
  int ret = fetch_catalog('k', core_version, published ? ObString::make_string("published") : ObString(),
      OB_INVALID_VERSION, reply);
  if (!ret) { version = static_cast<int64_t>(reply.number()); }
  if (worker_request_schema_version != OB_INVALID_VERSION) {
    fprintf(stderr,
        "PROTOTYPE_SCHEMA_VERSION_FETCH ns=%llu published=%d core=%d pin=%lld fetched=%lld ret=%d\n",
        (unsigned long long)worker_namespace, published, core_version,
        (long long)worker_request_schema_version, (long long)version, ret);
  }
  return ret ? ret : reply.consumed() ? OB_SUCCESS : OB_INVALID_ARGUMENT;
}
} } }
