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

#ifndef OB_ALL_VIRTUAL_TX_LOCK_STAT_H
#define OB_ALL_VIRTUAL_TX_LOCK_STAT_H

#include "observer/virtual_table/ob_virtual_table_scanner_iterator.h"
#include "sql/ob_scanner.h"
#include "common/row/ob_row.h"
#include "lib/container/ob_se_array.h"
#include "storage/tx/ob_trans_ctx_mgr.h"
#include "lib/time/ob_clock_generator.h"

namespace oceanbase
{
namespace storage
{
class ObLS;
}
namespace transaction
{
class ObTransService;
class ObTxLockStat;
}

namespace observer
{
class ObGVTxLockStat : public common::ObVirtualTableScannerIterator
{
public:
  ObGVTxLockStat();
  ~ObGVTxLockStat();
public:
  int inner_get_next_row(common::ObNewRow *&row) override;
  void reset() override;
private:
  int prepare_start_to_read_();
  int get_next_tx_lock_stat_iter_(transaction::ObTxLockStatIterator &tx_lock_stat_iter);
  int get_next_tx_lock_stat_(transaction::ObTxLockStat &tx_lock_stat);
  static const int64_t OB_MEMTABLE_KEY_BUFFER_SIZE = 128;
  char memtable_key_buffer_[OB_MEMTABLE_KEY_BUFFER_SIZE];
private:
  storage::ObLS *ls_;
  transaction::ObLSTxCtxIterator tx_ctx_iter_;
  transaction::ObTxLockStatIterator tx_lock_stat_iter_;
};
}//observer
}//oceanbase

#endif /* OB_ALL_VIRTUAL_TX_LOCK_STAT_H */
