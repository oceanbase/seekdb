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

#ifndef OB_ALL_VIRTUAL_TIMESTAMP_SERVICE_H_
#define OB_ALL_VIRTUAL_TIMESTAMP_SERVICE_H_

#include "observer/virtual_table/ob_virtual_table_scanner_iterator.h"

namespace oceanbase
{
namespace observer
{
class ObAllVirtualTimestampService: public common::ObVirtualTableScannerIterator
{
public:
  explicit ObAllVirtualTimestampService() { reset(); }
  virtual ~ObAllVirtualTimestampService() = default;
public:
  void reset() override;
  int inner_get_next_row(common::ObNewRow *&row) override;
  TO_STRING_KV(K_(ts_value));
private:
  int64_t ts_value_;
private:
  DISALLOW_COPY_AND_ASSIGN(ObAllVirtualTimestampService);
};
} // observer
} // oceanbase
#endif // OB_ALL_VIRTUAL_TIMESTAMP_SERVICE_H_
