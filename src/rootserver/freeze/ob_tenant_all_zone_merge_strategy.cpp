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

#define USING_LOG_PREFIX RS_COMPACTION
#include "rootserver/freeze/ob_tenant_all_zone_merge_strategy.h"
#include "rootserver/freeze/ob_zone_merge_manager.h"

namespace oceanbase
{
namespace rootserver
{
using namespace oceanbase::common;

int ObTenantAllZoneMergeStrategy::get_next_zone(ObIArray<ObZone> &to_merge_zones)
{
  int ret = OB_SUCCESS;
  if (IS_NOT_INIT) {
    ret = OB_NOT_INIT;
    LOG_WARN("not init", KR(ret), K_(tenant_id));
  } else if (OB_FAIL(zone_merge_mgr_->get_zone(to_merge_zones))) {
    LOG_WARN("fail to get zone", KR(ret), K(tenant_id_));
  } else if (OB_FAIL(filter_merging_zones(to_merge_zones))) {
    LOG_WARN("fail to filter merging zones", KR(ret), K(tenant_id_));
  } else {
    LOG_INFO("get_next_zone of merge strategy", K(tenant_id_),
             "to_merge_cnt", to_merge_zones.count(), K(to_merge_zones));
  }

  return ret;
}

} // namespace rootserver
} // namespace oceanbase
