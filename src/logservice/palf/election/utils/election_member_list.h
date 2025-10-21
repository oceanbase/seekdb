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

#ifndef LOGSERVICE_PALF_ELECTION_UTILS_OB_ELECTION_MEMBERLIST_H
#define LOGSERVICE_PALF_ELECTION_UTILS_OB_ELECTION_MEMBERLIST_H

#include "lib/ob_define.h"
#include "lib/container/ob_array.h"
#include "logservice/palf/log_meta_info.h"

namespace oceanbase
{
namespace palf
{
namespace election
{

class MemberList
{
public:
  MemberList();
  bool only_membership_version_different(const MemberList &rhs) const;
  bool operator==(const MemberList &rhs) const;
  bool operator!=(const MemberList &rhs) const;
  int assign(const MemberList &rhs);
  LogConfigVersion get_membership_version() const;
  void set_membership_version(const LogConfigVersion version);
  int64_t get_replica_num() const;
  int set_new_member_list(const common::ObArray<common::ObAddr> &addr_list,
                          const LogConfigVersion membership_version,
                          const int64_t replica_num);
  const common::ObArray<common::ObAddr> &get_addr_list() const;
  bool is_valid() const;
  TO_STRING_KV(K_(addr_list), K_(membership_version), K_(replica_num));
private:
  common::ObArray<common::ObAddr> addr_list_;
  LogConfigVersion membership_version_;
  uint8_t replica_num_;
};

}
}
}

#endif
