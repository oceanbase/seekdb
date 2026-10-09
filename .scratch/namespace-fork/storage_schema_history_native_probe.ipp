// Only included by a native integration test build.
static int run_storage_schema_history_native_probe()
{
  using Tx = InstanceMetaStore::Transaction;
  auto &store = share::server_service<ObAccessService>()->storage_schema_store();
  auto deadline = [] { return ObTimeUtility::current_time() + 120000000; };
  const uint64_t layout_id = 900000001;
  ObArenaAllocator allocator(ObMemAttr("LayoutProbe"));
  int ret = OB_SUCCESS;
#define LAYOUT_CALL(expr) do { ret = (expr); if (ret != OB_SUCCESS) { \
  fprintf(stderr, "LAYOUT_HISTORY_FAIL line=%d ret=%d expr=%s\n", __LINE__, ret, #expr); return ret; } } while (0)
#define LAYOUT_CHECK(expr) do { if (!(expr)) { \
  fprintf(stderr, "LAYOUT_HISTORY_FAIL line=%d ret=%d check=%s\n", __LINE__, ret, #expr); return OB_ERR_UNEXPECTED; } } while (0)
  auto definition = [&](int64_t version, int64_t default_size, ObStorageSchema &physical) -> int {
    ObTableSchema sql(&allocator);
    int rc = InstanceMetaStore::build_schema(ObTabletID(900000002), sql);
    sql.set_schema_version(version);
    ObColumnSchemaV2 column;
    column.set_table_id(sql.get_table_id());
    column.set_column_id(OB_APP_MIN_COLUMN_ID + 3);
    column.set_schema_version(version);
    column.set_nullable(false);
    column.set_rowkey_position(0);
    ObObjMeta type;
    type.set_varbinary();
    column.set_meta_type(type);
    column.set_data_length(200000);
    if (rc == OB_SUCCESS) { rc = column.set_column_name("layout_default"); }
    std::string bytes(default_size, 'z');
    if (default_size > 1) { bytes[1] = '\0'; }
    ObObj value;
    value.set_varbinary(ObString(bytes.size(), bytes.data()));
    if (rc == OB_SUCCESS) { rc = column.set_orig_default_value(value); }
    if (rc == OB_SUCCESS) { rc = column.set_cur_default_value(value, false); }
    if (rc == OB_SUCCESS) { rc = sql.add_column(column); }
    sql.set_max_used_column_id(column.get_column_id());
    if (rc == OB_SUCCESS) { rc = physical.init(allocator, sql, false); }
    return rc;
  };
  auto read_equal = [&](Tx &tx, uint64_t id, const ObStorageSchema &expected) -> int {
    ObArenaAllocator memory(ObMemAttr("LayoutCompare"));
    ObStorageSchema actual;
    StorageSchemaHistory history(store, tx);
    int rc = history.read(id, memory, actual);
    if (rc != OB_SUCCESS) { return rc; }
    std::string left(expected.get_serialize_size(), '\0'), right(actual.get_serialize_size(), '\0');
    int64_t a = 0, b = 0;
    rc = expected.serialize(&left[0], left.size(), a);
    if (rc == OB_SUCCESS) { rc = actual.serialize(&right[0], right.size(), b); }
    return rc == OB_SUCCESS && (left != right || a != b) ? OB_ERR_UNEXPECTED : rc;
  };
  ObStorageSchema large, small, newer;
  LAYOUT_CALL(definition(10, 140000, large));
  LAYOUT_CALL(definition(11, 800, small));
  LAYOUT_CALL(definition(12, 180000, newer));
  LAYOUT_CHECK(large.get_serialize_size() > InstanceMetaStore::MAX_VALUE_LENGTH * 2);
  Tx seed;
  LAYOUT_CALL(store.begin(seed, deadline()));
  ObStorageSchema recovered_schema;
  ret = StorageSchemaHistory(store, seed).read(layout_id, allocator, recovered_schema);
  const bool recovered = ret == OB_SUCCESS;
  LAYOUT_CHECK(recovered || ret == OB_ENTRY_NOT_EXIST);
  if (recovered) {
    LAYOUT_CHECK(recovered_schema.get_schema_version() == 11);
    LAYOUT_CALL(read_equal(seed, layout_id, small));
  } else {
    LAYOUT_CALL(StorageSchemaHistory(store, seed).create(layout_id, large));
  }
  LAYOUT_CALL(store.commit(seed));

  if (!recovered) {
    Tx old_reader, writer;
    LAYOUT_CALL(store.begin(old_reader, deadline(), true));
    LAYOUT_CALL(read_equal(old_reader, layout_id, large));
    InstanceMetaStore::SnapshotHandle retained;
    LAYOUT_CALL(store.retain_snapshot(old_reader, retained));
    LAYOUT_CALL(store.commit(old_reader));
    LAYOUT_CALL(store.begin(writer, deadline()));
    LAYOUT_CALL(StorageSchemaHistory(store, writer).publish(layout_id, small));
    LAYOUT_CALL(store.commit(writer));
    LAYOUT_CALL(store.begin_read(old_reader, deadline(), [&](SCN &snapshot) {
      snapshot = retained->version(); return OB_SUCCESS;
    }));
    LAYOUT_CALL(read_equal(old_reader, layout_id, large));

    // Dump both generations and ask minor merge to retain the large old value,
    // including chunks that the new smaller value has deleted.
    ObLS *ls = nullptr;
    LAYOUT_CALL(share::server_service<ObLSService>()->get_ls(ls));
    const ObTabletID tablet_id(ObTabletID::LS_STORAGE_SCHEMA_TABLET_ID);
    for (int i = 0; i < 3; ++i) {
      Tx marker;
      LAYOUT_CALL(store.begin(marker, deadline()));
      const auto collection = static_cast<MetaCollection>(10041);
      LAYOUT_CALL(store.put(marker, collection, ObString::make_string("flush"),
          ObString(sizeof(i), reinterpret_cast<const char *>(&i))));
      LAYOUT_CALL(store.commit(marker));
      ObTabletHandle handle;
      LAYOUT_CALL(ls->get_tablet(tablet_id, handle));
      const SCN before = handle.get_obj()->get_tablet_meta().clog_checkpoint_scn_;
      LAYOUT_CALL(ls->tablet_freeze(tablet_id, true, deadline(), false, ObFreezeSourceFlag::TEST_MODE));
      bool dumped = false;
      const int64_t limit = ObTimeUtility::current_time() + 60000000;
      while (!dumped && ObTimeUtility::current_time() < limit) {
        handle.reset();
        LAYOUT_CALL(ls->get_tablet(tablet_id, handle));
        dumped = handle.get_obj()->get_tablet_meta().clog_checkpoint_scn_ > before
            && handle.get_obj()->get_minor_table_count() > 0;
        if (!dumped) { usleep(100000); }
      }
      LAYOUT_CHECK(dumped);
    }
    bool compacted = false;
    const int64_t minor_deadline = ObTimeUtility::current_time() + 60000000;
    while (!compacted && ObTimeUtility::current_time() < minor_deadline) {
      ObTabletHandle handle;
      LAYOUT_CALL(ls->get_tablet(tablet_id, handle));
      compacted = handle.get_obj()->get_minor_table_count() < 3;
      if (!compacted) { usleep(100000); }
    }
    LAYOUT_CHECK(compacted);
    LAYOUT_CALL(read_equal(old_reader, layout_id, large));
    LAYOUT_CALL(store.commit(old_reader));
    retained.reset();

    // Layout rows and actual SQL metadata must share commit and rollback.
    const uint64_t mapping_id = 900000099;
    auto mapping_exists = [&](bool &found) -> int {
      ObISQLClient::ReadResult result;
      ObSqlString query;
      int rc = query.append_fmt("SELECT tablet_id FROM oceanbase.__all_tablet_to_table WHERE tablet_id=%lu", mapping_id);
      if (rc == OB_SUCCESS) { rc = directory_sql_proxy()->read(result, query.ptr()); }
      if (rc == OB_SUCCESS) { rc = result.get_result()->next(); }
      found = rc == OB_SUCCESS;
      return rc == OB_ITER_END ? OB_SUCCESS : rc;
    };
    for (bool commit_sql : {false, true}) {
      Tx borrowed;
      ObMySQLTransaction sql;
      struct SqlCleanup {
        InstanceMetaStore &store; Tx &borrowed; ObMySQLTransaction &sql;
        ~SqlCleanup() {
          if (sql.is_started()) { sql.end(false); }
          if (borrowed.is_active()) { store.detach(borrowed); }
        }
      } cleanup{store, borrowed, sql};
      LAYOUT_CALL(sql.start(directory_sql_proxy()));
      LAYOUT_CALL(query::ObInnerSQLConnectionAccess::with_native_transaction(sql.get_connection(),
          [&](transaction::ObTxDesc &native) {
        int rc = store.attach(borrowed, native, deadline());
        if (rc == OB_SUCCESS) { rc = StorageSchemaHistory(store, borrowed).create(layout_id + 1, large); }
        return rc;
      }));
      ObArray<share::ObTabletTablePair> pairs;
      LAYOUT_CALL(pairs.push_back(share::ObTabletTablePair(ObTabletID(mapping_id), 900000002)));
      LAYOUT_CALL(share::ObTabletMappingTableOperator::batch_update(sql, pairs));
      LAYOUT_CALL(sql.end(commit_sql));
      LAYOUT_CALL(store.detach(borrowed));
      Tx check;
      LAYOUT_CALL(store.begin(check, deadline(), true));
      ret = read_equal(check, layout_id + 1, large);
      LAYOUT_CHECK(commit_sql ? ret == OB_SUCCESS : ret == OB_ENTRY_NOT_EXIST);
      LAYOUT_CALL(store.commit(check));
      bool mapped = false;
      LAYOUT_CALL(mapping_exists(mapped));
      LAYOUT_CHECK(mapped == commit_sql);
    }
    ObSqlString erase_mapping;
    int64_t affected = 0;
    LAYOUT_CALL(erase_mapping.append_fmt("DELETE FROM oceanbase.__all_tablet_to_table WHERE tablet_id=%lu", mapping_id));
    LAYOUT_CALL(directory_sql_proxy()->write(erase_mapping.ptr(), affected));
    LAYOUT_CHECK(affected == 1);
  }
  Tx shared_recovery;
  LAYOUT_CALL(store.begin(shared_recovery, deadline(), true));
  LAYOUT_CALL(read_equal(shared_recovery, layout_id + 1, large));
  LAYOUT_CALL(store.commit(shared_recovery));

  // Failed/aborted publication must leave the entire old layout intact.
  Tx aborted;
  LAYOUT_CALL(store.begin(aborted, deadline()));
  LAYOUT_CHECK(StorageSchemaHistory(store, aborted).publish(layout_id, large) == OB_STATE_NOT_MATCH);
  LAYOUT_CALL(store.rollback(aborted));
  LAYOUT_CALL(store.begin(aborted, deadline()));
  LAYOUT_CALL(StorageSchemaHistory(store, aborted).publish(layout_id, newer));
  LAYOUT_CALL(read_equal(aborted, layout_id, newer));
  LAYOUT_CALL(store.rollback(aborted));
  Tx reader;
  LAYOUT_CALL(store.begin(reader, deadline(), true));
  LAYOUT_CALL(read_equal(reader, layout_id, small));
  LAYOUT_CALL(store.commit(reader));

  // C is commit visibility, not inherited S or the commit log SCN. Preserve it
  // through delete, assignment, and the native MDS persistence codec.
  ObTabletCreateDeleteMdsUserData status(ObTabletStatus::NORMAL,
      ObTabletMdsUserDataType::PROTOTYPE_MATERIALIZE_TABLET, 100), copied, restored;
  status.create_transaction_id_ = 456;
  SCN commit, log, removed;
  LAYOUT_CALL(commit.convert_for_tx(200));
  LAYOUT_CALL(log.convert_for_tx(210));
  LAYOUT_CALL(removed.convert_for_tx(300));
  status.on_commit(commit, log);
  LAYOUT_CHECK(status.create_commit_version_ == 100 && status.physical_create_version_ == 200);
  LAYOUT_CALL(copied.assign(status));
  copied.tablet_status_ = ObTabletStatus::DELETED;
  copied.data_type_ = ObTabletMdsUserDataType::REMOVE_TABLET;
  copied.on_commit(removed, removed);
  std::string bytes(copied.get_serialize_size(), '\0');
  int64_t pos = 0;
  LAYOUT_CALL(copied.serialize(&bytes[0], bytes.size(), pos));
  pos = 0;
  LAYOUT_CALL(restored.deserialize(bytes.data(), bytes.size(), pos));
  LAYOUT_CHECK(pos == bytes.size() && restored.create_commit_version_ == 100
      && restored.physical_create_version_ == 200 && restored.create_transaction_id_ == 456
      && restored.delete_commit_version_ == 300);
  status.data_type_ = ObTabletMdsUserDataType::CREATE_TABLET;
  status.on_commit(commit, log);
  LAYOUT_CHECK(status.create_commit_version_ == 200 && status.physical_create_version_ == 200);
  restored.reset();
  LAYOUT_CHECK(restored.physical_create_version_ == transaction::ObTransVersion::INVALID_TRANS_VERSION);
  fprintf(stderr, "LAYOUT_HISTORY_PASS recovered=%d large_bytes=%ld shrink=1 rollback=1 snapshot=1 minor=1 shared_sql_tx=1 physical_birth_codec=1\n",
      recovered, large.get_serialize_size());
#undef LAYOUT_CALL
#undef LAYOUT_CHECK
  return OB_SUCCESS;
}
