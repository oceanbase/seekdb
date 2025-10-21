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

#ifndef OCEANBASE_OB_INFORMATION_SCHEMA_ENABLE_ROLES_
#define OCEANBASE_OB_INFORMATION_SCHEMA_ENABLE_ROLES_

#include "share/ob_virtual_table_scanner_iterator.h"

namespace oceanbase
{
namespace observer
{
class ObInfoSchemaEnableRolesTable : public common::ObVirtualTableScannerIterator
{
public:
  ObInfoSchemaEnableRolesTable();
  virtual ~ObInfoSchemaEnableRolesTable();

  virtual void reset();
  virtual int inner_get_next_row(common::ObNewRow *&row) override;

private:
  int prepare_scan();

  DISALLOW_COPY_AND_ASSIGN(ObInfoSchemaEnableRolesTable);
};
}
}
#endif /* OCEANBASE_OB_INFORMATION_SCHEMA_ENABLE_ROLES_ */
