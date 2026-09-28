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
#include <limits>
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

int encode_pin(const InstanceNamespacePin &pin, std::string &value)
{
  if (pin.snapshot_id == 0
      || pin.snapshot_id > static_cast<uint64_t>(std::numeric_limits<int64_t>::max())
      || pin.schema_version < 0) { return OB_INVALID_ARGUMENT; }
  value.clear();
  value.push_back('P');
  append_meta_i64(value, pin.schema_version);
  return OB_SUCCESS;
}

int decode_pin(uint64_t snapshot_id, const std::string &value,
               InstanceNamespacePin &pin)
{
  InstanceNamespacePin decoded;
  decoded.snapshot_id = snapshot_id;
  size_t pos = 1;
  if (value.empty() || value[0] != 'P'
      || !read_meta_i64(value, pos, decoded.schema_version)
      || pos != value.size() || decoded.schema_version < 0
      || snapshot_id == 0
      || snapshot_id > static_cast<uint64_t>(std::numeric_limits<int64_t>::max())) {
    return OB_CHECKSUM_ERROR;
  }
  pin = decoded;
  return OB_SUCCESS;
}

int decode_gc_watermark(const std::string &value, int64_t &watermark)
{
  size_t pos = 1;
  if (value.empty() || value[0] != 'W'
      || !read_meta_i64(value, pos, watermark)
      || pos != value.size() || watermark < 0) { return OB_CHECKSUM_ERROR; }
  return OB_SUCCESS;
}

std::string encode_gc_watermark(int64_t watermark)
{
  std::string value(1, 'W');
  append_meta_i64(value, watermark);
  return value;
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

int InstanceNamespaceMetadata::initialize_snapshot_gc_watermark(int64_t watermark)
{
  if (watermark < 0) { return OB_INVALID_ARGUMENT; }
  return put_value(store_, transaction_, MetaCollection::SNAPSHOT_COORDINATION,
                   id_key(1), encode_gc_watermark(watermark), true);
}

int InstanceNamespaceMetadata::get_snapshot_gc_watermark(int64_t &watermark,
    bool lock)
{
  watermark = 0;
  std::string value;
  const int ret = get_value(store_, transaction_, MetaCollection::SNAPSHOT_COORDINATION,
                            id_key(1), value, lock);
  return ret == OB_SUCCESS ? decode_gc_watermark(value, watermark) : ret;
}

int InstanceNamespaceMetadata::advance_snapshot_gc_watermark(int64_t watermark)
{
  if (watermark < 0) { return OB_INVALID_ARGUMENT; }
  int64_t current = 0;
  int ret = get_snapshot_gc_watermark(current, true);
  if (ret == OB_ENTRY_NOT_EXIST) { return OB_NOT_INIT; }
  if (ret == OB_SUCCESS && watermark > current) {
    ret = put_value(store_, transaction_, MetaCollection::SNAPSHOT_COORDINATION,
                    id_key(1), encode_gc_watermark(watermark), false);
  }
  return ret;
}

int InstanceNamespaceMetadata::get_pin(uint64_t snapshot_id,
    InstanceNamespacePin &pin, bool lock)
{
  if (snapshot_id == 0) { return OB_INVALID_ARGUMENT; }
  std::string value;
  const int ret = get_value(store_, transaction_, MetaCollection::SNAPSHOT_PINS,
                            id_key(snapshot_id), value, lock);
  return ret == OB_SUCCESS ? decode_pin(snapshot_id, value, pin) : ret;
}

int InstanceNamespaceMetadata::insert_pin(const InstanceNamespacePin &pin)
{
  std::string value;
  int ret = encode_pin(pin, value);
  int64_t watermark = 0;
  if (ret == OB_SUCCESS) { ret = get_snapshot_gc_watermark(watermark, true); }
  if (ret == OB_ENTRY_NOT_EXIST) { return OB_NOT_INIT; }
  if (ret == OB_SUCCESS && pin.snapshot_id <= static_cast<uint64_t>(watermark)) {
    ret = OB_SNAPSHOT_DISCARDED;
  }
  if (ret == OB_SUCCESS) {
    ret = put_value(store_, transaction_, MetaCollection::SNAPSHOT_PINS,
                    id_key(pin.snapshot_id), value, true);
  }
  return ret;
}

int InstanceNamespaceMetadata::erase_pin(uint64_t snapshot_id)
{
  if (snapshot_id == 0) { return OB_INVALID_ARGUMENT; }
  return erase_value(store_, transaction_, MetaCollection::SNAPSHOT_PINS,
                     id_key(snapshot_id));
}

int InstanceNamespaceMetadata::scan_pins(const PinVisitor &visitor)
{
  if (!visitor) { return OB_INVALID_ARGUMENT; }
  InstanceMetaStore::KeyRange range;
  return store_.scan(transaction_, MetaCollection::SNAPSHOT_PINS, range,
      [&](const ObString &key, const ObString &value, bool &) {
        uint64_t id = 0;
        if (!key_id(key, id)) { return OB_CHECKSUM_ERROR; }
        InstanceNamespacePin pin;
        const int ret = decode_pin(id, std::string(value.ptr(), value.length()), pin);
        return ret == OB_SUCCESS ? visitor(pin) : ret;
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

int InstanceSnapshotLineageStore::load_for_update(uint64_t snapshot_id,
    ns::CatalogRoots &roots)
{
  int ret = metadata_.get_snapshot(snapshot_id, roots, true);
  InstanceNamespacePin pin;
  if (ret == OB_SUCCESS) {
    ret = metadata_.get_pin(snapshot_id, pin);
    if (ret == OB_ENTRY_NOT_EXIST) { ret = OB_STATE_NOT_MATCH; }
  }
  if (ret == OB_SUCCESS && pin.schema_version != roots.schema_version) {
    ret = OB_STATE_NOT_MATCH;
  }
  return ret;
}

int InstanceSnapshotLineageStore::increment_ref(uint64_t snapshot_id)
{
  ns::CatalogRoots roots;
  int ret = load_for_update(snapshot_id, roots);
  if (ret == OB_SUCCESS && roots.ref_count == std::numeric_limits<int64_t>::max()) {
    ret = OB_SIZE_OVERFLOW;
  }
  if (ret == OB_SUCCESS) {
    ++roots.ref_count;
    ret = metadata_.update_snapshot(snapshot_id, roots);
  }
  return ret;
}

int InstanceSnapshotLineageStore::decrement_ref(uint64_t snapshot_id)
{
  ns::CatalogRoots roots;
  int ret = load_for_update(snapshot_id, roots);
  if (ret == OB_SUCCESS && roots.ref_count <= 1) { ret = OB_STATE_NOT_MATCH; }
  if (ret == OB_SUCCESS) {
    --roots.ref_count;
    ret = metadata_.update_snapshot(snapshot_id, roots);
  }
  return ret;
}

int InstanceSnapshotLineageStore::insert_snapshot(const ns::CatalogRoots &roots)
{
  return roots.snapshot <= 0 ? OB_INVALID_ARGUMENT
      : metadata_.insert_snapshot(static_cast<uint64_t>(roots.snapshot), roots);
}

int InstanceSnapshotLineageStore::attach_child(uint64_t child_id,
    uint64_t parent_namespace_id, const ns::CatalogRoots &roots)
{
  if (parent_namespace_id == 0 || parent_namespace_id >= child_id
      || roots.snapshot <= 0
      || roots.snapshot_ref != static_cast<uint64_t>(roots.snapshot)) {
    return OB_INVALID_ARGUMENT;
  }
  InstanceNamespaceRecord child;
  int ret = metadata_.get_namespace(child_id, child, true);
  if (ret == OB_SUCCESS && (child.roots.state != 0
      || child.roots.snapshot_ref != 0 || child.parent_namespace != 0)) {
    ret = OB_STATE_NOT_MATCH;
  }
  if (ret == OB_SUCCESS) {
    child.roots = roots;
    child.parent_namespace = parent_namespace_id;
    child.fork_cap = roots.snapshot;
    ret = metadata_.update_namespace(child);
  }
  return ret;
}

int InstanceSnapshotLineageStore::remove_snapshot(uint64_t snapshot_id,
    const ns::CatalogRoots &roots)
{
  InstanceNamespacePin pin;
  int ret = metadata_.get_pin(snapshot_id, pin, true);
  if (ret == OB_SUCCESS && (roots.snapshot != static_cast<int64_t>(snapshot_id)
      || roots.ref_count != 1 || pin.schema_version != roots.schema_version)) {
    ret = OB_STATE_NOT_MATCH;
  }
  if (ret == OB_SUCCESS) { ret = metadata_.erase_pin(snapshot_id); }
  if (ret == OB_SUCCESS) { ret = metadata_.erase_snapshot(snapshot_id); }
  return ret;
}

int InstanceNamespaceMetadata::fork_namespace(const std::string &source_name,
    const std::string &target_name, const SnapshotAcquirer &acquire_snapshot,
    uint64_t &child_id)
{
  child_id = 0;
  if (source_name.empty() || source_name.size() > 128 || target_name.empty()
      || target_name.size() > 128 || !acquire_snapshot) {
    return OB_INVALID_ARGUMENT;
  }
  uint64_t source_id = 0;
  int ret = find_namespace(source_name, source_id);
  InstanceNamespaceRecord source;
  if (ret == OB_SUCCESS) { ret = get_namespace(source_id, source, true); }
  if (ret == OB_SUCCESS && (source.name != source_name || source.roots.state != 0)) {
    ret = OB_STATE_NOT_MATCH;
  }
  if (ret == OB_SUCCESS && (source.roots.active_schema_changes != 0
      || source.roots.pending_schema_version != 0)) {
    ret = OB_EAGAIN;
  }
  ns::CatalogRoots roots = source.roots;
  if (ret == OB_SUCCESS) { ret = acquire_snapshot(roots.snapshot); }
  if (ret == OB_SUCCESS && (roots.snapshot <= 0 || roots.schema_version <= 0)) {
    ret = OB_INVALID_ARGUMENT;
  }
  uint64_t allocated_id = 0;
  if (ret == OB_SUCCESS) { ret = allocate_namespace_id(allocated_id); }
  if (ret == OB_SUCCESS) {
    InstanceNamespaceRecord child;
    child.id = allocated_id;
    child.name = target_name;
    ret = insert_namespace(child);
  }
  if (ret == OB_SUCCESS) {
    ret = insert_pin({static_cast<uint64_t>(roots.snapshot), roots.schema_version});
  }
  if (ret == OB_SUCCESS) {
    InstanceSnapshotLineageStore lineage(*this);
    const auto result = ns::NamespaceSnapshotLineage::fork(
        source_id, allocated_id, roots, lineage);
    switch (result.error) {
      case ns::SnapshotForkError::NONE: break;
      case ns::SnapshotForkError::INVALID: ret = OB_INVALID_ARGUMENT; break;
      case ns::SnapshotForkError::OVERFLOW: ret = OB_SIZE_OVERFLOW; break;
      case ns::SnapshotForkError::STORE: ret = result.store_error; break;
    }
  }
  if (ret == OB_SUCCESS) { child_id = allocated_id; }
  return ret;
}

} // namespace rootserver
} // namespace oceanbase
