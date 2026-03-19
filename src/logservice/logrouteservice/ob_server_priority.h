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
#ifndef OCEANBASE_LOGSERVICE_SERVER_PRIORITY_H_
#define OCEANBASE_LOGSERVICE_SERVER_PRIORITY_H_

#include "share/ob_define.h" // ObReplicaType
#include "share/ob_errno.h"

namespace oceanbase
{
namespace logservice
{
// region priority
// The smaller the value, the higher the priority
enum RegionPriority
{
  REGION_PRIORITY_UNKNOWN = -1,
  REGION_PRIORITY_HIGH = 0,
  REGION_PRIORITY_LOW = 1,
  REGION_PRIORITY_MAX
};
const char *print_region_priority(RegionPriority type);

// Replica type priority
// The smaller the value, the higher the priority
//
// The priority is from highest to lowest as follows: L > R > F > OTHER
// where F is a fully functional copy, R is a read-only copy, L is a logged copy
// OTHER is the other type of replica, with the lowest default level
enum ReplicaPriority
{
  REPLICA_PRIORITY_UNKNOWN = -1,
  REPLICA_PRIORITY_LOGONLY = 0,
  REPLICA_PRIORITY_READONLY = 1,
  REPLICA_PRIORITY_FULL = 2,
  REPLICA_PRIORITY_OTHER = 3,
  REPLICA_PRIORITY_MAX
};
const char *print_replica_priority(ReplicaPriority type);

// Get replica priority based on replica type
int get_replica_priority(const common::ObReplicaType type,
    ReplicaPriority &priority);

} // namespace logservice
} // namespace oceanbase

#endif
