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

#ifndef OCEANBASE_OBSERVER_OB_ALL_VIRTUAL_LS_ARCHIVE_STAT_H_
#define OCEANBASE_OBSERVER_OB_ALL_VIRTUAL_LS_ARCHIVE_STAT_H_
#include "observer/omt/ob_multi_tenant.h"
#include "share/ob_virtual_table_scanner_iterator.h"
#include "share/ob_scanner.h"
#include "common/row/ob_row.h"

namespace oceanbase
{
namespace archive
{
struct LSArchiveStat;
}
namespace observer
{
class ObAllVirtualLSArchiveStat : public common::ObVirtualTableScannerIterator
{
public:
  explicit ObAllVirtualLSArchiveStat(omt::ObMultiTenant *omt) {}
public:
  virtual int inner_get_next_row(common::ObNewRow *&row);
};
} // namespace observer
} // namespace oceanbase
#endif /* OCEANBASE_OBSERVER_OB_ALL_VIRTUAL_LS_ARCHIVE_STAT_H_ */
