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

#ifndef LOGSERVICE_PALF_ELECTION_UTILS_ELECTION_EVENT_HISTORY_ACCESSOR_H
#define LOGSERVICE_PALF_ELECTION_UTILS_ELECTION_EVENT_HISTORY_ACCESSOR_H

#include "lib/list/ob_dlist.h"
#include "lib/oblog/ob_log_time_fmt.h"
#include "lib/string/ob_string_holder.h"
#include "lib/utility/ob_print_utils.h"
#include "logservice/palf/election/interface/election.h"
#include "logservice/palf/election/utils/election_member_list.h"
#include "logservice/palf/election/utils/election_utils.h"

namespace oceanbase
{
namespace palf
{
namespace election
{

enum class ElectionEventType
{
  VOTE,
  DECENTRALIZED_TO_BE_LEADER,
  DIRECTLY_CHANGE_LEADER,
  PREPARE_CHANGE_LEADER,
  CHANGE_LEADER_TO_REVOKE,
  LEASE_EXPIRED_TO_REVOKE,
  CHANGE_LEADER_TO_TAKEOVER,
  WITNESS_CHANGE_LEADER,
  CHANGE_MEMBERLIST,
  CHANGE_LEASE_TIME,
  LEASE_EXPIRED,
};

inline const char *obj_to_string(ElectionEventType type)
{
  switch (type) {
    case ElectionEventType::VOTE:
      return "vote";
    case ElectionEventType::DECENTRALIZED_TO_BE_LEADER:
      return "decentralized to be leader";
    case ElectionEventType::DIRECTLY_CHANGE_LEADER:
      return "directly change leader";
    case ElectionEventType::PREPARE_CHANGE_LEADER:
      return "prepare change leader";
    case ElectionEventType::CHANGE_LEADER_TO_REVOKE:
      return "change leader to revoke";
    case ElectionEventType::LEASE_EXPIRED_TO_REVOKE:
      return "lease expired to revoke";
    case ElectionEventType::CHANGE_LEADER_TO_TAKEOVER:
      return "change leader to takeover";
    case ElectionEventType::WITNESS_CHANGE_LEADER:
      return "witness change leader";
    case ElectionEventType::CHANGE_MEMBERLIST:
      return "change memberlist";
    case ElectionEventType::CHANGE_LEASE_TIME:
      return "change lease time";
    case ElectionEventType::LEASE_EXPIRED:
      return "lease expired";
    default:
      return "unknown type";
  }
}

class EventRecorder
{
public:
  EventRecorder(const int64_t &ls_id, const ObAddr &self_addr, ObOccamTimer *&timer) :
  ls_id_(ls_id), self_addr_(self_addr), timer_(timer), need_report_(true) {}
  void set_need_report(const bool need_report) { need_report_ = need_report; }
  // proposer event
  int report_decentralized_to_be_leader_event(const MemberListWithStates &member_list_with_states);// Elected as Leader
  int report_leader_lease_expired_event(const MemberListWithStates &member_list_with_states);// Leader lease renewal failed, resigning
  int report_directly_change_leader_event(const ObAddr &dest_svr, const ObStringHolder &reason);// Leader directly switch to leader without going through the RCS process
  int report_prepare_change_leader_event(const ObAddr &dest_svr, const ObStringHolder &reason);// Old Leader prepares for leader-follower switch
  int report_change_leader_to_revoke_event(const ObAddr &dest_svr);// Old Leader resigns
  int report_change_leader_to_takeover_event(const ObAddr &addr);// New Leader takes over
  int report_member_list_changed_event(const MemberList &old_list, const MemberList &new_list);// Modify member list
  // acceptor event
  int report_vote_event(const ObAddr &dest_svr, const ObStringHolder &reason);// one-to-all prepare phase voting
  int report_acceptor_lease_expired_event(const Lease &lease);// Lease expired
  int report_acceptor_witness_change_leader_event(const ObAddr &old_leader, const ObAddr &new_leader);// witness leader switch event
  int64_t to_string(char *buf, const int64_t len) const;
private:
  int report_event_(ElectionEventType type, const common::ObString &info);
  const int64_t &ls_id_;
  const ObAddr &self_addr_;
  ObOccamTimer *&timer_;
  bool need_report_;
};

}
}
}

#endif
