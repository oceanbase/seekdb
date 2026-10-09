// Real collector -> FreezeInfoMgr publication -> physical identity lookup.
static int run_physical_snapshot_retention_native_probe()
{
  auto *freeze = share::server_service<ObFreezeInfoMgr>();
  auto *transactions = share::server_service<transaction::ObTransService>();
  if (!freeze || !transactions) { return OB_NOT_INIT; }
  const int64_t deadline = ObTimeUtility::current_time() + 30000000;
  SCN after_fixtures, weak;
  int ret = transactions->get_read_snapshot_version(deadline, after_fixtures);
  // Earlier fixtures deliberately publish then erase fake logical sources.
  // Wait until the real weak cut excludes those artificial intermediate roots.
  while (ret == OB_SUCCESS) {
    ret = transactions->get_weak_read_snapshot_version(-1, weak);
    if (ret != OB_SUCCESS || weak >= after_fixtures) { break; }
    if (ObTimeUtility::current_time() >= deadline) { ret = OB_TIMEOUT; break; }
    usleep(10000);
  }
  while (ret == OB_SUCCESS) {
    ret = freeze->reload_for_test();
    if (ret != OB_EAGAIN || ObTimeUtility::current_time() >= deadline) { break; }
    ret = OB_SUCCESS;
    usleep(10000);
  }
  PhysicalSnapshotRetention plan;
  while (ret == OB_SUCCESS) {
    ret = NamespaceForkKernelPrototype::load_physical_retention(plan);
    if (ret != OB_EAGAIN || ObTimeUtility::current_time() >= deadline) { break; }
    ret = OB_SUCCESS;
    usleep(10000);
  }
  if (ret == OB_SUCCESS && (!plan.is_valid() || plan.tablets.empty())) { ret = OB_ERR_UNEXPECTED; }
  if (ret == OB_SUCCESS) {
    const auto &entry = *plan.tablets.begin();
    ObStorageSnapshotInfo info;
    ret = freeze->get_min_reserved_snapshot(ObTabletID(entry.first),
        entry.second.create_transaction_id, plan.read_snapshot, info);
    fprintf(stderr, "INSTANCE_RETENTION_LOOKUP physical=%lu identity=%ld kept=%ld required=%ld ret=%d\n", entry.first, entry.second.create_transaction_id, info.snapshot_, entry.second.snapshot, ret);
    if (ret == OB_SUCCESS && info.snapshot_ > entry.second.snapshot) {
      ret = OB_ERR_UNEXPECTED;
    }
    if (ret == OB_SUCCESS) {
      const int conflict = freeze->get_min_reserved_snapshot(ObTabletID(entry.first),
          entry.second.create_transaction_id + 1, plan.read_snapshot, info);
      fprintf(stderr, "INSTANCE_RETENTION_CONFLICT ret=%d\n", conflict);
      if (conflict != OB_STATE_NOT_MATCH) { ret = OB_ERR_UNEXPECTED; }
    }
  }
  fprintf(stderr, "INSTANCE_%s cut=%ld floor=%ld entries=%zu identity_conflict=1 ret=%d\n",
      ret == OB_SUCCESS ? "PHYSICAL_RETENTION_PROBE_PASS" : "META_PROBE_FAIL physical_retention",
      plan.read_snapshot, plan.new_source_floor, plan.tablets.size(), ret);
  return ret;
}
