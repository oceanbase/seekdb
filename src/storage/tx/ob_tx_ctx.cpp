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

#include "ob_tx_ctx.h"
#include "share/rc/ob_server_runtime.h"
#include "ob_tx_redo_submitter.h"
#include "storage/tx/ob_trans_service.h"
#define NEED_MDS_REGISTER_DEFINE
#include "storage/multi_data_source/compile_utility/mds_register.h"
#undef NEED_MDS_REGISTER_DEFINE
#include "storage/allocator/ob_shared_memory_allocator_mgr.h"
#include "storage/multi_data_source/runtime_utility/mds_service.h"
#include "share/rc/ob_server_runtime.h"
#include "lib/utility/ob_smart_call.h"
#include "logservice/ob_log_service.h"
#include "storage/tx/ob_tx_log_operator.h"
#include "storage/tx/ob_tx_print_time_guard.h"

namespace oceanbase {

using namespace common;
using namespace memtable;
using namespace share;
using namespace sql;
using namespace storage;
using namespace palf;

namespace transaction {
using namespace tablelock;

#define INC_ELR_STATISTIC(item) \
  do {                          \
  } while (0);

bool ObTxCtx::is_inited() const { return ATOMIC_LOAD(&is_inited_); }

int ObTxCtx::init(const uint32_t session_id,
                  const ObTransID &trans_id,
                  const int64_t trans_expired_time,
                  ObTransService *trans_service,
                  ObLSTxCtxMgr *ls_ctx_mgr,
                  const bool for_replay,
                  const TxCtxSource ctx_source)
{
  int ret = OB_SUCCESS;

  CtxLockGuard guard(lock_);
  // default init : just reset immediately
  default_init_();

  // specified init : initialize with specified value
  if (OB_UNLIKELY(is_inited_)) {
    TRANS_LOG(WARN, "ObTxCtx inited twice");
    ret = OB_INIT_TWICE;
  } else if (OB_UNLIKELY(!trans_id.is_valid())
             || OB_UNLIKELY(trans_expired_time <= 0)
             || OB_ISNULL(trans_service)) {
    TRANS_LOG(WARN, "invalid argument", K(trans_id), KP(trans_service));
    ret = OB_INVALID_ARGUMENT;
  } else {
    ls_tx_ctx_mgr_ = ls_ctx_mgr;
    trans_service_ = trans_service;
    if (OB_FAIL(lock_.init(this))) {
    } else if (OB_ISNULL(timer_ = &(trans_service->get_trans_timer()))) {
      TRANS_LOG(ERROR, "ObTransService is invalid, unexpected error");
      ret = OB_ERR_UNEXPECTED;
    } else if (OB_FAIL(timeout_task_.init(this))) {
    } else if (OB_FAIL(init_memtable_ctx_())) {
    } else if (OB_FAIL(init_log_cbs_(trans_id))) {
    } else if (OB_FAIL(ctx_tx_data_.init(trans_expired_time, ls_ctx_mgr, trans_id))) {
    } else if (OB_FAIL(mds_cache_.init(trans_id))) {
    }
  }

  if (OB_SUCC(ret)) {
    session_id_ = session_id;
    addr_ = trans_service->get_server();
    trans_id_ = trans_id;
    for_replay_ = for_replay;
    trans_expired_time_ = trans_expired_time;
    ctx_create_time_ = ObClockGenerator::getClock();
    ctx_source_ = ctx_source;
    part_trans_action_ = ObPartTransAction::INIT;
    commit_retry_timeout_ = get_commit_retry_interval_us_();
    last_request_ts_ = ctx_create_time_;

    last_check_tx_status_ts_ = ObClockGenerator::getClock();
    pending_write_ = 0;
    block_frozen_memtable_ = nullptr;

    if (is_for_replay()) {
      mt_ctx_.trans_replay_begin();
    } else {
      mt_ctx_.trans_begin();
    }

    mt_ctx_.set_trans_ctx(this);
    mt_ctx_.set_for_replay(is_for_replay());
    if (!GCONF.enable_record_trace_log) {
      tlog_ = NULL;
    } else {
      tlog_ = &trace_log_;
    }
#ifdef ENABLE_DEBUG_LOG
    tlog_ = &trace_log_;
#endif
    is_inited_ = true;
  } else {
    // reset immediately
    default_init_();
  }

  REC_TRANS_TRACE_EXT2(tlog_, init,
                       OB_ID(addr), (void*)this,
                       OB_ID(trans_id), trans_id,
                       OB_ID(ref), get_ref());
  return ret;
}

int ObTxCtx::init_memtable_ctx_()
{
  int ret = OB_SUCCESS;
  if (OB_FAIL(mt_ctx_.init())) {
  } else if (OB_FAIL(mt_ctx_.enable_lock_table(ls_tx_ctx_mgr_))) {
  } else {
    // the elr_handler.mt_ctx_ is used to notify the lock_wait_mgr for early lock release txn
    elr_handler_.set_memtable_ctx(&mt_ctx_);
  }
  return ret;
}

void ObTxCtx::destroy()
{
  int ret = OB_SUCCESS;
  if (OB_LIKELY(is_inited_)) {

#ifdef ENABLE_DEBUG_LOG
    if (trans_service_ != NULL && NULL != trans_service_->get_defensive_check_mgr()) {
      trans_service_->get_defensive_check_mgr()->del(trans_id_);
    }
#endif

    // Defensive Check 1 : earse ctx id descriptor
    mt_ctx_.reset();

    REC_TRANS_TRACE_EXT2(tlog_, destroy);

    // Defensive Check 2 : apply service callback
    if (!busy_cbs_.is_empty()) {
      TRANS_LOG(ERROR, "some BUG may happen !!!", K(lbt()), K(*this), K(trans_id_),
                K(busy_cbs_.get_size()));
    }

    if (NULL == ls_tx_ctx_mgr_) {
      TRANS_LOG(ERROR, "ls_tx_ctx_mgr_ is null, unexpected error", KP(ls_tx_ctx_mgr_), "context",
                *this);
    } else {
      ls_tx_ctx_mgr_->dec_total_tx_ctx_count();
    }
    // Defensive Check 3 : missing to callback scheduler
    if (!is_for_replay() && need_commit_callback_()) {
      int tx_result = OB_TRANS_UNKNOWN;
      switch (ctx_tx_data_.get_state()) {
      case ObTxCommitData::COMMIT: tx_result = OB_TRANS_COMMITED; break;
      case ObTxCommitData::ABORT: tx_result = OB_TRANS_KILLED; break;
      default:
        TRANS_LOG(ERROR, "oops! unexpected tx_state in tx data", K(ctx_tx_data_.get_state()));
      }
      TRANS_LOG(ERROR, "missing commit callback, do callback", K(tx_result), KPC(this));
      // NOTE: callback scheduler may introduce deadlock, need take care
      trans_service_->handle_tx_commit_result(trans_id_, tx_result, SCN());
      FORCE_PRINT_TRACE(tlog_, "[missing callback scheduler] ");
    }

    exec_info_.destroy(mds_cache_);

    mds_cache_.destroy();

    if (mds_cache_.is_mem_leak()) {
      TRANS_LOG_RET(ERROR, OB_ERR_UNEXPECTED, "mds memory leak!", K(trans_id_), K(mds_cache_), K(exec_info_), K(ctx_tx_data_), K(create_ctx_scn_),
                    K(ctx_source_), K(ctx_create_time_));
      FORCE_PRINT_TRACE(tlog_, "[check mds mem leak] ");
    }

    ctx_tx_data_.destroy();

    big_segment_info_.reset();

    reset_log_cbs_();

    if (NULL != tlog_) {
      print_trace_log_if_necessary_();
      tlog_ = NULL;
    }

    timeout_task_.destroy();
    trace_info_.reset();
    block_frozen_memtable_ = nullptr;

    is_inited_ = false;
  }
}

void ObTxCtx::default_init_()
{
  // TODO for ObTransCtx
  // lock_.reset();
  stc_.reset();
  commit_cb_.reset();
  pending_callback_param_ = OB_SUCCESS;
  trans_need_wait_wrap_.reset();
  is_exiting_ = false;
  for_replay_ = false;
  has_pending_callback_ = false;

  request_id_ = OB_INVALID_TIMESTAMP;
  session_id_ = 0;
  timeout_task_.reset();
  trace_info_.reset();
  can_elr_ = false;

  is_inited_ = false;
  mt_ctx_.reset();
  end_log_ts_.set_max();
  trans_expired_time_ = INT64_MAX;
  stmt_expired_time_ = INT64_MAX;
  cur_query_start_time_ = 0;
  target_state_ = ObTxState::INIT;
  exec_info_.reset();
  ctx_tx_data_.reset();
  runtime_state_.reset();
  reset_log_cbs_();
  last_op_sn_ = 0;
  last_scn_.reset();
  first_scn_.reset();
  rec_log_ts_.reset();
  prev_rec_log_ts_.reset();
  big_segment_info_.reset();
  is_ctx_table_merged_ = false;
  mds_cache_.reset();
  create_ctx_scn_.reset();
  ctx_source_ = TxCtxSource::UNKNOWN;
  replay_completeness_.reset();
  is_submitting_redo_log_for_freeze_ = false;
  reserve_allocator_.reset();
  elr_handler_.reset();
  trace_log_.reset();
  has_async_index_redo_ = false;
}

// thread-unsafe
int ObTxCtx::start_trans()
{
  int ret = OB_SUCCESS;
  // first register task timeout = 10s,
  // no need to unregister/register task when sp transaction commit
  int64_t default_timeout_us = 10000000;
  const int64_t left_time = trans_expired_time_ - ObClockGenerator::getClock();

  if (IS_NOT_INIT) {
    TRANS_LOG(WARN, "ObTxCtx not inited");
    ret = OB_NOT_INIT;
  } else if (OB_UNLIKELY(is_exiting_)) {
    TRANS_LOG(WARN, "transaction is exiting", "context", *this);
    ret = OB_TRANS_IS_EXITING;
  } else if (OB_UNLIKELY(is_for_replay())) {
    ret = OB_STATE_NOT_MATCH;
    TRANS_LOG(WARN, "invalid state, transaction is replaying", KR(ret), KPC(this));
  } else if (OB_UNLIKELY(left_time <= 0)) {
    ret = OB_TRANS_TIMEOUT;
    TRANS_LOG(WARN, "transaction is timeout", K(ret), K_(trans_expired_time), KPC(this));
  } else {
    part_trans_action_ = ObPartTransAction::START;
    replay_completeness_.set(true);
    if (left_time > 0 && left_time < default_timeout_us) {
      (void)unregister_timeout_task_();
      if (OB_FAIL(register_timeout_task_(left_time))) {
      }
    }
  }
  if (OB_FAIL(ret)) {
    set_exiting_();
  }
  TRANS_LOG(DEBUG, "start trans", K(ret), K(trans_id_), "ref", get_ref());
  REC_TRANS_TRACE_EXT2(tlog_, start_trans, OB_ID(ret), ret, OB_ID(left_time), left_time, OB_ID(ref),
                       get_ref());

  return ret;
}

int ObTxCtx::trans_kill_()
{
  int ret = OB_SUCCESS;
  TRANS_LOG(INFO, "trans killed", K(trans_id_));

  mt_ctx_.set_tx_rollbacked();

  if (ctx_tx_data_.get_state() == ObTxData::RUNNING) {
    if (OB_FAIL(ctx_tx_data_.set_state(ObTxData::ABORT))) {
    }
  }

  mt_ctx_.trans_kill();

  return ret;
}

int ObTxCtx::trans_clear_(const share::SCN log_ts)
{
  int ret = OB_SUCCESS;

  // For the purpose of the durability(ACID) of the tx ctx, we need to store the
  // rec_log_ts even after the tx ctx has been released. So we decide to store
  // it into aggre_rec_log_ts in the ctx_mgr.
  //
  // While To meet the demand, we should obey two rules:
  // 1. We must push rec_log_ts to aggre_rec_log_ts before tx ctx has been
  //    removed from ctx_mgr(in trans_clear_)
  // 2. We must disallow dump tx_ctx_table after we already push rec_log_ts(in
  //    get_tx_ctx_table_info)
  //
  // What's more, we need not to care about the retain tx_ctx, because it has
  // already meet the durability requirement and is just used for multi-source
  // data.
  share::SCN rec_log_ts = get_rec_log_ts_() == share::SCN::max_scn() ?
    log_ts : get_rec_log_ts_();

  if (is_ctx_table_merged_
      && OB_FAIL(ls_tx_ctx_mgr_->update_aggre_log_ts_wo_lock(rec_log_ts))) {
    TRANS_LOG(ERROR, "update aggre log ts wo lock failed", KR(ret), "context", *this);
  } else {
    ret = mt_ctx_.trans_clear();
  }

  return ret;
}

int ObTxCtx::handle_timeout(const int64_t delay)
{
  int ret = OB_SUCCESS;
  int tmp_ret = OB_SUCCESS;
  const int64_t now = ObClockGenerator::getClock();
  bool tx_expired = is_trans_expired_();
  bool commit_expired = now > stmt_expired_time_;
  common::ObTimeGuard timeguard("part_handle_timeout", 10 * 1000);
  if (OB_SUCC(lock_.lock(5000000 /*5 seconds*/))) {
    CtxLockGuard guard(lock_, false);
    timeguard.click();
    if (IS_NOT_INIT) {
      TRANS_LOG(WARN, "ObTxCtx not inited");
      ret = OB_NOT_INIT;
    } else if (OB_UNLIKELY(is_exiting_)) {
      TRANS_LOG(WARN, "transaction is exiting", "context", *this);
      ret = OB_TRANS_IS_EXITING;
    } else if (!timeout_task_.is_registered()) {
      // timer task is canceled, do nothing
    } else {
      timeguard.click();
      (void)unregister_timeout_task_();
      update_commit_retry_timeout_();
      timeout_task_.set_running(true);

      if (trans_expired_time_ > 0 && trans_expired_time_ < INT64_MAX) {
        if (part_trans_action_ == ObPartTransAction::COMMIT || part_trans_action_ == ObPartTransAction::ABORT) {
          if (now >= trans_expired_time_ + OB_TRANS_WARN_USE_TIME) {
            tmp_ret = OB_TRANS_COMMIT_TOO_MUCH_TIME;
            LOG_DBA_ERROR_V2(OB_TRANS_COMMIT_COST_TOO_MUCH_TIME, tmp_ret,
                             "Transaction commit cost too much time. ",
                             "trans_id is ", trans_id_, ". ",
                             "[suggestion] You can query GV$OB_PROCESSLIST view to get more information.");
          }
        } else {
          if (now >= ctx_create_time_ + OB_TRANS_WARN_USE_TIME) {
            print_first_mvcc_callback_();
            tmp_ret = OB_TRANS_LIVE_TOO_MUCH_TIME;
            LOG_DBA_WARN_V2(OB_TRANS_LIVE_TOO_LONG, tmp_ret,
                            "Transaction live too long without commit or abort. ",
                            "trans_id is ", trans_id_, ". ",
                            "[suggestion] This may be normal and simply because the client hasn't executed the 'commit' command yet. ",
                            "You can query GV$OB_PROCESSLIST view to get more information ",
                            "and confirm whether you need to submit this transaction.");
          }
        }
      }

      if (mds_cache_.need_retry_submit_mds()) {
        if (OB_TMP_FAIL(submit_log_impl_(ObTxLogType::TX_MULTI_DATA_SOURCE_LOG))) {
        } else {
          mds_cache_.set_need_retry_submit_mds(false);
        }
      }

      // handle commit timeout on root node
      if (!is_for_replay() && part_trans_action_ == ObPartTransAction::COMMIT) {
        if (tx_expired) {
          tmp_ret = post_tx_commit_resp_(OB_TRANS_TIMEOUT);
          TRANS_LOG(INFO, "callback scheduler txn has timeout", K(tmp_ret), KPC(this));
        } else if (commit_expired) {
          tmp_ret = post_tx_commit_resp_(OB_TRANS_STMT_TIMEOUT);
          TRANS_LOG(INFO, "callback scheduler txn commit has timeout", K(tmp_ret), KPC(this));
        } else {
          // make scheduler retry commit if clog disk has fatal error
          bool clog_is_full = false;
          bool clog_is_hang = false;
          logservice::ObLogService *log_service = ::oceanbase::share::server_service<::oceanbase::logservice::ObLogService>();
          if (OB_ISNULL(log_service)) {
            ret = OB_ERR_UNEXPECTED;
            TRANS_LOG(WARN, "log service is null", KR(ret));
          } else if (OB_FAIL(logservice::check_clog_disk_full_or_hang(
              *log_service, clog_is_full, clog_is_hang))) {
          } else if (clog_is_full || clog_is_hang) {
            tmp_ret = post_tx_commit_resp_(OB_EAGAIN);
            TRANS_LOG(WARN, "clog disk has fatal error, make scheduler retry commit", K(tmp_ret), KPC(this));
          }
        }
      }

      // go to preapre state when recover from redo complete
      if (!is_for_replay()) {
        if (ObTxState::REDO_COMPLETE == get_downstream_state()) {
          if (!is_logging_()) {
            if (OB_FAIL(one_phase_commit_())) {
            } else {
              part_trans_action_ = ObPartTransAction::COMMIT;
            }
          }
        }
      }

      // retry commiting for every node
      if (!is_for_replay() && is_committing_()) {
        try_submit_next_log_();
      }

      // retry submit abort log for local tx abort
      //
      // Force-abort may set a target state before the durable state catches up.
      if (!is_for_replay()
          && get_target_state() == ObTxState::ABORT
          && get_target_state() != get_downstream_state()) {
        if (OB_FAIL(compensate_abort_log_())) {
        }
      }

      // if not committing, abort txn if it was expired
      if (!is_for_replay() && !is_committing_() && tx_expired) {
        if (OB_FAIL(abort_(OB_TRANS_TIMEOUT))) {
        }
      }

      // register timeout task again if need
      if (!is_for_replay() && !is_exiting_) {
        const int64_t timeout_left = is_committing_() ? commit_retry_timeout_ :
            MIN(MAX_TRANS_COMMIT_RETRY_TIMEOUT_US, MAX(trans_expired_time_ - now, 1000 * 1000));
        if (OB_FAIL(register_timeout_task_(timeout_left))) {
        }
      }

      timeout_task_.set_running(false);
      timeguard.click();
    }
    REC_TRANS_TRACE_EXT2(tlog_, handle_timeout, OB_ID(ret), ret, OB_ID(used), timeguard, OB_ID(ref),
                         get_ref());

    TRANS_LOG(INFO,
              "handle timeout",
              K(ret),
              K(*this),
              K(tx_expired),
              K(commit_expired),
              K(delay));
    if (busy_cbs_.get_size() > 0) {
      TRANS_LOG(INFO, "trx is waiting log_cb", K(busy_cbs_.get_size()), KPC(busy_cbs_.get_first()),
                KPC(busy_cbs_.get_last()));
    }
  } else {
    TRANS_LOG(WARN, "failed to acquire lock in specified time", K_(trans_id));
    unregister_timeout_task_();
    register_timeout_task_(delay);
  }

  return ret;
}

int ObTxCtx::kill(const KillTransArg &arg, ObTxCommitCallback *&cb_list)
{
  int ret = OB_SUCCESS;
  int tmp_ret = OB_SUCCESS;
  int cb_param = OB_TRANS_UNKNOWN;

  common::ObTimeGuard timeguard("part_kill", 10 * 1000);
  CtxLockGuard guard(lock_);

  if (IS_NOT_INIT) {
    TRANS_LOG(WARN, "ObTxCtx not inited");
    ret = OB_NOT_INIT;
  } else if (is_exiting_) {
    TRANS_LOG(INFO, "trans is existing when being killed", K(*this), K(arg));
  } else {
    bool notify_scheduler_tx_killed = false;
    if (arg.graceful_) {
      if (FALSE_IT(notify_scheduler_tx_killed = !is_for_replay() && part_trans_action_ == ObPartTransAction::START)) {
      } else if (has_persisted_log_() || is_logging_()) {
        // submit abort_log and wait success
        if (OB_FAIL(do_local_tx_end_(TxEndAction::ABORT_TX))) {
        } else {
          TRANS_LOG(INFO, "kill trx with abort_log success", "context", *this);
        }
        if (OB_SUCC(ret)) {
          ret = OB_TRANS_CANNOT_BE_KILLED;
        }
      } else {
        cb_param = OB_TRANS_KILLED;
      }
      if (OB_SUCCESS != ret) {
      }
    }
    if (OB_SUCC(ret)) {
      if (OB_FAIL(do_local_tx_end_(TxEndAction::KILL_TX_FORCEDLY))) {
      }
      notify_scheduler_tx_killed = !is_for_replay() && part_trans_action_ == ObPartTransAction::START;
      // if ctx was killed gracefully or forcely killed
      // notify scheduler commit result, if in committing
      if (!is_for_replay() && part_trans_action_ == ObPartTransAction::COMMIT) {
      // notify scheduler only if commit callback has not been armed
        if (commit_cb_.is_enabled() && !commit_cb_.is_inited()) {
          if (OB_TMP_FAIL(prepare_commit_cb_for_role_change_(cb_param, cb_list))) {
            TRANS_LOG(WARN, "prepare commit cb fail", K(tmp_ret), K(cb_param), KPC(this));
            ret = (ret == OB_SUCCESS) ? tmp_ret : ret;
          }
        }
      }
    }
    if (notify_scheduler_tx_killed) {
      notify_tx_killed_(arg.graceful_
                        ? ObTxAbortCause::WRITE_STATE_KILLED_GRACEFULLY
                        : ObTxAbortCause::WRITE_STATE_KILLED_FORCEDLY);
    }
  }
  TRANS_LOG(WARN, "trans is killed", K(ret), K(arg), K(cb_param), KPC(this));
  REC_TRANS_TRACE_EXT2(tlog_, kill, OB_ID(ret), ret, OB_ID(arg1), arg.graceful_, OB_ID(used),
                       timeguard.get_diff(), OB_ID(ref), get_ref());
  return ret;
}

/*
 * commit - start to commiting txn
 *
 * the commiting is asynchronous
 *
 * @commit_time:    STC reference
 * @expire_ts:      timestamp in micorseconds after which
 *                  commit result is not concerned for the caller
 * @request_id:     commit request identifier
 *
 * Return:
 * OB_SUCCESS - request was accepted and promise result would be
 *              reported via calling back the caller (either
 *              in message or in procedure call)
 * OB_TRANS_COMMITED - has committed
 * OB_TRANS_KILLED   - has aborted
 * OB_ERR_XXX - the request was rejected, can not be handle
 *              caller can retry commit or choice to abort txn
 */
int ObTxCtx::commit(const MonotonicTs &commit_time,
                           const int64_t &expire_ts,
                           const int64_t &request_id)
{
  int ret = OB_SUCCESS;
  int tmp_ret = OB_SUCCESS;
  CtxLockGuard guard(lock_);
  if (IS_NOT_INIT) {
    TRANS_LOG(WARN, "ObTxCtx not inited");
    ret = OB_NOT_INIT;
  } else if (OB_UNLIKELY(0 >= expire_ts)) {
    ret = OB_INVALID_ARGUMENT;
    TRANS_LOG(WARN, "invalid argument", K(ret), K(expire_ts), KPC(this));
  } else if (OB_UNLIKELY(is_for_replay())) {
    ret = OB_STATE_NOT_MATCH;
    TRANS_LOG(WARN, "transaction is replaying", KR(ret), KPC(this));
  } else if (!(ObTxState::INIT == get_downstream_state() ||
      (ObTxState::REDO_COMPLETE == get_downstream_state() && part_trans_action_ < ObPartTransAction::COMMIT))) {
    ObTxState state = get_downstream_state();
    switch (state) {
    case ObTxState::ABORT:
      ret = OB_TRANS_KILLED;
      break;
    case ObTxState::PRE_COMMIT:
    case ObTxState::COMMIT:
    case ObTxState::CLEAR:
      ret = OB_TRANS_COMMITED;
      break;
    case ObTxState::PREPARE:
    default:
      ret = OB_SUCCESS;
    }
    TRANS_LOG(WARN, "tx is committing", K(state), KPC(this));
  } else if (OB_UNLIKELY(is_exiting_)) {
    ret = OB_TRANS_IS_EXITING;
    TRANS_LOG(WARN, "transaction is exiting", K(ret), KPC(this));
  } else if (OB_UNLIKELY(pending_write_)) {
    ret = OB_ERR_UNEXPECTED;
    TRANS_LOG(WARN, "access in progress", K(ret), K_(pending_write), KPC(this));
  } else if (OB_FAIL(set_commit_request_id_(request_id))) {
  } else if (FALSE_IT(stmt_expired_time_ = expire_ts)) {
  } else {
    exec_info_.mark_write_state();
    if (commit_time.is_valid()) {
      set_stc_(commit_time);
    } else {
      set_stc_by_now_();
    }
    can_elr_ = trans_service_->get_tx_elr_util().is_can_elr();
    if (OB_FAIL(one_phase_commit_())) {
    }
  }
  if (OB_SUCC(ret)) {
    commit_cb_.enable();
    part_trans_action_ = ObPartTransAction::COMMIT;
    last_request_ts_ = ObClockGenerator::getClock();
  }
  REC_TRANS_TRACE_EXT2(tlog_, commit, OB_ID(ret), ret,
                       OB_ID(tid), GETTID(),
                       OB_ID(ref), get_ref());
  if (OB_FAIL(ret) && OB_EAGAIN != ret && OB_TRANS_COMMITED != ret) {
    TRANS_LOG(WARN, "trx commit failed", KR(ret), KPC(this));
  }
  return ret;
}

int ObTxCtx::one_phase_commit_()
{
  int ret = OB_SUCCESS;

  if (OB_FAIL(do_local_tx_end_(TxEndAction::COMMIT_TX))) {
  }

  return ret;
}

int ObTxCtx::check_modify_schema_elapsed(
    const ObTabletID &tablet_id,
    const int64_t schema_version)
{
  int ret = OB_SUCCESS;
  if (OB_SUCC(lock_.lock(100000 /*100 ms*/))) {
    CtxLockGuard guard(lock_, false);

    if (IS_NOT_INIT) {
      TRANS_LOG(WARN, "ObTxCtx not inited");
      ret = OB_NOT_INIT;
    } else if (OB_UNLIKELY(schema_version <= 0) ||
               OB_UNLIKELY(!tablet_id.is_valid())) {
      TRANS_LOG(WARN, "invalid argument", K(tablet_id), K(schema_version),
                "context", *this);
      ret = OB_INVALID_ARGUMENT;
    } else if (is_exiting_) {
      // do nothing
    } else if (is_for_replay()) {
      ret = OB_STATE_NOT_MATCH;
      TRANS_LOG(WARN, "cannot check modify schema on replay context", K(ret),
                K(tablet_id), K(schema_version), "context", *this);
    } else if (OB_FAIL(mt_ctx_.check_modify_schema_elapsed(tablet_id,
                                                           schema_version))) {
      if (OB_EAGAIN != ret) {
        TRANS_LOG(WARN, "check modify schema elapsed failed", K(ret),
                  K(tablet_id), K(schema_version));
      } else if (REACH_TIME_INTERVAL(10 * 1000 * 1000)) {
        TRANS_LOG(INFO, "current transaction not end, need retry", K(ret),
                  K(tablet_id), K(schema_version), "context", *this);
      }
    } else {
      // do nothing
    }
  } else {
    TRANS_LOG(WARN, "spin lock time out after 100 ms", K(ret));
    ret = OB_EAGAIN;
  }

  return ret;
}

int ObTxCtx::check_modify_time_elapsed(
    const ObTabletID &tablet_id,
    const int64_t timestamp)
{
  int ret = OB_SUCCESS;
  if (OB_SUCC(lock_.lock(100000 /*100 ms*/))) {
    CtxLockGuard guard(lock_, false);

    if (IS_NOT_INIT) {
      TRANS_LOG(WARN, "ObTxCtx not inited");
      ret = OB_NOT_INIT;
    } else if (OB_UNLIKELY(timestamp <= 0) ||
               OB_UNLIKELY(!tablet_id.is_valid())) {
      TRANS_LOG(WARN, "invalid argument", K(tablet_id), K(timestamp),
                "context", *this);
      ret = OB_INVALID_ARGUMENT;
    } else if (is_exiting_) {
      // do nothing
    } else if (is_for_replay()) {
      ret = OB_STATE_NOT_MATCH;
      TRANS_LOG(WARN, "cannot check modify time on replay context", K(ret),
                K(tablet_id), K(timestamp), "context", *this);
    } else if (OB_FAIL(mt_ctx_.check_modify_time_elapsed(tablet_id,
                                                         timestamp))) {
      if (OB_EAGAIN != ret) {
        TRANS_LOG(WARN, "check modify time elapsed failed", K(ret),
                  K(tablet_id), K(timestamp));
      } else if (REACH_TIME_INTERVAL(10 * 1000 * 1000)) {
        TRANS_LOG(INFO, "current transaction not end, need retry", K(ret),
                  K(tablet_id), K(timestamp), "context", *this);
      }
    } else {
      // do nothing
    }
  } else {
    TRANS_LOG(WARN, "spin lock time out after 100 ms", K(ret));
    ret = OB_EAGAIN;
  }

  return ret;
}

int ObTxCtx::iterate_tx_obj_lock_op(ObLockOpIterator &iter) const
{
  int ret = OB_SUCCESS;
  if (IS_NOT_INIT) {
    TRANS_LOG(WARN, "ObTxCtx not inited");
    ret = OB_NOT_INIT;
  } else if (is_exiting_) {
    // do nothing
    // we just consider the active trans
  } else if (OB_FAIL(mt_ctx_.iterate_tx_obj_lock_op(iter))) {
  } else {
    // do nothing
    // should not set iterator is ready here,
    // because it may iterate other tx_ctx then
  }

  return ret;
}

int ObTxCtx::iterate_tx_lock_priority_list(ObPrioOpIterator &iter) const
{
  int ret = OB_SUCCESS;
  if (IS_NOT_INIT) {
    TRANS_LOG(WARN, "ObTxCtx not inited");
    ret = OB_NOT_INIT;
  } else if (is_exiting_) {
    // do nothing
    // we just consider the active trans
  } else if (OB_FAIL(mt_ctx_.iterate_tx_lock_priority_list(iter))) {
  } else {
    // do nothing
    // should not set iterator is ready here,
    // because it may iterate other tx_ctx then
  }

  return ret;
}

int ObTxCtx::iterate_tx_lock_stat(ObTxLockStatIterator &iter)
{
  int ret = OB_SUCCESS;
  ObMemtableKeyArray memtable_key_info_arr;

  if (IS_NOT_INIT) {
    TRANS_LOG(WARN, "ObTxCtx not inited");
    ret = OB_NOT_INIT;
  } else if (OB_FAIL(get_memtable_key_arr(memtable_key_info_arr))) {
  } else {
    // If the row has been dumped into sstable, we can not get the
    // memtable key info since the callback of it has been dropped.
    // So we need to judge whether the transaction has been dumped
    // into sstable here. Futhermore, we need to fitler out ratain
    // transactions by !tx_ctx->is_exiting().
    if (memtable_key_info_arr.empty() && !is_exiting() && get_memtable_ctx()->maybe_has_undecided_callback()) {
      ObMemtableKeyInfo key_info;
      memtable_key_info_arr.push_back(key_info);
    }
    int64_t count = memtable_key_info_arr.count();
    for (int i = 0; OB_SUCC(ret) && i < count; i++) {
      ObTxLockStat tx_lock_stat;
      if (OB_FAIL(tx_lock_stat.init(get_addr(),
                                    memtable_key_info_arr.at(i),
                                    get_session_id(),
                                    get_trans_id(),
                                    get_ctx_create_time(),
                                    get_trans_expired_time()))) {
      } else if (OB_FAIL(iter.push(tx_lock_stat))) {
      } else {
        // do nothing
      }
    }
  }

  return ret;
}

int ObTxCtx::trans_replay_abort_(const SCN &final_log_ts)
{
  int ret = OB_SUCCESS;

  if (OB_FAIL(mt_ctx_.trans_replay_end(false, /*commit*/
                                       ctx_tx_data_.get_commit_version(),
                                       final_log_ts))) {
  }

  return ret;
}

int ObTxCtx::trans_replay_commit_(const SCN &commit_version,
                                         const SCN &final_log_ts,
                                         const uint64_t checksum)
{
  ObTimeGuard tg("trans_replay_commit", 50 * 1000);
  int ret = OB_SUCCESS;
  int tmp_ret = OB_SUCCESS;

  if (OB_FAIL(update_publish_version_(commit_version, true))) {
  } else {
    int64_t freeze_ts = 0;
    if (OB_FAIL(mt_ctx_.trans_replay_end(true, /*commit*/
                                         commit_version,
                                         final_log_ts,
                                         checksum))) {
    }
  }

  return ret;
}

int ObTxCtx::update_publish_version_(const SCN &publish_version, const bool for_replay)
{
  int ret = OB_SUCCESS;
  if (!publish_version.is_valid()) {
    ret = OB_INVALID_ARGUMENT;
    TRANS_LOG(WARN, "invalid argument", KR(ret), K(publish_version));
  } else if (OB_FAIL(ctx_tx_data_.set_commit_version(publish_version))) {
  } else {
    trans_service_->get_tx_version_mgr().update_max_commit_ts(publish_version, false);
    REC_TRANS_TRACE_EXT2(tlog_, push_max_commit_version, OB_ID(trans_version), publish_version,
                         OB_ID(ref), get_ref());
  }

  return ret;
}


bool ObTxCtx::is_logging_() const { return !busy_cbs_.is_empty(); }

bool ObTxCtx::need_force_abort_() const
{
  return runtime_state_.is_force_abort() && !runtime_state_.is_state_log_submitted();
}

bool ObTxCtx::is_force_abort_logging_() const
{
  return runtime_state_.is_force_abort() && runtime_state_.is_state_log_submitting();
}

bool ObTxCtx::has_persisted_log_() const
{
  return exec_info_.max_applying_log_ts_.is_valid() ||
    // for ctx created by parallel replay, and no serial log replayed
    // the max_applying_log_ts_ has not been set, in this case use
    // replay_completeness to distinguish with situations on leader
    (is_for_replay() && replay_completeness_.is_unknown());
}

int ObTxCtx::get_prepare_version_if_prepared(bool &is_prepared, SCN &prepare_version)
{
  int ret = OB_SUCCESS;
  ObTxState cur_state = exec_info_.state_;
  // strong memory barrier on ARM
  WEAK_BARRIER();

  if (ObTxState::PREPARE == cur_state || ObTxState::PRE_COMMIT == cur_state) {
    is_prepared = true;
    prepare_version = exec_info_.prepare_version_;
  } else if (ObTxState::COMMIT == cur_state || ObTxState::ABORT == cur_state
             || ObTxState::CLEAR == cur_state) {
    is_prepared = true;
    prepare_version.set_max();
  } else {
    is_prepared = false;
    prepare_version.set_max();
  }
  if (is_prepared && OB_INVALID_SCN_VAL == prepare_version.get_val_for_gts()) {
    TRANS_LOG(ERROR, "invalid prepare version", K(cur_state));
    // try lock
    print_trace_log();
  }

  return ret;
}

int ObTxCtx::get_memtable_key_arr(ObMemtableKeyArray &memtable_key_arr)
{
  int ret = OB_SUCCESS;
  if (OB_SUCCESS == lock_.try_lock()) {
    if (IS_NOT_INIT || is_for_replay() || is_exiting_) {
    } else if (OB_FAIL(mt_ctx_.get_memtable_key_arr(memtable_key_arr))) {
    } else {
      // do nothing
    }
    lock_.unlock();
  } else {
    ObMemtableKeyInfo info;
    info.init(1);
    memtable_key_arr.push_back(info);
  }

  return ret;
}

bool ObTxCtx::can_be_recycled_()
{
  bool bool_ret = true;
  if (IS_NOT_INIT) {
    bool_ret = false;
  } else if (is_exiting_) {
    bool_ret = false;
  } else if (is_for_replay()) {
    bool_ret = false;
  } else if (ObTxState::REDO_COMPLETE < get_target_state()) {
    bool_ret = false;
  } else if (is_logging_()) { // FIXME. xiaoshi
    bool_ret = false;
  } else if (ObTxState::REDO_COMPLETE < get_downstream_state()) {
    bool_ret = false;
  } else {
  }
  return bool_ret;
}

bool ObTxCtx::need_to_check_tx_status_()
{
  bool bool_ret = false;
  if (can_be_recycled_()) {
    if (ObTimeUtility::current_time() - last_check_tx_status_ts_
        < CHECK_TX_STATUS_INTERVAL) {
      bool_ret = false;
    } else {
      bool_ret = true;
    }
  }
  return bool_ret;
}

int ObTxCtx::gc_ctx_()
{
  int ret = OB_SUCCESS;
  bool has_redo_log = false;
  if (OB_FAIL(prepare_mul_data_source_tx_end_(false))) {
  } else {
    TRANS_LOG(INFO, "[TRANS GC] participant will **abort** itself due to scheduler has quit", KPC(this));
    REC_TRANS_TRACE_EXT2(tlog_, tx_ctx_gc, OB_ID(ref), get_ref());
    if (need_commit_callback_()) {
      TRANS_LOG(INFO, "[TRANS GC] transaction owner has quit, skip commit callback", KP(this),
                K_(trans_id));
      commit_cb_.disable();
    }
    if (OB_FAIL(do_local_tx_end_(TxEndAction::ABORT_TX))) {
    }
  }
  return ret;
}

int ObTxCtx::check_tx_status()
{
  if (OB_SUCCESS == lock_.try_lock()) {
    CtxLockGuard guard(lock_, false);
    if (need_to_check_tx_status_()) {
      const int ctx_status = runtime_state_.is_force_abort() ? OB_TRANS_KILLED : OB_SUCCESS;
      report_write_ctx_status_(ctx_status, true);
      last_check_tx_status_ts_ = ObClockGenerator::getClock();
    }

    if (is_committing_() || ObClockGenerator::getClock() > trans_expired_time_) {
      (void)check_and_register_timeout_task_();
    }
  }
  return OB_SUCCESS;
}

int ObTxCtx::recover_tx_ctx_table_info(ObTxCtxTableInfo &ctx_info)
{
  int ret = OB_SUCCESS;
  CtxLockGuard guard(lock_);

  ObTxBufferNodeArray _unused_;
  if (!ctx_info.is_valid()) {
    ret = OB_INVALID_ARGUMENT;
    TRANS_LOG(WARN, "invalid argument", K(ctx_info));
  } else if (OB_FAIL(mt_ctx_.recover_from_table_lock_durable_info(ctx_info.table_lock_info_))) {
  } else if (OB_FAIL(ctx_tx_data_.recover_tx_data(ctx_info.tx_data_guard_.tx_data()))) {
  } else if (OB_FAIL(exec_info_.assign(ctx_info.exec_info_))) {
  } else {
    trans_id_ = ctx_info.tx_id_;
    set_target_state(get_downstream_state());

    if (ObTxState::REDO_COMPLETE == get_downstream_state()) {
      runtime_state_.set_info_log_submitted();
    }
    if (exec_info_.prepare_version_.is_valid()) {
      mt_ctx_.set_trans_version(exec_info_.prepare_version_);
    }
    exec_info_.multi_data_source_.reset();
    exec_info_.mds_buffer_ctx_array_.reset();
    if (OB_FAIL(ret)) {
      // do nothing
    } else if (OB_FAIL(deep_copy_mds_array_(ctx_info.exec_info_.multi_data_source_, _unused_))) {
    } else if (exec_info_.need_checksum_ &&
               OB_FAIL(mt_ctx_.update_checksum(exec_info_.checksum_,
                                               exec_info_.checksum_scn_))) {
      TRANS_LOG(ERROR, "recover checksum failed", K(ret), KPC(this), K(ctx_info));
    } else {
      is_ctx_table_merged_ = true;
      replay_completeness_.set(true);
    }

    if (OB_SUCC(ret) && !exec_info_.need_checksum_) {
      mt_ctx_.set_skip_checksum_calc();
    }

    if (OB_FAIL(ret)) {
      // do nothing
    } else if (ObTxState::COMMIT == exec_info_.state_ || ObTxState::ABORT == exec_info_.state_) {
      set_exiting_();
      TRANS_LOG(INFO, "set exiting with a finished trx in recover", K(ret), KPC(this));
    }

    if (OB_SUCC(ret)) {
      if (exec_info_.serial_final_scn_.is_valid()) {
        recovery_parallel_logging_();
      }
      create_ctx_scn_ = exec_info_.max_applying_log_ts_;

    }
    TRANS_LOG(INFO, "[TRANS RECOVERY] recover tx ctx table info succeed", K(ret), KPC(this), K(ctx_info));
  }

  REC_TRANS_TRACE_EXT2(tlog_,
                       recover_from_ctx_table,
                       OB_ID(ret),
                       ret,
                       OB_ID(max_applying_ts),
                       ctx_info.exec_info_.max_applying_log_ts_,
                       OB_ID(state),
                       ctx_info.exec_info_.state_);
  return ret;
}

// Checkpoint the tx ctx table
int ObTxCtx::serialize_tx_ctx_to_buffer(ObTxLocalBuffer &buffer, int64_t &serialize_size)
{
  int ret = OB_SUCCESS;
  ObTxCtxTableInfo ctx_info;

  CtxLockGuard guard(lock_);
  // 1. Tx ctx has already exited, so it means that it may have no chance to
  //    push its rec_log_ts to aggre_rec_log_ts, so we must not persist it
  if (is_exiting_) {
    ret = OB_TRANS_CTX_NOT_EXIST;
    TRANS_LOG(INFO, "tx ctx has exited", K(ret), KPC(this));
  // 2. Tx ctx has no persisted log, so donot need persisting
  } else if (!exec_info_.max_applying_log_ts_.is_valid()) {
    ret = OB_TRANS_CTX_NOT_EXIST;
    TRANS_LOG(INFO, "tx ctx has no persisted log", K(ret), KPC(this));
    // 3. Tx ctx replay incomplete, skip
  } else if (replay_completeness_.is_incomplete()) {
    // NB: we need refresh rec log ts for incomplete replay ctx
    if (OB_FAIL(refresh_rec_log_ts_())) {
    } else {
      ret = OB_TRANS_CTX_NOT_EXIST;
      TRANS_LOG(INFO, "tx ctx is an incomplete replay ctx", K(ret), KPC(this));
    }
    // ctx created by replay redo of parallel replay, skip
  } else if (replay_completeness_.is_unknown()) {
    if (OB_FAIL(refresh_rec_log_ts_())) {
    } else {
      ret = OB_TRANS_CTX_NOT_EXIST;
      TRANS_LOG(INFO, "tx ctx replay completeness unknown, skip checkpoint", K(ret), KPC(this));
    }
  // 4. Fetch the current state of the tx ctx table
  } else if (OB_FAIL(get_tx_ctx_table_info_(ctx_info))) {
  } else if (OB_UNLIKELY(!ctx_info.is_valid())) {
    ret = OB_ERR_UNEXPECTED;
    TRANS_LOG(WARN, "tx ctx info invalid", K(ret), K(ctx_info));
  // 5. Refresh the rec_log_ts for the next checkpoint
  } else if (OB_FAIL(refresh_rec_log_ts_())) {
  } else {
    SpinRLockManualGuard tx_op_guard;
    if (ctx_info.tx_data_guard_.tx_data()->op_guard_.is_valid()) {
      tx_op_guard.lock(ctx_info.tx_data_guard_.tx_data()->op_guard_->get_lock());
    }
    // 6. Do serialize
    int64_t pos = 0;
    serialize_size = ctx_info.get_serialize_size();
    if (OB_FAIL(buffer.reserve(serialize_size))) {
    } else if (OB_FAIL(ctx_info.serialize(buffer.get_ptr(), serialize_size, pos))) {
    } else {
      is_ctx_table_merged_ = true;
      serialize_size = pos;
    }
  }
  TRANS_LOG(INFO, "[TRANS CHECKPOINT] checkpoint trans ctx", K(ret), K_(trans_id), KP(this));
  return ret;
}

const SCN ObTxCtx::get_rec_log_ts() const
{
  return get_rec_log_ts_();
}

const SCN ObTxCtx::get_rec_log_ts_() const
{
  share::SCN log_ts = SCN::max_scn();

  share::SCN rec_log_ts = rec_log_ts_.atomic_load();
  share::SCN prev_rec_log_ts = prev_rec_log_ts_.atomic_load();

  // Before the checkpoint of the tx ctx table is succeed, we should still use
  // the prev_log_ts. And after successfully checkpointed, we can use the new
  // rec_log_ts if exist
  if (prev_rec_log_ts.is_valid()) {
    log_ts = prev_rec_log_ts;
  } else if (rec_log_ts.is_valid()) {
    log_ts = rec_log_ts;
  }


  return log_ts;
}

int ObTxCtx::on_tx_ctx_table_flushed()
{
  int ret = OB_SUCCESS;
  CtxLockGuard guard(lock_);
  // To mark the checkpoint is succeed, we reset the prev_rec_log_ts
  prev_rec_log_ts_.atomic_store(share::SCN::invalid_scn());

  return ret;
}

int64_t ObTxCtx::to_string(char* buf, const int64_t buf_len) const
{
  int64_t len1 = 0;
  int64_t len2 = 0;
  len1 = ObTransCtx::to_string(buf, buf_len);
  if (lock_.is_locked_by_self()) {
    len2 = to_string_(buf + len1, buf_len - len1);
  }
  return len1 + len2;
}

int ObTxCtx::remove_callback_for_uncommited_txn(
  const memtable::ObMemtableSet *memtable_set)
{
  int ret = OB_SUCCESS;
  CtxLockGuard guard(lock_);

  if (IS_NOT_INIT) {
    TRANS_LOG(WARN, "ObTxCtx not inited");
    ret = OB_NOT_INIT;
  } else if (OB_ISNULL(memtable_set)) {
    ret = OB_INVALID_ARGUMENT;
    TRANS_LOG(WARN, "memtable is NULL", K(memtable_set));
  } else if (OB_FAIL(mt_ctx_.remove_callback_for_uncommited_txn(memtable_set))) {
  }

  return ret;
}

// the semantic of submit redo for freeze is
// should flush all redos bellow specified freeze_clock (inclusive)
// otherwise, need return some error to caller to indicate need retry
int ObTxCtx::submit_redo_log_for_freeze(const uint32_t freeze_clock)
{
  int ret = OB_SUCCESS;
  ObTimeGuard tg("submit_redo_for_freeze_log", 100000);
  bool submitted = false;
  bool need_submit = fast_check_need_submit_redo_for_freeze_();
  if (need_submit) {
    CtxLockGuard guard(lock_);
    tg.click();
    ret = submit_redo_log_for_freeze_(submitted, freeze_clock);
    tg.click();
    if (submitted) {
      REC_TRANS_TRACE_EXT2(tlog_, submit_log_for_freeze, OB_Y(ret),
                           OB_ID(used), tg.get_diff(), OB_ID(ref), get_ref());
    }
    if (OB_TRANS_HAS_DECIDED == ret || OB_BLOCK_FROZEN == ret) {
      ret = OB_SUCCESS;
    }
  }

  return ret;
}

int ObTxCtx::submit_redo_after_write(const bool force, const ObTxSEQ &write_seq_no)
{
  int ret = OB_SUCCESS;
  TRANS_LOG(TRACE, "submit_redo_after_write", K(force), K(write_seq_no), K_(trans_id), K(mt_ctx_.get_pending_log_size()));
  ObTimeGuard tg("submit_redo_for_after_write", 100000);
  if (force || mt_ctx_.pending_log_size_too_large(write_seq_no)) {
    bool parallel_logging = false;
#define LOAD_PARALLEL_LOGGING parallel_logging = exec_info_.serial_final_scn_.atomic_load().is_valid()
    LOAD_PARALLEL_LOGGING;
    if (!parallel_logging) {
      int submitted_cnt = 0;
      if (force || OB_SUCCESS == lock_.try_lock()) {
        CtxLockGuard guard(lock_, force /* need lock */);
        // double check parallel_logging is on
        LOAD_PARALLEL_LOGGING;
        if (!parallel_logging) {
          ret = serial_submit_redo_after_write_(submitted_cnt);
        }
      }
      if (submitted_cnt > 0 && OB_EAGAIN == ret) {
        // has remains, try fill after switch to parallel logging
        LOAD_PARALLEL_LOGGING;
      }
    }
#undef LOAD_PARALLEL_LOGGING
    tg.click("serial_log");
    if (parallel_logging && OB_SUCC(lock_.try_rdlock_flush_redo())) {
      if (OB_SUCC(check_can_submit_redo_())) {
        if (is_committing_()) {
          ret = force ? OB_TRANS_HAS_DECIDED : OB_SUCCESS;
        } else {
          ObTxRedoSubmitter submitter(*this, mt_ctx_);
          if (OB_FAIL(submitter.parallel_submit(write_seq_no))) {
            if (!force && (OB_ITER_END == ret          // blocked by others, current remains
                           || OB_NEED_RETRY == ret     // acquire lock failed
                           )) {
              ret = OB_SUCCESS;
            }
          }
        }
      }
      lock_.unlock_flush_redo();
    }
    if (!force && (OB_TRANS_HAS_DECIDED == ret // do committing
                   || OB_BLOCK_FROZEN == ret   // memtable logging blocked
                   || OB_EAGAIN == ret         // partial submitted or submit to log-service fail
                   )) {
      ret = OB_SUCCESS;
    }
  }
  return ret;
}

int ObTxCtx::serial_submit_redo_after_write_(int &submitted_cnt)
{
  int ret = OB_SUCCESS;
  if (OB_SUCC(check_can_submit_redo_())) {
    int64_t before_submit_pending_size = mt_ctx_.get_pending_log_size();
    bool should_switch = should_switch_to_parallel_logging_();
    ObTxRedoSubmitter submitter(*this, mt_ctx_);
    ret = submitter.serial_submit(should_switch);
    submitted_cnt = submitter.get_submitted_cnt();
    if (should_switch && submitted_cnt > 0) {
      const share::SCN serial_final_scn = submitter.get_submitted_scn();
      int tmp_ret = switch_to_parallel_logging_(serial_final_scn, exec_info_.max_submitted_seq_no_);
      TRANS_LOG(INFO, "switch to parallel logging", K(tmp_ret),
                K_(trans_id),
                K(serial_final_scn),
                "serial_final_seq_no", exec_info_.serial_final_seq_no_,
                K(before_submit_pending_size),
                "curr_pending_size", mt_ctx_.get_pending_log_size());
    }
  }
  return ret;
}

bool ObTxCtx::should_switch_to_parallel_logging_()
{
  bool ok = false;
  if (GCONF._enable_parallel_redo_logging) {
    const int64_t switch_size = GCONF._parallel_redo_logging_trigger;
    ok = pending_write_ > 1 && mt_ctx_.get_pending_log_size() > switch_size;
#ifdef ENABLE_DEBUG_LOG
    if (!ok) {
      ok = trans_id_ % 5 == 0;  // force 20% transaction go parallel logging
    }
#endif
  }
 return ok;
}

int ObTxCtx::check_can_submit_redo_()
{
  int ret = OB_SUCCESS;
  bool is_tx_committing = ObTxState::INIT != get_downstream_state();
  bool final_log_submitting =
      runtime_state_.is_state_log_submitting() || runtime_state_.is_state_log_submitted();
  if (is_tx_committing
      ||final_log_submitting
      || is_force_abort_logging_()) {
    ret = OB_TRANS_HAS_DECIDED;
  }
  return ret;
}

// Concurrency safe annotation:
// init log_block_ is an local operation
// prepare_log_cb_ is protected by `log_cb_lock_`
int ObTxCtx::prepare_for_submit_redo(ObTxLogCb *&log_cb,
                                            ObTxLogBlock &log_block,
                                            const bool serial_final)
{
  int ret = OB_SUCCESS;
  if (!log_block.is_inited() && OB_FAIL(init_log_block_(log_block, ObTxAdaptiveLogBuf::NORMAL_LOG_BUF_SIZE, serial_final))) {
    TRANS_LOG(WARN, "init log block fail", K(ret));
  } else if (OB_FAIL(prepare_log_cb_(!NEED_FINAL_CB, log_cb)) && OB_TX_NOLOGCB != ret) {
    TRANS_LOG(WARN, "alloc log_cb fail", K(ret));
  }
  return ret;
}

int ObTxCtx::submit_redo_log_for_freeze_(bool &submitted, const uint32_t freeze_clock)
{
  int ret = OB_SUCCESS;
  ATOMIC_STORE(&is_submitting_redo_log_for_freeze_, true);
  if (OB_SUCC(check_can_submit_redo_())) {
    ObTxRedoSubmitter submitter(*this, mt_ctx_);
    if (OB_FAIL(submitter.submit_for_freeze(freeze_clock, true /*display blocked info*/))) {
      if (OB_BLOCK_FROZEN != ret) {
        TRANS_LOG(ERROR, "fail to submit redo log for freeze", K(ret));
        // for some error, txn will be aborted immediately
        handle_submit_log_err_(ObTxLogType::TX_REDO_LOG, ret);
      }
    }
    submitted = submitter.get_submitted_cnt() > 0;
  }
  if (OB_SUCC(ret) || OB_BLOCK_FROZEN == ret) {
    ret = submit_log_impl_(ObTxLogType::TX_MULTI_DATA_SOURCE_LOG);
    if (ret == OB_TRANS_KILLED) {
      ret = OB_TRANS_HAS_DECIDED;
    }
  }
  ATOMIC_STORE(&is_submitting_redo_log_for_freeze_, false);
  return ret;
}

bool ObTxCtx::fast_check_need_submit_redo_for_freeze_() const
{
  bool has_pending_log = true;
  bool blocked = false;
  if (OB_SUCCESS == lock_.try_wrlock_flush_redo()) {
    blocked = mt_ctx_.is_logging_blocked(has_pending_log);
    lock_.unlock_flush_redo();
  }
  return has_pending_log && !blocked;
}


int64_t ObTxCtx::get_part_trans_action() const
{
  return part_trans_action_;
}

bool ObTxCtx::is_table_lock_killed() const
{
  bool is_killed = false;
  is_killed = (mt_ctx_.is_table_lock_killed() ||
               (exec_info_.state_ == ObTxState::ABORT));
  return is_killed;
}

int ObTxCtx::compensate_abort_log_()
{
  int ret = OB_SUCCESS;
  if (is_force_abort_logging_()) {
    // do nothing
  } else if(OB_FALSE_IT(runtime_state_.set_force_abort())) {

  } else if (OB_FAIL(submit_log_impl_(ObTxLogType::TX_ABORT_LOG))) {
    int tmp_ret = OB_SUCCESS;
    if (OB_TMP_FAIL(restart_commit_retry_timer_())) {
    }
    TRANS_LOG(WARN, "submit abort log failed", KR(ret), K(*this));
  } else {
  }
  TRANS_LOG(INFO, "compensate abort log", K(ret), KPC(this));
  return ret;
}

int ObTxCtx::abort_(int reason)
{
  int ret = OB_SUCCESS;
  REC_TRANS_TRACE_EXT2(tlog_, abort_, OB_ID(reason), reason);
  if (OB_FAIL(do_local_tx_end_(TxEndAction::ABORT_TX))) {
  }
  part_trans_action_ = ObPartTransAction::ABORT;
  // if abort was caused by internal impl reason, don't disturb
  if (ObTxAbortCause::IMPLICIT_ROLLBACK != reason) {
    TRANS_LOG(INFO, "tx abort", K(ret), K(reason), "reason_str", ObTxAbortCauseNames::of(reason), KPC(this));
  }
  return ret;
}

int ObTxCtx::update_max_commit_version_()
{
  int ret = OB_SUCCESS;
  trans_service_->get_tx_version_mgr().update_max_commit_ts(
      ctx_tx_data_.get_commit_version(), false);
  return ret;
}

// Unified interface for normal transaction end(both commit and abort). We We
// want to integrate the following six things that all txn commits should do.
//
// 1.end_log_ts: We set end_log_ts during final log state is synced which must
// have been done, so we check the validation of end_log_ts here(Maybe set it in
// this function is better?)
// 2.commit_version: We set commit version after submit the commit log for local
// tx and during the do_prepare for dist tx which must have been done, so we
// check the validation of commit_version here(Maybe set it in this function is
// better?)
// 3.mt_ctx.tx_end: We need callback all txn ops for all data in txn after final
// state is synced. It must be called for all txns to clean and release its data
// resource.
// 4.set_status: We need set status to kill the concurrent read and write.
// 5.set_state: We need set state after final state is synced. It tells others
// that all data for this txn is decided and visible.
// 6.insert_tx_data: We need insert into tx_data in order to cleanot data which
// need be delay cleanout
//
// NB: You need pay much attention to the order of the following steps
// TODO: Integrate trans_kill and trans_replay_end into the same function
int ObTxCtx::tx_end_(const bool commit)
{
  int ret = OB_SUCCESS;

  // NB: The order of the following steps is critical
  int32_t state = commit ? ObTxData::COMMIT : ObTxData::ABORT;
  const SCN &commit_version = ctx_tx_data_.get_commit_version();
  const SCN &end_scn = ctx_tx_data_.get_end_log_ts();

  // STEP1: We need check whether the end_log_ts is valid before state is filled
  // in here because it will be used to cleanout the tnode if state is decided.
  // What's more the end_log_ts is also be used during mt_ctx_.trans_end to
  // backfill normal tnode.
  if (has_persisted_log_() && !end_scn.is_valid()) {
    ret = OB_ERR_UNEXPECTED;
    TRANS_LOG(ERROR, "end log ts is invalid when tx end", K(end_scn), K(ret), KPC(this));
  // STEP2: We need check whether the commi_version is valid before state is
  // filled in with commit here because it will be used to cleanout the tnode or
  // lock for read if state is decided. What's more the commit_version is also
  // be used during mt_ctx_.trans_end to backfill normal tnode..
  } else if (commit && !commit_version.is_valid()) {
    ret = OB_ERR_UNEXPECTED;
    TRANS_LOG(ERROR, "commit version is invalid when tx end", K(ret), KPC(this));
  // STEP3: We need set status in order to kill concurrent read and write. What
  // you need pay attention to is that we rely on the status to report the
  // suicide before the tnode can be cleanout by concurrent read using state in
  // ctx_tx_data.
  } else if (!commit && FALSE_IT(mt_ctx_.set_tx_rollbacked())) {
  // STEP4: We need set state in order to inform others of the final status of
  // my txn. What you need pay attention to is that only after this action,
  // others can cleanout the unfinished txn state and see all your data. It
  // should guarantee that all necesary information(including commit_version and
  // end_scn) is settled. What's more, it accelerates the data visibility for
  // the user.
  } else if (OB_FAIL(ctx_tx_data_.set_state(state))) {
  } else if (!commit && end_scn.is_valid() && OB_FAIL(ctx_tx_data_.add_abort_op(end_scn))) {
    TRANS_LOG(WARN, "add tx data abort_op failed", K(ret), KPC(this));
  // STEP5: We need invoke mt_ctx_.trans_end after the ctx_tx_data is decided
  // and filled in because we obey the rule that ObMvccRowCallback::trans_commit
  // is callbacked from front to back so that if the read or write is standing
  // on one tx node, all previous tx node is decided or can be simply cleanout
  // (which depends on the state in the ctx_tx_data). In conclusion, the action
  // of callbacking is depended on all states in the ctx_tx_data.
  } else if (OB_FAIL(mt_ctx_.trans_end(commit, commit_version, end_scn))) {
  } else if (has_persisted_log_() && OB_FAIL(ctx_tx_data_.insert_into_tx_table())) {
    TRANS_LOG(WARN, "insert to tx table failed", KR(ret), KPC(this));
  }

  return ret;
}

#ifdef ERRSIM
ERRSIM_POINT_DEF(EN_TX_ON_SUCCESS_DELAY)
#endif

int ObTxCtx::on_success(ObTxLogCb *log_cb)
{
  int ret = OB_SUCCESS;
  int tmp_ret = OB_SUCCESS;
  const int64_t cur_ts = ObTimeUtility::current_time();

  const int64_t LOG_CB_ON_SUCC_TIME_LIMIT = 100 * 1000;
  share::SCN max_cost_cb_scn;
  int64_t max_cost_cb_time = 0;
  int64_t skip_on_succ_cnt = 0;
  int64_t invoke_on_succ_cnt = 0;
  int64_t invoke_on_succ_time = 0;
  int64_t submit_record_log_time = 0;
  int64_t fast_commit_time = 0;
  int64_t on_succ_ctx_lock_hold_time = 0;
  int64_t try_submit_next_log_cost_time = 0;

  int64_t log_sync_used_time = 0;
  int64_t ctx_lock_wait_time = 0;

  bool handle_fast_commit = false;
  bool try_submit_next_log = false;
  bool need_return_log_cb = false;
  bool retry_submit_mds = false;
  {
    #ifdef ERRSIM
    uint64_t sleep_us = abs(EN_TX_ON_SUCCESS_DELAY);
    if (sleep_us > 0) {
      usleep(sleep_us);
      TRANS_LOG(INFO, "ERRSIM: delay tx on_success", K(ret), KPC(log_cb), K(sleep_us),
                K(EN_TX_ON_SUCCESS_DELAY));
    } else if (sleep_us < 0) {
      TRANS_LOG(WARN, "ERRSIM: unexpectd sleep us", K(ret), KPC(log_cb), K(sleep_us),K(EN_TX_ON_SUCCESS_DELAY));
    }
    #endif

    // allow fill redo concurrently with log callback
    CtxLockGuard guard(lock_, is_committing_() ? CtxLockGuard::MODE::ALL : CtxLockGuard::MODE::CTX);

    log_sync_used_time = cur_ts - log_cb->get_submit_ts();
    ctx_lock_wait_time = guard.get_lock_acquire_used_time();
    if (log_sync_used_time + ctx_lock_wait_time >= ObServerConfig::get_instance().clog_sync_time_warn_threshold) {
      TRANS_LOG_RET(WARN, OB_ERR_TOO_MUCH_TIME, "transaction log sync use too much time", KPC(log_cb),
                    K(log_sync_used_time), K(ctx_lock_wait_time));
    }
    if (log_cb->get_cb_arg_array().count() == 0) {
      ret = OB_ERR_UNEXPECTED;
      TRANS_LOG(ERROR, "cb arg array is empty", K(ret), KPC(this));
      print_trace_log_();
      OB_SAFE_ABORT();
    }
    if (log_cb->is_callbacked()) {
      skip_on_succ_cnt++;
#ifndef NDEBUG
      TRANS_LOG(INFO, "cb has been callbacked", KPC(log_cb));
#endif
      busy_cbs_.remove(log_cb);
      return_log_cb_(log_cb);
    } else if (is_exiting_) {
      skip_on_succ_cnt++;
      // the TxCtx maybe has been killed forcedly by background GC thread
      // the log_cb process has been skipped
      if (runtime_state_.is_force_abort()) {
        TRANS_LOG(WARN, "ctx has been aborted forcedly before log sync successfully", KPC(this));
        print_trace_log_();
        busy_cbs_.remove(log_cb);
        return_log_cb_(log_cb);
      } else {
        ret = OB_ERR_UNEXPECTED;
        TRANS_LOG(ERROR, "callback was missed when tx ctx exiting", K(ret), KPC(log_cb), KPC(this));
        print_trace_log_();
        OB_SAFE_ABORT();
      }
    } else {
      // save the first error code
      int save_ret = OB_SUCCESS;
      ObTxLogCb *cur_cb = busy_cbs_.get_first();
      // process all preceding log_cbs
      for (int64_t i = 0; i < busy_cbs_.get_size(); i++) {
        if (cur_cb->is_callbacked()) {
          skip_on_succ_cnt++;
          // do nothing
        } else {
          invoke_on_succ_cnt++;
          const int64_t before_invoke_ts = ObTimeUtility::fast_current_time();
          if (OB_FAIL(on_success_ops_(cur_cb))) {
            TRANS_LOG(ERROR, "invoke on_success_ops failed", K(ret), K(*this), K(*cur_cb));
            if (OB_SUCCESS == save_ret) {
              save_ret = ret;
            }
            // rewrite ret
            ret = OB_SUCCESS;

            usleep(1000*1000);
            ob_abort();
          }
          // ignore ret and set cur_cb callbacked
          cur_cb->set_callbacked();
          const int64_t after_invoke_ts = ObTimeUtility::fast_current_time();
          if (after_invoke_ts - before_invoke_ts > max_cost_cb_time) {
            max_cost_cb_time = after_invoke_ts - before_invoke_ts;
            max_cost_cb_scn = log_cb->get_log_ts();
          }
          if (after_invoke_ts - before_invoke_ts > LOG_CB_ON_SUCC_TIME_LIMIT) {
            TRANS_LOG(WARN, "invoke on_succ cost too much time", K(ret), K(trans_id_), K(cur_cb),
                      K(log_cb));
          }
          invoke_on_succ_time += (after_invoke_ts - before_invoke_ts);
        }
        if (cur_cb == log_cb) {
          break;
        } else {
          cur_cb = cur_cb->get_next();
        }
      }
      if (cur_cb != log_cb) {
        ob_abort();
        ret = OB_ERR_UNEXPECTED;
        TRANS_LOG(ERROR, "unexpected log callback", K(ret), K(*this), K(*cur_cb), K(*log_cb));
      } else {
        // return first error code
        ret = save_ret;
      }
      // try submit record log under CtxLock
      if (need_record_log_()) {
        // ignore error
        const int64_t before_submit_record_ts = ObTimeUtility::fast_current_time();
        if (OB_SUCCESS != (tmp_ret = submit_record_log_())) {
        }
        submit_record_log_time = ObTimeUtility::fast_current_time() -  before_submit_record_ts;
      }
      handle_fast_commit = !(runtime_state_.is_state_log_submitted() || log_cb->get_callbacks().count() == 0);
      try_submit_next_log = !ObTxLogTypeChecker::is_state_log(log_cb->get_last_log_type()) && is_committing_();
      retry_submit_mds = ObTxLogTypeChecker::is_mds_log(log_cb->get_last_log_type());
      busy_cbs_.remove(log_cb);
      need_return_log_cb = true;
    }

    on_succ_ctx_lock_hold_time = ObTimeUtility::fast_current_time() - guard.get_hold_ts();
  }
  // let fast commit out of ctx's lock, because it is time consuming in calculating checksum
  if (handle_fast_commit) {
    // acquire REDO_FLUSH_READ LOCK, which allow other thread flush redo
    // but disable other manage operation on ctx
    // FIXME: acquire CTX's READ lock maybe better
    const int64_t before_fast_commit_ts = ObTimeUtility::fast_current_time();
    CtxLockGuard guard(lock_, CtxLockGuard::MODE::REDO_FLUSH_R);
    mt_ctx_.remove_callbacks_for_fast_commit(log_cb->get_callbacks());
    fast_commit_time = ObTimeUtility::fast_current_time() - before_fast_commit_ts;
  }
  ObTxLogCbPool::finish_syncing_with_stat(log_cb->get_group_ptr(),
                                          log_cb->get_log_size(),
                                          ObTimeUtility::fast_current_time()
                                              - log_cb->get_submit_ts(),
                                          log_cb->get_submit_ts());
  if (need_return_log_cb) {
    return_log_cb_(log_cb);
  }
  // try submit log if txn is in commit phase
  if (try_submit_next_log) {
    // in commiting, acquire CTX lock is enough, because redo flushing must finished
    CtxLockGuard guard(lock_, CtxLockGuard::MODE::CTX);
    try_submit_next_log_(false);
  }

  if (retry_submit_mds) {
    CtxLockGuard guard(lock_, CtxLockGuard::MODE::CTX);
    if (OB_TMP_FAIL(submit_log_impl_(ObTxLogType::TX_MULTI_DATA_SOURCE_LOG))) {
    }
  }

  if (ObTimeUtility::fast_current_time() - cur_ts > LOG_CB_ON_SUCC_TIME_LIMIT) {
    TRANS_LOG(WARN, "on_success cost too much time", K(ret), K(trans_id_), K(max_cost_cb_scn), K(max_cost_cb_time), K(skip_on_succ_cnt), K(invoke_on_succ_cnt),
              K(invoke_on_succ_time), K(submit_record_log_time), K(fast_commit_time),
              K(on_succ_ctx_lock_hold_time), K(try_submit_next_log_cost_time), K(log_sync_used_time),
              K(ctx_lock_wait_time));
  }

  if (OB_SUCCESS != (tmp_ret = ls_tx_ctx_mgr_->revert_tx_ctx_without_lock(this))) {
  }

  return ret;
}

int ObTxCtx::replay_mds_to_tx_table_(const ObTxBufferNodeArray &mds_node_array,
                                            const share::SCN op_scn)
{
  int ret = OB_SUCCESS;
  ObTxDataGuard tx_data_guard;
  ObTxDataGuard new_tx_data_guard;
  bool op_exist = false;
  if (OB_FAIL(ctx_tx_data_.get_tx_data(tx_data_guard))) {
  } else if (OB_FAIL(tx_data_guard.tx_data()->check_tx_op_exist(op_scn, op_exist))) {
  } else if (op_exist) {
    // do nothing
  } else if (OB_FAIL(tx_data_guard.tx_data()->init_tx_op())) {
  } else {
    ObTxOpArray tx_op_batch;
    if (OB_FAIL(prepare_mds_tx_op_(mds_node_array,
                                   op_scn,
                                   *tx_data_guard.tx_data()->op_allocator_,
                                   tx_op_batch,
                                   true))) {
    } else if (OB_FAIL(tx_data_guard.tx_data()->op_guard_->add_tx_op_batch(trans_id_,
            op_scn, tx_op_batch))) {
    }
    // tx_op_batch not put into tx_data, need to release
    if (OB_FAIL(ret)) {
      for (int64_t idx = 0; idx < tx_op_batch.count(); idx++) {
        tx_op_batch.at(idx).release();
      }
    }
  }
  // tx_ctx and tx_data checkpoint independent
  if (OB_FAIL(ret)) {
  } else if (OB_FAIL(ls_tx_ctx_mgr_->get_tx_table()->alloc_tx_data(new_tx_data_guard, true, INT64_MAX))){
  } else {
    *new_tx_data_guard.tx_data() = *tx_data_guard.tx_data();
    ObTxData *new_tx_data = new_tx_data_guard.tx_data();
    new_tx_data->end_scn_ = op_scn;
    if (OB_FAIL(ls_tx_ctx_mgr_->get_tx_table()->insert(new_tx_data))) {
    }
  }
  TRANS_LOG(INFO, "replay mds to tx_table", KR(ret), K(mds_node_array.count()), K(trans_id_), K(op_scn), K(op_exist));
  return ret;
}

int ObTxCtx::insert_mds_to_tx_table_(ObTxLogCb &log_cb)
{
  int ret = OB_SUCCESS;
  const ObTxBufferNodeArray &node_array = log_cb.get_mds_range().get_range_array();
  if (OB_ISNULL(log_cb.get_tx_op_array())) {
    ret = OB_ERR_UNEXPECTED;
    TRANS_LOG(WARN, "log_cb tx_op is null", KR(ret), KPC(this), K(log_cb));
  } else if (node_array.count() != log_cb.get_tx_op_array()->count()) {
    ret = OB_ERR_UNEXPECTED;
    TRANS_LOG(WARN, "log_cb mds size is not match", KR(ret), KPC(this), K(log_cb), K(node_array.count()));
  } else {
    SCN op_scn = log_cb.get_log_ts();
    ObTxOpArray &tx_op_array = *log_cb.get_tx_op_array();
    ObTxDataGuard tx_data_guard;
    // assign mds for pre_alloc node
    for (int64_t idx = 0; OB_SUCC(ret) && idx < tx_op_array.count(); idx++) {
      tx_op_array.at(idx).set_op_scn(op_scn);
      ObTxBufferNodeWrapper &wrapper = *(ObTxBufferNodeWrapper*)(tx_op_array.at(idx).get_op_val());
      const ObTxBufferNode &mds_node = node_array.at(idx);
      if (wrapper.get_node().get_register_no() != mds_node.get_register_no() ||
          wrapper.get_node().get_data_source_type() != mds_node.get_data_source_type()) {
        ret = OB_ERR_UNEXPECTED;
        TRANS_LOG(WARN, "mds not match", KR(ret), KPC(this));
      } else if (OB_FAIL(wrapper.assign(trans_id_, mds_node, ::oceanbase::share::server_service<::oceanbase::share::ObSharedMemAllocMgr>()->tx_data_op_allocator(), true))) {
      }
    }
    if (OB_FAIL(ret)) {
    } else if (OB_FAIL(ctx_tx_data_.get_tx_data(tx_data_guard))) {
    } else if (OB_FAIL(tx_data_guard.tx_data()->init_tx_op())) {
    } else if (OB_FAIL(tx_data_guard.tx_data()->op_guard_->add_tx_op_batch(trans_id_,
          op_scn, tx_op_array))) {
    } else {
      *log_cb.get_tx_data_guard().tx_data() = *tx_data_guard.tx_data();
      ObTxData *new_tx_data = log_cb.get_tx_data_guard().tx_data();
      new_tx_data->end_scn_ = op_scn;
      if (OB_FAIL(ls_tx_ctx_mgr_->get_tx_table()->insert(new_tx_data))) {
      } else {
        tx_op_array.reset();
      }
    }
  }
  TRANS_LOG(INFO, "insert mds to tx_table", KR(ret), K(trans_id_), K(exec_info_.multi_data_source_.count()), K(log_cb));
  return ret;
}

int ObTxCtx::insert_undo_action_to_tx_table_(ObUndoAction &undo_action,
                                                    ObTxDataGuard &new_tx_data_guard,
                                                    storage::ObUndoStatusNode *&undo_node,
                                                    const share::SCN op_scn)
{
  int ret = OB_SUCCESS;
  // tx_data on part_ctx has modified
  ObTxDataGuard tx_data_guard;
  if (OB_FAIL(ctx_tx_data_.get_tx_data(tx_data_guard))) {
  } else if (OB_FAIL(tx_data_guard.tx_data()->init_tx_op())) {
  } else if (OB_FAIL(tx_data_guard.tx_data()->add_undo_action(ls_tx_ctx_mgr_->get_tx_table(), undo_action, undo_node))) {
  } else {
    *new_tx_data_guard.tx_data() = *tx_data_guard.tx_data();
    ObTxData *new_tx_data = new_tx_data_guard.tx_data();
    new_tx_data->end_scn_ = op_scn;
    if (OB_FAIL(ls_tx_ctx_mgr_->get_tx_table()->insert(new_tx_data))) {
     }
  }
  TRANS_LOG(INFO, "insert undo_action to tx_table", KR(ret), K(undo_action), K(trans_id_), K(op_scn), KP(undo_node));
  return ret;
}

int ObTxCtx::replay_undo_action_to_tx_table_(ObUndoAction &undo_action,
                                                    const share::SCN op_scn)
{
  int ret = OB_SUCCESS;
  ObTxDataGuard tx_data_guard;
  ObTxDataGuard new_tx_data_guard;
  ObTxDataOp *tx_data_op = nullptr;
  int64_t tx_data_op_ref = 0;
  if (OB_FAIL(ctx_tx_data_.get_tx_data(tx_data_guard))) {
  } else if (OB_FAIL(tx_data_guard.tx_data()->init_tx_op())) {
  } else if (OB_FAIL(ls_tx_ctx_mgr_->get_tx_table()->alloc_tx_data(new_tx_data_guard, true, INT64_MAX))){
  } else {
    *new_tx_data_guard.tx_data() = *tx_data_guard.tx_data();
    ObTxData *new_tx_data = new_tx_data_guard.tx_data();
    new_tx_data->end_scn_ = op_scn;
    tx_data_op = new_tx_data->op_guard_.ptr();
    if (OB_NOT_NULL(tx_data_op)) {
      tx_data_op_ref = tx_data_op->get_ref();
    }
    if (OB_FAIL(new_tx_data->add_undo_action(ls_tx_ctx_mgr_->get_tx_table(), undo_action))) {
    } else if (OB_FAIL(ls_tx_ctx_mgr_->get_tx_table()->insert(new_tx_data))) {
    }
  }
  TRANS_LOG(INFO, "replay undo_action to tx_table", KR(ret), K(undo_action), K(trans_id_),
      K(op_scn), KP(tx_data_op), K(tx_data_op_ref));
  return ret;
}

int ObTxCtx::on_success_ops_(ObTxLogCb *log_cb)
{
  int ret = OB_SUCCESS;
  const SCN log_ts = log_cb->get_log_ts();
  const palf::LSN log_lsn = log_cb->get_lsn();
  const ObTxCbArgArray &cb_arg_array = log_cb->get_cb_arg_array();

  if (OB_FAIL(common_on_success_(log_cb))) {
  }
  for (int64_t i = 0; OB_SUCC(ret) && i < cb_arg_array.count(); i++) {
    const ObTxLogType log_type = cb_arg_array.at(i).get_log_type();
    if (ObTxLogType::TX_REDO_LOG == log_type) {
      // do nothing
    }  else if (ObTxLogType::TX_MULTI_DATA_SOURCE_LOG == log_type) {
      share::SCN notify_redo_scn =
        log_cb->get_first_part_scn().is_valid() ? log_cb->get_first_part_scn() : log_ts;
      if (OB_FAIL(log_cb->get_mds_range().move_from_cache_to_arr(mds_cache_,
                                                                 exec_info_.multi_data_source_))) {
      } else if (FALSE_IT(mds_cache_.clear_submitted_iterator())) {
        // do nothing
      } else if (OB_FAIL(notify_data_source_(NotifyType::ON_REDO,
                                             notify_redo_scn,
                                             false,
                                             log_cb->get_mds_range().get_range_array()))) {
      } else if (OB_FAIL(insert_mds_to_tx_table_(*log_cb))) {
      } else {
        log_cb->get_mds_range().reset();
        log_cb->reset_tx_op_array();
      }
    } else if (ObTxLogType::TX_BIG_SEGMENT_LOG == log_type) {
      remove_unsynced_segment_cb_(log_cb->get_log_ts());
    } else if (ObTxLogType::TX_COMMIT_INFO_LOG == log_type) {
      set_durable_state_(ObTxState::REDO_COMPLETE);
    } else if (ObTxLogType::TX_ROLLBACK_TO_LOG == log_type) {
      if (OB_FAIL(insert_undo_action_to_tx_table_(log_cb->get_undo_action(),
                                                  log_cb->get_tx_data_guard(),
                                                  log_cb->get_undo_node(),
                                                  log_ts))) {
      } else {
        log_cb->set_tx_data(nullptr);
        log_cb->reset_undo_node();
      }
    } else if (ObTxLogTypeChecker::is_state_log(log_type)) {
      runtime_state_.clear_state_log_submitting();
      if (ObTxLogType::TX_COMMIT_LOG == log_type) {
        if (OB_SUCC(ret)) {
          if (OB_FAIL(ctx_tx_data_.set_end_log_ts(log_ts))) {
          } else {
            if (OB_FAIL(on_local_commit_tx_())) {
            }
          }
        }
      } else if (ObTxLogType::TX_ABORT_LOG == log_type) {
        if (OB_SUCC(ret)) {
          if (OB_FAIL(ctx_tx_data_.set_end_log_ts(log_ts))) {
          } else if (OB_FAIL(on_local_abort_tx_())) {
          }
        }
      } else if (ObTxLogType::TX_CLEAR_LOG == log_type) {
      } else {
        ret = OB_ERR_UNEXPECTED;
        TRANS_LOG(ERROR, "unknown log type", K(ret), K(*this));
      }
    }
    REC_TRANS_TRACE_EXT(tlog_, log_sync_succ_cb,
                        OB_ID(ret), ret,
                        OB_ID(log_type), (void*)log_type,
                        OB_ID(t), log_ts,
                        OB_ID(offset), log_lsn,
                        OB_ID(ref), get_ref());
  }
  return ret;
}

int ObTxCtx::common_on_success_(ObTxLogCb *log_cb)
{
  int ret = OB_SUCCESS;
  int tmp_ret = OB_SUCCESS;

  const SCN log_ts = log_cb->get_log_ts();
  const palf::LSN lsn = log_cb->get_lsn();
  const ObTxLogType last_log_type = log_cb->get_last_log_type();
  if (log_ts > exec_info_.max_applying_log_ts_) {
    exec_info_.max_applying_log_ts_ = log_ts;
    exec_info_.max_applying_part_log_no_ = 0;
  }
  if (log_ts > exec_info_.max_applied_log_ts_) {
    exec_info_.max_applied_log_ts_ = log_ts;
  }
  if (!exec_info_.max_durable_lsn_.is_valid() || lsn > exec_info_.max_durable_lsn_) {
    exec_info_.max_durable_lsn_ = lsn;
  }
  if (OB_SUCC(ret)) {
    if (OB_FAIL(mt_ctx_.sync_log_succ(log_ts, log_cb->get_callbacks()))) {
    }
  }
  return ret;
}

void ObTxCtx::check_and_register_timeout_task_()
{
  int64_t tmp_ret = OB_SUCCESS;

  if (!timeout_task_.is_running()
      && !timeout_task_.is_registered()
      && !is_for_replay()
      && !is_exiting_) {
    const int64_t timeout_left = is_committing_() ? commit_retry_timeout_
        : MIN(MAX_TRANS_COMMIT_RETRY_TIMEOUT_US, MAX(trans_expired_time_ - ObClockGenerator::getClock(), 1000 * 1000));
    if (OB_SUCCESS != (tmp_ret = register_timeout_task_(timeout_left))) {
      TRANS_LOG_RET(WARN, tmp_ret, "register timeout task failed", KR(tmp_ret), KPC(this));
    }
  }
}

int ObTxCtx::try_submit_next_log_(const bool for_freeze)
{
  int ret = OB_SUCCESS;
  ObTxLogType log_type = ObTxLogType::TX_COMMIT_LOG;
  if (ObPartTransAction::COMMIT == part_trans_action_ && !need_force_abort_()
      && !runtime_state_.is_state_log_submitted() && !runtime_state_.is_state_log_submitting()
      && exec_info_.state_ < ObTxState::PREPARE) {
    if (is_for_replay()) {
      ret = OB_STATE_NOT_MATCH;
    } else {
      if (OB_FAIL(submit_log_impl_(log_type))) {
      } else {
        TRANS_LOG(INFO, "submit log for commit success", K(log_type), KPC(this));
      }
    }
  }

  if (!for_freeze) {
    // ignore retcode
    (void)check_and_register_timeout_task_();
  }

  return ret;
}

int ObTxCtx::fix_redo_lsns_(const ObTxLogCb *log_cb)
{
  int ret = OB_SUCCESS;
  LSN lsn;
  ObRedoLSNArray &redo_lsns = exec_info_.redo_lsns_;
  while (!redo_lsns.empty()) {
    lsn = redo_lsns[redo_lsns.count() - 1];
    if (lsn >= log_cb->get_lsn()) {
      redo_lsns.pop_back();
    } else {
      break;
    }
  }
  return ret;
}

int ObTxCtx::on_failure(ObTxLogCb *log_cb)
{
  int ret = OB_SUCCESS;
  share::SCN max_committed_scn;
  if (OB_FAIL(ls_tx_ctx_mgr_->get_ls_log_adapter()->get_max_decided_scn(max_committed_scn))) {
    TRANS_LOG(ERROR, "get palf max committed scn fail, need retry", K(ret), KPC(this));
    OB_SAFE_ABORT();
  } else {
    TRANS_LOG(INFO, "succ get palf max_commited_scn", K(max_committed_scn), KPC(log_cb));
  }
  if (OB_SUCC(ret)) {
    {
      const int64_t log_sync_used_time = ObTimeUtility::current_time() - log_cb->get_submit_ts();
      CtxLockGuard guard(lock_);
      const int64_t ctx_lock_wait_time = guard.get_lock_acquire_used_time();
      if (log_sync_used_time + ctx_lock_wait_time >= ObServerConfig::get_instance().clog_sync_time_warn_threshold) {
        TRANS_LOG_RET(WARN, OB_ERR_TOO_MUCH_TIME, "transaction log sync use too much time", KPC(log_cb),
                      K(log_sync_used_time), K(ctx_lock_wait_time));
      }
      if (log_cb->get_cb_arg_array().count() == 0) {
        ret = OB_ERR_UNEXPECTED;
        TRANS_LOG(ERROR, "cb arg array is empty", K(ret), KPC(this));
        print_trace_log_();
        usleep(5000);
        ob_abort();
      }
      exec_info_.next_log_entry_no_--;
      const ObTxLogType log_type = log_cb->get_last_log_type();
      const SCN log_ts = log_cb->get_log_ts();
      mt_ctx_.sync_log_fail(log_cb->get_callbacks(), max_committed_scn);
      log_cb->get_mds_range().range_sync_failed(mds_cache_);
      if (log_ts == ctx_tx_data_.get_start_log_ts()) {
        ctx_tx_data_.set_start_log_ts(SCN());
      }
      if (ObTxLogTypeChecker::is_state_log(log_type)) {
        runtime_state_.clear_state_log_submitting();
      }
      if (OB_FAIL(fix_redo_lsns_(log_cb))) {
      }
      if (is_contain(log_cb->get_cb_arg_array(), ObTxLogType::TX_BIG_SEGMENT_LOG)) {
        remove_unsynced_segment_cb_(log_cb->get_log_ts());
      }
      if (ObTxLogType::TX_ROLLBACK_TO_LOG == log_type) {
        ObTxData *tx_data = log_cb->get_tx_data();
        if (OB_FAIL(ctx_tx_data_.free_tmp_tx_data(tx_data))) {
        } else {
          log_cb->set_tx_data(nullptr);
        }
      }
      if (ObTxLogType::TX_COMMIT_LOG == log_type) {
        // if local tx commit log callback on failure, reset trans_version to make standby read skip this
        if (!mt_ctx_.get_trans_version().is_max()) {
          mt_ctx_.set_trans_version(SCN::max_scn());
          TRANS_LOG(INFO, "clear local trans version when commit log on failure", K(ret), KPC(this));
        }
        // revert ELR_COMMIT to RUNNING
        if (ctx_tx_data_.get_state() == ObTxData::ELR_COMMIT) {
          ctx_tx_data_.set_state(ObTxData::RUNNING);
          mt_ctx_.elr_trans_revoke();
        }
      }
      ObTxLogCbPool::finish_syncing_with_stat(log_cb->get_group_ptr(),
                                              log_cb->get_log_size(),
                                              ObTimeUtility::fast_current_time()
                                                  - log_cb->get_submit_ts(),
                                              log_cb->get_submit_ts());
      busy_cbs_.remove(log_cb);
      return_log_cb_(log_cb, true);
      log_cb = NULL;
      if (ObTxLogType::TX_COMMIT_INFO_LOG == log_type) {
        runtime_state_.clear_info_log_submitted();
      }
      if (busy_cbs_.is_empty() && get_downstream_state() < ObTxState::PREPARE) {
        runtime_state_.clear_state_log_submitted();
      }
      if (busy_cbs_.is_empty() && !has_persisted_log_()) {
        // busy callback array is empty and trx has not persisted any log, exit here
        TRANS_LOG(ERROR, "log sync failed, txn aborted without persisted log", KPC(this));
        if (OB_FAIL(do_local_tx_end_(TxEndAction::ABORT_TX))) {
        }
        if (need_commit_callback_()) {
          int tmp_ret = OB_SUCCESS;
          if (OB_TMP_FAIL(defer_commit_callback_(OB_TRANS_KILLED, SCN::invalid_scn()))) {
          } else {
            commit_cb_.disable();
            TRANS_LOG(INFO, "notify scheduler txn killed success", K_(trans_id));
          }
          ret = COVER_SUCC(tmp_ret);
        }
      }
      REC_TRANS_TRACE_EXT(tlog_, on_fail_cb,
                          OB_ID(ret), ret,
                          OB_ID(log_type), (void*)log_type,
                          OB_ID(t), log_ts,
                          OB_ID(ref), get_ref());
      TRANS_LOG(INFO, "ObTxCtx::on_failure end", KR(ret), K(*this), KPC(log_cb));
    }
    int tmp_ret = OB_SUCCESS;
    if (OB_SUCCESS != (tmp_ret = ls_tx_ctx_mgr_->revert_tx_ctx_without_lock(this))) {
    }
  }
  return ret;
}

int ObTxCtx::get_local_max_read_version_(SCN &local_max_read_version)
{
  int ret = OB_SUCCESS;
  local_max_read_version = trans_service_->get_tx_version_mgr().get_max_read_ts();
  if (OB_FAIL(ret)) {
  }
  return ret;
}

int ObTxCtx::get_gts_(SCN &gts)
{
  int ret = OB_SUCCESS;
  MonotonicTs receive_gts_ts;
  const int64_t GET_GTS_AHEAD_INTERVAL = 0; //GCONF._ob_get_gts_ahead_interval;
  const MonotonicTs stc_ahead = get_stc_() - MonotonicTs(GET_GTS_AHEAD_INTERVAL);
  ObTsMgr *ts_mgr = trans_service_->get_ts_mgr();

  if (OB_FAIL(ts_mgr->get_gts(stc_ahead, gts, receive_gts_ts))) {
    if (OB_EAGAIN != ret) {
      TRANS_LOG(WARN, "get gts failed", KR(ret), K(*this), K(stc_ahead));
    }
  } else {
    set_trans_need_wait_wrap_(receive_gts_ts, GET_GTS_AHEAD_INTERVAL);
  }

  return ret;
}

int ObTxCtx::gts_callback_interrupted()
{
  int ret = OB_SUCCESS;
  bool need_revert_ctx = false;

  {
    CtxLockGuard guard(lock_);
    if (IS_NOT_INIT) {
      ret = OB_NOT_INIT;
      TRANS_LOG(ERROR, "ObTxCtx not inited", KR(ret));
    } else {
      runtime_state_.clear_gts_waiting();
      need_revert_ctx = true;
      TRANS_LOG(INFO, "transaction gts callback interrupted", KPC(this));
    }
  }

  if (need_revert_ctx) {
    if (OB_FAIL(ls_tx_ctx_mgr_->revert_tx_ctx_without_lock(this))) {
    }
  }
  return ret;
}

int ObTxCtx::gts_elapse_callback(const SCN &gts)
{
  int ret = OB_SUCCESS;
  bool need_revert_ctx = false;

  {
    CtxLockGuard guard(lock_);
    if (IS_NOT_INIT) {
      ret = OB_NOT_INIT;
      TRANS_LOG(WARN, "ObTxCtx not inited", KR(ret));
    } else if (OB_UNLIKELY(!gts.is_valid())) {
      ret = OB_INVALID_ARGUMENT;
      TRANS_LOG(WARN, "invalid gts", KR(ret), K(gts), KPC(this));
    } else if (OB_UNLIKELY(is_exiting_)) {
      ret = OB_TRANS_IS_EXITING;
      need_revert_ctx = true;
      TRANS_LOG(WARN, "transaction is exiting", KR(ret), KPC(this));
    } else if (ctx_tx_data_.get_commit_version() > gts) {
      ret = OB_EAGAIN;
    } else {
      if (OB_UNLIKELY(!runtime_state_.is_gts_waiting())) {
        TRANS_LOG(ERROR, "unexpected gts waiting flag", KPC(this));
      } else {
        runtime_state_.clear_gts_waiting();
      }
      if (OB_FAIL(after_local_commit_succ_())) {
      }
      need_revert_ctx = true;
    }
    REC_TRANS_TRACE_EXT2(tlog_, gts_elapse_callback,
                         OB_Y(ret), OB_Y(gts), OB_ID(ref), get_ref());

    if (OB_FAIL(ret) && OB_EAGAIN != ret && runtime_state_.is_gts_waiting()) {
      runtime_state_.clear_gts_waiting();
    }
  }

  if (need_revert_ctx) {
    if (OB_FAIL(ls_tx_ctx_mgr_->revert_tx_ctx_without_lock(this))) {
    }
  }
  return ret;
}

int ObTxCtx::wait_gts_elapse_commit_version_(bool &need_wait)
{
  int ret = OB_SUCCESS;
  need_wait = false;

  if (OB_FAIL(trans_service_->get_tx_timestamp_waiter().wait_gts_elapse(
          ctx_tx_data_.get_commit_version(), this, need_wait))) {
  } else if (need_wait) {
    runtime_state_.set_gts_waiting();
    if (OB_FAIL(acquire_ctx_ref_())) {
    }
    TRANS_LOG(INFO, "need wait gts elapse", KR(ret), KPC(this));
    REC_TRANS_TRACE_EXT2(tlog_, wait_gts_elapse, OB_ID(ref), get_ref());
  }

  return ret;
}

int ObTxCtx::generate_prepare_version_()
{
  int ret = OB_SUCCESS;

  if (!mt_ctx_.is_prepared() || !exec_info_.prepare_version_.is_valid()) {
    SCN gts = SCN::min_scn();
    SCN local_max_read_version = SCN::min_scn();
    bool is_gts_ok = false;
    // Only the root participant require to request gts
    const bool need_gts = true;

    if (need_gts) {
      if (OB_FAIL(get_gts_(gts))) {
        if (OB_EAGAIN == ret) {
          is_gts_ok = false;
          TRANS_LOG(INFO, "get gts eagain", KR(ret), KPC(this));
          ret = OB_SUCCESS;
        } else {
          is_gts_ok = false;
          TRANS_LOG(ERROR, "get gts failed", KR(ret), K(*this));
        }
      } else {
        is_gts_ok = true;
      }
    }

    if (OB_SUCC(ret)
        && ((need_gts && is_gts_ok)
            || !need_gts)) {
      // To order around the read-write conflict(anti dependency), we need push
      // the txn version upper than all previous read version. So we record all
      // read version each access begins and get the max read version to handle
      // the dependency conflict
      mt_ctx_.before_prepare(gts);
      if (OB_FAIL(get_local_max_read_version_(local_max_read_version))) {
      } else {
        // should not overwrite the prepare version of another participant context
        exec_info_.prepare_version_ = SCN::max(SCN::max(gts, local_max_read_version),
                                               exec_info_.prepare_version_);
        if (exec_info_.prepare_version_ > gts) {
          mt_ctx_.before_prepare(exec_info_.prepare_version_);
        }
      }
    }
  }

  return ret;
}

// for single-ls transaction, commit version may later be updated by log ts
int ObTxCtx::generate_commit_version_()
{
  int ret = OB_SUCCESS;
  if (!ctx_tx_data_.get_commit_version().is_valid()) {
    SCN gts;
    if (OB_FAIL(get_gts_(gts))) {
      if (OB_EAGAIN != ret) {
        TRANS_LOG(WARN, "get gts failed", KR(ret), K(*this));
      }
    } else {
      // the same as before prepare
      mt_ctx_.set_trans_version(gts);
      const SCN max_read_ts = trans_service_->get_tx_version_mgr().get_max_read_ts();
      if (OB_FAIL(ctx_tx_data_.set_commit_version(SCN::max(gts, max_read_ts)))) {
      }
    }
  }
  return ret;
}

// starting from 0
int64_t ObTxCtx::get_redo_log_no_() const
{
  return exec_info_.redo_lsns_.count();
}


inline
int ObTxCtx::submit_redo_if_serial_logging_(ObTxLogBlock &log_block,
                                                   bool &has_redo,
                                                   ObRedoLogSubmitHelper &helper)
{
  int ret = OB_SUCCESS;
  if (OB_LIKELY(!is_parallel_logging())) {
    ObTxRedoSubmitter submitter(*this, mt_ctx_);
    ret = submitter.fill(log_block, helper, true /*display blocked info*/);
    has_redo = submitter.get_submitted_cnt() > 0 || helper.callbacks_.count() > 0;
  } else {
    // sanity check, all redo must have been flushed
#ifndef NDEBUG
    mt_ctx_.check_all_redo_flushed();
#endif
  }
  return ret;
}

// when parallel logging, redo need submitted seperate with other txn's log
inline
int ObTxCtx::submit_redo_if_parallel_logging_()
{
  int ret = OB_SUCCESS;
  if (OB_UNLIKELY(is_parallel_logging())) {
    ObTxRedoSubmitter submitter(*this, mt_ctx_);
    if (OB_FAIL(submitter.submit_all(true /*display blocked info*/))) {
    }
  }
  return ret;
}

// this function is thread safe, not need other lock's protection
inline
int ObTxCtx::init_log_block_(ObTxLogBlock &log_block,
                                    const int64_t suggested_buf_size,
                                    const bool serial_final)
{
  ObTxLogBlockHeader &header = log_block.get_header();
  // the log_entry_no will be backfill before log-block to be submitted
  header.init(-1 /*log_entry_no*/, trans_id_);
  if (OB_UNLIKELY(serial_final)) { header.set_serial_final(); }
  if (OB_UNLIKELY(has_async_index_redo_)) { header.set_has_async_index(); }
  return log_block.init_for_fill(suggested_buf_size);
}

inline int ObTxCtx::reuse_log_block_(ObTxLogBlock &log_block)
{
  ObTxLogBlockHeader &header = log_block.get_header();
  header.init(exec_info_.next_log_entry_no_, trans_id_);
  if (OB_UNLIKELY(has_async_index_redo_)) { header.set_has_async_index(); }
  return log_block.reuse_for_fill();
}

int ObTxCtx::submit_redo_commit_info_log_()
{
  int ret = OB_SUCCESS;
  ObTxLogBlock log_block;
  bool has_redo = false;
  ObTxLogCb *log_cb = NULL;
  ObRedoLogSubmitHelper helper;
  const int64_t replay_hint = trans_id_.get_id();
  using LogBarrierType = logservice::ObReplayBarrierType;
  LogBarrierType barrier = LogBarrierType::NO_NEED_BARRIER;
  if (need_force_abort_() || is_force_abort_logging_()
      || get_downstream_state() == ObTxState::ABORT) {
    ret = OB_TRANS_KILLED;
    TRANS_LOG(ERROR, "tx has been aborting, can not submit commit info log", K(ret));
  } else if (runtime_state_.is_info_log_submitted()) {
    // state log already submitted, do nothing
  } else if (OB_FAIL(submit_redo_if_parallel_logging_())) {
  } else if (OB_FAIL(init_log_block_(log_block))) {
  } else if (OB_FAIL(submit_multi_data_source_(log_block))) {
  } else if (OB_FAIL(submit_redo_commit_info_log_(log_block, has_redo, helper, barrier))) {
  } else if (OB_FAIL(prepare_log_cb_(!NEED_FINAL_CB, log_cb))) {
    if (OB_UNLIKELY(OB_TX_NOLOGCB != ret)) {
      TRANS_LOG(WARN, "get log cb failed", KR(ret), K(*this));
    }
  } else if (log_block.get_cb_arg_array().count() == 0) {
    ret = OB_ERR_UNEXPECTED;
    TRANS_LOG(ERROR, "cb arg array is empty", K(ret), K(log_block));
    return_log_cb_(log_cb);
    log_cb = NULL;
  } else if (OB_FAIL(log_cb->reserve_callbacks(helper.callbacks_.count()))) {
    TRANS_LOG(WARN, "resolve callbacks failed", K(ret), KPC(this));
    return_log_cb_(log_cb);
    log_cb = NULL;
  } else if (OB_FAIL(acquire_ctx_ref_())) {
  } else if (OB_FAIL(submit_log_block_out_(log_block, SCN::min_scn(), log_cb, replay_hint, barrier))) {
    TRANS_LOG(ERROR, "submit log to clog adapter failed", KR(ret), K(*this));
    return_log_cb_(log_cb);
    log_cb = NULL;
    release_ctx_ref_();
  } else if (OB_FAIL(after_submit_log_(log_block, log_cb, &helper))) {
  } else {
    // TRANS_LOG(INFO, "submit redo and commit_info log in clog adapter success", K(*log_cb));
    reset_redo_lsns_();
    log_cb = NULL;
  }

  return ret;
}

int ObTxCtx::validate_commit_info_log_(const ObTxCommitInfoLog &commit_info_log)
{
  int ret = OB_SUCCESS;

  if (commit_info_log.get_redo_lsns().count() != exec_info_.redo_lsns_.count()) {
    ret = OB_ERR_UNEXPECTED;
    TRANS_LOG(ERROR, "invalid commit info log", K(ret), K(commit_info_log), KPC(this));
  }

  return ret;
}

int ObTxCtx::submit_redo_commit_info_log_(ObTxLogBlock &log_block,
                                                 bool &has_redo,
                                                 ObRedoLogSubmitHelper &helper,
                                                 logservice::ObReplayBarrierType &barrier)
{
  int ret = OB_SUCCESS;
  ObTxLogCb *log_cb = NULL;
  const int64_t replay_hint = trans_id_.get_id();
  barrier = logservice::ObReplayBarrierType::NO_NEED_BARRIER;

  if (runtime_state_.is_info_log_submitted()) {
    // state log already submitted, do nothing
  } else if (OB_FAIL(submit_redo_if_serial_logging_(log_block, has_redo, helper))) {
  } else if (OB_FAIL(decide_state_log_barrier_type_(ObTxLogType::TX_COMMIT_INFO_LOG, barrier))) {
  } else {
    ObTxCommitInfoLog commit_info_log(
        can_elr_, trace_info_.get_app_trace_id(),
        exec_info_.prev_record_lsn_, exec_info_.redo_lsns_);

    if (OB_FAIL(validate_commit_info_log_(commit_info_log))) {
    } else if (OB_FAIL(log_block.add_new_log(commit_info_log))) {
      if (OB_BUF_NOT_ENOUGH == ret) {
        // TRANS_LOG(WARN, "buf not enough", K(ret), K(commit_info_log), KPC(this));
        if (OB_FAIL(prepare_log_cb_(!NEED_FINAL_CB, log_cb))) {
          if (OB_UNLIKELY(OB_TX_NOLOGCB != ret)) {
            TRANS_LOG(WARN, "get log cb failed", KR(ret), K(*this));
          }
        } else if (log_block.get_cb_arg_array().count() == 0) {
          ret = OB_ERR_UNEXPECTED;
            TRANS_LOG(ERROR, "cb arg array is empty", K(ret), K(log_block));
            return_log_cb_(log_cb);
            log_cb = NULL;
          } else if (OB_FAIL(log_cb->reserve_callbacks(helper.callbacks_.count()))) {
            TRANS_LOG(WARN, "resolve callbacks failed", K(ret), KPC(this));
            return_log_cb_(log_cb);
            log_cb = NULL;
          } else if (OB_FAIL(acquire_ctx_ref_())) {
          } else if (OB_FAIL(submit_log_block_out_(log_block, SCN::min_scn(), log_cb))) {
            TRANS_LOG(ERROR, "submit log failed", KR(ret), K(*this));
            return_log_cb_(log_cb);
            log_cb = NULL;
            release_ctx_ref_();
          } else if (OB_FAIL(after_submit_log_(log_block, log_cb, &helper))) {
          } else {
            log_cb = NULL;
            if (OB_FAIL(validate_commit_info_log_(commit_info_log))) {
            } else if (OB_FAIL(log_block.add_new_log(commit_info_log))) {
            }
            has_redo = false;
          }
        } else {
          TRANS_LOG(WARN, "add new log failed", KR(ret), K(this));
        }
      }
      //} else if (commit_info_log_barrier_type != logservice::ObReplayBarrierType::NO_NEED_BARRIER
      //           && OB_FAIL(log_block.rewrite_barrier_log_block(trans_id_.get_id(),
      //                                                          commit_info_log_barrier_type))) {
      //  TRANS_LOG(WARN, "rewrite commit info log barrier type failed", K(ret),
      //            K(commit_info_log_barrier_type), KPC(this));
    }

  return ret;
}

// The commit log timestamp must be greater than commit_version_.
int ObTxCtx::submit_commit_log_()
{
  int ret = OB_SUCCESS;
  ObTxLogBlock log_block;
  palf::LSN prev_lsn;
  bool has_redo = false;
  ObRedoLogSubmitHelper helper;
  const int64_t replay_hint = trans_id_.get_id();

  using LogBarrierType = logservice::ObReplayBarrierType;
  LogBarrierType commit_info_log_barrier =  LogBarrierType::NO_NEED_BARRIER;
  if (need_force_abort_() || is_force_abort_logging_()
      || get_downstream_state() == ObTxState::ABORT) {
    ret = OB_TRANS_KILLED;
    TRANS_LOG(ERROR, "tx has been aborting, can not submit commit log", K(ret));
  } else if (OB_FAIL(mds_cache_.reserve_final_notify_array(exec_info_.multi_data_source_))) {
  } else if (OB_FAIL(mds_cache_.generate_final_notify_array(exec_info_.multi_data_source_,
                                                             true /*need_merge_cache*/,
                                                             false /*allow_log_overflow*/))) {
  } else {
    bool log_block_inited = false;
    int64_t suggested_buf_size = ObTxAdaptiveLogBuf::NORMAL_LOG_BUF_SIZE;
    if (mds_cache_.get_final_notify_array().count() == 0 &&
        // 512B
        ((mt_ctx_.get_pending_log_size() < ObTxAdaptiveLogBuf::MIN_LOG_BUF_SIZE / 4) ||
         // for corner case test
         IS_CORNER(10000))) {
      suggested_buf_size = ObTxAdaptiveLogBuf::MIN_LOG_BUF_SIZE;
    }
    if (!runtime_state_.is_info_log_submitted()) {
      prev_lsn.reset();
      if (OB_FAIL(submit_redo_if_parallel_logging_())) {
      } else if (OB_FAIL(init_log_block_(log_block, suggested_buf_size))) {
      } else if (FALSE_IT(log_block_inited = true)) {
      } else if (OB_FAIL(submit_multi_data_source_(log_block))) {
      } else if (OB_SUCC(submit_redo_commit_info_log_(log_block, has_redo, helper,
                                                      commit_info_log_barrier))) {
        // do nothing
      } else {
        TRANS_LOG(WARN, "submit redo commit state log failed", KR(ret), K(*this));
      }
    }
    // init log_block for commit log
    if (OB_SUCC(ret) && !log_block_inited && OB_FAIL(init_log_block_(log_block, suggested_buf_size))) {
      TRANS_LOG(WARN, "init log block failed", K(ret));
    }
  }

  if (OB_SUCC(ret)) {
    SCN log_commit_version;
    ObSEArray<uint64_t, 1> checksum_arr;
    ObTxPrevLogType prev_log_type;
    if (exec_info_.need_checksum_
        && replay_completeness_.is_complete()
        && OB_FAIL(mt_ctx_.calc_checksum_all(checksum_arr))) {
      TRANS_LOG(ERROR, "calc checksum failed", K(ret));
    } else {
      prev_log_type.set_commit_info();
      if (OB_FAIL(get_prev_log_lsn_(log_block, prev_log_type, prev_lsn))) {
      }
    }
    uint64_t collapsed_checksum = 0;
    uint8_t _checksum_sig[checksum_arr.count()];
    ObArrayHelper<uint8_t> checksum_sig(checksum_arr.count(), _checksum_sig);
    mt_ctx_.convert_checksum_for_commit_log(checksum_arr, collapsed_checksum, checksum_sig);
    ObTxCommitLog commit_log(log_commit_version,
                             collapsed_checksum,
                             checksum_sig,
                             mds_cache_.get_final_notify_array(), prev_lsn,
                             prev_log_type);
    ObTxLogCb *log_cb = NULL;
    bool redo_log_submitted = false;
    LogBarrierType commit_log_barrier_type =  LogBarrierType::NO_NEED_BARRIER;
    LogBarrierType compound_log_barrier_type = commit_info_log_barrier;
    if (OB_SUCC(ret)) {
      if (OB_FAIL(set_start_scn_in_commit_log_(commit_log))) {
      } else if (OB_FAIL(decide_state_log_barrier_type_(ObTxLogType::TX_COMMIT_LOG, commit_log_barrier_type))) {
      } else if (OB_FAIL(ObTxLogTypeChecker::decide_final_barrier_type(commit_log_barrier_type, compound_log_barrier_type))) {
      }
    }

    if (OB_FAIL(ret)) {
      //do nothing
    } else if (OB_FAIL(log_block.add_new_log(commit_log))) {
      if (OB_BUF_NOT_ENOUGH == ret) {
        TRANS_LOG(WARN, "buf not enough", K(ret), K(commit_log));
        if (OB_FAIL(prepare_log_cb_(!NEED_FINAL_CB, log_cb))) {
          if (OB_UNLIKELY(OB_TX_NOLOGCB != ret)) {
            TRANS_LOG(WARN, "get log cb failed", KR(ret), K(*this));
          }
        } else if (log_block.get_cb_arg_array().count() == 0) {
          ret = OB_ERR_UNEXPECTED;
          TRANS_LOG(ERROR, "cb arg array is empty", K(ret), K(log_block));
          return_log_cb_(log_cb);
          log_cb = NULL;
        } else if (OB_FAIL(log_cb->reserve_callbacks(helper.callbacks_.count()))) {
          TRANS_LOG(WARN, "resolve callbacks failed", K(ret), KPC(this));
          return_log_cb_(log_cb);
          log_cb = NULL;
        } else if (OB_FAIL(acquire_ctx_ref_())) {
        } else if (OB_FAIL(submit_log_block_out_(log_block, SCN::min_scn(), log_cb, replay_hint,
                                                 commit_info_log_barrier))) {
          TRANS_LOG(ERROR, "submit log to clog adapter failed", KR(ret), K(*this));
          return_log_cb_(log_cb);
          log_cb = NULL;
          release_ctx_ref_();
        } else if (OB_FAIL(after_submit_log_(log_block, log_cb, &helper))) {
        } else {
          redo_log_submitted = true;
          commit_log.set_prev_lsn(log_cb->get_lsn());
          // TRANS_LOG(INFO, "submit redo and commit_info log in clog adapter success", K(*log_cb));
          if (OB_SUCC(ret)) {
            if (OB_FAIL(set_start_scn_in_commit_log_(commit_log))) {
            }
          }

          log_cb = NULL;

          if(OB_FAIL(ret)) {
            // do nothing
          } else if (OB_FAIL(prepare_log_cb_(NEED_FINAL_CB, log_cb))) {
            if (OB_UNLIKELY(OB_TX_NOLOGCB != ret)) {
              TRANS_LOG(WARN, "get log cb failed", KR(ret), K(*this));
            }
          } else if (OB_FAIL(log_block.add_new_log(commit_log))) {
            TRANS_LOG(WARN, "add new log failed", KR(ret), K(*this));
            return_log_cb_(log_cb);
            log_cb = NULL;
          } else if (log_block.get_cb_arg_array().count() == 0) {
            ret = OB_ERR_UNEXPECTED;
            TRANS_LOG(ERROR, "cb arg array is empty", K(ret), K(log_block));
            return_log_cb_(log_cb);
            log_cb = NULL;
          } else if (OB_FAIL(acquire_ctx_ref_())) {
          } else if (OB_FAIL(submit_log_block_out_(log_block,
                                                   ctx_tx_data_.get_commit_version(),
                                                   log_cb,
                                                   replay_hint,
                                                   commit_log_barrier_type))) {
            TRANS_LOG(ERROR, "submit log to clog adapter failed", KR(ret), K(*this));
            return_log_cb_(log_cb);
            log_cb = NULL;
            release_ctx_ref_();
          } else {
            // The transaction updates its commit version from the commit log.
            if (OB_SUCC(ret)) {
              int tmp_ret = OB_SUCCESS;
              if (OB_SUCCESS
                  != (tmp_ret = ctx_tx_data_.set_commit_version(log_cb->get_log_ts()))) {
              }
            }
            if (OB_FAIL(after_submit_log_(log_block, log_cb, NULL))) {
              // do nothing
            }
          }
        }
      } else {
        TRANS_LOG(WARN, "add new log failed", KR(ret), K(*this));
        return_log_cb_(log_cb);
        log_cb = NULL;
      }
    } else if (log_block.get_cb_arg_array().count() == 0) {
      ret = OB_ERR_UNEXPECTED;
      TRANS_LOG(ERROR, "cb arg array is empty", K(ret), K(log_block));
      return_log_cb_(log_cb);
      log_cb = NULL;
    } else if (OB_FAIL(prepare_log_cb_(NEED_FINAL_CB, log_cb))) {
      if (OB_UNLIKELY(OB_TX_NOLOGCB != ret)) {
        TRANS_LOG(WARN, "get log cb failed", KR(ret), K(*this));
      }
    } else if (OB_FAIL(log_cb->reserve_callbacks(helper.callbacks_.count()))) {
      TRANS_LOG(WARN, "resolve callbacks failed", K(ret), KPC(this));
      return_log_cb_(log_cb);
      log_cb = NULL;
    } else if (OB_FAIL(acquire_ctx_ref_())) {
    } else if (OB_FAIL(submit_log_block_out_(log_block,
                                             ctx_tx_data_.get_commit_version(),
                                             log_cb,
                                             replay_hint,
                                             compound_log_barrier_type))) {
      TRANS_LOG(ERROR, "submit log to clog adapter failed", KR(ret), K(*this));
      release_ctx_ref_();
      return_log_cb_(log_cb);
      log_cb = NULL;
    } else {
      // The transaction updates its commit version from the commit log.
      if (OB_SUCC(ret)) {
        int tmp_ret = OB_SUCCESS;
        if (OB_SUCCESS != (tmp_ret = ctx_tx_data_.set_commit_version(log_cb->get_log_ts()))) {
        }
      }
      if (OB_FAIL(after_submit_log_(log_block, log_cb, &helper))) {
      } else {
        redo_log_submitted = true;
      }
    }
  }

  return ret;
}

int ObTxCtx::submit_abort_log_()
{
  int ret = OB_SUCCESS;
  set_target_state(ObTxState::ABORT);
  ObTxLogCb *log_cb = NULL;
  ObTxLogBlock log_block;
  const int64_t replay_hint = trans_id_.get_id();
  //using LogBarrierType = logservice::ObReplayBarrierType;
  //logservice::ObReplayBarrierType barrier = LogBarrierType::NO_NEED_BARRIER;

  logservice::ObReplayBarrierType abort_log_barrier_type =
      logservice::ObReplayBarrierType::NO_NEED_BARRIER;
  if (OB_FAIL(mds_cache_.reserve_final_notify_array(exec_info_.multi_data_source_))) {
  } else if (OB_FAIL(mds_cache_.generate_final_notify_array(exec_info_.multi_data_source_,
                                                            true /*need_merge_cache*/,
                                                            false /*allow_log_overflow*/))) {
  }

  ObTxAbortLog abort_log(mds_cache_.get_final_notify_array());

  if (OB_SUCC(ret)) {
    if (OB_FAIL(ret)) {
      // do nothing
    } else if (OB_FAIL(abort_log.init_tx_data_backup(ctx_tx_data_.get_start_log_ts()))) {
    } else if (exec_info_.redo_lsns_.count() > 0 || exec_info_.max_applying_log_ts_.is_valid()) {
      if (!abort_log.get_backup_start_scn().is_valid()) {
        ret = OB_ERR_UNEXPECTED;
        TRANS_LOG(WARN, "unexpected start scn in commit log", K(ret), K(abort_log), KPC(this));
      }
    }
  }

  if (OB_FAIL(ret)) {
  } else if (OB_FAIL(
                 decide_state_log_barrier_type_(ObTxLogType::TX_ABORT_LOG, abort_log_barrier_type))) {
  } else if (OB_FAIL(init_log_block_(log_block))) {
  } else if (OB_FAIL(log_block.add_new_log(abort_log))) {
  } else if (log_block.get_cb_arg_array().count() == 0) {
    ret = OB_ERR_UNEXPECTED;
    TRANS_LOG(ERROR, "cb arg array is empty", K(ret), K(log_block));
    return_log_cb_(log_cb);
    log_cb = NULL;
  } else if (OB_FAIL(prepare_log_cb_(NEED_FINAL_CB, log_cb))) {
    if (OB_UNLIKELY(OB_TX_NOLOGCB != ret)) {
      TRANS_LOG(WARN, "get log cb failed", KR(ret), K(*this));
    }
  } else if (OB_FAIL(ctx_tx_data_.reserve_tx_op_space(mds_cache_.count() + 1/*promise tx_op pre_alloc safe*/))) {
    TRANS_LOG(WARN, "reserve tx_op space failed", KR(ret), KPC(this));
    return_log_cb_(log_cb);
    log_cb = NULL;
  } else if (OB_FAIL(acquire_ctx_ref_())) {
  } else if (OB_FAIL(submit_log_block_out_(log_block, SCN::min_scn(), log_cb, replay_hint, abort_log_barrier_type, 50 * 1000))) {
    TRANS_LOG(ERROR, "submit log to clog adapter failed", KR(ret), K(*this));
    return_log_cb_(log_cb);
    log_cb = NULL;
    release_ctx_ref_();
  } else if (OB_FAIL(after_submit_log_(log_block, log_cb, NULL))) {
  } else {
    // TRANS_LOG(INFO, "submit abort log in clog adapter success", K(*log_cb));
    reset_redo_lsns_();
  }

  return ret;
}

int ObTxCtx::submit_clear_log_()
{
  int ret = OB_SUCCESS;
  ObTxLogBlock log_block;
  ObTxClearLog clear_log;
  const int64_t replay_hint = trans_id_.get_id();
  ObTxLogCb *log_cb = NULL;
  if (OB_FAIL(ret)) {
    // do nothing
  } else if (OB_FAIL(init_log_block_(log_block))) {
  } else if (OB_FAIL(prepare_log_cb_(!NEED_FINAL_CB, log_cb))) {
    if (OB_UNLIKELY(OB_TX_NOLOGCB != ret)) {
      TRANS_LOG(WARN, "get log cb failed", KR(ret), K(*this));
    }
  } else if (OB_FAIL(log_block.add_new_log(clear_log))) {
    TRANS_LOG(WARN, "add new log failed", KR(ret), K(*this));
    return_log_cb_(log_cb);
    log_cb = NULL;
  } else if (log_block.get_cb_arg_array().count() == 0) {
    ret = OB_ERR_UNEXPECTED;
    TRANS_LOG(ERROR, "cb arg array is empty", K(ret), K(log_block));
    return_log_cb_(log_cb);
    log_cb = NULL;
  } else if (OB_FAIL(acquire_ctx_ref_())) {
  } else if (OB_FAIL(submit_log_block_out_(log_block, ctx_tx_data_.get_end_log_ts(), log_cb))) {
    TRANS_LOG(ERROR, "submit log to clog adapter failed", KR(ret), K(*this));
    return_log_cb_(log_cb);
    log_cb = NULL;
    release_ctx_ref_();
  } else if (OB_FAIL(after_submit_log_(log_block, log_cb, NULL))) {
  } else {
    // TRANS_LOG(INFO, "submit clear log in clog adapter success", K(*log_cb));
    log_cb = NULL;
  }

  return ret;
}

int ObTxCtx::submit_record_log_()
{
  int ret = OB_SUCCESS;
  ObTxLogBlock log_block;
  ObTxRecordLog record_log(exec_info_.prev_record_lsn_, exec_info_.redo_lsns_);
  const int64_t replay_hint = trans_id_.get_id();
  ObTxLogCb *log_cb = NULL;
  if (OB_FAIL(init_log_block_(log_block))) {
  } else if (OB_FAIL(prepare_log_cb_(!NEED_FINAL_CB, log_cb))) {
    if (OB_UNLIKELY(OB_TX_NOLOGCB != ret)) {
      TRANS_LOG(WARN, "get log cb failed", KR(ret), K(*this));
    }
  } else if (OB_FAIL(log_block.add_new_log(record_log))) {
    TRANS_LOG(WARN, "add new log failed", KR(ret), K(*this));
    return_log_cb_(log_cb);
    log_cb = NULL;
  } else if (log_block.get_cb_arg_array().count() == 0) {
    ret = OB_ERR_UNEXPECTED;
    TRANS_LOG(ERROR, "cb arg array is empty", K(ret), K(log_block));
    return_log_cb_(log_cb);
    log_cb = NULL;
  } else if (OB_FAIL(acquire_ctx_ref_())) {
  } else if (OB_FAIL(submit_log_block_out_(log_block, SCN::min_scn(), log_cb))) {
    TRANS_LOG(ERROR, "submit log to clog adapter failed", KR(ret), K(*this));
    return_log_cb_(log_cb);
    log_cb = NULL;
    release_ctx_ref_();
  } else if (OB_FAIL(after_submit_log_(log_block, log_cb, NULL))) {
  } else {
    TRANS_LOG(INFO, "submit record log", K(*this));
    reset_redo_lsns_();
    set_prev_record_lsn_(log_cb->get_lsn());
    log_cb = NULL;
  }

  return ret;
}


int ObTxCtx::submit_big_segment_log_()
{
  int ret = OB_SUCCESS;

  ObTxLogBlock log_block;

  ObTxLogCb *log_cb = nullptr;
  const int64_t replay_hint = static_cast<int64_t>(trans_id_.get_id());
  const ObTxLogType source_log_type =
      (big_segment_info_.submit_log_cb_template_->get_cb_arg_array())[0].get_log_type();

  // TODO set replay_barrier_type

  // if one part of big segment log submit into palf failed , the transaction must drive into abort
  // phase.
  if (OB_FAIL(init_log_block_(log_block))) {
  }
  while (OB_SUCC(ret) && big_segment_info_.segment_buf_.is_active()) {
    const char *submit_buf = nullptr;
    int64_t submit_buf_len = 0;
    if (OB_FAIL(prepare_log_cb_(!NEED_FINAL_CB, log_cb))) {
      if (OB_UNLIKELY(OB_TX_NOLOGCB != ret)) {
        TRANS_LOG(WARN, "get log cb failed", KR(ret), K(*this));
      }
    } else if (OB_FAIL(log_cb->copy(*big_segment_info_.submit_log_cb_template_))) {
    } else if (OB_FALSE_IT(ret = (log_block.acquire_segment_log_buf(source_log_type, &big_segment_info_.segment_buf_)))) {
    } else if (OB_EAGAIN != ret && OB_ITER_END != ret) {
      TRANS_LOG(WARN, "acquire one part of big segment log failed", KR(ret), K(*this));
      return_log_cb_(log_cb);
      log_cb = NULL;
//    } else if (OB_ITER_END == ret
//               && OB_FALSE_IT(*log_cb = *(big_segment_info_.submit_log_cb_template_))) {
    } else if (log_block.get_cb_arg_array().count() == 0) {
      ret = OB_ERR_UNEXPECTED;
      TRANS_LOG(ERROR, "cb arg array is empty", K(ret), K(log_block));
      return_log_cb_(log_cb);
      log_cb = NULL;
    } else if (OB_FAIL(acquire_ctx_ref_())) {
    } else if (OB_FAIL(submit_log_block_out_(log_block,
                                             big_segment_info_.submit_base_scn_,
                                             log_cb,
                                             0,
                                             ObReplayBarrierType::NO_NEED_BARRIER,
                                             INT64_MAX))) {
      TRANS_LOG(ERROR, "submit log to clog adapter failed", KR(ret), K(*this));
      return_log_cb_(log_cb);
      log_cb = NULL;
      release_ctx_ref_();
    } else if (OB_FAIL(after_submit_log_(log_block, log_cb, NULL))) {
    } else {
      log_cb = NULL;
    }
  }

  return ret;
}

int ObTxCtx::prepare_big_segment_submit_(ObTxLogCb *segment_cb,
                                                const share::SCN &base_scn,
                                                logservice::ObReplayBarrierType barrier_type,
                                                const ObTxLogType &segment_log_type)
{
  int ret = OB_SUCCESS;

  if (!base_scn.is_valid()) {
    ret = OB_INVALID_ARGUMENT;
    TRANS_LOG(WARN, "invalid argument", K(ret), KPC(segment_cb), K(base_scn));
  } else if (!big_segment_info_.segment_buf_.is_active()) {
    ret = OB_ERR_UNEXPECTED;
    TRANS_LOG(WARN, "inactive segment buf", K(ret), K(big_segment_info_), KPC(this));
  } else if (OB_NOT_NULL(big_segment_info_.submit_log_cb_template_)) {
  } else if (OB_ISNULL(big_segment_info_.submit_log_cb_template_ = static_cast<ObTxLogCb *>(
                           share::server_malloc(sizeof(ObTxLogCb), "BigSegmentCb")))) {
    ret = OB_ALLOCATE_MEMORY_FAILED;
    TRANS_LOG(WARN, "alloc log cb template for big segment failed", K(ret), K(big_segment_info_));
  } else if (OB_FALSE_IT(new (big_segment_info_.submit_log_cb_template_) ObTxLogCb())) {
  }

  if (OB_SUCC(ret)) {
    if (OB_NOT_NULL(segment_cb)) {
      big_segment_info_.submit_log_cb_template_->get_cb_arg_array().reuse();
      if (OB_FAIL(big_segment_info_.submit_log_cb_template_->copy(*segment_cb))) {
      } else if (OB_FAIL(big_segment_info_.submit_log_cb_template_->get_cb_arg_array().push_back(
                     ObTxCbArg(segment_log_type, nullptr)))) {
      }
    }
    big_segment_info_.submit_base_scn_ = base_scn;
    big_segment_info_.submit_barrier_type_ = barrier_type;
  }

  return ret;
}

// void ObTxCtx::after_segment_part_submit_(ObTxLogCb * submit_log_cb,share::SCN log_scn)
// {
//
// }
int ObTxCtx::add_unsynced_segment_cb_(ObTxLogCb *log_cb)
{
  int ret = OB_SUCCESS;
  ObTxLogCbRecord cb_record(*log_cb);
  if (!is_for_replay()) {
    big_segment_info_.segment_buf_.set_prev_part_id(log_cb->get_log_ts().get_val_for_gts());
  }

  if (OB_FAIL(big_segment_info_.unsynced_segment_part_cbs_.push_back(cb_record))) {
  }
  return ret;
}

int ObTxCtx::remove_unsynced_segment_cb_(const share::SCN &remove_scn)
{
  int ret = OB_SUCCESS;
  // big_segment_info_.unsynced_segment_part_cbs_.remove(log_cb);
  int remove_index = -1;
  for (int i = 0; i < big_segment_info_.unsynced_segment_part_cbs_.count() && OB_SUCC(ret); i++) {
    if (big_segment_info_.unsynced_segment_part_cbs_[i].self_scn_ == remove_scn) {
      remove_index = i;
    }
  }

  if (OB_SUCC(ret) && remove_index >= 0) {
    if (OB_FAIL(big_segment_info_.unsynced_segment_part_cbs_.remove(remove_index))) {
    }
  }

  return ret;
}

share::SCN ObTxCtx::get_min_unsyncd_segment_scn_()
{
  share::SCN min_scn;
  min_scn.invalid_scn();

  if (!big_segment_info_.unsynced_segment_part_cbs_.empty()) {
    const int64_t cb_cnt = big_segment_info_.unsynced_segment_part_cbs_.count();
    for (int64_t i = 0; i < cb_cnt; i++) {
      if (!min_scn.is_valid()) {
        min_scn = big_segment_info_.unsynced_segment_part_cbs_[i].self_scn_;
      } else {
        min_scn =
            share::SCN::min(min_scn, big_segment_info_.unsynced_segment_part_cbs_[i].self_scn_);
      }
      if (big_segment_info_.unsynced_segment_part_cbs_[i].first_part_scn_.is_valid()) {
        min_scn = share::SCN::min(min_scn,
                                  big_segment_info_.unsynced_segment_part_cbs_[i].first_part_scn_);
      }
    }
  }

  return min_scn;
}

inline
int ObTxCtx::submit_log_block_out_(ObTxLogBlock &log_block,
                                          const share::SCN &base_scn,
                                          ObTxLogCb *&log_cb,
                                          const int64_t replay_hint,
                                          const logservice::ObReplayBarrierType barrier,
                                          const int64_t retry_timeout_us)
{
  int ret = OB_SUCCESS;
  if ((!is_contain(log_block.get_cb_arg_array(), ObTxLogType::TX_ABORT_LOG)
              && !is_contain(log_block.get_cb_arg_array(), ObTxLogType::TX_CLEAR_LOG))
             && (is_force_abort_logging_()
                 || get_downstream_state() == ObTxState::ABORT)) {
    ret = OB_TRANS_KILLED;
    TRANS_LOG(ERROR, "tx has been aborting, can not submit other log", K(ret), KPC(this));
  } else if (big_segment_info_.segment_buf_.is_active()
             && !is_contain(log_block.get_cb_arg_array(), ObTxLogType::TX_BIG_SEGMENT_LOG)) {
    ret = OB_LOG_TOO_LARGE;
    TRANS_LOG(INFO, "can not submit any log before all big log submittted", K(ret), KPC(log_cb),
              K(replay_hint), K(barrier), K(base_scn), K(big_segment_info_));
  } else {
    const int64_t replay_hint_v = replay_hint ?: trans_id_.get_id();
    log_block.get_header().set_log_entry_no(exec_info_.next_log_entry_no_);
    if (OB_FAIL(log_block.seal(replay_hint_v, barrier))) {
    } else if (OB_SUCC(ls_tx_ctx_mgr_->get_ls_log_adapter()
                       ->submit_log(log_block.get_buf(),
                                    log_block.get_size(),
                                    base_scn,
                                    log_cb,
                                    true,
                                    retry_timeout_us))) {
      busy_cbs_.add_last(log_cb);
      log_cb->set_log_size(log_block.get_size());
      ObTxLogCbPool::start_syncing_with_stat(log_cb->get_group_ptr(), log_block.get_size());
    }
  }
  return ret;
}

int ObTxCtx::submit_log_impl_(const ObTxLogType log_type)
{
  int ret = OB_SUCCESS;
  int tmp_ret = OB_SUCCESS;
  SERVER_MODULE_SCOPE
  {
    if (big_segment_info_.segment_buf_.is_active()) {
      ret = OB_LOG_TOO_LARGE;
      TRANS_LOG(INFO, "can not submit any log before all big log submittted", K(ret), K(log_type),
                K(trans_id_), K(big_segment_info_));
    } else {
      switch (log_type) {
      case ObTxLogType::TX_COMMIT_INFO_LOG: {
        ret = submit_redo_commit_info_log_();
        break;
      }
      case ObTxLogType::TX_COMMIT_LOG: {
        if (!mt_ctx_.is_prepared()) {
          ret = generate_commit_version_();
        }
        if (OB_SUCC(ret) && mt_ctx_.is_prepared()) {
          ret = submit_commit_log_();
        }
        break;
      }
      case ObTxLogType::TX_ABORT_LOG: {
        ret = submit_abort_log_();
        break;
      }
      case ObTxLogType::TX_CLEAR_LOG: {
        ret = submit_clear_log_();
        break;
      }
      case ObTxLogType::TX_MULTI_DATA_SOURCE_LOG: {
        ret = submit_multi_data_source_();
        break;
      }
      default: {
        TRANS_LOG(ERROR, "unknown submit log type");
      }
      }
    }
  }

  if (OB_FAIL(ret) && REACH_TIME_INTERVAL(100 * 1000)) {
    TRANS_LOG(WARN, "submit_log_impl_ failed", KR(ret), K(log_type), K(*this));
  } else {
#ifndef NDEBUG
    TRANS_LOG(INFO, "submit_log_impl_ end", KR(ret), K(log_type), K(*this));
#endif
  }
  if (OB_FAIL(ret)) {
    handle_submit_log_err_(log_type, ret);
  }
  return ret;
}

void ObTxCtx::handle_submit_log_err_(const ObTxLogType log_type, int &ret)
{
  if (OB_TX_NOLOGCB == ret) {
    if (REACH_COUNT_PER_SEC(10) && REACH_TIME_INTERVAL(100 * 1000)) {
      TRANS_LOG(INFO, "can not get log_cb when submit_log", KR(ret), K(log_type),
                "busy_cbs.first", PC(busy_cbs_.is_empty() ? NULL : busy_cbs_.get_first()));
    }
    if (ObTxLogType::TX_COMMIT_LOG == log_type || ObTxLogType::TX_COMMIT_INFO_LOG == log_type) {
      // need submit log in log sync callback
      // rewrite ret
      ret = OB_SUCCESS;
    }
  } else if (OB_LOG_TOO_LARGE == ret) {
    if (OB_FAIL(submit_big_segment_log_())) {
    }
  } else if (OB_ERR_TOO_BIG_ROWSIZE == ret) {
    int tmp_ret = OB_SUCCESS;
    if (ObPartTransAction::COMMIT == part_trans_action_
        || get_target_state() >= ObTxState::REDO_COMPLETE) {
      if (OB_TMP_FAIL(do_local_tx_end_(TxEndAction::ABORT_TX))) {
      } else {
        TRANS_LOG(WARN, "do abort tx end for committing txn", K(ret),
                  K(log_type), KPC(this));
      }
    } else {
      if (OB_TMP_FAIL(do_local_tx_end_(TxEndAction::DELAY_ABORT_TX))) {
      } else {
        TRANS_LOG(WARN, "row size is too big for only one redo", K(ret),
                  K(log_type), KPC(this));
      }
    }
  }
}

void ObTxCtx::reset_redo_lsns_()
{
  exec_info_.redo_lsns_.reset();
}

void ObTxCtx::set_prev_record_lsn_(const LogOffSet &prev_record_lsn)
{
  exec_info_.prev_record_lsn_ = prev_record_lsn;
}

bool ObTxCtx::need_record_log_() const
{
  // Record Log will be generated if the number of log ids
  // is no less than the max size of prev_redo_log_ids_
  uint64_t prev_redo_lsns_count = MAX_PREV_LOG_IDS_COUNT;
   #ifdef ERRSIM
  // Error injection test, used for changing prev_redo_lsns_count for test
  int tmp_ret = OB_E(EventTable::EN_LOG_IDS_COUNT_ERROR) OB_SUCCESS;
  if (tmp_ret != OB_SUCCESS) {
    prev_redo_lsns_count = 2;
    TRANS_LOG(INFO, "need_record_log: ", K(prev_redo_lsns_count));
  }
  #endif
  return get_redo_log_no_() >= prev_redo_lsns_count && !runtime_state_.is_info_log_submitted();
}

#define bitmap_is_contain(X) (bitmap & (uint64_t)X)

//big row may use a unused log_block to invoke after_submit_log_
int ObTxCtx::after_submit_log_(ObTxLogBlock &log_block,
                                      ObTxLogCb *log_cb,
                                      ObRedoLogSubmitHelper *helper)
{
  int ret = OB_SUCCESS;
  uint64_t bitmap = 0;
  const ObTxCbArgArray &cb_arg_array = log_block.get_cb_arg_array();
  if (cb_arg_array.count() == 0) {
    ret = OB_ERR_UNEXPECTED;
    TRANS_LOG(ERROR, "cb arg array is empty", K(ret), K(*this));
  } else if (OB_FAIL(log_cb->get_cb_arg_array().assign(cb_arg_array))) {
  } else {
    for (int i = 0; i < cb_arg_array.count(); i++) {
      bitmap |= (uint64_t)cb_arg_array.at(i).get_log_type();
    }
    if (bitmap_is_contain(ObTxLogType::TX_REDO_LOG) ||
        bitmap_is_contain(ObTxLogType::TX_ROLLBACK_TO_LOG) ||
        bitmap_is_contain(ObTxLogType::TX_BIG_SEGMENT_LOG) ||
        bitmap_is_contain(ObTxLogType::TX_MULTI_DATA_SOURCE_LOG)) {
      if (!bitmap_is_contain(ObTxLogType::TX_COMMIT_INFO_LOG)) {
        TRANS_LOG(TRACE, "redo_lsns.push", K(log_cb->get_lsn()));
        ret = exec_info_.redo_lsns_.push_back(log_cb->get_lsn());
      }
    }
  }
  if (OB_SUCC(ret) && OB_FAIL(update_rec_log_ts_(false/*for_replay*/, SCN()))) {
    TRANS_LOG(WARN, "update rec log ts failed", KR(ret), KPC(log_cb), K(*this));
  }
  if (OB_SUCC(ret) && bitmap_is_contain(ObTxLogType::TX_REDO_LOG)) {
    if (OB_FAIL(log_cb->set_callbacks(helper->callbacks_))) {
      ob_abort();
    } else {
      exec_info_.max_submitted_seq_no_.inc_update(helper->max_seq_no_);
      helper->log_scn_ = log_cb->get_log_ts();
      if (helper->callback_redo_submitted_ && OB_FAIL(mt_ctx_.log_submitted(*helper))) {
        TRANS_LOG(ERROR, "fill to do log_submitted on redo log gen", K(ret), K(*this));
      }
    }
  }
  if (OB_SUCC(ret) && bitmap_is_contain(ObTxLogType::TX_ROLLBACK_TO_LOG)) {
    // do nothing
  }
  if (OB_SUCC(ret) && bitmap_is_contain(ObTxLogType::TX_MULTI_DATA_SOURCE_LOG)) {
    // do nothing
    log_cb->get_mds_range().range_submitted(mds_cache_);
  }
  if(OB_SUCC(ret) && bitmap_is_contain(ObTxLogType::TX_BIG_SEGMENT_LOG))
  {
    add_unsynced_segment_cb_(log_cb);
    if (big_segment_info_.segment_buf_.is_completed()) {
      TRANS_LOG(INFO, "reuse big_segment_info_",K(ret),K(big_segment_info_),KPC(log_cb));
      big_segment_info_.reuse();
    }
  }
  if (OB_SUCC(ret) && bitmap_is_contain(ObTxLogType::TX_COMMIT_INFO_LOG)) {
    runtime_state_.set_info_log_submitted();
  }
  if (OB_SUCC(ret) && bitmap_is_contain(ObTxLogType::TX_COMMIT_LOG)) {
    runtime_state_.set_state_log_submitting();
    runtime_state_.set_state_log_submitted();
    // elr
    const bool has_row_updated = mt_ctx_.has_row_updated();
    if (can_elr_ && has_row_updated) {
      if (OB_FAIL(ctx_tx_data_.set_state(ObTxData::ELR_COMMIT))) {
      }
      elr_handler_.check_and_early_lock_release(has_row_updated, this);
    }
  }
  if (OB_SUCC(ret) && bitmap_is_contain(ObTxLogType::TX_ABORT_LOG)) {
    runtime_state_.set_state_log_submitting();
    runtime_state_.set_state_log_submitted();
  }
  if (OB_SUCC(ret) && bitmap_is_contain(ObTxLogType::TX_CLEAR_LOG)) {
    runtime_state_.set_state_log_submitting();
    runtime_state_.set_state_log_submitted();
  }
  if (OB_SUCC(ret)) {
    if (!ctx_tx_data_.get_start_log_ts().is_valid()) {
      if (OB_FAIL(ctx_tx_data_.set_start_log_ts(log_cb->get_log_ts()))) {
      }
    }
  }
  if(OB_FAIL(ret)) {
  } else {
#ifndef NDEBUG
    TRANS_LOG(INFO, "after submit log success", K(ret), K(trans_id_), K(exec_info_), K(*log_cb), KPC(this));
#endif
  }
  REC_TRANS_TRACE_EXT(tlog_,
                      after_submit_log,
                      OB_ID(ret),
                      ret,
                      OB_ID(log_no),
                      exec_info_.next_log_entry_no_,
                      OB_ID(base_ts),
                      log_cb->get_base_ts(),
                      OB_ID(t),
                      log_cb->get_log_ts(),
                      OB_ID(lsn),
                      log_cb->get_lsn());

  exec_info_.next_log_entry_no_++;
  reuse_log_block_(log_block);
  return ret;
}

int ObTxCtx::get_max_submitting_log_info_(palf::LSN &lsn, SCN &log_ts)
{
  int ret = OB_SUCCESS;
  ObTxLogCb *log_cb = NULL;
  lsn = LSN(palf::PALF_INITIAL_LSN_VAL);
  log_ts.reset();
  if (!busy_cbs_.is_empty()) {
    log_cb = busy_cbs_.get_last();
    if (OB_ISNULL(log_cb)) {
      ret = OB_ERR_UNEXPECTED;
      TRANS_LOG(ERROR, "log cb is NULL, unexpected error", K(ret));
    } else if (!log_cb->is_valid()) {
      ret = OB_ERR_UNEXPECTED;
      TRANS_LOG(ERROR, "log cb is invalid", K(ret));
    } else {
      lsn = log_cb->get_lsn();
      log_ts = log_cb->get_log_ts();
    }
  } else
  {
    lsn.reset();
  }

  return ret;
}

int ObTxCtx::get_prev_log_lsn_(const ObTxLogBlock &log_block,
                                      ObTxPrevLogType &prev_log_type,
                                      palf::LSN &lsn)
{
  int ret = OB_SUCCESS;
  palf::LSN tmp_lsn;
  SCN tmp_log_ts;
  bool in_same_block = false;

  if (!prev_log_type.is_normal_log() || !log_block.is_inited()) {
    ret = OB_INVALID_ARGUMENT;
    TRANS_LOG(INFO, "invalid arguments", K(ret), K(prev_log_type), K(log_block));
  } else if (is_contain(log_block.get_cb_arg_array(), prev_log_type.convert_to_tx_log_type())) {
    // invalid lsn
    lsn.reset();
    prev_log_type.set_self();
  } else if (OB_FAIL(get_max_submitting_log_info_(tmp_lsn, tmp_log_ts))) {
  } else if (tmp_lsn.is_valid()) {
    if (exec_info_.max_durable_lsn_.is_valid() && exec_info_.max_durable_lsn_ > tmp_lsn) {
      tmp_lsn = exec_info_.max_durable_lsn_;
    }
    lsn = tmp_lsn;
  } else {
    lsn = exec_info_.max_durable_lsn_;
  }

  if (!prev_log_type.is_valid() || (prev_log_type.is_normal_log() && !lsn.is_valid())) {
    ret = OB_ERR_UNEXPECTED;
    TRANS_LOG(WARN, "unexpected prev lsn", K(ret), K(log_block), K(prev_log_type), K(lsn),
              KPC(this));
  }
  return ret;
}

int ObTxCtx::set_start_scn_in_commit_log_(ObTxCommitLog &commit_log)
{
  int ret = OB_SUCCESS;

  if (OB_FAIL(commit_log.init_tx_data_backup(ctx_tx_data_.get_start_log_ts()))) {
  } else if (exec_info_.next_log_entry_no_ > 0) {
    if (!commit_log.get_backup_start_scn().is_valid()) {
      ret = OB_ERR_UNEXPECTED;
      TRANS_LOG(WARN, "unexpected start scn in commit log", K(ret), K(commit_log), KPC(this));
    }
  }

  return ret;
}


int ObTxCtx::try_submit_next_log()
{
  CtxLockGuard guard(lock_);
  return try_submit_next_log_(true);
}

//***************************** for 4.0
int ObTxCtx::check_replay_avaliable_(const palf::LSN &offset,
                                            const SCN &timestamp,
                                            const int64_t &part_log_no,
                                            bool &need_replay)
{
  int ret = OB_SUCCESS;
  need_replay = true;
  if (IS_NOT_INIT) {
    TRANS_LOG(WARN, "ObTxCtx not inited");
    ret = OB_NOT_INIT;
  } else if (OB_UNLIKELY(!is_for_replay())) {
    ret = OB_ERR_UNEXPECTED;
    TRANS_LOG(ERROR, "invalid state, transaction is not replaying", KR(ret), "context", *this);
  } else if (OB_UNLIKELY(!timestamp.is_valid())) {
    TRANS_LOG(WARN, "invalid argument", K(timestamp), K(offset), K(trans_id_));
    ret = OB_INVALID_ARGUMENT;
  // } else if (is_exiting_) {
  //   // ret = OB_TRANS_CTX_NOT_EXIST;
  } else {
    // check state like ObTxCtx::is_trans_valid_for_replay_
    if (!exec_info_.max_applying_log_ts_.is_valid()) {
      // do nothing
    } else if (exec_info_.max_applying_log_ts_ > timestamp) {
      need_replay = false;
    } else if (exec_info_.max_applying_log_ts_ == timestamp
               && exec_info_.max_applying_part_log_no_ > part_log_no) {
      need_replay = false;
    } else {
      // TODO check log_type and state
    }
  }

  if (OB_SUCC(ret)) {
    if (need_replay && !create_ctx_scn_.is_valid()) {
      create_ctx_scn_ = timestamp;
    }
  }

  return ret;
}

int ObTxCtx::push_replaying_log_ts(const SCN log_ts_ns, const int64_t log_entry_no)
{
  int ret = OB_SUCCESS;

  CtxLockGuard guard(lock_);

  if (log_ts_ns < exec_info_.max_applying_log_ts_) {
    TRANS_LOG(WARN,
              "[Replay Tx] replay a log with a older ts than part_ctx state, it will be ignored",
              K(exec_info_.max_applying_log_ts_), K(log_ts_ns));
  } else if (log_ts_ns > exec_info_.max_applying_log_ts_) {
    exec_info_.max_applying_log_ts_ = log_ts_ns;
    exec_info_.max_applying_part_log_no_ = 0;
  }
  if (OB_SUCC(ret)) {
    if (!ctx_tx_data_.get_start_log_ts().is_valid()) {
      ctx_tx_data_.set_start_log_ts(log_ts_ns);
    }
    if (OB_UNLIKELY(replay_completeness_.is_unknown())) {
      const bool replay_continous = exec_info_.next_log_entry_no_ == log_entry_no;
      if (OB_FAIL(set_replay_completeness_(replay_continous, log_ts_ns))) {
      }
    }
  }
  return ret;
}

int ObTxCtx::push_replayed_log_ts(const SCN log_ts_ns,
                                         const palf::LSN &offset,
                                         const int64_t log_entry_no)
{
  int ret = OB_SUCCESS;

  CtxLockGuard guard(lock_);

  if (exec_info_.max_applied_log_ts_ < log_ts_ns) {
    exec_info_.max_applied_log_ts_ = log_ts_ns;
  }

  if (!exec_info_.max_durable_lsn_.is_valid() || offset > exec_info_.max_durable_lsn_) {
    exec_info_.max_durable_lsn_ = offset;
  }

  if (log_entry_no >= exec_info_.next_log_entry_no_) {
    // In ActiveInfoLog is replayed, its log_entry_no is the final
    // of last leader, set the next_log_entry_no for new leader
    exec_info_.next_log_entry_no_ = log_entry_no + 1;
  }

  update_rec_log_ts_(true/*for_replay*/, log_ts_ns);

  if (OB_SUCC(ret)) {
    if (big_segment_info_.segment_buf_.is_completed()
        && big_segment_info_.unsynced_segment_part_cbs_.count() > 0) {
      // if (big_segment_info_.submit_log_cb_template_
      //     == big_segment_info_.unsynced_segment_part_cbs_.get_first()) {
        remove_unsynced_segment_cb_(big_segment_info_.unsynced_segment_part_cbs_[0].self_scn_);
        big_segment_info_.reuse();
      // } else {
      //   ret = OB_ERR_UNEXPECTED;
      //   TRANS_LOG(ERROR, "unexpectd unsynced_segment_part_cbs_", K(ret), K(log_ts_ns),
      //             K(big_segment_info_), KPC(this));
      // }
    }
  }

  return ret;
}

int ObTxCtx::iter_next_log_for_replay(ObTxLogBlock &log_block,
                                             ObTxLogHeader &log_header,
                                             const share::SCN log_scn)
{
  int ret = OB_SUCCESS;

  CtxLockGuard guard(lock_);

  if (OB_FAIL(log_block.get_next_log(log_header, &big_segment_info_.segment_buf_))) {
    if (OB_START_LOG_CURSOR_INVALID == ret) {
      TRANS_LOG(WARN, "start replay from the mid of big segment", K(ret), K(log_scn), K(log_header),
                K(big_segment_info_), KPC(this));
      ret = OB_SUCCESS;
    } else if (OB_LOG_TOO_LARGE == ret) {
      ret = OB_SUCCESS;
      if (OB_ISNULL(big_segment_info_.submit_log_cb_template_)) {
        if (OB_ISNULL(big_segment_info_.submit_log_cb_template_ = static_cast<ObTxLogCb *>(
                          share::server_malloc(sizeof(ObTxLogCb), "BigSegmentCb")))) {
          ret = OB_ALLOCATE_MEMORY_FAILED;
          TRANS_LOG(WARN, "alloc log cb template for big segment failed", K(ret), K(log_scn),
                    K(big_segment_info_));
        } else {
          new (big_segment_info_.submit_log_cb_template_) ObTxLogCb();
        }
      }

      if (OB_SUCC(ret)) {
        big_segment_info_.submit_log_cb_template_->set_log_ts(log_scn);
        if (!big_segment_info_.submit_log_cb_template_->get_first_part_scn().is_valid()) {
          big_segment_info_.submit_log_cb_template_->set_first_part_scn(log_scn);
          add_unsynced_segment_cb_(big_segment_info_.submit_log_cb_template_);
        }
      }

    } else if (OB_NO_NEED_UPDATE == ret) {
      TRANS_LOG(INFO, "collect all part of big segment", K(ret), K(log_scn), K(log_header),
                K(big_segment_info_), KPC(this));
      ret = OB_SUCCESS;
    } else if (OB_ITER_END == ret) {
      // do nothing
    }
  }

  return ret;
}

int ObTxCtx::replay_one_part_of_big_segment(const palf::LSN &offset,
                                                   const share::SCN &timestamp,
                                                   const int64_t &part_log_no)
{
  CtxLockGuard guard(lock_);

  int ret = OB_SUCCESS;
  bool need_replay = true;
  if (OB_FAIL(check_replay_avaliable_(offset, timestamp, part_log_no, need_replay))) {
  } else if (!need_replay) {
    TRANS_LOG(INFO, "need not replay log", K(timestamp), K(offset), K(*this));
    // no need to replay
  } else if (OB_FAIL(update_replaying_log_no_(timestamp, part_log_no))) {
  }

  return ret;
}

int ObTxCtx::update_replaying_log_no_(const SCN &log_ts_ns, int64_t part_log_no)
{
  int ret = OB_SUCCESS;

  if (exec_info_.max_applying_log_ts_ != log_ts_ns) {
    ret = OB_ERR_UNEXPECTED;
    TRANS_LOG(WARN, "Unexpected replaying log ts", K(ret), K(exec_info_.max_applying_log_ts_),
              K(log_ts_ns));
  } else if (exec_info_.max_applying_part_log_no_ != part_log_no
             && exec_info_.max_applying_part_log_no_ + 1 != part_log_no) {
    ret = OB_ERR_UNEXPECTED;
    TRANS_LOG(WARN, "Unexpected replaying log no", K(ret), K(exec_info_.max_applying_part_log_no_),
              K(part_log_no));
  } else {
    exec_info_.max_applying_part_log_no_ = part_log_no;
  }

  return ret;
}

int ObTxCtx::replay_update_tx_data_(const bool commit,
                                           const SCN &log_ts,
                                           const SCN &commit_version)
{
  int ret = OB_SUCCESS;

  if (OB_FAIL(ctx_tx_data_.set_end_log_ts(log_ts))) {
  } else if (commit) {
    if (!commit_version.is_valid()) {
      if (OB_FAIL(ctx_tx_data_.set_commit_version(log_ts))) {
      }
    } else {
      ret = OB_ERR_UNEXPECTED;
      TRANS_LOG(ERROR, "unexpected trans type or commit_version", K(ret), K(log_ts), K(commit_version));
    }
    if (OB_FAIL(ret)) {
    } else if (OB_FAIL(ctx_tx_data_.set_state(ObTxData::COMMIT))) {
    }
  } else {
    if (OB_FAIL(ctx_tx_data_.set_state(ObTxData::ABORT))) {
    }
  }

  return ret;
}

int ObTxCtx::replace_tx_data_with_backup_(const ObTxDataBackup &backup, SCN log_ts)
{
  int ret = OB_SUCCESS;
  share::SCN tmp_log_ts = log_ts;
  if (backup.get_start_log_ts().is_valid()) {
    tmp_log_ts = backup.get_start_log_ts();
  } else if (exec_info_.next_log_entry_no_ > 1) {
    ret = OB_ERR_UNEXPECTED;
    TRANS_LOG(WARN, "invalid start log ts with a applied log_entry", K(ret), K(backup), K(log_ts),
              KPC(this));
  }

  if (OB_SUCC(ret)) {
    if (OB_FAIL(ctx_tx_data_.set_start_log_ts(tmp_log_ts))) {
    }
  }

  return ret;
}

void ObTxCtx::force_no_need_replay_checksum(const bool parallel_replay,
                                                   const share::SCN &log_ts)
{
  if (ATOMIC_LOAD(&exec_info_.need_checksum_)) {
    CtxLockGuard guard(lock_);
    force_no_need_replay_checksum_(parallel_replay, log_ts);
  }
}

void ObTxCtx::force_no_need_replay_checksum_(const bool parallel_replay,
                                                    const share::SCN &log_ts)
{
  if (ATOMIC_LOAD(&exec_info_.need_checksum_)) {
    TRANS_LOG(INFO, "set skip calc checksum", K_(trans_id), KP(this), K(parallel_replay), K(log_ts));
    if (parallel_replay) {
      update_rec_log_ts_(true/*for_replay*/, log_ts);
    }
    ATOMIC_STORE(&exec_info_.need_checksum_, false);
    mt_ctx_.set_skip_checksum_calc();
  }
}

void ObTxCtx::check_no_need_replay_checksum(const SCN &log_ts, const int index)
{
  // TODO(handora.qc): How to lock the tx_ctx

  // checksum_scn_ means all data's checksum has been calculated before the
  // log of checksum_scn_(not included). So if the data with this scn is
  // not replayed with checksum_scn_ <= scn, it means may exist some data
  // will never be replayed because the memtable will filter the data.

  if (ATOMIC_LOAD(&exec_info_.need_checksum_)) {
    bool need_skip = true;
    bool serial_replay = index == 0;
    // the serial replay or the parallel replay in tx-log queue
    if (serial_replay) {
      const share::SCN serial_final_scn = exec_info_.serial_final_scn_.atomic_load();
      serial_replay = !serial_final_scn.is_valid() || log_ts <= serial_final_scn;
      // the log is before serial final point, if either of checksum_scn larger than
      // the log_ts, the checksum must contains the log_ts
      if (serial_replay) {
        CtxLockGuard guard(lock_); // acquire lock for access array
        ARRAY_FOREACH_NORET(exec_info_.checksum_scn_, i) {
          if (exec_info_.checksum_scn_.at(i) > log_ts) {
            need_skip = false;
            break;
          }
        }
      }
    }
    // for parallel replay, check the corresponding list's checksum_scn
    if (!serial_replay) {
      CtxLockGuard guard(lock_); // acquire lock for access array
      if (exec_info_.checksum_scn_.count() <= index) {
        // the checksum is not exist
      } else if (exec_info_.checksum_scn_.at(index) > log_ts) {
        need_skip = false;
      }
    }
    if (need_skip) {
      CtxLockGuard guard(lock_); // acquire lock for display ctx
      force_no_need_replay_checksum_(index != 0, log_ts);
      TRANS_LOG(INFO, "skip checksum, because checksum calc not continous",
                K(serial_replay), K(index), K(log_ts), KPC(this));
    }
  }
}

/*
 * replay redo in tx ctx
 *
 * since 4.2.4, support parallel replay redo, and the design principle is
 * seperate redo and other logs(named as Txn's Log), redo is belongs to
 * memtable (and locktable), and only Txn's Log will replay into Tx ctx
 * and affect the Tx ctx's state
 *
 */
int ObTxCtx::replay_redo_in_ctx(const ObTxRedoLog &redo_log,
                                       const palf::LSN &offset,
                                       const SCN &timestamp,
                                       const int64_t &part_log_no,
                                       const bool is_tx_log_queue,
                                       const bool serial_final,
                                       const ObTxSEQ &max_seq_no)
{
  int ret = OB_SUCCESS;
  bool need_replay = true;
  common::ObTimeGuard timeguard("replay_redo_in_ctx", 10 * 1000);
  {
    CtxLockGuard guard(lock_);
    if (is_tx_log_queue
        && OB_FAIL(check_replay_avaliable_(offset, timestamp, part_log_no, need_replay))) {
      TRANS_LOG(WARN, "check replay available for redo failed", K(ret), K(offset), K(timestamp),
                K(part_log_no), K_(trans_id));
    } else if (is_tx_log_queue && !need_replay) {
      TRANS_LOG(INFO, "need not replay redo in tx ctx", K(offset), K(timestamp),
                K(part_log_no), K_(trans_id));
    } else if (is_tx_log_queue && OB_FAIL(update_replaying_log_no_(timestamp, part_log_no))) {
      TRANS_LOG(WARN, "update replaying log no for redo failed", K(ret), K(timestamp),
                K(part_log_no), K_(trans_id));
    }
    if (OB_SUCC(ret) && serial_final) {
      // A serial-final redo switches subsequent logging to parallel mode.
      if (!is_tx_log_queue) {
        ret = OB_ERR_UNEXPECTED;
        TRANS_LOG(ERROR, "serial final redo must be in tx_log_queue", K(ret), KPC(this), K(timestamp));
        OB_SAFE_ABORT();
      } else if (!exec_info_.serial_final_scn_.is_valid()) {
        ret = switch_to_parallel_logging_(timestamp, max_seq_no);
      }
    }
  }
  if (OB_SUCC(ret)) {

  }

#ifndef NDEBUG
  TRANS_LOG(INFO, "[Replay Tx] Replay Redo in TxCtx", K(ret),
            K(is_tx_log_queue), K(timestamp), K(offset), KPC(this));
#endif
  return ret;
}

//
// Replay RollbackToLog
// the RollbackToLog operate on memtable, its replay seperate two step
// Step1: add UndoAction to TxData
// Step2: rollback(remove) data on memtable
//
// for Step1, repeatedly replay is handle here
// for Step2, it must be executed even Step1 is should be skipped
//
// When `Branch Savepoint` used, RollbackToLog can be replayed parallelly
// in this situation, Step1 can not be handled efficiently, so repeatedly
// replay such RollbackToLog is possible, maybe use TxData to deduplicate
// is possible.
//
//
int ObTxCtx::replay_rollback_to(const ObTxRollbackToLog &log,
                                       const palf::LSN &offset,
                                       const SCN &timestamp,
                                       const int64_t &part_log_no,
                                       const bool is_tx_log_queue,
                                       const bool pre_barrier)
{
  int ret = OB_SUCCESS;
  common::ObTimeGuard timeguard("replay_rollback_to", 10 * 1000);
  // int64_t start = ObTimeUtility::fast_current_time();
  CtxLockGuard guard(lock_);
  bool need_replay = true;
  ObTxSEQ from = log.get_from();
  ObTxSEQ to = log.get_to();
  if (OB_UNLIKELY(from.get_branch() != to.get_branch())) {
    ret = OB_ERR_UNEXPECTED;
    TRANS_LOG(ERROR, "invalid savepoint", K(log));
  }
  //
  // the log is replay in txn log queue
  // for parallel replay, a global savepoint after the serial final log
  // must set the pre-barrier replay flag
  // some branch savepoint also need this, but we can't distinguish
  // hence only sanity check for global savepoint
  //
  else if (is_tx_log_queue) { // global savepoint or branch level savepoint in txn-log queue
    if (is_parallel_logging()             // has enter parallel logging
        && to.get_branch() == 0           // is a global savepoint
        && timestamp > exec_info_.serial_final_scn_  // it is after the serial final log
        && !pre_barrier) {
      ret = OB_ERR_UNEXPECTED;
      TRANS_LOG(ERROR, "missing pre barrier flag for parallel replay", KR(ret), K(*this));
      OB_SAFE_ABORT();
    } else if (OB_FAIL(check_replay_avaliable_(offset, timestamp, part_log_no, need_replay))) {
    } else if (!need_replay) {
      TRANS_LOG(INFO, "need not replay log", K(log), K(timestamp), K(offset), K(*this));
    } else if (OB_FAIL((update_replaying_log_no_(timestamp, part_log_no)))) {
    }
  } else { // branch level savepoint, parallel replayed
    if (exec_info_.need_checksum_ && !has_replay_serial_final_()) {
      if (OB_UNLIKELY(pre_barrier || replay_completeness_.is_incomplete())) {
        // sanity check, if current is pre-barrier, then
        // either serial final log must been replayed
        // or the txn must been marked not `need_checksum`
        ret = OB_ERR_UNEXPECTED;
        TRANS_LOG(ERROR, "replay branch savepoint hit bug", K(ret), KP(this),
                  K(from), K(to), K(pre_barrier), K_(trans_id), K_(replay_completeness), K_(exec_info));
      } else if (replay_completeness_.is_complete()) {
        ret = OB_EAGAIN;
        if (TC_REACH_TIME_INTERVAL(1_s)) {
          TRANS_LOG(INFO, "branch savepoint should wait replay serial final because of calc checksum",
                    K(ret), K(from), K(to), K(timestamp), KP(this), K_(trans_id), K_(exec_info));
        }
      } else if (replay_completeness_.is_unknown()) {
        // try to fetch the replay position of replay-queue of txn-log
        // to determin whether previouse txn log has been replayed or won't be replayed
        // if so, can decide that the current txn was replayed from middle, and mark it
        // replay incomplete
        share::SCN min_unreplayed_scn;
        logservice::ObLogService *log_service = ::oceanbase::share::server_service<::oceanbase::logservice::ObLogService>();
        if (OB_ISNULL(log_service)) {
          ret = OB_ERR_UNEXPECTED;
          TRANS_LOG(ERROR, "tenant logservice is null", K(ret), K(timestamp), K_(trans_id));
        } else if (OB_FAIL(log_service->get_log_replay_service()->get_min_unreplayed_scn(min_unreplayed_scn))) {
        } else if (min_unreplayed_scn == timestamp) {
          // all previous log replayed
          // the txn must not replay from its first log, aka. incomplete-replay
          TRANS_LOG(INFO, "detect txn replayed from middle", K(ret), K(timestamp), K_(trans_id), K_(exec_info));
          if (OB_FAIL(set_replay_completeness_(false, timestamp))) {
          }
        } else if (min_unreplayed_scn > timestamp) {
          ret = OB_ERR_UNEXPECTED;
          TRANS_LOG(ERROR, "incorrect min unreplayed scn", K(ret), K(timestamp), K(min_unreplayed_scn), K_(trans_id));
        } else {
          ret = OB_EAGAIN;
          if (TC_REACH_TIME_INTERVAL(1_s)) {
            TRANS_LOG(INFO, "branch savepoint should wait replay serial final because of calc checksum",
                      K(ret), K(from), K(to), K(timestamp), K(min_unreplayed_scn), KP(this), K_(trans_id), K_(exec_info));
          }
        }
      } else {
        ret = OB_ERR_UNEXPECTED;
        TRANS_LOG(ERROR, "code should not go here", K(ret), K(timestamp), K_(trans_id), KPC(this));
        OB_SAFE_ABORT();
      }
    }
    if (OB_SUCC(ret) &&
        !ctx_tx_data_.get_start_log_ts().is_valid() &&
        OB_FAIL(ctx_tx_data_.set_start_log_ts(timestamp))) {
      // update start_log_ts for branch savepoint, because it may replayed before first log in txn queue
      TRANS_LOG(WARN, "set tx data start log ts fail", K(ret), K(timestamp), KPC(this));
    }
  }

  //
  // Step1, add Undo into TxData, both for parallel replay and serial replay
  //
  if (OB_SUCC(ret) && need_replay && OB_FAIL(rollback_to_savepoint_(log.get_from(), log.get_to(), timestamp))) {
    TRANS_LOG(WARN, "replay savepoint_rollback fail", K(ret), K(log), K(offset), K(timestamp),
              KPC(this));
  }

  //
  // Step2, remove TxNode(s)
  //
  if (OB_SUCC(ret) && !need_replay) {
    if (OB_FAIL(mt_ctx_.rollback(log.get_to(), log.get_from(), timestamp))) {
    }
  }

  if (OB_FAIL(ret) && OB_EAGAIN != ret) {
    TRANS_LOG(WARN, "[Replay Tx] Replay RollbackToLog in TxCtx Failed", K(timestamp), K(offset),
              K(ret), K(need_replay), K(log), KPC(this));
  } else {
#ifndef NDEBUG
    TRANS_LOG(INFO, "[Replay Tx] Replay RollbackToLog in TxCtx", K(timestamp), K(offset), K(ret),
              K(need_replay), K(log), KPC(this));
#endif
  }

  if (OB_EAGAIN != ret) {
    REC_TRANS_TRACE_EXT(tlog_,
                        replay_rollback_to,
                        OB_ID(ret),
                        ret,
                        OB_ID(used),
                        timeguard.get_diff(),
                        OB_Y(need_replay),
                        OB_ID(offset),
                        offset.val_,
                        OB_ID(t),
                        timestamp,
                        OB_ID(ref),
                        get_ref());
  }

  return ret;
}

int ObTxCtx::replay_commit_info(const ObTxCommitInfoLog &commit_info_log,
                                       const palf::LSN &offset,
                                       const SCN &timestamp,
                                       const int64_t &part_log_no,
                                       const bool pre_barrier)
{
  int ret = OB_SUCCESS;
  int tmp_ret = OB_SUCCESS;
  common::ObTimeGuard timeguard("replay_commit_info", 10 * 1000);
  // const int64_t start = ObTimeUtility::fast_current_time();
  bool need_replay = true;
  CtxLockGuard guard(lock_);
  if (is_parallel_logging() && !pre_barrier) {
    ret = OB_ERR_UNEXPECTED;
    TRANS_LOG(ERROR, "missing pre barrier flag for parallel replay", KR(ret), K(*this));
    OB_SAFE_ABORT();
  } else if (OB_FAIL(check_replay_avaliable_(offset, timestamp, part_log_no, need_replay))) {
  } else if (!need_replay) {
    TRANS_LOG(INFO, "need not replay log", K(commit_info_log), K(timestamp), K(offset), K(*this));
    // no need to replay
  } else if (OB_FAIL(update_replaying_log_no_(timestamp, part_log_no))) {
  } else if (OB_FAIL(exec_info_.redo_lsns_.assign(commit_info_log.get_redo_lsns()))) {
  } else if (OB_FAIL(set_app_trace_id_(commit_info_log.get_app_trace_id()))) {
  } else {
    exec_info_.mark_write_state();
    can_elr_ = commit_info_log.is_elr();
    runtime_state_.set_info_log_submitted();
    reset_redo_lsns_();
    set_durable_state_(ObTxState::REDO_COMPLETE);
    set_target_state(ObTxState::REDO_COMPLETE);
  }

  if (OB_FAIL(ret)) {
    // do nothing
  } else if (OB_FAIL(notify_data_source_(NotifyType::TX_END,
                                         timestamp,
                                         true,
                                         exec_info_.multi_data_source_,
                                         true /*willing_to_commit*/))) {
  }

  const int64_t used_time = timeguard.get_diff();
  REC_TRANS_TRACE_EXT2(tlog_, replay_commit_info, OB_ID(ret), ret,
      OB_ID(used), used_time,
      OB_ID(offset), offset.val_, OB_ID(t), timestamp,
      OB_ID(ref), get_ref());
  // TODO add commit_state_log statistics
  // ObTransStatistic::get_instance().add_redo_log_replay_count( 1);
  // ObTransStatistic::get_instance().add_redo_log_replay_time( end - start);
  if (OB_FAIL(ret)) {
  } else {
#ifndef NDEBUG
    TRANS_LOG(INFO, "[Replay Tx] replay commit info", K(ret), K(used_time), K(timestamp), K(offset),
              K(commit_info_log), K(*this));
#endif
  }
  return ret;
}

ERRSIM_POINT_DEF(TX_REPLAY_COMMIT_FAIL_BEFORE_NOTIFY_TABLELOCK)
ERRSIM_POINT_DEF(TX_REPLAY_COMMIT_FAIL_AFTER_NOTIFY_TABLELOCK)
ERRSIM_POINT_DEF(TX_REPLAY_COMMIT_FAIL_AFTER_NOTIFY_ON_COMMIT)
ERRSIM_POINT_DEF(TX_REPLAY_COMMIT_FAIL_AFTER_COMMIT)
int ObTxCtx::replay_commit(const ObTxCommitLog &commit_log,
                                  const palf::LSN &offset,
                                  const SCN &timestamp,
                                  const int64_t &part_log_no,
                                  const SCN &replay_compact_version)
{
  int ret = OB_SUCCESS;
  common::ObTimeGuard timeguard("replay_commit", 10 * 1000);
  CtxLockGuard guard(lock_);
  // const int64_t start = ObTimeUtility::fast_current_time();
  const SCN commit_version = commit_log.get_commit_version();
  bool need_replay = true;

  if (OB_FAIL(check_replay_avaliable_(offset, timestamp, part_log_no, need_replay))) {
  } else if (OB_FAIL(replay_update_tx_data_(true, timestamp, commit_version))) {
  } else if (OB_FAIL(replace_tx_data_with_backup_(commit_log.get_tx_data_backup(), timestamp))) {
  } else if (!need_replay) {
    // TODO insert_into_tx_table before need_replay and give it the ability to retry
    TRANS_LOG(INFO, "need not replay log", K(commit_log), K(timestamp), K(offset), K(*this));
    // no need to replay
  } else if (OB_FAIL(update_replaying_log_no_(timestamp, part_log_no))) {
  } else {
    if (OB_SUCC(ret)) {
      if ((!commit_log.get_multi_source_data().empty() || !exec_info_.multi_data_source_.empty())
          && replay_completeness_.is_incomplete()) {
        // ret = OB_ERR_UNEXPECTED;
        TRANS_LOG(ERROR, "mds part_ctx can not replay from the middle", K(ret), K(timestamp), K(offset),
                  K(commit_log), KPC(this));
      }
    }
    if (OB_SUCC(ret)) {
      set_durable_state_(ObTxState::COMMIT);
      set_target_state(ObTxState::COMMIT);
    }
  }

  if (OB_SUCC(ret)) {
    const uint64_t checksum =
        (exec_info_.need_checksum_ && replay_completeness_.is_complete() ? commit_log.get_checksum() : 0);
    mt_ctx_.set_replay_compact_version(replay_compact_version);

    if (OB_FAIL(TX_REPLAY_COMMIT_FAIL_BEFORE_NOTIFY_TABLELOCK)) {
    } else if (OB_FAIL(notify_table_lock_(timestamp,
                                          true,
                                          exec_info_.multi_data_source_,
                                          false /* not a force kill */))) {
    } else if (OB_FAIL(TX_REPLAY_COMMIT_FAIL_AFTER_NOTIFY_TABLELOCK)) {
    } else if (OB_FAIL(notify_data_source_(NotifyType::ON_COMMIT,
                                           timestamp,
                                           true,
                                           exec_info_.multi_data_source_))) {
    } else if (OB_FAIL(TX_REPLAY_COMMIT_FAIL_AFTER_NOTIFY_ON_COMMIT)) {
    } else if (OB_FAIL(trans_replay_commit_(ctx_tx_data_.get_commit_version(),
                                            timestamp,
                                            checksum))) {
    } else if (OB_FAIL(TX_REPLAY_COMMIT_FAIL_AFTER_COMMIT)) {
    } else if ((!ctx_tx_data_.is_read_only()) && OB_FAIL(ctx_tx_data_.insert_into_tx_table())) {
      TRANS_LOG(WARN, "insert to tx table failed", KR(ret), K(*this));
    } else {
      if (OB_FAIL(trans_clear_(timestamp))) {
      } else {
        set_exiting_();
      }
    }
    trans_service_->get_tx_version_mgr().update_max_commit_ts(
        ctx_tx_data_.get_commit_version(), false);
  }
  if (OB_SUCC(ret)) {
    runtime_state_.set_state_log_submitted();
  }

  const int64_t used_time = timeguard.get_diff();
  REC_TRANS_TRACE_EXT2(tlog_, replay_commit, OB_ID(ret), ret, OB_ID(used), used_time, OB_ID(offset),
                       offset.val_, OB_ID(t), timestamp, OB_ID(ref), get_ref());
  if (OB_FAIL(ret)) {
  } else {
#ifndef NDEBUG
    TRANS_LOG(INFO, "[Replay Tx] replay commit log", K(ret), K(used_time), K(timestamp), K(offset),
              K(commit_log), K(*this));
#endif
  }

  return ret;
}

int ObTxCtx::replay_clear(const ObTxClearLog &clear_log,
                                 const palf::LSN &offset,
                                 const SCN &timestamp,
                                 const int64_t &part_log_no)
{
  int ret = OB_SUCCESS;
  common::ObTimeGuard timeguard("replay_clear", 10 * 1000);
  CtxLockGuard guard(lock_);
  // const int64_t start = ObTimeUtility::fast_current_time();
  bool need_replay = true;
  if (OB_FAIL(check_replay_avaliable_(offset, timestamp, part_log_no, need_replay))) {
  } else if (!need_replay) {
    TRANS_LOG(INFO, "need not replay log", K(clear_log), K(timestamp), K(offset), K(*this));
    // no need to replay
    if (OB_FAIL(trans_clear_(timestamp))) {
    }
  } else if (OB_FAIL(update_replaying_log_no_(timestamp, part_log_no))) {
  } else if (OB_FAIL(trans_clear_(timestamp))) {
  } else {
    //TODO  ignore err_code when replay from a middle log without tx_ctx_table_info
    TRANS_LOG(WARN, "unexpected clear log", KPC(this), K(clear_log));
    print_trace_log_();
    // ret = OB_ERR_UNEXPECTED;
    if (OB_SUCC(ret)) {
      set_exiting_();
    }
  }
  if (OB_SUCC(ret)) {
    runtime_state_.set_state_log_submitted();
  }
  if (OB_FAIL(ret)) {
  } else {
  }
  const int64_t used_time = timeguard.get_diff();
  REC_TRANS_TRACE_EXT2(tlog_, replay_clear, OB_ID(ret), ret, OB_ID(used),
                       used_time, OB_ID(offset), offset.val_,
                       OB_ID(t), timestamp, OB_ID(ref), get_ref());
  if (OB_FAIL(ret)) {
  } else {
#ifndef NDEBUG
    TRANS_LOG(INFO, "[Replay Tx] replay clear log", K(ret), K(used_time), K(timestamp), K(offset),
              K(clear_log), K(*this));
#endif
  }
  return ret;
}

ERRSIM_POINT_DEF(TX_REPLAY_ABORT_FAIL_BEFORE_NOTIFY_TX_END)
ERRSIM_POINT_DEF(TX_REPLAY_ABORT_FAIL_AFTER_NOTIFY_TX_END)
ERRSIM_POINT_DEF(TX_REPLAY_ABORT_FAIL_AFTER_ABORT)
ERRSIM_POINT_DEF(TX_REPLAY_ABORT_FAIL_AFTER_CLEAR)
ERRSIM_POINT_DEF(TX_REPLAY_ABORT_FAIL_AFTER_NOTIFY_ON_ABORT)
int ObTxCtx::replay_abort(const ObTxAbortLog &abort_log,
                                 const palf::LSN &offset,
                                 const SCN &timestamp,
                                 const int64_t &part_log_no)
{
  int ret = OB_SUCCESS;

  common::ObTimeGuard timeguard("replay_abort", 10 * 1000);
  // const int64_t start = ObTimeUtility::fast_current_time();
  bool need_replay = false;

  CtxLockGuard guard(lock_);

  if (OB_FAIL(check_replay_avaliable_(offset, timestamp, part_log_no, need_replay))) {
  } else if (OB_FAIL(replay_update_tx_data_(false, timestamp, SCN() /*unused*/))) {
  } else if (OB_FAIL(replace_tx_data_with_backup_(abort_log.get_tx_data_backup(), timestamp))) {
  } else if (!need_replay) {
    TRANS_LOG(INFO, "need not replay log", K(abort_log), K(timestamp), K(offset), K(*this));
    // no need to replay,
  } else if (OB_FAIL(update_replaying_log_no_(timestamp, part_log_no))) {
  } else {
    if (OB_SUCC(ret)) {
      if ((!abort_log.get_multi_source_data().empty() || !exec_info_.multi_data_source_.empty())
          && replay_completeness_.is_incomplete()) {
        // ret = OB_ERR_UNEXPECTED;
        TRANS_LOG(ERROR, "mds part_ctx can not replay from the middle", K(ret), K(timestamp), K(offset),
                  K(abort_log), KPC(this));
      }
    }
    if (OB_SUCC(ret)) {
      set_durable_state_(ObTxState::ABORT);
      set_target_state(ObTxState::ABORT);
    }
  }
  if (OB_SUCC(ret)) {
    // we must notify mds tx_end before invoking trans_replay_abort_ for clearing tablet lock
    if (OB_FAIL(mds_cache_.generate_final_notify_array(
            exec_info_.multi_data_source_, true /*need_merge_cache*/, true /*allow_log_overflow*/))) {
    } else if (OB_FAIL(TX_REPLAY_ABORT_FAIL_BEFORE_NOTIFY_TX_END)) {
    } else if (OB_FAIL(notify_data_source_(NotifyType::TX_END, timestamp, true,
                                           exec_info_.multi_data_source_,  false/*willing_to_commit*/))) {
    } else if (OB_FAIL(TX_REPLAY_ABORT_FAIL_AFTER_NOTIFY_TX_END)) {
    } else if (OB_FAIL(trans_replay_abort_(timestamp))) {
    } else if (OB_FAIL(TX_REPLAY_ABORT_FAIL_AFTER_ABORT)) {
    } else if (OB_FAIL(trans_clear_(timestamp))) {
    } else if (OB_FAIL(TX_REPLAY_ABORT_FAIL_AFTER_CLEAR)) {
    } else if (OB_FAIL(notify_data_source_(NotifyType::ON_ABORT, timestamp, true,
                                           mds_cache_.get_final_notify_array(),
                                           false/*willing_to_commit*/))) {
    } else if (OB_FAIL(TX_REPLAY_ABORT_FAIL_AFTER_NOTIFY_ON_ABORT)) {
    } else if (!ctx_tx_data_.is_read_only() && OB_FAIL(ctx_tx_data_.add_abort_op(timestamp))) {
      TRANS_LOG(WARN, "add tx data abort_op failed", K(ret), KPC(this));
    } else if ((!ctx_tx_data_.is_read_only()) && OB_FAIL(ctx_tx_data_.insert_into_tx_table())) {
      TRANS_LOG(WARN, "insert to tx table failed", KR(ret), K(*this));
    } else {
      reset_redo_lsns_();
      set_exiting_();
    }
  }
  if (OB_SUCC(ret)) {
    runtime_state_.set_state_log_submitted();
  }
  const int64_t used_time = timeguard.get_diff();
  REC_TRANS_TRACE_EXT2(tlog_, replay_abort, OB_ID(ret), ret, OB_ID(used),
                       used_time, OB_ID(offset), offset.val_,
                       OB_ID(t), timestamp, OB_ID(ref), get_ref());

  if (OB_FAIL(ret)) {
  } else {
#ifndef NDEBUG
    TRANS_LOG(INFO, "[Replay Tx] replay abort log", K(ret), K(used_time), K(timestamp), K(offset),
              K(abort_log), K(*this));
#endif
  }

  return ret;
}

int ObTxCtx::replay_multi_data_source(const ObTxMultiDataSourceLog &log,
                                             const palf::LSN &lsn,
                                             const SCN &timestamp,
                                             const int64_t &part_log_no)
{
  int ret = OB_SUCCESS;
  common::ObTimeGuard timeguard("replay_multi_data_source", 10 * 1000);
  bool need_replay = false;
  CtxLockGuard guard(lock_);

  const int64_t start = ObTimeUtility::current_time();

  bool repeat_replay = (timestamp == exec_info_.max_applied_log_ts_);

  ObTxBufferNodeArray increamental_array;
  int64_t additional_index = exec_info_.multi_data_source_.count();
  if (OB_FAIL(check_replay_avaliable_(lsn, timestamp, part_log_no, need_replay))) {
  } else if (!need_replay || repeat_replay) {
    TRANS_LOG(INFO, "need not replay log", K(need_replay), K(repeat_replay), K(log), K(timestamp), K(lsn), K(*this));
    // no need to replay
  } else if (OB_FAIL(update_replaying_log_no_(timestamp, part_log_no))) {
  }

  if (OB_SUCC(ret)) {
    if (replay_completeness_.is_incomplete()) {
      // ret = OB_ERR_UNEXPECTED;
      TRANS_LOG(ERROR, "mds part_ctx can not replay from the middle", K(ret), K(timestamp), K(lsn),
                K(log), KPC(this));
    }
  }

  share::SCN notify_redo_scn =
      OB_NOT_NULL(big_segment_info_.submit_log_cb_template_)
              && big_segment_info_.submit_log_cb_template_->get_first_part_scn().is_valid()
          ? big_segment_info_.submit_log_cb_template_->get_first_part_scn()
          : timestamp;

  if (OB_FAIL(ret)) {
  // do nothing
  //TODO & ATTENTION: deep copy a part of the mds array in the log twice after recovered from the tx_ctx_table
  } else if (OB_FAIL(deep_copy_mds_array_(log.get_data(), increamental_array))) {
  } else if (OB_FAIL(notify_data_source_(NotifyType::REGISTER_SUCC,
                                         timestamp,
                                         true,
                                         increamental_array))) {
  } else if (OB_FAIL(notify_data_source_(NotifyType::ON_REDO,
                                         timestamp,
                                         true,
                                         increamental_array))) {
  } else if (OB_FAIL(replay_mds_to_tx_table_(increamental_array, timestamp))) {
  }

  // rollback mds log replay
  if (OB_FAIL(ret)) {
    int tmp_ret = OB_SUCCESS;
    for (int64_t i = additional_index;
         i < exec_info_.multi_data_source_.count() && OB_SUCCESS == tmp_ret; i++) {

      ObTxBufferNode &node = exec_info_.multi_data_source_.at(i);
      if (nullptr != node.data_.ptr()) {
        mds_cache_.free_mds_node(node.data_, node.get_register_no());
        node.get_buffer_ctx_node().destroy_ctx();
      }
    }
    for (int64_t i = exec_info_.multi_data_source_.count() - 1;
         i >= additional_index && OB_SUCCESS == tmp_ret; i--) {
      if (OB_TMP_FAIL(exec_info_.multi_data_source_.remove(i))) {
      }
    }
  }
  REC_TRANS_TRACE_EXT2(tlog_, replay_multi_data_source, OB_ID(ret), ret, OB_ID(used),
                       timeguard.get_diff(), OB_ID(offset), lsn.val_, OB_ID(t), timestamp,
                       OB_ID(ref), get_ref());

  if (OB_FAIL(ret)) {
  } else {
#ifndef NDEBUG
    TRANS_LOG(INFO, "[Replay Tx] Replay MSD Redo in TxCtx", K(ret), K(timestamp), K(lsn),
              K(need_replay), K(log), K(*this));
#endif
  }
  return ret;
}

int ObTxCtx::replay_record(const ObTxRecordLog &log,
                                  const palf::LSN &lsn,
                                  const SCN &timestamp,
                                  const int64_t &part_log_no)
{
  int ret = OB_SUCCESS;
  common::ObTimeGuard timeguard("replay_record", 10 * 1000);
  bool need_replay = false;
  CtxLockGuard guard(lock_);

  if (OB_FAIL(check_replay_avaliable_(lsn, timestamp, part_log_no, need_replay))) {
  } else if (!need_replay) {
    TRANS_LOG(INFO, "need not replay log", K(log), K(timestamp), K(lsn), K(*this));
    // no need to replay
  } else if (OB_FAIL(update_replaying_log_no_(timestamp, part_log_no))) {
  } else {
    reset_redo_lsns_();
    set_prev_record_lsn_(lsn);
  }

  if (OB_FAIL(ret)) {
  } else {
    TRANS_LOG(INFO, "[Replay Tx] Replay Record in TxCtx", K(ret), K(timestamp), K(lsn),
              K(need_replay), K(log), K(*this));
  }

  return ret;
}

const SCN ObTxCtx::get_min_undecided_log_ts() const
{
  SCN log_ts;
  CtxLockGuard guard(lock_);
  if (!busy_cbs_.is_empty()) {
    const ObTxLogCb *log_cb = busy_cbs_.get_first();
    if (OB_ISNULL(log_cb)) {
      TRANS_LOG_RET(ERROR, OB_ERR_UNEXPECTED, "unexpected null ptr", K(*this));
    } else {
      log_ts = log_cb->get_log_ts();
    }
  }
  return log_ts;
}


bool ObTxCtx::is_committing_() const
{
  return ObTxState::INIT != exec_info_.state_ || ObPartTransAction::COMMIT == part_trans_action_
         || ObPartTransAction::ABORT == part_trans_action_;
}

inline bool ObTxCtx::need_commit_callback_() {
  return ObPartTransAction::COMMIT == part_trans_action_
    && commit_cb_.is_enabled()
    && !commit_cb_.is_inited();
}


int ObTxCtx::check_with_tx_data(ObITxDataCheckFunctor &fn)
{
  // NB: You need notice the lock is not acquired during check
  int ret = OB_SUCCESS;
  ObTxData *tx_data_ptr = NULL;
  if (OB_FAIL(ctx_tx_data_.get_tx_data_ptr(tx_data_ptr))) {
  } else {
    // const ObTxData &tx_data = *tx_data_ptr;
    // NB: we must read the state then the version without lock. If you are interested in the
    // order, then you can read the comment in ob_tx_data_functor.cpp
    ObTxState state = exec_info_.state_;
    ObTxCCCtx tx_cc_ctx(state, mt_ctx_.get_trans_version());

    if (OB_FAIL(fn(*tx_data_ptr, &tx_cc_ctx))) {
    } else {
    }
  }

  return ret;
}

int ObTxCtx::update_rec_log_ts_for_parallel_replay(const SCN &rec_scn)
{
  int ret = OB_SUCCESS;
  CtxLockGuard guard(lock_);
  // NB: If we need calculate the checksum, we cannot allow them to recycle the
  // logs before the checksum of the logs are computed. Otherwise, we may not be
  // able to calculate the checksum for the concurrent replay portion of the log
  // after a restart.
  //
  // Let's see the example:
  //
  // The log sequence of the Txn is : 1 -> 2 -> 3 -> 4
  //
  // The 1, 4 is in the queue 0(aka tx-log-queue), 2 is in the queue 2 and 3 is
  // in the queue 3 because of the parallel replay. Assuming the queue 0 has
  // replayed 4, and the queue 2 and 3 has not replayed 2, 3 yet. At the moment,
  // a checkpoint is issued, and the checksum calculation for queue 2 and queue
  // 3 will miss the portion of the log 2 and 3 during the checkpoint. And if we
  // donot update the rec_scn, After 2, 3 has replayed, the checkpoint will
  // later recycle the logs 1-4. And the restart will miss to calculate the
  // checksum of 2 and 3 forever.
  //
  // The cons of this choice is that after restart, the log recycle position
  // will be somehow older which will cause more checkpoint of the tx ctx table.
  //
  if (exec_info_.need_checksum_) {
    update_rec_log_ts_(true/*for_replay*/, rec_scn);
  }
  return ret;
}

int ObTxCtx::update_rec_log_ts_(bool for_replay, const SCN &rec_log_ts)
{
  int ret = OB_SUCCESS;

  share::SCN min_big_segment_rec_scn = get_min_unsyncd_segment_scn_();

  // The semantic of the rec_log_ts means the log ts of the first state change
  // after previous checkpoint.
  if (for_replay) {
    // follower may support parallel replay redo, so must do dec update
    if (!rec_log_ts_.is_valid()) {
      rec_log_ts_.atomic_store(rec_log_ts);
    } else if (rec_log_ts_ > rec_log_ts){
      rec_log_ts_.atomic_store(rec_log_ts);
    }
  } else {
    if (!rec_log_ts_.is_valid()) {
      // Case 2: As leader, the application is discrete and not in order, so we
      // should set it as the log ts of the first log submmitted during
      // continuous logging(we call it FCL later) because all log of the txn with
      // its log ts in front of the FCL must be contained in the checkpoint.
      //
      // NB(TODO(handora.qc)): Remember to reset it when replaying start working
      if (!busy_cbs_.is_empty()) {
        const ObTxLogCb *log_cb = busy_cbs_.get_first();
        if (OB_ISNULL(log_cb)) {
          ret = OB_ERR_UNEXPECTED;
          TRANS_LOG(ERROR, "unexpected null ptr", K(*this));
        } else {
          rec_log_ts_.atomic_store(log_cb->get_log_ts());
        }
      } else {
        // there may exits if log cbs is empty
      }
    }
  }


  if (min_big_segment_rec_scn.is_valid() && !rec_log_ts_.is_valid()) {
    rec_log_ts_.atomic_store(min_big_segment_rec_scn);
  } else if (min_big_segment_rec_scn.is_valid() && rec_log_ts_.is_valid()) {
    rec_log_ts_.atomic_store(share::SCN::min(min_big_segment_rec_scn, rec_log_ts_));
  }

  return ret;
}

// When checkpointing the tx ctx table, we need refresh the rec_log_ts for the
// next checkpoint. While we shouldnot return the refreshed rec_log_ts before
// the checkpoint of the tx ctx table is succeed. So we need remember the
// rec_log_ts using prev_rec_log_ts before successfully checkpointing
int ObTxCtx::refresh_rec_log_ts_()
{
  int ret = OB_SUCCESS;

  if (!prev_rec_log_ts_.is_valid()) {
    // We should remember the rec_log_ts before the tx ctx table is successfully
    // checkpointed
    prev_rec_log_ts_.atomic_store(rec_log_ts_);

    if (busy_cbs_.is_empty()) {
      rec_log_ts_.atomic_store(share::SCN::invalid_scn());
    } else {
      rec_log_ts_.atomic_store(busy_cbs_.get_first()->get_log_ts());
    }
  } else {
    TRANS_LOG(WARN, "we should not allow concurrent merge of tx ctx table", K(*this));
  }

  return ret;
}

int ObTxCtx::get_tx_ctx_table_info_(ObTxCtxTableInfo &info)
{
  int ret = OB_SUCCESS;
  info.data_version_ = DATA_CURRENT_VERSION;
  // leave target_scn to MAX and the callee will choose the greatest
  // calculable scn, especially when parallel replay, the max scn of
  // a parallel replayed callback-list will be carefully choosen to
  // ensure checksum calculation was continous
  share::SCN target_scn = share::SCN::max_scn();
  if (OB_FAIL(ctx_tx_data_.get_tx_data(info.tx_data_guard_))) {
  } else if (exec_info_.need_checksum_ &&
             OB_FAIL(mt_ctx_.calc_checksum_before_scn(target_scn,
                 exec_info_.checksum_, exec_info_.checksum_scn_))) {
    TRANS_LOG(ERROR, "calc checksum before log ts failed", K(ret), KPC(this));
  } else if (OB_FAIL(exec_info_.generate_mds_buffer_ctx_array())) {
  } else if (OB_FAIL(info.exec_info_.assign(exec_info_))) {
  } else {
    info.tx_id_ = trans_id_;
    if (OB_FAIL(mt_ctx_.get_table_lock_store_info(info.table_lock_info_))) {
    } else {
      TRANS_LOG(INFO, "store ctx_info: ", K(ret), K(info), KPC(this));
    }
  }
  exec_info_.mds_buffer_ctx_array_.reset();

  return ret;
}

int ObTxCtx::gen_total_mds_array_(ObTxBufferNodeArray &mds_array)
{
  int ret = OB_SUCCESS;

  if (OB_FAIL(mds_cache_.generate_final_notify_array(
          exec_info_.multi_data_source_, true /*need_merge_cache*/, true /*allow_log_overflow*/))) {
  } else if (OB_FAIL(mds_array.assign(mds_cache_.get_final_notify_array()))) {
  }
  return ret;
}

int ObTxCtx::deep_copy_mds_array_(const ObTxBufferNodeArray &mds_array,
                                         ObTxBufferNodeArray &incremental_array,
                                         bool need_replace)
{
  auto process_with_buffer_ctx = [this](const ObTxBufferNode &old_node,
                                        mds::BufferCtx *&new_ctx) -> int {
    int ret = OB_SUCCESS;
    if (old_node.get_data_source_type() <= ObTxDataSourceType::UNKNOWN
        || old_node.get_data_source_type() >= ObTxDataSourceType::MAX_TYPE) {
      ret = OB_ERR_UNDEFINED;
      TRANS_LOG(ERROR, "unexpected mds type", KR(ret), K(*this));
    } else if (uses_builtin_mds_notifier(old_node.get_data_source_type())) {
      TRANS_LOG(DEBUG, "built-in mds type has no buffer ctx",
                      K(old_node.get_data_source_type()), K(*this));
    } else {
      if (OB_ISNULL(old_node.get_buffer_ctx_node().get_ctx())) { // this is replay path, create ctx
        if (OB_FAIL(mds::MdsFactory::create_buffer_ctx(old_node.get_data_source_type(), trans_id_,
                                                       new_ctx))) {
        }
      } else { // this is recover path, copy ctx
        if (OB_FAIL(mds::MdsFactory::deep_copy_buffer_ctx(
                trans_id_, *(old_node.buffer_ctx_node_.get_ctx()), new_ctx))) {
        }
      }
    }
    return ret;
  };
  int ret = OB_SUCCESS;

  const int64_t origin_count = exec_info_.multi_data_source_.count();
  const int64_t additional_count = mds_array.count();

  ObTxBufferNodeArray tmp_buf_arr;

  // void *ptr = nullptr;
  int64_t len = 0;

  if (OB_FAIL(tmp_buf_arr.reserve(additional_count))) {
  } else if(OB_FAIL(incremental_array.reserve(additional_count))) {
  } else if (need_replace) {
    ret = exec_info_.multi_data_source_.reserve(additional_count);
  } else {
    ret = exec_info_.multi_data_source_.reserve(origin_count + additional_count);
  }

  if (OB_FAIL(ret)) {

  } else {

    for (int64_t i = 0; OB_SUCC(ret) && i < mds_array.count(); ++i) {
      const ObTxBufferNode &node = mds_array.at(i);
      len = node.data_.length();
      ObString tmp_data;
      if (OB_FAIL(mds_cache_.alloc_mds_node(this, node.data_.ptr(), len, tmp_data, node.get_register_no()))) {
      } else {
      // if (OB_ISNULL(ptr = mtl_malloc(len, ""))) {
      //   ret = OB_ALLOCATE_MEMORY_FAILED;
      //   TRANS_LOG(WARN, "allocate memory failed", KR(ret), K(*this), K(len));
      // } else {
        // MEMCPY(ptr, node.data_.ptr(), len);
        ObTxBufferNode new_node;
        // ObString data;
        // data.assign_ptr(reinterpret_cast<char *>(ptr), len);
        mds::BufferCtx *new_ctx = nullptr;
        if (OB_FAIL(process_with_buffer_ctx(node, new_ctx))) {
          mds_cache_.free_mds_node(tmp_data, node.get_register_no());
          // mtl_free(tmp_data.ptr());
          if (OB_NOT_NULL(new_ctx)) {
            ::oceanbase::share::server_service<::oceanbase::storage::mds::ObMdsService>()->get_buffer_ctx_allocator().free(new_ctx);
            new_ctx = nullptr;
          }
          TRANS_LOG(WARN, "process_with_buffer_ctx failed", KR(ret), K(*this));
        } else if (OB_FAIL(new_node.init(node.get_data_source_type(), tmp_data, node.mds_base_scn_,
                                         node.seq_no_, new_ctx))) {
          mds_cache_.free_mds_node(tmp_data, node.get_register_no());
          if (OB_NOT_NULL(new_ctx)) {
            ::oceanbase::share::server_service<::oceanbase::storage::mds::ObMdsService>()->get_buffer_ctx_allocator().free(new_ctx);
            new_ctx = nullptr;
          }
          TRANS_LOG(WARN, "init new node failed", KR(ret), K(*this));
        } else if (ObTxBufferNode::is_valid_register_no(node.get_register_no())
                   && OB_FAIL(new_node.set_mds_register_no(node.get_register_no()))) {
          mds_cache_.free_mds_node(tmp_data, node.get_register_no());
          // mtl_free(tmp_data.ptr());
          if (OB_NOT_NULL(new_ctx)) {
            ::oceanbase::share::server_service<::oceanbase::storage::mds::ObMdsService>()->get_buffer_ctx_allocator().free(new_ctx);
            new_ctx = nullptr;
          }
          TRANS_LOG(WARN, "set mds register_no failed", KR(ret), K(*this));
        } else if (OB_FAIL(tmp_buf_arr.push_back(new_node))) {
          mds_cache_.free_mds_node(tmp_data, node.get_register_no());
          if (OB_NOT_NULL(new_ctx)) {
            ::oceanbase::share::server_service<::oceanbase::storage::mds::ObMdsService>()->get_buffer_ctx_allocator().free(new_ctx);
            new_ctx = nullptr;
          }
          TRANS_LOG(WARN, "push multi source data failed", KR(ret), K(*this));
        }
      }
    }

    if (OB_FAIL(ret)) {
      for (int64_t i = 0; i < tmp_buf_arr.count(); ++i) {
        mds_cache_.free_mds_node(tmp_buf_arr[i].data_, tmp_buf_arr[i].get_register_no());
        tmp_buf_arr[i].buffer_ctx_node_.destroy_ctx();
      }
      tmp_buf_arr.reset();
    }
  }

  if (OB_FAIL(ret)) {

  } else if (need_replace) {

    for (int64_t i = 0; i < exec_info_.multi_data_source_.count(); ++i) {
      if (nullptr != exec_info_.multi_data_source_[i].data_.ptr()) {
        mds_cache_.free_mds_node(exec_info_.multi_data_source_[i].data_,
                                 exec_info_.multi_data_source_[i].get_register_no());
      }
      exec_info_.multi_data_source_[i].buffer_ctx_node_.destroy_ctx();
    }
    exec_info_.multi_data_source_.reset();
  }

  if (OB_FAIL(ret)) {

  } else {

    const int64_t tmp_buf_array_cnt = tmp_buf_arr.count();
    const int64_t ctx_mds_array_cnt = exec_info_.multi_data_source_.count();
    int64_t max_register_no_in_ctx = 0;
    if (exec_info_.multi_data_source_.count() > 0) {
      max_register_no_in_ctx =
          exec_info_.multi_data_source_[ctx_mds_array_cnt - 1].get_register_no();
    }
    int64_t ctx_array_start_index = 0;

    for (int64_t i = 0; OB_SUCC(ret) && i < tmp_buf_array_cnt; ++i) {
      if (is_for_replay()) {
        tmp_buf_arr[i].set_submitted();
        tmp_buf_arr[i].set_synced();
      }
      if (ObTxBufferNode::is_valid_register_no(max_register_no_in_ctx)
          && ObTxBufferNode::is_valid_register_no(tmp_buf_arr[i].get_register_no())
          && tmp_buf_arr[i].get_register_no() <= max_register_no_in_ctx) {
        while ((!ObTxBufferNode::is_valid_register_no(
                    exec_info_.multi_data_source_[ctx_array_start_index].get_register_no())
                || tmp_buf_arr[i].get_register_no()
                       > exec_info_.multi_data_source_[ctx_array_start_index].get_register_no())
               && ctx_array_start_index < ctx_mds_array_cnt) {
          ctx_array_start_index++;
        }
        if (tmp_buf_arr[i].get_register_no()
            == exec_info_.multi_data_source_[ctx_array_start_index].get_register_no()) {
          mds_cache_.free_mds_node(tmp_buf_arr[i].data_, tmp_buf_arr[i].get_register_no());
          // mtl_free(tmp_buf_arr[i].data_.ptr());
          tmp_buf_arr[i].buffer_ctx_node_.destroy_ctx();
          if (OB_FAIL(incremental_array.push_back(
                  exec_info_.multi_data_source_[ctx_array_start_index]))) {
          }
          TRANS_LOG(INFO, "filter mds node replay by the register_no", K(ret), K(trans_id_),
                    K(i), K(ctx_array_start_index), K(tmp_buf_arr[i].get_register_no()),
                    K(exec_info_.multi_data_source_[ctx_array_start_index]));
        } else {
          ret = OB_ERR_UNEXPECTED;
          TRANS_LOG(ERROR, "we can not find a mds node in ctx with the same register_no", K(ret),
                    K(i), K(ctx_array_start_index), K(tmp_buf_arr[i].get_register_no()),
                    K(exec_info_.multi_data_source_[ctx_array_start_index]), KPC(this));
        }
      } else {
        if (OB_FAIL(exec_info_.multi_data_source_.push_back(tmp_buf_arr[i]))) {
        } else if (OB_FAIL(incremental_array.push_back(tmp_buf_arr[i]))) {
        }
      }
    }
  }

  return ret;
}

int ObTxCtx::prepare_mds_tx_op_(const ObTxBufferNodeArray &mds_array,
                                       SCN op_scn,
                                       ObTxDataOpAllocator &tx_op_allocator,
                                       ObTxOpArray &tx_op_array,
                                       bool is_replay)
{
  int ret = OB_SUCCESS;

  for (int64_t i = 0; OB_SUCC(ret) && i < mds_array.count(); i++) {
    const ObTxBufferNode &node = mds_array.at(i);
    ObTxBufferNodeWrapper *new_node_wrapper = nullptr;
    ObTxOp tx_op;
    tx_op_allocator.reset_local_alloc_size();
    if (OB_ISNULL(new_node_wrapper = (ObTxBufferNodeWrapper*)(tx_op_allocator.alloc(sizeof(ObTxBufferNodeWrapper))))) {
      ret = OB_ALLOCATE_MEMORY_FAILED;
      TRANS_LOG(WARN, "allocate memory failed", KR(ret));
    } else if (FALSE_IT(new(new_node_wrapper) ObTxBufferNodeWrapper())) {
    } else if (!is_replay && OB_FAIL(new_node_wrapper->pre_alloc(trans_id_, node, tx_op_allocator))) {
      TRANS_LOG(WARN, "pre_alloc failed", KR(ret), KPC(this));
    } else if (is_replay && OB_FAIL(new_node_wrapper->assign(trans_id_, node, tx_op_allocator, false))) {
      TRANS_LOG(WARN, "assign failed", KR(ret), KPC(this));
    } else if (OB_FAIL(tx_op.init(ObTxOpCode::MDS_OP, op_scn, new_node_wrapper, tx_op_allocator.get_local_alloc_size()))) {
    } else if (OB_FAIL(tx_op_array.push_back(tx_op))) {
    }
    // attention tx_op is not put into tx_op_array
    if (OB_FAIL(ret) && OB_NOT_NULL(new_node_wrapper)) {
      new_node_wrapper->~ObTxBufferNodeWrapper();
      tx_op_allocator.free(new_node_wrapper);
    }
  }
  TRANS_LOG(INFO, "prepare_mds_tx_op", K(ret), K(trans_id_), K(mds_array), K(tx_op_array), K(op_scn));
  return ret;
}

int ObTxCtx::decide_state_log_barrier_type_(
    const ObTxLogType &state_log_type,
    logservice::ObReplayBarrierType &final_barrier_type)
{
  int ret = OB_SUCCESS;

  final_barrier_type = logservice::ObReplayBarrierType::NO_NEED_BARRIER;

  logservice::ObReplayBarrierType mds_cache_final_log_barrier_type =
      logservice::ObReplayBarrierType::NO_NEED_BARRIER;
  logservice::ObReplayBarrierType tmp_state_log_barrier_type =
      logservice::ObReplayBarrierType::NO_NEED_BARRIER;

  if (OB_SUCC(ret)) {
    if (OB_FAIL(mds_cache_.decide_cache_state_log_mds_barrier_type(
            state_log_type, mds_cache_final_log_barrier_type))) {
    } else {
      final_barrier_type = mds_cache_final_log_barrier_type;
    }
  }

  if (OB_SUCC(ret)) {
    for (int i = 0; i < exec_info_.multi_data_source_.count() && OB_SUCC(ret); i++) {

      tmp_state_log_barrier_type = ObTxLogTypeChecker::need_replay_barrier(
          state_log_type, exec_info_.multi_data_source_[i].get_data_source_type());
      if (OB_FAIL(ObTxLogTypeChecker::decide_final_barrier_type(tmp_state_log_barrier_type,
                                                                final_barrier_type))) {
      }
    }
  }

  // decide barrier for parallel logging
  if (OB_SUCC(ret)) {
    if (((state_log_type == ObTxLogType::TX_COMMIT_INFO_LOG)
         || (state_log_type == ObTxLogType::TX_ABORT_LOG))
      && OB_UNLIKELY(is_parallel_logging())) {
      using LogBarrierType = logservice::ObReplayBarrierType;
      switch(final_barrier_type) {
      case LogBarrierType::STRICT_BARRIER:
      case LogBarrierType::PRE_BARRIER:
        break;
      case LogBarrierType::NO_NEED_BARRIER:
        final_barrier_type = LogBarrierType::PRE_BARRIER;
        break;
      default:
        ret = OB_ERR_UNEXPECTED;
        TRANS_LOG(ERROR, "unexpected barrier type", K(final_barrier_type));
      }
    }
  }

  if (OB_SUCC(ret) && final_barrier_type != logservice::ObReplayBarrierType::NO_NEED_BARRIER
      && final_barrier_type != logservice::ObReplayBarrierType::INVALID_BARRIER) {
    TRANS_LOG(INFO, "decide a valid barrier type for state_log", K(ret),
              K(mds_cache_final_log_barrier_type), K(final_barrier_type), K(state_log_type),
              KPC(this));
  }

  return ret;
}

bool ObTxCtx::is_contain_mds_type_(const ObTxDataSourceType target_type)
{
  bool is_contain = false;

  for (int64_t i = 0; i < exec_info_.multi_data_source_.count(); i++) {
    if (exec_info_.multi_data_source_[i].get_data_source_type() == target_type) {
      is_contain = true;
    }
  }

  if (!is_contain) {
    is_contain = mds_cache_.is_contain(target_type);
  }

  return is_contain;
}

int ObTxCtx::submit_multi_data_source_()
{
  int ret = OB_SUCCESS;
  ObTxLogBlock log_block;
  if (OB_FAIL(init_log_block_(log_block))) {
  } else {
    ret = submit_multi_data_source_(log_block);
  }
  return ret;
}

int ObTxCtx::submit_multi_data_source_(ObTxLogBlock &log_block)
{
  int ret = OB_SUCCESS;

  logservice::ObReplayBarrierType barrier_type = logservice::ObReplayBarrierType::NO_NEED_BARRIER;
  share::SCN mds_base_scn;
  const int64_t replay_hint = trans_id_.get_id();
  ObTxLogCb *log_cb = nullptr;
  void *tmp_buf = nullptr;
  if (is_force_abort_logging_()
      || get_downstream_state() == ObTxState::ABORT) {
    ret = OB_TRANS_KILLED;
    TRANS_LOG(WARN, "tx has been aborting, can not submit multi data source log", K(ret));
  } else if (runtime_state_.is_info_log_submitted()) {
    // state log already submitted, do nothing
  } else if (mds_cache_.count() > 0) {
    ObTxMultiDataSourceLog log;
    ObTxMDSRange range;
    while (OB_SUCC(ret)) {
      log.reset();
      mds_base_scn.reset();
      barrier_type = logservice::ObReplayBarrierType::NO_NEED_BARRIER;
      if (OB_FAIL(exec_info_.redo_lsns_.reserve(exec_info_.redo_lsns_.count() + 1))) {
      } else if (OB_FAIL(prepare_log_cb_(!NEED_FINAL_CB, log_cb))) {
        if (OB_UNLIKELY(OB_TX_NOLOGCB != ret)) {
          TRANS_LOG(WARN, "get log cb failed", KR(ret), K(*this));
        }
      } else {
        ret = mds_cache_.fill_mds_log(this, log, log_cb->get_mds_range(), barrier_type, mds_base_scn);
      }

      // TRANS_LOG(INFO, "after fill mds log", K(ret), K(trans_id_));
      // OB_EAGAIN will be overwritten
      if (OB_EMPTY_RANGE == ret) {
        // do nothing
      } else if (OB_SUCCESS != ret && OB_EAGAIN != ret) {
        TRANS_LOG(WARN, "fill MDS log failed", K(ret));
      } else if (OB_FAIL(exec_info_.multi_data_source_.reserve(
                     exec_info_.multi_data_source_.count() + mds_cache_.count()))) {
      } else if (OB_FAIL(log_block.add_new_log(log, &big_segment_info_.segment_buf_))) {
        // do not handle ret code OB_BUF_NOT_ENOUGH, one log entry should be
        // enough to hold multi source data, if not, take it as an error.
        TRANS_LOG(WARN, "add new log failed", KR(ret), K(*this));

        if (OB_LOG_TOO_LARGE == ret) {
          share::SCN base_scn;
          base_scn.set_min();
          if (OB_FAIL(prepare_big_segment_submit_(log_cb, base_scn, barrier_type,
                                                  ObTxLogType::TX_MULTI_DATA_SOURCE_LOG))) {
          } else {
            ret = OB_LOG_TOO_LARGE;
            TRANS_LOG(INFO, "construct big multi data source",K(ret),K(trans_id_),K(log));
          }
        }
      } else if (log_block.get_cb_arg_array().count() == 0) {
        ret = OB_ERR_UNEXPECTED;
        TRANS_LOG(ERROR, "cb arg array is empty", K(ret), K(log_block));
      // when mds_op concurrent submit log, we must promise tx_op pre_alloc safe
      // for example: submit_mds1(tx_op_count=10)--->submit_mds2(tx_op_count=50)-->mds1_log_cb_apply()-->mds2_log_cb_apply()
      // we need pre_alloc tx_op_count=60 for log_cb apply not to alloc memory
      // reserve tx_op count equals unsubmit log mds_op (mds_cache)
      // this depend insert tx op and move from mds_cache process when mds redo log callback
      } else if (OB_FAIL(ctx_tx_data_.reserve_tx_op_space(mds_cache_.count()))) {
      } else if (OB_FAIL(ls_tx_ctx_mgr_->get_tx_table()->alloc_tx_data(log_cb->get_tx_data_guard(), true, INT64_MAX))) {
      } else if (OB_ISNULL(tmp_buf = server_malloc(sizeof(ObTxOpArray), "ObTxOpArray"))) {
        ret = OB_ALLOCATE_MEMORY_FAILED;
        TRANS_LOG(WARN, "alloc memory failed", KR(ret), KPC(this));
      } else if (FALSE_IT(new (tmp_buf) ObTxOpArray())) {
      } else if (FALSE_IT(log_cb->get_tx_op_array() = (ObTxOpArray*)tmp_buf)) {
      } else if (OB_FAIL(prepare_mds_tx_op_(log_cb->get_mds_range().get_range_array(),
                                            SCN::min_scn(),
                                            *log_cb->get_tx_data_guard().tx_data()->op_allocator_,
                                            *log_cb->get_tx_op_array(),
                                            false))) {
      } else if (OB_FAIL(acquire_ctx_ref_())) {
        TRANS_LOG(ERROR, "acquire ctx ref failed", KR(ret), K(*this));
        log_cb = nullptr;

      } else if ((mds_base_scn.is_valid() ? OB_FALSE_IT(mds_base_scn = share::SCN::scn_inc(mds_base_scn)) : OB_FALSE_IT(mds_base_scn.set_min()))) {
      } else if (OB_FAIL(submit_log_block_out_(log_block, mds_base_scn, log_cb, replay_hint, barrier_type))) {
        TRANS_LOG(ERROR, "submit log to clog adapter failed", KR(ret), K(*this));
        release_ctx_ref_();
      } else if (OB_FAIL(after_submit_log_(log_block, log_cb, NULL))) {
        log_cb = nullptr;
      } else {
        if (barrier_type != logservice::ObReplayBarrierType::NO_NEED_BARRIER || !mds_base_scn.is_min()) {
          TRANS_LOG(INFO, "submit MDS redo with barrier or base_scn successfully", K(ret), K(trans_id_),
                    KPC(log_cb), K(mds_cache_), K(exec_info_.multi_data_source_),
                    K(mds_base_scn), K(barrier_type));
        }
        log_cb = nullptr;
      }
      if (OB_NOT_NULL(log_cb) && OB_FAIL(ret)) {
        return_log_cb_(log_cb);
        log_cb = nullptr;
      }
    }
    if (OB_EMPTY_RANGE == ret) {
      ret = OB_SUCCESS;
    }
  }


  return ret;
}

int ObTxCtx::prepare_mul_data_source_tx_end_(bool is_commit)
{
  int ret = OB_SUCCESS;
  int tmp_ret = OB_SUCCESS;

  if (OB_SUCC(ret)) {

    if (is_commit && mds_cache_.count() > 0
        && OB_FAIL(submit_log_impl_(ObTxLogType::TX_MULTI_DATA_SOURCE_LOG))) {
      TRANS_LOG(WARN, "submit multi data souce log failed", K(ret));

      if (OB_TMP_FAIL(restart_commit_retry_timer_())) {
      }
    } else if (OB_FAIL(mds_cache_.generate_final_notify_array(exec_info_.multi_data_source_,
                                                               true /*need_merge_cache*/,
                                                               true /*allow_log_overflo*/))) {
    } else if (OB_FAIL(notify_data_source_(NotifyType::TX_END, SCN(), false,
                                           mds_cache_.get_final_notify_array(),
                                           is_commit/*willing_to_commit*/))) {
    }
  }
  return ret;
}

#ifdef ERRSIM
ERRSIM_POINT_DEF(EN_NOTIFY_MDS)
#endif

OB_NOINLINE OB_WEAK_SYMBOL int ObTxCtx::errsim_notify_mds_()
{
  int ret = OB_SUCCESS;

#ifdef ERRSIM
  ret = EN_NOTIFY_MDS;
#endif

  if (OB_FAIL(ret)) {
  }

  return ret;
}

int ObTxCtx::notify_table_lock_(const SCN &log_ts,
                                       const bool for_replay,
                                       const ObTxBufferNodeArray &notify_array,
                                       const bool is_force_kill)
{
  int ret = OB_SUCCESS;
  if (is_exiting_ && runtime_state_.is_force_abort()) {
    // do nothing
  } else {
    ObMulSourceDataNotifyArg arg;
    arg.tx_id_ = trans_id_;
    arg.scn_ = log_ts;
    arg.trans_version_ = ctx_tx_data_.get_commit_version();
    arg.for_replay_ = for_replay;
    // table lock only need tx end
    arg.notify_type_ = NotifyType::TX_END;
    arg.is_force_kill_ = is_force_kill;

    int64_t total_time = 0;

    if (OB_FAIL(ObMulSourceTxDataNotifier::notify_table_lock(notify_array,
                                                             arg,
                                                             this,
                                                             total_time))) {
    }
    if (notify_array.count() > 0) {
      TRANS_LOG(INFO, "notify MDS table lock", K(ret), K(trans_id_), K(log_ts), K(notify_array.count()), K(notify_array), K(total_time));
    }
  }
  return ret;
}

int ObTxCtx::notify_data_source_(const NotifyType notify_type,
                                        const SCN &log_ts,
                                        const bool for_replay,
                                        const ObTxBufferNodeArray &notify_array,
                                        const bool willing_to_commit,
                                        const bool is_force_kill)
{
  int ret = OB_SUCCESS;

  if (OB_FAIL(errsim_notify_mds_())) {
  }

  if (OB_FAIL(ret)) {
    // do nothing
  } else if (is_exiting_ && runtime_state_.is_force_abort()) {
    // do nothing
  } else {
    ObMulSourceDataNotifyArg arg;
    arg.tx_id_ = trans_id_;
    arg.scn_ = log_ts;
    arg.trans_version_ = (notify_type == NotifyType::ON_PREPARE ? log_ts : ctx_tx_data_.get_commit_version());// standby read needed prepare version
    arg.for_replay_ = for_replay;
    arg.notify_type_ = notify_type;
    arg.willing_to_commit_ = willing_to_commit;
    arg.is_force_kill_ = is_force_kill;
    arg.is_incomplete_replay_ = replay_completeness_.is_incomplete();

    int64_t total_time = 0;

    if (OB_FAIL(
            SMART_CALL(ObMulSourceTxDataNotifier::notify(notify_array, notify_type, arg, this, total_time)))) {
    }
    if (notify_array.count() > 0) {
      TRANS_LOG(INFO, "notify MDS", K(ret), K(trans_id_), "notify_type", ObMultiDataSourcePrinter::to_str_notify_type(notify_type),
                K(log_ts), K(notify_array.count()), K(notify_array),
                K(total_time));
    }
  }
  return ret;
}

ERRSIM_POINT_DEF(TX_FORCE_WRITE_CLOG)
int ObTxCtx::register_multi_data_source(const ObTxDataSourceType data_source_type,
                                               const char *buf,
                                               const int64_t len,
                                               const bool try_lock,
                                               const ObTxSEQ seq_no,
                                               const ObRegisterMdsFlag &register_flag)
{
  int ret = OB_SUCCESS;
  int tmp_ret = OB_SUCCESS;
  ObTxBufferNode node;
  ObString data;
  // void *ptr = nullptr;
  ObTxBufferNodeArray tmp_array;
  ObTxPrintTimeGuard tx_print_guard;

  tx_print_guard.click_start("ctx_lock_time", 0);
  bool need_lock = true;

  if (try_lock) {
    // avoid deadlock, but give a timeout ts to avoid lock conflict short time.
    ret = lock_.lock(100000 /* 100ms */);
    // lock timeout need retry again outside.
    ret = OB_TIMEOUT == ret ? OB_EAGAIN : ret;
    need_lock = false;
  } else {
    // do nothing
  }

  if (OB_SUCC(ret)) {
    CtxLockGuard guard(lock_, need_lock);

    tx_print_guard.click_end(0);
    if (OB_UNLIKELY(nullptr == buf || len <= 0 || data_source_type <= ObTxDataSourceType::UNKNOWN
                    || data_source_type >= ObTxDataSourceType::MAX_TYPE)) {
      ret = OB_INVALID_ARGUMENT;
      TRANS_LOG(WARN, "invalid argument", KR(ret), K(data_source_type), KP(buf), K(len));
    } else if (OB_UNLIKELY(is_committing_())) {
      ret = OB_TRANS_HAS_DECIDED;
      if (is_trans_expired_() && ObPartTransAction::ABORT == part_trans_action_) {
        // rewrite error with TRANS_TIMEOUT to easy user
        ret = OB_TRANS_TIMEOUT;
      } else if (ObPartTransAction::ABORT == part_trans_action_
                 || exec_info_.state_ == ObTxState::ABORT) {
        ret = OB_TRANS_KILLED;
      }
      TRANS_LOG(WARN, "tx has decided", K(ret), KPC(this));
    } else if (OB_UNLIKELY(runtime_state_.is_force_abort())) {
      ret = OB_TRANS_KILLED;
      TRANS_LOG(WARN, "tx force aborted due to data incomplete", K(ret), KPC(this));
    } else if (is_for_replay()) {
      ret = OB_STATE_NOT_MATCH;
      TRANS_LOG(ERROR, "can not register mds on a replay context", K(ret), K(data_source_type), K(len),
                KPC(this));
    } else if (is_committing_()) {
      ret = OB_TRANS_HAS_DECIDED;
      TRANS_LOG(ERROR, "can not register mds in committing part_ctx", K(ret), KPC(this));
    } else if (OB_FAIL(mds_cache_.try_recover_max_register_no(exec_info_.multi_data_source_))) {
    } else if (OB_FALSE_IT(tx_print_guard.click_start("register_mds", 1))) {
      // do nothing
    } else if (OB_FAIL(mds_cache_.alloc_mds_node(this, buf, len, data))) {
    } else {
      mds::BufferCtx *buffer_ctx = nullptr;
      if (!uses_builtin_mds_notifier(data_source_type)) {
        ret = mds::MdsFactory::create_buffer_ctx(data_source_type, trans_id_, buffer_ctx);
      }
      if (OB_FAIL(ret)) {
      } else if (OB_FAIL(node.init(data_source_type, data, register_flag.mds_base_scn_, seq_no, buffer_ctx))) {
      } else if (OB_FAIL(tmp_array.push_back(node))) {
      } else if (tmp_array.get_serialize_size() > ObTxMultiDataSourceLog::MAX_MDS_LOG_SIZE) {
        ret = OB_LOG_TOO_LARGE;
        TRANS_LOG(WARN, "too large mds buf node", K(ret), K(tmp_array.get_serialize_size()));
      } else if (OB_FAIL(mds_cache_.insert_mds_node(node))) {
      } else if (OB_FALSE_IT(tx_print_guard.click_end(1))) {
        // do nothing
      }

      if (OB_FAIL(ret)) {
        mds_cache_.free_mds_node(data, node.get_register_no());
        if (OB_NOT_NULL(buffer_ctx)) {
          ::oceanbase::share::server_service<::oceanbase::storage::mds::ObMdsService>()->get_buffer_ctx_allocator().free(buffer_ctx);
        }
      } else if (OB_FAIL(notify_data_source_(NotifyType::REGISTER_SUCC, SCN(), false, tmp_array))) {
        if (OB_SUCCESS != (tmp_ret = mds_cache_.rollback_last_mds_node())) {
          ret = OB_ERR_UNEXPECTED;
          TRANS_LOG(ERROR, "rollback last mds node failed", K(tmp_ret), K(ret));
        }

        TRANS_LOG(WARN, "notify data source for register_succ failed", K(tmp_ret));
      } else if (OB_FALSE_IT(tx_print_guard.click_start("submit_mds", 2))) {
        // do nothing
      } else if (mds_cache_.get_unsubmitted_size() < ObTxMultiDataSourceLog::MAX_PENDING_BUF_SIZE
                 && !register_flag.need_flush_redo_instantly_
                 && (OB_SUCCESS == TX_FORCE_WRITE_CLOG)) {
        // do nothing
      } else if (OB_SUCCESS
                 != (tmp_ret = submit_log_impl_(ObTxLogType::TX_MULTI_DATA_SOURCE_LOG))) {
        if (tmp_ret == OB_TX_NOLOGCB || tmp_ret == OB_EAGAIN) {
          ret = OB_SUCCESS;
          if (register_flag.need_flush_redo_instantly_) {
            mds_cache_.set_need_retry_submit_mds(true);
          }
        } else {
          ret = tmp_ret;
        }
        TRANS_LOG(WARN, "submit mds log failed", K(tmp_ret), K(ret), K(register_flag),
                  K(data_source_type), KPC(this));
      } else if (OB_FALSE_IT(tx_print_guard.click_end(2))) {
        // do nothing
      } else {
      }
    }
  }

  if (OB_FAIL(ret)) {
    tx_print_guard.get_diff();
    TRANS_LOG(WARN, "register MDS redo in part_ctx failed", K(ret), K(trans_id_), K(data_source_type), K(len), K(register_flag), K(mds_cache_), K(*this),
              K(tx_print_guard), K(lbt()));
  } else if (tx_print_guard.get_diff() > 1 * 1000 * 1000) {
    TRANS_LOG(INFO, "register MDS redo in ctx", K(ret), K(trans_id_), K(data_source_type),
              K(len), K(register_flag), K(tx_print_guard));
  }

  REC_TRANS_TRACE_EXT2(tlog_, register_multi_data_source, OB_ID(ret), ret, OB_ID(type),
                       data_source_type);

  return ret;
}

int ObTxCtx::submit_pending_log_block_(ObTxLogBlock &log_block,
                                              memtable::ObRedoLogSubmitHelper &helper,
                                              const logservice::ObReplayBarrierType &barrier)
{
  int ret = OB_SUCCESS;

  if (log_block.get_cb_arg_array().empty()) {
    TRANS_LOG(INFO, "no need to submit pending log block because of empty", K(ret), K(trans_id_),
              K(log_block));
  } else {
    bool need_final_cb = false;
    if (is_contain(log_block.get_cb_arg_array(), ObTxLogType::TX_COMMIT_LOG)
        || is_contain(log_block.get_cb_arg_array(), ObTxLogType::TX_ABORT_LOG)
        || is_contain(log_block.get_cb_arg_array(), ObTxLogType::TX_CLEAR_LOG)) {
      need_final_cb = true;
    }
    const int64_t replay_hint = trans_id_.get_id();
    ObTxLogCb *log_cb = NULL;
    if (OB_FAIL(prepare_log_cb_(need_final_cb, log_cb))) {
      if (OB_UNLIKELY(OB_TX_NOLOGCB != ret)) {
        TRANS_LOG(WARN, "get log cb failed", KR(ret), K(*this));
      }
    } else if (log_block.get_cb_arg_array().count() == 0) {
      ret = OB_ERR_UNEXPECTED;
      TRANS_LOG(ERROR, "cb arg array is empty", K(ret), K(log_block));
      return_log_cb_(log_cb);
      log_cb = NULL;
    } else if (OB_FAIL(log_cb->reserve_callbacks(helper.callbacks_.count()))) {
      TRANS_LOG(WARN, "resolve callbacks failed", K(ret), KPC(this));
      return_log_cb_(log_cb);
      log_cb = NULL;
    } else if (OB_FAIL(acquire_ctx_ref_())) {
    } else if (OB_FAIL(submit_log_block_out_(log_block, share::SCN::min_scn(), log_cb, replay_hint, barrier))) {
      TRANS_LOG(ERROR, "submit log to clog adapter failed", KR(ret), K(*this));
      return_log_cb_(log_cb);
      log_cb = NULL;
      release_ctx_ref_();
    } else if (OB_FAIL(after_submit_log_(log_block, log_cb, &helper))) {
    } else {
      // TRANS_LOG(INFO, "submit pending log block in clog adapter success", K(*log_cb));
      log_cb = NULL;
    }
  }

  return ret;
}

int ObTxCtx::check_status()
{
  CtxLockGuard guard(lock_, CtxLockGuard::MODE::ACCESS);
  return check_status_();
}

/* check_status_ - check ctx status is health
 *
 * it is used in three situations:
 * 1) before start to read/write:
 *    checks:
 *      a. is leader
 *      b. is active(not committing/aborting)
 *      c. is NOT exiting (due to concurrent created ctx, start_trans failed after created)
 * 2) after read:
 *    checks:
 *      a. is active(not aborted)
 * 3) savepoint rollback:
 *    checks: like 1)
 * in order to reuse this routine, do `check txn is active` at first, thus even if
 * an active txn which has switched into follower also pass these check
 */
inline int ObTxCtx::check_status_()
{
  int ret = OB_SUCCESS;
  if (OB_UNLIKELY(is_committing_())) {
    ret = OB_TRANS_HAS_DECIDED;
    if (is_trans_expired_()
        && ObPartTransAction::ABORT == part_trans_action_) {
      // rewrite error with TRANS_TIMEOUT to easy user
      ret = OB_TRANS_TIMEOUT;
    } else if (ObPartTransAction::ABORT == part_trans_action_ ||
               exec_info_.state_ == ObTxState::ABORT) {
      ret = OB_TRANS_KILLED;
    }
    TRANS_LOG(WARN, "tx has decided", K(ret), KPC(this));
  } else if (OB_UNLIKELY(runtime_state_.is_force_abort())) {
    if (is_trans_expired_()) {
      ret = OB_TRANS_TIMEOUT;
      TRANS_LOG(WARN, "tx has decided", K(ret), KPC(this));
    } else {
      ret = OB_TRANS_KILLED;
      TRANS_LOG(WARN, "tx force aborted due to data incomplete", K(ret), KPC(this));
    }
  } else if (OB_UNLIKELY(is_for_replay())) {
    ret = OB_STATE_NOT_MATCH;
  } else if (OB_UNLIKELY(is_exiting_)) {
    ret = OB_TRANS_IS_EXITING;
    TRANS_LOG(WARN, "tx is exiting", K(ret), KPC(this));
  }
  if (OB_FAIL(ret)) {
  }
  return ret;
}

/*
 * start transaction protected access
 *
 * purpose:
 * 1) verify transaction ctx is *writable*
 * 2) acquire memtable ctx's ref
 * 3) alloc data_scn if not specified
 */
int ObTxCtx::start_access(const ObTxDesc &tx_desc,
                                 ObTxSEQ &data_scn,
                                 const int16_t branch)
{
  int ret = OB_SUCCESS;
  int pending_write = -1;
  const bool alloc = !data_scn.is_valid();
  int callback_list_idx = 0;

  if(OB_SUCC(ret)) {
    CtxLockGuard guard(lock_, CtxLockGuard::MODE::ACCESS);
    if (OB_FAIL(check_status_())) {
    } else if (tx_desc.op_sn_ < last_op_sn_) {
      ret = OB_TRANS_SQL_SEQUENCE_ILLEGAL;
      TRANS_LOG(WARN, "stale access operation", K(ret),
                K_(tx_desc.op_sn), K_(last_op_sn), KPC(this), K(tx_desc));
    } else {
      if (tx_desc.op_sn_ != last_op_sn_) {
        last_op_sn_ = tx_desc.op_sn_;
      }
      if (alloc) {
        // in delete_insert table, delete and insert are in the same update trans
        // need to distinguish them by seq no., each takes one seq no.
        const int64_t seq_cnt = 1;
        if (OB_FAIL(tx_desc.inc_and_get_tx_seq(branch,
                                               seq_cnt,
                                               data_scn))) {
        }
      }
      if (OB_SUCC(ret)) {
        last_scn_ = MAX(data_scn, last_scn_);
        if (!first_scn_.is_valid()) {
          first_scn_ = last_scn_;
        }
        pending_write = ATOMIC_AAF(&pending_write_, 1);
        // others must wait the first thread of parallel open the write epoch
        // hence this must be done in lock
        if (pending_write == 1) {
          callback_list_idx = mt_ctx_.acquire_callback_list(true);
        }
      }
    }
  }
  // other operations are allowed to out of lock
  if (OB_SUCC(ret)) {
    mt_ctx_.inc_ref();
    if (pending_write != 1) {
      callback_list_idx = mt_ctx_.acquire_callback_list(false);
    }
    // remember selected callback_list idx into seq_no
    if (data_scn.get_branch() == 0 && alloc && callback_list_idx != 0) {
      data_scn.set_branch(callback_list_idx);
    }
  }

  last_request_ts_ = ObClockGenerator::getClock();
  common::ObTraceIdAdaptor trace_id;
  trace_id.set(ObCurTraceId::get());
  REC_TRANS_TRACE_EXT(tlog_, start_access,
                      OB_ID(ret), ret,
                      OB_ID(trace_id), trace_id,
                      OB_ID(opid), tx_desc.op_sn_,
                      OB_ID(data_seq), data_scn.cast_to_int(),
                      OB_ID(pending), pending_write,
                      OB_ID(ref), get_ref(),
                      OB_ID(tid), get_itid() + 1);
  return ret;
}

/*
 * end_access - end of txn protected access
 *
 * release memetable context lock
 * dec pending write num
 * dec ref of memtable context
 * merge provisional write's callbacks into the total final callback list
 */
int ObTxCtx::end_access()
{
  int ret = OB_SUCCESS;
  // to reduce lock contention, these operation is out of lock
  int pending_write = ATOMIC_SAF(&pending_write_, 1);
  mt_ctx_.dec_ref();
  mt_ctx_.revert_callback_list();
  REC_TRANS_TRACE_EXT(tlog_, end_access,
                      OB_ID(opid), last_op_sn_,
                      OB_ID(pending), pending_write,
                      OB_ID(ref), get_ref(),
                      OB_ID(tid), get_itid() + 1);
  return ret;
}

int ObTxCtx::check_pending_log_overflow(const int64_t stmt_timeout)
{
  int ret = OB_SUCCESS;
  const int64_t MAX_LOCAL_RETRY_US = 1 * 1000 * 1000; // 1s
  const int64_t LOCAL_RETRY_INTERVAL_US = 50 * 1000;  // 50ms

  if (OB_SUCC(ret) && ATOMIC_LOAD(&has_extra_log_cb_group_)) {
    const int64_t trx_max_log_cb_limit =
        true ? GCONF._trx_max_log_cb_limit : 16;
    // smaller than 16  || no limit with tx_log_cb =>  disable the check of pending logs
    if (trx_max_log_cb_limit >= 16) {
      const int64_t start_wait_us = ObTimeUtility::current_time();
      int64_t cur_us = start_wait_us;
      int64_t busy_cb_cnt = 0;
      int64_t extra_cb_group_cnt = 0;
      while (get_pending_log_size()
             > 4 * ObTxLogCbGroup::MAX_LOG_CB_COUNT_IN_GROUP * 2 * 1024 * 1024) {
        {
          ObSpinLockGuard guard(log_cb_lock_);
          busy_cb_cnt = busy_cbs_.get_size();
          extra_cb_group_cnt = extra_cb_group_list_.get_size();
          if (free_cbs_.is_empty() && ls_tx_ctx_mgr_->get_log_cb_pool_mgr().is_all_busy()) {
            ret = OB_TX_PENDING_LOG_OVERFLOW;
            if (REACH_COUNT_PER_SEC(3) && REACH_TIME_INTERVAL(100 * 1000)) {
              TRANS_LOG(WARN, "too may pending log", K(ret), K(free_cbs_.get_size()),
                        K(extra_cb_group_cnt), K(busy_cb_cnt), KPC(this));
            }
          } else {
            ret = OB_SUCCESS;
          }
        }

        cur_us = ObTimeUtility::current_time();

        if (cur_us >= stmt_timeout) {
          TRANS_LOG(INFO, "retry to wait log cb until stmt timeout", K(ret), K(stmt_timeout),
                    K(busy_cb_cnt), K(extra_cb_group_cnt), K(start_wait_us), KPC(this));
          ret = OB_TIMEOUT;
        }

        if (OB_TX_PENDING_LOG_OVERFLOW!= ret) {
          break;
        } else {
          if (cur_us - start_wait_us > MAX_LOCAL_RETRY_US) {
            TRANS_LOG(INFO, "retry to wait log cb with a long time", K(ret), K(stmt_timeout),
                      K(busy_cb_cnt), K(extra_cb_group_cnt), K(start_wait_us), K(MAX_LOCAL_RETRY_US),
                      KPC(this));
            break;
          }
          usleep(LOCAL_RETRY_INTERVAL_US);
        }
      }
    }
  }

  return ret;
}

/*
 * rollback_to_savepoint - rollback to savepoint
 *
 * @op_sn       - operation sequence number, used to reject out of order msg
 * @from_scn    - the start position of rollback, inclusive
 *                generally not specified, and generated in callee
 * @to_scn      - the end position of rollback, exclusive
 * @seq_base    - the baseline of TxSEQ of current transaction
 *
 * savepoint may be created in these ways:
 * 1) created at txn scheduler, named Global-Savepoint
 * 2) created at txn participant server, named Local-Savepoint
 * 3) created at txn participant logstream, named LS-Local-Savepoint
 * In Global-Savepoint, rollback will check in-flight write and
 *    reject with OB_NEED_RETRY. this is required, because global savepoint
 *    will cross over network, given up write may concurrency with rollback.
 * In other two types of savepoint, write always under control of local thread,
 *    so such check was skipped, caller should promise writing should not
 *    concurrency with rolling back.
 *
 * There is another flaw and should token care:
 *   the last_scn are not accurate when LS-Local-Savepoint was created. it is
 *   because of data's scn use a sequence after the savepoint, which is greater
 *   than the sequence passed to start_access. and the last_scn was only set
 *   when start_access was called
 */
int ObTxCtx::rollback_to_savepoint(const int64_t op_sn,
                                          ObTxSEQ from_scn,
                                          const ObTxSEQ to_scn,
                                          const int64_t seq_base)
{
  int ret = OB_SUCCESS;
  bool need_write_log = false;
  CtxLockGuard guard(lock_);
  if (OB_FAIL(check_status_())) {
  } else if(is_logging_()) {
    ret = OB_NEED_RETRY;
    TRANS_LOG(WARN, "rollback_to need retry because of logging", K(ret), K(trans_id_), K(busy_cbs_.get_size()));
  } else if (op_sn < last_op_sn_) {
    ret = OB_TRANS_SQL_SEQUENCE_ILLEGAL;
  } else if (FALSE_IT(last_op_sn_ = op_sn)) {
  } else if ((to_scn.get_branch() == 0) && pending_write_ > 0) {
    // for branch savepoint rollback, pending_write !=0 almostly
    ret = OB_NEED_RETRY;
    TRANS_LOG(WARN, "has pending write, rollback blocked", K(ret), K(to_scn), K(pending_write_), KPC(this));
  } else if (last_scn_ <= to_scn) {
    TRANS_LOG(INFO, "rollback succeed trivially", K_(trans_id), K(op_sn), K(to_scn), K_(last_scn));
  } else if (!from_scn.is_valid() &&
             // generate from if not specified
             FALSE_IT(from_scn = to_scn.clone_with_seq(ObSequence::inc_and_get_max_seq_no(), seq_base))) {
  } else if (OB_FAIL(rollback_to_savepoint_(from_scn, to_scn, share::SCN::invalid_scn()))) {
  } else if (to_scn.get_branch() == 0) {
    last_scn_ = to_scn;
  }

  REC_TRANS_TRACE_EXT(tlog_, rollback_savepoint,
                      OB_ID(ret), ret,
                      OB_ID(from), from_scn.cast_to_int(),
                      OB_ID(to), to_scn.cast_to_int(),
                      OB_ID(pending), pending_write_,
                      OB_ID(opid), op_sn,
                      OB_ID(tid), GETTID());
#ifndef NDEBUG
  TRANS_LOG(INFO, "rollback to savepoint", K(ret),
            K(from_scn), K(to_scn), KPC(this));
#endif
  return ret;
}

int ObTxCtx::rollback_to_savepoint_(const ObTxSEQ from_scn,
                                           const ObTxSEQ to_scn,
                                           const share::SCN replay_scn)
{
  int ret = OB_SUCCESS;

  // step 1: persistent 'UNDO' (if required)
  /*
   * Follower:
   *  1. add UndoAction into tx_ctx's tx_data
   *  2. insert tx-data into tx_data_table
   * Leader:
   *  1. submit 'RollbackToLog'
   *  2. add UndoAction into tx_ctx's tx_data
   *  3. insert tx-data into tx_data_table after log sync success
   */
  bool need_update_tx_data = false;
  ObTxDataGuard tmp_tx_data_guard;
  ObTxDataGuard update_tx_data_guard;
  tmp_tx_data_guard.reset();
  update_tx_data_guard.reset();
  if (is_for_replay()) { /* Follower */
    ObUndoAction undo_action(from_scn, to_scn);
    // _NOTICE_ must load Undo(s) from TxDataTable before overwriten
    if (replay_completeness_.is_unknown() &&
        !ctx_tx_data_.has_recovered_from_tx_table() &&
        OB_FAIL(supplement_tx_op_if_exist_(true, replay_scn))) {
      TRANS_LOG(WARN, "load undos from tx table fail", K(ret), KPC(this));
    } else if (OB_FAIL(replay_undo_action_to_tx_table_(undo_action, replay_scn))) {
    }
  } else if (OB_UNLIKELY(exec_info_.max_submitted_seq_no_ > to_scn)) {
    ObTxDataGuard tx_data_guard;
    ObTxTable *tx_table = nullptr;
    ctx_tx_data_.get_tx_table(tx_table);
    ObUndoAction undo(from_scn, to_scn);
    if (OB_FAIL(ctx_tx_data_.get_tx_data(tx_data_guard))) {
    } else if (OB_FAIL(tx_data_guard.tx_data()->init_tx_op())) {
    } else if (OB_FAIL(tx_data_guard.tx_data()->add_undo_action(ls_tx_ctx_mgr_->get_tx_table(),
                                                                undo))) {
    } else if (OB_FAIL(submit_rollback_to_log_(from_scn, to_scn))) {
    }
  }

  // step 2: remove TxNode(s) from memtable

  if (OB_SUCC(ret)) {
    if (OB_FAIL(mt_ctx_.rollback(to_scn, from_scn, replay_scn))) {
    }
  }

  return ret;
}

int ObTxCtx::submit_rollback_to_log_(const ObTxSEQ from_scn,
                                            const ObTxSEQ to_scn)
{
  int ret = OB_SUCCESS;
  ObTxLogBlock log_block;
  ObTxRollbackToLog log(from_scn, to_scn);
  ObTxLogCb *log_cb = NULL;
  int64_t replay_hint = trans_id_.get_id();
  ObUndoStatusNode *undo_node = NULL;
  logservice::ObReplayBarrierType barrier = logservice::ObReplayBarrierType::NO_NEED_BARRIER;
  if (is_parallel_logging()) {
    const int16_t branch_id = to_scn.get_branch();
    if (branch_id != 0 && to_scn.get_seq() > exec_info_.serial_final_seq_no_.get_seq()) {
      replay_hint += mt_ctx_.get_tx_seq_replay_idx(to_scn);
    } else {
      // either this is a global savepoint or the savepoint is before serial final point
      // must wait the redo log after this savepoint replayed
      barrier = logservice::ObReplayBarrierType::PRE_BARRIER;
    }
  }
  if (OB_FAIL(init_log_block_(log_block))) {
  } else if (OB_FAIL(exec_info_.redo_lsns_.reserve(exec_info_.redo_lsns_.count() + 1))) {
  } else if (OB_FAIL(prepare_log_cb_(!NEED_FINAL_CB, log_cb))) {
  } else if (OB_FAIL(log_block.add_new_log(log))) {
  } else if (log_block.get_cb_arg_array().count() == 0) {
    ret = OB_ERR_UNEXPECTED;
    TRANS_LOG(ERROR, "cb arg array is empty", K(ret), K(log_block));
    return_log_cb_(log_cb);
    log_cb = NULL;
  } else if (OB_FAIL(ls_tx_ctx_mgr_->get_tx_table()->alloc_tx_data(log_cb->get_tx_data_guard(), true, INT64_MAX))) {
    TRANS_LOG(WARN, "alloc_tx_data failed", KR(ret), KPC(this));
    return_log_cb_(log_cb);
    log_cb = NULL;
  } else if (OB_ISNULL(undo_node = (ObUndoStatusNode*)::oceanbase::share::server_service<::oceanbase::share::ObSharedMemAllocMgr>()->tx_data_allocator().alloc(true, INT64_MAX))) {
    ret = OB_ALLOCATE_MEMORY_FAILED;
    TRANS_LOG(WARN, "alloc_undo_status_node failed", KR(ret), KPC(this));
    return_log_cb_(log_cb);
    log_cb = NULL;
  } else if (FALSE_IT(new (undo_node) ObUndoStatusNode())) {
  } else if (FALSE_IT(log_cb->get_undo_node() = undo_node)) {
  } else if (OB_FAIL(submit_log_block_out_(log_block, SCN::min_scn(), log_cb, replay_hint, barrier))) {
    TRANS_LOG(ERROR, "submit log fail", K(ret), K(log_block), KPC(this));
    return_log_cb_(log_cb);
  } else if (OB_FAIL(acquire_ctx_ref())) {
  } else if (OB_FAIL(after_submit_log_(log_block, log_cb, NULL))) {
  } else {
    log_cb->set_undo_action(ObUndoAction(from_scn, to_scn));
  }
  REC_TRANS_TRACE_EXT(tlog_, submit_rollback_log,
                      OB_ID(ret), ret,
                      OB_ID(from), from_scn.cast_to_int(),
                      OB_ID(to), to_scn.cast_to_int());
  TRANS_LOG(INFO, "RollbackToLog submit", K(ret), K(from_scn), K(to_scn), KP(log_cb), KPC(this));
  return ret;
}

int ObTxCtx::abort(const int reason)
{
  UNUSED(reason);
  int ret = OB_SUCCESS;
  CtxLockGuard guard(lock_);
  if (OB_UNLIKELY(is_for_replay())) {
    ret = OB_STATE_NOT_MATCH;
    TRANS_LOG(WARN, "cannot abort replay context", KR(ret), KPC(this));
  } else if (OB_UNLIKELY(is_committing_())) {
    if (part_trans_action_ == ObPartTransAction::ABORT) {
      TRANS_LOG(INFO, "tx already aborting", KPC(this));
    } else if (part_trans_action_ == ObPartTransAction::COMMIT
               || exec_info_.state_ != ObTxState::ABORT) {
      ret = OB_TRANS_PROTOCOL_ERROR;
      TRANS_LOG(ERROR, "tx already committing", KR(ret), KPC(this));
    } else {
      TRANS_LOG(INFO, "tx already aborting", KPC(this));
    }
  // } else if (OB_FAIL(prepare_mul_data_source_tx_end_(false))) {
  //   TRANS_LOG(WARN, "trans abort need retry", K(ret), K(trans_id_), K(reason));
  } else {
    if (OB_FAIL(abort_(reason))) {
    }
    last_request_ts_ = ObClockGenerator::getClock();
  }
  return ret;
}

int ObTxCtx::handle_tx_keepalive_response(const int64_t status)
{
  int ret = OB_SUCCESS;

  if (OB_SUCCESS == lock_.try_lock()) {
    CtxLockGuard guard(lock_, false);
    ret = tx_keepalive_response_(status);
  }

  return ret;
}

int ObTxCtx::tx_keepalive_response_(const int64_t status)
{
  int ret = OB_SUCCESS;

  if ((OB_TRANS_CTX_NOT_EXIST == status || OB_TRANS_ROLLBACKED == status ||
       OB_TRANS_KILLED == status || common::OB_SERVER_RUNTIME_NOT_READY == status) && can_be_recycled_()) {
    if (REACH_TIME_INTERVAL(5 * 1000 * 1000)) {
      TRANS_LOG(WARN, "[TRANS GC] tx has quit, local tx will be aborted",
                K(status), KPC(this));
    }
    if (OB_FAIL(gc_ctx_())) {
    }
  } else if (OB_TRANS_COMMITED == status && can_be_recycled_() && first_scn_ >= last_scn_ /*all changes were rollbacked*/) {
    TRANS_LOG(WARN, "txn has comitted on scheduler, but this particiapnt can be recycled", KPC(this));
    FORCE_PRINT_TRACE(tlog_, "[participant leaky] ");
  } else if (OB_SUCCESS != status) {
    if (REACH_TIME_INTERVAL(5 * 1000 * 1000)) {
      TRANS_LOG(WARN, "[TRANS GC] tx keepalive fail", K(status), KPC(this));
    }
  } else {
  }
  return ret;
}

int ObTxCtx::do_local_tx_end_(TxEndAction tx_end_action)
{
  int ret = OB_SUCCESS;

  if (tx_end_action != TxEndAction::KILL_TX_FORCEDLY
      && OB_FAIL(prepare_mul_data_source_tx_end_(TxEndAction::COMMIT_TX == tx_end_action))) {
    TRANS_LOG(WARN, "prepare tx end notify failed", K(ret), KPC(this));
  } else {

    switch (tx_end_action) {
    case TxEndAction::COMMIT_TX: {
      if (runtime_state_.is_force_abort()) {
        if (OB_FAIL(compensate_abort_log_())) {
        } else {
          ret = OB_TRANS_KILLED;
        }
      } else {
        ret = do_local_commit_tx_();
      }
      // part_trans_action_ will be set as commit in ObTxCtx::commit function
      break;
    }
    case TxEndAction::ABORT_TX: {
      ret = do_local_abort_tx_();
      if (OB_SUCC(ret) && part_trans_action_ != ObPartTransAction::COMMIT) {
        part_trans_action_ = ObPartTransAction::ABORT;
      }
      break;
    }
    case TxEndAction::KILL_TX_FORCEDLY: {
      ret = do_force_kill_tx_();
      break;
    }
    case TxEndAction::DELAY_ABORT_TX: {
      runtime_state_.set_force_abort();
      // NOTE: clean unlog callbacks is requried:
      // if mvcc-row is too large and can't be serialized, freeze thread
      // will delay_abort the txn, if don't clean unlog_callbacks
      // the memtable's freeze will be blocked
      ret = mt_ctx_.clean_unlog_callbacks();
      break;
    }
    default: {
      ret = OB_INVALID_ARGUMENT;
      TRANS_LOG(WARN, "invalid tx_end_action", K(ret), K(tx_end_action));
      break;
    }
    }
  }
  return ret;
}

int ObTxCtx::do_local_commit_tx_()
{
  int ret = OB_SUCCESS;

  if (OB_FAIL(generate_commit_version_())) {
    if (OB_EAGAIN == ret) {
      ret = OB_SUCCESS;
    } else {
      TRANS_LOG(WARN, "generate commit version failed", KR(ret), K(*this));
    }
  } else if (OB_FAIL(submit_log_impl_(ObTxLogType::TX_COMMIT_LOG))) {
    // log submitting will retry in handle_timeout
    int tmp_ret = OB_SUCCESS;
    if (OB_TMP_FAIL(restart_commit_retry_timer_())) {
      TRANS_LOG(WARN, "restart_commit_retry_timer_ error", KR(ret), KR(tmp_ret), KPC(this));
      ret = OB_EAGAIN;
    } else {
      ret = OB_SUCCESS;
    }
  }

  return ret;
}

int ObTxCtx::do_local_abort_tx_()
{
  int ret = OB_SUCCESS;

  TRANS_LOG(WARN, "do_local_abort_tx_", KR(ret), K(*this));

  if (has_persisted_log_() || is_logging_()) {
    // part_trans_action_ = ObPartTransAction::ABORT;
    if (OB_FAIL(compensate_abort_log_())) {
    }
  } else {
    // if (part_trans_action_ < ObPartTransAction::COMMIT) {
    //   part_trans_action_ = ObPartTransAction::ABORT;
    // }
    runtime_state_.set_force_abort();
    if (OB_FAIL(on_local_abort_tx_())) {
    }
  }
  return ret;
}

int ObTxCtx::do_force_kill_tx_()
{
  int ret = OB_SUCCESS;

  ObTxBufferNodeArray tmp_array;

  if (get_downstream_state() >= ObTxState::COMMIT) {
    // do nothing
  // } else if (OB_FAIL(gen_total_mds_array_(tmp_array))) {
  //   TRANS_LOG(WARN, "gen total mds array failed", KR(ret), K(*this));
  // } else if (OB_FAIL(notify_data_source_(NotifyType::ON_ABORT,
  //                                        ctx_tx_data_.get_end_log_ts() /*invalid_scn*/, false,
  //                                        tmp_array, true /*is_force_kill*/))) {
  //   TRANS_LOG(WARN, "notify data source failed", KR(ret), K(*this));
  }

  if (OB_SUCC(ret)) {
    trans_kill_();
    // Force kill cannot guarantee the consistency, so we just set end_log_ts
    // to zero
    end_log_ts_.set_min();
    (void)trans_clear_(share::SCN::invalid_scn());
    if (OB_FAIL(unregister_timeout_task_())) {
    }
    runtime_state_.set_force_abort();
    // Ignore ret
    set_exiting_();
    TRANS_LOG(INFO, "transaction killed success", "context", *this);
  }
  return ret;
}

int ObTxCtx::on_local_commit_tx_()
{
  int ret = OB_SUCCESS;
  bool need_wait = false;

  if (runtime_state_.is_gts_waiting()) {
    ret = OB_ERR_UNEXPECTED;
    TRANS_LOG(WARN, "unexpected gts waiting flag", KR(ret), KPC(this));
  } else if (!OB_UNLIKELY(ctx_tx_data_.get_commit_version().is_valid())) {
    ret = OB_ERR_UNEXPECTED;
    TRANS_LOG(WARN, "invalid commit version", K(ret), KPC(this));
  } else if (OB_FAIL(wait_gts_elapse_commit_version_(need_wait))) {
  } else if (OB_FAIL(tx_end_(true /*commit*/))) {
  } else if (FALSE_IT(elr_handler_.reset_elr_state())) {
  } else if (OB_FAIL(trans_clear_(ctx_tx_data_.get_end_log_ts()))) {
  } else if (OB_FAIL(notify_data_source_(NotifyType::ON_COMMIT, ctx_tx_data_.get_end_log_ts(),
                                         false, exec_info_.multi_data_source_))) {
  } else if (FALSE_IT(set_durable_state_(ObTxState::COMMIT))) {

  } else if (FALSE_IT(unregister_timeout_task_())) {
  } else if (need_wait) {
    REC_TRANS_TRACE_EXT2(tlog_, wait_gts_elapse, OB_ID(ref), get_ref());
  }

  if (OB_FAIL(ret) || need_wait) {
    // do nothing
  } else if (OB_FAIL(after_local_commit_succ_())) {
  }

  return ret;
}

int ObTxCtx::after_local_commit_succ_()
{
  int ret = OB_SUCCESS;

  if (OB_FAIL(update_max_commit_version_())) {
  } else {
    (void)post_tx_commit_resp_(OB_SUCCESS);
    set_exiting_();
  }

  return ret;
}

int ObTxCtx::on_local_abort_tx_()
{
  int ret = OB_SUCCESS;

  if (OB_FAIL(tx_end_(false /*commit*/))) {
  } else if (OB_FAIL(trans_clear_(ctx_tx_data_.get_end_log_ts()))) {
  } else if (OB_FAIL(mds_cache_.generate_final_notify_array(exec_info_.multi_data_source_,
                                                             true /*need_merge_cache*/,
                                                             true /*allow_log_overflow*/))) {
  } else if (OB_FAIL(notify_data_source_(NotifyType::ON_ABORT, ctx_tx_data_.get_end_log_ts(), false,
                                         mds_cache_.get_final_notify_array(),
                                         false /*willing_to_commit*/))) {
  } else if (FALSE_IT(set_durable_state_(ObTxState::ABORT))) {

  } else if (FALSE_IT(unregister_timeout_task_())) {

  } else if (ObPartTransAction::COMMIT == part_trans_action_) {
    (void)post_tx_commit_resp_(OB_TRANS_KILLED);
  }

  if (OB_SUCC(ret)) {
    set_exiting_();
  }

  return ret;
}

int ObTxCtx::dump_2_text(FILE *fd)
{
  int ret = OB_SUCCESS;

  const ObTxData *tx_data_ptr = NULL;
  const int64_t buf_len = 4096;
  char buf[buf_len];
  MEMSET(buf, 0, buf_len);

  int64_t str_len = to_string(buf, buf_len);

  fprintf(fd, "********** ObTxCtx ***********\n\n");
  fprintf(fd, "%s\n", buf);
  ObTxDataGuard tx_data_guard;
  ctx_tx_data_.get_tx_data(tx_data_guard);
  if (OB_ISNULL(tx_data_ptr = tx_data_guard.tx_data())) {
    ret = OB_ERR_UNEXPECTED;
    TRANS_LOG(WARN, "unexpected nullptr", KR(ret));
  } else {
    tx_data_ptr->dump_2_text(fd);
  }

  fprintf(fd, "\n********** ObTxCtx ***********\n");
  return ret;
}

// Check whether an old transaction has already aborted in tx data.
int ObTxCtx::check_is_aborted_in_tx_data_(const ObTransID tx_id,
                                                 bool &is_aborted)
{
  int ret = OB_SUCCESS;
  ObTxTable *tx_table = nullptr;
  ObTxTableGuard guard;
  int64_t state;
  share::SCN trans_version;
  share::SCN recycled_scn;
  ctx_tx_data_.get_tx_table(tx_table);

  if (OB_FAIL(tx_table->get_tx_table_guard(guard))) {
  } else if (!guard.is_valid()) {
    ret = OB_ERR_UNEXPECTED;
    TRANS_LOG(WARN, "tx table is null", K(ret));
  } else if (OB_FAIL(guard.try_get_tx_state(tx_id,
                                            state,
                                            trans_version,
                                            recycled_scn))) {
    if (OB_TRANS_CTX_NOT_EXIST == ret) {
      is_aborted = false;
      ret = OB_SUCCESS;
    } else {
      TRANS_LOG(WARN, "get tx state from tx data failed", K(ret), KPC(this));
    }
  } else if (ObTxData::ABORT == state) {
    is_aborted = true;
    TRANS_LOG(INFO, "check is aborted in tx data", K(tx_id), K(state), KPC(this));
  } else {
    is_aborted = false;
    TRANS_LOG(INFO, "check is not aborted in tx data", K(tx_id), K(state), KPC(this));
  }

  return ret;
}

void ObTxCtx::print_first_mvcc_callback_()
{
  mt_ctx_.print_first_mvcc_callback();
}

void ObTxCtx::report_write_ctx_status_(const int status, const bool check_tx_status)
{
  int ret = OB_SUCCESS;
  int tx_status = OB_SUCCESS;
  if (OB_ISNULL(trans_service_)) {
    ret = OB_ERR_UNEXPECTED;
    TRANS_LOG(WARN, "trans service is null", K(ret), KPC(this));
  } else if (OB_FAIL(trans_service_->report_write_ctx_status(trans_id_, status, tx_status))) {
  } else if (check_tx_status && OB_FAIL(tx_keepalive_response_(tx_status))) {
    TRANS_LOG(WARN, "handle tx status fail", K(ret), K(tx_status), KPC(this));
  }
}

void ObTxCtx::notify_tx_killed_(const int kill_reason)
{
  report_write_ctx_status_(kill_reason, false);
}

int ObTxCtx::submit_redo_log_out(ObTxLogBlock &log_block,
                                        ObTxLogCb *&log_cb,
                                        ObRedoLogSubmitHelper &helper,
                                        const int64_t replay_hint,
                                        const bool has_hold_ctx_lock,
                                        share::SCN &submitted_scn)
{
  int ret = OB_SUCCESS;
  ObTimeGuard time_guard("submit_redo_log_out_");
  CtxLockGuard ctx_lock;
  if (!has_hold_ctx_lock) {
    get_ctx_guard(ctx_lock, CtxLockGuard::MODE::CTX);
  }
  bool with_ref = false;
  bool alloc_cb = OB_ISNULL(log_cb);
  submitted_scn.reset();
  if (alloc_cb && OB_FAIL(prepare_log_cb_(!NEED_FINAL_CB, log_cb))) {
    TRANS_LOG(WARN, "get log_cb fail", K(ret), KPC(this));
  } else if (alloc_cb && OB_FAIL(log_cb->reserve_callbacks(helper.callbacks_.count()))) {
    TRANS_LOG(WARN, "log cb reserve callbacks space fail", K(ret));
  } else if (OB_FAIL(exec_info_.redo_lsns_.reserve(exec_info_.redo_lsns_.count() + 1))) {
  } else if (OB_FAIL(acquire_ctx_ref_())) {
  } else if (FALSE_IT(with_ref = true)) {
  } else if (FALSE_IT(time_guard.click("before_submit_log_block"))) {
  } else if (OB_FAIL(submit_log_block_out_(log_block, share::SCN::min_scn(), log_cb, replay_hint))) {
  } else {
    time_guard.click("submit_out_to_palf");
    submitted_scn = log_cb->get_log_ts();
    ret = after_submit_log_(log_block, log_cb, &helper);
    time_guard.click("after_submit");
    log_cb = NULL;    // moved
    with_ref = false; // moved
  }
  if (log_cb) {
    return_log_cb_(log_cb);
    log_cb = NULL;
  }
  if (with_ref) {
    release_ctx_ref_();
  }
  return ret;
}

bool ObTxCtx::is_parallel_logging() const
{
  return exec_info_.serial_final_scn_.is_valid();
}

inline bool ObTxCtx::has_replay_serial_final_() const
{
  return exec_info_.serial_final_scn_.is_valid() &&
    exec_info_.max_applied_log_ts_ >= exec_info_.serial_final_scn_;
}

int ObTxCtx::set_replay_incomplete(const share::SCN log_ts) {
  int ret = OB_SUCCESS;
  CtxLockGuard guard(lock_);
  if (OB_FAIL(set_replay_completeness_(false, log_ts))) {
  }
  return ret;
}

int ObTxCtx::set_replay_completeness_(const bool complete, const SCN replay_scn)
{
  int ret = OB_SUCCESS;
  if (OB_UNLIKELY(replay_completeness_.is_unknown())) {
    if (!complete && !ctx_tx_data_.has_recovered_from_tx_table()) {
      if (OB_FAIL(supplement_tx_op_if_exist_(true, replay_scn))) {
      } else {
        TRANS_LOG(INFO, "replay from middle, load Undo(s) from tx-table succuess",
                  K(ret), K_(trans_id));
      }
    }
    if (OB_SUCC(ret)) {
      replay_completeness_.set(complete);
      if (!complete) {
        force_no_need_replay_checksum_(false, share::SCN::invalid_scn());
        TRANS_LOG(INFO, "incomplete replay, set skip checksum", K_(trans_id));
      }
    }
  }
  return ret;
}

inline int ObTxCtx::switch_to_parallel_logging_(const share::SCN serial_final_scn,
                                                       const ObTxSEQ max_seq_no)
{
  int ret = OB_SUCCESS;
  if (OB_UNLIKELY(!max_seq_no.is_valid())) {
    ret = OB_ERR_UNEXPECTED;
    TRANS_LOG(ERROR, "max seq_no of serial final log is invalid",
              K(ret), K(serial_final_scn), K(max_seq_no), KPC(this));
    print_trace_log_();
    OB_SAFE_ABORT();
  }
  if (OB_SUCC(ret)) {
    // when start replaying serial final redo log or submitted serial final redo log
    // switch the Tx's logging mode to parallel logging
    // this include mark serial final scn point in exec_info_
    // and notify callback_mgr to remember the serial_final_scn
    // which used to for check whether the callback-list has replayed continuously
    // or all of it's serial logs has been synced continously
    // if reach these condition, the checksum calculations of callback-list can continues
    // into the parallel logged part
    exec_info_.serial_final_scn_.atomic_set(serial_final_scn);
    // remember the max of seq_no of redos currently submitted
    // if an rollback to savepoint before this point, which means
    // replay of this rollback-savepoint-log must pre-berrier to
    // wait serial replay parts finished
    exec_info_.serial_final_seq_no_ = max_seq_no;
    mt_ctx_.set_parallel_logging(serial_final_scn, max_seq_no);
  }
  return ret;
}

inline void ObTxCtx::recovery_parallel_logging_()
{
  mt_ctx_.set_parallel_logging(exec_info_.serial_final_scn_, exec_info_.serial_final_seq_no_);
  if (exec_info_.max_applied_log_ts_ >= exec_info_.serial_final_scn_) {
    // the serial final log has been synced or replayed
    // notify callback_mgr serial part is finished
    // by fake an replay success call
    mt_ctx_.replay_end(true, 0, exec_info_.serial_final_scn_);
  }
}

int ObTxCtx::get_stat_for_virtual_table(bool &has_write_state, int &busy_cbs_cnt)
{
  int ret = OB_SUCCESS;
  if (OB_SUCC(lock_.try_rdlock_ctx())) {
    has_write_state = exec_info_.has_write_state();
    busy_cbs_cnt = busy_cbs_.get_size();
    lock_.unlock_ctx();
  }
  return ret;
}

int ObTxCtx::post_tx_commit_resp_(const int status)
{
  int ret = OB_SUCCESS;
  bool has_skip = false;
  const share::SCN commit_version = ctx_tx_data_.get_commit_version();
  if (!has_commit_callback_()) {
    if (OB_FAIL(defer_commit_callback_(status, commit_version))) {
    } else {
#ifndef NDEBUG
      TRANS_LOG(INFO, "report tx commit result succeed", K(status), KP(this));
#endif
    }
  } else {
    has_skip = true;
  }
  REC_TRANS_TRACE_EXT(tlog_, response_scheduler,
                      OB_ID(ret), ret,
                      OB_ID(tag1), has_skip,
                      OB_ID(status), status,
                      OB_ID(commit_version), commit_version);
  return ret;
}

int ObTxCtx::restart_commit_retry_timer_()
{
  int ret = OB_SUCCESS;

  commit_retry_timeout_ = get_commit_retry_interval_us_();
  (void)unregister_timeout_task_();
  if (OB_FAIL(register_timeout_task_(commit_retry_timeout_))) {
  }

  return ret;
}

int ObTxCtx::set_commit_request_id_(const int64_t request_id)
{
  int ret = OB_SUCCESS;

  request_id_ = request_id;

  return ret;
}

int ObTxCtx::supplement_tx_op_if_exist_(const bool for_replay, const SCN replay_scn)
{
  int ret = OB_SUCCESS;

  ObTxTable *tx_table = nullptr;
  ObTxDataGuard guard;
  ObTxDataGuard tmp_tx_data_guard;
  tmp_tx_data_guard.reset();
  ctx_tx_data_.get_tx_table(tx_table);

  if (for_replay && !replay_scn.is_valid()) {
    ret = OB_INVALID_ARGUMENT;
    TRANS_LOG(WARN, "supplement tx_op", KR(ret), K(for_replay), K(replay_scn), KPC(this));
  } else if (OB_FAIL(ctx_tx_data_.get_tx_data(guard))) {
  } else if (OB_FAIL(tx_table->alloc_tx_data(tmp_tx_data_guard))) {
  } else if (FALSE_IT(tmp_tx_data_guard.tx_data()->tx_id_ = trans_id_)) {
  } else if (OB_FAIL(tx_table->supplement_tx_op_if_exist(tmp_tx_data_guard.tx_data()))) {
  } else if (OB_FAIL(ctx_tx_data_.recover_tx_data(tmp_tx_data_guard.tx_data()))) {
  } else if (for_replay && tmp_tx_data_guard.tx_data()->op_guard_.is_valid() &&
      OB_FAIL(recover_tx_ctx_from_tx_op_(tmp_tx_data_guard.tx_data()->op_guard_->get_tx_op_list(), replay_scn))) {
    TRANS_LOG(WARN, "recover tx_ctx from tx_op failed", KR(ret));
  }
  TRANS_LOG(INFO, "supplement_tx_op_if_exist_", KR(ret), K(trans_id_), K(ctx_tx_data_));
  return ret;
}

int ObTxCtx::recover_tx_ctx_from_tx_op_(ObTxOpVector &tx_op_list, const SCN replay_scn)
{
  TRANS_LOG(INFO, "recover tx_ctx from_tx_op begin", K(tx_op_list.get_count()), K(replay_scn), KPC(this));
  int ret = OB_SUCCESS;
  // filter tx_op for this tx_ctx life_cycle
  ObTxOpArray ctx_tx_op;
  for (int64_t idx = 0; OB_SUCC(ret) && idx < tx_op_list.get_count(); idx++) {
    ObTxOp &tx_op = *tx_op_list.at(idx);
    if (tx_op.get_op_scn() < replay_scn) {
      if (tx_op.get_op_code() == ObTxOpCode::ABORT_OP) {
        ctx_tx_op.reuse();
      } else if (OB_FAIL(ctx_tx_op.push_back(tx_op))) {
      }
    } else {
      if (tx_op.get_op_code() == ObTxOpCode::ABORT_OP) {
        break;
      } else if (OB_FAIL(ctx_tx_op.push_back(tx_op))) {
      }
    }
  }
  // recover tx_op to tx_ctx
  ObTxBufferNodeArray mds_array;
  for (int64_t idx = 0; OB_SUCC(ret) && idx < ctx_tx_op.count(); idx++) {
    ObTxOp &tx_op = ctx_tx_op.at(idx);
    if (tx_op.get_op_code() == ObTxOpCode::MDS_OP) {
      ObTxBufferNodeWrapper &node_wrapper = *tx_op.get<ObTxBufferNodeWrapper>();
      const ObTxBufferNode &node = node_wrapper.get_node();
      if (OB_FAIL(mds_array.push_back(node))) {
      }
    } else {
      ret = OB_ERR_UNEXPECTED;
      TRANS_LOG(WARN, "recover tx_op undefined", KR(ret), KPC(this));
    }
  }
  ObTxBufferNodeArray _unused_;
  if (FAILEDx(deep_copy_mds_array_(mds_array, _unused_))) {
    TRANS_LOG(WARN, "deep copy mds array failed", KR(ret), KPC(this));
  }
  int64_t mds_max_register_no = 0;
  if (mds_array.count() > 0) {
    mds_max_register_no = mds_array.at(mds_array.count() - 1).get_register_no();
  }
  int64_t ctx_max_register_no = 0;
  if (exec_info_.multi_data_source_.count() > 0) {
    ctx_max_register_no = exec_info_.multi_data_source_.at(exec_info_.multi_data_source_.count() - 1).get_register_no();
  }
  TRANS_LOG(INFO, "recover tx_ctx from tx_op finish", KR(ret), K(tx_op_list.get_count()), K(ctx_tx_op.count()),
      K(mds_array.count()), K(exec_info_.multi_data_source_.count()),
      K(mds_max_register_no), K(ctx_max_register_no),
      KPC(this));
  return ret;
}


int ObTxCtx::update_local_max_commit_version_(const SCN &commit_version)
{
  int ret = OB_SUCCESS;
  trans_service_->get_tx_version_mgr().update_max_commit_ts(commit_version, false);
  return ret;
}

} // namespace transaction
} // namespace oceanbase
