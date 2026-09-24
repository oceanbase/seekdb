#include "namespace/catalog.h"

#include <algorithm>
#include <cstdio>
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

} // namespace ns
} // namespace oceanbase
