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

#ifndef OCEANBASE_ROOTSERVER_FREEZE_OB_TENANT_ALL_ZONE_MERGE_STRATEGY_H_
#define OCEANBASE_ROOTSERVER_FREEZE_OB_TENANT_ALL_ZONE_MERGE_STRATEGY_H_

#include "rootserver/freeze/ob_tenant_major_merge_strategy.h"

namespace oceanbase
{
namespace rootserver
{
class ObZoneManager;

class ObTenantAllZoneMergeStrategy : public ObTenantMajorMergeStrategy
{
public:
  friend class TestTenantAllZoneMergeStrategy_get_next_zone_Test;

  ObTenantAllZoneMergeStrategy() {}
  virtual ~ObTenantAllZoneMergeStrategy() {}
  virtual int get_next_zone(common::ObIArray<common::ObZone> &to_merge_zones) override;

private:
  DISALLOW_COPY_AND_ASSIGN(ObTenantAllZoneMergeStrategy);
};

} // namespace rootserver
} // namespace oceanbase

#endif //OCEANBASE_ROOTSERVER_FREEZE_OB_TENANT_ALL_ZONE_MERGE_STRATEGY_H_
