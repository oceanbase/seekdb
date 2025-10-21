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

#ifndef _OCEABASE_OBSERVER_OMT_OB_MULTI_TENANT_OPERATOR_H_
#define _OCEABASE_OBSERVER_OMT_OB_MULTI_TENANT_OPERATOR_H_

#include "lib/container/ob_array.h"
#include "common/row/ob_row.h"

namespace oceanbase
{
namespace omt
{
class ObTenant;

class ObMultiTenantOperator
{
public:
  ObMultiTenantOperator();
  virtual ~ObMultiTenantOperator();

  int init();
  // Process the current tenant
  virtual int process_curr_tenant(common::ObNewRow *&row) = 0;
  // Release the resources of the previous tenant
  virtual void release_last_tenant() = 0;
  // Filter tenant
  virtual bool is_need_process(uint64_t tenant_id) { return true; }
  // Release resources, note that subclasses inheriting from ObMultiTenantOperator must first call ObMultiTenantOperator::reset() when destroyed
  // Tenant object release on subclasses maintained by ObMultiTenantOperator
  void reset();

  int execute(common::ObNewRow *&row);
private:
  bool inited_;
  ObTenant *tenant_;
};


} // end of namespace omt
} // end of namespace oceanbase


#endif /* _OCEABASE_OBSERVER_OMT_OB_MULTI_TENANT_OPERATOR_H_ */
