// Namespace fork management and physical tablet materialization.
#define USING_LOG_PREFIX STORAGE
#include "query/session/ob_inner_sql_connection_access.h"
#include "rootserver/fork_table/namespace_fork_kernel_prototype.h"
#include "namespace/catalog.h"
#include "namespace/namespace.h"
#include "observer/namespace_worker_protocol_prototype.h"
#include "rootserver/ob_tablet_creator.h"
#include "rootserver/fork_table/table_creation_descriptor.h"
#include "rootserver/fork_table/namespace_schema_publication.h"
#include "rootserver/ob_tablet_drop.h"
#include "rootserver/ddl_task/ob_ddl_task_util.h"
#include "rootserver/fork_table/instance_namespace_metadata.h"
#include "common/mysqlclient/ob_mysql_proxy.h"
#include "common/mysqlclient/ob_mysql_transaction.h"
#include "share/ob_server_struct.h"
#include "share/ob_snapshot_table_proxy.h"
#include "share/ob_global_stat_proxy.h"
#include "share/ob_debug_sync.h"
#include "share/tablet/ob_tablet_mapping_operator.h"
#include "share/schema/ob_multi_version_schema_service.h"
#include "share/rc/ob_server_runtime.h"
#include "storage/compaction/ob_freeze_info_mgr.h"
#include "storage/compaction/ob_schedule_dag_func.h"
#include "storage/ddl/ob_tablet_fork_task.h"
#include "storage/ls/ob_ls.h"
#include "storage/ob_storage_schema.h"
#include "storage/ob_tablet_autoincrement_service.h"
#include "storage/tablet/ob_tablet_create_delete_helper.h"
#include "storage/tablelock/ob_lock_inner_connection_util.h"
#include "storage/tx_storage/ob_access_service.h"
#include "storage/tx_storage/ob_ls_service.h"
#include "lib/hash_func/murmur_hash.h"
#include "lib/time/ob_time_utility.h"
#include <algorithm>
#include <atomic>
#include <chrono>
#include <cstdlib>
#include <cstring>
#include <map>
#include <memory>
#include <mutex>
#include <shared_mutex>
#include <string>
#include <vector>
#include <unordered_map>
#include <unordered_set>

namespace oceanbase {
namespace storage {
using ::oceanbase::ns::NamespaceObjectKey;
using namespace common;
using namespace share;
using namespace share::schema;
using namespace transaction::tablelock;
namespace {
using Roots = ::oceanbase::ns::CatalogRoots;
ObMultiVersionSchemaService *directory_schema_service()
{
  return observer::namespace_worker_prototype::namespace_schema_service(1);
}
ObMySQLProxy *directory_sql_proxy()
{
  // Shared physical tablet DDL and the instance snapshot clock use the
  // explicitly selected Namespace 1 SQL context.
  return observer::namespace_worker_prototype::namespace_sql_proxy(1);
}
InstanceMetaStore *directory_kv_store()
{
  auto *access = share::server_service<ObAccessService>();
  return access == nullptr ? nullptr : &access->instance_meta_store();
}
int64_t directory_deadline()
{
  return THIS_WORKER.is_timeout_ts_valid()
      ? THIS_WORKER.get_timeout_ts()
      : ObTimeUtility::current_time() + 120 * 1000 * 1000;
}
// Entries exist only while an operation holds them. Logical admission and
// actual physical dependencies have distinct lifetimes: deleting a namespace
// drains its own work, while GC excludes any tablet still used by any reader.
std::mutex access_mutex;
std::unordered_map<uint64_t, int64_t> namespace_accesses;
std::unordered_map<uint64_t, int64_t> tablet_accesses;
std::mutex physical_reclamation_mutex;
int exclude_active_tablets(ObIArray<ObTabletID> &candidates, bool &need_retry)
{
  ObArray<ObTabletID> idle;
  int ret = OB_SUCCESS;
  std::lock_guard<std::mutex> lock(access_mutex);
  for (int64_t i = 0; OB_SUCC(ret) && i < candidates.count(); ++i) {
    if (tablet_accesses.count(candidates.at(i).id()) != 0) { need_retry = true; }
    else { ret = idle.push_back(candidates.at(i)); }
  }
  return ret ? ret : candidates.assign(idle);
}
// check_table_access runs on every storage scan/DML open; a roots() SQL per
// open dominated branch-worker cold schema refresh. The namespace registry is
// only mutated by begin/finish_namespace_drop in this process, so a LIVE entry
// stays valid until locally invalidated. Readers still register their namespace
// before consulting the cache, so drain_access keeps covering the close window.
std::shared_mutex namespace_state_mutex;
std::unordered_map<uint64_t, int64_t> namespace_state_cache;
bool cached_namespace_state(uint64_t id, int64_t &state) {
  // Replay does not run the primary's post-commit invalidation callbacks.
  if (share::server_is_recovery_mode()) { return false; }
  std::shared_lock<std::shared_mutex> lock(namespace_state_mutex);
  const auto it = namespace_state_cache.find(id);
  if (it == namespace_state_cache.end()) { return false; }
  state = it->second;
  return true;
}
void remember_namespace_state(uint64_t id, int64_t state) {
  if (share::server_is_recovery_mode()) { return; }
  std::unique_lock<std::shared_mutex> lock(namespace_state_mutex);
  namespace_state_cache[id] = state;
}
void invalidate_namespace_state(uint64_t id) {
  std::unique_lock<std::shared_mutex> lock(namespace_state_mutex);
  namespace_state_cache.erase(id);
}
// Encoded tablet -> table bindings are immutable (local ids are never reused),
// but tablet-stat refresh used to re-walk the directory per tablet per pass.
std::shared_mutex tablet_table_mutex;
std::unordered_map<uint64_t, uint64_t> tablet_table_cache;
void remember_tablet_table(uint64_t tablet, uint64_t table) {
  if (table == OB_INVALID_ID) { return; }
  std::unique_lock<std::shared_mutex> lock(tablet_table_mutex);
  if (tablet_table_cache.size() > (1u << 20)) { tablet_table_cache.clear(); }
  tablet_table_cache[tablet] = table;
}
// Physical reclamation excludes lineage/exception publication. Shared nesting
// allows materialization and DDL helpers to compose without a reader registry.
std::shared_timed_mutex metadata_mutex;
thread_local int metadata_depth = 0;
class MetadataReadGuard final {
public:
  MetadataReadGuard() : held_(false), ret_(OB_SUCCESS) {
    if (!metadata_depth) {
      while (!metadata_mutex.try_lock_shared_for(std::chrono::milliseconds(1))) {
        if (OB_SUCCESS != (ret_ = THIS_WORKER.check_status())) { return; }
      }
    }
    ++metadata_depth; held_ = true;
  }
  ~MetadataReadGuard() { if (held_ && --metadata_depth == 0) { metadata_mutex.unlock_shared(); } }
  int error() const { return ret_; }
private:
  bool held_;
  int ret_;
  MetadataReadGuard(const MetadataReadGuard &) = delete;
  MetadataReadGuard &operator=(const MetadataReadGuard &) = delete;
};
// Serialize the short resolve/register and dependency-publication windows with
// GC. Long-lived readers hold resource counts, not this mutex. Busy candidates
// are skipped; unrelated readers never stall this collector.
class PhysicalReclamationGuard final
{
public:
  PhysicalReclamationGuard()
      : serial_(physical_reclamation_mutex, std::try_to_lock), held_(false), ret_(OB_EAGAIN)
  {
    if (!serial_.owns_lock()) { return; }
    const auto deadline = std::chrono::steady_clock::now() + std::chrono::milliseconds(100);
    while (!metadata_mutex.try_lock_for(std::chrono::milliseconds(1))) {
      if (std::chrono::steady_clock::now() >= deadline) { return; }
    }
    held_ = true;
    ++metadata_depth;
    ret_ = OB_SUCCESS;
  }
  ~PhysicalReclamationGuard() { if (held_) { --metadata_depth; metadata_mutex.unlock(); } }
  int error() const { return ret_; }
private:
  std::unique_lock<std::mutex> serial_;
  bool held_;
  int ret_;
};
uint64_t encoded(uint64_t db, uint64_t local) {
  return NamespaceObjectKey{db, local}.storage_id();
}
uint64_t database_of(uint64_t id) { return NamespaceObjectKey::encoded_namespace(id); }
uint64_t local_of(uint64_t id) { return NamespaceObjectKey::local_part(id); }
// Exception-table model: a namespace row stores only its immutable parent link
// (parent_namespace, fork_cap), and the exceptions table records just the
// tablets this namespace physically owns or has explicitly dropped. Everything
// else is resolved by probing deterministic physical ids along the parent
// chain; namespace 1 terminates every walk with its encoded physical tablet ids.
// Parent links never change after fork commit and namespace ids are never
// reused; the cache entry is removed when its tombstone is pruned.
ns::NamespaceControlState &control_state() {
  return ns::namespace_registry().control_state();
}
class KVExceptionLoader final : public ns::IExceptionLoader
{
public:
  explicit KVExceptionLoader(InstanceMetaStore &store) : store_(store) {}
  int load(uint64_t namespace_id, IRowSink &sink) override
  {
    InstanceMetaStore::Transaction tx;
    int ret = store_.begin(tx, directory_deadline(), true);
    if (ret == OB_SUCCESS) {
      rootserver::InstanceNamespaceMetadata metadata(store_, tx);
      rootserver::InstanceExceptionLoader loader(metadata);
      ret = loader.load(namespace_id, sink);
    }
    if (tx.is_active()) {
      const int end_ret = ret == OB_SUCCESS ? store_.commit(tx) : store_.rollback(tx);
      if (ret == OB_SUCCESS) { ret = end_ret; }
    }
    return ret;
  }
private:
  InstanceMetaStore &store_;
};
int load_kv_exceptions(uint64_t namespace_id)
{
  auto *store = directory_kv_store();
  if (store == nullptr) { return OB_NOT_INIT; }
  KVExceptionLoader loader(*store);
  return control_state().load_exceptions(namespace_id, loader,
      share::server_is_recovery_mode());
}
int load_kv_namespace_state(uint64_t namespace_id, int64_t &state)
{
  auto *store = directory_kv_store();
  if (store == nullptr) { return OB_NOT_INIT; }
  rootserver::InstanceNamespaceDirectory directory(*store);
  rootserver::InstanceNamespaceRecord record;
  const int ret = directory.get(namespace_id, directory_deadline(), record);
  if (ret == OB_SUCCESS) { state = record.roots.state; }
  return ret;
}
// Following replicas cannot rely on locally invalidated ownership caches.
// A single-tablet lookup must stay a point read, even for partitioned tables.
int load_kv_tablet_ownership(uint64_t namespace_id, uint64_t local_tablet,
    bool &owned, uint64_t &table_id, int64_t *namespace_state = nullptr)
{
  owned = false;
  table_id = OB_INVALID_ID;
  auto *store = directory_kv_store();
  if (store == nullptr) { return OB_NOT_INIT; }
  InstanceMetaStore::Transaction tx;
  int ret = store->begin(tx, directory_deadline(), true);
  if (OB_SUCC(ret)) {
    rootserver::InstanceNamespaceMetadata metadata(*store, tx);
    if (namespace_state != nullptr) {
      rootserver::InstanceNamespaceRecord record;
      if (OB_FAIL(metadata.get_namespace(namespace_id, record))) {
      } else {
        *namespace_state = record.roots.state;
      }
    }
    rootserver::InstanceExceptionRecord exception;
    if (OB_SUCC(ret)) {
      ret = metadata.get_exception(namespace_id, local_tablet, exception);
      if (ret == OB_ENTRY_NOT_EXIST) { ret = OB_SUCCESS; }
      else if (OB_SUCC(ret) && exception.kind == 0) {
        owned = true;
        table_id = exception.table_id;
      }
    }
    const int cleanup = store->rollback(tx);
    if (OB_SUCC(ret)) { ret = cleanup; }
  }
  return ret;
}
// Committed physical presence in the tablet manager. Uncommitted creations do
// not count, so a reader racing a materialization simply falls through to the
// ancestor copy, which holds the same data at its fork cap.
int probe_physical_tablet(uint64_t id, bool &exists) {
  exists = false;
  ObTabletHandle handle;
  const int ret = ObTabletCreateDeleteHelper::check_and_get_tablet(
      ObTabletMapKey(ObTabletID(id)), handle, 0,
      ObMDSGetTabletMode::READ_READABLE_COMMITED,
      transaction::ObTransVersion::MAX_TRANS_VERSION);
  if (ret == OB_SUCCESS) { exists = true; return OB_SUCCESS; }
  return ret == OB_TABLET_NOT_EXIST || ret == OB_ENTRY_NOT_EXIST || ret == OB_EAGAIN
      ? OB_SUCCESS : ret;
}
// Absence and an unavailable historical copy are different results. In
// particular an empty shell cannot justify choosing a more distant ancestor.
int probe_historical_tablet(uint64_t id, int64_t cap, rootserver::TabletVisibility &visibility)
{
  visibility = rootserver::TabletVisibility::ABSENT;
  ObTabletHandle handle;
  int ret = ObTabletCreateDeleteHelper::check_and_get_tablet(
      ObTabletMapKey(ObTabletID(id)), handle, 0, ObMDSGetTabletMode::READ_WITHOUT_CHECK,
      transaction::ObTransVersion::MAX_TRANS_VERSION);
  if (ret == OB_TABLET_NOT_EXIST || ret == OB_ENTRY_NOT_EXIST) { return OB_SUCCESS; }
  if (ret != OB_SUCCESS) { return ret; }
  if (handle.get_obj()->is_empty_shell()) { return OB_SNAPSHOT_DISCARDED; }
  const int64_t snapshot = cap > 0 ? cap : transaction::ObTransVersion::MAX_TRANS_VERSION;
  ret = handle.get_obj()->check_new_mds_with_cache(snapshot);
  if (ret == OB_SUCCESS) { visibility = rootserver::TabletVisibility::READABLE; return OB_SUCCESS; }
  if (ret == OB_TABLET_NOT_EXIST) {
    visibility = rootserver::TabletVisibility::OUTSIDE_SNAPSHOT;
    return OB_SUCCESS;
  }
  if (ret == OB_SNAPSHOT_DISCARDED || ret == OB_EAGAIN) {
    ObTabletCreateDeleteMdsUserData data;
    mds::MdsWriter writer;
    mds::TwoPhaseCommitState state;
    SCN version;
    const int status_ret = handle.get_obj()->get_latest(data, writer, state, version);
    if (status_ret != OB_SUCCESS) { return status_ret; }
    if (state != mds::TwoPhaseCommitState::ON_COMMIT
        && (data.data_type_ == ObTabletMdsUserDataType::CREATE_TABLET
            || data.data_type_ == ObTabletMdsUserDataType::PROTOTYPE_MATERIALIZE_TABLET)) {
      // An allocated copy is not part of any committed read view yet. Its
      // logical birth may already be the fork SCN, so birth alone cannot
      // establish physical visibility. Continue to the inherited source;
      // an owned row from the committing transaction still selects this copy
      // and lets the native read wait for its commit callbacks.
      visibility = rootserver::TabletVisibility::OUTSIDE_SNAPSHOT;
      return OB_SUCCESS;
    }
    if (ret == OB_EAGAIN && state == mds::TwoPhaseCommitState::ON_COMMIT) {
      // The commit may have published since the first status observation.
      // Validate this committed value instead of returning the stale EAGAIN.
      ret = data.tablet_status_ == ObTabletStatus::NORMAL
          ? ObTabletCreateDeleteHelper::check_read_snapshot_for_normal(
                *handle.get_obj(), snapshot, data, writer, state, version)
          : ObTabletCreateDeleteHelper::check_read_snapshot_for_deleted(
                *handle.get_obj(), snapshot, data, writer, state, version);
      if (ret == OB_SUCCESS) {
        visibility = rootserver::TabletVisibility::READABLE;
        return OB_SUCCESS;
      }
      if (ret == OB_TABLET_NOT_EXIST) {
        visibility = rootserver::TabletVisibility::OUTSIDE_SNAPSHOT;
        return OB_SUCCESS;
      }
    }
    if (data.create_commit_version_ != transaction::ObTransVersion::INVALID_TRANS_VERSION
        && snapshot < data.create_commit_version_) {
      visibility = rootserver::TabletVisibility::OUTSIDE_SNAPSHOT;
      return OB_SUCCESS;
    }
  }
  return ret;
}
int physical_tablet_birth(uint64_t id, int64_t &birth)
{
  birth = 0;
  ObTabletHandle handle;
  int ret = ObTabletCreateDeleteHelper::check_and_get_tablet(
      ObTabletMapKey(ObTabletID(id)), handle, 0, ObMDSGetTabletMode::READ_WITHOUT_CHECK,
      transaction::ObTransVersion::MAX_TRANS_VERSION);
  if (ret != OB_SUCCESS) { return ret; }
  ObTabletCreateDeleteMdsUserData data;
  mds::MdsWriter writer;
  mds::TwoPhaseCommitState state;
  SCN version;
  ret = handle.get_obj()->get_latest(data, writer, state, version);
  if (ret == OB_SUCCESS) { birth = data.create_commit_version_; }
  return ret;
}
int schema_tablet_ids(const ObTableSchema &schema,
                      ObIArray<ObTabletID> &tablet_ids)
{
  int ret = schema.get_tablet_ids(tablet_ids);
  if (OB_SUCC(ret) && schema.get_hidden_partition_num() > 0) {
    ret = schema.get_first_level_hidden_tablet_ids(tablet_ids);
  }
  return ret;
}

bool directory_supported(const ObTableSchema &s) {
  if (!s.is_sys_table() && !s.is_aux_lob_table()
      && !s.is_index_table() && !s.is_user_table()) {
    return false;
  }
  if ((s.is_index_table() || s.is_aux_lob_table())
      && (s.get_data_table_id() == 0 || s.get_data_table_id() == OB_INVALID_ID)) {
    return false;
  }
  ObArray<ObTabletID> tablets;
  if (schema_tablet_ids(s, tablets) != OB_SUCCESS || tablets.empty()) {
    return false;
  }
  std::vector<uint64_t> ids;
  ids.reserve(tablets.count());
  for (int64_t i = 0; i < tablets.count(); ++i) {
    if (!tablets.at(i).is_valid()) { return false; }
    ids.push_back(tablets.at(i).id());
  }
  std::sort(ids.begin(), ids.end());
  return std::adjacent_find(ids.begin(), ids.end()) == ids.end();
}

template <typename Mapper>
int rewrite_tablet_ids(ObTableSchema &schema, Mapper mapper)
{
  int ret = OB_SUCCESS;
  auto rewrite = [&](ObTabletID tablet, auto setter) {
    uint64_t id = OB_INVALID_ID;
    int rewrite_ret = tablet.is_valid() ? mapper(tablet.id(), id) : OB_INVALID_ARGUMENT;
    if (rewrite_ret == OB_SUCCESS) { setter(ObTabletID(id)); }
    return rewrite_ret;
  };
  const ObPartitionLevel level = schema.get_part_level();
  if (level == PARTITION_LEVEL_ZERO) {
    ret = rewrite(schema.get_tablet_id(),
        [&](ObTabletID id) { schema.set_tablet_id(id); });
  } else if (level == PARTITION_LEVEL_ONE || level == PARTITION_LEVEL_TWO) {
    ObPartition **partitions = schema.get_part_array();
    const int64_t partition_count = schema.get_partition_num();
    if (partitions == nullptr || partition_count <= 0) {
      ret = OB_INVALID_ARGUMENT;
    }
    for (int64_t i = 0; OB_SUCC(ret) && i < partition_count; ++i) {
      ObPartition *partition = partitions[i];
      if (partition == nullptr) {
        ret = OB_ERR_UNEXPECTED;
      } else if (level == PARTITION_LEVEL_ONE) {
        ret = rewrite(partition->get_tablet_id(),
            [&](ObTabletID id) { partition->set_tablet_id(id); });
      } else {
        ObSubPartition **subpartitions = partition->get_subpart_array();
        const int64_t subpartition_count = partition->get_subpartition_num();
        if (subpartitions == nullptr || subpartition_count <= 0) {
          ret = OB_INVALID_ARGUMENT;
        }
        for (int64_t j = 0; OB_SUCC(ret) && j < subpartition_count; ++j) {
          if (subpartitions[j] == nullptr) {
            ret = OB_ERR_UNEXPECTED;
          } else {
            ret = rewrite(subpartitions[j]->get_tablet_id(),
                [&](ObTabletID id) { subpartitions[j]->set_tablet_id(id); });
          }
        }
      }
    }
    if (OB_SUCC(ret) && level == PARTITION_LEVEL_ONE
        && schema.get_hidden_partition_num() > 0) {
      ObPartition **hidden_partitions = schema.get_hidden_part_array();
      const int64_t hidden_partition_count = schema.get_hidden_partition_num();
      if (hidden_partitions == nullptr) {
        ret = OB_ERR_UNEXPECTED;
      }
      for (int64_t i = 0; OB_SUCC(ret) && i < hidden_partition_count; ++i) {
        ObPartition *partition = hidden_partitions[i];
        if (partition == nullptr) {
          ret = OB_ERR_UNEXPECTED;
        } else {
          ret = rewrite(partition->get_tablet_id(),
              [&](ObTabletID id) { partition->set_tablet_id(id); });
        }
      }
    }
  } else {
    ret = OB_NOT_SUPPORTED;
  }
  return ret;
}

int all_physical_tablet_ids(ObIArray<ObTabletID> &ids)
{
  auto *service = share::server_service<ObLSService>();
  ObLS *ls = nullptr;
  int ret = service == nullptr ? OB_NOT_INIT : service->get_ls(ls);
  if (ret == OB_SUCCESS && ls == nullptr) { ret = OB_ERR_UNEXPECTED; }
  if (ret == OB_SUCCESS) {
    ret = ls->get_tablet_svr()->get_all_tablet_ids(
        true /* except_ls_inner_tablet */, ids);
  }
  return ret;
}

int namespace_has_physical_tablet(uint64_t namespace_id, bool &has_tablet)
{
  has_tablet = false;
  ObArray<ObTabletID> tablets;
  int ret = all_physical_tablet_ids(tablets);
  for (int64_t i = 0; ret == OB_SUCCESS && i < tablets.count(); ++i) {
    const uint64_t id = tablets.at(i).id();
    if (NamespaceObjectKey::is_encoded(id) && database_of(id) == namespace_id) {
      has_tablet = true;
      break;
    }
  }
  return ret;
}

int collect_metadata() {
  auto *store = directory_kv_store();
  if (store == nullptr) { return OB_NOT_INIT; }
  InstanceMetaStore::Transaction tx;
  int ret = store->begin_directory_gc(tx, directory_deadline());
  int64_t deleted = 0;
  if (ret == OB_SUCCESS) {
    rootserver::InstanceNamespaceMetadata metadata(*store, tx);
    ret = metadata.collect_unreachable_pages(256, deleted);
  }
  if (tx.is_active()) {
    const int end = ret == OB_SUCCESS ? store->commit(tx) : store->rollback(tx);
    if (ret == OB_SUCCESS) { ret = end; }
  }
  LOG_INFO("PROTOTYPE_V9_METADATA_GC", K(ret), K(deleted));
  return ret;
}

}

int NamespaceForkKernelPrototype::ensure_control_schema() {
  auto *access = share::server_service<ObAccessService>();
  auto *schema_service = directory_schema_service();
  auto *proxy = directory_sql_proxy();
  if (access == nullptr || schema_service == nullptr || proxy == nullptr) {
    return OB_NOT_INIT;
  }
  ObSchemaGetterGuard guard;
  int64_t schema_version = 0;
  SCN watermark;
  int ret = schema_service->get_runtime_schema_guard(guard);
  if (OB_SUCC(ret)) { ret = guard.get_schema_version(schema_version); }
  if (OB_SUCC(ret)) { ret = ObGlobalStatProxy::get_snapshot_gc_scn(*proxy, watermark); }
  bool created = false;
  if (OB_SUCC(ret)) {
    rootserver::InstanceNamespaceDirectory directory(access->instance_meta_store());
    ret = directory.ensure_root("ns1", schema_version, watermark.get_val_for_tx(),
        ObTimeUtility::current_time() + 120 * 1000 * 1000, created);
  }
  if (OB_SUCC(ret) && created) {
    rootserver::NamespaceSchemaPublication publication(access->instance_meta_store(), proxy->target_namespace());
    ret = publication.initialize(guard);
  }
  if (OB_SUCC(ret)) {
    ret = observer::namespace_worker_prototype::complete_namespace_schema_bootstrap(*schema_service);
  }
  if (OB_SUCC(ret) && created) {
    uint64_t template_id = 0;
    ret = control_namespace(ObString::make_string("ns1"),
        ObString::make_string("__template__"), template_id);
  }
  LOG_INFO("PROTOTYPE_NAMESPACE_CONTROL_SCHEMA", K(ret));
  return ret;
}
void NamespaceForkKernelPrototype::invalidate_replayed_metadata()
{
  {
    std::unique_lock<std::shared_mutex> lock(namespace_state_mutex);
    namespace_state_cache.clear();
  }
  {
    std::unique_lock<std::shared_mutex> lock(tablet_table_mutex);
    tablet_table_cache.clear();
  }
  control_state().clear_exceptions();
}
int NamespaceForkKernelPrototype::begin_namespace_drop(const ObString &name, uint64_t &id, bool &done) {
  done = false; id = 0;
  auto *store = directory_kv_store();
  if (store == nullptr) { return OB_NOT_INIT; }
  MetadataReadGuard access; if (access.error() != OB_SUCCESS) { return access.error(); }
  rootserver::InstanceNamespaceDirectory directory(*store);
  rootserver::InstanceNamespaceRecord record;
  int ret = directory.find_named(std::string(name.ptr(), name.length()),
      directory_deadline(), record);
  if (OB_SUCC(ret)) {
    id = record.id;
    if (id == 1) {
      ret = OB_OP_NOT_ALLOW;
    } else if (record.roots.state != 0 && record.roots.state != 1) {
      ret = OB_STATE_NOT_MATCH;
    } else if (!ns::namespace_registry().begin_drop(id)) {
      ret = OB_OP_NOT_ALLOW;
      LOG_USER_ERROR(OB_OP_NOT_ALLOW, "drop a namespace with active connections");
    } else {
      ret = directory.mark_deleting(id, std::string(name.ptr(), name.length()),
          directory_deadline(), done);
      if (ret != OB_SUCCESS) {
        rootserver::InstanceNamespaceRecord after;
        if (directory.get(id, directory_deadline(), after) == OB_SUCCESS
            && after.roots.state == 0) {
          ns::namespace_registry().cancel_drop(id);
        }
        // An uncertain commit keeps admission closed until state can be read.
      }
    }
  }
  if (OB_SUCC(ret)) { invalidate_namespace_state(id); }
  LOG_INFO("PROTOTYPE_V7_NAMESPACE_CLOSE", K(ret), K(id), K(done));
  return ret;
}
int NamespaceForkKernelPrototype::lock_namespace_drop(uint64_t id,
    ObIArray<ObTabletID> &bound_tablets) {
  auto *store = directory_kv_store();
  if (store == nullptr) { return OB_NOT_INIT; }
  MetadataReadGuard access; if (access.error() != OB_SUCCESS) { return access.error(); }
  rootserver::InstanceNamespaceDirectory directory(*store);
  std::vector<uint64_t> local_tablets;
  int ret = directory.list_deleting_owned(id, directory_deadline(), local_tablets);
  for (uint64_t tablet : local_tablets) {
    if (OB_FAIL(ret)) { break; }
    ret = bound_tablets.push_back(ObTabletID(encoded(id, tablet)));
  }
  return ret;
}
int NamespaceForkKernelPrototype::finish_namespace_drop(uint64_t id) {
  auto *store = directory_kv_store();
  if (store == nullptr) { return OB_NOT_INIT; }
  MetadataReadGuard access; if (access.error() != OB_SUCCESS) { return access.error(); }
  rootserver::InstanceNamespaceDirectory directory(*store);
  int ret = directory.finish_drop(id, directory_deadline());
  if (OB_SUCC(ret)) { invalidate_namespace_state(id); }
  if (OB_SUCC(ret)) { control_state().drop_exceptions(id); }
  return ret;
}
int NamespaceForkKernelPrototype::check_baseline_access(
    const ObTabletID &tablet_id, TabletAccessProtection &protection) {
  MetadataReadGuard access; if (access.error() != OB_SUCCESS) { return access.error(); }
  if (!is_encoded_id(tablet_id.id())) { return OB_SUCCESS; }
  const uint64_t ns = database_of(tablet_id.id());
  int ret = acquire_namespace(ns, protection);
  if (OB_SUCC(ret)) { ret = protect_tablet_sources(tablet_id, protection); }
  if (OB_FAIL(ret)) { return ret; }
  // A baseline DAG is only valid on a tablet its namespace still owns. DROP
  // drains active accesses before deleting the owned rows, so an admitted DAG
  // always finishes against a valid binding.
  int64_t state = 0;
  bool owned = false;
  if (share::server_is_recovery_mode()) {
    uint64_t table = OB_INVALID_ID;
    ret = load_kv_tablet_ownership(ns, local_of(tablet_id.id()), owned, table, &state);
  } else {
    if (!cached_namespace_state(ns, state)) {
      if (OB_FAIL(load_kv_namespace_state(ns, state))) { return ret; }
      remember_namespace_state(ns, state);
    }
    ret = load_kv_exceptions(ns);
    owned = control_state().owned(ns, local_of(tablet_id.id()));
  }
  if (OB_SUCC(ret) && (state != 0 || !owned)) {
    // A retired intermediate copy may still supply descendants. Completing
    // its baseline lets those descendants eventually release the whole chain.
    ObArray<ObTabletID> candidate;
    bool retained = false;
    if (OB_FAIL(candidate.push_back(tablet_id))) {
    } else if (OB_FAIL(protect_snapshot_tablets(candidate, retained))) {
    } else if (!candidate.empty()) { ret = OB_ENTRY_NOT_EXIST; }
  }
  return ret;
}
TabletAccessProtection::~TabletAccessProtection() { reset(); }
void TabletAccessProtection::reset() { NamespaceForkKernelPrototype::release_access(*this); }

int NamespaceForkKernelPrototype::acquire_namespace(uint64_t id,
    TabletAccessProtection &protection)
{
  if (protection.namespaces_.insert(id).second) {
    std::lock_guard<std::mutex> lock(access_mutex);
    ++namespace_accesses[id];
  }
  return OB_SUCCESS;
}

int NamespaceForkKernelPrototype::protect_tablet_sources(const ObTabletID &tablet,
    TabletAccessProtection &protection)
{
  ObTabletID current = tablet;
  ObSEArray<uint64_t, 8> chain;
  int ret = OB_SUCCESS;
  while (OB_SUCC(ret) && current.is_valid()) {
    if (has_exist_in_array(chain, current.id())) { return OB_STATE_NOT_MATCH; }
    if (chain.count() >= 64) { return OB_SIZE_OVERFLOW; }
    if (OB_FAIL(chain.push_back(current.id()))) { break; }
    if (protection.tablets_.insert(current.id()).second) {
      std::lock_guard<std::mutex> lock(access_mutex);
      ++tablet_accesses[current.id()];
    }
    ObTabletHandle handle;
    ret = ObTabletCreateDeleteHelper::check_and_get_tablet(ObTabletMapKey(current),
        handle, 0, ObMDSGetTabletMode::READ_WITHOUT_CHECK,
        transaction::ObTransVersion::MAX_TRANS_VERSION);
    if (ret == OB_TABLET_NOT_EXIST || ret == OB_ENTRY_NOT_EXIST) { return OB_SUCCESS; }
    if (OB_FAIL(ret) || handle.get_obj()->is_empty_shell()) { break; }
    const auto &fork = handle.get_obj()->get_tablet_meta().fork_info_;
    if (!fork.is_valid() || fork.is_complete()) { break; }
    current = fork.get_fork_src_tablet_id();
  }
  return ret;
}

void NamespaceForkKernelPrototype::release_access(TabletAccessProtection &protection)
{
  if (protection.namespaces_.empty() && protection.tablets_.empty()) { return; }
  std::lock_guard<std::mutex> lock(access_mutex);
  for (uint64_t id : protection.namespaces_) {
    auto found = namespace_accesses.find(id);
    OB_ASSERT(found != namespace_accesses.end() && found->second > 0);
    if (--found->second == 0) { namespace_accesses.erase(found); }
  }
  for (uint64_t id : protection.tablets_) {
    auto found = tablet_accesses.find(id);
    OB_ASSERT(found != tablet_accesses.end() && found->second > 0);
    if (--found->second == 0) { tablet_accesses.erase(found); }
  }
  protection.namespaces_.clear();
  protection.tablets_.clear();
}

int NamespaceForkKernelPrototype::drain_access(uint64_t namespace_id)
{
  for (;;) {
    {
      std::lock_guard<std::mutex> lock(access_mutex);
      if (namespace_accesses.count(namespace_id) == 0) { return OB_SUCCESS; }
    }
    const int ret = THIS_WORKER.check_status();
    if (ret != OB_SUCCESS) { return ret; }
    ob_usleep(10 * 1000);
  }
}

int NamespaceForkKernelPrototype::prepare_access(uint64_t namespace_id, uint64_t table_id,
    ObTabletID &tablet, bool read_only, data_plane::ObNamespaceAccessMode mode,
    TabletAccessProtection &protection, const std::function<int(ObTabletID &)> &prepare)
{
  // The collector's explicit internal SQL context must be able to access its
  // own metadata while holding the publication lock exclusively.
  if (!is_encoded_id(tablet.id())
      || (mode == data_plane::ObNamespaceAccessMode::UNFENCED && is_inner_table(table_id))) {
    return prepare(tablet);
  }
  MetadataReadGuard access;
  int ret = access.error();
  if (OB_SUCC(ret)) { ret = acquire_namespace(namespace_id, protection); }
  if (OB_SUCC(ret)) { ret = check_table_access(table_id, tablet, read_only, mode); }
  if (OB_SUCC(ret)) { ret = prepare(tablet); }
  // Resolution and registration are under the same short publication guard.
  // Reusing a context adds dependencies without dropping its earlier ones.
  if (OB_SUCC(ret)) { ret = protect_tablet_sources(tablet, protection); }
  return ret;
}
int NamespaceForkKernelPrototype::check_table_access(
    uint64_t table_id, const ObTabletID &tablet_id, bool read_only,
    data_plane::ObNamespaceAccessMode access_mode) {
  if (!is_encoded_id(tablet_id.id())) {
    return OB_SUCCESS;
  }
  if (access_mode == data_plane::ObNamespaceAccessMode::UNFENCED) {
    // Internal SQL metadata remains unfenced so physical GC can run its own
    // control transaction. User data still participates in the read drain,
    // including the initial Namespace's explicitly unfenced SQL context.
    return OB_SUCCESS;
  }
  if (access_mode != data_plane::ObNamespaceAccessMode::LEASED) {
    const int ret = OB_INVALID_ARGUMENT;
    LOG_ERROR("encoded tablet access has no namespace policy", K(table_id),
              K(tablet_id), "access_mode", static_cast<int>(access_mode));
    return ret;
  }
  const uint64_t id = database_of(tablet_id.id());
  int ret = OB_SUCCESS;
  int64_t state = 0;
  if (cached_namespace_state(id, state)) {
  } else {
    if (OB_FAIL(load_kv_namespace_state(id, state))) {
      return ret;
    }
    remember_namespace_state(id, state);
  }
  if (state != 0 && !(read_only && (state == 1 || state == 2))) {
    // Descendants may still read a physical tablet owned by this ancestor.
    // Namespace admission and the access drain fence prevent new reads from
    // the dropped owner itself; all writes must target a live namespace.
    ret = OB_OP_NOT_ALLOW;
    LOG_USER_ERROR(OB_OP_NOT_ALLOW, "access a closing or deleted prototype namespace");
  }
  return ret;
}
int NamespaceForkKernelPrototype::protect_snapshot_tablets(
    ObIArray<ObTabletID> &candidates, bool &need_retry) {
  if (candidates.empty()) { return OB_SUCCESS; }
  bool has_namespace_tablets = false;
  std::vector<uint64_t> physical_ids;
  physical_ids.reserve(candidates.count());
  for (int64_t i = 0; i < candidates.count(); ++i) {
    const uint64_t id = candidates.at(i).id();
    has_namespace_tablets |= is_encoded_id(id);
    physical_ids.push_back(id);
  }
  if (!has_namespace_tablets) { return OB_SUCCESS; }
  auto *store = directory_kv_store();
  if (store == nullptr) { return OB_NOT_INIT; }
  rootserver::InstanceNamespaceDirectory directory(*store);
  std::vector<uint64_t> unreferenced;
  std::map<uint64_t, uint64_t> sources;
  ObArray<ObTabletID> tablets;
  int ret = all_physical_tablet_ids(tablets);
  for (int64_t i = 0; OB_SUCC(ret) && i < tablets.count(); ++i) {
    ObTabletHandle handle;
    ret = ObTabletCreateDeleteHelper::check_and_get_tablet(
        ObTabletMapKey(tablets.at(i)), handle, 0, ObMDSGetTabletMode::READ_WITHOUT_CHECK,
        transaction::ObTransVersion::MAX_TRANS_VERSION);
    if (ret == OB_TABLET_NOT_EXIST) { ret = OB_SUCCESS; continue; }
    if (OB_FAIL(ret)) { break; }
    const auto &tablet = *handle.get_obj();
    const auto &fork = tablet.get_tablet_meta().fork_info_;
    if (!tablet.is_empty_shell() && fork.is_valid() && !fork.is_complete()
        && fork.get_fork_src_tablet_id().is_valid()) {
      sources.emplace(tablets.at(i).id(), fork.get_fork_src_tablet_id().id());
    }
  }
  bool retained = false;
  if (OB_SUCC(ret)) {
    ret = directory.filter_unreferenced_tablets(physical_ids, probe_historical_tablet,
        sources, directory_deadline(), unreferenced, retained);
  }
  ObArray<ObTabletID> filtered;
  for (uint64_t id : unreferenced) {
    if (OB_FAIL(ret)) { break; }
    ret = filtered.push_back(ObTabletID(id));
  }
  if (OB_SUCC(ret)) { ret = candidates.assign(filtered); }
  if (OB_SUCC(ret)) { need_retry |= retained; }
  return ret;
}

int NamespaceForkKernelPrototype::reclaim_unreferenced_tablets(
    ObIArray<ObTabletID> &candidates, bool &need_retry,
    const std::function<int(const ObIArray<ObTabletID> &)> &reclaim)
{
  if (!reclaim) { return OB_INVALID_ARGUMENT; }
  PhysicalReclamationGuard fence;
  int ret = fence.error();
  if (OB_SUCC(ret)) { ret = protect_snapshot_tablets(candidates, need_retry); }
  if (OB_SUCC(ret)) { ret = exclude_active_tablets(candidates, need_retry); }
  if (OB_SUCC(ret) && !candidates.empty()) { ret = reclaim(candidates); }
  return ret;
}

int NamespaceForkKernelPrototype::collect_dropped_namespace_tablets() {
  auto *store = directory_kv_store();
  if (store == nullptr || !ATOMIC_LOAD(&GCTX.sys_package_ready_)) { return OB_SUCCESS; }
  static std::mutex scan_mutex;
  static uint64_t physical_cursor = 0;
  std::lock_guard<std::mutex> scan_guard(scan_mutex);
  PhysicalReclamationGuard fence;
  if (fence.error() != OB_SUCCESS) { return fence.error(); }
  rootserver::InstanceNamespaceDirectory directory(*store);
  std::vector<rootserver::InstanceNamespaceRecord> deleted;
  int ret = directory.list_deleted(directory_deadline(), deleted);
  if (OB_FAIL(ret) || deleted.empty()) { return ret; }
  std::unordered_set<uint64_t> deleted_ids;
  for (const auto &record : deleted) { deleted_ids.insert(record.id); }

  ObArray<ObTabletID> all_tablets;
  if (OB_FAIL(all_physical_tablet_ids(all_tablets))) { return ret; }
  std::vector<uint64_t> physical;
  for (int64_t i = 0; i < all_tablets.count(); ++i) {
    const uint64_t id = all_tablets.at(i).id();
    if (NamespaceObjectKey::is_encoded(id)
        && deleted_ids.count(database_of(id)) != 0) {
      physical.push_back(id);
    }
  }
  std::sort(physical.begin(), physical.end());
  physical.erase(std::unique(physical.begin(), physical.end()), physical.end());
  auto first = std::upper_bound(physical.begin(), physical.end(), physical_cursor);
  if (first == physical.end()) { first = physical.begin(); }
  ObArray<ObTabletID> candidates;
  const size_t start_index = first - physical.begin();
  for (size_t offset = 0; OB_SUCC(ret) && offset < physical.size()
      && candidates.count() < 64; ++offset) {
    const uint64_t id = physical[(start_index + offset) % physical.size()];
    physical_cursor = id;
    bool exists = false;
    if (OB_FAIL(probe_physical_tablet(id, exists))) {
    } else if (exists) {
      ret = candidates.push_back(ObTabletID(id));
    }
  }
  if (OB_FAIL(ret)) { return ret; }
  bool deferred = false;
  if (!candidates.empty()
      && OB_FAIL(protect_snapshot_tablets(candidates, deferred))) { return ret; }
  if (OB_FAIL(exclude_active_tablets(candidates, deferred))) { return ret; }

  if (!candidates.empty()) {
    int64_t schema_version = 0;
    ObSchemaGetterGuard guard;
    ObMultiVersionSchemaService *directory_schema = directory_schema_service();
    if (directory_schema == nullptr) {
      ret = OB_NOT_INIT;
    } else if (OB_FAIL(directory_schema->get_runtime_schema_guard(guard))) {
    } else if (OB_FAIL(guard.get_schema_version(schema_version))) {
    }
    ObMySQLTransaction trans;
    if (OB_SUCC(ret)) { ret = trans.start(directory_sql_proxy()); }
    if (OB_SUCC(ret)) {
      ObLockAloneTabletRequest locks;
      locks.lock_mode_ = EXCLUSIVE;
      locks.op_type_ = ObTableLockOpType::IN_TRANS_COMMON_LOCK;
      locks.timeout_us_ = 5 * 1000 * 1000L;
      if (OB_FAIL(locks.tablet_ids_.assign(candidates))) {
      } else if (OB_FAIL(query::ObInnerSQLConnectionAccess::lock_tablet(
              locks, trans.get_connection()))) {
      } else {
        rootserver::ObTabletDrop drop(trans, schema_version);
        if (OB_FAIL(drop.init())) {
        } else if (OB_FAIL(drop.add_drop_tablets_arg(candidates))) {
        } else {
          observer::namespace_worker_prototype::PhysicalTabletMdsScope physical_mds(true);
          ret = drop.execute();
        }
      }
    }
    if (trans.is_started()) {
      const int end = trans.end(OB_SUCC(ret));
      if (OB_SUCC(ret)) { ret = end; }
    }
  }
  // Physical cleanup commits before removing the directory's owned entries.
  // A crash in this gap is repaired by probing the same physical IDs next pass.
  for (const auto &record : deleted) {
    if (OB_FAIL(ret)) { break; }
    std::vector<uint64_t> owned;
    if (OB_FAIL(directory.list_deleted_owned(record.id, directory_deadline(), owned))) {
      break;
    }
    std::vector<uint64_t> missing;
    for (uint64_t local : owned) {
      ObTabletHandle handle;
      ret = ObTabletCreateDeleteHelper::check_and_get_tablet(
          ObTabletMapKey(ObTabletID(encoded(record.id, local))), handle, 0,
          ObMDSGetTabletMode::READ_WITHOUT_CHECK,
          transaction::ObTransVersion::MAX_TRANS_VERSION);
      if (ret == OB_TABLET_NOT_EXIST || ret == OB_ENTRY_NOT_EXIST) {
        ret = OB_SUCCESS;
        missing.push_back(local);
      } else if (OB_FAIL(ret)) { break; }
      else if (handle.get_obj()->is_empty_shell()) { missing.push_back(local); }
    }
    if (OB_SUCC(ret) && !missing.empty()) {
      ret = directory.erase_deleted_owned(record.id, missing, directory_deadline());
      if (OB_SUCC(ret)) { control_state().drop_exceptions(record.id); }
    }
    bool pruned = false;
    if (OB_SUCC(ret)) {
      ret = directory.prune_deleted(record.id, namespace_has_physical_tablet,
          directory_deadline(), pruned);
    }
    if (OB_SUCC(ret) && pruned) {
      invalidate_namespace_state(record.id);
      control_state().drop_exceptions(record.id);
      control_state().forget_chain_link(record.id);
    }
  }
  LOG_INFO("PROTOTYPE_NAMESPACE_DROPPED_TABLET_GC", K(ret),
      "dropped", candidates.count(), K(deferred), "deleted", deleted.size());
  return ret;
}
bool NamespaceForkKernelPrototype::is_encoded_id(uint64_t id) {
  return NamespaceObjectKey::is_encoded(id);
}
uint64_t NamespaceForkKernelPrototype::encode_id(uint64_t namespace_id, uint64_t local_id) {
  return NamespaceObjectKey{namespace_id, local_id}.storage_id();
}
int NamespaceForkKernelPrototype::local_object_id(
    uint64_t namespace_id, uint64_t object_id, uint64_t &local_id) {
  local_id = object_id;
  if (namespace_id == 0 || namespace_id >= NamespaceObjectKey::NAMESPACE_LIMIT) { return OB_INVALID_ARGUMENT; }
  if (is_encoded_id(object_id)) {
    if (database_of(object_id) != namespace_id) { return OB_INVALID_ARGUMENT; }
    local_id = local_of(object_id);
  }
  return OB_SUCCESS;
}
int NamespaceForkKernelPrototype::storage_object_id(
    uint64_t namespace_id, uint64_t object_id, uint64_t &storage_id) {
  uint64_t local_id = OB_INVALID_ID;
  int ret = local_object_id(namespace_id, object_id, local_id);
  const NamespaceObjectKey key{namespace_id, local_id};
  if (OB_SUCC(ret) && !key.is_valid()) {
    ret = OB_SIZE_OVERFLOW;
  } else if (OB_SUCC(ret)) {
    storage_id = key.storage_id();
  }
  return ret;
}
int NamespaceForkKernelPrototype::make_namespace_schema(
    uint64_t namespace_id, const ObTableSchema &storage_schema, ObTableSchema &namespace_schema) {
  if (!NamespaceObjectKey{namespace_id, 1}.is_valid()) { return OB_INVALID_ARGUMENT; }
  if (is_encoded_id(storage_schema.get_table_id())
      || is_encoded_id(storage_schema.get_database_id())) {
    return OB_INVALID_ARGUMENT;
  }
  int ret = namespace_schema.assign(storage_schema);
  if (OB_SUCC(ret) && !namespace_schema.is_view_table()) {
    ret = rewrite_tablet_ids(namespace_schema,
        [&](uint64_t id, uint64_t &rewritten) {
          return local_object_id(namespace_id, id, rewritten);
        });
  }
  return ret;
}
int NamespaceForkKernelPrototype::make_storage_schema(
    uint64_t namespace_id, const ObTableSchema &logical_schema, ObTableSchema &storage_schema) {
  if (!NamespaceObjectKey{namespace_id, 1}.is_valid()) { return OB_INVALID_ARGUMENT; }
  if (is_encoded_id(logical_schema.get_table_id())
      || is_encoded_id(logical_schema.get_database_id())) {
    return OB_INVALID_ARGUMENT;
  }
  int ret = storage_schema.assign(logical_schema);
  // Schema IDs stay local to their bound namespace; only tablet IDs address shared storage.
  if (OB_SUCC(ret) && !storage_schema.is_view_table()) {
    ret = rewrite_tablet_ids(storage_schema,
        [&](uint64_t id, uint64_t &rewritten) {
          return storage_object_id(namespace_id, id, rewritten);
        });
  }
  return ret;
}
int NamespaceForkKernelPrototype::namespace_schema_version(uint64_t ns, int64_t &version) {
  version = OB_INVALID_VERSION;
  auto *store = directory_kv_store();
  if (store == nullptr) { return OB_NOT_INIT; }
  if (!NamespaceObjectKey{ns, 1}.is_valid()) { return OB_INVALID_ARGUMENT; }
  rootserver::InstanceNamespaceDirectory directory(*store);
  return directory.schema_version(ns, directory_deadline(), version);
}
int NamespaceForkKernelPrototype::begin_schema_change(uint64_t ns) {
  auto *store = directory_kv_store();
  if (store == nullptr) { return OB_NOT_INIT; }
  rootserver::InstanceNamespaceDirectory directory(*store);
  return directory.begin_schema_change(ns, directory_deadline());
}
int NamespaceForkKernelPrototype::finish_schema_change(
    uint64_t ns, int64_t schema_version) {
  auto *store = directory_kv_store();
  if (store == nullptr) { return OB_NOT_INIT; }
  rootserver::InstanceNamespaceDirectory directory(*store);
  return directory.finish_schema_change(ns, schema_version, directory_deadline());
}
int NamespaceForkKernelPrototype::begin_schema_recovery(uint64_t ns, bool &needed) {
  auto *store = directory_kv_store();
  if (store == nullptr) { return OB_NOT_INIT; }
  rootserver::InstanceNamespaceDirectory directory(*store);
  return directory.begin_schema_recovery(ns, directory_deadline(), needed);
}
int NamespaceForkKernelPrototype::finish_schema_recovery(
    uint64_t ns, int64_t schema_version) {
  auto *store = directory_kv_store();
  if (store == nullptr) { return OB_NOT_INIT; }
  rootserver::InstanceNamespaceDirectory directory(*store);
  return directory.finish_schema_recovery(ns, schema_version, directory_deadline());
}
int NamespaceForkKernelPrototype::observe_database(ObISQLClient &trans, const ObDatabaseSchema &schema) {
  // Namespace schema authority is native: databases live in each worker's own
  // schema cache, no legacy catalog enrollment.
  return OB_SUCCESS;
}
int NamespaceForkKernelPrototype::check_database_ddl(const ObDatabaseSchema &schema, const ObISQLClient *trans) {
  return OB_SUCCESS;
}
int NamespaceForkKernelPrototype::control_namespace(const ObString &source, const ObString &target, uint64_t &id) {
  id = 0;
  if (source.empty() || target.empty() || target.length() > 128) {
    return OB_INVALID_ARGUMENT;
  }
  if (source == "__gc__" && target == "__gc__") {
    id = OB_INVALID_ID;
    return collect_metadata();
  }
  MetadataReadGuard publication;
  if (publication.error() != OB_SUCCESS) { return publication.error(); }
  auto *access = share::server_service<ObAccessService>();
  if (access == nullptr) { return OB_NOT_INIT; }
  const int64_t begin_us = ObTimeUtility::current_time();
  const std::string source_name(source.ptr(), source.length());
  const std::string target_name(target.ptr(), target.length());
  rootserver::InstanceNamespaceDirectory directory(access->instance_meta_store());
  rootserver::InstanceNamespaceRecord child;
  int ret = directory.fork_namespace(source_name, target_name,
      [&](int64_t &snapshot) {
        return observer::namespace_worker_prototype::acquire_storage_snapshot(snapshot);
      }, begin_us + 120 * 1000 * 1000, child);
  if (OB_SUCC(ret) && !NamespaceObjectKey{child.id, 1}.is_valid()) {
    ret = OB_SIZE_OVERFLOW;
  }
  if (OB_SUCC(ret)) {
    id = child.id;
    control_state().remember_chain_link(id, child.parent_namespace, child.fork_cap);
    ret = observer::namespace_worker_prototype::reload_storage_freeze_info();
  }
  if (OB_SUCC(ret) && target_name != "__template__"
      && target_name != "__template_build__") {
    char register_name[ns::Namespace::MAX_NAME_LEN];
    if (target.length() >= sizeof(register_name)) {
      ret = OB_SIZE_OVERFLOW;
    } else {
      MEMCPY(register_name, target.ptr(), target.length());
      register_name[target.length()] = '\0';
      if (ns::namespace_registry().add(id, register_name) != 0) {
        ret = OB_ERR_UNEXPECTED;
      }
    }
  }
  LOG_INFO("PROTOTYPE_NAMESPACE_KV_REGISTER", K(ret), K(id),
      "source_id", child.parent_namespace, "snapshot", child.roots.snapshot,
      "publish_us", ObTimeUtility::current_time() - begin_us);
  return ret;
}

namespace {

bool owns_namespace_directory_entries(const ObTableSchema &schema)
{
  return schema.is_user_table() || schema.is_index_table()
      || schema.is_aux_lob_table();
}

int collect_directory_tablets(
    uint64_t namespace_id,
    const ObIArray<const ObTableSchema *> &schemas,
    std::map<uint64_t, uint64_t> &tablets)
{
  int ret = OB_SUCCESS;
  for (int64_t i = 0; OB_SUCC(ret) && i < schemas.count(); ++i) {
    const ObTableSchema *schema = schemas.at(i);
    if (schema == nullptr) {
      ret = OB_INVALID_ARGUMENT;
    } else if (!owns_namespace_directory_entries(*schema)) {
      continue;
    } else if (!directory_supported(*schema)) {
      ret = OB_NOT_SUPPORTED;
    } else {
      const uint64_t table_id = schema->get_table_id();
      ObArray<ObTabletID> schema_tablets;
      if (NamespaceForkKernelPrototype::is_encoded_id(table_id)
          || NamespaceForkKernelPrototype::is_encoded_id(schema->get_database_id())
          || table_id >= (1ULL << 32)
          || OB_FAIL(schema_tablet_ids(*schema, schema_tablets))) {
        if (OB_SUCC(ret)) { ret = OB_INVALID_ARGUMENT; }
      }
      for (int64_t j = 0; OB_SUCC(ret) && j < schema_tablets.count(); ++j) {
        const uint64_t tablet_id = schema_tablets.at(j).id();
        const NamespaceObjectKey storage_key{namespace_id, tablet_id};
        if (NamespaceForkKernelPrototype::is_encoded_id(tablet_id)
            || !storage_key.is_valid()) {
          ret = OB_INVALID_ARGUMENT;
        } else {
          if (!tablets.emplace(tablet_id, table_id).second) {
            ret = OB_STATE_NOT_MATCH;
          }
        }
      }
    }
  }
  return ret;
}

} // namespace

int NamespaceForkKernelPrototype::publish_schema_delta(
    uint64_t namespace_id,
    int64_t base_schema_version,
    int64_t schema_version,
    const ObIArray<const ObTableSchema *> &current_schemas,
    const ObIArray<const ObTableSchema *> &previous_schemas) {
  if (namespace_id == 0 || namespace_id >= NamespaceObjectKey::NAMESPACE_LIMIT
      || base_schema_version <= 0 || schema_version < base_schema_version
      || directory_kv_store() == nullptr) {
    return OB_INVALID_ARGUMENT;
  }
  int ret = OB_SUCCESS;
  MetadataReadGuard publication;
  if (publication.error() != OB_SUCCESS) { return publication.error(); }
  std::unordered_set<uint64_t> current_table_ids;
  std::unordered_set<uint64_t> previous_table_ids;
  for (int64_t i = 0; OB_SUCC(ret) && i < current_schemas.count(); ++i) {
    const ObTableSchema *schema = current_schemas.at(i);
    if (schema == nullptr) {
      ret = OB_INVALID_ARGUMENT;
    } else if (is_encoded_id(schema->get_table_id())
        || is_encoded_id(schema->get_database_id())
        || !current_table_ids.insert(schema->get_table_id()).second) {
      ret = OB_INVALID_ARGUMENT;
    }
  }
  for (int64_t i = 0; OB_SUCC(ret) && i < previous_schemas.count(); ++i) {
    const ObTableSchema *schema = previous_schemas.at(i);
    if (schema == nullptr) {
      ret = OB_INVALID_ARGUMENT;
    } else if (is_encoded_id(schema->get_table_id())
        || is_encoded_id(schema->get_database_id())
        || !previous_table_ids.insert(schema->get_table_id()).second) {
      ret = OB_INVALID_ARGUMENT;
    }
  }
  std::map<uint64_t, uint64_t> previous_tablets;
  std::map<uint64_t, uint64_t> current_tablets;
  std::vector<uint64_t> removed_owned;
  if (OB_SUCC(ret)) {
    ret = collect_directory_tablets(namespace_id, previous_schemas, previous_tablets);
  }
  if (OB_SUCC(ret)) {
    ret = collect_directory_tablets(namespace_id, current_schemas, current_tablets);
  }
  if (OB_SUCC(ret)) {
    // DDL has committed, and its persistent pending marker prevents fork
    // until this delta publishes. A later fork must therefore be beyond this
    // deletion boundary, even though SQL and instance KV commit separately.
    int64_t drop_scn = 0;
    ret = observer::namespace_worker_prototype::acquire_storage_snapshot(drop_scn);
    rootserver::InstanceNamespaceDirectory directory(*directory_kv_store());
    if (OB_SUCC(ret)) { ret = directory.publish_schema_delta(namespace_id, base_schema_version,
        schema_version, drop_scn, previous_tablets, current_tablets,
        [&](uint64_t local_tablet, bool &exists) {
          return probe_physical_tablet(encoded(namespace_id, local_tablet), exists);
        }, [&](uint64_t local_tablet, int64_t &birth) {
          return physical_tablet_birth(encoded(namespace_id, local_tablet), birth);
        }, directory_deadline(), removed_owned); }
  }
  if (OB_SUCC(ret)) { ret = finish_schema_publication(namespace_id, schema_version, removed_owned); }
  LOG_INFO("PROTOTYPE_NAMESPACE_SCHEMA_DELTA", K(ret), K(namespace_id), K(schema_version),
      "current_count", current_schemas.count(), "previous_count", previous_schemas.count());
  return ret;
}

int NamespaceForkKernelPrototype::finish_schema_publication(uint64_t namespace_id,
    int64_t schema_version, const std::vector<uint64_t> &removed_owned)
{
  int ret = OB_SUCCESS;
  ObArray<ObTabletID> private_tablets;
  for (uint64_t local_tablet : removed_owned) {
    if (OB_FAIL(ret)) { break; }
    ret = private_tablets.push_back(ObTabletID(encoded(namespace_id, local_tablet)));
  }

  if (OB_SUCC(ret)) {
    // The delta rewrote several exception rows; force a lazy reload rather
    // than tracking every mutation.
    control_state().drop_exceptions(namespace_id);
  }
  // Native schema rows live in the namespace worker. Physical tablet mappings
  // are shared engine metadata, so reclaim them only after the namespace
  // catalog commit and through a control-namespace transaction.
  if (OB_SUCC(ret) && !private_tablets.empty()) {
    ObMySQLTransaction trans;
    ObLockAloneTabletRequest locks;
    locks.lock_mode_ = EXCLUSIVE;
    locks.op_type_ = ObTableLockOpType::IN_TRANS_COMMON_LOCK;
    locks.timeout_us_ = std::max(int64_t(1), THIS_WORKER.get_timeout_remain());
    ret = trans.start(directory_sql_proxy());
    for (int64_t i = 0; OB_SUCC(ret) && i < private_tablets.count(); ++i) {
      ret = locks.tablet_ids_.push_back(private_tablets.at(i));
    }
    if (OB_SUCC(ret) && !locks.tablet_ids_.empty()
        && OB_FAIL(query::ObInnerSQLConnectionAccess::lock_tablet(
            locks, trans.get_connection()))) {
    }
    if (OB_SUCC(ret)) {
      rootserver::ObTabletDrop tablet_drop(trans, schema_version);
      if (OB_FAIL(tablet_drop.init())) {
      } else if (OB_FAIL(tablet_drop.add_drop_tablets_arg(private_tablets))) {
      } else {
        observer::namespace_worker_prototype::PhysicalTabletMdsScope physical_mds(true);
        ret = tablet_drop.execute();
      }
    }
    if (trans.is_started()) {
      const int end_ret = trans.end(OB_SUCC(ret));
      if (OB_SUCC(ret)) { ret = end_ret; }
    }
  }
  LOG_INFO("namespace physical schema cleanup", K(ret), K(namespace_id), K(schema_version),
      "private_delete_count", private_tablets.count());
  return ret;
}
int NamespaceForkKernelPrototype::is_tablet_owned(
    uint64_t namespace_id, const ObTabletID &tablet_id, bool &owned) {
  owned = false;
  uint64_t local_tablet_id = OB_INVALID_ID;
  if (namespace_id <= 1 || namespace_id >= NamespaceObjectKey::NAMESPACE_LIMIT
      || !tablet_id.is_valid() || directory_kv_store() == nullptr) {
    return OB_INVALID_ARGUMENT;
  }
  int ret = local_object_id(namespace_id, tablet_id.id(), local_tablet_id);
  if (OB_FAIL(ret)) { return ret; }
  MetadataReadGuard access; if (access.error() != OB_SUCCESS) { return access.error(); }
  if (share::server_is_recovery_mode()) {
    uint64_t table = OB_INVALID_ID;
    return load_kv_tablet_ownership(namespace_id, local_tablet_id, owned, table);
  }
  // Ownership is exactly the owned exception row: the physical tablet id is a
  // pure function of (namespace, local tablet), so no binding is recorded.
  if (OB_FAIL(load_kv_exceptions(namespace_id))) {
  } else {
    owned = control_state().owned(namespace_id, local_tablet_id);
  }
  return ret;
}
int NamespaceForkKernelPrototype::owned_storage_tablets(
    uint64_t namespace_id,
    const ObIArray<ObTabletID> &logical_tablets,
    ObIArray<ObTabletID> &owned_tablets) {
  owned_tablets.reset();
  if (namespace_id <= 1
      || namespace_id >= NamespaceObjectKey::NAMESPACE_LIMIT
      || directory_kv_store() == nullptr) {
    return OB_INVALID_ARGUMENT;
  }
  MetadataReadGuard access;
  if (access.error() != OB_SUCCESS) { return access.error(); }
  int ret = load_kv_exceptions(namespace_id);
  for (int64_t i = 0; OB_SUCC(ret) && i < logical_tablets.count(); ++i) {
    uint64_t local_tablet_id = OB_INVALID_ID;
    if (!logical_tablets.at(i).is_valid()) {
      ret = OB_INVALID_ARGUMENT;
    } else if (OB_FAIL(local_object_id(
            namespace_id, logical_tablets.at(i).id(), local_tablet_id))) {
    } else {
      const NamespaceObjectKey local_key{namespace_id, local_tablet_id};
      if (!local_key.is_valid()) {
        ret = OB_INVALID_ARGUMENT;
      } else if (control_state().owned(namespace_id, local_tablet_id)) {
        ret = owned_tablets.push_back(ObTabletID(local_key.storage_id()));
      }
    }
  }
  return ret;
}
int NamespaceForkKernelPrototype::table_id_for_tablet(const ObTabletID &tablet, int64_t schema_version,
                                                     uint64_t &table_id) {
  table_id = OB_INVALID_ID;
  if (!is_encoded_id(tablet.id()) || directory_kv_store() == nullptr) {
    return OB_INVALID_ARGUMENT;
  }
  // System schemas retain their logical tablet/table identity in every
  // namespace. Physical encoding must not send them through user-table
  // ownership mappings, which contain no bootstrap system-table entries.
  const uint64_t logical_tablet = local_of(tablet.id());
  if (ObTabletID(logical_tablet).is_inner_tablet()) {
    table_id = logical_tablet;
    return OB_SUCCESS;
  }
  {
    std::shared_lock<std::shared_mutex> lock(tablet_table_mutex);
    const auto it = tablet_table_cache.find(tablet.id());
    if (it != tablet_table_cache.end()) { table_id = it->second; return OB_SUCCESS; }
  }
  MetadataReadGuard access; if (access.error() != OB_SUCCESS) { return access.error(); }
  // Only owned tablets have a table binding here; inherited tablets resolve
  // through their ancestor and never appear in this namespace's set.
  const uint64_t db = database_of(tablet.id());
  uint64_t local_table = 0;
  if (share::server_is_recovery_mode()) {
    bool owned = false;
    const int ret = load_kv_tablet_ownership(db, local_of(tablet.id()), owned, local_table);
    if (ret == OB_SUCCESS && owned) {
      table_id = local_table;
      remember_tablet_table(tablet.id(), table_id);
    }
    return ret;
  }
  const int ret = load_kv_exceptions(db);
  if (ret != OB_SUCCESS) { return ret; }
  if (!control_state().owned(db, local_of(tablet.id()), &local_table)) { return OB_SUCCESS; }
  table_id = local_table;
  remember_tablet_table(tablet.id(), table_id);
  return OB_SUCCESS;
}
int NamespaceForkKernelPrototype::check_ddl(const ObSimpleTableSchemaV2 &schema,
    ObMultiVersionSchemaService &schema_service, const ObISQLClient *trans) {
  if (!schema.is_user_table()) { return OB_SUCCESS; }
  if (is_encoded_id(schema.get_table_id())
      || is_encoded_id(schema.get_database_id())) { return OB_INVALID_ARGUMENT; }
  return OB_SUCCESS;
}
int NamespaceForkKernelPrototype::schedule_baseline(const ObTablet &tablet) {
  return schedule_baseline_impl(tablet, 0);
}
int NamespaceForkKernelPrototype::schedule_baseline_impl(const ObTablet &tablet, int depth) {
  if (depth >= 64) { return OB_SIZE_OVERFLOW; }
  const auto &meta = tablet.get_tablet_meta();
  if (!is_encoded_id(meta.tablet_id_.id()) || tablet.is_empty_shell()
      || !meta.fork_info_.is_valid() || meta.fork_info_.is_complete()) { return OB_SUCCESS; }
  if (directory_kv_store() == nullptr) { return OB_NOT_INIT; }
  MetadataReadGuard access; if (access.error() != OB_SUCCESS) { return access.error(); }
  int ret = OB_SUCCESS;
  TabletAccessProtection admission;
  ret = check_baseline_access(meta.tablet_id_, admission);
  if (ret == OB_ENTRY_NOT_EXIST) { return OB_SUCCESS; }
  if (ret != OB_SUCCESS) { return ret; }
  ObArenaAllocator allocator("NsForkBaseline");
  ObStorageSchema *storage_schema = nullptr;
  const uint64_t db = database_of(meta.tablet_id_.id());
  const uint64_t local = local_of(meta.tablet_id_.id());
  uint64_t table = OB_INVALID_ID;
  InstanceMetaStore::Transaction tx;
  rootserver::InstanceExceptionRecord binding;
  if (OB_FAIL(directory_kv_store()->begin(tx, directory_deadline(), true))) {
  } else {
    rootserver::InstanceNamespaceMetadata metadata(*directory_kv_store(), tx);
    ret = metadata.get_exception(db, local, binding);
    if (OB_SUCC(ret)) { table = binding.table_id; }
  }
  if (tx.is_active()) {
    const int end_ret = OB_SUCC(ret) ? directory_kv_store()->commit(tx) : directory_kv_store()->rollback(tx);
    if (OB_SUCC(ret)) { ret = end_ret; }
  }
  if (OB_FAIL(ret)) {
  } else if (OB_FAIL(tablet.load_storage_schema(allocator, storage_schema))) {
  } else if (OB_ISNULL(storage_schema)) {
    ret = OB_ERR_UNEXPECTED;
  } else {
    ObTabletForkParam param; bool ready = false;
    // The exception table owns logical-to-physical identity; the tablet owns
    // the physical schema needed by compaction.
    param.table_id_ = table;
    param.schema_version_ = storage_schema->get_schema_version();
    // Stable DAG identity only; no rootserver DDL task is created.
    param.task_id_ = meta.tablet_id_.id();
    param.source_tablet_id_ = meta.fork_info_.get_fork_src_tablet_id();
    param.dest_tablet_id_ = meta.tablet_id_; param.fork_snapshot_version_ = meta.fork_info_.get_fork_snapshot_version();
    param.data_format_version_ = DATA_CURRENT_VERSION;
    if (OB_FAIL(ObTabletForkUtil::check_satisfy_fork_condition(param, ready))) {
    } else if (!ready) {
      // A later copy cannot finish until its source has taken over its own
      // baseline. Drive that source even if its current SQL object was dropped.
      ObTabletHandle source;
      ret = ObTabletCreateDeleteHelper::check_and_get_tablet(
          ObTabletMapKey(param.source_tablet_id_), source, 0,
          ObMDSGetTabletMode::READ_WITHOUT_CHECK,
          transaction::ObTransVersion::MAX_TRANS_VERSION);
      if (OB_SUCC(ret)) { ret = schedule_baseline_impl(*source.get_obj(), depth + 1); }
    } else if (ready) {
      ret = compaction::ObScheduleDagFunc::schedule_tablet_fork_dag(param, false);
      if (ret == OB_EAGAIN || ret == OB_SIZE_OVERFLOW) { ret = OB_SUCCESS; }
      else { LOG_INFO("PROTOTYPE_V3_BASELINE_SCHEDULE", K(ret), K(param)); }
    }
  }
  // A crash needs no independent task journal: the committed tablet's incomplete fork
  // mark retries here; the existing table-store update persists the completed baseline.
  return (ret == OB_ITER_END || ret == OB_ENTRY_NOT_EXIST) ? OB_SUCCESS : ret;
}
int NamespaceForkKernelPrototype::acquire_read_view(uint64_t namespace_id,
    const std::function<int(SCN &)> &acquire, ns::NamespaceCatalogViews::Handle &view,
    const ns::NamespaceCatalogViews::Handle &previous)
{
  MetadataReadGuard publication;
  if (publication.error() != OB_SUCCESS) { return publication.error(); }
  auto *store = directory_kv_store();
  if (store == nullptr) { return OB_NOT_INIT; }
  rootserver::InstanceNamespaceDirectory directory(*store);
  return directory.acquire_read_view(namespace_id, directory_deadline(), acquire, view, previous);
}

int NamespaceForkKernelPrototype::resolve_read_tablet(
    const ObTabletID &tablet_id, ObTabletID &physical_tablet_id, int64_t &cap_scn,
    const ns::NamespaceCatalogViews::Handle &view) {
  physical_tablet_id = tablet_id;
  cap_scn = 0;
  if (!is_encoded_id(tablet_id.id())) { return OB_SUCCESS; }
  auto *store = directory_kv_store();
  if (store == nullptr) { return OB_NOT_INIT; }
  if (view) {
    if (view->entry().namespace_id != database_of(tablet_id.id())) { return OB_INVALID_ARGUMENT; }
    InstanceMetaStore::Transaction tx;
    ns::CatalogTabletSource source;
    int ret = store->begin(tx, directory_deadline(), true);
    if (OB_SUCC(ret)) {
      rootserver::InstanceNamespaceMetadata metadata(*store, tx);
      ret = metadata.find_tablet_source(view->entry().roots.directory,
          local_of(tablet_id.id()), source, cap_scn);
      if (ret == OB_ENTRY_NOT_EXIST) { ret = OB_TABLET_NOT_EXIST; }
    }
    if (tx.is_active()) {
      const int end = store->rollback(tx);
      if (OB_SUCC(ret)) { ret = end; }
    }
    ObTabletHandle origin;
    if (OB_SUCC(ret)) {
      ret = ObTabletCreateDeleteHelper::check_and_get_tablet(
          ObTabletMapKey(ObTabletID(source.physical_tablet_id)), origin, 0,
          ObMDSGetTabletMode::READ_WITHOUT_CHECK, transaction::ObTransVersion::MAX_TRANS_VERSION);
      if (ret == OB_TABLET_NOT_EXIST || ret == OB_ENTRY_NOT_EXIST
          || (OB_SUCC(ret) && origin.get_obj()->is_empty_shell())) { ret = OB_SNAPSHOT_DISCARDED; }
    }
    if (OB_SUCC(ret)) {
      ObTabletCreateDeleteMdsUserData status;
      mds::MdsWriter writer;
      mds::TwoPhaseCommitState state;
      SCN version;
      ret = origin.get_obj()->get_latest_tablet_status(status, writer, state, version);
      if (OB_SUCC(ret) && status.create_transaction_id_ != source.create_transaction_id) {
        ret = OB_SNAPSHOT_DISCARDED;
      }
    }
    rootserver::TabletVisibility visible = rootserver::TabletVisibility::ABSENT;
    const int64_t snapshot = ns::NamespaceCatalogCodec::cap_min(view->entry().snapshot, cap_scn);
    if (OB_SUCC(ret)) { ret = probe_historical_tablet(source.physical_tablet_id, snapshot, visible); }
    if (OB_SUCC(ret) && visible != rootserver::TabletVisibility::READABLE) { ret = OB_SNAPSHOT_DISCARDED; }
    if (OB_SUCC(ret)) {
      physical_tablet_id = ObTabletID(source.physical_tablet_id);
      if (physical_tablet_id != tablet_id) {
        // A local copy may have materialized after the fixed read snapshot.
        // Use it only when it represents this exact inherited view, so the
        // native iterator sees the transaction's own subsequent writes.
        ObTabletHandle local;
        const int found = ObTabletCreateDeleteHelper::check_and_get_tablet(
            ObTabletMapKey(tablet_id), local, 0, ObMDSGetTabletMode::READ_WITHOUT_CHECK,
            transaction::ObTransVersion::MAX_TRANS_VERSION);
        if (found == OB_SUCCESS && !local.get_obj()->is_empty_shell()) {
          const auto &fork = local.get_obj()->get_tablet_meta().fork_info_;
          if (fork.is_valid() && fork.get_fork_src_tablet_id() == physical_tablet_id
              && fork.get_fork_snapshot_version() == cap_scn) {
            ret = probe_historical_tablet(tablet_id.id(), view->entry().snapshot, visible);
            if (OB_SUCC(ret) && visible == rootserver::TabletVisibility::READABLE) {
              physical_tablet_id = tablet_id;
              cap_scn = 0;
            }
          }
        } else if (found != OB_SUCCESS && found != OB_TABLET_NOT_EXIST && found != OB_ENTRY_NOT_EXIST) {
          ret = found;
        }
      }
    }
    return ret;
  }
  rootserver::InstanceNamespaceDirectory directory(*store);
  uint64_t physical = 0;
  int ret = directory.resolve_read_tablet(database_of(tablet_id.id()),
      local_of(tablet_id.id()), probe_historical_tablet,
      directory_deadline(), physical, cap_scn);
  if (ret == OB_SUCCESS) { physical_tablet_id = ObTabletID(physical); }
  return ret;
}
int NamespaceForkKernelPrototype::ensure_tablet(const ObTabletID &tablet_id) {

  if (!is_encoded_id(tablet_id.id())) { return OB_SUCCESS; }
  int ret = OB_SUCCESS;
  const uint64_t db = database_of(tablet_id.id()), local = local_of(tablet_id.id());
  {
    // Reuse the tablet manager and its existing committed-status cache. A valid
    // logical birth SCN alone is not proof that physical creation has committed.
    bool exists = false;
    ret = probe_physical_tablet(tablet_id.id(), exists);
    if (OB_FAIL(ret)) { return ret; }
    if (exists) {
      if (OB_FAIL(load_kv_exceptions(db))) { return ret; }
      if (control_state().tombstoned(db, local)) { return OB_TABLET_NOT_EXIST; }
      // Committed physical presence needs no materialization. Bootstrap and
      // DDL create their own tablets before publishing directory entries;
      // requiring owned here would turn an existing object into a request to
      // inherit it. Their publication remains responsible for registration.
      return OB_SUCCESS;
    }
  } // Do not pin an uncommitted tablet while waiting for its creator's row lock.
  LOG_INFO("PROTOTYPE_V4_DIRECTORY_SLOW_PATH", K(tablet_id));
  if (directory_kv_store() == nullptr) { return OB_NOT_INIT; }
  MetadataReadGuard access; if (access.error() != OB_SUCCESS) { return access.error(); }
  struct MaterializeItem {
    ns::CatalogTabletSource source;
    uint64_t local_tablet = 0;
    int64_t cap = 0;
    std::string definition;
  };
  std::vector<MaterializeItem> items;
  Roots root;
  const char *failure_stage = "start";
  auto materialize_step = [&](const char *stage, int step_ret) {
    failure_stage = stage;
    return step_ret;
  };
  InstanceMetaStore::Transaction directory_tx;
  ObMySQLTransaction trans;
  rootserver::InstanceNamespaceMetadata metadata(*directory_kv_store(), directory_tx);
  bool already = false;
  const int64_t deadline = directory_deadline();
  // CREATE MDS, mappings and owned rows share the connection's native
  // transaction. Its namespace row lock serializes the entire binding unit.
  if (OB_FAIL(materialize_step("transaction", trans.start(directory_sql_proxy())))) {
  } else {
    ret = query::ObInnerSQLConnectionAccess::with_native_transaction(
        trans.get_connection(), [&](transaction::ObTxDesc &native) -> int {
      if (OB_FAIL(materialize_step("directory_transaction",
          directory_kv_store()->attach(directory_tx, native, deadline)))) {
      } else {
        rootserver::InstanceNamespaceRecord record;
        if (OB_FAIL(materialize_step("roots", metadata.get_namespace(db, record, true)))) {
        } else if (record.roots.state != 0) {
          ret = OB_OP_NOT_ALLOW;
        } else {
          root = record.roots;
        }
      }
      ns::CatalogTabletSource requested;
      int64_t requested_cap = 0;
      if (OB_SUCC(ret)) {
        failure_stage = "recheck";
        ret = metadata.find_tablet_source(root.directory, local, requested, requested_cap);
        if (ret == OB_ENTRY_NOT_EXIST) { ret = OB_TABLET_NOT_EXIST; }
        if (OB_SUCC(ret)) { already = requested.physical_tablet_id == tablet_id.id(); }
      }
      if (OB_SUCC(ret) && !already) {
        failure_stage = "source_definition";
        const uint64_t binding[] = {requested.data_tablet_id,
            requested.lob_meta_tablet_id, requested.lob_piece_tablet_id};
        bool requested_found = false;
        for (uint64_t logical : binding) {
          if (OB_FAIL(ret)) { break; }
          if (logical == 0) { continue; }
          MaterializeItem item;
          item.local_tablet = logical;
          requested_found |= logical == local;
          if (OB_FAIL(metadata.find_tablet_source(root.directory, logical, item.source, item.cap))) {
          } else if (item.source.data_tablet_id != requested.data_tablet_id
              || item.source.lob_meta_tablet_id != requested.lob_meta_tablet_id
              || item.source.lob_piece_tablet_id != requested.lob_piece_tablet_id
              || item.source.physical_tablet_id == encoded(db, logical)
              || item.cap <= 0 || item.cap > root.snapshot) {
            ret = OB_STATE_NOT_MATCH;
          } else if (OB_FAIL(metadata.read_table_definition(
                         root.catalog, item.source.table_id, item.definition))) {
          } else {
            // The immutable entry identifies an exact physical incarnation.
            // Never substitute a newly created object with the same tablet ID.
            ObTabletHandle source_handle;
            ret = ObTabletCreateDeleteHelper::check_and_get_tablet(
                ObTabletMapKey(ObTabletID(item.source.physical_tablet_id)), source_handle, 0,
                ObMDSGetTabletMode::READ_WITHOUT_CHECK, transaction::ObTransVersion::MAX_TRANS_VERSION);
            if (OB_SUCC(ret) && source_handle.get_obj()->is_empty_shell()) { ret = OB_SNAPSHOT_DISCARDED; }
            if (OB_SUCC(ret)) {
              ObTabletCreateDeleteMdsUserData status;
              mds::MdsWriter writer;
              mds::TwoPhaseCommitState state;
              SCN version;
              ret = source_handle.get_obj()->get_latest_tablet_status(status, writer, state, version);
              if (OB_SUCC(ret) && status.create_transaction_id_ != item.source.create_transaction_id) {
                ret = OB_SNAPSHOT_DISCARDED;
              }
            }
            rootserver::TabletVisibility visibility = rootserver::TabletVisibility::ABSENT;
            if (OB_SUCC(ret)) { ret = probe_historical_tablet(item.source.physical_tablet_id, item.cap, visibility); }
            if (OB_SUCC(ret) && visibility != rootserver::TabletVisibility::READABLE) { ret = OB_SNAPSHOT_DISCARDED; }
            if (OB_SUCC(ret)) { items.push_back(std::move(item)); }
          }
        }
        if (OB_SUCC(ret) && !requested_found) { ret = OB_STATE_NOT_MATCH; }
        auto *freeze = share::server_service<ObFreezeInfoMgr>();
        if (OB_SUCC(ret) && !freeze) {
          ret = OB_STATE_NOT_MATCH;
        }
        for (const auto &item : items) {
          if (OB_FAIL(ret)) { break; }
          failure_stage = "snapshot_pin";
          // The resolved cap is exactly the fork snapshot id of the chain hop
          // below the source, so its snapshot row is a point query away.
          Roots inherited;
          rootserver::InstanceNamespacePin pin;
          if (OB_FAIL(metadata.get_snapshot(uint64_t(item.cap), inherited))) {
          } else if (OB_FAIL(metadata.get_pin(uint64_t(item.cap), pin))) {
          } else {
            ObStorageSnapshotInfo reserved;
            if (pin.schema_version != inherited.schema_version) {
              ret = OB_STATE_NOT_MATCH;
            } else if (OB_FAIL(freeze->get_min_reserved_snapshot(
                           ObTabletID(item.source.physical_tablet_id), item.cap, reserved))) {
            } else if (reserved.snapshot_ > item.cap) {
              ret = OB_SNAPSHOT_DISCARDED;
            }
          }
        }
      }
      return ret;
    });
  }
  if (OB_SUCC(ret) && !already) {
    failure_stage = "tablet_create";
    rootserver::ObTabletCreator creator(SCN::min_scn(), trans);
    obcall::ObBatchCreateTabletArg batch;
    ret = batch.init_create_tablet(SCN::min_scn(), false);
    ObArray<ObTabletID> ids;
    ObArray<int64_t> schema_indexes;
    ObArray<int64_t> logical_birth;
    ObArray<ObForkTabletInfo> fork_infos;
    ObArray<ObTabletTablePair> mappings;
    ObArray<ObTabletID> source_ids;
    ObArray<int64_t> source_snapshot_versions;
    for (const auto &item : items) {
      if (OB_FAIL(ret)) { break; }
      ObForkTabletInfo fork;
      fork.set_fork_snapshot_version(item.cap);
      fork.set_fork_src_tablet_id(ObTabletID(item.source.physical_tablet_id));
      const ObTabletID destination(encoded(db, item.local_tablet));
      rootserver::TableCreationDescriptor definition;
      int64_t schema_index = -1;
      if (OB_FAIL(definition.decode(item.definition))) {
      } else if (definition.schema().get_table_id() != item.source.table_id) {
        ret = OB_CHECKSUM_ERROR;
      } else if (OB_FAIL(definition.append_to(batch, schema_index))) {
      } else if (OB_FAIL(ids.push_back(destination)) || OB_FAIL(schema_indexes.push_back(schema_index))
          || OB_FAIL(logical_birth.push_back(item.cap))
          || OB_FAIL(fork_infos.push_back(fork))
          || OB_FAIL(source_ids.push_back(ObTabletID(item.source.physical_tablet_id)))
          || OB_FAIL(source_snapshot_versions.push_back(item.cap))
          || OB_FAIL(mappings.push_back(ObTabletTablePair(destination, item.source.table_id)))) {
      }
    }
    const ObTabletID data_tablet(encoded(db, items.front().local_tablet));
    auto copy_physical_sequences = [&]() {
      observer::namespace_worker_prototype::PhysicalTabletMdsScope physical_mds(true);
      return ObTabletAutoincrementService::get_instance().copy_sequences_for_fork(
          source_ids, ids, source_snapshot_versions, trans);
    };
    obcall::ObCreateTabletInfo info;
    if (OB_FAIL(ret)) {
    } else if (OB_FAIL(info.init(ids, data_tablet, schema_indexes, false, logical_birth, fork_infos))) {
    } else if (OB_FAIL(batch.tablets_.push_back(info))) {
    } else if (OB_FAIL(creator.init(false))) {
    } else if (OB_FAIL(creator.add_create_tablet_batch(batch))) {
    } else if (FALSE_IT(batch.reset())) {
    } else if (FALSE_IT(creator.set_materialization_for_prototype())) {
    } else if (OB_FAIL(creator.execute())) {
    } else if (OB_FAIL(copy_physical_sequences())) {
    } else if (OB_FAIL(ObTabletMappingTableOperator::batch_update(trans, mappings))) {
    } else {
      // The physical binding unit is staged; owned rows join this same
      // transaction before it can commit.
      DEBUG_SYNC(AFTER_UPDATE_TABLET_TO_LS);
      ret = THIS_WORKER.check_status();
      LOG_INFO("PROTOTYPE_V2_STORAGE_MATERIALIZE", K(tablet_id), "tablet_count", items.size(),
          "input_snapshot", items.front().cap, "namespace_snapshot", root.snapshot,
          "entry_layer", "TabletAccess", K(ret));
    }
    batch.reset();
  }
  if (OB_SUCC(ret) && !already) {
    failure_stage = "exceptions";
    ret = query::ObInnerSQLConnectionAccess::with_native_transaction(
        trans.get_connection(), [&](transaction::ObTxDesc &native) -> int {
      ns::CatalogChanges sources;
      for (const auto &item : items) {
        if (OB_FAIL(ret)) { break; }
        ret = metadata.put_exception({db, item.local_tablet,
            local_of(item.source.table_id), 0, 0});
        ns::CatalogTabletSource source;
        int64_t cap = 0;
        if (OB_SUCC(ret)) { ret = metadata.find_tablet_source(root.directory, item.local_tablet, source, cap); }
        source.physical_tablet_id = encoded(db, item.local_tablet);
        source.create_transaction_id = native.get_tx_id().get_id();
        if (OB_SUCC(ret) && (!source.is_valid() || source.table_id != item.source.table_id)) {
          ret = OB_STATE_NOT_MATCH;
        }
        if (OB_SUCC(ret)) {
          sources[ns::NamespaceCatalogCodec::object_key(item.local_tablet)] = {
              {ns::NamespaceCatalogCodec::encode_source(source), 0}, false};
        }
      }
      if (OB_SUCC(ret)) { ret = metadata.stage_catalog_delta(db, root.schema_version, root.schema_version, {}, sources); }
      return ret;
    });
  }
  // End explicitly even for KV failures: the SQL wrapper's destructor cannot
  // infer those errors. Keep KV reader/GC protection until the owner finishes.
  if (trans.is_started()) {
    if (OB_SUCC(ret)) { failure_stage = "commit"; }
    const int end = trans.end(ret == OB_SUCCESS);
    if (ret == OB_SUCCESS) { ret = end; }
  }
  if (directory_tx.is_active()) {
    const int detach_ret = directory_kv_store()->detach(directory_tx);
    if (ret == OB_SUCCESS) { ret = detach_ret; }
  }
  if (ret == OB_SUCCESS && !already) {
    for (const auto &item : items) {
      control_state().apply_owned(db, item.local_tablet, local_of(item.source.table_id));
    }
  } else {
    // Commit may have succeeded even when its response was lost. A later
    // access must reload authoritative rows instead of repairing absence.
    control_state().drop_exceptions(db);
  }
  if (ret != OB_SUCCESS) {
    const MaterializeItem *item = items.empty() ? nullptr : &items.front();
    fprintf(stderr,
        "PROTOTYPE_NAMESPACE_MATERIALIZE_FAILED ret=%d stage=%s namespace=%llu tablet=%llu "
        "snapshot=%ld snapshot_ref=%llu items=%zu cap=%ld source=%llu local=%llu\n",
        ret, failure_stage, static_cast<unsigned long long>(db),
        static_cast<unsigned long long>(tablet_id.id()),
        static_cast<long>(root.snapshot),
        static_cast<unsigned long long>(root.snapshot_ref), items.size(),
        static_cast<long>(item ? item->cap : 0),
        static_cast<unsigned long long>(item ? item->source.physical_tablet_id : 0),
        static_cast<unsigned long long>(item ? item->local_tablet : 0));
    LOG_WARN("prototype storage materialization failed", K(ret), K(tablet_id));
  }
  return ret;
}
}
}
