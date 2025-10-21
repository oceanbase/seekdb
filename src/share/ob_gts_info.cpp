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

#include "ob_gts_info.h"

namespace oceanbase
{
namespace common
{
ObGtsInfo::ObGtsInfo()
{
  reset();
}

void ObGtsInfo::reset()
{
  gts_id_ = OB_INVALID_ID;
  gts_name_.reset();
  region_.reset();
  epoch_id_ = OB_INVALID_TIMESTAMP;
  member_list_.reset();
  standby_.reset();
  heartbeat_ts_ = OB_INVALID_TIMESTAMP;
}

bool ObGtsInfo::is_valid() const
{
  // standby cluster cannot guarantee that it will always be a valid value
  return is_valid_gts_id(gts_id_)
         && !gts_name_.is_empty()
         && !region_.is_empty()
         && (OB_INVALID_TIMESTAMP != epoch_id_)
         && member_list_.is_valid()
         && (OB_INVALID_TIMESTAMP != heartbeat_ts_);
}


ObGtsTenantInfo::ObGtsTenantInfo()
{
  reset();
}

void ObGtsTenantInfo::reset()
{
  gts_id_ = OB_INVALID_ID;
  tenant_id_ = OB_INVALID_ID;
  member_list_.reset();
}

} // namespace common
} // namespace oceanbase
