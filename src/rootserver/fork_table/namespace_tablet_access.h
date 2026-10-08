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

#include "rootserver/fork_table/namespace_fork_kernel_prototype.h"
#include "data_plane/access/ob_namespace_access_mode.h"
#include <functional>
#include <memory>
#include <unordered_map>
#include <vector>

namespace oceanbase {
namespace share { namespace schema { class ObSchemaGetterGuard; } }
namespace storage { class ObTableScanParam; }
namespace ns {

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
  int prepare_lob_read(uint64_t namespace_id, const common::ObLobLocatorV2 &locator,
                       data_plane::ObNamespaceAccessMode mode);
  int prepare_read(uint64_t namespace_id, uint64_t table_id,
                   const common::ObTabletID &logical_tablet,
                   data_plane::ObNamespaceAccessMode mode);
  int prepare_scan(uint64_t namespace_id, data_plane::ObNamespaceAccessMode mode,
                   storage::ObTableScanParam &param);
  int prepare_write(uint64_t namespace_id, uint64_t table_id,
                    const common::ObTabletID &logical_tablet,
                    data_plane::ObNamespaceAccessMode mode);
  const common::ObTabletID &tablet() const { return tablet_; }
  const common::ObTabletID &schema_tablet() const { return schema_tablet_; }
  int64_t cap_scn() const { return cap_scn_; }
private:
  int route(uint64_t namespace_id, const common::ObTabletID &logical_tablet);
  storage::TabletAccessProtection protection_;
  common::ObTabletID tablet_;
  common::ObTabletID schema_tablet_;
  int64_t cap_scn_ = 0;
  DISALLOW_COPY_AND_ASSIGN(TabletAccess);
};
} // namespace ns
} // namespace oceanbase
#endif
