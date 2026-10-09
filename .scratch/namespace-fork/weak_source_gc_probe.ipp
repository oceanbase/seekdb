#include "retention_probe_helpers.ipp"
// A future weak reader has no NamespaceCatalogViews registration yet.
extern thread_local int64_t weak_source_gc_probe_snapshot;
static int run_weak_source_gc_probe()
{
  auto &store = share::server_service<ObAccessService>()->instance_meta_store();
  rootserver::InstanceNamespaceDirectory directory(store);
  constexpr uint64_t owner = 64990, logical = 990091;
  std::vector<std::pair<uint64_t, int64_t>> physical_sources;
  const int selected = select_retention_probe_sources(physical_sources);
  if (selected != OB_SUCCESS) { return selected; }
  const uint64_t physical = physical_sources[0].first;
  auto deadline = [] { return ObTimeUtility::current_time() + 30000000; };
  auto write = [&](bool create) {
    InstanceMetaStore::Transaction tx;
    int rc = store.begin(tx, deadline());
    rootserver::InstanceNamespaceMetadata metadata(store, tx);
    if (rc == OB_SUCCESS && create) {
      rootserver::InstanceNamespaceRecord record;
      record.id = owner; record.name = "__weak_source_gc_probe__";
      record.roots.schema_version = 41;
      int64_t watermark = 0;
      rc = metadata.get_snapshot_gc_watermark(watermark);
      if (rc == OB_ENTRY_NOT_EXIST) { rc = metadata.initialize_snapshot_gc_watermark(0); }
      if (rc == OB_SUCCESS) { rc = metadata.insert_namespace(record); }
      ns::CatalogChanges changes;
      changes[ns::NamespaceCatalogCodec::object_key(logical)] = {
          {ns::NamespaceCatalogCodec::encode_source({990092, physical, physical_sources[0].second, logical, 0, 0}), 1}, false};
      if (rc == OB_SUCCESS) { rc = metadata.stage_catalog_delta(owner, 41, 42, {}, changes); }
    } else if (rc == OB_SUCCESS) { rc = metadata.erase_namespace(owner); }
    if (tx.is_active()) {
      const int end = rc ? store.rollback(tx) : store.commit(tx);
      if (rc == OB_SUCCESS) { rc = end; }
    }
    return rc;
  };
  int ret = write(true);
  // This lease protects KV history only, and does not register a source root.
  InstanceMetaStore::Transaction before;
  if (ret == OB_SUCCESS) { ret = store.begin(before, deadline(), true); }
  if (ret == OB_SUCCESS) { weak_source_gc_probe_snapshot = before.snapshot_version().get_val_for_tx(); }
  if (ret == OB_SUCCESS) { ret = write(false); }
  PhysicalSnapshotRetention plan;
  if (ret == OB_SUCCESS) {
    ret = load_retention_probe_plan(plan);
    if (ret == OB_SUCCESS && (plan.tablets.count(physical) != 1 || plan.tablets.at(physical).snapshot != 1)) {
      fprintf(stderr, "WEAK_SOURCE_GC_GAP entries=%zu snapshot=%ld\n", plan.tablets.size(), weak_source_gc_probe_snapshot);
      ret = OB_ERR_UNEXPECTED;
    }
  }
  // Once every new weak reader selects a post-delete root, this source is free.
  if (ret == OB_SUCCESS) {
    InstanceMetaStore::Transaction after;
    ret = store.begin(after, deadline(), true);
    if (ret == OB_SUCCESS) { weak_source_gc_probe_snapshot = after.snapshot_version().get_val_for_tx(); }
    if (ret == OB_SUCCESS) { ret = load_retention_probe_plan(plan); }
    if (ret == OB_SUCCESS && (plan.tablets.count(physical) && plan.tablets.at(physical).snapshot == 1)) { ret = OB_ERR_UNEXPECTED; }
    if (after.is_active()) { const int end = store.rollback(after); if (ret == OB_SUCCESS) { ret = end; } }
  }
  weak_source_gc_probe_snapshot = 0;
  if (before.is_active()) { const int end = store.rollback(before); if (ret == OB_SUCCESS) { ret = end; } }
  fprintf(stderr, "INSTANCE_WEAK_SOURCE_GC_%s no_view=1 weak_root=1 release=1 native_plan=1 ret=%d\n", ret ? "FAIL" : "PASS", ret);
  return ret;
}
