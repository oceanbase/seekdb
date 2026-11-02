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


#include "ob_tx_table_interface.h"
#include "storage/tx_table/ob_tx_table.h"

namespace oceanbase {
namespace storage {

int ObTxTableGuard::init(ObTxTable *tx_table)
{
  int ret = OB_SUCCESS;

  if (OB_ISNULL(tx_table)) {
    ret = OB_INVALID_ARGUMENT;
    STORAGE_LOG(WARN, "tx_data_table is nullptr.");
  } else {
    mini_cache_.reset();
    epoch_ = tx_table->get_epoch();
    tx_table_ = tx_table;
  }
  return ret;
}

int ObTxTableGuard::check_with_tx_data(ObReadTxDataArg &read_tx_data_arg,
                                       ObITxDataCheckFunctor &fn)
{
  if (OB_NOT_NULL(tx_table_)) {
    return tx_table_->check_with_tx_data(read_tx_data_arg, fn);
  } else {
    return OB_NOT_INIT;
  }
}


int ObTxTableGuard::load_tx_op(const transaction::ObTransID &tx_id, ObTxData &tx_data)
{
  /*
   * when we load tx_op from tx_data just skip tx_data cache
   * read tx_data from tx_data_sstable promise tx_op is complete (undo_action/mds etc)
   */
  if (OB_NOT_NULL(tx_table_)) {
    ObReadTxDataArg arg(tx_id,
                        epoch_,
                        mini_cache_,
                        true/*skip_cache*/);
    LoadTxOpFunctor functor(tx_data);
    functor.set_may_exist_undecided_state_in_tx_data_table();
    return tx_table_->check_with_tx_data(arg, functor);
  } else {
    return OB_NOT_INIT;
  }
}


int ObTxTableGuard::get_tx_state_with_scn(const transaction::ObTransID tx_id,
                                          const share::SCN scn,
                                          int64_t &state,
                                          share::SCN &trans_version)
{
  if (OB_NOT_NULL(tx_table_)) {
    ObReadTxDataArg arg(tx_id, epoch_, mini_cache_);
    return tx_table_->get_tx_state_with_scn(arg, scn, state, trans_version);
  } else {
    return OB_NOT_INIT;
  }
}

int ObTxTableGuard::try_get_tx_state(const transaction::ObTransID tx_id,
                                     int64_t &state,
                                     share::SCN &trans_version,
                                     share::SCN &recycled_scn)
{
  if (OB_NOT_NULL(tx_table_)) {
    ObReadTxDataArg arg(tx_id, epoch_, mini_cache_);
    return tx_table_->try_get_tx_state(arg, state, trans_version, recycled_scn);
  } else {
    return OB_NOT_INIT;
  }
}


int ObTxTableGuard::lock_for_read(const transaction::ObLockForReadArg &lock_for_read_arg,
                                  bool &can_read,
                                  share::SCN &trans_version,
                                  ObCleanoutOp &cleanout_op,
                                  ObReCheckOp &recheck_op)
{
  if (OB_NOT_NULL(tx_table_)) {
    ObReadTxDataArg read_tx_data_arg(lock_for_read_arg.data_trans_id_, epoch_, mini_cache_);
    return tx_table_->lock_for_read(read_tx_data_arg,
                                    lock_for_read_arg,
                                    can_read,
                                    trans_version,
                                    cleanout_op,
                                    recheck_op);
  } else {
    return OB_NOT_INIT;
  }
}

int ObTxTableGuard::cleanout_tx_node(const transaction::ObTransID tx_id,
                                     memtable::ObMvccRow &value,
                                     memtable::ObMvccTransNode &tnode,
                                     const bool need_row_latch)
{
  if (OB_NOT_NULL(tx_table_)) {
    ObReadTxDataArg arg(tx_id, epoch_, mini_cache_);
    return tx_table_->cleanout_tx_node(arg, value, tnode, need_row_latch);
  } else {
    return OB_NOT_INIT;
  }
}

int ObTxTableGuard::get_recycle_scn(share::SCN &recycle_scn) { return tx_table_->get_recycle_scn(recycle_scn); }

int ObTxTableGuard::self_freeze_task()
{
  if (OB_NOT_NULL(tx_table_)) {
    return tx_table_->self_freeze_task();
  } else {
    return OB_NOT_INIT;
  }
}

bool ObTxTableGuard::check_ls_offline()
{
  bool discover_ls_offline = false;
  int ret = OB_SUCCESS;

  if (OB_ISNULL(tx_table_)) {
    ret = OB_NOT_INIT;
    discover_ls_offline = false;
    STORAGE_LOG(WARN, "tx table is nullptr", K(ret), K(discover_ls_offline));
  } else {
    int64_t cur_epoch = tx_table_->get_epoch();
    ObTxTable::TxTableState tx_table_state = tx_table_->get_state();
    if (cur_epoch != epoch_ || tx_table_state == ObTxTable::TxTableState::PREPARE_OFFLINE
        || tx_table_state == ObTxTable::TxTableState::OFFLINE) {
      discover_ls_offline = true;

      STORAGE_LOG(INFO, "discover ls offline", K(discover_ls_offline), K(cur_epoch), K(epoch_),
                  K(tx_table_state));
    }
  }

  return discover_ls_offline;
}

share::ObLSID ObTxTableGuard::get_ls_id() const
{
  return tx_table_->get_ls_id();
}

}  // namespace storage
}  // end namespace oceanbase
