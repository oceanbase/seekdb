// Throwaway kernel experiment: immutable catalog and directory, storage-triggered materialization.
#ifndef OCEANBASE_NAMESPACE_FORK_KERNEL_PROTOTYPE_H_
#define OCEANBASE_NAMESPACE_FORK_KERNEL_PROTOTYPE_H_
#include "share/schema/ob_table_schema.h"
#include "data_plane/access/ob_namespace_access_mode.h"
#include "namespace/catalog.h"
#include <functional>
#include <unordered_set>
namespace oceanbase {
namespace common { class ObISQLClient; }
namespace ns { class TabletAccess; }
namespace share { class SCN; namespace schema {
class ObSimpleDatabaseSchema;
class ObMultiVersionSchemaService;
} }
namespace storage {
class ObTablet;
class NamespaceForkKernelPrototype;
// Holds all resources used by one operation, including resolved source tablets.
// Reset only after its native iterators and storage contexts have been released.
class TabletAccessProtection final {
public:
  TabletAccessProtection() = default;
  ~TabletAccessProtection();
  void reset();
private:
  friend class NamespaceForkKernelPrototype;
  std::unordered_set<uint64_t> namespaces_;
  std::unordered_set<uint64_t> tablets_;
  DISALLOW_COPY_AND_ASSIGN(TabletAccessProtection);
};
class NamespaceForkKernelPrototype final
{
public:
  static int ensure_control_schema(bool initial_install = false);
  // Called after replay is sealed, before publishing primary write admission.
  static void invalidate_replayed_metadata();
  static int begin_namespace_drop(const common::ObString &name, uint64_t &id, bool &done);
  static int finish_namespace_drop(uint64_t id);
  static int check_baseline_access(const common::ObTabletID &tablet_id,
                                   TabletAccessProtection &protection);
  static int drain_access(uint64_t namespace_id);
  static int protect_snapshot_tablets(common::ObIArray<common::ObTabletID> &candidates, bool &need_retry);
  // Keeps the dependency decision valid through physical reclamation.
  static int reclaim_unreferenced_tablets(common::ObIArray<common::ObTabletID> &candidates,
      bool &need_retry,
      const std::function<int(const common::ObIArray<common::ObTabletID> &)> &reclaim);
  static int collect_dropped_namespace_tablets();
  // Bounded primary-side creation from persisted sources. Existing physical
  // tablets subsequently complete takeover through the native scheduler.
  static int materialize_inherited_tablets();
  // Build from one readable cut without holding the publication fence during
  // tree/native traversal. Failure leaves the caller's previous plan intact.
  static int load_physical_retention(PhysicalSnapshotRetention &plan);
  static int control_namespace(const common::ObString &source, const common::ObString &target,
                                uint64_t &namespace_id, bool allow_login = true);
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
  // Capture before exposing a newly acquired read snapshot to its caller.
  // The handle protects both immutable directory pages and physical sources;
  // retain it until every operation using this view has finished.
  static int acquire_read_view(uint64_t namespace_id,
      const std::function<int(share::SCN &)> &acquire,
      ns::NamespaceCatalogViews::Handle &view,
      const ns::NamespaceCatalogViews::Handle &previous = {});
  static int is_tablet_owned(uint64_t namespace_id,
                             const common::ObTabletID &tablet_id,
                             bool &owned);
  static int owned_storage_tablets(
      uint64_t namespace_id,
      const common::ObIArray<common::ObTabletID> &logical_tablets,
      common::ObIArray<common::ObTabletID> &owned_tablets);
  static int table_id_for_tablet(const common::ObTabletID &tablet, int64_t schema_version,
                                 uint64_t &table_id);
  static int check_ddl(const share::schema::ObSimpleTableSchemaV2 &schema,
                       share::schema::ObMultiVersionSchemaService &schema_service,
                       const common::ObISQLClient *trans = nullptr);
  static int schedule_baseline(const ObTablet &tablet);
private:
  static int complete_initial_baseline(uint64_t namespace_id, int64_t deadline);
  static int check_initial_baseline(uint64_t namespace_id, int64_t deadline, bool &complete);
  static int materialize_source(uint64_t namespace_id, uint64_t table_id, uint64_t data_tablet_id);
  friend class ns::TabletAccess;
  friend class TabletAccessProtection;
  static int acquire_namespace(uint64_t id, TabletAccessProtection &protection);
  static int protect_tablet_sources(const common::ObTabletID &tablet,
                                    TabletAccessProtection &protection);
  static void release_access(TabletAccessProtection &protection);
  static int check_table_access(uint64_t table_id, const common::ObTabletID &tablet_id,
                                bool read_only, data_plane::ObNamespaceAccessMode access_mode);
  static int prepare_access(uint64_t namespace_id, uint64_t table_id,
      common::ObTabletID &tablet, bool read_only,
      data_plane::ObNamespaceAccessMode mode, TabletAccessProtection &protection,
      const std::function<int(common::ObTabletID &)> &prepare);
  // Read-path binding resolution without materialization. An encoded tablet
  // that exists locally resolves to itself; an unmaterialized inherited tablet
  // redirects to its bound source physical tablet, and cap_scn is the fork
  // snapshot the read must not exceed. Unencoded tablets pass through.
  static int resolve_read_tablet(const common::ObTabletID &tablet_id,
                                 common::ObTabletID &physical_tablet_id,
                                 int64_t &cap_scn,
                                 const ns::NamespaceCatalogViews::Handle &view);


  static int ensure_tablet(const common::ObTabletID &tablet_id);
  static int schedule_baseline_impl(const ObTablet &tablet, int depth);
};
}
}
#endif
