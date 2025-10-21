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

#include "ob_timestamp_access.h"
#include "ob_timestamp_service.h"
#include "ob_standby_timestamp_service.h"
 
namespace oceanbase
{
namespace transaction
{

int ObTimestampAccess::handle_request(const ObGtsRequest &request, obrpc::ObGtsRpcResult &result)
{
  int ret = OB_SUCCESS;
  if (GTS_LEADER == service_type_) {
    ret = MTL(ObTimestampService *)->handle_request(request, result);
  } else if (STS_LEADER == service_type_) {
    ret = MTL(ObStandbyTimestampService *)->handle_request(request, result);
  } else {
    ret = OB_NOT_MASTER;
    if (EXECUTE_COUNT_PER_SEC(10)) {
      TRANS_LOG(INFO, "ObTimestampAccess service type is FOLLOWER", K(ret), K_(service_type));
    }
  }
  return ret;
}

int ObTimestampAccess::get_number(int64_t &gts)
{
  int ret = OB_SUCCESS;
  if (GTS_LEADER == service_type_) {
    ret = MTL(ObTimestampService *)->get_timestamp(gts);
  } else if (STS_LEADER == service_type_) {
    ret = MTL(ObStandbyTimestampService *)->get_number(gts);
  } else {
    ret = OB_NOT_MASTER;
    if (EXECUTE_COUNT_PER_SEC(16)) {
      TRANS_LOG(INFO, "ObTimestampAccess service type is FOLLOWER", K(ret), K_(service_type));
    }
  }
  return ret;
}

void ObTimestampAccess::get_virtual_info(int64_t &ts_value,
                                         ServiceType &service_type,
                                         common::ObRole &role,
                                         int64_t &proposal_id)
{
  service_type = service_type_;
  if (MTL_TENANT_ROLE_CACHE_IS_PRIMARY_OR_INVALID()) {
    MTL(ObTimestampService *)->get_virtual_info(ts_value, role, proposal_id);
  } else {
    MTL(ObStandbyTimestampService *)->get_virtual_info(ts_value, role, proposal_id);
  }
}

}
}
