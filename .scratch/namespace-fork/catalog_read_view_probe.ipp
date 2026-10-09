#include "retention_probe_helpers.ipp"
#include "/home/nijia.nj/.herdr/worktrees/seekdb/herdr-fork/.scratch/namespace-fork/tablet_identity_native_probe.ipp"
// Real KV snapshots and page/physical retention, without an open user tablet.
static int run_catalog_read_view_probe()
{
  const int identity = run_tablet_identity_native_probe();
  if (identity != OB_SUCCESS) { return identity; }
  auto &store = share::server_service<ObAccessService>()->instance_meta_store();
  rootserver::InstanceNamespaceDirectory directory(store);
  constexpr uint64_t owner = 65000, logical = 990001, table = 990002;
  std::vector<std::pair<uint64_t, int64_t>> physical;
  const int selected = select_retention_probe_sources(physical);
  if (selected != OB_SUCCESS) { return selected; }
  const uint64_t old_physical = physical[0].first, next_physical = physical[1].first;
  auto deadline = [] { return ObTimeUtility::current_time() + 30000000; };
  auto changes = [&](uint64_t physical, int64_t generation) {
    ns::CatalogChanges delta;
    ns::CatalogTabletSource source{table, physical, generation, logical, 0, 0};
    delta[ns::NamespaceCatalogCodec::object_key(logical)] = {
        {ns::NamespaceCatalogCodec::encode_source(source), 1}, false};
    return delta;
  };
  auto write = [&](const std::function<int(rootserver::InstanceNamespaceMetadata &)> &f) {
    InstanceMetaStore::Transaction tx;
    int rc = store.begin(tx, deadline());
    rootserver::InstanceNamespaceMetadata metadata(store, tx);
    if (rc == OB_SUCCESS) { rc = f(metadata); }
    if (tx.is_active()) {
      const int end = rc == OB_SUCCESS ? store.commit(tx) : store.rollback(tx);
      if (rc == OB_SUCCESS) { rc = end; }
    }
    return rc;
  };
  int ret = write([&](rootserver::InstanceNamespaceMetadata &metadata) {
    rootserver::InstanceNamespaceRecord record;
    record.id = owner; record.name = "__catalog_view_probe__";
    record.roots.schema_version = 41;
    int rc = metadata.insert_namespace(record);
    return rc ? rc : metadata.stage_catalog_delta(owner, 41, 42, {}, changes(old_physical, physical[0].second));
  });
  ns::NamespaceCatalogViews::Handle view;
  if (ret == OB_SUCCESS) {
    ret = NamespaceForkKernelPrototype::acquire_read_view(owner, [&](SCN &snapshot) {
      int64_t selected = 0;
      int rc = observer::namespace_worker_prototype::acquire_storage_snapshot(selected);
      if (rc == OB_SUCCESS) { rc = snapshot.convert_for_tx(selected); }
      // Publish a newer root after the selected S but before the root read.
      // The view must still select version 42 from the native MVCC table.
      if (rc == OB_SUCCESS) {
        rc = write([&](rootserver::InstanceNamespaceMetadata &metadata) {
          return metadata.stage_catalog_delta(owner, 42, 43, {}, changes(next_physical, physical[1].second));
        });
      }
      return rc;
    }, view);
  }
  if (ret == OB_SUCCESS && (!view || view->entry().roots.schema_version != 42)) {
    fprintf(stderr, "CATALOG_VIEW_ASSERT line=%d\n", __LINE__); ret = OB_ERR_UNEXPECTED;
  }
  if (ret == OB_SUCCESS) {
    SCN retained;
    ret = store.min_retained_snapshot(retained);
    if (ret == OB_SUCCESS && retained.get_val_for_tx() > view->entry().snapshot) {
      fprintf(stderr, "CATALOG_VIEW_MVCC_GAP held=%ld retained=%ld\n",
          view->entry().snapshot, retained.get_val_for_tx());
      fprintf(stderr, "CATALOG_VIEW_ASSERT line=%d\n", __LINE__); ret = OB_ERR_UNEXPECTED;
    }
  }
  // The collector follows real native incarnations and the held immutable root.
  PhysicalSnapshotRetention plan;
  if (ret == OB_SUCCESS) { ret = load_retention_probe_plan(plan); }
  if (ret == OB_SUCCESS && (plan.tablets.count(old_physical) != 1
      || plan.tablets.at(old_physical).snapshot != 1
      || plan.tablets.at(old_physical).create_transaction_id != physical[0].second)) {
    fprintf(stderr, "CATALOG_VIEW_ASSERT native held source line=%d\n", __LINE__); ret = OB_ERR_UNEXPECTED;
  }
  uint64_t old_page = view ? view->entry().roots.directory.page : 0;
  if (ret == OB_SUCCESS) {
    ret = write([&](rootserver::InstanceNamespaceMetadata &metadata) {
      return metadata.erase_namespace(owner);
    });
  }
  // This section isolates already-held views from the new-weak-reader test.
  // Do not assume a just-committed removal is immediately visible to weak reads.
  if (ret == OB_SUCCESS) {
    auto *transactions = share::server_service<transaction::ObTransService>();
    SCN removed, weak;
    const int64_t until = deadline();
    ret = transactions->get_read_snapshot_version(until, removed);
    while (ret == OB_SUCCESS) {
      ret = transactions->get_weak_read_snapshot_version(-1, weak);
      if (ret != OB_SUCCESS || weak >= removed) { break; }
      if (ObTimeUtility::current_time() >= until) { ret = OB_TIMEOUT; break; }
      usleep(10000);
    }
  }
  // No ordinary KV transaction remains open for this view. Collection must
  // enter immediately and delete the latest row; the held snapshot protects
  // the historical page version, as it must when primary deletes are replayed.
  auto collect = [&](bool held) {
    InstanceMetaStore::Transaction tx;
    rootserver::InstanceNamespaceDirectory directory(store);
    int64_t deleted = 0;
    int rc = directory.collect_catalog_pages(deadline(), deleted);
    if (rc == OB_SUCCESS) { rc = store.begin(tx, deadline(), true); }
    rootserver::InstanceNamespaceMetadata metadata(store, tx);
    std::string page;
    if (rc == OB_SUCCESS) {
      const int read = metadata.read_page(old_page, page);
      if (read != OB_ENTRY_NOT_EXIST) {
        fprintf(stderr, "CATALOG_VIEW_ASSERT line=%d\n", __LINE__); rc = OB_ERR_UNEXPECTED;
      }
    }
    if (tx.is_active()) {
      const int end = rc == OB_SUCCESS ? store.commit(tx) : store.rollback(tx);
      if (rc == OB_SUCCESS) { rc = end; }
    }
    if (rc == OB_SUCCESS && held) {
      InstanceMetaStore::Transaction historical;
      rc = store.begin_read(historical, deadline(), [&](SCN &snapshot) {
        return snapshot.convert_for_tx(view->entry().snapshot);
      });
      rootserver::InstanceNamespaceMetadata history(store, historical);
      ns::CatalogTabletSource source;
      int64_t cap = 0;
      if (rc == OB_SUCCESS) { rc = history.find_tablet_source(view->entry().roots.directory, logical, source, cap); }
      if (rc == OB_SUCCESS && source.physical_tablet_id != old_physical) { fprintf(stderr, "CATALOG_VIEW_ASSERT line=%d\n", __LINE__); rc = OB_ERR_UNEXPECTED; }
      if (historical.is_active()) { const int end = store.rollback(historical); if (rc == OB_SUCCESS) { rc = end; } }
    }
    return rc;
  };
  if (ret == OB_SUCCESS) { ret = collect(true); }
  if (ret == OB_SUCCESS) { ret = load_retention_probe_plan(plan); }
  if (ret == OB_SUCCESS && (plan.tablets.count(old_physical) != 1
      || plan.tablets.at(old_physical).snapshot != 1
      || (plan.tablets.count(next_physical) && plan.tablets.at(next_physical).snapshot == 1))) {
    fprintf(stderr, "CATALOG_VIEW_ASSERT removed root held by view line=%d\n", __LINE__); ret = OB_ERR_UNEXPECTED;
  }
  if (ret == OB_SUCCESS) {
    ns::NamespaceCatalogViews::Handle repeated;
    ret = NamespaceForkKernelPrototype::acquire_read_view(owner, [&](SCN &snapshot) {
      return snapshot.convert_for_tx(view->entry().snapshot);
    }, repeated, view);
    if (ret == OB_SUCCESS && repeated != view) { fprintf(stderr, "CATALOG_VIEW_ASSERT line=%d\n", __LINE__); ret = OB_ERR_UNEXPECTED; }
  }
  const int64_t held_scn = view ? view->entry().snapshot : 0;
  std::vector<ns::NamespaceCatalogViews::Entry> copied;
  ns::namespace_registry().catalog_views().list(copied);
  std::weak_ptr<const void> retention = view ? view->entry().retention : std::shared_ptr<const void>{};
  view.reset();
  if (ret == OB_SUCCESS) {
    SCN retained;
    ret = store.min_retained_snapshot(retained);
    if (ret == OB_SUCCESS && retained.get_val_for_tx() > held_scn) { fprintf(stderr, "CATALOG_VIEW_ASSERT line=%d\n", __LINE__); ret = OB_ERR_UNEXPECTED; }
  }
  copied.clear();
  if (ret == OB_SUCCESS) { ret = load_retention_probe_plan(plan); }
  if (ret == OB_SUCCESS && plan.tablets.count(old_physical)
      && plan.tablets.at(old_physical).snapshot == 1) {
    fprintf(stderr, "CATALOG_VIEW_ASSERT released view retained source line=%d\n", __LINE__); ret = OB_ERR_UNEXPECTED;
  }
  if (ret == OB_SUCCESS) {
    auto *transactions = share::server_service<transaction::ObTransService>();
    SCN strong, weak, retained, weak_after;
    ret = transactions->get_read_snapshot_version(deadline(), strong);
    if (ret == OB_SUCCESS) { ret = transactions->get_weak_read_snapshot_version(-1, weak); }
    if (ret == OB_SUCCESS) { ret = store.min_retained_snapshot(retained); }
    if (ret == OB_SUCCESS) { ret = transactions->get_weak_read_snapshot_version(-1, weak_after); }
    fprintf(stderr, "CATALOG_VIEW_WATERMARK expired=%d strong=%ld weak=%ld retained=%ld weak_after=%ld ret=%d\n", retention.expired(), strong.get_val_for_tx(), weak.get_val_for_tx(), retained.get_val_for_tx(), weak_after.get_val_for_tx(), ret);
    if (ret == OB_SUCCESS && (!retention.expired() || retained > weak_after)) { fprintf(stderr, "CATALOG_VIEW_ASSERT line=%d\n", __LINE__); ret = OB_ERR_UNEXPECTED; }
  }
  // A reader is registered before its SCN is acquired. This real admission
  // window must conservatively pin MIN, independently of the released view.
  // Therefore releasing one view cannot imply a lower bound of min(strong,weak).
  if (ret == OB_SUCCESS) {
    InstanceMetaStore::Transaction admitting;
    ret = store.begin_read(admitting, deadline(), [&](SCN &selected) {
      SCN retained;
      int rc = store.min_retained_snapshot(retained);
      if (rc == OB_SUCCESS && !retained.is_min()) { rc = OB_ERR_UNEXPECTED; }
      fprintf(stderr, "CATALOG_VIEW_READER_ADMISSION minimum=%d expired=%d ret=%d\n",
          retained.is_min(), retention.expired(), rc);
      return rc ? rc : share::server_service<transaction::ObTransService>()->get_read_snapshot_version(deadline(), selected);
    });
    if (admitting.is_active()) {
      const int end = store.rollback(admitting);
      if (ret == OB_SUCCESS) { ret = end; }
    }
  }
  if (ret == OB_SUCCESS) { ret = collect(false); }
  if (ret == OB_SUCCESS) {
    ns::NamespaceCatalogViews::Handle failed;
    const int rc = NamespaceForkKernelPrototype::acquire_read_view(owner, [](SCN &) {
      return OB_TIMEOUT;
    }, failed);
    if (rc != OB_TIMEOUT || failed) { fprintf(stderr, "CATALOG_VIEW_ASSERT line=%d\n", __LINE__); ret = OB_ERR_UNEXPECTED; }
    InstanceMetaStore::Transaction gc;
    if (ret == OB_SUCCESS) { ret = store.begin_directory_gc(gc, deadline()); }
    if (gc.is_active()) { const int end = store.rollback(gc); if (ret == OB_SUCCESS) { ret = end; } }
  }
  fprintf(stderr, "INSTANCE_%s view_snapshot=1 late_open=1 page_gc_unblocked=1 deleted_page_history=1 native_physical_plan=1 retention_handoff=1 release=1 weak_horizon=1 source_graph_gc=1 ret=%d\n",
      ret == OB_SUCCESS ? "CATALOG_VIEW_PASS" : "META_PROBE_FAIL", ret);
  return ret;
}
