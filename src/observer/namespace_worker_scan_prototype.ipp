// Bounded tablet RPC. SQL expressions stay in the worker; V15 preserves DML snapshots.
#include "data_plane/access/ob_table_scan_param.h"
#include "storage/tx_storage/ob_access_service.h"
#include "storage/access/ob_table_scan_iterator.h"
#include "data_plane/access/ob_table_param.h"
#include "sql/engine/basic/ob_pushdown_filter.h"
#include "data_plane/transaction/ob_tx_desc_access.h"
namespace oceanbase { namespace observer { namespace namespace_worker_prototype {
using namespace common;
using namespace share;
using namespace share::schema;
using namespace storage;
int worker_send(const Frame &, bool cleanup = false);
int worker_read(Frame &);
// Cumulative exchange timing per frame type, dumped by slow inner queries.
struct ScanExchangeStats {
  std::atomic<int64_t> count{0};
  std::atomic<int64_t> send_us{0};
  std::atomic<int64_t> wait_us{0};
};
static ScanExchangeStats (&scan_exchange_stats())[128] {
  static ScanExchangeStats per_type[128];
  return per_type;
}
// Per-table open counting for the slow inner-query dump.
namespace scan_open_counter {
struct Entry { uint64_t table; int64_t count; };
inline Entry entries[64];
inline std::atomic<int> used{0};
inline lib::ObMutex lock;
}
inline void scan_open_count(uint64_t table) {
  using namespace scan_open_counter;
  ObMutexGuard guard(lock);
  int n = used.load();
  for (int i = 0; i < n; ++i) {
    if (entries[i].table == table) { ++entries[i].count; return; }
  }
  if (n < 64) { entries[n] = Entry{table, 1}; used.store(n + 1); }
}
void scan_exchange_stats_dump(FILE *out) {
  auto &per = scan_exchange_stats();
  for (int t = 0; t < 128; ++t) {
    const int64_t n = per[t].count.exchange(0);
    const int64_t send = per[t].send_us.exchange(0);
    const int64_t wait = per[t].wait_us.exchange(0);
    if (n) {
      fprintf(out, "PROTOTYPE_V23_SCAN_STATS type=%c count=%lld send_us=%lld wait_us=%lld\n",
              t >= 32 && t < 127 ? t : '?', static_cast<long long>(n),
              static_cast<long long>(send), static_cast<long long>(wait));
    }
  }
  {
    using namespace scan_open_counter;
    ObMutexGuard guard(lock);
    int n = used.load();
    for (int i = 0; i < n; ++i) {
      if (entries[i].count >= 100) {
        fprintf(out, "PROTOTYPE_V23_SCAN_OPENS table=%llu count=%lld\n",
                static_cast<unsigned long long>(entries[i].table),
                static_cast<long long>(entries[i].count));
      }
      entries[i].count = 0;
    }
  }
}
int storage_schema(StorageSpaceHandle storage_space, uint64_t id,
                   ObSchemaGetterGuard &guard, const ObTableSchema *&schema) {
  schema = nullptr;
  if (!storage_space.is_valid()) { return OB_INVALID_ARGUMENT; }
  const uint64_t ns = storage_space.namespace_id();
  if (storage_space.is_global()) {
    int ret = ObMultiVersionSchemaService::get_instance().get_runtime_schema_guard(guard);
    return ret ? ret : guard.get_table_schema(id, schema);
  }
  // A worker plan uses namespace-local object ids. Resolve them to the shared
  // engine's physical ids only here, where the request already carries its
  // authoritative namespace. Encoded ids remain accepted for old in-flight
  // plans during the prototype transition.
  if (ns != 1 && !is_inner_table(id)) {
    if (NamespaceForkKernelPrototype::is_encoded_id(id)) {
      uint64_t local_id = OB_INVALID_ID;
      int ret = NamespaceForkKernelPrototype::local_object_id(ns, id, local_id);
      return ret ? ret : NamespaceForkKernelPrototype::schema_by_id(id, schema);
    }
    const NamespaceObjectKey key{ns, id};
    return key.is_valid()
        ? NamespaceForkKernelPrototype::schema_by_id(key.storage_id(), schema)
        : OB_INVALID_ARGUMENT;
  }
  if (NamespaceForkKernelPrototype::is_encoded_id(id)) { return OB_INVALID_ARGUMENT; }
  int ret = ObMultiVersionSchemaService::get_instance().get_runtime_schema_guard(guard);
  return ret ? ret : guard.get_table_schema(id, schema);
}
bool owns_table(uint64_t ns, uint64_t id) {
  if (ns == 1 || is_inner_table(id)) {
    return !NamespaceForkKernelPrototype::is_encoded_id(id);
  }
  ObSchemaGetterGuard guard;
  const ObTableSchema *schema = nullptr;
  return storage_schema(StorageSpaceHandle::namespace_space(ns), id, guard, schema)
      == OB_SUCCESS && schema != nullptr;
}
int worker_storage_space_for_schema(const ObTableSchema &schema,
                                    ObSchemaGetterGuard &guard,
                                    StorageSpaceHandle &storage_space) {
  storage_space = StorageSpaceHandle();
  if (!serves_namespace_schema()
      || NamespaceForkKernelPrototype::is_encoded_id(schema.get_table_id())) {
    return OB_INVALID_ARGUMENT;
  }
  // Native all_* tables are the catalog of the namespace worker itself. A
  // global user table still records its schema in namespace 1's catalog; only
  // the target table's storage belongs to GLOBAL.
  if (is_inner_table(schema.get_table_id())) {
    storage_space = StorageSpaceHandle::namespace_space(serving_namespace());
    return storage_space.is_valid() ? OB_SUCCESS : OB_INVALID_ARGUMENT;
  }
  const ObDatabaseSchema *database = nullptr;
  int ret = guard.get_database_schema(schema.get_database_id(), database);
  if (OB_SUCC(ret) && OB_ISNULL(database)) {
    ret = OB_ERR_UNEXPECTED;
  }
  const bool control_database = OB_SUCC(ret)
      && is_namespace_control_database(database->get_database_name_str());
  if (OB_SUCC(ret) && control_database
      && !can_access_namespace_control_database()) {
    ret = OB_TABLE_NOT_EXIST;
  }
  if (OB_SUCC(ret)) {
    storage_space = uses_global_storage_scope() || control_database
        ? StorageSpaceHandle::global_space()
        : StorageSpaceHandle::namespace_space(serving_namespace());
    if (!storage_space.is_valid()) { ret = OB_INVALID_ARGUMENT; }
  }
  return ret;
}
int worker_local_table_schema(uint64_t table_id, int64_t schema_version,
                              ObSchemaGetterGuard &guard, const ObTableSchema *&schema,
                              StorageSpaceHandle &storage_space) {
  schema = nullptr;
  storage_space = StorageSpaceHandle();
  if (!serves_namespace_schema()
      || NamespaceForkKernelPrototype::is_encoded_id(table_id)
      || (!is_inner_table(table_id) && schema_version <= 0)) {
    return OB_INVALID_ARGUMENT;
  }
  // A scan param carries the table schema's own version.  Built-in table
  // versions are compile-time constants (for example 1048712), not a valid
  // multi-version SchemaService snapshot.  Resolve native __all_* definitions
  // from this worker's current pinned cache and keep the requested version only
  // as the storage-schema version below.
  const int64_t guard_version = is_inner_table(table_id)
      ? OB_INVALID_VERSION : schema_version;
  ObMultiVersionSchemaService *service = is_inner_table(table_id)
      ? &ObMultiVersionSchemaService::get_instance()
      : namespace_schema_service(serving_namespace());
  int ret = service == nullptr ? OB_NOT_INIT
      : service->get_runtime_schema_guard(guard, guard_version);
  if (!ret) { ret = guard.get_table_schema(table_id, schema); }
  if (!ret && (schema == nullptr || schema->get_table_id() != table_id)) {
    ret = OB_SCHEMA_EAGAIN;
  }
  if (!ret && is_inner_table(table_id)) {
    // Native __all_* tables always belong to the worker's namespace.  Looking
    // up their database just to rediscover that fact recursively scans
    // __all_database_history through this same remote iterator.
    return worker_storage_space_for_schema(*schema, guard, storage_space);
  }
  if (!ret && schema->get_schema_version() != schema_version) {
    ret = OB_SCHEMA_EAGAIN;
  }
  if (!ret) { ret = worker_storage_space_for_schema(*schema, guard, storage_space); }
  if (ret == OB_TABLE_NOT_EXIST) { schema = nullptr; }
  return ret;
}
int worker_materialization_schemas(
    const ObTableSchema &requested,
    ObSchemaGetterGuard &guard,
    ObIArray<const ObTableSchema *> &schemas) {
  schemas.reset();
  const ObTableSchema *main = &requested;
  int ret = OB_SUCCESS;
  if (requested.is_aux_lob_table()) {
    ret = guard.get_table_schema(requested.get_data_table_id(), main);
    if (!ret && (main == nullptr || main->is_aux_lob_table())) {
      ret = OB_SCHEMA_EAGAIN;
    }
  }
  if (!ret) { ret = schemas.push_back(main); }
  const uint64_t auxiliary_ids[] = {
      !ret ? main->get_aux_lob_meta_tid() : OB_INVALID_ID,
      !ret ? main->get_aux_lob_piece_tid() : OB_INVALID_ID};
  for (uint64_t id : auxiliary_ids) {
    if (!ret && id != OB_INVALID_ID) {
      const ObTableSchema *auxiliary = nullptr;
      if (OB_FAIL(guard.get_table_schema(id, auxiliary))) {
      } else if (auxiliary == nullptr || !auxiliary->is_aux_lob_table()
          || auxiliary->get_data_table_id() != main->get_table_id()) {
        ret = OB_SCHEMA_EAGAIN;
      } else {
        ret = schemas.push_back(auxiliary);
      }
    }
  }
  return ret;
}
// The NLJ right side reships the same serialized table schema per probe.
// Cache the decoded and namespace-routed schema keyed by the wire bytes: a
// repeated open pays one memcmp instead of a deserialize + schema copy.
struct EngineSchemaCacheEntry {
  ObArenaAllocator alloc{ObMemAttr("NsScanSchema")};
  ObTableSchema *logical = nullptr;
  ObTableSchema *routed = nullptr;
  ObArray<ObTabletID> logical_tablets;
  ObArray<ObTabletID> storage_tablets;
};
inline uint64_t engine_schema_hash(uint64_t ns, const ObString &blob) {
  uint64_t h = 1469598103934665603ULL ^ (ns * 1099511628211ULL);
  const char *p = blob.ptr();
  int32_t n = blob.length();
  while (n >= 8) {
    uint64_t w;
    memcpy(&w, p, 8);
    h = (h ^ w) * 1099511628211ULL;
    p += 8; n -= 8;
  }
  while (n-- > 0) { h = (h ^ static_cast<unsigned char>(*p++)) * 1099511628211ULL; }
  return h;
}
int engine_schema_resolve(uint64_t ns, bool namespace_local, const ObString &blob,
                          std::shared_ptr<EngineSchemaCacheEntry> &entry) {
  struct Cache {
    lib::ObMutex lock;
    std::map<uint64_t, std::pair<std::string, std::shared_ptr<EngineSchemaCacheEntry>>> by_hash;
  };
  static Cache cache;
  if (blob.empty() || blob.length() > 4 * 1024 * 1024) { return OB_INVALID_ARGUMENT; }
  const uint64_t hash = engine_schema_hash(ns, blob);
  {
    ObMutexGuard guard(cache.lock);
    auto it = cache.by_hash.find(hash);
    if (it != cache.by_hash.end()
        && it->second.first.size() == static_cast<size_t>(blob.length())
        && memcmp(it->second.first.data(), blob.ptr(), blob.length()) == 0) {
      entry = it->second.second;
      return OB_SUCCESS;
    }
  }
  auto built = std::make_shared<EngineSchemaCacheEntry>();
  void *buf = built->alloc.alloc(sizeof(ObTableSchema));
  if (!buf) { return OB_ALLOCATE_MEMORY_FAILED; }
  built->logical = new (buf) ObTableSchema(&built->alloc);
  int64_t pos = 0;
  int ret = built->logical->deserialize(blob.ptr(), blob.length(), pos);
  if (ret || pos != blob.length()) { return ret ? ret : OB_INVALID_ARGUMENT; }
  if (OB_FAIL(built->logical->get_tablet_ids(built->logical_tablets))) { return ret; }
  if (namespace_local && ns > 1) {
    buf = built->alloc.alloc(sizeof(ObTableSchema));
    if (!buf) { return OB_ALLOCATE_MEMORY_FAILED; }
    built->routed = new (buf) ObTableSchema(&built->alloc);
    if (OB_FAIL(NamespaceForkKernelPrototype::make_storage_schema(
            ns, *built->logical, *built->routed))) { return ret; }
  } else {
    built->routed = built->logical;
  }
  if (OB_FAIL(built->routed->get_tablet_ids(built->storage_tablets))) { return ret; }
  ObMutexGuard guard(cache.lock);
  if (cache.by_hash.size() >= 256) { cache.by_hash.clear(); }
  cache.by_hash[hash] = {std::string(blob.ptr(), blob.length()), built};
  entry = std::move(built);
  return OB_SUCCESS;
}
struct EngineScan {
  struct VirtualContext {
    sql::ObExecContext execution;
    sql::ObEvalCtx evaluation;
    sql::ObPushdownExprSpec spec;
    sql::ObPushdownOperator op;
    VirtualContext(ObIAllocator &allocator, sql::ObSQLSessionInfo &session)
        : execution(allocator), evaluation(execution), spec(allocator), op(evaluation, spec) {
      execution.set_my_session(&session);
    }
  };
  ObArenaAllocator allocator{ObMemAttr("NsRemoteScan")};
  // Iterator and range-key memory live in this per-scan arena so an NLJ
  // rescan can drop them without touching the schema copies in allocator.
  ObArenaAllocator iter_allocator{ObMemAttr("NsRemoteScanIt")};
  ObSchemaGetterGuard guard;
  std::shared_ptr<EngineSchemaCacheEntry> cached_schema;
  ObTableParam table{allocator};
  ObTableScanParam param;
  std::vector<ObObj> keys;
  ObNewRowIterator *iter = nullptr;
  const ObTableSchema *schema = nullptr;
  std::unique_ptr<VirtualContext> virtual_context;
  ~EngineScan() {
    if (iter) {
      if (virtual_context) { share::server_service<ObIVirtualTableScan>()->revert_scan_iter(iter); }
      else { share::server_service<ObITabletScan>()->revert_scan_iter(iter); }
    }
  }
  int open(StorageSpaceHandle channel_space, Frame &request,
           transaction::ObTxDesc *tx, sql::ObSQLSessionInfo *session) {
    int ret = OB_SUCCESS;
    const uint64_t logical_table_id = request.number(), logical_tablet_id = request.number();
    const bool has_logical_schema = request.number() != 0;
    StorageSpaceHandle storage_space;
    if (OB_FAIL(read_storage_space(request, channel_space, storage_space))) {
      return ret;
    }
    const bool namespace_local = storage_space.is_namespace();
    const uint64_t ns = storage_space.namespace_id();
    const int64_t requested_schema_version = request.number();
    // Length-prefixed blob: the cache keys on the exact wire bytes.
    const ObString schema_blob = has_logical_schema ? request.string() : ObString();
    if (has_logical_schema && !request.ret) {
      if (OB_FAIL(engine_schema_resolve(ns, namespace_local, schema_blob, cached_schema))) {
        return ret;
      }
    }
    param.scan_flag_.flag_ = request.number();
    const bool get = request.number() != 0;
    param.limit_param_.limit_ = static_cast<int64_t>(request.number());
    param.limit_param_.offset_ = static_cast<int64_t>(request.number());
    const uint64_t count = request.number();
    if (request.ret || count > OB_MAX_COLUMN_NUMBER) { return OB_NOT_SUPPORTED; }
    bool logical_tablet_matches = !has_logical_schema;
    if (has_logical_schema) {
      for (int64_t i = 0; i < cached_schema->logical_tablets.count(); ++i) {
        if (cached_schema->logical_tablets.at(i).id() == logical_tablet_id) {
          logical_tablet_matches = true;
          break;
        }
      }
    }
    const ObTableSchema *logical =
        has_logical_schema ? cached_schema->logical : nullptr;
    if (request.ret || (!has_logical_schema && namespace_local && ns > 1)
        || (has_logical_schema && ((requested_schema_version <= 0
                && !is_inner_table(logical_table_id))
            || logical->get_table_id() != logical_table_id
            || !logical_tablet_matches
            || logical->get_schema_version() < 0
            || (requested_schema_version > 0
                && logical->get_schema_version() != requested_schema_version)))) {
      return OB_INVALID_ARGUMENT;
    }
    if (has_logical_schema) {
      schema = cached_schema->routed;
    } else {
      // Resolving through the shared process SchemaService could lazy-load
      // via inner SQL routed back to the requesting Worker, which deadlocks
      // Worker activation.  Requests must carry the caller-resolved schema.
      fprintf(stderr, "PROTOTYPE_SCAN_SCHEMA_REQUIRED ns=%llu table=%llu local=%d\n",
          (unsigned long long)ns, (unsigned long long)logical_table_id,
          static_cast<int>(namespace_local));
      ret = OB_NOT_SUPPORTED;
    }
    if (ret) { return ret; }
    uint64_t tablet_id = logical_tablet_id;
    if (schema && namespace_local && ns > 1) {
      ret = NamespaceForkKernelPrototype::storage_object_id(
          ns, logical_tablet_id, tablet_id);
    }
    bool storage_tablet_matches = false;
    if (OB_SUCC(ret) && cached_schema) {
      for (int64_t i = 0; i < cached_schema->storage_tablets.count(); ++i) {
        if (cached_schema->storage_tablets.at(i).id() == tablet_id) {
          storage_tablet_matches = true;
          break;
        }
      }
    }
    if (OB_FAIL(ret)) { return ret; }
    if (!schema || (!is_virtual_table(logical_table_id) && !storage_tablet_matches)) {
      return OB_INVALID_ARGUMENT;
    }
    const int64_t storage_schema_version = has_logical_schema && requested_schema_version > 0
        ? requested_schema_version : schema->get_schema_version();
    const uint64_t table_id = schema->is_sys_table()
        ? logical_table_id : schema->get_table_id();
    for (uint64_t i = 0; !ret && i < count; ++i) {
      const uint64_t column = request.number();
      if (!schema->get_column_schema(column)) { ret = OB_NOT_SUPPORTED; }
      else { ret = param.column_ids_.push_back(column); }
    }
    const uint64_t ranges = request.number();
    // Must cover MAX_IN_QUERY_PER_TIME (1000): IN-batch refreshes arrive as
    // one range per element over a single IPC roundtrip.
    if (ret || request.ret || ranges > 8192) { return ret ? ret : OB_NOT_SUPPORTED; }
    const uint64_t width = request.number();
    if (request.ret || width == 0 || width > OB_MAX_ROWKEY_COLUMN_NUMBER) {
      return OB_INVALID_ARGUMENT;
    }
    keys.resize(ranges * 2 * width);
    for (uint64_t i = 0; !ret && i < ranges; ++i) {
      ObNewRange range; range.table_id_ = table_id;
      range.border_flag_.set_data(request.number());
      for (uint64_t j = 0; !ret && j < width * 2; ++j) {
        ObObj value; request.read(value);
        ret = request.ret ? request.ret : ob_write_obj(iter_allocator, value, keys[i * width * 2 + j]);
      }
      range.start_key_.assign(&keys[i * width * 2], width);
      range.end_key_.assign(&keys[i * width * 2 + width], width);
      if (!ret) { ret = param.key_ranges_.push_back(range); }
    }
    if (is_virtual_table(logical_table_id)) {
      param.sql_mode_ = request.number();
      if (ret || !request.consumed() || !session || ns != 1) { return ret ? ret : OB_INVALID_ARGUMENT; }
      virtual_context = std::make_unique<VirtualContext>(allocator, *session);
      param.index_id_ = table_id; param.tablet_id_ = ObTabletID(tablet_id);
      param.schema_version_ = schema->get_schema_version();
      param.runtime_schema_version_ = schema->get_schema_version();
      param.timeout_ = THIS_WORKER.get_timeout_ts();
      param.scan_allocator_ = &iter_allocator; param.reserved_cell_count_ = count;
      param.op_ = &virtual_context->op;
      ret = share::server_service<ObIVirtualTableScan>()->table_scan(param, iter);
      fprintf(stderr, "PROTOTYPE_V18_VIRTUAL_SCAN table=%llu ret=%d\n", (unsigned long long)table_id, ret);
      return ret;
    }
    const uint64_t txid = request.number();
    const bool read_latest = param.scan_flag_.is_read_latest();
    param.for_update_ = request.number() != 0;
    param.is_for_foreign_check_ = request.number() != 0;
    request.read(param.sample_info_);
    if (!tx || static_cast<uint64_t>(data_plane::tx_desc_id(tx).get_id()) != txid) {
      return OB_INVALID_ARGUMENT;
    }
    request.read(param.snapshot_);
    param.tx_lock_timeout_ = request.number();
    param.tx_seq_base_ = request.number();
    param.tx_id_ = data_plane::tx_desc_id(tx);
    param.trans_desc_ = tx; // Native pointer from this request, never from IPC.
    if (!param.snapshot_.is_valid() || param.snapshot_.is_weak_read()
        || (param.snapshot_.core_.tx_id_.is_valid() && param.snapshot_.core_.tx_id_ != param.tx_id_)
        || (!txid && read_latest)) {
      return OB_INVALID_ARGUMENT;
    }
    if (ret || !request.consumed()) {
      return ret ? ret : OB_INVALID_ARGUMENT;
    }
    param.index_id_ = table_id; param.tablet_id_ = ObTabletID(tablet_id);
    param.schema_version_ = storage_schema_version;
    param.runtime_schema_version_ = storage_schema_version;
    param.timeout_ = THIS_WORKER.get_timeout_ts();
    param.is_get_ = get;
    param.allocator_ = &iter_allocator; param.scan_allocator_ = &iter_allocator;
    param.reserved_cell_count_ = count;
    // Match the native SQL scan path: every LOB storage column needs a V2
    // locator, including __all_* columns. The worker owns SQL but the bytes
    // still live in the shared storage process, so an unmarked system-table
    // locator cannot be materialized after it crosses IPC.
    table.get_enable_lob_locator_v2() = true;
    // Reads never materialize: an inherited tablet is served through
    // resolve_read_tablet redirection inside the storage layer instead.
    if (!ret) { ret = table.convert(*schema, param.column_ids_, sql::ObStoragePushdownFlag()); }
    if (!ret) {
      param.table_param_ = &table;
      ret = share::server_service<ObITabletScan>()->table_scan(param, iter);
    }
    fprintf(stderr, "PROTOTYPE_V10_SCAN_OPEN ns=%llu table=%llu ret=%d\n",
        static_cast<unsigned long long>(ns), static_cast<unsigned long long>(table_id), ret);
    if (txid) { fprintf(stderr, "PROTOTYPE_V15_TX_SCAN tx=%llu latest=%d ret=%d\n", (unsigned long long)txid, read_latest, ret); }
    return ret;
  }
  // NLJ rescans only change key ranges. Rebuild the storage iterator in place
  // instead of paying a full schema ship + tablet lookup + scan open per row.
  int rescan(Frame &request, Frame &reply) {
    const uint64_t flag = request.number();
    const bool get = request.number() != 0;
    const uint64_t ranges = request.number();
    const uint64_t width = request.number();
    if (virtual_context || !iter || request.ret) { reply.number(OB_NOT_SUPPORTED); return OB_SUCCESS; }
    if (ranges > 256 || width == 0 || width > OB_MAX_ROWKEY_COLUMN_NUMBER) {
      reply.number(OB_INVALID_ARGUMENT);
      return OB_SUCCESS;
    }
    // Validate against the live iterator first; shallow cells stay frame-backed.
    std::vector<uint64_t> borders(ranges);
    std::vector<ObObj> shallow(ranges * 2 * width);
    for (uint64_t i = 0; i < ranges && !request.ret; ++i) {
      borders[i] = request.number();
      for (uint64_t j = 0; j < width * 2; ++j) { request.read(shallow[i * width * 2 + j]); }
    }
    if (request.ret || !request.consumed()) { reply.number(OB_INVALID_ARGUMENT); return OB_SUCCESS; }
    share::server_service<ObITabletScan>()->revert_scan_iter(iter);
    iter = nullptr;
    iter_allocator.reset();
    param.key_ranges_.reset();
    keys.resize(ranges * 2 * width);
    int ret = OB_SUCCESS;
    for (uint64_t i = 0; !ret && i < ranges; ++i) {
      ObNewRange range; range.table_id_ = param.index_id_;
      range.border_flag_.set_data(borders[i]);
      for (uint64_t j = 0; !ret && j < width * 2; ++j) {
        ret = ob_write_obj(iter_allocator, shallow[i * width * 2 + j], keys[i * width * 2 + j]);
      }
      range.start_key_.assign(&keys[i * width * 2], width);
      range.end_key_.assign(&keys[i * width * 2 + width], width);
      if (!ret) { ret = param.key_ranges_.push_back(range); }
    }
    param.scan_flag_.flag_ = flag;
    param.is_get_ = get;
    param.timeout_ = THIS_WORKER.get_timeout_ts();
    if (!ret) { ret = share::server_service<ObITabletScan>()->table_scan(param, iter); }
    reply.number(ret);
    return OB_SUCCESS;
  }
  int fetch(Frame &reply) {
    Frame rows('s'); rows.number(0); rows.number(0); rows.number(0);
    uint64_t count = 0; bool end = false; int ret = OB_SUCCESS;
    for (; count < 32; ++count) {
      if (virtual_context) {
        ObNewRow *row = nullptr;
        ret = iter->get_next_row(row);
        if (ret == OB_ITER_END) { ret = OB_SUCCESS; end = true; break; }
        if (ret) { break; }
        if (!row || row->get_count() != param.column_ids_.count()) { ret = OB_ERR_UNEXPECTED; break; }
        for (int64_t i = 0; i < row->get_count(); ++i) { rows.write_object(row->get_cell(i), false); }
        if ((ret = rows.ret)) { break; }
        continue;
      }
      blocksstable::ObDatumRow *row = nullptr;
      ret = static_cast<ObTableScanIterator *>(iter)->get_next_row(row);
      if (ret == OB_ITER_END) { ret = OB_SUCCESS; end = true; break; }
      if (ret) { break; }
      if (!row || row->get_column_count() != param.column_ids_.count()) { ret = OB_ERR_UNEXPECTED; break; }
      const auto &projector = table.get_output_projector();
      const auto &descriptors = table.get_read_info().get_columns_desc();
      if (projector.count() != row->get_column_count()) { ret = OB_ERR_UNEXPECTED; break; }
      for (int64_t i = 0; !ret && i < row->get_column_count(); ++i) {
        ObObj value;
        const int64_t index = projector.at(i);
        if (index < 0 || index >= descriptors.count() || descriptors.at(index).col_id_ != param.column_ids_.at(i)) {
          ret = OB_ERR_UNEXPECTED;
        } else {
          // Native descriptors include decimal precision/scale and LOB flags.
          ret = row->storage_datums_[i].to_obj_enhance(value, descriptors.at(index).col_type_);
        }
        if (!ret) {
          const bool has_lob_header = table.enable_lob_locator_v2() && value.is_lob_storage()
              && !value.is_null() && value.has_lob_header();
          rows.write_object(value, has_lob_header);
        }
      }
      if (ret) { break; }
      if (rows.ret) { ret = rows.ret; break; }
    }
    reply = Frame('s'); reply.number(ret); reply.number(end); reply.number(count);
    if (!ret) { reply.data.insert(reply.data.end(), rows.data.begin() + Frame::HEADER_SIZE + 24, rows.data.end()); }
    return OB_SUCCESS;
  }
};
struct ReadScans {
  StorageSpaceHandle storage_space;
  std::map<uint64_t, std::unique_ptr<EngineScan>> scans;
  uint64_t sequence = 0;
  explicit ReadScans(StorageSpaceHandle space) : storage_space(space) {}
  ~ReadScans() {
    const size_t remaining = scans.size();
    scans.clear();
    fprintf(stderr, "PROTOTYPE_V13_SCANS_RELEASED ns=%llu remaining=%zu\n",
        (unsigned long long)storage_space.namespace_id(), remaining);
  }
  int process(Frame &request, Frame &reply, transaction::ObTxDesc *tx = nullptr, sql::ObSQLSessionInfo *session = nullptr) {
    const uint64_t ns = storage_space.namespace_id();
    int ret = OB_SUCCESS;
    if (!storage_space.is_namespace()) { ret = OB_INVALID_ARGUMENT; }
    reply = Frame('s');
    if (request.type() == 'O') {
      if (scans.size() >= 4) { ret = OB_NOT_SUPPORTED; }
      auto scan = std::make_unique<EngineScan>();
      if (!ret) { ret = scan->open(storage_space, request, tx, session); }
      if (ret) { fprintf(stderr, "PROTOTYPE_V17_SCAN_FAILED ret=%d\n", ret); }
      reply.number(ret); reply.number(ret ? 0 : ++sequence);
      if (!ret) { scans.emplace(sequence, std::move(scan)); }
    } else {
      const uint64_t id = request.number();
      auto it = scans.find(id);
      if (request.type() == 'R') {
        // Rescan carries fresh key ranges beyond the handle; consume them in
        // the scan itself. A logical failure is a reply value so the worker
        // can fall back to a full reopen on the still-healthy channel.
        if (it == scans.end()) { reply.number(OB_INVALID_ARGUMENT); }
        else { it->second->rescan(request, reply); }
      }
      else if (!request.consumed() || it == scans.end()) { reply.number(OB_INVALID_ARGUMENT); }
      else if (request.type() == 'X') { scans.erase(it); reply.number(0); }
      else if (request.type() == 'F') { ret = it->second->fetch(reply); }
      else { reply.number(OB_NOT_SUPPORTED); }
    }
    // Storage errors belong in the reply. A sent RPC must receive that reply
    // before cancellation cleanup can issue its next operation.
    return reply.ret;
  }
};
class RemoteScanIterator final : public ObNewRowIterator {
public:
  ObVTableScanParam &param;
  uint64_t handle = 0, row_index = 0, rows_left = 0;
  int64_t qualified = 0, returned = 0;
  bool end = false;
  std::vector<ObObj> cells;
  Frame batch; // Own variable-length cell bytes until the next batch.
  ObNewRow row;
  explicit RemoteScanIterator(ObVTableScanParam &p) : param(p) {}
  ~RemoteScanIterator() override { reset(); }
  int open() {
    const sql::ObStoragePushdownFlag flags(param.pd_storage_flag_);
    scan_open_count(param.index_id_);
    // The storage process deliberately does not execute SQL expressions. A
    // pushed filter still has its expression list in op_filters_, so evaluate
    // it in this worker while streaming rows from the physical tablet.
    if (!param.op_ || !param.output_exprs_
        || param.output_exprs_->count() != param.column_ids_.count()
        || (param.aggregate_exprs_ && !param.aggregate_exprs_->empty())
        || (flags.is_filter_pushdown() && !param.op_filters_)
        || flags.is_aggregate_pushdown() || flags.is_group_by_pushdown()) {
      fprintf(stderr,
          "PROTOTYPE_V22_SCAN_UNSUPPORTED table=%llu columns=%lld outputs=%lld "
          "filters=%lld flags=%d aggregate=%lld\n",
          (unsigned long long)param.index_id_,
          (long long)param.column_ids_.count(),
          (long long)(param.output_exprs_ ? param.output_exprs_->count() : -1),
          (long long)(param.op_filters_ ? param.op_filters_->count() : -1),
          param.pd_storage_flag_,
          (long long)(param.aggregate_exprs_ ? param.aggregate_exprs_->count() : -1));
      return OB_NOT_SUPPORTED;
    }
    ObSchemaGetterGuard schema_guard;
    const ObTableSchema *logical_schema = nullptr;
    // Inner tables must carry their schema even for namespace 1: the shared
    // process otherwise re-resolves them through its own SchemaService, whose
    // lazy load needs inner SQL to this namespace's Worker.  During Worker
    // activation (crash recovery) that Worker is busy initialising itself, so
    // the lookup cannot complete and the activation deadlocks or fails.
    bool send_logical_schema = serves_namespace_schema()
        && !NamespaceForkKernelPrototype::is_encoded_id(param.index_id_);
    StorageSpaceHandle storage_space =
        StorageSpaceHandle::namespace_space(serving_namespace());
    int ret = send_logical_schema
        ? worker_local_table_schema(
              param.index_id_, param.schema_version_, schema_guard, logical_schema,
              storage_space)
        : OB_SUCCESS;
    if (ret) {
      fprintf(stderr,
              "PROTOTYPE_V22_SCAN_OPEN stage=schema ret=%d table=%llu schema=%lld send=%d\n",
              ret, static_cast<unsigned long long>(param.index_id_),
              static_cast<long long>(param.schema_version_), send_logical_schema);
      return ret;
    }
    Frame request('O'); request.number(param.index_id_); request.number(param.tablet_id_.id());
    request.number(send_logical_schema); write_storage_space(request, storage_space);
    request.number(param.schema_version_);
    if (send_logical_schema) {
      // Length-prefixed in place so the shared side can key its decoded
      // schema cache on the exact wire bytes without a second copy.
      const int64_t len_pos = request.data.size();
      request.number(0);
      const int64_t begin = request.data.size();
      request.append(*logical_schema);
      const uint64_t len = request.data.size() - begin;
      for (unsigned i = 0; i < 8; ++i) {
        request.data[len_pos + i] = static_cast<char>(len >> (8 * i));
      }
    }
    request.number(param.scan_flag_.flag_); request.number(param.is_get_);
    // Legacy op_filters are SQL callbacks even when storage pushdown is off.
    // Apply both those predicates and the scan limit in this worker, in order.
    request.number(static_cast<uint64_t>(-1)); request.number(0);
    request.number(param.column_ids_.count());
    for (int64_t i = 0; i < param.column_ids_.count(); ++i) { request.number(param.column_ids_.at(i)); }
    request.number(param.key_ranges_.count());
    const int64_t width = param.key_ranges_.empty() ? 1 : param.key_ranges_.at(0).start_key_.get_obj_cnt();
    request.number(width);
    for (int64_t i = 0; i < param.key_ranges_.count(); ++i) {
      const ObNewRange &range = param.key_ranges_.at(i);
      if (range.start_key_.get_obj_cnt() != width || range.end_key_.get_obj_cnt() != width) { return OB_NOT_SUPPORTED; }
      request.number(range.border_flag_.get_data());
      for (int64_t j = 0; j < width; ++j) { request.append(range.start_key_.get_obj_ptr()[j]); }
      for (int64_t j = 0; j < width; ++j) { request.append(range.end_key_.get_obj_ptr()[j]); }
    }
    if (is_virtual_table(param.index_id_)) { request.number(param.sql_mode_); }
    else {
      const auto &scan = static_cast<const ObTableScanParam &>(param);
      request.number(scan.tx_id_.get_id());
      request.number(param.for_update_);
      request.number(param.is_for_foreign_check_);
      request.append(scan.sample_info_);
      request.append(scan.snapshot_); request.number(scan.tx_lock_timeout_); request.number(scan.tx_seq_base_);
    }
    if (request.ret) {
      fprintf(stderr,
              "PROTOTYPE_V22_SCAN_OPEN stage=encode ret=%d table=%llu ranges=%lld sample=%d\n",
              request.ret, static_cast<unsigned long long>(param.index_id_),
              static_cast<long long>(param.key_ranges_.count()),
              is_virtual_table(param.index_id_) ? -1
                  : static_cast<const ObTableScanParam &>(param).sample_info_.method_);
    }
    Frame reply; ret = request.ret ? request.ret : exchange(request, reply);
    if (ret) {
      fprintf(stderr,
              "PROTOTYPE_V22_SCAN_OPEN stage=exchange ret=%d table=%llu\n",
              ret, static_cast<unsigned long long>(param.index_id_));
    }
    if (!ret) { handle = reply.number(); if (!reply.consumed() || handle == 0) { ret = OB_INVALID_ARGUMENT; } }
    return ret;
  }
  int get_next_row(ObNewRow *&out) override {
    int ret = OB_SUCCESS;
    const size_t columns = param.column_ids_.count();
    if (!rows_left) {
      if (end) { return OB_ITER_END; }
      Frame request('F'); Frame &reply = batch; request.number(handle);
      if ((ret = exchange(request, reply))) { return ret; }
      end = reply.number() != 0; const uint64_t rows = reply.number();
      if (reply.ret || rows > 32 || (!rows && !end)) { return OB_INVALID_ARGUMENT; }
      cells.resize(rows * columns); row_index = 0; rows_left = rows;
      for (auto &cell : cells) { reply.read_object(cell); }
      if (!reply.consumed()) { return OB_INVALID_ARGUMENT; }
      if (!rows) { return OB_ITER_END; }
    }
    row.cells_ = columns ? cells.data() + row_index : nullptr;
    row.count_ = columns; row_index += columns; --rows_left; out = &row;
    return ret;
  }
  int get_next_row() override {
    return next_row(false);
  }
  int next_row(bool stop_before_fetch) {
    if (param.limit_param_.limit_ >= 0 && returned >= param.limit_param_.limit_) { return OB_ITER_END; }
    int ret = OB_SUCCESS;
    for (;;) {
      if ((ret = THIS_WORKER.check_status())) { return ret; }
      // A vector batch must keep all returned string pointers in one wire frame.
      if (stop_before_fetch && !rows_left) { return OB_ITER_END; }
      ObNewRow *row = nullptr;
      if ((ret = get_next_row(row))) { return ret; }
      auto &ctx = param.op_->get_eval_ctx();
      param.op_->clear_datum_eval_flag();
      for (int64_t i = 0; !ret && i < row->count_; ++i) {
        sql::ObExpr *expr = param.output_exprs_->at(i);
        ObDatum &datum = expr->locate_datum_for_write(ctx);
        ret = datum.from_obj(row->cells_[i], expr->obj_datum_map_);
        if (!ret && row->cells_[i].has_lob_header()) {
          datum.set_has_lob_header();
        }
        expr->set_evaluated_projected(ctx);
        expr->set_evaluated_flag(ctx);
      }
      bool filtered = false;
      if (!ret && param.op_filters_) { ret = sql::ObOperator::filter_row(ctx, *param.op_filters_, filtered); }
      if (ret) { return ret; }
      if (!filtered && ++qualified > param.limit_param_.offset_) { ++returned; return OB_SUCCESS; }
    }
  }
  int get_next_rows(int64_t &count, int64_t capacity) override {
    count = 0;
    auto &ctx = param.op_->get_eval_ctx();
    sql::ObEvalCtx::BatchInfoScopeGuard batch(ctx);
    const int64_t limit = std::min<int64_t>(32, capacity);
    if (limit <= 0) { return OB_INVALID_ARGUMENT; }
    batch.set_batch_size(limit);
    int ret = OB_SUCCESS;
    while (count < limit) {
      batch.set_batch_idx(count);
      ret = next_row(count != 0);
      if (ret) { break; }
      ++count;
    }
    if (ret != OB_SUCCESS && ret != OB_ITER_END) {
      fprintf(stderr,
              "PROTOTYPE_V22_SCAN_FETCH ret=%d table=%llu count=%ld capacity=%ld handle=%llu end=%d\n",
              ret, static_cast<unsigned long long>(param.index_id_), count, capacity,
              static_cast<unsigned long long>(handle), end);
    }
    return ret == OB_ITER_END && count ? OB_SUCCESS : ret;
  }
  void reset() override {
    if (handle) { Frame request('X'), reply; request.number(handle); exchange(request, reply); handle = 0; }
    cells.clear(); row_index = 0; rows_left = 0; end = false; qualified = 0; returned = 0;
  }
  // NLJ rescan: same table, same columns, only the key ranges changed. Reuse
  // the shared-side scan instead of re-shipping the schema per row. Falls
  // back to a full close+open when the shared side cannot rescan.
  int rescan() {
    cells.clear(); row_index = 0; rows_left = 0; end = false; qualified = 0; returned = 0;
    if (!handle || is_virtual_table(param.index_id_)) { reset(); return open(); }
    Frame request('R'); request.number(handle);
    request.number(param.scan_flag_.flag_); request.number(param.is_get_);
    request.number(param.key_ranges_.count());
    const int64_t width = param.key_ranges_.empty() ? 1 : param.key_ranges_.at(0).start_key_.get_obj_cnt();
    request.number(width);
    for (int64_t i = 0; i < param.key_ranges_.count(); ++i) {
      const ObNewRange &range = param.key_ranges_.at(i);
      if (range.start_key_.get_obj_cnt() != width || range.end_key_.get_obj_cnt() != width) {
        return OB_NOT_SUPPORTED;
      }
      request.number(range.border_flag_.get_data());
      for (int64_t j = 0; j < width; ++j) { request.append(range.start_key_.get_obj_ptr()[j]); }
      for (int64_t j = 0; j < width; ++j) { request.append(range.end_key_.get_obj_ptr()[j]); }
    }
    Frame reply;
    const int ret = request.ret ? request.ret : exchange(request, reply);
    if (ret) {
      fprintf(stderr, "PROTOTYPE_V23_SCAN_RESCAN_FALLBACK ret=%d table=%llu ranges=%lld\n",
          ret, static_cast<unsigned long long>(param.index_id_),
          static_cast<long long>(param.key_ranges_.count()));
      reset(); return open();
    }
    return OB_SUCCESS;
  }
private:
  int exchange(const Frame &request, Frame &reply) {
    StorageSessionScope scope(param.op_ ? param.op_->get_eval_ctx().exec_ctx_.get_my_session() : nullptr);
    const int64_t begin = ObTimeUtility::current_time();
    int ret = scope.error() ? scope.error() : worker_send(request, request.type() == 'X');
    const int64_t sent = ObTimeUtility::current_time();
    if (!ret) { ret = worker_read(reply); }
    const int64_t done = ObTimeUtility::current_time();
    auto &stats = scan_exchange_stats()[static_cast<unsigned char>(request.type()) & 0x7f];
    stats.count.fetch_add(1);
    stats.send_us.fetch_add(sent - begin);
    stats.wait_us.fetch_add(done - sent);
    if (!ret && reply.type() != 's') { ret = OB_INVALID_ARGUMENT; }
    if (!ret) { ret = static_cast<int>(reply.number()); }
    return ret ? ret : reply.ret;
  }
};
class RemoteTabletScan final : public ObIVirtualTableScan {
public:
  int table_scan(ObVTableScanParam &param, ObNewRowIterator *&iter) override {
    if (iter) { return OB_INVALID_ARGUMENT; }
    auto scan = std::make_unique<RemoteScanIterator>(param);
    int ret = scan->open(); if (!ret) { iter = scan.release(); } return ret;
  }
  int revert_scan_iter(ObNewRowIterator *iter) override { delete iter; return OB_SUCCESS; }
  int reuse_scan_iter(bool, ObNewRowIterator *iter) override {
    auto *scan = static_cast<RemoteScanIterator *>(iter);
    if (!scan) { return OB_INVALID_ARGUMENT; }
    scan->reset(); return OB_SUCCESS;
  }
  int table_rescan(ObVTableScanParam &, ObNewRowIterator *iter) override {
    auto *scan = static_cast<RemoteScanIterator *>(iter);
    if (!scan) { return OB_INVALID_ARGUMENT; }
    return scan->rescan();
  }
};
} } }
