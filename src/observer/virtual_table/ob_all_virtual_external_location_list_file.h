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

#ifndef OCEANBASE_OBSERVER_OB_ALL_VIRTUAL_EXTERNAL_LOCATION_LIST_FILE_
#define OCEANBASE_OBSERVER_OB_ALL_VIRTUAL_EXTERNAL_LOCATION_LIST_FILE_
#include "lib/container/ob_se_array.h"
#include "share/ob_virtual_table_scanner_iterator.h"
#include "common/ob_range.h"

namespace oceanbase
{
namespace common
{
class ObString;
}
namespace observer
{
class ObAllVirtualExternalLocationListFile : public common::ObVirtualTableScannerIterator
{
public:
  ObAllVirtualExternalLocationListFile();
  virtual ~ObAllVirtualExternalLocationListFile();
  virtual int inner_open();
  virtual int inner_get_next_row(common::ObNewRow *&row);
  virtual void reset();
private:
  int resolve_param(uint64_t &location_id, ObString &sub_path, ObString &pattern);
  int fill_row_cells(uint64_t location_id,
                     const ObString &sub_path,
                     const ObString &pattern,
                     const ObString &file_url,
                     int64_t file_size);
private:
  DISALLOW_COPY_AND_ASSIGN(ObAllVirtualExternalLocationListFile);
};
}
}
#endif /* OCEANBASE_OBSERVER_OB_ALL_VIRTUAL_EXTERNAL_LOCATION_LIST_FILE_ */
