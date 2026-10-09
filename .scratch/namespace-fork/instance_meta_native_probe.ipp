// Local native integration probe. Injected only in a test build; never committed.
static int run_instance_meta_native_probe()
{
  auto &kv = share::server_service<storage::ObAccessService>()->instance_meta_store();
  using Tx = storage::InstanceMetaStore::Transaction;
  using Collection = storage::MetaCollection;
  const auto first = static_cast<Collection>(10001);
  const auto second = static_cast<Collection>(10002);
  auto deadline = [] { return common::ObTimeUtility::current_time() + 300000000; };
  common::ObArenaAllocator allocator(common::ObMemAttr("MetaProbe"));
  common::ObString value;
  const auto key = common::ObString::make_string("native-probe");
  const auto old = common::ObString::make_string("before");
  const auto next = common::ObString::make_string("after");
  int ret = common::OB_SUCCESS;
#define META_PROBE_CALL(expr) do { ret = (expr); if (ret != common::OB_SUCCESS) { \
  fprintf(stderr, "INSTANCE_META_PROBE_FAIL line=%d ret=%d operation=%s\n", __LINE__, ret, #expr); return ret; } } while (0)
#define META_PROBE_ASSERT(expr) do { if (!(expr)) { \
  fprintf(stderr, "INSTANCE_META_PROBE_FAIL line=%d assertion=%s ret=%d\n", __LINE__, #expr, ret); return common::OB_ERR_UNEXPECTED; } } while (0)
  Tx seed;
  META_PROBE_CALL(kv.begin(seed, deadline()));
  ret = kv.get(seed, first, key, allocator, value);
  const bool recovered = ret == common::OB_SUCCESS;
  META_PROBE_ASSERT(recovered || ret == common::OB_ENTRY_NOT_EXIST);
  if (recovered) { META_PROBE_ASSERT(value == next); }
  META_PROBE_CALL(kv.put(seed, first, key, old));
  META_PROBE_CALL(kv.get(seed, first, key, allocator, value));
  META_PROBE_ASSERT(value == old);
  std::string large(60000, 'x');
  large[123] = '\0';
  common::ObString large_value(large.size(), large.data());
  META_PROBE_CALL(kv.put(seed, second, key, large_value));
  META_PROBE_CALL(kv.commit(seed));

  // Sequential KV reads include both prior puts while preserving the
  // original snapshot for other transactions. Run after every crash restart.
  const auto sequential = static_cast<Collection>(10004);
  const auto own_key = common::ObString::make_string("own");
  const auto other_key = common::ObString::make_string("other");
  Tx initial, own, concurrent;
  META_PROBE_CALL(kv.begin(initial, deadline()));
  META_PROBE_CALL(kv.put(initial, sequential, other_key, old));
  META_PROBE_CALL(kv.commit(initial));
  META_PROBE_CALL(kv.begin(own, deadline()));
  META_PROBE_CALL(kv.put(own, sequential, own_key, old));
  META_PROBE_CALL(kv.put(own, sequential, own_key, next));
  META_PROBE_CALL(kv.begin(concurrent, deadline()));
  META_PROBE_CALL(kv.put(concurrent, sequential, other_key, next));
  META_PROBE_CALL(kv.commit(concurrent));
  META_PROBE_CALL(kv.get(own, sequential, own_key, allocator, value));
  META_PROBE_ASSERT(value == next);
  META_PROBE_CALL(kv.get(own, sequential, other_key, allocator, value));
  META_PROBE_ASSERT(value == old);
  int sequential_count = 0;
  storage::InstanceMetaStore::KeyRange sequential_range;
  META_PROBE_CALL(kv.scan(own, sequential, sequential_range,
      [&](const common::ObString &k, const common::ObString &v, bool &) {
        ++sequential_count;
        const int nested = kv.put(own, sequential, own_key, old);
        return nested == OB_INVALID_ARGUMENT
            && ((k == own_key && v == next) || (k == other_key && v == old))
            ? OB_SUCCESS : OB_ERR_UNEXPECTED;
      }));
  META_PROBE_ASSERT(sequential_count == 2);
  META_PROBE_CALL(kv.get_for_update(own, sequential, other_key, allocator, value));
  META_PROBE_ASSERT(value == next);
  META_PROBE_CALL(kv.rollback(own));
  fprintf(stderr, "INSTANCE_META_SEQUENTIAL_SCAN_PASS own_put_put=1 fixed_snapshot=1 locked_current=1 no_reentrant_write=1\n");

  const auto bounds = static_cast<Collection>(10003);
  Tx bounded;
  META_PROBE_CALL(kv.begin(bounded, deadline()));
  std::string max_key(512, 'k'), max_value(65536, 'v');
  max_key[0] = '\0'; max_value[0] = '\0'; max_value.back() = '\0';
  const common::ObString k512(max_key.size(), max_key.data());
  const common::ObString v64k(max_value.size(), max_value.data());
  META_PROBE_CALL(kv.insert(bounded, bounds, k512, v64k));
  META_PROBE_CALL(kv.get(bounded, bounds, k512, allocator, value));
  META_PROBE_ASSERT(value == v64k);
  std::string oversized_key(513, 'k'), oversized_value(65537, 'v');
  ret = kv.put(bounded, bounds, common::ObString(oversized_key.size(), oversized_key.data()), old);
  META_PROBE_ASSERT(ret == common::OB_SIZE_OVERFLOW);
  ret = kv.put(bounded, bounds, key, common::ObString(oversized_value.size(), oversized_value.data()));
  META_PROBE_ASSERT(ret == common::OB_SIZE_OVERFLOW);
  const char binary_key[] = {'a', '\0', 'b'};
  META_PROBE_CALL(kv.put(bounded, bounds, common::ObString(3, binary_key), common::ObString()));
  META_PROBE_CALL(kv.get(bounded, bounds, common::ObString(3, binary_key), allocator, value));
  META_PROBE_ASSERT(value.length() == 0);
  META_PROBE_CALL(kv.put(bounded, bounds, common::ObString::make_string("b"), old));
  storage::InstanceMetaStore::KeyRange prefix;
  prefix.has_lower = prefix.has_upper = true;
  prefix.include_lower = true; prefix.include_upper = false;
  prefix.lower = common::ObString::make_string("a");
  prefix.upper = common::ObString::make_string("b");
  int prefix_count = 0;
  META_PROBE_CALL(kv.scan(bounded, bounds, prefix,
      [&](const common::ObString &k, const common::ObString &v, bool &) {
        ++prefix_count;
        return k == common::ObString(3, binary_key) && v.length() == 0 ? OB_SUCCESS : OB_ERR_UNEXPECTED;
      }));
  META_PROBE_ASSERT(prefix_count == 1);
  META_PROBE_CALL(kv.rollback(bounded));
  Tx readonly;
  META_PROBE_CALL(kv.begin(readonly, deadline(), true));
  ret = kv.put(readonly, bounds, key, old);
  META_PROBE_ASSERT(ret == common::OB_INVALID_ARGUMENT);
  META_PROBE_CALL(kv.commit(readonly));
  auto freeze_and_wait = [&]() -> int {
    ObLS *ls = nullptr;
    ObTabletHandle handle;
    const ObTabletID tablet(ObTabletID::LS_INSTANCE_META_TABLET_ID);
    int rc = share::server_service<ObLSService>()->get_ls(ls);
    if (rc == OB_SUCCESS) { rc = ls->get_tablet(tablet, handle); }
    const SCN before = rc == OB_SUCCESS ? handle.get_obj()->get_tablet_meta().clog_checkpoint_scn_ : SCN();
    if (rc == OB_SUCCESS) { rc = ls->tablet_freeze(tablet, true, deadline(), false, ObFreezeSourceFlag::TEST_MODE); }
    const int64_t limit = common::ObTimeUtility::current_time() + 60000000;
    while (rc == OB_SUCCESS && common::ObTimeUtility::current_time() < limit) {
      handle.reset();
      rc = ls->get_tablet(tablet, handle);
      if (rc == OB_SUCCESS && handle.get_obj()->get_tablet_meta().clog_checkpoint_scn_ > before
          && handle.get_obj()->get_minor_table_count() > 0) {
        fprintf(stderr, "INSTANCE_META_PROBE_FLUSH minor=%ld\n", handle.get_obj()->get_minor_table_count());
        return OB_SUCCESS;
      }
      usleep(100000);
    }
    return rc == OB_SUCCESS ? OB_TIMEOUT : rc;
  };
  META_PROBE_CALL(freeze_and_wait());
  Tx reader, writer;
  META_PROBE_CALL(kv.begin(reader, deadline()));
  META_PROBE_CALL(kv.get(reader, first, key, allocator, value));
  META_PROBE_ASSERT(value == old);
  META_PROBE_CALL(kv.get(reader, second, key, allocator, value));
  META_PROBE_ASSERT(value == large_value);
  META_PROBE_CALL(kv.begin(writer, deadline()));
  META_PROBE_CALL(kv.put(writer, first, key, next));
  META_PROBE_CALL(kv.commit(writer));
  META_PROBE_CALL(freeze_and_wait());
  for (int generation = 0; generation < 2; ++generation) {
    Tx extra;
    META_PROBE_CALL(kv.begin(extra, deadline()));
    META_PROBE_CALL(kv.put(extra, first, key, next));
    META_PROBE_CALL(kv.commit(extra));
    META_PROBE_CALL(freeze_and_wait());
  }
  ObLS *merge_ls = nullptr;
  META_PROBE_CALL(share::server_service<ObLSService>()->get_ls(merge_ls));
  const int64_t merge_deadline = common::ObTimeUtility::current_time() + 60000000;
  bool merged = false;
  while (!merged && common::ObTimeUtility::current_time() < merge_deadline) {
    ObTabletHandle handle;
    META_PROBE_CALL(merge_ls->get_tablet(ObTabletID(ObTabletID::LS_INSTANCE_META_TABLET_ID), handle));
    merged = handle.get_obj()->get_minor_table_count() < 4;
    if (merged) {
      fprintf(stderr, "INSTANCE_META_PROBE_MINOR four_flushes=1 remaining=%ld\n", handle.get_obj()->get_minor_table_count());
    } else { usleep(100000); }
  }
  META_PROBE_ASSERT(merged);
  META_PROBE_CALL(kv.get(reader, first, key, allocator, value));
  META_PROBE_ASSERT(value == old);
  META_PROBE_CALL(kv.get_for_update(reader, first, key, allocator, value));
  META_PROBE_ASSERT(value == next);
  META_PROBE_CALL(kv.rollback(reader));

  Tx holder, contender;
  META_PROBE_CALL(kv.begin(holder, deadline()));
  META_PROBE_CALL(kv.get_for_update(holder, first, key, allocator, value));
  META_PROBE_CALL(kv.begin(contender, common::ObTimeUtility::current_time() + 200000));
  ret = kv.put(contender, first, key, old);
  fprintf(stderr, "INSTANCE_META_PROBE_CONFLICT ret=%d\n", ret);
  META_PROBE_ASSERT(ret == common::OB_TRY_LOCK_ROW_CONFLICT || ret == common::OB_TIMEOUT
                    || ret == common::OB_ERR_EXCLUSIVE_LOCK_CONFLICT);
  META_PROBE_CALL(kv.rollback(contender));
  META_PROBE_CALL(kv.rollback(holder));
  // LOCK waits for the current holder, then reads its committed value.
  META_PROBE_CALL(kv.begin(holder, deadline()));
  META_PROBE_CALL(kv.get_for_update(holder, first, key, allocator, value));
  META_PROBE_CALL(kv.put(holder, first, key, old));
  META_PROBE_CALL(kv.begin(contender, common::ObTimeUtility::current_time() + 5000000));
  int holder_result = common::OB_SUCCESS;
  std::thread release_holder([&] {
    usleep(100000);
    holder_result = kv.commit(holder);
  });
  const int64_t lock_start = common::ObTimeUtility::current_time();
  const int lock_result = kv.get_for_update(contender, first, key, allocator, value);
  release_holder.join();
  META_PROBE_ASSERT(holder_result == common::OB_SUCCESS && lock_result == common::OB_SUCCESS);
  META_PROBE_ASSERT(value == old && common::ObTimeUtility::current_time() - lock_start >= 50000);
  META_PROBE_CALL(kv.put(contender, first, key, next));
  META_PROBE_CALL(kv.commit(contender));
  META_PROBE_CALL(kv.begin(holder, deadline()));
  META_PROBE_CALL(kv.get_for_update(holder, first, key, allocator, value));
  META_PROBE_CALL(kv.begin(contender, common::ObTimeUtility::current_time() + 50000));
  ret = kv.get_for_update(contender, first, key, allocator, value);
  META_PROBE_ASSERT(ret == common::OB_TIMEOUT || ret == common::OB_TRANS_TIMEOUT
                    || ret == common::OB_ERR_EXCLUSIVE_LOCK_CONFLICT);
  META_PROBE_CALL(kv.rollback(contender));
  META_PROBE_CALL(kv.rollback(holder));
  fprintf(stderr, "INSTANCE_META_LOCK_WAIT_PASS commit_handoff=1 deadline=1\n");
  Tx aborted;
  META_PROBE_CALL(kv.begin(aborted, deadline()));
  META_PROBE_CALL(kv.put(aborted, first, key, old));
  META_PROBE_CALL(kv.put(aborted, second, key, old));
  META_PROBE_CALL(kv.rollback(aborted));
  Tx verify;
  META_PROBE_CALL(kv.begin(verify, deadline()));
  META_PROBE_CALL(kv.get(verify, first, key, allocator, value));
  META_PROBE_ASSERT(value == next);
  META_PROBE_CALL(kv.get(verify, second, key, allocator, value));
  META_PROBE_ASSERT(value == large_value);
  int count = 0;
  storage::InstanceMetaStore::KeyRange range;
  META_PROBE_CALL(kv.scan(verify, first, range,
      [&](const common::ObString &k, const common::ObString &v, bool &) {
        ++count;
        return k == key && v == next ? common::OB_SUCCESS : common::OB_ERR_UNEXPECTED;
      }));
  META_PROBE_ASSERT(count == 1);
  META_PROBE_CALL(kv.commit(verify));
  Tx duplicate;
  META_PROBE_CALL(kv.begin(duplicate, deadline()));
  ret = kv.insert(duplicate, first, key, old);
  META_PROBE_ASSERT(ret == common::OB_ERR_PRIMARY_KEY_DUPLICATE);
  META_PROBE_CALL(kv.rollback(duplicate));
  Tx remove;
  META_PROBE_CALL(kv.begin(remove, deadline()));
  bool existed = false;
  META_PROBE_CALL(kv.erase(remove, second, key, existed));
  META_PROBE_ASSERT(existed);
  ret = kv.get(remove, second, key, allocator, value);
  META_PROBE_ASSERT(ret == common::OB_ENTRY_NOT_EXIST);
  META_PROBE_CALL(kv.rollback(remove));
  Tx visible_page_tx;
  META_PROBE_CALL(kv.begin(visible_page_tx, deadline()));
  rootserver::InstanceNamespaceMetadata visible_pages(kv, visible_page_tx);
  uint64_t visible_page_id = 0;
  META_PROBE_CALL(visible_pages.save_page(std::string("page\0payload", 12), visible_page_id));
  META_PROBE_CALL(kv.commit(visible_page_tx));
  fprintf(stderr, "INSTANCE_META_PROBE_PASS recovered=%d large_value=60000 cross_collection=1 snapshot=1 locked_current_read=1\n", recovered);
#undef META_PROBE_ASSERT
#undef META_PROBE_CALL
  return common::OB_SUCCESS;
}
