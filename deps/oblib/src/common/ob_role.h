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

#ifndef OCEANBASE_COMMON_OB_ROLE_H_
#define OCEANBASE_COMMON_OB_ROLE_H_
#include <stdint.h>
namespace oceanbase
{
namespace common
{
class ObString;

enum ObRole
{
  INVALID_ROLE = 0,

  // Local primary role; supports strongly consistent reads and writes.
  LEADER = 1,

  FOLLOWER = 2,

  // Local physical-standby role; does not serve strongly consistent reads or writes.
  STANDBY_LEADER = 3,
};

// Is it a STRONG_LEADER role
bool is_strong_leader(const ObRole role);

// Is it STANDBY_LEADER role
bool is_standby_leader(const ObRole role);

// Is it a FOLLOWER role
bool is_follower(const ObRole role);

//////////////////////////////////////////////////////
// Utils function

// Aggregated judgment Leader interface
//
// STRONG_LEADER + STANDBY_LEADER
bool is_leader_like(const ObRole role);

int role_to_string(const ObRole &role, char *role_str, const int64_t str_len);


const char *role_to_string(const ObRole &role);
int string_to_role(const ObString &role_str, ObRole &role);
}//end namespace common
}//end namespace oceanbase

#endif //OCEANBASE_COMMON_OB_ROLE_H_
