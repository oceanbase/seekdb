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

#ifndef OCEANBASE_OBSERVER_VIRTUAL_TABLE_OB_TENANT_SHOW_CATALOG_DATABASES_
#define OCEANBASE_OBSERVER_VIRTUAL_TABLE_OB_TENANT_SHOW_CATALOG_DATABASES_

#include "share/ob_virtual_table_scanner_iterator.h"

namespace oceanbase
{
namespace observer
{
class ObTenantShowCatalogDatabases final : public common::ObVirtualTableScannerIterator
{
public:
  ObTenantShowCatalogDatabases() = default;
  virtual ~ObTenantShowCatalogDatabases() override = default;
  virtual int inner_open() override;
  virtual int inner_get_next_row(common::ObNewRow *&row) override;
  virtual void reset() override;
  void set_tenant_id(const uint64_t tenant_id) { tenant_id_ = tenant_id; }

private:
  enum class ALL_COLUMNS
  {
    CATALOG_ID = common::OB_APP_MIN_COLUMN_ID,
    DATABASE_NAME,
  };
  int fill_scanner();
  uint64_t tenant_id_ = OB_INVALID_ID;
  uint64_t catalog_id_ = OB_INVALID_ID;
  DISALLOW_COPY_AND_ASSIGN(ObTenantShowCatalogDatabases);
};
} // namespace observer
} // namespace oceanbase

#endif /* OCEANBASE_OBSERVER_VIRTUAL_TABLE_OB_TENANT_SHOW_CATALOG_DATABASES_ */
