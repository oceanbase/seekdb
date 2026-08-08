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

#include "observer/virtual_table/ob_all_virtual_tx_ctx_mgr_stat.h"
#include "storage/tx/ob_trans_service.h"
#include "observer/ob_server.h"

using namespace oceanbase::common;
using namespace oceanbase::transaction;

namespace oceanbase
{
namespace observer
{
void ObGVTxCtxMgrStat::reset()
{
  memstore_version_buffer_[0] = '\0';
  tx_ctx_mgr_stat_.reset();
  is_stat_outputted_ = false;

  ObVirtualTableScannerIterator::reset();
}

void ObGVTxCtxMgrStat::destroy()
{
  trans_service_ = NULL;
  memset(memstore_version_buffer_, 0, common::MAX_VERSION_LENGTH);
  tx_ctx_mgr_stat_.reset();
  is_stat_outputted_ = false;

  ObVirtualTableScannerIterator::reset();
}

int ObGVTxCtxMgrStat::prepare_start_to_read_()
{
  int ret = OB_SUCCESS;
  ObObj *cells = NULL;

  if (NULL == allocator_ || NULL == trans_service_) {
    SERVER_LOG(WARN, "invalid argument, allocator_ or trans_service_ is null", "allocator",
        OB_P(allocator_), "trans_service", OB_P(trans_service_));
    ret = OB_INVALID_ARGUMENT;
  } else if (NULL == (cells = cur_row_.cells_)) {
    ret = OB_ERR_UNEXPECTED;
    SERVER_LOG(ERROR, "cur row cell is NULL", K(ret));
  } else if (OB_FAIL(trans_service_->get_tx_ctx_mgr_stat(tx_ctx_mgr_stat_))) {
  }
  if (OB_SUCC(ret)) {
    start_to_read_ = true;
  }

  return ret;
}

int ObGVTxCtxMgrStat::inner_get_next_row(ObNewRow *&row)
{
  int ret = OB_SUCCESS;

  if (!start_to_read_ && OB_SUCCESS != (ret = prepare_start_to_read_())) {
    SERVER_LOG(WARN, "prepare_start_to_read_ error", K(ret), K(start_to_read_));
  } else if (is_stat_outputted_) {
    ret = OB_ITER_END;
  } else {
    // Column order after removing svr_ip and svr_port:
    // OB_APP_MIN_COLUMN_ID (16): is_stopped
    // OB_APP_MIN_COLUMN_ID + 1 (17): block_tx
    // OB_APP_MIN_COLUMN_ID + 2 (18): block_normal_tx
    // OB_APP_MIN_COLUMN_ID + 3 (19): block_all
    // OB_APP_MIN_COLUMN_ID + 4 (20): total_trans_ctx_count
    // OB_APP_MIN_COLUMN_ID + 5 (21): mgr_addr
    const int64_t col_count = output_column_ids_.count();
    for (int64_t i = 0; OB_SUCC(ret) && i < col_count; ++i) {
      uint64_t col_id = output_column_ids_.at(i);
      switch (col_id) {
        case OB_APP_MIN_COLUMN_ID:
          cur_row_.cells_[i].set_int(tx_ctx_mgr_stat_.is_stopped() ? 1 : 0);
          break;
        case OB_APP_MIN_COLUMN_ID + 1:
          cur_row_.cells_[i].set_int(tx_ctx_mgr_stat_.is_tx_blocked() ? 1 : 0);
          break;
        case OB_APP_MIN_COLUMN_ID + 2:
          cur_row_.cells_[i].set_int(
              tx_ctx_mgr_stat_.is_normal_tx_blocked() ? 1 : 0);
          break;
        case OB_APP_MIN_COLUMN_ID + 3:
          cur_row_.cells_[i].set_int(tx_ctx_mgr_stat_.is_all_blocked() ? 1 : 0);
          break;
        case OB_APP_MIN_COLUMN_ID + 4:
          // total_tx_ctx_count
          cur_row_.cells_[i].set_int(tx_ctx_mgr_stat_.get_total_tx_ctx_count());
          break;
        case OB_APP_MIN_COLUMN_ID + 5:
          cur_row_.cells_[i].set_int(tx_ctx_mgr_stat_.get_mgr_addr());
          break;
        default:
          ret = OB_ERR_UNEXPECTED;
          SERVER_LOG(WARN, "invalid column_id", K(ret), K(col_id));
          break;
      }
    }
  }
  if (OB_SUCC(ret)) {
    is_stat_outputted_ = true;
    row = &cur_row_;
  }

  return ret;
}

}/* ns observer*/
}/* ns oceanbase */
