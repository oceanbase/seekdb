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

#include "ob_trans_ctx_mgr_v4.h"
#include "share/rc/ob_module_provider.h"
#include "observer/ob_server.h"
#include "ob_trans_functor.h"
#include "storage/tx_storage/ob_ls_service.h"

#define USING_LOG_PREFIX TRANS

namespace oceanbase
{

using namespace common;
using namespace share;
using namespace common::hash;
using namespace storage;
using namespace memtable;
using namespace observer;

namespace transaction
{

#ifdef ERRSIM
ERRSIM_POINT_DEF(EN_SWITCH_TO_FOLLOWER_GRACEFULLY)
ERRSIM_POINT_DEF(EN_SUBMIT_START_WORKING_LOG)
ERRSIM_POINT_DEF(EN_APPLY_START_WORKING_LOG)
#endif

void ObLSTxCtxIterator::reset() {
  is_ready_ = false;
  current_bucket_pos_ = -1;
  ls_tx_ctx_mgr_ = NULL;
  tx_id_iter_.reset();
}

int ObLSTxCtxIterator::set_ready(ObLSTxCtxMgr* ls_tx_ctx_mgr)
{
  int ret = OB_SUCCESS;

  if (is_ready_) {
    ret = OB_ERR_UNEXPECTED;
  } else {
    is_ready_ = true;
    ls_tx_ctx_mgr_ = ls_tx_ctx_mgr;
    tx_id_iter_.set_ready();
  }
  return ret;
}

int ObLSTxCtxIterator::get_next_tx_ctx(ObPartTransCtx *&tx_ctx)
{

  int ret = OB_SUCCESS;
  ObTransID tx_id;

  bool try_next_loop = false;
  do {
    try_next_loop = false;

    if (!is_ready()) {
      ret = OB_NOT_INIT;
    } else if (NULL == ls_tx_ctx_mgr_) {
      ret = OB_NOT_INIT;
    } else if (OB_FAIL(get_next_tx_id_(tx_id))) {
      // do nothing
    } else {
      if (OB_FAIL(ls_tx_ctx_mgr_->get_tx_ctx_directly_from_hash_map(tx_id, tx_ctx))) {
        if (OB_TRANS_CTX_NOT_EXIST == ret) {
          try_next_loop = true;
        } else {
          TRANS_LOG(WARN, "get_tx_ctx_directly_from_hash_map failed", K(tx_id), K(ret));
        }
      } else {
        // do nothing
      }
    }
  } while (try_next_loop);

  return ret;
}

int ObLSTxCtxIterator::revert_tx_ctx(ObPartTransCtx *tx_ctx)
{
  int ret = OB_SUCCESS;

  if (!is_ready()) {
    ret = OB_NOT_INIT;
  } else if (NULL == ls_tx_ctx_mgr_) {
    ret = OB_NOT_INIT;
  } else {
    ret = ls_tx_ctx_mgr_->revert_tx_ctx(tx_ctx);
  }
  return ret;
}

int ObLSTxCtxIterator::get_next_tx_id_(ObTransID& tx_id)
{
  int ret = OB_SUCCESS;

  bool try_next_loop = false;
  do {
    try_next_loop = false;

    if (OB_FAIL(tx_id_iter_.get_next(tx_id))) {
      if (OB_ITER_END == ret) {
        ++ current_bucket_pos_;
        if (current_bucket_pos_ >= BUCKETS_CNT_) {
          ret = OB_ITER_END;
        } else {
          tx_id_iter_.reset();
          if (OB_FAIL(ls_tx_ctx_mgr_->
              iterator_tx_id_in_one_bucket(tx_id_iter_, current_bucket_pos_))) {
          } else {
            tx_id_iter_.set_ready();
            if (OB_FAIL(tx_id_iter_.get_next(tx_id))) {
              if (OB_ITER_END == ret) {
                try_next_loop = true;
              } else {
                TRANS_LOG(WARN, "tx_id_iter_.get_next failed", K(ret));
              }
            } else {
              // goto next step
            }
          }
        }
      } else {
        TRANS_LOG(WARN, "tx_id_iter_.get_next fail", K(ret));
      }
    } else {
      // goto next step
    }
  } while(try_next_loop);

  return ret;
}

OB_WEAK_SYMBOL int ObLSTxCtxMgr::init(const ObLSID &ls_id,
                       ObTxTable *tx_table,
                       ObLockTable *lock_table,
                       ObTsMgr *ts_mgr,
                       ObTransService *txs,
                       ObITxLogParam *param,
                       ObITxLogAdapter *log_adapter)
{
  int ret = OB_SUCCESS;

  if (IS_INIT) {
    ret = OB_INIT_TWICE;
  } else if (OB_UNLIKELY(!ls_id.is_valid()) || OB_ISNULL(ts_mgr) || OB_ISNULL(txs)) {
    ret = OB_INVALID_ARGUMENT;
  } else if (OB_FAIL(ls_tx_ctx_map_.init(lib::ObMemAttr("LSTxCtxMgr")))) {
  } else if (OB_ISNULL(log_adapter) && OB_FAIL(log_adapter_def_.init(param, tx_table))) {
    TRANS_LOG(WARN, "tx log adapter init error", KR(ret));
  } else if (OB_NOT_NULL(log_adapter) && OB_FALSE_IT(tx_log_adapter_ = log_adapter)) {
    ret = OB_ERR_UNEXPECTED;
  } else if (OB_FAIL(log_cb_pool_mgr_.init(ls_id))) {
  } else if (OB_FAIL(ls_log_writer_.init(ls_id, tx_log_adapter_, this))) {
  } else if (OB_FAIL(tx_ls_state_mgr_.init(ls_id))) {
  } else if (OB_FAIL(tx_ls_state_mgr_.switch_tx_ls_state(ObTxLSStateMgr::TxLSAction::START))) {
  } else {
    is_inited_ = true;
    is_leader_serving_ = false;
    
    ls_id_ = ls_id;
    tx_table_ = tx_table;
    lock_table_ = lock_table;
    txs_ = txs;
    ts_mgr_ = ts_mgr;
    aggre_rec_scn_.reset();
    prev_aggre_rec_scn_.reset();
    online_ts_ = 0;
  }
  return ret;
}

void ObLSTxCtxMgr::destroy()
{
  WLockGuardWithRetryInterval guard(rwlock_, TRY_THRESOLD_US, RETRY_INTERVAL_US);
  if (IS_INIT) {
    log_cb_pool_mgr_.destroy();
    ls_log_writer_.destroy();
    is_inited_ = false;
  }
}

void ObLSTxCtxMgr::reset()
{
  is_inited_ = false;
  
  ls_id_.reset();
  tx_table_ = NULL;
  lock_table_ = NULL;
  total_tx_ctx_count_ = 0;
  active_tx_count_ = 0;
  total_active_readonly_request_count_ = 0;
  total_request_by_transfer_dest_ = 0;
  leader_takeover_ts_.reset();
  max_replay_commit_version_.reset();
  aggre_rec_scn_.reset();
  prev_aggre_rec_scn_.reset();
  online_ts_ = 0;
  txs_ = NULL;
  ts_mgr_ = NULL;
  tx_ls_state_mgr_.reset();
  ls_retain_ctx_mgr_.reset();

  ObRemoveAllTxCtxFunctor fn;
  ls_tx_ctx_map_.remove_if(fn);
  ls_tx_ctx_map_.reset();
}

int ObLSTxCtxMgr::offline()
{
  int ret = OB_SUCCESS;
  aggre_rec_scn_.reset();
  prev_aggre_rec_scn_.reset();
  TRANS_LOG(INFO, "offline ls", K(ret), "manager", *this);

  return ret;
}

int ObLSTxCtxMgr::process_callback_(ObTxCommitCallback *&cb_list) const
{
  int ret = OB_SUCCESS;
  ObTxCommitCallback *next = NULL;
  for (ObTxCommitCallback *iter = cb_list; iter != NULL; iter = next) {
    next = iter->get_link_next();
    iter->callback();
  }
  return ret;
}

void ObLSTxCtxMgr::print_all_tx_ctx(const int64_t max_print, const bool verbose)
{
  print_all_tx_ctx_(max_print, verbose);
}

void ObLSTxCtxMgr::print_all_tx_ctx_(const int64_t max_print, const bool verbose)
{
  UNUSED(max_print);
  UNUSED(verbose);
  PrintFunctor print_fn(max_print, verbose);
  // ignore ret
  ls_tx_ctx_map_.for_each(print_fn);
}

int ObLSTxCtxMgr::create_tx_ctx(const ObTxCreateArg &arg,
                                bool& existed,
                                ObPartTransCtx *&ctx)
{
  int ret = OB_SUCCESS;
  RLockGuard guard(rwlock_);

  if (OB_FAIL(create_tx_ctx_(arg, existed, ctx))) {
  } else {
    // do nothing
  }

  return ret;
}

int ObLSTxCtxMgr::create_tx_ctx_(const ObTxCreateArg &arg,
                                 bool& exist,
                                 ObPartTransCtx *&ctx)
{
  int ret = OB_SUCCESS;
  ObTransCtx *tmp_ctx = NULL, *exist_ctx = NULL;
  bool leader = false, insert_succ = false;
  int64_t epoch = 0;

  bool block  = false;
  if (is_tx_blocked_()) {
    block = true;
  } else if (is_normal_blocked_()) {
    if (arg.ctx_source_ != PartCtxSource::REGISTER_MDS) {
      block = true;
    }
  }

  exist = false;
  if (IS_NOT_INIT) {
    ret = OB_NOT_INIT;
  } else if (OB_UNLIKELY(!arg.is_valid())) {
    ret = OB_INVALID_ARGUMENT;
  } else if (!arg.for_replay_ && !is_master_()) {
    ret = OB_NOT_MASTER;
  } else if (!arg.for_replay_ && block) {
    ret = OB_PARTITION_IS_BLOCKED;
  } else if (is_stopped_()) {
    ret = OB_PARTITION_IS_STOPPED;
  } else if (is_master_() && OB_FAIL(try_wait_gts_and_inc_max_commit_ts_())) {
    TRANS_LOG(WARN, "switch_to_leader processing is not finished", KR(ret), K(ls_id_));
  } else if (!arg.for_replay_ && OB_FAIL(tx_log_adapter_->get_role(leader, epoch))) {
    TRANS_LOG(WARN, "get replica role fail", K(ret));
  } else if (!arg.for_replay_ && OB_ISNULL(arg.move_arg_) && !leader) {
  /* when transfer move tx register phase create tx_ctx, switch_to_follower_forcedly may remove it
   * so on_redo callback need create tx_ctx ignore log role check */
    ret = OB_NOT_MASTER;
    TRANS_LOG(WARN, "replica not leader", K(ret));
  } else if (OB_ISNULL(tmp_ctx = ObTransCtxFactory::alloc(ObTransCtxType::PARTICIPANT))) {
    ret = OB_ALLOCATE_MEMORY_FAILED;
  } else {
    int64_t epoch_v = 0;
    if (arg.epoch_ > 0) {
      epoch_v = arg.epoch_;
    } else {
      // for transfer compatibility, we need old version follower's epoch be 0, so we need not check it
      if (!arg.for_replay_) {
        // pack `epoch(15bit) | ts_ns(48bit)` into int64_t, set most significant bit to zero
        epoch_v = ~(1ULL << 63) & ((epoch << 48) | (ObTimeUtility::current_time_ns() & ~(0xFFFFULL << 48)));
      } else {
        epoch_v = -1;
      }
    }
    CtxLockGuard ctx_lock_guard;
    ObPartTransCtx *tmp = static_cast<ObPartTransCtx *>(tmp_ctx);
    if (OB_FAIL(tmp->init(arg.scheduler_,
                          arg.session_id_,
                          arg.client_sid_,
                          arg.associated_session_id_,
                          arg.tx_id_,
                          arg.trans_expired_time_,
                          arg.ls_id_,
                          arg.cluster_version_,
                          arg.trans_service_,
                          arg.cluster_id_,
                          epoch_v,
                          this,
                          arg.for_replay_,
                          arg.ctx_source_,
                          arg.xid_))) {
    } else if (FALSE_IT(inc_total_tx_ctx_count())) {
    } else if (FALSE_IT(tmp_ctx->get_ctx_guard(ctx_lock_guard))) {
    } else if (OB_FAIL(ls_tx_ctx_map_.insert_and_get(arg.tx_id_, tmp_ctx, &exist_ctx))) {
      if (OB_ENTRY_EXIST == ret) {
        if (OB_ISNULL(exist_ctx)) {
          ret = OB_ERR_UNEXPECTED;
          TRANS_LOG(ERROR, "exist_ctx is null", KR(ret), K(arg));
        } else {
          exist = true;
        }
      } else {
        TRANS_LOG(WARN, "insert transaction context error", KR(ret), K(arg));
      }
    } else if (FALSE_IT(insert_succ = true)) {
    } else if (FALSE_IT(inc_active_tx_count())) {
    } else if (!arg.for_replay_ && OB_FAIL(tmp->start_trans())) {
      TRANS_LOG(WARN, "ctx start trans fail", K(ret), "ctx", tmp);
    } else {
      ctx = tmp;
    }
  }
  // if fail, cleanup
  if (OB_FAIL(ret) && OB_NOT_NULL(tmp_ctx)) {
    if (insert_succ) {
      ls_tx_ctx_map_.revert(tmp_ctx);
    } else {
      tmp_ctx->set_exiting();
      ObTransCtxFactory::release(tmp_ctx);
    }
  }
  // if exist, wait ctx create done
  if (exist) {
    ret = OB_SUCCESS;
    CtxLockGuard ctx_guard;
    exist_ctx->get_ctx_guard(ctx_guard);
    ctx = static_cast<ObPartTransCtx *>(exist_ctx);
  }
  if (REACH_TIME_INTERVAL(OB_TRANS_STATISTICS_INTERVAL)) {
  }
  return ret;
}

int ObLSTxCtxMgr::get_tx_ctx(const ObTransID &tx_id, const bool for_replay, ObPartTransCtx *&ctx)
{
  int ret = OB_SUCCESS;
  RLockGuard guard(rwlock_);

  if (OB_FAIL(get_tx_ctx_(tx_id, for_replay, ctx))) {
  } else {
    // do nothing
  }
  return ret;
}

int ObLSTxCtxMgr::get_tx_ctx_with_timeout(const ObTransID &tx_id,
                                          const bool for_replay,
                                          ObPartTransCtx *&tx_ctx,
                                          const int64_t lock_timeout)
{
  int ret = OB_SUCCESS;

  RWLock::RLockGuardWithTimeout guard(rwlock_, ObTimeUtility::fast_current_time() + lock_timeout,
                                      ret);

  if (OB_FAIL(ret)) {
  } else if (OB_FAIL(get_tx_ctx_(tx_id, for_replay, tx_ctx))) {
  } else {
    // do nothing
  }

  return ret;
}

int ObLSTxCtxMgr::get_tx_ctx_(const ObTransID &tx_id, const bool for_replay, ObPartTransCtx *&ctx)
{
  int ret = OB_SUCCESS;
  ObTransCtx *tmp_ctx = NULL;
  const int64_t MAX_LOOP_COUNT = 100;
  int64_t count = 0;
  int64_t gts = 0;

  if (IS_NOT_INIT) {
    ret = OB_NOT_INIT;
  } else if (OB_UNLIKELY(!tx_id.is_valid()) || OB_ISNULL(ts_mgr_)) {
    ret = OB_INVALID_ARGUMENT;
  } else if (!for_replay && !is_master_()) {
    ret = OB_NOT_MASTER;
  } else {
    if (OB_SUCC(ls_tx_ctx_map_.get(tx_id, tmp_ctx))) {
      if (OB_ISNULL(tmp_ctx)) {
        ret = OB_ERR_UNEXPECTED;
      } else {
        // for trans with is_exiting, we consider the thans has been ended.
        // if (tmp_ctx->is_exiting()) {
        //   ret = OB_TRANS_CTX_NOT_EXIST;
        //   ls_tx_ctx_map_.revert(tmp_ctx);
        //   tmp_ctx = NULL;
        // }
        ctx = static_cast<transaction::ObPartTransCtx*>(tmp_ctx);
      }
    } else if (OB_ENTRY_NOT_EXIST == ret) {
      ret = OB_TRANS_CTX_NOT_EXIST;
    } else {
      TRANS_LOG(ERROR, "get transaction context error", KR(ret), K(tx_id));
    }
    if (REACH_TIME_INTERVAL(OB_TRANS_STATISTICS_INTERVAL)) {
    }
#ifdef ENABLE_DEBUG_LOG
    // ENABLE_DEBUG_LOG macro only defined in inner test environment
    if (REACH_TIME_INTERVAL(3 * 60 * 1000 * 1000 /*3 min*/)) {
    }
#endif
  }
  return ret;
}

int ObLSTxCtxMgr::iterator_tx_id_in_one_bucket(ObTxIDIterator& iter, int bucket_pos)
{
  int ret = OB_SUCCESS;

  IteratorTxIDFunctor fn(iter);
  if (OB_FAIL(ls_tx_ctx_map_.for_each_in_one_bucket(fn, bucket_pos))) {
  } else {
  }
  return ret;
}


int ObLSTxCtxMgr::get_tx_ctx_directly_from_hash_map(const ObTransID &tx_id, ObPartTransCtx *&ctx)
{
  int ret = OB_SUCCESS;
  ObTransCtx *tmp_ctx = NULL;

  if (IS_NOT_INIT) {
    ret = OB_NOT_INIT;
  } else if (OB_UNLIKELY(!tx_id.is_valid())) {
    ret = OB_INVALID_ARGUMENT;
  } else {
    if (OB_FAIL(ls_tx_ctx_map_.get(tx_id, tmp_ctx))) {
      if (OB_ENTRY_NOT_EXIST == ret) {
        ret = OB_TRANS_CTX_NOT_EXIST;
      } else {
        TRANS_LOG(ERROR, "get transaction context error", KR(ret), K(tx_id));
      }
    } else {
      ctx = static_cast<transaction::ObPartTransCtx*>(tmp_ctx);
    }
  }
  return ret;
}

int ObLSTxCtxMgr::remove_callback_for_uncommited_tx(const memtable::ObMemtableSet *memtable_set)
{
  int ret = OB_SUCCESS;
  ObTimeGuard timeguard("remove callback for uncommited txn", 10L * 1000L);

  if (IS_NOT_INIT) {
    ret = OB_NOT_INIT;
  } else if (OB_ISNULL(memtable_set)) {
    ret = OB_INVALID_ARGUMENT;
  } else {
    ObRemoveCallbackFunctor fn(memtable_set);
    if (OB_FAIL(ls_tx_ctx_map_.for_each(fn))) {
    } else {
    }
  }
  return ret;
}

int ObLSTxCtxMgr::replay_start_working_log(const ObTxStartWorkingLog &log, SCN start_working_ts)
{
  int ret = OB_SUCCESS;
  int tmp_ret = OB_SUCCESS;
  UNUSED(log);

  share::SCN tmp_applying_swl_scn;
  if (OB_FAIL(retry_apply_start_working_log())) {
    TRANS_LOG(WARN, "retry to apply start working log failed", K(ret), K(log), K(start_working_ts),
              KPC(this));
    ret = OB_EAGAIN;
  } else if (tx_ls_state_mgr_.need_retry_apply_SWL(tmp_applying_swl_scn))

  {
    if (tmp_applying_swl_scn < start_working_ts) {
      ret = OB_EAGAIN;
      TRANS_LOG(WARN, "need retry to apply prev start working log", K(ret), K(tmp_applying_swl_scn),
                K(start_working_ts), K(log), KPC(this));
    } else {
      ret = OB_ERR_UNEXPECTED;
      TRANS_LOG(ERROR, "the applying SWL scn is larger than replaying SWL scn", K(ret),
                K(tmp_applying_swl_scn), K(start_working_ts), K(log), KPC(this));
    }

  } else {

    WLockGuardWithRetryInterval guard(rwlock_, TRY_THRESOLD_US, RETRY_INTERVAL_US);
    ReplayTxStartWorkingLogFunctor fn(start_working_ts);
    if (OB_FAIL(ls_tx_ctx_map_.for_each(fn))) {
      FLOG_WARN("[LsTxCtxMgr Role Change] replay start working log failed", KR(ret),
                K(ls_id_));
    } else {
      tx_ls_state_mgr_.replay_SWL_succ(start_working_ts);
      FLOG_INFO("[LsTxCtxMgr Role Change] replay start working log success",
                K(ls_id_));
    }
  }
  return ret;
}

int ObLSTxCtxMgr::retry_apply_start_working_log()
{
  int ret = OB_SUCCESS;
  int tmp_ret = OB_SUCCESS;

  share::SCN retry_start_working_ts;

  if (tx_ls_state_mgr_.need_retry_apply_SWL(retry_start_working_ts)) {
    ret = on_start_working_log_cb_succ(retry_start_working_ts);
  }

  if (OB_TMP_FAIL(log_cb_pool_mgr_.clear_log_cb_pool(false /*for_replay*/))) {
  }
  return ret;
}

int ObLSTxCtxMgr::on_start_working_log_cb_succ(SCN start_working_ts)
{
  int ret = OB_SUCCESS;
  bool ignore_ret = false;
  WLockGuardWithRetryInterval guard(rwlock_, TRY_THRESOLD_US, RETRY_INTERVAL_US);

  share::SCN retry_start_working_ts;
  if (!tx_ls_state_mgr_.waiting_SWL_cb()) {
    TRANS_LOG(INFO, "This ls is not waiting start_working_cb. Skip the on_success operation",
              K(ret), K(tx_ls_state_mgr_));
  } else if (OB_FAIL(tx_ls_state_mgr_.switch_tx_ls_state(ObTxLSStateMgr::TxLSAction::SWL_CB_SUCC,
                                                         start_working_ts))) {
  }

  if (OB_FAIL(ret)) {
    // do nothing
  } else if (OB_FAIL(errsim_apply_start_working_log())) {
  }

  if (!tx_ls_state_mgr_.need_retry_apply_SWL(retry_start_working_ts)) {
    TRANS_LOG(INFO, "This ls need not retry apply start_working. Skip the apply operation", K(ret),
              K(tx_ls_state_mgr_));
  } else {
    if (OB_FAIL(ret)) {
      // do nothing
    } else if (is_t_pending_()) {
      SwitchToLeaderFunctor fn(start_working_ts);
      if (OB_FAIL(ls_tx_ctx_map_.for_each(fn))) {
        TRANS_LOG(WARN, "switch to leader failed", KR(ret), K(ls_id_));
        if (OB_NOT_MASTER == fn.get_ret()) {
          // ignore ret
          // PALF will switch to follower when submitting log return OB_NOT_MASTER
          ignore_ret = true;
        }
      }
    } else if (is_r_pending_()) {
      ResumeLeaderFunctor fn(start_working_ts);
      if (OB_FAIL(ls_tx_ctx_map_.for_each(fn))) {
      }
    } else if (is_f_pending_()) {
      TRANS_LOG(INFO,
                "retry to apply start working on a follower",
                KR(ret),
                K(ls_id_),
                K(tx_ls_state_mgr_));
    } else {
      ret = OB_STATE_NOT_MATCH;
      TRANS_LOG(ERROR, "unexpected state", KR(ret), K(ls_id_), K(tx_ls_state_mgr_));
    }

    if (OB_FAIL(ret)) {
      if (ignore_ret) {
        ret = OB_SUCCESS;
      }
      // TODO dingxi, takeover failed, notify palf to revoke itself
      int tmp_ret = OB_SUCCESS;
      // restore to follower
      if (OB_TMP_FAIL(tx_ls_state_mgr_.switch_tx_ls_state(
              ObTxLSStateMgr::TxLSAction::APPLY_SWL_FAIL, start_working_ts))) {
        TRANS_LOG(ERROR, "restore follower failed", KR(tmp_ret), K(ls_id_), K(tx_ls_state_mgr_));
        ret = tmp_ret;
      }
    } else {
      int tmp_ret = OB_SUCCESS;
      if (OB_FAIL(tx_ls_state_mgr_.switch_tx_ls_state(ObTxLSStateMgr::TxLSAction::APPLY_SWL_SUCC,
                                                      start_working_ts))) {
      } else {
        leader_takeover_ts_ = MonotonicTs::current_time();
        try_wait_gts_and_inc_max_commit_ts_();
      }
    }
  }
  FLOG_INFO("[LsTxCtxMgr Role Change] on_start_working_log_cb_succ", K(ret), K(start_working_ts),
            KPC(this));
  return ret;
}

int ObLSTxCtxMgr::on_start_working_log_cb_fail()
{
  int ret = OB_SUCCESS;
  WLockGuardWithRetryInterval guard(rwlock_, TRY_THRESOLD_US, RETRY_INTERVAL_US);
  if (OB_FAIL(tx_ls_state_mgr_.switch_tx_ls_state(ObTxLSStateMgr::TxLSAction::SWL_CB_FAIL))) {
  }
  FLOG_INFO("[LsTxCtxMgr Role Change] on_start_working_log_cb_fail", K(ret), KPC(this));
  return ret;
}

int ObLSTxCtxMgr::submit_start_working_log_()
{
  int ret = OB_SUCCESS;
  SCN scn;
  const int64_t fake_epoch = 0xbaba;
  if (OB_FAIL(errsim_submit_start_working_log())) {
  } else if (OB_FAIL(ls_log_writer_.submit_start_working_log(fake_epoch, scn))) {
  }
  return ret;
}

int ObLSTxCtxMgr::switch_to_follower_forcedly()
{
  int ret = OB_SUCCESS;
  int tmp_ret = OB_SUCCESS;
  ObTimeGuard timeguard("ObLSTxCtxMgr::switch_to_follower_forcedly");
  ObTxCommitCallback *cb_list = NULL;
  {
    WLockGuardWithRetryInterval guard(rwlock_, TRY_THRESOLD_US, RETRY_INTERVAL_US);
    if (IS_NOT_INIT) {
      ret = OB_NOT_INIT;
    } else if (is_follower_()) {
      // already follower, do nothing
    } else if (OB_FAIL(tx_ls_state_mgr_.switch_tx_ls_state(ObTxLSStateMgr::TxLSAction::LEADER_REVOKE_FORCEDLLY))) {
    } else {
      SwitchToFollowerForcedlyFunctor fn(cb_list);
      if (OB_FAIL(ls_tx_ctx_map_.for_each(fn))) {
      } else {
        is_leader_serving_ = false;
      }

      if (OB_FAIL(ret)) {
        tx_ls_state_mgr_.restore_tx_ls_state();
      }
    }
  }
  if (OB_TMP_FAIL(log_cb_pool_mgr_.clear_log_cb_pool(false /*for_replay*/))) {
  }
  timeguard.click();
  // run callback out of lock, ignore ret
  (void)process_callback_(cb_list);
  if (timeguard.get_diff() > 3 * 1000000) {
    TRANS_LOG_RET(WARN, OB_ERR_TOO_MUCH_TIME, "switch_to_follower_forcedly use too much time", K(timeguard), "manager", *this);
  }
  FLOG_INFO("[LsTxCtxMgr Role Change] switch_to_follower_forcedly", K(ret), KPC(this));
  return ret;
}

int ObLSTxCtxMgr::try_wait_gts_and_inc_max_commit_ts_()
{
  int ret = OB_SUCCESS;
  if (!is_leader_serving_) {
    SCN gts;
    MonotonicTs receive_gts_ts(0);

    if (OB_FAIL(ts_mgr_->get_gts(leader_takeover_ts_,
                                 nullptr,
                                 gts,
                                 receive_gts_ts))) {
      if(OB_EAGAIN != ret) {
        TRANS_LOG(WARN, "wait gts error", KR(ret), K_(ls_id),
            K_(max_replay_commit_version));
      } else {
        ret = OB_NOT_MASTER;
      }
    } else {
      if (max_replay_commit_version_.is_valid() && max_replay_commit_version_ >= gts) {
        ret = OB_NOT_MASTER;
      } else {
        is_leader_serving_ = true;
        txs_->get_tx_version_mgr().update_max_commit_ts(gts, false);
      }
    }
    TRANS_LOG(INFO, "try wait gts", KR(ret), K_(ls_id),
        K_(max_replay_commit_version), K(gts));
  }
  return ret;
}

// TODO dingxi, add dup table related logic
int ObLSTxCtxMgr::switch_to_leader()
{
  int ret = OB_SUCCESS;
  int tmp_ret = OB_SUCCESS;

  if (OB_FAIL(retry_apply_start_working_log())) {
  } else {
    WLockGuardWithRetryInterval guard(rwlock_, TRY_THRESOLD_US, RETRY_INTERVAL_US);
    if (IS_NOT_INIT) {
      ret = OB_NOT_INIT;
      TRANS_LOG(WARN, "not init", KR(ret), K(ls_id_));
    } else if (OB_FAIL(tx_ls_state_mgr_.switch_tx_ls_state(
                   ObTxLSStateMgr::TxLSAction::LEADER_TAKEOVER))) {
    } else {
      if (OB_FAIL(submit_start_working_log_())) {
      }
      if (OB_FAIL(ret)) {
        tx_ls_state_mgr_.restore_tx_ls_state();
      }
    }
  }

  if (OB_TMP_FAIL(log_cb_pool_mgr_.switch_to_leader(get_active_tx_count()))) {
  }

  FLOG_INFO("[LsTxCtxMgr Role Change] switch_to_leader", K(ret), KPC(this));
  return ret;
}

int ObLSTxCtxMgr::switch_to_follower_gracefully()
{
  int ret = OB_SUCCESS;
  int tmp_ret = OB_SUCCESS;
  ObTimeGuard timeguard("switch_to_follower_gracefully");
  int64_t start_time = ObTimeUtility::current_time();
  int64_t process_count = 0;
  while (OB_SUCC(ret) && is_pending_()) {
    if (ObTimeUtility::current_time() - start_time >= WAIT_SW_CB_TIMEOUT) {
      ret = OB_TIMEOUT;
      TRANS_LOG(WARN, "start working cb waiting timeout", K(ret), KPC(this));
      if (tx_ls_state_mgr_.is_start_working_apply_pending()) {
        ret = OB_LS_NEED_REVOKE;
        TRANS_LOG(WARN, "apply start working log failed with waiting timeout, need revoke", K(ret), KPC(this));
      }
    } else {
      ob_usleep(WAIT_SW_CB_INTERVAL);
    }
  }
  timeguard.click();

  ObTxCommitCallback *cb_list = NULL;
  if (OB_SUCC(ret)) {
    WLockGuardWithRetryInterval guard(rwlock_, TRY_THRESOLD_US, RETRY_INTERVAL_US);
    timeguard.click();

    if (OB_FAIL(ret)) {
      // do nothing
    } else if (IS_NOT_INIT) {
      ret = OB_NOT_INIT;
      TRANS_LOG(WARN, "not init", KR(ret), K(ls_id_));
    } else if (OB_FAIL(tx_ls_state_mgr_.switch_tx_ls_state(
                   ObTxLSStateMgr::TxLSAction::LEADER_REVOKE_GRACEFULLY))) {
    } else {
      timeguard.click();
      // TODO
      const int64_t abs_expired_time = INT64_MAX;
      SwitchToFollowerGracefullyFunctor fn(abs_expired_time, cb_list);
      if (OB_FAIL(ls_tx_ctx_map_.for_each(fn))) {
        TRANS_LOG(WARN, "for each tx ctx error", KR(ret), "manager", *this);
        ret = fn.get_ret();
      } else if (OB_FAIL(errsim_switch_to_followr_gracefully())) {
      }
      process_count = fn.get_count();
      timeguard.click();
      if (OB_FAIL(ret)) {
        int tmp_ret = OB_SUCCESS;
        if (OB_TMP_FAIL(
                tx_ls_state_mgr_.switch_tx_ls_state(ObTxLSStateMgr::TxLSAction::RESUME_LEADER))) {
        } else if (OB_TMP_FAIL(submit_start_working_log_())) {
        }
        if (OB_SUCCESS != tmp_ret) {
          //Use a processing method that is compatible with the old code,
          //treating the situation as the on_failure of a start working log.
          tx_ls_state_mgr_.switch_tx_ls_state(ObTxLSStateMgr::TxLSAction::SWL_CB_FAIL);
          ret = OB_LS_NEED_REVOKE;
        }
        TRANS_LOG(WARN, "switch to follower failed", KR(ret), KR(tmp_ret), K(*this));
      } else {
        is_leader_serving_ = false;
        // TRANS_LOG(INFO, "switch to follower gracefully success", K(*this));
      }
      timeguard.click();
    }
  }
  
  if (OB_TMP_FAIL(log_cb_pool_mgr_.clear_log_cb_pool(false /*for_replay*/))) {
  }

  (void)process_callback_(cb_list);
  timeguard.click();
  FLOG_INFO("[LsTxCtxMgr Role Change] switch_to_follower_gracefully", K(ret), KPC(this),
            K(process_count));
  if (timeguard.get_diff() > 1000000) {
  }
  return ret;
}

// CALLED when RoleChangeService try to rollback after succeed calling of `switch_to_follower_gracefully`
int ObLSTxCtxMgr::resume_leader()
{
  int ret = OB_SUCCESS;
  if (OB_FAIL(retry_apply_start_working_log())) {
  } else {
    WLockGuardWithRetryInterval guard(rwlock_, TRY_THRESOLD_US, RETRY_INTERVAL_US);
    if (IS_NOT_INIT) {
      ret = OB_NOT_INIT;
      TRANS_LOG(WARN, "not init", KR(ret), K(ls_id_));
    } else if (OB_FAIL(tx_ls_state_mgr_.switch_tx_ls_state(
                   ObTxLSStateMgr::TxLSAction::RESUME_LEADER))) {
    } else {
      // previous active info logs will be filter by start_working_ts in part_ctx
      if (OB_FAIL(submit_start_working_log_())) {
      }
      if (OB_FAIL(ret)) {
        tx_ls_state_mgr_.restore_tx_ls_state();
        TRANS_LOG(WARN, "resume leader failed", KR(ret), K(*this));
      } else {
        is_leader_serving_ = true;
      }
    }
  }
  TRANS_LOG(INFO, "[LsTxCtxMgr Role Change] resume_leader", K(ret), KPC(this));
  return ret;
}

bool ObLSTxCtxMgr::in_leader_serving_state()
{
  bool bool_ret = false;
  int ret = OB_SUCCESS;
  RLockGuard guard(rwlock_);

  if (IS_NOT_INIT) {
  } else if (!is_master_()) {
  } else if (OB_FAIL(try_wait_gts_and_inc_max_commit_ts_())) {
  } else {
    bool_ret = true;
  }
  return bool_ret;
}

int ObLSTxCtxMgr::stop(const bool graceful)
{
  int ret = OB_SUCCESS;
  ObTxCommitCallback *cb_list = NULL;
  const KillTransArg arg(graceful);
  ObTimeGuard timeguard("ctxmgr stop");
  {
    WLockGuardWithRetryInterval guard(rwlock_, TRY_THRESOLD_US, RETRY_INTERVAL_US);
    const int64_t total_active_readonly_request_count = get_total_active_readonly_request_count();
    if (!graceful && total_active_readonly_request_count > 0) {
      ret = OB_EAGAIN;
      TRANS_LOG(WARN, "readonly requests are active", K(ret), K(total_active_readonly_request_count));
    } else if (OB_FAIL(ls_log_writer_.stop())) {
    } else {
      {
        WLockGuard guard(minor_merge_lock_);
        if (OB_FAIL(tx_ls_state_mgr_.switch_tx_ls_state(ObTxLSStateMgr::TxLSAction::STOP))) {
        }
      }

      if (OB_SUCC(ret)) {
        KillTxCtxFunctor fn(arg, cb_list);
        fn.set_release_audit_mgr_lock(true);
        if (OB_FAIL(ls_retain_ctx_mgr_.force_gc_retain_ctx())) {
        } else if (OB_FAIL(ls_tx_ctx_map_.for_each(fn))) {
        }
        if (OB_FAIL(ret)) {
          tx_ls_state_mgr_.restore_tx_ls_state();
        }
      }
    }
  }
  if (timeguard.get_diff() > 3 * 1000000) {
    TRANS_LOG_RET(WARN, OB_ERR_TOO_MUCH_TIME, "stop trans use too much time", K(timeguard), "manager", *this);
  }
  process_callback_(cb_list);
  return ret;
}

int ObLSTxCtxMgr::kill_all_tx(const bool graceful, bool &is_all_tx_cleaned_up)
{
  int ret = OB_SUCCESS;
  ObTimeGuard timeguard("ctxmgr kill_all_tx");
  ObTxCommitCallback *cb_list = NULL;
  const KillTransArg arg(graceful);
  {
    WLockGuardWithRetryInterval guard(rwlock_, TRY_THRESOLD_US, RETRY_INTERVAL_US);
    const int64_t total_active_readonly_request_count = get_total_active_readonly_request_count();
    KillTxCtxFunctor fn(arg, cb_list);
    if (OB_FAIL(ls_retain_ctx_mgr_.force_gc_retain_ctx())) {
    } else if (OB_FAIL(ls_tx_ctx_map_.for_each(fn))) {
    }
    is_all_tx_cleaned_up = (get_tx_ctx_count_() == 0);
  }
  if (timeguard.get_diff() > 3 * 1000000) {
    TRANS_LOG_RET(WARN, OB_ERR_TOO_MUCH_TIME, "kill_all_tx use too much time", K(timeguard), "manager", *this);
  }
  (void)process_callback_(cb_list);
  return ret;
}

int ObLSTxCtxMgr::block_tx(bool &is_all_tx_cleaned_up)
{
  int ret = OB_SUCCESS;
  WLockGuardWithRetryInterval guard(rwlock_, TRY_THRESOLD_US, RETRY_INTERVAL_US);

  if (is_stopped_()) {
  } else if (OB_FAIL(tx_ls_state_mgr_.switch_tx_ls_state(ObTxLSStateMgr::TxLSAction::BLOCK_START_TX))) {
  } else {
    is_all_tx_cleaned_up = (get_tx_ctx_count() == 0);
  }
  TRANS_LOG(INFO, "block ls", K(ret), "manager", *this);
  return ret;
}

int ObLSTxCtxMgr::block_all(bool &is_all_tx_cleaned_up)
{
  int ret = OB_SUCCESS;
  WLockGuardWithRetryInterval guard(rwlock_, TRY_THRESOLD_US, RETRY_INTERVAL_US);

  if (is_stopped_()) {
  } else if (OB_FAIL(tx_ls_state_mgr_.switch_tx_ls_state(ObTxLSStateMgr::TxLSAction::BLOCK_START_WR))) {
  } else {
    is_all_tx_cleaned_up = (get_tx_ctx_count() == 0);
  }
  TRANS_LOG(INFO, "block ls", K(ret), "manager", *this);
  return ret;
}

int ObLSTxCtxMgr::block_normal(bool &is_all_tx_cleaned_up)
{
  int ret = OB_SUCCESS;
  WLockGuardWithRetryInterval guard(rwlock_, TRY_THRESOLD_US, RETRY_INTERVAL_US);

  if (OB_FAIL(tx_ls_state_mgr_.switch_tx_ls_state(ObTxLSStateMgr::TxLSAction::BLOCK_START_NORMAL_TX))) {
  } else {
    is_all_tx_cleaned_up = (get_tx_ctx_count() == 0);
  }
  TRANS_LOG(INFO, "block ls normally", K(ret), "manager", *this);
  return ret;
}

int ObLSTxCtxMgr::online()
{
  int ret = OB_SUCCESS;
  WLockGuardWithRetryInterval guard(rwlock_, TRY_THRESOLD_US, RETRY_INTERVAL_US);

  if (OB_FAIL(tx_ls_state_mgr_.switch_tx_ls_state(ObTxLSStateMgr::TxLSAction::ONLINE))) {
  } else {
    online_ts_ = ObTimeUtility::current_time();
  }
  TRANS_LOG(INFO, "online ls", K(ret), "manager", *this);
  return ret;
}

int ObLSTxCtxMgr::unblock_normal()
{
  int ret = OB_SUCCESS;
  WLockGuardWithRetryInterval guard(rwlock_, TRY_THRESOLD_US, RETRY_INTERVAL_US);

  if (OB_FAIL(tx_ls_state_mgr_.switch_tx_ls_state(ObTxLSStateMgr::TxLSAction::UNBLOCK_NORMAL_TX))) {
  }
  TRANS_LOG(INFO, "unblock ls normally", K(ret), "manager", *this);
  return ret;
}

int ObLSTxCtxMgr::get_ls_min_uncommit_tx_prepare_version(SCN &min_prepare_version)
{
  int ret = OB_SUCCESS;

  if (ATOMIC_LOAD(&total_tx_ctx_count_) > 0 || ls_tx_ctx_map_.count() > 0) {
    IterateMinPrepareVersionFunctor fn;
    if (OB_FAIL(ls_tx_ctx_map_.for_each(fn))) {
    } else {
      min_prepare_version = fn.get_min_prepare_version();
    }
  } else {
    min_prepare_version.set_max();
  }

  return ret;
}

int ObLSTxCtxMgr::get_min_undecided_scn(SCN &scn)
{
  int ret = OB_SUCCESS;
  ObGetMinUndecidedLogTsFunctor fn;
  if (OB_FAIL(ls_tx_ctx_map_.for_each(fn))) {
  } else {
    scn = fn.get_min_undecided_scn();
  }
  return ret;
}

int ObLSTxCtxMgr::check_scheduler_status(SCN &min_start_scn, MinStartScnStatus &status)
{
  int ret = OB_SUCCESS;
  ObTimeGuard tg("ObLSTxCtxMgr::check_scheduler_status", 100000);

  IteratePartCtxAskSchedulerStatusFunctor functor;
  if (OB_FAIL(ls_tx_ctx_map_.for_each(functor))) {
  } else if (!min_start_scn.is_valid()) {
    // The default min_start_scn must be valid, or skip writting HAS_CTX/NO_CTX CLOG
    status = MinStartScnStatus::UNKOWN;
  } else {
    // use smaller one between max_decided_scn and min_start_scn of all tx ctx
    min_start_scn = std::min(min_start_scn, functor.get_min_start_scn());

    status = functor.get_min_start_status();
  }

  return ret;
}

int ObLSTxCtxMgr::get_max_decided_scn(share::SCN &scn)
{
  RLockGuard guard(rwlock_);

  int ret = OB_SUCCESS;
  if (IS_NOT_INIT) {
    ret = OB_NOT_INIT;
    // There is no need to check whether it is master
    // this interface is called by leader or follower
  } else if (is_stopped_()) {
    ret = OB_STATE_NOT_MATCH;
  } else if (OB_FAIL(tx_log_adapter_->get_max_decided_scn(scn))) {
  }
  return ret;
}

int ObLSTxCtxMgr::check_modify_schema_elapsed(const ObTabletID &tablet_id,
                                              const int64_t schema_version,
                                              ObTransID &block_tx_id)
{
  int ret = OB_SUCCESS;

  ObTimeGuard timeguard("ObLSTxCtxMgr::check_modify_schema_elapsed");

  if (IS_NOT_INIT) {
    ret = OB_NOT_INIT;
    // There is no need to check whether it is master
    // this interface is called by leader or follower
  } else {
    IterateCheckTabletModifySchema fn(tablet_id, schema_version);
    if (OB_FAIL(ls_tx_ctx_map_.for_each(fn))) {
    }
    // NOTE: for_each may return OB_EAGAIN if the iter break but not end.
    ret = OB_SUCC(ret) ? fn.get_ret_code() : ret;
    block_tx_id = fn.get_tx_id();
  }
  if (timeguard.get_diff() > 3 * 1000000) {
    TRANS_LOG_RET(WARN, OB_ERR_TOO_MUCH_TIME, "ObLSTxCtxMgr::check_modify_schema_elapsed use too much time",
              K(timeguard), "manager", *this);
  }

  return ret;
}

int ObLSTxCtxMgr::check_modify_time_elapsed(const ObTabletID &tablet_id,
                                            const int64_t timestamp,
                                            ObTransID &block_tx_id)
{
  int ret = OB_SUCCESS;

  ObTimeGuard timeguard("ObLSTxCtxMgr::check_modify_time_elapsed");

  if (IS_NOT_INIT) {
    ret = OB_NOT_INIT;
  } else {
    IterateCheckTabletModifyTimestamp fn(tablet_id, timestamp);
    if (OB_FAIL(ls_tx_ctx_map_.for_each(fn))) {
    }
    // NOTE: for_each may return OB_EAGAIN if the iter break but not end.
    ret = OB_SUCC(ret) ? fn.get_ret_code() : ret;
    block_tx_id = fn.get_tx_id();
  }
  if (timeguard.get_diff() > 3 * 1000000) {
    TRANS_LOG_RET(WARN, OB_ERR_TOO_MUCH_TIME, "ObLSTxCtxMgr::check_modify_time_elapsed use too much time",
              K(timeguard), "manager", *this);
  }

  return ret;
}

int ObLSTxCtxMgr::iterate_tx_obj_lock_op(ObLockOpIterator &iter)
{
  int ret = OB_SUCCESS;

  if (IS_NOT_INIT) {
    ret = OB_NOT_INIT;
  } else {
    IterateTxObjLockOpFunctor fn(iter);
    if (OB_FAIL(ls_tx_ctx_map_.for_each(fn))) {
    }
  }

  return ret;
}

int ObLSTxCtxMgr::iterate_tx_lock_stat(ObTxLockStatIterator &tx_lock_stat_iter)
{
  int ret = OB_SUCCESS;

  if (IS_NOT_INIT) {
    ret = OB_NOT_INIT;
  } else {
    IterateTxLockStatFunctor fn(tx_lock_stat_iter);
    if (OB_FAIL(ls_tx_ctx_map_.for_each(fn))) {
    }
  }

  return ret;
}

int ObLSTxCtxMgr::iterate_tx_ctx_stat(ObTxStatIterator &tx_stat_iter)
{
  int ret = OB_SUCCESS;

  if (IS_NOT_INIT) {
    ret = OB_NOT_INIT;
  } else {
    IterateTxStatFunctor fn(tx_stat_iter);
    if (OB_FAIL(ls_tx_ctx_map_.for_each(fn))) {
      // rewrite eagain to real ret
      ret = fn.get_ret();
      TRANS_LOG(WARN, "for each transaction context error", KR(ret), "manager", *this);
    }
  }

  return ret;
}

int ObLSTxCtxMgr::revert_tx_ctx(ObPartTransCtx *ctx)
{
  return revert_tx_ctx_without_lock(ctx);
}

int ObLSTxCtxMgr::revert_tx_ctx(ObTransCtx *ctx)
{
  return revert_tx_ctx_without_lock(ctx);
}

int ObLSTxCtxMgr::revert_tx_ctx_without_lock(ObTransCtx *ctx)
{
  int ret = OB_SUCCESS;

  if (OB_ISNULL(ctx)) {
    ret = OB_INVALID_ARGUMENT;
  } else {
    ls_tx_ctx_map_.revert(ctx);
  }
  return ret;
}

int ObLSTxCtxMgr::del_tx_ctx(ObTransCtx *ctx)
{
  int ret = OB_SUCCESS;

  if (IS_NOT_INIT) {
    ret = OB_NOT_INIT;
  } else if (OB_ISNULL(ctx)) {
    ret = OB_INVALID_ARGUMENT;
  } else if ((static_cast<ObPartTransCtx *>(ctx))->get_retain_cause() != RetainCause::UNKOWN) {
    ret = OB_SUCCESS;
  } else {
    ls_tx_ctx_map_.del(ctx->get_trans_id(), ctx);
  }

  return ret;
}

int ObLSTxCtxMgr::traverse_tx_to_submit_redo_log(ObTransID &fail_tx_id, const uint32_t freeze_clock)
{
  int ret = OB_SUCCESS;
  RLockGuard guard(rwlock_);
  ObTxSubmitLogFunctor fn(ObTxSubmitLogFunctor::SUBMIT_REDO_LOG, freeze_clock);
  if (is_follower_()) {
    // quit submit log because this is a follower
  } else if (OB_FAIL(ls_tx_ctx_map_.for_each(fn))) {
    if (OB_SUCCESS != fn.get_result()) {
      // get real ret code
      ret = fn.get_result();
    }
    TRANS_LOG(WARN, "failed to submit log", K(ret));
  } else {
    TRANS_LOG(INFO, "traverse tx to submit redo log finish", K(ret), K(freeze_clock));
  }

  fail_tx_id = fn.get_fail_tx_id();

  return ret;
}

int ObLSTxCtxMgr::traverse_tx_to_submit_next_log()
{
  int ret = OB_SUCCESS;
  RLockGuard guard(rwlock_);
  ObTxSubmitLogFunctor fn(ObTxSubmitLogFunctor::SUBMIT_NEXT_LOG);
  if (!is_follower_() && OB_FAIL(ls_tx_ctx_map_.for_each(fn))) {
    if (OB_SUCCESS != fn.get_result()) {
      // get real ret code
      ret = fn.get_result();
    }
    TRANS_LOG(WARN, "failed to submit log", K(ret));
  }

  return ret;
}

// Caution: do not lock rwlock to avoid deadlock
int ObLSTxCtxMgr::check_with_tx_data(const ObTransID& tx_id, ObITxDataCheckFunctor &fn)
{
  int ret = OB_SUCCESS;
  ObPartTransCtx *tx_ctx = NULL;

  if (OB_FAIL(get_tx_ctx_(tx_id,
                          true, /*for_replay*/
                          tx_ctx))) {
    if (ret == OB_TRANS_CTX_NOT_EXIST) {
      // this tx ctx is not exist
    } else {
      TRANS_LOG(WARN, "failed to get tx ctx", KR(ret));
    }
  } else if (OB_ISNULL(tx_ctx)) {
    ret = OB_ERR_UNEXPECTED;
    TRANS_LOG(WARN, "unexpected error", K(ret));
  } else {
    if (OB_FAIL(tx_ctx->check_with_tx_data(fn))) {
      if (OB_TRANS_CTX_NOT_EXIST == ret) {
        TRANS_LOG(DEBUG, "failed to check tx status", KR(ret));
      } else {
        TRANS_LOG(WARN, "failed to check tx status", KR(ret));
      }
    }

    int tmp_ret = OB_SUCCESS;
    if (OB_SUCCESS != (tmp_ret = revert_tx_ctx_without_lock(tx_ctx))) {
    }
  }

  return ret;
}

int ObLSTxCtxMgr::get_rec_scn(SCN &rec_scn)
{
  int ret = OB_SUCCESS;

  RLockGuard guard(rwlock_);

  if (IS_NOT_INIT) {
    ret = OB_NOT_INIT;
  } else if (is_stopped()) {
    ret = OB_STATE_NOT_MATCH;
  } else {
    GetRecLogTSFunctor fn;
    if (OB_FAIL(fn.init())) {
    } else if (OB_FAIL(ls_tx_ctx_map_.for_each(fn))) {
    } else {
      SCN aggre_rec_scn = get_aggre_rec_scn_();
      rec_scn = SCN::min(fn.get_rec_log_ts(), aggre_rec_scn);
    }
  }

  return ret;
}

int ObLSTxCtxMgr::on_tx_ctx_table_flushed()
{
  int ret = OB_SUCCESS;

  RLockGuard guard(rwlock_);

  if (IS_NOT_INIT) {
    ret = OB_NOT_INIT;
  } else if (is_stopped()) {
    ret = OB_STATE_NOT_MATCH;
  } else {
    OnTxCtxTableFlushedFunctor fn;
    if (OB_FAIL(fn.init())) {
    } else if (OB_FAIL(ls_tx_ctx_map_.for_each(fn))) {
    } else {
      // To mark the checkpoint is succeed, we reset the prev_aggre_rec_scn
      prev_aggre_rec_scn_.reset();
    }
  }
  return ret;
}

int ObLSTxCtxMgr::get_min_start_scn(SCN &min_start_scn)
{
  int ret = OB_SUCCESS;

  GetMinStartSCNFunctor fn;
  if (OB_FAIL(ls_tx_ctx_map_.for_each(fn))) {
  } else {
    min_start_scn = fn.get_min_start_scn();
  }

  return ret;
}

SCN ObLSTxCtxMgr::get_aggre_rec_scn_()
{
  SCN ret;
  SCN prev_aggre_rec_scn = prev_aggre_rec_scn_.atomic_get();
  SCN aggre_rec_scn = aggre_rec_scn_.atomic_get();

  // Before the checkpoint of the tx ctx table is succeed, we should still use
  // the prev_aggre_log_ts. And after successfully checkpointed, we can use the
  // new aggre_rec_scn if exist
  if (prev_aggre_rec_scn.is_valid() &&
      aggre_rec_scn.is_valid()) {
    ret = MIN(prev_aggre_rec_scn, aggre_rec_scn);
  } else if (prev_aggre_rec_scn.is_valid()) {
    ret = prev_aggre_rec_scn;
  } else if (aggre_rec_scn.is_valid()) {
    ret = aggre_rec_scn;
  } else {
    ret.set_max();
  }

  return ret;
}

int ObLSTxCtxMgr::refresh_aggre_rec_scn()
{
  int ret = OB_SUCCESS;
  WLockGuardWithRetryInterval guard(rwlock_, TRY_THRESOLD_US, RETRY_INTERVAL_US);

  if (!prev_aggre_rec_scn_.is_valid()) {
    // We should remember the rec_log_ts before the tx ctx table is successfully
    // checkpointed
    SCN old_v;
    SCN new_v;
    do {
      old_v = aggre_rec_scn_;
      new_v.reset();
    } while (aggre_rec_scn_.atomic_vcas(old_v, new_v) != old_v);

    prev_aggre_rec_scn_ = old_v;
  } else {
  }

  return ret;
}

int ObLSTxCtxMgr::update_aggre_log_ts_wo_lock(SCN rec_scn)
{
  int ret = OB_SUCCESS;

  if (rec_scn.is_valid()) {
    // we cannot lock here, because the lock order must be
    // ObLSTxCtxMgr -> ObPartTransCtx, otherwise we may be
    // deadlocked
    SCN old_v;
    SCN new_v;
    do {
      old_v = aggre_rec_scn_;
      if (!old_v.is_valid()) {
        new_v = rec_scn;
      } else {
        new_v = MIN(old_v, rec_scn);
      }
    } while (aggre_rec_scn_.atomic_vcas(old_v, new_v) != old_v);
  }

  return ret;
}

int ObLSTxCtxMgr::dump_single_tx_data_2_text(const int64_t tx_id_int, FILE *fd)
{
  int ret = OB_SUCCESS;

  ObPartTransCtx *tx_ctx = NULL;
  ObTransID tx_id(tx_id_int);

  if (OB_ISNULL(fd)) {
    ret = OB_INVALID_ARGUMENT;
    TRANS_LOG(WARN, "invalid fd to dump tx data", KR(ret));
  } else if (OB_FAIL(get_tx_ctx_(tx_id,
                          true, /*for_replay*/
                          tx_ctx))) {
    if (ret == OB_TRANS_CTX_NOT_EXIST) {
      // this tx ctx is not exist
    } else {
      TRANS_LOG(WARN, "failed to get tx ctx", KR(ret));
    }
  } else if (OB_ISNULL(tx_ctx)) {
    ret = OB_ERR_UNEXPECTED;
    TRANS_LOG(WARN, "unexpected error", K(ret));
  } else {
    if (OB_FAIL(tx_ctx->dump_2_text(fd))) {
      if (OB_TRANS_CTX_NOT_EXIST == ret) {
        TRANS_LOG(DEBUG, "failed to dump single tx data", KR(ret));
      } else {
        TRANS_LOG(WARN, "failed to dump single tx data", KR(ret));
      }
    }

    int tmp_ret = OB_SUCCESS;
    if (OB_SUCCESS != (tmp_ret = revert_tx_ctx_without_lock(tx_ctx))) {
    }
  }
  return ret;
}

int ObLSTxCtxMgr::start_readonly_request()
{
  int ret = OB_SUCCESS;
  RLockGuard guard(rwlock_);

  if (IS_NOT_INIT) {
    ret = OB_NOT_INIT;
  } else if (is_all_blocked_()) {
    // Single-node single-replica: no replica blacklist; just reject the
    // readonly request because the log stream is blocked.
    ret = OB_PARTITION_IS_BLOCKED;
    // readonly read must be blocked, because trx may be killed forcely
    TRANS_LOG(WARN, "logstream is blocked", K(ret));
  } else {
    inc_total_active_readonly_request_count();
  }
  return ret;
}

int ObLSTxCtxMgr::end_readonly_request()
{
  if (is_all_blocked_()) {
  }
  dec_total_active_readonly_request_count();
  return OB_SUCCESS;
}

int ObTxCtxMgr::remove_all_ls_()
{
  int ret = OB_SUCCESS;

  RemoveLSFunctor fn;
  if (OB_FAIL(remove_if_(fn))) {
  }

  return ret;
}

int ObTxCtxMgr::stop_ls_(const ObLSID &ls_id, const bool graceful)
{
  int ret = OB_SUCCESS;
  ObLSTxCtxMgr *ls_tx_ctx_mgr = NULL;

  if (OB_UNLIKELY(!ls_id.is_valid())) {
    ret = OB_INVALID_ARGUMENT;
    TRANS_LOG(WARN, "invalid argument", K(ret), K(ls_id));
  } else if (OB_FAIL(get_ls_tx_ctx_mgr(ls_id, ls_tx_ctx_mgr))) {
    ret = OB_PARTITION_NOT_EXIST;
    TRANS_LOG(WARN, "ls not exist", K(ret), K(ls_id));
  } else {
    if (OB_FAIL(ls_tx_ctx_mgr->stop(graceful))) {
    } else {
    }
    revert_ls_tx_ctx_mgr(ls_tx_ctx_mgr);
  }
  return ret;
}

int ObTxCtxMgr::wait_ls_(const ObLSID &ls_id)
{
  int ret = OB_SUCCESS;
  int64_t count = 0;
  ObLSTxCtxMgr *ls_tx_ctx_mgr = NULL;
  const int64_t PRINT_LOG_INTERVAL = 1000 * 1000;

  if (OB_UNLIKELY(!ls_id.is_valid())) {
    ret = OB_INVALID_ARGUMENT;
    TRANS_LOG(WARN, "invalid argument", K(ret), K(ls_id));
    // check transaction context before removing ls
  } else if (OB_FAIL(get_ls_tx_ctx_mgr(ls_id, ls_tx_ctx_mgr))) {
    ret = OB_PARTITION_NOT_EXIST;
    TRANS_LOG(WARN, "ls not exist", K(ret), K(ls_id));
  } else {
    if (OB_UNLIKELY(!ls_tx_ctx_mgr->is_stopped())) {
      ret = OB_PARTITION_IS_NOT_STOPPED;
      TRANS_LOG(WARN, "ls has not been stopped", K(ret), K(ls_id));
    } else if ((count = ls_tx_ctx_mgr->get_tx_ctx_count()) > 0) {
      if (REACH_TIME_INTERVAL(PRINT_LOG_INTERVAL)) {
      }
      ret = OB_EAGAIN;
    } else if (OB_FAIL(ls_tx_ctx_mgr->get_ls_log_writer()->wait())) {
    } else {
    }
    revert_ls_tx_ctx_mgr(ls_tx_ctx_mgr);
  }
  return ret;
}

int ObTxCtxMgr::init(ObTsMgr *ts_mgr,
                     ObTransService *txs)
{
  int ret = OB_SUCCESS;
  int64_t i = 0;

  if (OB_UNLIKELY(is_inited_)) {
    ret = OB_INIT_TWICE;
  } else if (OB_FAIL(ls_tx_ctx_mgr_map_.init(lib::ObMemAttr("TxCtxMgr")))) {
  } else if (OB_ISNULL(ts_mgr)) {
    ret = OB_ERR_UNEXPECTED;
  } else {
    
    ts_mgr_ = ts_mgr;
    txs_ = txs;
    is_inited_ = true;
  }

  return ret;
}

int ObTxCtxMgr::start()
{
  int ret = OB_SUCCESS;

  if (IS_NOT_INIT) {
    ret = OB_NOT_INIT;
  } else if (is_running_) {
    ret = OB_ERR_UNEXPECTED;
  } else {
    is_running_ = true;
  }

  return ret;
}

int ObTxCtxMgr::stop()
{
  int ret = OB_SUCCESS;

  if (IS_NOT_INIT) {
    ret = OB_NOT_INIT;
  } else if (!is_running_) {
    ret = OB_NOT_RUNNING;
  } else {
    StopLSFunctor fn;
    if (OB_FAIL(foreach_ls_(fn))) {
    } else {
      is_running_ = false;
    }
  }
  return ret;
}

int ObTxCtxMgr::print_all_ls_tx_ctx_()
{
  int ret = OB_SUCCESS;

  PrintAllLSTxCtxFunctor fn;
  if (OB_FAIL(foreach_ls_(fn))) {
  }
  return ret;
}

int ObTxCtxMgr::wait()
{
  int ret = OB_SUCCESS;
  int tmp_ret = OB_SUCCESS;
  const int64_t SLEEP_US = 100 * 1000;
  const int64_t MAX_WAIT_RETRY_TIMES = 10;

  if (IS_NOT_INIT) {
    ret = OB_NOT_INIT;
  } else if (OB_UNLIKELY(is_running_)) {
    ret = OB_ERR_UNEXPECTED;
  } else {
    int64_t retry = 0;
    for (; OB_SUCCESS == ret && retry < MAX_WAIT_RETRY_TIMES; ++retry) {
      {
        int64_t retry_count = 0;

        WaitLSFunctor fn(retry_count);
        if (OB_FAIL(foreach_ls_(fn))) {
        } else if (retry_count > 0) {
          ret = OB_SUCCESS;
        } else {
          break;
        }
      }
      ObTransCond::usleep(SLEEP_US);
    }
    if (OB_FAIL(ret) || MAX_WAIT_RETRY_TIMES == retry) {
      if (OB_TMP_FAIL(print_all_ls_tx_ctx_())) {
      }
    }
  }

  return ret;
}

void ObTxCtxMgr::destroy()
{
  int tmp_ret = OB_SUCCESS;

  if (is_inited_) {
    if (OB_TMP_FAIL(remove_all_ls_())) {
      TRANS_LOG_RET(WARN, tmp_ret, "remove all ls error", K(tmp_ret));
    } else {
      
      ls_tx_ctx_mgr_map_.destroy();
      is_inited_ = false;
    }
  }
}

void ObTxCtxMgr::reset()
{
  is_running_ = false;
  
  ls_tx_ctx_mgr_map_.reset();
  ts_mgr_ = NULL;
  txs_ = NULL;
  ls_alloc_cnt_ = 0;
  ls_release_cnt_ = 0;
  is_inited_ = false;
}

int ObTxCtxMgr::revert_ls_tx_ctx_mgr(ObLSTxCtxMgr *ls_tx_ctx_mgr)
{
  int ret = OB_SUCCESS;

  if (OB_ISNULL(ls_tx_ctx_mgr)) {
    ret = OB_INVALID_ARGUMENT;
    TRANS_LOG(WARN, "invalid argument", K(ret), KP(ls_tx_ctx_mgr));
  } else {
    ls_tx_ctx_mgr_map_.revert(ls_tx_ctx_mgr);
  }

  return ret;
}

int ObTxCtxMgr::get_ls_tx_ctx_mgr(const ObLSID &ls_id, ObLSTxCtxMgr *&ls_tx_ctx_mgr)
{
  int ret = OB_SUCCESS;

  if (IS_NOT_INIT) {
    ret = OB_NOT_INIT;
  } else if (OB_UNLIKELY(!ls_id.is_valid())) {
    ret = OB_INVALID_ARGUMENT;
  } else if (OB_FAIL(ls_tx_ctx_mgr_map_.get(ls_id, ls_tx_ctx_mgr))) {
    if (OB_ENTRY_NOT_EXIST == ret) {
      ret = OB_PARTITION_NOT_EXIST;
      TRANS_LOG(TRACE, "get ls_tx_ctx_mgr error", KR(ret), K(ls_id));
    } else {
      TRANS_LOG(WARN, "get ls_tx_ctx_mgr error", KR(ret), K(ls_id));
    }
    ls_tx_ctx_mgr = NULL;
  }

  return ret;
}

int ObTxCtxMgr::get_tx_ctx(const ObLSID &ls_id,
                           const ObTransID &tx_id,
                           const bool for_replay,
                           ObPartTransCtx *&ctx)
{
  int ret = OB_SUCCESS;
  ObLSTxCtxMgr *ls_tx_ctx_mgr = NULL;

  if (IS_NOT_INIT) {
    ret = OB_NOT_INIT;
  } else if (OB_UNLIKELY(!ls_id.is_valid()) || OB_UNLIKELY(!tx_id.is_valid())) {
    ret = OB_INVALID_ARGUMENT;
  } else if (OB_UNLIKELY(!is_running_)) {
    ret = OB_NOT_RUNNING;
  } else if (OB_FAIL(get_ls_tx_ctx_mgr(ls_id, ls_tx_ctx_mgr))) {
    ret = OB_PARTITION_NOT_EXIST;
  } else {
    if (OB_FAIL(ls_tx_ctx_mgr->get_tx_ctx(tx_id, for_replay, ctx))) {
    } else if (OB_ISNULL(ctx)) {
      ret = OB_ERR_UNEXPECTED;
    } else {
    }
    revert_ls_tx_ctx_mgr(ls_tx_ctx_mgr);
  }
  return ret;
}

int ObTxCtxMgr::create_tx_ctx(const ObTxCreateArg &arg,
                              bool& existed,
                              ObPartTransCtx *&ctx) {
  int ret = OB_SUCCESS;
  ObLSTxCtxMgr *ls_tx_ctx_mgr = NULL;

  if (IS_NOT_INIT) {
    ret = OB_NOT_INIT;
  } else if (OB_UNLIKELY(!arg.is_valid())) {
    ret = OB_INVALID_ARGUMENT;
  } else if (OB_UNLIKELY(!is_running_)) {
    ret = OB_NOT_RUNNING;
  } else if (OB_FAIL(get_ls_tx_ctx_mgr(arg.ls_id_, ls_tx_ctx_mgr))) {
    ret = OB_PARTITION_NOT_EXIST;
  } else {
    if (OB_FAIL(ls_tx_ctx_mgr->create_tx_ctx(arg, existed, ctx))) {
    } else if (OB_ISNULL(ctx)) {
      ret = OB_ERR_UNEXPECTED;
    } else {
    }
    revert_ls_tx_ctx_mgr(ls_tx_ctx_mgr);
  }
  return ret;
}

int ObTxCtxMgr::revert_tx_ctx(ObPartTransCtx *ctx)
{
  int ret = OB_SUCCESS;
  ObLSTxCtxMgr *ls_tx_ctx_mgr = NULL;

  if (IS_NOT_INIT) {
    ret = OB_NOT_INIT;
  } else if (OB_ISNULL(ctx)) {
    ret = OB_INVALID_ARGUMENT;
  } else {
    // reference cannot be used here, otherwise context memory will be released
    // and core dump may occur when printing ls_id
    const ObTransID tx_id = ctx->get_trans_id();
    const ObLSID ls_id = ctx->get_ls_id();
    if (OB_FAIL(get_ls_tx_ctx_mgr(ls_id, ls_tx_ctx_mgr))) {
      ret = OB_PARTITION_NOT_EXIST;
    } else {
      if (OB_FAIL(ls_tx_ctx_mgr->revert_tx_ctx(ctx))) {
      } else {
      }
      revert_ls_tx_ctx_mgr(ls_tx_ctx_mgr);
    }
  }

  return ret;
}

int ObTxCtxMgr::block_tx(const ObLSID &ls_id, bool &is_all_tx_cleaned_up)
{
  int ret = OB_SUCCESS;
  ObLSTxCtxMgr *ls_tx_ctx_mgr = NULL;

  if (IS_NOT_INIT) {
    ret = OB_NOT_INIT;
  } else if (OB_UNLIKELY(!ls_id.is_valid())) {
    ret = OB_INVALID_ARGUMENT;
  } else if (OB_FAIL(get_ls_tx_ctx_mgr(ls_id, ls_tx_ctx_mgr))) {
    ret = OB_PARTITION_NOT_EXIST;
  } else {
    if (OB_FAIL(ls_tx_ctx_mgr->block_tx(is_all_tx_cleaned_up))) {
    } else {
    }
    revert_ls_tx_ctx_mgr(ls_tx_ctx_mgr);
  }
  return ret;
}

int ObTxCtxMgr::block_all(const ObLSID &ls_id, bool &is_all_tx_cleaned_up)
{
  int ret = OB_SUCCESS;
  ObLSTxCtxMgr *ls_tx_ctx_mgr = NULL;

  if (IS_NOT_INIT) {
    ret = OB_NOT_INIT;
  } else if (OB_UNLIKELY(!ls_id.is_valid())) {
    ret = OB_INVALID_ARGUMENT;
  } else if (OB_FAIL(get_ls_tx_ctx_mgr(ls_id, ls_tx_ctx_mgr))) {
    ret = OB_PARTITION_NOT_EXIST;
  } else {
    if (OB_FAIL(ls_tx_ctx_mgr->block_all(is_all_tx_cleaned_up))) {
    } else {
    }
    revert_ls_tx_ctx_mgr(ls_tx_ctx_mgr);
  }
  return ret;
}

int ObTxCtxMgr::clear_all_tx(const ObLSID &ls_id)
{
  int ret = OB_SUCCESS;
  const KillTransArg arg(false);
  bool is_all_tx_cleaned_up = false;
  ObLSTxCtxMgr *ls_tx_ctx_mgr = NULL;

  if (OB_UNLIKELY(!ls_id.is_valid())) {
    ret = OB_INVALID_ARGUMENT;
  } else if (OB_FAIL(get_ls_tx_ctx_mgr(ls_id, ls_tx_ctx_mgr))) {
    ret = OB_PARTITION_NOT_EXIST;
  } else {
    if (OB_FAIL(ls_tx_ctx_mgr->kill_all_tx(arg.graceful_, is_all_tx_cleaned_up))) {
    } else {
    }
    revert_ls_tx_ctx_mgr(ls_tx_ctx_mgr);
  }

  return ret;
}


int ObTxCtxMgr::iterate_all_observer_tx_stat(ObTxStatIterator &tx_stat_iter)
{
  int ret = OB_SUCCESS;

  if (IS_NOT_INIT) {
    ret = OB_NOT_INIT;
  } else {
    IterateAllLSTxStatFunctor fn(tx_stat_iter);
    if (OB_FAIL(foreach_ls_(fn))) {
      // rewrite eagain to real ret code
      ret = fn.get_ret();
      TRANS_LOG(WARN, "foreach_ls_ tx_stat error", KR(ret));
    } else {
      // do nothing
    }
  }
  return ret;
}


int ObTxCtxMgr::iterate_ls_tx_lock_stat(const ObLSID &ls_id,
                                        ObTxLockStatIterator &tx_lock_stat_iter)
{
  int ret = OB_SUCCESS;
  ObLSTxCtxMgr *ls_tx_ctx_mgr = NULL;

  if (IS_NOT_INIT) {
    ret = OB_NOT_INIT;
  } else if (OB_UNLIKELY(!ls_id.is_valid())) {
    ret = OB_INVALID_ARGUMENT;
  } else if (OB_FAIL(get_ls_tx_ctx_mgr(ls_id, ls_tx_ctx_mgr))) {
    ret = OB_PARTITION_NOT_EXIST;
  } else {
    if (OB_FAIL(ls_tx_ctx_mgr->iterate_tx_lock_stat(tx_lock_stat_iter))) {
    } else {
    }
    revert_ls_tx_ctx_mgr(ls_tx_ctx_mgr);
  }

  // ignore retcode when virtual table access error
  if (OB_PARTITION_NOT_EXIST == ret || OB_EAGAIN == ret) {
    ret = OB_SUCCESS;
  }
  return ret;
}

int ObTxCtxMgr::iterate_ls_id(ObLSIDIterator &ls_id_iter)
{
  int ret = OB_SUCCESS;

  if (IS_NOT_INIT) {
    ret = OB_NOT_INIT;
  } else {
    IterateLSIDFunctor fn(ls_id_iter);
    if (OB_FAIL(foreach_ls_(fn))) {
    } else {
      // do nothing
    }
  }

  return ret;
}

int ObTxCtxMgr::iterate_tx_ctx_mgr_stat(const ObAddr &addr,
    ObTxCtxMgrStatIterator &tx_ctx_mgr_stat_iter)
{
  int ret = OB_SUCCESS;

  if (IS_NOT_INIT) {
    ret = OB_NOT_INIT;
  } else {
    IterateLSTxCtxMgrStatFunctor fn(addr, tx_ctx_mgr_stat_iter);
    if (OB_FAIL(foreach_ls_(fn))) {
    }
  }

  return ret;
}

int ObTxCtxMgr::get_ls_min_uncommit_tx_prepare_version(const ObLSID &ls_id, SCN &min_prepare_version)
{
  int ret = OB_SUCCESS;
  ObLSTxCtxMgr *ls_tx_ctx_mgr = NULL;

  if (IS_NOT_INIT) {
    ret = OB_NOT_INIT;
  } else if (OB_UNLIKELY(!ls_id.is_valid())) {
    ret = OB_INVALID_ARGUMENT;
  } else if (OB_FAIL(get_ls_tx_ctx_mgr(ls_id, ls_tx_ctx_mgr))) {
    ret = OB_PARTITION_NOT_EXIST;
  } else {
    if (OB_FAIL(ls_tx_ctx_mgr->get_ls_min_uncommit_tx_prepare_version(min_prepare_version))) {
    } else {
    }
    revert_ls_tx_ctx_mgr(ls_tx_ctx_mgr);
  }

  return ret;
}

int ObTxCtxMgr::get_min_undecided_scn(const ObLSID &ls_id, SCN &scn)
{
  int ret = OB_SUCCESS;
  ObLSTxCtxMgr *ls_tx_ctx_mgr = NULL;

  if (IS_NOT_INIT) {
    ret = OB_NOT_INIT;
  } else if (OB_UNLIKELY(!ls_id.is_valid())) {
    ret = OB_INVALID_ARGUMENT;
  } else if (OB_FAIL(get_ls_tx_ctx_mgr(ls_id, ls_tx_ctx_mgr))) {
    ret = OB_PARTITION_NOT_EXIST;
  } else {
    if (OB_FAIL(ls_tx_ctx_mgr->get_min_undecided_scn(scn))) {
    } else {
    }
    revert_ls_tx_ctx_mgr(ls_tx_ctx_mgr);
  }
  return ret;
}

int ObTxCtxMgr::remove_callback_for_uncommited_tx(
  const ObLSID ls_id, const memtable::ObMemtableSet *memtable_set)
{
  int ret = OB_SUCCESS;
  ObLSTxCtxMgr *ls_tx_ctx_mgr = NULL;

  if (IS_NOT_INIT) {
    ret = OB_NOT_INIT;
  } else if (OB_UNLIKELY(!ls_id.is_valid())) {
    ret = OB_INVALID_ARGUMENT;
  } else if (OB_FAIL(get_ls_tx_ctx_mgr(ls_id, ls_tx_ctx_mgr))) {
    ret = OB_PARTITION_NOT_EXIST;
  } else {
    if (OB_FAIL(ls_tx_ctx_mgr->remove_callback_for_uncommited_tx(memtable_set))) {
    } else {
    }
    revert_ls_tx_ctx_mgr(ls_tx_ctx_mgr);
  }

  return ret;
}

int ObTxCtxMgr::create_ls(const ObLSID &ls_id,
                          ObTxTable *tx_table,
                          ObLockTable *lock_table,
                          ObLSTxService &ls_tx_svr,
                          ObITxLogParam *param,
                          ObITxLogAdapter *log_adapter)
{
  int ret = OB_SUCCESS;
  ObLSTxCtxMgr *ls_tx_ctx_mgr = NULL;

  if (IS_NOT_INIT) {
    ret = OB_NOT_INIT;
  } else if (OB_UNLIKELY(!ls_id.is_valid())) {
    ret = OB_INVALID_ARGUMENT;
  } else if (OB_ISNULL(ls_tx_ctx_mgr = ObLSTxCtxMgrFactory::alloc())) {
    ret = OB_ALLOCATE_MEMORY_FAILED;
  } else if (OB_FAIL(ls_tx_ctx_mgr->init(ls_id, tx_table, lock_table,
                                         ts_mgr_, txs_, param, log_adapter))) {
    TRANS_LOG(WARN, "ls_tx_ctx_mgr inited error", KR(ret), K(ls_id));
    ObLSTxCtxMgrFactory::release(ls_tx_ctx_mgr);
    ls_tx_ctx_mgr = NULL;
  } else if (OB_FAIL(ls_tx_svr.init(ls_id, ls_tx_ctx_mgr, txs_))) {
    TRANS_LOG(WARN, "ls tx service init failed", K(ret), K(ls_id));
    ObLSTxCtxMgrFactory::release(ls_tx_ctx_mgr);
    ls_tx_ctx_mgr = NULL;
  } else if (OB_FAIL(ls_tx_ctx_mgr_map_.insert_and_get(ls_id, ls_tx_ctx_mgr, NULL))) {
    TRANS_LOG(WARN, "ls_tx_ctx_mgr_map_ insert error", KR(ret), K(ls_id));
    ObLSTxCtxMgrFactory::release(ls_tx_ctx_mgr);
    ls_tx_ctx_mgr = NULL;
  } else {
    ATOMIC_INC(&ls_alloc_cnt_);
    // need to revert the trans ctx ref explicitly
    ls_tx_ctx_mgr_map_.revert(ls_tx_ctx_mgr);
  }

  return ret;
}

int ObTxCtxMgr::remove_ls(const ObLSID &ls_id, const bool graceful)
{
  int ret = OB_SUCCESS;
  const KillTransArg arg(graceful, false);
  bool need_retry = true;
  const int64_t SLEEP_US = 20000; //20ms
  const int64_t PRINT_LOG_INTERVAL = 1000 * 1000; // 1s
  const int64_t MAX_RETRY_NUM = 50;
  ObLSTxCtxMgr *ls_tx_ctx_mgr = NULL;

  if (IS_NOT_INIT) {
    ret = OB_NOT_INIT;
  } else if (OB_UNLIKELY(!ls_id.is_valid())) {
    ret = OB_INVALID_ARGUMENT;
    TRANS_LOG(WARN, "invalid argument", K(ret), K(ls_id));
  } else if (OB_FAIL(stop_ls_(ls_id, graceful))) {
    if (OB_ENTRY_NOT_EXIST == ret || OB_PARTITION_NOT_EXIST == ret) {
      TRANS_LOG(INFO, "ls not found", KR(ret), K(ls_id));
      ret = OB_SUCCESS;
    } else {
      TRANS_LOG(WARN, "stop ls failed", KR(ret), K(ls_id));
    }
  } else {
    // there is no limit for retry times, tx ctx are required to be released
    for (int64_t retry = 0; need_retry && is_running_ && OB_SUCC(ret); ++retry) {
      need_retry = false;
      bool is_all_trans_cleaned_up = false;
      // if ls_id has been removed, OB_SUCCESS will be returned
      if (OB_FAIL(get_ls_tx_ctx_mgr(ls_id, ls_tx_ctx_mgr))) {
        need_retry = false;
      } else {
        if (OB_FAIL(wait_ls_(ls_id))) {
          if (REACH_TIME_INTERVAL(PRINT_LOG_INTERVAL)) {
            TRANS_LOG(WARN, "wait ls error",
                KR(ret), K(retry), K(ls_id), K(*ls_tx_ctx_mgr));
          }
          need_retry = (OB_EAGAIN == ret);
          if (need_retry && MAX_RETRY_NUM == retry && NULL != ls_tx_ctx_mgr) {
            // kill all trans if reach MAX_RETRY_NUM
            if (OB_FAIL(ls_tx_ctx_mgr->kill_all_tx(arg.graceful_, is_all_trans_cleaned_up))) {
            } else if (!is_all_trans_cleaned_up) {
              const bool verbose = true;
              ls_tx_ctx_mgr->print_all_tx_ctx(ObLSTxCtxMgr::MAX_HASH_ITEM_PRINT, verbose);
            } else {
              need_retry = false;
            }
          }
        } else {
          need_retry = false;
        }
        revert_ls_tx_ctx_mgr(ls_tx_ctx_mgr);
      }
      if (need_retry) {
        ret = OB_SUCCESS;
        ObTransCond::usleep(SLEEP_US); // retry after 20ms
      }
    }

    if (OB_SUCC(ret)) {
      // if ls_id has been removed, OB_SUCCESS is returned.
      if (OB_SUCC(get_ls_tx_ctx_mgr(ls_id, ls_tx_ctx_mgr))) {
        // remove ls_id transaction context from map
        ls_tx_ctx_mgr->get_ls_log_adapter()->reset();
        if (OB_FAIL(ls_tx_ctx_mgr_map_.del(ls_id, ls_tx_ctx_mgr))) {
        } else {
          ATOMIC_INC(&ls_release_cnt_);
        }
        revert_ls_tx_ctx_mgr(ls_tx_ctx_mgr);
      }
    } else if (OB_ENTRY_NOT_EXIST == ret || OB_PARTITION_NOT_EXIST == ret) {
      TRANS_LOG(INFO, "ls not found", KR(ret), K(ls_id));
      ret = OB_SUCCESS;
    } else if (OB_EAGAIN == ret) {
      TRANS_LOG(WARN, "remove ls error, but return OB_SUCCESS", KR(ret), K(ls_id));
      ret = OB_SUCCESS;
    } else {
      // do nothing
    }
  }
  TRANS_LOG(INFO, "remove ls", KR(ret), K(ls_id), K(graceful));

  return ret;
}

int ObTxCtxMgr::check_scheduler_status(share::ObLSID ls_id)
{
  int ret = OB_SUCCESS;
  ObLSTxCtxMgr *ls_tx_ctx_mgr = NULL;
  SCN min_start_scn;
  MinStartScnStatus min_status = MinStartScnStatus::UNKOWN;

  if (IS_NOT_INIT) {
    ret = OB_NOT_INIT;
  } else if (OB_UNLIKELY(!ls_id.is_valid())) {
    ret = OB_INVALID_ARGUMENT;
  } else if (OB_FAIL(get_ls_tx_ctx_mgr(ls_id, ls_tx_ctx_mgr))) {
    ret = OB_PARTITION_NOT_EXIST;
  } else {
    if (OB_FAIL(ls_tx_ctx_mgr->check_scheduler_status(min_start_scn, min_status))) {
    } else {
    }
    revert_ls_tx_ctx_mgr(ls_tx_ctx_mgr);
  }

  return ret;
}

// check ls status in trans layer
int ObTxCtxMgr::check_ls_status(const share::ObLSID &ls_id)
{
  int ret = OB_SUCCESS;
  ObLSTxCtxMgr *ls_tx_ctx_mgr = NULL;
  if (IS_NOT_INIT) {
    ret = OB_NOT_INIT;
    TRANS_LOG(WARN, "ObTxCtxMgr not inited", K(ret), K(ls_id));
  } else if (OB_UNLIKELY(!ls_id.is_valid())) {
    ret = OB_INVALID_ARGUMENT;
    TRANS_LOG(WARN, "invalid argument", K(ret), K(ls_id));
  } else if (OB_FAIL(get_ls_tx_ctx_mgr(ls_id, ls_tx_ctx_mgr))) {
    ret = OB_PARTITION_NOT_EXIST;
    TRANS_LOG(WARN, "get ls_tx_ctx_mgr failed", K(ret), K(ls_id));
  } else if(ls_tx_ctx_mgr->is_stopped()) {
    ret = OB_PARTITION_IS_BLOCKED;
    TRANS_LOG(WARN, "ls_tx_ctx_mgr is stopped", K(ret), K(ls_id));
  } else if (ls_tx_ctx_mgr->is_all_blocked()) {
    ret = OB_PARTITION_IS_BLOCKED;
    TRANS_LOG(WARN, "logstream is blocked", K(ret), K(ls_id));
  }
  if (OB_NOT_NULL(ls_tx_ctx_mgr)) {
    revert_ls_tx_ctx_mgr(ls_tx_ctx_mgr);
  }
  return ret;
}

int ObTxCtxMgr::do_all_ls_standby_cleanup(ObTimeGuard &cleanup_timeguard)
{
  int ret = OB_SUCCESS;

  if (IS_NOT_INIT) {
    ret = OB_NOT_INIT;
  } else {
    StandbyCleanUpAllLSFunctor fn(cleanup_timeguard);
    if (OB_FAIL(foreach_ls_(fn))) {
      ret = fn.get_ret();
      TRANS_LOG(WARN, "foreach_ls standby cleanup error", KR(ret));
    } else {
      // do nothing
    }
  }
  return ret;
}

int ObLSTxCtxMgr::do_standby_cleanup()
{
  int ret = OB_SUCCESS;

  if (IS_NOT_INIT) {
    ret = OB_NOT_INIT;
  } else {
    StandbyCleanUpFunctor fn;
    if (OB_FAIL(ls_tx_ctx_map_.for_each(fn))) {
      ret = fn.get_ret();
      TRANS_LOG(WARN, "for each transaction context error", KR(ret), "manager", *this);
    }
  }

  return ret;
}

OB_NOINLINE int ObLSTxCtxMgr::errsim_switch_to_followr_gracefully()
{
  int ret = OB_SUCCESS;

#ifdef ERRSIM
  ret = EN_SWITCH_TO_FOLLOWER_GRACEFULLY;
#endif

  if (OB_FAIL(ret)) {
  }

  return ret;
}

OB_NOINLINE int ObLSTxCtxMgr::errsim_submit_start_working_log()
{
  int ret = OB_SUCCESS;

#ifdef ERRSIM
  ret = EN_SUBMIT_START_WORKING_LOG;
#endif

  if (OB_FAIL(ret)) {
  }

  return ret;
}

OB_NOINLINE int ObLSTxCtxMgr::errsim_apply_start_working_log()
{
  int ret = OB_SUCCESS;

#ifdef ERRSIM
  ret = EN_APPLY_START_WORKING_LOG;
#endif

  if (OB_FAIL(ret)) {
  }

  return ret;
}

int ObLSTxCtxMgr::filter_tx_need_transfer(ObIArray<ObTabletID> &tablet_list,
                                           const share::SCN data_end_scn,
                                           ObIArray<transaction::ObTransID> &move_tx_ids)
{
  int ret = OB_SUCCESS;
  FilterTransferTxFunctor fn(tablet_list, data_end_scn, move_tx_ids);
  if (OB_FAIL(ls_tx_ctx_map_.for_each(fn))) {
    ret = fn.get_ret();
    TRANS_LOG(WARN, "for each tx ctx error", KR(ret), "manager", *this);
  }
  return ret;
}

int ObLSTxCtxMgr::transfer_out_tx_op(const ObTransferOutTxParam &param,
                                     int64_t& active_tx_count,
                                     int64_t &op_tx_count)
{
  int ret = OB_SUCCESS;
  if (OB_ISNULL(param.move_tx_ids_)) {
    TransferOutTxOpFunctor fn(param);
    if (OB_FAIL(ls_tx_ctx_map_.for_each(fn))) {
      ret = fn.get_ret();
      TRANS_LOG(WARN, "for each tx ctx error", KR(ret), "manager", *this);
    } else {
      active_tx_count = fn.get_count();
      op_tx_count = fn.get_op_tx_count();
    }
  } else {
    active_tx_count = ls_tx_ctx_map_.count();
    for (int64_t idx = 0; OB_SUCC(ret) && idx < param.move_tx_ids_->count(); idx++) {
      if (param.move_tx_ids_->at(idx).get_id() == param.except_tx_id_) {
        continue;
      }
      ObPartTransCtx *ctx = nullptr;
      ObTransCtx *tmp_ctx = nullptr;
      bool is_operated = false;
      if (OB_FAIL(ls_tx_ctx_map_.get(param.move_tx_ids_->at(idx), tmp_ctx))) {
        if (OB_ENTRY_NOT_EXIST != ret) {
          TRANS_LOG(WARN, "get tx ctx failed", KR(ret), K(param.move_tx_ids_->at(idx)));
        } else {
          ret = OB_SUCCESS;
        }
      } else if (FALSE_IT(ctx = static_cast<ObPartTransCtx*>(tmp_ctx))) {
      } else if (is_operated) {
        op_tx_count++;
      }
      if (OB_NOT_NULL(ctx)) {
        revert_tx_ctx(ctx);
      }
    }
  }
  return ret;
}

int ObLSTxCtxMgr::wait_tx_write_end(ObTimeoutCtx &timeout_ctx)
{
  int ret = OB_SUCCESS;
  int64_t active_tx_count = 0;
  int64_t abs_expired_time = INT64_MAX;
  if (timeout_ctx.get_abs_timeout() > 0) {
    abs_expired_time = timeout_ctx.get_abs_timeout();
  }
  WaitTxWriteEndFunctor fn(abs_expired_time);
  if (OB_FAIL(ls_tx_ctx_map_.for_each(fn))) {
    TRANS_LOG(WARN, "for each tx ctx error", KR(ret), "manager", *this);
    ret = fn.get_ret();
  } else {
    active_tx_count = fn.get_count();
  }
  TRANS_LOG(INFO, "wait_tx_write_end", KR(ret), K(active_tx_count));
  return ret;
}

int ObLSTxCtxMgr::collect_tx_ctx(const ObLSID dest_ls_id,
                                 const SCN log_scn,
                                 const ObIArray<ObTabletID> &tablet_list,
                                 const ObIArray<ObTransID> *move_tx_ids,
                                 int64_t &collect_count,
                                 ObIArray<ObTxCtxMoveArg> &res)
{
  int ret = OB_SUCCESS;
  if (OB_ISNULL(move_tx_ids)) {
    int64_t abs_expired_time = INT64_MAX;
    int64_t tx_count = 0;
    CollectTxCtxFunctor fn(abs_expired_time,
                           dest_ls_id,
                           log_scn,
                           tablet_list,
                           tx_count,
                           collect_count,
                           res);
    if (OB_FAIL(ls_tx_ctx_map_.for_each(fn))) {
      TRANS_LOG(WARN, "for each tx", KR(ret), "manager", *this);
      ret = fn.get_ret();
    }
  } else {
    ObSEArray<ObTransID, 1> final_move_tx_ids;
    for (int64_t idx = 0; OB_SUCC(ret) && idx < move_tx_ids->count(); idx++) {
      ObPartTransCtx *ctx = nullptr;
      ObTransCtx *tmp_ctx = nullptr;
      bool is_collected = false;
      ObTxCtxMoveArg arg;
      if (OB_FAIL(ls_tx_ctx_map_.get(move_tx_ids->at(idx), tmp_ctx))) {
        if (OB_ENTRY_NOT_EXIST != ret) {
          TRANS_LOG(WARN, "get tx ctx failed", KR(ret), K(move_tx_ids->at(idx)));
        } else {
          ret = OB_SUCCESS;
        }
      } else if (FALSE_IT(ctx = static_cast<ObPartTransCtx*>(tmp_ctx))) {
      } else if (!is_collected) {
      } else if (OB_FAIL(final_move_tx_ids.push_back(move_tx_ids->at(idx)))) {
      } else if (OB_FAIL(res.push_back(arg))) {
      } else {
        collect_count++;
      }
      if (OB_NOT_NULL(ctx)) {
        revert_tx_ctx(ctx);
      }
    }
  }
  return ret;
}

int ObLSTxCtxMgr::move_tx_op(const ObTransferMoveTxParam &move_tx_param,
                             const ObIArray<ObTxCtxMoveArg> &args)
{
  int ret = OB_SUCCESS;
  bool is_replay = move_tx_param.is_replay_;
  if (!is_replay && is_follower_()) {
    is_replay = true;
  }
  ObLSHandle ls_handle;
  // get weak read ts for check
  share::SCN weak_read_ts;
  bool need_check_wrs = true;
  //only check wrs for register and redo phase
  if (move_tx_param.op_type_ != NotifyType::REGISTER_SUCC && move_tx_param.op_type_ != NotifyType::ON_REDO) {
    need_check_wrs = false;
  } else if (OB_FAIL(share::g_mp->ls_service()->get_ls(ls_id_, ls_handle, ObLSGetMod::TRANS_MOD))) {
  } else {
    weak_read_ts = ls_handle.get_ls()->get_ls_wrs_handler()->get_ls_weak_read_ts();
    if (is_replay) {
      const SCN checkpoint_scn = ls_handle.get_ls()->get_clog_checkpoint_scn();
        // recover no this MDS operation so checkpoint is complete
        // replay from middle and incomplete when migrate happen
        if (!move_tx_param.is_incomplete_replay_) {
          ret = OB_ERR_UNEXPECTED;
          TRANS_LOG(ERROR, "move_tx_op replay unexpected", K(ret), K(ls_id_), K(move_tx_param), K(checkpoint_scn));
        } else {
          need_check_wrs = false;
        }
    }
  }

  for (int64_t idx = 0; OB_SUCC(ret) && idx < args.count(); idx++) {
    const ObTxCtxMoveArg &arg = args.at(idx);
    ObPartTransCtx *ctx = nullptr;
    ObTransCtx *tmp_ctx = nullptr, *exist_ctx = nullptr;
    bool is_exist = false;
    bool is_created = false;
    if (OB_SUCC(ls_tx_ctx_map_.get(arg.tx_id_, tmp_ctx))) {
      if (OB_ISNULL(tmp_ctx)) {
        ret = OB_ERR_UNEXPECTED;
        TRANS_LOG(WARN, "ctx is NULL", KR(ret), "ctx", OB_P(tmp_ctx));
      } else if (FALSE_IT(ctx = static_cast<ObPartTransCtx*>(tmp_ctx))) {
      } else {
        is_exist = true;
      }
    } else if (OB_ENTRY_NOT_EXIST != ret) {
      TRANS_LOG(WARN, "get tx ctx failed", KR(ret), K(arg));
    } else {
      ret = OB_SUCCESS;
    }

    // check to create
    if (OB_FAIL(ret)) {
    } else if (move_tx_param.op_type_ == NotifyType::ON_ABORT && !is_exist) {
      // a. transfer abort log now not impl STRICT_BARRIER
      // b. when on_register part failure do abort allow no this ctx
      continue;
    } else if (!is_exist) {
      ObTxCreateArg create_arg(!is_master(),
                               PartCtxSource::TRANSFER,
                               arg.tx_id_,
                               ls_id_,
                               arg.cluster_id_,
                               arg.cluster_version_,
                               arg.session_id_,
                               arg.client_sid_,
                               arg.associated_session_id_,
                               arg.scheduler_,
                               INT64_MAX, // tx expired time
                               txs_,
                               arg.xid_,
                               arg.epoch_,
                               &arg);
      if (need_check_wrs && arg.tx_state_ >= ObTxState::PREPARE && arg.prepare_version_ <= weak_read_ts) {
        ret = OB_ERR_UNEXPECTED;
        TRANS_LOG(ERROR, "move tx prepare_version less than dest_ls weak_read_ts", KR(ret), K(arg), K(weak_read_ts), K(ls_id_), K(move_tx_param));
      } else if (OB_FAIL(create_tx_ctx(create_arg, is_exist, ctx))) {
      } else if (!is_exist) {
        is_exist = true;
        is_created = true;
      }
    }
    // do move
    if (OB_FAIL(ret)) {
    } else if (!is_exist || OB_ISNULL(ctx)) {
      ret = OB_ERR_UNEXPECTED;
      TRANS_LOG(WARN, "ctx not found", KR(ret), K(is_exist), KP(ctx));
    }
    if (OB_NOT_NULL(ctx)) {
      revert_tx_ctx(ctx);
    }
    TRANS_LOG(INFO, "move_tx_op", KR(ret), K(arg.tx_id_), K(ls_id_), K(is_replay), K(is_created), K(move_tx_param.op_type_));
  }
  return ret;
}

}
}
