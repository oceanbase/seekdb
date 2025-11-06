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

#ifndef OCEANBASE_TRANSACTION_OB_TENANT_WEAK_READ_STAT_H_
#define OCEANBASE_TRANSACTION_OB_TENANT_WEAK_READ_STAT_H_

#include "lib/ob_define.h"
#include "lib/net/ob_addr.h"                          // ObAddr
#include "lib/lock/ob_spin_rwlock.h"                  // SpinRWLock
#include "ob_tenant_weak_read_server_version_mgr.h"   // ObTenantWeakReadServerVersionMgr

namespace oceanbase
{
using namespace common;
namespace transaction
{
class ObTenantWeakReadStat
{
public:
  ObTenantWeakReadStat();
  virtual ~ObTenantWeakReadStat();
  void destroy();

public:
  uint64_t tenant_id_;
  //server level weak read info stat
  share::SCN server_version_;              // server level weak read version
  int64_t total_part_count_;            // total partition count
  int64_t valid_inner_part_count_;            // valid inner partition count
  int64_t valid_user_part_count_;
  int64_t server_version_delta_;
  share::SCN local_cluster_version_;
  int64_t local_cluster_version_delta_;
  common::ObAddr self_;

  // heartbeat info stat(from server to wrs leader)
  int64_t cluster_heartbeat_post_tstamp_;         //last heartbeat post timestamp
  int64_t cluster_heartbeat_post_count_;
  int64_t cluster_heartbeat_succ_tstamp_;
  int64_t cluster_heartbeat_succ_count_;
  common::ObAddr cluster_master_;

  // self check info stat
  int64_t self_check_tstamp_;      // last self check timestamp
  int64_t local_current_tstamp_;

  //cluster level weak read info
  int64_t in_cluster_service_;                 	      //if in cluster weak read service
  int64_t is_cluster_master_;
  int64_t cluster_service_epoch_;                     // cluster master epoch
  int64_t cluster_servers_count_;                     // server count
  int64_t cluster_skipped_servers_count_;             // skipped server count
  int64_t cluster_version_gen_tstamp_;		      // last cluster version generation timestamp
  share::SCN cluster_version_;            		      // cluster level weak read version
  int64_t cluster_version_delta_;
  share::SCN min_cluster_version_;
  share::SCN max_cluster_version_;
};

}// transaction
}// oceanbase

#endif /* OCEANBASE_TRANSACTION_OB_TENANT_WEAK_READ_STAT_H_ */
