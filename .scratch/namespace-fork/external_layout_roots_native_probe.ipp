// Disposable native test: exercise actual external-tablet load/destruction.
static int run_external_layout_roots_native_probe()
{
  int ret = OB_SUCCESS;
  auto *manager = share::server_service<ObStorageMetaMemMgr>();
  ObLS *ls = nullptr;
  ObLSTabletIterator iter(ObMDSGetTabletMode::READ_WITHOUT_CHECK);
  ObTabletHandle original;
  if (OB_ISNULL(manager)) { return OB_NOT_INIT; }
  if (OB_FAIL(share::server_service<ObLSService>()->get_ls(ls))) { return ret; }
  if (OB_FAIL(ls->build_tablet_iter(iter))) { return ret; }
  while (OB_SUCC(ret)) {
    original.reset();
    if (OB_FAIL(iter.get_next_tablet(original))) { return ret; }
    const auto &tablet = *original.get_obj();
    if (!tablet.is_empty_shell() && !tablet.is_ls_inner_tablet()
        && tablet.get_tablet_meta().storage_layout_id_ != 0) { break; }
  }
  const auto id = original.get_obj()->get_tablet_id();
  const ObTabletMapKey key(id);
  // Retain an actual pool object across a native persisted replacement.
  if (OB_FAIL(manager->get_tablet(WashTabletPriority::WTP_LOW, key, original))) { return ret; }
  const ObTablet *old = original.get_obj();
  if (old->is_external_tablet()) { return OB_ERR_UNEXPECTED; }
  if (OB_FAIL(ls->get_tablet_svr()->update_tablet_snapshot_version(id,
      old->get_snapshot_version()))) { return ret; }
  ObTabletHandle replacement;
  if (OB_FAIL(manager->get_tablet(WashTabletPriority::WTP_LOW, key, replacement))) { return ret; }
  if (replacement.get_obj() == old) { return OB_ERR_UNEXPECTED; }
  ObArenaAllocator first_allocator(ObMemAttr("LayoutRootTest"));
  ObArenaAllocator second_allocator(ObMemAttr("LayoutRootTest"));
  ObTabletHandle first, second;
  if (OB_FAIL(manager->get_tablet_with_allocator(WashTabletPriority::WTP_LOW,
      key, first_allocator, first, true))) { return ret; }
  if (OB_FAIL(manager->get_tablet_with_allocator(WashTabletPriority::WTP_LOW,
      key, second_allocator, second, true))) { return ret; }
  const ObTablet *a = first.get_obj(), *b = second.get_obj();
  if (!a->is_external_tablet() || !b->is_external_tablet() || a == b) {
    return OB_ERR_UNEXPECTED;
  }
  if (manager->register_external_tablet(*first.get_obj()) != OB_ENTRY_EXIST) {
    return OB_ERR_UNEXPECTED;
  }
  auto scan = [&](const std::function<int(const ObTablet &)> &visit) {
    int rc = OB_EAGAIN;
    const int64_t until = ObTimeUtility::current_time() + 5000000;
    while (rc == OB_EAGAIN && ObTimeUtility::current_time() < until) {
      rc = manager->scan_external_tablets(visit);
      if (rc == OB_EAGAIN) { usleep(1000); }
    }
    return rc;
  };
  auto check = [&](bool expect_a, bool expect_b) {
    int64_t seen_a = 0, seen_b = 0;
    const int rc = scan([&](const ObTablet &tablet) {
      seen_a += &tablet == a;
      seen_b += &tablet == b;
      return OB_SUCCESS;
    });
    return rc != OB_SUCCESS ? rc :
        seen_a == int(expect_a) && seen_b == int(expect_b) ? OB_SUCCESS : OB_ERR_UNEXPECTED;
  };
  if (OB_FAIL(check(true, true))) { return ret; }
  int64_t all_visits = 0;
  const int64_t collect_until = ObTimeUtility::current_time() + 5000000;
  int64_t elapsed = 0;
  do {
    int64_t seen_a = 0, seen_b = 0, seen_old = 0, seen_current = 0;
    all_visits = 0;
    const int64_t started = ObTimeUtility::current_time();
    ret = manager->scan_tablet_references([&](const ObTablet &tablet) {
      ++all_visits;
      seen_a += &tablet == a;
      seen_b += &tablet == b;
      seen_old += &tablet == old;
      seen_current += &tablet == replacement.get_obj();
      return OB_SUCCESS;
    });
    elapsed = ObTimeUtility::current_time() - started;
    if (ret == OB_SUCCESS && (seen_a == 0 || seen_b == 0 || seen_old == 0
        || seen_current == 0 || all_visits < 4)) {
      return OB_ERR_UNEXPECTED;
    }
    if (ret == OB_EAGAIN) { usleep(1000); }
  } while (ret == OB_EAGAIN && ObTimeUtility::current_time() < collect_until);
  if (ret != OB_SUCCESS) { return ret; }
  fprintf(stderr, "TABLE_LAYOUT_ALL_ROOTS_PASS visits=%ld cost_us=%ld external_copies=2 old_pool=1 current_pool=1\n",
      all_visits, elapsed);

  // Re-publish the same current object during the external phase. Even this
  // ABA case must invalidate capture; per-object addresses cannot detect it.
  ObUpdateTabletPointerParam publish_param;
  publish_param.tablet_addr_ = replacement.get_obj()->get_tablet_addr();
  std::atomic<bool> publish{false}, published{false}, cancel{false};
  int publish_ret = OB_ERR_UNEXPECTED;
  std::thread publisher([&] {
    while (!publish && !cancel) { usleep(1000); }
    if (publish) {
      publish_ret = manager->compare_and_swap_tablet(key, replacement, replacement, publish_param);
      published = true;
    }
  });
  ret = manager->scan_tablet_references([&](const ObTablet &tablet) {
    if (&tablet == b) {
      publish = true;
      while (!published) { usleep(1000); }
    }
    return OB_SUCCESS;
  });
  cancel = true;
  publisher.join();
  if (ret != OB_EAGAIN || !published || publish_ret != OB_SUCCESS) { return OB_ERR_UNEXPECTED; }
  fprintf(stderr, "TABLE_LAYOUT_CAPTURE_RACE_PASS concurrent_publication_rejected=1\n");

  ObArray<StorageSchemaHistory::PhysicalReference> references;
  const int64_t collect_started = ObTimeUtility::current_time();
  do {
    ret = StorageSchemaHistory::collect_physical_references(references);
    if (ret == OB_EAGAIN) { usleep(1000); }
  } while (ret == OB_EAGAIN && ObTimeUtility::current_time() < collect_started + 5000000);
  if (ret != OB_SUCCESS || references.empty()) { return ret == OB_SUCCESS ? OB_ERR_UNEXPECTED : ret; }
  auto &layouts = share::server_service<ObAccessService>()->storage_schema_store();
  InstanceMetaStore::Transaction tx;
  ret = layouts.begin(tx, ObTimeUtility::current_time() + 30000000, true);
  for (int64_t i = 0; ret == OB_SUCCESS && i < references.count(); ++i) {
    ObArenaAllocator scratch(ObMemAttr("LayoutRootTest"));
    ObStorageSchema schema;
    const auto &reference = references.at(i);
    ret = StorageSchemaHistory(layouts, tx).read_published(reference.layout_id,
        reference.minimum_version, scratch, schema);
    if (ret != OB_SUCCESS) {
      fprintf(stderr, "TABLE_LAYOUT_PHYSICAL_REFS_FAIL layout=%lu version=%ld ret=%d\n",
          reference.layout_id, reference.minimum_version, ret);
    }
  }
  if (tx.is_active()) {
    const int end = layouts.commit(tx);
    if (ret == OB_SUCCESS) { ret = end; }
  }
  if (ret != OB_SUCCESS) { return ret; }
  fprintf(stderr, "TABLE_LAYOUT_PHYSICAL_REFS_PASS layouts=%ld exact_bodies=1 cost_us=%ld\n",
      references.count(), ObTimeUtility::current_time() - collect_started);
  ret = StorageSchemaHistory::collect_physical_references(references, ObTimeUtility::current_time() - 1);
  if (ret != OB_TIMEOUT || !references.empty()) { return OB_ERR_UNEXPECTED; }
  fprintf(stderr, "TABLE_LAYOUT_CAPTURE_TIMEOUT_PASS partial_results_discarded=1\n");
  first.reset();
  if (OB_FAIL(check(false, true))) { return ret; }

  // The owner's last handle can reach refcount zero during a scan. The
  // registry must keep its storage alive until the callback returns.
  std::atomic<bool> entered{false}, releasing{false}, released{false}, stop{false};
  std::thread release([&] {
    while (!entered && !stop) { usleep(1000); }
    if (entered) {
      releasing = true;
      second.reset();
      released = true;
    }
  });
  ret = scan([&](const ObTablet &tablet) {
    if (&tablet != b) { return OB_SUCCESS; }
    entered = true;
    while (!releasing) { usleep(1000); }
    usleep(20000);
    if (released) { return OB_ERR_UNEXPECTED; }
    int64_t version = 0;
    int rc = tablet.get_schema_version_from_storage_schema(version);
    if (rc == OB_SUCCESS && (version < 0 || tablet.get_tablet_id() != id
        || tablet.get_tablet_meta().storage_layout_id_ == 0)) { rc = OB_ERR_UNEXPECTED; }
    return rc;
  });
  stop = true;
  release.join();
  if (ret != OB_SUCCESS) { return ret; }
  if (!entered || !released) { return OB_ERR_UNEXPECTED; }
  if (OB_FAIL(check(false, false))) { return ret; }
  fprintf(stderr, "TABLE_LAYOUT_EXTERNAL_ROOTS_PASS copies=2 duplicate_rejected=1 exact_unregister=1 destruction_waits=1\n");
  fflush(stderr);
  return OB_SUCCESS;
}
