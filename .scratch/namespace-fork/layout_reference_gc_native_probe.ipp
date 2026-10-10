// Test-only command runner. All state is confined to a disposable process.
// Real SQL creates/deletes the subject; these handles exercise actual T3M lifetimes.
void run_layout_reference_gc_command()
{
  const char *control = getenv("SEEKDB_LAYOUT_REFERENCE_GC_CONTROL");
  std::string process_control;
  if (const char *directory = getenv("SEEKDB_LAYOUT_REFERENCE_GC_CONTROL_DIR")) {
    process_control = std::string(directory) + "/" + std::to_string(getpid());
    control = process_control.c_str();
  }
  if (control == nullptr) { return; }
  FILE *request = fopen(control, "r");
  if (request == nullptr) { return; }
  int64_t sequence = 0, wanted_version = 0;
  uint64_t physical_id = 0, wanted_layout = 0, wanted_orphan = 0;
  char action[32] = {};
  const int fields = fscanf(request, "%ld %31s %lu %lu %ld %lu", &sequence, action,
      &physical_id, &wanted_layout, &wanted_version, &wanted_orphan);
  fclose(request);
  static int64_t completed_sequence = 0;
  if (fields < 5 || sequence <= completed_sequence) { return; }
  completed_sequence = sequence;
  struct Held {
    ObArenaAllocator a{ObMemAttr("LayoutGcTest")}, b{ObMemAttr("LayoutGcTest")};
    ObTabletHandle old, current, first, second;
    uint64_t id = 0, layout = 0, orphan = 0;
    int64_t version = 0;
  };
  static Held *held = nullptr;
  if (held == nullptr) { held = new Held(); }
  auto &state = *held;
  if (wanted_orphan != 0) { state.orphan = wanted_orphan; }
  auto &manager = *share::server_service<ObStorageMetaMemMgr>();
  auto &store = share::server_service<ObAccessService>()->storage_schema_store();
  auto deadline = [] { return ObTimeUtility::current_time() + 120000000; };
  const int64_t old_timeout = THIS_WORKER.get_timeout_ts();
  THIS_WORKER.set_timeout_ts(deadline());
  ObLS *ls = nullptr;
  int64_t seen_old = 0, seen_current = 0, seen_a = 0, seen_b = 0, matching = 0;
  int64_t capture_us = 0, capture_attempts = 0, capture_total_us = 0;
  int64_t foreign_files = 0;
  int64_t checkpoint = 0, minor_files = 0, retention = 0;
  bool retained = false;
  int body_rc = OB_ERR_UNEXPECTED, head_rc = OB_ERR_UNEXPECTED, orphan_rc = OB_ERR_UNEXPECTED;
  bool mapped = false;
  const int ret = [&]() -> int {
    int rc = OB_SUCCESS;
#define REF_GC_CALL(expr) do { rc = (expr); if (rc != OB_SUCCESS) { \
  fprintf(stderr, "LAYOUT_REFERENCE_GC_FAIL seq=%ld line=%d ret=%d expr=%s\n", sequence, __LINE__, rc, #expr); return rc; } } while (0)
#define REF_GC_CHECK(expr) do { if (!(expr)) { \
  fprintf(stderr, "LAYOUT_REFERENCE_GC_FAIL seq=%ld line=%d check=%s\n", sequence, __LINE__, #expr); return OB_ERR_UNEXPECTED; } } while (0)
    REF_GC_CALL(share::server_service<ObLSService>()->get_ls(ls));
    if (strcmp(action, "hold") == 0 || strcmp(action, "hold_external") == 0) {
      REF_GC_CHECK(!state.old.is_valid() && !state.first.is_valid() && !state.second.is_valid());
      state.id = physical_id;
      const ObTabletMapKey key{ObTabletID(state.id)};
      REF_GC_CALL(manager.get_tablet(WashTabletPriority::WTP_LOW, key, state.old));
      REF_GC_CHECK(!state.old.get_obj()->is_external_tablet());
      state.layout = state.old.get_obj()->get_tablet_meta().storage_layout_id_;
      REF_GC_CALL(state.old.get_obj()->get_schema_version_from_storage_schema(state.version));
      if (strcmp(action, "hold") == 0) {
        REF_GC_CALL(ls->get_tablet_svr()->update_tablet_snapshot_version(ObTabletID(state.id),
            state.old.get_obj()->get_snapshot_version()));
        REF_GC_CALL(manager.get_tablet(WashTabletPriority::WTP_LOW, key, state.current));
        REF_GC_CHECK(state.old.get_obj() != state.current.get_obj());
      }
      REF_GC_CALL(manager.get_tablet_with_allocator(WashTabletPriority::WTP_LOW, key, state.a, state.first, true));
      REF_GC_CALL(manager.get_tablet_with_allocator(WashTabletPriority::WTP_LOW, key, state.b, state.second, true));
      if (strcmp(action, "hold_external") == 0) { state.old.reset(); }
    } else if (strcmp(action, "describe") == 0) {
      ObTabletHandle subject;
      state.id = physical_id;
      REF_GC_CALL(manager.get_tablet(WashTabletPriority::WTP_LOW, ObTabletMapKey(ObTabletID(state.id)), subject));
      state.layout = subject.get_obj()->get_tablet_meta().storage_layout_id_;
      REF_GC_CALL(subject.get_obj()->get_schema_version_from_storage_schema(state.version));
    } else if (strcmp(action, "release_pool") == 0) {
      state.old.reset(); state.current.reset();
    } else if (strcmp(action, "release_a") == 0) {
      state.first.reset(); state.a.reset();
    } else if (strcmp(action, "release_b") == 0) {
      state.second.reset(); state.b.reset();
    } else if (strcmp(action, "clear") == 0) {
      delete held; held = nullptr;
      return OB_SUCCESS;
    } else if ((strcmp(action, "inspect") == 0 || strcmp(action, "inspect_files") == 0) && state.id == 0) {
      state.id = physical_id; state.layout = wanted_layout; state.version = wanted_version;
    } else if (strcmp(action, "orphan") == 0) {
      // Deletion of this unrelated sentinel proves the real minor filter ran.
      state.orphan = 990000000 + sequence;
      ObArenaAllocator memory(ObMemAttr("LayoutGcTest"));
      ObTableSchema logical(&memory);
      ObStorageSchema layout;
      REF_GC_CALL(InstanceMetaStore::build_schema(ObTabletID(990000000), logical));
      logical.set_schema_version(10);
      REF_GC_CALL(layout.init(memory, logical, false));
      InstanceMetaStore::Transaction tx;
      REF_GC_CALL(store.begin(tx, deadline()));
      REF_GC_CALL(StorageSchemaHistory(store, tx).create(state.orphan, layout));
      REF_GC_CALL(store.commit(tx));
    } else if (strcmp(action, "cycle") == 0 || strcmp(action, "advance") == 0) {
      // Advance the existing durable fence; do not bypass the merge policy,
      // coherent reference capture, local restore gate, or actual minor DAG.
      ObMySQLTransaction sql;
      SCN previous, now;
      int64_t affected = 0;
      REF_GC_CALL(sql.start(directory_sql_proxy()));
      REF_GC_CALL(ObGlobalStatProxy::select_snapshot_gc_scn_for_update(sql, previous));
      REF_GC_CALL(share::server_service<transaction::ObTransService>()->get_read_snapshot_version(deadline(), now));
      REF_GC_CALL(ObGlobalStatProxy::update_snapshot_gc_scn(sql, now, affected));
      REF_GC_CALL(sql.end(true));
      const int64_t until = deadline();
      do {
        rc = share::server_service<ObFreezeInfoMgr>()->reload_for_test();
        if (rc == OB_EAGAIN) { usleep(100000); }
      } while (rc == OB_EAGAIN && ObTimeUtility::current_time() < until);
      REF_GC_CALL(rc);
      SCN active;
      do {
        REF_GC_CALL(store.min_retained_snapshot(active));
        if (active < now) { usleep(100000); }
      } while (active < now && ObTimeUtility::current_time() < until);
      REF_GC_CHECK(active >= now);
      const ObTabletID schema_id(ObTabletID::LS_STORAGE_SCHEMA_TABLET_ID);
      for (int i = 0; strcmp(action, "cycle") == 0 && i < 3; ++i) {
        InstanceMetaStore::Transaction tx;
        REF_GC_CALL(store.begin(tx, deadline()));
        const int64_t marker = ObTimeUtility::current_time();
        REF_GC_CALL(store.put(tx, static_cast<MetaCollection>(10043), ObString::make_string("flush"),
            ObString(sizeof(marker), reinterpret_cast<const char *>(&marker))));
        REF_GC_CALL(store.commit(tx));
        ObTabletHandle handle;
        REF_GC_CALL(ls->get_tablet(schema_id, handle));
        const SCN before = handle.get_obj()->get_tablet_meta().clog_checkpoint_scn_;
        REF_GC_CALL(ls->tablet_freeze(schema_id, true, deadline(), false, ObFreezeSourceFlag::TEST_MODE));
        bool dumped = false;
        const int64_t limit = deadline();
        while (!dumped && ObTimeUtility::current_time() < limit) {
          handle.reset();
          REF_GC_CALL(ls->get_tablet(schema_id, handle));
          dumped = handle.get_obj()->get_tablet_meta().clog_checkpoint_scn_ > before;
          if (!dumped) { usleep(100000); }
        }
        REF_GC_CHECK(dumped);
      }
      bool compacted = false;
      while (strcmp(action, "cycle") == 0 && !compacted && ObTimeUtility::current_time() < until) {
        ObTabletHandle handle;
        REF_GC_CALL(ls->get_tablet(schema_id, handle));
        compacted = handle.get_obj()->get_minor_table_count() < 3;
        if (!compacted) { usleep(100000); }
      }
      REF_GC_CHECK(strcmp(action, "advance") == 0 || compacted);
    } else if (strcmp(action, "flush") == 0) {
      // Read-only replica action: no SQL/KV writes or forced completion state.
      // Primary publications supply replayed rows. The driver observes actual
      // local minor deletion, including retries when publication overlaps capture.
      const int64_t until = deadline();
      do {
        rc = share::server_service<ObFreezeInfoMgr>()->reload_for_test();
        if (rc == OB_EAGAIN) { usleep(100000); }
      } while (rc == OB_EAGAIN && ObTimeUtility::current_time() < until);
      REF_GC_CALL(rc);
      REF_GC_CALL(ls->tablet_freeze(ObTabletID(ObTabletID::LS_STORAGE_SCHEMA_TABLET_ID),
          true, deadline(), false, ObFreezeSourceFlag::TEST_MODE));
    } else if (strcmp(action, "inspect") != 0 && strcmp(action, "inspect_files") != 0) {
      return OB_INVALID_ARGUMENT;
    }
    REF_GC_CHECK(state.id != 0 && state.layout != 0);
    REF_GC_CALL(manager.has_tablet(ObTabletMapKey(ObTabletID(state.id)), mapped));
    const char *capture_budget = getenv("SEEKDB_LAYOUT_REFERENCE_GC_CAPTURE_US");
    const int64_t capture_until = capture_budget == nullptr ? deadline()
        : ObTimeUtility::current_time() + strtoll(capture_budget, nullptr, 10);
    do {
      std::set<const ObTablet *> visited;
      seen_old = seen_current = seen_a = seen_b = matching = 0;
      foreign_files = 0;
      rc = manager.scan_tablet_references([&](const ObTablet &tablet) {
        if (!visited.insert(&tablet).second) { return OB_SUCCESS; }
        seen_old += &tablet == state.old.get_obj();
        seen_current += &tablet == state.current.get_obj();
        seen_a += &tablet == state.first.get_obj();
        seen_b += &tablet == state.second.get_obj();
        matching += !tablet.is_empty_shell() && tablet.get_tablet_meta().storage_layout_id_ == state.layout;
        if (strcmp(action, "inspect_files") == 0 && !tablet.is_empty_shell() && !tablet.is_ls_inner_tablet()
            && tablet.get_tablet_meta().storage_layout_id_ != state.layout) {
          ObTabletMemberWrapper<ObTabletTableStore> tables;
          ObTableStoreIterator files;
          int visit_rc = tablet.fetch_table_store(tables);
          if (visit_rc == OB_SUCCESS) { visit_rc = tables.get_member()->get_all_sstable(files); }
          ObITable *file = nullptr;
          while (visit_rc == OB_SUCCESS) {
            visit_rc = files.get_next(file);
            if (visit_rc == OB_ITER_END) { return OB_SUCCESS; }
            if (visit_rc == OB_SUCCESS && file != nullptr && !file->is_mds_sstable()) {
              blocksstable::ObSSTableMetaHandle meta;
              visit_rc = static_cast<blocksstable::ObSSTable *>(file)->get_meta(meta);
              if (visit_rc == OB_SUCCESS) {
                const auto &definition = meta.get_sstable_meta().get_basic_meta();
                foreign_files += definition.storage_layout_id_ == state.layout
                    && definition.schema_version_ == state.version;
              }
            }
          }
          return visit_rc;
        }
        return OB_SUCCESS;
      }, capture_until);
      if (rc == OB_EAGAIN) { usleep(10000); }
    } while (rc == OB_EAGAIN && ObTimeUtility::current_time() < capture_until);
    REF_GC_CALL(rc);
    ObArray<StorageSchemaHistory::PhysicalReference> refs;
    const int64_t collection_started = ObTimeUtility::current_time();
    do {
      const int64_t started = ObTimeUtility::current_time();
      rc = StorageSchemaHistory::collect_physical_references(refs, capture_until);
      capture_us = ObTimeUtility::current_time() - started;
      ++capture_attempts;
      if (rc == OB_EAGAIN) { usleep(10000); }
    } while (rc == OB_EAGAIN && ObTimeUtility::current_time() < capture_until);
    capture_total_us = ObTimeUtility::current_time() - collection_started;
    REF_GC_CALL(rc);
    for (int64_t i = 0; i < refs.count(); ++i) {
      retained |= refs.at(i).layout_id == state.layout && refs.at(i).minimum_version <= state.version;
    }
    if (state.old.is_valid() || state.first.is_valid() || state.second.is_valid()) { REF_GC_CHECK(retained); }
    ObArenaAllocator memory(ObMemAttr("LayoutGcTest"));
    ObStorageSchema body;
    InstanceMetaStore::Transaction tx;
    REF_GC_CALL(store.begin(tx, deadline(), true));
    int64_t version = 0;
    StorageSchemaHistory history(store, tx);
    body_rc = history.read_published(state.layout, state.version, memory, body);
    head_rc = history.read_version(state.layout, version);
    if (state.orphan != 0) { orphan_rc = history.read_version(state.orphan, version); }
    REF_GC_CALL(store.commit(tx));
    ObTabletHandle schema_tablet;
    REF_GC_CALL(ls->get_tablet(ObTabletID(ObTabletID::LS_STORAGE_SCHEMA_TABLET_ID), schema_tablet));
    checkpoint = schema_tablet.get_obj()->get_tablet_meta().clog_checkpoint_scn_.get_val_for_tx();
    minor_files = schema_tablet.get_obj()->get_minor_table_count();
    SCN bound;
    REF_GC_CALL(share::server_service<ObFreezeInfoMgr>()->get_schema_history_retention(bound));
    retention = bound.get_val_for_tx();
    return OB_SUCCESS;
#undef REF_GC_CALL
#undef REF_GC_CHECK
  }();
  THIS_WORKER.set_timeout_ts(old_timeout);
  const std::string response = std::string(control) + ".result";
  FILE *result = fopen(response.c_str(), "w");
  if (result != nullptr) {
    fprintf(result, "seq=%ld ret=%d G=%lu V=%ld mapped=%d old=%ld current=%ld a=%ld b=%ld matching=%ld body=%d head=%d orphan=%d capture_us=%ld attempts=%ld retained=%d foreign=%ld capture_total_us=%ld orphan_id=%lu checkpoint=%ld minor_files=%ld retention=%ld\n",
        sequence, ret, held == nullptr ? 0 : state.layout, held == nullptr ? 0 : state.version,
        mapped, seen_old, seen_current, seen_a, seen_b, matching, body_rc, head_rc, orphan_rc, capture_us, capture_attempts,
        retained, foreign_files, capture_total_us, held == nullptr ? 0 : state.orphan,
        checkpoint, minor_files, retention);
    fclose(result);
  }
}
