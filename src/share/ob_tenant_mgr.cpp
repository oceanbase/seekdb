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

#include <algorithm>
#include <new>

#include "share/ob_tenant_mgr.h"
#include "lib/alloc/alloc_func.h"
#include "lib/allocator/ob_malloc.h"
#include "lib/container/ob_iarray.h"
#include "lib/net/ob_addr.h"
#include "lib/ob_define.h"
#include "lib/oblog/ob_log_level.h"
#include "lib/oblog/ob_log_print_kv.h"
#include "lib/resource/ob_resource_mgr.h"
#include "lib/utility/ob_mod_define.h"
#include "rpc/ob_lock_wait_node.h"
#include "share/config/ob_server_config.h"
#include "share/config/ob_tenant_config_mgr.h"
#include "share/ob_errno.h"

namespace oceanbase
{

namespace obcall
{
using namespace oceanbase::common;
using namespace oceanbase::lib;
using namespace oceanbase::share;

} // namespace obcall

namespace common
{
using namespace oceanbase::obcall;

int64_t ObTenantCpuShare::calc_px_pool_share(int64_t min_cpu)
{
  int64_t share = 3;
  int ret = OB_SUCCESS;
  if (!true) {
    share = 3;
    COMMON_LOG(ERROR, "fail get tenant config. share default to 3", K(share));
  } else {
    share = std::max(static_cast<int64_t>(3), min_cpu * GCONF.px_workers_per_cpu_quota);
  }
  return share;
}

} // namespace common
} // namespace oceanbase
