/* Copyright (c) 2025 OceanBase. Licensed under the Apache License, Version 2.0. */
#ifndef OCEANBASE_ROOTSERVER_TABLE_STORAGE_LAYOUTS_H_
#define OCEANBASE_ROOTSERVER_TABLE_STORAGE_LAYOUTS_H_

#include "storage/instance_meta/instance_meta_store.h"
#include "lib/container/ob_array.h"

namespace oceanbase {
namespace common { class ObMySQLTransaction; }
namespace storage { class ObCreateTabletSchema; }
namespace rootserver {

// Upper-layer ownership of physical layouts. Bindings are immutable, local to
// an owner/table pair, and are never inherited by fork. The first CREATE or DDL
// seeds the owner's layout from its own complete definition. Storage consumers
// see only the resulting G, never the SQL identity used to select it here.
class TableStorageLayouts final
{
public:
  struct Definition
  {
    uint64_t namespace_id = 0;
    uint64_t table_id = 0;
    uint64_t layout_id = 0;
    int64_t schema_version = -1;
    TO_STRING_KV(K(namespace_id), K(table_id), K(layout_id), K(schema_version));
  };
  // Enumerate complete expected SQL objects independently of arrived reports.
  // Each V is selected from its own G in the same retained snapshot F.
  static int read_at(storage::InstanceMetaStore &store, const share::SCN &target,
      int64_t deadline, common::ObIArray<Definition> &definitions);
  TableStorageLayouts(storage::InstanceMetaStore &store,
      storage::InstanceMetaStore::Transaction &tx, uint64_t namespace_id)
    : store_(store), tx_(tx), namespace_id_(namespace_id) {}
  // The SQL owner retains the borrowed KV participant through commit/rollback.
  static int attach(common::ObMySQLTransaction &owner, transaction::ObTxDesc &native,
      storage::InstanceMetaStore &store, std::shared_ptr<storage::InstanceMetaStore::Transaction> &tx);
  // Reusing a binding does not publish the seed again or lock its layout row.
  int prepare_create(storage::ObCreateTabletSchema &schema);
  // Called under existing same-object DDL publication coordination. Even a
  // definition-only version change publishes the complete layout and its V.
  int publish(storage::ObCreateTabletSchema &schema);
private:
  int bind(storage::ObCreateTabletSchema &schema);
  storage::InstanceMetaStore &store_;
  storage::InstanceMetaStore::Transaction &tx_;
  const uint64_t namespace_id_;
  DISALLOW_COPY_AND_ASSIGN(TableStorageLayouts);
};

} // namespace rootserver
} // namespace oceanbase
#endif
