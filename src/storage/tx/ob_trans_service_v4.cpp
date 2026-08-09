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

#include "src/storage/tx/ob_tx_ctx.h"
#include "share/rc/ob_server_runtime.h"
#include "storage/tx/ob_trans_id_service.h"
#include "storage/tx_storage/ob_ls_service.h"

/*  interface(s)  */
namespace oceanbase {
namespace transaction {

using namespace memtable;
using namespace share;

static const int64_t POST_COMMIT_REQ_RETRY_INTERVAL = 100 * 1000; // 100msg

int ObTransService::create_ls(ObLS &ls,
                              ObITxLogParam *param,
                              ObITxLogAdapter *log_adapter)
{
  int ret = OB_SUCCESS;
  ObTableHandleV2 lock_memtable;
  ObTxTable *tx_table = nullptr;
  if (IS_NOT_INIT) {
    ret = OB_NOT_INIT;
    TRANS_LOG(WARN, "ObTransService not inited", K(ret), K(*this));
  } else if (OB_UNLIKELY(!is_running_)) {
    ret = OB_NOT_RUNNING;
    TRANS_LOG(WARN, "ObTransService is not running", K(ret), K(*this));
  } else if (OB_ISNULL(tx_table = ls.get_tx_table())) {
    ret = OB_ERR_UNEXPECTED;
    TRANS_LOG(WARN, "get tx table fail", K(ret));
  } else if (OB_FAIL(tx_ctx_mgr_.create_context_manager(tx_table,
                                                        ls.get_lock_table(),
                                                        *ls.get_tx_svr(),
                                                        param,
                                                        log_adapter))) {
  } else {
    // do nothing
  }
  if (OB_FAIL(ret)) {
  } else {
    TRANS_LOG(INFO, "create transaction context manager success");
  }

  return ret;
}

int ObTransService::remove_ls(const bool graceful)
{
  int ret = OB_SUCCESS;

  if (IS_NOT_INIT) {
    ret = OB_NOT_INIT;
    TRANS_LOG(WARN, "ObTransService not inited", K(ret));
  } else if (OB_UNLIKELY(!is_running_)) {
    ret = OB_NOT_RUNNING;
    TRANS_LOG(WARN, "ObTransService is not running", K(ret));
  } else if (OB_FAIL(tx_ctx_mgr_.remove_context_manager(graceful))) {
  } else {
    TRANS_LOG(INFO, "remove transaction context manager success", K(graceful));
  }

  return ret;
}

int ObTransService::acquire_tx(const char* buf,
                               const int64_t len,
                               int64_t &pos,
                               ObTxDesc *&tx)
{
  int ret = OB_SUCCESS;
  if (OB_FAIL(tx_desc_mgr_.alloc(tx))) {
  } else if (OB_FAIL(tx->deserialize(buf, len, pos))) {
    tx_desc_mgr_.revert(*tx);
    tx = NULL;
    TRANS_LOG(WARN, "desrialize txDesc fail", K(ret),
              K(len),K(pos), K(buf), KPC(this));
  } else if (OB_UNLIKELY(DATA_CURRENT_VERSION != tx->data_version_)) {
    ret = OB_NOT_SUPPORTED;
    TRANS_LOG(WARN, "transaction descriptor data version mismatch", K(ret), KPC(tx),
              "current_data_version", DATA_CURRENT_VERSION);
    tx_desc_mgr_.revert(*tx);
    tx = NULL;
  } else {
    tx->flags_.SHADOW_ = true;
  }
  if (tx) {
    REC_TRANS_TRACE_EXT(&tx->get_tlog(), deserialize,
                        OB_ID(addr), (void*)tx,
                        OB_ID(txid), tx->tx_id_);
  }
  return ret;
}

/*
 * do_commit_tx_ - the real work of commit tx
 *
 * steps:
 * 1. prepare commit info
 * 2. try local call optimization, if fail fallback to step 3
 * 3. post commit message to the transaction context
 *
 * If any failures occurred:
 * - if no message has been sent, state can be revert to
 *   ACTIVE, and the caller can retry
 * - if any message has been sent, a prepose timer task will
 *   drive the retry in background, the commit return success
 *
 * Return:
 * OB_SUCCESS - either local commit started or
 *              commit retry task has been registred
 * OB_XXX     - try local commit failed and can not been
 *              fallback to commit retry via send message
 */
int ObTransService::do_commit_tx_(ObTxDesc &tx,
                                  const int64_t expire_ts,
                                  ObITxCallback &cb,
                                  SCN &commit_version)
{
  int ret = OB_SUCCESS;
  tx.set_commit_cb(&cb);
  tx.commit_expire_ts_ = expire_ts;
  if (OB_FAIL(tx.commit_task_.init(&tx, this))) {
  } else if (OB_SUCC(local_ls_commit_tx_(tx.tx_id_,
                                         expire_ts,
                                         tx.op_sn_,
                                         SCN::max_scn(),
                                         commit_version))
             || !commit_need_retry_(ret)) {
    if (OB_FAIL(ret)) {
    } else {
    }
  } else {
    // get gts cache as commit start scn
    if (OB_FAIL(ts_mgr_->get_gts(tx.commit_start_scn_))) {
    }
    if (OB_FAIL(do_commit_tx_slowpath_(tx))) {
    } else {
    }
  }
  // start commit fail
  if (OB_FAIL(ret)) {
    tx.cancel_commit_cb();
  }
  return ret;
}

/*
 * try send commit msg to the transaction context, and register retry task
 * if msg send fail, the retry task will retry later
 * if both register task fail and send are failed, the commit failed
 */
int ObTransService::do_commit_tx_slowpath_(ObTxDesc &tx)
{
  int ret = OB_SUCCESS;
  SCN commit_version;
  const int commit_ret = local_ls_commit_tx_(tx.tx_id_,
                                             tx.commit_expire_ts_,
                                             tx.op_sn_,
                                             tx.commit_start_scn_,
                                             commit_version);
  if (OB_SUCCESS == commit_ret) {
    ++tx.commit_times_;
  } else if (commit_need_retry_(commit_ret)) {
    if (OB_FAIL(register_commit_retry_task_(tx, POST_COMMIT_REQ_RETRY_INTERVAL))) {
    }
  } else {
    ret = handle_tx_commit_result_(tx, commit_ret, commit_version);
  }
  return ret;
}

int ObTransService::register_commit_retry_task_(ObTxDesc &tx, int64_t max_delay)
{
  const int64_t MIN_DELAY = 50 * 1000;// 50ms
  int ret = OB_SUCCESS;
  int saved_ret = OB_SUCCESS;
  max_delay = max_delay == INT64_MAX ? ObTransCtx::MAX_TRANS_COMMIT_RETRY_TIMEOUT_US : max_delay;
  int64_t now = ObClockGenerator::getClock();
  int64_t expire_after = std::min(tx.expire_ts_ - now, tx.commit_expire_ts_ - now);
  int64_t delay = std::min(max_delay, tx.commit_task_.get_delay() * 2);
  if (expire_after > 0) { delay = std::min(delay, expire_after); }
  delay = std::max(delay, MIN_DELAY);
  if (delay != MIN_DELAY) {
    delay = ObRandom::rand(MIN_DELAY, delay);
  }
  if (OB_FAIL(tx_desc_mgr_.acquire_tx_ref(tx.tx_id_))) {
  } else {
    if (OB_FAIL(timer_.register_timeout_task(tx.commit_task_, delay))) {
      TRANS_LOG(WARN, "register tx retry task fail", KR(ret), K(delay), K(tx));
      tx_desc_mgr_.revert(tx);
      if (OB_TIMER_TASK_HAS_SCHEDULED == ret) {
        saved_ret = ret;
        // rewrite ret
        ret = OB_SUCCESS;
      }
    }
  }
#ifndef NDEBUG
  TRANS_LOG(INFO, "register commit retry task", K(ret), K(delay), K(tx));
#else
  if (OB_FAIL(ret)) {
  }
#endif
  ObTransTraceLog &tlog = tx.get_tlog();
  REC_TRANS_TRACE_EXT(&tlog, register_timeout_task,
                      OB_ID(ret), OB_SUCCESS != ret ? ret : saved_ret,
                      OB_ID(arg), delay,
                      OB_ID(ref), tx.get_ref());
  return ret;
}

// unregister commit retry task, handle its reference to tx correctly
int ObTransService::unregister_commit_retry_task_(ObTxDesc &tx)
{
  int ret = OB_SUCCESS;
  const bool is_registered = tx.commit_task_.is_registered();

  if (!is_registered) {
    // task has not been scheduled, it has't ref to txDesc
    TRANS_LOG(INFO, "task canceled", K(tx));
  } else if (OB_SUCC(timer_.unregister_timeout_task(tx.commit_task_))) {
    // task has been scheduled but hasn't ran and won't ran in the future
    // release ref of TxDesc hold by task.
    tx_desc_mgr_.revert(tx);
  } else if(OB_TIMER_TASK_HAS_NOT_SCHEDULED == ret) {
    // task has been scheduled and then was picked up to run
    // it must will run finally, its ref will handle by itself.
    ret = OB_SUCCESS;
  } else if (FALSE_IT(tx.commit_task_.set_registered(false))) {
  } else {
    TRANS_LOG(WARN, "deregister timeout task fail", K(ret), K(tx));
  }
  ObTransTraceLog &tlog = tx.get_tlog();
  REC_TRANS_TRACE_EXT(&tlog, unregister_timeout_task, OB_Y(ret),
                      OB_ID(arg), is_registered,
                      OB_ID(ref), tx.get_ref());

  return ret;
}
/*
 * retry tx commit
 * 1. if tx already terminated, ignore
 * 2. send commit msg to the transaction context
 * 3. register retry task again
 */
int ObTransService::handle_tx_commit_timeout(ObTxDesc &tx, const int64_t delay)
{
  int ret = OB_SUCCESS;
  int32_t ref_cnt = 0;
  ObTransID tx_id;
  bool cb_executed = false;
  {
    // remember tx_id because tx maybe cleanout and reused
    // in this function's following steps.
    ObSpinLockGuard guard(tx.lock_);
    tx_id = tx.tx_id_;
    int64_t now = ObClockGenerator::getClock();
    if (!tx.commit_task_.is_registered()){
      TRANS_LOG(INFO, "task canceled", K(tx));
    } else if (OB_FAIL(unregister_commit_retry_task_(tx))) {
    } else if (tx.flags_.RELEASED_) {
      TRANS_LOG(INFO, "tx released, cancel commit retry", K(tx));
    } else if (tx.state_ != ObTxDesc::State::IN_TERMINATE) {
      ret = OB_ERR_UNEXPECTED;
      TRANS_LOG(WARN, "unexpect tx state", K(ret), K_(tx.state), K(tx));
    } else if (tx.expire_ts_ <= now) {
      TRANS_LOG(WARN, "tx has timeout", K_(tx.expire_ts), K(tx));
      handle_tx_commit_result_(tx, OB_TRANS_TIMEOUT);
    } else if (tx.commit_expire_ts_ <= now) {
      TRANS_LOG(WARN, "tx commit timeout", K_(tx.commit_expire_ts), K(tx));
      handle_tx_commit_result_(tx, OB_TRANS_STMT_TIMEOUT);
    } else if (OB_FAIL(do_commit_tx_slowpath_(tx))) {
      TRANS_LOG(WARN, "retry do commit tx failed", K(ret), K(tx));
      handle_tx_commit_result_(tx, ret);
    }
    ref_cnt = tx.get_ref();
  }
  cb_executed = tx.execute_commit_cb();
  // NOTE:
  // it not safe and meaningless to access tx after commit_cb
  // has been called, the tx may has been reused or release
  // in the commit_cb
  ObTransTraceLog &tlog = tx.get_tlog();
  REC_TRANS_TRACE_EXT(&tlog, handle_timeout, OB_Y(ret),
                      OB_ID(arg), delay,
                      OB_ID(ref), tx.get_ref());
  TRANS_LOG(INFO, "handle tx commit timeout", K(ret), K(tx_id), K(ref_cnt), K(cb_executed));
  return ret;
}

/*
 * handle_tx_commit_result - commit result callback
 */
int ObTransService::handle_tx_commit_result(const ObTransID &tx_id,
                                            const int result,
                                            const SCN commit_version)
{
  int ret = OB_SUCCESS;
  ObTxDesc *tx = NULL;
  if (OB_FAIL(tx_desc_mgr_.get(tx_id, tx))) {
  } else {
    bool need_cb = false;
    tx->lock_.lock();
    if (tx->state_ < ObTxDesc::State::IN_TERMINATE) {
      ret = OB_ERR_UNEXPECTED;
      TRANS_LOG(ERROR, "unexpected tx state", K(ret),
                K_(tx->state), K(tx_id), K(result), KPC(tx));
      tx->print_trace_();
    } else if (tx->state_ > ObTxDesc::State::IN_TERMINATE) {
      TRANS_LOG(WARN, "tx has terminated", K_(tx->state),
                K(tx_id), K(result), KPC(tx));
      tx->print_trace_();
    } else {
      need_cb = true;
      ret = handle_tx_commit_result_(*tx, result, commit_version);
    }
    tx->lock_.unlock();
    if (need_cb) { tx->execute_commit_cb(); }
  }
  if (OB_NOT_NULL(tx)) {
    tx_desc_mgr_.revert(*tx);
  }
  return ret;
}

/* handle_tx_commit_result_ - handle commit's result
 *
 * the result may not be final result
 *
 * result was fall into three categories:
 * 1) finished and finalized:
 *    COMMITTED / ABORTED / NOT_FOUND / TIME_OUT
 * 2) local transient errors that should retry, such as freeze blocking
 * 3) other errors : should be ignored and retry
 */
int ObTransService::handle_tx_commit_result_(ObTxDesc &tx,
                                             const int result,
                                             const SCN commit_version)
{
  int ret = OB_SUCCESS;
  int32_t ref_cnt_0 = tx.get_ref();
  bool commit_fin = true;
  ObTxDesc::State state = ObTxDesc::State::INVL;
  int commit_out = OB_SUCCESS;
  switch (result) {
  case OB_EAGAIN:
  case OB_BLOCK_FROZEN:
    commit_fin = false;
    if (tx.commit_task_.is_registered()) {
      if (OB_FAIL(unregister_commit_retry_task_(tx))) {
      }
    }
    if (OB_SUCC(ret)) {
      const int64_t max_delay = OB_EAGAIN == result ? 300 * 1000 : INT64_MAX;
      if (OB_FAIL(register_commit_retry_task_(tx, max_delay))) {
        commit_fin = true;
        state = ObTxDesc::State::ROLLED_BACK;
        commit_out = OB_TRANS_ROLLBACKED;
      }
    }
    break;
  case OB_TRANS_COMMITED:
  case OB_SUCCESS:
    state = ObTxDesc::State::COMMITTED;
    tx.commit_version_ = commit_version;
    commit_out = OB_SUCCESS;
    break;
  case OB_TRANS_KILLED:
  case OB_TRANS_ROLLBACKED:
    state = ObTxDesc::State::ROLLED_BACK;
    commit_out = result;
    break;
  case OB_TRANS_TIMEOUT:
  case OB_TRANS_STMT_TIMEOUT:
    state = ObTxDesc::State::COMMIT_TIMEOUT;
    commit_out = result;
    break;
  case OB_TRANS_UNKNOWN:
    state = ObTxDesc::State::COMMIT_UNKNOWN;
    commit_out = result;
    break;
  case OB_TRANS_CTX_NOT_EXIST:
    if (tx.commit_times_ <= 1) {
      state = ObTxDesc::State::ROLLED_BACK;
      commit_out = OB_TRANS_KILLED;
    } else {
      state = ObTxDesc::State::COMMIT_UNKNOWN;
      commit_out = OB_TRANS_UNKNOWN;
    }
    break;
  default:
    commit_fin = false;
    TRANS_LOG(WARN, "recv unrecongized commit result, just ignore", K(result), K(tx));
    break;
  }
  // commit finished, cleanup
  if (commit_fin) {
    if (tx.finish_ts_ <= 0) { // maybe aborted early
      tx.finish_ts_ = ObClockGenerator::getClock();
    }
    /*
     * store_release ObTxDesc::{commit_out_, state_}
     * pair with ObTxDesc::execute_commit_cb
     */
    tx.commit_out_ = commit_out;
    ATOMIC_STORE_REL((int*)&tx.state_, (int)state);
    if (tx.commit_task_.is_registered()) {
      if (OB_FAIL(unregister_commit_retry_task_(tx))) {
      }
    }
    tx_post_terminate_(tx);
  }
#ifndef NDEBUG
  TRANS_LOG(INFO, "handle tx commit result", K(ret), K(tx), K(commit_fin), K(result));
#else
  if (OB_FAIL(ret)
      || (OB_SUCCESS != result && OB_TRANS_COMMITED != result)
      || (ObClockGenerator::getClock() - tx.commit_ts_) > 5 * 1000 * 1000) {
    TRANS_LOG(INFO, "handle tx commit result", K(ret), K(ref_cnt_0), K(tx), K(commit_fin), K(result));
  }
#endif
  ObTransTraceLog &tlog = tx.get_tlog();
  REC_TRANS_TRACE_EXT(&tlog, handle_tx_commit_result, OB_Y(ret),
                      OB_ID(arg), result,
                      OB_ID(is_finish), commit_fin,
                      OB_ID(result), commit_out,
                      OB_ID(state), tx.state_,
                      OB_ID(tag1), ref_cnt_0,
                      OB_ID(ref), tx.get_ref(),
                      OB_ID(commit_version), commit_version,
                      OB_ID(thread_id), GETTID());
  return ret;
}

void ObTransService::abort_tx__(ObTxDesc &tx, const bool cleanup)
{
  abort_write_state_(tx);
  if (!cleanup) {
    invalid_registered_snapshot_(tx);
  } else {
    tx_post_terminate_(tx);
  }
}

int ObTransService::abort_tx_(ObTxDesc &tx, const int cause, const bool cleanup)
{
  int ret = OB_SUCCESS;
  if (tx.state_ >= ObTxDesc::State::IN_TERMINATE) {
    ret = OB_TRANS_HAS_DECIDED;
    TRANS_LOG(WARN, "try abort tx which has decided",
              K(ret), K(tx), K(cause));
  } else {
    if (ObTxDesc::State::IDLE == tx.state_) {
      // for tx free route, when switch from idle to abort, same as tx actived
      tx.state_change_flags_.mark_all();
    }
    tx.state_ = ObTxDesc::State::IN_TERMINATE;
    tx.abort_cause_ = cause;
    // promise the abort request always send from scheduler
    if (tx.addr_ == self_) {
      abort_tx__(tx, cleanup);
    } else {
      abort_write_state_(tx);
      tx.flags_.DEFER_ABORT_ = true;
    }
    tx.state_ = ObTxDesc::State::ABORTED;
  }
  if (ObTxAbortCause::IMPLICIT_ROLLBACK != cause) {
    TRANS_LOG(INFO, "abort tx", K(ret), K(*this), K(tx), K(cause));
  }
  return ret;
}


void ObTransService::invalid_registered_snapshot_(ObTxDesc &tx)
{
  int ret = OB_SUCCESS;
  ARRAY_FOREACH(tx.savepoints_, i) {
    ObTxSavePoint &it = tx.savepoints_[i];
    if (it.is_snapshot()) {
      it.rollback();
    }
  }
}

void ObTransService::process_registered_snapshot_on_commit_(ObTxDesc &tx)
{
  // cleanup snapshot's write state info, so that they will skip
  // verify participant txn ctx, which cause false negative,
  // because txn ctx has quit when txn committed.
  int ret = OB_SUCCESS;
  ARRAY_FOREACH(tx.savepoints_, i) {
    ObTxSavePoint &p = tx.savepoints_[i];
    if (p.is_snapshot() && p.snapshot_->valid_) {
      p.snapshot_->reset_write_state();
      p.snapshot_->committed_ = true;
    }
  }
}

int ObTransService::interrupt(ObTxDesc &tx, int cause)
{
  int ret = OB_SUCCESS;
  TRANS_LOG(INFO, "start interrupt tx", KPC(this), K(tx.tx_id_), K(cause));
  bool busy_wait = false;
  {
    ObSpinLockGuard guard(tx.lock_);
    tx.flags_.INTERRUPTED_ = true;
    if (tx.flags_.BLOCK_) {
      TRANS_LOG(INFO, "will busy wait tx quit from block state", K(tx));
      busy_wait = true;
    }
  }
  while (busy_wait) {
    if (tx.flags_.BLOCK_) {
      ob_throttle_usleep(500, ret, tx.get_tx_id().get_id());
    } else {
      ObSpinLockGuard guard(tx.lock_);
      tx.flags_.INTERRUPTED_ = false;
      break;
    }
  }
  TRANS_LOG(INFO, "interrupt tx done", KR(ret), KPC(this), K(cause));
  return ret;
}

int ObTransService::report_write_ctx_status(const ObTransID &tx_id,
                                            const int status,
                                            int &tx_status)
{
  int ret = OB_SUCCESS;
  tx_status = OB_SUCCESS;
  ObTxDesc *tx = NULL;
  if (OB_FAIL(tx_desc_mgr_.get(tx_id, tx))) {
    if (OB_ENTRY_NOT_EXIST == ret) {
      ret = OB_SUCCESS;
      tx_status = OB_TRANS_CTX_NOT_EXIST;
    } else {
      TRANS_LOG(WARN, "get tx fail", K(ret), K(tx_id));
      tx_status = ret;
    }
  } else if (OB_ISNULL(tx)) {
    tx_status = OB_TRANS_CTX_NOT_EXIST;
  } else if (tx->is_committed() && tx_id == tx->tx_id_) {
    tx_status = OB_TRANS_COMMITED;
  } else if (tx->is_rollbacked() && tx_id == tx->tx_id_) {
    tx_status = OB_TRANS_ROLLBACKED;
  } else if (tx->is_aborted() && tx_id == tx->tx_id_) {
    tx_status = OB_TRANS_KILLED;
  } else if (OB_SUCCESS != status) {
    TRANS_LOG(WARN, "write ctx reported failure", K(tx_id), K(status));
    if (OB_TRANS_KILLED == status)  {
      tx->mark_write_state_aborted(tx_id, OB_TRANS_KILLED);
      tx_status = OB_TRANS_NEED_ROLLBACK;
    } else if (status > 0) {
      tx->mark_write_state_aborted(tx_id, status);
      tx_status = OB_TRANS_NEED_ROLLBACK;
    }
  }
  if (OB_NOT_NULL(tx)) {
    tx_desc_mgr_.revert(*tx);
  }
  return ret;
}

int ObTransService::find_write_state_after_savepoint_(ObTxDesc &tx,
                                        ObTxWriteState *&part,
                                        const ObTxSEQ scn)
{
  int ret = OB_SUCCESS;
  if (OB_FAIL(tx.find_write_state_after(part, scn))) {
  }
  return ret;
}

int ObTransService::get_read_store_ctx(const ObTxReadSnapshot &snapshot,
                                       const bool read_latest,
                                       const int64_t lock_timeout,
                                       ObStoreCtx &store_ctx,
                                       ObTxDesc *tx_desc)
{
  int ret = OB_SUCCESS;
  if (OB_UNLIKELY(store_ctx.timeout_ < 0)) {
    ret = OB_INVALID_ARGUMENT;
    TRANS_LOG(WARN, "store_ctx.timeout_ is invalid", K(ret), K(store_ctx), K(lbt()));
  } else if (OB_UNLIKELY(!snapshot.valid_ || OB_ISNULL(store_ctx.ls_))) {
    ret = OB_INVALID_ARGUMENT;
    TRANS_LOG(WARN, "invalid ls or invalid snapshot store_ctx", K(ret), K(snapshot), K(store_ctx), K(lbt()));
  } else if (snapshot.is_special()) {
    if (OB_FAIL(validate_snapshot_version_(snapshot.core_.version_,
                                           store_ctx.timeout_,
                                           *store_ctx.ls_))) {
    }
  }

  ObTransID snap_tx_id = snapshot.core_.tx_id_;
  ObTxCtx *tx_ctx = NULL;
  if (OB_SUCC(ret) && snap_tx_id.is_valid()) {
    // inner tx read, we verify txCtx's status
    const bool exist = snapshot.has_write_state();
    if (exist || read_latest) {
      if (OB_FAIL(get_tx_ctx_(store_ctx.ls_, snap_tx_id, tx_ctx))) {
        if (OB_TRANS_CTX_NOT_EXIST == ret && !exist) {
          ret = OB_SUCCESS;
        } else {
          TRANS_LOG(WARN, "get tx ctx fail",
                    K(ret), K(store_ctx), K(snapshot), K(exist), K(read_latest));
        }
      } else if (OB_FAIL(tx_ctx->check_status())) {
      }
      if (OB_FAIL(ret) && OB_NOT_NULL(tx_ctx)) {
        revert_tx_ctx_(store_ctx.ls_, tx_ctx);
        tx_ctx = NULL;
      }
    }
  }

  bool create_tx_ctx = false;
  if (OB_SUCC(ret) && !tx_ctx && snapshot.read_elr()) {
    if (!tx_desc) {
      TRANS_LOG(WARN, "try elr read fail, txdesc is null", K(snapshot));
    } else {
      int tmp_ret = OB_SUCCESS;
      bool exist = false;
      if (OB_TMP_FAIL(acquire_tx_ctx(*tx_desc, tx_ctx, store_ctx.ls_, false, false, exist))) {
      } else {
        create_tx_ctx = !exist;
      }
    }
  }

  // setup tx_table_guard
  if (FAILEDx(store_ctx.mvcc_acc_ctx_.init_read(tx_ctx,
                                                (tx_ctx ? tx_ctx->get_memtable_ctx() : NULL),
                                                store_ctx.ls_->get_tx_table(),
                                                snapshot.core_,
                                                store_ctx.timeout_,
                                                lock_timeout,
                                                snapshot.is_weak_read(),
                                                create_tx_ctx,
                                                tx_desc))) {
    TRANS_LOG(WARN, "mvcc_acc_ctx init read fail", KR(ret), K(store_ctx), KPC(this));
  }

  // fail, rollback
  if (OB_FAIL(ret)) {
    if (OB_NOT_NULL(tx_ctx)) {
      revert_tx_ctx_(store_ctx.ls_, tx_ctx);
      tx_ctx = NULL;
    }
  } else {
    update_max_read_ts_(snapshot.core_.version_);
  }

  return ret;
}

int ObTransService::get_read_store_ctx(const SCN snapshot_version,
                                       const int64_t lock_timeout,
                                       ObStoreCtx &store_ctx)
{
  int ret = OB_SUCCESS;
  if (!snapshot_version.is_valid()) {
    ret = OB_INVALID_ARGUMENT;
    TRANS_LOG(WARN, "invalid speficied snapshot", K(ret), K(snapshot_version));
  } else {
    ObTxReadSnapshot snapshot;
    snapshot.valid_ = true;
    snapshot.core_.version_ = snapshot_version;
    snapshot.source_ = ObTxReadSnapshot::SRC::SPECIAL;
    ret = get_read_store_ctx(snapshot, false, lock_timeout, store_ctx);
  }
  TRANS_LOG(INFO, "get-read-store-ctx for specified snapshot", K(ret), K(snapshot_version), K(store_ctx));
  return ret;
}

int ObTransService::get_write_store_ctx(ObTxDesc &tx,
                                        const ObTxReadSnapshot &snapshot,
                                        const concurrent_control::ObWriteFlag write_flag,
                                        storage::ObStoreCtx &store_ctx,
                                        const ObTxSEQ &spec_seq_no,
                                        const bool special)
{
  int ret = OB_SUCCESS;
  ObTxCtx *tx_ctx = NULL;
  const int16_t branch = store_ctx.branch_;
  ObTxSEQ data_scn = spec_seq_no; // for LOB aux table, spec_seq_no is valid
  ObTxSnapshot snap = snapshot.core_;
  bool access_started = false;
  bool ctx_exist = false;
  ObTxTable *tx_table = nullptr;

  if (tx.access_mode_ == ObTxAccessMode::RD_ONLY) {
    ret = OB_ERR_READ_ONLY_TRANSACTION;
    TRANS_LOG(WARN, "tx is readonly", K(ret), K(tx), KPC(this));
  } else if (OB_UNLIKELY(!snapshot.valid_)) {
    ret = OB_INVALID_ARGUMENT;
    TRANS_LOG(WARN, "snapshot invalid", K(ret), K(snapshot), K(lbt()));
  } else if (OB_UNLIKELY(store_ctx.timeout_ < 0)) {
    ret = OB_INVALID_ARGUMENT;
    TRANS_LOG(WARN, "store_ctx.timeout_ is invalid", K(ret), K(store_ctx), K(lbt()));
  } else if (OB_ISNULL(store_ctx.ls_)) {
    ret = OB_INVALID_ARGUMENT;
    TRANS_LOG(WARN, "store_ctx's ls_ is invalid", K(ret), K(store_ctx), K(lbt()));
  } else if (snapshot.is_none_read()
             && OB_FAIL(acquire_local_snapshot_(snap.version_))) {
    TRANS_LOG(WARN, "acquire ls snapshot for mvcc write fail", K(ret));
  } else if (OB_FAIL(acquire_tx_ctx(tx, tx_ctx, store_ctx.ls_, special, snapshot.read_elr(), ctx_exist))) {
  } else if (OB_FAIL(tx_ctx->start_access(tx, data_scn, branch))) {
  }
  if (OB_FAIL(ret)) {
  } else if (FALSE_IT(access_started = true)) {
  } else if (OB_NOT_NULL(tx_ctx) && OB_FAIL(tx_ctx->check_pending_log_overflow(store_ctx.timeout_))) {
    TRANS_LOG(WARN, "too many pending log in the tx_ctx", K(ret), K(tx), K(store_ctx));
  } else if (OB_FAIL(store_ctx.mvcc_acc_ctx_.init_write(*tx_ctx,
                                                        *tx_ctx->get_memtable_ctx(),
                                                        tx.tx_id_,
                                                        data_scn,
                                                        tx,
                                                        store_ctx.ls_->get_tx_table(),
                                                        snap,
                                                        store_ctx.timeout_,
                                                        tx.lock_timeout_us_,
                                                        write_flag))) {
  }

  // fail, rollback
  if (OB_FAIL(ret)) {
    if (OB_NOT_NULL(tx_ctx)) {
      if (access_started) { tx_ctx->end_access(); }
      revert_tx_ctx_(store_ctx.ls_, tx_ctx);
      tx_ctx = NULL;
    }
  } else {
    if (tx.get_active_ts() <= 0) {
      tx.active_ts_ = ObClockGenerator::getClock();
    }
    /* NOTE: some write with adjoint reads:
     * eg. insert row to a table with primary key will _check_
     * rowkey-exist before do insert (this check is a read).
     *
     * so it's required to update `max_read_ts` for these write
     */
    update_max_read_ts_(snap.version_);
  }
  TRANS_LOG(DEBUG, "get-write-store-ctx", K(ret),
            K(store_ctx), KPC(this), K(tx), K(snapshot), K(lbt()));
  return ret;
}

/*
 * the get here imply `get if exist` or `create if should`
 * A missing local context is created directly in the unique log stream.
 */
int ObTransService::acquire_tx_ctx(const ObTxDesc &tx,
                                   ObTxCtx *&ctx,
                                   ObLS *ls,
                                   const bool special,
                                   const bool try_get,
                                   bool &exist)
{
  int ret = OB_SUCCESS;
  exist = tx.has_write_state();
  if (exist) {
    if (OB_FAIL(get_tx_ctx_(ls, tx.tx_id_, ctx))) {
      TRANS_LOG(WARN, "get tx ctx fail", K(ret), K(tx));
      if (ret == OB_TRANS_CTX_NOT_EXIST) {
        TRANS_LOG(WARN, "write state lost update", K_(tx.tx_id));
      }
    }
  } else if (try_get && OB_SUCC(get_tx_ctx_(ls, tx.tx_id_, ctx))) {
  } else if (OB_FAIL(create_tx_ctx_(ls, tx, ctx, special, exist))) {
  }

  return ret;
}

// plain create
int ObTransService::get_tx_ctx_(ObLS *ls,
                                const ObTransID &tx_id,
                                ObTxCtx *&ctx)
{
  int ret = OB_SUCCESS;
  if (OB_NOT_NULL(ls)) {
    ret = ls->get_tx_ctx(tx_id, false, ctx);
  } else {
    ret = tx_ctx_mgr_.get_tx_ctx(tx_id, false, ctx);
  }

  return ret;
}

int ObTransService::get_tx_ctx_(const ObTransID &tx_id,
                                ObTxCtx *&ctx)
{ return get_tx_ctx_(NULL, tx_id, ctx); }

int ObTransService::revert_tx_ctx_(ObLS* ls, ObTxCtx *ctx)
{
  int ret = OB_SUCCESS;
  if (OB_NOT_NULL(ls)) {
    ret = ls->revert_tx_ctx(ctx);
  } else {
    ret = tx_ctx_mgr_.revert_tx_ctx(ctx);
  }

  return ret;
}

int ObTransService::revert_tx_ctx_(ObTxCtx *ctx)
{ return revert_tx_ctx_(NULL, ctx); }

/*
 * create fresh tranaction ctx
 * 1) allocate
 * 2) initialize
 *
 * NB: special tx_ctx would not blocked when in block_normal state
 */
int ObTransService::create_tx_ctx_(ObLS *ls,
                                   const ObTxDesc &tx,
                                   ObTxCtx *&ctx,
                                   const bool special,
                                   bool &exist)
{
  int ret = OB_SUCCESS;
  int64_t epoch = 0;
  TxCtxSource ctx_source = TxCtxSource::MVCC_WRITE;
  if(special) {
    ctx_source = TxCtxSource::REGISTER_MDS;
  }
  ObTxCreateArg arg(false,  /* for_replay */
                    ctx_source,
                    tx.tx_id_,
                    tx.sess_id_, /*session_id*/
                    tx.get_expire_ts(),
                    this);
  ret = OB_NOT_NULL(ls) ?
    ls->create_tx_ctx(arg, exist, ctx) :
    tx_ctx_mgr_.create_tx_ctx(arg, exist, ctx);
  if (OB_FAIL(ret)) {
    TRANS_LOG(WARN, "get tx ctx from mgr fail", K(ret), K(tx.tx_id_), K(exist), K(tx), K(arg));
    ctx = NULL;
  }
  return ret;
}

int ObTransService::create_tx_ctx_(const ObTxDesc &tx,
                                   ObTxCtx *&ctx,
                                   bool &exist)
{ return create_tx_ctx_(NULL, tx, ctx, false, exist); }

int ObTransService::revert_store_ctx(storage::ObStoreCtx &store_ctx)
{
  int ret = OB_SUCCESS;
  ObMvccAccessCtx &acc_ctx = store_ctx.mvcc_acc_ctx_;
  ObTxCtx *tx_ctx = acc_ctx.tx_ctx_;
  if (acc_ctx.is_read()) {
    if (OB_NOT_NULL(tx_ctx)) {
      if (acc_ctx.has_create_tx_ctx_) { // elr read will try to create tx ctx
        ObTxDesc *tx_desc = acc_ctx.tx_desc_;
        if (OB_ISNULL(tx_desc)) {
          ret = OB_ERR_UNEXPECTED;
          TRANS_LOG(ERROR, "tx desc is null", K(ret), K(store_ctx));
        } else if (OB_FAIL(tx_desc->init_clean_write_state_if_absent())) {
        }
      }
      acc_ctx.tx_ctx_ = NULL;
      revert_tx_ctx_(store_ctx.ls_, tx_ctx);
    }
  } else if (acc_ctx.is_write()) {
    if (OB_ISNULL(tx_ctx)) {
      ret = OB_ERR_UNEXPECTED;
      TRANS_LOG(ERROR, "write access but tx ctx is NULL", K(ret), K(store_ctx));
    } else {
      /*
       * record transaction write state info
       */
      ObTxDesc *tx = acc_ctx.tx_desc_;
      acc_ctx.tx_ctx_ = NULL;
      ObTxWriteState p;
      p.first_scn_  = tx_ctx->first_scn_;
      p.last_scn_   = tx_ctx->last_scn_;
      if (OB_FAIL(tx->merge_write_state(p))) {
      }
      tx_ctx->end_access();
      revert_tx_ctx_(store_ctx.ls_, tx_ctx);
    }
  } else {
    ret = OB_ERR_UNEXPECTED;
    TRANS_LOG(ERROR, "unexpected store ctx type", K(ret), K(store_ctx));
  }

  TRANS_LOG(DEBUG, "revert store ctx", K(ret), K(*this), K(lbt()));
  return ret;
}

/*
 * used to validate specified snapshot version
 * precondition: version <= current gts value
 */
int ObTransService::validate_snapshot_version_(const SCN snapshot,
                                               const int64_t expire_ts,
                                               ObLS &ls)
{
  int ret = OB_SUCCESS;
  const SCN ls_weak_read_ts = ls.get_ls_wrs_handler()->get_ls_weak_read_ts();
  if (snapshot <= tx_version_mgr_.get_max_commit_ts(false) ||
      snapshot <= tx_version_mgr_.get_max_read_ts() ||
      snapshot <= ls_weak_read_ts) {
  } else {
    SCN gts;
    const MonotonicTs stc_ahead = get_req_receive_mts_();
    MonotonicTs tmp_receive_gts_ts(0);
    do {
      ret = ts_mgr_->get_gts(stc_ahead, gts, tmp_receive_gts_ts);
      if (ret == OB_EAGAIN) {
        if (expire_ts <= ObClockGenerator::getClock()) {
          ret = OB_TIMEOUT;
        } else {
          ob_usleep(100);
        }
      } else if (OB_FAIL(ret)) {
      } else if (!gts.is_valid()) {
        ret = OB_ERR_UNEXPECTED;
        TRANS_LOG(WARN, "get gts fail", K(gts));
      } else if (snapshot > gts) {
        ret = OB_INVALID_QUERY_TIMESTAMP;
        TRANS_LOG(WARN, "validate snapshot version fail", K(snapshot), K(gts));
      } else {
      }
    } while (ret == OB_EAGAIN);
  }
  return ret;
}

MonotonicTs ObTransService::get_req_receive_mts_()
{
  /*
  MonotonicTs mts;
  const rpc::ObRequest *req = THIS_WORKER.get_cur_request();
  if (NULL != req && req->get_receive_mts().is_valid()) {
    mts = req->get_receive_mts();
  } else {
    mts = MonotonicTs::current_time();
  }
  return mts;
  */
  return MonotonicTs::current_time();
}

/*
 * collect trans exec result
 */
int ObTransService::collect_tx_exec_result(ObTxDesc &tx,
                                           ObTxExecResult &result)
{
  int ret = OB_SUCCESS;
  ret = get_tx_exec_result(tx, result);
  TRANS_LOG(TRACE, "collect tx exec result", K(ret), K(tx), K(result), K(lbt()));
  return ret;
}

int ObTransService::abort_write_state_(const ObTxDesc &tx_desc)
{
  int ret = OB_SUCCESS;
  const ObTxWriteState *part = NULL;
  if (OB_FAIL(tx_desc.get_abort_write_state(part))) {
  } else if (OB_NOT_NULL(part) && OB_FAIL(abort_write_ctx_(tx_desc))) {
    TRANS_LOG(WARN, "abort write context failed", K(ret), K(tx_desc), KPC(part));
  }
  return ret;
}

OB_NOINLINE int ObTransService::acquire_local_snapshot_(SCN &snapshot)
{
  int ret = OB_SUCCESS;
  SCN snapshot0;
  SCN snapshot1;
  const bool can_elr = share::server_is_write_enabled();
  if (FALSE_IT(snapshot0 = tx_version_mgr_.get_max_commit_ts(can_elr))) {
  } else if (!snapshot0.is_valid_and_not_min()) {
    ret = OB_EAGAIN;
  } else if (OB_FAIL(ts_mgr_->get_gts(snapshot1))) {
  } else {
    snapshot = SCN::max(snapshot0, snapshot1);
  }

#ifdef ENABLE_DEBUG_LOG
  TRANS_LOG(TRACE, "acquire local snapshot", K(ret), K(snapshot));
#endif
  return ret;
}

/********************************************************************
 *
 * RPC and Message Handle
 *
 ********************************************************************/

int ObTransService::abort_write_ctx_(const ObTxDesc &tx_desc)
{
  int ret = OB_SUCCESS;
  ObTxCtx *ctx = NULL;
  if (OB_FAIL(get_tx_ctx_(tx_desc.tx_id_, ctx))) {
  } else if (OB_FAIL(ctx->abort(tx_desc.abort_cause_))) {
  }
  if (OB_NOT_NULL(ctx)) {
    revert_tx_ctx_(ctx);
  }
  return ret;
}



int ObTransService::local_ls_commit_tx_(const ObTransID &tx_id,
                                        const int64_t &expire_ts,
                                        const int64_t &request_id,
                                        const SCN commit_start_scn,
                                        SCN &commit_version)
{
  int ret = OB_SUCCESS;
  MonotonicTs commit_time = get_req_receive_mts_();
  ObTxCtx *ctx = NULL;
  if (OB_FAIL(get_tx_ctx_(tx_id, ctx))) {
    TRANS_LOG(WARN, "get transaction context fail", K(ret), K(tx_id));
    if (OB_TRANS_CTX_NOT_EXIST == ret) {
      int64_t tx_state = ObTxData::RUNNING;
      share::SCN recycle_scn;
      if (OB_FAIL(get_tx_state_from_tx_table_(tx_id, tx_state, commit_version, recycle_scn))) {
        TRANS_LOG(WARN, "get tx state from tx table fail", K(ret), K(tx_id));
        if (OB_TRANS_CTX_NOT_EXIST == ret) {
          if (commit_start_scn > recycle_scn) {
            ret = OB_TRANS_KILLED; // abort without persistent
          } else {
            // recycled, either committed or aborted
          }
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
    }
  } else if (OB_FAIL(ctx->commit(commit_time, expire_ts, request_id))) {
  }
  if (OB_NOT_NULL(ctx)) {
    revert_tx_ctx_(ctx);
  }
  return ret;
}

int ObTransService::get_tx_state_from_tx_table_(const ObTransID &tx_id,
                                                int64_t &state,
                                                SCN &commit_version,
                                                SCN &recycled_scn)
{
  int ret = OB_SUCCESS;
  ObTxTableGuard tx_table_guard;
  ObLS *tenant_ls = nullptr;
  if (OB_FAIL(::oceanbase::share::server_service<::oceanbase::storage::ObLSService>()->get_ls(tenant_ls))) {
  } else if (OB_FAIL(tenant_ls->get_tx_table()->get_tx_table_guard(tx_table_guard))) {
  } else if (OB_FAIL(tx_table_guard.try_get_tx_state(tx_id, state, commit_version, recycled_scn))) {
  }
  return ret;
}

bool ObTransService::common_retryable_error_(const int ret) {
  return (OB_EAGAIN == ret
          || OB_NEED_RETRY == ret
          || OB_TX_NOLOGCB == ret
          || OB_PARTITION_IS_BLOCKED == ret
          );
}

int ObTransService::update_max_read_ts_(const SCN ts)
{
  int ret = OB_SUCCESS;
  tx_version_mgr_.update_max_read_ts(ts);
  return ret;
}

int ObTransService::gen_trans_id(ObTransID &trans_id)
{
  int ret = OB_SUCCESS;

  int retry_times = 0;
  {
    const int MAX_RETRY_TIMES = 50;
    int64_t start_id = 0;
    int64_t end_id = 0;
    do {
      if (OB_ISNULL(trans_id_service_)) {
        ret = OB_ERR_UNEXPECTED;
        TRANS_LOG(ERROR, "trans id service is null", K(ret), KPC(this));
      } else if (OB_SUCC(trans_id_service_->get_number(1, 0, start_id, end_id))) {
        if (OB_UNLIKELY(end_id != start_id + 1)) {
          ret = OB_ERR_UNEXPECTED;
          TRANS_LOG(ERROR, "unexpected trans id range", K(ret), K(start_id), K(end_id));
        }
      } else if (OB_EAGAIN == ret) {
        if (retry_times++ > MAX_RETRY_TIMES) {
          ret = OB_GTI_NOT_READY;
          TRANS_LOG(WARN, "get trans id not ready", K(ret), K(retry_times), KPC(this));
        } else {
          ob_usleep(1000);
        }
      } else {
        TRANS_LOG(WARN, "get trans id fail", KR(ret));
      }
    } while (OB_EAGAIN == ret);
    if (OB_SUCC(ret)) {
      trans_id = ObTransID(start_id);
    }
  }
  return ret;
}

bool ObTransService::commit_need_retry_(const int ret)
{
  return OB_BLOCK_FROZEN == ret
    || common_retryable_error_(ret);
}



int ObTransService::block_tx(bool &is_all_tx_cleaned_up)
{
  int ret = OB_SUCCESS;

  if (IS_NOT_INIT) {
    TRANS_LOG(WARN, "ObTransService not inited");
    ret = OB_NOT_INIT;
  } else if (OB_UNLIKELY(!is_running_)) {
    TRANS_LOG(WARN, "ObTransService is not running");
    ret = OB_NOT_RUNNING;
  } else if (OB_FAIL(tx_ctx_mgr_.block_tx(is_all_tx_cleaned_up))) {
  } else {
    TRANS_LOG(INFO, "block transaction context manager success", K(is_all_tx_cleaned_up));
  }
  return ret;
}


int ObTransService::get_tx_ctx_mgr_stat(ObLSTxCtxMgrStat &tx_ctx_mgr_stat)
{
  int ret = OB_SUCCESS;

  if (IS_NOT_INIT) {
    TRANS_LOG(WARN, "ObTransService not inited");
    ret = OB_NOT_INIT;
  } else if (OB_UNLIKELY(!is_running_)) {
    TRANS_LOG(WARN, "ObTransService is not running");
    ret = OB_NOT_RUNNING;
  } else if (OB_FAIL(tx_ctx_mgr_.get_tx_ctx_mgr_stat(self_, tx_ctx_mgr_stat))) {
  } else {
    // do nothing
  }
  return ret;
}



int ObTransService::iterate_all_observer_tx_stat(ObTxStatIterator &tx_stat_iter)
{
  int ret = OB_SUCCESS;
  const int64_t PRINT_SCHE_COUNT = 128;

  if (IS_NOT_INIT) {
    TRANS_LOG(WARN, "ObTransService not inited");
    ret = OB_NOT_INIT;
  } else if (OB_UNLIKELY(!is_running_)) {
    TRANS_LOG(WARN, "ObTransService is not running");
    ret = OB_NOT_RUNNING;
  } else if (OB_FAIL(tx_ctx_mgr_.iterate_all_observer_tx_stat(tx_stat_iter))) {
  } else {
    // do nothing
  }

  return ret;
}

int ObTransService::iterate_tx_scheduler_stat(ObTxSchedulerStatIterator &tx_scheduler_stat_iter)
{
  int ret = OB_SUCCESS;
  if (IS_NOT_INIT) {
    TRANS_LOG(WARN, "ObTransService not inited");
    ret = OB_NOT_INIT;
  } else if (OB_UNLIKELY(!is_running_)) {
    TRANS_LOG(WARN, "ObTransService is not running");
    ret = OB_NOT_RUNNING;
  } else if (OB_FAIL(tx_desc_mgr_.iterate_tx_scheduler_stat(tx_scheduler_stat_iter))) {
  } else {
    // do nothing
  }
  return ret;
}



/*
 * create_in_txn_implicit_savepoint - create an implicit savepoint when txn is active
 */
int ObTransService::create_in_txn_implicit_savepoint(ObTxDesc &tx, ObTxSEQ &savepoint)
{
  int ret = OB_SUCCESS;

  ObTxParam tx_param;
  tx_param.timeout_us_ = tx.timeout_us_;
  tx_param.lock_timeout_us_ = tx.lock_timeout_us_;
  tx_param.access_mode_ = tx.access_mode_;
  tx_param.isolation_ = tx.isolation_;
  if (tx_param.is_valid()) {
    ret = create_implicit_savepoint(tx, tx_param, savepoint);
  } else {
    ret = OB_ERR_UNEXPECTED;
    TRANS_LOG(WARN, "create in txn implicit savepoint, but txn not in txn", K(ret), K(tx));
  }
  return ret;
}

void ObTransService::force_release_tx_when_session_destroy(ObTxDesc &tx)
{
  {
    ObSpinLockGuard guard(tx.lock_);
    TRANS_LOG_RET(WARN, OB_SUCCESS, "txdesc will be released forcedly", K(tx));
    tx.print_trace_();
  }
  ObTxDescMgr::force_release(tx);
}

void ObTransService::adjust_tx_snapshot_(ObTxDesc &tx, ObTxReadSnapshot &snapshot)
{
  // ensure snapshot won't go backward
  if (tx.is_RC_isolevel()) {
    if (tx.last_rc_snapshot_version_ > snapshot.core_.version_) {
      snapshot.core_.version_ = tx.last_rc_snapshot_version_;
      snapshot.uncertain_bound_ = 0;
    } else if (tx.last_rc_snapshot_version_ < snapshot.core_.version_) {
      tx.last_rc_snapshot_version_ = snapshot.core_.version_;
    }
  }
}

} // transaction
} // ocenabase
