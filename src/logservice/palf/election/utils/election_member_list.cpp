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

#include "election_member_list.h"
#include "election_common_define.h"

namespace oceanbase
{
namespace palf
{
namespace election
{

using namespace common;

MemberList::MemberList() : replica_num_(0)
{
  addr_list_.set_attr(ObMemAttr(OB_SERVER_TENANT_ID, "AddrList"));
}

bool MemberList::only_membership_version_different(const MemberList &rhs) const
{
  bool ret = true;
  if (*this == rhs) {
    ELECT_LOG(WARN, "even membership version is same", K(*this), K(rhs), KR(ret));
  } else if (replica_num_ != rhs.replica_num_) {
    ret = false;
  } else if (addr_list_.count() != rhs.addr_list_.count()) {
    ret = false;
  } else {
    for (int64_t idx = 0; idx < addr_list_.count(); ++idx) {
      if (addr_list_[idx] != rhs.addr_list_[idx]) {
        ret = false;
        break;
      }
    }
    if (ret) {
      if (membership_version_ == rhs.membership_version_) {
        ELECT_LOG(ERROR, "not found different element", K(*this), K(rhs), KR(ret));
      }
    }
  }
  return ret;
}

bool MemberList::operator==(const MemberList &rhs) const
{
  ELECT_TIME_GUARD(500_ms);
  bool ret = false;
  // Check if the information of the old member group is consistent
  int valid_member_list_count = 0;
  valid_member_list_count += rhs.is_valid() ? 1 : 0;
  valid_member_list_count += this->is_valid() ? 1 : 0;
  if (valid_member_list_count == 0) {// both are invalid}
    ret = true;
  } else if (valid_member_list_count == 2) {// both are valid
    if (membership_version_ == rhs.membership_version_ && replica_num_ == rhs.replica_num_) {
      // Member version number and replica count are equal
      if (addr_list_.count() == rhs.addr_list_.count()) {// The number of members in the list is consistent
        ret = true;
        for (int64_t i = 0; i < addr_list_.count() && ret; ++i) {
          // Determine if every member in our member list can be found in rhs
          if (addr_list_[i] != rhs.addr_list_[i]) {// The order and members of the member list must be consistent
            ret = false;
          }
        }
      } else {// The number of members in the list is inconsistent
        ret = false;
      }
    } else {// member version number and replica count are not equal
      ret = false;
    }
  } else {// one is valid, the other is invalid
    ret = false;
  }
  return ret;
}

bool MemberList::operator!=(const MemberList &rhs) const { return !this->operator==(rhs); }

int MemberList::assign(const MemberList &rhs)
{
  ELECT_TIME_GUARD(500_ms);
  int ret = OB_SUCCESS;
  if (CLICK_FAIL(addr_list_.assign(rhs.get_addr_list()))) {
    ELECT_LOG(ERROR, "assign addrlist filed", KR(ret));
  } else {
    membership_version_ = rhs.membership_version_;
    replica_num_ = rhs.replica_num_;
  }
  return ret;
}

int MemberList::set_new_member_list(const common::ObArray<common::ObAddr> &addr_list,
                                    const LogConfigVersion membership_version,
                                    const int64_t replica_num)
{
  ELECT_TIME_GUARD(500_ms);
  int ret = OB_SUCCESS;
  if (CLICK_FAIL(addr_list_.assign(addr_list))) {
    ELECT_LOG(ERROR, "assign addrlist filed", KR(ret));
  } else {
    membership_version_ = membership_version;
    replica_num_ = replica_num;
  }
  return ret;
}

bool MemberList::is_valid() const
{
  bool ret = false;
  if (addr_list_.count() != 0 && membership_version_.is_valid() && replica_num_ > 0) {
    ret = true;
  }
  return ret;
}

LogConfigVersion MemberList::get_membership_version() const { return membership_version_; }

void MemberList::set_membership_version(const LogConfigVersion version) { membership_version_ = version; }

int64_t MemberList::get_replica_num() const { return replica_num_; }

const ObArray<ObAddr> &MemberList::get_addr_list() const { return addr_list_; }

}
}
}
