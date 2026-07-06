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
 
#ifndef OB_ALL_VIRTUAL_TX_SCHEDULER_STAT_H_
#define OB_ALL_VIRTUAL_TX_SCHEDULER_STAT_H_

#include "observer/virtual_table/ob_virtual_table_scanner_iterator.h"
#include "storage/tx/ob_trans_define_v4.h"
#include "storage/tx/ob_tx_stat.h"
#include "lib/time/ob_clock_generator.h"
#include "lib/time/ob_clock_generator.h"

namespace oceanbase
{
namespace transaction
{
  class ObTxSchedulerStat;
}
namespace observer
{

class ObGVTxSchedulerStat: public common::ObVirtualTableScannerIterator
{
public:
  ObGVTxSchedulerStat();
  ~ObGVTxSchedulerStat();

public:
  virtual int inner_get_next_row(common::ObNewRow *&row);
  virtual void reset();

private:
  int get_next_tx_info_(transaction::ObTxSchedulerStat &tx_scheduler_stat);
  bool is_valid_timestamp_(const int64_t timestamp) const;

private:
  enum
  {
    SESSION_ID = common::OB_APP_MIN_COLUMN_ID,
    TX_ID,
    STATE,
    CLUSTER_ID,
    COORDINATOR,
    PARTICIPANTS,
    ISOLATION_LEVEL,
    SNAPSHOT_VERSION,
    ACCESS_MODE,
    TX_OP_SN,
    FLAG,
    ACTIVE_TS,
    EXPIRE_TS,
    TIMEOUT_US,
    REF_CNT,
    TX_DESC_ADDR,
    SAVEPOINTS,
    SAVEPOINTS_TOTAL_CNT,
    INTERNAL_ABORT_CAUSE,
    CAN_EARLY_LOCK_RELEASE,
    GTRID,
    BQUAL,
    FORMAT_ID
  };
  static const int64_t OB_MAX_BUFFER_SIZE = 1024;
  char parts_buffer_[OB_MAX_BUFFER_SIZE];
  char tx_desc_addr_buffer_[20];
  char savepoints_buffer_[OB_MAX_BUFFER_SIZE];
  transaction::ObXATransID xid_;
  transaction::ObTxSchedulerStatIterator tx_scheduler_stat_iter_;
  DISALLOW_COPY_AND_ASSIGN(ObGVTxSchedulerStat);
};

}
}
#endif /* OB_ALL_VIRTUAL_TX_SCHEDULER_STAT_H_ */
