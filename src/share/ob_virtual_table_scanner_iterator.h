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

#ifndef OCEANBASE_COMMON_OB_VIRTUAL_TABLE_SCANNER_ITERATOR_
#define OCEANBASE_COMMON_OB_VIRTUAL_TABLE_SCANNER_ITERATOR_

#include "lib/net/ob_addr.h"
#include "share/ob_virtual_table_iterator.h"
#include "share/ob_scanner.h"
#include "common/row/ob_row.h"

namespace oceanbase
{
namespace common
{
class ObVirtualTableScannerIterator : public common::ObVirtualTableIterator
{
public:
  ObVirtualTableScannerIterator();
  virtual ~ObVirtualTableScannerIterator();
  virtual void reset();
protected:
  common::ObScanner scanner_;
  common::ObScanner::Iterator scanner_it_;
  bool start_to_read_;
private:
  DISALLOW_COPY_AND_ASSIGN(ObVirtualTableScannerIterator);
};
}
}
#endif /* OCEANBASE_COMMON_OB_VIRTUAL_TABLE_SCANNER_ITERATOR_ */
