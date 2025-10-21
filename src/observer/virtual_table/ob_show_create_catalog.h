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

#ifndef OCEANBASE_OBSERVER_OB_SHOW_CREATE_CATALOG_
#define OCEANBASE_OBSERVER_OB_SHOW_CREATE_CATALOG_
#include "common/ob_range.h"
#include "lib/container/ob_se_array.h"
#include "share/ob_virtual_table_scanner_iterator.h"
#include "src/share/catalog/ob_catalog_properties.h"

namespace oceanbase
{
namespace common
{
class ObString;
}
namespace observer
{
class ObShowCreateCatalog : public common::ObVirtualTableScannerIterator
{
public:
  ObShowCreateCatalog();
  virtual ~ObShowCreateCatalog();
  virtual int inner_get_next_row(common::ObNewRow *&row);
  virtual void reset();

private:
  int calc_show_catalog_id(uint64_t &show_catalog_id);
  int fill_row_cells(uint64_t show_catalog_id, const common::ObString &catalog_name);
  int print_catalog_definition(const uint64_t tenant_id,
                               const uint64_t catalog_id,
                               char *buf,
                               const int64_t &buf_len,
                               int64_t &pos) const;
  int print_odps_catalog_definition(const share::ObODPSCatalogProperties &odps,
                                    char *buf,
                                    const int64_t &buf_len,
                                    int64_t &pos) const;

private:
  DISALLOW_COPY_AND_ASSIGN(ObShowCreateCatalog);
};
} // namespace observer
} // namespace oceanbase
#endif /* OCEANBASE_OBSERVER_OB_SHOW_CREATE_CATALOG_ */
