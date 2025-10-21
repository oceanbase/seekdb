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

#ifndef OCEANBASE_LOGSERVICE_OB_LOG_HANDLER_BASE_
#define OCEANBASE_LOGSERVICE_OB_LOG_HANDLER_BASE_
#include <cstdint>
#include "lib/lock/ob_tc_rwlock.h"
#include "common/ob_role.h"
#include "palf/palf_handle.h"

namespace oceanbase
{
namespace common
{
class ObAddr;
}
namespace palf
{
class PalfEnv;
}
namespace logservice
{
class ObLogHandlerBase
{
public:
  ObLogHandlerBase();
  // @breif query role and proposal_id from ObLogHandlerBase and palf.
  // @param[out], curr_role, role of ObLogHandler.
  // @param[out], curr_proposal_id, proposal_id of ObLogHandler.
  // @param[out], new_role, role of palf.
  // @param[out], new_proposal_id, proposal_id of palf.
  int prepare_switch_role(common::ObRole &curr_role,
                          int64_t &curr_proposal_id,
                          common::ObRole &new_role,
                          int64_t &new_proposal_id,
                          bool &is_pending_state) const;
  // NB: only called by ObRoleChangeService
  virtual void switch_role(const common::ObRole &role, const int64_t proposal_id) = 0;
  int advance_election_epoch_and_downgrade_priority(const int64_t downgrade_priority_time_us,
                                                     const char *reason);
  int change_leader_to(const common::ObAddr &dst_addr);
  int get_role_atomically(common::ObRole &role) const;

protected:
  // @brief query role and proposal_id from ObLogHandler or ObLogRestoreHandler
  // @param[out], role:
  //    LEADER, if 'role_' of ObLogHandler or ObLogRestoreHandler is LEADER and 'proposal_id' is same with PalfHandle.
  //    FOLLOWER, otherwise.
  // @param[out], proposal_id, global monotonically increasing.
  // @retval
  //   OB_SUCCESS
  // NB: for standby, ObLogHandler is always FOLLOWER and for primary, ObLogRestoreHandler is always FOLLOWER
  int get_role(common::ObRole &role, int64_t &proposal_id) const;

public:
  typedef common::RWLock RWLock;
  typedef RWLock::RLockGuard RLockGuard;
  typedef RWLock::WLockGuard WLockGuard;
  mutable RWLock lock_;
  common::ObRole role_;
  int64_t proposal_id_;
  int64_t id_;
  palf::PalfHandle palf_handle_;
  palf::PalfEnv *palf_env_;
  bool is_in_stop_state_;
  bool is_inited_;
};
} // end namespace logservice
} // end namespace oceanbase
#endif
