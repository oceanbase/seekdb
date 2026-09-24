// PROTOTYPE: real immutable B+ tree pages stored in engine tables, one-engine transactions.
// Fixed two-integer-column schemas; mode 6 adds explicit metadata page reclamation.
#define USING_LOG_PREFIX STORAGE
#include "query/session/ob_inner_sql_connection_access.h"
#include "rootserver/fork_table/namespace_fork_kernel_prototype.h"
#include "namespace/catalog.h"
#include "namespace/namespace.h"
#include "observer/namespace_worker_protocol_prototype.h"
#include "rootserver/ob_tablet_creator.h"
#include "rootserver/ob_tablet_drop.h"
#include "rootserver/ddl_task/ob_ddl_task_util.h"
#include "common/mysqlclient/ob_mysql_proxy.h"
#include "common/mysqlclient/ob_mysql_transaction.h"
#include "share/ob_server_struct.h"
#include "share/ob_snapshot_table_proxy.h"
#include "share/ob_debug_sync.h"
#include "share/tablet/ob_tablet_mapping_operator.h"
#include "share/schema/ob_multi_version_schema_service.h"
#include "share/rc/ob_server_runtime.h"
#include "storage/compaction/ob_freeze_info_mgr.h"
#include "storage/compaction/ob_schedule_dag_func.h"
#include "storage/ddl/ob_tablet_fork_task.h"
#include "storage/ob_storage_schema.h"
#include "storage/ob_tablet_autoincrement_service.h"
#include "storage/tablet/ob_tablet_create_delete_helper.h"
#include "storage/tablelock/ob_lock_inner_connection_util.h"
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
using Codec = ::oceanbase::ns::NamespaceCatalogCodec;
using Ref = ::oceanbase::ns::CatalogPageRef;
using Value = ::oceanbase::ns::CatalogValue;
using Node = ::oceanbase::ns::CatalogNode;
using Roots = ::oceanbase::ns::CatalogRoots;
const char *ROOTS = "__fork_proto_meta.roots";
const char *PAGES = "__fork_proto_meta.pages";
const char *NAMESPACES = "__fork_proto_meta.namespaces";
const char *SNAPSHOTS = "__fork_proto_meta.snapshots";
const char *EXCEPTIONS = "__fork_proto_meta.exceptions";
// Only the native source DROP uses this scoped internal DDL capability.
std::atomic<const ObISQLClient *> source_drop_trans{nullptr};
// ponytail: one global count, so DROP can wait for unrelated long scans/DAGs.
// No per-namespace registry/cache; use worker-local draining for production isolation.
std::atomic<int64_t> active_accesses{0};
// check_table_access runs on every storage scan/DML open; a roots() SQL per
// open dominated branch-worker cold schema refresh. The namespace registry is
// only mutated by begin/finish_namespace_drop in this process, so a LIVE entry
// stays valid until locally invalidated. Readers still register active_accesses
// before consulting the cache, so drain_access keeps covering the close window.
std::shared_mutex namespace_state_mutex;
std::unordered_map<uint64_t, int64_t> namespace_state_cache;
bool cached_namespace_state(uint64_t id, int64_t &state) {
  std::shared_lock<std::shared_mutex> lock(namespace_state_mutex);
  const auto it = namespace_state_cache.find(id);
  if (it == namespace_state_cache.end()) { return false; }
  state = it->second;
  return true;
}
void remember_namespace_state(uint64_t id, int64_t state) {
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
// ponytail: manual GC excludes all metadata operations while marking/sweeping.
// One lock and nesting depth per thread, no resident page/reader registry. Use
// versioned reader epochs if measurements justify concurrent marking later.
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
struct SchemaHolder {
  ObArenaAllocator allocator;
  ObTableSchema schema;
  SchemaHolder() : allocator("ForkProtoSchema"), schema(&allocator) {}
};
std::mutex schema_mutex;
// Immutable schemas remain alive for existing schema guards and cached plans.
// ponytail: process-lifetime schema cache; bounded by tables opened in this disposable instance.
std::map<uint64_t, std::unique_ptr<SchemaHolder>> schemas;
struct DatabaseHolder {
  ObArenaAllocator allocator;
  ObDatabaseSchema schema;
  ObSimpleDatabaseSchema simple;
  DatabaseHolder() : allocator("ForkProtoDB"), schema(&allocator), simple(&allocator) {}
};
// Schemas stay alive for old guards/plans; storage admission rejects deleted namespaces.
std::map<uint64_t, std::unique_ptr<DatabaseHolder>> database_schemas;

// Native namespace workers load their only schema authority from their own
// all_* tables.  This is an architecture-wide mode, not a property of the
// process currently executing a hook: namespace 1 enrollment runs in the
// shared process and must still avoid constructing the legacy schema catalog.
bool native_namespace_schema_authority()
{
  return true;
}

int64_t cap_min(int64_t a, int64_t b) { return Codec::cap_min(a, b); }
uint64_t encoded(uint64_t db, uint64_t local) {
  return NamespaceObjectKey{db, local}.storage_id();
}
uint64_t database_of(uint64_t id) { return NamespaceObjectKey::encoded_namespace(id); }
uint64_t local_of(uint64_t id) { return NamespaceObjectKey::local_part(id); }
std::string key_of(uint64_t id) {
  return Codec::object_key(id);
}
std::string hex(const std::string &s) {
  const char *digits = "0123456789abcdef";
  std::string out; out.reserve(s.size() * 2);
  for (unsigned char c : s) { out += digits[c >> 4]; out += digits[c & 15]; }
  return out;
}
std::string entry(uint64_t schema_object, uint64_t local_table, uint64_t local_tablet,
                  uint64_t bound_tablet = 0) {
  return Codec::encode_entry(schema_object, local_table, local_tablet, bound_tablet);
}
bool entry(const std::string &s, uint64_t &object, uint64_t &table, uint64_t &tablet, uint64_t &bound) {
  return Codec::decode_entry(s, object, table, tablet, bound);
}
int write_sql(ObISQLClient &sql, const ObSqlString &statement) {
  int64_t affected = 0; return sql.write(statement.ptr(), affected);
}
int blob(ObISQLClient &sql, uint64_t id, std::string &out) {
  int ret = OB_SUCCESS;
  ObSqlString q; ObMySQLProxy::MySQLResult res; sqlclient::ObMySQLResult *r = nullptr;
  ObString value;
  if (OB_FAIL(q.assign_fmt("SELECT payload FROM %s WHERE id=%lu", PAGES, id))) {
  } else if (OB_FAIL(sql.read(res, q.ptr()))) {
  } else if (OB_ISNULL(r = res.get_result())) { ret = OB_ERR_UNEXPECTED;
  } else if (OB_FAIL(r->next())) {
  } else if (OB_FAIL(r->get_varchar(0L, value))) {
  } else {
    out.assign(value.ptr(), value.length());
    if (murmurhash(out.data(), static_cast<int32_t>(out.size()), 0) != id) {
      ret = OB_CHECKSUM_ERROR;
    }
  }
  return ret;
}
int save_blob(ObISQLClient &sql, const std::string &data, uint64_t &id) {
  int ret = OB_SUCCESS;
  // ob_crc64() is backed by the CPU CRC32C instruction and only provides 32
  // effective bits.  A few thousand immutable metadata pages are enough to
  // make collisions observable.  The page key needs the full 64-bit hash.
  id = murmurhash(data.data(), static_cast<int32_t>(data.size()), 0);
  std::string existing;
  if (id == 0 || data.size() > 60000) { ret = OB_SIZE_OVERFLOW;
  } else if (OB_FAIL(blob(sql, id, existing))) {
    if (ret == OB_ITER_END) {
      ObSqlString q;
      if (OB_FAIL(q.assign_fmt("INSERT INTO %s VALUES(%lu,UNHEX('%s'))", PAGES, id, hex(data).c_str()))) {
      } else { ret = write_sql(sql, q); }
    }
  } else if (existing != data) { ret = OB_CHECKSUM_ERROR; }
  return ret;
}
// Exception-table model: a namespace row stores only its immutable parent link
// (parent_namespace, fork_cap), and the exceptions table records just the
// tablets this namespace physically owns or has explicitly dropped. Everything
// else is resolved by probing deterministic physical ids along the parent
// chain; encode(1, local) == local, so namespace 1 terminates every walk with
// its raw tablet ids.
// Parent links never change after fork commit and namespace ids are never
// reused; the cache entry is removed when its tombstone is pruned.
ns::NamespaceControlState &control_state() {
  return ns::namespace_registry().control_state();
}
int namespace_chain_link(ObISQLClient &sql, uint64_t ns, uint64_t &parent, int64_t &fork_cap) {
  if (control_state().chain_link(ns, parent, fork_cap)) { return OB_SUCCESS; }
  ObSqlString q; ObMySQLProxy::MySQLResult res; sqlclient::ObMySQLResult *r = nullptr;
  int ret = q.assign_fmt(
      "SELECT parent_namespace,fork_cap FROM %s WHERE namespace_id=%lu",
      NAMESPACES, ns);
  if (OB_FAIL(ret)) {
  } else if (OB_FAIL(sql.read(res, q.ptr()))) {
  } else if (OB_ISNULL(r = res.get_result())) { ret = OB_ERR_UNEXPECTED;
  } else if (OB_FAIL(r->next())) {
  } else if (OB_FAIL(r->get_uint(0L, parent))) {
  } else {
    uint64_t cap_value = 0;
    if (OB_FAIL(r->get_uint(1L, cap_value))) {
    } else {
      fork_cap = static_cast<int64_t>(cap_value);
      control_state().remember_chain_link(ns, parent, fork_cap);
    }
  }
  return ret;
}
class SqlExceptionLoader final : public ns::IExceptionLoader {
public:
  explicit SqlExceptionLoader(ObISQLClient &sql) : sql_(sql) {}
  int load(uint64_t ns, IRowSink &sink) override {
    ObSqlString q; ObMySQLProxy::MySQLResult res; sqlclient::ObMySQLResult *r = nullptr;
    int ret = q.assign_fmt(
        "SELECT tablet_id,table_id,kind FROM %s WHERE namespace_id=%lu", EXCEPTIONS, ns);
    if (OB_FAIL(ret)) {
    } else if (OB_FAIL(sql_.read(res, q.ptr()))) {
    } else if (OB_ISNULL(r = res.get_result())) { ret = OB_ERR_UNEXPECTED;
    } else {
      while (OB_SUCC(ret = r->next())) {
        ns::NamespaceExceptionRow row;
        if (OB_FAIL(r->get_uint(0L, row.tablet)) || OB_FAIL(r->get_uint(1L, row.table))
            || OB_FAIL(r->get_int(2L, row.kind))) {
          break;
        }
        sink.add(row);
      }
      if (ret == OB_ITER_END) { ret = OB_SUCCESS; }
    }
    return ret;
  }
private:
  ObISQLClient &sql_;
};
int load_exceptions(ObISQLClient &sql, uint64_t ns) {
  SqlExceptionLoader loader(sql);
  return control_state().load_exceptions(ns, loader);
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
// Find the nearest ancestor that physically holds this tablet. The cap
// accumulates the fork snapshot of every crossed hop, so a hit at any depth
// yields exactly the view the namespace had at its own fork.
int resolve_inherited_tablet(ObISQLClient &sql, uint64_t ns, uint64_t local,
                             uint64_t &physical, int64_t &cap_scn) {
  int ret = OB_SUCCESS;
  uint64_t cur = ns;
  int64_t cap = 0;
  bool found = false;
  for (int depth = 0; OB_SUCC(ret) && !found && depth < 64; ++depth) {
    uint64_t parent = 0; int64_t fork_cap = 0;
    ret = namespace_chain_link(sql, cur, parent, fork_cap);
    if (ret == OB_ITER_END) { ret = OB_SUCCESS; break; }
    if (OB_FAIL(ret) || parent == 0) { break; }
    cap = cap_min(cap, fork_cap);
    const uint64_t candidate = encoded(parent, local);
    bool exists = false;
    if (OB_FAIL(probe_physical_tablet(candidate, exists))) { break; }
    if (exists) {
      physical = candidate; cap_scn = cap; found = true;
    } else {
      cur = parent;
    }
  }
  if (OB_SUCC(ret) && !found) { ret = OB_TABLET_NOT_EXIST; }
  return ret;
}
class SqlCatalogPageStore final : public ::oceanbase::ns::ICatalogPageStore {
public:
  explicit SqlCatalogPageStore(ObISQLClient &sql) : sql_(sql) {}
  int read(uint64_t page, std::string &data) override { return blob(sql_, page, data); }
  int write(const std::string &data, uint64_t &page) override {
    return save_blob(sql_, data, page);
  }
private:
  ObISQLClient &sql_;
};

int catalog_tree_error(const ::oceanbase::ns::CatalogTreeResult &result) {
  using ::oceanbase::ns::CatalogTreeError;
  switch (result.error) {
    case CatalogTreeError::NONE: return OB_SUCCESS;
    case CatalogTreeError::NOT_FOUND: return OB_ENTRY_NOT_EXIST;
    case CatalogTreeError::CORRUPT: return OB_CHECKSUM_ERROR;
    case CatalogTreeError::TOO_DEEP: return OB_SIZE_OVERFLOW;
    case CatalogTreeError::STORE: return result.store_error;
  }
  return OB_ERR_UNEXPECTED;
}
int read_node(ObISQLClient &sql, Ref ref, Node &node) {
  SqlCatalogPageStore pages(sql);
  return catalog_tree_error(::oceanbase::ns::NamespaceCatalogTree(pages).read_node(ref, node));
}
int find(ObISQLClient &sql, Ref ref, const std::string &key, Value &value) {
  SqlCatalogPageStore pages(sql);
  return catalog_tree_error(::oceanbase::ns::NamespaceCatalogTree(pages).find(ref, key, value));
}
int put(ObISQLClient &sql, Ref root, const std::string &key, Value value, Ref &next) {
  SqlCatalogPageStore pages(sql);
  return catalog_tree_error(::oceanbase::ns::NamespaceCatalogTree(pages).put(
      root, key, std::move(value), next));
}
int remove_key(ObISQLClient &sql, Ref root, const std::string &key, Ref &next) {
  SqlCatalogPageStore pages(sql);
  return catalog_tree_error(::oceanbase::ns::NamespaceCatalogTree(pages).remove(root, key, next));
}
int roots(ObISQLClient &sql, uint64_t db, Roots &root, bool lock = false, bool inactive = false) {
  int ret = OB_SUCCESS; ObSqlString q; ObMySQLProxy::MySQLResult res;
  sqlclient::ObMySQLResult *r = nullptr;
  if (OB_FAIL(q.assign_fmt("SELECT source_id,catalog_page,catalog_cap,directory_page,directory_cap,snapshot,schema_version,snapshot_ref,state,active_schema_changes,pending_schema_version FROM %s WHERE namespace_id=%lu%s%s",
      NAMESPACES, db,
      !inactive ? " AND state=0" : "", lock ? " FOR UPDATE" : ""))) {
  } else if (OB_FAIL(sql.read(res, q.ptr()))) {
  } else if (OB_ISNULL(r = res.get_result())) { ret = OB_ERR_UNEXPECTED;
  } else if (OB_FAIL(r->next())) {
  } else if (OB_FAIL(r->get_uint(0L, root.source)) || OB_FAIL(r->get_uint(1L, root.catalog.page))
      || OB_FAIL(r->get_int(2L, root.catalog.cap)) || OB_FAIL(r->get_uint(3L, root.directory.page))
      || OB_FAIL(r->get_int(4L, root.directory.cap)) || OB_FAIL(r->get_int(5L, root.snapshot))
      || OB_FAIL(r->get_int(6L, root.schema_version))
      || OB_FAIL(r->get_uint(7L, root.snapshot_ref))
          || OB_FAIL(r->get_int(8L, root.state))
          || OB_FAIL(r->get_int(9L, root.active_schema_changes))
          || OB_FAIL(r->get_int(10L, root.pending_schema_version))) {
  }
  return ret;
}
int snapshot_roots(ObISQLClient &sql, uint64_t id, Roots &root, bool lock = false) {
  ObSqlString q; ObMySQLProxy::MySQLResult res; sqlclient::ObMySQLResult *r = nullptr;
  int ret = q.assign_fmt("SELECT catalog_page,directory_page,snapshot,schema_version,catalog_cap,directory_cap,parent_ref,ref_count FROM %s WHERE snapshot_id=%lu%s",
      SNAPSHOTS, id, lock ? " FOR UPDATE" : "");
  if (OB_FAIL(ret)) {
  } else if (OB_FAIL(sql.read(res, q.ptr()))) {
  } else if (OB_ISNULL(r = res.get_result())) { ret = OB_ERR_UNEXPECTED;
  } else if (OB_FAIL(r->next())) {
  } else if (OB_FAIL(r->get_uint(0L, root.catalog.page)) || OB_FAIL(r->get_uint(1L, root.directory.page))
      || OB_FAIL(r->get_int(2L, root.snapshot)) || OB_FAIL(r->get_int(3L, root.schema_version))) {
  } else {
    root.catalog.cap = root.directory.cap = root.snapshot; root.snapshot_ref = id;
    if (OB_FAIL(r->get_int(4L, root.catalog.cap)) || OB_FAIL(r->get_int(5L, root.directory.cap))
        || OB_FAIL(r->get_uint(6L, root.parent_ref)) || OB_FAIL(r->get_int(7L, root.ref_count))) {
    } else if (!root.valid_snapshot(id)) {
      ret = OB_CHECKSUM_ERROR;
    }
  }
  return ret;
}
int save_roots(ObISQLClient &sql, uint64_t db, const Roots &root) {
  ObSqlString q;
  int ret = q.assign_fmt("UPDATE %s SET source_id=%lu,catalog_page=%lu,catalog_cap=%ld,directory_page=%lu,directory_cap=%ld,snapshot=%ld,schema_version=%ld WHERE namespace_id=%lu",
      NAMESPACES, root.source, root.catalog.page, root.catalog.cap, root.directory.page,
      root.directory.cap, root.snapshot, root.schema_version, db);
  return ret == OB_SUCCESS ? write_sql(sql, q) : ret;
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

int corresponding_tablet_id(const ObTableSchema &source_schema,
                            uint64_t source_local_tablet_id,
                            const ObTableSchema &target_schema,
                            uint64_t &target_tablet_id)
{
  ObArray<ObTabletID> source_tablets;
  ObArray<ObTabletID> target_tablets;
  int ret = schema_tablet_ids(source_schema, source_tablets);
  if (OB_SUCC(ret)) {
    ret = schema_tablet_ids(target_schema, target_tablets);
  }
  if (OB_SUCC(ret) && (source_tablets.empty()
      || source_tablets.count() != target_tablets.count())) {
    ret = OB_STATE_NOT_MATCH;
  }
  int64_t tablet_index = OB_INVALID_INDEX;
  for (int64_t i = 0; OB_SUCC(ret) && i < source_tablets.count(); ++i) {
    if (local_of(source_tablets.at(i).id()) == source_local_tablet_id) {
      if (tablet_index != OB_INVALID_INDEX) {
        ret = OB_STATE_NOT_MATCH;
      } else {
        tablet_index = i;
      }
    }
  }
  if (OB_SUCC(ret) && tablet_index == OB_INVALID_INDEX) {
    ret = OB_ENTRY_NOT_EXIST;
  } else if (OB_SUCC(ret)) {
    target_tablet_id = target_tablets.at(tablet_index).id();
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

bool legacy_catalog_supported(const ObTableSchema &s) {
  if (s.is_sys_table() || s.is_aux_lob_table()) {
    return s.get_tablet_id().is_valid();
  }
  if (s.is_index_table()) {
    const ObIndexType type = s.get_index_type();
    const bool ordinary = type == INDEX_TYPE_NORMAL_LOCAL || type == INDEX_TYPE_UNIQUE_LOCAL
        || type == INDEX_TYPE_NORMAL_GLOBAL_LOCAL_STORAGE
        || type == INDEX_TYPE_UNIQUE_GLOBAL_LOCAL_STORAGE;
    return s.get_table_type() == USER_INDEX && ordinary && s.is_storage_local_index_table()
        && !s.is_partitioned_table() && s.get_data_table_id() != 0
        && s.get_data_table_id() != OB_INVALID_ID && s.get_tablet_id().is_valid()
        && s.get_column_count() > 0 && s.get_rowkey_column_num() > 0;
  }
  for (int64_t i = 0; i < s.get_foreign_key_infos().count(); ++i) {
    const auto &fk = s.get_foreign_key_infos().at(i);
    if (fk.is_parent_table_mock_ || fk.fk_ref_type_ != FK_REF_TYPE_PRIMARY_KEY) { return false; }
  }
  for (int64_t i = 0; i < s.get_simple_index_infos().count(); ++i) {
    const auto &index = s.get_simple_index_infos().at(i);
    if (index.table_type_ != USER_INDEX
        || (index.index_type_ != INDEX_TYPE_NORMAL_LOCAL
            && index.index_type_ != INDEX_TYPE_UNIQUE_LOCAL
            && index.index_type_ != INDEX_TYPE_NORMAL_GLOBAL_LOCAL_STORAGE
            && index.index_type_ != INDEX_TYPE_UNIQUE_GLOBAL_LOCAL_STORAGE)) {
      return false;
    }
  }
  const bool has_lob_meta = s.get_aux_lob_meta_tid() != OB_INVALID_ID;
  const bool has_lob_piece = s.get_aux_lob_piece_tid() != OB_INVALID_ID;
  return s.get_table_type() == USER_TABLE && !s.is_partitioned_table()
      && s.get_trigger_list().empty() && !s.has_generated_column() && s.get_autoinc_column_id() == 0
      && s.get_column_count() > 0 && s.get_rowkey_column_num() > 0
      && has_lob_meta == has_lob_piece;
}
std::vector<ObAuxTableMetaInfo> index_infos(const ObTableSchema &schema) {
  std::vector<ObAuxTableMetaInfo> indexes;
  indexes.reserve(schema.get_simple_index_infos().count());
  for (int64_t i = 0; i < schema.get_simple_index_infos().count(); ++i) {
    indexes.push_back(schema.get_simple_index_infos().at(i));
  }
  return indexes;
}
int schema_from_value(uint64_t db, const Value &value, const ObTableSchema *&schema) {
  uint64_t object = 0, table = 0, tablet = 0, bound = 0;
  if (!entry(value.data, object, table, tablet, bound)) { return OB_CHECKSUM_ERROR; }
  const uint64_t id = encoded(db, table);
  { std::lock_guard<std::mutex> lock(schema_mutex);
    auto it = schemas.find(id); if (it != schemas.end()) { schema = &it->second->schema; return OB_SUCCESS; } }
  std::string data; int ret = blob(*GCTX.sql_proxy_, object, data);
  std::unique_ptr<SchemaHolder> holder(new SchemaHolder());
  ObTableSchema *copy = &holder->schema; int64_t pos = 0;
  if (ret != OB_SUCCESS) { return ret; }
  if ((ret = copy->deserialize(data.data(), data.size(), pos)) != OB_SUCCESS) { return ret; }
  if (pos != int64_t(data.size()) || !legacy_catalog_supported(*copy)) {
    return OB_NOT_SUPPORTED;
  }
  if (!copy->is_sys_table()) {
    copy->set_database_id(encoded(db, copy->get_database_id()));
    copy->set_table_id(id);
    if (copy->get_data_table_id() != 0 && copy->get_data_table_id() != OB_INVALID_ID) {
      copy->set_data_table_id(encoded(db, copy->get_data_table_id()));
    }
    if (copy->get_association_table_id() != 0 && copy->get_association_table_id() != OB_INVALID_ID) {
      copy->set_association_table_id(encoded(db, copy->get_association_table_id()));
    }
    if (copy->get_aux_lob_meta_tid() != OB_INVALID_ID) {
      copy->set_aux_lob_meta_tid(encoded(db, copy->get_aux_lob_meta_tid()));
    }
    if (copy->get_aux_lob_piece_tid() != OB_INVALID_ID) {
      copy->set_aux_lob_piece_tid(encoded(db, copy->get_aux_lob_piece_tid()));
    }
    std::vector<ObAuxTableMetaInfo> indexes = index_infos(*copy);
    copy->reset_simple_index_infos();
    for (auto &index : indexes) {
      index.table_id_ = encoded(db, index.table_id_);
      if (OB_FAIL(copy->add_simple_index_info(index))) { return ret; }
    }
    for (int64_t i = 0; i < copy->get_foreign_key_infos().count(); ++i) {
      auto &fk = copy->get_foreign_key_infos().at(i);
      fk.table_id_ = id;
      fk.child_table_id_ = encoded(db, fk.child_table_id_);
      fk.parent_table_id_ = encoded(db, fk.parent_table_id_);
      if (fk.ref_cst_id_ != OB_INVALID_ID) { fk.ref_cst_id_ = encoded(db, fk.ref_cst_id_); }
    }
    for (int64_t i = 0; i < copy->get_column_count(); ++i) {
      const_cast<ObColumnSchemaV2 *>(copy->get_column_schema_by_idx(i))->set_table_id(id);
    }
  } else {
    if (copy->get_aux_lob_meta_tid() != OB_INVALID_ID) {
      copy->set_aux_lob_meta_tid(encoded(db, copy->get_aux_lob_meta_tid()));
    }
    if (copy->get_aux_lob_piece_tid() != OB_INVALID_ID) {
      copy->set_aux_lob_piece_tid(encoded(db, copy->get_aux_lob_piece_tid()));
    }
  }
  // Namespace identity belongs to storage addressing. System schemas keep
  // their native logical IDs while each namespace gets a distinct tablet.
  copy->set_tablet_id(ObTabletID(encoded(db, tablet)));
  { std::lock_guard<std::mutex> lock(schema_mutex);
    auto &slot = schemas[id]; if (!slot) { slot = std::move(holder); } schema = &slot->schema; }
  return OB_SUCCESS;
}
int namespace_named(ObISQLClient &sql, const ObString &name, uint64_t &id) {
  ObSqlString q; ObMySQLProxy::MySQLResult res; int ret = OB_SUCCESS;
  sqlclient::ObMySQLResult *r = nullptr;
  if (OB_FAIL(q.assign_fmt("SELECT namespace_id FROM %s WHERE name=UNHEX('%s')", NAMESPACES,
      hex(std::string(name.ptr(), name.length())).c_str()))) {
  } else if (OB_FAIL(sql.read(res, q.ptr()))) {
  } else if (OB_ISNULL(r = res.get_result())) { ret = OB_ERR_UNEXPECTED;
  } else if (OB_FAIL(r->next())) {
  } else { ret = r->get_uint(0L, id); }
  return ret;
}
// DDL hooks can run while the native bootstrap transaction is still creating
// system schemas. Use the process-local SchemaService only as that bootstrap
// gate; runtime namespace access below goes straight to the global registry.
int namespace_registry_ready(bool &ready) {
  ready = false; ObSchemaGetterGuard guard; uint64_t db = OB_INVALID_ID;
  const ObSimpleTableSchemaV2 *schema = nullptr;
  int ret = GSCHEMASERVICE.get_runtime_schema_guard(guard);
  if (ret == OB_SUCCESS) { ret = guard.get_database_id(ObString("__fork_proto_meta"), db); }
  if (ret == OB_SUCCESS && db != OB_INVALID_ID) {
    ret = guard.get_simple_table_schema(db, ObString("namespaces"), false, schema);
    ready = ret == OB_SUCCESS && schema != nullptr;
    ret = guard.get_simple_table_schema(db, ObString("snapshots"), false, schema);
    ready = ret == OB_SUCCESS && schema != nullptr;
  }
  return ret;
}
int database_from_value(uint64_t ns, const Value &value, const ObDatabaseSchema *&schema) {
  uint64_t object = 0, db = 0, unused = 0, bound = 0;
  if (!entry(value.data, object, db, unused, bound) || unused || bound
      || !NamespaceObjectKey{ns, db}.is_valid()) { return OB_CHECKSUM_ERROR; }
  const uint64_t id = encoded(ns, db);
  { std::lock_guard<std::mutex> lock(schema_mutex);
    auto it = database_schemas.find(id);
    if (it != database_schemas.end()) { schema = &it->second->schema; return OB_SUCCESS; } }
  std::string data; int ret = blob(*GCTX.sql_proxy_, object, data); int64_t pos = 0;
  std::unique_ptr<DatabaseHolder> holder(new DatabaseHolder());
  if (ret != OB_SUCCESS) { return ret; }
  if ((ret = holder->schema.deserialize(data.data(), data.size(), pos)) != OB_SUCCESS) { return ret; }
  if (pos != int64_t(data.size()) || holder->schema.get_database_id() != db) { return OB_CHECKSUM_ERROR; }
  holder->schema.set_database_id(id);
  holder->simple.set_database_id(id);
  holder->simple.set_schema_version(holder->schema.get_schema_version());
  holder->simple.set_name_case_mode(OB_LOWERCASE_AND_INSENSITIVE);
  if ((ret = holder->simple.set_database_name(holder->schema.get_database_name_str())) != OB_SUCCESS) { return ret; }
  { std::lock_guard<std::mutex> lock(schema_mutex);
    auto &slot = database_schemas[id]; if (!slot) { slot = std::move(holder); } schema = &slot->schema; }
  return OB_SUCCESS;
}
class SqlSnapshotLineageStore final : public ::oceanbase::ns::ISnapshotLineageStore {
public:
  explicit SqlSnapshotLineageStore(ObISQLClient &trans) : trans_(trans) {}
  int load_for_update(uint64_t id, Roots &root) override {
    return snapshot_roots(trans_, id, root, true);
  }
  int increment_ref(uint64_t id) override {
    ObSqlString q;
    int ret = q.assign_fmt("UPDATE %s SET ref_count=ref_count+1 WHERE snapshot_id=%lu", SNAPSHOTS, id);
    return ret == OB_SUCCESS ? write_sql(trans_, q) : ret;
  }
  int decrement_ref(uint64_t id) override {
    ObSqlString q;
    int ret = q.assign_fmt("UPDATE %s SET ref_count=ref_count-1 WHERE snapshot_id=%lu", SNAPSHOTS, id);
    return ret == OB_SUCCESS ? write_sql(trans_, q) : ret;
  }
  int insert_snapshot(const Roots &root) override {
    ObSqlString q;
    int ret = q.assign_fmt("INSERT INTO %s VALUES(%ld,%lu,%lu,%ld,%ld,%ld,%ld,%lu,1)", SNAPSHOTS,
        root.snapshot, root.catalog.page, root.directory.page, root.snapshot, root.schema_version,
        root.catalog.cap, root.directory.cap, root.snapshot_ref);
    return ret == OB_SUCCESS ? write_sql(trans_, q) : ret;
  }
  int attach_child(uint64_t child_id, uint64_t parent_namespace_id,
                   const Roots &root) override {
    ObSqlString q;
    int ret = q.assign_fmt("UPDATE %s SET snapshot_ref=%ld WHERE namespace_id=%lu",
        NAMESPACES, root.snapshot, child_id);
    if (OB_SUCC(ret)) { ret = write_sql(trans_, q); }
    if (OB_SUCC(ret)) { ret = save_roots(trans_, child_id, root); }
    if (OB_SUCC(ret)) {
      ret = q.assign_fmt("UPDATE %s SET parent_namespace=%lu,fork_cap=%ld WHERE namespace_id=%lu",
          NAMESPACES, parent_namespace_id, root.snapshot, child_id);
    }
    return ret == OB_SUCCESS ? write_sql(trans_, q) : ret;
  }
  int remove_snapshot(uint64_t id, const Roots &snapshot) override {
    ObSnapshotInfo pin; ObSnapshotTableProxy pins; SCN scn; ObArray<ObTabletID> tablets;
    ObSqlString q;
    int ret = OB_SUCCESS;
    if (OB_FAIL(scn.convert_for_tx(snapshot.snapshot))) {
    } else if (OB_FAIL(pins.get_snapshot(trans_, SNAPSHOT_FOR_MULTI_VERSION, scn, pin))) {
    } else if (pin.tablet_id_ != 0 || pin.schema_version_ != snapshot.schema_version) { ret = OB_STATE_NOT_MATCH;
    } else if (OB_FAIL(tablets.push_back(ObTabletID(0)))) {
    } else if (OB_FAIL(pins.batch_remove_snapshots(trans_, SNAPSHOT_FOR_MULTI_VERSION,
        snapshot.schema_version, scn, tablets))) {
    } else if (OB_FAIL(q.assign_fmt("DELETE FROM %s WHERE snapshot_id=%lu", SNAPSHOTS, id))) {
    } else { ret = write_sql(trans_, q); }
    LOG_INFO("PROTOTYPE_V8_RELEASE_SNAPSHOT_IN_TRANS", K(ret), K(id), "parent", snapshot.parent_ref);
    return ret;
  }
private:
  ObISQLClient &trans_;
};

int release_lineage(ObISQLClient &trans, uint64_t id) {
  SqlSnapshotLineageStore store(trans);
  return ::oceanbase::ns::NamespaceSnapshotLineage::release(id, store);
}

int sql_has_row(ObISQLClient &sql, const ObSqlString &query, bool &has_row) {
  has_row = false;
  ObMySQLProxy::MySQLResult result;
  sqlclient::ObMySQLResult *rows = nullptr;
  int ret = sql.read(result, query.ptr());
  if (OB_FAIL(ret)) {
  } else if (OB_ISNULL(rows = result.get_result())) {
    ret = OB_ERR_UNEXPECTED;
  } else {
    ret = rows->next();
    if (ret == OB_ITER_END) { ret = OB_SUCCESS; }
    else if (OB_SUCC(ret)) { has_row = true; }
  }
  return ret;
}

// Namespace ids are never reused. A deleted row only needs to survive while a
// child still points at it or its private tablets await asynchronous GC.
int prune_dropped_namespace_row(uint64_t &cursor, bool &found) {
  found = false;
  ObSqlString query;
  int ret = query.assign_fmt("SELECT namespace_id FROM %s WHERE state=2 "
      "AND namespace_id<%lu ORDER BY namespace_id DESC LIMIT 1", NAMESPACES, cursor);
  ObMySQLProxy::MySQLResult result;
  sqlclient::ObMySQLResult *rows = nullptr;
  uint64_t id = 0;
  if (OB_FAIL(ret)) {
  } else if (OB_FAIL(GCTX.sql_proxy_->read(result, query.ptr()))) {
  } else if (OB_ISNULL(rows = result.get_result())) {
    ret = OB_ERR_UNEXPECTED;
  } else if (OB_FAIL(ret = rows->next())) {
    if (ret == OB_ITER_END) { cursor = UINT64_MAX; ret = OB_SUCCESS; }
  } else if (OB_FAIL(rows->get_uint(0L, id))) {
  }
  if (OB_FAIL(ret) || id == 0) { return ret; }
  cursor = id;
  found = true;

  ObMySQLTransaction trans;
  Roots root;
  bool has_child = false, has_owned = false;
  if (OB_FAIL(trans.start(GCTX.sql_proxy_))) {
  } else if (OB_FAIL(roots(trans, id, root, true, true))) {
  } else if (root.state != 2) {
    ret = OB_SUCCESS;
  } else if (OB_FAIL(query.assign_fmt("SELECT 1 FROM %s WHERE parent_namespace=%lu LIMIT 1",
      NAMESPACES, id))) {
  } else if (OB_FAIL(sql_has_row(trans, query, has_child))) {
  } else if (has_child) {
  } else if (OB_FAIL(query.assign_fmt("SELECT 1 FROM %s WHERE namespace_id=%lu AND kind=0 LIMIT 1",
      EXCEPTIONS, id))) {
  } else if (OB_FAIL(sql_has_row(trans, query, has_owned))) {
  } else if (has_owned) {
  } else if (OB_FAIL(query.assign_fmt("DELETE FROM %s WHERE namespace_id=%lu", EXCEPTIONS, id))) {
  } else if (OB_FAIL(write_sql(trans, query))) {
  } else if (OB_FAIL(query.assign_fmt("DELETE FROM %s WHERE namespace_id=%lu AND state=2",
      NAMESPACES, id))) {
  } else {
    ret = write_sql(trans, query);
  }
  if (trans.is_started()) {
    const int end = trans.end(OB_SUCC(ret));
    if (OB_SUCC(ret)) { ret = end; }
  }
  if (OB_SUCC(ret) && !has_child && !has_owned && root.state == 2) {
    invalidate_namespace_state(id);
    control_state().drop_exceptions(id);
    control_state().forget_chain_link(id);
    LOG_INFO("PROTOTYPE_NAMESPACE_TOMBSTONE_PRUNED", K(id));
  }
  return ret;
}

int prune_dropped_namespace_rows(uint64_t &cursor) {
  int ret = OB_SUCCESS;
  for (int64_t i = 0; OB_SUCC(ret) && i < 64; ++i) {
    bool found = false;
    ret = prune_dropped_namespace_row(cursor, found);
    if (!found) { break; }
  }
  return ret;
}

int collect_metadata() {
  if (metadata_depth || !GCTX.sql_proxy_) { return OB_STATE_NOT_MATCH; }
  std::unique_lock<std::shared_timed_mutex> exclusive(metadata_mutex, std::defer_lock);
  int ret = OB_SUCCESS;
  while (!exclusive.try_lock_for(std::chrono::milliseconds(1))) {
    if (OB_SUCCESS != (ret = THIS_WORKER.check_status())) { return ret; }
  }
  ObMySQLTransaction trans; ObSqlString q;
  std::vector<Ref> pending;
  std::unordered_set<uint64_t> reachable;
  std::vector<uint64_t> garbage;
  // External DDL owns its transaction beyond observe_schema/database(). Its root
  // row lock must also be gone. NOWAIT avoids waiting for an owner whose next
  // metadata callback is blocked by our exclusive guard. Retry the whole GC.
  if (OB_FAIL(trans.start(GCTX.sql_proxy_))) {
  } else if (OB_FAIL(q.assign_fmt("SELECT catalog_page,directory_page FROM %s ORDER BY namespace_id FOR UPDATE NOWAIT", NAMESPACES))) {
  } else {
    ObMySQLProxy::MySQLResult res; sqlclient::ObMySQLResult *r = nullptr;
    if (OB_FAIL(trans.read(res, q.ptr()))) {
    } else if (OB_ISNULL(r = res.get_result())) { ret = OB_ERR_UNEXPECTED;
    } else {
      while (OB_SUCC(ret = r->next())) {
        Ref catalog, directory;
        if (OB_FAIL(r->get_uint(0L, catalog.page)) || OB_FAIL(r->get_uint(1L, directory.page))) { break; }
        if (catalog.page) { pending.push_back(catalog); }
        if (directory.page) { pending.push_back(directory); }
      }
      if (ret == OB_ITER_END) { ret = OB_SUCCESS; }
    }
  }
  if (OB_SUCC(ret)) {
    ObMySQLProxy::MySQLResult res; sqlclient::ObMySQLResult *r = nullptr;
    if (OB_FAIL(q.assign_fmt("SELECT catalog_page,directory_page FROM %s WHERE ref_count>0", SNAPSHOTS))) {
    } else if (OB_FAIL(trans.read(res, q.ptr()))) {
    } else if (OB_ISNULL(r = res.get_result())) { ret = OB_ERR_UNEXPECTED;
    } else {
      while (OB_SUCC(ret = r->next())) {
        Ref catalog, directory;
        if (OB_FAIL(r->get_uint(0L, catalog.page)) || OB_FAIL(r->get_uint(1L, directory.page))) { break; }
        if (catalog.page) { pending.push_back(catalog); }
        if (directory.page) { pending.push_back(directory); }
      }
      if (ret == OB_ITER_END) { ret = OB_SUCCESS; }
    }
  }
  while (OB_SUCC(ret) && !pending.empty()) {
    Ref ref = pending.back(); pending.pop_back();
    if (!reachable.insert(ref.page).second) { continue; }
    Node node;
    if (OB_FAIL(THIS_WORKER.check_status())) {
    } else if (OB_FAIL(read_node(trans, ref, node))) {
    } else if (!node.leaf) { pending.insert(pending.end(), node.children.begin(), node.children.end());
    } else {
      for (const auto &value : node.values) {
        uint64_t object = 0, table = 0, source = 0, bound = 0;
        if (!entry(value.data, object, table, source, bound)) {
          ret = OB_CHECKSUM_ERROR;
          break;
        }
        // Native worker directory entries deliberately have no schema blob.
        // Catalog entries from the compatibility path still carry one.
        if (object != 0 && reachable.insert(object).second) {
          std::string schema;
          if (OB_FAIL(blob(trans, object, schema))) { break; }
        }
      }
    }
  }
  if (OB_SUCC(ret)) {
    ObMySQLProxy::MySQLResult res; sqlclient::ObMySQLResult *r = nullptr;
    if (OB_FAIL(q.assign_fmt("SELECT id FROM %s ORDER BY id", PAGES))) {
    } else if (OB_FAIL(trans.read(res, q.ptr()))) {
    } else if (OB_ISNULL(r = res.get_result())) { ret = OB_ERR_UNEXPECTED;
    } else {
      // Bound deletions per request. Marking still scans all reachable metadata.
      while (garbage.size() < 256 && OB_SUCC(ret = r->next())) {
        uint64_t id = 0;
        if (OB_FAIL(r->get_uint(0L, id))) { break; }
        if (!reachable.count(id)) { garbage.push_back(id); }
      }
      if (ret == OB_ITER_END) { ret = OB_SUCCESS; }
    }
  }
  if (OB_SUCC(ret) && !garbage.empty()) {
    if (OB_FAIL(q.assign_fmt("DELETE FROM %s WHERE id IN (", PAGES))) {
    } else {
      for (size_t i = 0; OB_SUCC(ret) && i < garbage.size(); ++i) {
        ret = q.append_fmt("%s%lu", i ? "," : "", garbage[i]);
      }
      int64_t affected = 0;
      if (OB_FAIL(ret)) {
      } else if (OB_FAIL(q.append(")"))) {
      } else if (OB_FAIL(trans.write(q.ptr(), affected))) {
      } else if (affected != int64_t(garbage.size())) { ret = OB_STATE_NOT_MATCH; }
    }
  }
  if (OB_SUCC(ret)) {
    DEBUG_SYNC(AFTER_UPDATE_TABLET_TO_LS);
    ret = THIS_WORKER.check_status();
  }
  if (trans.is_started()) { const int end = trans.end(ret == OB_SUCCESS); if (ret == OB_SUCCESS) { ret = end; } }
  LOG_INFO("PROTOTYPE_V9_METADATA_GC", K(ret), "reachable", reachable.size(), "deleted", garbage.size());
  return ret;
}

}

int NamespaceForkKernelPrototype::ensure_control_schema() {
  if (!GCTX.sql_proxy_) { return OB_NOT_INIT; }
  const char *statements[] = {
    "CREATE DATABASE IF NOT EXISTS __fork_proto_meta",
    "CREATE TABLE IF NOT EXISTS __fork_proto_meta.pages("
      "id BIGINT UNSIGNED PRIMARY KEY,payload VARBINARY(60000))",
    "CREATE TABLE IF NOT EXISTS __fork_proto_meta.roots("
      "database_id BIGINT UNSIGNED PRIMARY KEY,source_id BIGINT UNSIGNED,"
      "catalog_page BIGINT UNSIGNED,catalog_cap BIGINT,directory_page BIGINT UNSIGNED,"
      "directory_cap BIGINT,snapshot BIGINT,schema_version BIGINT)",
    "CREATE TABLE IF NOT EXISTS __fork_proto_meta.namespaces("
      "namespace_id BIGINT UNSIGNED PRIMARY KEY AUTO_INCREMENT,name VARBINARY(128) UNIQUE,"
      "source_id BIGINT UNSIGNED,catalog_page BIGINT UNSIGNED,catalog_cap BIGINT,"
      "directory_page BIGINT UNSIGNED,directory_cap BIGINT,snapshot BIGINT,schema_version BIGINT,"
      "snapshot_ref BIGINT UNSIGNED DEFAULT 0,state BIGINT DEFAULT 0,"
      "active_schema_changes BIGINT DEFAULT 0,pending_schema_version BIGINT DEFAULT 0,"
      "parent_namespace BIGINT UNSIGNED DEFAULT 0,fork_cap BIGINT UNSIGNED DEFAULT 0)",
    "CREATE TABLE IF NOT EXISTS __fork_proto_meta.exceptions("
      "namespace_id BIGINT UNSIGNED,tablet_id BIGINT UNSIGNED,"
      "table_id BIGINT UNSIGNED,kind BIGINT,drop_scn BIGINT DEFAULT 0,"
      "PRIMARY KEY(namespace_id,tablet_id))",
    "CREATE TABLE IF NOT EXISTS __fork_proto_meta.snapshots("
      "snapshot_id BIGINT UNSIGNED PRIMARY KEY,catalog_page BIGINT UNSIGNED,"
      "directory_page BIGINT UNSIGNED,snapshot BIGINT,schema_version BIGINT,"
      "catalog_cap BIGINT,directory_cap BIGINT,parent_ref BIGINT UNSIGNED,ref_count BIGINT)"
  };
  int ret = OB_SUCCESS;
  for (const char *statement : statements) {
    int64_t affected_rows = 0;
    if (OB_FAIL(GCTX.sql_proxy_->write(statement, affected_rows))) { break; }
  }
  if (OB_SUCC(ret)) {
    ObMySQLProxy::MySQLResult result;
    sqlclient::ObMySQLResult *rows = nullptr;
    if (OB_FAIL(GCTX.sql_proxy_->read(result,
        "SELECT namespace_id FROM __fork_proto_meta.namespaces WHERE namespace_id=1"))) {
    } else if (OB_ISNULL(rows = result.get_result())) {
      ret = OB_ERR_UNEXPECTED;
    } else {
      const int next_ret = rows->next();
      if (next_ret == OB_ITER_END) {
        uint64_t id = 0;
        if (OB_FAIL(control_namespace(ObString::make_string("__empty__"),
                                     ObString::make_string("ns1"), id))) {
        } else if (id != 1) {
          ret = OB_ERR_UNEXPECTED;
        } else {
          ret = control_namespace(ObString::make_string("ns1"),
                                  ObString::make_string("__template__"), id);
        }
      } else if (next_ret != OB_SUCCESS) {
        ret = next_ret;
      }
    }
  }
  LOG_INFO("PROTOTYPE_NAMESPACE_CONTROL_SCHEMA", K(ret));
  return ret;
}
NamespaceSourceDropGuard::NamespaceSourceDropGuard(ObISQLClient &trans) : valid_(false) {
  const ObISQLClient *expected = nullptr;
  valid_ = source_drop_trans.compare_exchange_strong(expected, &trans);
}
NamespaceSourceDropGuard::~NamespaceSourceDropGuard() {
  if (valid_) { source_drop_trans.store(nullptr); }
}
int NamespaceForkKernelPrototype::begin_namespace_drop(const ObString &name, uint64_t &id, bool &done) {
  done = false; id = 0;
  if (!GCTX.sql_proxy_) { return OB_NOT_SUPPORTED; }
  MetadataReadGuard access; if (access.error() != OB_SUCCESS) { return access.error(); }
  ObMySQLTransaction trans; Roots root; ObSqlString q;
  bool registry_closed = false;
  int ret = trans.start(GCTX.sql_proxy_);
  if (OB_FAIL(ret)) {
  } else if (OB_FAIL(namespace_named(trans, name, id))) {
  } else if (id == 1) {
    // Namespace 1 is both the default user namespace and the only SQL control
    // entry. It remains a valid fork source, but deleting it would orphan the
    // GLOBAL catalog and every child endpoint.
    ret = OB_OP_NOT_ALLOW;
  } else if (OB_FAIL(roots(trans, id, root, true, true))) {
  } else if (root.active_schema_changes != 0 || root.pending_schema_version != 0) {
    ret = OB_EAGAIN;
  } else if (root.state == 2) { done = true;
  } else if (root.state != 0 && root.state != 1) { ret = OB_STATE_NOT_MATCH;
  } else if (!ns::namespace_registry().begin_drop(id)) {
    ret = OB_OP_NOT_ALLOW;
    LOG_USER_ERROR(OB_OP_NOT_ALLOW, "drop a namespace with active connections");
  } else if (OB_FAIL(q.assign_fmt("UPDATE %s SET state=1 WHERE namespace_id=%lu", NAMESPACES, id))) {
    registry_closed = true;
  } else {
    registry_closed = true;
    ret = write_sql(trans, q);
  }
  const bool commit_attempted = ret == OB_SUCCESS;
  if (trans.is_started()) { const int end = trans.end(commit_attempted); if (ret == OB_SUCCESS) { ret = end; } }
  if (ret != OB_SUCCESS && registry_closed && !commit_attempted) {
    ns::namespace_registry().cancel_drop(id);
  }
  if (OB_SUCC(ret)) { invalidate_namespace_state(id); }
  LOG_INFO("PROTOTYPE_V7_NAMESPACE_CLOSE", K(ret), K(id), K(done));
  return ret;
}
int NamespaceForkKernelPrototype::lock_namespace_drop(ObISQLClient &trans, uint64_t id,
    ObIArray<ObTabletID> &bound_tablets) {
  MetadataReadGuard access; if (access.error() != OB_SUCCESS) { return access.error(); }
  Roots root; int ret = roots(trans, id, root, true, true);
  if (ret != OB_SUCCESS) { return ret; }
  if (root.state != 1) { return OB_STATE_NOT_MATCH; }
  if (id == 1) { return OB_SUCCESS; } // Native DROP owns its own enumeration.
  // The owned exception rows are exactly this namespace's private tablets.
  ObSqlString q; ObMySQLProxy::MySQLResult res; sqlclient::ObMySQLResult *r = nullptr;
  if (OB_FAIL(q.assign_fmt(
      "SELECT tablet_id FROM %s WHERE namespace_id=%lu AND kind=0", EXCEPTIONS, id))) {
  } else if (OB_FAIL(trans.read(res, q.ptr()))) {
  } else if (OB_ISNULL(r = res.get_result())) { ret = OB_ERR_UNEXPECTED;
  } else {
    while (OB_SUCC(ret = r->next())) {
      uint64_t tablet = 0;
      if (OB_FAIL(r->get_uint(0L, tablet))) {
      } else {
        ret = bound_tablets.push_back(ObTabletID(encoded(id, tablet)));
      }
    }
    if (ret == OB_ITER_END) { ret = OB_SUCCESS; }
  }
  return ret;
}
int NamespaceForkKernelPrototype::finish_namespace_drop(ObISQLClient &trans, uint64_t id) {
  MetadataReadGuard access; if (access.error() != OB_SUCCESS) { return access.error(); }
  Roots root; ObSqlString q; int ret = roots(trans, id, root, true, true);
  if (OB_FAIL(ret)) {
  } else if (root.state != 1) { ret = OB_STATE_NOT_MATCH;
  } else if (OB_FAIL(q.assign_fmt("UPDATE %s SET state=2,name=NULL,source_id=0,snapshot_ref=0,catalog_page=0,catalog_cap=0,directory_page=0,directory_cap=0,snapshot=0,schema_version=0,active_schema_changes=0,pending_schema_version=0 WHERE namespace_id=%lu AND state=1", NAMESPACES, id))) {
  } else { ret = write_sql(trans, q); }
  if (OB_SUCC(ret) && root.snapshot_ref) {
    ret = release_lineage(trans, root.snapshot_ref);
  }
  // The caller still commits, but a spurious cache miss after a rollback is
  // harmless while a stale LIVE entry after a committed drop is not.
  if (OB_SUCC(ret)) { invalidate_namespace_state(id); }
  if (OB_SUCC(ret)) { control_state().drop_exceptions(id); }
  return ret;
}
int NamespaceForkKernelPrototype::check_baseline_access(const ObTabletID &tablet_id, bool &held) {
  if (!held) { active_accesses.fetch_add(1); held = true; }
  MetadataReadGuard access; if (access.error() != OB_SUCCESS) { return access.error(); }
  if (!is_encoded_id(tablet_id.id())) { return OB_SUCCESS; }
  const uint64_t ns = database_of(tablet_id.id());
  // A baseline DAG is only valid on a tablet its namespace still owns. DROP
  // drains active accesses before deleting the owned rows, so an admitted DAG
  // always finishes against a valid binding.
  int64_t state = 0;
  if (cached_namespace_state(ns, state)) {
  } else {
    Roots root;
    const int roots_ret = roots(*GCTX.sql_proxy_, ns, root, false, true);
    if (roots_ret != OB_SUCCESS) { return roots_ret; }
    state = root.state;
    remember_namespace_state(ns, state);
  }
  if (state != 0) { return OB_ENTRY_NOT_EXIST; }
  int ret = load_exceptions(*GCTX.sql_proxy_, ns);
  if (OB_SUCC(ret) && !control_state().owned(ns, local_of(tablet_id.id()))) {
    ret = OB_ENTRY_NOT_EXIST;
  }
  return ret;
}
void NamespaceForkKernelPrototype::release_access(bool &held) {
  if (held) { held = false; active_accesses.fetch_sub(1); }
}
int NamespaceForkKernelPrototype::drain_access() {
  int ret = OB_SUCCESS;
  // Called after durable close, BEFORE taking the directory or DDL locks. Otherwise
  // an admitted cold-table materializer could wait on DROP while DROP waited on it.
  while (active_accesses.load() != 0 && OB_SUCC(ret = THIS_WORKER.check_status())) {
    if (REACH_TIME_INTERVAL(1000 * 1000)) {
      LOG_INFO("PROTOTYPE_V7_DRAIN_ACCESS", "active", active_accesses.load());
    }
    ob_usleep(10 * 1000);
  }
  return ret;
}
int NamespaceForkKernelPrototype::check_table_access(
    uint64_t table_id, const ObTabletID &tablet_id, bool read_only, bool &held) {
  if (tablet_id.is_inner_tablet() && !is_encoded_id(tablet_id.id())) {
    return OB_SUCCESS;
  }
  // Classify encoded storage by its actual tablet, including old plans and DML callers
  // without a schema parameter. Internal LOB scans must still bypass by owning tablet.
  const uint64_t id = namespace_of(tablet_id.id());
  int ret = OB_SUCCESS;
  if (id == 1) {
    // Namespace 1 is the undeletable physical root and also owns global control
    // storage.  Its lifetime therefore needs no namespace-registry lease.  More
    // importantly, the storage process must not consult its own SchemaService to
    // classify a table whose authoritative schema lives in the SQL worker.
    return OB_SUCCESS;
  }
  if (OB_SUCC(ret)) {
    // Register BEFORE reading LIVE; release only after iterators/store contexts or
    // a baseline DAG have released their inputs. New work after close cannot enter.
    if (!held) { active_accesses.fetch_add(1); held = true; }
    int64_t state = 0;
    if (cached_namespace_state(id, state)) {
    } else {
      Roots root;
      if (OB_FAIL(roots(*GCTX.sql_proxy_, id, root, false, true))) {
        return ret;
      }
      state = root.state;
      remember_namespace_state(id, state);
    }
    if (state != 0 && !(read_only && (state == 1 || state == 2))) {
      // Descendants may still read a physical tablet owned by this ancestor.
      // Namespace admission and the access drain fence prevent new reads from
      // the dropped owner itself; all writes must target a live namespace.
      ret = OB_OP_NOT_ALLOW;
      LOG_USER_ERROR(OB_OP_NOT_ALLOW, "access a closing or deleted prototype namespace");
    }
  }
  return ret;
}
int NamespaceForkKernelPrototype::protect_snapshot_tablets(ObIArray<ObTabletID> &candidates, bool &need_retry) {
  if (candidates.empty()) { return OB_SUCCESS; }
  MetadataReadGuard access; if (access.error() != OB_SUCCESS) { return access.error(); }
  int ret = OB_SUCCESS;
  // Candidates are committed deletions. A candidate stays referenced while any
  // LIVE namespace still resolves the tablet to it: the namespace itself must
  // not have a tombstone for the local id, and no nearer level on its parent
  // chain may hold its own owned copy. Prototype simplification: ownership is
  // checked regardless of fork order, so a tablet dropped in a parent is
  // retained until every LIVE descendant is gone.
  std::unordered_map<uint64_t, uint64_t> parents;
  std::unordered_set<uint64_t> live_readers;
  {
    ObSqlString q; ObMySQLProxy::MySQLResult res; sqlclient::ObMySQLResult *r = nullptr;
    if (OB_FAIL(q.assign_fmt("SELECT namespace_id,parent_namespace,state FROM %s", NAMESPACES))) {
    } else if (OB_FAIL(GCTX.sql_proxy_->read(res, q.ptr()))) {
    } else if (OB_ISNULL(r = res.get_result())) { ret = OB_ERR_UNEXPECTED;
    } else {
      while (OB_SUCC(ret = r->next())) {
        uint64_t ns = 0, parent = 0;
        int64_t state = 0;
        if (OB_FAIL(r->get_uint(0L, ns)) || OB_FAIL(r->get_uint(1L, parent))
            || OB_FAIL(r->get_int(2L, state))) { break; }
        parents[ns] = parent;
        if (state == 0) { live_readers.insert(ns); }
      }
      if (ret == OB_ITER_END) { ret = OB_SUCCESS; }
    }
  }
  std::unordered_map<uint64_t, std::unordered_set<uint64_t>> owned_by_local;
  std::unordered_map<uint64_t, std::unordered_set<uint64_t>> tombstoned_by_local;
  if (OB_SUCC(ret)) {
    ObSqlString q;
    if (OB_FAIL(q.assign_fmt("SELECT namespace_id,tablet_id,kind FROM %s WHERE tablet_id IN (", EXCEPTIONS))) {
    } else {
      std::unordered_set<uint64_t> locals;
      for (int64_t i = 0; OB_SUCC(ret) && i < candidates.count(); ++i) {
        const uint64_t local = local_of(candidates.at(i).id());
        if (locals.insert(local).second) {
          ret = q.append_fmt("%s%lu", locals.size() == 1 ? "" : ",", local);
        }
      }
      if (OB_SUCC(ret)) { ret = q.append(")"); }
    }
    if (OB_SUCC(ret)) {
      ObMySQLProxy::MySQLResult res; sqlclient::ObMySQLResult *r = nullptr;
      if (OB_FAIL(GCTX.sql_proxy_->read(res, q.ptr()))) {
      } else if (OB_ISNULL(r = res.get_result())) { ret = OB_ERR_UNEXPECTED;
      } else {
        while (OB_SUCC(ret = r->next())) {
          uint64_t ns = 0, tablet = 0; int64_t kind = 0;
          if (OB_FAIL(r->get_uint(0L, ns)) || OB_FAIL(r->get_uint(1L, tablet))
              || OB_FAIL(r->get_int(2L, kind))) {
            break;
          } else if (kind == 0) {
            owned_by_local[tablet].insert(ns);
          } else {
            tombstoned_by_local[tablet].insert(ns);
          }
        }
        if (ret == OB_ITER_END) { ret = OB_SUCCESS; }
      }
    }
  }
  ObArray<ObTabletID> unreferenced;
  for (int64_t i = 0; OB_SUCC(ret) && i < candidates.count(); ++i) {
    const uint64_t id = candidates.at(i).id();
    const uint64_t owner = namespace_of(id);
    const uint64_t local = local_of(id);
    bool retained = false;
    for (const uint64_t reader : live_readers) {
      if (reader == owner) { continue; }
      if (tombstoned_by_local[local].count(reader) != 0) { continue; }
      // Walk the reader's chain: the first owned copy below the owner serves
      // the reader instead, and reaching namespace 1 only helps a raw
      // candidate.
      uint64_t cur = reader;
      bool via_candidate = false;
      for (int depth = 0; depth < 64; ++depth) {
        if (cur == owner) { via_candidate = true; break; }
        if (owned_by_local[local].count(cur) != 0) { break; }
        const auto parent = parents.find(cur);
        if (parent == parents.end() || parent->second == 0) { break; }
        cur = parent->second;
      }
      if (via_candidate) { retained = true; break; }
    }
    if (retained) {
      need_retry = true;
      LOG_INFO("PROTOTYPE_V6_RETAIN_SNAPSHOT_TABLET", "tablet_id", candidates.at(i));
    } else {
      ret = unreferenced.push_back(candidates.at(i));
    }
  }
  if (OB_SUCC(ret)) { ret = candidates.assign(unreferenced); }
  return ret;
}
int NamespaceForkKernelPrototype::collect_dropped_namespace_tablets() {
  if (!GCTX.sql_proxy_ || !ATOMIC_LOAD(&GCTX.sys_package_ready_)) { return OB_SUCCESS; }
  static std::mutex scan_mutex;
  static uint64_t cursor_namespace = 0, cursor_tablet = 0;
  static uint64_t tombstone_cursor = UINT64_MAX;
  std::lock_guard<std::mutex> scan_guard(scan_mutex);
  ObSqlString scan;
  int ret = scan.assign_fmt(
      "SELECT e.namespace_id,e.tablet_id FROM %s e "
      "JOIN %s n ON n.namespace_id=e.namespace_id "
      "WHERE n.state=2 AND e.kind=0 AND "
      "(e.namespace_id>%lu OR (e.namespace_id=%lu AND e.tablet_id>%lu)) "
      "ORDER BY e.namespace_id,e.tablet_id LIMIT 64",
      EXCEPTIONS, NAMESPACES, cursor_namespace, cursor_namespace, cursor_tablet);
  ObMySQLProxy::MySQLResult result;
  sqlclient::ObMySQLResult *rows = nullptr;
  if (OB_SUCC(ret)) { ret = GCTX.sql_proxy_->read(result, scan.ptr()); }
  ObArray<ObTabletID> candidates;
  ObArray<ObTabletID> stale;
  if (OB_SUCC(ret) && OB_ISNULL(rows = result.get_result())) { ret = OB_ERR_UNEXPECTED; }
  bool saw_row = false;
  while (OB_SUCC(ret)) {
    ret = rows->next();
    if (ret == OB_ITER_END) { ret = OB_SUCCESS; break; }
    uint64_t namespace_id = 0, local_tablet_id = 0;
    if (OB_FAIL(rows->get_uint(0L, namespace_id))
        || OB_FAIL(rows->get_uint(1L, local_tablet_id))) {
    } else {
      saw_row = true;
      cursor_namespace = namespace_id;
      cursor_tablet = local_tablet_id;
      const NamespaceObjectKey key{namespace_id, local_tablet_id};
      bool exists = false;
      if (!key.is_valid()) {
        ret = OB_INVALID_ARGUMENT;
      } else if (OB_FAIL(probe_physical_tablet(key.storage_id(), exists))) {
      } else if (exists) {
        ret = candidates.push_back(ObTabletID(key.storage_id()));
      } else {
        ret = stale.push_back(ObTabletID(key.storage_id()));
      }
    }
  }
  if (OB_SUCC(ret) && !saw_row) { cursor_namespace = cursor_tablet = 0; }
  bool deferred = false;
  if (OB_SUCC(ret) && !candidates.empty()) {
    ret = protect_snapshot_tablets(candidates, deferred);
  }
  if (OB_FAIL(ret)) { return ret; }
  if (candidates.empty() && stale.empty()) {
    return prune_dropped_namespace_rows(tombstone_cursor);
  }
  int64_t schema_version = 0;
  ObSchemaGetterGuard guard;
  if (OB_FAIL(GSCHEMASERVICE.get_runtime_schema_guard(guard))) {
  } else if (OB_FAIL(guard.get_schema_version(schema_version))) {
  }
  ObMySQLTransaction trans;
  if (OB_SUCC(ret)) { ret = trans.start(GCTX.sql_proxy_); }
  if (OB_SUCC(ret) && !candidates.empty()) {
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
      } else { ret = drop.execute(); }
    }
  }
  for (int64_t i = 0; OB_SUCC(ret) && i < candidates.count() + stale.count(); ++i) {
    const ObTabletID tablet = i < candidates.count()
        ? candidates.at(i) : stale.at(i - candidates.count());
    ObSqlString q;
    if (OB_FAIL(q.assign_fmt("DELETE FROM %s WHERE namespace_id=%lu "
            "AND tablet_id=%lu AND kind=0", EXCEPTIONS,
            database_of(tablet.id()), local_of(tablet.id())))) {
    } else { ret = write_sql(trans, q); }
  }
  if (trans.is_started()) {
    const int end = trans.end(OB_SUCC(ret));
    if (OB_SUCC(ret)) { ret = end; }
  }
  if (OB_SUCC(ret)) {
    for (int64_t i = 0; i < candidates.count(); ++i) {
      control_state().drop_exceptions(database_of(candidates.at(i).id()));
    }
  }
  LOG_INFO("PROTOTYPE_NAMESPACE_DROPPED_TABLET_GC", K(ret),
      "dropped", candidates.count(), "stale", stale.count(), K(deferred));
  if (OB_SUCC(ret)) { ret = prune_dropped_namespace_rows(tombstone_cursor); }
  return ret;
}
bool NamespaceForkKernelPrototype::is_encoded_id(uint64_t id) {
  return NamespaceObjectKey::is_encoded(id);
}
uint64_t NamespaceForkKernelPrototype::encode_id(uint64_t namespace_id, uint64_t local_id) {
  return NamespaceObjectKey{namespace_id, local_id}.storage_id();
}
uint64_t NamespaceForkKernelPrototype::namespace_of(uint64_t id) {
  return is_encoded_id(id) ? database_of(id) : 1;
}
int NamespaceForkKernelPrototype::local_object_id(
    uint64_t namespace_id, uint64_t object_id, uint64_t &local_id) {
  local_id = object_id;
  if (namespace_id == 0 || namespace_id >= (1ULL << 30)) { return OB_INVALID_ARGUMENT; }
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
uint64_t NamespaceForkKernelPrototype::encode_object(uint64_t database_id, uint64_t local_id) {
  return is_encoded_id(database_id) && NamespaceObjectKey{database_of(database_id), local_id}.is_valid()
      ? encoded(database_of(database_id), local_id) : local_id;
}
int NamespaceForkKernelPrototype::make_namespace_schema(
    uint64_t namespace_id, const ObTableSchema &storage_schema, ObTableSchema &namespace_schema) {
  if (!NamespaceObjectKey{namespace_id, 1}.is_valid()) { return OB_INVALID_ARGUMENT; }
  int ret = namespace_schema.assign(storage_schema);
  auto local_id = [&](uint64_t object_id, uint64_t &result) {
    return local_object_id(namespace_id, object_id, result);
  };
  auto set_optional = [&](uint64_t object_id, auto setter) {
    if (object_id == 0 || object_id == OB_INVALID_ID) { return OB_SUCCESS; }
    uint64_t result = OB_INVALID_ID;
    const int optional_ret = local_id(object_id, result);
    if (optional_ret == OB_SUCCESS) { setter(result); }
    return optional_ret;
  };
  uint64_t logical_table_id = storage_schema.get_table_id();
  uint64_t result = OB_INVALID_ID;
  if (OB_SUCC(ret) && !storage_schema.is_sys_table()) {
    if (OB_FAIL(local_id(storage_schema.get_database_id(), result))) {
    } else {
      namespace_schema.set_database_id(result);
    }
    if (OB_SUCC(ret)) {
      if (OB_FAIL(local_id(storage_schema.get_table_id(), logical_table_id))) {
      } else {
        namespace_schema.set_table_id(logical_table_id);
      }
    }
    if (OB_SUCC(ret)) {
      ret = set_optional(storage_schema.get_data_table_id(),
          [&](uint64_t id) { namespace_schema.set_data_table_id(id); });
    }
    if (OB_SUCC(ret)) {
      ret = set_optional(storage_schema.get_association_table_id(),
          [&](uint64_t id) { namespace_schema.set_association_table_id(id); });
    }
    if (OB_SUCC(ret)) {
      std::vector<ObAuxTableMetaInfo> indexes = index_infos(storage_schema);
      namespace_schema.reset_simple_index_infos();
      for (auto &index : indexes) {
        if (OB_FAIL(local_id(index.table_id_, result))) { break; }
        index.table_id_ = result;
        if (OB_FAIL(namespace_schema.add_simple_index_info(index))) { break; }
      }
    }
    for (int64_t i = 0; OB_SUCC(ret) && i < namespace_schema.get_foreign_key_infos().count(); ++i) {
      auto &foreign_key = namespace_schema.get_foreign_key_infos().at(i);
      foreign_key.table_id_ = logical_table_id;
      if (OB_FAIL(local_id(foreign_key.child_table_id_, result))) {
      } else {
        foreign_key.child_table_id_ = result;
      }
      if (OB_SUCC(ret)) {
        if (OB_FAIL(local_id(foreign_key.parent_table_id_, result))) {
        } else {
          foreign_key.parent_table_id_ = result;
        }
      }
      if (OB_SUCC(ret) && foreign_key.ref_cst_id_ != OB_INVALID_ID) {
        if (OB_FAIL(local_id(foreign_key.ref_cst_id_, result))) {
        } else {
          foreign_key.ref_cst_id_ = result;
        }
      }
    }
    for (auto it = namespace_schema.constraint_begin_for_non_const_iter();
         OB_SUCC(ret) && it != namespace_schema.constraint_end_for_non_const_iter(); ++it) {
      if (*it == nullptr) {
        ret = OB_ERR_UNEXPECTED;
      } else if (OB_FAIL(local_id((*it)->get_table_id(), result))) {
      } else {
        (*it)->set_table_id(result);
        if (OB_FAIL(local_id((*it)->get_constraint_id(), result))) {
        } else {
          (*it)->set_constraint_id(result);
        }
      }
    }
    for (int64_t i = 0; OB_SUCC(ret) && i < namespace_schema.get_column_count(); ++i) {
      ObColumnSchemaV2 *column = namespace_schema.get_column_schema_by_idx(i);
      if (column == nullptr) {
        ret = OB_ERR_UNEXPECTED;
      } else if (OB_FAIL(local_id(column->get_table_id(), result))) {
      } else if (result != logical_table_id) {
        ret = OB_INVALID_ARGUMENT;
      } else {
        column->set_table_id(logical_table_id);
      }
    }
  }
  if (OB_SUCC(ret)) {
    ret = set_optional(storage_schema.get_aux_lob_meta_tid(),
        [&](uint64_t id) { namespace_schema.set_aux_lob_meta_tid(id); });
  }
  if (OB_SUCC(ret)) {
    ret = set_optional(storage_schema.get_aux_lob_piece_tid(),
        [&](uint64_t id) { namespace_schema.set_aux_lob_piece_tid(id); });
  }
  if (OB_SUCC(ret) && !namespace_schema.is_view_table()) {
    ret = rewrite_tablet_ids(namespace_schema,
        [&](uint64_t id, uint64_t &rewritten) {
          return local_id(id, rewritten);
        });
  }
  return ret;
}
int NamespaceForkKernelPrototype::make_storage_schema(
    uint64_t namespace_id, const ObTableSchema &logical_schema, ObTableSchema &storage_schema) {
  if (!NamespaceObjectKey{namespace_id, 1}.is_valid()) { return OB_INVALID_ARGUMENT; }
  int ret = storage_schema.assign(logical_schema);
  auto storage_id = [&](uint64_t object_id, uint64_t &result) {
    uint64_t local_id = OB_INVALID_ID;
    int local_ret = local_object_id(namespace_id, object_id, local_id);
    const NamespaceObjectKey key{namespace_id, local_id};
    if (local_ret != OB_SUCCESS) { return local_ret; }
    if (!key.is_valid()) { return OB_SIZE_OVERFLOW; }
    result = key.storage_id();
    return OB_SUCCESS;
  };
  auto set_optional = [&](uint64_t object_id, auto setter) {
    if (object_id == 0 || object_id == OB_INVALID_ID) { return OB_SUCCESS; }
    uint64_t physical_id = OB_INVALID_ID;
    const int optional_ret = storage_id(object_id, physical_id);
    if (optional_ret == OB_SUCCESS) { setter(physical_id); }
    return optional_ret;
  };
  uint64_t physical_table_id = logical_schema.get_table_id();
  uint64_t physical_id = OB_INVALID_ID;
  if (OB_SUCC(ret) && !logical_schema.is_sys_table()) {
    if (OB_FAIL(storage_id(logical_schema.get_database_id(), physical_id))) {
    } else {
      storage_schema.set_database_id(physical_id);
    }
    if (OB_SUCC(ret)) {
      if (OB_FAIL(storage_id(logical_schema.get_table_id(), physical_table_id))) {
      } else {
        storage_schema.set_table_id(physical_table_id);
      }
    }
    if (OB_SUCC(ret)) {
      ret = set_optional(logical_schema.get_data_table_id(),
          [&](uint64_t id) { storage_schema.set_data_table_id(id); });
    }
    if (OB_SUCC(ret)) {
      ret = set_optional(logical_schema.get_association_table_id(),
          [&](uint64_t id) { storage_schema.set_association_table_id(id); });
    }
    if (OB_SUCC(ret)) {
      std::vector<ObAuxTableMetaInfo> indexes = index_infos(logical_schema);
      storage_schema.reset_simple_index_infos();
      for (auto &index : indexes) {
        uint64_t index_id = OB_INVALID_ID;
        if (OB_FAIL(storage_id(index.table_id_, index_id))) { break; }
        index.table_id_ = index_id;
        if (OB_FAIL(storage_schema.add_simple_index_info(index))) { break; }
      }
    }
    for (int64_t i = 0; OB_SUCC(ret) && i < storage_schema.get_foreign_key_infos().count(); ++i) {
      auto &foreign_key = storage_schema.get_foreign_key_infos().at(i);
      foreign_key.table_id_ = physical_table_id;
      if (OB_FAIL(storage_id(foreign_key.child_table_id_, physical_id))) {
      } else {
        foreign_key.child_table_id_ = physical_id;
      }
      if (OB_SUCC(ret)) {
        if (OB_FAIL(storage_id(foreign_key.parent_table_id_, physical_id))) {
        } else {
          foreign_key.parent_table_id_ = physical_id;
        }
      }
      if (OB_SUCC(ret) && foreign_key.ref_cst_id_ != OB_INVALID_ID) {
        if (OB_FAIL(storage_id(foreign_key.ref_cst_id_, physical_id))) {
        } else {
          foreign_key.ref_cst_id_ = physical_id;
        }
      }
    }
    for (int64_t i = 0; OB_SUCC(ret) && i < storage_schema.get_column_count(); ++i) {
      ObColumnSchemaV2 *column = const_cast<ObColumnSchemaV2 *>(
          storage_schema.get_column_schema_by_idx(i));
      if (column == nullptr) { ret = OB_ERR_UNEXPECTED; }
      else { column->set_table_id(physical_table_id); }
    }
  }
  if (OB_SUCC(ret)) {
    ret = set_optional(logical_schema.get_aux_lob_meta_tid(),
        [&](uint64_t id) { storage_schema.set_aux_lob_meta_tid(id); });
  }
  if (OB_SUCC(ret)) {
    ret = set_optional(logical_schema.get_aux_lob_piece_tid(),
        [&](uint64_t id) { storage_schema.set_aux_lob_piece_tid(id); });
  }
  if (OB_SUCC(ret) && !storage_schema.is_view_table()) {
    ret = rewrite_tablet_ids(storage_schema,
        [&](uint64_t id, uint64_t &rewritten) {
          return storage_id(id, rewritten);
        });
  }
  return ret;
}
int NamespaceForkKernelPrototype::namespace_schema_version(uint64_t ns, int64_t &version) {
  version = OB_INVALID_VERSION;
  if (!GCTX.sql_proxy_ || !NamespaceObjectKey{ns, 1}.is_valid()) {
    return OB_INVALID_ARGUMENT;
  }
  MetadataReadGuard access;
  if (access.error() != OB_SUCCESS) { return access.error(); }
  Roots root;
  const int ret = roots(*GCTX.sql_proxy_, ns, root);
  if (ret == OB_SUCCESS) { version = root.schema_version; }
  return ret;
}
int NamespaceForkKernelPrototype::begin_schema_change(uint64_t ns) {
  if (!GCTX.sql_proxy_ || ns <= 1 || ns >= (1ULL << 30)) {
    return OB_INVALID_ARGUMENT;
  }
  ObSqlString q;
  int ret = q.assign_fmt(
      "UPDATE %s SET active_schema_changes=active_schema_changes+1 "
      "WHERE namespace_id=%lu AND state=0 AND active_schema_changes<%ld",
      NAMESPACES, ns, INT64_MAX);
  int64_t affected_rows = 0;
  if (OB_SUCC(ret)) { ret = GCTX.sql_proxy_->write(q.ptr(), affected_rows); }
  return OB_SUCC(ret) && affected_rows != 1 ? OB_STATE_NOT_MATCH : ret;
}
int NamespaceForkKernelPrototype::finish_schema_change(
    uint64_t ns, int64_t schema_version) {
  if (!GCTX.sql_proxy_ || ns <= 1 || ns >= (1ULL << 30)
      || schema_version < 0) {
    return OB_INVALID_ARGUMENT;
  }
  ObMySQLTransaction trans;
  Roots root;
  ObSqlString q;
  int ret = trans.start(GCTX.sql_proxy_);
  if (OB_SUCC(ret)) { ret = roots(trans, ns, root, true); }
  if (OB_SUCC(ret) && root.active_schema_changes <= 0) {
    ret = OB_STATE_NOT_MATCH;
  }
  if (OB_SUCC(ret)) {
    const int64_t pending = schema_version > 0
        ? std::max(root.pending_schema_version, schema_version)
        : root.pending_schema_version;
    ret = q.assign_fmt(
        "UPDATE %s SET active_schema_changes=%ld,pending_schema_version=%ld "
        "WHERE namespace_id=%lu AND state=0",
        NAMESPACES, root.active_schema_changes - 1, pending, ns);
  }
  if (OB_SUCC(ret)) { ret = write_sql(trans, q); }
  if (trans.is_started()) {
    const int end_ret = trans.end(OB_SUCC(ret));
    if (OB_SUCC(ret)) { ret = end_ret; }
  }
  return ret;
}
int NamespaceForkKernelPrototype::begin_schema_recovery(uint64_t ns, bool &needed) {
  needed = false;
  if (!GCTX.sql_proxy_ || ns <= 1 || ns >= (1ULL << 30)) {
    return OB_INVALID_ARGUMENT;
  }
  ObMySQLTransaction trans;
  Roots root;
  ObSqlString q;
  int ret = trans.start(GCTX.sql_proxy_);
  if (OB_SUCC(ret)) { ret = roots(trans, ns, root, true); }
  if (OB_SUCC(ret)) {
    needed = root.active_schema_changes != 0 || root.pending_schema_version != 0;
  }
  if (OB_SUCC(ret) && needed) {
    ret = q.assign_fmt(
        "UPDATE %s SET active_schema_changes=0,pending_schema_version=%ld "
        "WHERE namespace_id=%lu AND state=0",
        NAMESPACES, INT64_MAX, ns);
  }
  if (OB_SUCC(ret) && needed) { ret = write_sql(trans, q); }
  if (trans.is_started()) {
    const int end_ret = trans.end(OB_SUCC(ret));
    if (OB_SUCC(ret)) { ret = end_ret; }
  }
  return ret;
}
int NamespaceForkKernelPrototype::finish_schema_recovery(
    uint64_t ns, int64_t schema_version) {
  if (!GCTX.sql_proxy_ || ns <= 1 || ns >= (1ULL << 30)
      || schema_version <= 0) {
    return OB_INVALID_ARGUMENT;
  }
  ObMySQLTransaction trans;
  Roots root;
  ObSqlString q;
  int ret = trans.start(GCTX.sql_proxy_);
  if (OB_SUCC(ret)) { ret = roots(trans, ns, root, true); }
  if (OB_SUCC(ret) && (root.active_schema_changes != 0
      || root.schema_version > schema_version)) {
    ret = OB_STATE_NOT_MATCH;
  }
  if (OB_SUCC(ret) && root.schema_version < schema_version) {
    root.schema_version = schema_version;
    ret = save_roots(trans, ns, root);
  }
  if (OB_SUCC(ret)) {
    ret = q.assign_fmt(
        "UPDATE %s SET pending_schema_version=0 WHERE namespace_id=%lu AND state=0",
        NAMESPACES, ns);
  }
  if (OB_SUCC(ret)) { ret = write_sql(trans, q); }
  if (trans.is_started()) {
    const int end_ret = trans.end(OB_SUCC(ret));
    if (OB_SUCC(ret)) { ret = end_ret; }
  }
  return ret;
}
int NamespaceForkKernelPrototype::begin_schema_changes(ObISQLClient &, uint64_t namespace_id) {
  return namespace_id > 1 ? begin_schema_change(namespace_id) : OB_SUCCESS;
}
int NamespaceForkKernelPrototype::finish_schema_changes(
    ObISQLClient &, uint64_t namespace_id, int64_t committed_schema_version) {
  return namespace_id > 1
      ? finish_schema_change(namespace_id, committed_schema_version) : OB_SUCCESS;
}
bool NamespaceForkKernelPrototype::is_namespace_address(const ObString &name) {
  return name.prefix_match("__fork_ns_");
}
int NamespaceForkKernelPrototype::parse_namespace_address(
    const ObString &address, uint64_t &namespace_id, ObString &database_name) {
  namespace_id = 0;
  database_name.reset();
  constexpr int64_t prefix_length = 10;
  if (!is_namespace_address(address) || address.length() <= prefix_length + 2) {
    return OB_INVALID_ARGUMENT;
  }
  int64_t separator = -1;
  for (int64_t i = prefix_length; i + 1 < address.length(); ++i) {
    if (address[i] == '_' && address[i + 1] == '_') {
      separator = i;
      break;
    }
    if (address[i] < '0' || address[i] > '9'
        || i - prefix_length >= 10) {
      return OB_INVALID_ARGUMENT;
    }
    namespace_id = namespace_id * 10 + address[i] - '0';
  }
  if (separator == prefix_length || separator < 0
      || separator + 2 >= address.length()
      || !NamespaceObjectKey{namespace_id, 1}.is_valid()) {
    return OB_INVALID_ARGUMENT;
  }
  database_name.assign_ptr(address.ptr() + separator + 2,
                           address.length() - separator - 2);
  return OB_SUCCESS;
}
int NamespaceForkKernelPrototype::database_by_address(const ObString &address, const ObDatabaseSchema *&schema) {
  schema = nullptr;
  uint64_t namespace_id = 0;
  ObString database_name;
  const int ret = parse_namespace_address(address, namespace_id, database_name);
  return ret == OB_SUCCESS
      ? database_in_namespace(namespace_id, database_name, schema) : ret;
}
int NamespaceForkKernelPrototype::database_in_namespace(uint64_t ns, const ObString &name,
                                                       const ObDatabaseSchema *&schema) {
  schema = nullptr;
  if (!GCTX.sql_proxy_ || !NamespaceObjectKey{ns, 1}.is_valid()) { return OB_INVALID_ARGUMENT; }
  MetadataReadGuard access; if (access.error() != OB_SUCCESS) { return access.error(); }
  Roots root; Value value; int ret = roots(*GCTX.sql_proxy_, ns, root);
  if (ret == OB_SUCCESS) { ret = find(*GCTX.sql_proxy_, root.catalog, "D" + std::string(name.ptr(), name.length()), value); }
  if (ret == OB_ITER_END || ret == OB_ENTRY_NOT_EXIST) { return OB_SUCCESS; }
  return ret == OB_SUCCESS ? database_from_value(ns, value, schema) : ret;
}
int NamespaceForkKernelPrototype::database_by_id(uint64_t id, const ObDatabaseSchema *&schema) {
  schema = nullptr; Roots root; Value value;
  if (!is_encoded_id(id) || !GCTX.sql_proxy_) { return OB_INVALID_ARGUMENT; }
  MetadataReadGuard access; if (access.error() != OB_SUCCESS) { return access.error(); }
  int ret = roots(*GCTX.sql_proxy_, database_of(id), root);
  if (ret == OB_SUCCESS) { ret = find(*GCTX.sql_proxy_, root.catalog, "@" + key_of(local_of(id)), value); }
  if (ret == OB_ITER_END || ret == OB_ENTRY_NOT_EXIST) { return OB_SUCCESS; }
  return ret == OB_SUCCESS ? database_from_value(database_of(id), value, schema) : ret;
}
int NamespaceForkKernelPrototype::database_by_id(uint64_t id, const ObSimpleDatabaseSchema *&schema) {
  schema = nullptr; const ObDatabaseSchema *full = nullptr;
  int ret = database_by_id(id, full);
  if (ret == OB_SUCCESS && full) {
    std::lock_guard<std::mutex> lock(schema_mutex);
    schema = &database_schemas.at(id)->simple;
  }
  return ret;
}
int NamespaceForkKernelPrototype::observe_database(ObISQLClient &trans, const ObDatabaseSchema &schema) {
  if (is_inner_db(schema.get_database_id())
      || schema.get_database_name_str().prefix_match("__fork_proto_meta")) { return OB_SUCCESS; }
  // Namespace schema authority is native: databases live in each worker's own
  // schema cache, no legacy catalog enrollment.
  return OB_SUCCESS;
}
int NamespaceForkKernelPrototype::check_database_ddl(const ObDatabaseSchema &schema, const ObISQLClient *trans) {
  if (trans && source_drop_trans.load() == trans) { return OB_SUCCESS; }
  // Namespace schema authority is native: only the control database is
  // protected, and only from workers without control access.
  return !observer::namespace_worker_prototype::can_access_namespace_control_database()
      && observer::namespace_worker_prototype::is_namespace_control_database(
          schema.get_database_name_str())
      ? OB_NOT_SUPPORTED : OB_SUCCESS;
}
int NamespaceForkKernelPrototype::control_namespace(const ObString &source, const ObString &target, uint64_t &id) {
  if (!GCTX.sql_proxy_ || target.empty() || target.length() > 128) { return OB_INVALID_ARGUMENT; }
  if (source == "__gc__" && target == "__gc__") { id = OB_INVALID_ID; return collect_metadata(); }
  MetadataReadGuard access; if (access.error() != OB_SUCCESS) { return access.error(); }
  const int64_t begin_us = ObTimeUtility::current_time();
  const bool bootstrap = source == "__empty__";
  ObMySQLTransaction trans; Roots root; ObSchemaGetterGuard guard; uint64_t source_id = 0;
  int ret = OB_SUCCESS; ObSqlString q;
  bool source_locked = false;
  while (OB_SUCC(ret) && !source_locked) {
    ret = trans.start(GCTX.sql_proxy_);
    if (OB_FAIL(ret)) {
    } else if (bootstrap) {
      source_locked = true;
    } else if (OB_FAIL(namespace_named(trans, source, source_id))) {
    } else if (OB_FAIL(roots(trans, source_id, root, true))) {
    } else if (root.active_schema_changes == 0 && root.pending_schema_version == 0) {
      source_locked = true;
    } else {
      const int end_ret = trans.end(false);
      if (end_ret != OB_SUCCESS) {
        ret = end_ret;
      } else if (OB_FAIL(THIS_WORKER.check_status())) {
      } else {
        ob_usleep(10 * 1000L);
      }
    }
  }
  if (ret != OB_SUCCESS) {
    if (trans.is_started()) { trans.end(false); }
    return ret;
  }
  if (OB_SUCC(ret) && bootstrap) {
    ret = GSCHEMASERVICE.get_runtime_schema_guard(guard);
  }
  if (OB_SUCC(ret) && bootstrap) {
    ret = guard.get_schema_version(root.schema_version);
  }
  if (OB_SUCC(ret)) {
    if (bootstrap) {
      ret = q.assign_fmt("INSERT INTO %s(namespace_id,name,source_id,catalog_page,catalog_cap,directory_page,directory_cap,snapshot,schema_version) VALUES(1,UNHEX('%s'),0,0,0,0,0,0,%ld)", NAMESPACES,
          hex(std::string(target.ptr(), target.length())).c_str(), root.schema_version);
    } else {
      ret = q.assign_fmt("INSERT INTO %s(name,source_id,catalog_page,catalog_cap,directory_page,directory_cap,snapshot,schema_version) VALUES(UNHEX('%s'),0,0,0,0,0,0,0)",
          NAMESPACES, hex(std::string(target.ptr(), target.length())).c_str());
    }
  }
  if (OB_SUCC(ret)) { ret = write_sql(trans, q); }
  if (OB_SUCC(ret)) { ret = namespace_named(trans, target, id); }
  if (OB_SUCC(ret) && !NamespaceObjectKey{id, 1}.is_valid()) { ret = OB_SIZE_OVERFLOW; }
  // The exception-table model has no per-table enrollment: namespace 1 keeps
  // its raw tablets and a fork records only its parent link and fork cap.
  if (OB_SUCC(ret) && !bootstrap) {
    ObSnapshotInfo pin; ObSnapshotTableProxy pins;
    if (OB_FAIL(observer::namespace_worker_prototype::acquire_storage_snapshot(root.snapshot))) {
    } else if (OB_FAIL(pin.snapshot_scn_.convert_for_tx(root.snapshot))) {
    } else {
      pin.snapshot_type_ = SNAPSHOT_FOR_MULTI_VERSION; pin.tablet_id_ = 0;
      pin.schema_version_ = root.schema_version; pin.comment_ = "PROTOTYPE namespace root; source retained";
      // ObSnapshotTableProxy deliberately locks snapshot_gc_scn with NOWAIT.
      // The background GC renewer can hold that row briefly, so retry the
      // statement in the same transaction just like the native fork-table
      // snapshot path does.  Nothing has been inserted when this conflict is
      // returned.
      while (OB_SUCC(ret) && OB_FAIL(pins.add_snapshot(trans, pin))
          && ret == OB_ERR_EXCLUSIVE_LOCK_CONFLICT_NOWAIT
          && THIS_WORKER.is_timeout_ts_valid() && !THIS_WORKER.is_timeout()) {
        trans.reset_last_error();
        ret = OB_SUCCESS;
        ob_usleep(10 * 1000);
      }
      if (OB_FAIL(ret)) {
      } else {
        SqlSnapshotLineageStore store(trans);
        const auto result = ::oceanbase::ns::NamespaceSnapshotLineage::fork(
            source_id, id, root, store);
        if (result.error == ::oceanbase::ns::SnapshotForkError::INVALID) {
          ret = OB_INVALID_ARGUMENT;
        } else if (result.error == ::oceanbase::ns::SnapshotForkError::OVERFLOW) {
          ret = OB_SIZE_OVERFLOW;
        } else if (result.error == ::oceanbase::ns::SnapshotForkError::STORE) {
          ret = result.store_error;
        }
      }
    }
  }
  if (OB_SUCC(ret) && !bootstrap) {
    DEBUG_SYNC(AFTER_UPDATE_TABLET_TO_LS);
    ret = THIS_WORKER.check_status();
  }
  const int end = trans.end(ret == OB_SUCCESS); if (ret == OB_SUCCESS) { ret = end; }
  const int64_t published_us = ObTimeUtility::current_time();
  if (ret == OB_SUCCESS && !bootstrap) {
    // The parent link is immutable from this commit on; cache it forever.
    control_state().remember_chain_link(id, source_id, root.snapshot);
  }
  if (ret == OB_SUCCESS && !bootstrap) {
    ret = observer::namespace_worker_prototype::reload_storage_freeze_info();
  }
  if (ret == OB_SUCCESS && target != "__template__"
      && target != "__template_build__") {
    // Register the name so later logins can bind the runtime directly.
    char register_name[ns::Namespace::MAX_NAME_LEN];
    if (target.length() < ns::Namespace::MAX_NAME_LEN) {
      MEMCPY(register_name, target.ptr(), target.length());
      register_name[target.length()] = '\0';
      ns::namespace_registry().add(id, register_name);
    }
  }
  LOG_INFO("PROTOTYPE_V5_NAMESPACE_REGISTER", K(ret), K(id), K(source_id), K(bootstrap),
      "snapshot", root.snapshot, "catalog_root", root.catalog.page, "directory_root", root.directory.page,
      "publish_us", published_us - begin_us, "reload_us", ObTimeUtility::current_time() - published_us);
  return ret;
}
int NamespaceForkKernelPrototype::observe_schema(ObISQLClient &trans, const ObTableSchema &schema) {
  const uint64_t namespace_id = trans.target_namespace();
  const uint64_t schema_namespace = namespace_id > 1 ? namespace_id : 0;
  return observe_schema_in_namespace(trans, schema, schema_namespace);
}

int NamespaceForkKernelPrototype::observe_schema_in_namespace(
    ObISQLClient &trans, const ObTableSchema &schema, uint64_t namespace_id) {
  if ((!schema.is_user_table() && !schema.is_index_table()
      && !schema.is_sys_table() && !schema.is_aux_lob_table())) {
    return OB_SUCCESS;
  }
  if (native_namespace_schema_authority()) {
    // The exception-table model has no per-table directory registration:
    // tablets of a forked namespace resolve along the parent chain until they
    // are materialized, and namespace 1 keeps its raw tablets unregistered.
    return OB_SUCCESS;
  }
  if (namespace_id >= (1ULL << 30)) { return OB_INVALID_ARGUMENT; }
  const bool system_object = schema.is_sys_table() || schema.is_aux_lob_table()
      || (is_inner_table(schema.get_table_id()) && schema.is_index_table());
  uint64_t owner = namespace_id;
  bool has_encoded_id = false;
  auto take_owner = [&](uint64_t id) {
    if (!is_encoded_id(id)) { return true; }
    has_encoded_id = true;
    const uint64_t candidate = database_of(id);
    if (owner == 0) { owner = candidate; }
    return owner == candidate;
  };
  if (!take_owner(schema.get_table_id()) || !take_owner(schema.get_database_id())
      || ((schema.is_aux_lob_table() || schema.is_index_table())
          && !take_owner(schema.get_data_table_id()))) {
    return OB_INVALID_ARGUMENT;
  }
  for (int64_t i = 0; i < schema.get_simple_index_infos().count(); ++i) {
    if (!take_owner(schema.get_simple_index_infos().at(i).table_id_)) { return OB_INVALID_ARGUMENT; }
  }
  ObArray<ObTabletID> schema_tablets;
  if (schema_tablet_ids(schema, schema_tablets) != OB_SUCCESS || schema_tablets.empty()) {
    return OB_INVALID_ARGUMENT;
  }
  for (int64_t i = 0; i < schema_tablets.count(); ++i) {
    if (!take_owner(schema_tablets.at(i).id())) { return OB_INVALID_ARGUMENT; }
  }
  const bool encoded_schema = has_encoded_id;
  const uint64_t database_id = is_encoded_id(schema.get_database_id())
      ? local_of(schema.get_database_id()) : schema.get_database_id();
  const uint64_t table_id = is_encoded_id(schema.get_table_id())
      ? local_of(schema.get_table_id()) : schema.get_table_id();
  std::vector<uint64_t> tablet_ids;
  tablet_ids.reserve(schema_tablets.count());
  for (int64_t i = 0; i < schema_tablets.count(); ++i) {
    const uint64_t id = schema_tablets.at(i).id();
    tablet_ids.push_back(is_encoded_id(id) ? local_of(id) : id);
  }
  const uint64_t tablet_id = tablet_ids.front();
  if (native_namespace_schema_authority()
      && (!directory_supported(schema)
          || database_id >= (1ULL << 30)
          || table_id >= (1ULL << 32)
          || std::any_of(tablet_ids.begin(), tablet_ids.end(),
                         [](uint64_t id) { return id >= (1ULL << 32); }))) {
    return OB_NOT_SUPPORTED;
  }
  const bool all_tablets_encoded = std::all_of(
      schema_tablets.begin(), schema_tablets.end(),
      [](const ObTabletID &id) { return NamespaceForkKernelPrototype::is_encoded_id(id.id()); });
  if (encoded_schema && ((namespace_id == 0 && !is_encoded_id(schema.get_database_id()))
      || !all_tablets_encoded
      || ((!schema.is_aux_lob_table() && !schema.is_index_table())
          && !is_encoded_id(schema.get_table_id()))
      || ((schema.is_aux_lob_table() || schema.is_index_table())
          && !is_encoded_id(schema.get_data_table_id())))) {
    return OB_INVALID_ARGUMENT;
  }
  if (owner == 0) { owner = 1; }
  if (is_inner_db(database_id) && !system_object) { return OB_SUCCESS; }
  int ret = OB_SUCCESS;
  if (!native_namespace_schema_authority()) {
    bool ready = false; ret = namespace_registry_ready(ready);
    if (ret != OB_SUCCESS || !ready) { return ret; }
  }
  if (!system_object && namespace_id == 0) { // Finish the result before issuing another statement on the same DDL connection.
  ObSqlString q; ObMySQLProxy::MySQLResult res; ObString name;
  sqlclient::ObMySQLResult *r = nullptr;
  if (OB_FAIL(q.assign_fmt("SELECT database_name FROM oceanbase.__all_database WHERE database_id=%lu", database_id))) {
  } else if (OB_FAIL(trans.read(res, q.ptr()))) {
  } else if (OB_ISNULL(r = res.get_result())) { ret = OB_ERR_UNEXPECTED;
  } else if (OB_FAIL(r->next())) {
  } else if (OB_FAIL(r->get_varchar(0L, name))) {
  } else if (name.prefix_match("__fork_proto_meta")) { return OB_SUCCESS;
  } else if ((!native_namespace_schema_authority() && !legacy_catalog_supported(schema))
      || database_id >= (1ULL << 30)
      || table_id >= (1ULL << 32) || tablet_id >= (1ULL << 32)) { ret = OB_NOT_SUPPORTED; }
  }
  if (ret != OB_SUCCESS) { return ret; }
  {
  MetadataReadGuard access; if (access.error() != OB_SUCCESS) { return access.error(); }
  Roots root;
  ret = roots(trans, owner, root, true);
  if (ret == OB_ITER_END || ret == OB_TABLE_NOT_EXIST || ret == OB_ERR_BAD_DATABASE) {
    if (ret == OB_ITER_END) {
      ret = roots(trans, 1, root, false, true);
      if (ret == OB_SUCCESS) { return OB_OP_NOT_ALLOW; }
      if (ret != OB_ITER_END) { return ret; }
    }
    return OB_SUCCESS; // Native bootstrap is enrolled once when namespace 1 is registered.
  }
  if (ret == OB_ITER_END) { ret = OB_SUCCESS; }
  if (ret != OB_SUCCESS) { return ret; }
  if (root.snapshot != 0 && !encoded_schema && namespace_id == 0) { return OB_NOT_SUPPORTED; }
  ObArenaAllocator allocator("NsDDLSchema");
  ObTableSchema normalized(&allocator);
  if (OB_FAIL(normalized.assign(schema))) { return ret; }
  if (encoded_schema) {
    // Catalog blobs stay namespace-neutral; schema lookup re-encodes them for
    // the requesting namespace while the directory retains the physical owner.
    normalized.set_database_id(database_id);
    normalized.set_table_id(table_id);
    if (OB_FAIL(rewrite_tablet_ids(normalized,
            [&](uint64_t id, uint64_t &rewritten) {
              if (!is_encoded_id(id) || database_of(id) != owner) {
                return OB_INVALID_ARGUMENT;
              }
              rewritten = local_of(id);
              return OB_SUCCESS;
            }))) {
      return ret;
    }
    if (normalized.get_data_table_id() != 0 && normalized.get_data_table_id() != OB_INVALID_ID) {
      normalized.set_data_table_id(local_of(normalized.get_data_table_id()));
    }
    if (normalized.get_association_table_id() != 0
        && normalized.get_association_table_id() != OB_INVALID_ID) {
      normalized.set_association_table_id(local_of(normalized.get_association_table_id()));
    }
    if (normalized.get_aux_lob_meta_tid() != OB_INVALID_ID) {
      normalized.set_aux_lob_meta_tid(local_of(normalized.get_aux_lob_meta_tid()));
    }
    if (normalized.get_aux_lob_piece_tid() != OB_INVALID_ID) {
      normalized.set_aux_lob_piece_tid(local_of(normalized.get_aux_lob_piece_tid()));
    }
    std::vector<ObAuxTableMetaInfo> indexes = index_infos(normalized);
    normalized.reset_simple_index_infos();
    for (auto &index : indexes) {
      index.table_id_ = local_of(index.table_id_);
      if (OB_FAIL(normalized.add_simple_index_info(index))) { return ret; }
    }
    for (int64_t i = 0; i < normalized.get_foreign_key_infos().count(); ++i) {
      auto &fk = normalized.get_foreign_key_infos().at(i);
      fk.table_id_ = local_of(fk.table_id_);
      fk.child_table_id_ = local_of(fk.child_table_id_);
      fk.parent_table_id_ = local_of(fk.parent_table_id_);
      if (fk.ref_cst_id_ != OB_INVALID_ID) { fk.ref_cst_id_ = local_of(fk.ref_cst_id_); }
    }
    for (int64_t i = 0; i < normalized.get_column_count(); ++i) {
      const_cast<ObColumnSchemaV2 *>(normalized.get_column_schema_by_idx(i))->set_table_id(table_id);
    }
  }
  // A namespace worker persists its canonical schema through the native
  // all_* tables.  The storage directory needs only logical identity and the
  // current physical binding; duplicating ObTableSchema blobs here creates a
  // second schema authority and doubles the long-lived metadata.
  const bool persist_legacy_catalog = !native_namespace_schema_authority();
  std::string serialized;
  int64_t pos = 0;
  uint64_t object = 0;
  std::vector<bool> insert_directory(tablet_ids.size(), true);
  if (native_namespace_schema_authority()) {
    for (size_t i = 0; OB_SUCC(ret) && i < tablet_ids.size(); ++i) {
      Value existing;
      const int find_ret = find(trans, root.directory, key_of(tablet_ids[i]), existing);
      if (find_ret == OB_SUCCESS) {
        uint64_t existing_object = 0;
        uint64_t existing_table = 0;
        uint64_t existing_tablet = 0;
        uint64_t existing_bound = 0;
        if (!entry(existing.data, existing_object, existing_table,
                   existing_tablet, existing_bound)
            || existing_table != table_id || existing_tablet != tablet_ids[i]) {
          ret = OB_STATE_NOT_MATCH;
        } else {
          // An ALTER or a recovered schema delta must not turn an inherited
          // tablet into a locally-owned tablet. Physical ownership changes only
          // after materialization commits its create MDS and directory update.
          insert_directory[i] = false;
        }
      } else if (find_ret != OB_ENTRY_NOT_EXIST) {
        ret = find_ret;
      }
    }
  }
  if (OB_SUCC(ret) && persist_legacy_catalog) {
    serialized.resize(normalized.get_serialize_size());
    if (OB_FAIL(normalized.serialize(&serialized[0], serialized.size(), pos))) {
    } else if (OB_FAIL(save_blob(trans, serialized, object))) {
    }
  }
  if (OB_SUCC(ret)) {
    Value value;
    const uint64_t bound_tablet = encoded_schema ? schema_tablets.at(0).id()
        : namespace_id > 1 ? NamespaceObjectKey{namespace_id, tablet_id}.storage_id() : 0;
    value.data = entry(object, table_id, tablet_id, bound_tablet);
    const std::string name_key = "T" + key_of(database_id) + "/" + schema.get_table_name();
    if (persist_legacy_catalog
        && OB_FAIL(put(trans, root.catalog, name_key, value, root.catalog))) {
    } else if (persist_legacy_catalog
        && OB_FAIL(put(trans, root.catalog, "#" + key_of(table_id), value, root.catalog))) {
    }
    for (size_t i = 0; OB_SUCC(ret) && i < tablet_ids.size(); ++i) {
      if (!insert_directory[i]) { continue; }
      const uint64_t tablet_bound = encoded_schema ? schema_tablets.at(i).id()
          : namespace_id > 1
              ? NamespaceObjectKey{namespace_id, tablet_ids[i]}.storage_id() : 0;
      value.data = entry(object, table_id, tablet_ids[i], tablet_bound);
      ret = put(trans, root.directory, key_of(tablet_ids[i]), value, root.directory);
    }
    if (OB_SUCC(ret)) {
      root.schema_version = std::max(root.schema_version, schema.get_schema_version());
      ret = save_roots(trans, owner, root);
      for (size_t i = 0; OB_SUCC(ret) && i < tablet_ids.size(); ++i) {
        Value verified;
        ret = find(trans, root.directory, key_of(tablet_ids[i]), verified);
      }
      LOG_INFO("PROTOTYPE_V2_SOURCE_DIRECTORY", K(ret),
          "table_id", schema.get_table_id(), "tablet_count", tablet_ids.size(),
          "directory_root", root.directory.page);
    }
  }
  } // Reader guard ends; the external native DDL transaction still owns the root.
  if (OB_SUCC(ret)) {
    DEBUG_SYNC(BEFORE_CREATE_TABLE_TRANS_COMMIT);
    ret = THIS_WORKER.check_status();
  }
  return ret;
}
int NamespaceForkKernelPrototype::observe_schemas(ObISQLClient &trans,
                                                  const ObIArray<ObTableSchema> &schemas) {
  int ret = OB_SUCCESS;
  for (int64_t i = 0; OB_SUCC(ret) && i < schemas.count(); ++i) {
    const ObTableSchema &schema = schemas.at(i);
    if (!schema.is_user_table()) {
      ret = observe_schema(trans, schema);
    } else {
      ObArenaAllocator allocator("NsDDLBatch");
      ObTableSchema complete(&allocator);
      std::vector<ObAuxTableMetaInfo> indexes = index_infos(schema);
      const uint64_t data_table_id = is_encoded_id(schema.get_table_id())
          ? local_of(schema.get_table_id()) : schema.get_table_id();
      for (int64_t j = 0; j < schemas.count(); ++j) {
        const ObTableSchema &auxiliary = schemas.at(j);
        const uint64_t auxiliary_data_table_id = is_encoded_id(auxiliary.get_data_table_id())
            ? local_of(auxiliary.get_data_table_id()) : auxiliary.get_data_table_id();
        if (auxiliary.is_index_table() && auxiliary_data_table_id == data_table_id) {
          indexes.emplace_back(auxiliary.get_table_id(), auxiliary.get_table_type(),
                               auxiliary.get_index_type());
        }
      }
      std::sort(indexes.begin(), indexes.end(), [](const ObAuxTableMetaInfo &left,
                                                   const ObAuxTableMetaInfo &right) {
        return left.table_id_ < right.table_id_;
      });
      indexes.erase(std::unique(indexes.begin(), indexes.end(),
                                [](const ObAuxTableMetaInfo &left,
                                   const ObAuxTableMetaInfo &right) {
        return left.table_id_ == right.table_id_;
      }), indexes.end());
      if (OB_FAIL(complete.assign(schema))) {
      } else {
        complete.reset_simple_index_infos();
        for (const auto &index : indexes) {
          if (OB_FAIL(complete.add_simple_index_info(index))) { break; }
        }
      }
      if (OB_SUCC(ret)) { ret = observe_schema(trans, complete); }
    }
  }
  return ret;
}

int NamespaceForkKernelPrototype::forget_schema(ObISQLClient &trans, const ObTableSchema &schema,
                                                int64_t schema_version, bool *private_tablet) {
  const uint64_t namespace_id = trans.target_namespace();
  const uint64_t schema_namespace = namespace_id > 1 ? namespace_id : 0;
  return forget_schema_in_namespace(
      trans, schema, schema_version, schema_namespace, private_tablet);
}

int NamespaceForkKernelPrototype::forget_schema_in_namespace(
    ObISQLClient &trans, const ObTableSchema &schema, int64_t schema_version,
    uint64_t namespace_id, bool *private_tablet,
    ObIArray<ObTabletID> *private_tablets,
    const ObTableSchema *replacement_schema) {
  if (private_tablet != nullptr) { *private_tablet = false; }
  if (native_namespace_schema_authority()) {
    // Namespace DDL in a fork publishes its tablet ownership delta through
    // publish_schema_delta; namespace 1 keeps raw tablets and needs no
    // unregistration either.
    return OB_SUCCESS;
  }
  // Dropping namespace 1 deletes its native schema rows and then clears the
  // whole namespace root.  Per-table directory updates would read that root as
  // LIVE after begin_namespace_drop() has already made it DELETING.  They are
  // also unnecessary: descendant snapshots retain the old immutable root and
  // finish_namespace_drop() discards namespace 1's mutable root as one unit.
  if (source_drop_trans.load() == &trans) {
    return OB_SUCCESS;
  }
  if ((!schema.is_user_table() && !schema.is_index_table()
      && !schema.is_aux_lob_table())) {
    return OB_SUCCESS;
  }
  const bool encoded_schema = is_encoded_id(schema.get_table_id());
  const uint64_t owner = namespace_id != 0 ? namespace_id
      : encoded_schema ? database_of(schema.get_table_id())
      : 1;
  ObArray<ObTabletID> schema_tablets;
  if (schema_tablet_ids(schema, schema_tablets) != OB_SUCCESS || schema_tablets.empty()) {
    return OB_INVALID_ARGUMENT;
  }
  bool encoded_tablets_valid = true;
  for (int64_t i = 0; encoded_schema && encoded_tablets_valid
      && i < schema_tablets.count(); ++i) {
    encoded_tablets_valid = is_encoded_id(schema_tablets.at(i).id())
        && database_of(schema_tablets.at(i).id()) == owner;
  }
  if (owner == 0 || owner >= (1ULL << 30) || schema_version <= 0
      || (encoded_schema && ((namespace_id == 0 && !is_encoded_id(schema.get_database_id()))
          || !encoded_tablets_valid
          || (is_encoded_id(schema.get_database_id())
              && database_of(schema.get_database_id()) != owner)))) {
    return OB_INVALID_ARGUMENT;
  }
  const uint64_t database_id = encoded_schema ? local_of(schema.get_database_id())
      : schema.get_database_id();
  const uint64_t table_id = encoded_schema ? local_of(schema.get_table_id())
      : schema.get_table_id();
  std::vector<uint64_t> tablet_ids;
  tablet_ids.reserve(schema_tablets.count());
  for (int64_t i = 0; i < schema_tablets.count(); ++i) {
    tablet_ids.push_back(is_encoded_id(schema_tablets.at(i).id())
        ? local_of(schema_tablets.at(i).id()) : schema_tablets.at(i).id());
  }
  // For an in-place schema replacement, retain bindings that are present in
  // both schema versions.  ALTER TABLE commonly keeps all tablets, while
  // TRUNCATE and repartitioning may replace all or only a subset.  Removing
  // every old binding and observing the new schema afterwards would silently
  // turn retained inherited tablets into child-owned tablets.
  std::unordered_set<uint64_t> retained_tablet_ids;
  if (replacement_schema != nullptr) {
    const bool replacement_encoded = is_encoded_id(replacement_schema->get_table_id());
    const uint64_t replacement_owner = replacement_encoded
        ? database_of(replacement_schema->get_table_id()) : owner;
    const uint64_t replacement_table_id = replacement_encoded
        ? local_of(replacement_schema->get_table_id())
        : replacement_schema->get_table_id();
    ObArray<ObTabletID> replacement_tablets;
    const int replacement_ret = schema_tablet_ids(
        *replacement_schema, replacement_tablets);
    if (replacement_owner != owner || replacement_table_id != table_id
        || replacement_ret != OB_SUCCESS
        || replacement_tablets.empty()) {
      return replacement_ret != OB_SUCCESS ? replacement_ret : OB_INVALID_ARGUMENT;
    }
    for (int64_t i = 0; i < replacement_tablets.count(); ++i) {
      const uint64_t id = replacement_tablets.at(i).id();
      if (replacement_encoded
          && (!is_encoded_id(id) || database_of(id) != owner)) {
        return OB_INVALID_ARGUMENT;
      }
      retained_tablet_ids.insert(is_encoded_id(id) ? local_of(id) : id);
    }
  }
  // Namespace mode can be enabled before the global registry is installed
  // (notably during the worker-only bootstrap probe).  CREATE already gates
  // directory maintenance on registry visibility; DROP must make the same
  // decision before trying to compile SQL against the control tables.
  bool registry_ready = false;
  int ret = namespace_registry_ready(registry_ready);
  if (ret != OB_SUCCESS || !registry_ready) { return ret; }
  MetadataReadGuard access; if (access.error() != OB_SUCCESS) { return access.error(); }
  Roots root; ret = roots(trans, owner, root, true);
  if (ret == OB_ITER_END) {
    // The global control schema is installed before namespace 1 receives its
    // first snapshot root. DDL in that interval is captured by the one-time
    // enrollment, so a DROP has no directory entry to remove yet. Preserve
    // the tombstone check used by CREATE to prevent writes after deletion.
    ret = roots(trans, owner, root, false, true);
    if (ret == OB_SUCCESS) { return OB_OP_NOT_ALLOW; }
    if (ret != OB_ITER_END) { return ret; }
    return OB_SUCCESS;
  }
  Ref next;
  const std::string name_key = "T" + key_of(database_id) + "/" + schema.get_table_name();
  bool all_private = !tablet_ids.empty();
  bool removed_any = false;
  for (size_t i = 0; OB_SUCC(ret) && i < tablet_ids.size(); ++i) {
    if (retained_tablet_ids.count(tablet_ids[i]) != 0) { continue; }
    removed_any = true;
    Value directory_value;
    if (OB_FAIL(find(trans, root.directory, key_of(tablet_ids[i]), directory_value))) {
    } else {
      uint64_t object = 0, entry_table = 0, entry_tablet = 0, bound = 0;
      if (!entry(directory_value.data, object, entry_table, entry_tablet, bound)
          || entry_table != table_id || entry_tablet != tablet_ids[i]) {
        ret = OB_CHECKSUM_ERROR;
      } else {
        const uint64_t physical_tablet = encoded_schema ? schema_tablets.at(i).id()
            : NamespaceObjectKey{owner, tablet_ids[i]}.storage_id();
        const bool locally_owned = bound == physical_tablet;
        all_private = all_private && locally_owned;
        if (locally_owned && private_tablets != nullptr
            && OB_FAIL(private_tablets->push_back(ObTabletID(physical_tablet)))) {
        }
      }
    }
  }
  all_private = removed_any && all_private;
  if (OB_SUCC(ret) && private_tablet != nullptr) { *private_tablet = all_private; }
  if (OB_FAIL(ret)) {
  } else if (!native_namespace_schema_authority()
      && OB_FAIL(remove_key(trans, root.catalog, name_key, next))) {
  } else if (!native_namespace_schema_authority()
      && FALSE_IT(root.catalog = next)) {
  } else if (!native_namespace_schema_authority()
      && OB_FAIL(remove_key(trans, root.catalog, "#" + key_of(table_id), next))) {
  } else if (!native_namespace_schema_authority()
      && FALSE_IT(root.catalog = next)) {
  }
  for (size_t i = 0; OB_SUCC(ret) && i < tablet_ids.size(); ++i) {
    if (retained_tablet_ids.count(tablet_ids[i]) != 0) { continue; }
    if (OB_FAIL(remove_key(trans, root.directory, key_of(tablet_ids[i]), next))) {
    } else {
      root.directory = next;
    }
  }
  if (OB_SUCC(ret)) {
    root.schema_version = std::max(root.schema_version, schema_version);
    ret = save_roots(trans, owner, root);
  }
  return ret;
}

namespace {

struct DirectoryTabletState
{
  uint64_t table_id = OB_INVALID_ID;
  uint64_t physical_tablet_id = OB_INVALID_ID;
};

bool owns_namespace_directory_entries(const ObTableSchema &schema)
{
  return schema.is_user_table() || schema.is_index_table()
      || schema.is_aux_lob_table();
}

int collect_directory_tablets(
    uint64_t namespace_id,
    const ObIArray<const ObTableSchema *> &schemas,
    std::map<uint64_t, DirectoryTabletState> &tablets)
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
      const bool encoded_schema = NamespaceForkKernelPrototype::is_encoded_id(
          schema->get_table_id());
      const uint64_t table_owner = encoded_schema
          ? database_of(schema->get_table_id()) : namespace_id;
      const uint64_t table_id = encoded_schema
          ? local_of(schema->get_table_id()) : schema->get_table_id();
      ObArray<ObTabletID> schema_tablets;
      if (table_owner != namespace_id || table_id >= (1ULL << 32)
          || OB_FAIL(schema_tablet_ids(*schema, schema_tablets))) {
        if (OB_SUCC(ret)) { ret = OB_INVALID_ARGUMENT; }
      }
      for (int64_t j = 0; OB_SUCC(ret) && j < schema_tablets.count(); ++j) {
        const uint64_t schema_tablet_id = schema_tablets.at(j).id();
        const bool encoded_tablet = NamespaceForkKernelPrototype::is_encoded_id(
            schema_tablet_id);
        const uint64_t tablet_owner = encoded_tablet
            ? database_of(schema_tablet_id) : namespace_id;
        const uint64_t tablet_id = encoded_tablet
            ? local_of(schema_tablet_id) : schema_tablet_id;
        const NamespaceObjectKey storage_key{namespace_id, tablet_id};
        if (encoded_tablet != encoded_schema || tablet_owner != namespace_id
            || !storage_key.is_valid()) {
          ret = OB_INVALID_ARGUMENT;
        } else {
          DirectoryTabletState state;
          state.table_id = table_id;
          state.physical_tablet_id = encoded_tablet
              ? schema_tablet_id : storage_key.storage_id();
          if (!tablets.emplace(tablet_id, state).second) {
            ret = OB_STATE_NOT_MATCH;
          }
        }
      }
    }
  }
  return ret;
}

// Apply a complete native-schema delta to the namespace's exception set.
// Looking at the whole delta is essential: operations such as EXCHANGE
// PARTITION move an existing tablet between tables without changing its
// physical binding. Per-table forget/observe calls cannot distinguish that
// move from a drop followed by a conflicting create.
int replace_namespace_exceptions(
    ObISQLClient &trans,
    uint64_t namespace_id,
    int64_t schema_version,
    const ObIArray<const ObTableSchema *> &current_schemas,
    const ObIArray<const ObTableSchema *> &previous_schemas,
    ObIArray<ObTabletID> &private_tablets)
{
  std::map<uint64_t, DirectoryTabletState> previous_tablets;
  std::map<uint64_t, DirectoryTabletState> current_tablets;
  int ret = collect_directory_tablets(
      namespace_id, previous_schemas, previous_tablets);
  if (OB_SUCC(ret)) {
    ret = collect_directory_tablets(
        namespace_id, current_schemas, current_tablets);
  }
  MetadataReadGuard access;
  if (OB_SUCC(ret) && access.error() != OB_SUCCESS) {
    ret = access.error();
  }
  Roots root;
  if (OB_SUCC(ret)) {
    // The row lock serializes this delta against forks and materializations.
    ret = roots(trans, namespace_id, root, true);
  }
  if (OB_SUCC(ret)) {
    ret = load_exceptions(trans, namespace_id);
  }

  // Tablets that disappeared from the changed-schema set are dropped here:
  // physically delete the ones this namespace owns, and tombstone every one
  // so no later read can fall through to an inherited ancestor copy.
  for (auto it = previous_tablets.begin(); OB_SUCC(ret)
      && it != previous_tablets.end(); ++it) {
    const uint64_t tablet_id = it->first;
    if (current_tablets.count(tablet_id) != 0) { continue; }
    if (control_state().owned(namespace_id, tablet_id)) {
      const NamespaceObjectKey key{namespace_id, tablet_id};
      if (!key.is_valid()) {
        ret = OB_INVALID_ARGUMENT;
      } else {
        ret = private_tablets.push_back(ObTabletID(key.storage_id()));
      }
    }
    ObSqlString q;
    if (OB_FAIL(ret)) {
    } else if (OB_FAIL(q.assign_fmt("REPLACE INTO %s VALUES(%lu,%lu,%lu,1,0)",
        EXCEPTIONS, namespace_id, tablet_id, it->second.table_id))) {
    } else {
      ret = write_sql(trans, q);
    }
  }

  // Truly new logical tablets are physically created by the worker's DDL and
  // become owned rows here. A kept tablet only needs its table id refreshed
  // when the delta moved it between tables.
  for (auto it = current_tablets.begin(); OB_SUCC(ret)
      && it != current_tablets.end(); ++it) {
    const uint64_t tablet_id = it->first;
    const auto previous_it = previous_tablets.find(tablet_id);
    ObSqlString q;
    if (previous_it == previous_tablets.end()) {
      // A recovery delta can list tablets this namespace only inherits: their
      // physical copy lives in an ancestor, so they must stay chain-resolved.
      // Only a tablet whose physical copy exists locally becomes an owned row.
      const NamespaceObjectKey key{namespace_id, tablet_id};
      bool exists = false;
      if (!key.is_valid()) {
        ret = OB_INVALID_ARGUMENT;
      } else if (OB_FAIL(probe_physical_tablet(key.storage_id(), exists))) {
      } else if (!exists) {
      } else if (OB_FAIL(q.assign_fmt("REPLACE INTO %s VALUES(%lu,%lu,%lu,0,0)",
          EXCEPTIONS, namespace_id, tablet_id, it->second.table_id))) {
      } else {
        ret = write_sql(trans, q);
      }
    } else if (previous_it->second.table_id != it->second.table_id) {
      if (OB_FAIL(q.assign_fmt(
          "UPDATE %s SET table_id=%lu WHERE namespace_id=%lu AND tablet_id=%lu AND kind=0",
          EXCEPTIONS, it->second.table_id, namespace_id, tablet_id))) {
      } else {
        ret = write_sql(trans, q);
      }
    }
  }

  if (OB_SUCC(ret) && schema_version > root.schema_version) {
    ObSqlString q;
    if (OB_FAIL(q.assign_fmt("UPDATE %s SET schema_version=%ld WHERE namespace_id=%lu",
        NAMESPACES, schema_version, namespace_id))) {
    } else {
      ret = write_sql(trans, q);
    }
  }
  return ret;
}

} // namespace

int NamespaceForkKernelPrototype::publish_schema_delta(
    uint64_t namespace_id,
    int64_t schema_version,
    const ObIArray<const ObTableSchema *> &current_schemas,
    const ObIArray<const ObTableSchema *> &previous_schemas) {
  if (namespace_id <= 1 || namespace_id >= (1ULL << 30)
      || schema_version <= 0 || GCTX.sql_proxy_ == nullptr) {
    return OB_INVALID_ARGUMENT;
  }
  int ret = OB_SUCCESS;
  ObArray<ObTabletID> private_tablets;
  std::map<uint64_t, const ObTableSchema *> current_by_table_id;
  std::unordered_set<uint64_t> previous_table_ids;
  for (int64_t i = 0; OB_SUCC(ret) && i < current_schemas.count(); ++i) {
    const ObTableSchema *schema = current_schemas.at(i);
    if (schema == nullptr) {
      ret = OB_INVALID_ARGUMENT;
    } else {
      const uint64_t id = is_encoded_id(schema->get_table_id())
          ? local_of(schema->get_table_id()) : schema->get_table_id();
      if (!current_by_table_id.emplace(id, schema).second) {
        ret = OB_INVALID_ARGUMENT;
      }
    }
  }
  for (int64_t i = 0; OB_SUCC(ret) && i < previous_schemas.count(); ++i) {
    const ObTableSchema *schema = previous_schemas.at(i);
    if (schema == nullptr) {
      ret = OB_INVALID_ARGUMENT;
    } else {
      const uint64_t id = is_encoded_id(schema->get_table_id())
          ? local_of(schema->get_table_id()) : schema->get_table_id();
      if (!previous_table_ids.insert(id).second) {
        ret = OB_INVALID_ARGUMENT;
      }
    }
  }
  {
    // The native schema rows were committed by the target Worker. This
    // transaction mutates only the global namespace directory, so route its
    // SQL through the control Worker. Child Workers deliberately do not load
    // or resolve the global control schema.
    ObMySQLTransaction trans;
    if (OB_SUCC(ret)) { ret = trans.start(GCTX.sql_proxy_); }
    if (OB_SUCC(ret) && native_namespace_schema_authority()) {
      ret = replace_namespace_exceptions(
          trans, namespace_id, schema_version,
          current_schemas, previous_schemas, private_tablets);
    } else {
      for (int64_t i = 0; OB_SUCC(ret) && i < previous_schemas.count(); ++i) {
        const ObTableSchema *previous = previous_schemas.at(i);
        const uint64_t id = is_encoded_id(previous->get_table_id())
            ? local_of(previous->get_table_id()) : previous->get_table_id();
        const auto current = current_by_table_id.find(id);
        if (OB_FAIL(forget_schema_in_namespace(
            trans, *previous, schema_version, namespace_id,
            nullptr, &private_tablets,
            current == current_by_table_id.end() ? nullptr : current->second))) {
        }
      }
      for (int64_t i = 0; OB_SUCC(ret) && i < current_schemas.count(); ++i) {
        ret = observe_schema_in_namespace(
            trans, *current_schemas.at(i), namespace_id);
      }
      if (OB_SUCC(ret)) {
        Roots root;
        if (OB_FAIL(roots(trans, namespace_id, root, true))) {
        } else if (schema_version > root.schema_version) {
          root.schema_version = schema_version;
          ret = save_roots(trans, namespace_id, root);
        }
      }
    }
    if (OB_SUCC(ret)) {
      ObSqlString q;
      if (OB_FAIL(q.assign_fmt(
          "UPDATE %s SET pending_schema_version=0 "
          "WHERE namespace_id=%lu AND pending_schema_version>0 "
          "AND pending_schema_version<=%ld",
          NAMESPACES, namespace_id, schema_version))) {
      } else {
        ret = write_sql(trans, q);
      }
    }
    const int end_ret = trans.end(OB_SUCC(ret));
    if (OB_SUCC(ret)) { ret = end_ret; }
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
    ret = trans.start(GCTX.sql_proxy_);
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
      } else if (OB_FAIL(tablet_drop.execute())) {
      }
    }
    if (trans.is_started()) {
      const int end_ret = trans.end(OB_SUCC(ret));
      if (OB_SUCC(ret)) { ret = end_ret; }
    }
  }
  LOG_INFO("PROTOTYPE_NAMESPACE_SCHEMA_DELTA", K(ret), K(namespace_id), K(schema_version),
      "current_count", current_schemas.count(),
      "previous_count", previous_schemas.count(),
      "private_delete_count", private_tablets.count());
  return ret;
}
int NamespaceForkKernelPrototype::is_tablet_owned(
    uint64_t namespace_id, const ObTabletID &tablet_id, bool &owned) {
  owned = false;
  uint64_t local_tablet_id = OB_INVALID_ID;
  if (namespace_id <= 1 || namespace_id >= (1ULL << 30)
      || !tablet_id.is_valid() || !GCTX.sql_proxy_) {
    return OB_INVALID_ARGUMENT;
  }
  int ret = local_object_id(namespace_id, tablet_id.id(), local_tablet_id);
  if (OB_FAIL(ret)) { return ret; }
  MetadataReadGuard access; if (access.error() != OB_SUCCESS) { return access.error(); }
  // Ownership is exactly the owned exception row: the physical tablet id is a
  // pure function of (namespace, local tablet), so no binding is recorded.
  if (OB_FAIL(load_exceptions(*GCTX.sql_proxy_, namespace_id))) {
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
      || namespace_id >= (1ULL << 30) || !GCTX.sql_proxy_) {
    return OB_INVALID_ARGUMENT;
  }
  MetadataReadGuard access;
  if (access.error() != OB_SUCCESS) { return access.error(); }
  int ret = load_exceptions(*GCTX.sql_proxy_, namespace_id);
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
void NamespaceForkKernelPrototype::release_schema(uint64_t table_id) {
  std::lock_guard<std::mutex> lock(schema_mutex);
  schemas.erase(table_id);
}
int NamespaceForkKernelPrototype::release_namespace_schemas(
    uint64_t namespace_id,
    int64_t &table_count,
    int64_t &database_count) {
  table_count = 0;
  database_count = 0;
  if (namespace_id <= 1
      || namespace_id >= (1ULL << 30)) {
    return OB_INVALID_ARGUMENT;
  }
  // DROP tombstones the namespace and drains storage access before committing.
  // After that commit no shared request can acquire another pointer from these
  // compatibility holders. Worker SchemaService memory is released with the
  // Worker process; only this shared adapter cache needs explicit erasure.
  std::lock_guard<std::mutex> lock(schema_mutex);
  for (auto it = schemas.begin(); it != schemas.end();) {
    if (is_encoded_id(it->first) && database_of(it->first) == namespace_id) {
      it = schemas.erase(it);
      ++table_count;
    } else {
      ++it;
    }
  }
  for (auto it = database_schemas.begin(); it != database_schemas.end();) {
    if (is_encoded_id(it->first) && database_of(it->first) == namespace_id) {
      it = database_schemas.erase(it);
      ++database_count;
    } else {
      ++it;
    }
  }
  return OB_SUCCESS;
}
int NamespaceForkKernelPrototype::capture(ObISQLClient &trans, uint64_t source, uint64_t target,
                                          int64_t snapshot, int64_t schema_version) {
  MetadataReadGuard access; if (access.error() != OB_SUCCESS) { return access.error(); }
  Roots root; int ret = roots(trans, source, root, true);
  if (ret == OB_ITER_END) { ret = OB_SUCCESS; }
  if (ret != OB_SUCCESS) { return ret; }
  if (root.snapshot || target >= (1ULL << 30) || root.schema_version > schema_version) { return OB_NOT_SUPPORTED; }
  root.source = source; root.snapshot = snapshot; root.schema_version = schema_version;
  root.catalog.cap = cap_min(root.catalog.cap, snapshot); root.directory.cap = cap_min(root.directory.cap, snapshot);
  ret = save_roots(trans, target, root);
  LOG_INFO("PROTOTYPE_V2_ROOT_CAPTURE", K(ret), K(source), K(target), K(snapshot),
           "catalog_root", root.catalog.page, "directory_root", root.directory.page);
  return ret;
}
int NamespaceForkKernelPrototype::schema_by_name(uint64_t db, const ObString &name, const ObTableSchema *&schema) {
  schema = nullptr; if (!GCTX.sql_proxy_) { return OB_NOT_INIT; }
  MetadataReadGuard access; if (access.error() != OB_SUCCESS) { return access.error(); }
  const uint64_t owner = namespace_of(db);
  Roots root; Value value; int ret = roots(*GCTX.sql_proxy_, owner, root);
  if (ret == OB_ITER_END) { return OB_SUCCESS; }
  if (ret != OB_SUCCESS) { return ret; }
  DEBUG_SYNC(BEFORE_FETCH_SIMPLE_TABLES);
  if (OB_FAIL(THIS_WORKER.check_status())) { return ret; }
  const std::string name_key = "T" + key_of(local_of(db)) + "/"
      + std::string(name.ptr(), name.length());
  ret = find(*GCTX.sql_proxy_, root.catalog, name_key, value);
  return ret == OB_ENTRY_NOT_EXIST ? OB_SUCCESS : ret != OB_SUCCESS ? ret : schema_from_value(owner, value, schema);
}
int NamespaceForkKernelPrototype::schema_by_id(uint64_t id, const ObTableSchema *&schema) {
  schema = nullptr; if (!is_encoded_id(id)) { return OB_INVALID_ARGUMENT; }
  { std::lock_guard<std::mutex> lock(schema_mutex);
    auto it = schemas.find(id); if (it != schemas.end()) { schema = &it->second->schema; return OB_SUCCESS; } }
  if (!GCTX.sql_proxy_) { return OB_NOT_INIT; }
  MetadataReadGuard access; if (access.error() != OB_SUCCESS) { return access.error(); }
  Roots root; Value value; int ret = roots(*GCTX.sql_proxy_, database_of(id), root);
  if (ret != OB_SUCCESS) { return ret; }
  if (!root.snapshot) { return OB_TABLE_NOT_EXIST; }
  ret = find(*GCTX.sql_proxy_, root.catalog, "#" + key_of(local_of(id)), value);
  return ret != OB_SUCCESS ? ret : schema_from_value(database_of(id), value, schema);
}
int NamespaceForkKernelPrototype::table_id_for_tablet(const ObTabletID &tablet, int64_t schema_version,
                                                     uint64_t &table_id) {
  table_id = OB_INVALID_ID;
  if (!is_encoded_id(tablet.id()) || !GCTX.sql_proxy_) { return OB_INVALID_ARGUMENT; }
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
  const int ret = load_exceptions(*GCTX.sql_proxy_, db);
  if (ret != OB_SUCCESS) { return ret; }
  if (!control_state().owned(db, local_of(tablet.id()), &local_table)) { return OB_SUCCESS; }
  table_id = encoded(db, local_table);
  remember_tablet_table(tablet.id(), table_id);
  return OB_SUCCESS;
}
int NamespaceForkKernelPrototype::list_schemas(uint64_t db, ObIArray<const ObTableSchema *> &out) {
  MetadataReadGuard access; if (access.error() != OB_SUCCESS) { return access.error(); }
  const uint64_t owner = namespace_of(db);
  Roots root; int ret = roots(*GCTX.sql_proxy_, owner, root);
  if (ret == OB_ITER_END) { return OB_SUCCESS; }
  if (ret != OB_SUCCESS || !root.snapshot) { return ret; }
  std::vector<Ref> pending; if (root.catalog.page) { pending.push_back(root.catalog); }
  while (!pending.empty() && ret == OB_SUCCESS) {
    Ref ref = pending.back(); pending.pop_back(); Node node;
    if ((ret = read_node(*GCTX.sql_proxy_, ref, node)) != OB_SUCCESS) { break; }
    if (node.leaf) {
      for (size_t i = 0; i < node.keys.size() && ret == OB_SUCCESS; ++i) {
        // The catalog indexes each schema by id and by name. Enumerate only the id index.
        if (!node.keys[i].empty() && node.keys[i][0] == '#') {
          const ObTableSchema *schema = nullptr;
          if ((ret = schema_from_value(owner, node.values[i], schema)) == OB_SUCCESS
              && schema->get_database_id() == db) { ret = out.push_back(schema); }
        }
      }
    } else { pending.insert(pending.end(), node.children.rbegin(), node.children.rend()); }
  }
  return ret;
}
int NamespaceForkKernelPrototype::check_ddl(const ObSimpleTableSchemaV2 &schema, const ObISQLClient *trans) {
  if (trans && source_drop_trans.load() == trans) { return OB_SUCCESS; }
  if (!schema.is_user_table()) { return OB_SUCCESS; }
  if (is_encoded_id(schema.get_table_id())) {
    if (!is_encoded_id(schema.get_database_id())
        || database_of(schema.get_database_id()) != database_of(schema.get_table_id())) {
      return OB_INVALID_ARGUMENT;
    }
    return OB_SUCCESS;
  }
  {
    bool ready = false; const int ret = namespace_registry_ready(ready);
    if (ret != OB_SUCCESS || !ready) { return ret; }
  }
  ObSchemaGetterGuard guard; const ObDatabaseSchema *db = nullptr;
  int ret = GSCHEMASERVICE.get_runtime_schema_guard(guard);
  if (ret != OB_SUCCESS) { return ret; }
  if ((ret = guard.get_database_schema(schema.get_database_id(), db)) != OB_SUCCESS || !db) { return ret; }
  if (db->get_database_name_str().prefix_match("__fork_proto_b")) { return OB_NOT_SUPPORTED; }
  if (observer::namespace_worker_prototype::is_namespace_control_database(
          db->get_database_name_str())) {
    return !observer::namespace_worker_prototype::can_access_namespace_control_database()
        ? OB_NOT_SUPPORTED : OB_SUCCESS;
  }
  MetadataReadGuard access; if (access.error() != OB_SUCCESS) { return access.error(); }
  Roots root; Value existing;
  ret = roots(*GCTX.sql_proxy_, 1, root);
  if (ret == OB_ITER_END) { return OB_SUCCESS; }
  if (ret != OB_SUCCESS) { return ret; }
  ret = find(*GCTX.sql_proxy_, root.catalog, "#" + key_of(schema.get_table_id()), existing);
  // Fixed physical definitions: new source tables are supported; existing ones cannot be altered/dropped.
  return ret == OB_ENTRY_NOT_EXIST ? OB_SUCCESS : ret == OB_SUCCESS ? OB_NOT_SUPPORTED : ret;
}
int NamespaceForkKernelPrototype::schedule_baseline(const ObTablet &tablet) {
  const auto &meta = tablet.get_tablet_meta();
  if (!is_encoded_id(meta.tablet_id_.id()) || tablet.is_empty_shell()
      || !meta.fork_info_.is_valid() || meta.fork_info_.is_complete()) { return OB_SUCCESS; }
  if (!GCTX.sql_proxy_) { return OB_NOT_INIT; }
  MetadataReadGuard access; if (access.error() != OB_SUCCESS) { return access.error(); }
  int ret = OB_SUCCESS;
  ObArenaAllocator allocator("NsForkBaseline");
  ObStorageSchema *storage_schema = nullptr;
  const uint64_t db = database_of(meta.tablet_id_.id());
  const uint64_t local = local_of(meta.tablet_id_.id());
  uint64_t table = OB_INVALID_ID;
  // The owned exception row is the proof that this tablet's physical binding
  // committed. A dropped namespace has no rows left and simply skips.
  if (OB_FAIL(load_exceptions(*GCTX.sql_proxy_, db))) {
  } else if (!control_state().owned(db, local, &table)) {
    ret = OB_ENTRY_NOT_EXIST;
  } else if (OB_FAIL(tablet.load_storage_schema(allocator, storage_schema))) {
  } else if (OB_ISNULL(storage_schema)) {
    ret = OB_ERR_UNEXPECTED;
  } else {
    ObTabletForkParam param; bool ready = false;
    // The exception table owns logical-to-physical identity; the tablet owns
    // the physical schema needed by compaction.
    param.table_id_ = is_inner_table(table) ? table : encoded(db, table);
    param.schema_version_ = storage_schema->get_schema_version();
    // Stable DAG identity only; no rootserver DDL task is created.
    param.task_id_ = meta.tablet_id_.id();
    param.source_tablet_id_ = meta.fork_info_.get_fork_src_tablet_id();
    param.dest_tablet_id_ = meta.tablet_id_; param.fork_snapshot_version_ = meta.fork_info_.get_fork_snapshot_version();
    param.data_format_version_ = DATA_CURRENT_VERSION;
    if (OB_FAIL(ObTabletForkUtil::check_satisfy_fork_condition(param, ready))) {
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
int NamespaceForkKernelPrototype::ensure_tablet(const ObTabletID &tablet_id) {
  return ensure_tablet_impl(tablet_id, nullptr, nullptr);
}
int NamespaceForkKernelPrototype::resolve_read_tablet(
    const ObTabletID &tablet_id, ObTabletID &physical_tablet_id, int64_t &cap_scn) {
  physical_tablet_id = tablet_id; cap_scn = 0;
  if (!is_encoded_id(tablet_id.id())) { return OB_SUCCESS; }
  {
    // Committed local tablets serve their own reads; only a miss means the
    // tablet is still inherited and must resolve along the parent chain.
    bool exists = false;
    const int ret = probe_physical_tablet(tablet_id.id(), exists);
    if (ret != OB_SUCCESS) { return ret; }
    if (exists) { return OB_SUCCESS; }
  }
  if (!GCTX.sql_proxy_) { return OB_NOT_INIT; }
  MetadataReadGuard access; if (access.error() != OB_SUCCESS) { return access.error(); }
  const uint64_t db = database_of(tablet_id.id());
  const uint64_t local = local_of(tablet_id.id());
  int ret = load_exceptions(*GCTX.sql_proxy_, db);
  if (OB_FAIL(ret)) { return ret; }
  if (control_state().tombstoned(db, local)) {
    // Dropped in this namespace: never fall through to an inherited copy.
    return OB_TABLET_NOT_EXIST;
  }
  if (control_state().owned(db, local)) {
    // Owned here; the creation commit is ahead of tablet-manager visibility,
    // so let the caller's tablet open wait out the transient instead of failing.
    return OB_SUCCESS;
  }
  uint64_t physical = 0;
  ret = resolve_inherited_tablet(*GCTX.sql_proxy_, db, local, physical, cap_scn);
  if (ret == OB_SUCCESS) { physical_tablet_id = ObTabletID(physical); }
  return OB_SUCCESS;
}
int NamespaceForkKernelPrototype::ensure_tablet(
    const ObTabletID &tablet_id,
    const ObTableSchema &requested_schema,
    const ObIArray<const ObTableSchema *> &binding_schemas) {
  return ensure_tablet_impl(tablet_id, &requested_schema, &binding_schemas);
}
int NamespaceForkKernelPrototype::ensure_tablet_impl(
    const ObTabletID &tablet_id,
    const ObTableSchema *supplied_schema,
    const ObIArray<const ObTableSchema *> *binding_schemas) {
  if (!is_encoded_id(tablet_id.id())) { return OB_SUCCESS; }
  int ret = OB_SUCCESS;
  const uint64_t db = database_of(tablet_id.id()), local = local_of(tablet_id.id());
  {
    // Reuse the tablet manager and its existing committed-status cache. A valid
    // logical birth SCN alone is not proof that physical creation has committed.
    bool exists = false;
    ret = probe_physical_tablet(tablet_id.id(), exists);
    if (OB_FAIL(ret)) { return ret; }
    if (exists) { return OB_SUCCESS; }
  } // Do not pin an uncommitted tablet while waiting for its creator's row lock.
  LOG_INFO("PROTOTYPE_V4_DIRECTORY_SLOW_PATH", K(tablet_id));
  if (!GCTX.sql_proxy_) { return OB_NOT_INIT; }
  MetadataReadGuard access; if (access.error() != OB_SUCCESS) { return access.error(); }
  if (OB_FAIL(load_exceptions(*GCTX.sql_proxy_, db))) { return ret; }
  if (control_state().tombstoned(db, local)) {
    // Dropped in this namespace: there is nothing to materialize onto.
    return OB_TABLET_NOT_EXIST;
  }
  if (control_state().owned(db, local)) {
    // Creation commit is ahead of tablet-manager visibility.
    return OB_SUCCESS;
  }
  struct MaterializeItem {
    const ObTableSchema *schema = nullptr;
    uint64_t local_tablet = 0;
    uint64_t source_tablet = 0;
    int64_t cap = 0;
  };
  std::vector<MaterializeItem> items;
  Roots root;
  const char *failure_stage = "start";
  auto materialize_step = [&](const char *stage, int step_ret) {
    failure_stage = stage;
    return step_ret;
  };
  ObMySQLTransaction trans;
  bool already = false;
  // The row lock joins concurrent requests before physical creation. The business transaction is untouched.
  if (OB_FAIL(materialize_step("transaction", trans.start(GCTX.sql_proxy_)))) {
  } else if (OB_FAIL(materialize_step("roots", roots(trans, db, root, true)))) {
  } else if (OB_FAIL(materialize_step("recheck", [&]() -> int {
      // A concurrent materializer may have committed while this request waited
      // on the namespace row lock; re-check inside it before creating anything.
      bool exists = false;
      int result = probe_physical_tablet(tablet_id.id(), exists);
      if (result == OB_SUCCESS) { already = exists || control_state().owned(db, local); }
      return result;
  }()))) {
  }
  if (OB_SUCC(ret) && !already) {
    failure_stage = "schema";
    const bool supplied = supplied_schema != nullptr && binding_schemas != nullptr;
    const ObTableSchema *requested_schema = supplied_schema;
    if (!supplied) {
      // The namespace worker is the schema authority and always supplies the
      // binding schemas on this path; physical creation without one is not
      // representable here.
      ret = OB_NOT_SUPPORTED;
    }
    auto supplied_by_table = [&](uint64_t table_id) -> const ObTableSchema * {
      const uint64_t local_table_id = local_of(table_id);
      for (int64_t i = 0; binding_schemas && i < binding_schemas->count(); ++i) {
        const ObTableSchema *schema = binding_schemas->at(i);
        if (schema != nullptr && local_of(schema->get_table_id()) == local_table_id) {
          return schema;
        }
      }
      return nullptr;
    };
    uint64_t requested_local_tablet = local;
    if (OB_SUCC(ret)) {
      ObArray<ObTabletID> requested_tablets;
      bool contains_requested_tablet = false;
      if (schema_tablet_ids(*requested_schema, requested_tablets) != OB_SUCCESS) {
        ret = OB_INVALID_ARGUMENT;
      }
      for (int64_t i = 0; OB_SUCC(ret) && i < requested_tablets.count(); ++i) {
        const uint64_t id = requested_tablets.at(i).id();
        if (!is_encoded_id(id) || database_of(id) != db) {
          ret = OB_INVALID_ARGUMENT;
        } else if (local_of(id) == local) {
          contains_requested_tablet = true;
        }
      }
      if (OB_SUCC(ret) && !contains_requested_tablet) { ret = OB_INVALID_ARGUMENT; }
    }
    const ObTableSchema *accessed_schema = requested_schema;
    if (OB_SUCC(ret) && requested_schema->is_aux_lob_table()) {
      // An LOB main tablet and both auxiliaries are one storage binding unit.
      // Canonicalize an auxiliary first access to the main schema so no partial
      // materialization can make that unit impossible to bind later.
      const uint64_t main_table = local_of(requested_schema->get_data_table_id());
      requested_schema = supplied_by_table(main_table);
      if (requested_schema == nullptr || requested_schema->is_aux_lob_table()) {
        ret = OB_INVALID_ARGUMENT;
      } else {
        uint64_t main_tablet = OB_INVALID_ID;
        if (OB_FAIL(corresponding_tablet_id(
                *accessed_schema, local, *requested_schema, main_tablet))) {
        } else {
          requested_local_tablet = local_of(main_tablet);
        }
      }
    }
    auto append_item = [&](const ObTableSchema *schema, uint64_t item_local) -> int {
      MaterializeItem item;
      item.schema = schema;
      item.local_tablet = item_local;
      const int resolve_ret = resolve_inherited_tablet(
          trans, db, item_local, item.source_tablet, item.cap);
      if (resolve_ret != OB_SUCCESS) { return resolve_ret; }
      // The cap of every hop is a fork snapshot of this namespace's chain, so
      // it can never exceed this namespace's own fork snapshot.
      if (item.cap <= 0 || item.cap > root.snapshot) { return OB_STATE_NOT_MATCH; }
      items.push_back(item);
      return OB_SUCCESS;
    };
    if (OB_SUCC(ret)) {
      failure_stage = "append";
      ret = append_item(requested_schema, requested_local_tablet);
    }
    const uint64_t auxiliary_tables[] = {
      OB_SUCC(ret) ? requested_schema->get_aux_lob_meta_tid() : OB_INVALID_ID,
      OB_SUCC(ret) ? requested_schema->get_aux_lob_piece_tid() : OB_INVALID_ID
    };
    for (uint64_t auxiliary_table : auxiliary_tables) {
      if (OB_SUCC(ret) && auxiliary_table != OB_INVALID_ID) {
        const ObTableSchema *auxiliary_schema = supplied_by_table(auxiliary_table);
        if (auxiliary_schema == nullptr) {
          ret = OB_INVALID_ARGUMENT;
        } else if (!auxiliary_schema->is_aux_lob_table()) {
          ret = OB_STATE_NOT_MATCH;
        } else {
          uint64_t auxiliary_tablet = OB_INVALID_ID;
          if (OB_FAIL(corresponding_tablet_id(
                  *requested_schema, requested_local_tablet,
                  *auxiliary_schema, auxiliary_tablet))) {
          } else {
            ret = append_item(auxiliary_schema, local_of(auxiliary_tablet));
          }
        }
      }
    }
    auto *freeze = share::server_service<ObFreezeInfoMgr>();
    if (OB_SUCC(ret) && !freeze) {
      ret = OB_STATE_NOT_MATCH;
    }
    ObSnapshotTableProxy pins;
    for (const auto &item : items) {
      if (OB_FAIL(ret)) { break; }
      failure_stage = "snapshot_pin";
      // The resolved cap is exactly the fork snapshot id of the chain hop
      // below the source, so its snapshot row is a point query away.
      Roots inherited;
      if (OB_FAIL(snapshot_roots(trans, uint64_t(item.cap), inherited))) {
      } else {
        ObSnapshotInfo pin;
        SCN scn;
        ObStorageSnapshotInfo reserved;
        if (OB_FAIL(scn.convert_for_tx(item.cap))) {
        } else if (OB_FAIL(pins.get_snapshot(trans, SNAPSHOT_FOR_MULTI_VERSION, scn, pin))) {
        } else if (pin.tablet_id_ != 0 || pin.schema_version_ != inherited.schema_version) {
          ret = OB_STATE_NOT_MATCH;
        } else if (OB_FAIL(freeze->get_min_reserved_snapshot(
                       ObTabletID(item.source_tablet), item.cap, reserved))) {
        } else if (reserved.snapshot_ > item.cap) {
          ret = OB_SNAPSHOT_DISCARDED;
        }
      }
    }
    if (OB_SUCC(ret)) {
      failure_stage = "tablet_create";
      rootserver::ObTabletCreator creator(SCN::min_scn(), trans);
      rootserver::ObTabletCreatorArg arg;
      ObArray<ObTabletID> ids;
      ObArray<const ObTableSchema *> definitions;
      ObArray<bool> empty_major;
      ObArray<int64_t> logical_birth;
      ObArray<ObForkTabletInfo> fork_infos;
      ObArray<ObTabletTablePair> mappings;
      ObArray<ObTabletID> source_ids;
      ObArray<int64_t> source_snapshot_versions;
      for (const auto &item : items) {
        ObForkTabletInfo fork;
        fork.set_fork_snapshot_version(item.cap);
        fork.set_fork_src_tablet_id(ObTabletID(item.source_tablet));
        const ObTabletID destination(encoded(db, item.local_tablet));
        if (OB_FAIL(ids.push_back(destination)) || OB_FAIL(definitions.push_back(item.schema))
            || OB_FAIL(empty_major.push_back(false)) || OB_FAIL(logical_birth.push_back(item.cap))
            || OB_FAIL(fork_infos.push_back(fork))
            || OB_FAIL(source_ids.push_back(ObTabletID(item.source_tablet)))
            || OB_FAIL(source_snapshot_versions.push_back(item.cap))
            || OB_FAIL(mappings.push_back(ObTabletTablePair(destination, item.schema->get_table_id())))) {
          break;
        }
      }
      const ObTabletID data_tablet(encoded(db, items.front().local_tablet));
      if (OB_FAIL(ret)) {
      } else if (OB_FAIL(arg.init(ids, data_tablet, definitions, false, DATA_CURRENT_VERSION,
                                 empty_major, logical_birth, fork_infos))) {
      } else if (OB_FAIL(creator.init(false))) {
      } else if (OB_FAIL(creator.add_create_tablet_arg(arg))) {
      } else if (FALSE_IT(creator.set_materialization_for_prototype())) {
      } else if (OB_FAIL(creator.execute())) {
      } else if (OB_FAIL(ObTabletAutoincrementService::get_instance()
                             .copy_sequences_for_fork(
                                 source_ids, ids, source_snapshot_versions, trans))) {
      } else if (OB_FAIL(ObTabletMappingTableOperator::batch_update(trans, mappings))) {
      } else {
        failure_stage = "exceptions";
        for (const auto &item : items) {
          ObSqlString q;
          if (OB_FAIL(ret)) { break; }
          if (OB_FAIL(q.assign_fmt("REPLACE INTO %s VALUES(%lu,%lu,%lu,0,0)", EXCEPTIONS,
                  db, item.local_tablet, local_of(item.schema->get_table_id())))) {
          } else {
            ret = write_sql(trans, q);
          }
        }
        if (OB_SUCC(ret)) {
          // Reuse the existing mapping-update sync point in this isolated prototype.
          // It exposes the whole physical binding unit plus uncommitted exceptions.
          DEBUG_SYNC(AFTER_UPDATE_TABLET_TO_LS);
          ret = THIS_WORKER.check_status();
          LOG_INFO("PROTOTYPE_V2_STORAGE_MATERIALIZE", K(tablet_id), "tablet_count", items.size(),
              "input_snapshot", items.front().cap, "namespace_snapshot", root.snapshot,
              "entry_layer", "ObAccessService", K(ret));
        }
      }
    }
  }
  if (trans.is_started()) { int end = trans.end(ret == OB_SUCCESS); if (ret == OB_SUCCESS) { ret = end; } }
  if (ret == OB_SUCCESS && !already) {
    for (const auto &item : items) {
      control_state().apply_owned(db, item.local_tablet, local_of(item.schema->get_table_id()));
    }
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
        static_cast<unsigned long long>(item ? item->source_tablet : 0),
        static_cast<unsigned long long>(item ? item->local_tablet : 0));
    LOG_WARN("prototype storage materialization failed", K(ret), K(tablet_id));
  }
  return ret;
}
}
}
