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

#ifndef OB_ALL_VIRTUAL_TX_STAT_H_
#define OB_ALL_VIRTUAL_TX_STAT_H_

#include "observer/virtual_table/ob_virtual_table_scanner_iterator.h"
#include "sql/ob_scanner.h"
#include "common/row/ob_row.h"
#include "lib/container/ob_se_array.h"
#include "common/ob_simple_iterator.h"
#include "storage/tx/ob_trans_ctx.h"
#include "storage/tx/ob_trans_ctx_mgr_v4.h"
#include "lib/time/ob_clock_generator.h"
#include "storage/tx/ob_tx_stat.h"

namespace oceanbase
{
namespace memtable
{
class ObMemtable;
}
namespace transaction
{
class ObTransService;
class ObTransID;
class ObStartTransParam;
class ObTxStat;
}
namespace observer
{
class ObGVTxStat: public common::ObVirtualTableScannerIterator
{
public:
  ObGVTxStat();
  ~ObGVTxStat() override = default;
public:
  int inner_get_next_row(common::ObNewRow *&row) override;
  void reset() override;
private:
  int prepare_start_to_read_();
  bool is_valid_timestamp_(const int64_t timestamp) const;
private:
  enum
  {
    TX_ID = OB_APP_MIN_COLUMN_ID,
    SESSION_ID,
    IS_DECIDED,
    WRITE_STATE,
    TX_CTX_CREATE_TIME,
    TX_EXPIRED_TIME,
    REF_CNT,
    LAST_OP_SN,
    PENDING_WRITE,
    STATE,
    PART_TX_ACTION,
    TX_CTX_ADDR,
    PENDING_LOG_SIZE,
    FLUSHED_LOG_SIZE,
    IS_EXITING,
    LAST_REQUEST_TS,
    GTRID,
    BQUAL,
    FORMAT_ID,
    START_SCN,
    END_SCN,
    REC_SCN,
    BUSY_CBS_CNT,
    REPLAY_COMPLETE,
    SERIAL_LOG_FINAL_SCN,
    CALLBACK_LIST_STATS
  };

  static const int64_t CTX_ADDR_BUFFER_SIZE = 20;
  char ctx_addr_buffer_[CTX_ADDR_BUFFER_SIZE];
private:
  transaction::ObTxStatIterator tx_stat_iter_;
  transaction::ObXATransID xid_;
  ObCStringHelper cstring_helper_;
private:
  DISALLOW_COPY_AND_ASSIGN(ObGVTxStat);
};

}
}
#endif /* OB_ALL_VIRTUAL_TRANS_STAT_H */
