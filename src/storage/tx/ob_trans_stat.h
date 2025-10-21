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

#ifndef OCEANBASE_TRANSACTION_OB_TRANS_STAT_
#define OCEANBASE_TRANSACTION_OB_TRANS_STAT_

#include "ob_trans_define.h"
#include "common/ob_range.h"

namespace oceanbase
{
namespace transaction
{
class ObTransStat
{
public:
  ObTransStat() { reset(); }
  ~ObTransStat() { }
  void reset();
  common::ObAddr &get_addr() { return addr_; }
  ObTransID &get_trans_id() { return trans_id_; }
  uint64_t get_tenant_id() const { return tenant_id_; }
  bool is_exiting() const { return is_exiting_; }
  bool is_readonly() const { return is_readonly_; }
  bool has_decided() const { return has_decided_; }
  bool is_dirty() const { return is_dirty_; }
  common::ObVersion &get_active_memstore_version() { return active_memstore_version_; }
  uint32_t get_session_id() { return session_id_; }
  uint64_t get_proxy_session_id() { return proxy_session_id_; }
  ObStartTransParam &get_trans_param() { return trans_param_; }
  int64_t get_ctx_create_time() const { return ctx_create_time_; }
  int64_t get_trans_expired_time() const { return expired_time_; }
  int64_t get_trans_refer_cnt() const { return refer_; }
  int64_t get_sql_no() const { return sql_no_; }
  int64_t get_state() const { return state_; }
  int get_trans_type() const { return trans_type_; }
  int64_t get_part_trans_action() const { return part_trans_action_; }
  uint64_t get_lock_for_read_retry_count() const { return lock_for_read_retry_count_; }
  int64_t get_ctx_addr() const { return ctx_addr_; }
  ObElrTransInfoArray &get_prev_trans_arr() { return prev_trans_arr_; }
  ObElrTransInfoArray &get_next_trans_arr() { return next_trans_arr_; }
  int32_t get_prev_trans_commit_count() const { return prev_trans_commit_count_; }
  uint32_t get_ctx_id() const { return ctx_id_; }
  int64_t get_pending_log_size() const { return pending_log_size_; }
  int64_t get_flushed_log_size() const { return flushed_log_size_; }

  TO_STRING_KV(K_(addr), K_(trans_id), K_(tenant_id), K_(is_exiting), K_(is_readonly),
      K_(has_decided), K_(is_dirty), K_(active_memstore_version),
      K_(trans_param), K_(ctx_create_time), K_(expired_time), K_(refer),
      K_(sql_no), K_(state), K_(session_id), K_(proxy_session_id), K_(trans_type),
      K_(ctx_addr), K_(prev_trans_arr), K_(next_trans_arr), K_(prev_trans_commit_count), K_(ctx_id),
      K_(pending_log_size), K_(flushed_log_size));
private:
  bool is_inited_;
  common::ObAddr addr_;
  ObTransID trans_id_;
  uint64_t tenant_id_;
  bool is_exiting_;
  bool is_readonly_;
  bool has_decided_;
  bool is_dirty_;
  common::ObVersion active_memstore_version_;
  ObStartTransParam trans_param_;
  int64_t ctx_create_time_;
  int64_t expired_time_;
  int64_t refer_;
  int64_t sql_no_;
  int64_t state_;
  uint32_t session_id_;
  uint64_t proxy_session_id_;
  //SP_TRANS,MINI_SP_TRANS,DIST_TRANS
  int trans_type_;
  int64_t part_trans_action_;
  uint64_t lock_for_read_retry_count_;
  int64_t ctx_addr_;
  ObElrTransInfoArray prev_trans_arr_;
  ObElrTransInfoArray next_trans_arr_;
  // < 0 if has aborted transaction
  int32_t prev_trans_commit_count_;
  uint32_t ctx_id_;
  int64_t pending_log_size_;
  int64_t flushed_log_size_;
};

class ObTransLockStat
{
public:
  ObTransLockStat() { reset(); }
  ~ObTransLockStat() {}
  void reset();
  const common::ObAddr &get_addr() const { return addr_; }
  uint64_t get_tenant_id() const { return tenant_id_; }
  const ObMemtableKeyInfo &get_memtable_key() const { return memtable_key_; }
  uint32_t get_session_id() const { return session_id_; }
  uint64_t get_proxy_session_id() const { return proxy_session_id_; }
  const ObTransID &get_trans_id() const { return trans_id_; }
  int64_t get_ctx_create_time() const { return ctx_create_time_; }
  int64_t get_trans_expired_time() const { return expired_time_; }

  TO_STRING_KV(K_(addr),
               K_(tenant_id),
               K_(memtable_key),
               K_(session_id),
               K_(proxy_session_id),
               K_(trans_id),
               K_(ctx_create_time),
               K_(expired_time));

private:
  bool is_inited_;
  common::ObAddr addr_;
  uint64_t tenant_id_;
  ObMemtableKeyInfo memtable_key_;
  uint32_t session_id_;
  uint64_t proxy_session_id_;
  ObTransID trans_id_;
  int64_t ctx_create_time_;
  int64_t expired_time_;
};

} // transaction
} // oceanbase
#endif // OCEANABAE_TRANSACTION_OB_TRANS_STAT_
