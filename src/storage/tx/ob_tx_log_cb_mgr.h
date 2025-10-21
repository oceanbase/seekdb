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

#ifndef OCEANBASE_TRANSACTION_OB_TX_LOG_CB_MGR_HEADER
#define OCEANBASE_TRANSACTION_OB_TX_LOG_CB_MGR_HEADER

#include "storage/tx/ob_tx_log_cb_define.h"

namespace oceanbase
{

namespace transaction
{

#define MAX_SYNC_SIZE_HISTORY_RECORD_SIZE 10
// #define MAX_SYNC_SIZE_HISTORY_ARRAY_SIZE = MAX_SYNC_SIZE_HISTORY_RECORD_SIZE * 2;

class ObPartTransCtx;

typedef common::ObDList<ObTxLogCbPool> TxLogCbPoolList;

class ObTxLogCbPoolMgr
{
public:
  static const int64_t DEFAULT_LOG_CB_POOL_CNT = 4;

public:
  ObTxLogCbPoolMgr() : allocator_("TxLogCbPool", common::OB_SERVER_TENANT_ID) { reset(); }

  int init(const int64_t tenant_id, const ObLSID ls_id);
  void reset();
  void destroy();

  // release in switch_to_follower or replay
  int clear_log_cb_pool(const bool for_replay);
  int switch_to_leader(const int64_t active_tx_cnt);

  int adjust_log_cb_pool(const int64_t active_tx_cnt);

  int acquire_idle_log_cb_group(ObTxLogCbGroup *&group_ptr, ObPartTransCtx *tx_ctx);
  
  void dec_ls_occupying_cnt() {ATOMIC_DEC(&ls_occupying_cnt_);} 
  bool is_all_busy();

  TO_STRING_KV(K(ls_id_),
               K(is_inited_),
               K(allow_expand_),
               K(pool_list_.get_size()),
               KP(idle_pool_ptr_));

private:
  int append_new_log_cb_pool_();

  int alloc_log_cb_pool_(ObTxLogCbPool *&alloc_ptr);
  int free_log_cb_pool_(ObTxLogCbPool *free_ptr, const int64_t wait_timeout = INT64_MAX);

  int iter_idle_pool_(ObTxLogCbPoolRefGuard &ref_guard);

  enum SyncSizeHistoryFlag
  {
    UNKNOWN = 0,
    NO_CHANGE = -1,
    EXPAND = -2,
    SHRINK = -3
  };

  static const char *sync_size_his_to_str(const int64_t flag)
  {
    const char *tmp_str;
    switch (flag) {
    case NO_CHANGE:
      tmp_str = "NO_CHANGE";
      break;
    case EXPAND:
      tmp_str = "EXPAND";
      break;
    case SHRINK:
      tmp_str = "SHRINK";
      break;
    default:
      tmp_str = "UNKNOWN";
    }
    return tmp_str;
  }

  int check_sync_size_increased_(int64_t &expand_cnt, int64_t &sync_size_increased_cnt);
  int push_back_sync_size_history_(const int64_t sync_size, SyncSizeHistoryFlag his_flag);
  int print_sync_size_history_();
  void clear_sync_size_history_();


private:
  bool is_inited_;
  ObLSID ls_id_;
  TransModulePageAllocator allocator_;

  common::SpinRWLock pool_list_rw_lock_;
  TxLogCbPoolList pool_list_;
  bool allow_expand_;

  common::SpinRWLock sync_size_his_lock_;
  int64_t sync_size_history_[MAX_SYNC_SIZE_HISTORY_RECORD_SIZE * 2];

  int64_t ls_occupying_cnt_; 
  int64_t acquire_extra_log_cb_group_failed_cnt_; 

  // modified by the read-only lock
  ObTxLogCbPool *idle_pool_ptr_;
};

} // namespace transaction

} // namespace oceanbase

#endif
