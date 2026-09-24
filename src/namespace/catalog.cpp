#include "namespace/catalog.h"

#include <algorithm>
#include <cstdio>

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

} // namespace ns
} // namespace oceanbase
