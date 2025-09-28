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
   * @description: 返回选举对象当前的角色和epoch
   * @param {ObRole} &role 当前的角色，LEADER always
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
   * @description: 获取当前的leader及其epoch
   * @param {common::ObAddr} 自己的addr
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
