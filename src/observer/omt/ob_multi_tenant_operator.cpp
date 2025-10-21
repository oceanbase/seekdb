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
    // If the iteration is not finished, release the remaining resources
    int ret = OB_SUCCESS;
    if (tenant_ != nullptr) {
      {
        share::ObTenantSwitchGuard guard(tenant_);
        release_last_tenant();
      }
      tenant_->unlock();
    }
    if (OB_FAIL(ret)) {
      // Unable to handle the issue of resource release failure
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
