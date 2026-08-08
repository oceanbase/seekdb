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

#ifndef OCEANBASE_OBSERVER_VIRTUAL_TABLE_ALL_DTL_INTERM_RESULT_MONITOR_
#define OCEANBASE_OBSERVER_VIRTUAL_TABLE_ALL_DTL_INTERM_RESULT_MONITOR_

#include "observer/virtual_table/ob_virtual_table_scanner_iterator.h"
#include "lib/net/ob_addr.h"
#include "sql/dtl/ob_dtl_interm_result_manager.h"
namespace oceanbase
{
namespace observer
{

class ObDTLIntermResultMonitorInfoGetter
    : public sql::dtl::ObIDTLIntermResultConsumer
{
public:
  ObDTLIntermResultMonitorInfoGetter(common::ObScanner &scanner,
                                     common::ObIAllocator &allocator,
                                     common::ObIArray<uint64_t> &output_column_ids,
                                     common::ObNewRow &cur_row)
    : scanner_(scanner),
      allocator_(allocator),
      output_column_ids_(output_column_ids),
      cur_row_(cur_row)
  {}
  virtual ~ObDTLIntermResultMonitorInfoGetter() = default;
  int consume(const sql::dtl::ObDTLIntermResultKey &key,
              const sql::dtl::ObDTLIntermResultInfo &info) override;
public:
  
  DISALLOW_COPY_AND_ASSIGN(ObDTLIntermResultMonitorInfoGetter);
private:
  common::ObScanner &scanner_;
  common::ObIAllocator &allocator_;
  common::ObIArray<uint64_t> &output_column_ids_;
  common::ObNewRow &cur_row_;
  
};

class ObAllDtlIntermResultMonitor : public common::ObVirtualTableScannerIterator
{
  friend ObDTLIntermResultMonitorInfoGetter;
public:
  ObAllDtlIntermResultMonitor();
  virtual ~ObAllDtlIntermResultMonitor();
  virtual void reset();
  virtual int inner_get_next_row(common::ObNewRow *&row) override;
private:
  enum INSPECT_COLUMN
  {
        TRACE_ID = common::OB_APP_MIN_COLUMN_ID,
    OWNER,
    START_TIME,
    EXPIRE_TIME,
    HOLD_MEMORY,
    DUMP_SIZE,
    DUMP_COST,
    DUMP_TIME,
    DUMP_FD,
    DUMP_DIR_ID,
    CHANNEL_ID,
    QC_ID,
    DFO_ID,
    SQC_ID,
    BATCH_ID,
    MAX_HOLD_MEM,
  };
private:
  int fill_scanner();
  DISALLOW_COPY_AND_ASSIGN(ObAllDtlIntermResultMonitor);
};


} // namespace observer
} // namespace oceanbase
#endif // OCEANBASE_OBSERVER_VIRTUAL_TABLE_ALL_DTL_INTERM_RESULT_MONITOR_
