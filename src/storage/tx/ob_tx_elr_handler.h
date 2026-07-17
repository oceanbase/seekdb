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

#ifndef OCEANBASE_TX_ELR_HANDLER_
#define OCEANBASE_TX_ELR_HANDLER_

#include "ob_trans_define.h"

namespace oceanbase
{
namespace memtable
{
class ObMemtableCtx;
}

namespace transaction
{
class ObPartTransCtx;

enum TxELRState
{
  // Initialize state
  ELR_INIT = 0,
  // Single partition and single machine multi-partition transactions, the log has been successfully submitted to clog, indicating elr preparing
  ELR_PREPARING = 1,
  //gts's timestamp has been advanced beyond global trans version
  ELR_PREPARED = 2
};

class ObTxELRHandler
{
public:
  ObTxELRHandler() : elr_prepared_state_(ELR_INIT), mt_ctx_(NULL) {}
  void reset();

  int check_and_early_lock_release(bool row_updated, ObPartTransCtx *ctx);
  void set_memtable_ctx(memtable::ObMemtableCtx *mt_ctx) { mt_ctx_ = mt_ctx; }
  memtable::ObMemtableCtx *get_memtable_ctx() const { return mt_ctx_; }

  // elr state
  void set_elr_prepared() { ATOMIC_STORE(&elr_prepared_state_, TxELRState::ELR_PREPARED); }
  bool is_elr_prepared() const { return TxELRState::ELR_PREPARED == ATOMIC_LOAD(&elr_prepared_state_); }
  void reset_elr_state() { ATOMIC_STORE(&elr_prepared_state_, TxELRState::ELR_INIT); }
  TO_STRING_KV(K_(elr_prepared_state), KP_(mt_ctx));
private:
  // whether it is ready for elr
  TxELRState elr_prepared_state_;
  memtable::ObMemtableCtx *mt_ctx_;
};

} // transaction
} // oceanbase

#endif
