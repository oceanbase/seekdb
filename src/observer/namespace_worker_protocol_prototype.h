// In-process prototype frame format pending typed service calls.
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
  static constexpr int64_t HEADER_SIZE = 1; // request type
  std::vector<char> data;
  int64_t pos = HEADER_SIZE;
  int ret = common::OB_SUCCESS;
  size_t limit = MAX_SQL_MESSAGE;
  explicit Frame(char type = '?', size_t max_size = MAX_SQL_MESSAGE) : data(HEADER_SIZE, 0), limit(max_size) { data[0] = type; }
  char type() const { return data.empty() ? '?' : data[0]; }
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
// The namespace this thread currently serves SQL for, published at
// command/query entry by InProcessServingScope.
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
  return in_process_serving_ns ? in_process_serving_ns
      : in_process_bound_namespace();
}
// True while this thread serves SQL for a forked namespace. Storage stubs
// use it to ship caller-resolved schema and to pick the
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
  return serving_namespace() <= 1;
}
// True while this thread serves SQL for a forked namespace whose schema it
// can resolve and ship.
inline bool serves_namespace_schema()
{
  return serves_forked_schema();
}
// Shared management code can move across runtime threads before it starts
// Native inner SQL uses the target namespace carried by its SQL client.
// Shared-process inner SQL normally bounces to the target namespace worker
// over IPC. With the ticket-05a gate, ns-1-bound inner SQL instead executes
// on the vanilla local path inside the shared process; ns>1 still bounces.
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
// Restore committed namespace names after the system package load completes.
int restore_namespace_registry();
// Publish the table-schema delta committed by a namespace-local DDL into the
// shared namespace directory before the SQL command is acknowledged.
int sync_namespace_schema_delta(uint64_t namespace_id,
                                share::schema::ObMultiVersionSchemaService &schema_service,
                                int64_t base_schema_version,
                                int64_t &published_schema_version);
int fetch_schema_version(bool published, bool core_version, int64_t &version);
struct SessionBinding;
// Native inner SQL can switch sessions while keeping the same execution stack.
// Keep its storage route with that session and restore the caller on return.
class StorageSessionScope {
public:
  explicit StorageSessionScope(sql::ObSQLSessionInfo *session, bool create = true);
  ~StorageSessionScope();
  int error() const { return error_; }
  void close(SessionBinding *&binding);
private:
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
void close_session(SessionBinding *binding);
// Thin TCP entry (worker mode): routes "user@branch" logins on the public
// MySQL port to the branch's worker Unix socket and byte-proxies from there.
namespace proxy {
int start();
void stop();
}
} } }
#endif
