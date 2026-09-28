/*
 * Copyright (c) 2025 OceanBase.
 *
 * Licensed under the Apache License, Version 2.0 (the "License");
 * you may not use this file except in compliance with the License.
 * You may obtain a copy of the License at
 *
 *     http://www.apache.org/licenses/LICENSE-2.0
 *
 * Unless required by applicable law or agreed to in writing, software
 * distributed under the License is distributed on an "AS IS" BASIS,
 * WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
 * See the License for the specific language governing permissions and
 * limitations under the License.
 */

#define USING_LOG_PREFIX RS
#include "rootserver/fork_table/instance_namespace_metadata.h"
#include "namespace/namespace.h"
#include "lib/hash_func/murmur_hash.h"
#include "lib/allocator/ob_allocator.h"
#include <cstring>
#include <utility>

namespace oceanbase
{
namespace rootserver
{
namespace
{
using namespace common;
using storage::MetaCollection;
using storage::InstanceMetaStore;

void append_meta_u64(std::string &out, uint64_t value)
{
  for (int i = 0; i < 8; ++i) { out.push_back(static_cast<char>(value >> (56 - i * 8))); }
}

bool read_meta_u64(const std::string &data, size_t &offset, uint64_t &value)
{
  if (offset > data.size() || data.size() - offset < 8) { return false; }
  value = 0;
  for (int i = 0; i < 8; ++i) {
    value = (value << 8) | static_cast<unsigned char>(data[offset++]);
  }
  return true;
}

void append_meta_i64(std::string &out, int64_t value)
{
  append_meta_u64(out, static_cast<uint64_t>(value));
}

bool read_meta_i64(const std::string &data, size_t &offset, int64_t &value)
{
  uint64_t bits = 0;
  if (!read_meta_u64(data, offset, bits)) { return false; }
  static_assert(sizeof(bits) == sizeof(value), "metadata integer widths differ");
  std::memcpy(&value, &bits, sizeof(value));
  return true;
}

void append_meta_bytes(std::string &out, const std::string &value)
{
  append_meta_u64(out, value.size());
  out.append(value);
}

bool read_meta_bytes(const std::string &data, size_t &offset, std::string &value,
                     size_t max_size)
{
  uint64_t size = 0;
  if (!read_meta_u64(data, offset, size) || size > max_size
      || offset > data.size() || size > data.size() - offset) { return false; }
  value.assign(data, offset, size);
  offset += size;
  return true;
}

std::string id_key(uint64_t id)
{
  std::string key;
  append_meta_u64(key, id);
  return key;
}

std::string exception_key(uint64_t ns_id, uint64_t local_tablet)
{
  std::string key = id_key(ns_id);
  append_meta_u64(key, local_tablet);
  return key;
}

ObString meta_bytes(const std::string &value)
{
  return ObString(value.size(), value.data());
}

int get_value(InstanceMetaStore &store, InstanceMetaStore::Transaction &tx,
              MetaCollection collection, const std::string &key,
              std::string &value, bool lock)
{
  ObArenaAllocator allocator(ObMemAttr("InstanceMetaGet"));
  ObString result;
  const int ret = lock ? store.get_for_update(tx, collection, meta_bytes(key), allocator, result)
                       : store.get(tx, collection, meta_bytes(key), allocator, result);
  if (ret == OB_SUCCESS) {
    if (result.empty()) { value.clear(); }
    else { value.assign(result.ptr(), result.length()); }
  }
  return ret;
}

int put_value(InstanceMetaStore &store, InstanceMetaStore::Transaction &tx,
              MetaCollection collection, const std::string &key,
              const std::string &value, bool insert)
{
  return insert ? store.insert(tx, collection, meta_bytes(key), meta_bytes(value))
                : store.put(tx, collection, meta_bytes(key), meta_bytes(value));
}

int erase_value(InstanceMetaStore &store, InstanceMetaStore::Transaction &tx,
                MetaCollection collection, const std::string &key)
{
  bool existed = false;
  const int ret = store.erase(tx, collection, meta_bytes(key), existed);
  return ret == OB_SUCCESS && !existed ? OB_ENTRY_NOT_EXIST : ret;
}

void append_roots(std::string &out, const ns::CatalogRoots &roots)
{
  append_meta_u64(out, roots.source);
  append_meta_u64(out, roots.catalog.page);
  append_meta_i64(out, roots.catalog.cap);
  append_meta_u64(out, roots.directory.page);
  append_meta_i64(out, roots.directory.cap);
  append_meta_i64(out, roots.snapshot);
  append_meta_i64(out, roots.schema_version);
  append_meta_u64(out, roots.snapshot_ref);
  append_meta_u64(out, roots.parent_ref);
  append_meta_i64(out, roots.ref_count);
  append_meta_i64(out, roots.state);
  append_meta_i64(out, roots.active_schema_changes);
  append_meta_i64(out, roots.pending_schema_version);
}

bool read_roots(const std::string &data, size_t &offset, ns::CatalogRoots &roots)
{
  return read_meta_u64(data, offset, roots.source)
      && read_meta_u64(data, offset, roots.catalog.page)
      && read_meta_i64(data, offset, roots.catalog.cap)
      && read_meta_u64(data, offset, roots.directory.page)
      && read_meta_i64(data, offset, roots.directory.cap)
      && read_meta_i64(data, offset, roots.snapshot)
      && read_meta_i64(data, offset, roots.schema_version)
      && read_meta_u64(data, offset, roots.snapshot_ref)
      && read_meta_u64(data, offset, roots.parent_ref)
      && read_meta_i64(data, offset, roots.ref_count)
      && read_meta_i64(data, offset, roots.state)
      && read_meta_i64(data, offset, roots.active_schema_changes)
      && read_meta_i64(data, offset, roots.pending_schema_version);
}

int encode_namespace(const InstanceNamespaceRecord &record, std::string &value)
{
  if (!ns::NamespaceObjectKey{record.id, 1}.is_valid()
      || (record.name.empty() && record.roots.state != 2)
      || record.name.size() > 128
      || record.roots.state < 0 || record.roots.state > 2
      || record.roots.active_schema_changes < 0
      || record.roots.pending_schema_version < 0
      || record.parent_namespace >= record.id
      || record.fork_cap < 0) { return OB_INVALID_ARGUMENT; }
  value.clear();
  value.push_back('N');
  append_meta_bytes(value, record.name);
  append_roots(value, record.roots);
  append_meta_u64(value, record.parent_namespace);
  append_meta_i64(value, record.fork_cap);
  return OB_SUCCESS;
}

int decode_namespace(uint64_t id, const std::string &value, InstanceNamespaceRecord &record)
{
  InstanceNamespaceRecord decoded;
  decoded.id = id;
  size_t pos = 1;
  if (value.empty() || value[0] != 'N'
      || !read_meta_bytes(value, pos, decoded.name, 128)
      || !read_roots(value, pos, decoded.roots)
      || !read_meta_u64(value, pos, decoded.parent_namespace)
      || !read_meta_i64(value, pos, decoded.fork_cap)
      || pos != value.size()
      || !ns::NamespaceObjectKey{id, 1}.is_valid()
      || (decoded.name.empty() && decoded.roots.state != 2)
      || decoded.roots.state < 0 || decoded.roots.state > 2
      || decoded.roots.active_schema_changes < 0
      || decoded.roots.pending_schema_version < 0
      || decoded.parent_namespace >= id || decoded.fork_cap < 0) {
    return OB_CHECKSUM_ERROR;
  }
  record = std::move(decoded);
  return OB_SUCCESS;
}

int encode_snapshot(uint64_t id, const ns::CatalogRoots &roots,
                    std::string &value, bool inserting)
{
  ns::CatalogRoots canonical = roots;
  if (inserting) {
    canonical.parent_ref = roots.snapshot_ref;
    canonical.ref_count = 1;
  }
  canonical.snapshot_ref = id;
  if (!canonical.valid_snapshot(id)) { return OB_INVALID_ARGUMENT; }
  value.clear();
  value.push_back('S');
  append_roots(value, canonical);
  return OB_SUCCESS;
}

int decode_snapshot(uint64_t id, const std::string &value, ns::CatalogRoots &roots)
{
  ns::CatalogRoots decoded;
  size_t pos = 1;
  if (value.empty() || value[0] != 'S'
      || !read_roots(value, pos, decoded)
      || pos != value.size() || !decoded.valid_snapshot(id)) { return OB_CHECKSUM_ERROR; }
  roots = decoded;
  return OB_SUCCESS;
}

int encode_exception(const InstanceExceptionRecord &record, std::string &value)
{
  if (!ns::NamespaceObjectKey{record.namespace_id, record.tablet_id}.is_valid()
      || record.kind < 0 || record.kind > 1 || record.drop_scn < 0) { return OB_INVALID_ARGUMENT; }
  value.clear();
  value.push_back('E');
  append_meta_u64(value, record.table_id);
  append_meta_i64(value, record.kind);
  append_meta_i64(value, record.drop_scn);
  return OB_SUCCESS;
}

int decode_exception(uint64_t ns_id, uint64_t tablet_id,
                     const std::string &value, InstanceExceptionRecord &record)
{
  InstanceExceptionRecord decoded;
  decoded.namespace_id = ns_id;
  decoded.tablet_id = tablet_id;
  size_t pos = 1;
  if (value.empty() || value[0] != 'E'
      || !read_meta_u64(value, pos, decoded.table_id)
      || !read_meta_i64(value, pos, decoded.kind)
      || !read_meta_i64(value, pos, decoded.drop_scn)
      || pos != value.size() || decoded.kind < 0 || decoded.kind > 1
      || decoded.drop_scn < 0) { return OB_CHECKSUM_ERROR; }
  record = decoded;
  return OB_SUCCESS;
}

bool key_id(const ObString &key, uint64_t &id)
{
  if (key.length() != 8) { return false; }
  const std::string copy(key.ptr(), key.length());
  size_t offset = 0;
  return read_meta_u64(copy, offset, id);
}

} // namespace

int InstanceNamespaceMetadata::get_namespace(uint64_t id,
    InstanceNamespaceRecord &record, bool lock)
{
  std::string value;
  const int ret = get_value(store_, transaction_, MetaCollection::NAMESPACES,
                            id_key(id), value, lock);
  return ret == OB_SUCCESS ? decode_namespace(id, value, record) : ret;
}

int InstanceNamespaceMetadata::find_namespace(const std::string &name, uint64_t &id)
{
  id = 0;
  if (name.empty() || name.size() > 128) { return OB_INVALID_ARGUMENT; }
  std::string value;
  const int ret = get_value(store_, transaction_, MetaCollection::NAMESPACE_NAMES,
                            name, value, false);
  if (ret != OB_SUCCESS) { return ret; }
  size_t offset = 0;
  return read_meta_u64(value, offset, id) && offset == value.size()
      ? OB_SUCCESS : OB_CHECKSUM_ERROR;
}

int InstanceNamespaceMetadata::insert_namespace(const InstanceNamespaceRecord &record)
{
  if (record.name.empty()) { return OB_INVALID_ARGUMENT; }
  std::string value, id;
  int ret = encode_namespace(record, value);
  if (ret == OB_SUCCESS) {
    append_meta_u64(id, record.id);
    ret = put_value(store_, transaction_, MetaCollection::NAMESPACE_NAMES,
                    record.name, id, true);
  }
  if (ret == OB_SUCCESS) {
    ret = put_value(store_, transaction_, MetaCollection::NAMESPACES,
                    id_key(record.id), value, true);
  }
  return ret;
}

int InstanceNamespaceMetadata::update_namespace(const InstanceNamespaceRecord &record)
{
  InstanceNamespaceRecord previous;
  std::string value, id;
  int ret = encode_namespace(record, value);
  if (ret == OB_SUCCESS) { ret = get_namespace(record.id, previous, true); }
  if (ret == OB_SUCCESS && previous.roots.state == 2 && record.roots.state != 2) {
    ret = OB_STATE_NOT_MATCH;
  }
  if (ret == OB_SUCCESS && record.name != previous.name) {
    if (!previous.name.empty()) {
      ret = erase_value(store_, transaction_, MetaCollection::NAMESPACE_NAMES,
                        previous.name);
    }
    if (ret == OB_SUCCESS && !record.name.empty()) {
      append_meta_u64(id, record.id);
      ret = put_value(store_, transaction_, MetaCollection::NAMESPACE_NAMES,
                      record.name, id, true);
    }
  }
  if (ret == OB_SUCCESS) {
    ret = put_value(store_, transaction_, MetaCollection::NAMESPACES,
                    id_key(record.id), value, false);
  }
  return ret;
}

int InstanceNamespaceMetadata::erase_namespace(uint64_t id)
{
  InstanceNamespaceRecord previous;
  int ret = get_namespace(id, previous, true);
  if (ret == OB_SUCCESS && !previous.name.empty()) {
    ret = erase_value(store_, transaction_, MetaCollection::NAMESPACE_NAMES,
                      previous.name);
  }
  if (ret == OB_SUCCESS) {
    ret = erase_value(store_, transaction_, MetaCollection::NAMESPACES, id_key(id));
  }
  return ret;
}

int InstanceNamespaceMetadata::scan_namespaces(const NamespaceVisitor &visitor)
{
  if (!visitor) { return OB_INVALID_ARGUMENT; }
  InstanceMetaStore::KeyRange range;
  return store_.scan(transaction_, MetaCollection::NAMESPACES, range,
      [&](const ObString &key, const ObString &value, bool &) {
        uint64_t id = 0;
        if (!key_id(key, id)) { return OB_CHECKSUM_ERROR; }
        InstanceNamespaceRecord record;
        const int ret = decode_namespace(id, std::string(value.ptr(), value.length()), record);
        return ret == OB_SUCCESS ? visitor(record) : ret;
      });
}

int InstanceNamespaceMetadata::initialize_namespace_counter(uint64_t high_watermark)
{
  if (high_watermark < 1 || high_watermark >= ns::NamespaceObjectKey::NAMESPACE_LIMIT) {
    return OB_INVALID_ARGUMENT;
  }
  std::string value;
  append_meta_u64(value, high_watermark);
  return put_value(store_, transaction_, MetaCollection::COUNTERS,
                   id_key(1), value, true);
}

int InstanceNamespaceMetadata::allocate_namespace_id(uint64_t &id)
{
  id = 0;
  std::string value;
  int ret = get_value(store_, transaction_, MetaCollection::COUNTERS,
                      id_key(1), value, true);
  size_t offset = 0;
  uint64_t high = 0;
  if (ret == OB_ENTRY_NOT_EXIST) { return OB_NOT_INIT; }
  if (ret != OB_SUCCESS) { return ret; }
  if (!read_meta_u64(value, offset, high) || offset != value.size()) { return OB_CHECKSUM_ERROR; }
  if (high >= ns::NamespaceObjectKey::NAMESPACE_LIMIT - 1) { return OB_SIZE_OVERFLOW; }
  id = high + 1;
  value.clear();
  append_meta_u64(value, id);
  return put_value(store_, transaction_, MetaCollection::COUNTERS,
                   id_key(1), value, false);
}

int InstanceNamespaceMetadata::get_snapshot(uint64_t id,
    ns::CatalogRoots &roots, bool lock)
{
  std::string value;
  const int ret = get_value(store_, transaction_, MetaCollection::SNAPSHOTS,
                            id_key(id), value, lock);
  return ret == OB_SUCCESS ? decode_snapshot(id, value, roots) : ret;
}

int InstanceNamespaceMetadata::insert_snapshot(uint64_t id,
    const ns::CatalogRoots &roots)
{
  std::string value;
  const int ret = encode_snapshot(id, roots, value, true);
  return ret == OB_SUCCESS
      ? put_value(store_, transaction_, MetaCollection::SNAPSHOTS, id_key(id), value, true)
      : ret;
}

int InstanceNamespaceMetadata::update_snapshot(uint64_t id,
    const ns::CatalogRoots &roots)
{
  std::string old, value;
  int ret = encode_snapshot(id, roots, value, false);
  if (ret == OB_SUCCESS) {
    ret = get_value(store_, transaction_, MetaCollection::SNAPSHOTS,
                    id_key(id), old, true);
  }
  return ret == OB_SUCCESS
      ? put_value(store_, transaction_, MetaCollection::SNAPSHOTS, id_key(id), value, false)
      : ret;
}

int InstanceNamespaceMetadata::erase_snapshot(uint64_t id)
{
  return erase_value(store_, transaction_, MetaCollection::SNAPSHOTS, id_key(id));
}

int InstanceNamespaceMetadata::scan_snapshots(const SnapshotVisitor &visitor)
{
  if (!visitor) { return OB_INVALID_ARGUMENT; }
  InstanceMetaStore::KeyRange range;
  return store_.scan(transaction_, MetaCollection::SNAPSHOTS, range,
      [&](const ObString &key, const ObString &value, bool &) {
        uint64_t id = 0;
        if (!key_id(key, id)) { return OB_CHECKSUM_ERROR; }
        ns::CatalogRoots roots;
        const int ret = decode_snapshot(id, std::string(value.ptr(), value.length()), roots);
        return ret == OB_SUCCESS ? visitor(id, roots) : ret;
      });
}

int InstanceNamespaceMetadata::get_exception(uint64_t ns_id, uint64_t local_tablet,
    InstanceExceptionRecord &record, bool lock)
{
  if (!ns::NamespaceObjectKey{ns_id, local_tablet}.is_valid()) { return OB_INVALID_ARGUMENT; }
  std::string value;
  const int ret = get_value(store_, transaction_, MetaCollection::EXCEPTIONS,
                            exception_key(ns_id, local_tablet), value, lock);
  return ret == OB_SUCCESS
      ? decode_exception(ns_id, local_tablet, value, record) : ret;
}

int InstanceNamespaceMetadata::put_exception(const InstanceExceptionRecord &record)
{
  std::string value;
  const int ret = encode_exception(record, value);
  return ret == OB_SUCCESS
      ? put_value(store_, transaction_, MetaCollection::EXCEPTIONS,
                  exception_key(record.namespace_id, record.tablet_id), value, false)
      : ret;
}

int InstanceNamespaceMetadata::erase_exception(uint64_t ns_id, uint64_t local_tablet)
{
  if (!ns::NamespaceObjectKey{ns_id, local_tablet}.is_valid()) { return OB_INVALID_ARGUMENT; }
  return erase_value(store_, transaction_, MetaCollection::EXCEPTIONS,
                     exception_key(ns_id, local_tablet));
}

int InstanceNamespaceMetadata::scan_exceptions(uint64_t ns_id,
    const ExceptionVisitor &visitor)
{
  if (!ns::NamespaceObjectKey{ns_id, 1}.is_valid() || !visitor) { return OB_INVALID_ARGUMENT; }
  const std::string first = id_key(ns_id);
  const std::string end = id_key(ns_id + 1);
  InstanceMetaStore::KeyRange range;
  range.has_lower = range.has_upper = range.include_lower = true;
  range.include_upper = false;
  range.lower = meta_bytes(first);
  range.upper = meta_bytes(end);
  return store_.scan(transaction_, MetaCollection::EXCEPTIONS, range,
      [&](const ObString &key, const ObString &value, bool &) {
        if (key.length() != 16) { return OB_CHECKSUM_ERROR; }
        const std::string copy(key.ptr(), key.length());
        size_t offset = 0;
        uint64_t row_ns = 0, tablet = 0;
        if (!read_meta_u64(copy, offset, row_ns)
            || !read_meta_u64(copy, offset, tablet)
            || row_ns != ns_id) { return OB_CHECKSUM_ERROR; }
        InstanceExceptionRecord record;
        const int ret = decode_exception(row_ns, tablet,
            std::string(value.ptr(), value.length()), record);
        return ret == OB_SUCCESS ? visitor(record) : ret;
      });
}

int InstanceNamespaceMetadata::read_page(uint64_t page_id, std::string &data)
{
  const int ret = get_value(store_, transaction_, MetaCollection::PAGES,
                            id_key(page_id), data, false);
  return ret != OB_SUCCESS ? ret
      : murmurhash(data.data(), static_cast<int32_t>(data.size()), 0) == page_id
          ? OB_SUCCESS : OB_CHECKSUM_ERROR;
}

int InstanceNamespaceMetadata::save_page(const std::string &data, uint64_t &page_id)
{
  page_id = murmurhash(data.data(), static_cast<int32_t>(data.size()), 0);
  if (page_id == 0 || data.size() > 60000) { return OB_SIZE_OVERFLOW; }
  std::string existing;
  const int ret = read_page(page_id, existing);
  if (ret == OB_SUCCESS) { return existing == data ? OB_SUCCESS : OB_CHECKSUM_ERROR; }
  return ret == OB_ENTRY_NOT_EXIST
      ? put_value(store_, transaction_, MetaCollection::PAGES,
                  id_key(page_id), data, true) : ret;
}

int InstanceNamespaceMetadata::erase_page(uint64_t page_id)
{
  return erase_value(store_, transaction_, MetaCollection::PAGES, id_key(page_id));
}

int InstanceNamespaceMetadata::scan_pages(const PageVisitor &visitor)
{
  if (!visitor) { return OB_INVALID_ARGUMENT; }
  InstanceMetaStore::KeyRange range;
  return store_.scan(transaction_, MetaCollection::PAGES, range,
      [&](const ObString &key, const ObString &, bool &) {
        uint64_t id = 0;
        return key_id(key, id) ? visitor(id) : OB_CHECKSUM_ERROR;
      });
}

} // namespace rootserver
} // namespace oceanbase
