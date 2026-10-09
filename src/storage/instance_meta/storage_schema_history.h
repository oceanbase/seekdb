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
// owns the transaction: DDL atomically publishes a MVCC head and immutable
// (identity, definition version) bodies. Snapshot compaction selects the head
// at its protected SCN; physical rewrites can retain an exact body reference
// independently of that head's MVCC lifetime. Bodies remain until no physical
// reference needs them. Neither SQL catalogs nor Namespace services are used.
class StorageSchemaHistory final
{
public:
  StorageSchemaHistory(InstanceMetaStore &store, InstanceMetaStore::Transaction &tx)
    : store_(store), tx_(tx) {}
  int create(uint64_t layout_id, const ObStorageSchema &schema);
  int publish(uint64_t layout_id, const ObStorageSchema &schema);
  int read(uint64_t layout_id, common::ObIAllocator &allocator, ObStorageSchema &schema);
  int read_version(uint64_t layout_id, int64_t &schema_version);
  int read_published(uint64_t layout_id, int64_t schema_version,
      common::ObIAllocator &allocator, ObStorageSchema &schema);
  // Load an exact, still-referenced physical definition. A newer publication
  // cannot substitute for it. The caller holds the tablet/SSTable reference
  // which prevents retirement of this body while the read is registered.
  static int read_published(InstanceMetaStore &store, uint64_t layout_id,
      int64_t schema_version, int64_t deadline,
      common::ObIAllocator &allocator, ObStorageSchema &schema);
  // Read a historical merge target protected by persisted freeze/GC state.
  // Registers the reader before checking that protection and local replay.
  static int read_at(InstanceMetaStore &store, uint64_t layout_id,
      const share::SCN &target, int64_t deadline,
      common::ObIAllocator &allocator, ObStorageSchema &schema);
  // Select a new locally readable target after registering retention. Used by
  // non-global compactions when their tentative old snapshot is no longer
  // protected. The returned target belongs to this exact layout read.
  static int read_current(InstanceMetaStore &store, uint64_t layout_id,
      const share::SCN &minimum_target, int64_t deadline,
      common::ObIAllocator &allocator, share::SCN &target, ObStorageSchema &schema);

private:
  int write(uint64_t layout_id, const ObStorageSchema &schema, bool create);
  InstanceMetaStore &store_;
  InstanceMetaStore::Transaction &tx_;
  DISALLOW_COPY_AND_ASSIGN(StorageSchemaHistory);
};

} // namespace storage
} // namespace oceanbase
#endif
