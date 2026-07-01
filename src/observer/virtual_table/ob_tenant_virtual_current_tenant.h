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

#ifndef OCEANBASE_OBSERVER_VIRTUAL_TABLE_OB_TENANT_VIRTUAL_CURRENT_TENANT_
#define OCEANBASE_OBSERVER_VIRTUAL_TABLE_OB_TENANT_VIRTUAL_CURRENT_TENANT_
#include "share/ob_virtual_table_scanner_iterator.h"
#include "common/ob_range.h"
namespace oceanbase
{
namespace sql
{
class ObSQLSessionInfo;
}
namespace common
{
class ObMySQLProxy;
class ObNewRow;
}
namespace observer
{
class ObTenantVirtualCurrentTenant : public common::ObVirtualTableScannerIterator
{
public:
  ObTenantVirtualCurrentTenant();
  virtual ~ObTenantVirtualCurrentTenant();
  virtual int inner_get_next_row(common::ObNewRow *&row);
  virtual void reset();
  inline void set_sql_proxy(common::ObMySQLProxy *sql_proxy) { sql_proxy_ = sql_proxy; }
private:
  common::ObMySQLProxy *sql_proxy_;
  DISALLOW_COPY_AND_ASSIGN(ObTenantVirtualCurrentTenant);
};
}
}
#endif /* OCEANBASE_OBSERVER_VIRTUAL_TABLE_TENANT_VIRTUAL_CURRENT_TENANT_ */
