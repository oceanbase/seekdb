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
#include "share/resource_manager/ob_cgroup_ctrl.h"
#include "share/schema/ob_schema_struct.h"
#include "lib/resource/ob_affinity_ctrl.h"
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
  MAKE_TENANT_SWITCH_SCOPE_GUARD(guard);
  
  fn(ret);
}

void __attribute__((used)) lib_mtl_switch(lib::IRunWrapper *run_wrapper, std::function<void()> fn)
{
  int ret = OB_SUCCESS;
  MAKE_TENANT_SWITCH_SCOPE_GUARD(guard);
  if (OB_FAIL(guard.switch_to(static_cast<share::ObTenantBase *>(run_wrapper)))) {
    LOG_WARN("failed to switch to tenant", K(ret), KP(run_wrapper));
  } else {
    fn();
  }
}

int64_t __attribute__((used)) lib_mtl_cpu_count()
{
  return share::ObTenantEnv::get_tenant()->unit_max_cpu();
}


}
namespace share
{
using namespace oceanbase::common;


ObTenantBase::ObTenantBase(const int64_t epoch, bool enable_tenant_ctx_check)
    : epoch_(epoch),
    inited_(false),
    created_(false),
    mtl_init_ctx_(nullptr),
    tenant_role_value_(share::ObTenantRole::Role::INVALID_TENANT),
    unit_max_cpu_(0),
    unit_min_cpu_(0),
    unit_memory_size_(0),
    switchover_epoch_(0),
    cgroups_(nullptr),
    enable_tenant_ctx_check_(enable_tenant_ctx_check),
    thread_count_(0),
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
int ObTenantBase::init(ObCgroupCtrl *cgroup)
{
  int ret = OB_SUCCESS;

  ObMemAttr attr("DynamicFactor");
  if (inited_) {
    ret = OB_INIT_TWICE;
    LOG_WARN("init twice error", K(ret));
  } else if (OB_FAIL(tg_set_.create(1024))) {
    LOG_WARN("fail to create tg set", K(ret));
  } else if (OB_FAIL(thread_dynamic_factor_map_.create(1024, attr))) {
    LOG_WARN("fail to create thread dynamic_factor_map", K(ret));
  } else {
    if (cgroup == nullptr) {
      LOG_WARN("ObTenantBase init cgroup is null");
    } else {
      cgroups_ = cgroup;
    }
    inited_ = true;
  }

  return ret;
}


void ObTenantBase::destroy()
{
  if (tg_set_.size() > 0) {
    TGSetDumpFunc tg_set_dump_func;
    tg_set_.foreach_refactored(tg_set_dump_func);
    _OB_LOG_RET(ERROR, OB_ERR_UNEXPECTED,
                "tg thread not execute tg_destory make tg_id leak, tg_size=%ld, tg_set=[%s]",
                tg_set_.size(), tg_set_dump_func.buf_);
  }
  tg_set_.destroy();
  thread_dynamic_factor_map_.destroy();
  OB_ASSERT(thread_list_.get_size() == 0);
  inited_ = false;
}



ObCgroupCtrl *ObTenantBase::get_cgroup()
{
  ObCgroupCtrl *cgroup_ctrl = nullptr;
  cgroup_ctrl = cgroups_;
  return cgroup_ctrl;
}

int ObTenantBase::pre_run()
{
  int ret = OB_SUCCESS;
  ObTenantEnv::set_tenant(this);
  {
    ThreadListNode *node = lib::Thread::current().get_thread_list_node();
    lib::ObMutexGuard guard(thread_list_lock_);
    if (!thread_list_.add_last(node)) {
      ret = OB_ERR_UNEXPECTED;
      LOG_ERROR("add to thread list fail", K(ret));
    }
  }
  ATOMIC_INC(&thread_count_);
  if (GCONF._enable_numa_aware && OB_NUMA_SHARED_INDEX == AFFINITY_CTRL.get_tls_node()) {
    AFFINITY_CTRL.thread_bind_to_node(thread_count_);
  }
  // register in tenant cgroup without modifying group_id
  ObCgroupCtrl *cgroup_ctrl = get_cgroup();
  if (OB_NOT_NULL(cgroup_ctrl) && cgroup_ctrl->is_valid()) {
    // add thread to tenant OBCG_DEFAULT cgroup
    ret = cgroup_ctrl->add_self_to_cgroup_();
  }

  LOG_INFO("tenant thread pre_run", K(ret), K(thread_count_), K(GET_GROUP_ID()));
  return ret;
}

int ObTenantBase::end_run()
{
  int ret = OB_SUCCESS;
  {
    ThreadListNode *node = lib::Thread::current().get_thread_list_node();
    lib::ObMutexGuard guard(thread_list_lock_);
    thread_list_.remove(node);
  }
  ATOMIC_DEC(&thread_count_);
  LOG_INFO("tenant thread end_run", K(ret), K(thread_count_), K(GET_GROUP_ID()));
  return ret;
}

void ObTenantBase::tg_create_cb(int tg_id)
{
  tg_set_.set_refactored(tg_id);
}

void ObTenantBase::tg_destroy_cb(int tg_id)
{
  tg_set_.erase_refactored(tg_id);
}

int ObTenantBase::register_module_thread_dynamic(double dynamic_factor, int tg_id)
{
  int ret = OB_SUCCESS;
  if (dynamic_factor <= 0) {
    ret = OB_INVALID_ARGUMENT;
  } else {
    ThreadDynamicNode node(tg_id);
    ret = thread_dynamic_factor_map_.set_refactored(node, dynamic_factor);
  }
  return ret;
}

int ObTenantBase::unregister_module_thread_dynamic(int tg_id)
{
  ThreadDynamicNode node(tg_id);
  return thread_dynamic_factor_map_.erase_refactored(node);
}

int ObTenantBase::register_module_thread_dynamic(double dynamic_factor, lib::Threads *th)
{
  int ret = OB_SUCCESS;
  if (dynamic_factor <= 0 || th == nullptr) {
    ret = OB_INVALID_ARGUMENT;
  } else {
    ThreadDynamicNode node(th);
    ret = thread_dynamic_factor_map_.set_refactored(node, dynamic_factor);
  }
  return ret;
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

int ObTenantBase::update_thread_cnt(double tenant_unit_cpu)
{
  int64_t old_thread_count = ATOMIC_LOAD(&thread_count_);
  int ret = OB_SUCCESS;
  if (tenant_unit_cpu <= 0) {
    ret = OB_INVALID_ARGUMENT;
    LOG_WARN("update_thread_cnt", K(tenant_unit_cpu), K(id()), K(ret));
  }
  if (OB_SUCC(ret)) {
    for (ThreadDynamicFactorMap::iterator it = thread_dynamic_factor_map_.begin(); it != thread_dynamic_factor_map_.end(); it++) {
      int cnt = it->second * tenant_unit_cpu;
      if (cnt < 1) {
        cnt = 1;
      }
      int tmp_ret = OB_SUCCESS;
      if (it->first.get_type() == ThreadDynamicNode::TG) {
        tmp_ret = TG_SET_THREAD_CNT(it->first.get_tg_id(), cnt);
      } else if (it->first.get_type() == ThreadDynamicNode::USER_THREAD) {
        tmp_ret = it->first.get_user_thread()->do_set_thread_count(cnt);
      } else if (it->first.get_type() == ThreadDynamicNode::DYNAMIC_IMPL) {
        tmp_ret = it->first.get_dynamic_impl()->set_thread_cnt(cnt);
      }
      if (tmp_ret != OB_SUCCESS) {
        LOG_WARN("update_thread_cnt", K(it->first), K(cnt), K(tmp_ret), K(it->second));
      }
    }
  }
  int64_t new_thread_count = ATOMIC_LOAD(&thread_count_);
  LOG_INFO("update_thread_cnt", K(tenant_unit_cpu), K(old_thread_count), K(new_thread_count));
  return ret;
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
