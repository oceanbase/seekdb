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

#ifndef OCEANBASE_SHARE_TABLET_OB_TENANT_TABLET_TO_LS_MAP_H
#define OCEANBASE_SHARE_TABLET_OB_TENANT_TABLET_TO_LS_MAP_H

#include "lib/alloc/alloc_struct.h"   // ObLabel
#include "lib/hash/ob_hashmap.h"      // ObHashMap
#include "share/ob_ls_id.h"           // ObLSID

namespace oceanbase
{
namespace common
{
class ObISQLClient;
class ObTabletID;
class ObMySQLProxy;
}
namespace share
{
class ObTabletToLSTableOperator;

typedef common::hash::ObHashMap<common::ObTabletID, ObLSID> ObTabletToLSMap;

// Tenant all tablet to LS info
// It will build map in build() function.
class ObTenantTabletToLSMap final
{
public:
  ObTenantTabletToLSMap() : map_() {}
  ~ObTenantTabletToLSMap() {}

  int init(const int64_t bucket_num = 4096,
      const lib::ObLabel label = lib::ObLabel("TenantTabletToLSMap"),
      const uint64_t tenant_id = OB_SERVER_TENANT_ID)
  {
    return map_.create(bucket_num, label, ObModIds::OB_HASH_NODE, tenant_id);
  }
  void destroy() { map_.destroy(); }

  int build(const uint64_t tenant_id,
      common::ObMySQLProxy &sql_proxy);

  int clear() { return map_.clear(); }

  int get(const common::ObTabletID &tablet_id, ObLSID &ls_id) const
  {
    return map_.get_refactored(tablet_id, ls_id);
  }
  int64_t size() const { return map_.size(); }

private:
   ObTabletToLSMap map_;
};

} // end namespace share
} // end namespace oceanbase

#endif // OCEANBASE_SHARE_TABLET_OB_TENANT_TABLET_TO_LS_MAP_H
