// Local native checks: physical identity persists, MDS duration cannot overflow.
class MdsDeadlineProbeAccess : public ObITabletMdsInterface {
public:
  static auto raw_reader() { return &MdsDeadlineProbeAccess::read_raw_data; }
};
static int run_tablet_identity_native_probe()
{
  ObArray<ObTabletID> tablets;
  int ret = all_physical_tablet_ids(tablets);
  ObTabletHandle handle;
  if (ret == OB_SUCCESS && tablets.empty()) { ret = OB_ERR_UNEXPECTED; }
  if (ret == OB_SUCCESS) {
    ret = ObTabletCreateDeleteHelper::check_and_get_tablet(ObTabletMapKey(tablets.at(0)),
        handle, 0, ObMDSGetTabletMode::READ_WITHOUT_CHECK, transaction::ObTransVersion::MAX_TRANS_VERSION);
  }
  if (ret == OB_SUCCESS) {
    const auto &meta = handle.get_obj()->get_tablet_meta();
    ObTabletMeta copy, restored;
    ret = copy.assign(meta);
    std::vector<char> bytes(meta.get_serialize_size());
    int64_t pos = 0;
    if (ret == OB_SUCCESS) { ret = copy.serialize(bytes.data(), bytes.size(), pos); }
    if (ret == OB_SUCCESS && pos != bytes.size()) { ret = OB_ERR_UNEXPECTED; }
    pos = 0;
    if (ret == OB_SUCCESS) { ret = restored.deserialize(bytes.data(), bytes.size(), pos); }
    if (ret == OB_SUCCESS && (meta.create_table_id_ == OB_INVALID_ID
        || restored.create_table_id_ != meta.create_table_id_
        || !restored.is_valid() || pos != bytes.size())) { ret = OB_ERR_UNEXPECTED; }
  }
  for (int mode = 0; ret == OB_SUCCESS && mode < 3; ++mode) {
    ObArenaAllocator alloc("MdsDeadlineTest");
    mds::MdsDumpKV kv;
    // The protected native reader is invoked on the real tablet, bypassing its
    // MDS memory cache so this always exercises scan-param deadline construction.
    const int64_t timeout = mode == 0 ? INT64_MAX : mode == 1
        ? INT64_MAX - ObClockGenerator::getClock() + 1000000 : 1000000;
    const int read = ((*handle.get_obj()).*MdsDeadlineProbeAccess::raw_reader())(
        alloc, 1, ObString(), SCN::max_scn(), timeout, kv);
    if (read != OB_SUCCESS && read != OB_ITER_END) { ret = read; }
    fprintf(stderr, "INSTANCE_MDS_DEADLINE mode=%d timeout=%ld ret=%d\n", mode, timeout, read);
  }
  fprintf(stderr, "INSTANCE_%s creation_identity_roundtrip=1 mds_deadline=1 ret=%d\n",
      ret == OB_SUCCESS ? "TABLET_IDENTITY_PASS" : "META_PROBE_FAIL", ret);
  return ret;
}
