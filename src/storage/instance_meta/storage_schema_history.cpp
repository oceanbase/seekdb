/*
 * Copyright (c) 2025 OceanBase.
 * Licensed under the Apache License, Version 2.0.
 */

#define USING_LOG_PREFIX STORAGE
#include "storage/instance_meta/storage_schema_history.h"
#include "storage/ob_storage_schema.h"
#include "lib/allocator/ob_allocator.h"
#include "storage/compaction/ob_freeze_info_mgr.h"
#include "storage/tx/ob_trans_service.h"

namespace oceanbase
{
using namespace common;
namespace storage
{
namespace
{
// A head selects the definition visible at the transaction snapshot. Bodies
// are immutable and version-addressed, so an existing physical descriptor can
// still load its complete definition after the old head MVCC version is gone.
// Version -1 denotes the head; real definition versions include bootstrap 0.
constexpr int64_t LAYOUT_HEADER_SIZE = 2 * sizeof(int64_t);
constexpr int64_t LAYOUT_CHUNK_SIZE = InstanceMetaStore::MAX_VALUE_LENGTH - LAYOUT_HEADER_SIZE;

ObString layout_key(uint64_t id, int64_t version, int64_t chunk, char (&bytes)[24])
{
  int64_t pos = 0;
  (void)serialization::encode_i64(bytes, sizeof(bytes), pos, id);
  (void)serialization::encode_i64(bytes, sizeof(bytes), pos, version);
  (void)serialization::encode_i64(bytes, sizeof(bytes), pos, chunk);
  return ObString(sizeof(bytes), bytes);
}

int decode_layout_header(const ObString &row, bool head, int64_t &size, int64_t &version)
{
  int ret = OB_SUCCESS;
  int64_t pos = 0;
  if (row.length() < LAYOUT_HEADER_SIZE) {
    ret = OB_CHECKSUM_ERROR;
  } else if (OB_FAIL(serialization::decode_i64(row.ptr(), row.length(), pos, &size))) {
  } else if (OB_FAIL(serialization::decode_i64(row.ptr(), row.length(), pos, &version))) {
  } else if (size <= 0 || size > INT32_MAX || version < 0
      || row.length() != LAYOUT_HEADER_SIZE + (head ? 0 : std::min(size, LAYOUT_CHUNK_SIZE))) {
    ret = OB_CHECKSUM_ERROR;
  }
  return ret;
}
} // namespace

int StorageSchemaHistory::create(uint64_t layout_id, const ObStorageSchema &schema)
{
  return write(layout_id, schema, true);
}

int StorageSchemaHistory::publish(uint64_t layout_id, const ObStorageSchema &schema)
{
  return write(layout_id, schema, false);
}

int StorageSchemaHistory::read_version(uint64_t layout_id, int64_t &schema_version)
{
  ObArenaAllocator allocator(ObMemAttr("SchemaHistory"));
  char key_buf[24];
  ObString row;
  int64_t size = 0;
  schema_version = OB_INVALID_VERSION;
  int ret = layout_id == 0 || layout_id == OB_INVALID_ID ? OB_INVALID_ARGUMENT
      : store_.get(tx_, MetaCollection::STORAGE_LAYOUTS,
          layout_key(layout_id, -1, 0, key_buf), allocator, row);
  if (ret == OB_SUCCESS) { ret = decode_layout_header(row, true, size, schema_version); }
  return ret;
}

int StorageSchemaHistory::begin_read_at(InstanceMetaStore &store, InstanceMetaStore::Transaction &tx,
    const share::SCN &target, int64_t deadline)
{
  auto *freezes = share::server_service<ObFreezeInfoMgr>();
  auto *transactions = share::server_service<transaction::ObTransService>();
  if (freezes == nullptr || transactions == nullptr) { return OB_NOT_INIT; }
  return store.begin_read(tx, deadline, [&](share::SCN &snapshot) {
    share::SCN retained, readable;
    int rc = freezes->get_schema_history_retention(retained);
    if (rc == OB_SUCCESS && target < retained) { rc = OB_SNAPSHOT_DISCARDED; }
    if (rc == OB_SUCCESS) { rc = transactions->get_weak_read_snapshot_version(-1, readable); }
    if (rc == OB_SUCCESS && target > readable) { rc = OB_EAGAIN; }
    if (rc == OB_SUCCESS) { snapshot = target; }
    return rc;
  });
}

int StorageSchemaHistory::read_at(InstanceMetaStore &store, uint64_t layout_id,
    const share::SCN &target, int64_t deadline,
    common::ObIAllocator &allocator, ObStorageSchema &schema)
{
  InstanceMetaStore::Transaction tx;
  int ret = begin_read_at(store, tx, target, deadline);
  if (ret == OB_SUCCESS) { ret = StorageSchemaHistory(store, tx).read(layout_id, allocator, schema); }
  if (tx.is_active()) {
    const int end = store.commit(tx);
    if (ret == OB_SUCCESS) { ret = end; }
  }
  return ret;
}

int StorageSchemaHistory::read_current(InstanceMetaStore &store, uint64_t layout_id,
    const share::SCN &minimum_target, int64_t deadline,
    common::ObIAllocator &allocator, share::SCN &target, ObStorageSchema &schema)
{
  target.reset();
  if (!minimum_target.is_valid() || minimum_target.is_min() || minimum_target.is_max()) {
    return OB_INVALID_ARGUMENT;
  }
  InstanceMetaStore::Transaction tx;
  int ret = store.begin_weak_read(tx, deadline);
  if (ret == OB_SUCCESS && tx.snapshot_version() < minimum_target) { ret = OB_EAGAIN; }
  if (ret == OB_SUCCESS) { ret = StorageSchemaHistory(store, tx).read(layout_id, allocator, schema); }
  if (ret == OB_SUCCESS) { target = tx.snapshot_version(); }
  if (tx.is_active()) {
    const int end = store.commit(tx);
    if (ret == OB_SUCCESS) { ret = end; }
  }
  return ret;
}

int StorageSchemaHistory::write(uint64_t layout_id, const ObStorageSchema &schema, bool create)
{
  int ret = OB_SUCCESS;
  ObArenaAllocator allocator(ObMemAttr("SchemaHistory"));
  const int64_t size = schema.get_serialize_size();
  const int64_t version = schema.get_schema_version();
  int64_t old_size = 0;
  int64_t old_version = 0;
  char key_buf[24];
  ObString old_row;
  char *bytes = nullptr;
  char *first = nullptr;
  if (layout_id == 0 || layout_id == OB_INVALID_ID || !schema.is_valid()
      || schema.is_column_info_simplified() || version < 0) {
    ret = OB_INVALID_ARGUMENT;
  } else if (size <= 0 || size > INT32_MAX) {
    ret = OB_SIZE_OVERFLOW;
  } else if (!create && OB_FAIL(store_.get_for_update(tx_, MetaCollection::STORAGE_LAYOUTS,
      layout_key(layout_id, -1, 0, key_buf), allocator, old_row))) {
  } else if (!create && OB_FAIL(decode_layout_header(old_row, true, old_size, old_version))) {
  } else if (!create && version <= old_version) {
    // Compare versions only within this stable physical layout identity.
    ret = OB_STATE_NOT_MATCH;
  } else if (nullptr == (bytes = static_cast<char *>(allocator.alloc(size)))
      || nullptr == (first = static_cast<char *>(allocator.alloc(
          LAYOUT_HEADER_SIZE + std::min(size, LAYOUT_CHUNK_SIZE))))) {
    ret = OB_ALLOCATE_MEMORY_FAILED;
  } else {
    int64_t pos = 0;
    if (OB_FAIL(schema.serialize(bytes, size, pos))) {
    } else if (pos != size) {
      ret = OB_ERR_UNEXPECTED;
    } else {
      pos = 0;
      const int64_t first_size = LAYOUT_HEADER_SIZE + std::min(size, LAYOUT_CHUNK_SIZE);
      if (OB_FAIL(serialization::encode_i64(first, first_size, pos, size))) {
      } else if (OB_FAIL(serialization::encode_i64(first, first_size, pos, version))) {
      } else {
        const ObString key = layout_key(layout_id, -1, 0, key_buf);
        const ObString head(LAYOUT_HEADER_SIZE, first);
        ret = create ? store_.insert(tx_, MetaCollection::STORAGE_LAYOUTS, key, head)
                     : store_.put(tx_, MetaCollection::STORAGE_LAYOUTS, key, head);
        MEMCPY(first + pos, bytes, first_size - pos);
        if (OB_SUCC(ret)) {
          ret = store_.insert(tx_, MetaCollection::STORAGE_LAYOUTS,
              layout_key(layout_id, version, 0, key_buf), ObString(first_size, first));
        }
      }
    }
    const int64_t chunks = (size + LAYOUT_CHUNK_SIZE - 1) / LAYOUT_CHUNK_SIZE;
    for (int64_t i = 1; OB_SUCC(ret) && i < chunks; ++i) {
      const int64_t offset = i * LAYOUT_CHUNK_SIZE;
      ret = store_.insert(tx_, MetaCollection::STORAGE_LAYOUTS,
          layout_key(layout_id, version, i, key_buf),
          ObString(std::min(LAYOUT_CHUNK_SIZE, size - offset), bytes + offset));
    }
    // Old bodies belong to physical references, not to the current head. Their
    // retirement is separate from publication and obeys the same MVCC readers.
  }
  return ret;
}

int StorageSchemaHistory::read(uint64_t layout_id, ObIAllocator &allocator, ObStorageSchema &schema)
{
  int64_t version = 0;
  int ret = read_version(layout_id, version);
  if (ret == OB_SUCCESS) { ret = read_published(layout_id, version, allocator, schema); }
  return ret;
}

int StorageSchemaHistory::read_published(InstanceMetaStore &store, uint64_t layout_id,
    int64_t schema_version, int64_t deadline, ObIAllocator &allocator, ObStorageSchema &schema)
{
  if (layout_id == 0 || layout_id == OB_INVALID_ID || schema_version < 0 || schema.is_inited()) {
    return OB_INVALID_ARGUMENT;
  }
  InstanceMetaStore::Transaction tx;
  int ret = store.begin_weak_read(tx, deadline);
  StorageSchemaHistory history(store, tx);
  // The referenced immutable body is itself committed in the publication
  // transaction. Its visibility is sufficient; the current head may have a
  // different lifetime from a retired object's still-referenced files.
  if (ret == OB_SUCCESS) { ret = history.read_published(layout_id, schema_version, allocator, schema); }
  if (tx.is_active()) {
    const int end = store.commit(tx);
    if (ret == OB_SUCCESS) { ret = end; }
  }
  return ret;
}

int StorageSchemaHistory::read_published(uint64_t layout_id, int64_t expected_version,
    ObIAllocator &allocator, ObStorageSchema &schema)
{
  int ret = OB_SUCCESS;
  ObArenaAllocator scratch(ObMemAttr("SchemaHistory"));
  char key_buf[24];
  ObString row;
  int64_t size = 0;
  int64_t version = 0;
  char *bytes = nullptr;
  if (layout_id == 0 || layout_id == OB_INVALID_ID || expected_version < 0 || schema.is_inited()) {
    ret = OB_INVALID_ARGUMENT;
  } else if (OB_FAIL(store_.get(tx_, MetaCollection::STORAGE_LAYOUTS,
      layout_key(layout_id, expected_version, 0, key_buf), scratch, row))) {
  } else if (OB_FAIL(decode_layout_header(row, false, size, version))) {
  } else if (version != expected_version) {
    ret = OB_CHECKSUM_ERROR;
  } else if (nullptr == (bytes = static_cast<char *>(scratch.alloc(size)))) {
    ret = OB_ALLOCATE_MEMORY_FAILED;
  } else {
    MEMCPY(bytes, row.ptr() + LAYOUT_HEADER_SIZE, row.length() - LAYOUT_HEADER_SIZE);
    for (int64_t offset = LAYOUT_CHUNK_SIZE; OB_SUCC(ret) && offset < size; offset += LAYOUT_CHUNK_SIZE) {
      if (OB_FAIL(store_.get(tx_, MetaCollection::STORAGE_LAYOUTS,
          layout_key(layout_id, expected_version, offset / LAYOUT_CHUNK_SIZE, key_buf), scratch, row))) {
      } else if (row.length() != std::min(LAYOUT_CHUNK_SIZE, size - offset)) {
        ret = OB_CHECKSUM_ERROR;
      } else {
        MEMCPY(bytes + offset, row.ptr(), row.length());
      }
    }
    int64_t pos = 0;
    if (OB_FAIL(ret)) {
    } else if (OB_FAIL(schema.deserialize(allocator, bytes, size, pos))) {
    } else if (pos != size || !schema.is_valid() || schema.is_column_info_simplified()
        || schema.get_schema_version() != version) {
      ret = OB_CHECKSUM_ERROR;
    }
    if (OB_FAIL(ret)) { schema.reset(); }
  }
  return ret;
}

} // namespace storage
} // namespace oceanbase
