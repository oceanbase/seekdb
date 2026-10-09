#include "namespace/catalog.h"
#include <algorithm>
#include <cstdlib>
#include <iostream>
#include <map>
#include <random>
#include <set>
#include <stdexcept>
#include <thread>
#include <atomic>

using namespace oceanbase::ns;
static void check(bool ok, const char *message) {
  if (!ok) { throw std::runtime_error(message); }
}
class Pages final : public ICatalogPageStore {
public:
  std::map<uint64_t, std::string> pages;
  std::map<std::string, uint64_t> ids;
  std::map<uint64_t, int> reads;
  size_t writes = 0;
  int fail_write = -1;
  int fail_read = -1;
  int read(uint64_t id, std::string &out) override {
    if (fail_read == 0) { return 71; }
    if (fail_read > 0) { --fail_read; }
    ++reads[id];
    auto it = pages.find(id);
    if (it == pages.end()) { return 72; }
    out = it->second;
    return 0;
  }
  int write(const std::string &data, uint64_t &id) override {
    if (fail_write == 0) { return 73; }
    if (fail_write > 0) { --fail_write; }
    ++writes;
    check(data.size() <= NamespaceCatalogCodec::PAGE_BYTES, "oversized page");
    auto found = ids.find(data);
    if (found != ids.end()) { id = found->second; }
    else { id = pages.size() + 1; pages[id] = data; ids[data] = id; }
    return 0;
  }
};
static std::string key(uint64_t id) { return NamespaceCatalogCodec::object_key(id); }
static CatalogValue value(uint64_t id, int64_t cap = 0) {
  return {NamespaceCatalogCodec::encode_entry(0, 42, id, id + 100000), cap};
}
static std::set<uint64_t> reachable(NamespaceCatalogTree &tree, CatalogPageRef root,
                                  int *height = nullptr) {
  std::set<uint64_t> result;
  std::vector<std::pair<CatalogPageRef, int>> pending;
  if (root.page) { pending.push_back({root, 1}); }
  int leaf_depth = -1;
  while (!pending.empty()) {
    auto entry = pending.back(); pending.pop_back();
    if (!result.insert(entry.first.page).second) { continue; }
    CatalogNode node;
    check(tree.read_node(entry.first, node).ok(), "read reachable page");
    if (node.leaf) {
      check(!node.keys.empty(), "empty leaf stored");
      if (leaf_depth < 0) { leaf_depth = entry.second; }
      check(leaf_depth == entry.second, "unbalanced leaf depths");
    } else {
      for (auto child : node.children) { pending.push_back({child, entry.second + 1}); }
    }
  }
  if (height) { *height = leaf_depth; }
  return result;
}
static void no_garbage(Pages &pages, NamespaceCatalogTree &tree,
                       CatalogPageRef root, size_t previous) {
  auto live = reachable(tree, root);
  for (const auto &page : pages.pages) {
    if (page.first > previous) { check(live.count(page.first), "unreachable new page"); }
  }
}
static void expect(NamespaceCatalogTree &tree, CatalogPageRef root, uint64_t id,
                   const CatalogValue *wanted) {
  CatalogValue found;
  auto result = tree.find(root, key(id), found);
  if (!wanted) { check(result.error == CatalogTreeError::NOT_FOUND, "unexpected key"); }
  else {
    check(result.ok(), "missing key");
    check(found.data == wanted->data && found.cap == wanted->cap, "wrong value/cap");
  }
}
static void verify_model(NamespaceCatalogTree &tree, CatalogPageRef root,
                         const std::map<uint64_t, CatalogValue> &model) {
  std::map<std::string, CatalogValue> actual;
  std::vector<CatalogPageRef> pending;
  if (root.page) { pending.push_back(root); }
  while (!pending.empty()) {
    auto ref = pending.back(); pending.pop_back();
    CatalogNode node;
    check(tree.read_node(ref, node).ok(), "model traversal read");
    if (!node.leaf) {
      pending.insert(pending.end(), node.children.begin(), node.children.end());
    } else {
      for (size_t i = 0; i < node.keys.size(); ++i) {
        check(actual.emplace(node.keys[i], node.values[i]).second, "duplicate logical key");
      }
      for (size_t i : {size_t(0), node.keys.size() - 1}) {
        CatalogValue found;
        check(tree.find(root, node.keys[i], found).ok()
            && found.data == node.values[i].data && found.cap == node.values[i].cap,
            "leaf boundary routed incorrectly");
      }
    }
  }
  check(actual.size() == model.size(), "model size mismatch");
  auto entry = actual.begin();
  for (const auto &expected : model) {
    check(entry->first == key(expected.first) && entry->second.data == expected.second.data
        && entry->second.cap == expected.second.cap, "model value mismatch");
    ++entry;
  }
}
static void retention_workset() {
  Pages pages;
  NamespaceCatalogTree tree(pages);
  CatalogChanges changes;
  for (uint64_t i = 1; i <= 8000; ++i) {
    changes[key(i)] = {{NamespaceCatalogCodec::encode_source({42, i + 100000, 17, i, 0, 0}), i == 1 ? 30 : 0}, false};
  }
  CatalogPageRef root;
  check(tree.apply({}, changes, root).ok(), "retention fixture");
  std::map<uint64_t, PhysicalRetention> retained;
  pages.reads.clear();
  check(tree.retain_sources({root, {root.page, 50}, {root.page, 80}}, 100, 10000, retained).ok(), "retention roots");
  check(retained.size() == 8000, "retention lost physical object");
  for (const auto &item : retained) {
    check(item.second.create_transaction_id == 17
        && item.second.snapshot == (item.first == 100001 ? 30 : 50), "retention cap/incarnation");
  }
  for (const auto &item : pages.reads) { check(item.second <= 2, "shared page reread without stricter cap"); }
  oceanbase::storage::PhysicalSnapshotRetention plan;
  int64_t selected = 0;
  check(!plan.get(100001, 17, selected), "uninitialized retention plan accepted");
  plan.read_snapshot = 100; plan.new_source_floor = 70; plan.tablets = retained;
  check(plan.get(100001, 17, selected) && selected == 30, "fixed source snapshot lost");
  check(plan.get(100002, 17, selected) && selected == 50, "shared source snapshot lost");
  check(plan.get(9000001, 18, selected) && selected == 70, "future source floor lost");
  check(!plan.get(100001, 18, selected), "plan ignored incarnation conflict");
  plan.new_source_floor = 20;
  check(plan.get(100001, 17, selected) && selected == 20, "plan exceeded publication floor");
  plan.new_source_floor = 101;
  check(!plan.get(100001, 17, selected), "plan floor exceeded readable cut");
  retained.clear();
  check(tree.retain_sources({root}, 100, 7999, retained).error == CatalogTreeError::TOO_LARGE, "retention budget ignored");
  retained.clear();
  check(tree.retain_sources({root}, 100, 10000, retained).ok(), "retention reset after budget");
  CatalogPageRef conflict;
  check(tree.put({}, key(1), {NamespaceCatalogCodec::encode_source({42, 100001, 18, 1, 0, 0}), 0}, conflict).ok(), "conflict fixture");
  check(tree.retain_sources({conflict}, 200, 10000, retained).error == CatalogTreeError::CORRUPT, "two live incarnations accepted");
  retained.clear();
  pages.fail_read = 0;
  auto result = tree.retain_sources({root}, 100, 10000, retained);
  check(result.error == CatalogTreeError::STORE && result.store_error == 71, "retention store failure lost");
  pages.fail_read = -1;
  CatalogNode cycle;
  cycle.leaf = false; cycle.keys = {key(1)};
  cycle.children = {{9000000, 0}, root};
  pages.pages[9000000] = NamespaceCatalogCodec::encode_node(cycle);
  retained.clear();
  check(tree.retain_sources({{9000000, 0}}, 100, 10000, retained).error == CatalogTreeError::CORRUPT, "retention cycle accepted");
  std::cout << "PASS retention_workset partitions=8000 caps=1 incarnation=1 shared_pages=1 budget=1 cycle=1\n";
}

int main() {
  try {
    retention_workset();
    std::cout << std::unitbuf;
    Pages pages;
    NamespaceCatalogTree tree(pages);
    CatalogChanges changes;
    for (uint64_t i = 0; i < 8000; ++i) { changes[key(i)] = {value(i), false}; }
    CatalogPageRef parent;
    check(tree.apply({}, changes, parent).ok(), "bulk create");
    int height = 0;
    auto initial_live = reachable(tree, parent, &height);
    check(initial_live.size() == pages.pages.size(), "bulk creation garbage");
    check(pages.writes < 100, "bulk creation write amplification");
    std::cout << "8000 mappings: pages=" << initial_live.size() << " height=" << height << '\n';
    for (uint64_t i = 0; i < 8000; ++i) { auto v = value(i); expect(tree, parent, i, &v); }

    {
      std::string after;
      size_t total = 0;
      pages.reads.clear();
      for (;;) {
        std::vector<std::pair<std::string, CatalogValue>> batch;
        check(tree.scan({parent.page, 100}, after, 63, batch).ok(), "paged scan failed");
        check(batch.size() <= 63, "paged scan exceeded budget");
        for (const auto &entry : batch) {
          check(entry.first == key(total) && entry.second.data == value(total).data
              && entry.second.cap == 100, "paged scan order/value/cap");
          after = entry.first;
          ++total;
        }
        if (batch.size() < 63) { break; }
      }
      check(total == 8000, "paged scan lost entries");
      int reads = 0;
      for (const auto &entry : pages.reads) { reads += entry.second; }
      check(reads < 600, "paged scan reread the entire prefix");
      std::vector<std::pair<std::string, CatalogValue>> batch;
      pages.fail_read = 2;
      auto failed = tree.scan(parent, "", 8000, batch);
      check(!failed.ok() && batch.empty(), "paged scan published partial failure");
      pages.fail_read = -1;
      check(tree.scan({}, "", 64, batch).ok() && batch.empty(), "empty source scan");
      check(tree.scan(parent, key(7999), 64, batch).ok() && batch.empty(), "end source scan");
      std::cout << "PASS paged source scan: 8000 entries, bounded reads, cap, empty/error atomicity\n";
    }

    CatalogPageRef child{parent.page, 100};
    changes.clear();
    changes[key(100)] = {value(8100), false};
    changes[key(101)] = {value(8101), false};
    size_t previous = pages.pages.size();
    pages.reads.clear();
    size_t writes = pages.writes;
    check(tree.apply(child, changes, child).ok(), "fork update");
    check(pages.writes - writes == static_cast<size_t>(height), "shared path written twice");
    for (auto read : pages.reads) { check(read.second == 1, "old page read twice"); }
    no_garbage(pages, tree, child, previous);
    auto old = value(100), updated = value(8100), inherited = value(102, 100);
    expect(tree, parent, 100, &old);
    expect(tree, child, 100, &updated);
    expect(tree, child, 102, &inherited);
    CatalogPageRef grandchild{child.page, 200};
    auto updated_at_200 = value(8100, 200);
    expect(tree, grandchild, 100, &updated_at_200);
    expect(tree, grandchild, 102, &inherited);
    changes.clear();
    changes[key(9000)] = {value(9000), false};
    CatalogPageRef edited_grandchild;
    check(tree.apply(grandchild, changes, edited_grandchild).ok(), "grandchild edit");
    expect(tree, edited_grandchild, 100, &updated_at_200);
    expect(tree, edited_grandchild, 102, &inherited);

    // A large payload forces a multi-level tree with very different fanout.
    Pages wide_pages;
    NamespaceCatalogTree wide_tree(wide_pages);
    changes.clear();
    for (uint64_t i = 0; i < 8000; ++i) {
      changes[key(i)] = {{std::string(4000, char(i % 251)), 0}, false};
    }
    CatalogPageRef wide_root;
    check(wide_tree.apply({}, changes, wide_root).ok(), "wide multi-level create");
    int wide_height = 0;
    reachable(wide_tree, wide_root, &wide_height);
    check(wide_height >= 3, "no multi-level split coverage");
    no_garbage(wide_pages, wide_tree, wide_root, 0);
    std::cout << "wide mappings: pages=" << wide_pages.pages.size()
              << " height=" << wide_height << '\n';
    changes.clear();
    for (uint64_t i = 0; i < 7999; ++i) { changes[key(i)] = {{}, true}; }
    previous = wide_pages.pages.size();
    check(wide_tree.apply(wide_root, changes, wide_root).ok(), "bulk erase and root collapse");
    no_garbage(wide_pages, wide_tree, wide_root, previous);
    reachable(wide_tree, wide_root, &wide_height);
    check(wide_height == 1, "root not collapsed");
    check(wide_tree.remove(wide_root, key(7999), wide_root).ok() && !wide_root.page,
          "erase final key");
    auto absent = wide_tree.remove(wide_root, key(7999), wide_root);
    check(absent.error == CatalogTreeError::NOT_FOUND, "missing single erase status");

    std::map<uint64_t, CatalogValue> model;
    for (uint64_t i = 0; i < 8000; ++i) { model[i] = value(i); }
    CatalogPageRef current = parent;
    std::mt19937 random(97);
    for (int batch = 0; batch < 200; ++batch) {
      if (batch % 17 == 0) {
        current.cap = 1000 + batch;
        for (auto &entry : model) {
          entry.second.cap = NamespaceCatalogCodec::cap_min(entry.second.cap, current.cap);
        }
      }
      changes.clear();
      for (int i = 0; i < 100; ++i) {
        uint64_t id = random() % 12000;
        const bool erase = random() % 3 == 0;
        auto v = value(random());
        changes[key(id)] = {v, erase};
        if (erase) { model.erase(id); } else { model[id] = v; }
      }
      previous = pages.pages.size();
      pages.reads.clear();
      check(tree.apply(current, changes, current).ok(), "random edit batch");
      for (auto read : pages.reads) { check(read.second == 1, "batch reread old page"); }
      no_garbage(pages, tree, current, previous);
      verify_model(tree, current, model);
      for (uint64_t sample = 0; sample < 128; ++sample) {
        const uint64_t id = (sample * 97 + batch * 31) % 12000;
        auto found = model.find(id);
        expect(tree, current, id, found == model.end() ? nullptr : &found->second);
      }
      if (batch % 50 == 49) { std::cout << "random batches verified=" << batch + 1 << '\n'; }
    }
    // Validate errors before writes, retain caller's root on IO failure.
    changes.clear(); changes[key(1)] = {{std::string(16384, 'x'), 0}, false};
    CatalogPageRef sentinel{987654, 77};
    writes = pages.writes;
    check(tree.apply(parent, changes, sentinel).error == CatalogTreeError::TOO_LARGE,
          "oversized entry accepted");
    check(pages.writes == writes && sentinel.page == 987654, "error published root");
    changes.clear(); changes[std::string(513, 'k')] = {value(1), false};
    check(tree.apply(parent, changes, sentinel).error == CatalogTreeError::INVALID,
          "oversized key accepted");
    changes.clear(); changes[key(1)] = {value(1, -1), false};
    check(tree.apply(parent, changes, sentinel).error == CatalogTreeError::INVALID,
          "negative cap accepted");
    changes.clear(); changes[key(1)] = {value(999999), false};
    for (int fail = 0; fail < height; ++fail) {
      pages.fail_write = fail;
      auto result = tree.apply(parent, changes, sentinel);
      check(result.error == CatalogTreeError::STORE && result.store_error == 73,
            "write error not propagated");
      check(sentinel.page == 987654 && sentinel.cap == 77, "partial root published");
    }
    pages.fail_write = -1;
    pages.fail_read = 0;
    check(tree.apply(parent, changes, sentinel).store_error == 71, "read error lost");
    pages.fail_read = -1;
    for (uint64_t i = 0; i < 8000; ++i) { auto v = value(i); expect(tree, parent, i, &v); }
    changes.clear(); changes[key(1)] = {value(1), false};
    writes = pages.writes;
    check(tree.apply(parent, changes, sentinel).ok() && sentinel.page == parent.page,
          "no-op replaced root");
    check(writes == pages.writes, "no-op wrote pages");
    CatalogNode malformed;
    malformed.keys = {"b", "a"}; malformed.values.resize(2);
    CatalogNode decoded;
    check(!NamespaceCatalogCodec::decode_node(NamespaceCatalogCodec::encode_node(malformed),
                                               0, decoded), "unsorted page accepted");
    uint64_t object_size = 0;
    std::vector<uint64_t> chunks;
    check(NamespaceCatalogCodec::decode_object(
        NamespaceCatalogCodec::encode_object(120001, {11, 12, 13}), object_size, chunks)
        && object_size == 120001 && chunks == std::vector<uint64_t>({11, 12, 13}),
        "object manifest round trip");
    check(NamespaceCatalogCodec::decode_object(
        NamespaceCatalogCodec::encode_object(0, {}), object_size, chunks)
        && object_size == 0 && chunks.empty(), "empty object manifest");
    check(!NamespaceCatalogCodec::decode_object(
        NamespaceCatalogCodec::encode_object(60001, {1}), object_size, chunks),
        "object missing chunk accepted");
    check(!NamespaceCatalogCodec::decode_object(
        NamespaceCatalogCodec::encode_object(60000, {1, 2}), object_size, chunks),
        "object extra chunk accepted");
    check(!NamespaceCatalogCodec::decode_object(
        NamespaceCatalogCodec::encode_object(1, {0}), object_size, chunks),
        "object null chunk accepted");
    check(!NamespaceCatalogCodec::decode_object(
        NamespaceCatalogCodec::encode_object(1, {1}) + "x", object_size, chunks),
        "object trailing garbage accepted");
    CatalogTabletSource source{101, 4611686430744248401ULL, 4567, 801, 802, 803};
    const auto encoded_source = NamespaceCatalogCodec::encode_source(source);
    CatalogTabletSource decoded_source;
    check(NamespaceCatalogCodec::decode_source(encoded_source, decoded_source)
        && NamespaceCatalogCodec::encode_source(decoded_source) == encoded_source,
        "source identity and binding round trip");
    check(!NamespaceCatalogCodec::decode_source(encoded_source + "x", decoded_source),
        "source trailing bytes accepted");
    check(!NamespaceCatalogCodec::decode_source(encoded_source.substr(1), decoded_source),
        "source truncated bytes accepted");
    source.create_transaction_id = 0;
    check(!NamespaceCatalogCodec::decode_source(NamespaceCatalogCodec::encode_source(source), decoded_source),
        "source missing incarnation accepted");
    source.create_transaction_id = -1;
    check(!NamespaceCatalogCodec::decode_source(NamespaceCatalogCodec::encode_source(source), decoded_source),
        "source negative incarnation accepted");
    source.create_transaction_id = 4567;
    source.lob_meta_tablet_id = source.lob_piece_tablet_id = 0;
    check(NamespaceCatalogCodec::decode_source(NamespaceCatalogCodec::encode_source(source), decoded_source),
        "source without LOB rejected");
    CatalogChanges source_changes;
    source_changes[key(801)] = {{NamespaceCatalogCodec::encode_source(source), 0}, false};
    CatalogPageRef source_root;
    check(tree.apply({}, source_changes, source_root).ok(), "source root creation");
    const CatalogPageRef inherited_source{source_root.page, 900};
    source.physical_tablet_id += 1000000;
    source.create_transaction_id += 1;
    source_changes[key(801)] = {{NamespaceCatalogCodec::encode_source(source), 0}, false};
    CatalogPageRef materialized_source;
    check(tree.apply(inherited_source, source_changes, materialized_source).ok(), "source materialization");
    CatalogValue selected_source;
    check(tree.find(inherited_source, key(801), selected_source).ok()
        && selected_source.cap == 900
        && NamespaceCatalogCodec::decode_source(selected_source.data, decoded_source)
        && decoded_source.create_transaction_id == 4567, "fork source changed after materialization");
    check(tree.find(materialized_source, key(801), selected_source).ok()
        && selected_source.cap == 0
        && NamespaceCatalogCodec::decode_source(selected_source.data, decoded_source)
        && decoded_source.create_transaction_id == 4568, "materialized source kept fork cap");
    NamespaceCatalogViews views;
    CatalogRoots held_roots;
    held_roots.schema_version = 42;
    held_roots.directory = inherited_source;
    auto holder = views.hold(7, 1000, held_roots);
    auto duplicate = holder;
    auto worker_view = views.find(7, 1000);
    check(worker_view == holder, "parallel lookup did not share protected view");
    std::vector<NamespaceCatalogViews::Entry> entries;
    holder.reset();
    views.list(entries);
    check(entries.size() == 1 && entries[0].namespace_id == 7
        && entries[0].roots.directory.page == inherited_source.page, "copied view lost its root");
    duplicate.reset();
    views.list(entries);
    check(entries.size() == 1, "coordinator release lost parallel protection");
    worker_view.reset();
    views.list(entries);
    check(entries.empty() && !views.find(7, 1000), "released view leaked or resurrected");
    auto retention = std::make_shared<int>(42);
    std::weak_ptr<int> observed_retention = retention;
    auto retained_view = views.hold(7, 1001, held_roots, retention);
    retention.reset();
    views.list(entries);
    retained_view.reset();
    check(!observed_retention.expired(), "GC entry lost its independent retention");
    entries.clear();
    check(observed_retention.expired(), "released GC entries leaked storage retention");
    std::atomic<bool> done{false};
    std::thread churn([&] {
      for (int i = 0; i < 10000; ++i) {
        auto local_view = views.hold(8, i + 1, held_roots);
        auto child = local_view;
        local_view.reset();
      }
      done.store(true);
    });
    while (!done.load()) {
      views.list(entries);
      for (const auto &entry : entries) {
        check(entry.namespace_id == 8 && entry.roots.schema_version == 42,
              "concurrent view snapshot corrupt");
      }
    }
    churn.join();
    views.list(entries);
    check(entries.empty(), "concurrent released views leaked");
    NamespaceCatalogViews::Handle beyond_registry;
    {
      NamespaceCatalogViews temporary;
      beyond_registry = temporary.hold(9, 99, held_roots);
    }
    check(beyond_registry->entry().roots.schema_version == 42, "registry destruction invalidated view");
    beyond_registry.reset();
    std::cout << "PASS view holders: shared lifetime, concurrent collection, registry shutdown\n";
    std::cout << "PASS batch COW: snapshots, splits, collapse, random edits, no intermediate pages, IO failures, object manifests, typed physical sources\n";
  } catch (const std::exception &error) {
    std::cerr << "FAIL: " << error.what() << '\n'; return 1;
  }
}
