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
#include "ob_server_priority.h"

namespace oceanbase
{
namespace logservice
{
using namespace oceanbase::common;

const char *print_region_priority(RegionPriority type)
{
  const char *str = nullptr;
  switch (type) {
    case REGION_PRIORITY_UNKNOWN:
      str = "UNKNOWN";
      break;
    case REGION_PRIORITY_LOW:
      str = "LOW";
      break;
    case REGION_PRIORITY_HIGH:
      str = "HIGH";
      break;
    default:
      str = "INVALID";
      break;
  }
  return str;
}

const char *print_replica_priority(ReplicaPriority type)
{
  const char *str = nullptr;
  switch (type) {
    case REPLICA_PRIORITY_UNKNOWN:
      str = "UNKNOWN";
      break;
    case REPLICA_PRIORITY_OTHER:
      str = "OTHER_REPLICA";
      break;
    case REPLICA_PRIORITY_FULL:
      str = "FULL";
      break;
    case REPLICA_PRIORITY_READONLY:
      str = "READ_ONLY";
      break;
    case REPLICA_PRIORITY_LOGONLY:
      str = "LOG_ONLY";
      break;
    default:
      str = "INVALID";
      break;
  }
  return str;
}

int get_replica_priority(const common::ObReplicaType type,
    ReplicaPriority &priority)
{
  int ret = OB_SUCCESS;
  priority = REPLICA_PRIORITY_UNKNOWN;

  if (!ObReplicaTypeCheck::is_replica_type_valid(type)) {
    ret = OB_INVALID_ARGUMENT;
  } else {
    switch(type) {
      case REPLICA_TYPE_FULL: {
        priority = REPLICA_PRIORITY_FULL;
        break;
      }
      case REPLICA_TYPE_READONLY: {
        priority = REPLICA_PRIORITY_READONLY;
        break;
      }
      case REPLICA_TYPE_LOGONLY: {
        priority = REPLICA_PRIORITY_LOGONLY;
        break;
      }
      default: {
        priority = REPLICA_PRIORITY_OTHER;
        break;
      }
    };
  }

  return ret;
}

} // namespace logservice
} // namespace oceanbase
