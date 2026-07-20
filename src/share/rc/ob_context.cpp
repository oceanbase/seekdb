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
#include "share/rc/ob_context.h"
#include "share/rc/ob_module_provider.h"
#include "share/rc/ob_tenant_base.h"

using namespace oceanbase::common;
using namespace oceanbase::lib;
namespace oceanbase
{
namespace lib
{
uint64_t current_resource_owner_id()
{
  return CURRENT_ENTITY(RESOURCE_OWNER)->get_owner_id();
}
} // end of namespace lib

namespace share
{


int ObTenantSpace::guard_init_cb(const ObTenantSpace &tenant_space, char *buf, bool &is_inited)
{
  int ret = OB_SUCCESS;
  UNUSEDx(tenant_space, buf);
  is_inited = false;
  return ret;
}

void ObTenantSpace::guard_deinit_cb(const ObTenantSpace &tenant_space, char *buf)
{
  UNUSEDx(tenant_space, buf);
}

ObTenantSpace &ObTenantSpace::root()
{
  static ObTenantSpace *root = nullptr;
  if (OB_UNLIKELY(nullptr == root)) {
    static lib::ObMutex mutex;
    lib::ObMutexGuard guard(mutex);
    if (nullptr == root) {
      static ObTenantSpace tmp(nullptr);
      int ret = tmp.init();
      abort_unless(OB_SUCCESS == ret);
      root = &tmp;
    }
  }
  return *root;
}

ObResourceOwner &ObResourceOwner::root()
{
  static ObResourceOwner *root = nullptr;
  if (OB_UNLIKELY(nullptr == root)) {
    static lib::ObMutex mutex;
    lib::ObMutexGuard guard(mutex);
    if (nullptr == root) {
      static ObResourceOwner tmp(common::OB_SERVER_TENANT_ID);
      int ret = tmp.init();
      abort_unless(OB_SUCCESS == ret);
      root = &tmp;
    }
  }
  return *root;
}

ObTenantSpaceFetcher::ObTenantSpaceFetcher()
  : ret_(OB_SUCCESS),
    entity_(nullptr)
{
  int ret = common::OB_SUCCESS;
  ObTenantSpace *tmp = nullptr;
  if (OB_FAIL(get_tenant_ctx_with_tenant_lock(tmp))) {
    if (REACH_TIME_INTERVAL(1000 * 1000)) {
      SHARE_LOG(WARN, "get tenant ctx failed", K(ret));
    }
  } else if (OB_ISNULL(tmp)) {
    ret = OB_ERR_UNEXPECTED;
    SHARE_LOG(WARN, "null ptr", K(ret));
  } else {
    entity_ = tmp;
  }
  if (OB_FAIL(ret) && ret == OB_IN_STOP_STATE) {
    ret = OB_TENANT_NOT_IN_SERVER;
  }
  ret_ = ret;
}

ObTenantSpaceFetcher::~ObTenantSpaceFetcher()
{
  if (entity_ != nullptr && entity_->get_tenant() != nullptr) {
    entity_->get_tenant()->unlock();
    entity_ = nullptr;
  }
}


} // end of namespace share
} // end of namespace oceanbase
