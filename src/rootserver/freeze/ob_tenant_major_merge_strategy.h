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

#ifndef OCEANBASE_ROOTSERVER_FREEZE_OB_TENANT_MAJOR_MERGE_STRATEGY_H_
#define OCEANBASE_ROOTSERVER_FREEZE_OB_TENANT_MAJOR_MERGE_STRATEGY_H_

#include "lib/container/ob_iarray.h"
#include "common/ob_zone.h"
#include "lib/ob_define.h"

namespace oceanbase
{
namespace rootserver
{
class ObZoneMergeManager;

class ObTenantMajorMergeStrategy
{
public:
  ObTenantMajorMergeStrategy()
    : is_inited_(false),
      tenant_id_(OB_INVALID_TENANT_ID),
      zone_merge_mgr_(NULL)
  {}
  virtual ~ObTenantMajorMergeStrategy() {}

  int init(const uint64_t tenant_id,
           ObZoneMergeManager *zone_merge_mgr);
  virtual int get_next_zone(common::ObIArray<common::ObZone> &to_merge_zones) = 0;

protected:
  int filter_merging_zones(common::ObIArray<common::ObZone> &to_merge_zones);

protected:
  bool is_inited_;
  uint64_t tenant_id_;
  ObZoneMergeManager *zone_merge_mgr_;

private:
  DISALLOW_COPY_AND_ASSIGN(ObTenantMajorMergeStrategy);
};

} // namespace rootserver
} // namespace oceanbase

#endif // OCEANBASE_ROOTSERVER_FREEZE_OB_TENANT_MAJOR_MERGE_STRATEGY_H_
