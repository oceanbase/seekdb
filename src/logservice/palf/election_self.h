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

// election_self.h
//
// Single-replica (seekdb-lite) degenerate "election".  The real palf election
// algorithm/messages/transport (formerly src/logservice/palf/election/) have
// been deleted: in a single-node single-replica deployment there is no peer to
// vote with, so this replica is leader forever.  This header relocates the small
// self-leader stub plus the minimal interface surface still referenced by the
// rest of palf (LogStateMgr / LogConfigMgr query get_role()/get_current_leader_likely()
// and deterministically take over via the existing follower->reconfirm->leader path).

#ifndef OCEANBASE_LOGSERVICE_PALF_ELECTION_SELF_H_
#define OCEANBASE_LOGSERVICE_PALF_ELECTION_SELF_H_

#include "lib/container/ob_array.h"
#include "lib/net/ob_addr.h"
#include "lib/lock/ob_spin_lock.h"
#include "lib/ob_errno.h"
#include "lib/oblog/ob_log_module.h"
#include "lib/function/ob_function.h"
#include "common/ob_role.h"
#include "logservice/palf/log_meta_info.h"     // LogConfigVersion
#include "palf_callback_wrapper.h"               // LogPlugins, PalfRoleChangeCbWrapper (formerly via election.h)

namespace oceanbase
{
namespace palf
{
namespace election
{

// retained constant: ObTimestampService preallocated range is derived from this.
constexpr int64_t MAX_LEASE_TIME = 10L * 1000L * 1000L; // 10s

// retained for palf_handle_impl priority-seed bookkeeping (all no-ops now).
enum class PRIORITY_SEED_BIT : uint64_t
{
  DEFAULT_SEED = (1ULL << 12),
  TEST_BIT = (1ULL << 13),
  SEED_TEMORARILY_DOWNGRADE_PRIORIY_BIT = (1ULL << 22),
  SEED_IN_REBUILD_PHASE_BIT = (1ULL << 32),
  SEED_NOT_NORMOL_REPLICA_BIT = (1ULL << 48),
};

enum class RoleChangeReason
{
  DevoteToBeLeader = 1,
  ChangeLeaderToBeLeader = 2,
  LeaseExpiredToRevoke = 3,
  ChangeLeaderToRevoke = 4,
  StopToRevoke = 5,
};

// ---- minimal arg-checker macros (relocated from election_args_checker.h) ----
#define CHECK_ELECTION_INIT() \
do {\
  if (OB_UNLIKELY(!is_inited_)) {\
    PALF_LOG_RET(WARN, common::OB_NOT_INIT, "election not init yet", K(*this));\
    return common::OB_NOT_INIT;\
  }\
} while(0)

// election member list: a degenerate single-member view used only to validate
// that self is contained in the configured member list.
class MemberList
{
public:
  MemberList() : addr_list_(), membership_version_(), replica_num_(0) {}
  int set_new_member_list(const common::ObArray<common::ObAddr> &addr_list,
                          const LogConfigVersion membership_version,
                          const int64_t replica_num)
  {
    int ret = common::OB_SUCCESS;
    if (OB_FAIL(addr_list_.assign(addr_list))) {
    } else {
      membership_version_ = membership_version;
      replica_num_ = static_cast<uint8_t>(replica_num);
    }
    return ret;
  }
  const common::ObArray<common::ObAddr> &get_addr_list() const { return addr_list_; }
  bool is_valid() const { return membership_version_.is_valid() && replica_num_ > 0; }
  TO_STRING_KV(K_(addr_list), K_(membership_version), K_(replica_num));
private:
  common::ObArray<common::ObAddr> addr_list_;
  LogConfigVersion membership_version_;
  uint8_t replica_num_;
};

// abstract priority base: only referenced as a pointer type in now-no-op
// set_election_priority()/reset_election_priority() signatures.  The concrete
// impl (coordinator::ElectionPriorityImpl) was deleted with leader_coordinator.
class ElectionPriority
{
public:
  virtual ~ElectionPriority() {}
  virtual int64_t to_string(char *buf, const int64_t buf_len) const = 0;
};

// abstract election interface still queried by LogStateMgr / LogConfigMgr.
class Election
{
public:
  virtual ~Election() {}
  virtual void stop() = 0;
  virtual int can_set_memberlist(const palf::LogConfigVersion &new_config_version) const = 0;
  virtual int set_memberlist(const MemberList &new_member_list) = 0;
  virtual int get_role(common::ObRole &role, int64_t &epoch) const = 0;
  virtual int get_current_leader_likely(common::ObAddr &addr, int64_t &cur_leader_epoch) const = 0;
  virtual int change_leader_to(const common::ObAddr &dest_addr) = 0;
  virtual int temporarily_downgrade_protocol_priority(const int64_t time_us, const char *reason) = 0;
  virtual const common::ObAddr &get_self_addr() const = 0;
  virtual int64_t to_string(char *buf, const int64_t buf_len) const = 0;
  virtual int set_priority(ElectionPriority *priority) = 0;
  virtual int reset_priority() = 0;
};

struct ElectionImpl;
struct DefaultRoleChangeCallBack
{
  void operator()(ElectionImpl *, common::ObRole before, common::ObRole after, RoleChangeReason reason)
  {
    UNUSED(before); UNUSED(after); UNUSED(reason);
  }
};

// constant self-leader election.  init_and_start() makes this replica leader and
// it stays leader forever; the real leader-takeover event is synthesized by the
// unchanged LogStateMgr state machine, which observes get_role()==LEADER on the
// follower-active->reconfirm->leader-active path and fires
// PalfRoleChangeCbWrapper::on_role_change exactly once per LS per boot.
class ElectionImpl : public Election
{
  friend struct DefaultRoleChangeCallBack;
public:
  ElectionImpl();
  ~ElectionImpl();
  int init_and_start(const int64_t id,
                     const common::ObAddr &self_addr,
                     const uint64_t inner_priority_seed,
                     const int64_t restart_counter,
                     const common::ObFunction<int(const int64_t, const common::ObAddr &)> &prepare_change_leader_cb,
                     const common::ObFunction<void(ElectionImpl *, common::ObRole, common::ObRole, RoleChangeReason)> &cb = DefaultRoleChangeCallBack());
  virtual void stop() override final;
  virtual int can_set_memberlist(const palf::LogConfigVersion &new_config_version) const override final;
  virtual int set_memberlist(const MemberList &new_memberlist) override final;
  virtual int change_leader_to(const common::ObAddr &dest_addr) override final;
  virtual int temporarily_downgrade_protocol_priority(const int64_t time_us, const char *reason) override final;
  // single-replica: self is leader forever.
  virtual int get_role(common::ObRole &role, int64_t &epoch) const override final
  {
    int ret = common::OB_SUCCESS;
    CHECK_ELECTION_INIT();
    role = common::ObRole::LEADER;
    epoch = 1;
    return ret;
  }
  virtual int get_current_leader_likely(common::ObAddr &addr, int64_t &cur_leader_epoch) const override final
  {
    int ret = common::OB_SUCCESS;
    CHECK_ELECTION_INIT();
    addr = self_addr_;
    cur_leader_epoch = 1;
    return ret;
  }
  virtual const common::ObAddr &get_self_addr() const override final { return self_addr_; }
  virtual int set_priority(ElectionPriority *priority) override final;
  virtual int reset_priority() override final;
  int add_inner_priority_seed_bit(const PRIORITY_SEED_BIT new_bit);
  int clear_inner_priority_seed_bit(const PRIORITY_SEED_BIT old_bit);
  int set_inner_priority_seed(const uint64_t seed);
  TO_STRING_KV(K_(is_inited), K_(is_running), K_(self_addr));
private:
  bool is_inited_;
  bool is_running_;
  mutable common::ObSpinLock lock_;
  int64_t id_;
  common::ObAddr self_addr_;
};

}// namespace election
}// namespace palf
}// namespace oceanbase
#endif // OCEANBASE_LOGSERVICE_PALF_ELECTION_SELF_H_
