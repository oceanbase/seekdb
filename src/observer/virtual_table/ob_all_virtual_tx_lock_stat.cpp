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

#include "observer/virtual_table/ob_all_virtual_tx_lock_stat.h"
#include "share/rc/ob_module_provider.h"
#include "storage/ls/ob_ls.h"
#include "storage/tx_storage/ob_ls_service.h"
#include "storage/tx/ob_tx_ctx.h"

namespace oceanbase
{
using namespace common;
using namespace transaction;

namespace observer
{
ObGVTxLockStat::ObGVTxLockStat()
    : ObVirtualTableScannerIterator(),
      memtable_key_buffer_(),
      ls_(nullptr),
      tx_ctx_iter_(),
      tx_lock_stat_iter_() {}
ObGVTxLockStat::~ObGVTxLockStat() { reset(); }

void ObGVTxLockStat::reset()
{
  memtable_key_buffer_[0] = '\0';
  ls_ = nullptr;
  tx_ctx_iter_.reset();
  tx_lock_stat_iter_.reset();
  start_to_read_ = false;
  ObVirtualTableScannerIterator::reset();
}

int ObGVTxLockStat::get_next_tx_lock_stat_iter_(transaction::ObTxLockStatIterator &tx_lock_stat_iter)
{
  int ret = OB_SUCCESS;
  transaction::ObTxCtx *tx_ctx = nullptr;

  if (OB_FAIL(tx_ctx_iter_.get_next_tx_ctx(tx_ctx))) {
    if (OB_ITER_END != ret) {
      SERVER_LOG(WARN, "fail to get next tx_ctx", K(ret));
    }
  } else if (OB_ISNULL(tx_ctx)) {
    ret = OB_ERR_UNEXPECTED;
    SERVER_LOG(WARN, "tx_ctx is null", K(ret));
  } else {
    tx_lock_stat_iter.reset();
    if (OB_FAIL(tx_ctx->iterate_tx_lock_stat(tx_lock_stat_iter))) {
      SERVER_LOG(WARN, "fail to get lock op iter", K(ret));
    } else if (OB_FAIL(tx_lock_stat_iter.set_ready())) {
      SERVER_LOG(WARN, "set lock_op_iter_ ready failed", K(ret));
    }
  }
  if (OB_NOT_NULL(tx_ctx)) {
    tx_ctx_iter_.revert_tx_ctx(tx_ctx);
  }
  return ret;
}

int ObGVTxLockStat::get_next_tx_lock_stat_(ObTxLockStat &tx_lock_stat)
{
  int ret = OB_SUCCESS;

  while (OB_SUCC(ret)) {
    if (OB_FAIL(tx_lock_stat_iter_.get_next(tx_lock_stat))) {
      if (OB_ITER_END == ret) {
        if (OB_FAIL(get_next_tx_lock_stat_iter_(tx_lock_stat_iter_))) {
          if (OB_ITER_END != ret) {
            TRANS_LOG(WARN, "get next tx_lock_stat_iter failed", K(ret));
          }
        }
      } else {
        TRANS_LOG(WARN, "get next tx_lock_stat failed", K(ret));
      }
    } else {
      break;
    }
  }

  return ret;
}

int ObGVTxLockStat::prepare_start_to_read_()
{
  int ret = OB_SUCCESS;
  ObLSService *ls_service = share::g_mp->ls_service();
  if (OB_ISNULL(allocator_)) {
    ret = OB_NOT_INIT;
    SERVER_LOG(WARN, "allocator_ shouldn't be NULL", K(allocator_), K(ret));
  } else if (OB_ISNULL(ls_service)) {
    ret = OB_ERR_UNEXPECTED;
    SERVER_LOG(WARN, "ls service is null", K(ret));
  } else if (OB_FAIL(ls_service->get_ls(ls_))) {
    SERVER_LOG(WARN, "get log stream failed", K(ret));
  } else if (OB_FAIL(ls_->iterate_tx_ctx(tx_ctx_iter_))) {
    SERVER_LOG(WARN, "fail to get tx ctx iter", K(ret));
  } else if (OB_FAIL(get_next_tx_lock_stat_iter_(tx_lock_stat_iter_))) {
    SERVER_LOG(WARN, "init tx_lock_stat_iter_ failed", K(ret));
  } else {
    start_to_read_ = true;
  }
  return ret;
}

int ObGVTxLockStat::inner_get_next_row(ObNewRow *&row)
{
  int ret = OB_SUCCESS;
  ObTxLockStat tx_lock_stat;

  if (!start_to_read_ && OB_FAIL(prepare_start_to_read_())) {
    SERVER_LOG(WARN, "prepare_start_to_read_ error", K(ret), K(start_to_read_));
  } else if (OB_FAIL(get_next_tx_lock_stat_(tx_lock_stat))) {
    if (OB_ITER_END != ret) {
      SERVER_LOG(WARN, "get_next_lock_op failed", K(ret));
    }
  } else {
    const int64_t col_count = output_column_ids_.count();

    for (int64_t i = 0; i < col_count; i++) {
      uint64_t col_id = output_column_ids_.at(i);
      switch(col_id) {

      case OB_APP_MIN_COLUMN_ID + 0: {
        // trans_id
        cur_row_.cells_[i].set_int(tx_lock_stat.get_tx_id().get_id());
        break;
      }
      case OB_APP_MIN_COLUMN_ID + 1:
        // tablet_id
        cur_row_.cells_[i].set_int(tx_lock_stat.get_memtable_key_info().get_tablet_id().id());
        break;
      case OB_APP_MIN_COLUMN_ID + 2:
        // rowkey
        snprintf(memtable_key_buffer_, OB_MEMTABLE_KEY_BUFFER_SIZE, "%s", tx_lock_stat.get_memtable_key_info().read_buf());
        if ('\0' == memtable_key_buffer_[0]) {
          // if rowkey is empty, we should set it as NULL
          cur_row_.cells_[i].reset();
        } else {
          cur_row_.cells_[i].set_varchar(memtable_key_buffer_);
        }
        cur_row_.cells_[i].set_collation_type(ObCharset::get_default_collation(ObCharset::get_default_charset()));
        break;
      case OB_APP_MIN_COLUMN_ID + 3:
        // session_id
        cur_row_.cells_[i].set_int(tx_lock_stat.get_client_sid());
        break;
      case OB_APP_MIN_COLUMN_ID + 4:
        // tx_ctx_create_time
        cur_row_.cells_[i].set_timestamp(tx_lock_stat.get_tx_ctx_create_time());
        break;
      case OB_APP_MIN_COLUMN_ID + 5:
        // expired_time
        cur_row_.cells_[i].set_timestamp(tx_lock_stat.get_tx_expired_time());
        break;
      case OB_APP_MIN_COLUMN_ID + 6:
        // time_after_recv
        cur_row_.cells_[i].set_int(ObTimeUtility::current_time() - tx_lock_stat.get_tx_ctx_create_time());
        break;
      case OB_APP_MIN_COLUMN_ID + 7:
        // row_lock_addr
        cur_row_.cells_[i].set_uint64(uint64_t(tx_lock_stat.get_memtable_key_info().get_row_lock()));
        break;
      }
    }
    if (OB_SUCC(ret)) {
      row = &cur_row_;
    }
  }
  return ret;
}

}//observer
}//oceanbase
