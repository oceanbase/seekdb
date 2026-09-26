// In-process namespace storage binding and typed service helpers.
#ifndef SEEKDB_NAMESPACE_WORKER_PROTOCOL_PROTOTYPE_H_
#define SEEKDB_NAMESPACE_WORKER_PROTOCOL_PROTOTYPE_H_
#include "namespace/namespace.h"
#include "lib/ob_errno.h"
#include "lib/string/ob_string.h"
#include "common/object/ob_object.h"
#include "data_plane/access/ob_namespace_access_mode.h"
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
namespace oceanbase { namespace common { class ObMySQLProxy; } }
namespace oceanbase { namespace common { class ObMySQLTransaction; } }
namespace oceanbase { namespace common { class ObILobReadService; } }
namespace oceanbase { namespace common { class ObOptStatManager; } }
namespace oceanbase { namespace common { class ObOptStatMonitorManager; } }
namespace oceanbase { namespace data_plane { class ObIRangeService; } }
namespace oceanbase { namespace data_plane { class ObIDmlService; } }
namespace oceanbase { namespace data_plane { class ObIWriteContextService; } }
namespace oceanbase { namespace data_plane { class ObITransactionService; } }
namespace oceanbase { namespace obcall { struct ObAdminSetConfigArg; } }
namespace oceanbase { namespace query { class ObIRootCommandService; } }
namespace oceanbase { namespace rootserver { class ObIRootserverLocalRuntime; } }
namespace oceanbase { namespace common { namespace sqlclient { class ObISQLConnection; } } }
namespace oceanbase { namespace transaction { namespace tablelock { struct ObLockObjRequest; } } }
namespace oceanbase { namespace transaction { namespace tablelock { class ObIInnerConnectionLockRuntime; } } }
namespace oceanbase { namespace share { namespace schema { class ObPrivMgr; } } }
namespace oceanbase { namespace share { namespace schema { class ObMultiVersionSchemaService; } } }
namespace oceanbase { namespace share { namespace schema { class ObTableSchema; } } }
namespace oceanbase { namespace observer { namespace namespace_worker_prototype {
class INamespaceSchemaLifecycle
{
public:
  virtual ~INamespaceSchemaLifecycle() = default;
  virtual int refresh() = 0;
  virtual int fetch_version(bool published, bool core_version, int64_t &version) = 0;
  virtual int begin_change() = 0;
  virtual int finish_change(int64_t committed_schema_version) = 0;
  virtual int publish(share::schema::ObMultiVersionSchemaService &schema_service,
                      int64_t base_schema_version, int64_t &published_schema_version) = 0;
};
constexpr size_t MAX_FRAME = 256 * 1024;
constexpr size_t MAX_SQL_MESSAGE = 64 * 1024 * 1024;
struct RequestTag { uint64_t slot = 0, generation = 0; };
class StorageSpaceHandle final
{
public:
  enum class Scope : uint8_t { INVALID, NAMESPACE, GLOBAL, PHYSICAL_MDS };
  StorageSpaceHandle() = default;
  static StorageSpaceHandle namespace_space(uint64_t namespace_id)
  {
    return namespace_id > 0 && namespace_id < ns::NamespaceObjectKey::NAMESPACE_LIMIT
        ? StorageSpaceHandle(Scope::NAMESPACE, namespace_id) : StorageSpaceHandle();
  }
  static StorageSpaceHandle global_space()
  {
    return StorageSpaceHandle(Scope::GLOBAL, 0);
  }
  static StorageSpaceHandle physical_mds_space()
  {
    return StorageSpaceHandle(Scope::PHYSICAL_MDS, 0);
  }
  bool is_valid() const { return scope_ != Scope::INVALID; }
  bool is_namespace() const { return scope_ == Scope::NAMESPACE; }
  bool is_global() const { return scope_ == Scope::GLOBAL; }
  bool is_physical_mds() const { return scope_ == Scope::PHYSICAL_MDS; }
  uint64_t namespace_id() const { return is_namespace() ? value_ : 0; }
  uint64_t tablet_namespace_id() const { return is_global() ? 1 : namespace_id(); }
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
inline int storage_access_mode(StorageSpaceHandle space,
                               data_plane::ObNamespaceAccessMode &mode)
{
  mode = data_plane::ObNamespaceAccessMode::UNBOUND;
  if (space.is_global()) {
    mode = data_plane::ObNamespaceAccessMode::UNFENCED;
  } else if (space.is_namespace()) {
    ns::NamespaceRuntime *runtime = nullptr;
    if (!ns::namespace_registry().get(space.namespace_id(), runtime) || runtime == nullptr) {
      return common::OB_NOT_INIT;
    }
    mode = runtime->storage_access_lease_required()
        ? data_plane::ObNamespaceAccessMode::LEASED
        : data_plane::ObNamespaceAccessMode::UNFENCED;
  } else {
    return common::OB_INVALID_ARGUMENT;
  }
  return common::OB_SUCCESS;
}
// A narrow global scope lets a native SQL operation address shared control
// tablets in the same transaction without switching its SchemaService.
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
inline thread_local uint64_t worker_physical_mds_scope_depth = 0;
class PhysicalTabletMdsScope final
{
public:
  explicit PhysicalTabletMdsScope(bool active) : active_(active)
  { if (active_) { ++worker_physical_mds_scope_depth; } }
  ~PhysicalTabletMdsScope()
  { if (active_) { --worker_physical_mds_scope_depth; } }
  PhysicalTabletMdsScope(const PhysicalTabletMdsScope &) = delete;
  PhysicalTabletMdsScope &operator=(const PhysicalTabletMdsScope &) = delete;
private:
  bool active_;
};
inline bool uses_physical_tablet_mds_scope()
{
  return worker_physical_mds_scope_depth != 0;
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
inline bool has_global_control_authority()
{
  const uint64_t namespace_id = serving_namespace();
  if (namespace_id == 0) { return true; } // Bootstrap has no bound session.
  ns::NamespaceRuntime *runtime = nullptr;
  return ns::namespace_registry().get(namespace_id, runtime)
      && runtime != nullptr && runtime->has_global_control_authority();
}
// True while this thread serves SQL for a forked namespace. Storage adapters
// use it to carry caller-resolved schema and pick the namespace storage space.
inline bool serves_forked_schema()
{
  return !has_global_control_authority();
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
  return has_global_control_authority();
}
// True while this thread serves SQL for a forked namespace whose schema it
// can resolve and ship.
inline bool serves_namespace_schema()
{
  return serving_namespace() > 0;
}
// Native inner SQL uses the target namespace carried by its SQL client.
// The shared transaction service provides the clock for a fork snapshot.
int acquire_storage_snapshot(int64_t &snapshot);
// Refresh snapshot retention after committing a new acquired-snapshot row.
int reload_storage_freeze_info();
int build_tablet_write_defensive(const share::schema::ObTableSchema &schema,
                                 int64_t schema_version,
                                 common::ObMySQLTransaction &trans);
// Namespace DROP drains access leases before reclaiming physical tablets.
int drain_storage_namespace_access(uint64_t namespace_id);
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
  uint64_t previous_serving_ns_ = 0;
  bool serving_switched_ = false;
  bool switched_ = false;
  int error_ = common::OB_SUCCESS;
  StorageSessionScope(const StorageSessionScope &) = delete;
  StorageSessionScope &operator=(const StorageSessionScope &) = delete;
};
// Services such as the DDL scheduler run outside a client SQL request. Give
// one such call its own storage route without borrowing a user session.
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
// in_process_session_ns returns the namespace id for a bound session, or 0.
// ensure_in_process_namespace lazily constructs its schema service and plan
// cache. The effective_* helpers select the service for that session.
uint64_t in_process_session_ns(sql::ObSQLSessionInfo *session);
INamespaceSchemaLifecycle *namespace_schema_lifecycle(uint64_t namespace_id);
int refresh_session_schema(sql::ObSQLSessionInfo *session);
int begin_namespace_schema_change(uint64_t namespace_id);
int finish_namespace_schema_change(uint64_t namespace_id, int64_t committed_schema_version);
int publish_namespace_schema_change(uint64_t namespace_id,
    share::schema::ObMultiVersionSchemaService &schema_service,
    int64_t base_schema_version, int64_t &published_schema_version);
rootserver::ObIRootserverLocalRuntime *root_namespace_ddl_runtime();
void register_root_namespace_storage_services(ns::NamespaceRuntime &runtime);
query::ObIRootCommandService *effective_root_command_service(
    sql::ObSQLSessionInfo *session);
transaction::tablelock::ObIInnerConnectionLockRuntime *inprocess_lock_runtime(
    common::sqlclient::ObISQLConnection *conn);
int ensure_in_process_namespace(uint64_t namespace_id);
int prepare_namespace_login(ns::NamespaceRuntime &runtime);
int resolve_inprocess_tablet_schema(uint64_t physical_tablet_id,
    share::schema::ObMultiVersionSchemaService *&schema_service,
    uint64_t &logical_tablet_id);
int inprocess_refresh_schema(uint64_t namespace_id);
share::schema::ObMultiVersionSchemaService *namespace_schema_service(uint64_t namespace_id);
common::ObMySQLProxy *namespace_sql_proxy(uint64_t namespace_id);
common::ObITabletScan *effective_tablet_scan(sql::ObSQLSessionInfo *session);
common::ObILobReadService *effective_lob_read_service(sql::ObSQLSessionInfo *session);
data_plane::ObIRangeService *effective_range_service(sql::ObSQLSessionInfo *session);
data_plane::ObIDmlService *effective_dml_service(sql::ObSQLSessionInfo *session);
data_plane::ObIWriteContextService *effective_write_context_service(
    sql::ObSQLSessionInfo *session);
data_plane::ObITransactionService *effective_transaction_service(
    sql::ObSQLSessionInfo *session);
sql::ObPlanCache *effective_plan_cache(sql::ObSQLSessionInfo *session);
common::ObOptStatManager *effective_opt_stat_manager(sql::ObSQLSessionInfo *session);
common::ObOptStatMonitorManager *effective_opt_stat_monitor_manager(
    sql::ObSQLSessionInfo *session);
void stop_in_process_opt_stat_monitors();
void wait_in_process_opt_stat_monitors();
void close_session(SessionBinding *binding);
} } }
#endif
