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
#include <vector>
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

enum class TabletVisibility
{
  ABSENT,
  READABLE,
  OUTSIDE_SNAPSHOT
};

// Typed access to instance metadata in one store-owned transaction. The
// caller keeps its Transaction alive through every operation and decides when
// to commit. Scan callbacks must not reenter that transaction.
class InstanceNamespaceMetadata final
{
public:
  using Transaction = storage::InstanceMetaStore::Transaction;
  using NamespaceVisitor = std::function<int(const InstanceNamespaceRecord &)>;
  using PageVisitor = std::function<int(uint64_t)>;
  using SnapshotAcquirer = std::function<int(int64_t &)>;
  using NamespacePhysicalProbe = std::function<int(uint64_t, bool &)>;

  InstanceNamespaceMetadata(storage::InstanceMetaStore &store, Transaction &transaction)
      : store_(store), transaction_(transaction) {}

  int get_namespace(uint64_t id, InstanceNamespaceRecord &record, bool lock = false);
  int find_namespace(const std::string &name, uint64_t &id);
  int insert_namespace(const InstanceNamespaceRecord &record);
  int update_namespace(const InstanceNamespaceRecord &record);
  // Atomically changes a live member's name and its unique name index.
  // A mismatched old name or duplicate new name aborts the caller's tx.
  int rename_namespace(uint64_t id, const std::string &expected_name,
                       const std::string &new_name);
  int erase_namespace(uint64_t id);
  int scan_namespaces(const NamespaceVisitor &visitor);

  // Bootstrap an empty instance directory. The caller owns the KV
  // transaction and must initialize the snapshot GC watermark in it as well.
  int insert_root_namespace(const std::string &name, int64_t schema_version);

  // Locks the source through snapshot acquisition, then stages the child,
  // name and capped roots in this transaction. The coordination row lock
  // fences this publication against watermark advancement until commit.
  int fork_namespace(const std::string &source_name, const std::string &target_name,
                     const SnapshotAcquirer &acquire_snapshot, uint64_t &child_id);

  // The caller closes new access before marking DELETING, then drains admitted
  // access before finishing the logical drop in a later KV transaction.
  int mark_namespace_deleting(uint64_t id, bool &done);
  int finish_namespace_drop(uint64_t id);
  // Stage final tombstone removal only after all descendants
  // and physical tablets have gone. Caller commits the KV transaction.
  int prune_deleted_namespace(uint64_t id,
      const NamespacePhysicalProbe &has_physical, bool &pruned);

  int initialize_namespace_counter(uint64_t high_watermark);
  int allocate_namespace_id(uint64_t &id);

  int initialize_snapshot_gc_watermark(int64_t watermark);
  int get_snapshot_gc_watermark(int64_t &watermark, bool lock = false);
  // Advances to at least watermark; stale concurrent requests leave it unchanged.
  int advance_snapshot_gc_watermark(int64_t watermark);

  int read_page(uint64_t page_id, std::string &data);
  int save_page(const std::string &data, uint64_t &page_id);
  int erase_page(uint64_t page_id);
  int scan_pages(const PageVisitor &visitor);
  // Immutable creation descriptions may exceed one KV value. A manifest owns
  // fixed-size chunks in PAGES; catalog values reference the manifest. All rows
  // are written in this transaction and reclaimed with their catalog roots.
  int save_object(const std::string &data, uint64_t &object);
  int read_object(uint64_t object, std::string &data);
  int find_tablet_source(ns::CatalogPageRef root, uint64_t logical_tablet,
                        ns::CatalogTabletSource &source, int64_t &cap);
  int read_table_definition(ns::CatalogPageRef root, uint64_t table_id,
                            std::string &definition);
  // Locks the Namespace root, checks the caller's schema base, and stages both
  // COW trees and their schema version in this transaction. Equal versions are
  // permitted for physical materialization. The caller must roll back on any
  // error and commit together with its native schema/physical-object changes.
  int stage_catalog_delta(uint64_t namespace_id, int64_t base_schema_version,
      int64_t schema_version, const ns::CatalogChanges &definitions,
      const ns::CatalogChanges &sources);
  // Requires a store.begin_directory_gc transaction. Marks current namespace
  // roots, then stages at most max_deletes page erases.
  // The caller commits the transaction or rolls it back on any error.
  int collect_unreachable_pages(int64_t max_deletes, int64_t &deleted);

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

// One call owns one native KV transaction. Construct on the management path;
// this object has no Namespace runtime, SQL proxy, cache, or background thread.
class InstanceNamespaceDirectory final
{
public:
  explicit InstanceNamespaceDirectory(storage::InstanceMetaStore &store)
      : store_(store) {}

  // Root, namespace ID counter, and GC watermark first become visible together.
  // Repeated startup validates the root and advances a stale watermark.
  int ensure_root(const std::string &name, int64_t schema_version,
                  int64_t gc_watermark, int64_t deadline, bool &created);
  // Commits the child, name and shared capped roots before returning child.
  int fork_namespace(const std::string &source_name, const std::string &target_name,
                     const InstanceNamespaceMetadata::SnapshotAcquirer &acquire_snapshot,
                     int64_t deadline, InstanceNamespaceRecord &child);
  int find_live(const std::string &name, int64_t deadline,
                InstanceNamespaceRecord &record);
  int find_named(const std::string &name, int64_t deadline,
                 InstanceNamespaceRecord &record);
  int get(uint64_t id, int64_t deadline, InstanceNamespaceRecord &record);
  // Select the root at the acquired SQL read snapshot and register it before
  // releasing the short KV transaction. The caller also holds the physical
  // publication fence until this method returns. No KV transaction stays open
  // for the returned view's lifetime.
  int acquire_read_view(uint64_t namespace_id, int64_t deadline,
      const storage::InstanceMetaStore::SnapshotAcquirer &acquire,
      ns::NamespaceCatalogViews::Handle &view,
      const ns::NamespaceCatalogViews::Handle &previous = {});
  // Mark source trees of live/closing Namespaces and protected reader roots.
  // sources maps incomplete physical copies to their baseline sources. The
  // caller fences new dependencies through the actual physical reclamation.
  int filter_unreferenced_tablets(const std::vector<uint64_t> &candidates,
      const std::map<uint64_t, uint64_t> &sources,
      int64_t deadline, std::vector<uint64_t> &unreferenced, bool &need_retry);
  int list_live(int64_t deadline, std::vector<InstanceNamespaceRecord> &records);
  int list_deleted(int64_t deadline, std::vector<InstanceNamespaceRecord> &records);
  int rename_live(uint64_t id, const std::string &expected_name,
                  const std::string &new_name, int64_t deadline);
  // Caller closes new access first. Commit DELETING before draining admitted
  // access; if commit returns an uncertain result, leave access closed.
  int mark_deleting(uint64_t id, const std::string &expected_name,
                    int64_t deadline, bool &done);
  int prune_deleted(uint64_t id,
      const InstanceNamespaceMetadata::NamespacePhysicalProbe &has_physical,
      int64_t deadline, bool &pruned);
  // Clears the owned roots and marks DELETED. Shared physical GC subsequently
  // reclaims unreferenced owned and orphan tablets.
  int finish_drop(uint64_t id, int64_t deadline);
  int schema_version(uint64_t id, int64_t deadline, int64_t &version);
private:
  storage::InstanceMetaStore &store_;
};

} // namespace rootserver
} // namespace oceanbase
#endif
