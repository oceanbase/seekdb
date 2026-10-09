/*
 * Copyright (c) 2025 OceanBase.
 * Licensed under the Apache License, Version 2.0.
 */

#define USING_LOG_PREFIX STORAGE
#include "storage/instance_meta/storage_schema_history.h"
#include "storage/ob_storage_schema.h"
#include "lib/allocator/ob_allocator.h"

namespace oceanbase
{
using namespace common;
namespace storage
{
namespace
{
// Chunk zero starts with the total serialized length and the source definition
// version. Small layouts need just this row. Subsequent chunks have fixed
// positions; overwrite and shrink are atomic with the new chunk zero.
constexpr int64_t LAYOUT_HEADER_SIZE = 2 * sizeof(int64_t);
constexpr int64_t LAYOUT_CHUNK_SIZE = InstanceMetaStore::MAX_VALUE_LENGTH - LAYOUT_HEADER_SIZE;

ObString layout_key(uint64_t id, int64_t chunk, char (&bytes)[16])
{
  int64_t pos = 0;
  (void)serialization::encode_i64(bytes, sizeof(bytes), pos, id);
  (void)serialization::encode_i64(bytes, sizeof(bytes), pos, chunk);
  return ObString(sizeof(bytes), bytes);
}

int decode_layout_header(const ObString &row, int64_t &size, int64_t &version)
{
  int ret = OB_SUCCESS;
  int64_t pos = 0;
  if (row.length() <= LAYOUT_HEADER_SIZE) {
    ret = OB_CHECKSUM_ERROR;
  } else if (OB_FAIL(serialization::decode_i64(row.ptr(), row.length(), pos, &size))) {
  } else if (OB_FAIL(serialization::decode_i64(row.ptr(), row.length(), pos, &version))) {
  } else if (size <= 0 || size > INT32_MAX || version < 0
      || row.length() != LAYOUT_HEADER_SIZE + std::min(size, LAYOUT_CHUNK_SIZE)) {
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
  char key_buf[16];
  ObString row;
  int64_t size = 0;
  schema_version = OB_INVALID_VERSION;
  int ret = layout_id == 0 || layout_id == OB_INVALID_ID ? OB_INVALID_ARGUMENT
      : store_.get(tx_, MetaCollection::STORAGE_LAYOUTS,
          layout_key(layout_id, 0, key_buf), allocator, row);
  if (ret == OB_SUCCESS) { ret = decode_layout_header(row, size, schema_version); }
  return ret;
}

int StorageSchemaHistory::write(uint64_t layout_id, const ObStorageSchema &schema, bool create)
{
  int ret = OB_SUCCESS;
  ObArenaAllocator allocator(ObMemAttr("SchemaHistory"));
  const int64_t size = schema.get_serialize_size();
  int64_t old_size = 0;
  int64_t old_version = 0;
  char key_buf[16];
  ObString old_row;
  char *bytes = nullptr;
  char *first = nullptr;
  if (layout_id == 0 || layout_id == OB_INVALID_ID || !schema.is_valid()
      || schema.is_column_info_simplified()) {
    ret = OB_INVALID_ARGUMENT;
  } else if (size <= 0 || size > INT32_MAX) {
    ret = OB_SIZE_OVERFLOW;
  } else if (!create && OB_FAIL(store_.get_for_update(tx_, MetaCollection::STORAGE_LAYOUTS,
      layout_key(layout_id, 0, key_buf), allocator, old_row))) {
  } else if (!create && OB_FAIL(decode_layout_header(old_row, old_size, old_version))) {
  } else if (!create && schema.get_schema_version() <= old_version) {
    // Versions are comparable only within this stable layout identity. Initial
    // child materialization uses create(), never a publish of an older seed.
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
      } else if (OB_FAIL(serialization::encode_i64(first, first_size, pos, schema.get_schema_version()))) {
      } else {
        MEMCPY(first + pos, bytes, first_size - pos);
        const ObString key = layout_key(layout_id, 0, key_buf);
        const ObString value(first_size, first);
        ret = create ? store_.insert(tx_, MetaCollection::STORAGE_LAYOUTS, key, value)
                     : store_.put(tx_, MetaCollection::STORAGE_LAYOUTS, key, value);
      }
    }
    const int64_t chunks = (size + LAYOUT_CHUNK_SIZE - 1) / LAYOUT_CHUNK_SIZE;
    const int64_t old_chunks = (old_size + LAYOUT_CHUNK_SIZE - 1) / LAYOUT_CHUNK_SIZE;
    for (int64_t i = 1; OB_SUCC(ret) && i < chunks; ++i) {
      const int64_t offset = i * LAYOUT_CHUNK_SIZE;
      ret = store_.put(tx_, MetaCollection::STORAGE_LAYOUTS, layout_key(layout_id, i, key_buf),
          ObString(std::min(LAYOUT_CHUNK_SIZE, size - offset), bytes + offset));
    }
    for (int64_t i = chunks; OB_SUCC(ret) && i < old_chunks; ++i) {
      bool existed = false;
      ret = store_.erase(tx_, MetaCollection::STORAGE_LAYOUTS, layout_key(layout_id, i, key_buf), existed);
      if (OB_SUCC(ret) && !existed) { ret = OB_CHECKSUM_ERROR; }
    }
  }
  return ret;
}

int StorageSchemaHistory::read(uint64_t layout_id, ObIAllocator &allocator, ObStorageSchema &schema)
{
  int ret = OB_SUCCESS;
  ObArenaAllocator scratch(ObMemAttr("SchemaHistory"));
  char key_buf[16];
  ObString row;
  int64_t size = 0;
  int64_t version = 0;
  char *bytes = nullptr;
  if (layout_id == 0 || layout_id == OB_INVALID_ID || schema.is_inited()) {
    ret = OB_INVALID_ARGUMENT;
  } else if (OB_FAIL(store_.get(tx_, MetaCollection::STORAGE_LAYOUTS,
      layout_key(layout_id, 0, key_buf), scratch, row))) {
  } else if (OB_FAIL(decode_layout_header(row, size, version))) {
  } else if (nullptr == (bytes = static_cast<char *>(scratch.alloc(size)))) {
    ret = OB_ALLOCATE_MEMORY_FAILED;
  } else {
    MEMCPY(bytes, row.ptr() + LAYOUT_HEADER_SIZE, row.length() - LAYOUT_HEADER_SIZE);
    for (int64_t offset = LAYOUT_CHUNK_SIZE; OB_SUCC(ret) && offset < size; offset += LAYOUT_CHUNK_SIZE) {
      if (OB_FAIL(store_.get(tx_, MetaCollection::STORAGE_LAYOUTS,
          layout_key(layout_id, offset / LAYOUT_CHUNK_SIZE, key_buf), scratch, row))) {
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
