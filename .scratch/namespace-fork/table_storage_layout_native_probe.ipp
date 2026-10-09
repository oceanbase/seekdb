// Native transactional tests for the upper-layer table -> layout binding.
static int run_table_storage_layout_native_probe()
{
  auto &store = share::server_service<ObAccessService>()->storage_schema_store();
  using Tx = InstanceMetaStore::Transaction;
  using rootserver::TableStorageLayouts;
  const uint64_t owner = 10001, child = 10002;
  const uint64_t table = 900000071;
  auto deadline = [] { return ObTimeUtility::current_time() + 60000000; };
  int ret = OB_SUCCESS;
#define BIND_CALL(expr) do { ret = (expr); if (ret != OB_SUCCESS) { \
  fprintf(stderr, "TABLE_LAYOUT_FAIL line=%d ret=%d expr=%s\n", __LINE__, ret, #expr); return ret; } } while (0)
#define BIND_CHECK(expr) do { if (!(expr)) { \
  fprintf(stderr, "TABLE_LAYOUT_FAIL line=%d check=%s\n", __LINE__, #expr); return OB_ERR_UNEXPECTED; } } while (0)
  ObArenaAllocator allocator(ObMemAttr("LayoutBinding"));
  auto definition = [&](uint64_t id, int64_t version, ObCreateTabletSchema &out) {
    ObTableSchema sql;
    int rc = InstanceMetaStore::build_schema(ObTabletID(id), sql);
    sql.set_schema_version(version);
    if (rc == OB_SUCCESS) { rc = out.init(allocator, sql, false); }
    return rc;
  };
  ObCreateTabletSchema seed, later;
  BIND_CALL(definition(table, 10, seed));
  BIND_CALL(definition(table, 11, later));
  Tx first, contender;
  BIND_CALL(store.begin(first, deadline()));
  BIND_CALL(TableStorageLayouts(store, first, owner).prepare_create(seed));
  const uint64_t parent_g = seed.get_storage_layout_id();
  BIND_CHECK(parent_g != 0);
  BIND_CALL(TableStorageLayouts(store, first, owner).prepare_create(seed));
  BIND_CHECK(seed.get_storage_layout_id() == parent_g);
  BIND_CALL(store.begin(contender, deadline()));
  const int64_t start = ObTimeUtility::current_time();
  ret = TableStorageLayouts(store, contender, owner).prepare_create(seed);
  BIND_CHECK(ret == OB_TRY_LOCK_ROW_CONFLICT);
  BIND_CHECK(ObTimeUtility::current_time() - start < 1000000);
  BIND_CALL(store.rollback(contender));
  BIND_CALL(store.commit(first));

  BIND_CALL(store.begin(first, deadline()));
  BIND_CALL(TableStorageLayouts(store, first, child).prepare_create(seed));
  const uint64_t child_g = seed.get_storage_layout_id();
  BIND_CHECK(child_g != 0 && child_g != parent_g);
  BIND_CALL(TableStorageLayouts(store, first, child).publish(later));
  BIND_CHECK(later.get_storage_layout_id() == child_g);
  BIND_CALL(store.commit(first));

  // A later partition can present the original seed; it must not revert V=11.
  BIND_CALL(store.begin(first, deadline()));
  BIND_CALL(TableStorageLayouts(store, first, child).prepare_create(seed));
  int64_t version = 0;
  BIND_CALL(StorageSchemaHistory(store, first).read_version(child_g, version));
  BIND_CHECK(seed.get_storage_layout_id() == child_g && version == 11);
  BIND_CALL(StorageSchemaHistory(store, first).read_version(parent_g, version));
  BIND_CHECK(version == 10);
  BIND_CALL(store.commit(first));

  // The SQL transaction, not a short-lived creator, holds the KV participant.
  uint64_t aborted_g = 0;
  std::weak_ptr<Tx> retained;
  ObMySQLTransaction sql;
  BIND_CALL(sql.start(directory_sql_proxy()));
  BIND_CALL(query::ObInnerSQLConnectionAccess::with_native_transaction(sql.get_connection(),
      [&](transaction::ObTxDesc &native) {
    std::shared_ptr<Tx> borrowed;
    int rc = TableStorageLayouts::attach(sql, native, store, borrowed);
    retained = borrowed;
    ObCreateTabletSchema aborted;
    if (rc == OB_SUCCESS) { rc = definition(table + 1, 1, aborted); }
    if (rc == OB_SUCCESS) { rc = TableStorageLayouts(store, *borrowed, owner).prepare_create(aborted); }
    aborted_g = aborted.get_storage_layout_id();
    return rc;
  }));
  BIND_CHECK(!retained.expired());
  BIND_CALL(sql.end(false));
  BIND_CHECK(retained.expired());
  BIND_CALL(store.begin(first, deadline()));
  ret = StorageSchemaHistory(store, first).read_version(aborted_g, version);
  BIND_CHECK(ret == OB_ENTRY_NOT_EXIST);
  ObCreateTabletSchema retry;
  BIND_CALL(definition(table + 1, 1, retry));
  BIND_CALL(TableStorageLayouts(store, first, owner).prepare_create(retry));
  BIND_CHECK(retry.get_storage_layout_id() != aborted_g);
  BIND_CALL(store.commit(first));
  fprintf(stderr, "TABLE_LAYOUT_PASS parent=%lu child=%lu conflict_nowait=1 no_seed_overwrite=1 rollback=1 participant_lifetime=1\n",
      parent_g, child_g);
#undef BIND_CALL
#undef BIND_CHECK
  return OB_SUCCESS;
}

static int audit_table_storage_layouts()
{
  auto &store = share::server_service<ObAccessService>()->storage_schema_store();
  InstanceMetaStore::Transaction tx;
  int ret = store.begin(tx, ObTimeUtility::current_time() + 120000000, true);
  struct Binding { int64_t owner = 0, table = 0, layout = 0; };
  std::vector<Binding> bindings;
  if (ret == OB_SUCCESS) {
    InstanceMetaStore::KeyRange range;
    ret = store.scan(tx, MetaCollection::TABLE_STORAGE_LAYOUTS, range,
        [&](const ObString &key, const ObString &value, bool &) {
      Binding b;
      int64_t pos = 0;
      int rc = serialization::decode_i64(key.ptr(), key.length(), pos, &b.owner);
      if (rc == OB_SUCCESS) { rc = serialization::decode_i64(key.ptr(), key.length(), pos, &b.table); }
      if (rc == OB_SUCCESS && pos != key.length()) { rc = OB_CHECKSUM_ERROR; }
      pos = 0;
      if (rc == OB_SUCCESS) { rc = serialization::decode_i64(value.ptr(), value.length(), pos, &b.layout); }
      if (rc == OB_SUCCESS && pos != value.length()) { rc = OB_CHECKSUM_ERROR; }
      if (rc == OB_SUCCESS) { bindings.push_back(b); }
      return rc;
    });
  }
  for (const auto &b : bindings) {
    if (ret != OB_SUCCESS) { break; }
    int64_t version = 0;
    ret = StorageSchemaHistory(store, tx).read_version(b.layout, version);
    fprintf(stderr, "TABLE_LAYOUT_AUDIT ns=%ld table=%ld layout=%ld version=%ld ret=%d\n",
        b.owner, b.table, b.layout, version, ret);
  }
  if (tx.is_active()) {
    const int end = store.commit(tx);
    if (ret == OB_SUCCESS) { ret = end; }
  }
  ObLS *ls = nullptr;
  if (ret == OB_SUCCESS) { ret = share::server_service<ObLSService>()->get_ls(ls); }
  ObLSTabletIterator iter(ObMDSGetTabletMode::READ_WITHOUT_CHECK);
  if (ret == OB_SUCCESS) { ret = ls->get_tablet_svr()->build_tablet_iter(iter, true); }
  int64_t count = 0;
  while (ret == OB_SUCCESS) {
    ObTabletHandle handle;
    ret = iter.get_next_tablet(handle);
    if (ret == OB_ITER_END) { ret = OB_SUCCESS; break; }
    if (ret != OB_SUCCESS) { break; }
    if (handle.get_obj()->is_empty_shell()) { continue; }
    const auto &meta = handle.get_obj()->get_tablet_meta();
    fprintf(stderr, "TABLET_LAYOUT_AUDIT tablet=%lu table=%lu layout=%lu\n",
        meta.tablet_id_.id(), meta.create_table_id_, meta.storage_layout_id_);
    ++count;
  }
  fprintf(stderr, "TABLE_LAYOUT_AUDIT_END bindings=%zu tablets=%ld ret=%d\n", bindings.size(), count, ret);
  return ret;
}
