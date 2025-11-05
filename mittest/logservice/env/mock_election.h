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
#include "logservice/palf/election/interface/election.h"
#include "logservice/palf/palf_handle_impl.h"

namespace oceanbase
{
using namespace palf::election;
namespace unittest
{
class MockElection : public Election, public common::LinkHashValue<palf::LSKey>
{
public:
  MockElection();
  MockElection(const int64_t id, const common::ObAddr &self);
  virtual ~MockElection() { }
  int init(const int64_t id, const common::ObAddr &self);
  void stop() override final;
  int can_set_memberlist(const palf::LogConfigVersion &new_config_version) const override final;
  // Set member list
  int set_memberlist(const MemberList &new_member_list) override final;
  // Get the current role of the election
  int get_role(common::ObRole &role, int64_t &epoch) const override final;
  // If you are the leader, then you get the accurate leader; if you are not the leader, then you get the lease owner
  int get_current_leader_likely(common::ObAddr &addr,
                                        int64_t &cur_leader_epoch) const override final;
  // For role change service use
  int change_leader_to(const common::ObAddr &dest_addr) override final;
  int temporarily_downgrade_protocol_priority(const int64_t time_us, const char *reason) override final;
  // Get local address
  const common::ObAddr &get_self_addr() const override final;
  // print log
  int64_t to_string(char *buf, const int64_t buf_len) const override final;
  // Set election priority
  int set_priority(ElectionPriority *priority) override final;
  int reset_priority() override final;
  // Process message
  int handle_message(const ElectionPrepareRequestMsg &msg) override final;
  int handle_message(const ElectionAcceptRequestMsg &msg) override final;
  int handle_message(const ElectionPrepareResponseMsg &msg) override final;
  int handle_message(const ElectionAcceptResponseMsg &msg) override final;
  int handle_message(const ElectionChangeLeaderMsg &msg) override final;
  int set_leader(const common::ObAddr &leader, const int64_t new_epoch);
private:
  int64_t id_;
  common::ObAddr self_;
  common::ObRole role_;
  int64_t epoch_;
  common::ObAddr leader_;
  bool is_inited_;
};
}// unittest
}// oceanbase