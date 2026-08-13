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

#include "ob_tx_stat.h"

namespace oceanbase
{
using namespace common;
using namespace share;

namespace transaction
{

void ObTxStat::reset()
{
  is_inited_ = false;
  addr_.reset();
  tx_id_.reset();
  has_decided_ = false;
  has_write_state_ = false;
  tx_ctx_create_time_ = -1;
  tx_expired_time_ = -1;
  ref_cnt_ = -1;
  last_op_sn_ = 0;
  pending_write_ = 0;
  state_ = static_cast<int64_t>(ObTxState::UNKNOWN);
  part_tx_action_ = ObPartTransAction::UNKNOWN;
  tx_ctx_addr_ = (void*)0;
  pending_log_size_ = 0;
  flushed_log_size_ = 0;
  session_id_ = 0;
  is_exiting_ = false;
  last_request_ts_ = OB_INVALID_TIMESTAMP;
  busy_cbs_cnt_ = 0;
  replay_completeness_ = -1;
  serial_final_scn_.reset();
  callback_list_stats_.reset();
}
int ObTxStat::init(const common::ObAddr &addr, const ObTransID &tx_id,  const bool has_decided,
                   const bool has_write_state,
                   const int64_t tx_ctx_create_time, const int64_t tx_expired_time,
                   const int64_t ref_cnt, const int64_t last_op_sn,
                   const int64_t pending_write, const int64_t state,
                   const int64_t part_tx_action,
                   const void* const tx_ctx_addr,
                   const int64_t pending_log_size, const int64_t flushed_log_size,
                   const int64_t session_id,
                   const bool is_exiting,
                   const int64_t last_request_ts,
                   SCN start_scn, SCN end_scn, SCN rec_scn,
                   const int busy_cbs_cnt,
                   int replay_completeness,
                   share::SCN serial_final_scn)
{
  int ret = OB_SUCCESS;
  if (is_inited_) {
    TRANS_LOG(WARN, "ObTxStat init twice");
    ret = OB_INIT_TWICE;
  } else {
    is_inited_ = true;
    addr_ = addr;
    tx_id_ = tx_id;
    has_decided_ = has_decided;
    has_write_state_ = has_write_state;
    tx_ctx_create_time_ = tx_ctx_create_time;
    tx_expired_time_ = tx_expired_time;
    ref_cnt_ = ref_cnt;
    last_op_sn_ = last_op_sn;
    pending_write_ = pending_write;
    state_ = state;
    part_tx_action_ = part_tx_action;
    tx_ctx_addr_ = tx_ctx_addr;
    pending_log_size_ = pending_log_size;
    flushed_log_size_ = flushed_log_size;
    session_id_ = session_id;
    is_exiting_ = is_exiting;
    last_request_ts_ = last_request_ts;
    start_scn_ = start_scn;
    end_scn_ = end_scn;
    rec_scn_ = rec_scn;
    busy_cbs_cnt_ = busy_cbs_cnt;
    replay_completeness_ = replay_completeness;
    serial_final_scn_ = serial_final_scn;
  }
  return ret;
}

int ObTxLockStat::init(const common::ObAddr &addr,
                      const ObMemtableKeyInfo &memtable_key_info,
                      uint32_t session_id,
                      const ObTransID &tx_id,
                      int64_t tx_ctx_create_time,
                      int64_t tx_expired_time)
{
  int ret = OB_SUCCESS;

  if (is_inited_) {
    TRANS_LOG(WARN, "ObTxLockStat init twice");
    ret = OB_INIT_TWICE;
  } else {
    is_inited_ = true;
    addr_ = addr;
    memtable_key_info_ = memtable_key_info;
    session_id_ = session_id;
    tx_id_ = tx_id;
    tx_ctx_create_time_ = tx_ctx_create_time;
    tx_expired_time_ = tx_expired_time;
  }

  return ret;
}

void ObTxLockStat::reset()
{
  is_inited_ = false;
  addr_.reset();
  memtable_key_info_.reset();
  session_id_ = 0;
  tx_id_.reset();
  tx_ctx_create_time_ = 0;
  tx_expired_time_ = 0;
}

int ObTxSchedulerStat::init(const common::ObAddr &addr,
                            const uint32_t sess_id,
                            const ObTransID &tx_id,
                            const int64_t state,
                            const bool has_write_state,
                            const ObTxWriteState &write_state,
                            const ObTxIsolationLevel &isolation,
                            const share::SCN &snapshot_version,
                            const ObTxAccessMode &access_mode,
                            const uint64_t op_sn,
                            const uint64_t flag,
                            const int64_t active_ts,
                            const int64_t expire_ts,
                            const int64_t timeout_us,
                            const int32_t ref_cnt,
                            const void* const tx_desc_addr,
                            const ObTxSavePointList &savepoints,
                            const int16_t abort_cause,
                            const bool can_elr)
{
  int ret = OB_SUCCESS;
  if (is_inited_) {
    TRANS_LOG(WARN, "ObTxSchedulerStat init twice");
    ret = OB_INIT_TWICE;
  } else if (OB_FAIL(get_valid_savepoints(savepoints))) {
  } else {
    is_inited_ = true;
    addr_ = addr;
    sess_id_ = sess_id;
    tx_id_ = tx_id;
    state_ = state;
    has_write_state_ = has_write_state;
    if (has_write_state_) {
      write_state_ = write_state;
    }
    isolation_ = isolation;
    snapshot_version_ = snapshot_version;
    access_mode_ = access_mode;
    op_sn_ = op_sn;
    flag_ = flag;
    active_ts_ = active_ts;
    expire_ts_ = expire_ts;
    timeout_us_ = timeout_us;
    ref_cnt_ = ref_cnt;
    tx_desc_addr_ = tx_desc_addr;
    abort_cause_ = abort_cause;
    can_elr_ = can_elr;
  }
  return ret;
}

void ObTxSchedulerStat::reset()
{
  is_inited_ = false;
  addr_.reset();
  sess_id_ = 0;
  tx_id_.reset();
  state_ = 0;
  has_write_state_ = false;
  write_state_ = ObTxWriteState();
  isolation_ = ObTxIsolationLevel::INVALID;
  snapshot_version_.reset();
  access_mode_ = ObTxAccessMode::INVL;
  op_sn_ = -1;
  flag_ = 0;
  active_ts_ = -1;
  expire_ts_ = -1;
  timeout_us_ = -1;
  ref_cnt_ = -1;
  tx_desc_addr_ = (void*)0;
  savepoints_.reset();
  abort_cause_ = 0;
  can_elr_ = false;
}

int64_t ObTxSchedulerStat::get_parts_str(char* buf, const int64_t buf_len)
{
  int64_t pos = 0;
  J_OBJ_START();
  J_KV("write_state", write_state_);
  J_OBJ_END();
  return pos;
}

int ObTxSchedulerStat::get_valid_savepoints(const ObTxSavePointList &savepoints)
{
  int ret = OB_SUCCESS;
  for (int i = 0; OB_SUCC(ret) && i < savepoints.count(); i++) {
    if (savepoints.at(i).is_savepoint()) {
      if (OB_FAIL(savepoints_.push_back(savepoints.at(i)))) {
      }
    }
  }
  return ret;
}

} // transaction
} // oceanbase
