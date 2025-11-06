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

#ifndef OCEANBASE_SHARE_OB_LOCALITY_PRIORITY_H_
#define OCEANBASE_SHARE_OB_LOCALITY_PRIORITY_H_

#include "share/ob_locality_info.h"
namespace oceanbase
{
namespace share
{
class ObLocalityPriority
{
public:
  static int get_primary_region_prioriry(const char *primary_zone,
      const common::ObIArray<ObLocalityRegion> &locality_region_array,
      common::ObIArray<ObLocalityRegion> &tenant_region_array);
  static int get_region_priority(const ObLocalityInfo &locality_info,
    const common::ObIArray<ObLocalityRegion> &tenant_locality_region,
    uint64_t &region_priority);
};

} // end namespace share
} // end namespace oceanbase
#endif
