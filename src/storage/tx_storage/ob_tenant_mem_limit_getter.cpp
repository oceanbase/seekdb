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

#include "ob_tenant_mem_limit_getter.h"
#include "observer/omt/ob_multi_tenant.h"  // previously hidden behind a transitive include
#include "share/rc/ob_module_provider.h"

#include "storage/tx_storage/ob_tenant_freezer.h"

namespace oceanbase
{
using namespace share;
namespace common
{

ObTenantMemLimitGetter &ObTenantMemLimitGetter::get_instance()
{
  static ObTenantMemLimitGetter instance_;
  return instance_;
}

bool ObTenantMemLimitGetter::has_tenant() const
{
  bool bool_ret = false;
  int ret = OB_SUCCESS;
  omt::ObMultiTenant *omt = GCTX.omt_;
  if (OB_ISNULL(omt)) {
    ret = OB_ERR_UNEXPECTED;
    LOG_WARN("omt should not be null", K(ret));
  } else if (omt->has_tenant()) {
    bool_ret = true;
  } else {
    // do nothing
  }
  return bool_ret;
}


int ObTenantMemLimitGetter::get_tenant_mem_limit(
    int64_t &lower_limit,
    int64_t &upper_limit) const
{
  int ret = OB_SUCCESS;
  MOD_SCOPE {
    storage::ObTenantFreezer *freezer = nullptr;
    freezer = share::g_mp->tenant_freezer();
    if (OB_ISNULL(freezer)) {
      LOG_WARN("freezer is null");
      ret = OB_ERR_UNEXPECTED;
    } else if (OB_FAIL(freezer->get_tenant_mem_limit(lower_limit,
                                              upper_limit))) {
      LOG_WARN("get tenant mem limit failed.", K(ret));
    }
  }
  return ret;
}

}
}
