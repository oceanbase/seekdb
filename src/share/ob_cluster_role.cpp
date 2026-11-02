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

#define USING_LOG_PREFIX COMMON

#include "share/ob_cluster_role.h"

namespace oceanbase
{
namespace common
{

// The standard view value
static const char *cluster_role_strs[] = { "INVALID", "PRIMARY", "PHYSICAL STANDBY" };
static const char *cluster_status_strs[] = { "INVALID", "VALID", "DISABLED", "REGISTERED",
                                             "DISABLED WITH READ ONLY"};

static const char *cluster_protection_mode_strs[] = { "INVALID PROTECTION MODE", 
                                                      "MAXIMUM PERFORMANCE", 
                                                      "MAXIMUM AVAILABILITY", 
                                                      "MAXIMUM PROTECTION"};
static const char *cluster_protection_level_strs[] = { "INVALID PROTECTION LEVEL", 
                                                       "MAXIMUM PERFORMANCE", 
                                                       "MAXIMUM AVAILABILITY", 
                                                       "MAXIMUM PROTECTION",
                                                       "RESYNCHRONIZATION", 
                                                       "MAXIMUM PERFORMANCE", 
                                                       "MAXIMUM PERFORMANCE"};
const char *cluster_role_to_str(ObClusterRole type)
{
  const char *type_str = "UNKNOWN";
  if (OB_UNLIKELY(type < INVALID_CLUSTER_ROLE) || OB_UNLIKELY(type > STANDBY_CLUSTER)) {
    LOG_ERROR_RET(OB_ERR_UNEXPECTED, "fatal error, unknown cluster type", K(type));
  } else {
    type_str = cluster_role_strs[type];
  }
  return type_str;
}





/**
 * @description:
 *  Check if the protection level is the MAXIMUM PROTECTION or MAXIMUM AVAILABILITY protection level
 * @param[in] level protection level
 */


}
}//end namespace oceanbase
