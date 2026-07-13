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
#define USING_LOG_PREFIX STORAGE
#include "ob_tx_table_guards.h"
#include "storage/tx_table/ob_tx_table.h"

namespace oceanbase
{
namespace storage
{

int ObTxTableGuards::check_row_locked(const transaction::ObTransID &read_tx_id,
                                      const transaction::ObTransID &data_tx_id,
                                      const transaction::ObTxSEQ &sql_sequence,
                                      const share::SCN &scn,
                                      storage::ObStoreRowLockState &lock_state)
{
  int ret = OB_SUCCESS;

  CheckRowLockedFunctor fn(read_tx_id,
                           data_tx_id,
                           sql_sequence,
                           lock_state);

  ret = check_with_tx_data(data_tx_id, fn);

  return ret;
}

int ObTxTableGuards::check_sql_sequence_can_read(
    const transaction::ObTransID &data_tx_id,
    const transaction::ObTxSEQ &sql_sequence,
    const share::SCN &scn,
    bool &can_read)
{
  int ret = OB_SUCCESS;

  CheckSqlSequenceCanReadFunctor fn(sql_sequence,
                                    can_read);

  ret = check_with_tx_data(data_tx_id, fn);

  return ret;
}

int ObTxTableGuards::get_tx_state_with_scn(
    const transaction::ObTransID &data_tx_id,
    const share::SCN scn,
    int64_t &state,
    share::SCN &trans_version)
{
  int ret = OB_SUCCESS;

  GetTxStateWithSCNFunctor fn(scn,
                              state,
                              trans_version);

  ret = check_with_tx_data(data_tx_id, fn);

  return ret;
}

int ObTxTableGuards::lock_for_read(
    const transaction::ObLockForReadArg &lock_for_read_arg,
    bool &can_read,
    share::SCN &trans_version,
    ObCleanoutOp &cleanout_op,
    ObReCheckOp &recheck_op)
{
  int ret = OB_SUCCESS;

  LockForReadFunctor fn(lock_for_read_arg,
                        can_read,
                        trans_version,
                        tx_table_guard_.get_ls_id(),
                        cleanout_op,
                        recheck_op);

  ret = check_with_tx_data(lock_for_read_arg.data_trans_id_, fn);

  if (OB_SUCC(ret) && cleanout_op.need_cleanout()) {
    cleanout_op(fn.get_tx_data_check_data());
  }

  return ret;
}

int ObTxTableGuards::lock_for_read(
    const transaction::ObLockForReadArg &lock_for_read_arg,
    bool &can_read,
    share::SCN &trans_version)
{
  int ret = OB_SUCCESS;
  ObCleanoutNothingOperation clean_nothing_op;
  ObReCheckNothingOperation recheck_nothing_op;

  LockForReadFunctor fn(lock_for_read_arg,
                        can_read,
                        trans_version,
                        tx_table_guard_.get_ls_id(),
                        clean_nothing_op,
                        recheck_nothing_op);

  ret = check_with_tx_data(lock_for_read_arg.data_trans_id_, fn);

  return ret;
}

int ObTxTableGuards::cleanout_tx_node(
    const transaction::ObTransID &data_tx_id,
    memtable::ObMvccRow &value,
    memtable::ObMvccTransNode &tnode,
    const bool need_row_latch)
{
  int ret = OB_SUCCESS;

  ObCleanoutTxNodeOperation op(value,
                               tnode,
                               need_row_latch);
  CleanoutTxStateFunctor fn(tnode.seq_no_, op);

  ret = check_with_tx_data(data_tx_id, fn);

  if (OB_SUCC(ret) && op.need_cleanout()) {
    op(fn.get_tx_data_check_data());
  }

  return ret;
}

bool ObTxTableGuards::check_ls_offline()
{
  return tx_table_guard_.check_ls_offline();
}

int ObTxTableGuards::check_with_tx_data(
  const transaction::ObTransID &data_tx_id,
  ObITxDataCheckFunctor &functor)
{
  int ret = OB_SUCCESS;
  ObReadTxDataArg arg(data_tx_id,
                      tx_table_guard_.get_epoch(),
                      tx_table_guard_.get_mini_cache());

  if (!is_valid()) {
    ret = OB_INVALID_ARGUMENT;
    LOG_WARN("tx table guards is invalid", K(ret), KPC(this), K(arg));
  } else if (OB_FAIL(tx_table_guard_.check_with_tx_data(arg,
                                                        functor))) {
    if (OB_TRANS_CTX_NOT_EXIST != ret) {
      LOG_WARN("check with dst tx data failed", K(ret), KPC(this), K(arg));
    }
  }

  return ret;
}

} // end namespace storage
} // end namespace oceanbase
