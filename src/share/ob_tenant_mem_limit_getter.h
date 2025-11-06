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

#ifndef OCEABASE_SHARE_OB_TENANT_MEM_LIMIT_GETTER_H_
#define OCEABASE_SHARE_OB_TENANT_MEM_LIMIT_GETTER_H_
#include "share/ob_i_tenant_mem_limit_getter.h"

namespace oceanbase
{
namespace common
{

// just used by KVGlobalCache
class ObTenantMemLimitGetter : public ObITenantMemLimitGetter
{
public:
  static ObTenantMemLimitGetter &get_instance();
  // check if the tenant exist.
  // virtual tenant will check ObVirtualTenantManager.
  // mtl tenant will check ObMultiTenant.
  // @param[in] tenant_id, which tenant is need check.
  bool has_tenant(const uint64_t tenant_id) const override;
  // get all the tenant id, include both virtual tenant and mtl tenant.
  // get virtual tenant id from ObVirtualTenantManager.
  // get mtl tenant id from ObMultiTenant.
  // @param[out] tenant_ids, all the tenant id.
  int get_all_tenant_id(ObIArray<uint64_t> &tenant_ids) const override;
  // get the min/max memory limit of a tenant.
  // virtual tenant's result comes from ObVirtualTenantManager.
  // mtl tenant's result comes from ObTenantFreezer.
  // @param[in] tenant_id, which tenant's memory limit need get.
  // @param[out] lower_limit, the min tenant memory limit.
  // @param[out] upper_limit, the max tenant memory limit.
  int get_tenant_mem_limit(const uint64_t tenant_id,
                           int64_t &lower_limit,
                           int64_t &upper_limit) const override;
private:
  ObTenantMemLimitGetter() {}
  virtual ~ObTenantMemLimitGetter() {}
};


}
}
#endif
