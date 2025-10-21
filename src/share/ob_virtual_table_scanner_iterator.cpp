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

#include "share/ob_virtual_table_scanner_iterator.h"
using namespace oceanbase::common;
namespace oceanbase
{
namespace common
{

ObVirtualTableScannerIterator::ObVirtualTableScannerIterator()
    : ObVirtualTableIterator(),
      scanner_(),
      scanner_it_(),
      start_to_read_(false)
{
}

ObVirtualTableScannerIterator::~ObVirtualTableScannerIterator()
{
}

void ObVirtualTableScannerIterator::reset()
{
  scanner_it_.reset();
  scanner_.reset();
  start_to_read_ = false;
  ObVirtualTableIterator::reset();
}

}/* ns observer*/
}/* ns oceanbase */
