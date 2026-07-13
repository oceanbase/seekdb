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
#include "ob_tenant_base.h"
#include "observer/omt/ob_tenant_mtl_helper.h"  // get_mtl_ptr real user(dual MTL framework definitions, header already legalized as conf L2)
#include "share/rc/ob_module_provider.h"
#include "share/roaringbitmap/ob_rb_memory_mgr.h"
#include "share/schema/ob_schema_struct.h"
#include "share/ob_server_struct.h"

namespace oceanbase
{

namespace common
{
uint64_t mtl_get_id()
{
  return 1;
}
}

namespace common
{


int64_t __attribute__((used)) get_mtl_id()
{
  return 1;
}

ObRbMemMgr *__attribute__((used)) get_rb_mem_mgr()
{
  return ::oceanbase::share::g_mp->rb_mem_mgr();
}

void __attribute__((used)) lib_mtl_switch(std::function<void(int)> fn)
{
  int ret = OB_SUCCESS;
  fn(ret);
}

void __attribute__((used)) lib_mtl_switch(lib::IRunWrapper *run_wrapper, std::function<void()> fn)
{
  UNUSED(run_wrapper);
  fn();
}

int64_t __attribute__((used)) lib_mtl_cpu_count()
{
  return share::ObTenantEnv::get_tenant()->unit_max_cpu();
}


}
namespace share
{
using namespace oceanbase::common;


ObTenantBase::ObTenantBase(const int64_t epoch)
    : epoch_(epoch),
    inited_(false),
    created_(false),
    mtl_init_ctx_(nullptr),
    tenant_role_value_(share::ObTenantRole::Role::INVALID_TENANT),
    unit_max_cpu_(0),
    unit_min_cpu_(0),
    unit_memory_size_(0),
    switchover_epoch_(0),
    marked_prepare_gc_ts_(0)
{
}

ObTenantBase &ObTenantBase::operator=(const ObTenantBase &ctx)
{
  if (this == &ctx) {
    return *this;
  }
  epoch_ = ctx.epoch_;
  mtl_init_ctx_ = ctx.mtl_init_ctx_;
  tenant_role_value_ = ctx.tenant_role_value_;
  switchover_epoch_ = ctx.switchover_epoch_;
  return *this;
}

class FuncWrapper
{
public:
  std::function<void ()> func_;
  int64_t to_string(char *buf, const int64_t buf_len) const
  {
    UNUSED(buf);
    UNUSED(buf_len);
    int64_t pos = 0;
    return pos;
  }
};
// TODO parameters to be adjusted
int ObTenantBase::init()
{
  int ret = OB_SUCCESS;

  if (inited_) {
    ret = OB_INIT_TWICE;
    LOG_WARN("init twice error", K(ret));
  } else {
    inited_ = true;
  }

  return ret;
}


void ObTenantBase::destroy()
{
  inited_ = false;
}

int ObTenantBase::pre_run()
{
  return OB_SUCCESS;
}

int ObTenantBase::end_run()
{
  return OB_SUCCESS;
}




int64_t ObTenantBase::get_max_session_num(const int64_t rl_max_session_num)
{
  int64_t max_session_num = 0;
  if (rl_max_session_num != 0) {
    max_session_num = rl_max_session_num;
  } else {
    /* As test, one session occupies 100K bytes*/
    max_session_num = max(100, (unit_memory_size_ * 5 / 100) / (100<<10));
  }
  return max_session_num;
}

void ObTenantEnv::set_tenant(ObTenantBase *ctx)
{
  // Single tenant: fall back to the dummy when no tenant is provided.
  // This allows unit tests to set a mock tenant for MTL() access.
  g_tenant_ptr = OB_NOT_NULL(ctx) ? ctx : &g_tenant_ctx;
}

ObTenantSwitchGuard::ObTenantSwitchGuard(ObTenantBase *ctx)
{
  // Single tenant: only one tenant exists, no switching needed.
  UNUSED(ctx);
  reset();
}

int ObTenantSwitchGuard::switch_to(ObTenantBase *ctx)
{
  // Single tenant: only one tenant exists, no switching needed.
  UNUSED(ctx);
  return OB_SUCCESS;
}

bool check_allow_switch(uint64_t src_tenant, uint64_t dest_tenant)
{
  bool allow = true;
  return allow;
}

int ObTenantSwitchGuard::switch_to(bool need_check_allow)
{
  UNUSED(need_check_allow);
  // Single tenant: g_tenant_ptr starts as &g_tenant_ctx (dummy, no MTL services).
  // It becomes the real ObTenant* after create_mtl_module() completes.
  // MTL_SWITCH calls switch_to() as a readiness guard: only proceed when the
  // real tenant is available.
  return (g_tenant_ptr != &g_tenant_ctx) ? OB_SUCCESS : OB_TENANT_NOT_IN_SERVER;
}

void ObTenantSwitchGuard::release()
{
  // Single tenant: no tenant switching state to restore.
  reset();
}

ObTenantBase g_tenant_ctx(0);

} // end of namespace share
} // end of namespace oceanbase
