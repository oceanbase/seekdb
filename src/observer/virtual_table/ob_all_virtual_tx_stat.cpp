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

#include "observer/virtual_table/ob_all_virtual_tx_stat.h"
#include "share/rc/ob_server_runtime.h"

#include "observer/ob_server.h"
#include "storage/tx/ob_trans_service.h"

using namespace oceanbase::common;
using namespace oceanbase::transaction;

namespace oceanbase
{
namespace observer
{
ObGVTxStat::ObGVTxStat()
    : ObVirtualTableScannerIterator(),
      ctx_addr_buffer_(),
      tx_stat_iter_(),
      cstring_helper_()
{}

void ObGVTxStat::reset()
{
  ctx_addr_buffer_[0] = '\0';
  tx_stat_iter_.reset();
  cstring_helper_.reset();
  ObVirtualTableScannerIterator::reset();
}

int ObGVTxStat::prepare_start_to_read_()
{
  int ret = OB_SUCCESS;
  tx_stat_iter_.reset();
  if (OB_ISNULL(allocator_)) {
    ret = OB_NOT_INIT;
    SERVER_LOG(WARN, "allocator is null", K(ret));
  } else {
    SERVER_MODULE_SCOPE {
      transaction::ObTransService *txs = ::oceanbase::share::server_service<::oceanbase::transaction::ObTransService>();
      if (OB_ISNULL(txs)) {
        ret = OB_ERR_UNEXPECTED;
        SERVER_LOG(WARN, "transaction service is null", K(ret));
      } else if (OB_FAIL(txs->iterate_all_observer_tx_stat(tx_stat_iter_))) {
      }
    }
  }
  if (OB_SUCC(ret) && OB_FAIL(tx_stat_iter_.set_ready())) {
    SERVER_LOG(WARN, "ObTransStatIterator set ready error", K(ret));
  } else if (OB_SUCC(ret)) {
    start_to_read_ = true;
  }

  return ret;
}

int ObGVTxStat::inner_get_next_row(ObNewRow *&row)
{
  int ret = OB_SUCCESS;
  ObTxStat tx_stat;

  if (!start_to_read_ && OB_SUCCESS != (ret = prepare_start_to_read_())) {
    SERVER_LOG(WARN, "prepare start to read error", K(ret), K(start_to_read_));
  } else if (OB_FAIL(tx_stat_iter_.get_next(tx_stat))) {
    if (OB_ITER_END != ret) {
      SERVER_LOG(WARN, "ObGVTxStat iter error", K(ret));
    } else {
    }
  } else {
    const int64_t col_count = output_column_ids_.count();
    cstring_helper_.reset();
    for (int64_t i = 0; OB_SUCC(ret) && i < col_count; ++i) {
      uint64_t col_id = output_column_ids_.at(i);
      switch (col_id) {
        case TX_ID:
          cur_row_.cells_[i].set_int(tx_stat.tx_id_.get_id());
          break;
        case SESSION_ID:
          cur_row_.cells_[i].set_int(tx_stat.session_id_);
          break;
        case IS_DECIDED:
          cur_row_.cells_[i].set_bool(tx_stat.has_decided_);
          break;
        case WRITE_STATE:
          cur_row_.cells_[i].set_varchar(tx_stat.has_write_state_ ? "true" : "false");
          cur_row_.cells_[i].set_collation_type(ObCharset::get_default_collation(ObCharset::get_default_charset()));
          break;
        case TX_CTX_CREATE_TIME:
          if (is_valid_timestamp_(tx_stat.tx_ctx_create_time_)) {
            cur_row_.cells_[i].set_timestamp(tx_stat.tx_ctx_create_time_);
          } else {
            // if invalid timestamp, display NULL
            cur_row_.cells_[i].reset();
          }
          break;
        case TX_EXPIRED_TIME:
          if (is_valid_timestamp_(tx_stat.tx_expired_time_)) {
            cur_row_.cells_[i].set_timestamp(tx_stat.tx_expired_time_);
          } else {
            // if invalid timestamp, display NULL
            cur_row_.cells_[i].reset();
          }
          break;
        case REF_CNT:
          cur_row_.cells_[i].set_int(tx_stat.ref_cnt_);
          break;
        case LAST_OP_SN:
          cur_row_.cells_[i].set_int(tx_stat.last_op_sn_);
          break;
        case PENDING_WRITE:
          cur_row_.cells_[i].set_int(tx_stat.pending_write_);
          break;
        case STATE:
          cur_row_.cells_[i].set_int(tx_stat.state_);
          break;
        case PART_TX_ACTION:
          cur_row_.cells_[i].set_int(tx_stat.part_tx_action_);
          break;
        case TX_CTX_ADDR:
          ctx_addr_buffer_[0] = 0;
            snprintf(ctx_addr_buffer_, CTX_ADDR_BUFFER_SIZE, "0x%lx", (uint64_t)tx_stat.tx_ctx_addr_);
          cur_row_.cells_[i].set_varchar(ctx_addr_buffer_);
          cur_row_.cells_[i].set_default_collation_type();
          break;
        case PENDING_LOG_SIZE:
          cur_row_.cells_[i].set_int(tx_stat.pending_log_size_);
          break;
        case FLUSHED_LOG_SIZE:
          cur_row_.cells_[i].set_int(tx_stat.flushed_log_size_);
          break;
        case IS_EXITING:
          cur_row_.cells_[i].set_int(tx_stat.is_exiting_);
          break;
        case LAST_REQUEST_TS:
          cur_row_.cells_[i].set_timestamp(tx_stat.last_request_ts_);
          break;
        case START_SCN:
          cur_row_.cells_[i].set_uint64(tx_stat.start_scn_.get_val_for_inner_table_field());
          break;
        case END_SCN:
          cur_row_.cells_[i].set_uint64(tx_stat.end_scn_.get_val_for_inner_table_field());
          break;
        case REC_SCN:
          cur_row_.cells_[i].set_uint64(tx_stat.rec_scn_.get_val_for_inner_table_field());
          break;
        case BUSY_CBS_CNT:
          cur_row_.cells_[i].set_int(tx_stat.busy_cbs_cnt_);
          break;
        case REPLAY_COMPLETE:
          cur_row_.cells_[i].set_int(tx_stat.replay_completeness_);
          break;
        case SERIAL_LOG_FINAL_SCN:
          cur_row_.cells_[i].set_int(tx_stat.serial_final_scn_.get_val_for_inner_table_field());
          break;
        case CALLBACK_LIST_STATS:
          {
            const char *buf = NULL;
            if (OB_FAIL(cstring_helper_.convert(tx_stat.get_callback_list_stats_displayer(), buf))) {
            } else {
              const int32_t buf_len = static_cast<int32_t>(strlen(buf));
              cur_row_.cells_[i].set_lob_value(ObLongTextType, buf, buf_len);
              cur_row_.cells_[i].set_collation_type(ObCharset::get_default_collation(ObCharset::get_default_charset()));
            }
          }
          break;
        default:
          ret = OB_ERR_UNEXPECTED;
          SERVER_LOG(WARN, "invalid coloum_id", K(ret), K(col_id));
          break;
      }
    }
  }
  if (OB_SUCC(ret)) {
    row = &cur_row_;
  }

  return ret;
}

bool ObGVTxStat::is_valid_timestamp_(const int64_t timestamp) const
{
  bool ret_bool = true;
  if (INT64_MAX == timestamp || 0 > timestamp) {
    ret_bool = false;
  }
  return ret_bool;
}

}/* ns observer*/
}/* ns oceanbase */
