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
#include "share/ob_server_struct.h"

namespace oceanbase
{
using namespace common;
using namespace share;
using namespace sql;
using namespace storage;
using namespace memtable;

namespace transaction
{
OB_SERIALIZE_MEMBER(ObStartTransParam, access_mode_, type_, isolation_, consistency_type_,
                    is_inner_trans_, read_snapshot_type_);
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
                  "consistency_type=%d(%s), read_snapshot_type=%d(%s), "
                  "is_inner_trans=%d]",
                  access_mode_, type_, isolation_, magic_, autocommit_,
                  consistency_type_, ObTransConsistencyType::cstr(consistency_type_),
                  read_snapshot_type_, ObTransReadSnapshotType::cstr(read_snapshot_type_),
                  is_inner_trans_);
  return pos;
}


void ObTraceInfo::reset()
{
  app_trace_id_.set_length(0);
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

int TxBufferNodeArrayHolder::ensure(ObTxBufferNodeArray *&array)
{
  int ret = OB_SUCCESS;
  if (!array_) {
    ObTxBufferNodeArray *new_array = nullptr;
    if (nullptr == allocator_) {
      new_array = new (std::nothrow) ObTxBufferNodeArray();
    } else {
      new_array = new (std::nothrow) ObTxBufferNodeArray(
          OB_MALLOC_NORMAL_BLOCK_SIZE, ModulePageAllocator(*allocator_, "MDS_ARRAY"));
    }
    if (OB_ISNULL(new_array)) {
      ret = OB_ALLOCATE_MEMORY_FAILED;
      TRANS_LOG(WARN, "allocate multi data source array failed", K(ret));
    } else {
      array_.reset(new_array);
    }
  }
  array = array_.get();
  return ret;
}

ObTxBufferNodeArray &TxBufferNodeArrayHolder::get_array()
{
  static ObTxBufferNodeArray EMPTY_ARRAY;
  return array_ ? *array_ : EMPTY_ARRAY;
}

const ObTxBufferNodeArray &TxBufferNodeArrayHolder::get_array() const
{
  static const ObTxBufferNodeArray EMPTY_ARRAY;
  return array_ ? *array_ : EMPTY_ARRAY;
}

int TxBufferNodeArrayHolder::assign(const TxBufferNodeArrayHolder &other)
{
  int ret = OB_SUCCESS;
  if (other.empty()) {
    reset();
  } else {
    ObTxBufferNodeArray *array = nullptr;
    if (OB_FAIL(ensure(array))) {
    } else if (OB_FAIL(array->assign(other.get_array()))) {
    }
  }
  return ret;
}

int TxBufferNodeArrayHolder::reserve(const int64_t capacity)
{
  int ret = OB_SUCCESS;
  ObTxBufferNodeArray *array = nullptr;
  if (capacity <= 0) {
  } else if (OB_FAIL(ensure(array))) {
  } else if (OB_FAIL(array->reserve(capacity))) {
  }
  return ret;
}

int TxBufferNodeArrayHolder::push_back(const ObTxBufferNode &node)
{
  int ret = OB_SUCCESS;
  ObTxBufferNodeArray *array = nullptr;
  if (OB_FAIL(ensure(array))) {
  } else if (OB_FAIL(array->push_back(node))) {
  }
  return ret;
}

int TxBufferNodeArrayHolder::remove(const int64_t idx)
{
  int ret = OB_SUCCESS;
  if (OB_ISNULL(array_)) {
    ret = OB_ARRAY_OUT_OF_RANGE;
  } else if (OB_FAIL(array_->remove(idx))) {
  } else if (array_->empty()) {
    reset();
  }
  return ret;
}

int TxBufferNodeArrayHolder::serialize(char *buf, const int64_t buf_len, int64_t &pos) const
{
  return get_array().serialize(buf, buf_len, pos);
}

int TxBufferNodeArrayHolder::deserialize(
    const char *buf, const int64_t data_len, int64_t &pos)
{
  int ret = OB_SUCCESS;
  int64_t count = 0;
  int64_t tmp_pos = pos;
  reset();
  if (OB_FAIL(serialization::decode_vi64(buf, data_len, tmp_pos, &count))) {
  } else if (0 == count) {
    pos = tmp_pos;
  } else {
    ObTxBufferNodeArray *array = nullptr;
    if (OB_FAIL(ensure(array))) {
    } else if (OB_FAIL(array->deserialize(buf, data_len, pos))) {
      reset();
    }
  }
  return ret;
}

int64_t TxBufferNodeArrayHolder::get_serialize_size() const
{
  return get_array().get_serialize_size();
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
  //touched_pkeys_.reset();
  multi_data_source_.reset();
  need_checksum_ = true;
  serial_final_scn_.reset();
  serial_final_seq_no_.reset();
}

void ObTxExecInfo::destroy(ObTxMDSCache &mds_cache)
{
  for (int64_t i = 0; i < multi_data_source_.count(); ++i) {
    ObTxBufferNode &node = multi_data_source_.at(i);
    if (nullptr != node.data_.ptr()) {
      mds_cache.free_mds_node(node.data_, node.get_register_no());
      // share::server_free(node.data_.ptr());
      node.buffer_ctx_node_.destroy_ctx();
    }
  }
  reset();
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
  } else if (OB_FAIL(multi_data_source_.assign(exec_info.multi_data_source_))) {
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
    } else if (OB_FAIL(checksum_scn_.assign(exec_info.checksum_scn_))) {
    }
    max_durable_lsn_ = exec_info.max_durable_lsn_;
    data_complete_ = exec_info.data_complete_;
    need_checksum_ = exec_info.need_checksum_;
    serial_final_scn_ = exec_info.serial_final_scn_;
    serial_final_seq_no_ = exec_info.serial_final_seq_no_;
  }
  return ret;
}

OB_DEF_SERIALIZE(ObTxExecInfo)
{
  int ret = OB_SUCCESS;
  LST_DO_CODE(OB_UNIS_ENCODE,
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
              need_checksum_);
  if (OB_SUCC(ret) && OB_FAIL(serialization::encode_vi64(
          buf, buf_len, pos, multi_data_source_.count()))) {
    TRANS_LOG(WARN, "encode mds buffer ctx count failed", K(ret), K(pos), K(buf_len));
  }
  for (int64_t i = 0; OB_SUCC(ret) && i < multi_data_source_.count(); ++i) {
    if (OB_FAIL(multi_data_source_[i].get_buffer_ctx_node().serialize(buf, buf_len, pos))) {
      TRANS_LOG(WARN, "encode mds buffer ctx failed", K(ret), K(i), K(pos), K(buf_len));
    }
  }
  LST_DO_CODE(OB_UNIS_ENCODE,
              checksum_,
              checksum_scn_,
              serial_final_scn_,
              serial_final_seq_no_);
  return ret;
}

OB_DEF_DESERIALIZE(ObTxExecInfo)
{
  int ret = OB_SUCCESS;
  int64_t buffer_ctx_count = 0;
  int64_t decoded_buffer_ctx_count = 0;
  clear_buffer_ctx_in_multi_data_source();
  LST_DO_CODE(OB_UNIS_DECODE,
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
              need_checksum_);
  if (OB_SUCC(ret) && OB_FAIL(serialization::decode_vi64(
          buf, data_len, pos, &buffer_ctx_count))) {
    TRANS_LOG(WARN, "decode mds buffer ctx count failed", K(ret), K(pos), K(data_len));
  } else if (OB_SUCC(ret) && buffer_ctx_count != multi_data_source_.count()) {
    ret = OB_ERR_UNEXPECTED;
    TRANS_LOG(ERROR, "mds buffer ctx count does not match multi data source count",
              K(ret), K(buffer_ctx_count), "mds_count", multi_data_source_.count());
  }
  for (; OB_SUCC(ret) && decoded_buffer_ctx_count < buffer_ctx_count;
       ++decoded_buffer_ctx_count) {
    if (OB_FAIL(multi_data_source_[decoded_buffer_ctx_count]
                    .get_buffer_ctx_node().deserialize(buf, data_len, pos))) {
      TRANS_LOG(WARN, "decode mds buffer ctx failed",
                K(ret), K(decoded_buffer_ctx_count), K(pos), K(data_len));
    }
  }
  LST_DO_CODE(OB_UNIS_DECODE,
              checksum_,
              checksum_scn_,
              serial_final_scn_,
              serial_final_seq_no_);
  if (OB_FAIL(ret)) {
    for (int64_t i = 0; i < decoded_buffer_ctx_count; ++i) {
      multi_data_source_[i].get_buffer_ctx_node().destroy_ctx();
    }
  }
  return ret;
}

OB_DEF_SERIALIZE_SIZE(ObTxExecInfo)
{
  int64_t len = 0;
  LST_DO_CODE(OB_UNIS_ADD_LEN,
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
              need_checksum_);
  len += serialization::encoded_length_vi64(multi_data_source_.count());
  for (int64_t i = 0; i < multi_data_source_.count(); ++i) {
    len += multi_data_source_[i].get_buffer_ctx_node().get_serialize_size();
  }
  LST_DO_CODE(OB_UNIS_ADD_LEN,
              checksum_,
              checksum_scn_,
              serial_final_scn_,
              serial_final_seq_no_);
  return len;
}

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
