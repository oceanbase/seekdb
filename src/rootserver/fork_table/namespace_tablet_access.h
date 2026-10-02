/*
 * Copyright (c) 2025 OceanBase.
 * Licensed under the Apache License, Version 2.0 (the "License");
 * you may not use this file except in compliance with the License.
 * You may obtain a copy of the License at
 *     http://www.apache.org/licenses/LICENSE-2.0
 * Unless required by applicable law or agreed to in writing, software
 * distributed under the License is distributed on an "AS IS" BASIS,
 * WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
 * See the License for the specific language governing permissions and
 * limitations under the License.
 */
#ifndef OCEANBASE_NAMESPACE_TABLET_ACCESS_H_
#define OCEANBASE_NAMESPACE_TABLET_ACCESS_H_

#include "share/schema/ob_table_schema.h"
#include "data_plane/access/ob_namespace_access_mode.h"
#include <memory>
#include <vector>

namespace oceanbase {
namespace share { namespace schema { class ObSchemaGetterGuard; } }
namespace storage { class ObTableScanParam; }
namespace ns {

// Owns one physical schema per member of the main/LOB binding. All definitions
// come from the caller's pinned guard, including when the requested object is
// an auxiliary. Physical storage never looks up a Namespace SchemaService.
class TabletBinding final
{
public:
  explicit TabletBinding(common::ObIAllocator &allocator) : allocator_(allocator) {}
  int init(uint64_t namespace_id, const share::schema::ObTableSchema &requested,
           share::schema::ObSchemaGetterGuard &guard);
  const share::schema::ObTableSchema &schema() const { return *requested_; }
private:
  friend class TabletAccess;
  int ensure(const common::ObTabletID &storage_tablet) const;
  common::ObIAllocator &allocator_;
  std::vector<std::unique_ptr<share::schema::ObTableSchema>> definitions_;
  common::ObSEArray<const share::schema::ObTableSchema *, 3> schemas_;
  const share::schema::ObTableSchema *requested_ = nullptr;
  DISALLOW_COPY_AND_ASSIGN(TabletBinding);
};

// Keeps logical access admitted until the caller releases its physical
// iterators/store contexts. Resolution is read-only; only prepare_write may
// create a binding. Physical replay, compaction and GC do not enter here.
class TabletAccess final
{
public:
  TabletAccess() = default;
  ~TabletAccess() { reset(); }
  void reset();
  // Persistent locators already carry a resolved physical source and snapshot.
  int prepare_lob_read(const common::ObLobLocatorV2 &locator,
                       data_plane::ObNamespaceAccessMode mode);
  int prepare_read(uint64_t namespace_id, uint64_t table_id,
                   const common::ObTabletID &logical_tablet,
                   data_plane::ObNamespaceAccessMode mode);
  int prepare_scan(uint64_t namespace_id, data_plane::ObNamespaceAccessMode mode,
                   storage::ObTableScanParam &param);
  int prepare_write(uint64_t namespace_id, uint64_t table_id,
                    const common::ObTabletID &logical_tablet,
                    data_plane::ObNamespaceAccessMode mode,
                    const TabletBinding &binding);
  const common::ObTabletID &tablet() const { return tablet_; }
  const common::ObTabletID &schema_tablet() const { return schema_tablet_; }
  int64_t cap_scn() const { return cap_scn_; }
private:
  int admit(uint64_t namespace_id, uint64_t table_id,
            const common::ObTabletID &logical_tablet, bool read_only,
            data_plane::ObNamespaceAccessMode mode);
  bool held_ = false;
  common::ObTabletID tablet_;
  common::ObTabletID schema_tablet_;
  int64_t cap_scn_ = 0;
  DISALLOW_COPY_AND_ASSIGN(TabletAccess);
};
} // namespace ns
} // namespace oceanbase
#endif
