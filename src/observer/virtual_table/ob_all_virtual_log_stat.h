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

#ifndef OCEANBASE_OBSERVER_VIRTUAL_TABLE_OB_ALL_VIRTUAL_PALF_STAT_
#define OCEANBASE_OBSERVER_VIRTUAL_TABLE_OB_ALL_VIRTUAL_PALF_STAT_
#include "observer/virtual_table/ob_virtual_table_scanner_iterator.h"
#include "sql/ob_scanner.h"
#include "common/row/ob_row.h"
#include "logservice/palf/palf_handle.h"

namespace oceanbase
{
namespace observer
{
class ObAllVirtualPalfStat: public common::ObVirtualTableScannerIterator
{
public:
  ObAllVirtualPalfStat() = default;
  virtual ~ObAllVirtualPalfStat();
public:
  virtual int inner_get_next_row(common::ObNewRow *&row);
  void destroy();
private:
  int insert_log_stat_(const palf::PalfStat &palf_stat);
private:
  static const int64_t VARCHAR_32 = 32;
  char access_mode_str_[VARCHAR_32] = {'\0'};
};
}//namespace observer
}//namespace oceanbase
#endif
