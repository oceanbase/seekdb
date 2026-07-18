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

#include "ob_log_handler_base.h"

namespace oceanbase
{
namespace logservice
{
ObLogHandlerBase::ObLogHandlerBase() :
  lock_(),
  role_(common::FOLLOWER),
  proposal_id_(palf::INVALID_PROPOSAL_ID),
  id_(-1),
  palf_handle_(),
  palf_env_(NULL),
  is_in_stop_state_(true),
  is_inited_(false)
{}

int ObLogHandlerBase::prepare_switch_role(common::ObRole &curr_role,
                                          int64_t &curr_proposal_id,
                                          common::ObRole &new_role,
                                          int64_t &new_proposal_id,
                                          bool &is_pending_state) const
{
  int ret = OB_SUCCESS;
  RLockGuard guard(lock_);
  if (OB_FAIL(palf_handle_.get_role(new_role, new_proposal_id, is_pending_state))) {
    PALF_LOG(WARN, "PalfHandle get_role failed", K(ret), K_(id), K(curr_role), K(curr_proposal_id));
  } else {
    curr_role = role_;
    curr_proposal_id = proposal_id_;
    PALF_LOG(TRACE, "prepare_switch_role success", K(ret), K_(id), K(curr_role), K(curr_proposal_id),
        K(new_role), K(new_proposal_id));
  }
  return ret;
}

int ObLogHandlerBase::advance_election_epoch_and_downgrade_priority(const int64_t downgrade_priority_time_us,
                                                                    const char *reason)
{
  RLockGuard guard(lock_);
  return palf_handle_.advance_election_epoch_and_downgrade_priority(proposal_id_, downgrade_priority_time_us, reason);
}

int ObLogHandlerBase::change_leader_to(const common::ObAddr &dst_addr)
{
  int ret = OB_SUCCESS;
  RLockGuard guard(lock_);
  if (OB_FAIL(palf_handle_.change_leader_to(dst_addr))) {
    PALF_LOG(WARN, "palf change_leader failed", K(ret), K(dst_addr), K(palf_handle_));
  } else {
    PALF_LOG(INFO, "ObLogHandler change_laeder success", K(ret), K(dst_addr), K(palf_handle_));
  }
  return ret;
}

int ObLogHandlerBase::get_role(common::ObRole &role, int64_t &proposal_id) const
{
  int ret = OB_SUCCESS;
  bool is_pending_state = false;
  int64_t curr_palf_proposal_id;
  ObRole curr_palf_role;
  // Get the current proposal_id
  RLockGuard guard(lock_);
  const int64_t saved_proposal_id = ATOMIC_LOAD(&proposal_id_);
  const ObRole saved_role = ATOMIC_LOAD(&role_);
  if (IS_NOT_INIT) {
    ret = OB_NOT_INIT;
  } else if (is_in_stop_state_) {
    ret = OB_NOT_RUNNING;
  } else if (FOLLOWER == saved_role) {
    role = FOLLOWER;
    proposal_id = saved_proposal_id;
  } else if (OB_FAIL(palf_handle_.get_role(curr_palf_role, curr_palf_proposal_id, is_pending_state))) {
    CLOG_LOG(WARN, "get_role failed", K(ret));
  } else if (curr_palf_proposal_id != saved_proposal_id) {
    // palf's proposal_id has changed, return FOLLOWER
    role = FOLLOWER;
    proposal_id = saved_proposal_id;
  } else {
    role = curr_palf_role;
    proposal_id = saved_proposal_id;
  }
  return ret;
}

// Note: do not acquire any lock in the function
int ObLogHandlerBase::get_role_atomically(common::ObRole &role) const
{
  int ret = OB_SUCCESS;
  role = ATOMIC_LOAD(&role_);
  return ret;
}
} // end namespace logservice
} // end namespace oceanbase
