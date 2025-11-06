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

#include "ob_weak_read_util.h"
#include "storage/tx/ob_timestamp_service.h"
namespace oceanbase
{
using namespace common;
using namespace share;
namespace transaction
{
// 1. follower if readable depend on follower readable snapshot version and weak read cluster version
//    and correspond to clog keepalive msg interval and weak read version refresh interval respectively
// 2. meanwhile weak read cluster version depond on follower readable snapshot version and max stable time
// 3. so keepalive msg interval should bigger than weak read version refresh interval
// 4. keepalive msg interval set DEAULT  if weak read version refresh interval
int64_t ObWeakReadUtil::replica_keepalive_interval()
{
  int64_t interval = 0;
  int64_t weak_read_refresh_interval = GCTX.in_bootstrap_ ? BOOTSTRAP_REPLICA_KEEPALIVE_INTERVAL
    : GCONF.weak_read_version_refresh_interval;
  if (weak_read_refresh_interval <= 0
      || weak_read_refresh_interval > DEFAULT_REPLICA_KEEPALIVE_INTERVAL) {
    interval = DEFAULT_REPLICA_KEEPALIVE_INTERVAL;
  } else {
    interval = weak_read_refresh_interval;
  }
  return interval;
}

// following bellow, weak read version calulate as 'now - max_stale_time', to support weak read version increase
// 1. no partitions in server
// 2. all partitions offline
// 3. all partitions delay too much or in invalid status
// 4. all partitions in migrating and readable snapshot version delay more than 500ms
int ObWeakReadUtil::generate_min_weak_read_version(const uint64_t tenant_id, SCN &scn)
{
  int ret = OB_SUCCESS;
  int64_t max_stale_time = 0;
  // generating min weak version version should statisfy following constraint
  // 1. not smaller than max_stale_time_for_weak_consistency - DEFAULT_MAX_STALE_BUFFER_TIME
  // 2. not bigger than readable snapshot version
  int64_t buffer_time = std::max(
          std::min(static_cast<int64_t>(GCONF.weak_read_version_refresh_interval),
                   static_cast<int64_t>(DEFAULT_REPLICA_KEEPALIVE_INTERVAL)),
          static_cast<int64_t>(DEFAULT_MAX_STALE_BUFFER_TIME));

  if (MTL_TENANT_ROLE_CACHE_IS_PRIMARY()) {
    max_stale_time = GCONF.max_stale_time_for_weak_consistency - buffer_time;
  } else {
  //standby, restore, invalid
    max_stale_time = GCONF.max_stale_time_for_weak_consistency + transaction::ObTimestampService::PREALLOCATE_RANGE_FOR_SWITHOVER - buffer_time;
  }

  max_stale_time = std::max(max_stale_time, static_cast<int64_t>(DEFAULT_REPLICA_KEEPALIVE_INTERVAL));
  SCN tmp_scn;
  if (OB_FAIL(OB_TS_MGR.get_gts(tenant_id, NULL, tmp_scn))) {
    TRANS_LOG(WARN, "get gts cache error", K(ret), K(tenant_id));
  } else {
    // the unit of max_stale_time is us, we should change to ns
    scn.convert_from_ts(tmp_scn.convert_to_ts() - max_stale_time);
  }

  return ret;
}

bool ObWeakReadUtil::enable_monotonic_weak_read(const uint64_t tenant_id)
{
  return GCONF.enable_monotonic_weak_read;
}

int64_t ObWeakReadUtil::max_stale_time_for_weak_consistency(const uint64_t tenant_id, int64_t ignore_warn)
{
  return GCONF.max_stale_time_for_weak_consistency;
}

bool ObWeakReadUtil::check_weak_read_service_available()
{
  return true;
}

}// transaction
}// oceanbase
