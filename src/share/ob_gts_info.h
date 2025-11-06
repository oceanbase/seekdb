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

#ifndef OCEANBASE_COMMON_OB_GTS_INFO_H_
#define OCEANBASE_COMMON_OB_GTS_INFO_H_

#include "common/ob_region.h"
#include "common/ob_member_list.h"
#include "share/ob_gts_name.h"

namespace oceanbase
{
namespace common
{
class ObGtsInfo
{
public:
  ObGtsInfo();
  ~ObGtsInfo() {}
  void reset();
  bool is_valid() const;
public:
  uint64_t gts_id_;
  common::ObGtsName gts_name_;
  common::ObRegion region_;
  int64_t epoch_id_;
  common::ObMemberList member_list_;
  common::ObAddr standby_;
  int64_t heartbeat_ts_;

  TO_STRING_KV(K(gts_id_), K(gts_name_), K(region_), K(epoch_id_),
               K(member_list_), K(standby_), K(heartbeat_ts_));
};

class ObGtsTenantInfo
{
public:
  ObGtsTenantInfo();
  ~ObGtsTenantInfo() {}
  void reset();
public:
  uint64_t gts_id_;
  uint64_t tenant_id_;
  common::ObMemberList member_list_;

  TO_STRING_KV(K(gts_id_), K(tenant_id_), K(member_list_));
};
} // namespace common
} // namespace oceanbase

#endif // OCEANBASE_COMMON_OB_GTS_INFO_H_
