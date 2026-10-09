// Native integration probe, included only in disposable test binaries.
static thread_local std::function<int()> layout_retention_sample_hook;
int run_layout_retention_sample_hook()
{
  return layout_retention_sample_hook ? layout_retention_sample_hook() : OB_SUCCESS;
}

static int run_layout_retention_native_probe()
{
  using Tx = InstanceMetaStore::Transaction;
  auto &store = share::server_service<ObAccessService>()->storage_schema_store();
  auto &freezes = *share::server_service<ObFreezeInfoMgr>();
  auto &transactions = *share::server_service<transaction::ObTransService>();
  auto &scheduler = *compaction::ObBasicMergeScheduler::get_merge_scheduler();
  auto &proxy = *directory_sql_proxy();
  auto deadline = [] { return ObTimeUtility::current_time() + 120000000; };
  const uint64_t layout_id = 900000101;
  const auto collection = static_cast<MetaCollection>(10042);
  const auto key = ObString::make_string("freeze");
  ObArenaAllocator allocator(ObMemAttr("LayoutRetention"));
  int ret = OB_SUCCESS;
#define RETENTION_CALL(expr) do { ret = (expr); if (ret != OB_SUCCESS) { \
  fprintf(stderr, "LAYOUT_RETENTION_FAIL line=%d ret=%d expr=%s\n", __LINE__, ret, #expr); return ret; } } while (0)
#define RETENTION_CHECK(expr) do { if (!(expr)) { \
  fprintf(stderr, "LAYOUT_RETENTION_FAIL line=%d ret=%d check=%s\n", __LINE__, ret, #expr); return OB_ERR_UNEXPECTED; } } while (0)
  // This probe controls the persisted completion input. It does not pretend
  // that advancing this input tests the still-separate checksum implementation.
  ObGlobalMergeInfo progress;
  RETENTION_CALL(ObGlobalMergeTableOperator::load_global_merge_info(proxy, progress));
  progress.suspend_merging_.set_val(true, true);
  RETENTION_CALL(ObGlobalMergeTableOperator::update_partial_global_merge_info(proxy, progress));
  scheduler.stop_major_merge();
  scheduler.set_inner_table_merged_scn(progress.last_merged_scn().get_val_for_tx());

  Tx state;
  RETENTION_CALL(store.begin(state, deadline(), true));
  ObString saved;
  ret = store.get(state, collection, key, allocator, saved);
  const bool recovered = ret == OB_SUCCESS;
  RETENTION_CHECK(recovered || ret == OB_ENTRY_NOT_EXIST);
  SCN frozen;
  if (recovered) {
    RETENTION_CHECK(saved.length() == sizeof(int64_t));
    int64_t value = 0;
    MEMCPY(&value, saved.ptr(), sizeof(value));
    RETENTION_CALL(frozen.convert_for_tx(value));
  }
  RETENTION_CALL(store.commit(state));
  auto publish = [&](int64_t version) -> int {
    ObTableSchema logical(&allocator);
    ObStorageSchema layout;
    int rc = InstanceMetaStore::build_schema(ObTabletID(900000102), logical);
    logical.set_schema_version(version);
    if (rc == OB_SUCCESS) { rc = layout.init(allocator, logical, false); }
    Tx tx;
    if (rc == OB_SUCCESS) { rc = store.begin(tx, deadline()); }
    if (rc == OB_SUCCESS) {
      StorageSchemaHistory history(store, tx);
      rc = version == 10 ? history.create(layout_id, layout) : history.publish(layout_id, layout);
    }
    if (tx.is_active()) {
      const int end = rc == OB_SUCCESS ? store.commit(tx) : store.rollback(tx);
      if (rc == OB_SUCCESS) { rc = end; }
    }
    return rc;
  };
  if (!recovered) {
    RETENTION_CALL(publish(10));
    ObMySQLTransaction sql;
    Tx reader;
    RETENTION_CALL(sql.start(&proxy));
    SCN fence;
    RETENTION_CALL(ObGlobalStatProxy::select_snapshot_gc_scn_for_update(sql, fence));
    RETENTION_CALL(store.begin_read(reader, deadline(), [&](SCN &snapshot) {
      return transactions.get_read_snapshot_version(deadline(), snapshot);
    }));
    frozen = reader.snapshot_version();
    RETENTION_CHECK(frozen > fence);
    ObFreezeInfo info;
    info.frozen_scn_ = frozen;
    info.data_version_ = DATA_CURRENT_VERSION;
    RETENTION_CALL(ObFreezeInfoProxy().set_freeze_info(sql, info));
    RETENTION_CALL(sql.end(true));
    RETENTION_CALL(store.commit(reader));
    RETENTION_CALL(publish(11));
    Tx marker;
    RETENTION_CALL(store.begin(marker, deadline()));
    const int64_t value = frozen.get_val_for_tx();
    RETENTION_CALL(store.put(marker, collection, key,
        ObString(sizeof(value), reinterpret_cast<const char *>(&value))));
    RETENTION_CALL(store.commit(marker));
  }

  // Move the persisted GC fence beyond F. With no old active reader, only the
  // unfinished freeze should prevent metadata mini/minor from discarding F.
  {
    ObMySQLTransaction sql;
    RETENTION_CALL(sql.start(&proxy));
    SCN previous, current;
    RETENTION_CALL(ObGlobalStatProxy::select_snapshot_gc_scn_for_update(sql, previous));
    RETENTION_CALL(transactions.get_read_snapshot_version(deadline(), current));
    RETENTION_CHECK(current > frozen && current >= previous);
    int64_t affected = 0;
    RETENTION_CALL(ObGlobalStatProxy::update_snapshot_gc_scn(sql, current, affected));
    RETENTION_CALL(sql.end(true));
  }
  // Allow a concurrent ordinary reload's immutable plan to finish first.
  const int64_t reload_deadline = deadline();
  do {
    ret = freezes.reload_for_test();
    if (ret == OB_EAGAIN) { usleep(100000); }
  } while (ret == OB_EAGAIN && ObTimeUtility::current_time() < reload_deadline);
  RETENTION_CHECK(ret == OB_SUCCESS);
  SCN retained, active;
  RETENTION_CALL(freezes.get_schema_history_retention(retained));
  RETENTION_CHECK(retained == frozen);
  const int64_t weak_deadline = deadline();
  do {
    RETENTION_CALL(store.min_retained_snapshot(active));
    if (active <= frozen) { usleep(100000); }
  } while (active <= frozen && ObTimeUtility::current_time() < weak_deadline);
  RETENTION_CHECK(active > frozen);

  ObLS *ls = nullptr;
  RETENTION_CALL(share::server_service<ObLSService>()->get_ls(ls));
  const ObTabletID tablet_id(ObTabletID::LS_STORAGE_SCHEMA_TABLET_ID);
  auto dump_and_minor = [&]() -> int {
    for (int i = 0; i < 3; ++i) {
      Tx tx;
      RETENTION_CALL(store.begin(tx, deadline()));
      const int64_t marker = ObTimeUtility::current_time();
      RETENTION_CALL(store.put(tx, collection, ObString::make_string("flush"),
          ObString(sizeof(marker), reinterpret_cast<const char *>(&marker))));
      RETENTION_CALL(store.commit(tx));
      ObTabletHandle handle;
      RETENTION_CALL(ls->get_tablet(tablet_id, handle));
      const SCN before = handle.get_obj()->get_tablet_meta().clog_checkpoint_scn_;
      RETENTION_CALL(ls->tablet_freeze(tablet_id, true, deadline(), false, ObFreezeSourceFlag::TEST_MODE));
      bool dumped = false;
      const int64_t limit = deadline();
      while (!dumped && ObTimeUtility::current_time() < limit) {
        handle.reset();
        RETENTION_CALL(ls->get_tablet(tablet_id, handle));
        dumped = handle.get_obj()->get_tablet_meta().clog_checkpoint_scn_ > before;
        if (!dumped) { usleep(100000); }
      }
      RETENTION_CHECK(dumped);
    }
    bool compacted = false;
    const int64_t limit = deadline();
    while (!compacted && ObTimeUtility::current_time() < limit) {
      ObTabletHandle handle;
      RETENTION_CALL(ls->get_tablet(tablet_id, handle));
      compacted = handle.get_obj()->get_minor_table_count() < 3;
      if (!compacted) { usleep(100000); }
    }
    RETENTION_CHECK(compacted);
    return OB_SUCCESS;
  };
  RETENTION_CALL(dump_and_minor());
  {
    ObStorageSchema layout;
    RETENTION_CALL(StorageSchemaHistory::read_at(store, layout_id, frozen, deadline(), allocator, layout));
    RETENTION_CHECK(layout.get_schema_version() == 10);
    Tx latest;
    RETENTION_CALL(store.begin(latest, deadline(), true));
    int64_t version = 0;
    RETENTION_CALL(StorageSchemaHistory(store, latest).read_version(layout_id, version));
    RETENTION_CHECK(version == 11);
    RETENTION_CALL(store.commit(latest));
    ObTabletHandle handle;
    RETENTION_CALL(ls->get_tablet(tablet_id, handle));
    RETENTION_CHECK(handle.get_obj()->get_multi_version_start() <= frozen.get_val_for_tx());
  }
  if (recovered) {
    // Deterministically interleave a reader and completion between the
    // policy's two samples. This must never permit reclaiming that reader's F.
    Tx late_reader;
    bool interleaved = false;
    layout_retention_sample_hook = [&]() -> int {
      RETENTION_CALL(store.begin_read(late_reader, deadline(), [&](SCN &snapshot) {
        SCN bound;
        int rc = freezes.get_schema_history_retention(bound);
        if (rc == OB_SUCCESS && frozen < bound) { rc = OB_SNAPSHOT_DISCARDED; }
        if (rc == OB_SUCCESS) { snapshot = frozen; }
        return rc;
      }));
      progress.last_merged_scn_.set_scn(frozen, true);
      progress.global_broadcast_scn_.set_scn(frozen, true);
      progress.frozen_scn_.set_scn(frozen, true);
      RETENTION_CALL(ObGlobalMergeTableOperator::update_partial_global_merge_info(proxy, progress));
      scheduler.set_inner_table_merged_scn(frozen.get_val_for_tx());
      RETENTION_CALL(freezes.reload_for_test());
      interleaved = true;
      return OB_SUCCESS;
    };
    ObTabletHandle before_release;
    RETENTION_CALL(ls->get_tablet(tablet_id, before_release));
    ObVersionRange range;
    SCN fresh;
    RETENTION_CALL(transactions.get_read_snapshot_version(deadline(), fresh));
    range.snapshot_version_ = fresh.get_val_for_tx();
    range.multi_version_start_ = before_release.get_obj()->get_multi_version_start();
    ObStorageSnapshotInfo unused;
    ret = compaction::ObPartitionMergePolicy::get_multi_version_start(
        compaction::MINOR_MERGE, *ls, *before_release.get_obj(), range, unused);
    layout_retention_sample_hook = {};
    RETENTION_CHECK(ret == OB_SUCCESS && interleaved);
    RETENTION_CHECK(range.multi_version_start_ <= frozen.get_val_for_tx());
    ObStorageSchema held_layout;
    RETENTION_CALL(StorageSchemaHistory(store, late_reader).read(layout_id, allocator, held_layout));
    RETENTION_CHECK(held_layout.get_schema_version() == 10);
    RETENTION_CALL(store.commit(late_reader));
    RETENTION_CALL(freezes.get_schema_history_retention(retained));
    RETENTION_CHECK(retained > frozen);
    RETENTION_CALL(publish(12));
    RETENTION_CALL(dump_and_minor());
    ObTabletHandle handle;
    RETENTION_CALL(ls->get_tablet(tablet_id, handle));
    RETENTION_CHECK(handle.get_obj()->get_multi_version_start() > frozen.get_val_for_tx());
    ObStorageSchema obsolete;
    RETENTION_CHECK(StorageSchemaHistory::read_at(store, layout_id, frozen, deadline(), allocator, obsolete)
        == OB_SNAPSHOT_DISCARDED);
    SCN selected;
    ObStorageSchema current;
    RETENTION_CALL(StorageSchemaHistory::read_current(store, layout_id, frozen,
        deadline(), allocator, selected, current));
    RETENTION_CHECK(selected > frozen && current.get_schema_version() == 12);
    ObStorageSchema referenced;
    RETENTION_CALL(StorageSchemaHistory::read_published(store, layout_id, 10,
        deadline(), allocator, referenced));
    RETENTION_CHECK(referenced.get_schema_version() == 10);
    fprintf(stderr, "LAYOUT_RETENTION_EXACT_BODY V=10 after_head_gc=1\n");
    fprintf(stderr, "LAYOUT_RETENTION_NEW_TARGET old=%ld selected=%ld V=%ld\n",
        frozen.get_val_for_tx(), selected.get_val_for_tx(), current.get_schema_version());
  } else {
    // Model a paused replica whose primary has already retired this freeze.
    // Its local persisted broadcast is still unfinished, even though the
    // runtime scheduler has not started that broadcast (including on restart).
    progress.global_broadcast_scn_.set_scn(frozen, true);
    progress.frozen_scn_.set_scn(frozen, true);
    RETENTION_CALL(ObGlobalMergeTableOperator::update_partial_global_merge_info(proxy, progress));
    RETENTION_CALL(ObFreezeInfoProxy().batch_delete(proxy, frozen));
    RETENTION_CHECK(scheduler.get_frozen_version() < frozen.get_val_for_tx());
    RETENTION_CALL(freezes.reload_for_test());
    RETENTION_CALL(freezes.get_schema_history_retention(retained));
    RETENTION_CHECK(retained == frozen);
  }
  fprintf(stderr, "LAYOUT_RETENTION_PASS recovered=%d F=%ld active=%ld durable=%ld mini_minor=1 released=%d handoff=%d\n",
      recovered, frozen.get_val_for_tx(), active.get_val_for_tx(), retained.get_val_for_tx(), recovered, recovered);
#undef RETENTION_CALL
#undef RETENTION_CHECK
  return OB_SUCCESS;
}
