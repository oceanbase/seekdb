/**
 * Copyright (c) 2021 OceanBase
 * OceanBase CE is licensed under Mulan PubL v2.
 * You can use this software according to the terms and conditions of the Mulan PubL v2.
 * You may obtain a copy of Mulan PubL v2 at:
 *          http://license.coscl.org.cn/MulanPubL-2.0
 * THIS SOFTWARE IS PROVIDED ON AN "AS IS" BASIS, WITHOUT WARRANTIES OF ANY KIND,
 * EITHER EXPRESS OR IMPLIED, INCLUDING BUT NOT LIMITED TO NON-INFRINGEMENT,
 * MERCHANTABILITY OR FIT FOR A PARTICULAR PURPOSE.
 * See the Mulan PubL v2 for more details.
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
