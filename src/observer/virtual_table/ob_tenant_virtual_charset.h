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

#ifndef OCEANBASE_OBSERVER_VIRTUAL_TABLE_OB_TENANT_VIRTUAL_CHARSET_
#define OCEANBASE_OBSERVER_VIRTUAL_TABLE_OB_TENANT_VIRTUAL_CHARSET_
#include "share/ob_virtual_table_scanner_iterator.h"
namespace oceanbase
{
namespace observer
{
class ObTenantVirtualCharset:public common::ObVirtualTableScannerIterator
{
public:
  ObTenantVirtualCharset()
  {
  }
  ~ObTenantVirtualCharset()
  {
    reset();
  }
  virtual void reset() override
  {
    ObVirtualTableScannerIterator::reset();
  }
  virtual int inner_get_next_row(common::ObNewRow *&row) override;
private:
  ObTenantVirtualCharset(const ObTenantVirtualCharset &other)=delete;
  ObTenantVirtualCharset &operator=(const ObTenantVirtualCharset &other)=delete;
  enum CHARSET_COLUMN
  {
    CHARSET = common::OB_APP_MIN_COLUMN_ID,
    DESCRIPTION,
    DEFAULT_COLLATION,
    MAX_LENGTH
  };
  int fill_scanner();
};
}
}
#endif
