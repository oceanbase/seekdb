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
