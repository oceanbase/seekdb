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

#ifndef OCEANBASE_STORAGE_OB_TRANS_TABLE_INTERFACE_
#define OCEANBASE_STORAGE_OB_TRANS_TABLE_INTERFACE_

#include "lib/ob_define.h"
#include "lib/oblog/ob_log_module.h"
#include "lib/utility/ob_print_utils.h"
#include "lib/function/ob_function.h"
#include "storage/tx/ob_tx_data_define.h"
#include "storage/tx/ob_tx_data_functor.h"

namespace oceanbase
{
namespace storage
{
class ObTxTable;
class ObTxTableGuard
{
public:
  ObTxTableGuard() : tx_table_(nullptr), epoch_(-1), mini_cache_() {}
  ~ObTxTableGuard() { reset(); }

  ObTxTableGuard(const ObTxTableGuard &guard) : ObTxTableGuard() { *this = guard; }

  ObTxTableGuard &operator=(const ObTxTableGuard &guard)
  {
    reset();
    tx_table_ = guard.tx_table_;
    epoch_ = guard.epoch_;
    return *this;
  }

  void reset()
  {
    if (OB_NOT_NULL(tx_table_)) {
      tx_table_ = nullptr;
      epoch_ = -1;
    }
    mini_cache_.reset();
  }

  int init(ObTxTable *tx_table);
  bool is_valid() const { return nullptr != tx_table_; }

  ObTxTable *get_tx_table() const { return tx_table_; }

  share::ObLSID get_ls_id() const;

  int64_t get_epoch() const { return epoch_; }

  ObTxDataMiniCache &get_mini_cache() { return mini_cache_; }

public: // dalegate functions
  int check_with_tx_data(ObReadTxDataArg &read_tx_data_arg,
                         ObITxDataCheckFunctor &fn);


  int load_tx_op(const transaction::ObTransID &tx_id, ObTxData &tx_data);


  int get_tx_state_with_scn(const transaction::ObTransID tx_id,
                            const share::SCN scn,
                            int64_t &state,
                            share::SCN &trans_version);

  int try_get_tx_state(const transaction::ObTransID tx_id,
                       int64_t &state,
                       share::SCN &trans_version,
                       share::SCN &recycled_scn);


  int lock_for_read(const transaction::ObLockForReadArg &lock_for_read_arg,
                    bool &can_read,
                    share::SCN &trans_version,
                    ObCleanoutOp &cleanout_op,
                    ObReCheckOp &recheck_op);

  int cleanout_tx_node(const transaction::ObTransID tx_id,
                       memtable::ObMvccRow &value,
                       memtable::ObMvccTransNode &tnode,
                       const bool need_row_latch);

  int get_recycle_scn(share::SCN &recycle_scn);

  int self_freeze_task();

  bool check_ls_offline();

  void reuse() { mini_cache_.reset(); }

  TO_STRING_KV(KP_(tx_table), K_(epoch), K(mini_cache_));

private:
  ObTxTable *tx_table_;
  int64_t epoch_;
  ObTxDataMiniCache mini_cache_;
};

}  // namespace storage
}  // namespace oceanbase

#endif
