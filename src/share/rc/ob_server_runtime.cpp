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
#include <functional>
#include "ob_server_runtime.h"
#include "share/rc/ob_module_provider.h"
#include "share/roaringbitmap/ob_rb_memory_mgr.h"

namespace oceanbase
{

namespace common
{
uint64_t server_runtime_id()
{
  return 1;
}
}

namespace common
{


ObRbMemMgr *__attribute__((used)) get_rb_mem_mgr()
{
  return ::oceanbase::share::g_mp->rb_mem_mgr();
}

void __attribute__((used)) lib_server_runtime_dispatch(lib::IRunWrapper *run_wrapper,
                                                       std::function<void()> fn)
{
  UNUSED(run_wrapper);
  fn();
}

}
namespace share
{
using namespace oceanbase::common;


ObServerRuntimeState::ObServerRuntimeState()
    : inited_(false),
    module_init_ctx_(nullptr),
    role_(share::ObServerRole::Role::INVALID_ROLE),
    max_cpu_(0),
    min_cpu_(0),
    memory_size_(0),
    switchover_epoch_(0)
{
}

void ObServerRuntimeState::set_role(const share::ObServerRole::Role role)
{
  const share::ObServerRole::Role old_role = this->role();
  if (old_role != role) {
    SHARE_LOG(INFO, "set server role", K(old_role), K(role));
    (void)ATOMIC_STORE(&role_, role);
  }
}

void ObServerRuntimeState::set_switchover_epoch(const int64_t switchover_epoch)
{
  int64_t cached_epoch = this->switchover_epoch();
  if (OB_INVALID_VERSION != switchover_epoch && cached_epoch < switchover_epoch) {
    SHARE_LOG(INFO, "set server switchover epoch", K(switchover_epoch), K(cached_epoch));
    ATOMIC_BCAS(&switchover_epoch_, cached_epoch, switchover_epoch);
  }
}

int ObServerRuntimeState::init()
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


void ObServerRuntimeState::destroy()
{
  inited_ = false;
}

ObServerRuntimeState g_bootstrap_server_runtime;

} // end of namespace share
} // end of namespace oceanbase
