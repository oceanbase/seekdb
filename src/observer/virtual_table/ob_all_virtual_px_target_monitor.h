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

#ifndef OCEANBASE_OBSERVER_VIRTUAL_TABLE_OB_ALL_VIRTUAL_PX_TARGET_MONITOR_H_
#define OCEANBASE_OBSERVER_VIRTUAL_TABLE_OB_ALL_VIRTUAL_PX_TARGET_MONITOR_H_


#include "observer/virtual_table/ob_virtual_table_scanner_iterator.h"
#include "sql/ob_scanner.h"
#include "common/row/ob_row.h"
#include "sql/engine/px/ob_px_target_monitor.h"

namespace oceanbase
{
namespace observer
{
class ObAllVirtualPxTargetMonitor: public common::ObVirtualTableScannerIterator
{
public:
  ObAllVirtualPxTargetMonitor();
  virtual ~ObAllVirtualPxTargetMonitor() {}
public:
  int init();
  virtual int inner_open();
  virtual int inner_get_next_row(common::ObNewRow *&row);
  virtual int inner_close();
private:
  enum TARGET_MONITOR_COLUMN
  {
    LOCAL_TARGET = common::OB_APP_MIN_COLUMN_ID,
    LOCAL_TARGET_USED,
    LOCAL_PARALLEL_SESSION_COUNT
  };
  bool row_emitted_;
}; //class ObAllVirtualPxTargetMonitor
}//namespace observer
}//namespace oceanbase
#endif //OCEANBASE_OBSERVER_VIRTUAL_TABLE_OB_ALL_VIRTUAL_PX_TARGET_MONITOR_H_
