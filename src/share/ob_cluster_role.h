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

#ifndef OCEANBASE_COMMON_OB_CLUSTER_ROLE_H_
#define OCEANBASE_COMMON_OB_CLUSTER_ROLE_H_
#include <pthread.h>
#include "lib/string/ob_string.h"
namespace oceanbase
{
namespace common
{
enum ObClusterRole
{
  INVALID_CLUSTER_ROLE = 0,
  PRIMARY_CLUSTER = 1,
  STANDBY_CLUSTER = 2,
};

enum ObClusterStatus
{
  INVALID_CLUSTER_STATUS = 0,
  CLUSTER_VALID = 1,
  CLUSTER_DISABLED,
  REGISTERED,
  CLUSTER_DISABLED_WITH_READONLY,
  MAX_CLUSTER_STATUS,
};

enum ObProtectionMode
{
  INVALID_PROTECTION_MODE = 0,
  MAXIMUM_PERFORMANCE_MODE = 1,
  MAXIMUM_AVAILABILITY_MODE = 2,
  MAXIMUM_PROTECTION_MODE = 3,
  PROTECTION_MODE_MAX
};

enum ObProtectionLevel
{
  INVALID_PROTECTION_LEVEL = 0,
  MAXIMUM_PERFORMANCE_LEVEL = 1,
  MAXIMUM_AVAILABILITY_LEVEL = 2,
  MAXIMUM_PROTECTION_LEVEL = 3,
  RESYNCHRONIZATION_LEVEL = 4,
  MPF_TO_MPT_LEVEL = 5,// mpf->mpt middle state, used under mpf mode
  MPF_TO_MA_MPT_LEVEL = 6,// mpf->ma middle state, mpf->mpt middle state, used under ma mode
  PROTECTION_LEVEL_MAX
};
const char *cluster_role_to_str(ObClusterRole type);

// Check primary protectioni level whether need sync-transport clog to standby
// ex. MA, MPT, and other middle level

// Check standby protection level whether only receive sync-transported clog
// ex. MA, MPT, RESYNC

// Whether in steady state.
// Only steady state can change protection mode, switchover and so on.

// The protection mode which need SYNC mode standby
// MPT or MA mode

//Check if the protection level is the MAXIMUM PROTECTION or MAXIMUM AVAILABILITY protection level

// Updating mode and level is not atomic, to check whether mode and level can match
}//end namespace common
}//end namespace oceanbase

#endif //OCEANBASE_COMMON_OB_CLUSTER_ROLE_H_
