// Local typed metadata integration probe. Never committed to the branch.
static int run_instance_namespace_metadata_probe()
{
  auto &kv = share::server_service<storage::ObAccessService>()->instance_meta_store();
  using Tx = storage::InstanceMetaStore::Transaction;
  using rootserver::InstanceNamespaceMetadata;
  using rootserver::InstanceNamespaceRecord;
  using rootserver::InstanceNamespaceDirectory;
  int ret = OB_SUCCESS;
#define META_RECORD_CALL(expr) do { ret = (expr); if (ret != OB_SUCCESS) { \
  fprintf(stderr, "INSTANCE_META_PROBE_FAIL record line=%d ret=%d operation=%s\n", __LINE__, ret, #expr); return ret; } } while (0)
#define META_RECORD_ASSERT(expr) do { if (!(expr)) { \
  fprintf(stderr, "INSTANCE_META_PROBE_FAIL record line=%d assertion=%s ret=%d\n", __LINE__, #expr, ret); return OB_ERR_UNEXPECTED; } } while (0)
  rootserver::InstanceNamespaceRecord root;
  InstanceNamespaceDirectory directory(kv);
  bool root_created = false;
  {
    Tx lookup;
    META_RECORD_CALL(kv.begin(lookup, ObTimeUtility::current_time() + 120000000, true));
    InstanceNamespaceMetadata initial(kv, lookup);
    const int lookup_ret = initial.get_namespace(1, root);
    META_RECORD_CALL(kv.commit(lookup));
    if (lookup_ret == OB_ENTRY_NOT_EXIST) {
      // The freeze detector can initialize coordination before the Namespace
      // directory bootstrap. Exercise that real ordering explicitly.
      Tx early_gc;
      META_RECORD_CALL(kv.begin(early_gc, ObTimeUtility::current_time() + 120000000));
      InstanceNamespaceMetadata early_metadata(kv, early_gc);
      int64_t early_watermark = 0;
      const int early_ret = early_metadata.get_snapshot_gc_watermark(early_watermark);
      if (early_ret == OB_ENTRY_NOT_EXIST) {
        META_RECORD_CALL(early_metadata.initialize_snapshot_gc_watermark(100));
      } else {
        META_RECORD_CALL(early_ret);
      }
      META_RECORD_CALL(kv.commit(early_gc));
      META_RECORD_CALL(directory.ensure_root("ns1", 5, 100,
          ObTimeUtility::current_time() + 120000000, root_created));
    } else {
      META_RECORD_CALL(lookup_ret);
    }
  }
  const bool root_recovered = !root_created;
  {
    Tx bootstrap;
    META_RECORD_CALL(kv.begin(bootstrap, ObTimeUtility::current_time() + 120000000, true));
    InstanceNamespaceMetadata initial(kv, bootstrap);
    META_RECORD_CALL(initial.get_namespace(1, root));
    META_RECORD_ASSERT(root.name == "ns1" && root.roots.schema_version >= 5);
    META_RECORD_CALL(kv.commit(bootstrap));
  }
  InstanceNamespaceRecord live_root;
  META_RECORD_CALL(directory.find_live("ns1",
      ObTimeUtility::current_time() + 120000000, live_root));
  META_RECORD_ASSERT(live_root.id == 1);
  std::vector<InstanceNamespaceRecord> live_records;
  META_RECORD_CALL(directory.list_live(
      ObTimeUtility::current_time() + 120000000, live_records));
  META_RECORD_ASSERT(!live_records.empty() && live_records.front().id == 1);
  if (root_recovered) {
    fprintf(stderr, "INSTANCE_RECORD_PROBE_PASS root_recovered=1 durable_followup=1\n");
    return OB_SUCCESS;
  }
  Tx tx;
  META_RECORD_CALL(kv.begin(tx, ObTimeUtility::current_time() + 120000000));
  InstanceNamespaceMetadata meta(kv, tx);
  META_RECORD_CALL(meta.get_namespace(1, root));
  META_RECORD_ASSERT(root.name == "ns1" && root.roots.schema_version == 5);
  int64_t watermark = 0;
  ret = meta.get_snapshot_gc_watermark(watermark);
  if (ret == OB_ENTRY_NOT_EXIST) {
    watermark = 100;
    META_RECORD_CALL(meta.initialize_snapshot_gc_watermark(watermark));
  } else {
    META_RECORD_CALL(ret);
  }
  META_RECORD_ASSERT(watermark <= std::numeric_limits<int64_t>::max() - 25);
  const int64_t first_snapshot = watermark + 23;
  const int64_t second_snapshot = watermark + 24;
  const int64_t third_snapshot = watermark + 25;
  uint64_t id = 0, found = 0;
  META_RECORD_CALL(meta.allocate_namespace_id(id));
  META_RECORD_ASSERT(id == 2);
  InstanceNamespaceRecord record;
  record.id = id;
  record.name = std::string("repo\0namespace", 14);
  record.parent_namespace = 1;
  record.fork_cap = first_snapshot;
  record.allow_login = false;
  record.roots.snapshot = first_snapshot;
  record.roots.schema_version = 5;
  record.roots.catalog.cap = first_snapshot;
  record.roots.directory.cap = first_snapshot;
  META_RECORD_CALL(meta.insert_namespace(record));
  META_RECORD_CALL(meta.find_namespace(record.name, found));
  META_RECORD_ASSERT(found == id);
  ret = meta.rename_namespace(id, "wrong", "repo-renamed");
  META_RECORD_ASSERT(ret == OB_STATE_NOT_MATCH);
  META_RECORD_CALL(meta.rename_namespace(id, record.name, "repo-renamed"));
  ret = meta.find_namespace(record.name, found);
  META_RECORD_ASSERT(ret == OB_ENTRY_NOT_EXIST);
  META_RECORD_CALL(meta.find_namespace("repo-renamed", found));
  META_RECORD_ASSERT(found == id);
  META_RECORD_CALL(meta.rename_namespace(id, "repo-renamed", record.name));
  InstanceNamespaceRecord read;
  META_RECORD_CALL(meta.get_namespace(id, read, true));
  META_RECORD_ASSERT(read.name == record.name && read.parent_namespace == 1 && read.fork_cap == first_snapshot && !read.allow_login);
  int namespace_count = 0;
  META_RECORD_CALL(meta.scan_namespaces([&](const InstanceNamespaceRecord &row) {
    namespace_count++;
    return row.id == 1 || row.id == id ? OB_SUCCESS : OB_ERR_UNEXPECTED;
  }));
  META_RECORD_ASSERT(namespace_count == 2);
  uint64_t rejected_id = 0;
  ret = meta.fork_namespace(record.name, "too-old",
      [&](int64_t &scn) { scn = watermark; return OB_SUCCESS; }, rejected_id);
  META_RECORD_ASSERT(ret == OB_SNAPSHOT_DISCARDED && rejected_id == 0);
  ret = meta.find_namespace("too-old", found);
  META_RECORD_ASSERT(ret == OB_ENTRY_NOT_EXIST);
  uint64_t child_id = 0;
  META_RECORD_CALL(meta.fork_namespace(record.name, "repo-child",
      [&](int64_t &scn) { scn = second_snapshot; return OB_SUCCESS; }, child_id));
  InstanceNamespaceRecord child;
  META_RECORD_CALL(meta.get_namespace(child_id, child));
  META_RECORD_ASSERT(child.allow_login && child.id == id + 1 && child.parent_namespace == id
      && child.fork_cap == second_snapshot
      && child.roots.catalog.cap == first_snapshot
      && child.roots.directory.cap == first_snapshot);
  // A captured source remains fixed across changes to its former owner.
  const uint64_t physical_copy = ns::NamespaceObjectKey{1, 901}.storage_id();
  ns::CatalogTabletSource captured{987, physical_copy, 71, 901, 0, 0};
  ns::CatalogChanges delta;
  delta[ns::NamespaceCatalogCodec::object_key(901)] = {
      {ns::NamespaceCatalogCodec::encode_source(captured), first_snapshot}, false};
  rootserver::InstanceCatalogPageStore source_pages(meta);
  ns::NamespaceCatalogTree source_tree(source_pages);
  ns::CatalogPageRef source_root, changed;
  META_RECORD_ASSERT(source_tree.apply({}, delta, source_root).ok());
  ns::CatalogTabletSource found_source;
  int64_t read_cap = 0;
  META_RECORD_CALL(meta.find_tablet_source(source_root, 901, found_source, read_cap));
  META_RECORD_ASSERT(found_source.physical_tablet_id == physical_copy && read_cap == first_snapshot);
  captured.physical_tablet_id = ns::NamespaceObjectKey{child.id, 901}.storage_id();
  captured.create_transaction_id = 72;
  delta.begin()->second.value = {ns::NamespaceCatalogCodec::encode_source(captured), 0};
  META_RECORD_ASSERT(source_tree.apply(source_root, delta, changed).ok());
  META_RECORD_CALL(meta.find_tablet_source(changed, 901, found_source, read_cap));
  META_RECORD_ASSERT(found_source.physical_tablet_id == captured.physical_tablet_id && read_cap == 0);
  META_RECORD_CALL(meta.find_tablet_source(source_root, 901, found_source, read_cap));
  META_RECORD_ASSERT(found_source.physical_tablet_id == physical_copy && read_cap == first_snapshot);
  delta.begin()->second.erase = true;
  META_RECORD_ASSERT(source_tree.apply(changed, delta, changed).ok());
  META_RECORD_ASSERT(meta.find_tablet_source(changed, 901, found_source, read_cap) == OB_ENTRY_NOT_EXIST);
  META_RECORD_CALL(meta.find_tablet_source(source_root, 901, found_source, read_cap));
  META_RECORD_ASSERT(found_source.physical_tablet_id == physical_copy);
  uint64_t automatic_child_id = 0;
  META_RECORD_CALL(meta.fork_namespace(record.name, "repo-auto",
      [&](int64_t &snapshot_id) { snapshot_id = third_snapshot; return OB_SUCCESS; },
      automatic_child_id));
  META_RECORD_ASSERT(automatic_child_id == 4);
  InstanceNamespaceRecord automatic_child;
  META_RECORD_CALL(meta.get_namespace(automatic_child_id, automatic_child));
  META_RECORD_ASSERT(automatic_child.name == "repo-auto"
      && automatic_child.parent_namespace == id
      && automatic_child.fork_cap == third_snapshot);
  bool drop_done = false;
  ret = meta.mark_namespace_deleting(1, drop_done);
  META_RECORD_ASSERT(ret == OB_OP_NOT_ALLOW);
  META_RECORD_CALL(meta.mark_namespace_deleting(automatic_child_id, drop_done));
  META_RECORD_ASSERT(!drop_done);
  META_RECORD_CALL(meta.mark_namespace_deleting(automatic_child_id, drop_done));
  META_RECORD_ASSERT(!drop_done);
  META_RECORD_CALL(meta.finish_namespace_drop(automatic_child_id));
  META_RECORD_CALL(meta.get_namespace(automatic_child_id, automatic_child));
  META_RECORD_ASSERT(automatic_child.roots.state == 2 && automatic_child.name.empty());
  ret = meta.find_namespace("repo-auto", found);
  META_RECORD_ASSERT(ret == OB_ENTRY_NOT_EXIST);
  META_RECORD_CALL(meta.mark_namespace_deleting(automatic_child_id, drop_done));
  META_RECORD_ASSERT(drop_done);
  bool pruned = false;
  InstanceNamespaceRecord dependent;
  dependent.id = automatic_child_id + 1;
  dependent.name = "repo-dependent";
  dependent.parent_namespace = automatic_child_id;
  META_RECORD_CALL(meta.insert_namespace(dependent));
  META_RECORD_CALL(meta.prune_deleted_namespace(automatic_child_id,
      [](uint64_t, bool &physical) { physical = false; return OB_SUCCESS; }, pruned));
  META_RECORD_ASSERT(!pruned);
  META_RECORD_CALL(meta.erase_namespace(dependent.id));
  META_RECORD_CALL(meta.prune_deleted_namespace(automatic_child_id,
      [](uint64_t, bool &physical) { physical = true; return OB_SUCCESS; }, pruned));
  META_RECORD_ASSERT(!pruned);
  META_RECORD_CALL(meta.prune_deleted_namespace(automatic_child_id,
      [](uint64_t, bool &physical) { physical = false; return OB_SUCCESS; }, pruned));
  META_RECORD_ASSERT(pruned);
  ret = meta.get_namespace(automatic_child_id, automatic_child);
  META_RECORD_ASSERT(ret == OB_ENTRY_NOT_EXIST);
  META_RECORD_CALL(meta.mark_namespace_deleting(child.id, drop_done));
  META_RECORD_CALL(meta.finish_namespace_drop(child.id));
  META_RECORD_CALL(meta.advance_snapshot_gc_watermark(second_snapshot));
  watermark = 0;
  META_RECORD_CALL(meta.get_snapshot_gc_watermark(watermark));
  META_RECORD_ASSERT(watermark == second_snapshot);
  ret = meta.fork_namespace(record.name, "at-watermark",
      [&](int64_t &scn) { scn = second_snapshot; return OB_SUCCESS; }, rejected_id);
  META_RECORD_ASSERT(ret == OB_SNAPSHOT_DISCARDED);
  std::string page_data("page\0payload", 12), page_read;
  uint64_t page = 0, same_page = 0;
  META_RECORD_CALL(meta.save_page(page_data, page));
  META_RECORD_CALL(meta.save_page(page_data, same_page));
  META_RECORD_ASSERT(page != 0 && page == same_page);
  META_RECORD_CALL(meta.read_page(page, page_read));
  META_RECORD_ASSERT(page_read == page_data);
  rootserver::InstanceCatalogPageStore pages(meta);
  ns::NamespaceCatalogTree tree(pages);
  ns::CatalogPageRef new_root;
  const auto tree_put = tree.put({}, "table:987", {"tablet:42", first_snapshot}, new_root);
  META_RECORD_ASSERT(tree_put.ok() && new_root.page != 0);
  ns::CatalogValue tree_value;
  const auto tree_find = tree.find(new_root, "table:987", tree_value);
  META_RECORD_ASSERT(tree_find.ok() && tree_value.data == "tablet:42");
  ns::CatalogChanges definition_changes, source_changes;
  std::string object_data(180001, '\0'), object_read;
  for (size_t i = 0; i < object_data.size(); ++i) { object_data[i] = char(i % 251); }
  uint64_t object_id = 0;
  META_RECORD_CALL(meta.save_object(object_data, object_id));
  META_RECORD_CALL(meta.read_object(object_id, object_read));
  META_RECORD_ASSERT(object_read == object_data);
  definition_changes[ns::NamespaceCatalogCodec::object_key(987)] = {
      {ns::NamespaceCatalogCodec::encode_entry(object_id, 987, 0), 0}, false};
  for (uint64_t tablet = 1000; tablet < 1800; ++tablet) {
    source_changes[ns::NamespaceCatalogCodec::object_key(tablet)] = {
        {ns::NamespaceCatalogCodec::encode_source({987, ns::NamespaceObjectKey{1, tablet}.storage_id(), 71, tablet, 0, 0}), 0}, false};
  }
  META_RECORD_CALL(meta.stage_catalog_delta(id, 5, 10, definition_changes, source_changes));
  META_RECORD_CALL(meta.get_namespace(id, read));
  META_RECORD_ASSERT(read.roots.schema_version == 10 && read.roots.catalog.page != 0
      && read.roots.directory.page != 0);
  const auto published_roots = read.roots;
  for (uint64_t tablet = 1000; tablet < 1800; ++tablet) {
    const auto found_source = tree.find(read.roots.directory,
        ns::NamespaceCatalogCodec::object_key(tablet), tree_value);
    META_RECORD_ASSERT(found_source.ok() && tree_value.data
        == ns::NamespaceCatalogCodec::encode_source({987, ns::NamespaceObjectKey{1, tablet}.storage_id(), 71, tablet, 0, 0}));
  }
  ret = meta.stage_catalog_delta(id, 8, 11, {}, source_changes);
  META_RECORD_ASSERT(ret == OB_EAGAIN);
  META_RECORD_CALL(meta.get_namespace(id, read));
  META_RECORD_ASSERT(read.roots.schema_version == 10
      && read.roots.catalog.page == published_roots.catalog.page
      && read.roots.directory.page == published_roots.directory.page);
  source_changes.clear();
  source_changes[ns::NamespaceCatalogCodec::object_key(1000)] = {{}, true};
  META_RECORD_CALL(meta.stage_catalog_delta(id, 10, 10, {}, source_changes));
  META_RECORD_CALL(meta.get_namespace(id, read));
  const auto removed_source = tree.find(read.roots.directory,
      ns::NamespaceCatalogCodec::object_key(1000), tree_value);
  META_RECORD_ASSERT(removed_source.error == ns::CatalogTreeError::NOT_FOUND);
  const auto old_source = tree.find(published_roots.directory,
      ns::NamespaceCatalogCodec::object_key(1000), tree_value);
  META_RECORD_ASSERT(old_source.ok());
  InstanceNamespaceRecord duplicate = record;
  duplicate.id = id + 1;
  ret = meta.insert_namespace(duplicate);
  META_RECORD_ASSERT(ret == OB_ERR_PRIMARY_KEY_DUPLICATE);
  META_RECORD_CALL(kv.rollback(tx));
  Tx verify;
  META_RECORD_CALL(kv.begin(verify, ObTimeUtility::current_time() + 120000000));
  InstanceNamespaceMetadata absent(kv, verify);
  ret = absent.get_namespace(id, read);
  META_RECORD_ASSERT(ret == OB_ENTRY_NOT_EXIST);
  ret = absent.read_page(published_roots.directory.page, page_read);
  META_RECORD_ASSERT(ret == OB_ENTRY_NOT_EXIST);
  ret = absent.read_object(object_id, object_read);
  META_RECORD_ASSERT(ret == OB_ENTRY_NOT_EXIST);
  ret = absent.get_namespace(1, read);
  META_RECORD_ASSERT(ret == OB_SUCCESS && read.name == "ns1");
  META_RECORD_CALL(kv.commit(verify));
  Tx held;
  META_RECORD_CALL(kv.begin(held, ObTimeUtility::current_time() + 120000000));
  Tx blocked;
  ret = kv.begin_directory_gc(blocked, ObTimeUtility::current_time() + 100000);
  META_RECORD_ASSERT(ret == OB_TIMEOUT && !blocked.is_active());
  META_RECORD_CALL(kv.rollback(held));
  Tx gc_tx;
  META_RECORD_CALL(kv.begin_directory_gc(gc_tx, ObTimeUtility::current_time() + 120000000));
  InstanceNamespaceMetadata gc_meta(kv, gc_tx);
  Tx ordinary_blocked;
  ret = kv.begin(ordinary_blocked, ObTimeUtility::current_time() + 100000);
  META_RECORD_ASSERT(ret == OB_TIMEOUT && !ordinary_blocked.is_active());
  rootserver::InstanceCatalogPageStore gc_pages(gc_meta);
  ns::NamespaceCatalogTree gc_tree(gc_pages);
  ns::CatalogPageRef live_page, snapshot_page;
  META_RECORD_CALL(gc_meta.save_object(object_data, object_id));
  const auto live_result = gc_tree.put({}, "live",
      {ns::NamespaceCatalogCodec::encode_entry(object_id, 987, 42), 777}, live_page);
  META_RECORD_ASSERT(live_result.ok() && live_page.page != 0);
  const auto snapshot_result = gc_tree.put({}, "snapshot",
      {ns::NamespaceCatalogCodec::encode_entry(0, 988, 43), 777}, snapshot_page);
  META_RECORD_ASSERT(snapshot_result.ok() && snapshot_page.page != 0);
  live_page.cap = snapshot_page.cap = 777;
  uint64_t orphan_page = 0;
  META_RECORD_CALL(gc_meta.save_page("unreachable page", orphan_page));
  InstanceNamespaceRecord gc_namespace;
  gc_namespace.id = 500000;
  gc_namespace.name = "gc-probe";
  gc_namespace.parent_namespace = 1;
  gc_namespace.fork_cap = 777;
  gc_namespace.roots.catalog = live_page;
  gc_namespace.roots.schema_version = 5;
  META_RECORD_CALL(gc_meta.insert_namespace(gc_namespace));
  InstanceNamespaceRecord gc_child;
  gc_child.id = 500001;
  gc_child.name = "gc-child";
  gc_child.parent_namespace = gc_namespace.id;
  gc_child.fork_cap = 777;
  gc_child.roots.schema_version = 5;
  gc_child.roots.catalog = snapshot_page;
  META_RECORD_CALL(gc_meta.insert_namespace(gc_child));
  int64_t deleted_pages = 0;
  META_RECORD_CALL(gc_meta.collect_unreachable_pages(256, deleted_pages));
  META_RECORD_ASSERT(deleted_pages >= 1);
  ret = gc_meta.read_page(orphan_page, page_read);
  META_RECORD_ASSERT(ret == OB_ENTRY_NOT_EXIST);
  META_RECORD_CALL(gc_meta.read_page(live_page.page, page_read));
  META_RECORD_CALL(gc_meta.read_page(snapshot_page.page, page_read));
  META_RECORD_CALL(gc_meta.read_object(object_id, object_read));
  META_RECORD_ASSERT(object_read == object_data);
  META_RECORD_CALL(gc_meta.erase_namespace(gc_namespace.id));
  META_RECORD_CALL(gc_meta.collect_unreachable_pages(256, deleted_pages));
  ret = gc_meta.read_object(object_id, object_read);
  META_RECORD_ASSERT(ret == OB_ENTRY_NOT_EXIST);
  uint64_t broken_object = 0, protected_orphan = 0;
  META_RECORD_CALL(gc_meta.save_page(
      ns::NamespaceCatalogCodec::encode_object(1, {UINT64_MAX}), broken_object));
  ns::CatalogPageRef broken_root;
  META_RECORD_ASSERT(gc_tree.put({}, "broken",
      {ns::NamespaceCatalogCodec::encode_entry(broken_object, 989, 44), 0}, broken_root).ok());
  gc_namespace.roots.catalog = broken_root;
  META_RECORD_CALL(gc_meta.insert_namespace(gc_namespace));
  META_RECORD_CALL(gc_meta.save_page("must survive failed graph traversal", protected_orphan));
  ret = gc_meta.collect_unreachable_pages(256, deleted_pages);
  META_RECORD_ASSERT(ret == OB_ENTRY_NOT_EXIST && deleted_pages == 0);
  META_RECORD_CALL(gc_meta.read_page(protected_orphan, page_read));
  META_RECORD_CALL(kv.rollback(gc_tx));
  fprintf(stderr, "INSTANCE_RECORD_PROBE_PASS root_recovered=%d capped_roots=1 watermark_fence=1 source_tree=1 page=1 tree=1 rollback=1\n",
      root_recovered);
#undef META_RECORD_ASSERT
#undef META_RECORD_CALL
  return OB_SUCCESS;
}
