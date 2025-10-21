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

#define USING_LOG_PREFIX SHARE

#include "share/tablet/ob_tablet_to_ls_iterator.h" // ObTenantTabletToLSIterator

#include "share/tablet/ob_tenant_tablet_to_ls_map.h"

namespace oceanbase
{
namespace share
{

int ObTenantTabletToLSMap::build(const uint64_t tenant_id,
    common::ObMySQLProxy &sql_proxy)
{
  int ret = OB_SUCCESS;

  ObTenantTabletToLSIterator iter;
  if (OB_FAIL(iter.init(sql_proxy, tenant_id))) {
    LOG_WARN("init iter fail", KR(ret), K(tenant_id));
  } else {
    ObTabletLSPair tablet_ls_pair;
    while (OB_SUCC(ret)) {
      if (OB_FAIL(iter.next(tablet_ls_pair))) {
        if (OB_ITER_END != ret) {
          LOG_WARN("iter next fail", KR(ret), K(tenant_id));
        } else {
          ret = OB_SUCCESS;
          break;
        }
      } else if (OB_FAIL(map_.set_refactored(tablet_ls_pair.get_tablet_id(), tablet_ls_pair.get_ls_id()))) {
        LOG_WARN("tablet_to_ls map set fail", KR(ret), K(tenant_id), K(tablet_ls_pair));
      }
    }
  }

  return ret;
}


} // end namespace share
} // end namespace oceanbase
