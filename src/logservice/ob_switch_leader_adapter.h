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

#ifndef OCEANBASE_LOGSERVICE_OB_LEADER_SWITCH_
#define OCEANBASE_LOGSERVICE_OB_LEADER_SWITCH_

#include "lib/net/ob_addr.h"

namespace oceanbase
{
namespace logservice
{

class ObSwitchLeaderAdapter
{
public:
  ObSwitchLeaderAdapter() { }
  ~ObSwitchLeaderAdapter() { }

  static int remove_from_election_blacklist(const int64_t palf_id, const common::ObAddr &server);
  static int set_election_blacklist(const int64_t palf_id, const common::ObAddr &server);

private:
  static int is_meta_tenant_dropped_(const uint64_t tenant_id, bool &is_dropped);
};
} // end namespace logservice
} // end namespace oceanbase
#endif
