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
