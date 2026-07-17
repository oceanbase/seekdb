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

#define USING_LOG_PREFIX TRANS

#include "ob_trans_define.h"
#include "observer/ob_server.h"

namespace oceanbase
{
using namespace common;
using namespace share;
using namespace sql;
using namespace storage;
using namespace memtable;
using namespace observer;

namespace transaction
{
int ObTransID::compare(const ObTransID& other) const
{
  int compare_ret = 0;
  if (this == &other) {
    compare_ret = 0;
  } else if (tx_id_ != other.tx_id_) {
    // iterate transaction ctx sequentially
    compare_ret = tx_id_ > other.tx_id_ ? 1 : -1;
  } else {
    compare_ret = 0;
  }
  return compare_ret;
}

OB_SERIALIZE_MEMBER(ObTransID, tx_id_);
OB_SERIALIZE_MEMBER(ObStartTransParam, access_mode_, type_, isolation_, consistency_type_,
                    cluster_version_, is_inner_trans_, read_snapshot_type_);
OB_SERIALIZE_MEMBER(ObElrTransInfo, trans_id_, commit_version_, result_);
OB_SERIALIZE_MEMBER(ObTransDesc, a_);

// class ObStartTransParam
void ObStartTransParam::reset()
{
  access_mode_ = ObTransAccessMode::UNKNOWN;
  type_ = ObTransType::UNKNOWN;
  isolation_ = ObTransIsolation::UNKNOWN;
  magic_ = 0xF0F0F0F0F0F0F0F0;
  autocommit_ = false;
  consistency_type_ = ObTransConsistencyType::CURRENT_READ;
  read_snapshot_type_ = ObTransReadSnapshotType::STATEMENT_SNAPSHOT;
  cluster_version_ = ObStartTransParam::INVALID_CLUSTER_VERSION;
  is_inner_trans_ = false;
}





bool ObStartTransParam::is_serializable_isolation() const
{
  return ObTransIsolation::SERIALIZABLE == isolation_
    || ObTransIsolation::REPEATABLE_READ == isolation_;
}

int64_t ObStartTransParam::to_string(char *buf, const int64_t buf_len) const
{
  int64_t pos = 0;
  databuff_printf(buf, buf_len, pos,
                  "[access_mode=%d, type=%d, isolation=%d, magic=%lu, autocommit=%d, "
                  "consistency_type=%d(%s), read_snapshot_type=%d(%s), cluster_version=%lu, "
                  "is_inner_trans=%d]",
                  access_mode_, type_, isolation_, magic_, autocommit_,
                  consistency_type_, ObTransConsistencyType::cstr(consistency_type_),
                  read_snapshot_type_, ObTransReadSnapshotType::cstr(read_snapshot_type_),
                  cluster_version_, is_inner_trans_);
  return pos;
}


void ObTraceInfo::reset()
{
  if (app_trace_info_.length() >= MAX_TRACE_INFO_BUFFER) {
    void *buf = app_trace_info_.ptr();
    if (NULL != buf &&
        buf != &app_trace_info_) {
      ob_free(buf);
      buf = NULL;
    }
    app_trace_info_.assign_buffer(app_trace_info_buffer_, sizeof(app_trace_info_buffer_));
  } else {
    app_trace_info_.set_length(0);
  }
  app_trace_id_.set_length(0);
}

int ObTraceInfo::set_app_trace_info(const ObString &app_trace_info)
{
  const int64_t len = app_trace_info.length();
  int ret = OB_SUCCESS;

  if (len < 0 || len > OB_MAX_TRACE_INFO_BUFFER_SIZE) {
    TRANS_LOG(WARN, "unexpected trace info str", K(app_trace_info));
    ret = OB_INVALID_ARGUMENT;
  } else if (0 != app_trace_info_.length()) {
    ret = OB_ERR_UNEXPECTED;
    TRANS_LOG(ERROR, "different app trace info", K(ret), K(app_trace_info_), K(app_trace_info));
  } else if (len < MAX_TRACE_INFO_BUFFER) {
    (void)app_trace_info_.write(app_trace_info.ptr(), len);
    app_trace_info_buffer_[len] = '\0';
  } else {
    char *buf = NULL;
    if (NULL == (buf = (char *)ob_malloc(len+1, "AppTraceInfo"))) {
      ret = OB_ALLOCATE_MEMORY_FAILED;
      TRANS_LOG(WARN, "allocate memory for app trace info failed", K(ret), K(app_trace_info));
    } else {
      app_trace_info_.reset();
      (void)app_trace_info_.assign_buffer(buf, len+1);
      (void)app_trace_info_.write(app_trace_info.ptr(), len);
      buf[len] = '\0';
    }
  }

  return ret;
}

int ObTraceInfo::set_app_trace_id(const ObString &app_trace_id)
{
  int ret = OB_SUCCESS;
  const int64_t len = app_trace_id.length();

  if (len < 0 || len > OB_MAX_TRACE_ID_BUFFER_SIZE) {
    TRANS_LOG(WARN, "unexpected trace id str", K(app_trace_id));
    ret = OB_INVALID_ARGUMENT;
  } else if (0 != app_trace_id_.length()) {
    ret = OB_ERR_UNEXPECTED;
    TRANS_LOG(ERROR, "different app trace id", K(ret), K(app_trace_id_), K(app_trace_id));
  } else {
    (void)app_trace_id_.write(app_trace_id.ptr(), len);
    app_trace_id_buffer_[len] = '\0';
  }

  return ret;
}

const ObString ObTransIsolation::LEVEL_NAME[ObTransIsolation::MAX_LEVEL] =
{
  "READ-UNCOMMITTED",
  "READ-COMMITTED",
  "REPEATABLE-READ",
  "SERIALIZABLE"
};

int32_t ObTransIsolation::get_level(const ObString &level_name)
{
  int32_t level = UNKNOWN;
  for (int32_t i = 0; i < MAX_LEVEL; i++) {
    if (0 == LEVEL_NAME[i].case_compare(level_name)) {
      level = i;
    }
  }
  return level;
}

const ObString &ObTransIsolation::get_name(int32_t level)
{
  static const ObString EMPTY_NAME;
  const ObString *level_name = &EMPTY_NAME;
  if (ObTransIsolation::UNKNOWN < level && level < ObTransIsolation::MAX_LEVEL) {
    level_name = &LEVEL_NAME[level];
  }
  return *level_name;
}

int ObMemtableKeyInfo::init(const uint64_t hash_val)
{
  int ret = OB_SUCCESS;

  if (hash_val == 0) {
    ret = OB_INVALID_ARGUMENT;
    TRANS_LOG(WARN, "memtable key info init fail", KR(ret), K(hash_val));
  } else {
    hash_val_ = hash_val;
  }

  return ret;
}

void ObMemtableKeyInfo::reset()
{
  hash_val_ = 0;
  row_lock_ = NULL;
  buf_[0] = '\0';
}


void ObElrTransInfo::reset()
{
  trans_id_.reset();
  commit_version_.reset();
  result_ = ObTransResultState::UNKNOWN;
  ctx_id_ = 0;
}


void ObTransTask::reset()
{
  retry_interval_us_ = 0;
  next_handle_ts_ = 0;
  task_type_ = ObTransRetryTaskType::UNKNOWN;
}

int ObTransTask::make(const int64_t task_type)
{
  int ret = OB_SUCCESS;

  if (!ObTransRetryTaskType::is_valid(task_type)) {
    TRANS_LOG(WARN, "invalid argument", K(task_type));
    ret = OB_INVALID_ARGUMENT;
  } else {
    task_type_ = task_type;
  }

  return ret;
}


bool ObTransTask::ready_to_handle()
{
  bool boot_ret = false;;
  int64_t current_ts = ObTimeUtility::current_time();

  if (current_ts >= next_handle_ts_) {
    boot_ret = true;
    next_handle_ts_ = current_ts + retry_interval_us_;
  } else {
    int64_t left_time = next_handle_ts_ - current_ts;
    if (left_time > RETRY_SLEEP_TIME_US) {
      ob_usleep(RETRY_SLEEP_TIME_US);
      boot_ret = false;
    } else {
      ob_usleep(left_time);
      boot_ret = true;
      next_handle_ts_ += retry_interval_us_;
    }
  }

  return boot_ret;
}






void ObCoreLocalPartitionAuditInfo::reset()
{
  if (NULL != val_array_) {
    for (int i = 0; i < array_len_; i++) {
      ObPartitionAuditInfoFactory::release(VAL_ARRAY_AT(ObPartitionAuditInfo*, i));
    }
    ob_free(val_array_);
    val_array_ = NULL;
  }
  core_num_ = 0;
  array_len_ = 0;
  is_inited_ = false;
}


void ObAddrLogId::reset()
{
  addr_.reset();
  log_id_ = 0;
}


int64_t ObTransNeedWaitWrap::get_remaining_wait_interval_us() const
{
  int64_t ret_val = 0;

  if (receive_gts_ts_ <= MonotonicTs(0)) {
    ret_val = 0;
  } else if (need_wait_interval_us_ <= 0) {
    ret_val = 0;
  } else {
    MonotonicTs tmp_ts = MonotonicTs(need_wait_interval_us_) - (MonotonicTs::current_time() - receive_gts_ts_);
    ret_val = tmp_ts.mts_;
    ret_val = ret_val > 0 ? ret_val : 0;
  }

  return ret_val;
}

void ObTransNeedWaitWrap::set_trans_need_wait_wrap(const MonotonicTs receive_gts_ts,
                                                   const int64_t need_wait_interval_us)
{
  if (need_wait_interval_us > 0) {
    receive_gts_ts_ = receive_gts_ts;
    need_wait_interval_us_ = need_wait_interval_us;
  }
}

OB_SERIALIZE_MEMBER(ObUndoAction, undo_from_, undo_to_);




DEF_TO_STRING(ObLockForReadArg)
{
  int64_t pos = 0;
  J_OBJ_START();
  J_KV(K(mvcc_acc_ctx_), K(data_trans_id_), K(data_sql_sequence_), K(read_latest_), K(read_uncommitted_), K(scn_));
  J_OBJ_END();
  return pos;
}

void ObTxExecInfo::reset()
{
  state_ = ObTxState::INIT;
  has_write_state_ = false;
  prev_record_lsn_.reset();
  redo_lsns_.reset();
  prepare_version_.reset();
  next_log_entry_no_ = 0;
  max_applied_log_ts_.reset();
  max_applying_log_ts_.reset();
  max_applying_part_log_no_ = INT64_MAX;
  max_submitted_seq_no_.reset();
  checksum_.reset();
  checksum_.push_back(0);
  checksum_scn_.reset();
  checksum_scn_.push_back(share::SCN::min_scn());
  max_durable_lsn_.reset();
  data_complete_ = false;
  is_dup_tx_ = false;
  //touched_pkeys_.reset();
  multi_data_source_.reset();
  xid_.reset();
  need_checksum_ = true;
  serial_final_scn_.reset();
  serial_final_seq_no_.reset();
  dli_batch_set_.destroy();
}

void ObTxExecInfo::destroy(ObTxMDSCache &mds_cache)
{
  if (!mds_buffer_ctx_array_.empty()) {
    TRANS_LOG_RET(WARN, OB_ERR_UNEXPECTED, "mds_buffer_ctx_array_ is valid when exec_info destroy",
                        K_(mds_buffer_ctx_array), K(*this));
    for (int64_t i = 0; i < mds_buffer_ctx_array_.count(); ++i) {
      mds_buffer_ctx_array_[i].destroy_ctx();
    }
  }
  for (int64_t i = 0; i < multi_data_source_.count(); ++i) {
    ObTxBufferNode &node = multi_data_source_.at(i);
    if (nullptr != node.data_.ptr()) {
      mds_cache.free_mds_node(node.data_, node.get_register_no());
      // share::mtl_free(node.data_.ptr());
      node.buffer_ctx_node_.destroy_ctx();
    }
  }
  reset();
}

int ObTxExecInfo::generate_mds_buffer_ctx_array()
{
  int ret = OB_SUCCESS;
  mds_buffer_ctx_array_.reset();
  for (int64_t idx = 0; idx < multi_data_source_.count() && OB_SUCC(ret); ++idx) {
    const ObTxBufferNode &buffer_node = multi_data_source_.at(idx);
    if (OB_FAIL(mds_buffer_ctx_array_.push_back(buffer_node.get_buffer_ctx_node()))) {
      TRANS_LOG(WARN, "fail to push back", KR(ret), K(*this));
    }
  }
  if (OB_FAIL(ret)) {
    mds_buffer_ctx_array_.reset();
  }
  TRANS_LOG(INFO, "generate mds buffer ctx array", KR(ret), K(multi_data_source_), K(mds_buffer_ctx_array_));
  return ret;
}

void ObTxExecInfo::mrege_buffer_ctx_array_to_multi_data_source() const
{
  ObTxBufferNodeArray &multi_data_source = const_cast<ObTxBufferNodeArray &>(multi_data_source_);
  ObTxBufferCtxArray &mds_buffer_ctx_array = const_cast<ObTxBufferCtxArray &>(mds_buffer_ctx_array_);
  TRANS_LOG_RET(INFO, OB_SUCCESS, "merge deserialized buffer ctx to multi_data_source", K(mds_buffer_ctx_array), K(multi_data_source));
  if (mds_buffer_ctx_array.count() != multi_data_source.count()) {
    if (mds_buffer_ctx_array.count() == 0) {// 4.1 -> 4.2 compat case
      TRANS_LOG_RET(WARN, OB_ERR_UNEXPECTED,
                    "mds buffer ctx array size not equal to multi data source array size"
                    ", destroy deserialized mds_buffer_ctx_array directly",
                    K(multi_data_source), K(mds_buffer_ctx_array), K(*this));
    } else {
      TRANS_LOG_RET(ERROR, OB_ERR_UNEXPECTED,
                    "mds buffer ctx array size not equal to multi data source array size"
                    ", destroy deserialized mds_buffer_ctx_array directly",
                    K(multi_data_source), K(mds_buffer_ctx_array), K(*this));
    }
    for (int64_t idx = 0; idx < mds_buffer_ctx_array.count(); ++idx) {
      mds_buffer_ctx_array[idx].destroy_ctx();
    }
  } else {
    for (int64_t idx = 0; idx < multi_data_source.count(); ++idx) {
      multi_data_source[idx].buffer_ctx_node_ = mds_buffer_ctx_array[idx];
    }
  }
  mds_buffer_ctx_array.reset();
}

void ObTxExecInfo::clear_buffer_ctx_in_multi_data_source()
{
  for (int64_t idx = 0; idx < multi_data_source_.count(); ++idx) {
    multi_data_source_[idx].buffer_ctx_node_.destroy_ctx();
  }
}

int ObTxExecInfo::assign(const ObTxExecInfo &exec_info)
{
  int ret = OB_SUCCESS;

  if (this == &exec_info) {
    ret = OB_ERR_UNEXPECTED;
    TRANS_LOG(ERROR, "no need to assign the same object", KR(ret), K(exec_info));
  } else if (OB_FAIL(redo_lsns_.assign(exec_info.redo_lsns_))) {
    TRANS_LOG(WARN, "redo_lsns assign error", KR(ret), K(exec_info));
  } else if (OB_FAIL(multi_data_source_.assign(exec_info.multi_data_source_))) {
    TRANS_LOG(WARN, "multi_data_source assign error", KR(ret), K(exec_info));
  } else if (OB_FAIL(mds_buffer_ctx_array_.assign(exec_info.mds_buffer_ctx_array_))) {
    TRANS_LOG(WARN, "mds_buffer_ctx_array assign error", KR(ret), K(exec_info));
  } else if (OB_FAIL(dli_batch_set_.assign(exec_info.dli_batch_set_))) {
    TRANS_LOG(WARN, "direct load inc batch set assign error", K(ret), K(exec_info.dli_batch_set_));
  } else {
    // Prepare version should be initialized before state_
    // for ObTransPartCtx::get_prepare_version_if_preapred();
    prepare_version_.atomic_store(exec_info.prepare_version_);
    state_ = exec_info.state_;
    has_write_state_ = exec_info.has_write_state_;
    prev_record_lsn_ = exec_info.prev_record_lsn_;
    next_log_entry_no_ = exec_info.next_log_entry_no_;
    max_applied_log_ts_ = exec_info.max_applied_log_ts_;
    max_applying_log_ts_ = exec_info.max_applying_log_ts_;
    max_applying_part_log_no_ = exec_info.max_applying_part_log_no_;
    max_submitted_seq_no_ = exec_info.max_submitted_seq_no_;
    if (OB_FAIL(checksum_.assign(exec_info.checksum_))) {
      TRANS_LOG(WARN, "assign failed", K(ret));
    } else if (OB_FAIL(checksum_scn_.assign(exec_info.checksum_scn_))) {
      TRANS_LOG(WARN, "assign failed", K(ret));
    }
    max_durable_lsn_ = exec_info.max_durable_lsn_;
    data_complete_ = exec_info.data_complete_;
    is_dup_tx_ = exec_info.is_dup_tx_;
    xid_ = exec_info.xid_;
    need_checksum_ = exec_info.need_checksum_;
    serial_final_scn_ = exec_info.serial_final_scn_;
    serial_final_seq_no_ = exec_info.serial_final_seq_no_;
  }
  return ret;
}

OB_SERIALIZE_MEMBER(ObTxExecInfo,
                    state_,
                    has_write_state_,
                    prev_record_lsn_,
                    redo_lsns_,
                    multi_data_source_,
                    prepare_version_,
                    next_log_entry_no_,
                    max_applying_log_ts_,
                    max_applied_log_ts_,
                    max_applying_part_log_no_,
                    max_submitted_seq_no_,
                    checksum_[0],       // FARM COMPAT WHITELIST
                    checksum_scn_[0],   // FARM COMPAT WHITELIST
                    max_durable_lsn_,
  data_complete_,
  is_dup_tx_,
//                    touched_pkeys_,
                    xid_,
                    need_checksum_,
                    mds_buffer_ctx_array_,
                    checksum_,
                    checksum_scn_,
                    serial_final_scn_,
                    serial_final_seq_no_,
                    dli_batch_set_  // FARM COMPAT WHITELIST
                    );

void ObMulSourceDataNotifyArg::reset()
{
  tx_id_.reset();
  scn_.reset();
  trans_version_.reset();
  for_replay_ = false;
  notify_type_ = NotifyType::ON_ABORT;
  redo_submitted_ = false;
  redo_synced_ = false;
  willing_to_commit_ = false;
  is_force_kill_ = false;
  is_incomplete_replay_ = false;
}





} // transaction
} // oceanbase
