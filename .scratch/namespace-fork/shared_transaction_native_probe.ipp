// Local integration probe for borrowing the actual SQL-owned native transaction.
static int run_shared_transaction_native_probe()
{
  auto &kv = *directory_kv_store();
  using Tx = InstanceMetaStore::Transaction;
  using Access = query::ObInnerSQLConnectionAccess;
  const auto collection = static_cast<MetaCollection>(10004);
  const ObString key = ObString::make_string("shared-transaction");
  const ObString value = ObString::make_string("committed");
  const uint64_t tablet = encoded(1, 4294900000ULL);
  const uint64_t catalog_namespace = 600000;
  auto deadline = [] { return ObTimeUtility::current_time() + 30000000; };
  auto mapping_exists = [&](ObISQLClient &sql, bool &found) -> int {
    ObSqlString query;
    int ret = query.append_fmt("SELECT tablet_id FROM oceanbase.__all_tablet_to_table WHERE tablet_id=%lu", tablet);
    ObISQLClient::ReadResult result;
    if (ret == OB_SUCCESS) { ret = sql.read(result, query.ptr()); }
    if (ret == OB_SUCCESS) {
      ret = result.get_result()->next();
      found = ret == OB_SUCCESS;
      if (ret == OB_ITER_END) { ret = OB_SUCCESS; }
    }
    return ret;
  };
  int ret = OB_SUCCESS;
#define SHARED_CALL(expr) do { ret = (expr); if (ret != OB_SUCCESS) { \
  fprintf(stderr, "INSTANCE_META_PROBE_FAIL shared line=%d ret=%d expr=%s\n", __LINE__, ret, #expr); return ret; } } while (0)
#define SHARED_CHECK(expr) do { if (!(expr)) { \
  fprintf(stderr, "INSTANCE_META_PROBE_FAIL shared line=%d expr=%s\n", __LINE__, #expr); return OB_ERR_UNEXPECTED; } } while (0)
  for (bool commit : {false, true}) {
    Tx borrowed;
    ObMySQLTransaction sql;
    struct Cleanup {
      InstanceMetaStore &kv; Tx &tx; ObMySQLTransaction &sql;
      ~Cleanup() { if (sql.is_started()) { sql.end(false); } if (tx.is_active()) { kv.detach(tx); } }
    } cleanup{kv, borrowed, sql};
    SHARED_CALL(sql.start(directory_sql_proxy()));
    transaction::ObTxDesc *identity = nullptr;
    SCN pinned_before;
    SHARED_CALL(kv.min_retained_snapshot(pinned_before));
    SHARED_CALL(Access::with_native_transaction(sql.get_connection(), [&](transaction::ObTxDesc &native) -> int {
      identity = &native;
      int rc = kv.attach(borrowed, native, deadline());
      if (rc == OB_SUCCESS) { rc = kv.put(borrowed, collection, key, value); }
      rootserver::InstanceNamespaceMetadata metadata(kv, borrowed);
      rootserver::InstanceNamespaceRecord record;
      record.id = catalog_namespace;
      record.name = "shared-catalog-tx-probe";
      record.roots.schema_version = 1;
      if (rc == OB_SUCCESS) { rc = metadata.insert_namespace(record); }
      ns::CatalogChanges definitions, sources;
      definitions["table"] = {{ns::NamespaceCatalogCodec::encode_entry(0, 4294900000ULL, 0), 0}, false};
      sources["tablet"] = {{ns::NamespaceCatalogCodec::encode_entry(0, 4294900000ULL, 4294900000ULL, tablet), 0}, false};
      if (rc == OB_SUCCESS) {
        rc = metadata.stage_catalog_delta(catalog_namespace, 1, 2, definitions, sources);
      }
      return rc;
    }));
    SHARED_CHECK(kv.commit(borrowed) == OB_INVALID_ARGUMENT);
    SHARED_CHECK(kv.rollback(borrowed) == OB_INVALID_ARGUMENT);
    ObArray<ObTabletTablePair> mappings;
    SHARED_CALL(mappings.push_back(ObTabletTablePair(ObTabletID(tablet), 4294900000ULL)));
    SHARED_CALL(ObTabletMappingTableOperator::batch_update(sql, mappings));
    bool found = false;
    SHARED_CALL(mapping_exists(sql, found));
    SHARED_CHECK(found);
    SHARED_CALL(Access::with_native_transaction(sql.get_connection(), [&](transaction::ObTxDesc &native) -> int {
      if (&native != identity) { return OB_ERR_UNEXPECTED; }
      ObArenaAllocator allocator(ObMemAttr("SharedTxProbe"));
      ObString actual;
      int rc = kv.get(borrowed, collection, key, allocator, actual);
      if (rc == OB_SUCCESS && actual != value) { rc = OB_ERR_UNEXPECTED; }
      if (rc == OB_SUCCESS) { rc = kv.put(borrowed, collection, key, value); }
      return rc;
    }));
    SCN retained;
    SHARED_CALL(kv.min_retained_snapshot(retained));
    SHARED_CHECK(retained >= pinned_before);
    SHARED_CALL(sql.end(commit));
    // The SQL descriptor has been released. KV's retention and GC guard must
    // still exist, and detaching must not touch the released descriptor.
    SCN after_end;
    SHARED_CALL(kv.min_retained_snapshot(after_end));
    // The weak-read horizon may advance independently while SQL ends. The
    // borrowed transaction still bounds retention by its selected snapshot.
    SHARED_CHECK(after_end >= retained && after_end <= borrowed.snapshot_version());
    Tx blocked_gc;
    SHARED_CHECK(kv.begin_directory_gc(blocked_gc, ObTimeUtility::current_time() + 2000) == OB_TIMEOUT);
    SHARED_CALL(kv.detach(borrowed));
    Tx gc;
    SHARED_CALL(kv.begin_directory_gc(gc, deadline()));
    SHARED_CALL(kv.rollback(gc));
    Tx reader;
    SHARED_CALL(kv.begin(reader, deadline()));
    ObArenaAllocator allocator(ObMemAttr("SharedTxProbe"));
    ObString actual;
    ret = kv.get(reader, collection, key, allocator, actual);
    SHARED_CHECK(commit ? ret == OB_SUCCESS && actual == value : ret == OB_ENTRY_NOT_EXIST);
    rootserver::InstanceNamespaceMetadata metadata(kv, reader);
    rootserver::InstanceNamespaceRecord record;
    ret = metadata.get_namespace(catalog_namespace, record);
    SHARED_CHECK(commit ? ret == OB_SUCCESS && record.roots.schema_version == 2
        && record.roots.catalog.page != 0 && record.roots.directory.page != 0
        : ret == OB_ENTRY_NOT_EXIST);
    if (commit) {
      rootserver::InstanceCatalogPageStore pages(metadata);
      ns::NamespaceCatalogTree tree(pages);
      ns::CatalogValue source;
      SHARED_CHECK(tree.find(record.roots.directory, "tablet", source).ok()
          && source.data == ns::NamespaceCatalogCodec::encode_entry(
              0, 4294900000ULL, 4294900000ULL, tablet));
    }
    SHARED_CALL(kv.commit(reader));
    SHARED_CALL(mapping_exists(*directory_sql_proxy(), found));
    SHARED_CHECK(found == commit);
    fprintf(stderr, "INSTANCE_SHARED_TX_PHASE commit=%d own_writes=1 sql_mapping=1 guards=1\n", commit);
  }
  // Cleanup the probe's committed rows in another shared transaction.
  Tx borrowed;
  ObMySQLTransaction sql;
  SHARED_CALL(sql.start(directory_sql_proxy()));
  SHARED_CALL(Access::with_native_transaction(sql.get_connection(), [&](transaction::ObTxDesc &native) -> int {
    int rc = kv.attach(borrowed, native, deadline());
    bool erased = false;
    if (rc == OB_SUCCESS) { rc = kv.erase(borrowed, collection, key, erased); }
    rootserver::InstanceNamespaceMetadata metadata(kv, borrowed);
    if (rc == OB_SUCCESS) { rc = metadata.erase_namespace(catalog_namespace); }
    return rc == OB_SUCCESS && !erased ? OB_ERR_UNEXPECTED : rc;
  }));
  ObSqlString remove;
  SHARED_CALL(remove.append_fmt("DELETE FROM oceanbase.__all_tablet_to_table WHERE tablet_id=%lu", tablet));
  int64_t affected = 0;
  SHARED_CALL(sql.write(remove.ptr(), affected));
  SHARED_CHECK(affected == 1);
  SHARED_CALL(sql.end(true));
  SHARED_CALL(kv.detach(borrowed));
  fprintf(stderr, "INSTANCE_SHARED_TX_PROBE_PASS native_identity=1 commit=1 rollback=1 guards=1\n");
#undef SHARED_CHECK
#undef SHARED_CALL
  return OB_SUCCESS;
}
