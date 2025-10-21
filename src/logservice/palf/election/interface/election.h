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

#ifndef LOGSERVICE_PALF_ELECTION_INTERFACE_OB_I_ELECTION_H
#define LOGSERVICE_PALF_ELECTION_INTERFACE_OB_I_ELECTION_H

#include <time.h>
#include "lib/net/ob_addr.h"
#include "lib/container/ob_array.h"
#include "lib/function/ob_function.h"
#include "lib/lock/ob_spin_lock.h"
#include "lib/oblog/ob_log_module.h"
#include "share/ob_occam_timer.h"
#include "common/ob_role.h"
#include "election_msg_handler.h"
#include "logservice/palf/election/utils/election_member_list.h"
#include "logservice/palf/palf_callback_wrapper.h"

namespace oceanbase
{
namespace palf
{
namespace election
{

typedef common::ObSpinLockGuard LockGuard;

enum class RoleChangeReason
{
  DevoteToBeLeader = 1, // Leader election from Follower to Leader
  ChangeLeaderToBeLeader = 2, // leader-follower switch new Follower becomes Leader
  LeaseExpiredToRevoke = 3, // Leader re-election failed, Lease expired, switch from Leader to Follower
  ChangeLeaderToRevoke = 4, // leader-follower switch old Leader becomes Follower
  StopToRevoke = 5,// After the leader calls the stop interface during an election, the leader steps down
};

class ElectionPrepareRequestMsg;
class ElectionAcceptRequestMsg;
class ElectionPrepareResponseMsg;
class ElectionAcceptResponseMsg;
class ElectionChangeLeaderMsg;
class ElectionMsgSender;
class ElectionPriority;
class RequestChecker;

class Election
{
  friend class RequestChecker;
public:
  virtual ~Election() {}
  virtual void stop() = 0;
  virtual int can_set_memberlist(const palf::LogConfigVersion &new_config_version) const = 0;
  // Set member list
  virtual int set_memberlist(const MemberList &new_member_list) = 0;
  // Get the current role of the election
  virtual int get_role(common::ObRole &role, int64_t &epoch) const = 0;
  // If oneself is the leader, then the accurate leader is obtained; if oneself is not the leader, then the owner of the lease is obtained
  virtual int get_current_leader_likely(common::ObAddr &addr,
                                        int64_t &cur_leader_epoch) const = 0;
  // for role change service use
  virtual int change_leader_to(const common::ObAddr &dest_addr) = 0;
  virtual int temporarily_downgrade_protocol_priority(const int64_t time_us, const char *reason) = 0;
  // Get local address
  virtual const common::ObAddr &get_self_addr() const = 0;
  // print log
  virtual int64_t to_string(char *buf, const int64_t buf_len) const = 0;
  // Set election priority
  virtual int set_priority(ElectionPriority *priority) = 0;
  virtual int reset_priority() = 0;
  // Process message
  virtual int handle_message(const ElectionPrepareRequestMsg &msg) = 0;
  virtual int handle_message(const ElectionAcceptRequestMsg &msg) = 0;
  virtual int handle_message(const ElectionPrepareResponseMsg &msg) = 0;
  virtual int handle_message(const ElectionAcceptResponseMsg &msg) = 0;
  virtual int handle_message(const ElectionChangeLeaderMsg &msg) = 0;
};

inline int64_t get_monotonic_ts()
{
  int64_t ts = 0;
  timespec tm;
  if (OB_UNLIKELY(0 != clock_gettime(CLOCK_MONOTONIC, &tm))) {
    ELECT_LOG_RET(ERROR, common::OB_ERROR, "FATAL ERROR!!! get monotonic clock ts failed!");
    abort();
  } else {
    ts = tm.tv_sec * 1000000 + tm.tv_nsec / 1000;
  }
  return ts;
}

extern int64_t INIT_TS;
extern ObOccamTimer GLOBAL_REPORT_TIMER;// used to report election event to inner table

inline int GLOBAL_INIT_ELECTION_MODULE(const int64_t queue_size_square_of_2 = 10)
{
  int ret = common::OB_SUCCESS;
  static int64_t call_times = 0;
  if (ATOMIC_FAA(&call_times, 1) == 0) {
    if (ATOMIC_LOAD(&INIT_TS) <= 0) {
      ATOMIC_STORE(&INIT_TS, get_monotonic_ts());
    }
    if (OB_FAIL(GLOBAL_REPORT_TIMER.init_and_start(1, 10_ms, "GEleTimer", queue_size_square_of_2))) {
      ELECT_LOG(ERROR, "int global report timer failed", KR(ret));
    } else {
      ELECT_LOG(INFO, "election module global init success");
    }
  } else {
    ELECT_LOG(WARN, "election module global init has been called", K(call_times), K(lbt()));
  }
  return ret;
}

}// namespace election
}// namespace palf
}// namesapce oceanbase

#endif
