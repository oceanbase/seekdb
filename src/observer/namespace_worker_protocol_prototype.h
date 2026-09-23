// Throwaway V13 wire protocol. Q/U carry one absolute deadline; Z cancels its tag.
#ifndef SEEKDB_NAMESPACE_WORKER_PROTOCOL_PROTOTYPE_H_
#define SEEKDB_NAMESPACE_WORKER_PROTOCOL_PROTOTYPE_H_
#include "lib/ob_errno.h"
#include "lib/string/ob_string.h"
#include "common/object/ob_object.h"
#include <cstdint>
#include <cstdlib>
#include <cstring>
#include <functional>
#include <string>
#include <vector>
namespace oceanbase { namespace sql { class ObSQLSessionInfo; } }
namespace oceanbase { namespace sql { class ObBasicSessionInfo; } }
namespace oceanbase { namespace sql { class ObPlanCache; } }
namespace oceanbase { namespace common { class ObITabletScan; } }
namespace oceanbase { namespace common { class ObILobReadService; } }
namespace oceanbase { namespace data_plane { class ObIRangeService; } }
namespace oceanbase { namespace data_plane { class ObIDmlService; } }
namespace oceanbase { namespace data_plane { class ObIWriteContextService; } }
namespace oceanbase { namespace data_plane { class ObITransactionService; } }
namespace oceanbase { namespace obcall { struct ObAdminSetConfigArg; } }
namespace oceanbase { namespace query { class ObIRootCommandService; } }
namespace oceanbase { namespace common { namespace sqlclient { class ObISQLConnection; } } }
namespace oceanbase { namespace transaction { namespace tablelock { struct ObLockObjRequest; } } }
namespace oceanbase { namespace transaction { namespace tablelock { class ObIInnerConnectionLockRuntime; } } }
namespace oceanbase { namespace share { namespace schema { class ObPrivMgr; } } }
namespace oceanbase { namespace share { namespace schema { class ObMultiVersionSchemaService; } } }
namespace oceanbase { namespace observer { namespace namespace_worker_prototype {
constexpr size_t MAX_FRAME = 256 * 1024;
constexpr size_t MAX_SQL_MESSAGE = 64 * 1024 * 1024;
struct RequestTag { uint64_t slot = 0, generation = 0; };
constexpr uint64_t WORKER_REQUEST = 1ULL << 63;
class StorageSpaceHandle final
{
public:
  enum class Scope : uint8_t { INVALID, NAMESPACE, GLOBAL };
  StorageSpaceHandle() = default;
  static StorageSpaceHandle namespace_space(uint64_t namespace_id)
  {
    return namespace_id > 0 && namespace_id < (1ULL << 30)
        ? StorageSpaceHandle(Scope::NAMESPACE, namespace_id) : StorageSpaceHandle();
  }
  static StorageSpaceHandle global_space()
  {
    return StorageSpaceHandle(Scope::GLOBAL, 0);
  }
  bool is_valid() const { return scope_ != Scope::INVALID; }
  bool is_namespace() const { return scope_ == Scope::NAMESPACE; }
  bool is_global() const { return scope_ == Scope::GLOBAL; }
  uint64_t namespace_id() const { return is_namespace() ? value_ : 0; }
  bool operator==(const StorageSpaceHandle &other) const
  {
    return scope_ == other.scope_ && value_ == other.value_;
  }
  bool operator!=(const StorageSpaceHandle &other) const { return !(*this == other); }
private:
  StorageSpaceHandle(Scope scope, uint64_t value) : scope_(scope), value_(value) {}
  Scope scope_ = Scope::INVALID;
  uint64_t value_ = 0;
};
struct Frame {
  static constexpr int64_t HEADER_SIZE = 17; // type + request slot + generation
  std::vector<char> data;
  int64_t pos = HEADER_SIZE;
  int ret = common::OB_SUCCESS;
  size_t limit = MAX_SQL_MESSAGE;
  explicit Frame(char type = '?', size_t max_size = MAX_SQL_MESSAGE) : data(HEADER_SIZE, 0), limit(max_size) { data[0] = type; }
  char type() const { return data.empty() ? '?' : data[0]; }
  RequestTag tag() {
    if (data.size() < HEADER_SIZE) { ret = common::OB_INVALID_ARGUMENT; return {}; }
    const int64_t saved = pos; pos = 1;
    RequestTag result{number(), number()}; pos = saved; return result;
  }
  void tag(RequestTag route) {
    for (unsigned i = 0; i < 8; ++i) {
      data[1 + i] = static_cast<char>(route.slot >> (8 * i));
      data[9 + i] = static_cast<char>(route.generation >> (8 * i));
    }
  }
  void number(uint64_t n) {
    if (data.size() + 8 > limit) { ret = common::OB_SIZE_OVERFLOW; return; }
    for (unsigned i = 0; i < 8; ++i) { data.push_back(static_cast<char>(n >> (8 * i))); }
  }
  uint64_t number() {
    uint64_t n = 0;
    if (pos + 8 > static_cast<int64_t>(data.size())) { ret = common::OB_INVALID_ARGUMENT; return 0; }
    for (unsigned i = 0; i < 8; ++i) { n |= uint64_t(static_cast<unsigned char>(data[pos++])) << (8 * i); }
    return n;
  }
  void string(const common::ObString &s) {
    number(s.length());
    if (ret || s.length() < 0 || data.size() + s.length() > limit) { ret = common::OB_SIZE_OVERFLOW; return; }
    if (!s.empty()) { data.insert(data.end(), s.ptr(), s.ptr() + s.length()); }
  }
  common::ObString string() {
    const uint64_t n = number();
    if (ret || n > data.size() - pos) { ret = common::OB_INVALID_ARGUMENT; return {}; }
    common::ObString s(static_cast<int32_t>(n), data.data() + pos); pos += n; return s;
  }
  template<class T> void append(const T &value) {
    if (ret) { return; }
    const int64_t n = value.get_serialize_size();
    int64_t p = data.size();
    if (n < 0 || data.size() + n > limit) { ret = common::OB_SIZE_OVERFLOW; return; }
    data.resize(p + n); ret = value.serialize(data.data(), data.size(), p); data.resize(p);
  }
  template<class T> void read(T &value) {
    if (!ret) { ret = value.deserialize(data.data(), data.size(), pos); }
  }
  void write_object(const common::ObObj &value, const bool has_lob_header) {
    append(value);
    number(has_lob_header);
  }
  void write_object(const common::ObObj &value) {
    write_object(value, value.has_lob_header());
  }
  bool read_object(common::ObObj &value) {
    read(value);
    const bool has_lob_header = !ret && number() != 0;
    if (!ret && has_lob_header) { value.set_has_lob_header(); }
    return !ret && has_lob_header;
  }
  bool consumed() const { return !ret && pos == static_cast<int64_t>(data.size()); }
};
// Storage requests normally inherit the immutable space bound to their
// Channel. Only GLOBAL needs a wire discriminator; namespace_id is never
// repeated on ordinary requests. The shared endpoint treats GLOBAL as a
// capability available only to the default namespace Worker.
inline void write_storage_space(Frame &frame, const StorageSpaceHandle &space)
{
  if (!space.is_valid()) {
    frame.ret = common::OB_INVALID_ARGUMENT;
  } else {
    frame.number(space.is_global() ? 1 : 0);
  }
}
inline int read_storage_space(Frame &frame,
                              const StorageSpaceHandle &channel_space,
                              StorageSpaceHandle &request_space)
{
  const uint64_t wire_scope = frame.number();
  int ret = frame.ret;
  if (OB_SUCC(ret) && wire_scope == 0 && channel_space.is_namespace()) {
    request_space = channel_space;
  } else if (OB_SUCC(ret) && wire_scope == 1
             && channel_space.is_namespace()
             && channel_space.namespace_id() == 1) {
    request_space = StorageSpaceHandle::global_space();
  } else if (OB_SUCC(ret)) {
    ret = common::OB_INVALID_ARGUMENT;
  }
  return ret;
}
using CatalogFetch = int (*)(char, uint64_t, const common::ObString &, int64_t, Frame &);
inline CatalogFetch worker_catalog_fetch = nullptr;
inline uint64_t worker_namespace = 0;
inline bool worker_process = false;
inline bool worker_bootstrapping = false;
// Phase 1 transition gate (ticket 05a): with SEEKDB_NAMESPACE_NS1_IN_PROCESS
// set, the shared process executes namespace-1 SQL in process and the proxy
// dispatches those logins to its own local NIO endpoint instead of spawning a
// worker. ns>1 still resolves to worker processes. Removed in Phase 3
// together with the worker process mode.
inline bool ns1_in_process()
{
  static const bool enabled = std::getenv("SEEKDB_NAMESPACE_NS1_IN_PROCESS") != nullptr;
  return enabled && !worker_process;
}
// Phase 1c transition gate (ticket 05c): with SEEKDB_NAMESPACE_FORKED_IN_PROCESS
// set, forked namespaces (ns>1) are also served inside the shared process.
// The proxy dispatches their logins to the local NIO endpoint keeping the
// "@ns" suffix, sessions bind a per-ns runtime, and storage access crosses a
// direct in-process call instead of the worker IPC socket. Removed in Phase 3
// when every namespace is served in process unconditionally.
inline bool forked_in_process()
{
  static const bool enabled = std::getenv("SEEKDB_NAMESPACE_FORKED_IN_PROCESS") != nullptr;
  return enabled && !worker_process;
}
inline bool in_process_namespace_enabled(uint64_t namespace_id)
{
  return namespace_id == 1 ? ns1_in_process()
      : namespace_id > 1 && namespace_id < (1ULL << 30) && forked_in_process();
}
// Defined in namespace_worker_scan_prototype.ipp. Dumps and resets cumulative
// storage-frame exchange timing (count, send, wait per frame type).
void scan_exchange_stats_dump(FILE *out);
// Shared-process inner SQL is executed by this worker as well. While serving
// that bounced request, schema lookups must use the worker's local cache;
// fetching the version through IPC again would route the same SQL back here.
inline thread_local bool worker_inner_sql_execution = false;
// Shared storage can start periodic SQL callers before bootstrap has created
// every native system tablet.  Mark those bounced inner-SQL requests so the
// worker does not occupy an executor retrying a tablet that bootstrap itself
// still has to create.
inline thread_local bool worker_shared_bootstrap_request = false;
// A user statement pins one namespace schema version at admission. Native SQL
// may create many short-lived guards while resolving and optimizing that same
// statement; all of them must reuse the pinned version instead of asking the
// shared process for a newer version on every lookup.
inline thread_local int64_t worker_request_schema_version = common::OB_INVALID_VERSION;
// Storage scope is independent from the worker's fixed namespace identity.
// A narrow global scope lets a native SQL operation address shared control
// tablets in the same transaction without switching the worker SchemaService.
inline thread_local uint64_t worker_global_storage_scope_depth = 0;
class GlobalStorageScope final
{
public:
  GlobalStorageScope() { ++worker_global_storage_scope_depth; }
  ~GlobalStorageScope() { --worker_global_storage_scope_depth; }
  GlobalStorageScope(const GlobalStorageScope &) = delete;
  GlobalStorageScope &operator=(const GlobalStorageScope &) = delete;
};
inline bool uses_global_storage_scope()
{
  return worker_global_storage_scope_depth != 0;
}
// The namespace this thread currently serves SQL for. Worker mode: the
// process identity. In-process (ticket 05c): the bound session's namespace,
// published at command/query entry by InProcessServingScope.
struct InProcessStorage;
inline thread_local InProcessStorage *in_process_storage = nullptr;
inline thread_local uint64_t in_process_serving_ns = 0;
class InProcessServingScope final
{
public:
  explicit InProcessServingScope(uint64_t ns) : previous_(in_process_serving_ns)
  { in_process_serving_ns = ns; }
  ~InProcessServingScope() { in_process_serving_ns = previous_; }
  InProcessServingScope(const InProcessServingScope &) = delete;
  InProcessServingScope &operator=(const InProcessServingScope &) = delete;
private:
  uint64_t previous_;
};
uint64_t in_process_bound_namespace();
inline uint64_t serving_namespace()
{
  return worker_process ? worker_namespace
      : in_process_serving_ns ? in_process_serving_ns
      : in_process_bound_namespace();
}
// True while this thread serves SQL for a forked namespace, in either hosting
// mode. Storage stubs use it to ship caller-resolved schema and to pick the
// namespace storage space; schema refresh uses it for local-id translation.
inline bool serves_forked_schema()
{
  return serving_namespace() > 1;
}
inline StorageSpaceHandle active_worker_storage_space()
{
  return uses_global_storage_scope()
      ? StorageSpaceHandle::global_space()
      : StorageSpaceHandle::namespace_space(serving_namespace());
}
inline bool is_namespace_control_database(const common::ObString &name)
{
  return name.prefix_match("__fork_proto_meta");
}
inline bool can_access_namespace_control_database()
{
  return !worker_process || worker_namespace <= 1;
}
inline bool owns_namespace_schema()
{
  return worker_process && worker_namespace != 0;
}
// True while this thread serves SQL for a namespace whose schema it can
// resolve and ship: any Worker (ns>=1), or in-process serving of a forked
// namespace (ticket 05c). Plain shared-process threads return false.
inline bool serves_namespace_schema()
{
  return owns_namespace_schema() || serves_forked_schema();
}
inline bool uses_remote_schema()
{
  return worker_process && worker_namespace != 0 && !owns_namespace_schema();
}
// Shared management code can move across runtime threads before it starts
// native inner SQL. Bind namespace to the propagated call trace at the IPC
// boundary instead of deriving it from database/table IDs.
int bind_shared_inner_sql_namespace(uint64_t trace_seq, uint64_t namespace_id);
void unbind_shared_inner_sql_namespace(uint64_t trace_seq);
int push_inner_sql_namespace_override(uint64_t namespace_id);
bool has_inner_sql_namespace_override();
void pop_inner_sql_namespace_override();
uint64_t resolve_shared_inner_sql_namespace();
int check_sql_execution_role();
// Shared-process inner SQL normally bounces to the target namespace worker
// over IPC. With the ticket-05a gate, ns-1-bound inner SQL instead executes
// on the vanilla local path inside the shared process; ns>1 still bounces.
bool shared_inner_sql_bounces(sql::ObSQLSessionInfo &session);
// A worker owns the decision to create a fork snapshot, while the storage
// process owns the transaction clock used to produce its SCN.
int acquire_storage_snapshot(int64_t &snapshot);
// Snapshot retention is cached by the shared storage process. Refresh that
// cache after the worker commits a new acquired-snapshot row.
int reload_storage_freeze_info();
// Namespace access leases and compatibility schema holders live beside the
// shared storage engine. Namespace DROP must drain and reclaim them in that
// process rather than touching the control Worker's process-local copies.
int drain_storage_namespace_access(uint64_t namespace_id);
int release_storage_namespace_schemas(uint64_t namespace_id,
                                      int64_t &table_count,
                                      int64_t &database_count);
// A committed namespace owns a Worker endpoint.  The namespace-1 Worker asks
// the shared process manager to create that endpoint without routing a dummy
// SQL session through the compatibility listener.
int activate_namespace(uint64_t namespace_id);
// Remove the endpoint after namespace deletion has committed. Existing client
// sessions are disconnected through the same Channel failure path as crashes.
int deactivate_namespace(uint64_t namespace_id);
// Recreate endpoints for durable LIVE namespaces after the shared process has
// completed bootstrap. Sessions are intentionally not recovered.
int reconcile_namespace_workers();
// The package loader runs once in the shared process, while CALL resolution
// waits on process-local GCTX state. Publish the completed durable state to
// every live SQL Worker; workers spawned later receive it in their bootstrap.
int broadcast_system_package_ready(bool ready);
// Publish the table-schema delta committed by a namespace-local DDL into the
// shared namespace directory before the SQL command is acknowledged.
int sync_namespace_schema_delta(uint64_t namespace_id,
                                share::schema::ObMultiVersionSchemaService &schema_service,
                                int64_t base_schema_version,
                                int64_t &published_schema_version);
int begin_namespace_schema_change();
int finish_namespace_schema_change(int64_t committed_schema_version);
int begin_namespace_schema_recovery(bool &needed);
int finish_namespace_schema_recovery(int64_t reconciled_schema_version);
int fetch_schema_version(bool published, bool core_version, int64_t &version);
share::schema::ObPrivMgr *make_remote_priv_mgr(int64_t version);
int admin_set_config(obcall::ObAdminSetConfigArg &arg);
struct SessionBinding;
struct PendingRequest;
// Native inner SQL can switch sessions while keeping the same execution stack.
// Keep its storage route with that session and restore the caller on return.
class StorageSessionScope {
public:
  explicit StorageSessionScope(sql::ObSQLSessionInfo *session, bool create = true);
  ~StorageSessionScope();
  int error() const { return error_; }
  void close(SessionBinding *&binding);
private:
  PendingRequest *previous_;
  InProcessStorage *previous_in_process_ = nullptr;
  bool switched_ = false;
  int error_ = common::OB_SUCCESS;
  StorageSessionScope(const StorageSessionScope &) = delete;
  StorageSessionScope &operator=(const StorageSessionScope &) = delete;
};
// Native worker services such as the DDL scheduler run outside a client SQL
// request. Give one such call its own multiplexed storage route; it must not
// borrow a user session merely to reach the shared storage process.
class IndependentStorageScope final {
public:
  IndependentStorageScope();
  ~IndependentStorageScope();
  int error() const { return error_; }
private:
  SessionBinding *binding_ = nullptr;
  PendingRequest *previous_ = nullptr;
  InProcessStorage *previous_in_process_ = nullptr;
  int64_t previous_timeout_ = 0;
  int error_ = common::OB_SUCCESS;
  IndependentStorageScope(const IndependentStorageScope &) = delete;
  IndependentStorageScope &operator=(const IndependentStorageScope &) = delete;
};
// Ticket 05c (in-process forked namespaces). in_process_session_ns returns
// the served forked namespace id for a session bound to an in-process
// runtime, else 0. ensure_in_process_namespace lazily constructs the
// namespace's service group (schema service, plan cache) on first use.
// inprocess_refresh_schema mirrors the worker-mode per-command schema
// refresh. The effective_* helpers resolve the storage service a session's
// SQL must use: the in-process remote stub for forked-namespace sessions,
// the process-local implementation otherwise.
uint64_t in_process_session_ns(sql::ObSQLSessionInfo *session);
query::ObIRootCommandService *effective_root_command_service(
    sql::ObSQLSessionInfo *session, query::ObIRootCommandService *fallback);
transaction::tablelock::ObIInnerConnectionLockRuntime *inprocess_lock_runtime(
    common::sqlclient::ObISQLConnection *conn);
int ensure_in_process_namespace(uint64_t namespace_id);
int inprocess_refresh_schema(uint64_t namespace_id);
share::schema::ObMultiVersionSchemaService *namespace_schema_service(uint64_t namespace_id);
common::ObITabletScan *effective_tablet_scan(sql::ObSQLSessionInfo *session,
                                             common::ObITabletScan *fallback);
common::ObILobReadService *effective_lob_read_service(sql::ObSQLSessionInfo *session,
                                                      common::ObILobReadService *fallback);
data_plane::ObIRangeService *effective_range_service(sql::ObSQLSessionInfo *session,
                                                     data_plane::ObIRangeService *fallback);
data_plane::ObIDmlService *effective_dml_service(sql::ObSQLSessionInfo *session,
                                                 data_plane::ObIDmlService *fallback);
data_plane::ObIWriteContextService *effective_write_context_service(
    sql::ObSQLSessionInfo *session, data_plane::ObIWriteContextService *fallback);
data_plane::ObITransactionService *effective_transaction_service(
    sql::ObSQLSessionInfo *session, data_plane::ObITransactionService *fallback);
sql::ObPlanCache *effective_plan_cache(sql::ObSQLSessionInfo *session,
                                       sql::ObPlanCache *fallback);
int begin_direct_request(uint32_t sid, SessionBinding *&binding, bool internal = false);
int finish_direct_request();
int bind_direct_session(SessionBinding *binding, sql::ObSQLSessionInfo &session);
int open_session(uint64_t namespace_id, sql::ObSQLSessionInfo &gateway, SessionBinding *&binding, bool internal = false);
sql::ObSQLSessionInfo *bound_session(SessionBinding *binding);
int append_session_state(sql::ObSQLSessionInfo &session, Frame &frame, bool identity = false);
int apply_session_state(sql::ObSQLSessionInfo &session, Frame &frame);
void close_session(SessionBinding *binding);
void stop_all();
// Thin TCP entry (worker mode): routes "user@branch" logins on the public
// MySQL port to the branch's worker Unix socket and byte-proxies from there.
namespace proxy {
int start();
void stop();
}
} } }
#endif
