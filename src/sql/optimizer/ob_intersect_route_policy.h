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

#ifndef OCEANBASE_SQL_OB_INTERSET_ROUTE_POLICY_H
#define OCEANBASE_SQL_OB_INTERSET_ROUTE_POLICY_H
#include "share/ob_define.h"
#include "lib/net/ob_addr.h"
#include "lib/container/ob_iarray.h"
#include "lib/list/ob_list.h"
#include "lib/allocator/page_arena.h"
#include "sql/optimizer/ob_route_policy.h"
#include "common/ob_zone_status.h"
#include "share/partition_table/ob_partition_location.h"
#include "share/ob_server_locality_cache.h"
namespace oceanbase
{
namespace sql
{
class ObCandiTabletLoc;
class ObCandiTableLoc;
class ObIntersectRoutePolicy:public ObRoutePolicy
{
public:
  using ObRoutePolicy::ObRoutePolicy;
};





}//sql
}//oceanbase
#endif
