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

#ifndef OCEANBASE_SHARE_OB_ZONE_STATUS_H_
#define OCEANBASE_SHARE_OB_ZONE_STATUS_H_

#include "lib/string/ob_string.h"

namespace oceanbase
{
namespace share
{

struct ObZoneStatus
{
  enum Status
  {
    INACTIVE = 1,
    ACTIVE = 2, // zone is working
    UNKNOWN, // not a status, just the max limit value
  };
  static const char *get_status_str(const Status status);
  static Status get_status(const common::ObString &status_str);
};

inline ObZoneStatus::Status ObZoneStatus::get_status(const common::ObString &status_str)
{
  Status ret_status = UNKNOWN;
  if (status_str == common::ObString::make_string(get_status_str(INACTIVE))) {
    ret_status = INACTIVE;
  } else if (status_str == common::ObString::make_string(get_status_str(ACTIVE))) {
    ret_status = ACTIVE;
  } else {
    SERVER_LOG_RET(WARN, common::OB_ERR_UNEXPECTED, "invalid status_str, return UNKNOWN status", K(status_str));
  }
  return ret_status;
}

inline const char *ObZoneStatus::get_status_str(const ObZoneStatus::Status status)
{
  const char *str = "UNKNOWN";
  switch (status) {
    case ACTIVE:
      str = "ACTIVE";
      break;
    case INACTIVE:
      str = "INACTIVE";
      break;
    default:
      SERVER_LOG_RET(WARN, common::OB_ERR_UNEXPECTED, "unknown zone status, fatal error", K(status));
      break;
  }

  return str;
}

} // end namespace share
} // end namespace oceanbase
#endif  //OCEANBASE_SHARE_OB_ZONE_STATUS_H_
