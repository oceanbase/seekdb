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

  InstanceNamespaceMetadata(storage::InstanceMetaStore &store, Transaction &transaction)
      : store_(store), transaction_(transaction) {}

  int get_namespace(uint64_t id, InstanceNamespaceRecord &record, bool lock = false);
  int find_namespace(const std::string &name, uint64_t &id);
  int insert_namespace(const InstanceNamespaceRecord &record);
  int update_namespace(const InstanceNamespaceRecord &record);
  int erase_namespace(uint64_t id);
  int scan_namespaces(const NamespaceVisitor &visitor);

  int initialize_namespace_counter(uint64_t high_watermark);
  int allocate_namespace_id(uint64_t &id);

  int get_snapshot(uint64_t id, ns::CatalogRoots &roots, bool lock = false);
  int insert_snapshot(uint64_t id, const ns::CatalogRoots &roots);
  int update_snapshot(uint64_t id, const ns::CatalogRoots &roots);
  int erase_snapshot(uint64_t id);
  int scan_snapshots(const SnapshotVisitor &visitor);

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

} // namespace rootserver
} // namespace oceanbase
#endif
