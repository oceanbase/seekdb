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

#include "election_impl.h"
#include "observer/ob_server.h"
namespace oceanbase
{
using namespace share;
namespace palf
{
namespace election
{

int64_t MAX_TST = 1_s;
int64_t INIT_TS = -1;
ObOccamTimer GLOBAL_REPORT_TIMER;

void DefaultRoleChangeCallBack::operator()(ElectionImpl *election,
                                           common::ObRole before,
                                           common::ObRole after,
                                           RoleChangeReason reason)
{
  UNUSED(election);
  UNUSED(before);
  UNUSED(after);
  UNUSED(reason);
}

ElectionImpl::ElectionImpl()
    : is_inited_(false),
      is_running_(false),
      lock_(common::ObLatchIds::ELECTION_LOCK)
{}

ElectionImpl::~ElectionImpl()
{
  #define PRINT_WRAPPER K(*this)
  if (is_running_) {
    stop();
  }
  is_inited_ = false;
  LOG_DESTROY(INFO, "election destroyed");
  #undef PRINT_WRAPPER
}

int ElectionImpl::init_and_start(const int64_t id,
                                 const common::ObAddr &self_addr,
                                 const uint64_t inner_priority_seed,/*smaller value has higher priority*/
                                 const int64_t restart_counter,
                                 const ObFunction<int(const int64_t, const ObAddr &)> &prepare_change_leader_cb,
                                 const ObFunction<void(ElectionImpl *, common::ObRole, common::ObRole, RoleChangeReason)> &role_change_cb)
{
  UNUSED(inner_priority_seed);
  UNUSED(restart_counter);
  UNUSED(prepare_change_leader_cb);
  ELECT_TIME_GUARD(500_ms);
  #define PRINT_WRAPPER KR(ret), K(*this)
  int ret = OB_SUCCESS;
  LockGuard lock_guard(lock_);
  if (OB_UNLIKELY(is_inited_)) {
    ret = OB_INIT_TWICE;
    LOG_INIT(ERROR, "init election impl twice");
  } else {
    id_ = id;
    self_addr_ = self_addr;
    is_inited_ = true;
    is_running_ = true;
    LOG_INIT(INFO, "election init and start");
  }
  role_change_cb(this, common::ObRole::FOLLOWER, common::ObRole::LEADER, RoleChangeReason::DevoteToBeLeader);
  return ret;
  #undef PRINT_WRAPPER
}

void ElectionImpl::stop()
{
  #define PRINT_WRAPPER KR(ret), K(*this)
  int ret = OB_SUCCESS;
  {
    LockGuard lock_guard(lock_);
    if (OB_UNLIKELY(!is_inited_ || !is_running_)) {
      ret = OB_NOT_RUNNING;
      LOG_DESTROY(WARN, "election is not running or not inited");
    } else {
      is_running_ = false;
      LOG_DESTROY(INFO, "election stopped");
    }
  }
  #undef PRINT_WRAPPER
}

int ElectionImpl::can_set_memberlist(const palf::LogConfigVersion &new_config_version) const
{
  #define PRINT_WRAPPER KR(ret), K(*this), K(new_config_version)
  int ret = common::OB_SUCCESS;
  CHECK_ELECTION_ARGS(new_config_version);
  LockGuard lock_guard(lock_);
  CHECK_ELECTION_INIT();
  return ret;
  #undef PRINT_WRAPPER
}

int ElectionImpl::set_memberlist(const MemberList &new_memberlist)
{
  #define PRINT_WRAPPER KR(ret), K(*this), K(new_memberlist)
  int ret = common::OB_SUCCESS;
  CHECK_ELECTION_ARGS(new_memberlist);
  LockGuard lock_guard(lock_);
  CHECK_ELECTION_INIT();
  bool self_in_memberlist = false;
  const ObArray<ObAddr> &addr_list = new_memberlist.get_addr_list();
  for (int64_t i = 0; !self_in_memberlist && i < addr_list.count(); ++i) {
    self_in_memberlist = addr_list.at(i) == self_addr_;
  }
  if (!self_in_memberlist) {
    ret = OB_INVALID_ARGUMENT;
    LOG_DESTROY(WARN, "self addr not in memberlist", K(ret), K(new_memberlist));
  } else {} // do nothing
  return ret;
  #undef PRINT_WRAPPER
}

int ElectionImpl::change_leader_to(const common::ObAddr &dest_addr)
{
  return OB_NOT_SUPPORTED;
}

int ElectionImpl::set_priority(ElectionPriority *priority)
{
  #define PRINT_WRAPPER KR(ret), K(*this), KPC(priority)
  int ret = OB_SUCCESS;
  CHECK_ELECTION_ARGS(priority);
  LockGuard lock_guard(lock_);
  return ret;
  #undef PRINT_WRAPPER
}

int ElectionImpl::reset_priority()
{
  LockGuard lock_guard(lock_);
  CHECK_ELECTION_INIT();
  return OB_SUCCESS;
}

int ElectionImpl::handle_message(const ElectionPrepareRequestMsg &msg)
{
  UNUSED(msg);
  return OB_NOT_SUPPORTED;
}

int ElectionImpl::handle_message(const ElectionAcceptRequestMsg &msg)
{
  UNUSED(msg);
  return OB_NOT_SUPPORTED;
}

int ElectionImpl::handle_message(const ElectionPrepareResponseMsg &msg)
{
  UNUSED(msg);
  return OB_NOT_SUPPORTED;
}

int ElectionImpl::handle_message(const ElectionAcceptResponseMsg &msg)
{
  UNUSED(msg);
  return OB_NOT_SUPPORTED;
}

int ElectionImpl::handle_message(const ElectionChangeLeaderMsg &msg)
{
  UNUSED(msg);
  return OB_NOT_SUPPORTED;
}

const common::ObAddr &ElectionImpl::get_self_addr() const
{
  return self_addr_;
}

int ElectionImpl::temporarily_downgrade_protocol_priority(const int64_t time_us, const char *reason)
{
  return OB_NOT_SUPPORTED;
}

int ElectionImpl::add_inner_priority_seed_bit(const PRIORITY_SEED_BIT new_bit)
{
  return OB_NOT_SUPPORTED;
}

int ElectionImpl::clear_inner_priority_seed_bit(const PRIORITY_SEED_BIT old_bit)
{
  return OB_NOT_SUPPORTED;
}

int ElectionImpl::set_inner_priority_seed(const uint64_t seed)
{
  return OB_NOT_SUPPORTED;
}

}
}
}
