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
#include "observer/ob_server_struct.h"


using namespace oceanbase::common;
using namespace oceanbase::share;

int ObResourcePlanManager::init()
{
  return OB_SUCCESS;
}

int ObResourcePlanManager::refresh_global_background_cpu()
{
  int ret = OB_SUCCESS;
  if (GCONF.enable_global_background_resource_isolation && GCTX.cgroup_ctrl_->is_valid()) {
    double cpu = static_cast<double>(GCONF.global_background_cpu_quota);
    if (cpu <= 0) {
      cpu = -1;
    }
    if (cpu >= 0 && OB_FAIL(GCTX.cgroup_ctrl_->set_cpu_shares(cpu,
                        OB_INVALID_GROUP_ID,
                        true /* is_background */))) {
      LOG_WARN("fail to set background cpu shares", K(ret));
    }
    int compare_ret = 0;
    if (OB_SUCC(ret) && OB_SUCC(GCTX.cgroup_ctrl_->compare_cpu(background_quota_, cpu, compare_ret))) {
      if (0 == compare_ret) {
        // do nothing
      } else if (OB_FAIL(GCTX.cgroup_ctrl_->set_cpu_cfs_quota(cpu,
                     OB_INVALID_GROUP_ID,
                     true /* is_background */))) {
      } else {
        if (compare_ret < 0) {
#ifdef _WIN32
          SYSTEM_INFO si;
          GetSystemInfo(&si);
          const int64_t phy_cpu_cnt = static_cast<int64_t>(si.dwNumberOfProcessors);
#else
          const int64_t phy_cpu_cnt = sysconf(_SC_NPROCESSORS_ONLN);
#endif
          int tmp_ret = OB_SUCCESS;
          {
            double target_cpu = -1;
            {
              target_cpu = (phy_cpu_cnt <= 4) ? 1.0 : OB_DTL_CPU;
            }
            if (OB_TMP_FAIL(GCTX.cgroup_ctrl_->compare_cpu(target_cpu, cpu, compare_ret))) {
              LOG_WARN_RET(tmp_ret, "compare tenant cpu failed", K(tmp_ret), K(1UL));
            } else if (compare_ret > 0) {
              target_cpu = cpu;
            }
            if (OB_TMP_FAIL(GCTX.cgroup_ctrl_->set_cpu_cfs_quota(target_cpu, OB_INVALID_GROUP_ID, true /* is_background */))) {
              LOG_WARN_RET(tmp_ret, "set tenant cpu cfs quota failed", K(tmp_ret), K(1UL));
            }
          }
        }
      }
      if (OB_SUCC(ret) && 0 != compare_ret) {
        background_quota_ = cpu;
      }
    }
  }
  return ret;
}

int64_t ObResourcePlanManager::to_string(char *buf, const int64_t len) const
{
  int ret = OB_SUCCESS;
  int64_t pos = 0;
  if (OB_SUCC(databuff_printf(buf, len, pos, "background_quota:%d", background_quota_))) {
  }
  return pos;
}
