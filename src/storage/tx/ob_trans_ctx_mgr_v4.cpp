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
#include "ob_trans_service.h"
#include "share/ob_server_struct.h"
#include "ob_trans_functor.h"
#include "storage/ls/ob_ls_tx_service.h"

#define USING_LOG_PREFIX TRANS

namespace oceanbase
{

using namespace common;
using namespace share;
using namespace common::hash;
using namespace storage;
using namespace memtable;

namespace transaction
{

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
    OB_LOG(WARN, "ObLSTxCtxIterator is already ready");
    ret = OB_ERR_UNEXPECTED;
  } else {
    is_ready_ = true;
    ls_tx_ctx_mgr_ = ls_tx_ctx_mgr;
    tx_id_iter_.set_ready();
  }
  return ret;
}

int ObLSTxCtxIterator::get_next_tx_ctx(ObTxCtx *&tx_ctx)
{

  int ret = OB_SUCCESS;
  ObTransID tx_id;

  bool try_next_loop = false;
  do {
    try_next_loop = false;

    if (!is_ready()) {
      TRANS_LOG(ERROR, "ObLSTxCtxIterator is not ready");
      ret = OB_NOT_INIT;
    } else if (NULL == ls_tx_ctx_mgr_) {
      TRANS_LOG(ERROR, "ls_tx_ctx_mgr_ is null");
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

int ObLSTxCtxIterator::revert_tx_ctx(ObTxCtx *tx_ctx)
{
  int ret = OB_SUCCESS;

  if (!is_ready()) {
    TRANS_LOG(ERROR, "ObLSTxCtxIterator is not ready");
    ret = OB_NOT_INIT;
  } else if (NULL == ls_tx_ctx_mgr_) {
    TRANS_LOG(ERROR, "ls_tx_ctx_mgr_ is null");
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

OB_WEAK_SYMBOL int ObLSTxCtxMgr::init(ObTxTable *tx_table,
                       ObLockTable *lock_table,
                       ObTsMgr *ts_mgr,
                       ObTransService *txs,
                       ObITxLogParam *param,
                       ObITxLogAdapter *log_adapter)
{
  int ret = OB_SUCCESS;

  if (IS_INIT) {
    TRANS_LOG(WARN, "ObLSTxCtxMgr inited twice");
    ret = OB_INIT_TWICE;
  } else if (OB_ISNULL(ts_mgr) || OB_ISNULL(txs)) {
    TRANS_LOG(WARN, "invalid argument", KP(ts_mgr), KP(txs));
    ret = OB_INVALID_ARGUMENT;
  } else if (OB_FAIL(ls_tx_ctx_map_.init(lib::ObMemAttr("LSTxCtxMgr")))) {
  } else if (OB_ISNULL(log_adapter) && OB_FAIL(log_adapter_def_.init(param, tx_table))) {
    TRANS_LOG(WARN, "tx log adapter init error", KR(ret));
  } else if (OB_NOT_NULL(log_adapter) && OB_FALSE_IT(tx_log_adapter_ = log_adapter)) {
    ret = OB_ERR_UNEXPECTED;
  } else {
    is_inited_ = true;
    stopped_ = false;
    block_tx_ = false;
    block_normal_tx_ = false;
    block_all_ = false;
    tx_table_ = tx_table;
    lock_table_ = lock_table;
    txs_ = txs;
    ts_mgr_ = ts_mgr;
    aggre_rec_scn_.reset();
    prev_aggre_rec_scn_.reset();
    online_ts_ = 0;
    TRANS_LOG(INFO, "ObLSTxCtxMgr inited success", KP(this));
  }
  return ret;
}

void ObLSTxCtxMgr::destroy()
{
  WLockGuardWithRetryInterval guard(rwlock_, TRY_THRESOLD_US, RETRY_INTERVAL_US);
  if (IS_INIT) {
    is_inited_ = false;
    TRANS_LOG(INFO, "ObLSTxCtxMgr destroyed", KP(this));
  }
}

void ObLSTxCtxMgr::reset()
{
  is_inited_ = false;
  
  tx_table_ = NULL;
  lock_table_ = NULL;
  total_tx_ctx_count_ = 0;
  active_tx_count_ = 0;
  total_active_readonly_request_count_ = 0;
  stopped_ = true;
  block_tx_ = false;
  block_normal_tx_ = false;
  block_all_ = false;
  aggre_rec_scn_.reset();
  prev_aggre_rec_scn_.reset();
  online_ts_ = 0;
  txs_ = NULL;
  ts_mgr_ = NULL;

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
                                ObTxCtx *&ctx)
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
                                 ObTxCtx *&ctx)
{
  int ret = OB_SUCCESS;
  ObTransCtx *tmp_ctx = NULL, *exist_ctx = NULL;
  bool insert_succ = false;

  bool block  = false;
  if (is_tx_blocked_()) {
    block = true;
  } else if (is_normal_blocked_()) {
    if (arg.ctx_source_ != TxCtxSource::REGISTER_MDS) {
      block = true;
    }
  }

  exist = false;
  if (IS_NOT_INIT) {
    TRANS_LOG(WARN, "ObLSTxCtxMgr not inited");
    ret = OB_NOT_INIT;
  } else if (OB_UNLIKELY(!arg.is_valid())) {
    TRANS_LOG(WARN, "invalid argument", K(arg), KP(ts_mgr_));
    ret = OB_INVALID_ARGUMENT;
  } else if (!arg.for_replay_ && block) {
    TRANS_LOG(WARN, "ObLSTxCtxMgr is blocked", K(arg));
    ret = OB_PARTITION_IS_BLOCKED;
  } else if (is_stopped_()) {
    TRANS_LOG(WARN, "ObLSTxCtxMgr is stopped", K(arg));
    ret = OB_PARTITION_IS_STOPPED;
  } else if (OB_ISNULL(tmp_ctx = ObTxCtxFactory::alloc())) {
    TRANS_LOG(WARN, "alloc transaction context error", K(arg));
    ret = OB_ALLOCATE_MEMORY_FAILED;
  } else {
    CtxLockGuard ctx_lock_guard;
    ObTxCtx *tmp = static_cast<ObTxCtx *>(tmp_ctx);
    if (OB_FAIL(tmp->init(arg.session_id_,
                          arg.tx_id_,
                          arg.trans_expired_time_,
                          arg.trans_service_,
                          this,
                          arg.for_replay_,
                          arg.ctx_source_))) {
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
      ObTxCtxFactory::release(tmp_ctx);
    }
  }
  // if exist, wait ctx create done
  if (exist) {
    ret = OB_SUCCESS;
    CtxLockGuard ctx_guard;
    exist_ctx->get_ctx_guard(ctx_guard);
    ctx = static_cast<ObTxCtx *>(exist_ctx);
  }
  if (REACH_TIME_INTERVAL(OB_TRANS_STATISTICS_INTERVAL)) {
    TRANS_LOG(INFO, "transaction statistics", "total_count", get_tx_ctx_count_());
  }
  return ret;
}

int ObLSTxCtxMgr::get_tx_ctx(const ObTransID &tx_id, const bool for_replay, ObTxCtx *&ctx)
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
                                          ObTxCtx *&tx_ctx,
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

int ObLSTxCtxMgr::get_tx_ctx_(const ObTransID &tx_id, const bool for_replay, ObTxCtx *&ctx)
{
  int ret = OB_SUCCESS;
  ObTransCtx *tmp_ctx = NULL;
  const int64_t MAX_LOOP_COUNT = 100;
  int64_t count = 0;
  int64_t gts = 0;

  if (IS_NOT_INIT) {
    TRANS_LOG(WARN, "ObLSTxCtxMgr not inited");
    ret = OB_NOT_INIT;
  } else if (OB_UNLIKELY(!tx_id.is_valid()) || OB_ISNULL(ts_mgr_)) {
    TRANS_LOG(WARN, "invalid argument", K(tx_id), KP(ts_mgr_));
    ret = OB_INVALID_ARGUMENT;
  } else {
    if (OB_SUCC(ls_tx_ctx_map_.get(tx_id, tmp_ctx))) {
      if (OB_ISNULL(tmp_ctx)) {
        TRANS_LOG(WARN, "ctx is NULL", "ctx", OB_P(tmp_ctx));
        ret = OB_ERR_UNEXPECTED;
      } else {
        // for trans with is_exiting, we consider the thans has been ended.
        // if (tmp_ctx->is_exiting()) {
        //   ret = OB_TRANS_CTX_NOT_EXIST;
        //   ls_tx_ctx_map_.revert(tmp_ctx);
        //   tmp_ctx = NULL;
        // }
        ctx = static_cast<transaction::ObTxCtx*>(tmp_ctx);
      }
    } else if (OB_ENTRY_NOT_EXIST == ret) {
      ret = OB_TRANS_CTX_NOT_EXIST;
    } else {
      TRANS_LOG(ERROR, "get transaction context error", KR(ret), K(tx_id));
    }
    if (REACH_TIME_INTERVAL(OB_TRANS_STATISTICS_INTERVAL)) {
      TRANS_LOG(INFO, "transaction statistics", "total_tx_ctx_count", get_tx_ctx_count_());
    }
#ifdef ENABLE_DEBUG_LOG
    // ENABLE_DEBUG_LOG macro only defined in inner test environment
    if (REACH_TIME_INTERVAL(3 * 60 * 1000 * 1000 /*3 min*/)) {
      TRANS_LOG(INFO, "transaction statistics", "total_tx_ctx_count", get_tx_ctx_count_(), K(lbt()));
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


int ObLSTxCtxMgr::get_tx_ctx_directly_from_hash_map(const ObTransID &tx_id, ObTxCtx *&ctx)
{
  int ret = OB_SUCCESS;
  ObTransCtx *tmp_ctx = NULL;

  if (IS_NOT_INIT) {
    TRANS_LOG(WARN, "ObLSTxCtxMgr not inited");
    ret = OB_NOT_INIT;
  } else if (OB_UNLIKELY(!tx_id.is_valid())) {
    TRANS_LOG(WARN, "invalid argument", K(tx_id));
    ret = OB_INVALID_ARGUMENT;
  } else {
    if (OB_FAIL(ls_tx_ctx_map_.get(tx_id, tmp_ctx))) {
      if (OB_ENTRY_NOT_EXIST == ret) {
        ret = OB_TRANS_CTX_NOT_EXIST;
      } else {
        TRANS_LOG(ERROR, "get transaction context error", KR(ret), K(tx_id));
      }
    } else {
      ctx = static_cast<transaction::ObTxCtx*>(tmp_ctx);
    }
  }
  return ret;
}

int ObLSTxCtxMgr::remove_callback_for_uncommited_tx(const memtable::ObMemtableSet *memtable_set)
{
  int ret = OB_SUCCESS;
  ObTimeGuard timeguard("remove callback for uncommited txn", 10L * 1000L);

  if (IS_NOT_INIT) {
    TRANS_LOG(WARN, "ObLSTxCtxMgr not inited");
    ret = OB_NOT_INIT;
  } else if (OB_ISNULL(memtable_set)) {
    ret = OB_INVALID_ARGUMENT;
    TRANS_LOG(WARN, "memtable is null");
  } else {
    ObRemoveCallbackFunctor fn(memtable_set);
    if (OB_FAIL(ls_tx_ctx_map_.for_each(fn))) {
    } else {
    }
  }
  return ret;
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
    } else {
      ATOMIC_STORE(&stopped_, true);
      KillTxCtxFunctor fn(arg, cb_list);
      fn.set_release_audit_mgr_lock(true);
      if (OB_FAIL(ls_tx_ctx_map_.for_each(fn))) {
      }
    }
  }
  if (timeguard.get_diff() > 3 * 1000000) {
    TRANS_LOG_RET(WARN, OB_ERR_TOO_MUCH_TIME, "stop trans use too much time", K(timeguard), "manager", *this);
  }
  process_callback_(cb_list);
  TRANS_LOG(INFO, "[LsTxCtxMgr] stop done", K(timeguard), "manager", *this);
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
    if (OB_FAIL(ls_tx_ctx_map_.for_each(fn))) {
    }
    is_all_tx_cleaned_up = (get_tx_ctx_count_() == 0);
  }
  if (timeguard.get_diff() > 3 * 1000000) {
    TRANS_LOG_RET(WARN, OB_ERR_TOO_MUCH_TIME, "kill_all_tx use too much time", K(timeguard), "manager", *this);
  }
  (void)process_callback_(cb_list);
  TRANS_LOG(INFO, "[LsTxCtxMgr] kill_all_tx done", K(timeguard), "manager", *this);
  return ret;
}

int ObLSTxCtxMgr::block_tx(bool &is_all_tx_cleaned_up)
{
  int ret = OB_SUCCESS;
  WLockGuardWithRetryInterval guard(rwlock_, TRY_THRESOLD_US, RETRY_INTERVAL_US);

  if (is_stopped_()) {
    TRANS_LOG(WARN, "ls_tx_ctx_mgr is stopped, not need block");
  } else {
    ATOMIC_STORE(&block_tx_, true);
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
    TRANS_LOG(WARN, "ls_tx_ctx_mgr is stopped, not need block");
  } else {
    ATOMIC_STORE(&block_all_, true);
    is_all_tx_cleaned_up = (get_tx_ctx_count() == 0);
  }
  TRANS_LOG(INFO, "block ls", K(ret), "manager", *this);
  return ret;
}

int ObLSTxCtxMgr::block_normal(bool &is_all_tx_cleaned_up)
{
  int ret = OB_SUCCESS;
  WLockGuardWithRetryInterval guard(rwlock_, TRY_THRESOLD_US, RETRY_INTERVAL_US);

  ATOMIC_STORE(&block_normal_tx_, true);
  is_all_tx_cleaned_up = (get_tx_ctx_count() == 0);
  TRANS_LOG(INFO, "block ls normally", K(ret), "manager", *this);
  return ret;
}

int ObLSTxCtxMgr::online()
{
  int ret = OB_SUCCESS;
  WLockGuardWithRetryInterval guard(rwlock_, TRY_THRESOLD_US, RETRY_INTERVAL_US);

  ATOMIC_STORE(&stopped_, false);
  ATOMIC_STORE(&block_tx_, false);
  ATOMIC_STORE(&block_all_, false);
  online_ts_ = ObTimeUtility::current_time();
  TRANS_LOG(INFO, "online ls", K(ret), "manager", *this);
  return ret;
}

int ObLSTxCtxMgr::unblock_normal()
{
  int ret = OB_SUCCESS;
  WLockGuardWithRetryInterval guard(rwlock_, TRY_THRESOLD_US, RETRY_INTERVAL_US);

  ATOMIC_STORE(&block_normal_tx_, false);
  TRANS_LOG(INFO, "unblock ls normally", K(ret), "manager", *this);
  return ret;
}

int ObLSTxCtxMgr::get_min_uncommit_tx_prepare_version(SCN &min_prepare_version)
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

int ObLSTxCtxMgr::check_tx_status(SCN &min_start_scn, MinStartScnStatus &status)
{
  int ret = OB_SUCCESS;
  ObTimeGuard tg("ObLSTxCtxMgr::check_tx_status", 100000);

  IterateTxCtxStatusFunctor functor;
  if (OB_FAIL(ls_tx_ctx_map_.for_each(functor))) {
  } else if (!min_start_scn.is_valid()) {
    // The default min_start_scn must be valid, or skip writting HAS_CTX/NO_CTX CLOG
    status = MinStartScnStatus::UNKOWN;
  } else {
    // use smaller one between max_decided_scn and min_start_scn of all tx ctx
    TRANS_LOG(DEBUG, "set min start scn", K(min_start_scn), K(functor.get_min_start_scn()));
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
    TRANS_LOG(WARN, "ObLSTxCtxMgr not inited");
    ret = OB_NOT_INIT;
    // There is no need to check whether it is master
    // this interface is called by leader or follower
  } else if (is_stopped_()) {
    ret = OB_STATE_NOT_MATCH;
    TRANS_LOG(WARN, "this ls has beend stopped", KPC(this));
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
    TRANS_LOG(WARN, "ObLSTxCtxMgr not inited");
    ret = OB_NOT_INIT;
    // There is no need to check whether it is master
    // this interface is called by leader or follower
  } else {
    IterateCheckTabletModifySchema fn(tablet_id, schema_version);
    const int for_each_ret = ls_tx_ctx_map_.for_each(fn);
    if (OB_SUCCESS != for_each_ret) {
    }
    // NOTE: for_each may return OB_EAGAIN if the iter break but not end.
    ret = OB_SUCCESS != fn.get_ret_code() ? fn.get_ret_code() : for_each_ret;
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
    TRANS_LOG(WARN, "ObLSTxCtxMgr not inited");
    ret = OB_NOT_INIT;
  } else {
    IterateCheckTabletModifyTimestamp fn(tablet_id, timestamp);
    const int for_each_ret = ls_tx_ctx_map_.for_each(fn);
    if (OB_SUCCESS != for_each_ret) {
    }
    // NOTE: for_each may return OB_EAGAIN if the iter break but not end.
    ret = OB_SUCCESS != fn.get_ret_code() ? fn.get_ret_code() : for_each_ret;
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
    TRANS_LOG(WARN, "ObLSTxCtxMgr not inited");
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
    TRANS_LOG(WARN, "ObLSTxCtxMgr not inited");
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
    TRANS_LOG(WARN, "ObLSTxCtxMgr not inited");
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

int ObLSTxCtxMgr::revert_tx_ctx(ObTxCtx *ctx)
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
    TRANS_LOG(WARN, "invalid argument", KP(ctx));
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
    TRANS_LOG(WARN, "ObLSTxCtxMgr not inited");
    ret = OB_NOT_INIT;
  } else if (OB_ISNULL(ctx)) {
    TRANS_LOG(WARN, "invalid argument", KP(ctx));
    ret = OB_INVALID_ARGUMENT;
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
  if (OB_FAIL(ls_tx_ctx_map_.for_each(fn))) {
    if (OB_SUCCESS != fn.get_result()) {
      // get real ret code
      ret = fn.get_result();
    }
    TRANS_LOG(ERROR, "failed to submit log", K(ret));
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
  if (OB_FAIL(ls_tx_ctx_map_.for_each(fn))) {
    if (OB_SUCCESS != fn.get_result()) {
      // get real ret code
      ret = fn.get_result();
    }
    TRANS_LOG(ERROR, "failed to submit log", K(ret));
  }

  return ret;
}

// Caution: do not lock rwlock to avoid deadlock
int ObLSTxCtxMgr::check_with_tx_data(const ObTransID& tx_id, ObITxDataCheckFunctor &fn)
{
  int ret = OB_SUCCESS;
  ObTxCtx *tx_ctx = NULL;

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
    TRANS_LOG(WARN, "ObLSTxCtxMgr not inited", K(this));
    ret = OB_NOT_INIT;
  } else if (is_stopped()) {
    TRANS_LOG(WARN, "ObLSTxCtxMgr is stopped");
    ret = OB_STATE_NOT_MATCH;
  } else {
    GetRecLogTSFunctor fn;
    if (OB_FAIL(fn.init())) {
    } else if (OB_FAIL(ls_tx_ctx_map_.for_each(fn))) {
    } else {
      SCN aggre_rec_scn = get_aggre_rec_scn_();
      rec_scn = SCN::min(fn.get_rec_log_ts(), aggre_rec_scn);
      TRANS_LOG(INFO, "succ to get rec scn", K(*this), K(aggre_rec_scn));
    }
  }

  return ret;
}

int ObLSTxCtxMgr::on_tx_ctx_table_flushed()
{
  int ret = OB_SUCCESS;

  RLockGuard guard(rwlock_);

  if (IS_NOT_INIT) {
    TRANS_LOG(WARN, "ObLSTxCtxMgr not inited", K(this));
    ret = OB_NOT_INIT;
  } else if (is_stopped()) {
    TRANS_LOG(WARN, "ObLSTxCtxMgr is stopped");
    ret = OB_STATE_NOT_MATCH;
  } else {
    OnTxCtxTableFlushedFunctor fn;
    if (OB_FAIL(fn.init())) {
    } else if (OB_FAIL(ls_tx_ctx_map_.for_each(fn))) {
    } else {
      // To mark the checkpoint is succeed, we reset the prev_aggre_rec_scn
      prev_aggre_rec_scn_.reset();
      TRANS_LOG(INFO, "succ to on tx ctx table flushed", K(*this));
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
    TRANS_LOG(WARN, "Concurrent merge may be because of previous failure", K(*this));
  }

  return ret;
}

int ObLSTxCtxMgr::update_aggre_log_ts_wo_lock(SCN rec_scn)
{
  int ret = OB_SUCCESS;

  if (rec_scn.is_valid()) {
    // we cannot lock here, because the lock order must be
    // ObLSTxCtxMgr -> ObTxCtx, otherwise we may be
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

  ObTxCtx *tx_ctx = NULL;
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
    TRANS_LOG(WARN, "ObLSTxCtxMgr not inited", K(this));
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
    TRANS_LOG(INFO, "end readonly request when ls is blocked");
  }
  dec_total_active_readonly_request_count();
  return OB_SUCCESS;
}

int ObTxCtxMgr::remove_context_manager_()
{
  int ret = OB_SUCCESS;
  if (OB_NOT_NULL(tx_ctx_mgr_)) {
    if (!tx_ctx_mgr_->is_stopped()) {
      const int check_ret = OB_PARTITION_IS_NOT_STOPPED;
      TRANS_LOG(WARN, "transaction context manager has not been stopped",
                K(check_ret), KPC(tx_ctx_mgr_));
      ret = OB_ERR_UNEXPECTED;
      TRANS_LOG(WARN, "remove transaction context manager failed", KR(ret), KPC(tx_ctx_mgr_));
    } else {
      tx_ctx_mgr_->destroy();
      TRANS_LOG(INFO, "remove transaction context manager", KP(tx_ctx_mgr_));
      release_tx_ctx_mgr_();
    }
  }

  return ret;
}

int ObTxCtxMgr::stop_context_manager_(const bool graceful)
{
  int ret = OB_SUCCESS;
  if (OB_FAIL(tx_ctx_mgr_->stop(graceful))) {
  } else {
    TRANS_LOG(INFO, "stop transaction context manager success", "ctx_count", tx_ctx_mgr_->get_tx_ctx_count());
  }
  return ret;
}

int ObTxCtxMgr::wait_context_manager_()
{
  int ret = OB_SUCCESS;
  int64_t count = 0;
  const int64_t PRINT_LOG_INTERVAL = 1000 * 1000;

  if (OB_UNLIKELY(!tx_ctx_mgr_->is_stopped())) {
    ret = OB_PARTITION_IS_NOT_STOPPED;
    TRANS_LOG(WARN, "transaction context manager has not been stopped", K(ret));
  } else if ((count = tx_ctx_mgr_->get_tx_ctx_count()) > 0) {
    if (REACH_TIME_INTERVAL(PRINT_LOG_INTERVAL)) {
      TRANS_LOG(WARN, "transaction context not empty, try again", KP(tx_ctx_mgr_), K(count));
    }
    ret = OB_EAGAIN;
  } else {
    TRANS_LOG(INFO, "wait transaction context manager success");
  }
  return ret;
}

int ObTxCtxMgr::init(ObTsMgr *ts_mgr,
                     ObTransService *txs)
{
  int ret = OB_SUCCESS;

  if (OB_UNLIKELY(is_inited_)) {
    ret = OB_INIT_TWICE;
    TRANS_LOG(WARN, "ObTxCtxMgr inited twice", K(*this));
  } else if (OB_ISNULL(ts_mgr)) {
    ret = OB_ERR_UNEXPECTED;
    TRANS_LOG(WARN, "ts mgr is null");
  } else {
    
    ts_mgr_ = ts_mgr;
    txs_ = txs;
    is_inited_ = true;
    TRANS_LOG(INFO, "ObTxCtxMgr inited success", K(*this), KP(txs));
  }

  return ret;
}

int ObTxCtxMgr::start()
{
  int ret = OB_SUCCESS;

  if (IS_NOT_INIT) {
    ret = OB_NOT_INIT;
    TRANS_LOG(WARN, "ObTxCtxMgr is not inited", K(*this));
  } else if (is_running_) {
    ret = OB_ERR_UNEXPECTED;
    TRANS_LOG(WARN, "ObTxCtxMgr is already running", K(*this));
  } else {
    is_running_ = true;
    TRANS_LOG(INFO, "ObTxCtxMgr start success", K(*this));
  }

  return ret;
}

int ObTxCtxMgr::stop()
{
  int ret = OB_SUCCESS;

  if (IS_NOT_INIT) {
    ret = OB_NOT_INIT;
    TRANS_LOG(WARN, "ObTxCtxMgr is not inited", K(*this));
  } else if (!is_running_) {
    ret = OB_NOT_RUNNING;
    TRANS_LOG(WARN, "ObTxCtxMgr already has been stopped", K(*this));
  } else if (OB_ISNULL(tx_ctx_mgr_)) {
    is_running_ = false;
  } else {
    const bool graceful = false;
    int tmp_ret = OB_SUCCESS;
    if (OB_SUCCESS != (tmp_ret = stop_context_manager_(graceful))) {
      ret = OB_ERR_UNEXPECTED;
      TRANS_LOG(WARN, "stop transaction context manager failed", KR(ret), K(tmp_ret));
    } else {
      is_running_ = false;
      TRANS_LOG(INFO, "ObTxCtxMgr stop success", K(*this));
    }
  }
  return ret;
}

int ObTxCtxMgr::print_tx_ctx_()
{
  int ret = OB_SUCCESS;
  const bool verbose = true;

  if (IS_NOT_INIT) {
    ret = OB_NOT_INIT;
    TRANS_LOG(WARN, "ObTxCtxMgr is not inited", K(*this));
  } else if (OB_NOT_NULL(tx_ctx_mgr_)) {
    tx_ctx_mgr_->print_all_tx_ctx(ObLSTxCtxMgr::MAX_HASH_ITEM_PRINT, verbose);
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
    TRANS_LOG(WARN, "ObTxCtxMgr is not inited", K(*this));
  } else if (OB_UNLIKELY(is_running_)) {
    ret = OB_ERR_UNEXPECTED;
    TRANS_LOG(WARN, "ObTxCtxMgr is running", K(*this));
  } else {
    int64_t retry = 0;
    for (; OB_SUCCESS == ret && retry < MAX_WAIT_RETRY_TIMES; ++retry) {
      {
        bool need_retry = false;

        if (OB_ISNULL(tx_ctx_mgr_)) {
          break;
        } else if (OB_FAIL(wait_context_manager_())) {
          if (OB_EAGAIN == ret) {
            // Unfinished transactions require another wait cycle.
            need_retry = true;
            ret = OB_SUCCESS;
          } else {
            const int wait_ret = ret;
            ret = OB_ERR_UNEXPECTED;
            TRANS_LOG(WARN, "wait transaction context manager failed", KR(ret), K(wait_ret));
          }
        }
        if (OB_FAIL(ret)) {
          // do nothing
        } else if (!need_retry) {
          break;
        }
      }
      ObTransCond::usleep(SLEEP_US);
    }
    if (OB_FAIL(ret) || MAX_WAIT_RETRY_TIMES == retry) {
      if (OB_TMP_FAIL(print_tx_ctx_())) {
      }
    }
  }

  return ret;
}

void ObTxCtxMgr::destroy()
{
  int tmp_ret = OB_SUCCESS;

  if (is_inited_) {
    if (OB_TMP_FAIL(remove_context_manager_())) {
      TRANS_LOG_RET(WARN, tmp_ret, "remove transaction context manager error", K(tmp_ret));
    } else {
      TRANS_LOG(INFO, "ObTxCtxMgr destroyed");
      is_inited_ = false;
    }
  }
}

void ObTxCtxMgr::reset()
{
  is_running_ = false;
  release_tx_ctx_mgr_();
  ts_mgr_ = NULL;
  txs_ = NULL;
  is_inited_ = false;
}

void ObTxCtxMgr::release_tx_ctx_mgr_()
{
  if (OB_NOT_NULL(tx_ctx_mgr_)) {
    TRANS_LOG(INFO, "transaction context manager release", K(*tx_ctx_mgr_));
    ObLSTxCtxMgrFactory::release(tx_ctx_mgr_);
    tx_ctx_mgr_ = NULL;
  }
}

int ObTxCtxMgr::get_tx_ctx(const ObTransID &tx_id,
                           const bool for_replay,
                           ObTxCtx *&ctx)
{
  int ret = OB_SUCCESS;

  if (IS_NOT_INIT) {
    TRANS_LOG(WARN, "ObTxCtxMgr not inited", K(*this));
    ret = OB_NOT_INIT;
  } else if (OB_UNLIKELY(!tx_id.is_valid())) {
    TRANS_LOG(WARN, "invalid argument", K(tx_id));
    ret = OB_INVALID_ARGUMENT;
  } else if (OB_UNLIKELY(!is_running_)) {
    TRANS_LOG(WARN, "ObTxCtxMgr is not running");
    ret = OB_NOT_RUNNING;
  } else {
    if (OB_FAIL(tx_ctx_mgr_->get_tx_ctx(tx_id, for_replay, ctx))) {
    } else if (OB_ISNULL(ctx)) {
      TRANS_LOG(WARN, "transaction context is null", K(tx_id));
      ret = OB_ERR_UNEXPECTED;
    } else {
    }
  }
  return ret;
}

int ObTxCtxMgr::create_tx_ctx(const ObTxCreateArg &arg,
                              bool& existed,
                              ObTxCtx *&ctx) {
  int ret = OB_SUCCESS;

  if (IS_NOT_INIT) {
    TRANS_LOG(WARN, "ObTxCtxMgr not inited", K(*this));
    ret = OB_NOT_INIT;
  } else if (OB_UNLIKELY(!arg.is_valid())) {
    TRANS_LOG(WARN, "invalid argument", K(arg));
    ret = OB_INVALID_ARGUMENT;
  } else if (OB_UNLIKELY(!is_running_)) {
    TRANS_LOG(WARN, "ObTxCtxMgr is not running");
    ret = OB_NOT_RUNNING;
  } else {
    if (OB_FAIL(tx_ctx_mgr_->create_tx_ctx(arg, existed, ctx))) {
    } else if (OB_ISNULL(ctx)) {
      TRANS_LOG(WARN, "transaction context is null", K(arg));
      ret = OB_ERR_UNEXPECTED;
    } else {
    }
  }
  return ret;
}

int ObTxCtxMgr::revert_tx_ctx(ObTxCtx *ctx)
{
  int ret = OB_SUCCESS;

  if (IS_NOT_INIT) {
    TRANS_LOG(WARN, "ObTxCtxMgr not inited", K(*this));
    ret = OB_NOT_INIT;
  } else if (OB_ISNULL(ctx)) {
    TRANS_LOG(WARN, "invalid argument", KP(ctx));
    ret = OB_INVALID_ARGUMENT;
  } else {
    const ObTransID tx_id = ctx->get_trans_id();
    if (OB_FAIL(tx_ctx_mgr_->revert_tx_ctx(ctx))) {
    } else {
    }
  }

  return ret;
}

int ObTxCtxMgr::block_tx(bool &is_all_tx_cleaned_up)
{
  int ret = OB_SUCCESS;

  if (IS_NOT_INIT) {
    TRANS_LOG(WARN, "ObTxCtxMgr not inited");
    ret = OB_NOT_INIT;
  } else {
    if (OB_FAIL(tx_ctx_mgr_->block_tx(is_all_tx_cleaned_up))) {
    } else {
      TRANS_LOG(INFO, "block transaction context manager success", "ctx_count", tx_ctx_mgr_->get_tx_ctx_count());
    }
  }
  return ret;
}

int ObTxCtxMgr::block_all(bool &is_all_tx_cleaned_up)
{
  int ret = OB_SUCCESS;

  if (IS_NOT_INIT) {
    TRANS_LOG(WARN, "ObTxCtxMgr not inited");
    ret = OB_NOT_INIT;
  } else {
    if (OB_FAIL(tx_ctx_mgr_->block_all(is_all_tx_cleaned_up))) {
    } else {
      TRANS_LOG(INFO, "block all on transaction context manager success", "ctx_count", tx_ctx_mgr_->get_tx_ctx_count());
    }
  }
  return ret;
}

int ObTxCtxMgr::iterate_all_observer_tx_stat(ObTxStatIterator &tx_stat_iter)
{
  int ret = OB_SUCCESS;

  if (IS_NOT_INIT) {
    TRANS_LOG(WARN, "ObTxCtxMgr not inited");
    ret = OB_NOT_INIT;
  } else if (OB_NOT_NULL(tx_ctx_mgr_)) {
    IterateObserverTxStatFunctor fn(tx_stat_iter);
    if (!fn(tx_ctx_mgr_)) {
      ret = fn.get_ret();
      if (OB_SUCC(ret)) {
        ret = OB_ERR_UNEXPECTED;
      }
      TRANS_LOG(WARN, "iterate transaction stat failed", KR(ret));
    }
  }
  return ret;
}

int ObTxCtxMgr::get_tx_ctx_mgr_stat(const ObAddr &addr,
    ObLSTxCtxMgrStat &tx_ctx_mgr_stat)
{
  int ret = OB_SUCCESS;

  if (IS_NOT_INIT) {
    TRANS_LOG(WARN, "ObTxCtxMgr not inited");
    ret = OB_NOT_INIT;
  } else if (OB_ISNULL(tx_ctx_mgr_)) {
    ret = OB_ENTRY_NOT_EXIST;
  } else if (OB_FAIL(tx_ctx_mgr_stat.init(addr,
                                         tx_ctx_mgr_->is_stopped(),
                                         tx_ctx_mgr_->is_tx_blocked(),
                                         tx_ctx_mgr_->is_normal_tx_blocked(),
                                         tx_ctx_mgr_->is_all_blocked(),
                                         tx_ctx_mgr_->get_tx_ctx_count(),
                                         reinterpret_cast<int64_t>(tx_ctx_mgr_)))) {
  }

  return ret;
}

int ObTxCtxMgr::get_min_uncommit_tx_prepare_version(SCN &min_prepare_version)
{
  int ret = OB_SUCCESS;

  if (IS_NOT_INIT) {
    TRANS_LOG(WARN, "ObTxCtxMgr not inited");
    ret = OB_NOT_INIT;
  } else {
    if (OB_FAIL(tx_ctx_mgr_->get_min_uncommit_tx_prepare_version(min_prepare_version))) {
    } else {
    }
  }

  return ret;
}

int ObTxCtxMgr::remove_callback_for_uncommited_tx(const memtable::ObMemtableSet *memtable_set)
{
  int ret = OB_SUCCESS;

  if (IS_NOT_INIT) {
    TRANS_LOG(WARN, "ObTxCtxMgr not inited");
    ret = OB_NOT_INIT;
  } else {
    if (OB_FAIL(tx_ctx_mgr_->remove_callback_for_uncommited_tx(memtable_set))) {
    } else {
    }
  }

  return ret;
}

int ObTxCtxMgr::create_context_manager(ObTxTable *tx_table,
                                       ObLockTable *lock_table,
                                       ObLSTxService &ls_tx_svr,
                                       ObITxLogParam *param,
                                       ObITxLogAdapter *log_adapter)
{
  int ret = OB_SUCCESS;
  const bool manager_existed = OB_NOT_NULL(tx_ctx_mgr_);

  if (IS_NOT_INIT) {
    TRANS_LOG(WARN, "ObTxCtxMgr not inited");
    ret = OB_NOT_INIT;
  } else if (OB_NOT_NULL(tx_ctx_mgr_)) {
    ret = OB_INIT_TWICE;
    TRANS_LOG(WARN, "transaction context manager already exists", KR(ret));
  } else if (OB_ISNULL(tx_ctx_mgr_ = ObLSTxCtxMgrFactory::alloc())) {
    TRANS_LOG(WARN, "alloc transaction context manager failed");
    ret = OB_ALLOCATE_MEMORY_FAILED;
  } else if (OB_FAIL(tx_ctx_mgr_->init(tx_table, lock_table,
                                       ts_mgr_, txs_, param, log_adapter))) {
  } else if (OB_FAIL(ls_tx_svr.init(tx_ctx_mgr_, txs_))) {
  } else {
    TRANS_LOG(INFO, "create transaction context manager success", KP(tx_ctx_mgr_));
  }
  if (OB_FAIL(ret) && !manager_existed && OB_NOT_NULL(tx_ctx_mgr_)) {
    release_tx_ctx_mgr_();
  }

  return ret;
}

int ObTxCtxMgr::remove_context_manager(const bool graceful)
{
  int ret = OB_SUCCESS;
  const KillTransArg arg(graceful, false);
  bool need_retry = true;
  const int64_t SLEEP_US = 20000; //20ms
  const int64_t PRINT_LOG_INTERVAL = 1000 * 1000; // 1s
  const int64_t MAX_RETRY_NUM = 50;

  if (IS_NOT_INIT) {
    TRANS_LOG(WARN, "ObTxCtxMgr not inited");
    ret = OB_NOT_INIT;
  } else if (OB_ISNULL(tx_ctx_mgr_)) {
    // The transaction service may stop before storage initialization completes.
  } else if (OB_FAIL(stop_context_manager_(graceful))) {
  } else {
    // Transaction contexts must drain before the log adapter is detached.
    for (int64_t retry = 0; need_retry && is_running_ && OB_SUCC(ret); ++retry) {
      need_retry = false;
      bool is_all_trans_cleaned_up = false;
      if (OB_FAIL(wait_context_manager_())) {
        if (REACH_TIME_INTERVAL(PRINT_LOG_INTERVAL)) {
          TRANS_LOG(WARN, "wait transaction context manager failed",
              KR(ret), K(retry), K(*tx_ctx_mgr_));
        }
        need_retry = (OB_EAGAIN == ret);
        if (need_retry && MAX_RETRY_NUM == retry) {
          if (OB_FAIL(tx_ctx_mgr_->kill_all_tx(arg.graceful_, is_all_trans_cleaned_up))) {
          } else if (!is_all_trans_cleaned_up) {
            const bool verbose = true;
            tx_ctx_mgr_->print_all_tx_ctx(ObLSTxCtxMgr::MAX_HASH_ITEM_PRINT, verbose);
          } else {
            need_retry = false;
          }
        }
      } else {
        need_retry = false;
      }
      if (need_retry) {
        ret = OB_SUCCESS;
        ObTransCond::usleep(SLEEP_US); // retry after 20ms
      }
    }

    if (OB_SUCC(ret)) {
      tx_ctx_mgr_->get_ls_log_adapter()->reset();
    } else if (OB_EAGAIN == ret) {
      TRANS_LOG(WARN, "remove transaction context manager timed out", KR(ret));
      ret = OB_SUCCESS;
    }
  }
  TRANS_LOG(INFO, "remove transaction context manager", KR(ret), K(graceful));

  return ret;
}

}
}
