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

#ifndef OCEANBASE_OBSERVER_VIRTUAL_TABLE_OB_TENANT_VIRTUAL_PRIVILEGE_
#define OCEANBASE_OBSERVER_VIRTUAL_TABLE_OB_TENANT_VIRTUAL_PRIVILEGE_
#include "share/ob_virtual_table_scanner_iterator.h"
namespace oceanbase
{
namespace observer
{
class ObTenantVirtualPrivilege : public ObVirtualTableScannerIterator
{
public:
  ObTenantVirtualPrivilege() {};
  virtual ~ObTenantVirtualPrivilege() { reset(); };
  virtual void reset() override
  {
    ObVirtualTableScannerIterator::reset();
  }
  virtual int inner_get_next_row(common::ObNewRow *&row) override;

private:
  ObTenantVirtualPrivilege(const ObTenantVirtualPrivilege &other) = delete;
  ObTenantVirtualPrivilege &operator=(const ObTenantVirtualPrivilege &other) = delete;
  enum PRIVILEGE_COLUMN
  {
    PRIVILEGE_COL =  common::OB_APP_MIN_COLUMN_ID,
    CONTEXT_COL,
    COMMENT_COL
  };
  int fill_scanner();
};
}
}
#endif
