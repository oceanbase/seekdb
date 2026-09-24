#include "namespace/catalog.h"

#include <algorithm>
#include <cstdio>
#include <limits>
#include <utility>

namespace oceanbase {
namespace ns {
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
      || !number(data, pos, count) || count > FANOUT) { return false; }
  node = CatalogNode();
  node.leaf = leaf;
  node.keys.resize(count);
  for (auto &key : node.keys) { if (!bytes(data, pos, key)) { return false; } }
  if (!std::is_sorted(node.keys.begin(), node.keys.end())
      || std::adjacent_find(node.keys.begin(), node.keys.end()) != node.keys.end()) {
    return false;
  }
  if (node.leaf) {
    node.values.resize(count);
    for (auto &value : node.values) {
      if (!bytes(data, pos, value.data) || !number(data, pos, value_cap)) { return false; }
      value.cap = cap_min(value_cap, cap);
    }
  } else {
    node.children.resize(count + 1);
    for (auto &child : node.children) {
      if (!number(data, pos, child.page) || !child.page
          || !number(data, pos, value_cap)) { return false; }
      child.cap = cap_min(value_cap, cap);
    }
  }
  return pos == data.size();
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

CatalogTreeResult NamespaceCatalogTree::put_path(CatalogPageRef root,
    const std::string &key, CatalogValue value, Split &out, int depth)
{
  if (depth > 32) { return {CatalogTreeError::TOO_DEEP, 0}; }
  CatalogNode node;
  CatalogTreeResult result = read_node(root, node);
  if (!result.ok()) { return result; }
  if (node.leaf) {
    const size_t i = std::lower_bound(node.keys.begin(), node.keys.end(), key)
        - node.keys.begin();
    if (i < node.keys.size() && node.keys[i] == key) {
      node.values[i] = std::move(value);
    } else {
      node.keys.insert(node.keys.begin() + i, key);
      node.values.insert(node.values.begin() + i, std::move(value));
    }
  } else {
    const size_t i = std::upper_bound(node.keys.begin(), node.keys.end(), key)
        - node.keys.begin();
    Split child;
    result = put_path(node.children[i], key, std::move(value), child, depth + 1);
    if (!result.ok()) { return result; }
    node.children[i] = child.left;
    if (child.right.page) {
      node.keys.insert(node.keys.begin() + i, child.separator);
      node.children.insert(node.children.begin() + i + 1, child.right);
    }
  }
  if (node.keys.size() <= NamespaceCatalogCodec::FANOUT) {
    return save_node(node, out.left);
  }
  const size_t mid = node.keys.size() / 2;
  CatalogNode right;
  right.leaf = node.leaf;
  out.separator = node.keys[mid];
  if (node.leaf) {
    right.keys.assign(node.keys.begin() + mid, node.keys.end());
    right.values.assign(node.values.begin() + mid, node.values.end());
    node.keys.resize(mid);
    node.values.resize(mid);
  } else {
    right.keys.assign(node.keys.begin() + mid + 1, node.keys.end());
    right.children.assign(node.children.begin() + mid + 1, node.children.end());
    node.keys.resize(mid);
    node.children.resize(mid + 1);
  }
  result = save_node(node, out.left);
  return result.ok() ? save_node(right, out.right) : result;
}

CatalogTreeResult NamespaceCatalogTree::put(CatalogPageRef root,
    const std::string &key, CatalogValue value, CatalogPageRef &next)
{
  Split split;
  CatalogTreeResult result = put_path(root, key, std::move(value), split, 0);
  if (!result.ok()) { return result; }
  if (!split.right.page) { next = split.left; return {}; }
  CatalogNode node;
  node.leaf = false;
  node.keys.push_back(split.separator);
  node.children = {split.left, split.right};
  return save_node(node, next);
}

CatalogTreeResult NamespaceCatalogTree::first_key(CatalogPageRef ref, std::string &key)
{
  for (int depth = 0; depth < 32; ++depth) {
    CatalogNode node;
    const CatalogTreeResult result = read_node(ref, node);
    if (!result.ok()) { return result; }
    if (node.leaf) {
      if (node.keys.empty()) { return {CatalogTreeError::CORRUPT, 0}; }
      key = node.keys.front();
      return {};
    }
    if (node.children.empty()) { return {CatalogTreeError::CORRUPT, 0}; }
    ref = node.children.front();
  }
  return {CatalogTreeError::TOO_DEEP, 0};
}

CatalogTreeResult NamespaceCatalogTree::remove_path(CatalogPageRef root,
    const std::string &key, CatalogPageRef &next, bool &found,
    std::string &minimum, int depth)
{
  if (depth > 32) { return {CatalogTreeError::TOO_DEEP, 0}; }
  if (!root.page) { next = root; return {}; }
  CatalogNode node;
  CatalogTreeResult result = read_node(root, node);
  if (!result.ok()) { return result; }
  if (node.leaf) {
    const size_t i = std::lower_bound(node.keys.begin(), node.keys.end(), key)
        - node.keys.begin();
    if (i == node.keys.size() || node.keys[i] != key) {
      next = root;
    } else {
      found = true;
      node.keys.erase(node.keys.begin() + i);
      node.values.erase(node.values.begin() + i);
      if (node.keys.empty()) { next = CatalogPageRef(); }
      else {
        minimum = node.keys.front();
        result = save_node(node, next);
      }
    }
  } else {
    const size_t i = std::upper_bound(node.keys.begin(), node.keys.end(), key)
        - node.keys.begin();
    CatalogPageRef child;
    std::string child_minimum;
    result = remove_path(node.children[i], key, child, found, child_minimum,
                         depth + 1);
    if (!result.ok()) {
    } else if (!found) {
      next = root;
    } else {
      if (child.page) {
        node.children[i] = child;
        if (i > 0) { node.keys[i - 1] = child_minimum; }
      } else {
        node.children.erase(node.children.begin() + i);
        node.keys.erase(node.keys.begin() + (i == 0 ? 0 : i - 1));
      }
      if (node.children.empty()) {
        result = {CatalogTreeError::CORRUPT, 0};
      } else {
        result = first_key(node.children.front(), minimum);
        if (result.ok()) {
          if (node.keys.empty()) { next = node.children.front(); }
          else { result = save_node(node, next); }
        }
      }
    }
  }
  return result;
}

CatalogTreeResult NamespaceCatalogTree::remove(CatalogPageRef root,
    const std::string &key, CatalogPageRef &next)
{
  bool found = false;
  std::string minimum;
  const CatalogTreeResult result = remove_path(root, key, next, found, minimum, 0);
  return result.ok() && !found
      ? CatalogTreeResult{CatalogTreeError::NOT_FOUND, 0} : result;
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
    IExceptionLoader &loader)
{
  std::lock_guard<std::mutex> lock(exceptions_mutex_);
  ExceptionSet &set = exception_sets_[namespace_id];
  if (set.loaded) { return 0; }
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
  } sink(set);
  const int error = loader.load(namespace_id, sink);
  if (error != 0) {
    exception_sets_.erase(namespace_id);
  } else {
    set.loaded = true;
  }
  return error;
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
  if (it != exception_sets_.end() && it->second.loaded) {
    it->second.owned[local_tablet] = table;
    it->second.tombstoned.erase(local_tablet);
  }
}

void NamespaceControlState::drop_exceptions(uint64_t namespace_id)
{
  std::lock_guard<std::mutex> lock(exceptions_mutex_);
  exception_sets_.erase(namespace_id);
}

} // namespace ns
} // namespace oceanbase
