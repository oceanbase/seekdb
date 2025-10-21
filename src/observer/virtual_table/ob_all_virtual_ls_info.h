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

#ifndef OB_ALL_VIRTUAL_OB_LS_INFO_H_
#define OB_ALL_VIRTUAL_OB_LS_INFO_H_

#include "share/ob_virtual_table_scanner_iterator.h"
#include "storage/tx_storage/ob_ls_map.h"
#include "observer/omt/ob_multi_tenant_operator.h"

namespace oceanbase
{
namespace observer
{

class ObAllVirtualLSInfo : public common::ObVirtualTableScannerIterator,
                           public omt::ObMultiTenantOperator
{
public:
  ObAllVirtualLSInfo();
  virtual ~ObAllVirtualLSInfo();
public:
  virtual int inner_get_next_row(common::ObNewRow *&row);
  virtual void reset();
  inline void set_addr(common::ObAddr &addr)
  {
    addr_ = addr;
  }
private:
  // Filter to get the tenants that need processing
  virtual bool is_need_process(uint64_t tenant_id) override;
  // Process the tenant of the current iteration
  virtual int process_curr_tenant(common::ObNewRow *&row) override;
  // Release the resources of the previous tenant
  virtual void release_last_tenant() override;
private:
  int next_ls_info_(ObLSVTInfo &ls_info);
private:
  common::ObAddr addr_;
  char ip_buf_[common::OB_IP_STR_BUFF];
  char state_name_[common::MAX_LS_STATE_LENGTH];
  /* The resources for cross-tenant access must be handled and released by ObMultiTenantOperator */
  int64_t ls_id_;
  ObSharedGuard<storage::ObLSIterator> ls_iter_guard_;
private:
  DISALLOW_COPY_AND_ASSIGN(ObAllVirtualLSInfo);
};

} // observer
} // oceanbase
#endif
