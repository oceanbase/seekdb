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

#ifndef OCEANBASE_ROOTSERVER_INSTANCE_NAMESPACE_METADATA_H_
#define OCEANBASE_ROOTSERVER_INSTANCE_NAMESPACE_METADATA_H_

#include <functional>
#include <string>
#include "namespace/catalog.h"
#include "storage/instance_meta/instance_meta_store.h"

namespace oceanbase
{
namespace rootserver
{

struct InstanceNamespaceRecord
{
  uint64_t id = 0;
  std::string name;
  ns::CatalogRoots roots;
  uint64_t parent_namespace = 0;
  int64_t fork_cap = 0;
};

struct InstanceExceptionRecord
{
  uint64_t namespace_id = 0;
  uint64_t tablet_id = 0; // Namespace-local physical tablet identity.
  uint64_t table_id = 0;  // Raw schema table identity.
  int64_t kind = 0;       // 0 owned, 1 tombstone.
  int64_t drop_scn = 0;
};

struct InstanceNamespacePin
{
  uint64_t snapshot_id = 0;
  int64_t schema_version = 0;
};

// Typed access to instance metadata in one store-owned transaction. The
// caller keeps its Transaction alive through every operation and decides when
// to commit. Scan callbacks must not reenter that transaction.
class InstanceNamespaceMetadata final
{
public:
  using Transaction = storage::InstanceMetaStore::Transaction;
  using NamespaceVisitor = std::function<int(const InstanceNamespaceRecord &)>;
  using SnapshotVisitor = std::function<int(uint64_t, const ns::CatalogRoots &)>;
  using ExceptionVisitor = std::function<int(const InstanceExceptionRecord &)>;
  using PageVisitor = std::function<int(uint64_t)>;
  using PinVisitor = std::function<int(const InstanceNamespacePin &)>;
  using SnapshotAcquirer = std::function<int(int64_t &)>;
  using PhysicalTabletProbe = std::function<int(uint64_t, bool &)>;
  using NamespacePhysicalProbe = std::function<int(uint64_t, bool &)>;

  InstanceNamespaceMetadata(storage::InstanceMetaStore &store, Transaction &transaction)
      : store_(store), transaction_(transaction) {}

  int get_namespace(uint64_t id, InstanceNamespaceRecord &record, bool lock = false);
  int find_namespace(const std::string &name, uint64_t &id);
  int insert_namespace(const InstanceNamespaceRecord &record);
  int update_namespace(const InstanceNamespaceRecord &record);
  int erase_namespace(uint64_t id);
  int scan_namespaces(const NamespaceVisitor &visitor);

  // Bootstrap an empty instance directory. The caller owns the KV
  // transaction and must initialize the snapshot GC watermark in it as well.
  int insert_root_namespace(const std::string &name, int64_t schema_version);

  // Locks the source through snapshot acquisition, then stages the child,
  // name, pin, and lineage in this transaction. Caller commits or rolls back.
  int fork_namespace(const std::string &source_name, const std::string &target_name,
                     const SnapshotAcquirer &acquire_snapshot, uint64_t &child_id);

  // The caller closes new access before marking DELETING, then drains and
  // removes physical tablets before finishing the drop in a later KV tx.
  int mark_namespace_deleting(uint64_t id, bool &done);
  int finish_namespace_drop(uint64_t id);
  // Stage final tombstone removal only after all descendants, owned records,
  // and physical tablets have gone. Caller commits the KV transaction.
  int prune_deleted_namespace(uint64_t id,
      const NamespacePhysicalProbe &has_physical, bool &pruned);

  // The native schema transaction is separate from this KV transaction. Mark
  // it before native DDL, then publish its version after reconciling the
  // directory. Recovery treats an interrupted change as requiring a rescan.
  int begin_schema_change(uint64_t id);
  int finish_schema_change(uint64_t id, int64_t schema_version);
  int begin_schema_recovery(uint64_t id, bool &needed);
  int finish_schema_recovery(uint64_t id, int64_t schema_version);

  // Stage a complete native-schema delta and its directory version together.
  // A local tablet in removed_owned must be dropped only after this KV commit.
  // Roll back the caller's transaction on error. The physical probe must not
  // reenter this transaction.
  int stage_schema_delta(uint64_t id, int64_t schema_version,
      const std::map<uint64_t, uint64_t> &previous_tablets,
      const std::map<uint64_t, uint64_t> &current_tablets,
      const PhysicalTabletProbe &probe,
      std::vector<uint64_t> &removed_owned);

  // Repair a physical creation committed before its owned exception. The map
  // comes from this namespace's current schema; the probe checks that same
  // namespace's physical tablet. This does not publish a schema version or
  // clear a pending DDL marker. Caller rolls back its KV transaction on error.
  int reconcile_owned_tablets(uint64_t id,
      const std::map<uint64_t, uint64_t> &current_tablets,
      const PhysicalTabletProbe &probe);

  int initialize_namespace_counter(uint64_t high_watermark);
  int allocate_namespace_id(uint64_t &id);

  int get_snapshot(uint64_t id, ns::CatalogRoots &roots, bool lock = false);
  int insert_snapshot(uint64_t id, const ns::CatalogRoots &roots);
  int update_snapshot(uint64_t id, const ns::CatalogRoots &roots);
  int erase_snapshot(uint64_t id);
  int scan_snapshots(const SnapshotVisitor &visitor);

  int initialize_snapshot_gc_watermark(int64_t watermark);
  int get_snapshot_gc_watermark(int64_t &watermark, bool lock = false);
  // Advances to at least watermark; stale concurrent requests leave it unchanged.
  int advance_snapshot_gc_watermark(int64_t watermark);

  int get_pin(uint64_t snapshot_id, InstanceNamespacePin &pin, bool lock = false);
  // Serializes registration with watermark advancement in this KV store.
  int insert_pin(const InstanceNamespacePin &pin);
  int erase_pin(uint64_t snapshot_id);
  int scan_pins(const PinVisitor &visitor);

  int get_exception(uint64_t ns_id, uint64_t local_tablet,
                    InstanceExceptionRecord &record, bool lock = false);
  int put_exception(const InstanceExceptionRecord &record);
  int erase_exception(uint64_t ns_id, uint64_t local_tablet);
  int scan_exceptions(uint64_t ns_id, const ExceptionVisitor &visitor);

  int read_page(uint64_t page_id, std::string &data);
  int save_page(const std::string &data, uint64_t &page_id);
  int erase_page(uint64_t page_id);
  int scan_pages(const PageVisitor &visitor);

private:
  storage::InstanceMetaStore &store_;
  Transaction &transaction_;
};

class InstanceCatalogPageStore final : public ns::ICatalogPageStore
{
public:
  explicit InstanceCatalogPageStore(InstanceNamespaceMetadata &metadata)
      : metadata_(metadata) {}
  int read(uint64_t page, std::string &data) override
  {
    return metadata_.read_page(page, data);
  }
  int write(const std::string &data, uint64_t &page) override
  {
    return metadata_.save_page(data, page);
  }
private:
  InstanceNamespaceMetadata &metadata_;
};

// The lineage algorithm owns ordering and reference decisions; this adapter
// keeps its snapshot rows, child attachment, and pin removal in one KV transaction.
class InstanceSnapshotLineageStore final : public ns::ISnapshotLineageStore
{
public:
  explicit InstanceSnapshotLineageStore(InstanceNamespaceMetadata &metadata)
      : metadata_(metadata) {}
  int load_for_update(uint64_t snapshot_id, ns::CatalogRoots &roots) override;
  int increment_ref(uint64_t snapshot_id) override;
  int decrement_ref(uint64_t snapshot_id) override;
  int insert_snapshot(const ns::CatalogRoots &roots) override;
  int attach_child(uint64_t child_id, uint64_t parent_namespace_id,
                   const ns::CatalogRoots &roots) override;
  int remove_snapshot(uint64_t snapshot_id, const ns::CatalogRoots &roots) override;
private:
  InstanceNamespaceMetadata &metadata_;
};

} // namespace rootserver
} // namespace oceanbase
#endif
