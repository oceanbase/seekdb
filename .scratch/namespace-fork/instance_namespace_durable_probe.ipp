// Local cross-restart fork and interrupted DROP probe. Never committed.
static int run_instance_namespace_durable_probe()
{
  auto &kv = share::server_service<storage::ObAccessService>()->instance_meta_store();
  using Tx = storage::InstanceMetaStore::Transaction;
  using rootserver::InstanceNamespaceMetadata;
  using rootserver::InstanceNamespaceRecord;
  int ret = OB_SUCCESS;
#define META_DURABLE_CALL(expr) do { ret = (expr); if (ret != OB_SUCCESS) { \
  fprintf(stderr, "INSTANCE_META_PROBE_FAIL durable line=%d ret=%d operation=%s\n", __LINE__, ret, #expr); return ret; } } while (0)
#define META_DURABLE_ASSERT(expr) do { if (!(expr)) { \
  fprintf(stderr, "INSTANCE_META_PROBE_FAIL durable line=%d assertion=%s ret=%d\n", __LINE__, #expr, ret); return OB_ERR_UNEXPECTED; } } while (0)
  Tx tx;
  META_DURABLE_CALL(kv.begin(tx, ObTimeUtility::current_time() + 120000000));
  InstanceNamespaceMetadata meta(kv, tx);
  InstanceNamespaceRecord child;
  ret = meta.get_namespace(2, child);
  if (ret == OB_ENTRY_NOT_EXIST) {
    uint64_t template_id = 0;
    const int template_ret = meta.find_namespace("__template__", template_id);
    if (template_ret == OB_SUCCESS) {
      uint64_t former_id = 0;
      META_DURABLE_ASSERT(meta.find_namespace("repo-durable", former_id)
          == OB_ENTRY_NOT_EXIST);
      META_DURABLE_CALL(kv.commit(tx));
      fprintf(stderr, "INSTANCE_RECORD_DURABLE phase=verified id=2 pruned=1\n");
      return OB_SUCCESS;
    }
    META_DURABLE_ASSERT(template_ret == OB_ENTRY_NOT_EXIST);
    META_DURABLE_CALL(kv.rollback(tx));
    rootserver::InstanceNamespaceDirectory directory(kv);
    META_DURABLE_CALL(directory.fork_namespace("ns1", "repo-durable",
        [&](int64_t &snapshot) {
          return observer::namespace_worker_prototype::acquire_storage_snapshot(snapshot);
        }, ObTimeUtility::current_time() + 120000000, child));
    META_DURABLE_ASSERT(child.id == 2);
    META_DURABLE_ASSERT(child.roots.state == 0 && child.roots.directory.page != 0
        && child.parent_namespace == 1 && child.fork_cap == child.roots.snapshot);
    InstanceNamespaceRecord found_child;
    META_DURABLE_CALL(directory.find_live("repo-durable",
        ObTimeUtility::current_time() + 120000000, found_child));
    META_DURABLE_ASSERT(found_child.id == child.id);
    std::vector<InstanceNamespaceRecord> live_records;
    META_DURABLE_CALL(directory.list_live(
        ObTimeUtility::current_time() + 120000000, live_records));
    META_DURABLE_ASSERT(live_records.size() >= 2);
    META_DURABLE_CALL(directory.rename_live(child.id, "repo-durable", "repo-renamed",
        ObTimeUtility::current_time() + 120000000));
    ret = directory.find_live("repo-durable",
        ObTimeUtility::current_time() + 120000000, found_child);
    META_DURABLE_ASSERT(ret == OB_ENTRY_NOT_EXIST);
    META_DURABLE_CALL(directory.rename_live(child.id, "repo-renamed", "repo-durable",
        ObTimeUtility::current_time() + 120000000));
    int64_t schema_version = 0;
    META_DURABLE_CALL(directory.schema_version(child.id,
        ObTimeUtility::current_time() + 120000000, schema_version));
    META_DURABLE_ASSERT(schema_version == child.roots.schema_version);
    META_DURABLE_CALL(kv.begin(tx, ObTimeUtility::current_time() + 120000000));
    // Publish a COW replacement using a real inherited physical incarnation.
    // A fabricated source would make the production retention collector fail.
    rootserver::InstanceCatalogPageStore pages(meta);
    ns::NamespaceCatalogTree tree(pages);
    ns::CatalogPageRef ref = child.roots.directory;
    ns::CatalogNode node;
    for (int depth = 0; depth < 64; ++depth) {
      META_DURABLE_ASSERT(tree.read_node(ref, node).ok());
      if (node.leaf) { break; }
      META_DURABLE_ASSERT(!node.children.empty());
      ref = node.children.front();
    }
    META_DURABLE_ASSERT(node.leaf && !node.keys.empty());
    ns::CatalogChanges sources;
    sources[node.keys.front()] = {node.values.front(), false};
    META_DURABLE_CALL(meta.stage_catalog_delta(child.id, schema_version, schema_version,
                                              {}, sources));
    META_DURABLE_CALL(kv.commit(tx));
    fprintf(stderr, "INSTANCE_RECORD_DURABLE phase=created id=%lu snapshot=%ld\n",
        child.id, child.roots.snapshot);
  } else {
    META_DURABLE_CALL(ret);
    META_DURABLE_CALL(meta.get_namespace(2, child, true));
    META_DURABLE_ASSERT(child.parent_namespace == 1 && child.id == 2);
    if (child.roots.state != 2) {
      rootserver::InstanceCatalogPageStore pages(meta);
      ns::NamespaceCatalogTree tree(pages);
      ns::CatalogPageRef ref = child.roots.directory;
      ns::CatalogNode node;
      for (int depth = 0; depth < 64; ++depth) {
        META_DURABLE_ASSERT(tree.read_node(ref, node).ok());
        if (node.leaf) { break; }
        META_DURABLE_ASSERT(!node.children.empty());
        ref = node.children.front();
      }
      META_DURABLE_ASSERT(node.leaf && !node.values.empty());
      ns::CatalogTabletSource source;
      META_DURABLE_ASSERT(ns::NamespaceCatalogCodec::decode_source(node.values.front().data, source));
      // The periodic worker can materialize this real child before the crash.
      // Recovered owned bindings have cap 0; inherited bindings retain fork S.
      const bool owned = database_of(source.physical_tablet_id) == child.id;
      META_DURABLE_ASSERT(node.values.front().cap == (owned ? 0 : child.fork_cap));
      ObTabletHandle handle;
      META_DURABLE_CALL(ObTabletCreateDeleteHelper::check_and_get_tablet(
          ObTabletMapKey(ObTabletID(source.physical_tablet_id)), handle, 0,
          ObMDSGetTabletMode::READ_WITHOUT_CHECK, transaction::ObTransVersion::MAX_TRANS_VERSION));
      int64_t creation = 0;
      META_DURABLE_CALL(handle.get_obj()->get_create_transaction_id(creation));
      META_DURABLE_ASSERT(creation == source.create_transaction_id);
      fprintf(stderr, "INSTANCE_CATALOG_DURABLE_PASS id=2 root=%lu\n", child.roots.directory.page);
    }
    uint64_t found = 0;
    bool done = false;
    if (child.roots.state == 0) {
      META_DURABLE_CALL(meta.find_namespace("repo-durable", found));
      META_DURABLE_ASSERT(found == 2 && child.roots.directory.page != 0);
      META_DURABLE_ASSERT(child.roots.directory.cap == 0 || child.roots.directory.cap == child.fork_cap);
      META_DURABLE_CALL(kv.rollback(tx));
      rootserver::InstanceNamespaceDirectory directory(kv);
      META_DURABLE_CALL(directory.mark_deleting(2, "repo-durable",
          ObTimeUtility::current_time() + 120000000, done));
      META_DURABLE_ASSERT(!done);
      fprintf(stderr, "INSTANCE_RECORD_DURABLE phase=marked id=2\n");
    } else if (child.roots.state == 1) {
      META_DURABLE_CALL(kv.rollback(tx));
      rootserver::InstanceNamespaceDirectory directory(kv);
      META_DURABLE_CALL(directory.finish_drop(2,
          ObTimeUtility::current_time() + 120000000));
      META_DURABLE_CALL(kv.begin(tx, ObTimeUtility::current_time() + 120000000, true));
      InstanceNamespaceRecord deleted;
      META_DURABLE_CALL(meta.get_namespace(child.id, deleted));
      META_DURABLE_ASSERT(deleted.roots.state == 2 && deleted.roots.directory.page == 0
          && deleted.roots.catalog.page == 0);
      META_DURABLE_CALL(kv.rollback(tx));
      fprintf(stderr, "INSTANCE_RECORD_DURABLE phase=finished id=2\n");
    } else {
      META_DURABLE_ASSERT(child.roots.state == 2 && child.name.empty());
      ret = meta.find_namespace("repo-durable", found);
      META_DURABLE_ASSERT(ret == OB_ENTRY_NOT_EXIST);
      META_DURABLE_CALL(kv.rollback(tx));
      rootserver::InstanceNamespaceDirectory directory(kv);
      META_DURABLE_CALL(directory.mark_deleting(2, "repo-durable",
          ObTimeUtility::current_time() + 120000000, done));
      META_DURABLE_ASSERT(done);
      fprintf(stderr, "INSTANCE_RECORD_DURABLE phase=verified id=2\n");
    }
  }
#undef META_DURABLE_ASSERT
#undef META_DURABLE_CALL
  return OB_SUCCESS;
}
