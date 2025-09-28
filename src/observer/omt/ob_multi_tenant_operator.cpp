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

#define USING_LOG_PREFIX SERVER_OMT
#include "observer/omt/ob_multi_tenant_operator.h"
#include "src/share/ob_server_struct.h"
#include "observer/omt/ob_tenant.h"

namespace oceanbase
{
namespace omt
{

ObMultiTenantOperator::ObMultiTenantOperator() :
  inited_(false),
  tenant_(nullptr)
{}

ObMultiTenantOperator::~ObMultiTenantOperator()
{
  reset();
}

void ObMultiTenantOperator::reset()
{
  if (inited_) {
    // 如果迭代未结束，释放残留的资源
    int ret = OB_SUCCESS;
    if (tenant_ != nullptr) {
      {
        share::ObTenantSwitchGuard guard(tenant_);
        release_last_tenant();
      }
      tenant_->unlock();
    }
    if (OB_FAIL(ret)) {
      // 无法处理资源释放不了的问题
      abort();
    } else {
      tenant_ = nullptr;
      inited_ = false;
    }
  }
}

int ObMultiTenantOperator::init()
{
  int ret = OB_SUCCESS;
  if (inited_) {
    ret = OB_INIT_TWICE;
    LOG_ERROR("operator init", K(ret));
  } else {
    inited_ = true;
  }
  return ret;
}

int ObMultiTenantOperator::execute(common::ObNewRow *&row)
{
  int ret = OB_SUCCESS;
  if (!inited_) {
    ret = init();
  }
  if (OB_SUCC(ret)) {
    uint64_t tenant_id = OB_SYS_TENANT_ID;
    int process_ret = OB_SUCCESS;
    if (tenant_ == nullptr) {
      if (OB_FAIL(GCTX.omt_->get_active_tenant_with_tenant_lock(tenant_id, tenant_))) {
        LOG_WARN("get_tenant_with_tenant_lock", K(ret), K(tenant_id));
      }
    }
    if (OB_SUCC(ret)) {
      share::ObTenantSwitchGuard guard(tenant_);
      process_ret = process_curr_tenant(row);
    }
    if (OB_SUCC(ret)) {
      if (process_ret == OB_SUCCESS) {
        // succ do nothing
      } else if (process_ret == OB_ITER_END) {
        {
          // release last tenant obj
          share::ObTenantSwitchGuard guard(tenant_);
          release_last_tenant();
        }
        tenant_->unlock();
        tenant_ = nullptr;
        ret = OB_ITER_END;
      } else {
        ret = process_ret;
        LOG_WARN("operator process", K(ret), K(lbt()));
      }
    } else if (ret == OB_TENANT_NOT_IN_SERVER) {
      ret = OB_ITER_END;
    }
  }
  return ret;
}

} // end omt
} // end oceanbase
