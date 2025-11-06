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

#ifndef LOGSERVICE_PALF_ELECTION_ALGORITHM_OB_ELECTION_IMPL_H
#define LOGSERVICE_PALF_ELECTION_ALGORITHM_OB_ELECTION_IMPL_H

#include "lib/container/ob_array.h"
#include "lib/net/ob_addr.h"
#include "lib/lock/ob_spin_lock.h"
#include "lib/ob_errno.h"
#include "lib/oblog/ob_log_module.h"
#include "lib/string/ob_string_holder.h"
#include "common/ob_role.h"
#include "logservice/palf/election/interface/election.h"
#include "logservice/palf/election/interface/election_msg_handler.h"
#include "logservice/palf/election/interface/election_priority.h"
#include "logservice/palf/election/utils/election_utils.h"
#include "logservice/palf/election/utils/election_args_checker.h"
#include "logservice/palf/election/message/election_message.h"
#include "logservice/palf/election/utils/election_event_recorder.h"

namespace oceanbase
{
namespace unittest
{
class TestElection;
}
namespace palf
{
namespace election
{
class ElectionImpl;
struct DefaultRoleChangeCallBack
{
  void operator()(ElectionImpl *, common::ObRole before, common::ObRole after, RoleChangeReason reason);
};

class RequestChecker;
class ElectionImpl : public Election
{
  friend class unittest::TestElection;
  friend class ElectionProposer;
  friend class ElectionAcceptor;
  friend class RequestChecker;
  friend class DefaultRoleChangeCallBack;
public:
  ElectionImpl();
  ~ElectionImpl();
  int init_and_start(const int64_t id,
                     const common::ObAddr &self_addr,
                     const uint64_t inner_priority_seed,
                     const int64_t restart_counter,
                     const ObFunction<int(const int64_t, const ObAddr &)> &prepare_change_leader_cb,
                     const ObFunction<void(ElectionImpl *, common::ObRole, common::ObRole, RoleChangeReason)> &cb = DefaultRoleChangeCallBack());
  virtual void stop() override final;
  virtual int can_set_memberlist(const palf::LogConfigVersion &new_config_version) const override final;
  virtual int set_memberlist(const MemberList &new_memberlist) override final;
  virtual int change_leader_to(const common::ObAddr &dest_addr) override final;
  virtual int temporarily_downgrade_protocol_priority(const int64_t time_us, const char *reason) override final;
  /**
   * @description: Return the current role and epoch of the election object
   * @param {ObRole} &role Current role, LEADER always
   * @param {int64_t} &epoch 1 always
   * @return {int} OB_SUCCESS always
   * @Date: 2025-07-16 19:57:06
   */
  virtual int get_role(common::ObRole &role, int64_t &epoch) const final
  {
    int ret = common::OB_SUCCESS;
    CHECK_ELECTION_INIT();
    // for observer-lite, only one replica exists, so self is leader forever 
    role = common::ObRole::LEADER;
    epoch = 1;
    return ret;
  }
  /**
   * @description: Get the current leader and its epoch
   * @param {common::ObAddr} own addr
   * @param {int64_t} 1 always
   * @return {int} OB_SUCCESS always
   * @Date: 2025-07-16 19:56:06
   */
  virtual int get_current_leader_likely(common::ObAddr &addr,
                                        int64_t &cur_leader_epoch) const override final
  {
    int ret = common::OB_SUCCESS;
    CHECK_ELECTION_INIT();
    // for observer-lite, only one replica exists, so self is leader forever 
    addr = self_addr_;
    cur_leader_epoch = 1;
    return ret;
  }
  virtual int set_priority(ElectionPriority *priority) override final;
  virtual int reset_priority() override final;
  virtual int handle_message(const ElectionPrepareRequestMsg &msg) override final;
  virtual int handle_message(const ElectionAcceptRequestMsg &msg) override final;
  virtual int handle_message(const ElectionPrepareResponseMsg &msg) override final;
  virtual int handle_message(const ElectionAcceptResponseMsg &msg) override final;
  virtual int handle_message(const ElectionChangeLeaderMsg &msg) override final;
  virtual const common::ObAddr &get_self_addr() const override;
  int add_inner_priority_seed_bit(const PRIORITY_SEED_BIT new_bit);
  int clear_inner_priority_seed_bit(const PRIORITY_SEED_BIT old_bit);
  int set_inner_priority_seed(const uint64_t seed);
  TO_STRING_KV(K_(is_inited), K_(is_running));
private:
  bool is_inited_;
  bool is_running_;
  mutable common::ObSpinLock lock_;
  int64_t id_;
  common::ObAddr self_addr_;
};

}// namespace election
}// namespace palf
}// namesapce oceanbase
#endif
