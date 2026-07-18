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

#define USING_LOG_PREFIX PALF
#include "election_self.h"

namespace oceanbase
{
using namespace common;
namespace palf
{
namespace election
{

ElectionImpl::ElectionImpl()
    : is_inited_(false),
      is_running_(false),
      lock_(common::ObLatchIds::ELECTION_LOCK),
      id_(0),
      self_addr_()
{}

ElectionImpl::~ElectionImpl()
{
  if (is_running_) {
    stop();
  }
  is_inited_ = false;
}

int ElectionImpl::init_and_start(const int64_t id,
                                 const common::ObAddr &self_addr,
                                 const uint64_t inner_priority_seed,
                                 const int64_t restart_counter,
                                 const common::ObFunction<int(const int64_t, const common::ObAddr &)> &prepare_change_leader_cb,
                                 const common::ObFunction<void(ElectionImpl *, common::ObRole, common::ObRole, RoleChangeReason)> &role_change_cb)
{
  UNUSED(inner_priority_seed);
  UNUSED(restart_counter);
  UNUSED(prepare_change_leader_cb);
  int ret = OB_SUCCESS;
  ObSpinLockGuard lock_guard(lock_);
  if (OB_UNLIKELY(is_inited_)) {
    ret = OB_INIT_TWICE;
    PALF_LOG(ERROR, "init election impl twice", K(ret), K(id));
  } else {
    id_ = id;
    self_addr_ = self_addr;
    is_inited_ = true;
    is_running_ = true;
    PALF_LOG(INFO, "election init and start (single-replica self-leader)", K(id), K(self_addr));
  }
  // NB: palf passes only prepare_change_leader_cb; role_change_cb is the default
  // no-op.  Real leader takeover is synthesized by LogStateMgr (get_role()==LEADER).
  role_change_cb(this, common::ObRole::FOLLOWER, common::ObRole::LEADER, RoleChangeReason::DevoteToBeLeader);
  return ret;
}

void ElectionImpl::stop()
{
  ObSpinLockGuard lock_guard(lock_);
  if (OB_UNLIKELY(!is_inited_ || !is_running_)) {
    PALF_LOG_RET(WARN, OB_NOT_RUNNING, "election is not running or not inited", K_(self_addr));
  } else {
    is_running_ = false;
    PALF_LOG(INFO, "election stopped", K_(self_addr));
  }
}

int ElectionImpl::can_set_memberlist(const palf::LogConfigVersion &new_config_version) const
{
  int ret = common::OB_SUCCESS;
  if (OB_UNLIKELY(!new_config_version.is_valid())) {
    ret = OB_INVALID_ARGUMENT;
  } else {
    ObSpinLockGuard lock_guard(lock_);
    CHECK_ELECTION_INIT();
  }
  return ret;
}

int ElectionImpl::set_memberlist(const MemberList &new_memberlist)
{
  int ret = common::OB_SUCCESS;
  if (OB_UNLIKELY(!new_memberlist.is_valid())) {
    ret = OB_INVALID_ARGUMENT;
  } else {
    ObSpinLockGuard lock_guard(lock_);
    CHECK_ELECTION_INIT();
    bool self_in_memberlist = false;
    const ObArray<ObAddr> &addr_list = new_memberlist.get_addr_list();
    for (int64_t i = 0; !self_in_memberlist && i < addr_list.count(); ++i) {
      self_in_memberlist = addr_list.at(i) == self_addr_;
    }
    if (!self_in_memberlist) {
      ret = OB_INVALID_ARGUMENT;
      PALF_LOG(WARN, "self addr not in memberlist", K(ret), K(new_memberlist), K_(self_addr));
    }
  }
  return ret;
}

int ElectionImpl::change_leader_to(const common::ObAddr &dest_addr)
{
  UNUSED(dest_addr);
  return OB_NOT_SUPPORTED;
}

int ElectionImpl::set_priority(ElectionPriority *priority)
{
  int ret = OB_SUCCESS;
  if (OB_ISNULL(priority)) {
    ret = OB_INVALID_ARGUMENT;
  } else {
    ObSpinLockGuard lock_guard(lock_);
    CHECK_ELECTION_INIT();
  }
  return ret;
}

int ElectionImpl::reset_priority()
{
  ObSpinLockGuard lock_guard(lock_);
  CHECK_ELECTION_INIT();
  return OB_SUCCESS;
}

int ElectionImpl::temporarily_downgrade_protocol_priority(const int64_t time_us, const char *reason)
{
  UNUSED(time_us);
  UNUSED(reason);
  return OB_NOT_SUPPORTED;
}

int ElectionImpl::add_inner_priority_seed_bit(const PRIORITY_SEED_BIT new_bit)
{
  UNUSED(new_bit);
  return OB_NOT_SUPPORTED;
}

int ElectionImpl::clear_inner_priority_seed_bit(const PRIORITY_SEED_BIT old_bit)
{
  UNUSED(old_bit);
  return OB_NOT_SUPPORTED;
}

int ElectionImpl::set_inner_priority_seed(const uint64_t seed)
{
  UNUSED(seed);
  return OB_NOT_SUPPORTED;
}

}// namespace election
}// namespace palf
}// namespace oceanbase
