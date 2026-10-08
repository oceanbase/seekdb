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
#include "common/json_type/ob_json_base.h"
#include "common/json_type/ob_json_tree.h"
#include "share/instance_meta/instance_meta_key_codec.h"
#include "share/instance_meta/instance_meta_value_codec.h"
#include <algorithm>
#include <cstring>
#include <limits>
#include <unordered_map>
#include <unordered_set>
#include <utility>
#include <vector>

namespace oceanbase
{
namespace rootserver
{
namespace
{
using namespace common;
using storage::MetaCollection;
using storage::InstanceMetaStore;
using share::instance_meta::InstanceMetaKeyCodec;
using share::instance_meta::InstanceMetaValueCodec;
using share::instance_meta::ValueFormat;

void append_json_string(const std::string &value, std::string &out)
{
  static const char hex[] = "0123456789abcdef";
  out += '"';
  for (unsigned char c : value) {
    if (c == '"' || c == '\\') {
      out += '\\';
      out += static_cast<char>(c);
    } else if (c < 0x20) {
      out += "\\u00";
      out += hex[c >> 4];
      out += hex[c & 0xf];
    } else {
      out += static_cast<char>(c);
    }
  }
  out += '"';
}

void append_json_field_name(std::string &out, const char *name)
{
  if (out.size() > 1) { out += ','; }
  out += '"';
  out += name;
  out += "\":";
}

void append_json_u64(std::string &out, const char *name, uint64_t value)
{
  append_json_field_name(out, name);
  out += std::to_string(value);
}

void append_json_i64(std::string &out, const char *name, int64_t value)
{
  append_json_field_name(out, name);
  out += std::to_string(value);
}

void append_json_text(std::string &out, const char *name, const std::string &value)
{
  append_json_field_name(out, name);
  append_json_string(value, out);
}

int parse_json_object(const std::string &value, ObArenaAllocator &allocator,
                      ObJsonObject *&object)
{
  ObIJsonBase *base = nullptr;
  const ObString text(value.size(), value.data());
  int ret = ObJsonBaseFactory::get_json_base(
      &allocator, text, ObJsonInType::JSON_TREE, ObJsonInType::JSON_TREE, base);
  if (ret == OB_SUCCESS) {
    if (base == nullptr || base->json_type() != ObJsonNodeType::J_OBJECT) {
      ret = OB_CHECKSUM_ERROR;
    } else {
      object = static_cast<ObJsonObject *>(base);
    }
  }
  return ret;
}

int json_u64(const ObJsonObject &object, const char *name, uint64_t &value)
{
  const ObJsonNode *node = object.get_value(name);
  if (node == nullptr) { return OB_CHECKSUM_ERROR; }
  if (node->json_type() == ObJsonNodeType::J_UINT) {
    value = node->get_uint();
  } else if (node->json_type() == ObJsonNodeType::J_INT && node->get_int() >= 0) {
    value = static_cast<uint64_t>(node->get_int());
  } else {
    return OB_CHECKSUM_ERROR;
  }
  return OB_SUCCESS;
}

int json_i64(const ObJsonObject &object, const char *name, int64_t &value)
{
  const ObJsonNode *node = object.get_value(name);
  if (node == nullptr) { return OB_CHECKSUM_ERROR; }
  if (node->json_type() == ObJsonNodeType::J_INT) {
    value = node->get_int();
  } else if (node->json_type() == ObJsonNodeType::J_UINT
             && node->get_uint() <= static_cast<uint64_t>(INT64_MAX)) {
    value = static_cast<int64_t>(node->get_uint());
  } else {
    return OB_CHECKSUM_ERROR;
  }
  return OB_SUCCESS;
}

int json_text(const ObJsonObject &object, const char *name, std::string &value)
{
  const ObJsonNode *node = object.get_value(name);
  if (node == nullptr || node->json_type() != ObJsonNodeType::J_STRING) {
    return OB_CHECKSUM_ERROR;
  }
  const ObString text = static_cast<const ObJsonString *>(node)->get_str();
  value.assign(text.ptr(), text.length());
  return OB_SUCCESS;
}

std::string id_key(MetaCollection collection, uint64_t id)
{
  std::string key;
  (void)InstanceMetaKeyCodec::encode_u64(collection, id, key);
  return key;
}

std::string exception_key(uint64_t ns_id, uint64_t local_tablet)
{
  std::string key;
  (void)InstanceMetaKeyCodec::encode_pair(MetaCollection::EXCEPTIONS, ns_id, local_tablet, key);
  return key;
}

std::string name_key(const std::string &name)
{
  std::string key;
  (void)InstanceMetaKeyCodec::encode_name(MetaCollection::NAMESPACE_NAMES, name, key);
  return key;
}

ObString meta_bytes(const std::string &value)
{
  return ObString(value.size(), value.data());
}

int get_value(InstanceMetaStore &store, InstanceMetaStore::Transaction &tx,
              MetaCollection collection, const std::string &key,
              std::string &value, bool lock, ValueFormat expected = ValueFormat::JSON)
{
  ObArenaAllocator allocator(ObMemAttr("InstanceMetaGet"));
  ObString result;
  const int ret = lock ? store.get_for_update(tx, collection, meta_bytes(key), allocator, result)
                       : store.get(tx, collection, meta_bytes(key), allocator, result);
  if (ret == OB_SUCCESS) {
    ValueFormat actual;
    ObString payload;
    const int decode_ret = InstanceMetaValueCodec::decode(result, actual, payload);
    if (decode_ret != OB_SUCCESS || actual != expected) { return OB_CHECKSUM_ERROR; }
    value.assign(payload.ptr(), payload.length());
  }
  return ret;
}

int put_value(InstanceMetaStore &store, InstanceMetaStore::Transaction &tx,
              MetaCollection collection, const std::string &key,
              const std::string &value, bool insert,
              ValueFormat format = ValueFormat::JSON)
{
  std::string encoded;
  int ret = InstanceMetaValueCodec::encode(format, value, encoded);
  if (ret == OB_SUCCESS) {
    ret = insert ? store.insert(tx, collection, meta_bytes(key), meta_bytes(encoded))
                 : store.put(tx, collection, meta_bytes(key), meta_bytes(encoded));
  }
  return ret;
}

int scan_value(const ObString &encoded, ValueFormat expected, std::string &value)
{
  ValueFormat actual;
  ObString payload;
  const int ret = InstanceMetaValueCodec::decode(encoded, actual, payload);
  if (ret != OB_SUCCESS || actual != expected) { return OB_CHECKSUM_ERROR; }
  value.assign(payload.ptr(), payload.length());
  return OB_SUCCESS;
}

int erase_value(InstanceMetaStore &store, InstanceMetaStore::Transaction &tx,
                MetaCollection collection, const std::string &key)
{
  bool existed = false;
  const int ret = store.erase(tx, collection, meta_bytes(key), existed);
  return ret == OB_SUCCESS && !existed ? OB_ENTRY_NOT_EXIST : ret;
}

void append_roots_json(std::string &out, const ns::CatalogRoots &roots)
{
  append_json_u64(out, "source", roots.source);
  append_json_u64(out, "catalog_page", roots.catalog.page);
  append_json_i64(out, "catalog_cap", roots.catalog.cap);
  append_json_u64(out, "directory_page", roots.directory.page);
  append_json_i64(out, "directory_cap", roots.directory.cap);
  append_json_i64(out, "snapshot", roots.snapshot);
  append_json_i64(out, "schema_version", roots.schema_version);
  append_json_u64(out, "snapshot_ref", roots.snapshot_ref);
  append_json_u64(out, "parent_ref", roots.parent_ref);
  append_json_i64(out, "ref_count", roots.ref_count);
  append_json_i64(out, "state", roots.state);
  append_json_i64(out, "active_schema_changes", roots.active_schema_changes);
  append_json_i64(out, "pending_schema_version", roots.pending_schema_version);
}

int read_roots_json(const ObJsonObject &object, ns::CatalogRoots &roots)
{
  int ret = json_u64(object, "source", roots.source);
  if (ret == OB_SUCCESS) { ret = json_u64(object, "catalog_page", roots.catalog.page); }
  if (ret == OB_SUCCESS) { ret = json_i64(object, "catalog_cap", roots.catalog.cap); }
  if (ret == OB_SUCCESS) { ret = json_u64(object, "directory_page", roots.directory.page); }
  if (ret == OB_SUCCESS) { ret = json_i64(object, "directory_cap", roots.directory.cap); }
  if (ret == OB_SUCCESS) { ret = json_i64(object, "snapshot", roots.snapshot); }
  if (ret == OB_SUCCESS) { ret = json_i64(object, "schema_version", roots.schema_version); }
  if (ret == OB_SUCCESS) { ret = json_u64(object, "snapshot_ref", roots.snapshot_ref); }
  if (ret == OB_SUCCESS) { ret = json_u64(object, "parent_ref", roots.parent_ref); }
  if (ret == OB_SUCCESS) { ret = json_i64(object, "ref_count", roots.ref_count); }
  if (ret == OB_SUCCESS) { ret = json_i64(object, "state", roots.state); }
  if (ret == OB_SUCCESS) { ret = json_i64(object, "active_schema_changes", roots.active_schema_changes); }
  if (ret == OB_SUCCESS) { ret = json_i64(object, "pending_schema_version", roots.pending_schema_version); }
  return ret;
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
  value = "{";
  append_json_text(value, "name", record.name);
  append_roots_json(value, record.roots);
  append_json_u64(value, "parent_namespace", record.parent_namespace);
  append_json_i64(value, "fork_cap", record.fork_cap);
  value += '}';
  return OB_SUCCESS;
}

int decode_namespace(uint64_t id, const std::string &value, InstanceNamespaceRecord &record)
{
  InstanceNamespaceRecord decoded;
  decoded.id = id;
  ObArenaAllocator allocator(ObMemAttr("InstMetaJson"));
  ObJsonObject *object = nullptr;
  int ret = parse_json_object(value, allocator, object);
  if (ret == OB_SUCCESS) { ret = json_text(*object, "name", decoded.name); }
  if (ret == OB_SUCCESS) { ret = read_roots_json(*object, decoded.roots); }
  if (ret == OB_SUCCESS) { ret = json_u64(*object, "parent_namespace", decoded.parent_namespace); }
  if (ret == OB_SUCCESS) { ret = json_i64(*object, "fork_cap", decoded.fork_cap); }
  if (ret != OB_SUCCESS
      || !ns::NamespaceObjectKey{id, 1}.is_valid()
      || (decoded.name.empty() && decoded.roots.state != 2)
      || decoded.name.size() > 128
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
  value = "{";
  append_roots_json(value, canonical);
  value += '}';
  return OB_SUCCESS;
}

int decode_snapshot(uint64_t id, const std::string &value, ns::CatalogRoots &roots)
{
  ns::CatalogRoots decoded;
  ObArenaAllocator allocator(ObMemAttr("InstMetaJson"));
  ObJsonObject *object = nullptr;
  int ret = parse_json_object(value, allocator, object);
  if (ret == OB_SUCCESS) { ret = read_roots_json(*object, decoded); }
  if (ret != OB_SUCCESS || !decoded.valid_snapshot(id)) { return OB_CHECKSUM_ERROR; }
  roots = decoded;
  return OB_SUCCESS;
}

int encode_pin(const InstanceNamespacePin &pin, std::string &value)
{
  if (pin.snapshot_id == 0
      || pin.snapshot_id > static_cast<uint64_t>(std::numeric_limits<int64_t>::max())
      || pin.schema_version < 0) { return OB_INVALID_ARGUMENT; }
  value = "{";
  append_json_i64(value, "schema_version", pin.schema_version);
  value += '}';
  return OB_SUCCESS;
}

int decode_pin(uint64_t snapshot_id, const std::string &value,
               InstanceNamespacePin &pin)
{
  InstanceNamespacePin decoded;
  decoded.snapshot_id = snapshot_id;
  ObArenaAllocator allocator(ObMemAttr("InstMetaJson"));
  ObJsonObject *object = nullptr;
  int ret = parse_json_object(value, allocator, object);
  if (ret == OB_SUCCESS) { ret = json_i64(*object, "schema_version", decoded.schema_version); }
  if (ret != OB_SUCCESS || decoded.schema_version < 0
      || snapshot_id == 0
      || snapshot_id > static_cast<uint64_t>(std::numeric_limits<int64_t>::max())) {
    return OB_CHECKSUM_ERROR;
  }
  pin = decoded;
  return OB_SUCCESS;
}

int decode_gc_watermark(const std::string &value, int64_t &watermark)
{
  ObArenaAllocator allocator(ObMemAttr("InstMetaJson"));
  ObJsonObject *object = nullptr;
  int ret = parse_json_object(value, allocator, object);
  if (ret == OB_SUCCESS) { ret = json_i64(*object, "watermark", watermark); }
  if (ret != OB_SUCCESS || watermark < 0) { return OB_CHECKSUM_ERROR; }
  return OB_SUCCESS;
}

std::string encode_gc_watermark(int64_t watermark)
{
  std::string value = "{";
  append_json_i64(value, "watermark", watermark);
  value += '}';
  return value;
}

int encode_exception(const InstanceExceptionRecord &record, std::string &value)
{
  if (!ns::NamespaceObjectKey{record.namespace_id, record.tablet_id}.is_valid()
      || record.kind < 0 || record.kind > 1 || record.drop_scn < 0
      || record.create_scn < 0
      || (record.kind == 1 && (record.drop_scn == 0
          || (record.was_owned && record.create_scn == 0)))) { return OB_INVALID_ARGUMENT; }
  value = "{";
  append_json_u64(value, "table_id", record.table_id);
  append_json_i64(value, "kind", record.kind);
  append_json_i64(value, "drop_scn", record.drop_scn);
  append_json_i64(value, "was_owned", record.was_owned ? 1 : 0);
  append_json_i64(value, "create_scn", record.create_scn);
  value += '}';
  return OB_SUCCESS;
}

int decode_exception(uint64_t ns_id, uint64_t tablet_id,
                     const std::string &value, InstanceExceptionRecord &record)
{
  InstanceExceptionRecord decoded;
  decoded.namespace_id = ns_id;
  decoded.tablet_id = tablet_id;
  ObArenaAllocator allocator(ObMemAttr("InstMetaJson"));
  ObJsonObject *object = nullptr;
  int ret = parse_json_object(value, allocator, object);
  if (ret == OB_SUCCESS) { ret = json_u64(*object, "table_id", decoded.table_id); }
  if (ret == OB_SUCCESS) { ret = json_i64(*object, "kind", decoded.kind); }
  if (ret == OB_SUCCESS) { ret = json_i64(*object, "drop_scn", decoded.drop_scn); }
  int64_t was_owned = 0;
  if (ret == OB_SUCCESS) { ret = json_i64(*object, "was_owned", was_owned); }
  if (ret == OB_SUCCESS) { ret = json_i64(*object, "create_scn", decoded.create_scn); }
  if (ret != OB_SUCCESS || decoded.kind < 0 || decoded.kind > 1
      || decoded.drop_scn < 0 || was_owned < 0 || was_owned > 1
      || decoded.create_scn < 0
      || (decoded.kind == 1 && (decoded.drop_scn == 0
          || (was_owned == 1 && decoded.create_scn == 0)))) { return OB_CHECKSUM_ERROR; }
  decoded.was_owned = was_owned == 1;
  record = decoded;
  return OB_SUCCESS;
}

bool key_id(MetaCollection collection, const ObString &key, uint64_t &id)
{
  return OB_SUCCESS == InstanceMetaKeyCodec::decode_u64(collection, key, id);
}

} // namespace

int InstanceNamespaceMetadata::get_namespace(uint64_t id,
    InstanceNamespaceRecord &record, bool lock)
{
  std::string value;
  const int ret = get_value(store_, transaction_, MetaCollection::NAMESPACES,
                            id_key(MetaCollection::NAMESPACES, id), value, lock);
  return ret == OB_SUCCESS ? decode_namespace(id, value, record) : ret;
}

int InstanceNamespaceMetadata::find_namespace(const std::string &name, uint64_t &id)
{
  id = 0;
  if (name.empty() || name.size() > 128) { return OB_INVALID_ARGUMENT; }
  std::string value;
  const int ret = get_value(store_, transaction_, MetaCollection::NAMESPACE_NAMES,
                            name_key(name), value, false);
  if (ret != OB_SUCCESS) { return ret; }
  ObArenaAllocator allocator(ObMemAttr("InstMetaJson"));
  ObJsonObject *object = nullptr;
  int decode_ret = parse_json_object(value, allocator, object);
  if (decode_ret == OB_SUCCESS) { decode_ret = json_u64(*object, "namespace_id", id); }
  return decode_ret == OB_SUCCESS ? OB_SUCCESS : OB_CHECKSUM_ERROR;
}

int InstanceNamespaceMetadata::insert_namespace(const InstanceNamespaceRecord &record)
{
  if (record.name.empty()) { return OB_INVALID_ARGUMENT; }
  std::string value, id;
  int ret = encode_namespace(record, value);
  if (ret == OB_SUCCESS) {
    id = "{";
    append_json_u64(id, "namespace_id", record.id);
    id += '}';
    ret = put_value(store_, transaction_, MetaCollection::NAMESPACE_NAMES,
                    name_key(record.name), id, true);
  }
  if (ret == OB_SUCCESS) {
    ret = put_value(store_, transaction_, MetaCollection::NAMESPACES,
                    id_key(MetaCollection::NAMESPACES, record.id), value, true);
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
                        name_key(previous.name));
    }
    if (ret == OB_SUCCESS && !record.name.empty()) {
      id = "{";
      append_json_u64(id, "namespace_id", record.id);
      id += '}';
      ret = put_value(store_, transaction_, MetaCollection::NAMESPACE_NAMES,
                      name_key(record.name), id, true);
    }
  }
  if (ret == OB_SUCCESS) {
    ret = put_value(store_, transaction_, MetaCollection::NAMESPACES,
                    id_key(MetaCollection::NAMESPACES, record.id), value, false);
  }
  return ret;
}

int InstanceNamespaceMetadata::rename_namespace(uint64_t id,
    const std::string &expected_name, const std::string &new_name)
{
  if (expected_name.empty() || new_name.empty() || new_name.size() > 128
      || expected_name == new_name) { return OB_INVALID_ARGUMENT; }
  InstanceNamespaceRecord record;
  int ret = get_namespace(id, record, true);
  if (ret == OB_SUCCESS && (record.roots.state != 0
      || record.name != expected_name)) { ret = OB_STATE_NOT_MATCH; }
  if (ret == OB_SUCCESS) {
    record.name = new_name;
    ret = update_namespace(record);
  }
  return ret;
}

int InstanceNamespaceMetadata::erase_namespace(uint64_t id)
{
  InstanceNamespaceRecord previous;
  int ret = get_namespace(id, previous, true);
  if (ret == OB_SUCCESS && !previous.name.empty()) {
    ret = erase_value(store_, transaction_, MetaCollection::NAMESPACE_NAMES,
                      name_key(previous.name));
  }
  if (ret == OB_SUCCESS) {
    ret = erase_value(store_, transaction_, MetaCollection::NAMESPACES, id_key(MetaCollection::NAMESPACES, id));
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
        if (!key_id(MetaCollection::NAMESPACES, key, id)) { return OB_CHECKSUM_ERROR; }
        InstanceNamespaceRecord record;
        std::string payload;
        int ret = scan_value(value, ValueFormat::JSON, payload);
        if (ret == OB_SUCCESS) { ret = decode_namespace(id, payload, record); }
        return ret == OB_SUCCESS ? visitor(record) : ret;
      });
}

int InstanceNamespaceMetadata::insert_root_namespace(const std::string &name,
    int64_t schema_version)
{
  if (name.empty() || name.size() > 128 || schema_version <= 0) {
    return OB_INVALID_ARGUMENT;
  }
  InstanceNamespaceRecord root;
  root.id = 1;
  root.name = name;
  root.roots.schema_version = schema_version;
  int ret = initialize_namespace_counter(root.id);
  if (ret == OB_SUCCESS) { ret = insert_namespace(root); }
  return ret;
}

int InstanceNamespaceMetadata::initialize_namespace_counter(uint64_t high_watermark)
{
  if (high_watermark < 1 || high_watermark >= ns::NamespaceObjectKey::NAMESPACE_LIMIT) {
    return OB_INVALID_ARGUMENT;
  }
  std::string value = "{";
  append_json_u64(value, "high_watermark", high_watermark);
  value += '}';
  return put_value(store_, transaction_, MetaCollection::COUNTERS,
                   id_key(MetaCollection::COUNTERS, 1), value, true);
}

int InstanceNamespaceMetadata::allocate_namespace_id(uint64_t &id)
{
  id = 0;
  std::string value;
  int ret = get_value(store_, transaction_, MetaCollection::COUNTERS,
                      id_key(MetaCollection::COUNTERS, 1), value, true);
  uint64_t high = 0;
  if (ret == OB_ENTRY_NOT_EXIST) { return OB_NOT_INIT; }
  if (ret != OB_SUCCESS) { return ret; }
  ObArenaAllocator allocator(ObMemAttr("InstMetaJson"));
  ObJsonObject *object = nullptr;
  ret = parse_json_object(value, allocator, object);
  if (ret == OB_SUCCESS) { ret = json_u64(*object, "high_watermark", high); }
  if (ret != OB_SUCCESS) { return OB_CHECKSUM_ERROR; }
  if (high >= ns::NamespaceObjectKey::NAMESPACE_LIMIT - 1) { return OB_SIZE_OVERFLOW; }
  id = high + 1;
  value = "{";
  append_json_u64(value, "high_watermark", id);
  value += '}';
  return put_value(store_, transaction_, MetaCollection::COUNTERS,
                   id_key(MetaCollection::COUNTERS, 1), value, false);
}

int InstanceNamespaceMetadata::get_snapshot(uint64_t id,
    ns::CatalogRoots &roots, bool lock)
{
  std::string value;
  const int ret = get_value(store_, transaction_, MetaCollection::SNAPSHOTS,
                            id_key(MetaCollection::SNAPSHOTS, id), value, lock);
  return ret == OB_SUCCESS ? decode_snapshot(id, value, roots) : ret;
}

int InstanceNamespaceMetadata::insert_snapshot(uint64_t id,
    const ns::CatalogRoots &roots)
{
  std::string value;
  const int ret = encode_snapshot(id, roots, value, true);
  return ret == OB_SUCCESS
      ? put_value(store_, transaction_, MetaCollection::SNAPSHOTS, id_key(MetaCollection::SNAPSHOTS, id), value, true)
      : ret;
}

int InstanceNamespaceMetadata::update_snapshot(uint64_t id,
    const ns::CatalogRoots &roots)
{
  std::string old, value;
  int ret = encode_snapshot(id, roots, value, false);
  if (ret == OB_SUCCESS) {
    ret = get_value(store_, transaction_, MetaCollection::SNAPSHOTS,
                    id_key(MetaCollection::SNAPSHOTS, id), old, true);
  }
  return ret == OB_SUCCESS
      ? put_value(store_, transaction_, MetaCollection::SNAPSHOTS, id_key(MetaCollection::SNAPSHOTS, id), value, false)
      : ret;
}

int InstanceNamespaceMetadata::erase_snapshot(uint64_t id)
{
  return erase_value(store_, transaction_, MetaCollection::SNAPSHOTS, id_key(MetaCollection::SNAPSHOTS, id));
}

int InstanceNamespaceMetadata::scan_snapshots(const SnapshotVisitor &visitor)
{
  if (!visitor) { return OB_INVALID_ARGUMENT; }
  InstanceMetaStore::KeyRange range;
  return store_.scan(transaction_, MetaCollection::SNAPSHOTS, range,
      [&](const ObString &key, const ObString &value, bool &) {
        uint64_t id = 0;
        if (!key_id(MetaCollection::SNAPSHOTS, key, id)) { return OB_CHECKSUM_ERROR; }
        ns::CatalogRoots roots;
        std::string payload;
        int ret = scan_value(value, ValueFormat::JSON, payload);
        if (ret == OB_SUCCESS) { ret = decode_snapshot(id, payload, roots); }
        return ret == OB_SUCCESS ? visitor(id, roots) : ret;
      });
}

int InstanceNamespaceMetadata::initialize_snapshot_gc_watermark(int64_t watermark)
{
  if (watermark < 0) { return OB_INVALID_ARGUMENT; }
  return put_value(store_, transaction_, MetaCollection::SNAPSHOT_COORDINATION,
                   id_key(MetaCollection::SNAPSHOT_COORDINATION, 1), encode_gc_watermark(watermark), true);
}

int InstanceNamespaceMetadata::get_snapshot_gc_watermark(int64_t &watermark,
    bool lock)
{
  watermark = 0;
  std::string value;
  const int ret = get_value(store_, transaction_, MetaCollection::SNAPSHOT_COORDINATION,
                            id_key(MetaCollection::SNAPSHOT_COORDINATION, 1), value, lock);
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
                    id_key(MetaCollection::SNAPSHOT_COORDINATION, 1), encode_gc_watermark(watermark), false);
  }
  return ret;
}

int InstanceNamespaceMetadata::get_pin(uint64_t snapshot_id,
    InstanceNamespacePin &pin, bool lock)
{
  if (snapshot_id == 0) { return OB_INVALID_ARGUMENT; }
  std::string value;
  const int ret = get_value(store_, transaction_, MetaCollection::SNAPSHOT_PINS,
                            id_key(MetaCollection::SNAPSHOT_PINS, snapshot_id), value, lock);
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
                    id_key(MetaCollection::SNAPSHOT_PINS, pin.snapshot_id), value, true);
  }
  return ret;
}

int InstanceNamespaceMetadata::erase_pin(uint64_t snapshot_id)
{
  if (snapshot_id == 0) { return OB_INVALID_ARGUMENT; }
  return erase_value(store_, transaction_, MetaCollection::SNAPSHOT_PINS,
                     id_key(MetaCollection::SNAPSHOT_PINS, snapshot_id));
}

int InstanceNamespaceMetadata::scan_pins(const PinVisitor &visitor)
{
  if (!visitor) { return OB_INVALID_ARGUMENT; }
  InstanceMetaStore::KeyRange range;
  return store_.scan(transaction_, MetaCollection::SNAPSHOT_PINS, range,
      [&](const ObString &key, const ObString &value, bool &) {
        uint64_t id = 0;
        if (!key_id(MetaCollection::SNAPSHOT_PINS, key, id)) { return OB_CHECKSUM_ERROR; }
        InstanceNamespacePin pin;
        std::string payload;
        int ret = scan_value(value, ValueFormat::JSON, payload);
        if (ret == OB_SUCCESS) { ret = decode_pin(id, payload, pin); }
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
  std::string first, end;
  (void)InstanceMetaKeyCodec::encode_first_u64(MetaCollection::EXCEPTIONS, ns_id, first);
  (void)InstanceMetaKeyCodec::encode_first_u64(MetaCollection::EXCEPTIONS, ns_id + 1, end);
  InstanceMetaStore::KeyRange range;
  range.has_lower = range.has_upper = range.include_lower = true;
  range.include_upper = false;
  range.lower = meta_bytes(first);
  range.upper = meta_bytes(end);
  return store_.scan(transaction_, MetaCollection::EXCEPTIONS, range,
      [&](const ObString &key, const ObString &value, bool &) {
        uint64_t row_ns = 0, tablet = 0;
        if (OB_SUCCESS != InstanceMetaKeyCodec::decode_pair(
                MetaCollection::EXCEPTIONS, key, row_ns, tablet)
            || row_ns != ns_id) { return OB_CHECKSUM_ERROR; }
        InstanceExceptionRecord record;
        std::string payload;
        int ret = scan_value(value, ValueFormat::JSON, payload);
        if (ret == OB_SUCCESS) { ret = decode_exception(row_ns, tablet, payload, record); }
        return ret == OB_SUCCESS ? visitor(record) : ret;
      });
}

int InstanceExceptionLoader::load(uint64_t namespace_id, IRowSink &sink)
{
  return metadata_.scan_exceptions(namespace_id,
      [&](const InstanceExceptionRecord &record) {
        sink.add({record.tablet_id, record.table_id, record.kind});
        return OB_SUCCESS;
      });
}

int InstanceNamespaceMetadata::read_page(uint64_t page_id, std::string &data)
{
  const int ret = get_value(store_, transaction_, MetaCollection::PAGES,
                            id_key(MetaCollection::PAGES, page_id), data, false,
                            ValueFormat::BYTES);
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
                  id_key(MetaCollection::PAGES, page_id), data, true,
                  ValueFormat::BYTES) : ret;
}

int InstanceNamespaceMetadata::erase_page(uint64_t page_id)
{
  return erase_value(store_, transaction_, MetaCollection::PAGES, id_key(MetaCollection::PAGES, page_id));
}

int InstanceNamespaceMetadata::scan_pages(const PageVisitor &visitor)
{
  if (!visitor) { return OB_INVALID_ARGUMENT; }
  InstanceMetaStore::KeyRange range;
  return store_.scan(transaction_, MetaCollection::PAGES, range,
      [&](const ObString &key, const ObString &, bool &) {
        uint64_t id = 0;
        return key_id(MetaCollection::PAGES, key, id) ? visitor(id) : OB_CHECKSUM_ERROR;
      });
}

int InstanceNamespaceMetadata::save_object(const std::string &data, uint64_t &object)
{
  using Codec = ns::NamespaceCatalogCodec;
  if (data.size() > Codec::MAX_OBJECT_CHUNKS * Codec::OBJECT_CHUNK_BYTES) {
    return OB_SIZE_OVERFLOW;
  }
  std::vector<uint64_t> chunks;
  int ret = OB_SUCCESS;
  for (size_t pos = 0; ret == OB_SUCCESS && pos < data.size(); pos += Codec::OBJECT_CHUNK_BYTES) {
    uint64_t chunk = 0;
    ret = save_page(data.substr(pos, Codec::OBJECT_CHUNK_BYTES), chunk);
    if (ret == OB_SUCCESS) { chunks.push_back(chunk); }
  }
  uint64_t staged = 0;
  if (ret == OB_SUCCESS) { ret = save_page(Codec::encode_object(data.size(), chunks), staged); }
  if (ret == OB_SUCCESS) { object = staged; }
  return ret;
}

int InstanceNamespaceMetadata::read_object(uint64_t object, std::string &data)
{
  using Codec = ns::NamespaceCatalogCodec;
  std::string manifest, decoded;
  uint64_t size = 0;
  std::vector<uint64_t> chunks;
  int ret = read_page(object, manifest);
  if (ret == OB_SUCCESS && !Codec::decode_object(manifest, size, chunks)) {
    ret = OB_CHECKSUM_ERROR;
  }
  for (uint64_t chunk : chunks) {
    if (ret != OB_SUCCESS) { break; }
    std::string bytes;
    ret = read_page(chunk, bytes);
    if (ret == OB_SUCCESS && bytes.size()
        != std::min<uint64_t>(Codec::OBJECT_CHUNK_BYTES, size - decoded.size())) {
      ret = OB_CHECKSUM_ERROR;
    }
    if (ret == OB_SUCCESS) { decoded += bytes; }
  }
  if (ret == OB_SUCCESS) { data.swap(decoded); }
  return ret;
}

int InstanceNamespaceMetadata::find_tablet_source(ns::CatalogPageRef root,
    uint64_t logical_tablet, ns::CatalogTabletSource &source, int64_t &cap)
{
  InstanceCatalogPageStore pages(*this);
  ns::NamespaceCatalogTree tree(pages);
  ns::CatalogValue value;
  const auto result = tree.find(root, ns::NamespaceCatalogCodec::object_key(logical_tablet), value);
  if (!result.ok()) {
    return result.error == ns::CatalogTreeError::NOT_FOUND ? OB_ENTRY_NOT_EXIST
        : result.error == ns::CatalogTreeError::STORE ? result.store_error : OB_CHECKSUM_ERROR;
  }
  if (!ns::NamespaceCatalogCodec::decode_source(value.data, source)) { return OB_CHECKSUM_ERROR; }
  cap = value.cap;
  return OB_SUCCESS;
}

int InstanceNamespaceMetadata::read_table_definition(ns::CatalogPageRef root,
    uint64_t table_id, std::string &definition)
{
  InstanceCatalogPageStore pages(*this);
  ns::NamespaceCatalogTree tree(pages);
  ns::CatalogValue value;
  const auto result = tree.find(root, ns::NamespaceCatalogCodec::object_key(table_id), value);
  if (!result.ok()) {
    return result.error == ns::CatalogTreeError::NOT_FOUND ? OB_ENTRY_NOT_EXIST
        : result.error == ns::CatalogTreeError::STORE ? result.store_error : OB_CHECKSUM_ERROR;
  }
  uint64_t object = 0, table = 0, tablet = 0, bound = 0;
  if (!ns::NamespaceCatalogCodec::decode_entry(value.data, object, table, tablet, bound)
      || object == 0 || table != table_id || tablet != 0 || bound != 0) {
    return OB_CHECKSUM_ERROR;
  }
  return read_object(object, definition);
}

int InstanceNamespaceMetadata::stage_catalog_delta(uint64_t namespace_id,
    int64_t base_schema_version, int64_t schema_version,
    const ns::CatalogChanges &definitions, const ns::CatalogChanges &sources)
{
  if (base_schema_version < 0 || schema_version <= 0
      || schema_version < base_schema_version) { return OB_INVALID_ARGUMENT; }
  InstanceNamespaceRecord record;
  int ret = get_namespace(namespace_id, record, true);
  if (ret == OB_SUCCESS && record.roots.state != 0) { ret = OB_OP_NOT_ALLOW; }
  if (ret == OB_SUCCESS && record.roots.schema_version != base_schema_version) {
    ret = OB_EAGAIN;
  }
  if (ret == OB_SUCCESS) {
    InstanceCatalogPageStore pages(*this);
    ns::NamespaceCatalogTree tree(pages);
    auto result = tree.apply(record.roots.catalog, definitions, record.roots.catalog);
    if (result.ok()) {
      result = tree.apply(record.roots.directory, sources, record.roots.directory);
    }
    switch (result.error) {
      case ns::CatalogTreeError::NONE: break;
      case ns::CatalogTreeError::INVALID: ret = OB_INVALID_ARGUMENT; break;
      case ns::CatalogTreeError::TOO_LARGE:
      case ns::CatalogTreeError::TOO_DEEP: ret = OB_SIZE_OVERFLOW; break;
      case ns::CatalogTreeError::CORRUPT: ret = OB_CHECKSUM_ERROR; break;
      case ns::CatalogTreeError::STORE: ret = result.store_error; break;
      default: ret = OB_ERR_UNEXPECTED; break;
    }
    if (ret == OB_SUCCESS) {
      record.roots.schema_version = schema_version;
      ret = update_namespace(record);
    }
  }
  return ret;
}

int InstanceNamespaceMetadata::collect_unreachable_pages(
    int64_t max_deletes, int64_t &deleted)
{
  deleted = 0;
  if (!transaction_.is_directory_gc() || max_deletes <= 0 || max_deletes > 256) {
    return OB_INVALID_ARGUMENT;
  }
  // Only table-definition leaves own description objects. Source leaves have
  // physical identities, not object-page references.
  struct PendingTree { ns::CatalogPageRef ref; bool definitions; };
  std::vector<PendingTree> pending;
  int ret = scan_namespaces([&](const InstanceNamespaceRecord &record) {
    if (record.roots.catalog.page != 0) { pending.push_back({record.roots.catalog, true}); }
    if (record.roots.directory.page != 0) { pending.push_back({record.roots.directory, false}); }
    return OB_SUCCESS;
  });
  if (ret == OB_SUCCESS) {
    ret = scan_snapshots([&](uint64_t, const ns::CatalogRoots &roots) {
      if (roots.ref_count > 0) {
        if (roots.catalog.page != 0) { pending.push_back({roots.catalog, true}); }
        if (roots.directory.page != 0) { pending.push_back({roots.directory, false}); }
      }
      return OB_SUCCESS;
    });
  }
  // Active readers retain their KV snapshot, so logical page deletes may
  // proceed. Their historical page versions remain readable until the last
  // holder releases, including when deletes originate on another server.
  InstanceCatalogPageStore pages(*this);
  ns::NamespaceCatalogTree tree(pages);
  std::unordered_set<uint64_t> reachable;
  std::unordered_set<uint64_t> visited_nodes[2];
  std::unordered_set<uint64_t> visited_objects;
  while (ret == OB_SUCCESS && !pending.empty()) {
    const PendingTree item = pending.back();
    const ns::CatalogPageRef ref = item.ref;
    pending.pop_back();
    if (!visited_nodes[item.definitions].insert(ref.page).second) { continue; }
    reachable.insert(ref.page);
    ns::CatalogNode node;
    const auto result = tree.read_node(ref, node);
    if (!result.ok()) {
      switch (result.error) {
        case ns::CatalogTreeError::CORRUPT: ret = OB_CHECKSUM_ERROR; break;
        case ns::CatalogTreeError::TOO_DEEP: ret = OB_SIZE_OVERFLOW; break;
        case ns::CatalogTreeError::STORE: ret = result.store_error; break;
        default: ret = OB_ERR_UNEXPECTED; break;
      }
    } else if (!node.leaf) {
      for (const auto &child : node.children) { pending.push_back({child, item.definitions}); }
    } else if (item.definitions) {
      for (const auto &value : node.values) {
        uint64_t object = 0, table = 0, tablet = 0, bound = 0;
        if (!ns::NamespaceCatalogCodec::decode_entry(
                value.data, object, table, tablet, bound)) {
          ret = OB_CHECKSUM_ERROR;
          break;
        }
        if (object != 0 && visited_objects.insert(object).second) {
          reachable.insert(object);
          std::string manifest;
          uint64_t size = 0;
          std::vector<uint64_t> chunks;
          if (OB_SUCCESS != (ret = read_page(object, manifest))) { break; }
          if (!ns::NamespaceCatalogCodec::decode_object(manifest, size, chunks)) {
            ret = OB_CHECKSUM_ERROR;
            break;
          }
          for (uint64_t chunk : chunks) {
            std::string bytes;
            if (OB_SUCCESS != (ret = read_page(chunk, bytes))) { break; }
            const uint64_t expected = std::min<uint64_t>(
                ns::NamespaceCatalogCodec::OBJECT_CHUNK_BYTES, size);
            if (bytes.size() != expected) { ret = OB_CHECKSUM_ERROR; break; }
            size -= expected;
            reachable.insert(chunk);
          }
          if (ret != OB_SUCCESS) { break; }
        }
      }
    }
  }
  std::vector<uint64_t> garbage;
  if (ret == OB_SUCCESS) {
    ret = scan_pages([&](uint64_t page) {
      if (page == 0) { return OB_CHECKSUM_ERROR; }
      if (garbage.size() < static_cast<size_t>(max_deletes)
          && reachable.count(page) == 0) { garbage.push_back(page); }
      return OB_SUCCESS;
    });
  }
  for (const uint64_t page : garbage) {
    if (ret != OB_SUCCESS) { break; }
    ret = erase_page(page);
  }
  if (ret == OB_SUCCESS) { deleted = garbage.size(); }
  return ret;
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

int InstanceNamespaceMetadata::mark_namespace_deleting(uint64_t id, bool &done)
{
  done = false;
  if (id == 1) { return OB_OP_NOT_ALLOW; }
  InstanceNamespaceRecord record;
  int ret = get_namespace(id, record, true);
  if (ret == OB_SUCCESS && (record.roots.active_schema_changes != 0
      || record.roots.pending_schema_version != 0)) {
    ret = OB_EAGAIN;
  }
  if (ret == OB_SUCCESS) {
    if (record.roots.state == 2) {
      done = true;
    } else if (record.roots.state == 0) {
      record.roots.state = 1;
      ret = update_namespace(record);
    } else if (record.roots.state != 1) {
      ret = OB_STATE_NOT_MATCH;
    }
  }
  return ret;
}

int InstanceNamespaceMetadata::finish_namespace_drop(uint64_t id)
{
  if (id == 1) { return OB_OP_NOT_ALLOW; }
  InstanceNamespaceRecord record;
  int ret = get_namespace(id, record, true);
  if (ret == OB_SUCCESS && record.roots.state != 1) {
    ret = OB_STATE_NOT_MATCH;
  }
  if (ret == OB_SUCCESS) {
    const uint64_t snapshot_ref = record.roots.snapshot_ref;
    record.name.clear();
    record.roots = ns::CatalogRoots();
    record.roots.state = 2;
    ret = update_namespace(record);
    if (ret == OB_SUCCESS && snapshot_ref != 0) {
      InstanceSnapshotLineageStore lineage(*this);
      ret = ns::NamespaceSnapshotLineage::release(snapshot_ref, lineage);
    }
  }
  return ret;
}

int InstanceNamespaceMetadata::prune_deleted_namespace(uint64_t id,
    const NamespacePhysicalProbe &has_physical, bool &pruned)
{
  pruned = false;
  if (id <= 1 || !has_physical) { return OB_INVALID_ARGUMENT; }
  InstanceNamespaceRecord record;
  int ret = get_namespace(id, record, true);
  if (ret == OB_SUCCESS && record.roots.state != 2) { ret = OB_STATE_NOT_MATCH; }
  bool has_child = false;
  if (ret == OB_SUCCESS) {
    ret = scan_namespaces([&](const InstanceNamespaceRecord &other) {
      if (other.parent_namespace == id) { has_child = true; }
      return OB_SUCCESS;
    });
  }
  if (ret != OB_SUCCESS || has_child) { return ret; }
  bool has_owned = false;
  std::vector<uint64_t> exceptions;
  ret = scan_exceptions(id, [&](const InstanceExceptionRecord &exception) {
    if (exception.kind == 0) { has_owned = true; }
    exceptions.push_back(exception.tablet_id);
    return OB_SUCCESS;
  });
  if (ret != OB_SUCCESS || has_owned) { return ret; }
  bool physical = false;
  ret = has_physical(id, physical);
  if (ret != OB_SUCCESS || physical) { return ret; }
  for (const uint64_t tablet : exceptions) {
    if (ret == OB_SUCCESS) { ret = erase_exception(id, tablet); }
  }
  if (ret == OB_SUCCESS) { ret = erase_namespace(id); }
  if (ret == OB_SUCCESS) { pruned = true; }
  return ret;
}

int InstanceNamespaceMetadata::begin_schema_change(uint64_t id)
{
  InstanceNamespaceRecord record;
  int ret = get_namespace(id, record, true);
  if (ret == OB_SUCCESS && (record.roots.state != 0
      || record.roots.active_schema_changes == INT64_MAX)) {
    ret = OB_STATE_NOT_MATCH;
  }
  if (ret == OB_SUCCESS) {
    ++record.roots.active_schema_changes;
    ret = update_namespace(record);
  }
  return ret;
}

int InstanceNamespaceMetadata::finish_schema_change(uint64_t id,
    int64_t schema_version)
{
  if (schema_version < 0) { return OB_INVALID_ARGUMENT; }
  InstanceNamespaceRecord record;
  int ret = get_namespace(id, record, true);
  if (ret == OB_SUCCESS && (record.roots.state != 0
      || record.roots.active_schema_changes == 0)) {
    ret = OB_STATE_NOT_MATCH;
  }
  if (ret == OB_SUCCESS) {
    --record.roots.active_schema_changes;
    if (schema_version > record.roots.pending_schema_version) {
      record.roots.pending_schema_version = schema_version;
    }
    ret = update_namespace(record);
  }
  return ret;
}

int InstanceNamespaceMetadata::begin_schema_recovery(uint64_t id, bool &needed)
{
  needed = false;
  InstanceNamespaceRecord record;
  int ret = get_namespace(id, record, true);
  if (ret == OB_SUCCESS && record.roots.state != 0) { ret = OB_STATE_NOT_MATCH; }
  if (ret == OB_SUCCESS) {
    needed = record.roots.active_schema_changes != 0
        || record.roots.pending_schema_version != 0;
    if (needed) {
      record.roots.active_schema_changes = 0;
      record.roots.pending_schema_version = INT64_MAX;
      ret = update_namespace(record);
    }
  }
  return ret;
}

int InstanceNamespaceMetadata::finish_schema_recovery(uint64_t id,
    int64_t schema_version)
{
  if (schema_version <= 0) { return OB_INVALID_ARGUMENT; }
  InstanceNamespaceRecord record;
  int ret = get_namespace(id, record, true);
  if (ret == OB_SUCCESS && (record.roots.state != 0
      || record.roots.active_schema_changes != 0
      || record.roots.schema_version > schema_version)) {
    ret = OB_STATE_NOT_MATCH;
  }
  if (ret == OB_SUCCESS) {
    record.roots.schema_version = schema_version;
    record.roots.pending_schema_version = 0;
    ret = update_namespace(record);
  }
  return ret;
}

int InstanceNamespaceMetadata::stage_schema_delta(uint64_t id,
    int64_t base_schema_version, int64_t schema_version, int64_t drop_scn,
    const std::map<uint64_t, uint64_t> &previous_tablets,
    const std::map<uint64_t, uint64_t> &current_tablets,
    const PhysicalTabletProbe &probe,
    const PhysicalTabletBirthProbe &birth_probe,
    std::vector<uint64_t> &removed_owned)
{
  removed_owned.clear();
  if (base_schema_version <= 0 || schema_version < base_schema_version
      || drop_scn <= 0 || !probe || !birth_probe) {
    return OB_INVALID_ARGUMENT;
  }
  InstanceNamespaceRecord record;
  int ret = get_namespace(id, record, true);
  if (ret == OB_SUCCESS && record.roots.state != 0) {
    ret = OB_STATE_NOT_MATCH;
  } else if (ret == OB_SUCCESS && record.roots.schema_version != base_schema_version) {
    ret = OB_EAGAIN;
  }
  const auto actions = ns::NamespaceExceptionDelta::plan(
      previous_tablets, current_tablets);
  std::vector<uint64_t> pending_removal;
  for (const auto &action : actions) {
    if (ret != OB_SUCCESS) { break; }
    if (!ns::NamespaceObjectKey{id, action.tablet_id}.is_valid()
        || action.table_id == 0 || action.table_id >= (1ULL << 32)) {
      ret = OB_INVALID_ARGUMENT;
      break;
    }
    InstanceExceptionRecord old;
    ret = get_exception(id, action.tablet_id, old, true);
    const bool had_old = ret == OB_SUCCESS;
    if (ret == OB_ENTRY_NOT_EXIST) { ret = OB_SUCCESS; }
    if (ret != OB_SUCCESS) { break; }
    if (action.kind == ns::ExceptionDeltaKind::TOMBSTONE) {
      bool local_exists = false;
      ret = probe(action.tablet_id, local_exists);
      if (ret != OB_SUCCESS) { break; }
      const bool was_owned = local_exists || (had_old && (old.kind == 0 || old.was_owned));
      int64_t create_scn = had_old ? old.create_scn : 0;
      if (was_owned && create_scn == 0) {
        ret = birth_probe(action.tablet_id, create_scn);
        if (ret != OB_SUCCESS) { break; }
        if (create_scn <= 0) { ret = OB_STATE_NOT_MATCH; break; }
      }
      if (local_exists || (had_old && old.kind == 0)) {
        pending_removal.push_back(action.tablet_id);
      }
      ret = put_exception({id, action.tablet_id, action.table_id, 1,
          had_old && old.kind == 1 ? old.drop_scn : drop_scn, was_owned, create_scn});
    } else {
      bool local_exists = false;
      ret = probe(action.tablet_id, local_exists);
      if (ret != OB_SUCCESS) { break; }
      if (local_exists) {
        ret = put_exception({id, action.tablet_id, action.table_id, 0, 0});
      } else if (had_old && old.kind == 0) {
        ret = OB_STATE_NOT_MATCH;
      } else if (had_old) {
        ret = erase_exception(id, action.tablet_id);
      }
    }
  }
  if (ret == OB_SUCCESS) {
    bool changed = false;
    if (schema_version > record.roots.schema_version) {
      record.roots.schema_version = schema_version;
      changed = true;
    }
    if (record.roots.pending_schema_version > 0
        && record.roots.pending_schema_version != INT64_MAX
        && record.roots.pending_schema_version <= schema_version) {
      record.roots.pending_schema_version = 0;
      changed = true;
    }
    if (changed) { ret = update_namespace(record); }
  }
  if (ret == OB_SUCCESS) { removed_owned.swap(pending_removal); }
  return ret;
}

int InstanceNamespaceMetadata::reconcile_owned_tablets(uint64_t id,
    int64_t base_schema_version, int64_t current_schema_version,
    const std::map<uint64_t, uint64_t> &current_tablets,
    const PhysicalTabletProbe &probe)
{
  if (base_schema_version <= 0 || current_schema_version < base_schema_version
      || !probe) { return OB_INVALID_ARGUMENT; }
  InstanceNamespaceRecord record;
  int ret = get_namespace(id, record, true);
  if (ret == OB_SUCCESS && record.roots.state != 0) {
    ret = OB_STATE_NOT_MATCH;
  } else if (ret == OB_SUCCESS && (record.roots.schema_version != base_schema_version
      || record.roots.active_schema_changes != 0
      || (record.roots.pending_schema_version != INT64_MAX
          && record.roots.pending_schema_version > current_schema_version))) {
    ret = OB_EAGAIN;
  }
  for (const auto &tablet : current_tablets) {
    if (ret != OB_SUCCESS) { break; }
    if (!ns::NamespaceObjectKey{id, tablet.first}.is_valid()
        || tablet.second == 0 || tablet.second >= (1ULL << 32)) {
      ret = OB_INVALID_ARGUMENT;
      break;
    }
    bool local_exists = false;
    ret = probe(tablet.first, local_exists);
    if (ret != OB_SUCCESS) { break; }
    InstanceExceptionRecord old;
    ret = get_exception(id, tablet.first, old, true);
    const bool had_old = ret == OB_SUCCESS;
    if (ret == OB_ENTRY_NOT_EXIST) { ret = OB_SUCCESS; }
    if (ret != OB_SUCCESS) { break; }
    if (local_exists) {
      if (had_old && old.kind == 1) {
        ret = OB_STATE_NOT_MATCH;
      } else if (!had_old || old.table_id != tablet.second) {
        ret = put_exception({id, tablet.first, tablet.second, 0, 0});
      }
    } else if (had_old && old.kind == 0) {
      ret = OB_EAGAIN;
    }
  }
  return ret;
}

namespace
{
int finish_directory_transaction(storage::InstanceMetaStore &store,
    storage::InstanceMetaStore::Transaction &tx, int ret)
{
  if (tx.is_active()) {
    const int end_ret = ret == OB_SUCCESS ? store.commit(tx) : store.rollback(tx);
    if (ret == OB_SUCCESS) { ret = end_ret; }
  }
  return ret;
}
} // namespace

int InstanceNamespaceDirectory::ensure_root(const std::string &name,
    int64_t schema_version, int64_t gc_watermark, int64_t deadline, bool &created)
{
  created = false;
  if (name.empty() || name.size() > 128 || schema_version <= 0
      || gc_watermark < 0) { return OB_INVALID_ARGUMENT; }
  storage::InstanceMetaStore::Transaction tx;
  int ret = store_.begin(tx, deadline);
  bool staged = false;
  if (ret == OB_SUCCESS) {
    InstanceNamespaceMetadata metadata(store_, tx);
    InstanceNamespaceRecord root;
    // Missing native rows must be inserted without first taking a row lock:
    // this DML path cannot turn a lock on absence into a later insert.
    ret = metadata.get_namespace(1, root);
    if (ret == OB_ENTRY_NOT_EXIST) {
      // The freeze detector can initialize coordination before the Namespace
      // directory. Preserve that record and its monotonic watermark.
      int64_t stored_watermark = 0;
      ret = metadata.get_snapshot_gc_watermark(stored_watermark);
      if (ret == OB_ENTRY_NOT_EXIST) {
        ret = metadata.initialize_snapshot_gc_watermark(gc_watermark);
      } else if (ret == OB_SUCCESS && stored_watermark < gc_watermark) {
        ret = metadata.advance_snapshot_gc_watermark(gc_watermark);
      }
      if (ret == OB_SUCCESS) { ret = metadata.insert_root_namespace(name, schema_version); }
      staged = ret == OB_SUCCESS;
    } else if (ret == OB_SUCCESS) {
      ret = metadata.get_namespace(1, root, true);
    }
    if (ret == OB_SUCCESS && !staged) {
      if (root.name != name || root.roots.state != 0
          || root.parent_namespace != 0 || root.fork_cap != 0
          || root.roots.schema_version <= 0
          || root.roots.schema_version > schema_version) {
        ret = OB_STATE_NOT_MATCH;
      } else {
        int64_t stored_watermark = 0;
        ret = metadata.get_snapshot_gc_watermark(stored_watermark, true);
        if (ret == OB_ENTRY_NOT_EXIST) { ret = OB_STATE_NOT_MATCH; }
        if (ret == OB_SUCCESS && stored_watermark < gc_watermark) {
          ret = metadata.advance_snapshot_gc_watermark(gc_watermark);
        }
        // Startup must reconcile any committed SQL DDL from this published
        // base; advancing here would discard its missing exception delta.
      }
    }
  }
  ret = finish_directory_transaction(store_, tx, ret);
  if (ret == OB_SUCCESS) { created = staged; }
  return ret;
}

int InstanceNamespaceDirectory::fork_namespace(const std::string &source_name,
    const std::string &target_name,
    const InstanceNamespaceMetadata::SnapshotAcquirer &acquire_snapshot,
    int64_t deadline, InstanceNamespaceRecord &child)
{
  child = InstanceNamespaceRecord();
  storage::InstanceMetaStore::Transaction tx;
  int ret = store_.begin(tx, deadline);
  InstanceNamespaceRecord staged;
  if (ret == OB_SUCCESS) {
    InstanceNamespaceMetadata metadata(store_, tx);
    uint64_t child_id = 0;
    ret = metadata.fork_namespace(source_name, target_name, acquire_snapshot, child_id);
    if (ret == OB_SUCCESS) { ret = metadata.get_namespace(child_id, staged); }
    if (ret == OB_SUCCESS && (staged.roots.state != 0
        || staged.parent_namespace == 0 || staged.fork_cap <= 0
        || staged.name != target_name)) { ret = OB_STATE_NOT_MATCH; }
  }
  ret = finish_directory_transaction(store_, tx, ret);
  if (ret == OB_SUCCESS) { child = std::move(staged); }
  return ret;
}

int InstanceNamespaceDirectory::find_live(const std::string &name,
    int64_t deadline, InstanceNamespaceRecord &record)
{
  int ret = find_named(name, deadline, record);
  if (ret == OB_SUCCESS && record.roots.state != 0) {
    record = InstanceNamespaceRecord();
    ret = OB_ENTRY_NOT_EXIST;
  }
  return ret;
}

int InstanceNamespaceDirectory::find_named(const std::string &name,
    int64_t deadline, InstanceNamespaceRecord &record)
{
  record = InstanceNamespaceRecord();
  storage::InstanceMetaStore::Transaction tx;
  int ret = store_.begin(tx, deadline, true);
  InstanceNamespaceRecord found;
  if (ret == OB_SUCCESS) {
    InstanceNamespaceMetadata metadata(store_, tx);
    uint64_t id = 0;
    ret = metadata.find_namespace(name, id);
    if (ret == OB_SUCCESS) { ret = metadata.get_namespace(id, found); }
    if (ret == OB_SUCCESS && found.name != name) { ret = OB_STATE_NOT_MATCH; }
  }
  ret = finish_directory_transaction(store_, tx, ret);
  if (ret == OB_SUCCESS) { record = std::move(found); }
  return ret;
}

int InstanceNamespaceDirectory::get(uint64_t id,
    int64_t deadline, InstanceNamespaceRecord &record)
{
  record = InstanceNamespaceRecord();
  if (id == 0) { return OB_INVALID_ARGUMENT; }
  storage::InstanceMetaStore::Transaction tx;
  int ret = store_.begin(tx, deadline, true);
  InstanceNamespaceRecord found;
  if (ret == OB_SUCCESS) {
    InstanceNamespaceMetadata metadata(store_, tx);
    ret = metadata.get_namespace(id, found);
  }
  ret = finish_directory_transaction(store_, tx, ret);
  if (ret == OB_SUCCESS) { record = std::move(found); }
  return ret;
}

int InstanceNamespaceDirectory::acquire_read_view(uint64_t namespace_id,
    int64_t deadline, const storage::InstanceMetaStore::SnapshotAcquirer &acquire,
    ns::NamespaceCatalogViews::Handle &view,
    const ns::NamespaceCatalogViews::Handle &previous)
{
  if (namespace_id == 0 || view || !acquire) { return OB_INVALID_ARGUMENT; }
  storage::InstanceMetaStore::Transaction tx;
  int ret = store_.begin_read(tx, deadline, acquire);
  ns::NamespaceCatalogViews::Handle held;
  if (ret == OB_SUCCESS && previous && previous->entry().namespace_id == namespace_id
      && previous->entry().snapshot == tx.snapshot_version().get_val_for_tx()) {
    // RR already holds this immutable root and its KV snapshot lease. Reuse
    // both without consulting the Namespace row again.
    held = previous;
  } else if (ret == OB_SUCCESS) {
    InstanceNamespaceMetadata metadata(store_, tx);
    InstanceNamespaceRecord record;
    ret = metadata.get_namespace(namespace_id, record);
    if (ret == OB_SUCCESS && record.roots.state != 0) { ret = OB_OP_NOT_ALLOW; }
    storage::InstanceMetaStore::SnapshotHandle retention;
    if (ret == OB_SUCCESS) { ret = store_.retain_snapshot(tx, retention); }
    if (ret == OB_SUCCESS) {
      held = ns::namespace_registry().catalog_views().hold(
          namespace_id, tx.snapshot_version().get_val_for_tx(), record.roots, std::move(retention));
      if (!held) { ret = OB_STATE_NOT_MATCH; }
    }
  }
  ret = finish_directory_transaction(store_, tx, ret);
  if (ret == OB_SUCCESS) { view = std::move(held); }
  return ret;
}

int InstanceNamespaceDirectory::filter_unreferenced_tablets(
    const std::vector<uint64_t> &candidates,
    const std::map<uint64_t, uint64_t> &sources, int64_t deadline,
    std::vector<uint64_t> &unreferenced, bool &need_retry)
{
  unreferenced.clear();
  need_retry = false;
  storage::InstanceMetaStore::Transaction tx;
  int ret = store_.begin(tx, deadline, true);
  std::vector<ns::CatalogPageRef> live;
  std::unordered_set<uint64_t> referenced;
  std::vector<uint64_t> staged;
  if (ret == OB_SUCCESS) {
    InstanceNamespaceMetadata metadata(store_, tx);
    ret = metadata.scan_namespaces([&](const InstanceNamespaceRecord &record) {
      // DELETING still has admitted users until its access drain completes.
      if (record.roots.state != 2 && record.roots.directory.page != 0) {
        live.push_back(record.roots.directory);
      }
      return OB_SUCCESS;
    });
    auto mark_source = [&](uint64_t physical) {
      bool terminated = false;
      for (int depth = 0; depth < 64; ++depth) {
        referenced.insert(physical);
        const auto source = sources.find(physical);
        if (source == sources.end()) { terminated = true; break; }
        physical = source->second;
      }
      return terminated ? OB_SUCCESS : OB_SIZE_OVERFLOW;
    };
    // This pass decides physical existence only. Caps and creation identities
    // stay in the source entries; never infer an address from Namespace ancestry.
    // Shared pages in this KV snapshot need to be visited only once.
    auto mark_roots = [&](InstanceNamespaceMetadata &reader,
                          const std::vector<ns::CatalogPageRef> &roots) {
      InstanceCatalogPageStore pages(reader);
      ns::NamespaceCatalogTree tree(pages);
      std::unordered_set<uint64_t> visited;
      std::unordered_set<uint64_t> path;
      std::function<int(ns::CatalogPageRef, int)> walk;
      walk = [&](ns::CatalogPageRef ref, int depth) -> int {
        if (ref.page == 0) { return OB_SUCCESS; }
        if (path.count(ref.page) != 0) { return OB_CHECKSUM_ERROR; }
        if (visited.count(ref.page) != 0) { return OB_SUCCESS; }
        if (depth >= 64) { return OB_SIZE_OVERFLOW; }
        path.insert(ref.page);
        ns::CatalogNode node;
        const auto result = tree.read_node(ref, node);
        int rc = result.ok() ? OB_SUCCESS
            : result.error == ns::CatalogTreeError::STORE ? result.store_error : OB_CHECKSUM_ERROR;
        if (rc == OB_SUCCESS && node.leaf) {
          for (const auto &value : node.values) {
            ns::CatalogTabletSource source;
            if (!ns::NamespaceCatalogCodec::decode_source(value.data, source)) {
              rc = OB_CHECKSUM_ERROR;
            } else {
              rc = mark_source(source.physical_tablet_id);
            }
            if (rc != OB_SUCCESS) { break; }
          }
        } else if (rc == OB_SUCCESS) {
          for (const auto &child : node.children) {
            rc = walk(child, depth + 1);
            if (rc != OB_SUCCESS) { break; }
          }
        }
        path.erase(ref.page);
        if (rc == OB_SUCCESS) { visited.insert(ref.page); }
        return rc;
      };
      int rc = OB_SUCCESS;
      for (const auto &root : roots) {
        rc = walk(root, 0);
        if (rc != OB_SUCCESS) { break; }
      }
      return rc;
    };
    if (ret == OB_SUCCESS) { ret = mark_roots(metadata, live); }
    // A transaction may have acquired its view before DROP, without opening
    // this tablet yet. Its immutable root is a dependency even without a native
    // tablet handle. Include incomplete physical fork edges as for live roots.
    std::vector<ns::NamespaceCatalogViews::Entry> views;
    ns::namespace_registry().catalog_views().list(views);
    for (const auto &view : views) {
      if (ret != OB_SUCCESS) { break; }
      storage::InstanceMetaStore::Transaction view_tx;
      ret = store_.begin_read(view_tx, deadline, [&](share::SCN &snapshot) {
        return snapshot.convert_for_tx(view.snapshot);
      });
      InstanceNamespaceMetadata view_metadata(store_, view_tx);
      if (ret == OB_SUCCESS) { ret = mark_roots(view_metadata, {view.roots.directory}); }
      ret = finish_directory_transaction(store_, view_tx, ret);
    }
    if (ret == OB_SUCCESS) {
      for (uint64_t candidate : candidates) {
        if (referenced.count(candidate) == 0) { staged.push_back(candidate); }
        else { need_retry = true; }
      }
    }
  }
  ret = finish_directory_transaction(store_, tx, ret);
  if (ret == OB_SUCCESS) { unreferenced.swap(staged); }
  return ret;
}

int InstanceNamespaceDirectory::list_live(int64_t deadline,
    std::vector<InstanceNamespaceRecord> &records)
{
  records.clear();
  storage::InstanceMetaStore::Transaction tx;
  int ret = store_.begin(tx, deadline, true);
  std::vector<InstanceNamespaceRecord> staged;
  if (ret == OB_SUCCESS) {
    InstanceNamespaceMetadata metadata(store_, tx);
    ret = metadata.scan_namespaces([&](const InstanceNamespaceRecord &record) {
      if (record.roots.state == 0) {
        if (record.name.empty()) { return OB_CHECKSUM_ERROR; }
        staged.push_back(record);
      }
      return OB_SUCCESS;
    });
  }
  ret = finish_directory_transaction(store_, tx, ret);
  if (ret == OB_SUCCESS) { records.swap(staged); }
  return ret;
}

int InstanceNamespaceDirectory::list_deleted(int64_t deadline,
    std::vector<InstanceNamespaceRecord> &records)
{
  records.clear();
  storage::InstanceMetaStore::Transaction tx;
  int ret = store_.begin(tx, deadline, true);
  std::vector<InstanceNamespaceRecord> staged;
  if (ret == OB_SUCCESS) {
    InstanceNamespaceMetadata metadata(store_, tx);
    ret = metadata.scan_namespaces([&](const InstanceNamespaceRecord &record) {
      if (record.roots.state == 2) { staged.push_back(record); }
      return OB_SUCCESS;
    });
  }
  ret = finish_directory_transaction(store_, tx, ret);
  if (ret == OB_SUCCESS) { records.swap(staged); }
  return ret;
}

int InstanceNamespaceDirectory::rename_live(uint64_t id,
    const std::string &expected_name, const std::string &new_name, int64_t deadline)
{
  storage::InstanceMetaStore::Transaction tx;
  int ret = store_.begin(tx, deadline);
  if (ret == OB_SUCCESS) {
    InstanceNamespaceMetadata metadata(store_, tx);
    ret = metadata.rename_namespace(id, expected_name, new_name);
  }
  return finish_directory_transaction(store_, tx, ret);
}

int InstanceNamespaceDirectory::mark_deleting(uint64_t id,
    const std::string &expected_name, int64_t deadline, bool &done)
{
  done = false;
  if (id == 0 || expected_name.empty()) { return OB_INVALID_ARGUMENT; }
  storage::InstanceMetaStore::Transaction tx;
  int ret = store_.begin(tx, deadline);
  bool staged_done = false;
  if (ret == OB_SUCCESS) {
    InstanceNamespaceMetadata metadata(store_, tx);
    InstanceNamespaceRecord record;
    ret = metadata.get_namespace(id, record, true);
    if (ret == OB_SUCCESS && record.roots.state != 2
        && record.name != expected_name) {
      ret = OB_STATE_NOT_MATCH;
    }
    if (ret == OB_SUCCESS) {
      ret = metadata.mark_namespace_deleting(id, staged_done);
    }
  }
  ret = finish_directory_transaction(store_, tx, ret);
  if (ret == OB_SUCCESS) { done = staged_done; }
  return ret;
}

int InstanceNamespaceDirectory::list_deleting_owned(uint64_t id,
    int64_t deadline, std::vector<uint64_t> &local_tablets)
{
  local_tablets.clear();
  if (id <= 1) { return OB_INVALID_ARGUMENT; }
  storage::InstanceMetaStore::Transaction tx;
  int ret = store_.begin(tx, deadline, true);
  std::vector<uint64_t> staged;
  if (ret == OB_SUCCESS) {
    InstanceNamespaceMetadata metadata(store_, tx);
    InstanceNamespaceRecord record;
    ret = metadata.get_namespace(id, record);
    if (ret == OB_SUCCESS && record.roots.state != 1) {
      ret = OB_STATE_NOT_MATCH;
    }
    if (ret == OB_SUCCESS) {
      ret = metadata.scan_exceptions(id, [&](const InstanceExceptionRecord &exception) {
        if (exception.kind == 0) { staged.push_back(exception.tablet_id); }
        return OB_SUCCESS;
      });
    }
  }
  ret = finish_directory_transaction(store_, tx, ret);
  if (ret == OB_SUCCESS) { local_tablets.swap(staged); }
  return ret;
}

int InstanceNamespaceDirectory::list_deleted_owned(uint64_t id,
    int64_t deadline, std::vector<uint64_t> &local_tablets)
{
  local_tablets.clear();
  if (id <= 1) { return OB_INVALID_ARGUMENT; }
  storage::InstanceMetaStore::Transaction tx;
  int ret = store_.begin(tx, deadline, true);
  std::vector<uint64_t> staged;
  if (ret == OB_SUCCESS) {
    InstanceNamespaceMetadata metadata(store_, tx);
    InstanceNamespaceRecord record;
    ret = metadata.get_namespace(id, record);
    if (ret == OB_SUCCESS && record.roots.state != 2) { ret = OB_STATE_NOT_MATCH; }
    if (ret == OB_SUCCESS) {
      ret = metadata.scan_exceptions(id, [&](const InstanceExceptionRecord &exception) {
        if (exception.kind == 0) { staged.push_back(exception.tablet_id); }
        return OB_SUCCESS;
      });
    }
  }
  ret = finish_directory_transaction(store_, tx, ret);
  if (ret == OB_SUCCESS) { local_tablets.swap(staged); }
  return ret;
}

int InstanceNamespaceDirectory::erase_deleted_owned(uint64_t id,
    const std::vector<uint64_t> &local_tablets, int64_t deadline)
{
  if (id <= 1) { return OB_INVALID_ARGUMENT; }
  storage::InstanceMetaStore::Transaction tx;
  int ret = store_.begin(tx, deadline);
  if (ret == OB_SUCCESS) {
    InstanceNamespaceMetadata metadata(store_, tx);
    InstanceNamespaceRecord record;
    ret = metadata.get_namespace(id, record, true);
    if (ret == OB_SUCCESS && record.roots.state != 2) { ret = OB_STATE_NOT_MATCH; }
    for (uint64_t local : local_tablets) {
      if (ret != OB_SUCCESS) { break; }
      InstanceExceptionRecord exception;
      ret = metadata.get_exception(id, local, exception, true);
      if (ret == OB_ENTRY_NOT_EXIST) { ret = OB_SUCCESS; }
      else if (ret == OB_SUCCESS && exception.kind != 0) { ret = OB_STATE_NOT_MATCH; }
      else if (ret == OB_SUCCESS) { ret = metadata.erase_exception(id, local); }
    }
  }
  return finish_directory_transaction(store_, tx, ret);
}

int InstanceNamespaceDirectory::prune_deleted(uint64_t id,
    const InstanceNamespaceMetadata::NamespacePhysicalProbe &has_physical,
    int64_t deadline, bool &pruned)
{
  pruned = false;
  storage::InstanceMetaStore::Transaction tx;
  int ret = store_.begin(tx, deadline);
  bool staged = false;
  if (ret == OB_SUCCESS) {
    InstanceNamespaceMetadata metadata(store_, tx);
    ret = metadata.prune_deleted_namespace(id, has_physical, staged);
  }
  ret = finish_directory_transaction(store_, tx, ret);
  if (ret == OB_SUCCESS) { pruned = staged; }
  return ret;
}

int InstanceNamespaceDirectory::finish_drop(uint64_t id, int64_t deadline)
{
  if (id <= 1) { return OB_INVALID_ARGUMENT; }
  storage::InstanceMetaStore::Transaction tx;
  int ret = store_.begin(tx, deadline);
  if (ret == OB_SUCCESS) {
    InstanceNamespaceMetadata metadata(store_, tx);
    ret = metadata.finish_namespace_drop(id);
  }
  return finish_directory_transaction(store_, tx, ret);
}

int InstanceNamespaceDirectory::schema_version(uint64_t id,
    int64_t deadline, int64_t &version)
{
  version = 0;
  if (id == 0) { return OB_INVALID_ARGUMENT; }
  storage::InstanceMetaStore::Transaction tx;
  int ret = store_.begin(tx, deadline, true);
  int64_t staged = 0;
  if (ret == OB_SUCCESS) {
    InstanceNamespaceMetadata metadata(store_, tx);
    InstanceNamespaceRecord record;
    ret = metadata.get_namespace(id, record);
    if (ret == OB_SUCCESS && record.roots.state != 0) {
      ret = OB_ENTRY_NOT_EXIST;
    }
    if (ret == OB_SUCCESS) { staged = record.roots.schema_version; }
  }
  ret = finish_directory_transaction(store_, tx, ret);
  if (ret == OB_SUCCESS) { version = staged; }
  return ret;
}

int InstanceNamespaceDirectory::begin_schema_change(uint64_t id, int64_t deadline)
{
  storage::InstanceMetaStore::Transaction tx;
  int ret = store_.begin(tx, deadline);
  if (ret == OB_SUCCESS) {
    InstanceNamespaceMetadata metadata(store_, tx);
    ret = metadata.begin_schema_change(id);
  }
  return finish_directory_transaction(store_, tx, ret);
}

int InstanceNamespaceDirectory::finish_schema_change(uint64_t id,
    int64_t version, int64_t deadline)
{
  storage::InstanceMetaStore::Transaction tx;
  int ret = store_.begin(tx, deadline);
  if (ret == OB_SUCCESS) {
    InstanceNamespaceMetadata metadata(store_, tx);
    ret = metadata.finish_schema_change(id, version);
  }
  return finish_directory_transaction(store_, tx, ret);
}

int InstanceNamespaceDirectory::begin_schema_recovery(uint64_t id,
    int64_t deadline, bool &needed)
{
  needed = false;
  storage::InstanceMetaStore::Transaction tx;
  int ret = store_.begin(tx, deadline);
  bool staged = false;
  if (ret == OB_SUCCESS) {
    InstanceNamespaceMetadata metadata(store_, tx);
    ret = metadata.begin_schema_recovery(id, staged);
  }
  ret = finish_directory_transaction(store_, tx, ret);
  if (ret == OB_SUCCESS) { needed = staged; }
  return ret;
}

int InstanceNamespaceDirectory::finish_schema_recovery(uint64_t id,
    int64_t version, int64_t deadline)
{
  storage::InstanceMetaStore::Transaction tx;
  int ret = store_.begin(tx, deadline);
  if (ret == OB_SUCCESS) {
    InstanceNamespaceMetadata metadata(store_, tx);
    ret = metadata.finish_schema_recovery(id, version);
  }
  return finish_directory_transaction(store_, tx, ret);
}

int InstanceNamespaceDirectory::publish_schema_delta(uint64_t id,
    int64_t base_version, int64_t version, int64_t drop_scn,
    const std::map<uint64_t, uint64_t> &previous_tablets,
    const std::map<uint64_t, uint64_t> &current_tablets,
    const InstanceNamespaceMetadata::PhysicalTabletProbe &probe,
    const InstanceNamespaceMetadata::PhysicalTabletBirthProbe &birth_probe,
    int64_t deadline, std::vector<uint64_t> &removed_owned)
{
  removed_owned.clear();
  storage::InstanceMetaStore::Transaction tx;
  int ret = store_.begin(tx, deadline);
  std::vector<uint64_t> staged;
  if (ret == OB_SUCCESS) {
    InstanceNamespaceMetadata metadata(store_, tx);
    ret = metadata.stage_schema_delta(id, base_version, version, drop_scn,
        previous_tablets, current_tablets, probe, birth_probe, staged);
  }
  ret = finish_directory_transaction(store_, tx, ret);
  if (ret == OB_SUCCESS) { removed_owned.swap(staged); }
  return ret;
}

int InstanceNamespaceDirectory::reconcile_owned(uint64_t id,
    int64_t base_version, int64_t schema_version,
    const std::map<uint64_t, uint64_t> &current_tablets,
    const InstanceNamespaceMetadata::PhysicalTabletProbe &probe,
    int64_t deadline)
{
  storage::InstanceMetaStore::Transaction tx;
  int ret = store_.begin(tx, deadline);
  if (ret == OB_SUCCESS) {
    InstanceNamespaceMetadata metadata(store_, tx);
    ret = metadata.reconcile_owned_tablets(id, base_version, schema_version,
        current_tablets, probe);
  }
  return finish_directory_transaction(store_, tx, ret);
}

} // namespace rootserver
} // namespace oceanbase
