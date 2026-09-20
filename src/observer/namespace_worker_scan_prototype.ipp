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
  if (!owns_namespace_schema()
      || NamespaceForkKernelPrototype::is_encoded_id(schema.get_table_id())) {
    return OB_INVALID_ARGUMENT;
  }
  // Native all_* tables are the catalog of the namespace worker itself. A
  // global user table still records its schema in namespace 1's catalog; only
  // the target table's storage belongs to GLOBAL.
  if (is_inner_table(schema.get_table_id())) {
    storage_space = StorageSpaceHandle::namespace_space(worker_namespace);
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
        : StorageSpaceHandle::namespace_space(worker_namespace);
    if (!storage_space.is_valid()) { ret = OB_INVALID_ARGUMENT; }
  }
  return ret;
}
int worker_local_table_schema(uint64_t table_id, int64_t schema_version,
                              ObSchemaGetterGuard &guard, const ObTableSchema *&schema,
                              StorageSpaceHandle &storage_space) {
  schema = nullptr;
  storage_space = StorageSpaceHandle();
  if (!owns_namespace_schema()
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
  int ret = ObMultiVersionSchemaService::get_instance().get_runtime_schema_guard(
      guard, guard_version);
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
  ObSchemaGetterGuard guard;
  ObTableSchema logical_schema{&allocator};
  ObTableSchema routed_schema{&allocator};
  std::vector<std::unique_ptr<ObTableSchema>> logical_materialization_schemas;
  std::vector<std::unique_ptr<ObTableSchema>> routed_materialization_schemas;
  ObArray<const ObTableSchema *> materialization_schemas;
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
    if (has_logical_schema) { request.read(logical_schema); }
    const uint64_t materialization_schema_count = request.number();
    if (materialization_schema_count > 3
        || (!namespace_local && materialization_schema_count != 0)) {
      return OB_INVALID_ARGUMENT;
    }
    for (uint64_t i = 0; !request.ret && i < materialization_schema_count; ++i) {
      auto logical = std::make_unique<ObTableSchema>(&allocator);
      request.read(*logical);
      if (request.ret) { break; }
      auto routed = std::make_unique<ObTableSchema>(&allocator);
      int schema_ret = !namespace_local || ns == 1
          ? routed->assign(*logical)
          : NamespaceForkKernelPrototype::make_storage_schema(ns, *logical, *routed);
      if (schema_ret != OB_SUCCESS) { return schema_ret; }
      if (OB_FAIL(materialization_schemas.push_back(routed.get()))) { return ret; }
      logical_materialization_schemas.push_back(std::move(logical));
      routed_materialization_schemas.push_back(std::move(routed));
    }
    param.scan_flag_.flag_ = request.number();
    const bool get = request.number() != 0;
    param.limit_param_.limit_ = static_cast<int64_t>(request.number());
    param.limit_param_.offset_ = static_cast<int64_t>(request.number());
    const uint64_t count = request.number();
    fprintf(stderr, "PROTOTYPE_V17_SCAN_REQUEST ns=%llu table=%llu tablet=%llu columns=%llu\n",
        (unsigned long long)ns, (unsigned long long)logical_table_id,
        (unsigned long long)logical_tablet_id, (unsigned long long)count);
    if (request.ret || count > OB_MAX_COLUMN_NUMBER) { return OB_NOT_SUPPORTED; }
    bool logical_tablet_matches = !has_logical_schema;
    if (has_logical_schema) {
      ObArray<ObTabletID> tablets;
      if (OB_FAIL(logical_schema.get_tablet_ids(tablets))) {
        return ret;
      }
      for (int64_t i = 0; i < tablets.count(); ++i) {
        if (tablets.at(i).id() == logical_tablet_id) {
          logical_tablet_matches = true;
          break;
        }
      }
    }
    if (request.ret || (!has_logical_schema && namespace_local && ns > 1)
        || (has_logical_schema && ((requested_schema_version <= 0
                && !is_inner_table(logical_table_id))
            || logical_schema.get_table_id() != logical_table_id
            || !logical_tablet_matches
            || logical_schema.get_schema_version() < 0
            || (requested_schema_version > 0
                && logical_schema.get_schema_version() != requested_schema_version)))) {
      return OB_INVALID_ARGUMENT;
    }
    if (has_logical_schema) {
      if (!namespace_local || ns == 1) {
        schema = &logical_schema;
      } else {
        ret = NamespaceForkKernelPrototype::make_storage_schema(
            ns, logical_schema, routed_schema);
        if (!ret) { schema = &routed_schema; }
      }
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
    ObArray<ObTabletID> storage_tablets;
    if (OB_SUCC(ret) && schema != nullptr
        && OB_FAIL(schema->get_tablet_ids(storage_tablets))) {
    }
    for (int64_t i = 0; OB_SUCC(ret) && i < storage_tablets.count(); ++i) {
      if (storage_tablets.at(i).id() == tablet_id) {
        storage_tablet_matches = true;
        break;
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
    if (ret || request.ret || ranges > 256) { return ret ? ret : OB_NOT_SUPPORTED; }
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
        ret = request.ret ? request.ret : ob_write_obj(allocator, value, keys[i * width * 2 + j]);
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
      param.scan_allocator_ = &allocator; param.reserved_cell_count_ = count;
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
    param.allocator_ = &allocator; param.scan_allocator_ = &allocator;
    param.reserved_cell_count_ = count;
    // Match the native SQL scan path: every LOB storage column needs a V2
    // locator, including __all_* columns. The worker owns SQL but the bytes
    // still live in the shared storage process, so an unmarked system-table
    // locator cannot be materialized after it crosses IPC.
    table.get_enable_lob_locator_v2() = true;
    if (!materialization_schemas.empty()) {
      ret = NamespaceForkKernelPrototype::ensure_tablet(
          ObTabletID(tablet_id), *schema, materialization_schemas);
    }
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
      if (!request.consumed() || it == scans.end()) { reply.number(OB_INVALID_ARGUMENT); }
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
    bool send_logical_schema = owns_namespace_schema()
        && !NamespaceForkKernelPrototype::is_encoded_id(param.index_id_);
    StorageSpaceHandle storage_space =
        StorageSpaceHandle::namespace_space(worker_namespace);
    int ret = send_logical_schema
        ? worker_local_table_schema(
              param.index_id_, param.schema_version_, schema_guard, logical_schema,
              storage_space)
        : OB_SUCCESS;
    const bool namespace_local = storage_space.is_namespace();
    ObArray<const ObTableSchema *> materialization_schemas;
    if (!ret && send_logical_schema && namespace_local && worker_namespace > 1) {
      ret = worker_materialization_schemas(
          *logical_schema, schema_guard, materialization_schemas);
    }
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
    if (send_logical_schema) { request.append(*logical_schema); }
    request.number(materialization_schemas.count());
    for (const ObTableSchema *schema : materialization_schemas) {
      request.append(*schema);
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
private:
  int exchange(const Frame &request, Frame &reply) {
    StorageSessionScope scope(param.op_ ? param.op_->get_eval_ctx().exec_ctx_.get_my_session() : nullptr);
    int ret = scope.error() ? scope.error() : worker_send(request, request.type() == 'X');
    if (!ret) { ret = worker_read(reply); }
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
    scan->reset(); return scan->open();
  }
};
} } }
