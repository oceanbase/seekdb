#include "namespace/catalog.h"

#include <algorithm>
#include <cstdio>
#include <limits>
#include <utility>

namespace oceanbase {
namespace ns {
struct NamespaceCatalogViews::State {
  struct Registered {
    Entry entry;
    std::weak_ptr<const View> handle;
    const View *identity;
  };
  std::mutex mutex;
  std::multimap<std::pair<uint64_t, int64_t>, Registered> views;
};

NamespaceCatalogViews::NamespaceCatalogViews() : state_(std::make_shared<State>()) {}

NamespaceCatalogViews::View::View(std::shared_ptr<State> state, const Entry &entry)
    : state_(std::move(state)), entry_(entry)
{}

NamespaceCatalogViews::View::~View()
{
  std::lock_guard<std::mutex> lock(state_->mutex);
  const auto range = state_->views.equal_range({entry_.namespace_id, entry_.snapshot});
  for (auto it = range.first; it != range.second; ++it) {
    if (it->second.identity == this) { state_->views.erase(it); break; }
  }
}

NamespaceCatalogViews::Handle NamespaceCatalogViews::hold(
    uint64_t namespace_id, int64_t snapshot, const CatalogRoots &roots,
    std::shared_ptr<const void> retention)
{
  if (namespace_id == 0 || snapshot <= 0 || roots.schema_version <= 0) { return {}; }
  Handle view(new View(state_, {namespace_id, snapshot, roots, std::move(retention)}));
  {
    std::lock_guard<std::mutex> lock(state_->mutex);
    state_->views.emplace(std::make_pair(namespace_id, snapshot),
        State::Registered{view->entry(), view, view.get()});
  }
  return view;
}

NamespaceCatalogViews::Handle NamespaceCatalogViews::find(uint64_t namespace_id, int64_t snapshot) const
{
  std::lock_guard<std::mutex> lock(state_->mutex);
  const auto range = state_->views.equal_range({namespace_id, snapshot});
  for (auto it = range.first; it != range.second; ++it) {
    if (auto view = it->second.handle.lock()) { return view; }
  }
  return {};
}

void NamespaceCatalogViews::list(std::vector<Entry> &entries) const
{
  entries.clear();
  std::lock_guard<std::mutex> lock(state_->mutex);
  entries.reserve(state_->views.size());
  for (const auto &view : state_->views) { entries.push_back(view.second.entry); }
}

namespace {
void number(std::string &data, uint64_t value)
{
  for (int i = 0; i < 8; ++i) { data += static_cast<char>(value >> (8 * i)); }
}

bool number(const std::string &data, size_t &pos, uint64_t &value)
{
  if (pos > data.size() || data.size() - pos < 8) { return false; }
  value = 0;
  for (int i = 0; i < 8; ++i) {
    value |= uint64_t(static_cast<unsigned char>(data[pos++])) << (8 * i);
  }
  return true;
}

void bytes(std::string &data, const std::string &value)
{
  number(data, value.size());
  data += value;
}

bool bytes(const std::string &data, size_t &pos, std::string &value)
{
  uint64_t size = 0;
  if (!number(data, pos, size) || size > data.size() - pos) { return false; }
  value.assign(data, pos, size);
  pos += size;
  return true;
}
} // namespace

bool CatalogRoots::valid_snapshot(uint64_t expected_id) const
{
  return expected_id != 0 && snapshot > 0
      && static_cast<uint64_t>(snapshot) == expected_id
      && snapshot_ref == expected_id && parent_ref < expected_id
      && ref_count > 0 && catalog.cap > 0 && directory.cap > 0
      && catalog.cap <= snapshot && directory.cap <= snapshot;
}

SnapshotForkResult NamespaceSnapshotLineage::fork(uint64_t parent_namespace_id,
    uint64_t child_id, CatalogRoots &roots, ISnapshotLineageStore &store)
{
  if (parent_namespace_id == 0 || child_id == 0 || roots.snapshot <= 0) {
    return {SnapshotForkError::INVALID, 0};
  }
  roots.catalog.cap = NamespaceCatalogCodec::cap_min(roots.catalog.cap, roots.snapshot);
  roots.directory.cap = NamespaceCatalogCodec::cap_min(roots.directory.cap, roots.snapshot);
  int ret = 0;
  if (roots.snapshot_ref != 0) {
    CatalogRoots parent;
    ret = store.load_for_update(roots.snapshot_ref, parent);
    if (ret != 0) { return {SnapshotForkError::STORE, ret}; }
    if (parent.ref_count == std::numeric_limits<int64_t>::max()) {
      return {SnapshotForkError::OVERFLOW, 0};
    }
    ret = store.increment_ref(roots.snapshot_ref);
    if (ret != 0) { return {SnapshotForkError::STORE, ret}; }
  }
  ret = store.insert_snapshot(roots);
  if (ret != 0) { return {SnapshotForkError::STORE, ret}; }
  roots.source = 0;
  roots.snapshot_ref = static_cast<uint64_t>(roots.snapshot);
  ret = store.attach_child(child_id, parent_namespace_id, roots);
  return ret == 0 ? SnapshotForkResult{} : SnapshotForkResult{SnapshotForkError::STORE, ret};
}

int NamespaceSnapshotLineage::release(uint64_t snapshot_id, ISnapshotLineageStore &store)
{
  int ret = 0;
  // Each child owns one parent reference. Lock from child to parent so the
  // whole chain can be released in the caller's transaction.
  while (ret == 0 && snapshot_id != 0) {
    CatalogRoots roots;
    ret = store.load_for_update(snapshot_id, roots);
    if (ret == 0) {
      if (roots.ref_count > 1) {
        ret = store.decrement_ref(snapshot_id);
        break;
      }
      ret = store.remove_snapshot(snapshot_id, roots);
      if (ret == 0) { snapshot_id = roots.parent_ref; }
    }
  }
  return ret;
}

int64_t NamespaceCatalogCodec::cap_min(int64_t a, int64_t b)
{
  return a == 0 ? b : b == 0 ? a : std::min(a, b);
}

std::string NamespaceCatalogCodec::object_key(uint64_t id)
{
  char buf[32];
  snprintf(buf, sizeof(buf), "%020lu", id);
  return buf;
}

std::string NamespaceCatalogCodec::encode_entry(uint64_t schema_object,
    uint64_t local_table, uint64_t local_tablet, uint64_t bound_tablet)
{
  std::string data;
  number(data, schema_object);
  number(data, local_table);
  number(data, local_tablet);
  number(data, bound_tablet);
  return data;
}

bool NamespaceCatalogCodec::decode_entry(const std::string &data,
    uint64_t &schema_object, uint64_t &local_table, uint64_t &local_tablet,
    uint64_t &bound_tablet)
{
  size_t pos = 0;
  return number(data, pos, schema_object) && number(data, pos, local_table)
      && number(data, pos, local_tablet) && number(data, pos, bound_tablet)
      && pos == data.size();
}

std::string NamespaceCatalogCodec::encode_node(const CatalogNode &node)
{
  std::string data;
  number(data, 1);
  number(data, node.leaf);
  number(data, node.keys.size());
  for (const auto &key : node.keys) { bytes(data, key); }
  if (node.leaf) {
    for (const auto &value : node.values) { bytes(data, value.data); number(data, value.cap); }
  } else {
    for (const auto &child : node.children) { number(data, child.page); number(data, child.cap); }
  }
  return data;
}

bool NamespaceCatalogCodec::decode_node(const std::string &data, int64_t cap,
    CatalogNode &node)
{
  uint64_t version = 0, leaf = 0, count = 0, value_cap = 0;
  size_t pos = 0;
  if (!number(data, pos, version) || version != 1
      || !number(data, pos, leaf) || leaf > 1
      || !number(data, pos, count) || data.size() > PAGE_BYTES
      || count > PAGE_BYTES / 16 || cap < 0) { return false; }
  node = CatalogNode();
  node.leaf = leaf;
  node.keys.resize(count);
  for (auto &key : node.keys) {
    if (!bytes(data, pos, key) || key.size() > MAX_KEY_BYTES) { return false; }
  }
  if (!std::is_sorted(node.keys.begin(), node.keys.end())
      || std::adjacent_find(node.keys.begin(), node.keys.end()) != node.keys.end()) {
    return false;
  }
  if (node.leaf) {
    node.values.resize(count);
    for (auto &value : node.values) {
      if (!bytes(data, pos, value.data) || !number(data, pos, value_cap)
          || value_cap > static_cast<uint64_t>(std::numeric_limits<int64_t>::max())) {
        return false;
      }
      value.cap = cap_min(value_cap, cap);
    }
  } else {
    node.children.resize(count + 1);
    for (auto &child : node.children) {
      if (!number(data, pos, child.page) || !child.page
          || !number(data, pos, value_cap)
          || value_cap > static_cast<uint64_t>(std::numeric_limits<int64_t>::max())) {
        return false;
      }
      child.cap = cap_min(value_cap, cap);
    }
  }
  return pos == data.size();
}

std::string NamespaceCatalogCodec::encode_source(const CatalogTabletSource &source)
{
  std::string data;
  number(data, source.table_id);
  number(data, source.physical_tablet_id);
  number(data, static_cast<uint64_t>(source.create_transaction_id));
  number(data, source.data_tablet_id);
  number(data, source.lob_meta_tablet_id);
  number(data, source.lob_piece_tablet_id);
  return data;
}

bool NamespaceCatalogCodec::decode_source(const std::string &data, CatalogTabletSource &source)
{
  if (data.size() != 48) { return false; }
  CatalogTabletSource decoded;
  uint64_t identity = 0;
  size_t pos = 0;
  if (!number(data, pos, decoded.table_id) || !number(data, pos, decoded.physical_tablet_id)
      || !number(data, pos, identity) || identity > uint64_t(INT64_MAX)
      || !number(data, pos, decoded.data_tablet_id)
      || !number(data, pos, decoded.lob_meta_tablet_id)
      || !number(data, pos, decoded.lob_piece_tablet_id)) { return false; }
  decoded.create_transaction_id = static_cast<int64_t>(identity);
  if (!decoded.is_valid()) { return false; }
  source = decoded;
  return true;
}

std::string NamespaceCatalogCodec::encode_object(uint64_t size,
    const std::vector<uint64_t> &chunks)
{
  std::string data;
  number(data, 0x4e534f424a454354ULL); // NSOBJECT; distinct from tree node version.
  number(data, size);
  number(data, chunks.size());
  for (uint64_t chunk : chunks) { number(data, chunk); }
  return data;
}

bool NamespaceCatalogCodec::decode_object(const std::string &data,
    uint64_t &size, std::vector<uint64_t> &chunks)
{
  size_t pos = 0;
  uint64_t magic = 0, count = 0, length = 0;
  if (!number(data, pos, magic) || magic != 0x4e534f424a454354ULL
      || !number(data, pos, length) || !number(data, pos, count)
      || count > MAX_OBJECT_CHUNKS || data.size() - pos != count * 8
      || length > count * OBJECT_CHUNK_BYTES
      || (count > 0 && length <= (count - 1) * OBJECT_CHUNK_BYTES)) { return false; }
  std::vector<uint64_t> decoded;
  decoded.reserve(count);
  for (uint64_t i = 0; i < count; ++i) {
    uint64_t chunk = 0;
    if (!number(data, pos, chunk) || chunk == 0) { return false; }
    decoded.push_back(chunk);
  }
  size = length;
  chunks.swap(decoded);
  return true;
}

CatalogTreeResult NamespaceCatalogTree::read_node(CatalogPageRef ref, CatalogNode &node)
{
  if (!ref.page) { node = CatalogNode(); return {}; }
  std::string data;
  const int error = store_.read(ref.page, data);
  if (error != 0) { return CatalogTreeResult::from_store(error); }
  return NamespaceCatalogCodec::decode_node(data, ref.cap, node)
      ? CatalogTreeResult{} : CatalogTreeResult{CatalogTreeError::CORRUPT, 0};
}

CatalogTreeResult NamespaceCatalogTree::save_node(const CatalogNode &node, CatalogPageRef &ref)
{
  const std::string data = NamespaceCatalogCodec::encode_node(node);
  if (data.size() > NamespaceCatalogCodec::PAGE_BYTES) {
    return {CatalogTreeError::TOO_LARGE, 0};
  }
  ref.cap = 0;
  const int error = store_.write(data, ref.page);
  return error == 0 ? CatalogTreeResult{} : CatalogTreeResult::from_store(error);
}

CatalogTreeResult NamespaceCatalogTree::find(CatalogPageRef ref,
    const std::string &key, CatalogValue &value)
{
  for (int depth = 0; depth < 32; ++depth) {
    if (!ref.page) { return {CatalogTreeError::NOT_FOUND, 0}; }
    CatalogNode node;
    const CatalogTreeResult result = read_node(ref, node);
    if (!result.ok()) { return result; }
    if (node.leaf) {
      const auto it = std::lower_bound(node.keys.begin(), node.keys.end(), key);
      if (it == node.keys.end() || *it != key) {
        return {CatalogTreeError::NOT_FOUND, 0};
      }
      value = node.values[it - node.keys.begin()];
      return {};
    }
    ref = node.children[std::upper_bound(node.keys.begin(), node.keys.end(), key)
        - node.keys.begin()];
  }
  return {CatalogTreeError::TOO_DEEP, 0};
}

CatalogTreeResult NamespaceCatalogTree::stage_leaves(const CatalogNode &node,
    const std::string &lower, std::vector<Branch> &out)
{
  for (size_t begin = 0; begin < node.keys.size();) {
    CatalogNode leaf;
    size_t bytes = 24;
    size_t end = begin;
    while (end < node.keys.size()) {
      const size_t entry_bytes = 24 + node.keys[end].size() + node.values[end].data.size();
      if (bytes + entry_bytes > NamespaceCatalogCodec::PAGE_BYTES) { break; }
      bytes += entry_bytes;
      leaf.keys.push_back(node.keys[end]);
      leaf.values.push_back(node.values[end]);
      ++end;
    }
    if (end == begin) { return {CatalogTreeError::TOO_LARGE, 0}; }
    Branch branch;
    branch.lower = begin == 0 ? lower : leaf.keys.front();
    branch.pending = std::make_shared<PendingPage>();
    branch.pending->node = std::move(leaf);
    out.push_back(std::move(branch));
    begin = end;
  }
  return {};
}

CatalogTreeResult NamespaceCatalogTree::stage_branches(
    const std::vector<Branch> &children, std::vector<Branch> &out)
{
  for (size_t begin = 0; begin < children.size();) {
    CatalogNode node;
    node.leaf = false;
    node.children.push_back(children[begin].ref);
    size_t bytes = 24 + 16;
    size_t end = begin + 1;
    while (end < children.size()) {
      const size_t entry_bytes = 24 + children[end].lower.size();
      if (bytes + entry_bytes > NamespaceCatalogCodec::PAGE_BYTES) { break; }
      bytes += entry_bytes;
      node.keys.push_back(children[end].lower);
      node.children.push_back(children[end].ref);
      ++end;
    }
    Branch branch;
    branch.lower = children[begin].lower;
    branch.pending = std::make_shared<PendingPage>();
    branch.pending->node = std::move(node);
    branch.pending->children.assign(children.begin() + begin, children.begin() + end);
    out.push_back(std::move(branch));
    begin = end;
  }
  return {};
}

CatalogTreeResult NamespaceCatalogTree::apply_path(CatalogPageRef root,
    const std::string &lower, ChangeIterator begin, ChangeIterator end,
    std::vector<Branch> &out, int depth)
{
  if (depth >= 32) { return {CatalogTreeError::TOO_DEEP, 0}; }
  if (begin == end) {
    if (root.page != 0) { out.push_back({root, lower, nullptr}); }
    return {};
  }
  CatalogNode node;
  auto result = read_node(root, node);
  if (!result.ok()) { return result; }
  bool changed = false;
  if (node.leaf) {
    CatalogNode merged;
    size_t i = 0;
    auto change = begin;
    while (i < node.keys.size() || change != end) {
      if (change == end || (i < node.keys.size() && node.keys[i] < change->first)) {
        merged.keys.push_back(std::move(node.keys[i]));
        merged.values.push_back(std::move(node.values[i++]));
      } else {
        const bool exists = i < node.keys.size() && node.keys[i] == change->first;
        if (change->second.erase) {
          changed = changed || exists;
        } else {
          changed = changed || !exists
              || node.values[i].data != change->second.value.data
              || node.values[i].cap != change->second.value.cap;
          merged.keys.push_back(change->first);
          merged.values.push_back(change->second.value);
        }
        if (exists) { ++i; }
        ++change;
      }
    }
    if (changed) { return stage_leaves(merged, lower, out); }
  } else {
    std::vector<Branch> children;
    auto change = begin;
    for (size_t i = 0; i < node.children.size(); ++i) {
      auto limit = change;
      if (i == node.keys.size()) { limit = end; }
      else { while (limit != end && limit->first < node.keys[i]) { ++limit; } }
      const size_t before = children.size();
      result = apply_path(node.children[i], i == 0 ? lower : node.keys[i - 1],
          change, limit, children, depth + 1);
      if (!result.ok()) { return result; }
      changed = changed || children.size() != before + 1
          || children[before].pending != nullptr
          || children[before].ref.page != node.children[i].page
          || children[before].ref.cap != node.children[i].cap;
      change = limit;
    }
    if (changed) {
      // A deleted first child must not leave its former key range unroutable.
      if (!children.empty()) { children.front().lower = lower; }
      return stage_branches(children, out);
    }
  }
  if (root.page != 0) { out.push_back({root, lower, nullptr}); }
  return {};
}

CatalogTreeResult NamespaceCatalogTree::apply(CatalogPageRef root,
    const CatalogChanges &changes, CatalogPageRef &next)
{
  if (root.cap < 0) { return {CatalogTreeError::INVALID, 0}; }
  // Validate the whole batch before persisting any replacements.
  for (const auto &change : changes) {
    if (change.first.size() > NamespaceCatalogCodec::MAX_KEY_BYTES
        || (!change.second.erase && change.second.value.cap < 0)) {
      return {CatalogTreeError::INVALID, 0};
    }
    if (!change.second.erase && change.second.value.data.size()
        > NamespaceCatalogCodec::PAGE_BYTES - 48 - change.first.size()) {
      return {CatalogTreeError::TOO_LARGE, 0};
    }
  }
  if (changes.empty()) { next = root; return {}; }
  std::vector<Branch> branches;
  auto result = apply_path(root, std::string(), changes.begin(), changes.end(), branches, 0);
  if (!result.ok()) { return result; }
  while (branches.size() > 1) {
    std::vector<Branch> parents;
    result = stage_branches(branches, parents);
    if (!result.ok()) { return result; }
    branches.swap(parents);
  }
  CatalogPageRef staged;
  if (!branches.empty()) {
    Branch root_branch = std::move(branches.front());
    // Internal nodes keep their level during editing. Only the final root may
    // collapse; discarded pending nodes have never reached the page store.
    for (int depth = 0; ; ++depth) {
      if (depth >= 32) { return {CatalogTreeError::TOO_DEEP, 0}; }
      if (root_branch.pending != nullptr) {
        if (root_branch.pending->node.leaf
            || root_branch.pending->children.size() != 1) { break; }
        Branch child = root_branch.pending->children.front();
        root_branch = std::move(child);
      } else {
        if (root_branch.ref.page == root.page && root_branch.ref.cap == root.cap) { break; }
        CatalogNode node;
        result = read_node(root_branch.ref, node);
        if (!result.ok()) { return result; }
        if (node.leaf || node.children.size() != 1) { break; }
        root_branch.ref = node.children.front();
      }
    }
    result = persist(root_branch, staged, 0);
    if (!result.ok()) { return result; }
  }
  next = staged;
  return {};
}

CatalogTreeResult NamespaceCatalogTree::persist(Branch &branch,
    CatalogPageRef &ref, int depth)
{
  if (depth >= 32) { return {CatalogTreeError::TOO_DEEP, 0}; }
  if (branch.pending == nullptr) { ref = branch.ref; return {}; }
  auto &page = *branch.pending;
  for (size_t i = 0; i < page.children.size(); ++i) {
    const auto result = persist(page.children[i], page.node.children[i], depth + 1);
    if (!result.ok()) { return result; }
  }
  return save_node(page.node, ref);
}

CatalogTreeResult NamespaceCatalogTree::put(CatalogPageRef root,
    const std::string &key, CatalogValue value, CatalogPageRef &next)
{
  CatalogChanges changes;
  changes.emplace(key, CatalogChange{std::move(value), false});
  return apply(root, changes, next);
}

CatalogTreeResult NamespaceCatalogTree::remove(CatalogPageRef root,
    const std::string &key, CatalogPageRef &next)
{
  CatalogChanges changes;
  changes.emplace(key, CatalogChange{CatalogValue(), true});
  CatalogPageRef staged;
  const auto result = apply(root, changes, staged);
  if (!result.ok()) { return result; }
  next = staged;
  return staged.page == root.page && staged.cap == root.cap
      ? CatalogTreeResult{CatalogTreeError::NOT_FOUND, 0} : result;
}

std::vector<ExceptionDeltaAction> NamespaceExceptionDelta::plan(
    const std::map<uint64_t, uint64_t> &previous,
    const std::map<uint64_t, uint64_t> &current)
{
  std::vector<ExceptionDeltaAction> actions;
  actions.reserve(previous.size() + current.size());
  for (const auto &tablet : previous) {
    if (current.count(tablet.first) == 0) {
      actions.push_back({ExceptionDeltaKind::TOMBSTONE, tablet.first, tablet.second});
    }
  }
  for (const auto &tablet : current) {
    const auto old = previous.find(tablet.first);
    if (old == previous.end()) {
      actions.push_back({ExceptionDeltaKind::PROBE_NEW, tablet.first, tablet.second});
    } else if (old->second != tablet.second) {
      actions.push_back({ExceptionDeltaKind::UPDATE_TABLE, tablet.first, tablet.second});
    }
  }
  return actions;
}

bool NamespaceControlState::chain_link(uint64_t namespace_id,
    uint64_t &parent, int64_t &fork_cap) const
{
  std::shared_lock<std::shared_mutex> lock(chain_mutex_);
  const auto it = chain_links_.find(namespace_id);
  if (it == chain_links_.end()) { return false; }
  parent = it->second.first;
  fork_cap = it->second.second;
  return true;
}

void NamespaceControlState::remember_chain_link(uint64_t namespace_id,
    uint64_t parent, int64_t fork_cap)
{
  std::unique_lock<std::shared_mutex> lock(chain_mutex_);
  chain_links_[namespace_id] = {parent, fork_cap};
}

void NamespaceControlState::forget_chain_link(uint64_t namespace_id)
{
  std::unique_lock<std::shared_mutex> lock(chain_mutex_);
  chain_links_.erase(namespace_id);
}

int NamespaceControlState::load_exceptions(uint64_t namespace_id,
    IExceptionLoader &loader, bool reload)
{
  std::lock_guard<std::mutex> lock(exceptions_mutex_);
  ExceptionSet &set = exception_sets_[namespace_id];
  if (set.loaded && !reload) { return 0; }
  ExceptionSet refreshed;
  struct RowSink final : IExceptionLoader::IRowSink {
    explicit RowSink(ExceptionSet &set) : set_(set) {}
    void add(const NamespaceExceptionRow &row) override {
      if (row.kind == 0) {
        set_.owned[row.tablet] = row.table;
      } else {
        set_.tombstoned.insert(row.tablet);
        set_.owned.erase(row.tablet);
      }
    }
    ExceptionSet &set_;
  } sink(refreshed);
  const int error = loader.load(namespace_id, sink);
  if (error != 0) {
    exception_sets_.erase(namespace_id);
  } else {
    refreshed.loaded = true;
    set = std::move(refreshed);
  }
  return error;
}

void NamespaceControlState::clear_exceptions()
{
  std::lock_guard<std::mutex> lock(exceptions_mutex_);
  exception_sets_.clear();
}

bool NamespaceControlState::owned(uint64_t namespace_id,
    uint64_t local_tablet, uint64_t *table) const
{
  std::lock_guard<std::mutex> lock(exceptions_mutex_);
  const auto it = exception_sets_.find(namespace_id);
  if (it == exception_sets_.end() || !it->second.loaded) { return false; }
  const auto owned = it->second.owned.find(local_tablet);
  if (owned == it->second.owned.end()) { return false; }
  if (table != nullptr) { *table = owned->second; }
  return true;
}

bool NamespaceControlState::tombstoned(uint64_t namespace_id,
    uint64_t local_tablet) const
{
  std::lock_guard<std::mutex> lock(exceptions_mutex_);
  const auto it = exception_sets_.find(namespace_id);
  return it != exception_sets_.end() && it->second.loaded
      && it->second.tombstoned.count(local_tablet) != 0;
}

void NamespaceControlState::apply_owned(uint64_t namespace_id,
    uint64_t local_tablet, uint64_t table)
{
  std::lock_guard<std::mutex> lock(exceptions_mutex_);
  const auto it = exception_sets_.find(namespace_id);
  // Physical IDs are not reused after a committed DROP. A delayed creator's
  // cache publication must not revive a tombstone loaded after that DROP.
  if (it != exception_sets_.end() && it->second.loaded
      && it->second.tombstoned.count(local_tablet) == 0) {
    it->second.owned[local_tablet] = table;
  }
}

void NamespaceControlState::drop_exceptions(uint64_t namespace_id)
{
  std::lock_guard<std::mutex> lock(exceptions_mutex_);
  exception_sets_.erase(namespace_id);
}

} // namespace ns
} // namespace oceanbase
