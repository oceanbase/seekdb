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
