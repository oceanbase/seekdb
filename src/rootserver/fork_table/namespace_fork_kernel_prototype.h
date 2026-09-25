// Throwaway kernel experiment: immutable catalog and directory, storage-triggered materialization.
#ifndef OCEANBASE_NAMESPACE_FORK_KERNEL_PROTOTYPE_H_
#define OCEANBASE_NAMESPACE_FORK_KERNEL_PROTOTYPE_H_
#include "share/schema/ob_table_schema.h"
#include "data_plane/access/ob_namespace_access_mode.h"
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
class NamespaceForkKernelPrototype final
{
public:
  static int ensure_control_schema();
  static int begin_namespace_drop(const common::ObString &name, uint64_t &id, bool &done);
  static int lock_namespace_drop(common::ObISQLClient &trans, uint64_t id,
                                 common::ObIArray<common::ObTabletID> &bound);
  static int finish_namespace_drop(common::ObISQLClient &trans, uint64_t id);
  static int check_table_access(uint64_t table_id, const common::ObTabletID &tablet_id,
                                bool read_only, data_plane::ObNamespaceAccessMode access_mode,
                                bool &held);
  static int check_baseline_access(const common::ObTabletID &tablet_id, bool &held);
  static void release_access(bool &held);
  static int drain_access();
  static int protect_snapshot_tablets(common::ObIArray<common::ObTabletID> &candidates, bool &need_retry);
  static int collect_dropped_namespace_tablets();
  static int control_namespace(const common::ObString &source, const common::ObString &target,
                                uint64_t &namespace_id);
  static int observe_database(common::ObISQLClient &trans, const share::schema::ObDatabaseSchema &schema);
  static int check_database_ddl(const share::schema::ObDatabaseSchema &schema,
                                const common::ObISQLClient *trans = nullptr);
  static bool is_encoded_id(uint64_t id);
  // Single source of truth for the namespace-scoped id encoding. Code outside
  // this file must never hand-roll the marker bit; use these instead of
  // `(1ULL << 62) | (ns << 37) | local` or `(id & ~(1ULL << 62)) >> 37`.
  static uint64_t encode_id(uint64_t namespace_id, uint64_t local_id);
  static int local_object_id(uint64_t namespace_id, uint64_t object_id, uint64_t &local_id);
  static int storage_object_id(uint64_t namespace_id, uint64_t object_id, uint64_t &storage_id);
  static int make_namespace_schema(uint64_t namespace_id,
                                   const share::schema::ObTableSchema &storage_schema,
                                   share::schema::ObTableSchema &namespace_schema);
  static int make_storage_schema(uint64_t namespace_id,
                                 const share::schema::ObTableSchema &logical_schema,
                                 share::schema::ObTableSchema &storage_schema);
  static int namespace_schema_version(uint64_t namespace_id, int64_t &schema_version);
  // Persistent lifecycle fence between namespace-local DDL and namespace fork.
  static int begin_schema_change(uint64_t namespace_id);
  static int finish_schema_change(uint64_t namespace_id, int64_t schema_version);
  static int begin_schema_recovery(uint64_t namespace_id, bool &needed);
  static int finish_schema_recovery(uint64_t namespace_id, int64_t schema_version);
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
  static int capture(common::ObISQLClient &trans, uint64_t source, uint64_t target,
                     int64_t snapshot, int64_t schema_version);
  static int table_id_for_tablet(const common::ObTabletID &tablet, int64_t schema_version,
                                 uint64_t &table_id);
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
};
}
}
#endif
