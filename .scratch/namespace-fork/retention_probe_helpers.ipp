#pragma once
// Local fixtures use real, committed physical incarnations from this instance.
static int select_retention_probe_sources(std::vector<std::pair<uint64_t, int64_t>> &sources)
{
  ObArray<ObTabletID> tablets;
  int ret = all_physical_tablet_ids(tablets);
  for (int64_t i = 0; ret == OB_SUCCESS && i < tablets.count() && sources.size() < 2; ++i) {
    ObTabletHandle handle;
    ret = ObTabletCreateDeleteHelper::check_and_get_tablet(ObTabletMapKey(tablets.at(i)),
        handle, 0, ObMDSGetTabletMode::READ_WITHOUT_CHECK, transaction::ObTransVersion::MAX_TRANS_VERSION);
    if (ret != OB_SUCCESS) { break; }
    if (handle.get_obj()->is_empty_shell() || handle.get_obj()->get_tablet_meta().fork_info_.is_valid()) { continue; }
    ObTabletCreateDeleteMdsUserData status;
    mds::MdsWriter writer;
    mds::TwoPhaseCommitState state;
    SCN version;
    ret = handle.get_obj()->get_latest_tablet_status(status, writer, state, version);
    if (ret == OB_SUCCESS && state == mds::TwoPhaseCommitState::ON_COMMIT && status.create_transaction_id_ > 0) {
      sources.emplace_back(tablets.at(i).id(), status.create_transaction_id_);
    }
  }
  return ret != OB_SUCCESS ? ret : sources.size() == 2 ? OB_SUCCESS : OB_ERR_UNEXPECTED;
}

static int load_retention_probe_plan(PhysicalSnapshotRetention &plan)
{
  const int64_t deadline = ObTimeUtility::current_time() + 30000000;
  int ret = OB_SUCCESS;
  do {
    ret = NamespaceForkKernelPrototype::load_physical_retention(plan);
    if (ret == OB_EAGAIN) { usleep(10000); }
  } while (ret == OB_EAGAIN && ObTimeUtility::current_time() < deadline);
  return ret;
}
