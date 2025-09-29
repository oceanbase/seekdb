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
