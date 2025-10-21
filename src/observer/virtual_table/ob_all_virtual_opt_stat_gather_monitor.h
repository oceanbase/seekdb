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

#ifndef OB_ALL_VIRTUAL_OPT_STAT_GATHER_STAT_H
#define OB_ALL_VIRTUAL_OPT_STAT_GATHER_STAT_H

#include "share/ob_virtual_table_scanner_iterator.h"
#include "lib/net/ob_addr.h"
#include "share/stat/ob_opt_stat_gather_stat.h"

namespace oceanbase
{

namespace observer
{

class ObAllVirtualOptStatGatherMonitor : public common::ObVirtualTableScannerIterator
{
public:
  ObAllVirtualOptStatGatherMonitor();
  virtual ~ObAllVirtualOptStatGatherMonitor();
  virtual void reset();
  virtual int inner_get_next_row(common::ObNewRow *&row);
  inline void set_addr(common::ObAddr &addr) { addr_ = &addr; }
  int set_ip();
private:
  common::ObAddr *addr_;
  bool start_to_read_;
  common::ObArray<ObOptStatGatherStat> stat_array_;
  int64_t index_;
  common::ObString ipstr_;
  int32_t port_;
  char svr_ip_[common::MAX_IP_ADDR_LENGTH + 2];
  enum COLUMNS
  {
    TENANT_ID = common::OB_APP_MIN_COLUMN_ID,
    SVR_IP,
    SVR_PORT,
    SESSION_ID,
    TRACE_ID,
    TASK_ID,
    TYPE,
    TASK_START_TIME,
    TASK_TABLE_COUNT,
    TASK_DURATION_TIME,
    COMPLETED_TABLE_COUNT,
    RUNNING_TABLE_OWNER,
    RUNNING_TABLE_NAME,
    RUNNING_TABLE_DURATION_TIME,
    SPARE1,
    SPARE2
  };
private:
  DISALLOW_COPY_AND_ASSIGN(ObAllVirtualOptStatGatherMonitor);
};

}// namespace observer
}// namespace oceanbase

#endif /* !OB_ALL_VIRTUAL_OPT_STAT_GATHER_MONITOR_H */
