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

#ifndef OCEANBASE_OBSERVER_VIRTUAL_TABLE_ALL_DATA_TYPE_TABLE_
#define OCEANBASE_OBSERVER_VIRTUAL_TABLE_ALL_DATA_TYPE_TABLE_

#include "share/ob_virtual_table_scanner_iterator.h"

namespace oceanbase
{
namespace observer
{
class ObAllDataTypeTable : public common::ObVirtualTableScannerIterator
{
  static const int32_t DATA_TYPE_COLUMN_COUNT = 3;
  enum COLUMN_NAME {
    DATA_TYPE = common::OB_APP_MIN_COLUMN_ID,
    DATA_TYPE_STR,
    DATA_TYPE_CLASS,
  };

public:
  ObAllDataTypeTable();
  virtual ~ObAllDataTypeTable();

  virtual int inner_get_next_row(common::ObNewRow *&row);

private:
  DISALLOW_COPY_AND_ASSIGN(ObAllDataTypeTable);
};
} // namespace observer
} // namespace oceanbase
#endif // OCEANBASE_OBSERVER_VIRTUAL_TABLE_ALL_DATA_TYPE_TABLE_
