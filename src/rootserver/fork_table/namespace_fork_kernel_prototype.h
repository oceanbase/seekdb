// Throwaway kernel experiment: immutable catalog and directory, storage-triggered materialization.
#ifndef OCEANBASE_NAMESPACE_FORK_KERNEL_PROTOTYPE_H_
#define OCEANBASE_NAMESPACE_FORK_KERNEL_PROTOTYPE_H_
#include "share/schema/ob_table_schema.h"
namespace oceanbase {
namespace common { class ObISQLClient; }
namespace share { namespace schema { class ObSimpleDatabaseSchema; } }
namespace storage {
class ObTablet;
// A capability for one internal DROP transaction, never a current namespace.
class NamespaceSourceDropGuard final
{
public:
  explicit NamespaceSourceDropGuard(common::ObISQLClient &trans);
  ~NamespaceSourceDropGuard();
  bool is_valid() const { return valid_; }
private:
  bool valid_;
  NamespaceSourceDropGuard(const NamespaceSourceDropGuard &) = delete;
  NamespaceSourceDropGuard &operator=(const NamespaceSourceDropGuard &) = delete;
};
// Bounded adapter for the prototype's existing 64-bit storage/cache/lock keys.
// Namespace 1 owns the original engine objects; other namespaces keep the same local ids.
struct NamespaceObjectKey
{
  uint64_t namespace_id;
  uint64_t local_id;
  bool is_valid() const { return namespace_id > 0 && namespace_id < (1ULL << 30)
      && local_id > 0 && local_id < (1ULL << 32); }
  uint64_t storage_id() const { return namespace_id == 1 ? local_id
      : (1ULL << 62) | (namespace_id << 32) | local_id; }
};
class NamespaceForkKernelPrototype final
{
public:
  static int ensure_control_schema();
  static int begin_namespace_drop(const common::ObString &name, uint64_t &id, bool &done);
  static int lock_namespace_drop(common::ObISQLClient &trans, uint64_t id,
                                 common::ObIArray<common::ObTabletID> &bound);
  static int finish_namespace_drop(common::ObISQLClient &trans, uint64_t id);
  static int check_table_access(uint64_t table_id, const common::ObTabletID &tablet_id, bool &held);
  static int check_baseline_access(const common::ObTabletID &tablet_id, bool &held);
  static void release_access(bool &held);
  static int drain_access();
  static int protect_snapshot_tablets(common::ObIArray<common::ObTabletID> &candidates, bool &need_retry);
  static bool is_namespace_address(const common::ObString &name);
  static int parse_namespace_address(const common::ObString &address,
                                     uint64_t &namespace_id,
                                     common::ObString &database_name);
  static int control_namespace(const common::ObString &source, const common::ObString &target,
                                uint64_t &namespace_id);
  static int observe_database(common::ObISQLClient &trans, const share::schema::ObDatabaseSchema &schema);
  static int check_database_ddl(const share::schema::ObDatabaseSchema &schema,
                                const common::ObISQLClient *trans = nullptr);
  static int database_in_namespace(uint64_t namespace_id, const common::ObString &name,
                                   const share::schema::ObDatabaseSchema *&schema);
  static int database_by_address(const common::ObString &address,
                                 const share::schema::ObDatabaseSchema *&schema);
  static int database_by_id(uint64_t id, const share::schema::ObDatabaseSchema *&schema);
  static int database_by_id(uint64_t id, const share::schema::ObSimpleDatabaseSchema *&schema);
  static bool is_encoded_id(uint64_t id);
  // Single source of truth for the namespace-scoped id encoding. Code outside
  // this file must never hand-roll the marker bit; use these instead of
  // `(1ULL << 62) | (ns << 32) | local` or `(id & ~(1ULL << 62)) >> 32`.
  static uint64_t encode_id(uint64_t namespace_id, uint64_t local_id);
  // Owner namespace of an id: the encoded namespace, or 1 for a raw (unscoped)
  // id, which is only namespace 1's original engine objects.
  static uint64_t namespace_of(uint64_t id);
  static uint64_t current_namespace_id();
  static int local_object_id(uint64_t namespace_id, uint64_t object_id, uint64_t &local_id);
  static int storage_object_id(uint64_t namespace_id, uint64_t object_id, uint64_t &storage_id);
  static uint64_t encode_object(uint64_t database_id, uint64_t local_id);
  static int make_namespace_schema(uint64_t namespace_id,
                                   const share::schema::ObTableSchema &storage_schema,
                                   share::schema::ObTableSchema &namespace_schema);
  static int make_storage_schema(uint64_t namespace_id,
                                 const share::schema::ObTableSchema &logical_schema,
                                 share::schema::ObTableSchema &storage_schema);
  static int namespace_schema_version(uint64_t namespace_id, int64_t &schema_version);
  // Persistent lifecycle fence between namespace-local DDL and namespace
  // fork. The shared storage process invokes these operations; SQL Workers
  // only use the typed protocol declared in namespace_worker_protocol_prototype.h.
  static int begin_schema_change(uint64_t namespace_id);
  static int finish_schema_change(uint64_t namespace_id, int64_t schema_version);
  static int begin_schema_recovery(uint64_t namespace_id, bool &needed);
  static int finish_schema_recovery(uint64_t namespace_id, int64_t schema_version);
  static int begin_schema_changes(common::ObISQLClient &trans, uint64_t namespace_id);
  static int finish_schema_changes(common::ObISQLClient &trans,
                                   uint64_t namespace_id, int64_t committed_schema_version);
  static int observe_schema(common::ObISQLClient &trans, const share::schema::ObTableSchema &schema);
  static int observe_schemas(common::ObISQLClient &trans,
                             const common::ObIArray<share::schema::ObTableSchema> &schemas);
  static int flush_schema_changes(common::ObISQLClient &trans, bool commit);
  static int forget_schema(common::ObISQLClient &trans, const share::schema::ObTableSchema &schema,
                           int64_t schema_version, bool *private_tablet = nullptr);
  static int publish_schema_delta(
      uint64_t namespace_id,
      int64_t schema_version,
      const common::ObIArray<const share::schema::ObTableSchema *> &current_schemas,
      const common::ObIArray<const share::schema::ObTableSchema *> &previous_schemas);
  static int is_tablet_owned(uint64_t namespace_id,
                             const common::ObTabletID &tablet_id,
                             bool &owned);
  static int owned_storage_tablets(
      uint64_t namespace_id,
      const common::ObIArray<common::ObTabletID> &logical_tablets,
      common::ObIArray<common::ObTabletID> &owned_tablets);
  static void release_schema(uint64_t table_id);
  static int release_namespace_schemas(uint64_t namespace_id,
                                       int64_t &table_count,
                                       int64_t &database_count);
  static int capture(common::ObISQLClient &trans, uint64_t source, uint64_t target,
                     int64_t snapshot, int64_t schema_version);
  static int schema_by_name(uint64_t database, const common::ObString &name,
                            const share::schema::ObTableSchema *&schema);
  static int schema_by_id(uint64_t table_id, const share::schema::ObTableSchema *&schema);
  static int table_id_for_tablet(const common::ObTabletID &tablet, int64_t schema_version,
                                 uint64_t &table_id);
  static int list_schemas(uint64_t database, common::ObIArray<const share::schema::ObTableSchema *> &schemas);
  static int check_ddl(const share::schema::ObSimpleTableSchemaV2 &schema,
                       const common::ObISQLClient *trans = nullptr);
  static int ensure_tablet(const common::ObTabletID &tablet_id);
  // Read-path binding resolution without materialization. An encoded tablet
  // that exists locally resolves to itself; an unmaterialized inherited tablet
  // redirects to its bound source physical tablet, and cap_scn is the fork
  // snapshot the read must not exceed. Unencoded tablets pass through.
  static int resolve_read_tablet(const common::ObTabletID &tablet_id,
                                 common::ObTabletID &physical_tablet_id,
                                 int64_t &cap_scn);
  static int ensure_tablet(
      const common::ObTabletID &tablet_id,
      const share::schema::ObTableSchema &requested_schema,
      const common::ObIArray<const share::schema::ObTableSchema *> &binding_schemas);
  static int schedule_baseline(const ObTablet &tablet);
private:
  static int ensure_tablet_impl(
      const common::ObTabletID &tablet_id,
      const share::schema::ObTableSchema *requested_schema,
      const common::ObIArray<const share::schema::ObTableSchema *> *binding_schemas);
  static int observe_schema_in_namespace(common::ObISQLClient &trans,
                                         const share::schema::ObTableSchema &schema,
                                         uint64_t namespace_id);
  static int forget_schema_in_namespace(common::ObISQLClient &trans,
                                        const share::schema::ObTableSchema &schema,
                                        int64_t schema_version,
                                        uint64_t namespace_id,
                                        bool *private_tablet = nullptr,
                                        common::ObIArray<common::ObTabletID> *private_tablets = nullptr,
                                        const share::schema::ObTableSchema *replacement_schema = nullptr);
};
}
}
#endif
