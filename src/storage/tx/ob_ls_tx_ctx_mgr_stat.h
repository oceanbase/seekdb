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

#ifndef OCEANBASE_TRANSACTION_OB_LS_TX_CTX_MGR_STAT_
#define OCEANBASE_TRANSACTION_OB_LS_TX_CTX_MGR_STAT_

#include "ob_trans_define.h"
#include "common/ob_range.h"

namespace oceanbase
{
namespace transaction
{
class ObLSTxCtxMgrStat
{
public:
  ObLSTxCtxMgrStat() { reset(); }
  virtual ~ObLSTxCtxMgrStat() { }
  void reset();
  int init(const common::ObAddr &addr,
      const bool is_stopped, const bool block_tx,
      const bool block_normal_tx, const bool block_all,
      const int64_t total_tx_ctx_count, const int64_t mgr_addr);

  const common::ObAddr &get_addr() const { return addr_; }
  bool is_stopped() const { return is_stopped_; }
  bool is_tx_blocked() const { return block_tx_; }
  bool is_normal_tx_blocked() const { return block_normal_tx_; }
  bool is_all_blocked() const { return block_all_; }
  int64_t get_total_tx_ctx_count() const { return total_tx_ctx_count_; }
  int64_t get_mgr_addr() const { return mgr_addr_; }

  TO_STRING_KV(K_(addr), K_(is_stopped), K_(block_tx),
      K_(block_normal_tx), K_(block_all),
      K_(total_tx_ctx_count), K_(mgr_addr));

private:
  common::ObAddr addr_;
  bool is_stopped_;
  bool block_tx_;
  bool block_normal_tx_;
  bool block_all_;
  int64_t total_tx_ctx_count_;
  int64_t mgr_addr_;
};

} // transaction
} // oceanbase

#endif // OCEANBASE_TRANSACTION_OB_LS_TX_CTX_MGR_STAT_
