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

#ifndef OCEANBASE_TX_LOG_OPERATOR_HEADER
#define OCEANBASE_TX_LOG_OPERATOR_HEADER

#include "storage/ls/ob_ls_ddl_log_handler.h"
#include "storage/tx/ob_trans_define.h"
#include "storage/tx/ob_trans_part_ctx.h"
#include "storage/tx/ob_tx_log.h"

namespace oceanbase
{
namespace transaction
{

/****************************************
 * Public TxLog Operator Template
 * Begin
 ****************************************/
enum class ObTxLogOpType
{
  SUBMIT,
  APPLY_SUCC,
  APPLY_FAIL,
  REPLAY
};

template <typename T>
class ObTxCtxLogOperator
{
private:
  int construct_log_object_();

  // submit log function
  int prepare_generic_resource_();
  int prepare_special_resource_();
  int submit_prev_log_();
  int insert_into_log_block_();
  int pre_check_for_log_submiting_();
  int submit_log_block_out_();
  int common_submit_log_succ_();
  void after_submit_log_succ_();
  void after_submit_log_fail_(const int submit_ret);
  int log_sync_succ_()
  {
    int ret = OB_NOT_SUPPORTED;
    TRANS_LOG(WARN, "empty function of log_sync_succ", K(ret), KPC(this));
    return ret;
  }
  int log_sync_fail_()
  {
    int ret = OB_NOT_SUPPORTED;
    TRANS_LOG(WARN, "empty function of log_sync_fail", K(ret), KPC(this));
    return ret;
  }

  // replay log function
  int deserialize_log_()
  {
    int ret = OB_SUCCESS;

    if (OB_FAIL(construct_log_object_())) {
      TRANS_LOG(WARN, "construct log object failed", K(ret), KPC(this));
    } else if (OB_FAIL(log_block_->deserialize_log_body(*log_object_ptr_))) {
      TRANS_LOG(WARN, "deserialize log body failed", K(ret), KPC(this));
    }
    return ret;
  }
  int replay_in_ctx_()
  {
    int ret = OB_SUCCESS;

    return ret;
  }
  int replay_out_ctx_()
  {
    int ret = OB_SUCCESS;

    return ret;
  }
  int replay_fail_out_ctx_()
  {
    int ret = OB_SUCCESS;

    return ret;
  }
  int replay_log_()
  {
    int ret = OB_SUCCESS;
    int tmp_ret = OB_SUCCESS;
    if (OB_FAIL(deserialize_log_())) {
      TRANS_LOG(WARN, "deserialize the log failed", K(ret), K(tx_ctx_->trans_id_),
                K(tx_ctx_->ls_id_), KPC(this));
    } else if (OB_FAIL(replay_out_ctx_())) {
      TRANS_LOG(WARN, "replay log out ctx failed", K(ret), K(tx_ctx_->trans_id_),
                K(tx_ctx_->ls_id_), KPC(this));
    } else if (OB_FAIL(replay_in_ctx_())) {
      TRANS_LOG(WARN, "replay log in ctx failed", K(ret), K(tx_ctx_->trans_id_), K(tx_ctx_->ls_id_),
                KPC(this));
    }

    if (OB_FAIL(ret)) {
      if (OB_TMP_FAIL(replay_fail_out_ctx_())) {
        TRANS_LOG(ERROR, "an error occurred while handling replay failure outside of the tx_ctx ",
                  K(tmp_ret), K(ret), KPC(this));
      }
    }
    return ret;
  }

public:
  // submit
  ObTxCtxLogOperator(ObPartTransCtx *tx_ctx_ptr,
                     ObTxLogBlock *log_block_ptr,
                     typename T::ConstructArg *construct_arg,
                     const typename T::SubmitArg &submit_arg)
      : tx_ctx_(tx_ctx_ptr), log_object_ptr_(nullptr), log_block_(log_block_ptr),
        construct_arg_(construct_arg), scn_(), lsn_()
  {
    log_op_arg_.submit_arg_.reset();
    log_op_arg_.submit_arg_ = submit_arg;
  };

  // apply
  ObTxCtxLogOperator(ObPartTransCtx *tx_ctx_ptr, ObTxLogCb *log_cb_ptr)
      : tx_ctx_(tx_ctx_ptr), log_object_ptr_(nullptr), log_block_(nullptr), construct_arg_(nullptr),
        scn_(log_cb_ptr->get_log_ts()), lsn_(log_cb_ptr->get_lsn())
  {
    log_op_arg_.submit_arg_.reset();
    log_op_arg_.submit_arg_.log_cb_ = log_cb_ptr;
  };

  // replay
  ObTxCtxLogOperator(ObPartTransCtx *tx_ctx_ptr,
                     ObTxLogBlock *log_block_ptr,
                     typename T::ConstructArg *construct_arg,
                     const typename T::ReplayArg &replay_arg,
                     const share::SCN scn,
                     const palf::LSN lsn)
      : tx_ctx_(tx_ctx_ptr), log_object_ptr_(nullptr), log_block_(log_block_ptr),
        construct_arg_(construct_arg), scn_(scn), lsn_(lsn)
  {
    log_op_arg_.replay_arg_.reset();
    log_op_arg_.replay_arg_ = replay_arg;
  };

  ~ObTxCtxLogOperator()
  {
    if (OB_NOT_NULL(log_object_ptr_)) {
      log_object_ptr_->~T();
    }
  };

  int operator()(const ObTxLogOpType op_type);

  TO_STRING_KV(KPC(construct_arg_), KPC(tx_ctx_), K(scn_), K(lsn_), KPC(log_block_));

public:
  const share::SCN &get_scn() { return scn_; }
  const palf::LSN &get_lsn() { return lsn_; }

private:
  ObPartTransCtx *tx_ctx_;
  char log_object_memory_[sizeof(T)];
  T *log_object_ptr_;
  ObTxLogBlock *log_block_;
  typename T::ConstructArg *construct_arg_;

  share::SCN scn_;
  palf::LSN lsn_;

  union LogOpArg
  {
    typename T::SubmitArg submit_arg_;
    typename T::ReplayArg replay_arg_;

    LogOpArg(){};
    ~LogOpArg(){};

  } log_op_arg_;
  // bool retain_in_memory_; TODO: retain prev log in log_block
};

template <typename T>
OB_INLINE int ObTxCtxLogOperator<T>::prepare_generic_resource_()
{
  int ret = OB_SUCCESS;

  if (OB_ISNULL(tx_ctx_) || OB_ISNULL(log_block_)) {
    ret = OB_INVALID_ARGUMENT;
    TRANS_LOG(WARN, "invalid arguments", K(ret), KPC(this));
  } else if (!log_block_->is_inited()) {
    {
      CtxLockGuard guard;
      if (!tx_ctx_->lock_.is_locked_by_self()) {
        tx_ctx_->get_ctx_guard(guard, CtxLockGuard::MODE::CTX);
      }

      // From 4.3, we must init the cluster_version_ of the log block header before init the log
      // block.
      // the log_entry_no will be backfill before log-block to be submitted
      log_block_->get_header().init(tx_ctx_->cluster_id_, tx_ctx_->cluster_version_,
                                    INT64_MAX /*log_entry_no*/, tx_ctx_->trans_id_,
                                    tx_ctx_->exec_info_.scheduler_);
    }
    log_op_arg_.submit_arg_.suggested_buf_size_ = log_op_arg_.submit_arg_.suggested_buf_size_ <= 0
                                                      ? ObTxAdaptiveLogBuf::NORMAL_LOG_BUF_SIZE
                                                      : log_op_arg_.submit_arg_.suggested_buf_size_;
    if (OB_FAIL(log_block_->init_for_fill(log_op_arg_.submit_arg_.suggested_buf_size_))) {
      TRANS_LOG(WARN, "init log block for fill failed", K(ret), KPC(this));
    }
  }

  if (OB_FAIL(ret)) {
    // do nothing
  } else if (OB_FAIL(tx_ctx_->prepare_log_cb_(false, log_op_arg_.submit_arg_.log_cb_))) {
    if (OB_UNLIKELY(OB_TX_NOLOGCB != ret)) {
      TRANS_LOG(WARN, "get log cb failed", K(ret), KPC(this));
    }
  } else if (OB_FAIL(tx_ctx_->acquire_ctx_ref_())) {
    TRANS_LOG(ERROR, "acquire ctx ref failed", KR(ret), K(tx_ctx_->trans_id_), K(tx_ctx_->ls_id_),
              KPC(this));
  } else if (OB_FALSE_IT(log_op_arg_.submit_arg_.hold_tx_ctx_ref_ = true)) {
    // do nothing
  }

  return ret;
}

template <typename T>
OB_INLINE int ObTxCtxLogOperator<T>::prepare_special_resource_()
{
  int ret = OB_SUCCESS;
  // do nothing
  return ret;
}

template <typename T>
OB_INLINE int ObTxCtxLogOperator<T>::submit_prev_log_()
{
  int ret = OB_SUCCESS;
  // do nothing
  // TODO submit memtable redo, mds redo, dlc redo, commit info before the prepare
  // T::LOG_TYPE
  return ret;
}

template <typename T>
OB_INLINE int ObTxCtxLogOperator<T>::construct_log_object_()
{
  int ret = OB_SUCCESS;

  if (OB_ISNULL(construct_arg_)) {
    ret = OB_INVALID_ARGUMENT;
    TRANS_LOG(WARN, "invalid construct arg pointer", K(ret), KPC(construct_arg_));
  } else {
    memset(log_object_memory_, 0, sizeof(T));
    new (log_object_memory_) T(*construct_arg_);
    log_object_ptr_ = (T *)(log_object_memory_);
  }
  return ret;
}

template <typename T>
OB_INLINE int ObTxCtxLogOperator<T>::insert_into_log_block_()
{
  int ret = OB_SUCCESS;

  if (OB_FAIL(log_block_->add_new_log(*log_object_ptr_))) {
    if (OB_BUF_NOT_ENOUGH != ret) {
      TRANS_LOG(WARN, "add new log failed", KR(ret), KPC(this));
    } else {
      TRANS_LOG(DEBUG, "the buffer is not enough in log_block", K(ret), K(tx_ctx_->trans_id_),
                K(tx_ctx_->ls_id_), KPC(this));
    }
  }

  return ret;
}

template <typename T>
OB_INLINE int ObTxCtxLogOperator<T>::pre_check_for_log_submiting_()
{
  int ret = OB_SUCCESS;

  return ret;
}

template <typename T>
OB_INLINE int ObTxCtxLogOperator<T>::submit_log_block_out_()
{
  int ret = OB_SUCCESS;
  bool is_2pc_state_log = false;

  if (tx_ctx_->is_exiting()) {
    ret = OB_TRANS_IS_EXITING;
    TRANS_LOG(WARN, "the tx ctx is exiting", K(ret), K(T::LOG_TYPE), KPC(tx_ctx_));
  } else if (tx_ctx_->is_force_abort_logging_()
             || tx_ctx_->get_downstream_state() == ObTxState::ABORT) {
    ret = OB_TRANS_KILLED;
    TRANS_LOG(ERROR, "tx has been aborting, can not submit log", K(ret), K(T::LOG_TYPE),
              KPC(tx_ctx_));
  } else if (tx_ctx_->is_follower_()) {
    ret = OB_NOT_MASTER;
    TRANS_LOG(ERROR, "we can not submit a tx log on the follower", K(ret), K(T::LOG_TYPE),
              KPC(tx_ctx_));
  } else if (tx_ctx_->exec_info_.data_complete_
             && tx_ctx_->start_working_log_ts_ > tx_ctx_->exec_info_.max_applying_log_ts_) {
    ret = OB_ERR_UNEXPECTED;
    TRANS_LOG(WARN,
              "There exists a data completed transaction whose start_working_log_ts_ is greater "
              "than any of its log_ts",
              K(ret), K(T::LOG_TYPE), KPC(tx_ctx_));
    tx_ctx_->print_trace_log_();
  } else if (ObTxLogTypeChecker::is_data_log(T::LOG_TYPE)
             && tx_ctx_->get_downstream_state() >= ObTxState::REDO_COMPLETE) {
    ret = OB_STATE_NOT_MATCH;
    TRANS_LOG(ERROR, "the data log can not be submitted after the commit info log", K(ret),
              K(T::LOG_TYPE), KPC(tx_ctx_));
  } else if (is_contain_stat_log(log_block_->get_cb_arg_array())
             && FALSE_IT(is_2pc_state_log = true)) {
  } else {
    const int64_t real_replay_hint = log_op_arg_.submit_arg_.replay_hint_ > 0
                                         ? log_op_arg_.submit_arg_.replay_hint_
                                         : tx_ctx_->trans_id_.get_id();
    log_block_->get_header().set_log_entry_no(tx_ctx_->exec_info_.next_log_entry_no_);
    if (OB_FAIL(log_block_->seal(real_replay_hint, log_op_arg_.submit_arg_.replay_barrier_type_))) {
      TRANS_LOG(WARN, "seal log block fail", K(ret));
    } else if (OB_SUCC(tx_ctx_->ls_tx_ctx_mgr_->get_ls_log_adapter()->submit_log(
                   log_block_->get_buf(), log_block_->get_size(), log_op_arg_.submit_arg_.base_scn_,
                   log_op_arg_.submit_arg_.log_cb_, false))) {
      tx_ctx_->busy_cbs_.add_last(log_op_arg_.submit_arg_.log_cb_);
      scn_ = log_op_arg_.submit_arg_.log_cb_->get_log_ts();
      lsn_ = log_op_arg_.submit_arg_.log_cb_->get_lsn();
    }
  }

  return ret;
}

template <typename T>
OB_INLINE int ObTxCtxLogOperator<T>::common_submit_log_succ_()
{
  int ret = OB_SUCCESS;
  if (OB_SUCC(ret)
      && OB_FAIL(tx_ctx_->update_rec_log_ts_(false /*for_replay*/, share::SCN::invalid_scn()))) {
    TRANS_LOG(WARN, "update rec log ts failed", KR(ret), KPC(log_op_arg_.submit_arg_.log_cb_),
              K(*this));
  }
  if (OB_SUCC(ret)) {
    const ObTxCbArgArray &cb_arg_array = log_block_->get_cb_arg_array();
    if (cb_arg_array.count() == 0) {
      ret = OB_ERR_UNEXPECTED;
      TRANS_LOG(ERROR, "cb arg array is empty", K(ret), K(*this));
    } else if (OB_FAIL(log_op_arg_.submit_arg_.log_cb_->get_cb_arg_array().assign(cb_arg_array))) {
      TRANS_LOG(WARN, "assign cb arg array failed", K(ret));
    }
  }
  if (OB_SUCC(ret)) {
    if (!tx_ctx_->ctx_tx_data_.get_start_log_ts().is_valid()) {
      if (OB_FAIL(tx_ctx_->ctx_tx_data_.set_start_log_ts(
              log_op_arg_.submit_arg_.log_cb_->get_log_ts()))) {
        TRANS_LOG(WARN, "set tx data start log ts failed", K(ret), K(tx_ctx_->ctx_tx_data_));
      }
    }
  }
  return ret;
}

template <typename T>
OB_INLINE void ObTxCtxLogOperator<T>::after_submit_log_succ_()
{}

template <typename T>
OB_INLINE void ObTxCtxLogOperator<T>::after_submit_log_fail_(const int submit_ret)
{
  int ret = OB_SUCCESS;
  int tmp_ret = OB_SUCCESS;
  if (OB_BUF_NOT_ENOUGH == submit_ret) {
    // do nothing
    // TODO submit prev log block with BUF_NOT_ENOUGH. It will reuse the generic_resource and
    // rewrite err_ret.
    tmp_ret = OB_NOT_SUPPORTED;
  }

  if (submit_ret != OB_BUF_NOT_ENOUGH || tmp_ret != OB_SUCCESS) {
    if (OB_NOT_NULL(log_op_arg_.submit_arg_.log_cb_)) {
      if (log_op_arg_.submit_arg_.log_cb_->get_prev() != nullptr
          || log_op_arg_.submit_arg_.log_cb_->get_next() != nullptr) {
        TRANS_LOG(ERROR, "the log cb is not alone", K(submit_ret), K(tmp_ret),
                  K(tx_ctx_->get_trans_id()), K(tx_ctx_->get_ls_id()),
                  KPC(log_op_arg_.submit_arg_.log_cb_), K(T::LOG_TYPE), KPC(this));
      }
      if (OB_TMP_FAIL(tx_ctx_->return_log_cb_(log_op_arg_.submit_arg_.log_cb_))) {
        TRANS_LOG(ERROR, "free the log cb failed", K(submit_ret), K(tmp_ret),
                  K(tx_ctx_->get_trans_id()), K(tx_ctx_->get_ls_id()),
                  KPC(log_op_arg_.submit_arg_.log_cb_), K(T::LOG_TYPE), KPC(this));
      }
    }
    if (log_op_arg_.submit_arg_.hold_tx_ctx_ref_) {
      tx_ctx_->release_ctx_ref_();
    }
    log_block_->reset();
  }
}

template <typename T>
OB_INLINE int ObTxCtxLogOperator<T>::operator()(const ObTxLogOpType op_type)
{
  int ret = OB_SUCCESS;
  int tmp_ret = OB_SUCCESS;
  if (op_type == ObTxLogOpType::REPLAY) {
    if (OB_FAIL(replay_log_())) {
      TRANS_LOG(WARN, "replay log failed", K(ret), K(T::LOG_TYPE), KPC(this));
    }
  } else if (op_type == ObTxLogOpType::SUBMIT) {
    if (OB_FAIL(prepare_special_resource_())) {
      TRANS_LOG(WARN, "prepare special resource failed", K(ret), K(T::LOG_TYPE), KPC(this));
    } else if (OB_FAIL(prepare_generic_resource_())) {
      if (OB_TX_NOLOGCB != ret) {
        TRANS_LOG(WARN, "prepare generic resource failed", K(ret), K(T::LOG_TYPE), KPC(this));
      }
    } else if (OB_FAIL(construct_log_object_())) {
      TRANS_LOG(WARN, "construct log object failed", K(ret), K(T::LOG_TYPE), KPC(this));
    } else if (OB_FAIL(insert_into_log_block_())) {
      TRANS_LOG(WARN, "insert tx log into log block failed", K(ret), K(T::LOG_TYPE), KPC(this));
    } else {
      CtxLockGuard guard;
      if (!tx_ctx_->lock_.is_locked_by_self()) {
        tx_ctx_->get_ctx_guard(guard, CtxLockGuard::MODE::CTX);
      }

      if (OB_FAIL(pre_check_for_log_submiting_())) {
        TRANS_LOG(WARN, "pre check for log submitting",K(ret), K(T::LOG_TYPE), KPC(this));
      } else if (OB_FAIL(submit_log_block_out_())) {
        TRANS_LOG(WARN, "submit tx log block into palf failed", K(ret), K(T::LOG_TYPE), KPC(this));
      } else if (OB_TMP_FAIL(common_submit_log_succ_())) {
        TRANS_LOG(WARN, "common after_submit_log_succ failed", K(ret), K(T::LOG_TYPE), KPC(this));
      } else {
        (void)after_submit_log_succ_();
        tx_ctx_->exec_info_.next_log_entry_no_++;
        tx_ctx_->reuse_log_block_(*log_block_);
      }
    }

    if (OB_FAIL(ret)) {
      (void)after_submit_log_fail_(ret);
    }

  } else if (op_type == ObTxLogOpType::APPLY_SUCC) {

    if (OB_FAIL(log_sync_succ_())) {
      TRANS_LOG(ERROR, "invoke on_success for tx_log failed", K(ret), K(T::LOG_TYPE), KPC(this));
    }

  } else if (op_type == ObTxLogOpType::APPLY_FAIL) {

    if (OB_FAIL(log_sync_fail_())) {
      TRANS_LOG(WARN, "invoke on_failure for tx_log failed", K(ret), K(T::LOG_TYPE), KPC(this));
    }

  } else {
    ret = OB_ERR_UNEXPECTED;
    TRANS_LOG(ERROR, "invalid tx log op type", K(ret), K(op_type), K(T::LOG_TYPE), KPC(this));
  }
  return ret;
}

/****************************************
 * Public TxLog Operator Template
 * End
 ****************************************/

/****************************************
 * End
 ****************************************/

} // namespace transaction

} // namespace oceanbase

#endif
