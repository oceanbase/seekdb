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

#define USING_LOG_PREFIX SERVER_OMT

#include "ob_tenant_timezone.h"
#include "observer/omt/ob_tenant_timezone_mgr.h"

using namespace oceanbase::common;

namespace oceanbase {
namespace omt {
ObTenantTimezone::ObTenantTimezone(common::ObMySQLProxy &sql_proxy, uint64_t tenant_id)
    : is_inited_(false), tenant_id_(tenant_id),
    tz_info_mgr_(sql_proxy, tenant_id), update_task_not_exist_(false)
{
}
ObTenantTimezone::~ObTenantTimezone()
{
}

int ObTenantTimezone::init()
{
  int ret = OB_SUCCESS;
  if (OB_UNLIKELY(is_inited_)) {
    ret = OB_INIT_TWICE;
    LOG_WARN("init twice", K(ret), K(tenant_id_));
  } else if (OB_FAIL(tz_info_mgr_.init())) {
    LOG_WARN("fail to init tz_info_mgr_", K(ret));
  } else {
    is_inited_ = true;
  }
  LOG_INFO("tenant timezone init", K(ret), K(tenant_id_), K(sizeof(ObTimeZoneInfoManager)));
  return ret;
}

} // omt
} // oceanbase
