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
#include "storage/ob_storage_rpc_arg.h"
#include "share/rc/ob_module_provider.h"
#include "ob_trans_functor.h"
#include "storage/tx/ob_ts_mgr.h"
#include "storage/tx_storage/ob_ls_service.h"
#include "observer/ob_srv_network_frame.h"

namespace oceanbase
{

using namespace obcall;
using namespace common;
using namespace lib;
using namespace share;
using namespace storage;
//using namespace memtable;
using namespace sql;
using namespace observer;

namespace transaction
{
ObTransService::ObTransService()
    : is_inited_(false),
      is_running_(false),
      schema_service_(NULL),
      ts_mgr_(NULL),
      input_queue_count_(0),
      output_queue_count_(0),
#ifdef ENABLE_DEBUG_LOG
      defensive_check_mgr_(NULL),
#endif
      tx_desc_mgr_(*this),
      tx_debug_seq_(0),
      read_only_checker_()
{
  check_env_();
}

int ObTransService::mtl_init(ObTransService *&it)
{
  int ret = OB_SUCCESS;
  const ObAddr &self = GCTX.self_addr();
  share::schema::ObMultiVersionSchemaService *schema_service = GCTX.schema_service_;
  if (OB_FAIL(it->gti_source_def_.init())) {
    TRANS_LOG(ERROR, "gti source init error", KR(ret));
  } else if (OB_FAIL(it->init(self,
                              &it->gti_source_def_,
                              &OB_TS_MGR,
                              schema_service))) {
    TRANS_LOG(ERROR, "trans-service init error", KR(ret), KPC(it));
  }
  return ret;
}

int ObTransService::init(const ObAddr &self,
                         ObIGtiSource *gti_source,
                         ObTsMgr *ts_mgr,
                         share::schema::ObMultiVersionSchemaService *schema_service)
{
  int ret = OB_SUCCESS;
  set_run_wrapper(MTL_CTX());
  
  const int64_t tenant_memory_limit = lib::get_tenant_memory_limit();
  int64_t msg_task_cnt = MSG_TASK_CNT_PER_GB * (tenant_memory_limit / (1024 * 1024 * 1024));
  if (msg_task_cnt < MSG_TASK_CNT_PER_GB) {
    msg_task_cnt = MSG_TASK_CNT_PER_GB;
  }
  if (msg_task_cnt > MAX_MSG_TASK_CNT) {
    msg_task_cnt = MAX_MSG_TASK_CNT;
  }
  if (is_inited_) {
    TRANS_LOG(WARN, "ObTransService inited twice", KPC(this));
    ret = OB_INIT_TWICE;
  } else if (OB_UNLIKELY(!self.is_valid())
             || OB_ISNULL(gti_source)
             || OB_ISNULL(ts_mgr)
             || OB_ISNULL(schema_service)) {
    TRANS_LOG(WARN, "invalid argument", K(self),
              KP(gti_source), KP(ts_mgr),
              KP(schema_service));
    ret = OB_INVALID_ARGUMENT;
  } else if (OB_FAIL(timer_.init("TransTimeWheel"))) {
    TRANS_LOG(ERROR, "timer init error", KR(ret));
  } else if (OB_FAIL(ObLinkQueueThreadPool::init(1, msg_task_cnt, "TransService"))) {
    TRANS_LOG(WARN, "thread pool init error", KR(ret), K(msg_task_cnt));
  } else if (OB_FAIL(tx_desc_mgr_.init(std::bind(&ObTransService::gen_trans_id,
                                                 this, std::placeholders::_1),
                                       lib::ObMemAttr("TxDescMgr")))) {
    TRANS_LOG(WARN, "ObTxDescMgr init error", K(ret));
  } else if (OB_FAIL(tx_ctx_mgr_.init(ts_mgr, this))) {
    TRANS_LOG(WARN, "tx_ctx_mgr_ init error", KR(ret));
  } else if (OB_FAIL(tx_timestamp_waiter_.init(ts_mgr))) {
    TRANS_LOG(WARN, "tx timestamp waiter init error", KR(ret));
  } else if (OB_FAIL(read_only_checker_.init())) {
    TRANS_LOG(WARN, "read only checker init failed", K(ret));
  } else {
    self_ = self;
    
    gti_source_ = gti_source;
    schema_service_ = schema_service;
    ts_mgr_ = ts_mgr;
    is_inited_ = true;
    TRANS_LOG(INFO, "transaction service inited success", KPC(this), K(tenant_memory_limit));
  }
  if (OB_SUCC(ret)) {
#ifdef ENABLE_DEBUG_LOG
    void *p = NULL;
    if (!GCONF.enable_defensive_check()) {
      // do nothing
    } else if (NULL == (p = ob_malloc(sizeof(ObDefensiveCheckMgr),
                                      lib::ObMemAttr("ObDefenCheckMgr")))) {
      ret = OB_ALLOCATE_MEMORY_FAILED;
      TRANS_LOG(WARN, "memory alloc failed", KR(ret));
    } else {
      defensive_check_mgr_ = new(p) ObDefensiveCheckMgr();
      if (OB_FAIL(defensive_check_mgr_->init(lib::ObMemAttr("ObDefenCheckMgr")))) {
        TRANS_LOG(ERROR, "defensive check mgr init failed", K(ret), KP(defensive_check_mgr_));
        defensive_check_mgr_->destroy();
        ob_free(defensive_check_mgr_);
        defensive_check_mgr_ = NULL;
      } else {
        // do nothing
      }
    }
#endif
  } else {
    TRANS_LOG(WARN, "transaction service inited failed", K(ret), K(tenant_memory_limit));
  }
  return ret;
}

int ObTransService::start()
{
  int ret = OB_SUCCESS;

  if (IS_NOT_INIT) {
    TRANS_LOG(WARN, "ObTransService not inited");
    ret = OB_NOT_INIT;
  } else if (OB_UNLIKELY(is_running_)) {
    TRANS_LOG(WARN, "ObTransService is already running");
    ret = OB_ERR_UNEXPECTED;
  } else if (OB_FAIL(timer_.start())) {
    TRANS_LOG(WARN, "ObTransTimer start error", K(ret));
  } else if (OB_FAIL(gti_source_->start())) {
    TRANS_LOG(WARN, "ObGtiSource start error", KR(ret));
  } else if (OB_FAIL(tx_ctx_mgr_.start())) {
    TRANS_LOG(WARN, "tx_ctx_mgr_ start error", KR(ret));
  } else if (OB_FAIL(tx_timestamp_waiter_.start())) {
    TRANS_LOG(WARN, "tx timestamp waiter start error", KR(ret));
  } else if (OB_FAIL(tx_desc_mgr_.start())) {
    TRANS_LOG(WARN, "tx_desc_mgr_ start error", KR(ret));
  } else {
    is_running_ = true;

    TRANS_LOG(INFO, "transaction service start success", KPC(this));
  }

  return ret;
}

void ObTransService::stop()
{
  int ret = OB_SUCCESS;

  if (IS_NOT_INIT) {
    TRANS_LOG(WARN, "ObTransService not inited", KPC(this));
    ret = OB_NOT_INIT;
  } else if (OB_UNLIKELY(!is_running_)) {
    TRANS_LOG(WARN, "ObTransService already has stopped", KPC(this));
    ret = OB_NOT_RUNNING;
  } else if (OB_FAIL(tx_ctx_mgr_.stop())) {
    TRANS_LOG(WARN, "tx_ctx_mgr_ stop error", KR(ret));
  } else if (OB_FAIL(tx_desc_mgr_.stop())) {
    TRANS_LOG(WARN, "tx_desc_mgr stop error", KR(ret));
  } else if (OB_FAIL(timer_.stop())) {
    TRANS_LOG(WARN, "ObTransTimer stop error", K(ret));
  } else {
    tx_timestamp_waiter_.stop();
    gti_source_->stop();
    ObLinkQueueThreadPool::stop();
    is_running_ = false;
    TRANS_LOG(INFO, "transaction service stop success", KPC(this));
  }
}

int ObTransService::wait_()
{
  int ret = OB_SUCCESS;

  if (IS_NOT_INIT) {
    TRANS_LOG(WARN, "ObTransService not inited", KPC(this));
    ret = OB_NOT_INIT;
  } else if (OB_UNLIKELY(is_running_)) {
    TRANS_LOG(WARN, "ObTransService is running");
    ret = OB_ERR_UNEXPECTED;
  } else {
    ObLinkQueueThreadPool::wait();
    if (OB_FAIL(tx_ctx_mgr_.wait())) {
      TRANS_LOG(WARN, "tx_ctx_mgr_ wait error", KR(ret));
    } else if (OB_FAIL(tx_desc_mgr_.wait())) {
      TRANS_LOG(WARN, "tx_desc_mgr_ wait error", KR(ret));
    } else if (OB_FAIL(timer_.wait())) {
      TRANS_LOG(WARN, "ObTransTimer wait error", K(ret));
    } else {
      gti_source_->wait();
      TRANS_LOG(INFO, "transaction service wait success", KPC(this));
    }
  }
  return ret;
}

void ObTransService::destroy()
{
  if (is_inited_) {
    if (is_running_) {
      stop();
      wait();
    }
    timer_.destroy();
    gti_source_->destroy();
    tx_timestamp_waiter_.destroy();
    tx_ctx_mgr_.destroy();
    tx_desc_mgr_.destroy();
#ifdef ENABLE_DEBUG_LOG
    if (NULL != defensive_check_mgr_) {
      defensive_check_mgr_->destroy();
      ob_free(defensive_check_mgr_);
      defensive_check_mgr_ = NULL;
    }
#endif
    is_inited_ = false;
    TRANS_LOG(INFO, "transaction service destroyed", KPC(this));
  }
}



int ObTransService::get_trans_start_session_id(const ObTransID &tx_id, uint32_t &session_id)
{
  int ret = OB_SUCCESS;
  transaction::ObTxCtx *part_ctx = nullptr;
  ObLS *tenant_ls = nullptr;
  session_id = ObBasicSessionInfo::INVALID_SESSID;
  if (IS_NOT_INIT) {
    TRANS_LOG(WARN, "ObTransService not inited");
    ret = OB_NOT_INIT;
  } else if (OB_UNLIKELY(!is_running_)) {
    TRANS_LOG(WARN, "ObTransService is not running");
    ret = OB_NOT_RUNNING;
  } else if (OB_FAIL(share::g_mp->ls_service()->get_ls(tenant_ls))) {
    TRANS_LOG(WARN, "get transaction storage failed", K(ret), K(tx_id));
  } else if (OB_FAIL(tenant_ls->get_tx_start_session_id(tx_id, session_id))) {
    TRANS_LOG(WARN, "get ObTxCtx by tx_id failed", K(tx_id));
  }
  return ret;
}


void ObTransService::check_env_()
{
  // do nothing now
}

int ObTransService::push(LinkTask *task)
{
  ATOMIC_FAA(&input_queue_count_, 1);
  return ObLinkQueueThreadPool::push(task);
}

void ObTransService::handle(LinkTask *task)
{
  int ret = OB_SUCCESS;
  ObTransTask *trans_task = NULL;
  bool need_release_task = true;
  ATOMIC_FAA(&output_queue_count_, 1);

  if (OB_ISNULL(task)) {
    // ignore ret
    TRANS_LOG(ERROR, "task is null", KP(task));
  } else {
    trans_task = static_cast<ObTransTask*>(task);
    if (!trans_task->ready_to_handle()) {
      if (OB_FAIL(push(trans_task))) {
        TRANS_LOG(WARN, "transaction service push task error", KR(ret), K(*trans_task));
        //TransRpcTaskFactory::release(static_cast<TransRpcTask*>(trans_task));
      }
    } else if (ObTransRetryTaskType::END_TRANS_CB_TASK == trans_task->get_task_type()) {
      bool has_cb = false;
      ObTxCommitCallbackTask *commit_cb_task = static_cast<ObTxCommitCallbackTask*>(task);
      int64_t need_wait_us = commit_cb_task->get_need_wait_us();
      if (need_wait_us > 0) {
        ob_usleep(need_wait_us);
      }
      if (OB_FAIL(commit_cb_task->callback(has_cb))) {
        TRANS_LOG(WARN, "end trans cb task callback error", KR(ret), KPC(commit_cb_task));
      }
      if (has_cb) {
        ObTxCommitCallbackTaskFactory::release(commit_cb_task);
      } else if (OB_FAIL(push(commit_cb_task))) {
        TRANS_LOG(WARN, "transaction service push task error", KR(ret), KPC(commit_cb_task));
      } else {
        // do nothing
      }
    } else if (ObTransRetryTaskType::ADVANCE_LS_CKPT_TASK == trans_task->get_task_type()) {
      ObAdvanceLSCkptTask *advance_ckpt_task = static_cast<ObAdvanceLSCkptTask *>(trans_task);
      if (OB_ISNULL(advance_ckpt_task)) {
        // ignore ret
        TRANS_LOG(WARN, "advance ckpt task is null", KP(advance_ckpt_task));
      } else if (OB_FAIL(advance_ckpt_task->try_advance_ls_ckpt_ts())) {
        TRANS_LOG(WARN, "advance ls ckpt ts failed", K(ret));
      }

      if (OB_NOT_NULL(advance_ckpt_task)) {
        mtl_free(advance_ckpt_task);
        advance_ckpt_task = nullptr;
      }
    } else {
      ret = OB_ERR_UNEXPECTED;
      TRANS_LOG(ERROR, "unexpected trans task type!!!", KR(ret), K(*trans_task));
    }

    // print task queue status periodically
    if (REACH_TIME_INTERVAL(10 * 1000 * 1000)) {
      int64_t queue_num = get_queue_num();
      TRANS_LOG(INFO, "[statisic] trans service task queue statisic : ", K(queue_num), K_(input_queue_count), K_(output_queue_count));
      ATOMIC_STORE(&input_queue_count_, 0);
      ATOMIC_STORE(&output_queue_count_, 0);
      TRANS_LOG(INFO, "[statisic] tx desc statisic : ",
                "alloc_count", tx_desc_mgr_.get_alloc_count(),
                "total_count", tx_desc_mgr_.get_total_count());
    }
  }
  UNUSED(ret); //make compiler happy
}

int ObTransService::get_min_uncommit_prepare_version(SCN &min_prepare_version)
{
  int ret = OB_SUCCESS;

  if (IS_NOT_INIT) {
    TRANS_LOG(WARN, "ObTransService not inited");
    ret = OB_NOT_INIT;
  } else if (OB_UNLIKELY(!is_running_)) {
    TRANS_LOG(WARN, "ObTransService is not running");
    ret = OB_NOT_RUNNING;
  } else if (OB_FAIL(tx_ctx_mgr_.get_min_uncommit_tx_prepare_version(min_prepare_version))) {
    TRANS_LOG(WARN, "ObTxCtxMgr set memstore version error", KR(ret));
  } else if (!min_prepare_version.is_valid()) {
    TRANS_LOG(ERROR, "invalid min prepare version, unexpected error", K(min_prepare_version));
    ret = OB_ERR_UNEXPECTED;
  } else {
    TRANS_LOG(DEBUG, "get min uncommit prepare version success", K(min_prepare_version));
  }
  return ret;
}


int ObTransService::remove_callback_for_uncommited_txn(const memtable::ObMemtableSet *memtable_set)
{
  int ret = OB_SUCCESS;

  if (IS_NOT_INIT) {
    TRANS_LOG(WARN, "ObTransService not inited");
    ret = OB_NOT_INIT;
  } else if (OB_UNLIKELY(!is_running_)) {
    TRANS_LOG(WARN, "ObTransService is not running");
  } else if (OB_ISNULL(memtable_set)) {
    TRANS_LOG(WARN, "memtable is NULL");
    ret = OB_INVALID_ARGUMENT;
  } else if (OB_FAIL(tx_ctx_mgr_.remove_callback_for_uncommited_tx(memtable_set))) {
    TRANS_LOG(WARN, "participant remove callback for uncommitt txn failed", KR(ret), KP(memtable_set));
  } else {
    TRANS_LOG(DEBUG, "participant remove callback for uncommitt txn success", KP(memtable_set));
  }

  return ret;
}

/**
 * get snapshot_version for stmt
 *
 * NOTE: this function only handle *strong-consistency* read
 * @pkey : not NULL if this is a single local partition stmt,
 *         will try local publish version
 */

/*
 * get snapshot for CURRENT_READ consistency
 * @snapshot_epoch: for pkey != NULL, need record snapshot's epoch
 *                  and validate at get_store_ctx
 */



// only for ob_admin use

int ObTransService::register_mds_into_tx(ObTxDesc &tx_desc,
                                         const ObTxDataSourceType &type,
                                         const char *buf,
                                         const int64_t buf_len,
                                         const ObRegisterMdsFlag &register_flag,
                                         transaction::ObTxSEQ seq_no)
{
  int ret = OB_SUCCESS;
  int tmp_ret = OB_SUCCESS;
  ObTxExecResult tx_result;
  ObTxParam tx_param;
  tx_param.access_mode_ = tx_desc.access_mode_;
  tx_param.isolation_ = tx_desc.isolation_;
  tx_param.timeout_us_ = tx_desc.timeout_us_;
  ObTxSEQ savepoint;

  if (OB_UNLIKELY(!tx_desc.is_valid() || type <= ObTxDataSourceType::UNKNOWN
                  || type >= ObTxDataSourceType::MAX_TYPE || OB_ISNULL(buf) || buf_len <= 0)) {
    ret = OB_INVALID_ARGUMENT;
    TRANS_LOG(WARN, "invalid argument", KR(ret), K(tx_desc), K(type), KP(buf), K(buf_len));
  } else if (!tx_desc.is_tx_active()) {
    ret = OB_TRANS_IS_EXITING;
    TRANS_LOG(WARN, "txn must in active for register", K(ret));
  } else if (OB_FAIL(create_implicit_savepoint(tx_desc, tx_param, savepoint))) {
    TRANS_LOG(WARN, "create implicit savepoint failed", K(ret), K(tx_desc));
  } else if (!seq_no.is_valid()) {
    seq_no = tx_desc.inc_and_get_tx_seq(0);
  }

  if (OB_SUCC(ret)) {
    do {
      ret = register_mds_into_ctx_(tx_desc, type, buf, buf_len, seq_no, register_flag);
      if (OB_EAGAIN == ret && ObTimeUtil::current_time() >= tx_desc.expire_ts_) {
        ret = OB_TIMEOUT;
        TRANS_LOG(WARN, "register tx data timeout", KR(ret), K(tx_desc), K(type));
      } else if (OB_EAGAIN == ret) {
        ob_usleep(1000);
      }
    } while (OB_EAGAIN == ret);

    if (OB_TMP_FAIL(collect_tx_exec_result(tx_desc, tx_result))) {
      TRANS_LOG(WARN, "collect tx exec result failed", K(tmp_ret), K(tx_desc));
    }
    if (OB_SUCC(ret) && OB_SUCCESS != tmp_ret) {
      ret = tmp_ret;
    } else if (OB_SUCC(ret) && OB_FAIL(add_tx_exec_result(tx_desc, tx_result))) {
      TRANS_LOG(WARN, "add tx exec result failed", K(ret), K(tx_desc), K(tx_result));
    }
    if (OB_FAIL(ret)) {
      tmp_ret = rollback_to_implicit_savepoint(tx_desc, savepoint, tx_desc.expire_ts_, true);
      if (OB_SUCCESS != tmp_ret) {
        TRANS_LOG(WARN, "rollback to savepoint fail", K(tmp_ret), K(savepoint), K(tx_desc));
      }
    }
  }

  TRANS_LOG(INFO, "register multi data source result", KR(ret), K(tx_desc), K(type), K(seq_no));
  return ret;
}

int ObTransService::register_mds_into_ctx_(ObTxDesc &tx_desc,
                                           const ObTxDataSourceType &type,
                                           const char *buf,
                                           const int64_t buf_len,
                                           const transaction::ObTxSEQ seq_no,
                                           const ObRegisterMdsFlag &register_flag)
{
  int ret = OB_SUCCESS;
  ObLS *tenant_ls = nullptr;
  ObStoreCtx store_ctx;
  ObTxReadSnapshot snapshot;
  snapshot.init_none_read();
  concurrent_control::ObWriteFlag write_flag;
  write_flag.set_is_mds();
  if (OB_UNLIKELY(!tx_desc.is_valid() || OB_ISNULL(buf) || buf_len <= 0)) {
    ret = OB_INVALID_ARGUMENT;
    TRANS_LOG(WARN, "invalid argument", KR(ret), K(tx_desc), KP(buf), K(buf_len));
  } else if (OB_FAIL(share::g_mp->ls_service()->get_ls(tenant_ls))) {
    TRANS_LOG(WARN, "get ls fail", K(ret));
  } else {
    store_ctx.ls_ = tenant_ls;
    if (OB_FAIL(get_write_store_ctx(
            tx_desc, snapshot, write_flag, store_ctx, ObTxSEQ::INVL(), true))) {
      TRANS_LOG(WARN, "get store ctx failed", KR(ret), K(tx_desc));
    } else {
      ObTxCtx *ctx = store_ctx.mvcc_acc_ctx_.tx_ctx_;
      ObMdsThrottleGuard mds_throttle_guard(
          false /* for_replay */, ctx->get_trans_expired_time());
      if (OB_FAIL(ctx->register_multi_data_source(type,
                                                  buf,
                                                  buf_len,
                                                  false /*try lock*/,
                                                  seq_no,
                                                  register_flag))) {
        TRANS_LOG(WARN, "register multi source data failed", KR(ret), K(tx_desc), K(type), K(register_flag));
      } else if (ObTxDataSourceType::DDL_TRANS == type) {
        // Change Stream Fetcher filters by has_async_index; DDL logs must carry it.
        // MDS is registered before commit, and commit block writes MDS before redo.
        ctx->set_has_async_index_redo();
      }
      int tmp_ret = OB_SUCCESS;
      if (OB_SUCCESS != (tmp_ret = revert_store_ctx(store_ctx))) {
        TRANS_LOG(WARN, "revert store ctx failed", KR(tmp_ret), K(tx_desc), K(type));
      } else {
        store_ctx.reset();
      }
    }
  }
  TRANS_LOG(DEBUG, "register multi source data", KR(ret), K(tx_desc), K(type));
  return ret;
}

int ObTransService::get_max_commit_version(SCN &commit_version) const
{
 int ret = OB_SUCCESS;
  if (IS_NOT_INIT) {
    TRANS_LOG(WARN, "ObTransService not inited");
    ret = OB_NOT_INIT;
  } else if (OB_UNLIKELY(!is_running_)) {
    TRANS_LOG(WARN, "ObTransService is not running");
    ret = OB_NOT_RUNNING;
  } else {
    commit_version = tx_version_mgr_.get_max_commit_ts(false);
    TRANS_LOG(DEBUG, "get publish version success", K(commit_version));
  }
  return ret;
}
} // transaction
} // oceanbase
