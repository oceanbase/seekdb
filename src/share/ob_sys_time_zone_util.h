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

// Tenant system-time-zone helper used by DDL paths.
#ifndef OCEANBASE_SHARE_OB_SYS_TIME_ZONE_UTIL_H_
#define OCEANBASE_SHARE_OB_SYS_TIME_ZONE_UTIL_H_
#include "lib/string/ob_fixed_length_string.h"
#include "lib/timezone/ob_timezone_info.h"
namespace oceanbase
{
namespace share
{
class ObSysTimeZoneUtil
{
public:
  static int get_tenant_sys_time_zone_wrap(common::ObFixedLengthString<common::OB_MAX_TIMESTAMP_TZ_LENGTH> &time_zone,
      common::ObTimeZoneInfoWrap &time_zone_info_wrap);
};
} // namespace share
} // namespace oceanbase
#endif
