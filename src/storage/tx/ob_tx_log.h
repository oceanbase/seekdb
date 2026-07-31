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

#ifndef OCEANBASE_TRANSACTION_OB_TX_LOG
#define OCEANBASE_TRANSACTION_OB_TX_LOG

#include <type_traits>
#include "storage/ddl/ob_ddl_clog.h"
#include "storage/tx/ob_trans_define.h"
#include "storage/tx/ob_tx_data_define.h"
#include "storage/tx/ob_tx_big_segment_buf.h"
#include "storage/tx/ob_trans_factory.h"
#include "share/ob_admin_dump_helper.h"
#include "share/log/ob_log_base_type.h"
#include "share/log/ob_log_base_header.h"
#include "share/log/palf/lsn.h"
#include "share/scn.h"
#include "lib/utility/ob_unify_serialize.h"
#include "storage/tablelock/ob_table_lock_common.h"
//#include <cstdint>

#define OB_TX_MDS_LOG_USE_BIT_SEGMENT_BUF

namespace oceanbase
{

namespace memtable
{
class ObMemtableMutatorIterator;
class ObMemtableMutatorRow;
class ObRowData;
}

namespace share
{
struct ObAdminMutatorStringArg;

}

namespace palf
{
class LSN;
}

namespace common
{
class ObDataBuffer;
} // namespace common

namespace storage
{
class ObLSDDLLogHandler;
}

namespace transaction
{

class ObTxLogCb;
class ObTxCtx;

typedef int64_t TxID;
typedef palf::LSN LogOffSet;

// Trans Log
enum class ObTxLogType : int64_t
{
  UNKNOWN = 0,
  TX_REDO_LOG = 0x1,
  TX_ROLLBACK_TO_LOG = 0x2,
  TX_MULTI_DATA_SOURCE_LOG = 0x4,
  TX_RECORD_LOG = 0x20,
  TX_COMMIT_INFO_LOG = 0x40,
  TX_COMMIT_LOG = 0x100,
  TX_ABORT_LOG = 0x200,
  TX_CLEAR_LOG = 0x400,

  // BigSegment log
  TX_BIG_SEGMENT_LOG = 0x200000,
  TX_LOG_TYPE_LIMIT
};

class ObTxCbArg
{
public:
  ObTxCbArg() : log_type_(ObTxLogType::UNKNOWN), arg_(NULL) {}
  ObTxCbArg(const ObTxLogType log_type, const void *arg) : log_type_(log_type), arg_(arg) {}
  ObTxLogType get_log_type() const { return log_type_; }
  const void *get_arg() const { return arg_; }
  TO_STRING_KV(KP_(log_type), KP_(arg));
private:
  ObTxLogType log_type_;
  const void *arg_;
};

typedef common::ObSEArray<ObTxCbArg, 3> ObTxCbArgArray;

inline bool is_contain(const ObTxCbArgArray &array, const ObTxLogType log_type)
{
  bool bool_ret = false;
  for (int64_t i = 0; i < array.count(); i++) {
    if ((array.at(i).get_log_type() == log_type)) {
      bool_ret = true;
      break;
    }
  }
  return bool_ret;
}

static inline int64_t operator&(const ObTxLogType left, const ObTxLogType right)
{
  return (
      static_cast<int64_t>(left) &
      static_cast<int64_t>(right) );
}

static inline ObTxLogType operator|(const ObTxLogType left, const ObTxLogType right)
{
  return static_cast<ObTxLogType>(
      static_cast<int64_t>(left) |
      static_cast<int64_t>(right) );
}

static const ObTxLogType TX_LOG_TYPE_MASK = (ObTxLogType::TX_REDO_LOG |
                                             ObTxLogType::TX_ROLLBACK_TO_LOG |
                                             ObTxLogType::TX_MULTI_DATA_SOURCE_LOG |
                                             ObTxLogType::TX_RECORD_LOG |
                                             ObTxLogType::TX_COMMIT_INFO_LOG |
                                             ObTxLogType::TX_COMMIT_LOG |
                                             ObTxLogType::TX_ABORT_LOG |
                                             ObTxLogType::TX_CLEAR_LOG);

class ObTxLogTypeChecker {
public:
  static bool is_data_log(const ObTxLogType log_type)
  {
    return ObTxLogType::TX_REDO_LOG == log_type || ObTxLogType::TX_RECORD_LOG == log_type
           || ObTxLogType::TX_ROLLBACK_TO_LOG == log_type
           || ObTxLogType::TX_MULTI_DATA_SOURCE_LOG == log_type;
  }
  static bool is_state_log(const ObTxLogType log_type)
  {
    return ObTxLogType::TX_COMMIT_LOG  == log_type ||
           ObTxLogType::TX_ABORT_LOG   == log_type ||
           ObTxLogType::TX_CLEAR_LOG   == log_type;
  }
  static bool is_valid_log(const ObTxLogType log_type)
  {
    return (TX_LOG_TYPE_MASK & log_type) != 0;
  }
  static bool is_mds_log(const ObTxLogType log_type)
  {
    return ObTxLogType::TX_MULTI_DATA_SOURCE_LOG == log_type;
  }
  static bool can_be_spilt(const ObTxLogType log_type)
  {
    return ObTxLogType::TX_MULTI_DATA_SOURCE_LOG == log_type;
  }

  static logservice::ObReplayBarrierType
  need_replay_barrier(const ObTxLogType log_type, const ObTxDataSourceType data_source_type);
  static int decide_final_barrier_type(const logservice::ObReplayBarrierType tmp_log_barrier_type,
                                       logservice::ObReplayBarrierType &final_barrier_type);
};

inline bool is_contain_stat_log(const ObTxCbArgArray &array)
{
  bool bool_ret = false;
  for (int64_t i = 0; i < array.count(); i++) {
    if ((ObTxLogTypeChecker::is_state_log(array.at(i).get_log_type()))) {
      bool_ret = true;
      break;
    }
  }
  return bool_ret;
}

class ObTxPrevLogType
{
public:
  enum TypeEnum : uint8_t
  {
    UNKOWN = 0,
    SELF = 1,
    COMMIT_INFO = 2,
  };

public:
  NEED_SERIALIZE_AND_DESERIALIZE;
  ObTxPrevLogType() { reset(); }
  ObTxPrevLogType(const TypeEnum prev_log_type) : prev_log_type_(prev_log_type) {}

  bool is_valid() const { return prev_log_type_ > 0; }
  void set_self() { prev_log_type_ = TypeEnum::SELF; }
  bool is_self() const { return TypeEnum::SELF == prev_log_type_; }

  void set_commit_info() { prev_log_type_ = TypeEnum::COMMIT_INFO; }
  bool is_normal_log() const
  {
    return TypeEnum::COMMIT_INFO == prev_log_type_;
  }

  ObTxLogType convert_to_tx_log_type();

  void reset() { prev_log_type_ = TypeEnum::UNKOWN; }

  TO_STRING_KV("val", prev_log_type_);

private:
  TypeEnum prev_log_type_;
};

// ============================== Tx Log Header ==============================
class ObTxLogHeader
{
public:
  static const int64_t TX_LOG_HEADER_SIZE = sizeof(ObTxLogType);

  NEED_SERIALIZE_AND_DESERIALIZE;
  ObTxLogHeader() : tx_log_type_(ObTxLogType::UNKNOWN) {}
  ObTxLogHeader(const ObTxLogType type) : tx_log_type_(type) {}
  ObTxLogType get_tx_log_type() const { return tx_log_type_; };
  TO_STRING_KV(K(tx_log_type_));
private:
  DISALLOW_COPY_AND_ASSIGN(ObTxLogHeader);
  ObTxLogType tx_log_type_;
};

// ============================== Tx Log Body ==============================
class ObTxRedoLogTempRef
{
public:
  ObTxRedoLogTempRef() {}
public:
};

class ObTxRedoLog
{
  OB_UNIS_VERSION(1);
// public:
//   NEED_SERIALIZE_AND_DESERIALIZE;

public:
  ObTxRedoLog(ObTxRedoLogTempRef &temp_ref)
      : mutator_buf_(nullptr), replay_mutator_buf_(nullptr), mutator_size_(-1)
  {
    UNUSED(temp_ref);
  }
  ObTxRedoLog() : mutator_buf_(nullptr), replay_mutator_buf_(nullptr), mutator_size_(-1) {}

  char *get_mutator_buf() { return mutator_buf_; }
  const char *get_replay_mutator_buf() const { return replay_mutator_buf_; }
  const int64_t &get_mutator_size() const { return mutator_size_; }

  //------------ Only invoke in ObTxLogBlock
  int set_mutator_buf(char *buf);
  int set_mutator_size(const int64_t size, const bool after_fill);
  void reset_mutator_buf();
  //------------

  //for ob_admin dump self
  int ob_admin_dump(memtable::ObMemtableMutatorIterator *iter_ptr,
                    share::ObAdminMutatorStringArg &arg,
                    const char *block_name,
                    palf::LSN lsn,
                    int64_t tx_id,
                    share::SCN scn,
                    bool &has_output);
  //------------

  static const int8_t MUTATOR_SIZE_NEED_BYTES = 4;
  static const ObTxLogType LOG_TYPE;
  TO_STRING_KV(
      K(LOG_TYPE),
      // KP(mutator_buf_),
      K(mutator_size_));

private:
  //------------ For ob_admin
  int format_mutator_row_(const memtable::ObMemtableMutatorRow &row, share::ObAdminMutatorStringArg &arg);
  int smart_dump_rowkey_(const ObStoreRowkey &rowkey, share::ObAdminMutatorStringArg &arg);
  int format_row_data_(const memtable::ObRowData &row_data, share::ObAdminMutatorStringArg &arg);
  //------------
private:
  char *mutator_buf_;              // for fill
  const char *replay_mutator_buf_; // for replay, only set by deserialize
  int64_t mutator_size_;
};

// for dist trans write it's multi source data, the same as redo,
// for simplicity, add a new log type
class ObTxMultiDataSourceLog
{
  OB_UNIS_VERSION(1);

public:
  const static int64_t MAX_MDS_LOG_SIZE = 512 * 3 * 1024;
  const static int64_t MAX_PENDING_BUF_SIZE = 512 * 1024;

public:
  ObTxMultiDataSourceLog()
  {
    reset();
  }
  ~ObTxMultiDataSourceLog() {}
  void reset();
  const ObTxBufferNodeArray &get_data() const { return data_; }

  int fill_MDS_data(const ObTxBufferNode &node);
  int64_t count() const { return data_.count(); }

  int ob_admin_dump(share::ObAdminMutatorStringArg &arg);

  static const ObTxLogType LOG_TYPE;
  TO_STRING_KV(K(LOG_TYPE), K_(data));

private:
  ObTxBufferNodeArray data_;
};

class TxSubmitBaseArg
{
public:
  ObTxLogCb *log_cb_;
  share::SCN base_scn_;
  int64_t replay_hint_;
  logservice::ObReplayBarrierType replay_barrier_type_;
  int64_t suggested_buf_size_;
  bool hold_tx_ctx_ref_;

  TxSubmitBaseArg() { reset(); }
  // ~TxSubmitBaseArg() = default;
  void reset()
  {
    log_cb_ = nullptr;
    base_scn_.set_invalid();
    replay_hint_ = 0;
    replay_barrier_type_ = logservice::ObReplayBarrierType::NO_NEED_BARRIER;
    suggested_buf_size_ = 0;
    hold_tx_ctx_ref_ = 0;
  }
  bool is_valid()
  {
    return log_cb_ != nullptr && base_scn_.is_valid() && replay_hint_ >= 0
           && replay_barrier_type_ != logservice::ObReplayBarrierType::INVALID_BARRIER
           && hold_tx_ctx_ref_ >= 0;
  }
  TO_STRING_KV(KP(log_cb_),
               K(base_scn_),
               K(replay_hint_),
               K(replay_barrier_type_),
               K(suggested_buf_size_),
               K(hold_tx_ctx_ref_));
};

class TxReplayBaseArg
{
public:
  int64_t part_log_no_;
  TxReplayBaseArg() { reset(); }
  // ~TxReplayBaseArg() = default;
  void reset() { part_log_no_ = -1; }
  bool is_valid() { return part_log_no_ > 0; }
  TO_STRING_KV(K(part_log_no_));
};

class ObTxCommitInfoLogTempRef
{
public:
  ObTxCommitInfoLogTempRef()
    : app_trace_id_str_(), app_trace_info_(), prev_record_lsn_(), redo_lsns_()
  {}

public:
  common::ObString app_trace_id_str_;
  common::ObString app_trace_info_;
  LogOffSet prev_record_lsn_;
  ObRedoLSNArray redo_lsns_;
};

class ObTxCommitInfoLog
{
  OB_UNIS_VERSION(1);

public:
  ObTxCommitInfoLog(ObTxCommitInfoLogTempRef &temp_ref)
      : can_elr_(false),
        app_trace_id_str_(temp_ref.app_trace_id_str_),
        obsolete_trace_payload_(temp_ref.app_trace_info_),
        prev_record_lsn_(temp_ref.prev_record_lsn_), redo_lsns_(temp_ref.redo_lsns_)
  {}
  ObTxCommitInfoLog(bool is_elr,
                    common::ObString &app_trace_id,
                    const LogOffSet &prev_record_lsn,
                    ObRedoLSNArray &redo_lsns);

  bool is_elr() const { return can_elr_; }
  const common::ObString &get_app_trace_id() const { return app_trace_id_str_; }
  const ObRedoLSNArray &get_redo_lsns() const { return redo_lsns_; }
  const LogOffSet &get_prev_record_lsn() const { return prev_record_lsn_; }
  int ob_admin_dump(share::ObAdminMutatorStringArg &arg);

  static const ObTxLogType LOG_TYPE;
  TO_STRING_KV(K(LOG_TYPE),
               K(can_elr_),
               K(app_trace_id_str_),
               K(prev_record_lsn_),
               K(redo_lsns_))

private:
  bool can_elr_;

  // Transaction tracing and redo-chain metadata.
  common::ObString &app_trace_id_str_;
  // Historical field 8 is deserialized into this buffer and intentionally ignored.
  common::ObString obsolete_trace_payload_;
  LogOffSet prev_record_lsn_;
  ObRedoLSNArray &redo_lsns_;
};

class ObTxDataBackup
{
  OB_UNIS_VERSION(1);

public:
  ObTxDataBackup();
  int init(const share::SCN &start_scn);
  void reset();

  share::SCN get_start_log_ts() const { return start_log_ts_; }

  TO_STRING_KV(K(start_log_ts_));

private:
  share::SCN start_log_ts_;
};

class ObTxCommitLogTempRef
{
public:
  ObTxCommitLogTempRef() : checksum_signature_(), multi_source_data_()
  { checksum_signature_.set_max_print_count(1024); }

public:
  ObSEArray<uint8_t,1> checksum_signature_;
  ObTxBufferNodeArray multi_source_data_;
};

class ObTxCommitLog
{
  OB_UNIS_VERSION(1);

public:
  const static int64_t INVALID_COMMIT_VERSION = ObTransVersion::INVALID_TRANS_VERSION;

public:
  ObTxCommitLog(ObTxCommitLogTempRef &temp_ref)
      : commit_version_(), checksum_(0), checksum_sig_(temp_ref.checksum_signature_),
        checksum_sig_serde_(checksum_sig_),
        multi_source_data_(temp_ref.multi_source_data_), tx_data_backup_(), prev_lsn_(), prev_log_type_()
  {}
  ObTxCommitLog(share::SCN commit_version,
                uint64_t checksum,
                ObIArray<uint8_t> &checksum_sig,
                ObTxBufferNodeArray &multi_source_data,
                LogOffSet prev_lsn,
                ObTxPrevLogType prev_log_type);
  int init_tx_data_backup(const share::SCN &start_scn);
  const ObTxDataBackup &get_tx_data_backup() const { return tx_data_backup_; }
  share::SCN get_commit_version() const { return commit_version_; }
  uint64_t get_checksum() const { return checksum_; }
  const ObTxBufferNodeArray &get_multi_source_data() const { return multi_source_data_; }
  const LogOffSet &get_prev_lsn() const { return prev_lsn_; }
  const ObTxPrevLogType &get_prev_log_type() const  { return prev_log_type_; }
  void set_prev_lsn(const LogOffSet &lsn) { prev_lsn_ = lsn; }

  const share::SCN get_backup_start_scn() { return tx_data_backup_.get_start_log_ts(); }

  int ob_admin_dump(share::ObAdminMutatorStringArg &arg);

  static const ObTxLogType LOG_TYPE;
  TO_STRING_KV(K(LOG_TYPE),
               K(commit_version_),
               K(checksum_),
               K_(checksum_sig),
               K(multi_source_data_),
               K(tx_data_backup_),
               K(prev_lsn_),
               K(prev_log_type_));

private:
  share::SCN commit_version_;
  uint64_t checksum_;
  ObIArray<uint8_t> &checksum_sig_;
  ObIArraySerDeTrait<uint8_t> checksum_sig_serde_;
  ObTxBufferNodeArray &multi_source_data_;

  ObTxDataBackup tx_data_backup_;

  // Transaction redo-chain metadata.
  LogOffSet prev_lsn_;
  ObTxPrevLogType prev_log_type_;
};

class ObTxClearLogTempRef
{
public:
  ObTxClearLogTempRef() {}
};

class ObTxClearLog
{
  OB_UNIS_VERSION(1);

public:
  ObTxClearLog(ObTxClearLogTempRef &temp_ref)
  {
    UNUSED(temp_ref);
  }
  ObTxClearLog();

  int ob_admin_dump(share::ObAdminMutatorStringArg &arg);

  static const ObTxLogType LOG_TYPE;
  TO_STRING_KV(K(LOG_TYPE));

};

class ObTxAbortLogTempRef
{
public:
  ObTxAbortLogTempRef() : multi_source_data_() {};
public:
  ObTxBufferNodeArray multi_source_data_;
};

class ObTxAbortLog
{
  OB_UNIS_VERSION(1);

public:
  ObTxAbortLog(ObTxAbortLogTempRef &temp_ref)
      : multi_source_data_(temp_ref.multi_source_data_),
        tx_data_backup_()
  {}
  ObTxAbortLog(ObTxBufferNodeArray &multi_source_data)
      : multi_source_data_(multi_source_data), tx_data_backup_()
  {}
  const ObTxBufferNodeArray &get_multi_source_data() const { return multi_source_data_; }

  int init_tx_data_backup(const share::SCN &start_scn);

  const ObTxDataBackup &get_tx_data_backup() const { return tx_data_backup_; }

  const share::SCN get_backup_start_scn() { return tx_data_backup_.get_start_log_ts(); }

  int ob_admin_dump(share::ObAdminMutatorStringArg &arg);

  static const ObTxLogType LOG_TYPE;
  TO_STRING_KV(K(LOG_TYPE), K(multi_source_data_));

private:
  ObTxBufferNodeArray &multi_source_data_;
  ObTxDataBackup tx_data_backup_;
};

class ObTxRecordLogTempRef
{
public:
  ObTxRecordLogTempRef() : prev_record_lsn_(), redo_lsns_() {}

public:
  LogOffSet prev_record_lsn_;
  ObRedoLSNArray redo_lsns_;
};

class ObTxRecordLog
{
  OB_UNIS_VERSION(1);

public:
  ObTxRecordLog(ObTxRecordLogTempRef &temp_ref)
      : prev_record_lsn_(temp_ref.prev_record_lsn_), redo_lsns_(temp_ref.redo_lsns_)
  {}
  ObTxRecordLog(const LogOffSet &prev_record_lsn, ObRedoLSNArray &redo_lsns)
      : prev_record_lsn_(prev_record_lsn), redo_lsns_(redo_lsns)
  {}

  const ObRedoLSNArray &get_redo_lsns() { return redo_lsns_; }
  const LogOffSet &get_prev_record_lsn() { return prev_record_lsn_; }

  int ob_admin_dump(share::ObAdminMutatorStringArg &arg);

  static const ObTxLogType LOG_TYPE;
  TO_STRING_KV(K(LOG_TYPE), K(prev_record_lsn_), K(redo_lsns_));

private:
  LogOffSet prev_record_lsn_;
  ObRedoLSNArray &redo_lsns_;
};


class ObTxRollbackToLog
{
public:
  OB_UNIS_VERSION(1);
public:
  ObTxRollbackToLog() = default;
  ObTxRollbackToLog(const ObTxSEQ from, const ObTxSEQ to)
    : from_(from), to_(to) {}


  ObTxSEQ get_from() const { return from_; }
  ObTxSEQ get_to() const { return to_; }

  int ob_admin_dump(share::ObAdminMutatorStringArg &arg);

  static const ObTxLogType LOG_TYPE;
  TO_STRING_KV(KP(this), K(LOG_TYPE), K_(from), K_(to));

private:
  ObTxSEQ from_;
  ObTxSEQ to_;
};

// ============================== Tx Log Blcok ==============================

class ObTxLogBlockHeader
{
  OB_UNIS_VERSION(1);
public:
  struct FixSizeTrait_int64_t {
    FixSizeTrait_int64_t(int64_t &v): v_(v) {}
    int64_t &v_;
    int serialize(SERIAL_PARAMS) const { return NS_::encode_i64(buf, buf_len, pos, v_); }
    int64_t get_serialize_size(void) const { return NS_::encoded_length_i64(v_); }
    int deserialize(DESERIAL_PARAMS) { return NS_::decode_i64(buf, data_len, pos, &v_); }
  };
public:
  void reset()
  {
    log_entry_no_ = 0;
    tx_id_ = 0;
    flags_ = 0;
  }
  ObTxLogBlockHeader(): __log_entry_no_(log_entry_no_)
  {
    reset();
  };
  ObTxLogBlockHeader(const int64_t log_entry_no,
                     const ObTransID &tx_id)
    : __log_entry_no_(log_entry_no_), flags_(0)
  {
    init(log_entry_no, tx_id);
  }
  void init(const int64_t log_entry_no,
            const ObTransID &tx_id) {
    log_entry_no_ = log_entry_no;
    tx_id_ = tx_id;
    flags_ = 0;
  }
  int64_t get_log_entry_no() const { return log_entry_no_; }
  void set_log_entry_no(int64_t entry_no) { log_entry_no_ = entry_no; }
  const ObTransID &get_tx_id() const { return tx_id_; }

  void set_serial_final() { flags_ |= SERIAL_FINAL; }
  bool is_serial_final() const { return (flags_ & SERIAL_FINAL) == SERIAL_FINAL; }
  void set_has_async_index() { flags_ |= HAS_ASYNC_INDEX; }
  bool has_async_index() const { return (flags_ & HAS_ASYNC_INDEX) == HAS_ASYNC_INDEX; }
  uint8_t flags() const { return flags_; }
  TO_STRING_KV(K_(log_entry_no), K_(tx_id), K_(flags));

public:
  // the last serial log
  static const uint8_t SERIAL_FINAL = ((uint8_t)1) << 0;
  // tx contains redo that involves tables with async indexes (for Change Stream fast filtering)
  static const uint8_t HAS_ASYNC_INDEX = ((uint8_t)1) << 1;
private:
  int64_t log_entry_no_;
  FixSizeTrait_int64_t __log_entry_no_; // serialize helper member, hiden for others
  ObTransID tx_id_;
  uint8_t flags_;
};

class ObTxAdaptiveLogBuf
{
public:
  ObTxAdaptiveLogBuf() : buf_(default_buf_), len_(sizeof(default_buf_))
  {
    default_buf_[0] = '\0';
  }
  ~ObTxAdaptiveLogBuf() { reset(); }
  int init(const int64_t suggested_buf_size)
  {
    int ret = OB_SUCCESS;
    char *ptr = NULL;
    if (suggested_buf_size <= MIN_LOG_BUF_SIZE) {
      // do nothing
    } else if (suggested_buf_size <= NORMAL_LOG_BUF_SIZE) {
      if (OB_ISNULL(ptr = alloc_normal_buf_())) {
        ret = OB_ALLOCATE_MEMORY_FAILED;
      } else {
        buf_ = ptr;
        len_ = NORMAL_LOG_BUF_SIZE;
      }
    } else {
      if (OB_ISNULL(ptr = alloc_big_buf_())) {
        ret = OB_ALLOCATE_MEMORY_FAILED;
      } else {
        buf_ = ptr;
        len_ = BIG_LOG_BUF_SIZE;
      }
    }
    return ret;
  }
  int extend_and_copy(const int64_t pos)
  {
    int ret = OB_SUCCESS;
    char *ptr = NULL;
    if (buf_ == default_buf_) {
      if (OB_ISNULL(ptr = alloc_normal_buf_())) {
        ret = OB_ALLOCATE_MEMORY_FAILED;
      } else {
        if (pos > 0) {
          memcpy(ptr, buf_, pos);
        }
        buf_ = ptr;
        len_ = NORMAL_LOG_BUF_SIZE;
      }
    // it will be enabled after clog support big clog
    } else if (NORMAL_LOG_BUF_SIZE == len_) {
      if (OB_ISNULL(ptr = alloc_big_buf_())) {
        ret = OB_ALLOCATE_MEMORY_FAILED;
      } else {
        if (pos > 0) {
          memcpy(ptr, buf_, pos);
        }
        free_buf_(buf_);
        buf_ = ptr;
        len_ = BIG_LOG_BUF_SIZE;
      }  
    } else {
      ret = OB_ERR_UNEXPECTED;
    }
    return ret;
  }
  void reset()
  {
    if (NULL != buf_ && buf_ != default_buf_) {
      free_buf_(buf_);
      buf_ = NULL;
    }
    default_buf_[0] = '\0';
    len_ = 0;
  }
  char *get_buf()
  {
    return buf_;
  }
  int64_t get_length() const
  {
    return len_;
  }
  TO_STRING_KV(KP_(buf), KP_(default_buf), K_(len));
private:
  char *alloc_normal_buf_()
  {
    int ret = OB_ALLOCATE_MEMORY_FAILED;
    char *ptr = NULL;
    if (OB_ISNULL(ptr = static_cast<char *>(share::server_malloc(NORMAL_LOG_BUF_SIZE, "NORMAL_CLOG_BUF")))) {
      ret = OB_ALLOCATE_MEMORY_FAILED;
      TRANS_LOG(WARN, "alloc clog normal buffer failed", K(ret));
    }
    return ptr;
  }
  char *alloc_big_buf_()
  {
    int ret = OB_ALLOCATE_MEMORY_FAILED;
    char *ptr = NULL;
    if (OB_ISNULL(ptr = static_cast<char *>(share::server_malloc(BIG_LOG_BUF_SIZE, "BIG_CLOG_BUF")))) {
      ret = OB_ALLOCATE_MEMORY_FAILED;
      TRANS_LOG(WARN, "alloc clog big buffer failed", K(ret));
    }
    return ptr;
  }
  void free_buf_(char *buf)
  {
    if (OB_NOT_NULL(buf)) {
      share::server_free(buf);
    }
  }
public:
  static const int64_t MIN_LOG_BUF_SIZE = 4096;
  static const int64_t NORMAL_LOG_BUF_SIZE = common::OB_MAX_LOG_ALLOWED_SIZE;
  static const int64_t BIG_LOG_BUF_SIZE = palf::MAX_LOG_BODY_SIZE;
  STATIC_ASSERT((BIG_LOG_BUF_SIZE > 3 * 1024 * 1024 && BIG_LOG_BUF_SIZE < 4 * 1024 * 1024), "unexpected big log buf size");

private:
  char *buf_;
  int64_t len_;
  char default_buf_[MIN_LOG_BUF_SIZE];
};


class ObTxLogBlock
{
public:
  // static const int MIN_LOG_BLOCK_HEADER_SIZE;
  static const logservice::ObLogBaseType DEFAULT_LOG_BLOCK_TYPE; // TRANS_LOG
  static const int32_t DEFAULT_BIG_ROW_BLOCK_SIZE;
  static const int64_t BIG_SEGMENT_SPILT_SIZE;
  NEED_SERIALIZE_AND_DESERIALIZE;
  ObTxLogBlock();
  void reset();
  int reuse_for_fill();
  int init_for_fill(const int64_t suggested_buf_size = ObTxAdaptiveLogBuf::NORMAL_LOG_BUF_SIZE);
  int init_for_replay(const char *buf, const int64_t &size);
  int init_for_replay(const char *buf, const int64_t &size, int skip_pos); // init before replay
  ObTxLogBlockHeader &get_header() { return header_; }
  logservice::ObLogBaseHeader &get_log_base_header() { return log_base_header_; }
  typedef logservice::ObReplayBarrierType ObReplayBarrierType;
  int seal(const int64_t replay_hint, const ObReplayBarrierType barrier_type = ObReplayBarrierType::NO_NEED_BARRIER);
  ~ObTxLogBlock() { reset(); }
  bool is_inited() const { return inited_; }
  int get_next_log(ObTxLogHeader &header,
                   ObTxBigSegmentBuf *big_segment_buf = nullptr,
                   bool *contain_big_segment = nullptr);
  const ObTxCbArgArray &get_cb_arg_array() const { return cb_arg_array_; }
  template <typename T>
  int deserialize_log_body(T &tx_log_body); // make cur_log_type_ is UNKNOWN
  // fill log except RedoLog
  template <typename T>
  int add_new_log(T &tx_log_body, ObTxBigSegmentBuf * big_segment_buf = nullptr);
  // fill RedoLog and mutator_buf
  int prepare_mutator_buf(ObTxRedoLog &redo);
  int finish_mutator_buf(ObTxRedoLog &redo, const int64_t &mutator_size);
  int extend_log_buf();
  TO_STRING_KV(K(fill_buf_),
               KP(replay_buf_),
               K(len_),
               K(pos_),
               K(cur_log_type_),
               K(cb_arg_array_),
               KPC(big_segment_buf_),
               K_(inited),
               K_(header),
               K_(log_base_header));

public:
  // get fill buf for submit log
  char *get_buf() { return fill_buf_.get_buf(); }
  const int64_t &get_size() { return pos_; }

  int set_prev_big_segment_scn(const share::SCN prev_scn);
  int acquire_segment_log_buf(const ObTxLogType big_segment_log_type, ObTxBigSegmentBuf *big_segment_buf = nullptr);
private:
  int serialize_log_block_header_();
  int deserialize_log_block_header_();
  int update_next_log_pos_(); // skip log body if  cur_log_type_ is UNKNOWN (depend on
                              // DESERIALIZE_HEADER in ob_unify_serialize.h)
  DISALLOW_COPY_AND_ASSIGN(ObTxLogBlock);
  bool inited_;
  logservice::ObLogBaseHeader log_base_header_;
  ObTxLogBlockHeader header_;
  ObTxAdaptiveLogBuf fill_buf_;
  const char *replay_buf_;
  int64_t len_;
  int64_t pos_;
  ObTxLogType cur_log_type_;
  ObTxCbArgArray cb_arg_array_;

  ObTxBigSegmentBuf *big_segment_buf_;
};

template <typename T>
int ObTxLogBlock::deserialize_log_body(T &tx_log_body)
{
  int ret = OB_SUCCESS;
  int64_t tmp_pos = pos_;
  if (OB_ISNULL(replay_buf_)) {
    ret = OB_INVALID_ARGUMENT;
    TRANS_LOG(ERROR, "invalid argument", K(*this));
  } else if (T::LOG_TYPE != cur_log_type_) {
    ret = OB_INVALID_ARGUMENT;
    TRANS_LOG(ERROR, "invalid log_body type", K(T::LOG_TYPE), K(cur_log_type_));
  } else {
    if (OB_NOT_NULL(big_segment_buf_)) {
      if (OB_FAIL(big_segment_buf_->deserialize_object(tx_log_body))) {
        TRANS_LOG(WARN, "deserialize log body from big segment buf failed", K(ret), K(tx_log_body),
                  KPC(this));
      } else {
        // big_segment_buf_->reset();
        big_segment_buf_ = nullptr;
      }
    } else if (OB_FAIL(tx_log_body.deserialize(replay_buf_, len_, tmp_pos))) {
      TRANS_LOG(WARN, "desrialize tx_log_body error", K(ret), K(*this));
    }
    if (OB_SUCC(ret)) {
      pos_ = tmp_pos;
      cur_log_type_ = ObTxLogType::UNKNOWN;
    }
  }
  return ret;
}

template <typename T>
int ObTxLogBlock::add_new_log(T &tx_log_body, ObTxBigSegmentBuf *big_segment_buf)
{
  int ret = OB_SUCCESS;
  int64_t tmp_pos = pos_;
  ObTxLogHeader header(T::LOG_TYPE);
  if ((OB_ISNULL(fill_buf_.get_buf()))) {
    ret = OB_INVALID_ARGUMENT;
    TRANS_LOG(ERROR, "invalid argument", K(*this));
  } else if (ObTxLogType::TX_REDO_LOG == cur_log_type_) {
    ret = OB_EAGAIN;
    TRANS_LOG(WARN, "MutatorBuf is using");
  } else if (OB_NOT_NULL(big_segment_buf_) && big_segment_buf_->is_active()) {
    ret = OB_NOT_SUPPORTED;
    TRANS_LOG(WARN, "big segment is not empty, can not add new log", K(ret), KPC(this));
  } else if (T::LOG_TYPE == ObTxLogType::TX_REDO_LOG) {
    // ret = OB_EAGAIN;
    TRANS_LOG(DEBUG, "insert redo_log type into cb_arg_array_", K(tx_log_body), KPC(this));
  } else {
    if constexpr (requires(T &log) { log.before_serialize(); }) {
      if (OB_FAIL(tx_log_body.before_serialize())) {
        TRANS_LOG(WARN, "before serialize failed", K(ret), K(tx_log_body), K(*this));
      }
    }
    if (OB_SUCC(ret)) {
      const int64_t serialize_size = header.get_serialize_size() + tx_log_body.get_serialize_size();
      if (ObTxLogTypeChecker::can_be_spilt(T::LOG_TYPE) && BIG_SEGMENT_SPILT_SIZE < serialize_size + tmp_pos) {
        ret = OB_BUF_NOT_ENOUGH;
      } else {
        while (OB_SUCC(ret) && len_ < serialize_size + tmp_pos) {
          if (OB_FAIL(extend_log_buf())) {
            // ret = OB_BUF_NOT_ENOUGH;
            TRANS_LOG(WARN, "extend a log buf failed", K(ret), K(len_), K(tmp_pos), K(serialize_size),
                      K(fill_buf_), KPC(this));
          }
        }
      }
      if (OB_SUCC(ret)) {
        if (len_ < serialize_size + tmp_pos) {
          ret = OB_BUF_NOT_ENOUGH;
        } else if (OB_FAIL(header.serialize(fill_buf_.get_buf(), len_, tmp_pos))) {
          TRANS_LOG(WARN, "serialize log header error", K(ret), K(header), K(*this));
        } else if (OB_FAIL(tx_log_body.serialize(fill_buf_.get_buf(), len_, tmp_pos))) {
          TRANS_LOG(WARN, "serialize tx_log_body error", K(ret), K(tx_log_body), K(*this));
        }
      }
    }
  }

  if (OB_SUCC(ret)) {
    // it should be not failed
    if (OB_FAIL(cb_arg_array_.push_back(ObTxCbArg(T::LOG_TYPE, NULL)))) {
      TRANS_LOG(ERROR, "push log cb arg failed", K(ret), K(*this));
    } else {
      pos_ = tmp_pos;
    }
  } else if (OB_BUF_NOT_ENOUGH == ret && cb_arg_array_.empty()
             && ObTxLogTypeChecker::can_be_spilt(T::LOG_TYPE)) {
    // use big segment
    if (OB_ISNULL(big_segment_buf)) {
      ret = OB_INVALID_ARGUMENT;
      TRANS_LOG(WARN, "invalid argument", K(ret), KPC(big_segment_buf));
    } else if (OB_FALSE_IT(big_segment_buf_ = big_segment_buf)) {
    } else if (OB_FAIL(big_segment_buf_->init_for_serialize(header.get_serialize_size()
                                                            + tx_log_body.get_serialize_size()))) {
      TRANS_LOG(WARN, "init big segment buf failed", K(ret), KPC(big_segment_buf_));
    } else if (OB_FAIL(big_segment_buf_->serialize_object(header))) {
      TRANS_LOG(WARN, "serialize log header error", K(ret), K(header), K(*this));
    } else if (OB_FAIL(big_segment_buf_->serialize_object(tx_log_body))) {
      TRANS_LOG(WARN, "serialize tx_log_body error", K(ret), K(tx_log_body), K(*this));
    } else {
      ret = OB_LOG_TOO_LARGE;
    }
  }

  if (OB_FAIL(ret)) {
    TRANS_LOG(WARN, "[TxLogBlock] add new log", K(ret), K(tx_log_body), K(*this), K(tmp_pos));
  }

  return ret;
}

} // namespace transaction
} // namespace oceanbase
#endif
