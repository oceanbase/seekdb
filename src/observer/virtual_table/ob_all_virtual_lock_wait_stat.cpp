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

#include "observer/virtual_table/ob_all_virtual_lock_wait_stat.h"
#include "share/rc/ob_server_runtime.h"

#include "storage/memtable/ob_lock_wait_mgr.h"
#include "observer/ob_server_utils.h"

using namespace oceanbase::rpc;
using namespace oceanbase::common;
using namespace oceanbase::memtable;
using namespace oceanbase::storage;
namespace oceanbase
{
namespace observer
{

void ObAllVirtualLockWaitStat::reset()
{
  rowkey_[0] = '\0';
  lock_mode_[0] = '\0';
  start_to_read_ = false;
  node_iter_ = nullptr;
  ObVirtualTableScannerIterator::reset();
}

int ObAllVirtualLockWaitStat::inner_get_next_row(ObNewRow *&row)
{
  int ret = OB_SUCCESS;
  int tmp_ret = OB_SUCCESS;

  if (!start_to_read_) {
    start_to_read_ = true;
  }
  if (OB_ISNULL(node_iter_ = ::oceanbase::share::server_service<::oceanbase::memtable::ObLockWaitMgr>()
                                        ->next(node_iter_, &cur_node_))) {
    ret = OB_ITER_END;
  } else {
    const int type = get_lock_type(node_iter_->hash_);
    const int64_t col_count = output_column_ids_.count();
    for (int64_t i = 0; OB_SUCC(ret) && i < col_count; ++i) {
      uint64_t col_id = output_column_ids_.at(i);
      switch (col_id) {
        case TABLET_ID:
          (OB_SUCC(ret) ? ret = DatumValueBuffer::set_int(cur_row_.cells_, cur_row_.count_, i, node_iter_->tablet_id_) : ret);
          break;
        case ROWKEY:
          {
            snprintf(rowkey_, sizeof(rowkey_), "%s", node_iter_->key_);
            (OB_SUCC(ret) ? ret = DatumValueBuffer::set_varchar_view(cur_row_.cells_, cur_row_.count_, i, rowkey_) : ret);
            cur_row_.cells_[i].set_collation_type(ObCharset::get_default_collation(ObCharset::get_default_charset()));
            break;
          }
        case ADDR:
          (OB_SUCC(ret) ? ret = DatumValueBuffer::set_uint64(cur_row_.cells_, cur_row_.count_, i, (uint64_t)node_iter_->addr_) : ret);
          break;
        case NEED_WAIT:
          (OB_SUCC(ret) ? ret = DatumValueBuffer::set_bool(cur_row_.cells_, cur_row_.count_, i, node_iter_->need_wait_) : ret);
          break;
        case RECV_TS:
          (OB_SUCC(ret) ? ret = DatumValueBuffer::set_int(cur_row_.cells_, cur_row_.count_, i, node_iter_->recv_ts_) : ret);
          break;
        case LOCK_TS:
          (OB_SUCC(ret) ? ret = DatumValueBuffer::set_int(cur_row_.cells_, cur_row_.count_, i, node_iter_->lock_ts_) : ret);
          break;
        case ABS_TIMEOUT:
          (OB_SUCC(ret) ? ret = DatumValueBuffer::set_int(cur_row_.cells_, cur_row_.count_, i, node_iter_->abs_timeout_) : ret);
          break;
        case TRY_LOCK_TIMES:
          (OB_SUCC(ret) ? ret = DatumValueBuffer::set_int(cur_row_.cells_, cur_row_.count_, i, node_iter_->try_lock_times_) : ret);
          break;
        case TIME_AFTER_RECV:
          {
            int64_t cur_ts = ObTimeUtility::current_time();
            (OB_SUCC(ret) ? ret = DatumValueBuffer::set_int(cur_row_.cells_, cur_row_.count_, i, cur_ts - node_iter_->recv_ts_) : ret);
            break;
          }
        case SESSION_ID:
          {
            (OB_SUCC(ret) ? ret = DatumValueBuffer::set_int(cur_row_.cells_, cur_row_.count_, i, node_iter_->sessid_) : ret);
            break;
          }
        case BLOCK_SESSION_ID:
          (OB_SUCC(ret) ? ret = DatumValueBuffer::set_int(cur_row_.cells_, cur_row_.count_, i, node_iter_->block_sessid_) : ret);
          break;
        case TYPE:
          (OB_SUCC(ret) ? ret = DatumValueBuffer::set_int(cur_row_.cells_, cur_row_.count_, i, type) : ret);
          break;
        case LMODE: {
          if (type == 1 || type == 2) {
            (OB_SUCC(ret) ? ret = DatumValueBuffer::set_varchar_view(cur_row_.cells_, cur_row_.count_, i, "X") : ret);
          } else if (type == 3) {
            char lock_mode_tmp[MAX_LOCK_MODE_BUF_LENGTH];
            if (OB_FAIL(lock_mode_to_string(node_iter_->lock_mode_,
                                            lock_mode_tmp,
                                            sizeof(lock_mode_tmp)))) {
            } else {
              snprintf(lock_mode_, sizeof(lock_mode_), "%s", lock_mode_tmp);
              (OB_SUCC(ret) ? ret = DatumValueBuffer::set_varchar_view(cur_row_.cells_, cur_row_.count_, i, lock_mode_) : ret);
            }
          }
          if (OB_SUCC(ret)) {
            cur_row_.cells_[i].set_collation_type(
                ObCharset::get_default_collation(ObCharset::get_default_charset()));
          }
          cur_row_.cells_[i].set_collation_type(
              ObCharset::get_default_collation(
                  ObCharset::get_default_charset()));
          break;
        }
        case LAST_COMPACT_CNT:
          (OB_SUCC(ret) ? ret = DatumValueBuffer::set_int(cur_row_.cells_, cur_row_.count_, i, node_iter_->last_compact_cnt_) : ret);
          break;
        case TOTAL_UPDATE_CNT:
          (OB_SUCC(ret) ? ret = DatumValueBuffer::set_int(cur_row_.cells_, cur_row_.count_, i, node_iter_->total_update_cnt_) : ret);
          break;
        case TRANS_ID:
          (OB_SUCC(ret) ? ret = DatumValueBuffer::set_int(cur_row_.cells_, cur_row_.count_, i, node_iter_->tx_id_) : ret);
          break;
        case HOLDER_TRANS_ID: {
          // The default value is holder_tx_id on the ObLockWaitNode,
          // which is the holder for TX lock only gererated when the
          // row is dumped into sstable.
          // Actually, the lock on a row, which we named it as TR lock, is a
          // part of TX. It represents a class of resources held by TX lock.
          ObTransID holder_tx_id { node_iter_->holder_tx_id_ };
          // If the waiter is waitting on the row, we need to get the
          // real holder from the hash_holder, instead of the holder_tx_id
          // on the ObLockWaitNode.
          if (type == 1) {
            // TODO(yangyifei.yyf): rowkey holder is unstable now, so we use
            // tmp ret to catch error code here. We we fix it in the future.
            if (OB_TMP_FAIL(get_rowkey_holder(node_iter_->hash_, holder_tx_id))) {
            }
          }
          (OB_SUCC(ret) ? ret = DatumValueBuffer::set_int(cur_row_.cells_, cur_row_.count_, i, holder_tx_id.get_id()) : ret);
          break;
        }
        case HOLDER_SESSION_ID:
          (OB_SUCC(ret) ? ret = DatumValueBuffer::set_int(cur_row_.cells_, cur_row_.count_, i, node_iter_->holder_sessid_) : ret);
          break;
        default:
          ret = OB_ERR_UNEXPECTED;
          SERVER_LOG(WARN, "invalid col_id", K(ret), K(col_id));
          break;
      }
    }
    if (OB_SUCC(ret)) {
      row = &cur_row_;
    }
  }
  return ret;
}

int ObAllVirtualLockWaitStat::get_lock_type(int64_t hash) const
{
  int type = 0;
  if (LockHashHelper::is_rowkey_hash(hash)) {
    type = 1;
  } else if (LockHashHelper::is_trans_hash(hash)) {
    type = 2;
  } else if (LockHashHelper::is_table_lock_hash(hash)) {
    type = 3;
  }
  return type;
}

int ObAllVirtualLockWaitStat::get_rowkey_holder(int64_t hash, transaction::ObTransID &holder)
{
  int ret = OB_SUCCESS;
  ObLockWaitMgr *lwm = NULL;
  if (OB_ISNULL(lwm = ::oceanbase::share::server_service<::oceanbase::memtable::ObLockWaitMgr>())) {
    ret = OB_ERR_UNEXPECTED;
    SERVER_LOG(ERROR, "server LockWaitMgr is null");
  } else if (OB_FAIL(lwm->get_hash_holder(hash, holder))){
  }
  return ret;
}

}/* ns observer*/
}/* ns oceanbase */
