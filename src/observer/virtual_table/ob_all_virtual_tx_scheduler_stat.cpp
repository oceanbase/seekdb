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

#include "observer/virtual_table/ob_all_virtual_tx_scheduler_stat.h"
#include "share/rc/ob_server_runtime.h"
#include "observer/ob_server.h"
#include "storage/tx/ob_trans_service.h"

using namespace oceanbase::common;
using namespace oceanbase::transaction;

namespace oceanbase
{
namespace observer
{

ObGVTxSchedulerStat::ObGVTxSchedulerStat()
    : ObVirtualTableScannerIterator(),
      tx_scheduler_stat_iter_()
{
}

ObGVTxSchedulerStat::~ObGVTxSchedulerStat()
{
  reset();
}

void ObGVTxSchedulerStat::reset()
{
  tx_scheduler_stat_iter_.reset();
  parts_buffer_[0] = '\0';
  tx_desc_addr_buffer_[0] = '\0';
  savepoints_buffer_[0] = '\0';
  ObVirtualTableScannerIterator::reset();
}

int ObGVTxSchedulerStat::get_next_tx_info_(ObTxSchedulerStat &tx_scheduler_stat)
{
  ObTxSchedulerStat tmp_tx_scheduler_stat;

  int ret = tx_scheduler_stat_iter_.get_next(tmp_tx_scheduler_stat);

  if (OB_SUCC(ret)) {
    tx_scheduler_stat = tmp_tx_scheduler_stat;
  }

  return ret;

}

int ObGVTxSchedulerStat::inner_get_next_row(common::ObNewRow *&row)
{
  int ret = OB_SUCCESS;
  ObTxSchedulerStat tx_scheduler_stat;

  if (nullptr == allocator_) {
    ret = OB_NOT_INIT;
    SERVER_LOG(WARN, "allocator_ shouldn't be nullptr", K(allocator_), KR(ret));
  } else if (FALSE_IT(start_to_read_ = true)) {
  } else if (!tx_scheduler_stat_iter_.is_ready()) {
    transaction::ObTransService *txs = ::oceanbase::share::server_service<::oceanbase::transaction::ObTransService>();
    if (OB_ISNULL(txs)) {
      ret = OB_ERR_UNEXPECTED;
      SERVER_LOG(WARN, "transaction service is null", KR(ret));
    } else if (OB_FAIL(txs->iterate_tx_scheduler_stat(tx_scheduler_stat_iter_))) {
    }
    if (OB_FAIL(ret)) {
    } else if (OB_FAIL(tx_scheduler_stat_iter_.set_ready())) {
    }
  }

  if (OB_FAIL(ret)) {
  } else if (OB_FAIL(get_next_tx_info_(tx_scheduler_stat))) {
    if (OB_ITER_END != ret) {
      SERVER_LOG(WARN, "ObGVTxSchedulerStat iter error", KR(ret));
    }
  } else {
    const int64_t col_count = output_column_ids_.count();
    for (int64_t i = 0; OB_SUCC(ret) && i < col_count; ++i) {
      uint64_t col_id = output_column_ids_.at(i);
      switch (col_id) {
        case SESSION_ID:
          cur_row_.cells_[i].set_int(tx_scheduler_stat.sess_id_);
          break;
        case TX_ID:
          cur_row_.cells_[i].set_int(tx_scheduler_stat.tx_id_.get_id());
          break;
        case STATE:
          cur_row_.cells_[i].set_int(tx_scheduler_stat.state_);
          break;
        case WRITE_STATE:
          if (tx_scheduler_stat.has_write_state_) {
            tx_scheduler_stat.get_parts_str(parts_buffer_, OB_MAX_BUFFER_SIZE);
            cur_row_.cells_[i].set_varchar(parts_buffer_);
            cur_row_.cells_[i].set_default_collation_type();
          } else {
            cur_row_.cells_[i].reset();
          }
          break;
        case ISOLATION_LEVEL:
          cur_row_.cells_[i].set_int((int)tx_scheduler_stat.isolation_);
          break;
        case SNAPSHOT_VERSION:
          if (tx_scheduler_stat.snapshot_version_.get_val_for_inner_table_field() != OB_INVALID_SCN_VAL) {
            cur_row_.cells_[i].set_uint64(tx_scheduler_stat.snapshot_version_.get_val_for_inner_table_field());
          } else {
            cur_row_.cells_[i].reset();
          }
          break;
        case ACCESS_MODE:
          cur_row_.cells_[i].set_int((int)tx_scheduler_stat.access_mode_);
          break;
        case TX_OP_SN:
          cur_row_.cells_[i].set_int(tx_scheduler_stat.op_sn_);
          break;
        case FLAG:
          cur_row_.cells_[i].set_int(tx_scheduler_stat.flag_);
          break;
        case ACTIVE_TS:
          if (is_valid_timestamp_(tx_scheduler_stat.active_ts_)) {
            cur_row_.cells_[i].set_timestamp(tx_scheduler_stat.active_ts_);
          } else {
            cur_row_.cells_[i].reset();
          }
          break;
        case EXPIRE_TS:
          if (is_valid_timestamp_(tx_scheduler_stat.expire_ts_)) {
            cur_row_.cells_[i].set_timestamp(tx_scheduler_stat.expire_ts_);
          } else {
            cur_row_.cells_[i].reset();
          }
          break;
        case TIMEOUT_US:
          cur_row_.cells_[i].set_int(tx_scheduler_stat.timeout_us_);
          break;
        case REF_CNT:
          cur_row_.cells_[i].set_int(tx_scheduler_stat.ref_cnt_);
          break;
        case TX_DESC_ADDR:
          tx_desc_addr_buffer_[0] = 0;
          snprintf(tx_desc_addr_buffer_, 18, "0x%lx", (uint64_t)tx_scheduler_stat.tx_desc_addr_);
          cur_row_.cells_[i].set_varchar(tx_desc_addr_buffer_);
          cur_row_.cells_[i].set_default_collation_type();
          break;
        case SAVEPOINTS:
          if (0 < tx_scheduler_stat.savepoints_.count()) {
            (void)tx_scheduler_stat.savepoints_.to_string(savepoints_buffer_, OB_MAX_BUFFER_SIZE);
            cur_row_.cells_[i].set_varchar(savepoints_buffer_);
            cur_row_.cells_[i].set_default_collation_type();
          } else {
            cur_row_.cells_[i].reset();
          }
          break;
        case SAVEPOINTS_TOTAL_CNT:
          cur_row_.cells_[i].set_int(tx_scheduler_stat.savepoints_.count());
          break;
        case INTERNAL_ABORT_CAUSE:
          cur_row_.cells_[i].set_int(tx_scheduler_stat.abort_cause_);
          break;
        case CAN_EARLY_LOCK_RELEASE:
          cur_row_.cells_[i].set_bool(tx_scheduler_stat.can_elr_);
          break;
        default:
          ret = OB_ERR_UNEXPECTED;
          SERVER_LOG(WARN, "invalid coloum_id", KR(ret), K(col_id));
          break;
      }
    }
  }
  if (OB_SUCC(ret)) {
    row = &cur_row_;
  }

  return ret;
}

bool ObGVTxSchedulerStat::is_valid_timestamp_(const int64_t timestamp) const
{
  bool ret_bool = true;
  if (INT64_MAX == timestamp || 0 > timestamp) {
    ret_bool = false;
  }
  return ret_bool;
}

}
}
