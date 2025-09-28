/**
 * Copyright (c) 2021 OceanBase
 * OceanBase CE is licensed under Mulan PubL v2.
 * You can use this software according to the terms and conditions of the Mulan PubL v2.
 * You may obtain a copy of Mulan PubL v2 at:
 *          http://license.coscl.org.cn/MulanPubL-2.0
 * THIS SOFTWARE IS PROVIDED ON AN "AS IS" BASIS, WITHOUT WARRANTIES OF ANY KIND,
 * EITHER EXPRESS OR IMPLIED, INCLUDING BUT NOT LIMITED TO NON-INFRINGEMENT,
 * MERCHANTABILITY OR FIT FOR A PARTICULAR PURPOSE.
 * See the Mulan PubL v2 for more details.
 */

#define USING_LOG_PREFIX SHARE
#include "ob_resource_plan_manager.h"
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
    if (cpu >= 0 && OB_FAIL(GCTX.cgroup_ctrl_->set_cpu_shares(  // set cgroup/background/cpu.shares
                        OB_INVALID_TENANT_ID,
                        cpu,
                        OB_INVALID_GROUP_ID,
                        true /* is_background */))) {
      LOG_WARN("fail to set background cpu shares", K(ret));
    }
    int compare_ret = 0;
    if (OB_SUCC(ret) && OB_SUCC(GCTX.cgroup_ctrl_->compare_cpu(background_quota_, cpu, compare_ret))) {
      if (0 == compare_ret) {
        // do nothing
      } else if (OB_FAIL(GCTX.cgroup_ctrl_->set_cpu_cfs_quota(  // set cgroup/background/cpu.cfs_quota_us
                     OB_INVALID_TENANT_ID,
                     cpu,
                     OB_INVALID_GROUP_ID,
                     true /* is_background */))) {
        LOG_WARN("fail to set background cpu cfs quota", K(ret));
      } else {
        if (compare_ret < 0) {
          const int64_t phy_cpu_cnt = sysconf(_SC_NPROCESSORS_ONLN);
          int tmp_ret = OB_SUCCESS;
          omt::TenantIdList ids;
          GCTX.omt_->get_tenant_ids(ids);
          for (uint64_t i = 0; i < ids.size(); i++) {
            uint64_t tenant_id = ids[i];
            double target_cpu = -1;
            if (OB_DTL_TENANT_ID == tenant_id) {
              target_cpu = (phy_cpu_cnt <= 4) ? 1.0 : OB_DTL_CPU;
            } else if (OB_DATA_TENANT_ID == tenant_id) {
              target_cpu = (phy_cpu_cnt <= 4) ? 1.0 : OB_DATA_CPU;
            } else if (!is_virtual_tenant_id(tenant_id)) {
              MTL_SWITCH(tenant_id)
              {
                target_cpu = MTL_CTX()->unit_max_cpu();
              }
            }
            if (OB_TMP_FAIL(GCTX.cgroup_ctrl_->compare_cpu(target_cpu, cpu, compare_ret))) {
              LOG_WARN_RET(tmp_ret, "compare tenant cpu failed", K(tmp_ret), K(tenant_id));
            } else if (compare_ret > 0) {
              target_cpu = cpu;
            }
            if (OB_TMP_FAIL(GCTX.cgroup_ctrl_->set_cpu_cfs_quota(
                    tenant_id, target_cpu, OB_INVALID_GROUP_ID, true /* is_background */))) {
              LOG_WARN_RET(tmp_ret, "set tenant cpu cfs quota failed", K(tmp_ret), K(tenant_id));
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