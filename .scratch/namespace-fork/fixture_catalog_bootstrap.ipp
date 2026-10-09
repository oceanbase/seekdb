// The typed-record probe deliberately creates Namespace 1 before the actual
// bootstrap entry, using version 5 and empty roots. Complete that test fixture
// before durable-fork probes or the real template can inherit it.
static int initialize_fixture_catalog()
{
  auto &store = share::server_service<ObAccessService>()->instance_meta_store();
  rootserver::InstanceNamespaceDirectory directory(store);
  rootserver::InstanceNamespaceRecord root;
  const int64_t deadline = ObTimeUtility::current_time() + 120000000;
  int ret = directory.get(1, deadline, root);
  if (ret != OB_SUCCESS || root.roots.catalog.page != 0) { return ret; }
  if (root.roots.schema_version != 5 || root.roots.directory.page != 0) { return OB_ERR_UNEXPECTED; }
  ObSchemaGetterGuard guard;
  auto *service = directory_schema_service();
  if (service == nullptr) { return OB_NOT_INIT; }
  int64_t version = 0;
  ret = service->get_runtime_schema_guard(guard);
  if (ret == OB_SUCCESS) { ret = guard.get_schema_version(version); }
  InstanceMetaStore::Transaction tx;
  if (ret == OB_SUCCESS) { ret = store.begin(tx, deadline); }
  if (ret == OB_SUCCESS) {
    rootserver::InstanceNamespaceMetadata metadata(store, tx);
    ret = metadata.stage_catalog_delta(1, 5, version, {}, {});
  }
  if (tx.is_active()) {
    const int end = ret == OB_SUCCESS ? store.commit(tx) : store.rollback(tx);
    if (ret == OB_SUCCESS) { ret = end; }
  }
  if (ret == OB_SUCCESS) {
    rootserver::NamespaceSchemaPublication publication(store, 1);
    ret = publication.initialize(guard);
  }
  fprintf(stderr, "INSTANCE_FIXTURE_CATALOG version=%ld ret=%d\n", version, ret);
  return ret;
}
