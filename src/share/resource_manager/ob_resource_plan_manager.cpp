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

#define USING_LOG_PREFIX SHARE
#include "ob_resource_plan_manager.h"
#ifdef _WIN32
#include <windows.h>
#endif
#include "share/resource_manager/ob_cgroup_ctrl.h"
#include "share/ob_server_struct.h"


using namespace oceanbase::common;
using namespace oceanbase::share;

int ObResourcePlanManager::init()
{
  return OB_SUCCESS;
}

// moved definition to observer/omt/ob_multi_tenant.cpp(omt real user)

int64_t ObResourcePlanManager::to_string(char *buf, const int64_t len) const
{
  int ret = OB_SUCCESS;
  int64_t pos = 0;
  if (OB_SUCC(databuff_printf(buf, len, pos, "background_quota:%d", background_quota_))) {
  }
  return pos;
}
