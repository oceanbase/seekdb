// Checks that FreezeInfoMgr consumes committed KV pins. Local test build only.
static int run_instance_pin_retention_probe()
{
  auto &kv = share::server_service<storage::ObAccessService>()->instance_meta_store();
  auto *freeze = share::server_service<storage::ObFreezeInfoMgr>();
  using Tx = storage::InstanceMetaStore::Transaction;
  int ret = OB_SUCCESS;
#define PIN_PROBE_CALL(expr) do { ret = (expr); if (ret != OB_SUCCESS) { \
  fprintf(stderr, "INSTANCE_META_PROBE_FAIL pin line=%d ret=%d operation=%s\n", __LINE__, ret, #expr); return ret; } } while (0)
#define PIN_PROBE_ASSERT(expr) do { if (!(expr)) { \
  fprintf(stderr, "INSTANCE_META_PROBE_FAIL pin line=%d assertion=%s ret=%d\n", __LINE__, #expr, ret); return OB_ERR_UNEXPECTED; } } while (0)
  PIN_PROBE_ASSERT(freeze != nullptr);
  Tx registration;
  PIN_PROBE_CALL(kv.begin(registration, ObTimeUtility::current_time() + 120000000));
  rootserver::InstanceNamespaceMetadata metadata(kv, registration);
  int64_t watermark = 0;
  bool initialized_here = false;
  ret = metadata.get_snapshot_gc_watermark(watermark);
  if (ret == OB_ENTRY_NOT_EXIST) {
    watermark = 100;
    initialized_here = true;
    PIN_PROBE_CALL(metadata.initialize_snapshot_gc_watermark(watermark));
  } else {
    PIN_PROBE_CALL(ret);
  }
  const int64_t probe_snapshot = watermark + 1;
  PIN_PROBE_ASSERT(probe_snapshot > watermark);
  PIN_PROBE_CALL(freeze->reload_for_test());
  PIN_PROBE_ASSERT(!freeze->probe_has_instance_pin(probe_snapshot));
  PIN_PROBE_CALL(metadata.insert_pin({static_cast<uint64_t>(probe_snapshot), 5}));
  PIN_PROBE_CALL(kv.commit(registration));
  PIN_PROBE_CALL(freeze->reload_for_test());
  PIN_PROBE_ASSERT(freeze->probe_has_instance_pin(probe_snapshot));

  Tx cleanup;
  PIN_PROBE_CALL(kv.begin(cleanup, ObTimeUtility::current_time() + 120000000));
  rootserver::InstanceNamespaceMetadata cleanup_metadata(kv, cleanup);
  PIN_PROBE_CALL(cleanup_metadata.erase_pin(static_cast<uint64_t>(probe_snapshot)));
  if (initialized_here) {
    const char coordinator_key[8] = {0, 0, 0, 0, 0, 0, 0, 1};
    bool existed = false;
    PIN_PROBE_CALL(kv.erase(cleanup, storage::MetaCollection::SNAPSHOT_COORDINATION,
        common::ObString(8, coordinator_key), existed));
    PIN_PROBE_ASSERT(existed);
  }
  PIN_PROBE_CALL(kv.commit(cleanup));
  PIN_PROBE_CALL(freeze->reload_for_test());
  PIN_PROBE_ASSERT(!freeze->probe_has_instance_pin(probe_snapshot));
  fprintf(stderr, "INSTANCE_PIN_RELOAD_PROBE_PASS pin=%ld\n", probe_snapshot);
#undef PIN_PROBE_ASSERT
#undef PIN_PROBE_CALL
  return OB_SUCCESS;
}
