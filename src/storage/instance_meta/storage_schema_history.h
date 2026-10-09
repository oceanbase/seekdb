/*
 * Copyright (c) 2025 OceanBase.
 * Licensed under the Apache License, Version 2.0.
 */

#ifndef OCEANBASE_STORAGE_STORAGE_SCHEMA_HISTORY_H_
#define OCEANBASE_STORAGE_STORAGE_SCHEMA_HISTORY_H_

#include "storage/instance_meta/instance_meta_store.h"

namespace oceanbase
{
namespace storage
{
class ObStorageSchema;

// Complete physical layouts, addressed only by an opaque identity. The caller
// owns the transaction: DDL borrows its native transaction, compaction reads at
// an already protected target SCN. Neither SQL catalogs nor Namespace services
// are consulted here. A large layout uses several rows in the SAME MVCC view.
class StorageSchemaHistory final
{
public:
  StorageSchemaHistory(InstanceMetaStore &store, InstanceMetaStore::Transaction &tx)
    : store_(store), tx_(tx) {}
  int create(uint64_t layout_id, const ObStorageSchema &schema);
  int publish(uint64_t layout_id, const ObStorageSchema &schema);
  int read(uint64_t layout_id, common::ObIAllocator &allocator, ObStorageSchema &schema);
  int read_version(uint64_t layout_id, int64_t &schema_version);
  // Read a historical merge target protected by persisted freeze/GC state.
  // Registers the reader before checking that protection and local replay.
  static int read_at(InstanceMetaStore &store, uint64_t layout_id,
      const share::SCN &target, int64_t deadline,
      common::ObIAllocator &allocator, ObStorageSchema &schema);

private:
  int write(uint64_t layout_id, const ObStorageSchema &schema, bool create);
  InstanceMetaStore &store_;
  InstanceMetaStore::Transaction &tx_;
  DISALLOW_COPY_AND_ASSIGN(StorageSchemaHistory);
};

} // namespace storage
} // namespace oceanbase
#endif
