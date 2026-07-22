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

#include "storage/tx/ob_tx_loop_worker.h"
#include "share/rc/ob_module_provider.h"
#include "storage/tx/ob_trans_service.h"
#include "storage/tx/ob_weak_read_util.h"
#include "storage/tx_storage/ob_ls_service.h"
#include "storage/ls/ob_ls.h"

namespace oceanbase
{

using namespace share;
using namespace logservice;

namespace transaction
{

int ObTxLoopWorker::server_module_init(ObTxLoopWorker *& ka)
{
  return ka->init();
}

int ObTxLoopWorker::init()
{
  int ret = OB_SUCCESS;

  TRANS_LOG(INFO, "[Tx Loop Worker] init");

  return ret;
}

int ObTxLoopWorker::start()
{
  int ret = OB_SUCCESS;

  TRANS_LOG(INFO, "[Tx Loop Worker] start");
  if (OB_FAIL(timer_.init("TxLoopWorkerTimer", ObMemAttr("TxLoopWorker")))) {
    TRANS_LOG(WARN, "[Tx Loop Worker] init timer failed", K(ret));
  } else if (OB_FAIL(timer_.schedule(*this, LOOP_INTERVAL, true/*is_repeat*/))) {
    TRANS_LOG(WARN, "[Tx Loop Worker] schedule timer failed", K(ret));
  } else {
    stop_flag_ = false;
    // TRANS_LOG(INFO, "[Tx Loop Worker] start keep alive thread succeed", K(ret));
  }

  return ret;
}

void ObTxLoopWorker::stop()
{
  TRANS_LOG(INFO, "[Tx Loop Worker] stop");
  if (!stop_flag_) {
    timer_.stop();
    stop_flag_ = true;
  }
}

void ObTxLoopWorker::wait()
{
  TRANS_LOG(INFO, "[Tx Loop Worker] wait");
  timer_.wait();
}

void ObTxLoopWorker::destroy()
{
  TRANS_LOG(INFO, "[Tx Loop Worker] destroy");
  timer_.destroy();
  reset();
}

void ObTxLoopWorker::reset()
{
  last_tx_gc_ts_ = 0;
  last_log_cb_pool_adjust_ts_ = 0;
  last_runtime_config_refresh_ts_ = 0;
  stop_flag_ = true;
}

void ObTxLoopWorker::runTimerTask()
{
  int ret = OB_SUCCESS;
  int64_t start_time_us = 0;
  int64_t time_used = 0;
  lib::set_thread_name("TxLoopWorker");
  bool can_gc_tx = false;
  bool can_adjust_log_cb_pool =  false;

  start_time_us = ObTimeUtility::current_time();
    // tx gc, interval = 5s
    if (common::ObClockGenerator::getClock() - last_tx_gc_ts_ > TX_GC_INTERVAL) {
      TRANS_LOG(INFO, "tx gc loop thread is running");
      last_tx_gc_ts_ = common::ObClockGenerator::getClock();
      can_gc_tx = true;
    }
    
    if (common::ObClockGenerator::getClock() - last_log_cb_pool_adjust_ts_
        > TX_LOG_CB_POOL_ADJUST_INTERVAL) {
      TRANS_LOG(INFO, "try to adjust log cb pool");
      last_log_cb_pool_adjust_ts_ = common::ObClockGenerator::getClock();
      can_adjust_log_cb_pool = true;
    }

    // Refresh transaction runtime configuration.
    if (common::ObClockGenerator::getClock() -
          last_runtime_config_refresh_ts_ > LOOP_INTERVAL /*5s*/) {
      refresh_runtime_config_();
      last_runtime_config_refresh_ts_ = common::ObClockGenerator::getClock();
    }

  (void)maintain_tx_state_(can_gc_tx, can_adjust_log_cb_pool);

    // TODO shanyan.g
    // 1) We use max(max_commit_ts, gts_cache) as read snapshot,
    //    but now we adopt updating max_commit_ts periodly to avoid getting gts cache cost
    // 2) Some time later, we will revert current modification when performance problem solved;
  update_max_commit_ts_();

  time_used = ObTimeUtility::current_time() - start_time_us;
  UNUSED(time_used);
  can_gc_tx = false;
  can_adjust_log_cb_pool = false;
}

int ObTxLoopWorker::maintain_tx_state_(bool can_tx_gc,
                                      bool can_adjust_log_cb_pool)
{
  int ret = OB_SUCCESS;
  int tmp_ret = OB_SUCCESS;

  ObLS *tenant_ls = nullptr;
  ObLS *cur_ls_ptr = nullptr;

  if (OB_ISNULL(share::g_mp->ls_service())) {
    ret = OB_INVALID_ARGUMENT;
    TRANS_LOG(WARN, "[Tx Loop Worker] ls service is null", K(ret), KP(share::g_mp->ls_service()));
  } else if (OB_FAIL(share::g_mp->ls_service()->get_ls(tenant_ls))) {
    TRANS_LOG(WARN, "[Tx Loop Worker] get transaction storage failed", K(ret), KP(share::g_mp->ls_service()));
  } else {
    cur_ls_ptr = tenant_ls;
    SCN min_start_scn = SCN::invalid_scn();
    SCN max_decided_scn = SCN::invalid_scn();
    MinStartScnStatus status = MinStartScnStatus::UNKOWN;
    // tx gc, interval = 15s
    if (can_tx_gc) {
      // TODO shanyan.g close ctx gc temporarily because of logical bug
      //

      // ATTENTION : get_max_decided_scn must before iterating all trans ctx.
      // set max_decided_scn as default value
      if (OB_TMP_FAIL(cur_ls_ptr->get_log_handler()->get_max_decided_scn(max_decided_scn))) {
        TRANS_LOG(WARN, "get max decided scn failed", KR(tmp_ret), K(min_start_scn));
        max_decided_scn.set_invalid();
      } else {
        (void)cur_ls_ptr->update_min_start_scn_info(max_decided_scn);
      }
      min_start_scn = max_decided_scn;
      do_tx_gc_(cur_ls_ptr, min_start_scn, status);
    }

    if (MinStartScnStatus::UNKOWN == status) {
      min_start_scn.reset();
    } else if (MinStartScnStatus::NO_CTX == status) {
      min_start_scn.set_min();
    }

    // keep alive, interval = 5s
    do_keep_alive_(cur_ls_ptr, min_start_scn, status);

    // Drive the local weak-read timestamp here. The tenant WRS service that
    // used to do this has been deleted.
    do_update_ls_weak_read_ts_(cur_ls_ptr);

    // ignore ret
    (void)cur_ls_ptr->get_tx_svr()->check_all_readonly_tx_clean_up();

    if (can_adjust_log_cb_pool) {
      do_log_cb_pool_adjust_(cur_ls_ptr);
    }
  }

  return ret;
}

void ObTxLoopWorker::do_keep_alive_(ObLS *ls_ptr, const SCN &min_start_scn, MinStartScnStatus status)
{
  int ret = OB_SUCCESS;

  if (OB_FAIL(ls_ptr->get_keep_alive_ls_handler()->try_submit_log(min_start_scn, status))) {
    TRANS_LOG(WARN, "[Tx Loop Worker] try submit keep alive log failed", K(ret));
  } else if (REACH_TIME_INTERVAL(KEEP_ALIVE_PRINT_INFO_INTERVAL)) {
    ls_ptr->get_keep_alive_ls_handler()->print_stat_info();
  } else {
    // do nothing
  }

  UNUSED(ret);
}

void ObTxLoopWorker::do_update_ls_weak_read_ts_(ObLS *ls_ptr)
{
  int ret = OB_SUCCESS;
  SCN version;
  bool need_skip = false;
  const int64_t max_stale_time = ObWeakReadUtil::max_stale_time_for_weak_consistency();
  if (OB_FAIL(ls_ptr->get_ls_wrs_handler()->generate_ls_weak_read_snapshot_version(
                 *ls_ptr, need_skip, version, max_stale_time))) {
    if (REACH_TIME_INTERVAL(5 * 1000 * 1000)) {
      TRANS_LOG(WARN, "[Tx Loop Worker] generate ls weak read snapshot version fail", K(ret));
    }
  }
  UNUSED(ret);
}

void ObTxLoopWorker::do_tx_gc_(ObLS *ls_ptr, SCN &min_start_scn, MinStartScnStatus &status)
{
  int ret = OB_SUCCESS;

  if (OB_FAIL(ls_ptr->get_tx_svr()->check_tx_status(min_start_scn, status))) {
    TRANS_LOG(WARN, "[Tx Loop Worker] check transaction status failed", K(ret), K(*ls_ptr));
  } else {
    TRANS_LOG(INFO, "[Tx Loop Worker] check transaction status success", K(*ls_ptr));
  }

  UNUSED(ret);
}

void ObTxLoopWorker::refresh_runtime_config_()
{
  int ret = OB_SUCCESS;
  ObTransService *txs = NULL;
  if (OB_ISNULL(txs = share::g_mp->trans_service())) {
    ret = OB_ERR_UNEXPECTED;
    TRANS_LOG(ERROR, "unexpected transaction service", K(ret), KP(txs));
  } else {
    txs->get_tx_elr_util().check_and_update_tx_elr_info();
  }
}

void ObTxLoopWorker::update_max_commit_ts_()
{
  int ret = OB_SUCCESS;
  SCN snapshot;
  const int64_t expire_ts = ObClockGenerator::getClock() + 1000000; // 1s
  ObTransService *txs = NULL;

  do {
    int64_t n = ObClockGenerator::getClock();
    if (n >= expire_ts) {
      ret = OB_TIMEOUT;
    } else if (OB_FAIL(OB_TS_MGR.get_gts(snapshot))) {
      if (OB_EAGAIN == ret) {
        ob_usleep(500, true/*is_idle_sleep*/);
      } else {
        TRANS_LOG(WARN, "get gts fail");
      }
    } else if (OB_UNLIKELY(!snapshot.is_valid())) {
      ret = OB_ERR_UNEXPECTED;
      TRANS_LOG(WARN, "invalid snapshot from gts", K(snapshot));
    } else if (OB_ISNULL(txs = share::g_mp->trans_service())) {
      ret = OB_ERR_UNEXPECTED;
      TRANS_LOG(ERROR, "unexpected transaction service", K(ret), KP(txs));
    } else {
      txs->get_tx_version_mgr().update_max_commit_ts(snapshot, false);
    }
  } while (OB_EAGAIN == ret);
}

void ObTxLoopWorker::do_log_cb_pool_adjust_(ObLS *ls_ptr)
{
  int ret = OB_SUCCESS;
  int64_t active_tx_cnt = 0;
  (void)ls_ptr->get_tx_svr()->get_active_tx_count(active_tx_cnt);
  if (OB_FAIL(ls_ptr->get_tx_svr()->get_log_cb_pool_mgr()->adjust_log_cb_pool(active_tx_cnt))) {
    TRANS_LOG(WARN, "adjust log cb pool failed", K(ret), KPC(ls_ptr));
  }
}

}
}
