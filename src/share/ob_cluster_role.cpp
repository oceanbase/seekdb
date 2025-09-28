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
