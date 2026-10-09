// Local integration probe: no Namespace SQL Runtime may be loaded by creation.
static int probe_cold_materialization(uint64_t ns_id, const std::string &name)
{
  const char *path = getenv("SEEKDB_COLD_MATERIALIZE_CONTROL");
  if (path == nullptr) { return OB_SUCCESS; }
  FILE *input = fopen(path, "r");
  if (input == nullptr) { return OB_SUCCESS; }
  char selected[128] = {};
  unsigned long long logical = 0;
  int wrong_generation = 0;
  const int fields = fscanf(input, "%127s %llu %d", selected, &logical, &wrong_generation);
  fclose(input);
  if (fields != 3 || name != selected) { return OB_SUCCESS; }
  ns::NamespaceRuntime *runtime = nullptr;
  if (!ns::namespace_registry().get(ns_id, runtime) || runtime == nullptr
      || runtime->service(ns::NamespaceRuntime::SCHEMA_SERVICE) != nullptr) { return OB_ERR_UNEXPECTED; }
  auto &store = share::server_service<ObAccessService>()->instance_meta_store();
  ns::CatalogTabletSource original;
  int64_t cap = 0;
  auto replace_generation = [&](bool corrupt) -> int {
    InstanceMetaStore::Transaction tx;
    int rc = store.begin(tx, ObTimeUtility::current_time() + 30000000);
    rootserver::InstanceNamespaceMetadata metadata(store, tx);
    rootserver::InstanceNamespaceRecord record;
    if (rc == OB_SUCCESS) { rc = metadata.get_namespace(ns_id, record, true); }
    if (rc == OB_SUCCESS && corrupt) {
      rc = metadata.find_tablet_source(record.roots.directory, logical, original, cap);
    }
    ns::CatalogTabletSource changed = original;
    if (corrupt) { ++changed.create_transaction_id; }
    ns::CatalogChanges changes;
    changes[ns::NamespaceCatalogCodec::object_key(logical)] = {
        {ns::NamespaceCatalogCodec::encode_source(changed), cap}, false};
    if (rc == OB_SUCCESS) {
      rc = metadata.stage_catalog_delta(ns_id, record.roots.schema_version,
          record.roots.schema_version, {}, changes);
    }
    if (tx.is_active()) {
      const int end = rc == OB_SUCCESS ? store.commit(tx) : store.rollback(tx);
      if (rc == OB_SUCCESS) { rc = end; }
    }
    return rc;
  };
  int ret = wrong_generation ? replace_generation(true) : OB_SUCCESS;
  if (ret == OB_SUCCESS && wrong_generation) {
    ns::TabletAccess failed;
    const int rc = failed.prepare_write(ns_id, OB_INVALID_ID, ObTabletID(logical),
        data_plane::ObNamespaceAccessMode::LEASED);
    if (rc != OB_SNAPSHOT_DISCARDED) { return OB_ERR_UNEXPECTED; }
    ObTabletHandle tablet;
    const int physical = ObTabletCreateDeleteHelper::check_and_get_tablet(
        ObTabletMapKey(ObTabletID(ns::NamespaceObjectKey{ns_id, logical}.storage_id())),
        tablet, 0, ObMDSGetTabletMode::READ_WITHOUT_CHECK,
        transaction::ObTransVersion::MAX_TRANS_VERSION);
    if (physical != OB_TABLET_NOT_EXIST && physical != OB_ENTRY_NOT_EXIST) { return OB_ERR_UNEXPECTED; }
    ret = replace_generation(false);
  }
  ns::TabletAccess access;
  if (ret == OB_SUCCESS) {
    ret = access.prepare_write(ns_id, OB_INVALID_ID, ObTabletID(logical),
        data_plane::ObNamespaceAccessMode::LEASED);
  }
  for (uint8_t slot = 0; ret == OB_SUCCESS && slot < ns::NamespaceRuntime::SLOT_COUNT; ++slot) {
    if (runtime->service(static_cast<ns::NamespaceRuntime::ServiceSlot>(slot)) != nullptr) {
      ret = OB_ERR_UNEXPECTED;
    }
  }
  fprintf(stderr, "COLD_MATERIALIZATION namespace=%lu tablet=%llu wrong_generation=%d runtime_slots=0 ret=%d\n",
      ns_id, logical, wrong_generation, ret);
  return ret;
}
