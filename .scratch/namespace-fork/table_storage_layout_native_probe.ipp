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

  auto count_owner = [&](Tx &reader, uint64_t id, int64_t &count) {
    count = 0;
    return store.scan(reader, MetaCollection::TABLE_STORAGE_LAYOUTS, {},
        [&](const ObString &key, const ObString &, bool &) {
      int64_t found = 0, pos = 0;
      int rc = serialization::decode_i64(key.ptr(), key.length(), pos, &found);
      if (rc == OB_SUCCESS && found == id) { ++count; }
      return rc;
    });
  };
  Tx old_reader;
  BIND_CALL(store.begin(old_reader, deadline(), true));
  int64_t count = 0;
  BIND_CALL(count_owner(old_reader, child, count));
  BIND_CHECK(count == 1);
  auto &directory = share::server_service<ObAccessService>()->instance_meta_store();
  const auto marker_collection = static_cast<MetaCollection>(10072);
  const auto marker_key = ObString::make_string("layout_retirement");
  // Namespace deletion and binding retirement use two KV tablets, one native
  // transaction. Verify both rollback and commit, including MVCC old readers.
  for (bool commit : {false, true}) {
    Tx owner_tx, participant;
    BIND_CALL(directory.begin(owner_tx, deadline()));
    BIND_CALL(store.attach(participant, owner_tx, deadline()));
    BIND_CALL(directory.put(owner_tx, marker_collection, marker_key, marker_key));
    BIND_CALL(TableStorageLayouts(store, participant, child).retire_namespace());
    BIND_CALL(count_owner(participant, child, count));
    BIND_CHECK(count == 0);
    BIND_CALL(commit ? directory.commit(owner_tx) : directory.rollback(owner_tx));
    BIND_CALL(store.detach(participant));
    BIND_CALL(store.begin(first, deadline(), true));
    BIND_CALL(count_owner(first, child, count));
    BIND_CHECK(count == (commit ? 0 : 1));
    BIND_CALL(count_owner(first, owner, count));
    BIND_CHECK(count == 2);
    BIND_CALL(StorageSchemaHistory(store, first).read_version(child_g, version));
    BIND_CHECK(version == 11);
    BIND_CALL(store.commit(first));
    BIND_CALL(directory.begin(owner_tx, deadline(), true));
    ObString marker;
    ret = directory.get(owner_tx, marker_collection, marker_key, allocator, marker);
    BIND_CHECK(commit ? ret == OB_SUCCESS : ret == OB_ENTRY_NOT_EXIST);
    BIND_CALL(directory.commit(owner_tx));
  }
  BIND_CALL(count_owner(old_reader, child, count));
  BIND_CHECK(count == 1);
  BIND_CALL(StorageSchemaHistory(store, old_reader).read_version(child_g, version));
  BIND_CHECK(version == 11);
  BIND_CALL(store.commit(old_reader));

  // Physical exact reads must not depend on a live logical binding or head.
  BIND_CALL(store.begin(first, deadline()));
  char head[24];
  int64_t pos = 0;
  BIND_CALL(serialization::encode_i64(head, sizeof(head), pos, child_g));
  BIND_CALL(serialization::encode_i64(head, sizeof(head), pos, -1));
  BIND_CALL(serialization::encode_i64(head, sizeof(head), pos, 0));
  bool existed = false;
  BIND_CALL(store.erase(first, MetaCollection::STORAGE_LAYOUTS, ObString(sizeof(head), head), existed));
  BIND_CHECK(existed);
  BIND_CALL(store.commit(first));
  BIND_CALL(store.begin(first, deadline(), true));
  const share::SCN after_delete = first.snapshot_version();
  BIND_CALL(store.commit(first));
  bool observed = false;
  const int64_t until = deadline();
  while (!observed && ObTimeUtility::current_time() < until) {
    BIND_CALL(store.begin_weak_read(first, deadline()));
    ret = StorageSchemaHistory(store, first).read_version(child_g, version);
    BIND_CHECK(ret == OB_SUCCESS || ret == OB_ENTRY_NOT_EXIST);
    observed = ret == OB_ENTRY_NOT_EXIST && first.snapshot_version() >= after_delete;
    BIND_CALL(store.commit(first));
    if (!observed) { usleep(10000); }
  }
  BIND_CHECK(observed);
  ObStorageSchema exact;
  BIND_CALL(StorageSchemaHistory::read_published(store, child_g, 11, deadline(), allocator, exact));
  BIND_CHECK(exact.get_schema_version() == 11);
  BIND_CALL(store.begin(first, deadline()));
  BIND_CALL(TableStorageLayouts(store, first, owner).retire(table));
  BIND_CALL(TableStorageLayouts(store, first, owner).retire(table));
  BIND_CALL(count_owner(first, owner, count));
  BIND_CHECK(count == 1);
  BIND_CALL(store.commit(first));
  fprintf(stderr, "TABLE_LAYOUT_PASS parent=%lu child=%lu conflict_nowait=1 no_seed_overwrite=1 rollback=1 participant_lifetime=1 retire_mvcc=1 cross_store_atomic=1 exact_without_head=1\n",
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
