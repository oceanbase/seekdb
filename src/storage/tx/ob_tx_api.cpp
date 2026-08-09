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

#include "ob_trans_service.h"
#include "share/rc/ob_server_runtime.h"
#include "ob_tx_ctx.h"
#include "storage/tx/ob_weak_read_util.h"
#include "storage/tx_storage/ob_ls_service.h"
#include "storage/ls/ob_ls.h"
#include "data_plane/transaction/ob_i_transaction_service.h"
#include "data_plane/transaction/ob_tx_control.h"
#include "data_plane/transaction/ob_tx_desc_access.h"
#include "data_plane/transaction/ob_tx_desc_lifecycle.h"
// ------------------------------------------------------------------------------------------
// Implimentation notes:
// there are two relation we need care:
// a) the relation between data: WAR, we need read data after writen
// b) the relation between operations:
//    i. write happened before rollback, to barrier write after rollback
//    ii. savepoint happened before following writes, to barrier write after savepoint
//
// thus, the interface use two logical clock to track these relations:
// 1. the Logical Clock for data relation, and
// 2. the In-Transaction-Clock for operation relation
//
// ::get_snapshots::
//   1. advance Logical Clock to establish data relation of Write-After-Read
// ::create savepoint::
//   1. advance Logical Clock to establish data relation of Write-After-SavePoint
//   2. advance In-Transaction-Clock to establish operation relation of Write-After-SavePoint
// ::rollback to savepoint::
//   1. advance In-Transaction-Clock to establish operation relation of Rollback-After-Write
// -------------------------------------------------------------------------------------------
#define TXN_API_SANITY_CHECK_FOR_TXN_FREE_ROUTE(end_txn)                \
  do {                                                                  \
    bool inv = false;                                                   \
    inv = tx.addr_ != self_;                                            \
    if (inv) {                                                          \
      int ret = OB_TRANS_FREE_ROUTE_NOT_SUPPORTED;                      \
      TRANS_LOG(ERROR, "incorrect route of txn free route", K(ret), K(tx)); \
      return ret;                                                       \
    }                                                                   \
  } while (0);

namespace oceanbase {

using namespace share;

namespace transaction {

inline int ObTransService::init_tx_(ObTxDesc &tx,
                                    const uint32_t session_id)
{
  int ret = OB_SUCCESS;

  tx.addr_      = self_;
  tx.sess_id_   = session_id;
  tx.alloc_ts_  = ObClockGenerator::getClock();
  tx.expire_ts_ = INT64_MAX;
  tx.op_sn_     = 1;
  tx.state_     = ObTxDesc::State::IDLE;
  tx.data_version_ = DATA_CURRENT_VERSION;
  tx.seq_base_ = common::ObSequence::get_max_seq_no() - 1;
  return ret;
}

int ObTransService::acquire_tx(ObTxDesc *&tx,
                               const uint32_t session_id)
{
  int ret = OB_SUCCESS;
  if (OB_FAIL(tx_desc_mgr_.alloc(tx))) {
  } else {
    ret = init_tx_(*tx, session_id);
  }
  if (OB_SUCC(ret)) {
    ObTransTraceLog &tlog = tx->get_tlog();
    REC_TRANS_TRACE_EXT(&tlog, acquire, OB_Y(ret),
                        OB_ID(addr), (void*)tx,
                        OB_ID(session), session_id,
                        OB_ID(ref), tx->get_ref(),
                        OB_ID(thread_id), GETTID());
  }
  return ret;
}

int ObTransService::finalize_tx_(ObTxDesc &tx)
{
  int ret = OB_SUCCESS;
  ObSpinLockGuard guard(tx.lock_);
  if (!tx.flags_.RELEASED_) {
    tx.flags_.RELEASED_ = true;
    if (tx.is_tx_active()) {
      ret = OB_ERR_UNEXPECTED;
      TRANS_LOG(ERROR, "release tx when tx is active", K(ret), KPC(this), K(tx));
      tx.print_trace_();
    } else if (tx.is_committing()) {
      TRANS_LOG(WARN, "release tx when tx is committing", KPC(this), K(tx));
    }
    // invalid registered snapshot
    invalid_registered_snapshot_(tx);
    tx.cancel_commit_cb();
    if (tx.tx_id_.is_valid()) {
      tx_desc_mgr_.remove(tx);
    }
  }
  return ret;
}

/*
 * release_tx - release the Tx object
 *
 * release tx is the final step for user operate on TxDesc.
 * generally, user should commit / rollback the tx before they release it.
 * the txDesc object should not been access anymore after release.
 *
 * - for tx in async committing
 *   the commit callback will not be called if not already called, and
 *   don't forget to call release_tx before release callback's memory
 * - for tx which is a shadow copy of original tx (started on another server)
 *   release just free its memory used
 */
int ObTransService::release_tx(ObTxDesc &tx)
{
  int ret = OB_SUCCESS;
  // There is one process-wide ObTransService; release directly on this service.
  {
    ObTransTraceLog &tlog = tx.get_tlog();
    REC_TRANS_TRACE_EXT(&tlog, release, OB_Y(ret),
                        OB_ID(ref), tx.get_ref(),
                        OB_ID(thread_id), GETTID());
    if (tx.flags_.SHADOW_) {
#ifndef NDEBUG
      if (tx.tx_id_.is_valid()) {
        tx.print_trace();
      }
#endif
      tx_desc_mgr_.revert(tx);
    } else {
      finalize_tx_(tx);
      tx_desc_mgr_.revert(tx);
    }
  }
  TRANS_LOG(TRACE, "release tx done", KP(&tx), KPC(this), K(lbt()));
  return ret;
}

int ObTransService::reuse_tx(ObTxDesc &tx)
{
  int ret = OB_SUCCESS;
  int spin_cnt = 0;
  int final_ref_cnt = 0;
  ObTransID orig_tx_id = tx.tx_id_;
  if (tx.is_in_tx() && !tx.is_tx_end()) {
    ret = OB_ERR_UNEXPECTED;
    TRANS_LOG(ERROR, "can not reuse tx which has active and not end yet", K(ret), K(tx.tx_id_));
  } else if (OB_FAIL(finalize_tx_(tx))) {
  } else {
    // after finalize tx, the txDesc can not be fetch from TxDescMgr
    // but the reference maybe hold by user, wait to be queisenct
    // before we reuse it

    // if reuse come from commit_cb, assume current thread hold one reference
    int64_t cb_tid = ATOMIC_LOAD_ACQ(&tx.cb_tid_);
    final_ref_cnt = cb_tid == GETTID() ? 2 : 1;
    while (tx.get_ref() > final_ref_cnt) {
      PAUSE();
      if (++spin_cnt > 2000) {
        TRANS_LOG(WARN, "blocking to wait tx referent quiescent cost too much time",
                  "tx_id", orig_tx_id, KP(&tx), K(final_ref_cnt), K(spin_cnt), K(tx.get_ref()), K(cb_tid));
        tx.print_trace();
        usleep(2000000); // 2s
      } else if (spin_cnt > 200) {
        usleep(2000);    // 2ms
      } else if (spin_cnt > 100) {
        usleep(200);     // 200us
      }
#ifdef ENABLE_DEBUG_LOG
      if (spin_cnt > 2300) {
        // at least wait 600s
        ob_abort();
      }
#endif
    }
    // it is safe to operate tx without lock when not shared
    ret = reinit_tx_(tx, tx.sess_id_);
  }
  ObTransTraceLog &tlog = tx.get_tlog();
  REC_TRANS_TRACE_EXT(&tlog, reuse, OB_Y(ret),
                      OB_ID(addr), (void*)&tx,
                      OB_ID(txid), orig_tx_id,
                      OB_ID(tag1), spin_cnt,
                      OB_ID(tag2), final_ref_cnt,
                      OB_ID(ref), tx.get_ref(),
                      OB_ID(thread_id), GETTID());
  return ret;
}

int ObTransService::reinit_tx_(ObTxDesc &tx, const uint32_t session_id)
{
  tx.reset();
  return init_tx_(tx, session_id);
}

int ObTransService::stop_tx(ObTxDesc &tx)
{
  int ret = OB_SUCCESS;
  bool need_cb = false;
  {
    ObSpinLockGuard guard(tx.lock_);
    TRANS_LOG(INFO, "stop_tx, print its trace as following", K(tx));
    tx.print_trace_();
    if (tx.addr_ != self_) {
      // A transaction on a temporary node depends on session cleanup to quit.
      TRANS_LOG(INFO, "this is not txn start node.");
      need_cb = false;
    } else {
      if (tx.state_ < ObTxDesc::State::IN_TERMINATE) {
        abort_tx_(tx, ObTxAbortCause::STOP, true);
      } else if (!tx.is_terminated()) {
        unregister_commit_retry_task_(tx);
        // arm callback arguments
        tx.commit_out_ = OB_TRANS_UNKNOWN;
        tx.state_ = ObTxDesc::State::COMMIT_UNKNOWN;
      }
      need_cb = true;
    }
  }
  // run callback after unlock
  if (need_cb) {
    tx.execute_commit_cb();
  }
  return ret;
}

int ObTransService::start_tx(ObTxDesc &tx, const ObTxParam &tx_param)
{
  int ret = OB_SUCCESS;
  if (!tx_param.is_valid()) {
    ret = OB_INVALID_ARGUMENT;
    TRANS_LOG(WARN, "invalid tx param", K(ret), KR(ret), K(tx_param));
  } else {
    ObSpinLockGuard guard(tx.lock_);
    tx.inc_op_sn();
    const bool write_fenced = !share::server_is_write_enabled();
    if (!write_fenced) {
      ret = tx_desc_mgr_.add(tx);
    } else {
      // A transaction admitted while the process write gate is fenced never
      // writes and therefore needs no append-backed transaction ID.
      tx.flags_.SHADOW_ = false;
    }
    if (OB_FAIL(ret)) {
    } else {
      // Promotion changes admission for later transactions only. A transaction
      // begun during recovery remains read-only until it ends.
      tx.access_mode_     = write_fenced
          ? ObTxAccessMode::FENCED_RD_ONLY
          : tx_param.access_mode_;
      tx.isolation_       = tx_param.isolation_;
      tx.active_ts_       = ObClockGenerator::getClock();
      tx.timeout_us_      = tx_param.timeout_us_;
      tx.lock_timeout_us_ = tx_param.lock_timeout_us_;
      tx.expire_ts_       = tx.get_expire_ts();
      // start tx need reacquire snapshot
      tx.snapshot_version_.reset();
      // setup correct active_scn, whatever its used or not
      tx.active_scn_      = tx.get_tx_seq();
      tx.state_           = ObTxDesc::State::ACTIVE;
      tx.flags_.EXPLICIT_ = true;
    }
    ObTransTraceLog &tlog = tx.get_tlog();
    REC_TRANS_TRACE_EXT(&tlog, start_tx, OB_Y(ret),
                        OB_ID(txid), tx.tx_id_,
                        OB_ID(isolation_level), (int)tx.isolation_,
                        OB_ID(ref), tx.get_ref(),
                        OB_ID(thread_id), GETTID());
  }
  if (OB_FAIL(ret)) {
  } else {
    tx.state_change_flags_.mark_all();
#ifndef NDEBUG
    TRANS_LOG(INFO, "start tx succeed", K(tx));
#endif
  }
  return ret;
}

int ObTransService::rollback_tx(ObTxDesc &tx)
{
  TXN_API_SANITY_CHECK_FOR_TXN_FREE_ROUTE(true)
  int ret = OB_SUCCESS;
  ObSpinLockGuard guard(tx.lock_);
  tx.inc_op_sn();
  switch(tx.state_) {
  case ObTxDesc::State::ABORTED:
    tx.state_ = ObTxDesc::State::ROLLED_BACK;
    break;
  case ObTxDesc::State::ROLLED_BACK:
    ret = OB_TRANS_ROLLBACKED;
    TRANS_LOG(WARN, "tx rollbacked", K(ret), K(tx));
    break;
  case ObTxDesc::State::COMMITTED:
    ret = OB_TRANS_COMMITED;
    TRANS_LOG(WARN, "tx committed", K(ret), K(tx));
    break;
  case ObTxDesc::State::IN_TERMINATE:
  case ObTxDesc::State::COMMIT_TIMEOUT:
  case ObTxDesc::State::COMMIT_UNKNOWN:
    ret = OB_TRANS_HAS_DECIDED;
    TRANS_LOG(WARN, "tx in terminating", K(ret), K(tx));
    break;
  case ObTxDesc::State::ACTIVE:
  case ObTxDesc::State::IMPLICIT_ACTIVE:
    tx.state_ = ObTxDesc::State::IN_TERMINATE;
    tx.abort_cause_ = OB_TRANS_ROLLBACKED;
    abort_write_state_(tx);
  case ObTxDesc::State::IDLE:
    tx.state_ = ObTxDesc::State::ROLLED_BACK;
    tx.finish_ts_ = ObClockGenerator::getClock();
    tx_post_terminate_(tx);
    break;
  default:
    ret = OB_TRANS_INVALID_STATE;
    TRANS_LOG(WARN, "invalid state", K(ret), K_(tx.state), K(tx));
  }
  TRANS_LOG(INFO, "rollback tx", K(ret), K(*this), K(tx));
  ObTransTraceLog &tlog = tx.get_tlog();
  REC_TRANS_TRACE_EXT(&tlog, rollback_tx, OB_Y(ret),
                      OB_ID(ref), tx.get_ref(),
                      OB_ID(thread_id), GETTID());
  return ret;
}

// impl note
// abort tx should invalidate registered snapshot
// savepoint not invalidate, they were invalidate
// when do explicit rollback
int ObTransService::abort_tx(ObTxDesc &tx, int cause)
{
  int ret = OB_SUCCESS;
  ObSpinLockGuard guard(tx.lock_);
  tx.inc_op_sn();
  if (tx.state_ != ObTxDesc::State::ABORTED) {
    ret = abort_tx_(tx, cause);
  }
  ObTransTraceLog &tlog = tx.get_tlog();
  REC_TRANS_TRACE_EXT(&tlog, abort_tx, OB_Y(ret),
                      OB_ID(arg), cause,
                      OB_ID(ref), tx.get_ref(),
                      OB_ID(thread_id), GETTID());
  tx.print_trace_();
  return ret;
}

namespace {
  struct SyncTxCommitCb : public ObITxCallback
  {
    public:
    void callback(int ret) { cond_.notify(ret); }
    int wait(const int64_t time_us, int &ret) {
      return cond_.wait(time_us, ret);
    }
    ObTransCond cond_;
  };
}

int ObTransService::commit_tx(ObTxDesc &tx, const int64_t expire_ts)
{
  int ret = OB_SUCCESS;
  int64_t start_ts = ObTimeUtility::current_time();
  SyncTxCommitCb cb;
  if (OB_SUCC(submit_commit_tx(tx, expire_ts, cb))) {
    int result = 0;
    // plus 10s to wait callback, if callback leaky, wakeup self
    int64_t wait_us = MAX(expire_ts - ObTimeUtility::current_time(), 0) + 10 * 1000 * 1000L;
    if (OB_FAIL(cb.wait(wait_us, result))) {
      TRANS_LOG(WARN, "wait commit fail", K(ret), K(expire_ts), K(wait_us), K(tx.tx_id_));
      /* NOTE: must cancel callback before release it */
      ObITxCallback *cb_ret = tx.cancel_commit_cb();
      if (OB_ISNULL(cb_ret)) {
        // cancel cb fail, the cb has been processing, need wait
        while (OB_FAIL(cb.wait(1_s, result))) {
          TRANS_LOG(WARN, "wait commit fail, retry", K(ret), K(tx.tx_id_));
        }
        ret = result;
      } else {
        ret = ret == OB_TIMEOUT ? OB_TRANS_STMT_TIMEOUT : ret;
      }
    } else {
      ret = result;
    }
  }
  int64_t elapsed_us = ObTimeUtility::current_time() - start_ts;
#ifndef NDEBUG
  TRANS_LOG(INFO, "sync commit tx", K(ret), K(tx), K(expire_ts));
#else
  if (OB_FAIL(ret)) {
  }
#endif
  ObTransTraceLog &tlog = tx.get_tlog();
  REC_TRANS_TRACE_EXT(&tlog, commit_tx, OB_Y(ret), OB_Y(expire_ts),
                      OB_ID(time_used), elapsed_us,
                      OB_ID(ref), tx.get_ref(),
                      OB_ID(thread_id), GETTID());
  if (OB_FAIL(ret)) {
    tx.print_trace();
  }
  return ret;
}

// impl note
// imediately succeed cases:
//   1. idle state
//   2. empty valid part
// imediately fail cases:
//   1. aborted state
//   2. incorrect state
//   3. tx-timeout state
// on commit finish:
//   1. release savepoints
// on commit fail:
//   1. invalid registered snapshot
int ObTransService::submit_commit_tx(ObTxDesc &tx,
                                     const int64_t expire_ts,
                                     ObITxCallback &cb)
{
  TXN_API_SANITY_CHECK_FOR_TXN_FREE_ROUTE(true)
  int ret = OB_SUCCESS;
  bool need_cb = false;
  {
    ObSpinLockGuard guard(tx.lock_);
    if (tx.commit_ts_ <= 0) {
      tx.commit_ts_ = ObClockGenerator::getClock();
    }
    tx.inc_op_sn();
    switch(tx.state_) {
    case ObTxDesc::State::IDLE:
      (void)0;
      tx.set_commit_cb(&cb);
      handle_tx_commit_result_(tx, OB_SUCCESS);
      ret = OB_SUCCESS;
      break;
    case ObTxDesc::State::ABORTED:
      handle_tx_commit_result_(tx, OB_TRANS_ROLLBACKED);
      ret = OB_TRANS_ROLLBACKED;
      break;
    case ObTxDesc::State::ROLLED_BACK:
      ret = OB_TRANS_ROLLBACKED;
      TRANS_LOG(WARN, "insane tx action", K(ret), K(tx));
      break;
    case ObTxDesc::State::COMMITTED:
      ret = OB_TRANS_COMMITED;
      TRANS_LOG(WARN, "insane tx action", K(ret), K(tx));
      break;
    case ObTxDesc::State::IN_TERMINATE:
    case ObTxDesc::State::COMMIT_TIMEOUT:
    case ObTxDesc::State::COMMIT_UNKNOWN:
      ret = OB_TRANS_HAS_DECIDED;
      TRANS_LOG(WARN, "insane tx action", K(ret), K(tx));
      break;
    case ObTxDesc::State::ACTIVE:
    case ObTxDesc::State::IMPLICIT_ACTIVE:
      if (tx.expire_ts_ <= ObClockGenerator::getClock()) {
        TRANS_LOG(WARN, "tx has timeout, it has rollbacked internally", K_(tx.expire_ts), K(tx));
        tx.print_trace_();
        ret = OB_TRANS_ROLLBACKED;
        handle_tx_commit_result_(tx, OB_TRANS_ROLLBACKED);
      } else if (tx.flags_.WRITE_STATE_INCOMPLETE_) {
        TRANS_LOG(WARN, "txn write state state incomplete, can not commit", K(ret), K(tx));
        abort_tx_(tx, ObTxAbortCause::WRITE_STATE_INCOMPLETE);
        handle_tx_commit_result_(tx, OB_TRANS_ROLLBACKED);
        ret = OB_TRANS_ROLLBACKED;
      } else if (tx.flags_.WRITE_STATE_ABORTED_) {
        TRANS_LOG(WARN, "txn write state aborted, can not commit", K(ret), K(tx));
        abort_tx_(tx, OB_TRANS_ROLLBACKED);
        handle_tx_commit_result_(tx, OB_TRANS_ROLLBACKED);
        ret = OB_TRANS_ROLLBACKED;
      } else if (tx.is_write_state_without_valid_write()) {
        // explicit savepoint rollback cause empty valid-part-set
        tx.set_commit_cb(&cb);
        abort_write_state_(tx);             // let write state ctx quit
        handle_tx_commit_result_(tx, OB_SUCCESS); // commit success
        ret = OB_SUCCESS;
      }
      break;
    default:
      TRANS_LOG(WARN, "anormaly tx state", K(tx));
      abort_tx_(tx, ObTxAbortCause::IN_CONSIST_STATE);
      handle_tx_commit_result_(tx, OB_TRANS_ROLLBACKED);
      ret = OB_TRANS_ROLLBACKED;
    }
    // normal path, commit cont.
    if (OB_SUCC(ret) && (
        tx.state_ == ObTxDesc::State::ACTIVE ||
        tx.state_ == ObTxDesc::State::IMPLICIT_ACTIVE)) {
      ObTxDesc::State state0 = tx.state_;
      tx.state_ = ObTxDesc::State::IN_TERMINATE;
      SCN commit_version;
      if (OB_SUCC(ret) &&
          OB_FAIL(do_commit_tx_(tx, expire_ts, cb, commit_version))) {
        TRANS_LOG(WARN, "try to commit tx fail, tx will be aborted",
                  K(ret), K(expire_ts), K(tx), KP(&cb));
        // the error may caused by txn has terminated
        handle_tx_commit_result_(tx, ret, commit_version);
      }
      // if txn not terminated, it can be choice to abort
      if (OB_FAIL(ret) && tx.state_ == ObTxDesc::State::IN_TERMINATE) {
        tx.state_ = state0;
        abort_tx_(tx, ret);
        handle_tx_commit_result_(tx, OB_TRANS_ROLLBACKED);
        ret = OB_TRANS_ROLLBACKED;
      }
    }

    /* NOTE:
    * to prevent potential deadlock, distinguish the commit
    * completed by current thread from other cases
    */
    bool committed = tx.state_ == ObTxDesc::State::COMMITTED;
    // if tx committed, we should callback immediately
    //
    // NOTE: this must defer to final current function
    // in order to assure there is no access to tx, because
    // after calling the commit_cb, the tx object may be
    // released or reused
    if (OB_SUCC(ret) && committed) {
      need_cb = true;
    }
  #ifndef NDEBUG
    TRANS_LOG(INFO, "submit commit tx", K(ret),
              K(committed), KPC(this), K(tx), K(expire_ts), KP(&cb));
  #else
    if (OB_FAIL(ret)) {
    }
    #endif
    ObTransTraceLog &tlog = tx.get_tlog();
    REC_TRANS_TRACE_EXT(&tlog, submit_commit_tx, OB_Y(ret), OB_Y(expire_ts),
                        OB_ID(tag1), committed,
                        OB_ID(ref), tx.get_ref(),
                        OB_ID(thread_id), GETTID());
  }

  if (need_cb){
    direct_execute_commit_cb_(tx);
  }
  return ret;
}

// when callback exec directly, mock the general pattern
// acquire ref -> exec callback -> release ref
void ObTransService::direct_execute_commit_cb_(ObTxDesc &tx)
{
  tx.inc_ref(1);
  tx.execute_commit_cb();
  tx_desc_mgr_.revert(tx);
}

int ObTransService::get_read_snapshot(ObTxDesc &tx,
                                      const ObTxIsolationLevel iso_level,
                                      const int64_t expire_ts,
                                      ObTxReadSnapshot &snapshot)
{
  int ret = OB_SUCCESS;
  ObSpinLockGuard guard(tx.lock_);
  ObTxIsolationLevel isolation = iso_level;
  if (OB_SUCC(tx_sanity_check_(tx))) {
    if (tx.is_in_tx() && isolation != tx.isolation_) {
      //use txn's isolation if txn is active
      isolation = tx.isolation_;
    }
  }
  if (OB_FAIL(ret)) {
  } else if (is_RR_or_SERIAL_isolevel(isolation)) {
    // only acquire snapshot once in these isolation level
    if (tx.isolation_ != isolation /*change isolation*/ ||
        !tx.snapshot_version_.is_valid()/*version invalid*/) {
      SCN version;
      if (OB_FAIL(acquire_local_snapshot_(version))) {
      } else if (tx.access_mode_ != ObTxAccessMode::FENCED_RD_ONLY
                 && !tx.tx_id_.is_valid()
                 && OB_FAIL(tx_desc_mgr_.add(tx))) {
        TRANS_LOG(WARN, "add tx to mgr fail", K(ret), K(tx));
      }
      if (OB_SUCC(ret)) {
        tx.snapshot_version_ = version;
        tx.snapshot_uncertain_bound_ = 0;
        tx.snapshot_scn_ = tx.get_tx_seq(ObSequence::get_max_seq_no() + 1);
        tx.state_change_flags_.EXTRA_CHANGED_ = true;
      }
    }
    if (OB_SUCC(ret)) {
      tx.isolation_ = isolation;
      snapshot.core_.version_ = tx.snapshot_version_;
      snapshot.uncertain_bound_ = tx.snapshot_uncertain_bound_;
    }
  } else { // RC isolation level
    if (OB_FAIL(acquire_local_snapshot_(snapshot.core_.version_))) {
    } else {
      snapshot.uncertain_bound_ = 0;
      adjust_tx_snapshot_(tx, snapshot);
    }
  }

  if (OB_SUCC(ret)) {
    snapshot.source_ = ObTxReadSnapshot::SRC::LS;
    snapshot.uncertain_bound_ = 0;
    snapshot.reset_write_state();
    // If tx id is valid , record tx_id and scn
    if (tx.tx_id_.is_valid()) {
      snapshot.core_.tx_id_ = tx.tx_id_;
      snapshot.core_.scn_ = tx.get_tx_seq();
    }
    if (tx.state_ != ObTxDesc::State::IDLE &&
        OB_FAIL(tx.fill_read_snapshot_write_state(snapshot))) {
      TRANS_LOG(WARN, "fill snapshot write state failed", K(ret), K(tx), K(snapshot));
    }
    snapshot.valid_ = true;
  }
  ObTransTraceLog &tlog = tx.get_tlog();
  common::ObTraceIdAdaptor trace_id;
  trace_id.set(ObCurTraceId::get());
  REC_TRANS_TRACE_EXT(&tlog, get_read_snapshot, OB_Y(ret), OB_Y(expire_ts),
                      OB_ID(txid), tx.tx_id_,
                      OB_ID(isolation_level), (int)isolation,
                      OB_ID(snapshot_source), (int)snapshot.source_,
                      OB_ID(snapshot_version), snapshot.core_.version_,
                      OB_ID(snapshot_txid), snapshot.core_.tx_id_.get_id(),
                      OB_ID(snapshot_scn), snapshot.core_.scn_.cast_to_int(),
                      OB_ID(trace_id), trace_id,
                      OB_ID(ref), tx.get_ref(),
                      OB_ID(thread_id), GETTID());
  return ret;
}

int ObTransService::get_read_snapshot_version(const int64_t expire_ts,
                                              SCN &snapshot_version)
{
  int ret = OB_SUCCESS;
  UNUSED(expire_ts);
  ret = acquire_local_snapshot_(snapshot_version);
  return ret;
}


int ObTransService::get_weak_read_snapshot_version(const int64_t max_read_stale_us_for_user,
                                                   SCN &snapshot)
{
  int ret = OB_SUCCESS;
  // The weak-read snapshot is the local readable timestamp maintained by the
  // transaction loop worker.
  bool monotinic_read = ObWeakReadUtil::enable_monotonic_weak_read();
  SCN wrs_scn = SCN::max_scn();
  {
    storage::ObLSService *ls_svr = ::oceanbase::share::server_service<::oceanbase::storage::ObLSService>();
    storage::ObLS *tenant_ls = nullptr;
    storage::ObLS *ls = nullptr;
    if (OB_ISNULL(ls_svr)) {
      ret = OB_ERR_UNEXPECTED;
      TRANS_LOG(WARN, "ls service is null", K(ret), KPC(this));
    } else if (OB_FAIL(ls_svr->get_ls(tenant_ls))) {
    } else {
      ls = tenant_ls;
      wrs_scn = ls->get_ls_wrs_handler()->get_ls_weak_read_ts();
    }
    if (OB_SUCC(ret) && !wrs_scn.is_valid_and_not_min()) {
      // No readable ls weak-read ts yet: fall back to gts-derived min version.
      ret = ObWeakReadUtil::generate_min_weak_read_version(wrs_scn);
    }
  }
  if (OB_SUCC(ret)) {
    if (monotinic_read
        || max_read_stale_us_for_user < 0) {
      // no need to check barrier version
      snapshot = wrs_scn;
    } else {
      // check snapshot version barrier which is setted by user system variable
      SCN gts_cache;
      SCN current_scn;
      if (OB_FAIL(OB_TS_MGR.get_gts(gts_cache))) {
      } else {
        const int64_t current_time_us = std::max(ObTimeUtility::current_time(), gts_cache.convert_to_ts());
        current_scn.convert_from_ts(current_time_us - max_read_stale_us_for_user);
        snapshot = SCN::max(wrs_scn, current_scn);
      }
    }
  }
  return ret;
}

int ObTransService::release_snapshot(ObTxDesc &tx)
{
  int ret = OB_SUCCESS;
  SCN snapshot;
  ObSpinLockGuard guard(tx.lock_);
  tx.inc_op_sn();
  if (tx.state_ != ObTxDesc::State::IDLE) {
    ret = OB_NOT_SUPPORTED;
  } else if (tx.with_tx_snapshot()) {
    snapshot = tx.snapshot_version_;
    tx.snapshot_version_.reset();
    tx.snapshot_uncertain_bound_ = 0;
  }
  ObTransTraceLog &tlog = tx.get_tlog();
  REC_TRANS_TRACE_EXT(&tlog, release_snapshot, OB_Y(ret), OB_ID(thread_id), GETTID());
  return ret;
}

int ObTransService::register_tx_snapshot_verify(ObTxReadSnapshot &snapshot)
{
  int ret = OB_SUCCESS;
  const ObTransID &tx_id = snapshot.core_.tx_id_;
  if (tx_id.is_valid()) {
    ObTxDesc *tx = NULL;
    if (OB_SUCC(tx_desc_mgr_.get(tx_id, tx))) {
      ObTxSavePoint sp;
      sp.init(&snapshot);
      ObSpinLockGuard guard(tx->lock_);
      if (OB_FAIL(tx_sanity_check_(*tx))) {
      } else if (OB_FAIL(tx->savepoints_.push_back(sp))) {
      }
      ObTransTraceLog &tlog = tx->get_tlog();
      REC_TRANS_TRACE_EXT(&tlog, register_snapshot, OB_Y(ret),
                          OB_ID(arg), (void*)&snapshot,
                          OB_ID(snapshot_version), snapshot.core_.version_,
                          OB_ID(snapshot_scn), snapshot.core_.scn_.cast_to_int(),
                          OB_ID(ref), tx->get_ref(),
                          OB_ID(thread_id), GETTID());
    } else if (ret != OB_ENTRY_NOT_EXIST) {
      TRANS_LOG(WARN, "get tx fail", K(tx_id), K(snapshot));
    } else {
      ret = OB_SUCCESS;
    }
    if (OB_NOT_NULL(tx)) {
      tx_desc_mgr_.revert(*tx);
    }
  }
  return ret;
}


int ObTransService::create_branch_savepoint(ObTxDesc &tx,
                                            const int16_t branch,
                                            ObTxSEQ &savepoint)
{
  int ret = OB_SUCCESS;
  ObSpinLockGuard guard(tx.lock_);
  if (OB_SUCC(tx_sanity_check_(tx))) {
    savepoint = tx.inc_and_get_tx_seq(branch);
    ObTransTraceLog &tlog = tx.get_tlog();
    REC_TRANS_TRACE_EXT(&tlog, create_branch_savepoint,
                        OB_Y(ret),
                        OB_ID(savepoint), savepoint.cast_to_int(),
                        OB_ID(branch), (int)branch,
                        OB_ID(opid), tx.op_sn_,
                        OB_ID(ref), tx.get_ref(),
                        OB_ID(thread_id), GETTID());
  }
  return ret;
}

int ObTransService::create_implicit_savepoint(ObTxDesc &tx,
                                              const ObTxParam &tx_param,
                                              ObTxSEQ &savepoint,
                                              const bool release)
{
  int ret = OB_SUCCESS;
  ObSpinLockGuard guard(tx.lock_);
  if (!tx_param.is_valid()) {
    // NOTE: tx_param only required for create global implicit_savepoint when txn in IDLE state
    // TODO: rework this interface, allow skip pass tx_param if not required
    ret = OB_INVALID_ARGUMENT;
    TRANS_LOG(WARN, "tx param invalid", K(ret), K(tx_param), K(tx));
  } else if (OB_FAIL(tx_sanity_check_(tx))) {
  } else if (tx.state_ >= ObTxDesc::State::IN_TERMINATE) {
    ret = OB_TRANS_INVALID_STATE;
    TRANS_LOG(WARN, "create implicit savepoint but tx terminated", K(ret), K(tx));
  } else if (tx.flags_.SHADOW_ && tx.get_tx_id().is_valid()) {
    ret = create_local_implicit_savepoint_(tx, savepoint);
  } else {
    ret = create_global_implicit_savepoint_(tx, tx_param, savepoint, release);
  }
  return ret;
}

int ObTransService::create_local_implicit_savepoint_(ObTxDesc &tx,
                                                     ObTxSEQ &savepoint)
{
  int ret = OB_SUCCESS;
  savepoint = tx.inc_and_get_tx_seq(0);
  ObTransTraceLog &tlog = tx.get_tlog();
  REC_TRANS_TRACE_EXT(&tlog, create_local_implicit_savepoint,
                      OB_Y(ret),
                      OB_ID(savepoint), savepoint.cast_to_int(),
                      OB_ID(opid), tx.op_sn_,
                      OB_ID(ref), tx.get_ref(),
                      OB_ID(thread_id), GETTID());
  return ret;
}

int ObTransService::create_global_implicit_savepoint_(ObTxDesc &tx,
                                                      const ObTxParam &tx_param,
                                                      ObTxSEQ &savepoint,
                                                      const bool release)
{
  int ret = OB_SUCCESS;
  const bool fenced_read_only =
      tx.access_mode_ == ObTxAccessMode::FENCED_RD_ONLY;
  // tx is idle, update tx parameters
  if (tx.state_ == ObTxDesc::State::IDLE) {
    if (!fenced_read_only) {
      tx.access_mode_ = tx_param.access_mode_;
    }
    tx.timeout_us_      = tx_param.timeout_us_;
    if (tx.isolation_ != tx_param.isolation_) {
      tx.isolation_ = tx_param.isolation_;
      tx.snapshot_version_.reset(); // invalidate previouse snapshot
    }
  }
  if (OB_SUCC(ret)) {
    // NOTE: the lock_timeout_us_ can be changed even tx active
    tx.lock_timeout_us_ = tx_param.lock_timeout_us_;
    tx.inc_op_sn();
    savepoint = tx.inc_and_get_tx_seq(0);
    if (!fenced_read_only
        && tx.state_ == ObTxDesc::State::IDLE
        && !tx.tx_id_.is_valid()) {
      if (tx.has_implicit_savepoint()) {
        ret = OB_TRANS_INVALID_STATE;
        TRANS_LOG(WARN, "has implicit savepoint, but tx_id is invalid", K(ret), K(tx));
      } else if (OB_FAIL(tx_desc_mgr_.add(tx))) {
      }
    }
  }
  if (OB_SUCC(ret)) {
    if (release) {
      tx.release_all_implicit_savepoint();
      // reset branch_id alloc for further writes
      tx.last_branch_id_ = tx.branch_id_offset() - 1;
    }
    tx.add_implicit_savepoint(savepoint);
  }
  ObTransTraceLog &tlog = tx.get_tlog();
  REC_TRANS_TRACE_EXT(&tlog, create_global_implicit_savepoint, OB_Y(ret),
                      OB_ID(txid), tx.tx_id_,
                      OB_ID(savepoint), savepoint.cast_to_int(),
                      OB_Y(release),
                      OB_ID(opid), tx.op_sn_,
                      OB_ID(ref), tx.get_ref(),
                      OB_ID(thread_id), GETTID());
  return ret;
}

// impl note
// if tx aborted reject with need_rollback
// if tx terminated reject with COMMITTED / ABORTED
// if tx in-commtting reject with has_decided
// if tx in idle:
//    abort tx and reset tx [1]
// if tx in active:
//    normal rollback [2]
// if tx in implicit_active:
//    if tx.active_scn > savepoint:
//       abort tx and reset tx [1]
//    else
//       normal rollback [2]
// [1] abort tx and reset tx
//     re-register with new tx-id
//     state = IDLE
// [2] normal rollback:
//     if rollback failed: abort tx
int ObTransService::rollback_to_implicit_savepoint(ObTxDesc &tx,
                                                   const ObTxSEQ savepoint,
                                                   const int64_t expire_ts,
                                                   const bool touched_storage,
                                                   const ObTxCleanPolicy clean_policy)
{
  int ret = OB_SUCCESS;
  ObSpinLockGuard guard(tx.lock_);

  if (savepoint.get_branch() // NOTE: branch savepoint only support local rollback
             || tx.flags_.SHADOW_) {
    if (OB_FAIL(tx_sanity_check_(tx))) {
    } else if (touched_storage) {
      ret = OB_NOT_SUPPORTED;
      TRANS_LOG(WARN, "rollback on remote only suport collected tx parts",
                K(ret), K(savepoint), K(tx));
    } else {
      ret = rollback_to_local_implicit_savepoint_(tx, savepoint, expire_ts);
    }
  } else {
    if (tx.state_ < ObTxDesc::State::IN_TERMINATE) {
      if (touched_storage) {
        if (OB_FAIL(tx.mark_write())) {
          TRANS_LOG(WARN, "mark transaction write failed", K(ret), K(tx));
          abort_tx_(tx, ret);
        } else {
          TRANS_LOG(INFO, "mark transaction write", K_(tx.tx_id));
        }
      }
    }

    if (OB_FAIL(ret)) {
      // do nothing
    } else if (OB_FAIL(tx_sanity_check_(tx))) {
    } else {
      ret = rollback_to_global_implicit_savepoint_(tx,
                                                   savepoint,
                                                   expire_ts,
                                                   clean_policy);
    }
  }
  return ret;
}

int ObTransService::rollback_to_local_implicit_savepoint_(ObTxDesc &tx,
                                                          const ObTxSEQ savepoint,
                                                          const int64_t expire_ts)
{
  int ret = OB_SUCCESS;
  ObTxWriteState *part = NULL;
  int64_t start_ts = ObTimeUtility::current_time();
  // when rollback local we use this from_scn for the single write state
  ObTxSEQ from_scn = savepoint.clone_with_seq(ObSequence::inc_and_get_max_seq_no(), tx.seq_base_);
  if (OB_FAIL(find_write_state_after_savepoint_(tx, part, savepoint))) {
  } else if (OB_NOT_NULL(part)) {
    ObTxCtx *ctx = NULL;
    if (OB_FAIL(get_tx_ctx_(tx.tx_id_, ctx))) {
    } else if (OB_FAIL(sync_rollback_to_savepoint_(ctx,
                                                    savepoint,
                                                    tx.op_sn_,
                                                    tx.seq_base_,
                                                    expire_ts,
                                                    from_scn))) {
    } else {
      part->last_scn_ = savepoint;
    }
    if (OB_NOT_NULL(ctx)) {
      revert_tx_ctx_(ctx);
    }
  }

  int64_t elapsed_us = ObTimeUtility::current_time() - start_ts;
#ifndef NDEBUG
  TRANS_LOG(INFO, "rollback local implicit savepoint", K(ret), K(savepoint));
#else
  if (OB_FAIL(ret)) {
  }
#endif
  ObTransTraceLog &tlog = tx.get_tlog();
  REC_TRANS_TRACE_EXT(&tlog, rollback_local_implicit_savepoint,
                      OB_Y(ret), OB_ID(savepoint), savepoint.cast_to_int(), OB_Y(expire_ts),
                      OB_ID(time_used) , elapsed_us,
                      OB_ID(opid), tx.op_sn_,
                      OB_ID(ref), tx.get_ref(),
                      OB_ID(thread_id), GETTID());
  return ret;
}

static bool need_rollback_(const ObTxCleanPolicy p)
{
  return p == FAST_ROLLBACK || p == ROLLBACK;
}

static bool need_clean_writeset_(const ObTxCleanPolicy p)
{
  return p == KEEP || p == ROLLBACK;
}

int ObTransService::rollback_to_global_implicit_savepoint_(ObTxDesc &tx,
                                                           const ObTxSEQ savepoint,
                                                           const int64_t expire_ts,
                                                           const ObTxCleanPolicy clean_policy)
{
  int ret = OB_SUCCESS;
  int64_t start_ts = ObTimeUtility::current_time();
  tx.inc_op_sn();
  bool reset_tx = false, normal_rollback = false, reset_active_scn = false;
  if (OB_SUCC(ret)) {
    switch(tx.state_) {
    case ObTxDesc::State::IDLE:
      tx.release_implicit_savepoint(savepoint);
      ret = OB_SUCCESS;
      break;
    case ObTxDesc::State::ACTIVE:
      tx.release_implicit_savepoint(savepoint);
      normal_rollback = true;
      break;
    case ObTxDesc::State::IMPLICIT_ACTIVE:
      tx.release_implicit_savepoint(savepoint);
      if (!tx.has_implicit_savepoint() // to first savepoint
          && tx.active_scn_ >= savepoint  // rollback all dirty state
          && !tx.has_extra_state_()) {    // hasn't explicit savepoint or serializable snapshot
        reset_tx = need_rollback_(clean_policy);
        reset_active_scn = !reset_tx;
        normal_rollback = need_clean_writeset_(clean_policy);
      } else {
        normal_rollback = true;
      }
      break;
    default:
      ret = OB_TRANS_INVALID_STATE; // FIXME, better error code
    }
  }

  if (normal_rollback) {
    ObTxWriteState *part = NULL;
    if (OB_FAIL(ret)) {
    } else if (tx.flags_.WRITE_STATE_INCOMPLETE_) {
      ret = OB_TRANS_NEED_ROLLBACK;
      TRANS_LOG(WARN, "txn write state state incomplete, txn will rollback internally", K(ret));
    } else if (OB_FAIL(find_write_state_after_savepoint_(tx, part, savepoint))) {
    } else if (OB_FAIL(rollback_savepoint_(tx,
                                           part,
                                           savepoint,
                                           expire_ts))) {
    }
    // reset tx ignore rollback ret
    if (reset_tx) {
    } else if (OB_FAIL(ret)) {
      TRANS_LOG(WARN, "rollback savepoint fail, abort tx",
                K(ret), K(savepoint), KPC(part), K(tx));
      // advance op_sequence to reject further rollback resp messsages
      tx.inc_op_sn();
      abort_tx_(tx, ObTxAbortCause::SAVEPOINT_ROLLBACK_FAIL);
    } else {
      if (reset_active_scn) {
        tx.active_scn_.reset();
      }
       /*
       * advance txn op_seqence to barrier duplicate rollback msg
       * otherwise, rollback may erase following write
       */
      tx.inc_op_sn();
    }
  }
  /*
   * reset tx state from IMPLICIT_ACTIVE to IDLE
   * in progress tx was cleaned up via abort
   * but resources hold before beginning of tx
   * were reserved
   */
  if (reset_tx) {
    if (OB_FAIL(abort_tx_(tx, ObTxAbortCause::IMPLICIT_ROLLBACK,
              false /*don't cleanup resource*/))) {
    } else if (OB_FAIL(start_epoch_(tx))) {
    }
  }
  int64_t elapsed_us = ObTimeUtility::current_time() - start_ts;
#ifndef NDEBUG
  TRANS_LOG(INFO, "rollback to implicit savepoint", K(ret), K(savepoint), K(elapsed_us), K(tx));
#else
  if (OB_FAIL(ret)) {
  }
#endif
  ObTransTraceLog &tlog = tx.get_tlog();
  REC_TRANS_TRACE_EXT(&tlog, rollback_global_implicit_savepoint,
                      OB_Y(ret), OB_ID(savepoint), savepoint.cast_to_int(), OB_Y(expire_ts),
                      OB_ID(time_used), elapsed_us,
                      OB_ID(tag1), reset_tx,
                      OB_ID(opid), tx.op_sn_,
                      OB_ID(ref), tx.get_ref(),
                      OB_ID(thread_id), GETTID());
  return ret;
}

int ObTransService::sync_rollback_to_savepoint_(ObTxCtx *part_ctx,
                                                 const ObTxSEQ savepoint,
                                                 const int64_t op_sn,
                                                 const int64_t tx_seq_base,
                                                 const int64_t expire_ts,
                                                 const ObTxSEQ specified_from_scn)
{
  int ret = OB_SUCCESS;
  int64_t retry_cnt = 0;
  bool blockable = expire_ts > 0;
  do {
    ret = part_ctx->rollback_to_savepoint(op_sn,
                                          specified_from_scn,
                                          savepoint,
                                          tx_seq_base);
    if ((OB_NEED_RETRY == ret || OB_EAGAIN == ret || OB_TX_NOLOGCB == ret) && blockable) {
      if (ObTimeUtility::current_time() >= expire_ts) {
        ret = OB_TIMEOUT;
        TRANS_LOG(WARN, "can not retry rollback_to because of timeout", K(ret), K(retry_cnt));
      } else {
        if (retry_cnt % 5 == 0) {
          TRANS_LOG(WARN, "retry rollback_to savepoint in ctx", K(ret), K(retry_cnt));
        }
        retry_cnt += 1;
        ob_usleep(50 * 1000);
      }
    }
  } while ((OB_NEED_RETRY == ret || OB_EAGAIN == ret || OB_TX_NOLOGCB == ret) && blockable);
#ifndef NDEBUG
  TRANS_LOG(INFO, "rollback to savepoint sync", K(ret),
            K(part_ctx->get_trans_id()), K(retry_cnt),
            K(op_sn), K(savepoint), K(expire_ts));
#else
  if (OB_FAIL(ret)) {
  }
#endif
  return ret;
}

int ObTransService::create_explicit_savepoint(ObTxDesc &tx,
                                              const ObString &savepoint)
{
  int ret = OB_SUCCESS;
  ObSpinLockGuard guard(tx.lock_);
  tx.inc_op_sn();
  const ObTxSEQ scn = tx.inc_and_get_tx_seq(0);
  ObTxSavePoint sp;
  if (OB_SUCC(sp.init(scn, savepoint))) {
    if (OB_FAIL(tx.savepoints_.push_back(sp))) {
    } else if (tx.access_mode_ != ObTxAccessMode::FENCED_RD_ONLY
               && !tx.tx_id_.is_valid()
               && OB_FAIL(tx_desc_mgr_.add(tx))) {
      TRANS_LOG(WARN, "add tx to mgr failed", K(ret), K(tx));
      tx.savepoints_.pop_back();
    } else {
      // impl move semantic of savepoint
      ARRAY_FOREACH_X(tx.savepoints_, i, cnt, i != cnt - 1) {
        ObTxSavePoint &it = tx.savepoints_.at(cnt - 2 - i);
        if (it.is_stash()) { break; }
        if (it.is_savepoint() && it.name_ == savepoint) {
          it.release();
          break; // assume only one if exist
        }
      }
    }
  }
  tx.state_change_flags_.EXTRA_CHANGED_ = true;
  ObTransTraceLog &tlog = tx.get_tlog();
  REC_TRANS_TRACE_EXT(&tlog, create_explicit_savepoint, OB_Y(ret),
                      OB_ID(savepoint), savepoint,
                      OB_ID(seq_no), scn.cast_to_int(),
                      OB_ID(opid), tx.op_sn_,
                      OB_ID(ref), tx.get_ref(),
                      OB_ID(thread_id), GETTID());
  return ret;
}

// impl note
// 1. find savepoint Node
// 3. do rollback savepoint by savepoint No.
// 2. invalidate savepoint and snapshot after the savepoint Node.
int ObTransService::rollback_to_explicit_savepoint(ObTxDesc &tx,
                                                   const ObString &savepoint,
                                                   const int64_t expire_ts)
{
  int ret = OB_SUCCESS;
  int64_t start_ts = ObTimeUtility::current_time();
  ObTxSEQ sp_scn;
  ObSpinLockGuard guard(tx.lock_);
  if (OB_SUCC(tx_sanity_check_(tx))) {
    tx.inc_op_sn();
    ARRAY_FOREACH_N(tx.savepoints_, i, cnt) {
      const ObTxSavePoint &it = tx.savepoints_.at(cnt - 1 - i);
      if (it.is_stash()) { break; }
      if (it.is_savepoint() && it.name_ == savepoint) {
        sp_scn = it.scn_;
        break;
      }
    }
    if (!sp_scn.is_valid()) {
      ret = OB_SAVEPOINT_NOT_EXIST;
      TRANS_LOG(WARN, "savepoint not exist", K(ret), K(savepoint), K_(tx.savepoints));
    }
  }
  if (OB_SUCC(ret)) {
    ObTxWriteState *part = NULL;
    if (OB_FAIL(find_write_state_after_savepoint_(tx, part, sp_scn))) {
    } else if (OB_FAIL(rollback_savepoint_(tx,
                                           part,
                                           sp_scn,
                                           expire_ts))) {
    }
    if (OB_FAIL(ret)) {
      TRANS_LOG(WARN, "rollback savepoint fail, abort tx",
                K(ret), K(savepoint), K(sp_scn), KPC(part), K(tx));
      abort_tx_(tx, ObTxAbortCause::SAVEPOINT_ROLLBACK_FAIL);
    }
  }
  if (OB_SUCC(ret)) {
    // rollback savepoints > sp (note, current savepoint with sp won't be released)
    ARRAY_FOREACH_N(tx.savepoints_, i, cnt) {
      ObTxSavePoint &it = tx.savepoints_.at(cnt - 1 - i);
      if (it.scn_ > sp_scn) {
        it.rollback();
      }
      const bool is_stack_top = tx.savepoints_.count() == (cnt - i);
      if (is_stack_top && !it.is_valid()) {
        tx.savepoints_.pop_back();
      }
    }
  }
  int64_t elapsed_us = ObTimeUtility::current_time() - start_ts;
  ObTransTraceLog &tlog = tx.get_tlog();
  REC_TRANS_TRACE_EXT(&tlog, rollback_explicit_savepoint, OB_Y(ret),
                      OB_ID(id), savepoint,
                      OB_ID(savepoint), sp_scn.cast_to_int(),
                      OB_ID(time_used), elapsed_us,
                      OB_ID(opid), tx.op_sn_,
                      OB_ID(ref), tx.get_ref(),
                      OB_ID(thread_id), GETTID());
  return ret;
}

// impl note
// registered snapshot keep valid
int ObTransService::release_explicit_savepoint(ObTxDesc &tx, const ObString &savepoint)
{
  int ret = OB_SUCCESS;
  bool hit = false;
  ObTxSEQ sp_id;
  ObSpinLockGuard guard(tx.lock_);
  if (OB_SUCC(tx_sanity_check_(tx))) {
    tx.inc_op_sn();
    ARRAY_FOREACH_N(tx.savepoints_, i, cnt) {
      ObTxSavePoint &it = tx.savepoints_.at(cnt - 1 - i);
      if (it.is_savepoint() && it.name_ == savepoint) {
        hit = true;
        sp_id = it.scn_;
        break;
      }
      if (it.is_stash()) { break; }
    }
    if (!hit) {
      ret = OB_SAVEPOINT_NOT_EXIST;
      TRANS_LOG(WARN, "release savepoint fail", K(ret), K(savepoint), K(tx));
    } else {
      ARRAY_FOREACH_N(tx.savepoints_, i, cnt) {
        ObTxSavePoint &it = tx.savepoints_.at(cnt - 1 - i);
        if (it.is_savepoint() && it.scn_ >= sp_id) {
          it.release();
        }
        const bool is_stack_top = tx.savepoints_.count() == (cnt - i);
        if (is_stack_top && !it.is_valid()) {
          tx.savepoints_.pop_back();
        }
      }
    }
  }
  ObTransTraceLog &tlog = tx.get_tlog();
  REC_TRANS_TRACE_EXT(&tlog, release_explicit_savepoint, OB_Y(ret),
                      OB_ID(savepoint), savepoint,
                      OB_ID(seq_no), sp_id.cast_to_int(),
                      OB_ID(opid), tx.op_sn_);
  return ret;
}

int ObTransService::create_stash_savepoint(ObTxDesc &tx, const ObString &name)
{
  int ret = OB_SUCCESS;
  ObSpinLockGuard guard(tx.lock_);
  tx.inc_op_sn();
  const ObTxSEQ seq_no = tx.inc_and_get_tx_seq(0);
  ObTxSavePoint sp;
  if (OB_SUCC(sp.init(seq_no, name, true))) {
    if (OB_FAIL(tx.savepoints_.push_back(sp))) {
    }
  }
  REC_TRANS_TRACE_EXT(&tx.tlog_, create_stash_savepoint, OB_Y(ret),
                      OB_ID(savepoint), name,
                      OB_ID(seq_no), seq_no.cast_to_int(),
                      OB_ID(opid), tx.op_sn_);
  return ret;
}

int ObTransService::rollback_savepoint_(ObTxDesc &tx,
                                        ObTxWriteState *part,
                                        const ObTxSEQ savepoint,
                                        int64_t expire_ts)
{
  int ret = OB_SUCCESS;
  expire_ts = std::min(expire_ts, tx.get_expire_ts());
  if (OB_ISNULL(part)) {
    TRANS_LOG(INFO, "empty rollback write state set", K(tx), K(savepoint));
  } else {
    const ObTransID tx_id = tx.tx_id_;
    const int64_t op_sn = tx.op_sn_;
    const int64_t seq_base = tx.seq_base_;
    ret = rollback_tx_to_savepoint_(tx_id,
                                    op_sn,
                                    savepoint,
                                    seq_base,
                                    tx,
                                    ObTxSEQ::INVL(),
                                    -1/*non-blocking*/);
    if (common_retryable_error_(ret)) {
      TRANS_LOG(INFO, "fallback to blocking rollback", K(ret), K(savepoint), KPC(part), K(tx));
      if (OB_UNLIKELY(tx.flags_.INTERRUPTED_)) {
        ret = OB_ERR_INTERRUPTED;
        tx.clear_interrupt();
        TRANS_LOG(WARN, "interrupted", K(ret), K(tx));
      } else {
        const ObTxDesc::State save_state = tx.state_;
        tx.state_ = ObTxDesc::State::ROLLBACK_SAVEPOINT;
        tx.flags_.BLOCK_ = true;
        tx.lock_.unlock();
        const int64_t retry_expire_ts = std::max(ObTimeUtility::current_time() + 200 * 1000, expire_ts);
        int64_t retry_cnt = 0;
        do {
          ret = rollback_tx_to_savepoint_(tx_id,
                                          op_sn,
                                          savepoint,
                                          seq_base,
                                          tx,
                                          ObTxSEQ::INVL(),
                                          -1/*non-blocking*/);
          if (common_retryable_error_(ret)) {
            if (tx.flags_.INTERRUPTED_) {
              ret = OB_ERR_INTERRUPTED;
              TRANS_LOG(WARN, "rollback was interrupted", K(ret), K(tx_id), K(retry_cnt));
            } else if (ObTimeUtility::current_time() >= retry_expire_ts) {
              ret = OB_TIMEOUT;
              TRANS_LOG(WARN, "can not retry rollback_to because of timeout", K(ret), K(tx_id), K(retry_cnt));
            } else {
              if (retry_cnt % 5 == 0) {
                TRANS_LOG(WARN, "retry blocking rollback", K(ret), K(tx_id), K(retry_cnt));
              }
              ++retry_cnt;
              const int64_t retry_interval = std::min<int64_t>(10 * 1000 * retry_cnt, 50 * 1000);
              ob_usleep(retry_interval);
            }
          }
        } while (common_retryable_error_(ret));
        tx.lock_.lock();
        if (OB_SUCC(ret) && tx.is_tx_active()) {
          tx.state_ = save_state;
        }
        if (OB_SUCC(ret) && tx.flags_.INTERRUPTED_) {
          ret = OB_ERR_INTERRUPTED;
          TRANS_LOG(WARN, "rollback savepoint was interrupted", K(ret));
        }
        tx.clear_interrupt();
        tx.flags_.BLOCK_ = false;
      }
    }
    if (OB_FAIL(ret)) {
    } else {
      tx.update_clean_write_state();
    }
  }
  if (OB_TIMEOUT == ret && ObTimeUtility::current_time() >= tx.get_expire_ts()) {
     ret = OB_TRANS_TIMEOUT;
  }
  if (OB_SUCC(ret) && OB_NOT_NULL(part)) {
    tx.finish_write_state_rollback_(part, savepoint);
  }
  return ret;
}

/**
 * rollback_tx_to_savepoint - rollback a transaction to a savepoint
 * @op_sn:                    the operator sequence inner transaction
 *                            used to keep operation order correctly
 * @tx:                       transaction descriptor used to create the local tx ctx
 * @expire_ts:                an expire_ts used if retry was required
 *                            -1 if non-blocking desired
 *
 */
int ObTransService::rollback_tx_to_savepoint_(const ObTransID &tx_id,
                                              const int64_t op_sn,
                                              const ObTxSEQ savepoint,
                                              const int64_t tx_seq_base,
                                              const ObTxDesc &tx,
                                              const ObTxSEQ from_scn,
                                              int64_t expire_ts)
{
  int ret = OB_SUCCESS;
  ObTxCtx *ctx = NULL;
  if (OB_FAIL(get_tx_ctx_(tx_id, ctx))) {
    if (OB_TRANS_CTX_NOT_EXIST == ret) {
      int64_t tx_state = ObTxData::RUNNING;
      share::SCN commit_version;
      if (OB_FAIL(get_tx_state_from_tx_table_(tx_id, tx_state, commit_version))) {
        if (OB_TRANS_CTX_NOT_EXIST == ret) {
          bool ctx_exist = false;
          if (OB_FAIL(create_tx_ctx_(tx, ctx, ctx_exist))) {
          }
        } else {
          TRANS_LOG(WARN, "get tx state from tx table fail", K(ret), K(tx_id));
        }
      } else {
        switch (tx_state) {
        case ObTxData::COMMIT:
          ret = OB_TRANS_COMMITED;
          break;
        case ObTxData::ABORT:
          ret = OB_TRANS_KILLED;
          break;
        case ObTxData::RUNNING:
        default:
          ret = OB_ERR_UNEXPECTED;
          TRANS_LOG(WARN, "tx in-progress but ctx miss", K(ret), K(tx_state), K(tx_id));
        }
      }
    } else {
      TRANS_LOG(WARN, "get transaction context error", K(ret), K(tx_id));
    }
  }
  if (OB_SUCC(ret) && OB_NOT_NULL(ctx)) {
    if (OB_FAIL(sync_rollback_to_savepoint_(ctx,
                                             savepoint,
                                             op_sn,
                                             tx_seq_base,
                                             expire_ts,
                                             from_scn))) {
    }
  }
  if (OB_NOT_NULL(ctx)) {
    revert_tx_ctx_(ctx);
  }
  return ret;
}

int ObTransService::merge_tx_state(ObTxDesc &to, const ObTxDesc &from)
{
  int ret = to.merge_exec_info_with(from);
  ObTransTraceLog &tlog = to.get_tlog();
  REC_TRANS_TRACE_EXT(&tlog, merge_tx_state, OB_Y(ret),
                      OB_ID(to), (void*)&to,
                      OB_ID(from), (void*)&from,
                      OB_ID(opid), to.op_sn_,
                      OB_ID(thread_id), GETTID());
  return ret;
}
int ObTransService::get_tx_exec_result(ObTxDesc &tx, ObTxExecResult &exec_info)
{
  int ret = tx.get_inc_exec_info(exec_info);
  return ret;
}
int ObTransService::add_tx_exec_result(ObTxDesc &tx, const ObTxExecResult &exec_info)
{
  int ret = tx.add_exec_info(exec_info);
  ObTransTraceLog &tlog = tx.get_tlog();
  REC_TRANS_TRACE_EXT(&tlog, add_tx_exec_result, OB_ID(opid), tx.op_sn_,
                      OB_ID(flag), exec_info.is_incomplete(),
                      OB_ID(thread_id), GETTID());
  return ret;
}

/*
 * tx_post_terminate - cleanup resource after tx terminated
 *
 * after tx committed/aborted/rollbacked
 * tx resource need to be released, we do it here
 */
void ObTransService::tx_post_terminate_(ObTxDesc &tx)
{
  // invalid registered snapshot
  if (tx.state_ == ObTxDesc::State::ABORTED || tx.is_commit_unsucc()) {
    invalid_registered_snapshot_(tx);
  } else if (tx.state_ == ObTxDesc::State::COMMITTED) {
    process_registered_snapshot_on_commit_(tx);
  }
  // release all savepoints
  tx.min_implicit_savepoint_.reset();
  tx.savepoints_.reset();
  // reset snapshot
  if (tx.snapshot_version_.is_valid()) {
    tx.snapshot_version_.reset();
    tx.snapshot_scn_.reset();
    tx.state_change_flags_.EXTRA_CHANGED_ = true;
  }
}

int ObTransService::start_epoch_(ObTxDesc &tx)
{
  int ret = OB_SUCCESS;
  if (!tx.is_terminated()) {
    ret = OB_TRANS_INVALID_STATE;
    TRANS_LOG(WARN, "unexpected tx state to start new epoch", K(ret), K(tx));
  } else {
    tx.inc_op_sn();
    if (tx.flags_.RELEASED_) {
      ret = OB_ERR_UNEXPECTED;
      TRANS_LOG(ERROR, "tx released, cannot start new epoch", K(ret), K(tx));
    } else if (OB_FAIL(tx_desc_mgr_.remove(tx))) {
    } else if (OB_FAIL(tx.switch_to_idle())) {
    }
#ifndef NDEBUG
    TRANS_LOG(INFO, "tx start new epoch", K(ret), K(tx));
#endif
  }
  ObTransTraceLog &tlog = tx.get_tlog();
  int tlog_truncate_cnt = 0;
  if (OB_SUCC(ret) && tlog.count() > 50) {
    tlog_truncate_cnt = tlog.count() - 10;
    tlog.set_count(10);
  }
  REC_TRANS_TRACE_EXT(&tlog, start_epoch, OB_Y(ret), OB_ID(opid), tx.op_sn_, OB_ID(tag1), tlog_truncate_cnt);
  return ret;
}

int ObTransService::release_tx_ref(ObTxDesc &tx)
{
  return tx_desc_mgr_.release_tx_ref(&tx);
}


OB_INLINE int ObTransService::tx_sanity_check_(ObTxDesc &tx)
{
  int ret = OB_SUCCESS;
  if (tx.expire_ts_ <= ObClockGenerator::getClock()) {
    ret = OB_TRANS_TIMEOUT;
  } else if (tx.flags_.BLOCK_) {
    ret = OB_NOT_SUPPORTED;
    TRANS_LOG(WARN, "tx is blocked in other busy work", K(ret), K(tx));
  } else {
    switch(tx.state_) {
    case ObTxDesc::State::IDLE:
    case ObTxDesc::State::ACTIVE:
    case ObTxDesc::State::IMPLICIT_ACTIVE:
      if (tx.flags_.WRITE_STATE_ABORTED_) {
        TRANS_LOG(WARN, "write state was aborted, abort tx now");
        abort_tx_(tx, tx.abort_cause_);
        // go through
      } else {
        break;
      }
    case ObTxDesc::State::ABORTED:
      {
        const int cause = tx.abort_cause_;
        ret = cause < 0 ? cause : OB_TRANS_NEED_ROLLBACK;
        const char *err_name = cause < 0 ? common::ob_error_name(cause) : ObTxAbortCauseNames::of(cause);
        TRANS_LOG(WARN, "trans has been aborted", "caused_by", err_name, K(ret), "txid", tx.tx_id_);
      }
      break;
    case ObTxDesc::State::COMMITTED:
      ret = OB_TRANS_COMMITED;
      break;
    case ObTxDesc::State::ROLLED_BACK:
      ret = OB_TRANS_ROLLBACKED;
      break;
    case ObTxDesc::State::COMMIT_TIMEOUT:
    case ObTxDesc::State::COMMIT_UNKNOWN:
      ret = OB_TRANS_HAS_DECIDED;
      break;
    default:
      ret = OB_NOT_SUPPORTED; // FIXME: refine errno
    }
  }
  if (OB_FAIL(ret)) {
    TRANS_LOG(WARN, "tx state insanity", K(ret), K(tx));
    tx.print_trace_();
  }
  return ret;
}

} // transaction

namespace data_plane
{

ObITransactionService *query_transaction_service()
{
  return ::oceanbase::share::server_service<::oceanbase::transaction::ObTransService>();
}

bool tx_desc_is_explicit(const transaction::ObTxDesc *desc)
{
  return OB_NOT_NULL(desc) && desc->is_explicit();
}

bool tx_desc_is_in_tx(const transaction::ObTxDesc *desc)
{
  return OB_NOT_NULL(desc) && desc->is_in_tx();
}

bool tx_desc_has_temporary_tables(const transaction::ObTxDesc *desc)
{
  return OB_NOT_NULL(desc) && desc->with_temporary_table();
}

transaction::ObTransID tx_desc_id(const transaction::ObTxDesc *desc)
{
  return OB_NOT_NULL(desc) ? desc->get_tx_id() : transaction::ObTransID();
}

bool tx_desc_in_tx_for_free_route(transaction::ObTxDesc *desc)
{
  return OB_NOT_NULL(desc) && desc->in_tx_for_free_route();
}

bool tx_desc_is_read_only(const transaction::ObTxDesc *desc)
{
  return OB_NOT_NULL(desc) && desc->is_in_tx() && desc->is_rdonly();
}

bool tx_desc_is_committing(transaction::ObTxDesc *desc)
{
  return OB_NOT_NULL(desc) && desc->is_committing();
}

bool tx_desc_in_tx_or_has_extra_state(const transaction::ObTxDesc *desc)
{
  return OB_NOT_NULL(desc) && desc->in_tx_or_has_extra_state();
}

bool tx_desc_is_clean(const transaction::ObTxDesc *desc)
{
  return OB_ISNULL(desc) || desc->is_clean();
}

uint32_t tx_desc_session_id(const transaction::ObTxDesc *desc)
{
  return OB_NOT_NULL(desc) ? desc->get_session_id() : 0;
}

int64_t tx_desc_seq_base(const transaction::ObTxDesc *desc)
{
  return OB_NOT_NULL(desc) ? desc->get_seq_base() : 0;
}

uint64_t tx_desc_operation_sequence(const transaction::ObTxDesc *desc)
{
  return OB_NOT_NULL(desc) ? desc->get_op_sn() : 0;
}

int tx_desc_serialize(const transaction::ObTxDesc *desc,
                      char *buf,
                      int64_t buf_len,
                      int64_t &pos)
{
  return OB_NOT_NULL(desc) ? desc->serialize(buf, buf_len, pos)
                           : OB_INVALID_ARGUMENT;
}

int64_t tx_desc_serialize_size(const transaction::ObTxDesc *desc)
{
  return OB_NOT_NULL(desc) ? desc->get_serialize_size() : 0;
}

share::SCN tx_desc_snapshot_version(transaction::ObTxDesc *desc)
{
  return OB_NOT_NULL(desc) ? desc->get_tx_snapshot_version()
                           : share::SCN::invalid_scn();
}

bool tx_desc_uses_rr_or_serializable(transaction::ObTxDesc *desc)
{
  return OB_NOT_NULL(desc) && desc->is_RR_or_SERIAL_isolevel();
}

bool tx_desc_uses_read_committed(transaction::ObTxDesc *desc)
{
  return OB_NOT_NULL(desc) && desc->is_RC_isolevel();
}

common::ObAddr tx_desc_scheduler(const transaction::ObTxDesc *desc)
{
  return OB_NOT_NULL(desc) ? desc->get_addr() : common::ObAddr();
}

int64_t tx_desc_active_timestamp(const transaction::ObTxDesc *desc)
{
  return OB_NOT_NULL(desc) ? desc->get_active_ts() : 0;
}

bool tx_desc_contains_savepoint(transaction::ObTxDesc *desc,
                                const common::ObString &savepoint)
{
  return OB_NOT_NULL(desc) && desc->contain_savepoint(savepoint);
}

bool tx_desc_is_ended(transaction::ObTxDesc *desc)
{
  return OB_NOT_NULL(desc) && desc->is_tx_end();
}

bool tx_desc_is_timed_out(transaction::ObTxDesc *desc)
{
  return OB_NOT_NULL(desc) && desc->is_tx_timeout();
}

void dump_tx_desc_trace(transaction::ObTxDesc *desc)
{
  if (OB_NOT_NULL(desc)) {
    desc->dump_and_print_trace();
  }
}

ObTxCommitTimeoutState cancel_timed_out_tx_commit(
    transaction::ObTxDesc *desc,
    transaction::ObITxCallback *&callback)
{
  ObTxCommitTimeoutState state = ObTxCommitTimeoutState::NONE;
  callback = nullptr;
  if (OB_NOT_NULL(desc) && desc->is_committing()) {
    if (desc->is_tx_timeout()) {
      callback = desc->cancel_commit_cb();
      state = ObTxCommitTimeoutState::TRANSACTION;
    } else if (desc->is_tx_commit_timeout()) {
      callback = desc->cancel_commit_cb();
      state = ObTxCommitTimeoutState::STATEMENT;
    }
  }
  return state;
}

int64_t ObTxDescLogView::to_string(char *buf, const int64_t buf_len) const
{
  int64_t pos = 0;
  if (OB_LIKELY(OB_NOT_NULL(buf)) && OB_LIKELY(buf_len > 0)) {
    if (OB_ISNULL(desc_)) {
      (void)common::logdata_printf(buf, buf_len, pos, "NULL");
    } else {
      pos += desc_->to_string(buf, buf_len);
    }
  }
  return pos;
}

ObTxWeakReadPolicy evaluate_tx_weak_read_policy(const transaction::ObTxDesc &tx)
{
  ObTxWeakReadPolicy policy = ObTxWeakReadPolicy::ALLOW;
  if (!tx.is_clean()) {
    policy = ObTxWeakReadPolicy::FORCE_STRONG;
  } else if (transaction::is_RR_or_SERIAL_isolevel(tx.get_isolation_level())) {
    policy = ObTxWeakReadPolicy::REJECT_ISOLATION;
  }
  return policy;
}

void prepare_tx_for_statement(transaction::ObTxDesc &tx)
{
  tx.clear_interrupt();
}

void initialize_plain_insert_snapshot(
    const transaction::ObTxDesc &tx,
    transaction::ObTxReadSnapshot &snapshot)
{
  snapshot.init_none_read();
  snapshot.core_.tx_id_ = tx.get_tx_id();
  snapshot.core_.scn_ = tx.get_tx_seq();
}

bool tx_owns_local_temporary_tables(
    const transaction::ObTxDesc *tx,
    const common::ObAddr &local_addr)
{
  return OB_NOT_NULL(tx)
      && tx->with_temporary_table()
      && tx->get_addr() == local_addr;
}

int allocate_tx_branches(transaction::ObTxDesc &tx,
                         int64_t count,
                         int16_t &first_branch_id)
{
  return tx.alloc_branch_id(count, first_branch_id);
}

int prepare_tx_for_autocommit_retry(transaction::ObTxDesc &tx)
{
  return tx.clear_state_for_autocommit_retry();
}

static int transaction_abort_cause_(ObTxAbortReason reason)
{
  int cause = common::OB_INVALID_ARGUMENT;
  switch (reason) {
    case ObTxAbortReason::INCOMPLETE_RESULT:
      cause = transaction::ObTxAbortCause::TX_RESULT_INCOMPLETE;
      break;
    case ObTxAbortReason::SESSION_DISCONNECT:
      cause = transaction::ObTxAbortCause::SESSION_DISCONNECT;
      break;
  }
  return cause;
}

const char *describe_transaction_abort_error(int error_code)
{
  return error_code >= 0
      ? transaction::ObTxAbortCauseNames::of(error_code)
      : common::ob_error_name(error_code);
}

const char *describe_transaction_abort_reason(ObTxAbortReason reason)
{
  return transaction::ObTxAbortCauseNames::of(transaction_abort_cause_(reason));
}

int abort_transaction_for_error(transaction::ObTxDesc &tx, int error_code)
{
  int ret = common::OB_SUCCESS;
  ObITransactionService *txs = query_transaction_service();
  if (OB_ISNULL(txs)) {
    ret = common::OB_ERR_UNEXPECTED;
    TRANS_LOG(ERROR, "transaction service is null while aborting transaction",
              K(ret), K(error_code), "tx_id", tx.get_tx_id());
  } else {
    ret = txs->abort_tx(tx, error_code);
  }
  return ret;
}

int abort_transaction(transaction::ObTxDesc &tx, ObTxAbortReason reason)
{
  int ret = common::OB_SUCCESS;
  ObITransactionService *txs = query_transaction_service();
  if (OB_ISNULL(txs)) {
    ret = common::OB_ERR_UNEXPECTED;
    TRANS_LOG(ERROR, "transaction service is null while aborting transaction",
              K(ret), K(reason), "tx_id", tx.get_tx_id());
  } else {
    ret = txs->abort_tx(tx, transaction_abort_cause_(reason));
  }
  return ret;
}

void force_release_tx_when_tenant_gone(transaction::ObTxDesc &tx)
{
  transaction::ObTransService::force_release_tx_when_session_destroy(tx);
}

int clone_tx_desc(common::ObIAllocator &allocator,
                  transaction::ObTxDesc *source,
                  transaction::ObTxDesc *&clone)
{
  int ret = OB_SUCCESS;
  int64_t serialized_length = 0;
  int64_t serialize_pos = 0;
  int64_t deserialize_pos = 0;
  void *buffer = nullptr;
  clone = nullptr;
  if (OB_ISNULL(source)) {
    ret = OB_ERR_UNEXPECTED;
    TRANS_LOG(WARN, "unexpected null transaction descriptor", K(ret));
  } else if (FALSE_IT(serialized_length = source->get_serialize_size())) {
  } else if (OB_ISNULL(buffer = allocator.alloc(serialized_length))) {
    ret = OB_ALLOCATE_MEMORY_FAILED;
    TRANS_LOG(WARN, "failed to allocate transaction descriptor buffer",
              K(ret), K(serialized_length));
  } else if (OB_FAIL(source->serialize(
                 static_cast<char *>(buffer), serialized_length, serialize_pos))) {
  } else if (OB_FAIL(::oceanbase::share::server_service<::oceanbase::transaction::ObTransService>()->acquire_tx(
                 static_cast<const char *>(buffer), serialize_pos,
                 deserialize_pos, clone))) {
  } else if (OB_ISNULL(clone) || serialize_pos != deserialize_pos) {
    ret = OB_ERR_UNEXPECTED;
    TRANS_LOG(WARN, "invalid cloned transaction descriptor",
              K(ret), KP(clone), K(serialize_pos), K(deserialize_pos));
    if (OB_NOT_NULL(clone)) {
      ::oceanbase::share::server_service<::oceanbase::transaction::ObTransService>()->release_tx(*clone);
      clone = nullptr;
    }
  }
  return ret;
}

void release_tx_desc(transaction::ObTxDesc *&desc)
{
  if (OB_NOT_NULL(desc)) {
    ::oceanbase::share::server_service<::oceanbase::transaction::ObTransService>()->release_tx(*desc);
    desc = nullptr;
  }
}

} // namespace data_plane
} // namespace
#undef TXN_API_SANITY_CHECK_FOR_TXN_FREE_ROUTE
