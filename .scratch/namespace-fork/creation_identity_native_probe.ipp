// Local-only incarnation checks. Expected identities use the same native tx as
// CREATE MDS, so an aborted attempt must leave neither row nor visible tablet.
static constexpr MetaCollection IDENTITY_PROBE_COLLECTION = static_cast<MetaCollection>(10030);
static int record_creation_identity(InstanceMetaStore::Transaction &tx,
    const ObTabletID &tablet, int64_t expected)
{
  if (getenv("SEEKDB_CREATION_IDENTITY_PROBE") == nullptr) { return OB_SUCCESS; }
  ObTabletHandle handle;
  ObTabletCreateDeleteMdsUserData status;
  mds::MdsWriter writer;
  mds::TwoPhaseCommitState state;
  SCN version;
  int ret = ObTabletCreateDeleteHelper::check_and_get_tablet(ObTabletMapKey(tablet),
      handle, 0, ObMDSGetTabletMode::READ_WITHOUT_CHECK, transaction::ObTransVersion::MAX_TRANS_VERSION);
  if (ret == OB_SUCCESS) {
    ret = handle.get_obj()->get_latest_tablet_status(status, writer, state, version);
  }
  if (ret == OB_SUCCESS && (expected <= 0 || status.create_transaction_id_ != expected
      || writer.writer_id_ != expected || state == mds::TwoPhaseCommitState::ON_COMMIT
      || status.physical_create_version_ != transaction::ObTransVersion::INVALID_TRANS_VERSION)) {
    ret = OB_ERR_UNEXPECTED;
  }
  uint64_t id = tablet.id();
  if (ret == OB_SUCCESS) {
    ret = directory_kv_store()->put(tx, IDENTITY_PROBE_COLLECTION,
        ObString(sizeof(id), reinterpret_cast<const char *>(&id)),
        ObString(sizeof(expected), reinterpret_cast<const char *>(&expected)));
  }
  fprintf(stderr, "CREATION_IDENTITY_REGISTER tablet=%lu expected=%ld actual=%ld logical_birth=%ld ret=%d\n",
      id, expected, status.create_transaction_id_, status.create_commit_version_, ret);
  return ret;
}

static int verify_creation_identities()
{
  if (getenv("SEEKDB_CREATION_IDENTITY_PROBE") == nullptr) { return OB_SUCCESS; }
  auto &store = *directory_kv_store();
  InstanceMetaStore::Transaction tx;
  std::vector<std::pair<uint64_t, int64_t>> expected;
  int ret = store.begin(tx, ObTimeUtility::current_time() + 120000000, true);
  if (ret == OB_SUCCESS) {
    InstanceMetaStore::KeyRange range;
    ret = store.scan(tx, IDENTITY_PROBE_COLLECTION, range,
        [&](const ObString &key, const ObString &value, bool &) -> int {
      uint64_t tablet = 0; int64_t identity = 0;
      if (key.length() != sizeof(tablet) || value.length() != sizeof(identity)) { return OB_ERR_UNEXPECTED; }
      memcpy(&tablet, key.ptr(), sizeof(tablet));
      memcpy(&identity, value.ptr(), sizeof(identity));
      expected.push_back({tablet, identity});
      return OB_SUCCESS;
    });
    const int end = store.commit(tx);
    if (ret == OB_SUCCESS) { ret = end; }
  }
  for (const auto &entry : expected) {
    if (ret != OB_SUCCESS) { break; }
    ObTabletHandle handle;
    ObTabletCreateDeleteMdsUserData status;
    mds::MdsWriter writer;
    mds::TwoPhaseCommitState state;
    SCN version;
    ret = ObTabletCreateDeleteHelper::check_and_get_tablet(ObTabletMapKey(ObTabletID(entry.first)),
        handle, 0, ObMDSGetTabletMode::READ_WITHOUT_CHECK, transaction::ObTransVersion::MAX_TRANS_VERSION);
    if (ret == OB_SUCCESS) {
      ret = handle.get_obj()->get_latest_tablet_status(status, writer, state, version);
    }
    if (ret == OB_SUCCESS && (status.create_transaction_id_ != entry.second
        || state != mds::TwoPhaseCommitState::ON_COMMIT
        || status.physical_create_version_ <= status.create_commit_version_
        // The persisted-cache/SSTable branch of get_latest resets the node's
        // optional transaction version. Python checks C against the pre-crash
        // on_commit trace for BOTH memory and persisted reads.
        || (version.is_valid() && status.physical_create_version_ != version.get_val_for_tx()))) { ret = OB_ERR_UNEXPECTED; }
    fprintf(stderr, "CREATION_IDENTITY_RECOVER tablet=%lu expected=%ld actual=%ld logical=%ld physical=%ld node_version_valid=%d ret=%d\n",
        entry.first, entry.second, status.create_transaction_id_, status.create_commit_version_,
        status.physical_create_version_, version.is_valid(), ret);
  }
  // Also exercise the native status copy and persistence paths after DELETE.
  ObTabletCreateDeleteMdsUserData original(ObTabletStatus::NORMAL,
      ObTabletMdsUserDataType::CREATE_TABLET, 123), copied, restored;
  original.create_transaction_id_ = 456;
  if (ret == OB_SUCCESS) { ret = copied.assign(original); }
  copied.tablet_status_ = ObTabletStatus::DELETED;
  copied.data_type_ = ObTabletMdsUserDataType::REMOVE_TABLET;
  copied.on_commit(SCN::min_scn(), SCN::min_scn());
  std::string bytes(copied.get_serialize_size(), '\0');
  int64_t pos = 0;
  if (ret == OB_SUCCESS) { ret = copied.serialize(&bytes[0], bytes.size(), pos); }
  pos = 0;
  if (ret == OB_SUCCESS) { ret = restored.deserialize(bytes.data(), bytes.size(), pos); }
  if (ret == OB_SUCCESS && (pos != bytes.size() || restored.create_transaction_id_ != 456)) {
    ret = OB_ERR_UNEXPECTED;
  }
  fprintf(stderr, "CREATION_IDENTITY_VERIFY records=%lu deletion_codec=1 ret=%d\n", expected.size(), ret);
  return ret;
}
