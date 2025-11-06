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

#ifndef OCEANBASE_UNITTEST_LOGSERVICE_MOCK_CONTAINER_ELECTION_
#define OCEANBASE_UNITTEST_LOGSERVICE_MOCK_CONTAINER_ELECTION_

#include "logservice/palf/election/interface/election.h"

namespace oceanbase
{
namespace palf
{
using namespace election;

namespace mockelection
{
class MockElection : public palf::election::Election
{
public:
  MockElection() {}
  ~MockElection() {}

  int start()
  {
    int ret = OB_SUCCESS;
    return ret;
  }
  void stop() override final
  {}
  int can_set_memberlist(const palf::LogConfigVersion &new_config_version) const override final
  {
    int ret = OB_SUCCESS;
    UNUSED(new_config_version);
    return ret;
  }
  // Set member list
  int set_memberlist(const MemberList &new_member_list) override final
  {
    int ret = OB_SUCCESS;
    UNUSED(new_member_list);
    return ret;
  }
  int revoke(const RoleChangeReason &reason)
  {
    UNUSED(reason);
    int ret = OB_SUCCESS;
    return ret;
  }
  int set_priority(ElectionPriority *) override final { return  OB_SUCCESS; }
  int reset_priority() override final { return  OB_SUCCESS; }
  // Get the current role of the election
  int get_role(common::ObRole &role, int64_t &epoch) const override final
  {
    int ret = OB_SUCCESS;
    role = role_;
    epoch = leader_epoch_;
    return ret;
  }
  // If you are the leader, then you get the accurate leader, if you are not the leader, then you get the lease owner
  int get_current_leader_likely(common::ObAddr &p_addr,
                                int64_t &p_cur_leader_epoch) const override final
  {
    int ret = OB_SUCCESS;
    p_addr = leader_;
    p_cur_leader_epoch = leader_epoch_;
    return ret;
  }
  // For internal testing to switch the main use
  virtual int change_leader_to(const common::ObAddr &dest_addr) override final
  {
    int ret = OB_SUCCESS;
    UNUSED(dest_addr);
    return ret;
  }
  virtual int temporarily_downgrade_protocol_priority(const int64_t time_us, const char *reason) override final
  {
    int ret = OB_SUCCESS;
    UNUSED(time_us);
    UNUSED(reason);
    return OB_SUCCESS;
  }
  // Get local address
  const common::ObAddr &get_self_addr() const override final
  {
    return self_;
  }
  // print log
  virtual int64_t to_string(char *buf, const int64_t buf_len) const override final
  {
    UNUSED(buf);
    UNUSED(buf_len);
    return 0;
  }
  // Process message
  virtual int handle_message(const ElectionPrepareRequestMsg &msg) override final
  {
    int ret = OB_SUCCESS;
    UNUSED(msg);
    return ret;
  }
  virtual int handle_message(const ElectionAcceptRequestMsg &msg) override final
  {
    int ret = OB_SUCCESS;
    UNUSED(msg);
    return ret;
  }
  virtual int handle_message(const ElectionPrepareResponseMsg &msg) override final
  {
    int ret = OB_SUCCESS;
    UNUSED(msg);
    return ret;
  }
  virtual int handle_message(const ElectionAcceptResponseMsg &msg) override final
  {
    int ret = OB_SUCCESS;
    UNUSED(msg);
    return ret;
  }
  virtual int handle_message(const ElectionChangeLeaderMsg &msg) override final
  {
    int ret = OB_SUCCESS;
    UNUSED(msg);
    return ret;
  }
public:
  common::ObAddr self_;
  common::ObAddr leader_;
  int64_t leader_epoch_;
  common::ObRole role_;
};
} // end of election
} // end of palf
} // end of oceanbase

#endif
