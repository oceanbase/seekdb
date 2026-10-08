/* Copyright (c) 2025 OceanBase. Licensed under the Apache License, Version 2.0. */
#include "rootserver/fork_table/namespace_schema_publication.h"
#include "rootserver/ob_tablet_drop.h"
#include "rootserver/fork_table/instance_namespace_metadata.h"
#include "rootserver/fork_table/table_creation_descriptor.h"
#include "query/session/ob_inner_sql_connection_access.h"
#include "share/schema/ob_multi_version_schema_service.h"
#include "share/schema/ob_schema_getter_guard.h"
#include "storage/tablet/ob_tablet_create_delete_helper.h"
#include "storage/tablet/ob_tablet.h"
#include "common/mysqlclient/ob_mysql_transaction.h"
#include "common/mysqlclient/ob_mysql_result.h"
#include "observer/namespace_worker_protocol_prototype.h"
#include <map>
#include <set>

namespace oceanbase {
namespace rootserver {
namespace {
using namespace common;
using namespace share::schema;
using namespace storage;
using ns::NamespaceCatalogCodec;
using Schemas = std::map<uint64_t, const ObTableSchema *>;

int publication_tablets(const ObTableSchema &schema, ObIArray<ObTabletID> &ids)
{
  if (!schema.has_tablet()) { return OB_SUCCESS; }
  int ret = schema.get_tablet_ids(ids);
  if (ret == OB_SUCCESS && schema.get_hidden_partition_num() > 0) {
    ret = schema.get_first_level_hidden_tablet_ids(ids);
  }
  return ret;
}

int publication_physical_status(uint64_t physical, ObTabletCreateDeleteMdsUserData &data)
{
  ObTabletHandle handle;
  int ret = ObTabletCreateDeleteHelper::check_and_get_tablet(ObTabletMapKey(ObTabletID(physical)),
      handle, 0, ObMDSGetTabletMode::READ_WITHOUT_CHECK, transaction::ObTransVersion::MAX_TRANS_VERSION);
  if (ret == OB_SUCCESS && handle.get_obj()->is_empty_shell()) { ret = OB_TABLET_NOT_EXIST; }
  if (ret == OB_SUCCESS) {
    mds::MdsWriter writer;
    mds::TwoPhaseCommitState state;
    share::SCN version;
    ret = handle.get_obj()->get_latest_tablet_status(data, writer, state, version);
  }
  return ret;
}

// Both sides of a DDL delta come from the SQL owner's transaction. A prior
// parallel DDL can have published its root before the runtime schema cache has
// refreshed; that cache is not the authority for this publication's baseline.
int publication_schema(ObSchemaService &backend, const ObRefreshSchemaStatus &status,
    ObMySQLTransaction &sql, ObIAllocator &allocator, uint64_t id, int64_t version,
    bool historical, ObTableSchema *&schema)
{
  schema = nullptr;
  ObSqlString query;
  ObISQLClient::ReadResult result;
  int ret = historical
      ? query.append_fmt("SELECT is_deleted FROM oceanbase.__all_table_history "
          "WHERE table_id=%lu AND schema_version<=%ld ORDER BY schema_version DESC LIMIT 1", id, version)
      : query.append_fmt("SELECT 0 AS is_deleted FROM oceanbase.__all_table WHERE table_id=%lu", id);
  int64_t deleted = 0;
  if (ret == OB_SUCCESS) { ret = sql.read(result, query.ptr()); }
  if (ret == OB_SUCCESS) { ret = result.get_result()->next(); }
  const bool missing = ret == OB_ITER_END;
  if (ret == OB_SUCCESS) { ret = result.get_result()->get_int("is_deleted", deleted); }
  const int close_ret = result.close();
  // Closing the cursor does not release the handler's connection reference.
  result.reset();
  if (ret == OB_SUCCESS || missing) { ret = close_ret; }
  if (ret == OB_SUCCESS && !missing && deleted == 0) {
    ret = backend.get_table_schema(status, id, version, sql, allocator, schema);
    if (ret == OB_SUCCESS && schema == nullptr) { ret = OB_ERR_UNEXPECTED; }
    ObArray<ObAuxTableMetaInfo> auxiliary;
    if (ret == OB_SUCCESS) { ret = backend.fetch_aux_tables(status, id, version, sql, auxiliary); }
    for (int64_t i = 0; ret == OB_SUCCESS && i < auxiliary.count(); ++i) {
      const auto &aux = auxiliary.at(i);
      if (aux.table_type_ == AUX_LOB_META) { schema->set_aux_lob_meta_tid(aux.table_id_); }
      else if (aux.table_type_ == AUX_LOB_PIECE) { schema->set_aux_lob_piece_tid(aux.table_id_); }
    }
  }
  return ret;
}

// Prepares complete binding units in one publication. This is transaction-local
// work, not a persistent schema cache or per-partition copy of table definitions.
int build_publication(InstanceNamespaceMetadata &metadata,
    const InstanceNamespaceRecord &record, int64_t version,
    const Schemas &current, const Schemas &previous,
    std::vector<uint64_t> &removed_physical)
{
  int ret = OB_SUCCESS;
  ns::CatalogChanges definitions, sources;
  std::map<uint64_t, uint64_t> previous_tablets, current_tablets;
  std::map<uint64_t, ns::CatalogTabletSource> bindings;
  for (const auto &entry : previous) {
    if (ret != OB_SUCCESS) { break; }
    ObArray<ObTabletID> ids;
    ret = publication_tablets(*entry.second, ids);
    for (const auto &id : ids) { previous_tablets[id.id()] = entry.first; }
    if (current.count(entry.first) == 0 || !current.at(entry.first)->has_tablet()) {
      definitions[NamespaceCatalogCodec::object_key(entry.first)] = {{}, true};
    }
  }
  for (const auto &entry : current) {
    if (ret != OB_SUCCESS) { break; }
    const auto &schema = *entry.second;
    if (!schema.has_tablet()) { continue; }
    TableCreationDescriptor description;
    std::string bytes;
    uint64_t object = 0;
    if (OB_FAIL(description.init(schema, DATA_CURRENT_VERSION))) {
    } else if (OB_FAIL(description.encode(bytes))) {
    } else if (OB_FAIL(metadata.save_object(bytes, object))) {
    } else {
      definitions[NamespaceCatalogCodec::object_key(entry.first)] = {
          {NamespaceCatalogCodec::encode_entry(object, entry.first, 0), 0}, false};
    }
    if (schema.is_aux_lob_table()) { continue; }
    const uint64_t ids[] = {entry.first, schema.get_aux_lob_meta_tid(), schema.get_aux_lob_piece_tid()};
    ObArray<ObTabletID> tablets[3];
    for (int i = 0; ret == OB_SUCCESS && i < 3; ++i) {
      if (ids[i] == 0 || ids[i] == OB_INVALID_ID) { continue; }
      const auto found = current.find(ids[i]);
      if (found == current.end()) { ret = OB_SCHEMA_EAGAIN; }
      else { ret = publication_tablets(*found->second, tablets[i]); }
      if (ret == OB_SUCCESS && tablets[i].count() != tablets[0].count()) { ret = OB_STATE_NOT_MATCH; }
    }
    for (int64_t i = 0; ret == OB_SUCCESS && i < tablets[0].count(); ++i) {
      ns::CatalogTabletSource binding;
      binding.data_tablet_id = tablets[0].at(i).id();
      binding.lob_meta_tablet_id = tablets[1].empty() ? 0 : tablets[1].at(i).id();
      binding.lob_piece_tablet_id = tablets[2].empty() ? 0 : tablets[2].at(i).id();
      for (int j = 0; j < 3; ++j) {
        if (tablets[j].empty()) { continue; }
        const uint64_t logical = tablets[j].at(i).id();
        binding.table_id = ids[j];
        if (!bindings.emplace(logical, binding).second) { ret = OB_STATE_NOT_MATCH; break; }
        current_tablets[logical] = ids[j];
      }
    }
  }
  for (const auto &entry : previous_tablets) {
    if (ret != OB_SUCCESS) { break; }
    if (current_tablets.count(entry.first) == 0) {
      ns::CatalogTabletSource previous_source;
      int64_t cap = 0;
      ret = metadata.find_tablet_source(record.roots.directory, entry.first, previous_source, cap);
      if (ret != OB_SUCCESS) { break; }
      if (previous_source.physical_tablet_id == ns::NamespaceObjectKey{record.id, entry.first}.storage_id()) {
        ObTabletCreateDeleteMdsUserData status;
        ret = publication_physical_status(previous_source.physical_tablet_id, status);
        if (ret != OB_SUCCESS) { break; }
        if (status.create_transaction_id_ != previous_source.create_transaction_id) {
          ret = OB_STATE_NOT_MATCH;
          break;
        }
        removed_physical.push_back(previous_source.physical_tablet_id);
      }
      sources[NamespaceCatalogCodec::object_key(entry.first)] = {{}, true};
    }
  }
  for (const auto &entry : bindings) {
    if (ret != OB_SUCCESS) { break; }
    const uint64_t physical = ns::NamespaceObjectKey{record.id, entry.first}.storage_id();
    ns::CatalogTabletSource source = entry.second;
    int64_t cap = 0;
    ObTabletCreateDeleteMdsUserData data;
    ret = publication_physical_status(physical, data);
    if (ret == OB_TABLET_NOT_EXIST || ret == OB_ENTRY_NOT_EXIST) {
      // A definition can change while its data is still inherited. Preserve
      // the already published physical identity and its fixed snapshot cap.
      ns::CatalogTabletSource inherited;
      ret = metadata.find_tablet_source(record.roots.directory, entry.first, inherited, cap);
      if (ret == OB_SUCCESS && inherited.table_id != source.table_id) { ret = OB_STATE_NOT_MATCH; }
      source.physical_tablet_id = inherited.physical_tablet_id;
      source.create_transaction_id = inherited.create_transaction_id;
    } else if (ret == OB_SUCCESS) {
      source.physical_tablet_id = physical;
      source.create_transaction_id = data.create_transaction_id_;
    }
    if (ret == OB_SUCCESS && !source.is_valid()) { ret = OB_STATE_NOT_MATCH; }
    if (ret == OB_SUCCESS) {
      sources[NamespaceCatalogCodec::object_key(entry.first)] = {
          {NamespaceCatalogCodec::encode_source(source), cap}, false};
    }
  }
  if (ret == OB_SUCCESS) {
    ret = metadata.stage_catalog_delta(record.id, record.roots.schema_version,
        version, definitions, sources);
  }
  return ret;
}
} // namespace

int NamespaceSchemaPublication::initialize(share::schema::ObSchemaGetterGuard &guard)
{
  using namespace common;
  InstanceNamespaceMetadata metadata(store_, transaction_);
  InstanceNamespaceRecord record;
  common::ObArray<const share::schema::ObTableSchema *> tables;
  int ret = store_.begin(transaction_, ObTimeUtility::current_time() + 120000000);
  if (ret == OB_SUCCESS) { ret = metadata.get_namespace(namespace_id_, record, true); }
  if (ret == OB_SUCCESS && (record.roots.catalog.page != 0 || record.roots.directory.page != 0)) {
    ret = OB_INIT_TWICE;
  }
  if (ret == OB_SUCCESS) { ret = guard.get_table_schemas_in_runtime(tables); }
  Schemas current;
  for (const auto *table : tables) { if (table->has_tablet()) { current[table->get_table_id()] = table; } }
  if (ret == OB_SUCCESS) {
    std::vector<uint64_t> removed;
    ret = build_publication(metadata, record, record.roots.schema_version, current, {}, removed);
  }
  if (transaction_.is_active()) {
    const int end = ret == OB_SUCCESS ? store_.commit(transaction_) : store_.rollback(transaction_);
    if (ret == OB_SUCCESS) { ret = end; }
  }
  return ret;
}

int NamespaceSchemaPublication::stage(common::ObMySQLTransaction &sql,
    share::schema::ObMultiVersionSchemaService &service, int64_t version)
{
  using namespace common;
  using namespace share::schema;
  InstanceNamespaceMetadata metadata(store_, transaction_);
  InstanceNamespaceRecord record;
  int ret = query::ObInnerSQLConnectionAccess::with_native_transaction(sql.get_connection(),
      [&](transaction::ObTxDesc &native) -> int {
    int rc = store_.attach(transaction_, native, THIS_WORKER.get_timeout_ts());
    if (rc == OB_SUCCESS) { rc = metadata.get_namespace(namespace_id_, record, true); }
    return rc;
  });
  if (ret == OB_SUCCESS && (record.roots.state != 0 || version <= record.roots.schema_version)) {
    ret = OB_STATE_NOT_MATCH;
  }
  ObRefreshSchemaStatus status;
  ObSchemaService::SchemaOperationSetWithAlloc operations;
  auto *backend = service.get_schema_service();
  if (ret == OB_SUCCESS && backend == nullptr) { ret = OB_NOT_INIT; }
  if (ret == OB_SUCCESS) {
    ret = backend->get_increment_schema_operations(status, record.roots.schema_version, version, sql, operations);
  }
  std::set<uint64_t> pending, processed;
  for (int64_t i = 0; ret == OB_SUCCESS && i < operations.count(); ++i) {
    const auto &operation = operations.at(i);
    // Other schema operations reuse table_id_ for unrelated object IDs
    // (for example, routine privileges). Only table DDL changes these roots.
    if (operation.op_type_ > OB_DDL_TABLE_OPERATION_BEGIN
        && operation.op_type_ < OB_DDL_TABLE_OPERATION_END
        && operation.table_id_ != 0 && operation.table_id_ != OB_INVALID_ID) {
      pending.insert(operation.table_id_);
    }
  }
  ObArenaAllocator allocator("CatalogPublish");
  Schemas current, previous;
  std::vector<ObTableSchema *> allocated;
  auto include_family = [&](const ObTableSchema *schema) {
    if (schema == nullptr || !schema->has_tablet()) { return; }
    if (schema->is_aux_lob_table()) { pending.insert(schema->get_data_table_id()); }
    for (uint64_t id : {schema->get_aux_lob_meta_tid(), schema->get_aux_lob_piece_tid()}) {
      if (id != 0 && id != OB_INVALID_ID) { pending.insert(id); }
    }
  };
  while (ret == OB_SUCCESS && !pending.empty()) {
    const uint64_t id = *pending.begin();
    pending.erase(pending.begin());
    if (!processed.insert(id).second) { continue; }
    ObTableSchema *old = nullptr;
    ret = publication_schema(*backend, status, sql, allocator, id,
        record.roots.schema_version, true, old);
    if (old != nullptr) { allocated.push_back(old); }
    if (ret != OB_SUCCESS) { break; }
    if (old != nullptr) { previous[id] = old; include_family(old); }
    ObTableSchema *schema = nullptr;
    ret = publication_schema(*backend, status, sql, allocator, id, version, false, schema);
    if (schema != nullptr) { allocated.push_back(schema); }
    if (ret != OB_SUCCESS) { break; }
    if (schema == nullptr) { continue; }
    current[id] = schema;
    include_family(schema);
  }
  std::vector<uint64_t> removed_physical;
  if (ret == OB_SUCCESS) { ret = build_publication(metadata, record, version, current, previous, removed_physical); }
  // Native mapping/history SQL has already run in this DDL transaction. Only
  // register physical DELETE here, for the local incarnations removed from its
  // source tree. Inherited sources remain owned by their original physical copy.
  // SQL, source roots and DELETE MDS share the same commit/rollback boundary.
  if (ret == OB_SUCCESS && !removed_physical.empty()) {
    ObArray<ObTabletID> tablets;
    for (uint64_t physical : removed_physical) {
      if (OB_FAIL(tablets.push_back(ObTabletID(physical)))) { break; }
    }
    if (ret == OB_SUCCESS) {
      observer::namespace_worker_prototype::PhysicalTabletMdsScope physical_mds(true);
      ret = ObTabletDrop::register_delete(sql, tablets);
    }
  }
  for (auto *schema : allocated) { schema->~ObTableSchema(); }
  return ret;
}

int NamespaceSchemaPublication::detach()
{
  return transaction_.is_active() ? store_.detach(transaction_) : common::OB_SUCCESS;
}

} // namespace rootserver
} // namespace oceanbase
