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

#define USING_LOG_PREFIX SHARE_SCHEMA

#include "observer/scheduler/ob_tenant_ddl_count_guard.h"

#include "share/ob_server_struct.h"          // GCTX
#include "observer/omt/ob_multi_tenant.h"     // omt::ObMultiTenant inc/dec_tenant_ddl_count (L9 legal downward)

namespace oceanbase
{
namespace share
{
namespace schema
{

int ObTenantDDLCountGuard::try_inc_ddl_count(const int64_t cpu_quota_concurrency)
{
  int ret = OB_SUCCESS;
  omt::ObMultiTenant *omt = GCTX.omt_;
  if (OB_ISNULL(omt)) {
    ret = OB_ERR_UNEXPECTED;
    LOG_WARN("omt is null", KR(ret));
  } else if (OB_FAIL(omt->inc_tenant_ddl_count( cpu_quota_concurrency))) {
    LOG_WARN("fail to inc tenant ddl count", KR(ret));
  } else {
    had_inc_ddl_ = true;
  }
  return ret;
}

ObTenantDDLCountGuard::~ObTenantDDLCountGuard()
{
  int ret = OB_SUCCESS;
  if (had_inc_ddl_) {
    omt::ObMultiTenant *omt = GCTX.omt_;
    if (OB_ISNULL(omt)) {
      ret = OB_ERR_UNEXPECTED;
      LOG_WARN("omt is null", KR(ret));
    } else if (OB_FAIL(omt->dec_tenant_ddl_count())) {
      LOG_WARN("fail to dec tenant ddl count", KR(ret));
    }
  }
}

} // end schema
} // end share
} // end oceanbase
